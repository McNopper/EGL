#include "egl_common.h"

extern "C"
{

    EGLContext _eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list)
    {
        static const EGLint emptyAttrib[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyAttrib;

        if (g_localStorage.api == EGL_NONE)
        {
            g_localStorage.error = EGL_BAD_MATCH;

            return EGL_NO_CONTEXT;
        }

        EGLint requested_version[2]{1, 0};
        for (EGLint i = 0; attrib_list[i] != EGL_NONE; i += 2)
        {
            switch (attrib_list[i])
            {
            case EGL_CONTEXT_MAJOR_VERSION:
                requested_version[0] = attrib_list[i + 1];
                break;
            case EGL_CONTEXT_MINOR_VERSION:
                requested_version[1] = attrib_list[i + 1];
                break;
            }
        }

        if (g_localStorage.api == EGL_OPENGL_API)
        {
            if (requested_version[0] > g_GL_max_supported_version[0] ||
                (requested_version[0] == g_GL_max_supported_version[0] && requested_version[1] > g_GL_max_supported_version[1]))
            {
                g_localStorage.error = EGL_BAD_MATCH;
                return EGL_NO_CONTEXT;
            }
        }
        else if (g_localStorage.api == EGL_OPENGL_ES_API)
        {
            if (requested_version[0] > g_ES_max_supported_version[0] ||
                (requested_version[0] == g_ES_max_supported_version[0] && requested_version[1] > g_ES_max_supported_version[1]))
            {
                g_localStorage.error = EGL_BAD_MATCH;
                return EGL_NO_CONTEXT;
            }
        }
        else
        {
            g_localStorage.error = EGL_BAD_MATCH;
            return EGL_NO_CONTEXT;
        }

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                guard_t _{walkerDpy->mutex};

                if (!walkerDpy->initialized || walkerDpy->destroy)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;

                    return EGL_NO_CONTEXT;
                }

                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
                    {
                        EGLint target_attrib_list[CONTEXT_ATTRIB_LIST_SIZE];

                        // EGL 1.5 §3.7.1: the config has to be capable of the
                        // requested client API. EGL_CONFORMANT only advertises
                        // conformance, capability is EGL_RENDERABLE_TYPE — which is
                        // also what eglChooseConfig filters on.
                        const EGLint esBit = (requested_version[0] == 1) ? EGL_OPENGL_ES_BIT : (requested_version[0] == 2) ? EGL_OPENGL_ES2_BIT
                                                                                                                           : EGL_OPENGL_ES3_BIT;
                        if (g_localStorage.api == EGL_OPENGL_ES_API && (walkerConfig->renderableType & esBit) == 0)
                        {
                            g_localStorage.error = EGL_BAD_CONFIG;
                            return EGL_NO_CONTEXT;
                        }
                        if (g_localStorage.api == EGL_OPENGL_API && (walkerConfig->renderableType & EGL_OPENGL_BIT) == 0)
                        {
                            g_localStorage.error = EGL_BAD_CONFIG;
                            return EGL_NO_CONTEXT;
                        }
                        if (!__processAttribList(g_localStorage.api, target_attrib_list, attrib_list, &g_localStorage.error))
                        {
                            return EGL_NO_CONTEXT;
                        }

                        EGLContextImpl* sharedCtx = 0;

                        if (share_context != EGL_NO_CONTEXT)
                        {
                            // The share context must belong to the same display (EGL 1.5, 3.7.1);
                            // search only this display's contexts.
                            EGLContextImpl* sharedWalkerCtx = walkerDpy->rootCtx;

                            while (sharedWalkerCtx)
                            {
                                if (reinterpret_cast<EGLContext>(sharedWalkerCtx) == share_context)
                                {
                                    if (!sharedWalkerCtx->initialized || sharedWalkerCtx->destroy)
                                    {
                                        g_localStorage.error = EGL_BAD_CONTEXT;

                                        return EGL_NO_CONTEXT;
                                    }

                                    sharedCtx = sharedWalkerCtx;

                                    break;
                                }

                                sharedWalkerCtx = sharedWalkerCtx->next;
                            }

                            if (!sharedCtx)
                            {
                                g_localStorage.error = EGL_BAD_CONTEXT;

                                return EGL_NO_CONTEXT;
                            }
                        }

                        EGLContextImpl* newCtx = reinterpret_cast<EGLContextImpl*>(malloc(sizeof(EGLContextImpl)));

                        if (!newCtx)
                        {
                            g_localStorage.error = EGL_BAD_ALLOC;

                            return EGL_NO_CONTEXT;
                        }

                        // Move the atttibutes for later creation.
                        memcpy(newCtx->attribList, target_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

                        newCtx->initialized = EGL_TRUE;
                        newCtx->destroy     = EGL_FALSE;
                        newCtx->configId    = walkerConfig->configId;
                        newCtx->clientAPI   = g_localStorage.api;
                        newCtx->sharedCtx   = sharedCtx;
                        newCtx->rootCtxList = 0;

                        // attribList holds the processed NATIVE list, which no longer
                        // carries the EGL version tokens; keep the request itself for
                        // eglQueryContext.
                        newCtx->majorVersion = requested_version[0];
                        newCtx->minorVersion = requested_version[1];

                        newCtx->refCount = 0;

                        newCtx->next       = walkerDpy->rootCtx;
                        walkerDpy->rootCtx = newCtx;

                        g_localStorage.error = EGL_SUCCESS;

                        return reinterpret_cast<EGLContext>(newCtx);
                    }

                    walkerConfig = walkerConfig->next;
                }

                g_localStorage.error = EGL_BAD_CONFIG;

                return EGL_NO_CONTEXT;
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_NO_CONTEXT;
    }

    EGLBoolean _eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
    {
        EGLBoolean success = EGL_FALSE;
        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

            while (walkerDpy)
            {
                if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
                {
                    guard_t _{walkerDpy->mutex};

                    if (!walkerDpy->initialized || walkerDpy->destroy)
                    {
                        g_localStorage.error = EGL_NOT_INITIALIZED;

                        return EGL_FALSE;
                    }

                    EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

                    while (walkerCtx)
                    {
                        if (reinterpret_cast<EGLContext>(walkerCtx) == ctx)
                        {
                            if (!walkerCtx->initialized || walkerCtx->destroy)
                            {
                                g_localStorage.error = EGL_BAD_CONTEXT;

                                return EGL_FALSE;
                            }

                            walkerCtx->initialized = EGL_FALSE;
                            walkerCtx->destroy     = EGL_TRUE;

                            success = EGL_TRUE;
                            break;
                        }

                        walkerCtx = walkerCtx->next;
                    }

                    if (!success)
                    {
                        g_localStorage.error = EGL_BAD_CONTEXT;
                        return EGL_FALSE;
                    }
                }

                walkerDpy = walkerDpy->next;
            }
        }

        if (success)
        {
            _eglInternalCleanup();

            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    EGLContext _eglGetCurrentContext(void)
    {
        g_localStorage.error = EGL_SUCCESS;

        return g_localStorage.currentCtx;
    }

    EGLDisplay _eglGetCurrentDisplay(void)
    {
        g_localStorage.error = EGL_SUCCESS;

        // Bindings are per thread, so the answer comes straight from thread local
        // storage; walking the displays would report another thread's binding.
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT || !g_localStorage.currentDpy)
        {
            return EGL_NO_DISPLAY;
        }

        return reinterpret_cast<EGLDisplay>(g_localStorage.currentDpy);
    }

    EGLSurface _eglGetCurrentSurface(EGLint readdraw)
    {
        // EGL 1.5 §3.9.1: an unrecognized readdraw is EGL_BAD_PARAMETER regardless
        // of whether anything is current.
        if (readdraw != EGL_DRAW && readdraw != EGL_READ)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_NO_SURFACE;
        }

        if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
        {
            g_localStorage.error = EGL_SUCCESS;

            return EGL_NO_SURFACE;
        }

        g_localStorage.error = EGL_SUCCESS;

        return reinterpret_cast<EGLSurface>(readdraw == EGL_DRAW ? g_localStorage.currentDraw : g_localStorage.currentRead);
    }

    EGLBoolean _eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                guard_t _{walkerDpy->mutex};

                if (!walkerDpy->initialized || walkerDpy->destroy)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;

                    return EGL_FALSE;
                }

                EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

                while (walkerCtx)
                {
                    if (reinterpret_cast<EGLContext>(walkerCtx) == ctx)
                    {
                        if (!walkerCtx->initialized || walkerCtx->destroy)
                        {
                            g_localStorage.error = EGL_BAD_CONTEXT;

                            return EGL_FALSE;
                        }

                        switch (attribute)
                        {
                        case EGL_CONFIG_ID:
                        {
                            if (value)
                            {
                                *value = walkerCtx->configId;
                            }

                            g_localStorage.error = EGL_SUCCESS;

                            return EGL_TRUE;
                        }
                        case EGL_CONTEXT_CLIENT_TYPE:
                        {
                            if (value)
                            {
                                *value = walkerCtx->clientAPI;
                            }

                            g_localStorage.error = EGL_SUCCESS;

                            return EGL_TRUE;
                        }
                        case EGL_CONTEXT_CLIENT_VERSION:
                        {
                            if (value)
                            {
                                // attribList is the processed NATIVE list; it is
                                // terminated with 0 and never contains
                                // EGL_CONTEXT_MAJOR_VERSION, so scanning it walked
                                // off the end. The request is stored explicitly.
                                *value = walkerCtx->majorVersion;
                            }

                            g_localStorage.error = EGL_SUCCESS;

                            return EGL_TRUE;
                        }
                        case EGL_RENDER_BUFFER:
                        {
                            g_localStorage.error = EGL_SUCCESS;

                            // Bindings are per thread; only the calling thread's
                            // binding may be reported.
                            if (g_localStorage.currentCtx == walkerCtx)
                            {
                                EGLSurfaceImpl* currentSurface = g_localStorage.currentDraw ? g_localStorage.currentDraw : g_localStorage.currentRead;

                                if (currentSurface)
                                {
                                    if (currentSurface->drawToWindow)
                                    {
                                        if (value)
                                        {
                                            *value = currentSurface->doubleBuffer ? EGL_BACK_BUFFER : EGL_SINGLE_BUFFER;
                                        }

                                        return EGL_TRUE;
                                    }
                                    else if (currentSurface->drawToPixmap)
                                    {
                                        if (value)
                                        {
                                            *value = EGL_SINGLE_BUFFER;
                                        }

                                        return EGL_TRUE;
                                    }
                                    else if (currentSurface->drawToPBuffer)
                                    {
                                        if (value)
                                        {
                                            *value = EGL_BACK_BUFFER;
                                        }

                                        return EGL_TRUE;
                                    }
                                }
                            }

                            // Context not current, or no recognized surface bound.
                            // EGL 1.5 §3.7.4: return EGL_NONE and succeed.
                            if (value)
                            {
                                *value = EGL_NONE;
                            }

                            return EGL_TRUE;
                        }
                        }

                        // EGL 1.5 §3.7.4: an unrecognized attribute is EGL_BAD_ATTRIBUTE.
                        g_localStorage.error = EGL_BAD_ATTRIBUTE;

                        return EGL_FALSE;
                    }

                    walkerCtx = walkerCtx->next;
                }

                g_localStorage.error = EGL_BAD_CONTEXT;

                return EGL_FALSE;
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
    }

    EGLBoolean _eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
    {
        EGLBoolean success = EGL_FALSE;

        // The binding this thread currently holds. It is only released once the new
        // binding is in place, so a failing eglMakeCurrent leaves it untouched.
        EGLDisplayImpl* previousDpy  = g_localStorage.currentDpy;
        EGLSurfaceImpl* previousDraw = g_localStorage.currentDraw;
        EGLSurfaceImpl* previousRead = g_localStorage.currentRead;
        EGLContextImpl* previousCtx  = g_localStorage.currentCtx;

        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

            if ((ctx == EGL_NO_CONTEXT && (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE)) || (ctx != EGL_NO_CONTEXT && (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE)))
            {
                g_localStorage.error = EGL_BAD_MATCH;

                return EGL_FALSE;
            }

            while (walkerDpy)
            {
                if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
                {
                    guard_t _{walkerDpy->mutex};

                    if (!walkerDpy->initialized || walkerDpy->destroy)
                    {
                        g_localStorage.error = EGL_NOT_INITIALIZED;

                        return EGL_FALSE;
                    }

                    EGLSurfaceImpl* currentDraw = EGL_NO_SURFACE_IMPL;
                    EGLSurfaceImpl* currentRead = EGL_NO_SURFACE_IMPL;
                    EGLContextImpl* currentCtx  = EGL_NO_CONTEXT_IMPL;

                    NativeSurfaceContainer* nativeSurfaceContainer = 0;
                    NativeContextContainer* nativeContextContainer = 0;

                    EGLBoolean result = EGL_TRUE;

                    if (draw != EGL_NO_SURFACE)
                    {
                        EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

                        while (walkerSurface)
                        {
                            if (reinterpret_cast<EGLSurface>(walkerSurface) == draw)
                            {
                                if (!walkerSurface->initialized || walkerSurface->destroy)
                                {
                                    g_localStorage.error = EGL_BAD_SURFACE;

                                    return EGL_FALSE;
                                }

                                currentDraw = walkerSurface;

                                break;
                            }

                            walkerSurface = walkerSurface->next;
                        }

                        if (!currentDraw)
                        {
                            g_localStorage.error = EGL_BAD_SURFACE;

                            return EGL_FALSE;
                        }
                    }

                    if (read != EGL_NO_SURFACE)
                    {
                        EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

                        while (walkerSurface)
                        {
                            if (reinterpret_cast<EGLSurface>(walkerSurface) == read)
                            {
                                if (!walkerSurface->initialized || walkerSurface->destroy)
                                {
                                    g_localStorage.error = EGL_BAD_SURFACE;

                                    return EGL_FALSE;
                                }

                                currentRead = walkerSurface;

                                break;
                            }

                            walkerSurface = walkerSurface->next;
                        }

                        if (!currentRead)
                        {
                            g_localStorage.error = EGL_BAD_SURFACE;

                            return EGL_FALSE;
                        }
                    }

                    if (ctx != EGL_NO_CONTEXT)
                    {
                        EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

                        while (walkerCtx)
                        {
                            if (reinterpret_cast<EGLContext>(walkerCtx) == ctx)
                            {
                                if (!walkerCtx->initialized || walkerCtx->destroy)
                                {
                                    g_localStorage.error = EGL_BAD_CONTEXT;

                                    return EGL_FALSE;
                                }

                                currentCtx = walkerCtx;

                                break;
                            }

                            walkerCtx = walkerCtx->next;
                        }

                        if (!currentCtx)
                        {
                            g_localStorage.error = EGL_BAD_CONTEXT;

                            return EGL_FALSE;
                        }
                    }

                    if (currentCtx != EGL_NO_CONTEXT_IMPL)
                    {
                        // EGL 1.5 (3.7.3): the context must not already be current to
                        // another thread. A non-zero reference count that this thread
                        // does not hold means it is bound somewhere else.
                        if (currentCtx->refCount > 0 &&
                            g_localStorage.currentCtx != currentCtx)
                        {
                            g_localStorage.error = EGL_BAD_ACCESS;

                            return EGL_FALSE;
                        }
                    }

                    if (currentDraw != EGL_NO_SURFACE)
                    {
                        nativeSurfaceContainer = &currentDraw->nativeSurfaceContainer;
                    }

                    if (currentCtx != EGL_NO_CONTEXT)
                    {
                        EGLContextListImpl* ctxList = currentCtx->rootCtxList;

                        while (ctxList)
                        {
                            if (ctxList->surface == currentDraw)
                            {
                                break;
                            }

                            ctxList = ctxList->next;
                        }

                        if (!ctxList)
                        {
                            ctxList = reinterpret_cast<EGLContextListImpl*>(malloc(sizeof(EGLContextListImpl)));

                            if (!ctxList)
                            {
                                g_localStorage.error = EGL_BAD_ALLOC;

                                return EGL_FALSE;
                            }

                            // Gather shared context, if one exists.
                            EGLContextListImpl* sharedCtxList = 0;
                            if (currentCtx->sharedCtx)
                            {
                                EGLContextImpl* sharedWalkerCtx       = currentCtx->sharedCtx;
                                EGLContextImpl* beforeSharedWalkerCtx = 0;

                                while (sharedWalkerCtx)
                                {
                                    // Check, if already created.
                                    if (sharedWalkerCtx->rootCtxList)
                                    {
                                        sharedCtxList = sharedWalkerCtx->rootCtxList;

                                        break;
                                    }

                                    beforeSharedWalkerCtx = sharedWalkerCtx;
                                    sharedWalkerCtx       = sharedWalkerCtx->sharedCtx;

                                    // No created shared context found.
                                    if (!sharedWalkerCtx)
                                    {
                                        sharedCtxList = reinterpret_cast<EGLContextListImpl*>(malloc(sizeof(EGLContextListImpl)));

                                        if (!sharedCtxList)
                                        {
                                            free(ctxList);

                                            g_localStorage.error = EGL_BAD_ALLOC;

                                            return EGL_FALSE;
                                        }

                                        result = __createContext(&sharedCtxList->nativeContextContainer, walkerDpy, &currentDraw->nativeSurfaceContainer, 0, beforeSharedWalkerCtx->attribList);

                                        if (!result)
                                        {
                                            free(sharedCtxList);

                                            free(ctxList);

                                            g_localStorage.error = EGL_BAD_ALLOC;

                                            return EGL_FALSE;
                                        }

                                        sharedCtxList->surface = currentDraw;

                                        sharedCtxList->next                = beforeSharedWalkerCtx->rootCtxList;
                                        beforeSharedWalkerCtx->rootCtxList = sharedCtxList;
                                    }
                                }
                            }
                            else
                            {
                                // Use own context as shared context, if one exits.

                                sharedCtxList = currentCtx->rootCtxList;
                            }

                            result = __createContext(&ctxList->nativeContextContainer, walkerDpy, &currentDraw->nativeSurfaceContainer, sharedCtxList ? &sharedCtxList->nativeContextContainer : 0, currentCtx->attribList);

                            if (!result)
                            {
                                free(ctxList);

                                g_localStorage.error = EGL_BAD_ALLOC;

                                return EGL_FALSE;
                            }

                            ctxList->surface = currentDraw;

                            ctxList->next           = currentCtx->rootCtxList;
                            currentCtx->rootCtxList = ctxList;
                        }

                        nativeContextContainer = &ctxList->nativeContextContainer;
                    }

                    success = result = __makeCurrent(walkerDpy, nativeSurfaceContainer, nativeContextContainer);

                    if (!result)
                    {
                        g_localStorage.error = EGL_BAD_MATCH;

                        return EGL_FALSE;
                    }

                    walkerDpy->currentDraw = currentDraw;
                    walkerDpy->currentRead = currentRead;
                    walkerDpy->currentCtx  = currentCtx;

                    // Reference the new objects before releasing the old ones, so
                    // that re-binding the very same object never drops it to zero.
                    if (currentDraw != EGL_NO_SURFACE_IMPL)
                    {
                        currentDraw->refCount++;
                    }
                    if (currentRead != EGL_NO_SURFACE_IMPL && currentRead != currentDraw)
                    {
                        currentRead->refCount++;
                    }
                    if (currentCtx != EGL_NO_CONTEXT_IMPL)
                    {
                        currentCtx->refCount++;
                    }

                    g_localStorage.currentDpy  = (currentCtx != EGL_NO_CONTEXT_IMPL) ? walkerDpy : nullptr;
                    g_localStorage.currentDraw = currentDraw;
                    g_localStorage.currentRead = currentRead;
                    g_localStorage.currentCtx  = currentCtx;

                    // Previous binding on this very display: release it here, while
                    // its mutex is held. Other displays are handled below.
                    if (previousDpy == walkerDpy)
                    {
                        if (previousDraw != EGL_NO_SURFACE_IMPL)
                        {
                            previousDraw->refCount--;
                        }
                        if (previousRead != EGL_NO_SURFACE_IMPL && previousRead != previousDraw)
                        {
                            previousRead->refCount--;
                        }
                        if (previousCtx != EGL_NO_CONTEXT_IMPL)
                        {
                            previousCtx->refCount--;
                        }

                        previousDpy = 0;
                    }

                    break; // break displays loop
                }

                walkerDpy = walkerDpy->next;
            }

            // The thread may have been current on a DIFFERENT display. That display
            // must stop pointing at objects this thread no longer holds, otherwise it
            // keeps them alive forever and confuses eglGetCurrentDisplay.
            if (success && previousDpy)
            {
                guard_t _{previousDpy->mutex};

                if (previousDraw != EGL_NO_SURFACE_IMPL)
                {
                    previousDraw->refCount--;
                }
                if (previousRead != EGL_NO_SURFACE_IMPL && previousRead != previousDraw)
                {
                    previousRead->refCount--;
                }
                if (previousCtx != EGL_NO_CONTEXT_IMPL)
                {
                    previousCtx->refCount--;
                }

                if (previousDpy->currentCtx == previousCtx)
                {
                    previousDpy->currentDraw = EGL_NO_SURFACE_IMPL;
                    previousDpy->currentRead = EGL_NO_SURFACE_IMPL;
                    previousDpy->currentCtx  = EGL_NO_CONTEXT_IMPL;
                }
            }
        }

        if (success)
        {
            _eglInternalCleanup();

            g_localStorage.error = EGL_SUCCESS;
        }

        if (!success)
            g_localStorage.error = EGL_BAD_DISPLAY;

        return success;
    }

} // extern "C"

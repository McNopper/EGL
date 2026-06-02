#include "egl_common.h"

extern "C"
{

    EGLBoolean _eglBindAPI(EGLenum api)
    {
        if (api == EGL_OPENGL_API || api == EGL_OPENGL_ES_API)
        {
            g_localStorage.api = api;

            return EGL_TRUE;
        }

        g_localStorage.error = EGL_BAD_PARAMETER;

        return EGL_FALSE;
    }

    EGLenum _eglQueryAPI(void)
    {
        return g_localStorage.api;
    }

    EGLBoolean _eglWaitClient(void)
    {
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
        {
            return EGL_TRUE;
        }

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (walkerDpy->currentCtx == g_localStorage.currentCtx)
            {
                guard_t _{walkerDpy->mutex};

                if (!walkerDpy->initialized || walkerDpy->destroy)
                {
                    return EGL_FALSE;
                }

                if (walkerDpy->currentDraw && (!walkerDpy->currentDraw->initialized || walkerDpy->currentDraw->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                if (walkerDpy->currentRead && (!walkerDpy->currentRead->initialized || walkerDpy->currentRead->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                break;
            }

            walkerDpy = walkerDpy->next;
        }

        if (g_localStorage.api == EGL_OPENGL_API || g_localStorage.api == EGL_OPENGL_ES_API)
        {
            glFinish();
        }

        return EGL_TRUE;
    }

    EGLBoolean _eglWaitNative(EGLint engine)
    {
        if (engine != EGL_CORE_NATIVE_ENGINE)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

        if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
        {
            return EGL_TRUE;
        }

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (walkerDpy->currentCtx == g_localStorage.currentCtx)
            {
                guard_t _{walkerDpy->mutex};

                if (walkerDpy->currentDraw && (!walkerDpy->currentDraw->initialized || walkerDpy->currentDraw->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                if (walkerDpy->currentRead && (!walkerDpy->currentRead->initialized || walkerDpy->currentRead->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                break;
            }

            walkerDpy = walkerDpy->next;
        }

        if (g_localStorage.api == EGL_OPENGL_API || g_localStorage.api == EGL_OPENGL_ES_API)
        {
            glFinish();
        }

        return EGL_TRUE;
    }

    EGLBoolean _eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
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

                EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

                while (walkerSurface)
                {
                    if (reinterpret_cast<EGLSurface>(walkerSurface) == surface)
                    {
                        if (!walkerSurface->initialized || walkerSurface->destroy)
                        {
                            g_localStorage.error = EGL_BAD_SURFACE;

                            return EGL_FALSE;
                        }

                        return __swapBuffers(walkerDpy, walkerSurface);
                    }

                    walkerSurface = walkerSurface->next;
                }

                g_localStorage.error = EGL_BAD_SURFACE;

                return EGL_FALSE;
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
    }

    //
    // EGL_VERSION_1_1
    //

    EGLBoolean _eglSwapInterval(EGLDisplay dpy, EGLint interval)
    {
        // EGL 1.5 §3.9.3: requires a current context on the calling thread.
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL)
        {
            g_localStorage.error = EGL_BAD_CONTEXT;

            return EGL_FALSE;
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

                    return EGL_FALSE;
                }

                // Verify calling thread's context is current on this display.
                if (walkerDpy->currentCtx != g_localStorage.currentCtx)
                {
                    g_localStorage.error = EGL_BAD_SURFACE;

                    return EGL_FALSE;
                }

                if (walkerDpy->currentDraw == EGL_NO_SURFACE_IMPL || walkerDpy->currentRead == EGL_NO_SURFACE_IMPL)
                {
                    g_localStorage.error = EGL_BAD_SURFACE;

                    return EGL_FALSE;
                }

                return __swapInterval(walkerDpy, interval);
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
    }

    __eglMustCastToProperFunctionPointerType _eglGetProcAddress(const char* procname)
    {
        if (!procname)
            return nullptr;

        // EGL 1.5 allows eglGetProcAddress to return core EGL entry points, and many
        // loaders (glad, libepoxy, ...) rely on it. Resolve the exported egl* names
        // here; only names whose signature matches the core function are listed
        // (extension variants with different signatures fall through to the GL loader).
        struct EglProc
        {
            const char*                              name;
            __eglMustCastToProperFunctionPointerType fn;
        };
#define EGL_PROC(n)                                                        \
    {                                                                      \
        #n, reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&n) \
    }
        static const EglProc k_eglProcs[] = {
            // EGL 1.0
            EGL_PROC(eglChooseConfig),
            EGL_PROC(eglCopyBuffers),
            EGL_PROC(eglCreateContext),
            EGL_PROC(eglCreatePbufferSurface),
            EGL_PROC(eglCreatePixmapSurface),
            EGL_PROC(eglCreateWindowSurface),
            EGL_PROC(eglDestroyContext),
            EGL_PROC(eglDestroySurface),
            EGL_PROC(eglGetConfigAttrib),
            EGL_PROC(eglGetConfigs),
            EGL_PROC(eglGetCurrentDisplay),
            EGL_PROC(eglGetCurrentSurface),
            EGL_PROC(eglGetDisplay),
            EGL_PROC(eglGetError),
            EGL_PROC(eglGetProcAddress),
            EGL_PROC(eglInitialize),
            EGL_PROC(eglMakeCurrent),
            EGL_PROC(eglQueryContext),
            EGL_PROC(eglQueryString),
            EGL_PROC(eglQuerySurface),
            EGL_PROC(eglSwapBuffers),
            EGL_PROC(eglTerminate),
            EGL_PROC(eglWaitGL),
            EGL_PROC(eglWaitNative),
            // EGL 1.1
            EGL_PROC(eglBindTexImage),
            EGL_PROC(eglReleaseTexImage),
            EGL_PROC(eglSurfaceAttrib),
            EGL_PROC(eglSwapInterval),
            // EGL 1.2
            EGL_PROC(eglBindAPI),
            EGL_PROC(eglQueryAPI),
            EGL_PROC(eglCreatePbufferFromClientBuffer),
            EGL_PROC(eglReleaseThread),
            EGL_PROC(eglWaitClient),
            // EGL 1.4
            EGL_PROC(eglGetCurrentContext),
            // EGL 1.5
            EGL_PROC(eglCreateSync),
            EGL_PROC(eglDestroySync),
            EGL_PROC(eglClientWaitSync),
            EGL_PROC(eglGetSyncAttrib),
            EGL_PROC(eglWaitSync),
            EGL_PROC(eglCreateImage),
            EGL_PROC(eglDestroyImage),
            EGL_PROC(eglGetPlatformDisplay),
            EGL_PROC(eglCreatePlatformWindowSurface),
            EGL_PROC(eglCreatePlatformPixmapSurface),
        };
#undef EGL_PROC

        for (const auto& p : k_eglProcs)
            if (strcmp(p.name, procname) == 0)
                return p.fn;

        // Not a core EGL entry point — defer to the client (GL/GLES) loader.
        return __getProcAddress(procname);
    }

    //
    // EGL_VERSION_1_3
    //

    EGLSurface _eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint* attrib_list)
    {
        // Only EGL_OPENVG_IMAGE is a defined buftype, and OpenVG is not supported on desktop.
        (void)dpy;
        (void)buffer;
        (void)config;
        (void)attrib_list;

        if (buftype != EGL_OPENVG_IMAGE)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
        }
        else
        {
            g_localStorage.error = EGL_BAD_ACCESS;
        }

        return EGL_NO_SURFACE;
    }

} // extern "C"

#include "egl_common.h"
#include <cstring>
#include <cstdio>
#include <new>

// The extension blob eglQueryString assembles; grown only if the advertised
// extension list outgrows it.
#define EXTENSION_BUFFER_SIZE 2048

extern "C"
{

    // Walks the display list for the public handle; unknown handles get
    // EGL_BAD_DISPLAY. The caller holds the global read lock while using the
    // result - same as the hand-rolled walks this replaces.
    EGLDisplayImpl* _eglFindDisplay(EGLDisplay dpy)
    {
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                return walkerDpy;
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return nullptr;
    }

    // Returns the display currently bound to this thread, or nullptr when
    // nothing is current. No error is set: a thread without a binding is a
    // legitimate state (the Wait*/ReleaseThread entry points treat it as
    // success).
    EGLDisplayImpl* _eglFindCurrentDisplay()
    {
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (walkerDpy == g_localStorage.currentDpy)
            {
                return walkerDpy;
            }

            walkerDpy = walkerDpy->next;
        }

        return nullptr;
    }

    EGLint _eglGetError(void)
    {
        EGLint currentError = g_localStorage.error;

        g_localStorage.error = EGL_SUCCESS;

        return currentError;
    }

    EGLDisplay _eglGetDisplay(EGLNativeDisplayType display_id)
    {
        if (!_eglInternalInit())
        {
            // Report a reason: returning EGL_NO_DISPLAY with a stale EGL_SUCCESS
            // makes the failure indistinguishable from success for callers that
            // branch on eglGetError().
            g_localStorage.error = EGL_NOT_INITIALIZED;

            return EGL_NO_DISPLAY;
        }

        // EGL_DEFAULT_DISPLAY has to be resolved BEFORE the lookup, because the
        // resolved native handle is what gets stored. Searching for the
        // unresolved 0 would never match and allocate a new display every call.
        EGLNativeDisplayType resolved_id = display_id;

        if (!resolved_id)
        {
            auto dummy  = g_globalStorage.dummy_read();
            resolved_id = __getDefaultNativeDisplay(&dummy);
        }

        // Lookup and insert must happen atomically under the write lock: otherwise
        // two concurrent calls either lose one display or create two displays for
        // the same display_id.
        auto _wl = g_globalStorage.placeRootDpy_writelock();

        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (walkerDpy->display_id == resolved_id)
            {
                g_localStorage.error = EGL_SUCCESS;

                return reinterpret_cast<EGLDisplay>(walkerDpy);
            }

            walkerDpy = walkerDpy->next;
        }

        EGLDisplayImpl* newDpy = new (std::nothrow) EGLDisplayImpl();

        if (!newDpy)
        {
            g_localStorage.error = EGL_BAD_ALLOC;

            return EGL_NO_DISPLAY;
        }

        newDpy->initialized = EGL_FALSE;
        newDpy->destroy     = EGL_FALSE;
        newDpy->display_id  = resolved_id;
        newDpy->rootSurface = 0;
        newDpy->rootCtx     = 0;
        newDpy->rootConfig  = 0;
        newDpy->rootSync    = nullptr;
        newDpy->rootImage   = nullptr;
        newDpy->currentDraw = EGL_NO_SURFACE_IMPL;
        newDpy->currentRead = EGL_NO_SURFACE_IMPL;
        newDpy->currentCtx  = EGL_NO_CONTEXT_IMPL;
        newDpy->next        = g_globalStorage.rootDpy;

        g_globalStorage.rootDpy = newDpy;

        g_localStorage.error = EGL_SUCCESS;

        return newDpy;
    }

    EGLBoolean _eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_FALSE;
        }

        guard_t _{walkerDpy->mutex};

        if (walkerDpy->destroy)
        {
            // Allow re-initialization after eglTerminate (EGL 1.5 §3.2).
            walkerDpy->destroy = EGL_FALSE;
        }

        {
            // The bootstrap state is process wide, so the whole
            // read-modify-write around __initialize has to be serialized.
            guard_t _b{g_globalStorage.bootstrapMutex()};

            if (!walkerDpy->initialized)
            {
                // __initialize unconditionally installs a fresh config list,
                // so release the previous one first; otherwise a re-initialize
                // after eglTerminate orphans it.
                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    EGLConfigImpl* deleteConfig = walkerConfig;

                    walkerConfig = walkerConfig->next;

                    free(deleteConfig);
                }
                walkerDpy->rootConfig = 0;

                auto       dummy = g_globalStorage.dummy_read();
                EGLBoolean fail  = !__initialize(walkerDpy, &dummy, &g_localStorage.error);
                g_globalStorage.dummy_write(dummy);
                if (fail)
                {
                    return EGL_FALSE;
                }
            }
        }

        walkerDpy->initialized = EGL_TRUE;

        //

        if (major)
        {
            *major = 1;
        }

        if (minor)
        {
            *minor = 5;
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglTerminate(EGLDisplay dpy)
    {
        EGLBoolean success = EGL_FALSE;
        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

            if (walkerDpy)
            {
                guard_t _{walkerDpy->mutex};

                if (!walkerDpy->initialized || walkerDpy->destroy)
                {
                    // A display that was never initialized (or whose eglInitialize
                    // failed, leaving a partially built config list) still has to
                    // become collectable. Marking it for destruction lets
                    // _eglInternalCleanup unlink it below; returning from here
                    // instead made every failed get/terminate cycle leak a display
                    // node and its config list for the lifetime of the process.
                    walkerDpy->destroy = EGL_TRUE;

                    success = EGL_TRUE;
                }
                else
                {
                    // EGL 1.5 §3.2: eglTerminate marks all resources of the display
                    // for destruction. Without this the surface/context lists never
                    // empty, so the display itself is never released and its config
                    // list, native handles and the loaded GL library leak.
                    // The native drawables are torn down by _eglInternalCleanup once
                    // nothing has them current any more.
                    EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

                    while (walkerSurface)
                    {
                        walkerSurface->initialized = EGL_FALSE;
                        walkerSurface->destroy     = EGL_TRUE;

                        walkerSurface = walkerSurface->next;
                    }

                    EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

                    while (walkerCtx)
                    {
                        walkerCtx->initialized = EGL_FALSE;
                        walkerCtx->destroy     = EGL_TRUE;

                        walkerCtx = walkerCtx->next;
                    }

                    walkerDpy->initialized = EGL_FALSE;
                    walkerDpy->destroy     = EGL_TRUE;

                    success = EGL_TRUE;
                }
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

    const char* _eglQueryString(EGLDisplay dpy, EGLint name)
    {
        if (dpy == EGL_NO_DISPLAY)
        {
            if (name == EGL_EXTENSIONS)
            {
                g_localStorage.error = EGL_SUCCESS;

                return "EGL_EXT_client_extensions EGL_EXT_platform_device";
            }
            g_localStorage.error = EGL_BAD_DISPLAY;
            return nullptr;
        }

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return 0;
        }

        guard_t _{walkerDpy->mutex};

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;

            return 0;
        }

        g_localStorage.error = EGL_SUCCESS;

        switch (name)
        {
        case EGL_CLIENT_APIS:
        {
            bool glOK = (g_GL_max_supported_version[0] > 0);
            bool esOK = (g_ES_max_supported_version[0] > 0);
            if (glOK && esOK)
                return "OpenGL OpenGL_ES";
            if (glOK)
                return "OpenGL";
            if (esOK)
                return "OpenGL_ES";
            return "";
        }
        case EGL_VENDOR:
        {
            return _EGL_VENDOR;
        }
        case EGL_VERSION:
        {
            return _EGL_VERSION;
        }
        case EGL_EXTENSIONS:
        {
            static thread_local char extBuf[EXTENSION_BUFFER_SIZE];
            extBuf[0]          = '\0';
            uint32_t hdr       = walkerDpy->supportedHDRColorspaces;
            auto     appendExt = [&](const char* s)
            {
                size_t len = strlen(extBuf);
                snprintf(extBuf + len, sizeof(extBuf) - len, "%s%s", len ? " " : "", s);
            };
            appendExt("EGL_KHR_gl_colorspace");
            appendExt("EGL_KHR_create_context");
            appendExt("EGL_EXT_client_extensions");
            if (hdr & EGL_HDR_CS_SCRGB_LINEAR_BIT)
                appendExt("EGL_EXT_gl_colorspace_scrgb_linear");
            if (hdr & EGL_HDR_CS_SCRGB_BIT)
                appendExt("EGL_EXT_gl_colorspace_scrgb");
            if (hdr & EGL_HDR_CS_BT2020_PQ_BIT)
                appendExt("EGL_EXT_gl_colorspace_bt2020_pq");
            if (hdr & EGL_HDR_CS_BT2020_LINEAR_BIT)
                appendExt("EGL_EXT_gl_colorspace_bt2020_linear");
            if (hdr & EGL_HDR_CS_BT2020_HLG_BIT)
                appendExt("EGL_EXT_gl_colorspace_bt2020_hlg");
            if (hdr & EGL_HDR_CS_DISPLAY_P3_BIT)
                appendExt("EGL_EXT_gl_colorspace_display_p3");
            if (hdr & EGL_HDR_CS_DISPLAY_P3_LINEAR_BIT)
                appendExt("EGL_EXT_gl_colorspace_display_p3_linear");
            // Passthrough is a distinct colorspace, not just display_p3; it
            // has its own bit which the backends have to set to advertise it.
            if (hdr & EGL_HDR_CS_DISPLAY_P3_PASSTHROUGH_BIT)
                appendExt("EGL_EXT_gl_colorspace_display_p3_passthrough");
            if (hdr)
            {
                appendExt("EGL_EXT_surface_SMPTE2086_metadata");
                appendExt("EGL_EXT_surface_CTA861_3_metadata");
            }
            return extBuf;
        }
        default:
            break; // Unrecognized attribute; ignored.
        }

        g_localStorage.error = EGL_BAD_PARAMETER;

        return 0;
    }

    EGLDisplay _eglGetPlatformDisplay(EGLenum platform, const void* native_display, const EGLAttrib* attrib_list)
    {
        (void)attrib_list;
        EGLNativeDisplayType nativeDisplay = EGL_DEFAULT_DISPLAY;
        if (!__matchPlatformDisplay(platform, native_display, &nativeDisplay))
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_NO_DISPLAY;
        }
        return _eglGetDisplay(nativeDisplay);
    }

    EGLBoolean _eglReleaseThread(void)
    {
        EGLBoolean released = EGL_FALSE;

        if (g_localStorage.currentDpy)
        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = _eglFindCurrentDisplay();

            if (walkerDpy)
            {
                guard_t _{walkerDpy->mutex};

                __makeCurrent(walkerDpy, nullptr, nullptr);

                // Drop this thread's references so the objects may be freed.
                // _eglReleaseBindingRefs holds the draw == read single-reference
                // rule; its comment covers why the decrement is conditional.
                _eglReleaseBindingRefs(g_localStorage.currentDraw, g_localStorage.currentRead, g_localStorage.currentCtx);

                if (walkerDpy->currentCtx == g_localStorage.currentCtx)
                {
                    walkerDpy->currentDraw = EGL_NO_SURFACE_IMPL;
                    walkerDpy->currentRead = EGL_NO_SURFACE_IMPL;
                    walkerDpy->currentCtx  = EGL_NO_CONTEXT_IMPL;
                }

                released = EGL_TRUE;
            }
        }

        // Reset per-thread state to the same defaults a freshly created thread has;
        // the rendering API resets to EGL_OPENGL_ES_API per EGL 1.5 (3.7).
        g_localStorage = {EGL_SUCCESS, EGL_OPENGL_ES_API, EGL_NO_CONTEXT_IMPL, nullptr, EGL_NO_SURFACE_IMPL, EGL_NO_SURFACE_IMPL};

        if (released)
        {
            _eglInternalCleanup();
        }

        return EGL_TRUE;
    }

} // extern "C"

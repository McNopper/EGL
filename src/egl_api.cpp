// The pre-1.5 KHR entry point prototypes (EGL_KHR_fence_sync,
// EGL_KHR_wait_sync, EGL_KHR_image_base) for the proc table below.
#define EGL_EGLEXT_PROTOTYPES
#include "egl_common.h"

extern "C"
{

    EGLBoolean _eglBindAPI(EGLenum api)
    {
#ifdef EGL_ENABLE_OPENVG
        // OpenVG is only bindable when a provider implementation is registered
        // (see eglvg.h); without one EGL behaves as before.
        if (api == EGL_OPENGL_API || api == EGL_OPENGL_ES_API || (api == EGL_OPENVG_API && _eglGetVGProvider()))
#else
        if (api == EGL_OPENGL_API || api == EGL_OPENGL_ES_API)
#endif
        {
            g_localStorage.api = api;

            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }

        g_localStorage.error = EGL_BAD_PARAMETER;

        return EGL_FALSE;
    }

    EGLenum _eglQueryAPI(void)
    {
        g_localStorage.error = EGL_SUCCESS;

        return g_localStorage.api;
    }

    // Internal helper for eglWaitGL. Restores the rendering API without touching the
    // thread error, which eglBindAPI would clear and thereby hide a failure of the
    // eglWaitClient call it wraps.
    void _eglBindAPIKeepError(EGLenum api)
    {
        g_localStorage.api = api;
    }

    EGLBoolean _eglWaitClient(void)
    {
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
        {
            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }

        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = _eglFindCurrentDisplay();

            // Bindings are per thread.
            if (walkerDpy)
            {
                guard_t _{walkerDpy->mutex};

                if (!walkerDpy->initialized || walkerDpy->destroy)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;

                    return EGL_FALSE;
                }

                if (g_localStorage.currentDraw && (!g_localStorage.currentDraw->initialized || g_localStorage.currentDraw->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                if (g_localStorage.currentRead && (!g_localStorage.currentRead->initialized || g_localStorage.currentRead->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }
            }
        }

        if (g_localStorage.api == EGL_OPENGL_API || g_localStorage.api == EGL_OPENGL_ES_API)
        {
            // glFinish is a function pointer resolved at bootstrap; it may be null.
            if (glFinish_PTR)
            {
                glFinish();
            }
        }

        g_localStorage.error = EGL_SUCCESS;

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
            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }

        {
            auto            _rl       = g_globalStorage.placeRootDpy_readlock();
            EGLDisplayImpl* walkerDpy = _eglFindCurrentDisplay();

            if (walkerDpy)
            {
                guard_t _{walkerDpy->mutex};

                if (g_localStorage.currentDraw && (!g_localStorage.currentDraw->initialized || g_localStorage.currentDraw->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }

                if (g_localStorage.currentRead && (!g_localStorage.currentRead->initialized || g_localStorage.currentRead->destroy))
                {
                    g_localStorage.error = EGL_BAD_CURRENT_SURFACE;

                    return EGL_FALSE;
                }
            }
        }

        if (g_localStorage.api == EGL_OPENGL_API || g_localStorage.api == EGL_OPENGL_ES_API)
        {
            if (glFinish_PTR)
            {
                glFinish();
            }
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    // EGL_EXT_buffer_age / EGL_KHR_partial_update: a completed swap is a
    // frame boundary - the back-buffer generation advances and the
    // per-frame damage bookkeeping resets.
    static void _eglNoteFrameBoundary(EGLSurfaceImpl* surface)
    {
        surface->bufferAgeSwaps++;
        surface->ageQueriedThisFrame = EGL_FALSE;
        surface->damageSetThisFrame  = EGL_FALSE;
    }

    static EGLBoolean _eglSwapBuffersImpl(EGLDisplay dpy, EGLSurface surface)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_FALSE;
        }

        guard_t _{walkerDpy->mutex};

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        EGLSurfaceImpl* walkerSurface = _eglFindSurface(walkerDpy, surface);

        if (!walkerSurface)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        if (!walkerSurface->initialized || walkerSurface->destroy)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        // EGL 1.5 §3.10.3: surface must be bound as the draw surface of the
        // calling thread's current context. Without this __swapBuffers would
        // present whatever device context a foreign surface container
        // happens to carry.
        if (g_localStorage.currentDraw != walkerSurface)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        // EGL 1.5 §3.10.1: on a single-buffered window, pixmap, or pbuffer
        // surface eglSwapBuffers has no effect - but it is not an error.
        if (!walkerSurface->drawToWindow)
        {
            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }

        // EGL 1.5 §3.10.1.1: the swap detects a native-window resize and the
        // surface adopts the new size.
        {
            EGLint width = 0;
            EGLint height = 0;

            if (__windowSurfaceSize(walkerDpy, walkerSurface, &width, &height) == EGL_TRUE && width > 0 && height > 0)
            {
                // EGL_EXT_buffer_age permits resetting the age on resize;
                // with EGL_BUFFER_PRESERVED the content (and so the age)
                // survives the resize instead.
                if ((walkerSurface->width != width || walkerSurface->height != height) &&
                    walkerSurface->swapBehavior != EGL_BUFFER_PRESERVED)
                {
                    walkerSurface->bufferAgeSwaps = 0;
                }

                walkerSurface->width  = width;
                walkerSurface->height = height;
            }
        }

#ifdef EGL_ENABLE_OPENVG
        // OpenVG contexts present their software pixel buffer instead of the
        // native GL swapchain (see eglvg.h).
        if (g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL && g_localStorage.currentCtx->clientAPI == EGL_OPENVG_API)
        {
            _eglVGNotifySwap(g_localStorage.currentCtx);

            if (!__presentVGBuffer(walkerDpy, walkerSurface))
            {
                // EGL 1.5 §3.10.4: a failed post means the native window is no
                // longer valid. Returning EGL_FALSE without an error code would
                // leave eglGetError() reporting EGL_SUCCESS.
                g_localStorage.error = EGL_BAD_NATIVE_WINDOW;

                return EGL_FALSE;
            }

            _eglNoteFrameBoundary(walkerSurface);

            g_localStorage.error = EGL_SUCCESS;

            return EGL_TRUE;
        }
#endif

        if (!__swapBuffers(walkerDpy, walkerSurface))
        {
            // EGL 1.5 §3.10.4: a failed post means the native window is no
            // longer valid. Returning EGL_FALSE without an error code would
            // leave eglGetError() reporting EGL_SUCCESS.
            g_localStorage.error = EGL_BAD_NATIVE_WINDOW;

            return EGL_FALSE;
        }

        _eglNoteFrameBoundary(walkerSurface);

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
    {
        return _eglSwapBuffersImpl(dpy, surface);
    }

    // EGL_KHR_swap_buffers_with_damage: the rectangles are a compositor
    // optimization hint only - the whole back buffer is swapped regardless,
    // so discarding them and doing a plain swap is explicitly compliant
    // (extension issue 2).
    EGLBoolean _eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface, const EGLint* rects, EGLint n_rects)
    {
        if (n_rects < 0 || (n_rects > 0 && !rects))
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

        (void)rects;

        return _eglSwapBuffersImpl(dpy, surface);
    }

    EGLBoolean _eglSetDamageRegionKHR(EGLDisplay dpy, EGLSurface surface, EGLint* rects, EGLint n_rects)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_FALSE;
        }

        guard_t _{walkerDpy->mutex};

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        EGLSurfaceImpl* walkerSurface = _eglFindSurface(walkerDpy, surface);

        if (!walkerSurface)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        if (!walkerSurface->initialized || walkerSurface->destroy)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        // EGL_KHR_partial_update: the surface must be postable (a window) and
        // the current draw surface of the calling thread, and its swap
        // behavior must be EGL_BUFFER_DESTROYED; all three are EGL_BAD_MATCH.
        if (!walkerSurface->drawToWindow || g_localStorage.currentDraw != walkerSurface)
        {
            g_localStorage.error = EGL_BAD_MATCH;

            return EGL_FALSE;
        }

        if (walkerSurface->swapBehavior != EGL_BUFFER_DESTROYED)
        {
            g_localStorage.error = EGL_BAD_MATCH;

            return EGL_FALSE;
        }

        // One damage region per frame, and only after the buffer age was
        // queried this frame; both violations are EGL_BAD_ACCESS.
        if (walkerSurface->damageSetThisFrame)
        {
            g_localStorage.error = EGL_BAD_ACCESS;

            return EGL_FALSE;
        }

        if (!walkerSurface->ageQueriedThisFrame)
        {
            g_localStorage.error = EGL_BAD_ACCESS;

            return EGL_FALSE;
        }

        // The region itself is a rendering-efficiency hint; with full-frame
        // presents the pixels are correct without tracking it. The swap
        // behavior and age bookkeeping above are the observable contract.
        (void)rects;
        (void)n_rects;

        walkerSurface->damageSetThisFrame = EGL_TRUE;

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    //
    // EGL_VERSION_1_1
    //

    EGLBoolean _eglSwapInterval(EGLDisplay dpy, EGLint interval)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_FALSE;
        }

        guard_t _{walkerDpy->mutex};

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        // EGL 1.5 §3.9.3: requires a current context on the calling thread.
        // The display validation comes first - an invalid display must report
        // EGL_BAD_DISPLAY even when nothing is current.
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL)
        {
            g_localStorage.error = EGL_BAD_CONTEXT;

            return EGL_FALSE;
        }

        // Verify calling thread's context is current on this display.
        if (g_localStorage.currentDpy != walkerDpy)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        if (g_localStorage.currentDraw == EGL_NO_SURFACE_IMPL || g_localStorage.currentRead == EGL_NO_SURFACE_IMPL)
        {
            g_localStorage.error = EGL_BAD_SURFACE;

            return EGL_FALSE;
        }

        if (!__swapInterval(walkerDpy, interval))
        {
            return EGL_FALSE;
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    __eglMustCastToProperFunctionPointerType _eglGetProcAddress(const char* procname)
    {
        if (!procname)
            return nullptr;

        // EGL 1.5 allows eglGetProcAddress to return core EGL entry points, and many
        // loaders (glad, libepoxy, ...) rely on it. Resolve the exported egl* names
        // here: the core functions plus their pre-1.5 extension aliases
        // (EGL_KHR_fence_sync, EGL_KHR_wait_sync, EGL_KHR_image_base), whose
        // signatures differ in the attribute types, are wrapped above.
        struct EglProc
        {
            const char*                              name;
            __eglMustCastToProperFunctionPointerType fn;
        };
#define EGL_PROC(n)                                                        \
    {                                                                      \
        #n, reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&(n)) \
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
            // EGL_KHR_fence_sync / EGL_KHR_wait_sync: the pre-1.5 names of
            // the core sync functions, with EGLint signatures.
            EGL_PROC(eglCreateSyncKHR),
            EGL_PROC(eglDestroySyncKHR),
            EGL_PROC(eglClientWaitSyncKHR),
            EGL_PROC(eglGetSyncAttribKHR),
            EGL_PROC(eglWaitSyncKHR),
            // EGL_KHR_reusable_sync: signal a reusable sync's status.
            EGL_PROC(eglSignalSyncKHR),
            // EGL_KHR_image_base: the pre-1.5 name of the core image
            // functions, with an EGLint attribute list.
            EGL_PROC(eglCreateImageKHR),
            EGL_PROC(eglDestroyImageKHR),
            // EGL_KHR_swap_buffers_with_damage / EGL_KHR_partial_update:
            // damage is a hint; the full-frame present and the per-frame
            // bookkeeping live in this library.
            EGL_PROC(eglSwapBuffersWithDamageKHR),
            EGL_PROC(eglSetDamageRegionKHR),
            // GL_OES_EGL_image consumption: the handles are this library's
            // EGLImages, so the entry points are implemented here (see
            // egl_image.cpp) instead of forwarded to the client loader.
            {"glEGLImageTargetTexture2DOES", reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&_glEGLImageTargetTexture2DOES)},
            {"glEGLImageTargetRenderbufferStorageOES", reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&_glEGLImageTargetRenderbufferStorageOES)},
        };
#undef EGL_PROC

        for (const auto& p : k_eglProcs)
            if (strcmp(p.name, procname) == 0)
                return p.fn;

        // Unsupported names of the "egl" namespace belong to this library's
        // own API surface and must stay NULL: deferring them to the client
        // loader would return another EGL implementation's entry point,
        // which cannot know our handles (dEQP resolves eglCreateImageKHR
        // before any display exists and calls it unguarded). The answer must
        // not depend on what the GL/GLES delegation has loaded either.
        if (strncmp(procname, "egl", 3) == 0)
            return nullptr;

        // Not a core EGL entry point — defer to the client (GL/GLES) loader.
        return __getProcAddress(procname);
    }

    //
    // EGL_VERSION_1_3
    //

    EGLSurface _eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint* attrib_list)
    {
        (void)attrib_list;

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_NO_SURFACE;
        }

        guard_t _{walkerDpy->mutex};

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_NO_SURFACE;
        }

        // EGL_OPENVG_IMAGE is the only defined buftype.
        if (buftype != EGL_OPENVG_IMAGE)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_NO_SURFACE;
        }

        if (!_eglFindConfig(walkerDpy, config))
        {
            g_localStorage.error = EGL_BAD_CONFIG;
            return EGL_NO_SURFACE;
        }

        // This library has no VGImage objects (the OpenVG provider seam
        // renders to plain pixel buffers), so no client buffer can ever be a
        // valid OpenVG image here - and an invalid buffer is EGL_BAD_PARAMETER.
        (void)buffer;
        g_localStorage.error = EGL_BAD_PARAMETER;
        return EGL_NO_SURFACE;
    }

} // extern "C"

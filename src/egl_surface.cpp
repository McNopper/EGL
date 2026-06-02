#include "egl_common.h"

extern "C"
{

    EGLSurface _eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list)
    {
        static const EGLint emptyList[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyList;

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
                    return EGL_NO_SURFACE;
                }

                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
                    {
                        EGLSurfaceImpl* newSurface = reinterpret_cast<EGLSurfaceImpl*>(malloc(sizeof(EGLSurfaceImpl)));

                        if (!newSurface)
                        {
                            g_localStorage.error = EGL_BAD_ALLOC;
                            return EGL_NO_SURFACE;
                        }

                        memset(newSurface, 0, sizeof(*newSurface));

                        if (!__createPbufferSurface(newSurface, attrib_list, walkerDpy, walkerConfig, &g_localStorage.error))
                        {
                            free(newSurface);
                            return EGL_NO_SURFACE;
                        }

                        newSurface->next       = walkerDpy->rootSurface;
                        walkerDpy->rootSurface = newSurface;
                        return reinterpret_cast<EGLSurface>(newSurface);
                    }
                    walkerConfig = walkerConfig->next;
                }

                g_localStorage.error = EGL_BAD_CONFIG;
                return EGL_NO_SURFACE;
            }
            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_NO_SURFACE;
    }

    EGLSurface _eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list)
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
                    return EGL_NO_SURFACE;
                }

                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
                    {
                        EGLSurfaceImpl* newSurface = reinterpret_cast<EGLSurfaceImpl*>(malloc(sizeof(EGLSurfaceImpl)));

                        if (!newSurface)
                        {
                            g_localStorage.error = EGL_BAD_ALLOC;
                            return EGL_NO_SURFACE;
                        }

                        memset(newSurface, 0, sizeof(*newSurface));

                        if (!__createWindowSurface(newSurface, win, attrib_list, walkerDpy, walkerConfig, &g_localStorage.error))
                        {
                            free(newSurface);
                            return EGL_NO_SURFACE;
                        }

                        newSurface->next       = walkerDpy->rootSurface;
                        walkerDpy->rootSurface = newSurface;
                        return reinterpret_cast<EGLSurface>(newSurface);
                    }
                    walkerConfig = walkerConfig->next;
                }

                g_localStorage.error = EGL_BAD_CONFIG;
                return EGL_NO_SURFACE;
            }
            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_NO_SURFACE;
    }

    EGLSurface _eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint* attrib_list)
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
                    return EGL_NO_SURFACE;
                }

                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
                    {
                        EGLSurfaceImpl* newSurface = reinterpret_cast<EGLSurfaceImpl*>(malloc(sizeof(EGLSurfaceImpl)));

                        if (!newSurface)
                        {
                            g_localStorage.error = EGL_BAD_ALLOC;
                            return EGL_NO_SURFACE;
                        }

                        memset(newSurface, 0, sizeof(*newSurface));

                        if (!__createPixmapSurface(newSurface, pixmap, attrib_list, walkerDpy, walkerConfig, &g_localStorage.error))
                        {
                            free(newSurface);
                            return EGL_NO_SURFACE;
                        }

                        newSurface->next       = walkerDpy->rootSurface;
                        walkerDpy->rootSurface = newSurface;
                        return reinterpret_cast<EGLSurface>(newSurface);
                    }
                    walkerConfig = walkerConfig->next;
                }

                g_localStorage.error = EGL_BAD_CONFIG;
                return EGL_NO_SURFACE;
            }
            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_NO_SURFACE;
    }

    EGLBoolean _eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
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
                        return __copyBuffers(walkerDpy, walkerSurface, target);
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

    EGLBoolean _eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
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

                            walkerSurface->initialized = EGL_FALSE;
                            walkerSurface->destroy     = EGL_TRUE;

                            __destroySurface(walkerDpy->display_id, walkerSurface);

                            success = EGL_TRUE;
                            break;
                        }

                        walkerSurface = walkerSurface->next;
                    }

                    if (!success)
                    {
                        g_localStorage.error = EGL_BAD_SURFACE;
                        return EGL_FALSE;
                    }
                }

                walkerDpy = walkerDpy->next;
            }
        }
        if (success)
        {
            _eglInternalCleanup();
            return EGL_TRUE;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    EGLBoolean _eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value)
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

                        switch (attribute)
                        {
                        case EGL_CONFIG_ID:
                        {
                            if (value)
                                *value = walkerSurface->configId;
                            return EGL_TRUE;
                        }
                        case EGL_WIDTH:
                        {
                            if (value)
                                *value = walkerSurface->width;
                            return EGL_TRUE;
                        }
                        case EGL_HEIGHT:
                        {
                            if (value)
                                *value = walkerSurface->height;
                            return EGL_TRUE;
                        }
                        case EGL_LARGEST_PBUFFER:
                        {
                            if (value)
                                *value = walkerSurface->largestPbuffer;
                            return EGL_TRUE;
                        }
                        case EGL_MIPMAP_TEXTURE:
                        {
                            if (value)
                                *value = walkerSurface->mipmapTexture;
                            return EGL_TRUE;
                        }
                        case EGL_MIPMAP_LEVEL:
                        {
                            if (value)
                                *value = walkerSurface->mipmapLevel;
                            return EGL_TRUE;
                        }
                        case EGL_MULTISAMPLE_RESOLVE:
                        {
                            if (value)
                                *value = walkerSurface->multisampleResolve;
                            return EGL_TRUE;
                        }
                        case EGL_RENDER_BUFFER:
                        {
                            if (value)
                            {
                                if (walkerSurface->drawToWindow)
                                    *value = walkerSurface->doubleBuffer ? EGL_BACK_BUFFER : EGL_SINGLE_BUFFER;
                                else if (walkerSurface->drawToPixmap)
                                    *value = EGL_SINGLE_BUFFER;
                                else
                                    *value = EGL_BACK_BUFFER;
                            }
                            return EGL_TRUE;
                        }
                        case EGL_SWAP_BEHAVIOR:
                        {
                            if (value)
                                *value = walkerSurface->swapBehavior;
                            return EGL_TRUE;
                        }
                        case EGL_TEXTURE_FORMAT:
                        {
                            if (value)
                                *value = walkerSurface->textureFormat;
                            return EGL_TRUE;
                        }
                        case EGL_TEXTURE_TARGET:
                        {
                            if (value)
                                *value = walkerSurface->textureTarget;
                            return EGL_TRUE;
                        }
                        case EGL_VG_ALPHA_FORMAT:
                        {
                            if (value)
                                *value = EGL_VG_ALPHA_FORMAT_NONPRE;
                            return EGL_TRUE;
                        }
                        case EGL_VG_COLORSPACE:
                        {
                            if (value)
                                *value = EGL_VG_COLORSPACE_sRGB;
                            return EGL_TRUE;
                        }
                        case EGL_HORIZONTAL_RESOLUTION:
                        case EGL_VERTICAL_RESOLUTION:
                        case EGL_PIXEL_ASPECT_RATIO:
                        {
                            // Physical pixel geometry is not tracked; EGL 1.5 (3.5.6)
                            // permits reporting EGL_UNKNOWN for these.
                            if (value)
                                *value = EGL_UNKNOWN;
                            return EGL_TRUE;
                        }
                        case EGL_GL_COLORSPACE:
                        {
                            if (value)
                                *value = walkerSurface->glColorspace;
                            return EGL_TRUE;
                        }
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryRx;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryRy;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryGx;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryGy;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryBx;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086DisplayPrimaryBy;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_WHITE_POINT_X_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086WhitePointX;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_WHITE_POINT_Y_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086WhitePointY;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_MAX_LUMINANCE_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086MaxLuminance;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_MIN_LUMINANCE_EXT:
                            if (value)
                                *value = walkerSurface->smpte2086MinLuminance;
                            return EGL_TRUE;
                        case EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT:
                            if (value)
                                *value = walkerSurface->cta861MaxContentLightLevel;
                            return EGL_TRUE;
                        case EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT:
                            if (value)
                                *value = walkerSurface->cta861MaxFrameAverageLightLevel;
                            return EGL_TRUE;
                        }

                        g_localStorage.error = EGL_BAD_ATTRIBUTE;

                        return EGL_FALSE;
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

    EGLBoolean _eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
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

                        switch (attribute)
                        {
                        case EGL_MIPMAP_LEVEL:
                        {
                            walkerSurface->mipmapLevel = value;
                            return EGL_TRUE;
                        }
                        case EGL_MULTISAMPLE_RESOLVE:
                        {
                            if (value != EGL_MULTISAMPLE_RESOLVE_DEFAULT && value != EGL_MULTISAMPLE_RESOLVE_BOX)
                            {
                                g_localStorage.error = EGL_BAD_ATTRIBUTE;
                                return EGL_FALSE;
                            }
                            walkerSurface->multisampleResolve = value;
                            return EGL_TRUE;
                        }
                        case EGL_SWAP_BEHAVIOR:
                        {
                            if (value != EGL_BUFFER_PRESERVED && value != EGL_BUFFER_DESTROYED)
                            {
                                g_localStorage.error = EGL_BAD_ATTRIBUTE;
                                return EGL_FALSE;
                            }
                            walkerSurface->swapBehavior = value;
                            return EGL_TRUE;
                        }
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT:
                            walkerSurface->smpte2086DisplayPrimaryRx = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT:
                            walkerSurface->smpte2086DisplayPrimaryRy = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT:
                            walkerSurface->smpte2086DisplayPrimaryGx = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT:
                            walkerSurface->smpte2086DisplayPrimaryGy = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT:
                            walkerSurface->smpte2086DisplayPrimaryBx = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT:
                            walkerSurface->smpte2086DisplayPrimaryBy = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_WHITE_POINT_X_EXT:
                            walkerSurface->smpte2086WhitePointX = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_WHITE_POINT_Y_EXT:
                            walkerSurface->smpte2086WhitePointY = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_MAX_LUMINANCE_EXT:
                            walkerSurface->smpte2086MaxLuminance = value;
                            return EGL_TRUE;
                        case EGL_SMPTE2086_MIN_LUMINANCE_EXT:
                            walkerSurface->smpte2086MinLuminance = value;
                            return EGL_TRUE;
                        case EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT:
                            walkerSurface->cta861MaxContentLightLevel = value;
                            return EGL_TRUE;
                        case EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT:
                            walkerSurface->cta861MaxFrameAverageLightLevel = value;
                            return EGL_TRUE;
                        }

                        g_localStorage.error = EGL_BAD_ATTRIBUTE;

                        return EGL_FALSE;
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

    EGLSurface _eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void* native_window, const EGLAttrib* attrib_list)
    {
        // Convert EGLAttrib* (intptr_t) to EGLint* via a temporary buffer.
        EGLint converted[64];
        EGLint count = 0;
        if (attrib_list)
        {
            for (const EGLAttrib* p = attrib_list; *p != EGL_NONE && count + 2 < 63; p += 2, count += 2)
            {
                converted[count]     = (EGLint)p[0];
                converted[count + 1] = (EGLint)p[1];
            }
        }
        converted[count] = EGL_NONE;

        return _eglCreateWindowSurface(dpy, config, reinterpret_cast<EGLNativeWindowType>(native_window), converted);
    }

    EGLSurface _eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void* native_pixmap, const EGLAttrib* attrib_list)
    {
        // Convert EGLAttrib* (intptr_t) to EGLint* via a temporary buffer.
        EGLint converted[64];
        EGLint count = 0;
        if (attrib_list)
        {
            for (const EGLAttrib* p = attrib_list; *p != EGL_NONE && count + 2 < 63; p += 2, count += 2)
            {
                converted[count]     = (EGLint)p[0];
                converted[count + 1] = (EGLint)p[1];
            }
        }
        converted[count] = EGL_NONE;

        return _eglCreatePixmapSurface(dpy, config, reinterpret_cast<EGLNativePixmapType>(native_pixmap), converted);
    }

    EGLBoolean _eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
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
                        if (!walkerSurface->drawToPBuffer)
                        {
                            g_localStorage.error = EGL_BAD_SURFACE;
                            return EGL_FALSE;
                        }
                        if (walkerSurface->textureFormat == EGL_NO_TEXTURE || walkerSurface->textureTarget == EGL_NO_TEXTURE)
                        {
                            g_localStorage.error = EGL_BAD_MATCH;
                            return EGL_FALSE;
                        }
                        if (buffer != EGL_BACK_BUFFER)
                        {
                            g_localStorage.error = EGL_BAD_PARAMETER;
                            return EGL_FALSE;
                        }
                        return __bindTexImage(walkerDpy, walkerSurface, buffer);
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

    EGLBoolean _eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
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
                        if (!walkerSurface->drawToPBuffer)
                        {
                            g_localStorage.error = EGL_BAD_SURFACE;
                            return EGL_FALSE;
                        }
                        if (buffer != EGL_BACK_BUFFER)
                        {
                            g_localStorage.error = EGL_BAD_PARAMETER;
                            return EGL_FALSE;
                        }
                        return __releaseTexImage(walkerDpy, walkerSurface, buffer);
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

} // extern "C"

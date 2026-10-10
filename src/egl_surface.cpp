#include "egl_common.h"

extern "C"
{

    // Returns the surface node for `surface`, or nullptr when the handle is
    // unknown. No error is set - the not-found code stays at the call site.
    // The caller holds the display mutex while using the result.
    EGLSurfaceImpl* _eglFindSurface(EGLDisplayImpl* walkerDpy, EGLSurface surface)
    {
        EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

        while (walkerSurface)
        {
            if (reinterpret_cast<EGLSurface>(walkerSurface) == surface)
            {
                return walkerSurface;
            }

            walkerSurface = walkerSurface->next;
        }

        return nullptr;
    }

    EGLSurface _eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list)
    {
        static const EGLint emptyList[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyList;

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

        EGLConfigImpl* walkerConfig = _eglFindConfig(walkerDpy, config);

        if (!walkerConfig)
        {
            g_localStorage.error = EGL_BAD_CONFIG;
            return EGL_NO_SURFACE;
        }

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

        g_localStorage.error = EGL_SUCCESS;

        return reinterpret_cast<EGLSurface>(newSurface);
    }

    EGLSurface _eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list)
    {
        static const EGLint emptyList[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyList;

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

        EGLConfigImpl* walkerConfig = _eglFindConfig(walkerDpy, config);

        if (!walkerConfig)
        {
            g_localStorage.error = EGL_BAD_CONFIG;
            return EGL_NO_SURFACE;
        }

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

        g_localStorage.error = EGL_SUCCESS;

        return reinterpret_cast<EGLSurface>(newSurface);
    }

    EGLSurface _eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint* attrib_list)
    {
        static const EGLint emptyList[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyList;

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

        EGLConfigImpl* walkerConfig = _eglFindConfig(walkerDpy, config);

        if (!walkerConfig)
        {
            g_localStorage.error = EGL_BAD_CONFIG;
            return EGL_NO_SURFACE;
        }

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

        g_localStorage.error = EGL_SUCCESS;

        return reinterpret_cast<EGLSurface>(newSurface);
    }

    EGLBoolean _eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
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

        if (!__copyBuffers(walkerDpy, walkerSurface, target))
        {
            return EGL_FALSE;
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
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

                walkerSurface->initialized = EGL_FALSE;
                walkerSurface->destroy     = EGL_TRUE;

                // EGL 1.5 §3.5.6: the native drawable must survive as long
                // as the surface is current to any thread, so the native
                // teardown happens in _eglInternalCleanup, together with
                // the struct free.

                success = EGL_TRUE;
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

    // ── Surface attribute query table ────────────────────────────────────────
    // eglQuerySurface is driven by this table instead of a hand-written switch:
    // one row per attribute carrying the field offset or the constant kind.

    enum SurfaceQueryKind
    {
        SURFACE_FIELD,             // read an EGLint field through the offset
        SURFACE_CONST_UNKNOWN,     // EGL_UNKNOWN (pixel geometry is not tracked)
        SURFACE_CONST_VG_ALPHA,    // EGL_VG_ALPHA_FORMAT_NONPRE
        SURFACE_CONST_VG_COLORSPACE // EGL_VG_COLORSPACE_sRGB
    };

    struct SurfaceQueryDesc
    {
        size_t offset; // offsetof(EGLSurfaceImpl, field); unused for the constant kinds
        EGLint attribute;
        int    kind;   // SurfaceQueryKind
    };

    static const SurfaceQueryDesc s_surfaceQueryDescs[] = {
        {offsetof(EGLSurfaceImpl, configId),                   EGL_CONFIG_ID,                                      SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, width),                      EGL_WIDTH,                                          SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, height),                     EGL_HEIGHT,                                         SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, largestPbuffer),             EGL_LARGEST_PBUFFER,                                SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, mipmapTexture),              EGL_MIPMAP_TEXTURE,                                 SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, mipmapLevel),                EGL_MIPMAP_LEVEL,                                   SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, multisampleResolve),         EGL_MULTISAMPLE_RESOLVE,                            SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, swapBehavior),               EGL_SWAP_BEHAVIOR,                                  SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, textureFormat),              EGL_TEXTURE_FORMAT,                                 SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, textureTarget),              EGL_TEXTURE_TARGET,                                 SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, glColorspace),               EGL_GL_COLORSPACE,                                  SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryRx),  EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryRy),  EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryGx),  EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryGy),  EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryBx),  EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086DisplayPrimaryBy),  EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT,               SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086WhitePointX),       EGL_SMPTE2086_WHITE_POINT_X_EXT,                    SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086WhitePointY),       EGL_SMPTE2086_WHITE_POINT_Y_EXT,                    SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086MaxLuminance),      EGL_SMPTE2086_MAX_LUMINANCE_EXT,                    SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, smpte2086MinLuminance),      EGL_SMPTE2086_MIN_LUMINANCE_EXT,                    SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, cta861MaxContentLightLevel), EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT,           SURFACE_FIELD},
        {offsetof(EGLSurfaceImpl, cta861MaxFrameAverageLightLevel), EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT,           SURFACE_FIELD},
        {0,                                                    EGL_VG_ALPHA_FORMAT,                                SURFACE_CONST_VG_ALPHA},
        {0,                                                    EGL_VG_COLORSPACE,                                  SURFACE_CONST_VG_COLORSPACE},
        {0,                                                    EGL_HORIZONTAL_RESOLUTION,                          SURFACE_CONST_UNKNOWN},
        {0,                                                    EGL_VERTICAL_RESOLUTION,                            SURFACE_CONST_UNKNOWN},
        {0,                                                    EGL_PIXEL_ASPECT_RATIO,                             SURFACE_CONST_UNKNOWN},
    };

    EGLBoolean _eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value)
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

        // Cleared up front; the fall-through below overwrites it for
        // an unrecognized attribute.
        g_localStorage.error = EGL_SUCCESS;

        if (attribute == EGL_RENDER_BUFFER)
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

        const SurfaceQueryDesc* desc = nullptr;

        for (const SurfaceQueryDesc& d : s_surfaceQueryDescs)
        {
            if (d.attribute == attribute)
            {
                desc = &d;
                break;
            }
        }

        if (!desc)
        {
            g_localStorage.error = EGL_BAD_ATTRIBUTE;

            return EGL_FALSE;
        }

        if (value)
        {
            switch (desc->kind)
            {
            case SURFACE_FIELD:
                *value = *reinterpret_cast<const EGLint*>(reinterpret_cast<const char*>(walkerSurface) + desc->offset);
                break;
            case SURFACE_CONST_UNKNOWN:
                // Physical pixel geometry is not tracked; EGL 1.5 (3.5.6)
                // permits reporting EGL_UNKNOWN for these.
                *value = EGL_UNKNOWN;
                break;
            case SURFACE_CONST_VG_ALPHA:
                *value = EGL_VG_ALPHA_FORMAT_NONPRE;
                break;
            case SURFACE_CONST_VG_COLORSPACE:
                *value = EGL_VG_COLORSPACE_sRGB;
                break;
            default:
                break; // Unreachable: the table only carries the kinds above.
            }
        }

        return EGL_TRUE;
    }

    EGLBoolean _eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
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

        // EGL 1.5 §3.5.6: the surface's config has to advertise the
        // capability before it may be requested.
        EGLint surfaceTypeBits = 0;

        for (EGLConfigImpl* walkerConfig = walkerDpy->rootConfig; walkerConfig; walkerConfig = walkerConfig->next)
        {
            if (walkerConfig->configId == walkerSurface->configId)
            {
                surfaceTypeBits = walkerConfig->surfaceType;

                break;
            }
        }

        // Cleared up front; the fall-through below overwrites it for
        // an unrecognized attribute.
        g_localStorage.error = EGL_SUCCESS;

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
            if (value == EGL_MULTISAMPLE_RESOLVE_BOX && !(surfaceTypeBits & EGL_MULTISAMPLE_RESOLVE_BOX_BIT))
            {
                g_localStorage.error = EGL_BAD_MATCH;
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
            if (value == EGL_BUFFER_PRESERVED && !(surfaceTypeBits & EGL_SWAP_BEHAVIOR_PRESERVED_BIT))
            {
                g_localStorage.error = EGL_BAD_MATCH;
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
        default:
            break; // Unrecognized attribute; ignored.
        }

        g_localStorage.error = EGL_BAD_ATTRIBUTE;

        return EGL_FALSE;
    }

    // Converts an EGLAttrib list to the EGLint form the platform create
    // functions take. The conversion buffer caps the list at 31 pairs. A longer
    // list must fail loudly instead of being accepted truncated - the dropped
    // tail could carry an attribute the caller relies on, or one that should
    // raise EGL_BAD_ATTRIBUTE.
    static bool _eglConvertAttribList(const EGLAttrib* attrib_list, EGLint* converted)
    {
        EGLint count = 0;

        if (attrib_list)
        {
            const EGLAttrib* p = attrib_list;

            for (; *p != EGL_NONE && count + 2 < EGLATTRIB_CONVERT_BUFFER_SIZE - 1; p += 2, count += 2)
            {
                converted[count]     = (EGLint)p[0];
                converted[count + 1] = (EGLint)p[1];
            }

            if (*p != EGL_NONE)
            {
                return false;
            }
        }
        converted[count] = EGL_NONE;

        return true;
    }

    EGLSurface _eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void* native_window, const EGLAttrib* attrib_list)
    {
        EGLint converted[EGLATTRIB_CONVERT_BUFFER_SIZE];

        if (!_eglConvertAttribList(attrib_list, converted))
        {
            g_localStorage.error = EGL_BAD_ALLOC;

            return EGL_NO_SURFACE;
        }

        return _eglCreateWindowSurface(dpy, config, reinterpret_cast<EGLNativeWindowType>(native_window), converted);
    }

    EGLSurface _eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void* native_pixmap, const EGLAttrib* attrib_list)
    {
        EGLint converted[EGLATTRIB_CONVERT_BUFFER_SIZE];

        if (!_eglConvertAttribList(attrib_list, converted))
        {
            g_localStorage.error = EGL_BAD_ALLOC;

            return EGL_NO_SURFACE;
        }

        return _eglCreatePixmapSurface(dpy, config, reinterpret_cast<EGLNativePixmapType>(native_pixmap), converted);
    }

    EGLBoolean _eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
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
        if (!__bindTexImage(walkerDpy, walkerSurface, buffer))
        {
            return EGL_FALSE;
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
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
        if (!__releaseTexImage(walkerDpy, walkerSurface, buffer))
        {
            return EGL_FALSE;
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

} // extern "C"

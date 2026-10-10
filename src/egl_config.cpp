#include "egl_common.h"
#include <algorithm>

// Fixed match-phase working set; more matches than this fail loudly with
// EGL_BAD_ALLOC rather than truncating silently.
#define CONFIGS_ON_STACK_MAX 1024

// The request template is needed for sort rule 3, so it is passed in explicitly;
// a plain qsort comparator has no way of seeing it.
static int _ChooseConfig_sort_predicate(const EGLConfigImpl* lhs, const EGLConfigImpl* rhs, const EGLConfigImpl& request)
{
    if (lhs->configCaveat == rhs->configCaveat)
    {
        if (lhs->colorBufferType == rhs->colorBufferType)
        {
            // EGL 1.5 Table 3.5 rule 3: a colour component whose requested size is
            // 0 or EGL_DONT_CARE must NOT be counted. Summing all of them ranks an
            // RGBA8888 config above RGB888 for a request of 8/8/8 without alpha.
            EGLint color_bits[2] = {0, 0};
            switch (lhs->colorBufferType)
            {
            case EGL_RGB_BUFFER:
                if (request.redSize > 0)
                {
                    color_bits[0] += lhs->redSize;
                    color_bits[1] += rhs->redSize;
                }
                if (request.greenSize > 0)
                {
                    color_bits[0] += lhs->greenSize;
                    color_bits[1] += rhs->greenSize;
                }
                if (request.blueSize > 0)
                {
                    color_bits[0] += lhs->blueSize;
                    color_bits[1] += rhs->blueSize;
                }
                if (request.alphaSize > 0)
                {
                    color_bits[0] += lhs->alphaSize;
                    color_bits[1] += rhs->alphaSize;
                }
                break;
            case EGL_LUMINANCE_BUFFER:
                if (request.luminanceSize > 0)
                {
                    color_bits[0] += lhs->luminanceSize;
                    color_bits[1] += rhs->luminanceSize;
                }
                if (request.alphaSize > 0)
                {
                    color_bits[0] += lhs->alphaSize;
                    color_bits[1] += rhs->alphaSize;
                }
                break;
            default:
                break;
            }

            if (color_bits[0] == color_bits[1])
            {
                if (lhs->bufferSize == rhs->bufferSize)
                {
                    if (lhs->sampleBuffers == rhs->sampleBuffers)
                    {
                        if (lhs->samples == rhs->samples)
                        {
                            if (lhs->depthSize == rhs->depthSize)
                            {
                                if (lhs->stencilSize == rhs->stencilSize)
                                {
                                    if (lhs->alphaMaskSize == rhs->alphaMaskSize)
                                    {
                                        // skip rule 10 since it's impl-defined
                                        // 10. Special: EGL_NATIVE_VISUAL_TYPE (the actual sort order is implementation-defined, depending on the meaning of native visual types).

                                        // 11. Smaller EGL_CONFIG_ID (guarantees a unique ordering)
                                        return (lhs->configId - rhs->configId);
                                    }
                                    else
                                        return (lhs->alphaMaskSize - rhs->alphaMaskSize); // 9. Smaller EGL_ALPHA_MASK_SIZE
                                }
                                else
                                    return (lhs->stencilSize - rhs->stencilSize); // 8. Smaller EGL_STENCIL_SIZE
                            }
                            else
                                return (lhs->depthSize - rhs->depthSize); // 7. Smaller EGL_DEPTH_SIZE
                        }
                        else
                            return (lhs->samples - rhs->samples); // 6. Smaller EGL_SAMPLES
                    }
                    else
                        return (lhs->sampleBuffers - rhs->sampleBuffers); // 5. Smaller EGL_SAMPLE_BUFFERS
                }
                else
                    return (lhs->bufferSize - rhs->bufferSize); // 4. Smaller EGL_BUFFER_SIZE
            }
            else
                return color_bits[1] - color_bits[0]; // 3. by larger total number of color bits
        }
        else
            return (lhs->colorBufferType - rhs->colorBufferType); // 2. by EGL_COLOR_BUFFER_TYPE
    }
    else
        return (lhs->configCaveat - rhs->configCaveat); // 1. by EGL_CONFIG_CAVEAT
}

extern "C"
{

    // Returns the config node for `config`, or nullptr when the handle is
    // unknown. No error is set - the not-found code stays at the call site.
    // The caller holds the display mutex while using the result.
    EGLConfigImpl* _eglFindConfig(EGLDisplayImpl* walkerDpy, EGLConfig config)
    {
        EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

        while (walkerConfig)
        {
            if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
            {
                return walkerConfig;
            }

            walkerConfig = walkerConfig->next;
        }

        return nullptr;
    }

    // ── Attribute descriptor tables ──────────────────────────────────────────
    // eglChooseConfig and eglGetConfigAttrib are driven by these tables instead
    // of hand-written switches: the parse validation rule, the request-vs-config
    // match rule and the field offset are reviewed as data, one row per attribute.

    enum ConfigParseRule
    {
        PARSE_NONE,              // any value accepted
        PARSE_NON_NEGATIVE,      // EGL_DONT_CARE or >= 0
        PARSE_BOOLEAN,         // EGL_DONT_CARE / EGL_TRUE / EGL_FALSE
        PARSE_COLOR_BUFFER_TYPE, // EGL_DONT_CARE / EGL_RGB_BUFFER / EGL_LUMINANCE_BUFFER
        PARSE_CONFIG_CAVEAT,   // EGL_DONT_CARE / EGL_NONE / EGL_SLOW_CONFIG / EGL_NON_CONFORMANT_CONFIG
        PARSE_CONFORMANT_MASK, // EGL_DONT_CARE or a subset of the API bits
        PARSE_RENDERABLE_MASK, // EGL_DONT_CARE or a subset of the renderable bits
        PARSE_SURFACE_MASK,    // EGL_DONT_CARE or a subset of the surface-type bits
        PARSE_TRANSPARENT_TYPE // EGL_DONT_CARE / EGL_NONE / EGL_TRANSPARENT_RGB
    };

    enum ConfigMatchRule
    {
        MATCH_NONE,                   // never filters (rows that exist for the query table only)
        MATCH_NOT_LESS,               // request must not exceed the config's value; EGL_DONT_CARE is -1 and self-skips
        MATCH_EQUAL_DC,               // equal, unless the request is EGL_DONT_CARE
        MATCH_EQUAL_ALWAYS,           // always equal (no dont-care escape)
        MATCH_EQUAL_NE_NONE,          // equal, unless the request is EGL_NONE
        MATCH_SUBSET_DC,              // request bits must be a subset of the config's, unless EGL_DONT_CARE
        MATCH_EQUAL_DC_IF_TRANSPARENT // equal unless DONT_CARE, applied only to EGL_TRANSPARENT_RGB configs
    };

    struct ConfigAttribDesc
    {
        EGLint attribute;
        size_t offset;     // offsetof(EGLConfigImpl, field)
        int    parseRule;  // ConfigParseRule
        int    matchRule;  // ConfigMatchRule
    };

    static const ConfigAttribDesc s_chooseConfigDescs[] = {
        {EGL_ALPHA_MASK_SIZE,         offsetof(EGLConfigImpl, alphaMaskSize),         PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_ALPHA_SIZE,              offsetof(EGLConfigImpl, alphaSize),             PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_BIND_TO_TEXTURE_RGB,     offsetof(EGLConfigImpl, bindToTextureRGB),      PARSE_BOOLEAN,           MATCH_EQUAL_DC},
        {EGL_BIND_TO_TEXTURE_RGBA,    offsetof(EGLConfigImpl, bindToTextureRGBA),     PARSE_BOOLEAN,           MATCH_EQUAL_DC},
        {EGL_BLUE_SIZE,               offsetof(EGLConfigImpl, blueSize),              PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_BUFFER_SIZE,             offsetof(EGLConfigImpl, bufferSize),            PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_COLOR_BUFFER_TYPE,       offsetof(EGLConfigImpl, colorBufferType),       PARSE_COLOR_BUFFER_TYPE, MATCH_EQUAL_DC},
        {EGL_CONFIG_CAVEAT,           offsetof(EGLConfigImpl, configCaveat),          PARSE_CONFIG_CAVEAT,     MATCH_EQUAL_DC},
        {EGL_CONFIG_ID,               offsetof(EGLConfigImpl, configId),              PARSE_NONE,              MATCH_EQUAL_DC},
        {EGL_CONFORMANT,              offsetof(EGLConfigImpl, conformant),            PARSE_CONFORMANT_MASK,   MATCH_SUBSET_DC},
        {EGL_DEPTH_SIZE,              offsetof(EGLConfigImpl, depthSize),             PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_GREEN_SIZE,              offsetof(EGLConfigImpl, greenSize),             PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_LEVEL,                   offsetof(EGLConfigImpl, level),                 PARSE_NONE,              MATCH_EQUAL_ALWAYS},
        {EGL_LUMINANCE_SIZE,          offsetof(EGLConfigImpl, luminanceSize),         PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_MATCH_NATIVE_PIXMAP,     offsetof(EGLConfigImpl, matchNativePixmap),     PARSE_NONE,              MATCH_EQUAL_NE_NONE},
        {EGL_MAX_SWAP_INTERVAL,       offsetof(EGLConfigImpl, maxSwapInterval),       PARSE_NON_NEGATIVE,      MATCH_EQUAL_DC},
        {EGL_MIN_SWAP_INTERVAL,       offsetof(EGLConfigImpl, minSwapInterval),       PARSE_NON_NEGATIVE,      MATCH_EQUAL_DC},
        {EGL_NATIVE_RENDERABLE,       offsetof(EGLConfigImpl, nativeRenderable),      PARSE_BOOLEAN,           MATCH_EQUAL_DC},
        {EGL_RED_SIZE,                offsetof(EGLConfigImpl, redSize),               PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_RENDERABLE_TYPE,         offsetof(EGLConfigImpl, renderableType),        PARSE_RENDERABLE_MASK,   MATCH_SUBSET_DC},
        {EGL_SAMPLE_BUFFERS,          offsetof(EGLConfigImpl, sampleBuffers),         PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_SAMPLES,                 offsetof(EGLConfigImpl, samples),               PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_STENCIL_SIZE,            offsetof(EGLConfigImpl, stencilSize),           PARSE_NON_NEGATIVE,      MATCH_NOT_LESS},
        {EGL_SURFACE_TYPE,            offsetof(EGLConfigImpl, surfaceType),           PARSE_SURFACE_MASK,      MATCH_SUBSET_DC},
        {EGL_TRANSPARENT_TYPE,        offsetof(EGLConfigImpl, transparentType),       PARSE_TRANSPARENT_TYPE,  MATCH_EQUAL_ALWAYS},
        {EGL_TRANSPARENT_RED_VALUE,   offsetof(EGLConfigImpl, transparentRedValue),   PARSE_NON_NEGATIVE,      MATCH_EQUAL_DC_IF_TRANSPARENT},
        {EGL_TRANSPARENT_GREEN_VALUE, offsetof(EGLConfigImpl, transparentGreenValue), PARSE_NON_NEGATIVE,      MATCH_EQUAL_DC_IF_TRANSPARENT},
        {EGL_TRANSPARENT_BLUE_VALUE,  offsetof(EGLConfigImpl, transparentBlueValue),  PARSE_NON_NEGATIVE,      MATCH_EQUAL_DC_IF_TRANSPARENT},
    };

    static bool _eglValidateConfigAttribValue(int parseRule, EGLint value)
    {
        switch (parseRule)
        {
        case PARSE_NONE:
            return true;
        case PARSE_NON_NEGATIVE:
            return value == EGL_DONT_CARE || value >= 0;
        case PARSE_BOOLEAN:
            return value == EGL_DONT_CARE || value == EGL_TRUE || value == EGL_FALSE;
        case PARSE_COLOR_BUFFER_TYPE:
            return value == EGL_DONT_CARE || value == EGL_RGB_BUFFER || value == EGL_LUMINANCE_BUFFER;
        case PARSE_CONFIG_CAVEAT:
            return value == EGL_DONT_CARE || value == EGL_NONE || value == EGL_SLOW_CONFIG || value == EGL_NON_CONFORMANT_CONFIG;
        case PARSE_CONFORMANT_MASK:
        case PARSE_RENDERABLE_MASK:
            return value == EGL_DONT_CARE || (value & ~(EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT | EGL_OPENVG_BIT)) == 0;
        case PARSE_SURFACE_MASK:
            return value == EGL_DONT_CARE || (value & ~(EGL_MULTISAMPLE_RESOLVE_BOX_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT | EGL_VG_ALPHA_FORMAT_PRE_BIT | EGL_VG_COLORSPACE_LINEAR_BIT | EGL_WINDOW_BIT)) == 0;
        case PARSE_TRANSPARENT_TYPE:
            return value == EGL_DONT_CARE || value == EGL_NONE || value == EGL_TRANSPARENT_RGB;
        default:
            return false;
        }
    }

    // True when the row's rule rejects (request, current). currentTransparentType
    // gates the transparent-value rows, matching the original conditional group.
    static bool _eglConfigMatchRejects(const ConfigAttribDesc& desc, EGLint request, EGLint current, EGLint currentTransparentType)
    {
        switch (desc.matchRule)
        {
        case MATCH_NOT_LESS:
            return request > current;
        case MATCH_EQUAL_DC:
            return request != EGL_DONT_CARE && request != current;
        case MATCH_EQUAL_ALWAYS:
            return request != current;
        case MATCH_EQUAL_NE_NONE:
            return request != EGL_NONE && request != current;
        case MATCH_SUBSET_DC:
            return request != EGL_DONT_CARE && (request & current) != request;
        case MATCH_EQUAL_DC_IF_TRANSPARENT:
            return currentTransparentType == EGL_TRANSPARENT_RGB && request != EGL_DONT_CARE && request != current;
        case MATCH_NONE:
        default:
            return false;
        }
    }

    struct ConfigQueryDesc
    {
        EGLint attribute;
        size_t offset; // offsetof(EGLConfigImpl, field)
    };

    static const ConfigQueryDesc s_getConfigAttribDescs[] = {
        {EGL_ALPHA_MASK_SIZE,         offsetof(EGLConfigImpl, alphaMaskSize)},
        {EGL_ALPHA_SIZE,              offsetof(EGLConfigImpl, alphaSize)},
        {EGL_BIND_TO_TEXTURE_RGB,     offsetof(EGLConfigImpl, bindToTextureRGB)},
        {EGL_BIND_TO_TEXTURE_RGBA,    offsetof(EGLConfigImpl, bindToTextureRGBA)},
        {EGL_BLUE_SIZE,               offsetof(EGLConfigImpl, blueSize)},
        {EGL_BUFFER_SIZE,             offsetof(EGLConfigImpl, bufferSize)},
        {EGL_COLOR_BUFFER_TYPE,       offsetof(EGLConfigImpl, colorBufferType)},
        {EGL_CONFIG_CAVEAT,           offsetof(EGLConfigImpl, configCaveat)},
        {EGL_CONFIG_ID,               offsetof(EGLConfigImpl, configId)},
        {EGL_CONFORMANT,              offsetof(EGLConfigImpl, conformant)},
        {EGL_DEPTH_SIZE,              offsetof(EGLConfigImpl, depthSize)},
        {EGL_GREEN_SIZE,              offsetof(EGLConfigImpl, greenSize)},
        {EGL_LEVEL,                   offsetof(EGLConfigImpl, level)},
        {EGL_LUMINANCE_SIZE,          offsetof(EGLConfigImpl, luminanceSize)},
        {EGL_MAX_PBUFFER_WIDTH,       offsetof(EGLConfigImpl, maxPBufferWidth)},
        {EGL_MAX_PBUFFER_HEIGHT,      offsetof(EGLConfigImpl, maxPBufferHeight)},
        {EGL_MAX_PBUFFER_PIXELS,      offsetof(EGLConfigImpl, maxPBufferPixels)},
        {EGL_MAX_SWAP_INTERVAL,       offsetof(EGLConfigImpl, maxSwapInterval)},
        {EGL_MIN_SWAP_INTERVAL,       offsetof(EGLConfigImpl, minSwapInterval)},
        {EGL_NATIVE_RENDERABLE,       offsetof(EGLConfigImpl, nativeRenderable)},
        {EGL_NATIVE_VISUAL_ID,        offsetof(EGLConfigImpl, nativeVisualId)},
        {EGL_NATIVE_VISUAL_TYPE,      offsetof(EGLConfigImpl, nativeVisualType)},
        {EGL_RED_SIZE,                offsetof(EGLConfigImpl, redSize)},
        {EGL_RENDERABLE_TYPE,         offsetof(EGLConfigImpl, renderableType)},
        {EGL_SAMPLE_BUFFERS,          offsetof(EGLConfigImpl, sampleBuffers)},
        {EGL_SAMPLES,                 offsetof(EGLConfigImpl, samples)},
        {EGL_STENCIL_SIZE,            offsetof(EGLConfigImpl, stencilSize)},
        {EGL_SURFACE_TYPE,            offsetof(EGLConfigImpl, surfaceType)},
        {EGL_TRANSPARENT_TYPE,        offsetof(EGLConfigImpl, transparentType)},
        {EGL_TRANSPARENT_RED_VALUE,   offsetof(EGLConfigImpl, transparentRedValue)},
        {EGL_TRANSPARENT_GREEN_VALUE, offsetof(EGLConfigImpl, transparentGreenValue)},
        {EGL_TRANSPARENT_BLUE_VALUE,  offsetof(EGLConfigImpl, transparentBlueValue)},
    };

    EGLBoolean _eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config)
    {
        static const EGLint emptyList[] = {EGL_NONE};
        if (!attrib_list)
            attrib_list = emptyList;

        if (!num_config)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

        // config_size is signed and ends up as a memcpy size; a negative value would
        // turn into a huge size_t.
        if (config_size < 0)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

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

        EGLConfigImpl config;

        _eglInternalSetDefaultConfig(&config);
        config.configCaveat = EGL_DONT_CARE; // dont care for this attribute since it cant be queried on both WGL and GLX

        // More pairs than distinct legal attributes cannot exist. A fully
        // populated legal list ends on exactly that many pairs, so only more
        // than that is an error.
        const EGLint maxPairs = (EGLint)(sizeof(s_chooseConfigDescs) / sizeof(s_chooseConfigDescs[0]));

        EGLint attribListIndex = 0;

        while (attrib_list[attribListIndex] != EGL_NONE)
        {
            const EGLint attribute = attrib_list[attribListIndex];
            const EGLint value     = attrib_list[attribListIndex + 1];

            const ConfigAttribDesc* desc = nullptr;

            for (const ConfigAttribDesc& d : s_chooseConfigDescs)
            {
                if (d.attribute == attribute)
                {
                    desc = &d;
                    break;
                }
            }

            // Unknown attribute, or one that is only queryable.
            if (!desc || !_eglValidateConfigAttribValue(desc->parseRule, value))
            {
                g_localStorage.error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }

            *reinterpret_cast<EGLint*>(reinterpret_cast<char*>(&config) + desc->offset) = value;

            attribListIndex += 2;

            if (attribListIndex > 2 * maxPairs)
            {
                g_localStorage.error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        config.drawToWindow  = (config.surfaceType & EGL_WINDOW_BIT) ? EGL_TRUE : EGL_FALSE;
        config.drawToPixmap  = (config.surfaceType & EGL_PIXMAP_BIT) ? EGL_TRUE : EGL_FALSE;
        config.drawToPBuffer = (config.surfaceType & EGL_PBUFFER_BIT) ? EGL_TRUE : EGL_FALSE;

        // EGL 1.5 §3.4.1: if EGL_CONFIG_ID is given and is not EGL_DONT_CARE,
        // every other attribute is ignored.
        const EGLBoolean matchConfigIdOnly = (config.configId != EGL_DONT_CARE) ? EGL_TRUE : EGL_FALSE;

        // Check, if this configuration exists.
        EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

        // Properly typed storage: a char array reinterpret_cast to EGLConfig*
        // carries no alignment guarantee.
        EGLConfig    configsOnStack[CONFIGS_ON_STACK_MAX];
        const EGLint max_configs = static_cast<EGLint>(sizeof(configsOnStack) / sizeof(configsOnStack[0]));

        EGLint configIndex = 0;

        while (walkerConfig && configIndex < max_configs)
        {
            if (matchConfigIdOnly)
            {
                if (config.configId != walkerConfig->configId)
                {
                    walkerConfig = walkerConfig->next;

                    continue;
                }

                configsOnStack[configIndex] = walkerConfig;

                walkerConfig = walkerConfig->next;

                configIndex++;

                continue;
            }

            bool rejected = false;

            for (const ConfigAttribDesc& desc : s_chooseConfigDescs)
            {
                const EGLint request = *reinterpret_cast<const EGLint*>(reinterpret_cast<const char*>(&config) + desc.offset);
                const EGLint current = *reinterpret_cast<const EGLint*>(reinterpret_cast<const char*>(walkerConfig) + desc.offset);

                if (_eglConfigMatchRejects(desc, request, current, walkerConfig->transparentType))
                {
                    rejected = true;
                    break;
                }
            }

            if (rejected)
            {
                walkerConfig = walkerConfig->next;

                continue;
            }

            // EGL_DOUBLEBUFFER is not a choose criterion (the parse phase rejects
            // it), so the template keeps the EGL_TRUE default and only
            // double-buffered configs match - the hand-written filter did the same.
            if (config.doubleBuffer != EGL_DONT_CARE && config.doubleBuffer != walkerConfig->doubleBuffer)
            {
                walkerConfig = walkerConfig->next;

                continue;
            }

            //

            configsOnStack[configIndex] = walkerConfig;

            walkerConfig = walkerConfig->next;

            configIndex++;
        }

                if (walkerConfig)
        if (walkerConfig)
        {
            // More matches than the stack buffer holds. Truncating silently
            // would leave the caller with no way of noticing.
            g_localStorage.error = EGL_BAD_ALLOC;

            return EGL_FALSE;
        }

        if (configIndex)
        {
            std::sort(configsOnStack, configsOnStack + configIndex,
                      [&config](const EGLConfig lhs, const EGLConfig rhs)
                      {
                          return _ChooseConfig_sort_predicate(reinterpret_cast<const EGLConfigImpl*>(lhs), reinterpret_cast<const EGLConfigImpl*>(rhs), config) < 0;
                      });
        }

        // EGL 1.5 §3.4.1: when configs is not NULL, num_config reports the
        // number of entries actually written, not the total match count.
        EGLint numberWritten = configIndex;

        if (configs)
        {
            numberWritten = (std::min)(configIndex, config_size);

            memcpy(configs, configsOnStack, static_cast<size_t>(numberWritten) * sizeof(EGLConfig));
        }

        *num_config = numberWritten;

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint config_size, EGLint* num_config)
    {
        if (!num_config)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

        if (config_size < 0)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;

            return EGL_FALSE;
        }

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

        EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

        EGLint configIndex   = 0;
        EGLint numberWritten = 0;

        while (walkerConfig)
        {
            if (configs && configIndex < config_size)
            {
                configs[configIndex] = walkerConfig;

                numberWritten++;
            }

            walkerConfig = walkerConfig->next;

            configIndex++;
        }

        // EGL 1.5 §3.4.1: with configs != NULL only the number of entries
        // actually written may be reported.
        *num_config = configs ? numberWritten : configIndex;

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    EGLBoolean _eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value)
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

        EGLConfigImpl* walkerConfig = _eglFindConfig(walkerDpy, config);

        if (!walkerConfig)
        {
            g_localStorage.error = EGL_BAD_CONFIG;

            return EGL_FALSE;
        }

        const ConfigQueryDesc* desc = nullptr;

        for (const ConfigQueryDesc& d : s_getConfigAttribDescs)
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
            *value = *reinterpret_cast<const EGLint*>(reinterpret_cast<const char*>(walkerConfig) + desc->offset);
        }

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

} // extern "C"

#include "egl_common.h"
#include <algorithm>

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

                EGLint attribListIndex = 0;

                EGLConfigImpl config;

                _eglInternalSetDefaultConfig(&config);
                config.configCaveat = EGL_DONT_CARE; // dont care for this attribute since it cant be queried on both WGL and GLX

                while (attrib_list[attribListIndex] != EGL_NONE)
                {
                    EGLint value = attrib_list[attribListIndex + 1];

                    switch (attrib_list[attribListIndex])
                    {
                    case EGL_ALPHA_MASK_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.alphaMaskSize = value;
                    }
                    break;
                    case EGL_ALPHA_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.alphaSize = value;
                    }
                    break;
                    case EGL_BIND_TO_TEXTURE_RGB:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_TRUE && value != EGL_FALSE)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.bindToTextureRGB = value;
                    }
                    break;
                    case EGL_BIND_TO_TEXTURE_RGBA:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_TRUE && value != EGL_FALSE)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.bindToTextureRGBA = value;
                    }
                    break;
                    case EGL_BLUE_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.blueSize = value;
                    }
                    break;
                    case EGL_BUFFER_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.bufferSize = value;
                    }
                    break;
                    case EGL_COLOR_BUFFER_TYPE:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_RGB_BUFFER && value != EGL_LUMINANCE_BUFFER)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.colorBufferType = value;
                    }
                    break;
                    case EGL_CONFIG_CAVEAT:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_NONE && value != EGL_SLOW_CONFIG && value != EGL_NON_CONFORMANT_CONFIG)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.configCaveat = value;
                    }
                    break;
                    case EGL_CONFIG_ID:
                    {
                        config.configId = value;
                    }
                    break;
                    case EGL_CONFORMANT:
                    {
                        if (value != EGL_DONT_CARE && value & ~(EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT | EGL_OPENVG_BIT))
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.conformant = value;
                    }
                    break;
                    case EGL_DEPTH_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.depthSize = value;
                    }
                    break;
                    case EGL_GREEN_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.greenSize = value;
                    }
                    break;
                    case EGL_LEVEL:
                    {
                        config.level = value;
                    }
                    break;
                    case EGL_LUMINANCE_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.luminanceSize = value;
                    }
                    break;
                    case EGL_MATCH_NATIVE_PIXMAP:
                    {
                        config.matchNativePixmap = value;
                    }
                    break;
                    case EGL_NATIVE_RENDERABLE:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_TRUE && value != EGL_FALSE)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.nativeRenderable = value;
                    }
                    break;
                    case EGL_MAX_SWAP_INTERVAL:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.maxSwapInterval = value;
                    }
                    break;
                    case EGL_MIN_SWAP_INTERVAL:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.minSwapInterval = value;
                    }
                    break;
                    case EGL_RED_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.redSize = value;
                    }
                    break;
                    case EGL_SAMPLE_BUFFERS:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.sampleBuffers = value;
                    }
                    break;
                    case EGL_SAMPLES:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.samples = value;
                    }
                    break;
                    case EGL_STENCIL_SIZE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.stencilSize = value;
                    }
                    break;
                    case EGL_RENDERABLE_TYPE:
                    {
                        if (value != EGL_DONT_CARE && value & ~(EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT | EGL_OPENVG_BIT))
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.renderableType = value;
                    }
                    break;
                    case EGL_SURFACE_TYPE:
                    {
                        if (value != EGL_DONT_CARE && value & ~(EGL_MULTISAMPLE_RESOLVE_BOX_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT | EGL_VG_ALPHA_FORMAT_PRE_BIT | EGL_VG_COLORSPACE_LINEAR_BIT | EGL_WINDOW_BIT))
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.surfaceType = value;
                    }
                    break;
                    case EGL_TRANSPARENT_TYPE:
                    {
                        if (value != EGL_DONT_CARE && value != EGL_NONE && value != EGL_TRANSPARENT_RGB)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.transparentType = value;
                    }
                    break;
                    case EGL_TRANSPARENT_RED_VALUE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.transparentRedValue = value;
                    }
                    break;
                    case EGL_TRANSPARENT_GREEN_VALUE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.transparentGreenValue = value;
                    }
                    break;
                    case EGL_TRANSPARENT_BLUE_VALUE:
                    {
                        if (value != EGL_DONT_CARE && value < 0)
                        {
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;

                            return EGL_FALSE;
                        }

                        config.transparentBlueValue = value;
                    }
                    break;
                    default:
                    {
                        g_localStorage.error = EGL_BAD_ATTRIBUTE;

                        return EGL_FALSE;
                    }
                    }

                    attribListIndex += 2;

                    // More than 28 entries can not exist. A fully populated legal
                    // list ends on exactly 28 * 2, so only more than that is an error.
                    if (attribListIndex > 28 * 2)
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
                EGLConfig    configsOnStack[1024];
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

                    if (config.alphaMaskSize > walkerConfig->alphaMaskSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.alphaSize > walkerConfig->alphaSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.bindToTextureRGB != EGL_DONT_CARE && config.bindToTextureRGB != walkerConfig->bindToTextureRGB)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.bindToTextureRGBA != EGL_DONT_CARE && config.bindToTextureRGBA != walkerConfig->bindToTextureRGBA)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.blueSize > walkerConfig->blueSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.bufferSize > walkerConfig->bufferSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.colorBufferType != EGL_DONT_CARE && config.colorBufferType != walkerConfig->colorBufferType)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.configCaveat != EGL_DONT_CARE && config.configCaveat != walkerConfig->configCaveat)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.configId != EGL_DONT_CARE && config.configId != walkerConfig->configId)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    // EGL_DONT_CARE is -1, so the mask test below could never be
                    // satisfied and would silently match nothing.
                    if (config.conformant != EGL_DONT_CARE && (config.conformant & walkerConfig->conformant) != config.conformant)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.depthSize > walkerConfig->depthSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.greenSize > walkerConfig->greenSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.level != walkerConfig->level)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.luminanceSize > walkerConfig->luminanceSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.matchNativePixmap != EGL_NONE && config.matchNativePixmap != walkerConfig->matchNativePixmap)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.nativeRenderable != EGL_DONT_CARE && config.nativeRenderable != walkerConfig->nativeRenderable)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.maxSwapInterval != EGL_DONT_CARE && config.maxSwapInterval != walkerConfig->maxSwapInterval)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.minSwapInterval != EGL_DONT_CARE && config.minSwapInterval != walkerConfig->minSwapInterval)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.redSize > walkerConfig->redSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.sampleBuffers > walkerConfig->sampleBuffers)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.samples > walkerConfig->samples)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.stencilSize > walkerConfig->stencilSize)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.renderableType != EGL_DONT_CARE && (config.renderableType & walkerConfig->renderableType) != config.renderableType)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.surfaceType != EGL_DONT_CARE && (config.surfaceType & walkerConfig->surfaceType) != config.surfaceType)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (config.transparentType != walkerConfig->transparentType)
                    {
                        walkerConfig = walkerConfig->next;

                        continue;
                    }
                    if (walkerConfig->transparentType == EGL_TRANSPARENT_RGB)
                    {
                        if (config.transparentRedValue != EGL_DONT_CARE && config.transparentRedValue != walkerConfig->transparentRedValue)
                        {
                            walkerConfig = walkerConfig->next;

                            continue;
                        }
                        if (config.transparentGreenValue != EGL_DONT_CARE && config.transparentGreenValue != walkerConfig->transparentGreenValue)
                        {
                            walkerConfig = walkerConfig->next;

                            continue;
                        }
                        if (config.transparentBlueValue != EGL_DONT_CARE && config.transparentBlueValue != walkerConfig->transparentBlueValue)
                        {
                            walkerConfig = walkerConfig->next;

                            continue;
                        }
                    }

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

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
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

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
    }

    EGLBoolean _eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value)
    {
        auto _rl = g_globalStorage.placeRootDpy_readlock();

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

                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    if (reinterpret_cast<EGLConfig>(walkerConfig) == config)
                    {
                        break;
                    }

                    walkerConfig = walkerConfig->next;
                }

                if (!walkerConfig)
                {
                    g_localStorage.error = EGL_BAD_CONFIG;

                    return EGL_FALSE;
                }

                switch (attribute)
                {
                case EGL_ALPHA_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->alphaSize;
                    }
                }
                break;
                case EGL_ALPHA_MASK_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->alphaMaskSize;
                    }
                }
                break;
                case EGL_BIND_TO_TEXTURE_RGB:
                {
                    if (value)
                    {
                        *value = walkerConfig->bindToTextureRGB;
                    }
                }
                break;
                case EGL_BIND_TO_TEXTURE_RGBA:
                {
                    if (value)
                    {
                        *value = walkerConfig->bindToTextureRGBA;
                    }
                }
                break;
                case EGL_BLUE_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->blueSize;
                    }
                }
                break;
                case EGL_BUFFER_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->bufferSize;
                    }
                }
                break;
                case EGL_COLOR_BUFFER_TYPE:
                {
                    if (value)
                    {
                        *value = walkerConfig->colorBufferType;
                    }
                }
                break;
                case EGL_CONFIG_CAVEAT:
                {
                    if (value)
                    {
                        *value = walkerConfig->configCaveat;
                    }
                }
                break;
                case EGL_CONFIG_ID:
                {
                    if (value)
                    {
                        *value = walkerConfig->configId;
                    }
                }
                break;
                case EGL_CONFORMANT:
                {
                    if (value)
                    {
                        *value = walkerConfig->conformant;
                    }
                }
                break;
                case EGL_DEPTH_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->depthSize;
                    }
                }
                break;
                case EGL_GREEN_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->greenSize;
                    }
                }
                break;
                case EGL_LEVEL:
                {
                    if (value)
                    {
                        *value = walkerConfig->level;
                    }
                }
                break;
                case EGL_LUMINANCE_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->luminanceSize;
                    }
                }
                break;
                case EGL_MAX_PBUFFER_WIDTH:
                {
                    if (value)
                    {
                        *value = walkerConfig->maxPBufferWidth;
                    }
                }
                break;
                case EGL_MAX_PBUFFER_HEIGHT:
                {
                    if (value)
                    {
                        *value = walkerConfig->maxPBufferHeight;
                    }
                }
                break;
                case EGL_MAX_PBUFFER_PIXELS:
                {
                    if (value)
                    {
                        *value = walkerConfig->maxPBufferPixels;
                    }
                }
                break;
                case EGL_MAX_SWAP_INTERVAL:
                {
                    if (value)
                    {
                        *value = walkerConfig->maxSwapInterval;
                    }
                }
                break;
                case EGL_MIN_SWAP_INTERVAL:
                {
                    if (value)
                    {
                        *value = walkerConfig->minSwapInterval;
                    }
                }
                break;
                case EGL_NATIVE_RENDERABLE:
                {
                    if (value)
                    {
                        *value = walkerConfig->nativeRenderable;
                    }
                }
                break;
                case EGL_NATIVE_VISUAL_ID:
                {
                    if (value)
                    {
                        *value = walkerConfig->nativeVisualId;
                    }
                }
                break;
                case EGL_NATIVE_VISUAL_TYPE:
                {
                    if (value)
                    {
                        *value = walkerConfig->nativeVisualType;
                    }
                }
                break;
                case EGL_RED_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->redSize;
                    }
                }
                break;
                case EGL_RENDERABLE_TYPE:
                {
                    if (value)
                    {
                        *value = walkerConfig->renderableType;
                    }
                }
                break;
                case EGL_SAMPLE_BUFFERS:
                {
                    if (value)
                    {
                        *value = walkerConfig->sampleBuffers;
                    }
                }
                break;
                case EGL_SAMPLES:
                {
                    if (value)
                    {
                        *value = walkerConfig->samples;
                    }
                }
                break;
                case EGL_STENCIL_SIZE:
                {
                    if (value)
                    {
                        *value = walkerConfig->stencilSize;
                    }
                }
                break;
                case EGL_SURFACE_TYPE:
                {
                    if (value)
                    {
                        *value = walkerConfig->surfaceType;
                    }
                }
                break;
                case EGL_TRANSPARENT_TYPE:
                {
                    if (value)
                    {
                        *value = walkerConfig->transparentType;
                    }
                }
                break;
                case EGL_TRANSPARENT_RED_VALUE:
                {
                    if (value)
                    {
                        *value = walkerConfig->transparentRedValue;
                    }
                }
                break;
                case EGL_TRANSPARENT_GREEN_VALUE:
                {
                    if (value)
                    {
                        *value = walkerConfig->transparentGreenValue;
                    }
                }
                break;
                case EGL_TRANSPARENT_BLUE_VALUE:
                {
                    if (value)
                    {
                        *value = walkerConfig->transparentBlueValue;
                    }
                }
                break;
                default:
                {
                    g_localStorage.error = EGL_BAD_ATTRIBUTE;

                    return EGL_FALSE;
                }
                }

                g_localStorage.error = EGL_SUCCESS;

                return EGL_TRUE;
            }

            walkerDpy = walkerDpy->next;
        }

        g_localStorage.error = EGL_BAD_DISPLAY;

        return EGL_FALSE;
    }

} // extern "C"

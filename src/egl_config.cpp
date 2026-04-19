#include "egl_common.h"
#include <algorithm>

static int _ChooseConfig_sort_predicate(const void* _lhs, const void* _rhs)
{
	const EGLConfigImpl* lhs = *(const EGLConfigImpl**)_lhs;
	const EGLConfigImpl* rhs = *(const EGLConfigImpl**)_rhs;

	if (lhs->configCaveat == rhs->configCaveat)
	{
		if (lhs->colorBufferType == rhs->colorBufferType)
		{
			EGLint color_bits[2] = { 0, 0 };
			switch (lhs->colorBufferType)
			{
			case EGL_RGB_BUFFER:
				color_bits[0] = lhs->redSize + lhs->greenSize + lhs->blueSize + lhs->alphaSize;
				color_bits[1] = rhs->redSize + rhs->greenSize + rhs->blueSize + rhs->alphaSize;
				break;
			case EGL_LUMINANCE_BUFFER:
				color_bits[0] = lhs->luminanceSize + lhs->alphaSize;
				color_bits[1] = rhs->luminanceSize + rhs->alphaSize;
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
									else return (lhs->alphaMaskSize - rhs->alphaMaskSize); // 9. Smaller EGL_ALPHA_MASK_SIZE
								}
								else return (lhs->stencilSize - rhs->stencilSize); // 8. Smaller EGL_STENCIL_SIZE
							}
							else return (lhs->depthSize - rhs->depthSize); // 7. Smaller EGL_DEPTH_SIZE
						}
						else return (lhs->samples - rhs->samples); // 6. Smaller EGL_SAMPLES
					}
					else return (lhs->sampleBuffers - rhs->sampleBuffers); // 5. Smaller EGL_SAMPLE_BUFFERS
				}
				else return (lhs->bufferSize - rhs->bufferSize); // 4. Smaller EGL_BUFFER_SIZE
			}
			else return color_bits[1] - color_bits[0]; // 3. by larger total number of color bits
		}
		else return (lhs->colorBufferType - rhs->colorBufferType); // 2. by EGL_COLOR_BUFFER_TYPE
	}
	else return (lhs->configCaveat - rhs->configCaveat); // 1. by EGL_CONFIG_CAVEAT
}

extern "C"
{

static void _eglInternalSetDontCareConfig(EGLConfigImpl* config)
{
	if (!config)
	{
		return;
	}

	// Set default values

	config->alphaSize = EGL_DONT_CARE;
	config->alphaMaskSize = EGL_DONT_CARE;

	config->bindToTextureRGB = EGL_DONT_CARE;
	config->bindToTextureRGBA = EGL_DONT_CARE;
	config->blueSize = EGL_DONT_CARE;
	config->bufferSize = EGL_DONT_CARE;

	config->colorBufferType = EGL_RGB_BUFFER;
	config->configCaveat = EGL_DONT_CARE;
	config->configId = EGL_DONT_CARE;
	config->conformant = EGL_DONT_CARE;

	config->depthSize = EGL_DONT_CARE;

	config->greenSize = EGL_DONT_CARE;

	config->level = EGL_DONT_CARE;
	config->luminanceSize = EGL_DONT_CARE;

	config->matchNativePixmap = EGL_DONT_CARE;
	config->maxPBufferHeight = EGL_DONT_CARE;
	config->maxPBufferPixels = EGL_DONT_CARE;
	config->maxPBufferWidth = EGL_DONT_CARE;
	config->maxSwapInterval = EGL_DONT_CARE;
	config->minSwapInterval = EGL_DONT_CARE;

	config->nativeRenderable = EGL_DONT_CARE;
	config->nativeVisualId = EGL_DONT_CARE;
	config->nativeVisualType = EGL_DONT_CARE;

	config->redSize = EGL_DONT_CARE;
	config->renderableType = EGL_DONT_CARE;

	config->sampleBuffers = EGL_DONT_CARE;
	config->samples = EGL_DONT_CARE;
	config->stencilSize = EGL_DONT_CARE;
	config->surfaceType = EGL_WINDOW_BIT;

	config->transparentBlueValue = EGL_DONT_CARE;
	config->transparentGreenValue = EGL_DONT_CARE;
	config->transparentRedValue = EGL_DONT_CARE;
	config->transparentType = EGL_DONT_CARE;

	// Following parameters always do care.

	config->drawToWindow = EGL_TRUE;
	config->drawToPixmap = EGL_FALSE;
	config->drawToPBuffer = EGL_FALSE;

	config->doubleBuffer = EGL_TRUE;

	config->next = 0;
}

EGLBoolean _eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	static const EGLint emptyList[] = { EGL_NONE };
	if (!attrib_list)
		attrib_list = emptyList;

	if (!num_config)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

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
					break;
				}

				attribListIndex += 2;

				// More than 28 entries can not exist.
				if (attribListIndex >= 28 * 2)
				{
					g_localStorage.error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			config.drawToWindow = (config.surfaceType & EGL_WINDOW_BIT) ? EGL_TRUE : EGL_FALSE;
			config.drawToPixmap = (config.surfaceType & EGL_PIXMAP_BIT) ? EGL_TRUE : EGL_FALSE;
			config.drawToPBuffer = (config.surfaceType & EGL_PBUFFER_BIT) ? EGL_TRUE : EGL_FALSE;

			// Check, if this configuration exists.
			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			#define stack_mem_sz (1ull << 13) // 8k
			char stack_mem[stack_mem_sz];
			const EGLint max_configs = stack_mem_sz / sizeof(EGLConfig);
			EGLConfig* configsOnStack = (EGLConfig*)stack_mem;

			EGLint configIndex = 0;

			int itercount = 0;
			while (walkerConfig && configIndex < max_configs)
			{
				++itercount;
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
				if ((config.conformant & walkerConfig->conformant) != config.conformant)
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
				if ((config.renderableType & walkerConfig->renderableType) != config.renderableType)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if ((config.surfaceType & walkerConfig->surfaceType) != config.surfaceType)
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

			if (configIndex)
				qsort(configsOnStack, configIndex, sizeof(*configs), &_ChooseConfig_sort_predicate);

			*num_config = configIndex;
			if (configs)
				memcpy(configs, configsOnStack, (std::min)(configIndex, config_size)*sizeof(EGLConfig));

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}
	

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	if (!num_config)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			EGLint configIndex = 0;

			while (walkerConfig)
			{
				if (configs && configIndex < config_size)
					configs[configIndex] = walkerConfig;

				walkerConfig = walkerConfig->next;

				configIndex++;
			}

			*num_config = configIndex;

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	auto _rl = g_globalStorage.placeRootDpy_readlock();

	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			while (walkerConfig)
			{
				if ((EGLConfig)walkerConfig == config)
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
				break;
			}

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

	

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

} // extern "C"

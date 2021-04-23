/**
 * EGL windows desktop implementation.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) since 2014 Norbert Nopper
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <atomic>
#include <thread>
#include "egl_internal.h"

#define EGL_NO_SURFACE_IMPL static_cast<EGLSurfaceImpl*>(EGL_NO_SURFACE)
#define EGL_NO_CONTEXT_IMPL static_cast<EGLContextImpl*>(EGL_NO_CONTEXT)

struct GlobalStorage
{
	EGLDisplayImpl* rootDpy = nullptr;

	void rootDpy_readacq()
	{
		lock_read(lock_dpy);
	}
	void rootDpy_writeacq()
	{
		lock_write(lock_dpy);
	}
	void rootDpy_readrel()
	{
		unlock_read(lock_dpy);
	}
	void rootDpy_writerel()
	{
		unlock_write(lock_dpy);
	}

	auto dummy_read()
	{
		lock_read(lock_dummy);
		auto d = dummy;
		unlock_read(lock_dummy);
		return d;
	}
	void dummy_write(NativeLocalStorageContainer d)
	{
		lock_write(lock_dummy);
		dummy = d;
		unlock_write(lock_dummy);
	}

	struct ReadLock
	{
		ReadLock(GlobalStorage* gs) : parent(gs)
		{
			parent->rootDpy_readacq();
		}
		~ReadLock()
		{
			parent->rootDpy_readrel();
		}

		GlobalStorage* parent;
	};
	struct WriteLock
	{
		WriteLock(GlobalStorage* gs) : parent(gs)
		{
			parent->rootDpy_writeacq();
		}
		~WriteLock()
		{
			parent->rootDpy_writerel();
		}

		GlobalStorage* parent;
	};

	ReadLock  placeRootDpy_readlock() { return this; }
	WriteLock placeRootDpy_writelock() { return this; }

	GlobalStorage()
	{
		memset(&dummy, 0, sizeof(dummy));
	}

private:
	NativeLocalStorageContainer dummy;

	std::atomic_uint32_t lock_dpy = 0u;
	std::atomic_uint32_t lock_dummy = 0u;

	static void lock_read(std::atomic_uint32_t& c) 
	{
		if (++c > LOCK_WRITE_VALUE)
		{
			while (c >= LOCK_WRITE_VALUE)
				std::this_thread::yield();
		}
	}
	static void unlock_read(std::atomic_uint32_t& c)
	{
		--c;
	}
	static void lock_write(std::atomic_uint32_t& c)
	{
		uint32_t expected = 0u;
		while (!c.compare_exchange_strong(expected, LOCK_WRITE_VALUE))
		{
			expected = 0u;
			std::this_thread::yield();
		}
	}
	static void unlock_write(std::atomic_uint32_t& c)
	{
		c -= LOCK_WRITE_VALUE;
	}

	constexpr inline static uint32_t LOCK_WRITE_VALUE = 0xdeadbeefu;
};

typedef std::lock_guard<std::mutex> guard_t;

static thread_local LocalStorage g_localStorage =
    { EGL_SUCCESS, EGL_NONE, EGL_NO_CONTEXT_IMPL };

static GlobalStorage g_globalStorage;

static EGLint g_GL_max_supported_version[2] = { 0, 0 };
static EGLint g_ES_max_supported_version[2] = { 0, 0 };

#if defined(EGL_NO_GLEW)
extern void (*glFinish_PTR)();
#define glFinish(...) glFinish_PTR(__VA_ARGS__)
#endif

extern "C" 
{

static EGLBoolean _eglInternalInit()
{
	auto dummy = g_globalStorage.dummy_read();
	EGLBoolean r = __internalInit(&dummy, g_GL_max_supported_version, g_ES_max_supported_version);
	g_globalStorage.dummy_write(dummy);

	return r;
}

static void _eglInternalTerminate()
{
	auto dummy = g_globalStorage.dummy_read();
	__internalTerminate(&dummy);
	g_globalStorage.dummy_write(dummy);
}

static void _eglInternalCleanup()
{
	EGLDisplayImpl* tempDpy = 0;

	{
		auto _wl = g_globalStorage.placeRootDpy_writelock();
		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		while (walkerDpy)
		{
			EGLSurfaceImpl* tempSurface = 0;

			EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

			EGLContextImpl* tempCtx = 0;

			EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

			while (walkerSurface)
			{
				if (walkerSurface->destroy && walkerSurface != walkerDpy->currentDraw && walkerSurface != walkerDpy->currentRead)
				{
					EGLSurfaceImpl* deleteSurface = walkerSurface;

					if (tempSurface == 0)
					{
						walkerDpy->rootSurface = deleteSurface->next;

						walkerSurface = walkerDpy->rootSurface;
					}
					else
					{
						tempSurface->next = deleteSurface->next;

						walkerSurface = tempSurface;
					}

					free(deleteSurface);
				}

				tempSurface = walkerSurface;

				if (walkerSurface)
					walkerSurface = walkerSurface->next;
			}

			while (walkerCtx)
			{
				// Avoid deleting of a shared context.
				/*EGLContextImpl* innerWalkerCtx = walkerDpy->rootCtx;
				while (innerWalkerCtx)
				{
					if (innerWalkerCtx->sharedCtx == walkerCtx)
					{
						continue;
					}

					innerWalkerCtx = innerWalkerCtx->next;
				}*/

				if (walkerCtx->destroy && walkerCtx != walkerDpy->currentCtx && walkerCtx != g_localStorage.currentCtx)
				{
					EGLContextImpl* deleteCtx = walkerCtx;

					if (tempCtx == 0)
					{
						walkerDpy->rootCtx = deleteCtx->next;

						walkerCtx = walkerDpy->rootCtx;
					}
					else
					{
						tempCtx->next = deleteCtx->next;

						walkerCtx = tempCtx;
					}

					// Freeing the context.
					while (deleteCtx->rootCtxList)
					{
						EGLContextListImpl* deleteCtxList = deleteCtx->rootCtxList;

						deleteCtx->rootCtxList = deleteCtx->rootCtxList->next;

						__deleteContext(walkerDpy, &deleteCtxList->nativeContextContainer);

						free(deleteCtxList);
					}

					free(deleteCtx);
				}

				tempCtx = walkerCtx;

				if (walkerCtx)
				{
					walkerCtx = walkerCtx->next;
				}
			}

			if (walkerDpy->destroy)
			{
				if (walkerDpy->rootSurface == 0 && walkerDpy->rootCtx == 0 && walkerDpy->currentDraw == EGL_NO_SURFACE && walkerDpy->currentRead == EGL_NO_SURFACE && walkerDpy->currentCtx == EGL_NO_CONTEXT)
				{
					EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

					EGLConfigImpl* deleteConfig;

					while (walkerConfig)
					{
						deleteConfig = walkerConfig;

						walkerConfig = walkerConfig->next;

						free(deleteConfig);
					}
					walkerDpy->rootConfig = 0;

					//

					EGLDisplayImpl* deleteDpy = walkerDpy;

					if (tempDpy == 0)
					{
						g_globalStorage.rootDpy = deleteDpy->next;

						walkerDpy = g_globalStorage.rootDpy;
					}
					else
					{
						tempDpy->next = deleteDpy->next;

						walkerDpy = tempDpy;
					}

					delete deleteDpy;
				}
			}

			tempDpy = walkerDpy;

			if (walkerDpy)
			{
				walkerDpy = walkerDpy->next;
			}
		}
	}

	if (!g_globalStorage.rootDpy)
	{
		_eglInternalTerminate();
	}
}

void _eglInternalSetDefaultConfig(EGLConfigImpl* config)
{
	if (!config)
	{
		return;
	}

	config->alphaSize = 0;
	config->alphaMaskSize = 0;

	config->bindToTextureRGB = EGL_DONT_CARE;
	config->bindToTextureRGBA = EGL_DONT_CARE;
	config->blueSize = 0;
	config->bufferSize = 0;

	config->colorBufferType = EGL_DONT_CARE;
	config->configCaveat = EGL_DONT_CARE;
	config->configId = EGL_DONT_CARE;
	config->conformant = 0;

	config->depthSize = 0;

	config->greenSize = 0;

	config->level = 0;
	config->luminanceSize = 0;

	config->matchNativePixmap = EGL_NONE;
	config->maxPBufferHeight = EGL_DONT_CARE;
	config->maxPBufferPixels = EGL_DONT_CARE;
	config->maxPBufferWidth = EGL_DONT_CARE;
	config->maxSwapInterval = EGL_DONT_CARE;
	config->minSwapInterval = EGL_DONT_CARE;

	config->nativeRenderable = EGL_DONT_CARE;
	config->nativeVisualId = 0;
	config->nativeVisualType = EGL_NONE;

	config->redSize = 0;
	config->renderableType = EGL_OPENGL_ES_BIT;

	config->sampleBuffers = 0;
	config->samples = 0;
	config->stencilSize = 0;
	config->surfaceType = EGL_WINDOW_BIT;

	config->transparentBlueValue = EGL_DONT_CARE;
	config->transparentGreenValue = EGL_DONT_CARE;
	config->transparentRedValue = EGL_DONT_CARE;
	config->transparentType = EGL_NONE;

	//

	config->drawToWindow = EGL_TRUE;
	config->drawToPixmap = EGL_FALSE;
	config->drawToPBuffer = EGL_FALSE;
	config->doubleBuffer = EGL_TRUE;

	config->next = 0;
}

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

//
// EGL_VERSION_1_0
//

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

EGLBoolean _eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	if (!attrib_list)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	if (!configs)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	if (config_size == 0)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

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
						if (value != EGL_DONT_CARE && value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

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
						if (value != EGL_DONT_CARE && value != EGL_NONE && value != EGL_TRANSPARENT_TYPE)
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
				if ((config.stencilSize != EGL_DONT_CARE) && (config.stencilSize != walkerConfig->stencilSize))
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

				//
				/*
				if (config.drawToWindow != walkerConfig->drawToWindow)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				if (config.drawToPixmap != walkerConfig->drawToPixmap)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				if (config.drawToPBuffer != walkerConfig->drawToPBuffer)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				*/

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

			*num_config = (std::min)(configIndex, config_size);
			memcpy(configs, configsOnStack, *num_config*sizeof(EGLConfig));

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}
	

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

// comment this to bring back original code
#define FIXED_SHARE_CONTEXT
EGLContext _eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	if (!attrib_list)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_NO_CONTEXT;
	}

	if (g_localStorage.api == EGL_NONE)
	{
		g_localStorage.error = EGL_BAD_MATCH;

		return EGL_NO_CONTEXT;
	}

	EGLint requested_version[2]{ 1, 0 };
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
		if (requested_version[0] > g_GL_max_supported_version[0] || requested_version[1] > g_GL_max_supported_version[1])
			return EGL_NO_CONTEXT;
	}
	else if (g_localStorage.api == EGL_OPENGL_ES_API)
	{
		if (requested_version[0] > g_ES_max_supported_version[0] || requested_version[1] > g_ES_max_supported_version[1])
			return EGL_NO_CONTEXT;
	}
	else
	{
		return EGL_NO_CONTEXT;
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

			while (walkerConfig)
			{
				if ((EGLConfig)walkerConfig == config)
				{
					EGLint target_attrib_list[CONTEXT_ATTRIB_LIST_SIZE];

					if (g_localStorage.api == EGL_OPENGL_ES_API && (walkerConfig->conformant & EGL_OPENGL_ES3_BIT) == 0)
					{
						return EGL_FALSE;
					}
					if (!__processAttribList(g_localStorage.api, target_attrib_list, attrib_list, &g_localStorage.error))
					{
						return EGL_FALSE;
					}

					EGLContextImpl* sharedCtx = 0;

					if (share_context != EGL_NO_CONTEXT)
					{
						EGLDisplayImpl* sharedWalkerDpy = g_globalStorage.rootDpy;

						while (sharedWalkerDpy)
						{
							EGLContextImpl* sharedWalkerCtx = sharedWalkerDpy->rootCtx;

							while (sharedWalkerCtx)
							{
								if ((EGLContext)sharedWalkerCtx == share_context)
								{
									if (!sharedWalkerCtx->initialized || sharedWalkerCtx->destroy)
									{
										g_localStorage.error = EGL_BAD_CONTEXT;

										return EGL_FALSE;
									}

									sharedCtx = sharedWalkerCtx;

									break;
								}

								sharedWalkerCtx = sharedWalkerCtx->next;
							}

							sharedWalkerDpy = sharedWalkerDpy->next;
						}

						if (!sharedCtx)
						{
							g_localStorage.error = EGL_BAD_CONTEXT;

							return EGL_FALSE;
						}
					}

					EGLContextImpl* newCtx = (EGLContextImpl*)malloc(sizeof(EGLContextImpl));

					if (!newCtx)
					{
						g_localStorage.error = EGL_BAD_ALLOC;

						return EGL_FALSE;
					}

					// Move the atttibutes for later creation.
					memcpy(newCtx->attribList, target_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

					newCtx->initialized = EGL_TRUE;
					newCtx->destroy = EGL_FALSE;
					newCtx->configId = walkerConfig->configId;
					newCtx->sharedCtx = sharedCtx;
					newCtx->rootCtxList = 0;

					newCtx->next = walkerDpy->rootCtx;
					walkerDpy->rootCtx = newCtx;

					return (EGLContext)newCtx;
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

EGLSurface _eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list)
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

				return EGL_NO_SURFACE;
			}

			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			while (walkerConfig)
			{
				if ((EGLConfig)walkerConfig == config)
				{
					EGLSurfaceImpl* newSurface = (EGLSurfaceImpl*)malloc(sizeof(EGLSurfaceImpl));

					if (!newSurface)
					{
						g_localStorage.error = EGL_BAD_ALLOC;

						return EGL_NO_SURFACE;
					}

					if (!__createPbufferSurface(newSurface, attrib_list, walkerDpy, walkerConfig, &g_localStorage.error))
					{
						free(newSurface);

						return EGL_NO_SURFACE;
					}

					newSurface->next = walkerDpy->rootSurface;

					walkerDpy->rootSurface = newSurface;

					return (EGLSurface)newSurface;
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

EGLSurface _eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
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

				return EGL_NO_SURFACE;
			}

			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			while (walkerConfig)
			{
				if ((EGLConfig)walkerConfig == config)
				{
					EGLSurfaceImpl* newSurface = (EGLSurfaceImpl*)malloc(sizeof(EGLSurfaceImpl));

					if (!newSurface)
					{
						g_localStorage.error = EGL_BAD_ALLOC;

						return EGL_NO_SURFACE;
					}

					if (!__createWindowSurface(newSurface, win, attrib_list, walkerDpy, walkerConfig, &g_localStorage.error))
					{
						free(newSurface);

						return EGL_NO_SURFACE;
					}

					newSurface->next = walkerDpy->rootSurface;

					walkerDpy->rootSurface = newSurface;

					return (EGLSurface)newSurface;
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

EGLBoolean _eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	EGLBoolean success = EGL_FALSE;
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

				EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

				while (walkerCtx)
				{
					if ((EGLContext)walkerCtx == ctx)
					{
						if (!walkerCtx->initialized || walkerCtx->destroy)
						{
							g_localStorage.error = EGL_BAD_CONTEXT;

							return EGL_FALSE;
						}

						walkerCtx->initialized = EGL_FALSE;
						walkerCtx->destroy = EGL_TRUE;

						success = EGL_TRUE;
						break;
					}

					walkerCtx = walkerCtx->next;
				}

				g_localStorage.error = EGL_BAD_CONTEXT;

				if (!success)
					return EGL_FALSE;
			}

			walkerDpy = walkerDpy->next;
		}
	}
	
	if (success)
		_eglInternalCleanup();

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	EGLBoolean success = EGL_FALSE;

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

				EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

				while (walkerSurface)
				{
					if ((EGLSurface)walkerSurface == surface)
					{
						if (!walkerSurface->initialized || walkerSurface->destroy)
						{
							g_localStorage.error = EGL_BAD_SURFACE;

							return EGL_FALSE;
						}

						walkerSurface->initialized = EGL_FALSE;
						walkerSurface->destroy = EGL_TRUE;

						__destroySurface(walkerDpy->display_id, walkerSurface);

						success = EGL_TRUE;
						break;
					}

					walkerSurface = walkerSurface->next;
				}

				g_localStorage.error = EGL_BAD_SURFACE;

				if (!success)
					return EGL_FALSE;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	if (success)
		_eglInternalCleanup();

	g_localStorage.error = EGL_BAD_DISPLAY;

	return success;
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

EGLBoolean _eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	if (!configs)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	if (config_size == 0)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

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

			while (walkerConfig && configIndex < config_size)
			{
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

EGLDisplay _eglGetCurrentDisplay(void)
{
	if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
	{
		return EGL_NO_DISPLAY;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
			return (EGLDisplay)walkerDpy;
		}

		walkerDpy = walkerDpy->next;
	}
	

	return EGL_NO_DISPLAY;
}

EGLSurface _eglGetCurrentSurface(EGLint readdraw)
{
	if (g_localStorage.currentCtx == EGL_NO_CONTEXT)
	{
		return EGL_NO_SURFACE;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
			if (readdraw == EGL_DRAW)
			{
				return (EGLSurface)walkerDpy->currentDraw;
			}
			else if (readdraw == EGL_READ)
			{
				return (EGLSurface)walkerDpy->currentRead;
			}

			return EGL_NO_SURFACE;
		}

		walkerDpy = walkerDpy->next;
	}

	return EGL_NO_SURFACE;
}

EGLDisplay _eglGetDisplay(EGLNativeDisplayType display_id)
{
	if (!_eglInternalInit())
	{
		return EGL_NO_DISPLAY;
	}

	//
	{
		auto _rl = g_globalStorage.placeRootDpy_readlock();

		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		while (walkerDpy)
		{
			if (walkerDpy->display_id == display_id)
			{
				return (EGLDisplay)walkerDpy;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	EGLDisplayImpl* newDpy = new EGLDisplayImpl();

	if (!newDpy)
	{
		return EGL_NO_DISPLAY;
	}

	newDpy->initialized = EGL_FALSE;
	newDpy->destroy = EGL_FALSE;
#if defined(_WIN32) || defined(_WIN64)
	newDpy->display_id = display_id ? display_id : g_globalStorage.dummy_read().hdc;
#elif defined(__ANDROID__) || defined(ANDROID) || defined(WL_EGL_PLATFORM)
	newDpy->display_id = 0;
#else
	newDpy->display_id = display_id ? display_id : g_globalStorage.dummy_read().display;
#endif
	newDpy->rootSurface = 0;
	newDpy->rootCtx = 0;
	newDpy->rootConfig = 0;
	newDpy->currentDraw = EGL_NO_SURFACE_IMPL;
	newDpy->currentRead = EGL_NO_SURFACE_IMPL;
	newDpy->currentCtx = EGL_NO_CONTEXT_IMPL;
	newDpy->next = g_globalStorage.rootDpy;

	auto _wl = g_globalStorage.placeRootDpy_writelock();
	g_globalStorage.rootDpy = newDpy;

	return newDpy;
}

EGLint _eglGetError(void)
{
	EGLint currentError = g_localStorage.error;

	g_localStorage.error = EGL_SUCCESS;

	return currentError;
}

__eglMustCastToProperFunctionPointerType _eglGetProcAddress(const char *procname)
{
	return __getProcAddress(procname);
}

EGLBoolean _eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			guard_t _{ walkerDpy->mutex };

			if (walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			{
				auto dummy = g_globalStorage.dummy_read();
				EGLBoolean fail = (!walkerDpy->initialized && !__initialize(walkerDpy, &dummy, &g_localStorage.error));
				g_globalStorage.dummy_write(dummy);
				if (fail)
				{
					return EGL_FALSE;
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

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
	EGLBoolean success = EGL_FALSE;
	{
		auto _rl = g_globalStorage.placeRootDpy_readlock();
		EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

		if ((ctx == EGL_NO_CONTEXT && (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE)) || (ctx != EGL_NO_CONTEXT && (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE)))
		{
			g_localStorage.error = EGL_BAD_MATCH;

			return EGL_FALSE;
		}

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

				EGLSurfaceImpl* currentDraw = EGL_NO_SURFACE_IMPL;
				EGLSurfaceImpl* currentRead = EGL_NO_SURFACE_IMPL;
				EGLContextImpl* currentCtx = EGL_NO_CONTEXT_IMPL;

				NativeSurfaceContainer* nativeSurfaceContainer = 0;
				NativeContextContainer* nativeContextContainer = 0;

				EGLBoolean result;

				if (draw != EGL_NO_SURFACE)
				{
					EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

					while (walkerSurface)
					{
						if ((EGLSurface)walkerSurface == draw)
						{
							if (!walkerSurface->initialized || walkerSurface->destroy)
							{
								g_localStorage.error = EGL_BAD_NATIVE_WINDOW;

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
						if ((EGLSurface)walkerSurface == read)
						{
							if (!walkerSurface->initialized || walkerSurface->destroy)
							{
								g_localStorage.error = EGL_BAD_NATIVE_WINDOW;

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
						if ((EGLContext)walkerCtx == ctx)
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
						ctxList = (EGLContextListImpl*)malloc(sizeof(EGLContextListImpl));

						if (!ctxList)
						{
							return EGL_FALSE;
						}

						// Gather shared context, if one exists.
						EGLContextListImpl* sharedCtxList = 0;
						if (currentCtx->sharedCtx)
						{
							EGLContextImpl* sharedWalkerCtx = currentCtx->sharedCtx;
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
								sharedWalkerCtx = sharedWalkerCtx->sharedCtx;

								// No created shared context found.
								if (!sharedWalkerCtx)
								{
									sharedCtxList = (EGLContextListImpl*)malloc(sizeof(EGLContextListImpl));

									if (!sharedCtxList)
									{
										free(ctxList);

										return EGL_FALSE;
									}

									result = __createContext(&sharedCtxList->nativeContextContainer, walkerDpy, &currentDraw->nativeSurfaceContainer, 0, beforeSharedWalkerCtx->attribList);

									if (!result)
									{
										free(sharedCtxList);

										free(ctxList);

										return EGL_FALSE;
									}

									sharedCtxList->surface = currentDraw;

									sharedCtxList->next = beforeSharedWalkerCtx->rootCtxList;
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

							return EGL_FALSE;
						}

						ctxList->surface = currentDraw;

						ctxList->next = currentCtx->rootCtxList;
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
				walkerDpy->currentCtx = currentCtx;

				g_localStorage.currentCtx = currentCtx;

				break; // break displays loop
			}

			walkerDpy = walkerDpy->next;
		}
	}

	if (success)
		_eglInternalCleanup();

	g_localStorage.error = EGL_BAD_DISPLAY;

	return success;
}

EGLBoolean _eglQueryContext (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
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

			EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

			while (walkerCtx)
			{
				if ((EGLContext)walkerCtx == ctx)
				{
					if (!walkerCtx->initialized || walkerCtx->destroy)
					{
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

							return EGL_TRUE;
						}
						break;
						case EGL_CONTEXT_CLIENT_TYPE:
						{
							if (value)
							{
								*value = EGL_OPENGL_API;
							}

							return EGL_TRUE;
						}
						break;
						case EGL_CONTEXT_CLIENT_VERSION:
						{
							// Regarding the specification, it only makes sense for OpenGL ES.

							return EGL_FALSE;
						}
						break;
						case EGL_RENDER_BUFFER:
						{
							if (walkerDpy->currentCtx == walkerCtx)
							{
								EGLSurfaceImpl* currentSurface = walkerDpy->currentDraw ? walkerDpy->currentDraw : walkerDpy->currentRead;

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

								if (value)
								{
									*value = EGL_NONE;
								}

								return EGL_FALSE;
							}
							else
							{
								if (value)
								{
									*value = EGL_NONE;
								}

								return EGL_FALSE;
							}
						}
						break;
					}

					g_localStorage.error = EGL_BAD_PARAMETER;

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

const char *_eglQueryString(EGLDisplay dpy, EGLint name)
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

				return 0;
			}

			switch (name)
			{
				case EGL_CLIENT_APIS:
				{
					return "EGL_OPENGL_API";
				}
				break;
				case EGL_VENDOR:
				{
					return _EGL_VENDOR;
				}
				break;
				case EGL_VERSION:
				{
					return _EGL_VERSION;
				}
				break;
				case EGL_EXTENSIONS:
				{
					return "";
				}
				break;
			}

			g_localStorage.error = EGL_BAD_PARAMETER;

			return 0;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return 0;
}

EGLBoolean _eglQuerySurface (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	// TODO Implement querying a surface.

	return EGL_FALSE;
}

EGLBoolean _eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
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

				return 0;
			}

			EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

			while (walkerSurface)
			{
				if ((EGLSurface)walkerSurface == surface)
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

EGLBoolean _eglTerminate(EGLDisplay dpy)
{
	EGLBoolean success = EGL_FALSE;
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
					g_localStorage.error = EGL_BAD_DISPLAY;

					return EGL_FALSE;
				}

				walkerDpy->initialized = EGL_FALSE;
				walkerDpy->destroy = EGL_TRUE;

				success = EGL_TRUE;
			}

			walkerDpy = walkerDpy->next;
		}
	}

	if (success)
		_eglInternalCleanup();

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}



EGLBoolean _eglWaitNative(EGLint engine)
{
	if (engine != EGL_CORE_NATIVE_ENGINE)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
			guard_t _{ walkerDpy->mutex };

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

	if (g_localStorage.api == EGL_OPENGL_API)
	{
		glFinish();
	}

	return EGL_TRUE;
}

//
// EGL_VERSION_1_1
//

EGLBoolean _eglSwapInterval(EGLDisplay dpy, EGLint interval)
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
				g_localStorage.error = EGL_BAD_DISPLAY;

				return EGL_FALSE;
			}

			if (walkerDpy->currentDraw == EGL_NO_SURFACE || walkerDpy->currentRead == EGL_NO_SURFACE)
			{
				g_localStorage.error = EGL_BAD_SURFACE;

				return EGL_FALSE;
			}

			if (walkerDpy->currentCtx == EGL_NO_CONTEXT)
			{
				g_localStorage.error = EGL_BAD_CONTEXT;

				return EGL_FALSE;
			}

			return __swapInterval(walkerDpy, interval);
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

//
// EGL_VERSION_1_2
//

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

	auto _rl = g_globalStorage.placeRootDpy_readlock();
	EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
			guard_t _{ walkerDpy->mutex };

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

	if (g_localStorage.api == EGL_OPENGL_API)
	{
		glFinish();
	}

	return EGL_TRUE;
}

//
// EGL_VERSION_1_3
//

//
// EGL_VERSION_1_4
//

EGLContext _eglGetCurrentContext(void)
{
	return g_localStorage.currentCtx;
}

//
// EGL_VERSION_1_5
//

//
// non-standard stuff
//

/*
EGLBoolean _eglGetPlatformDependentHandles(void* out, EGLDisplay dpy, EGLSurface surface, EGLContext ctx)
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

			EGLSurfaceImpl* currentDraw = EGL_NO_SURFACE_IMPL;
			EGLContextImpl* currentCtx = EGL_NO_CONTEXT_IMPL;

			NativeSurfaceContainer* nativeSurfaceContainer = 0;
			NativeContextContainer* nativeContextContainer = 0;

			if (surface != EGL_NO_SURFACE)
			{
				EGLSurfaceImpl* walkerSurface = walkerDpy->rootSurface;

				while (walkerSurface)
				{
					if ((EGLSurface)walkerSurface == surface)
					{
						if (!walkerSurface->initialized || walkerSurface->destroy)
						{
							g_localStorage.error = EGL_BAD_NATIVE_WINDOW;

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
			else
			{
				g_localStorage.error = EGL_BAD_SURFACE;

				return EGL_FALSE;
			}

			if (ctx != EGL_NO_CONTEXT)
			{
				EGLContextImpl* walkerCtx = walkerDpy->rootCtx;

				while (walkerCtx)
				{
					if ((EGLContext)walkerCtx == ctx)
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
			else
			{
				g_localStorage.error = EGL_BAD_CONTEXT;

				return EGL_FALSE;
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

				nativeContextContainer = &ctxList->nativeContextContainer;
			}

			if (nativeSurfaceContainer && nativeContextContainer)
			{
				return __getPlatformDependentHandles(out, walkerDpy, nativeSurfaceContainer, nativeContextContainer);
			}
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}
*/

} // extern "C"
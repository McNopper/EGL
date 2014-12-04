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

#include <stdlib.h>
#include <string.h>

#include <X11/X.h>

#include <GL/glew.h>
#include <GL/glxew.h>

#include <EGL/egl.h>

#include "egl_internal.h"

#define GLX_ATTRIB_LIST_SIZE 13

typedef struct _EGLConfigImpl
{

	// Returns the number of bits in the alpha mask buffer.
	EGLint alphaMaskSize;

	// Returns the number of bits of alpha stored in the color buffer.
	EGLint alphaSize;

	// Returns EGL_TRUE if color buffers can be bound to an RGB texture, EGL_FALSE otherwise.
	EGLint bindToTextureRGB;

	// Returns EGL_TRUE if color buffers can be bound to an RGBA texture, EGL_FALSE otherwise.
	EGLint bindToTextureRGBA;

	// Returns the number of bits of blue stored in the color buffer.
	EGLint blueSize;

	// Returns the depth of the color buffer. It is the sum of EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, and EGL_ALPHA_SIZE.
	EGLint bufferSize;

	// Returns the color buffer type. Possible types are EGL_RGB_BUFFER and EGL_LUMINANCE_BUFFER.
	EGLint colorBufferType;

	// Returns the caveats for the frame buffer configuration. Possible caveat values are EGL_NONE, EGL_SLOW_CONFIG, and EGL_NON_CONFORMANT.
	EGLint configCaveat;

	// Returns the ID of the frame buffer configuration.
	EGLint configId;

	// Returns a bitmask indicating which client API contexts created with respect to this config are conformant.
	EGLint conformant;

	// Returns the number of bits in the depth buffer.
	EGLint depthSize;

	// Returns the number of bits of green stored in the color buffer.
	EGLint greenSize;

	// Returns the frame buffer level. Level zero is the default frame buffer. Positive levels correspond to frame buffers that overlay the default buffer and negative levels correspond to frame buffers that underlay the default buffer.
	EGLint level;

	// Returns the number of bits of luminance stored in the luminance buffer.
	EGLint luminanceSize;

	// Input only: Must be followed by the handle of a valid native pixmap, cast to EGLint, or EGL_NONE.
	EGLint matchNativePixmap;

	// Returns the maximum height of a pixel buffer surface in pixels.
	EGLint maxPBufferHeight;

	// Returns the maximum size of a pixel buffer surface in pixels.
	EGLint maxPBufferPixels;

	// Returns the maximum width of a pixel buffer surface in pixels.
	EGLint maxPBufferWidth;

	// Returns the maximum value that can be passed to eglSwapInterval.
	EGLint maxSwapInterval;

	// Returns the minimum value that can be passed to eglSwapInterval.
	EGLint minSwapInterval;

	// Returns EGL_TRUE if native rendering APIs can render into the surface, EGL_FALSE otherwise.
	EGLint nativeRenderable;

	// Returns the ID of the associated native visual.
	EGLint nativeVisualId;

	// Returns the type of the associated native visual.
	EGLint nativeVisualType;

	// Returns the number of bits of red stored in the color buffer.
	EGLint redSize;

	// Returns a bitmask indicating the types of supported client API contexts.
	EGLint renderableType;

	// Returns the number of multisample buffers.
	EGLint sampleBuffers;

	// Returns the number of samples per pixel.
	EGLint samples;

	// Returns the number of bits in the stencil buffer.
	EGLint stencilSize;

	// Returns a bitmask indicating the types of supported EGL surfaces.
	EGLint surfaceType;

	// Returns the transparent blue value.
	EGLint transparentBlueValue;

	// Returns the transparent green value.
	EGLint transparentGreenValue;

	// Returns the transparent red value.
	EGLint transparentRedValue;

	// Returns the type of supported transparency. Possible transparency values are: EGL_NONE, and EGL_TRANSPARENT_RGB.
	EGLint transparentType;

	// Own data.

	EGLint drawToWindow;
	EGLint drawToPixmap;
	EGLint drawToPBuffer;
	EGLint doubleBuffer;

	struct _EGLConfigImpl* next;

} EGLConfigImpl;

typedef struct _EGLSurfaceImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLBoolean drawToWindow;
	EGLBoolean drawToPixmap;
	EGLBoolean drawToPBuffer;
	EGLBoolean doubleBuffer;
	EGLint configId;

	EGLNativeWindowType win;
	GLXFBConfig config;

	struct _EGLSurfaceImpl* next;

} EGLSurfaceImpl;

typedef struct _GLXContextImpl
{

	EGLSurfaceImpl* surface;

	GLXContext ctx;

	struct _GLXContextImpl* next;

} GLXContextImpl;

typedef struct _EGLContextImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLint configId;

	struct _EGLContextImpl* sharedCtx;

	GLXContextImpl* rootGlxCtx;

	EGLint glxAttribList[GLX_ATTRIB_LIST_SIZE];

	struct _EGLContextImpl* next;

} EGLContextImpl;

typedef struct _EGLDisplayImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLNativeDisplayType display_id;

	EGLSurfaceImpl* rootSurface;
	EGLContextImpl* rootCtx;
	EGLConfigImpl* rootConfig;

	EGLSurfaceImpl* currentDraw;
	EGLSurfaceImpl* currentRead;
	EGLContextImpl* currentCtx;

	struct _EGLDisplayImpl* next;

} EGLDisplayImpl;

typedef struct _LocalStorage
{

	EGLNativeDisplayType dummyDisplay;

	EGLNativeWindowType dummyWindow;

	GLXContext dummyCtx;

	EGLint error;

	EGLenum api;

	EGLDisplayImpl* rootDpy;

	EGLContextImpl* currentCtx;

} LocalStorage;

static __thread LocalStorage g_localStorage = {0, 0, 0, EGL_SUCCESS, EGL_NONE, 0, EGL_NO_CONTEXT };

static EGLBoolean _eglInternalInit()
{
	if (g_localStorage.dummyDisplay && g_localStorage.dummyWindow && g_localStorage.dummyCtx)
	{
		return EGL_TRUE;
	}

	if (g_localStorage.dummyDisplay)
	{
		return EGL_FALSE;
	}

	if (g_localStorage.dummyWindow)
	{
		return EGL_FALSE;
	}

	if (g_localStorage.dummyCtx)
	{
		return EGL_FALSE;
	}

	g_localStorage.dummyDisplay = XOpenDisplay(NULL);

	if (!g_localStorage.dummyDisplay)
	{
		return EGL_FALSE;
	}

	g_localStorage.dummyWindow = DefaultRootWindow(g_localStorage.dummyDisplay);

	if (!g_localStorage.dummyWindow)
	{
		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;

		return EGL_FALSE;
	}

	int dummyAttribList[] = {
		GLX_USE_GL, True,
		GLX_DOUBLEBUFFER, True,
		GLX_RGBA, True,
		None
	};

	XVisualInfo* visualInfo = glXChooseVisual(g_localStorage.dummyDisplay, 0, dummyAttribList);

	if (!visualInfo)
	{
		g_localStorage.dummyWindow = 0;

		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;

		return EGL_FALSE;
	}

	g_localStorage.dummyCtx = glXCreateContext(g_localStorage.dummyDisplay, visualInfo, NULL, True);

	if (!g_localStorage.dummyCtx)
	{
		g_localStorage.dummyWindow = 0;

		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;

		return EGL_FALSE;
	}

	if (!glXMakeCurrent(g_localStorage.dummyDisplay, g_localStorage.dummyWindow, g_localStorage.dummyCtx))
	{
		glXDestroyContext(g_localStorage.dummyDisplay, g_localStorage.dummyCtx);
		g_localStorage.dummyCtx = 0;

		g_localStorage.dummyWindow = 0;

		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;

		return EGL_FALSE;
	}

	glewExperimental = GL_TRUE;
	if (glewInit() != GL_NO_ERROR)
	{
		glXMakeCurrent(g_localStorage.dummyDisplay, 0, 0);

		glXDestroyContext(g_localStorage.dummyDisplay, g_localStorage.dummyCtx);
		g_localStorage.dummyCtx = 0;

		g_localStorage.dummyWindow = 0;

		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;

		return EGL_FALSE;
	}

	return EGL_TRUE;
}

static void _eglInternalTerminate()
{
	if (g_localStorage.dummyDisplay)
	{
		glXMakeContextCurrent(g_localStorage.dummyDisplay, 0, 0, 0);
	}

	if (g_localStorage.dummyDisplay && g_localStorage.dummyCtx)
	{
		glXDestroyContext(g_localStorage.dummyDisplay, g_localStorage.dummyCtx);
		g_localStorage.dummyCtx = 0;
	}

	if (g_localStorage.dummyWindow)
	{
		g_localStorage.dummyWindow = 0;
	}

	if (g_localStorage.dummyDisplay)
	{
		XCloseDisplay(g_localStorage.dummyDisplay);
		g_localStorage.dummyDisplay = 0;
	}
}

static void _eglInternalCleanup()
{
	EGLDisplayImpl* tempDpy = 0;

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

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
			{
				walkerSurface = walkerSurface->next;
			}
		}

		while (walkerCtx)
		{
			// Avoid deleting of a shared context.
			EGLContextImpl* innerWalkerCtx = walkerDpy->rootCtx;
			while (innerWalkerCtx)
			{
				if (innerWalkerCtx->sharedCtx == walkerCtx)
				{
					continue;
				}

				innerWalkerCtx = innerWalkerCtx->next;
			}

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
				while (deleteCtx->rootGlxCtx)
				{
					GLXContextImpl* deleteGlxCtx = deleteCtx->rootGlxCtx;

					deleteCtx->rootGlxCtx = deleteCtx->rootGlxCtx->next;

					glXDestroyContext(walkerDpy->display_id, deleteGlxCtx->ctx);

					free(deleteGlxCtx);
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
					g_localStorage.rootDpy = deleteDpy->next;

					walkerDpy = g_localStorage.rootDpy;
				}
				else
				{
					tempDpy->next = deleteDpy->next;

					walkerDpy = tempDpy;
				}

				free(deleteDpy);
			}
		}

		tempDpy = walkerDpy;

		if (walkerDpy)
		{
			walkerDpy = walkerDpy->next;
		}
	}

	if (!g_localStorage.rootDpy)
	{
		_eglInternalTerminate();
	}
}

static void _eglInternalSetDefaultConfig(EGLConfigImpl* config)
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

	config->alphaSize = EGL_DONT_CARE;
	config->alphaMaskSize = EGL_DONT_CARE;

	config->bindToTextureRGB = EGL_DONT_CARE;
	config->bindToTextureRGBA = EGL_DONT_CARE;
	config->blueSize = EGL_DONT_CARE;
	config->bufferSize = EGL_DONT_CARE;

	config->colorBufferType = EGL_DONT_CARE;
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
	config->surfaceType = EGL_DONT_CARE;

	config->transparentBlueValue = EGL_DONT_CARE;
	config->transparentGreenValue = EGL_DONT_CARE;
	config->transparentRedValue = EGL_DONT_CARE;
	config->transparentType = EGL_DONT_CARE;

	// Following parameters always do care.

	config->drawToWindow = EGL_TRUE;
	config->drawToPixmap = EGL_DONT_CARE;
	config->drawToPBuffer = EGL_DONT_CARE;

	config->doubleBuffer = EGL_TRUE;

	config->next = 0;
}

//
// EGL_VERSION_1_0
//

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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			EGLint attribListIndex = 0;

			EGLConfigImpl config;

			_eglInternalSetDontCareConfig(&config);

			while (attrib_list[attribListIndex] != EGL_NONE)
			{
				EGLint value = attrib_list[attribListIndex + 1];

				switch (attrib_list[attribListIndex])
				{
					case EGL_ALPHA_MASK_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.alphaMaskSize = value;
					}
					break;
					case EGL_ALPHA_SIZE:
					{
						if (value < 0)
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
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.blueSize = value;
					}
					break;
					case EGL_BUFFER_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.bufferSize = value;
					}
					break;
					case EGL_COLOR_BUFFER_TYPE:
					{
						if (value != EGL_RGB_BUFFER && value != EGL_LUMINANCE_BUFFER)
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
						if (value & ~(EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT | EGL_OPENVG_BIT))
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.conformant = value;
					}
					break;
					case EGL_DEPTH_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.depthSize = value;
					}
					break;
					case EGL_GREEN_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.greenSize = value;
					}
					break;
					case EGL_LEVEL:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.level = value;
					}
					break;
					case EGL_LUMINANCE_SIZE:
					{
						if (value < 0)
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
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.maxSwapInterval = value;
					}
					break;
					case EGL_MIN_SWAP_INTERVAL:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.minSwapInterval = value;
					}
					break;
					case EGL_RED_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.redSize = value;
					}
					break;
					case EGL_SAMPLE_BUFFERS:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.sampleBuffers = value;
					}
					break;
					case EGL_SAMPLES:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.samples = value;
					}
					break;
					case EGL_STENCIL_SIZE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.stencilSize = value;
					}
					break;
					case EGL_RENDERABLE_TYPE:
					{
						if (value & ~(EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT | EGL_OPENVG_BIT))
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.renderableType = value;
					}
					break;
					case EGL_SURFACE_TYPE:
					{
						if (value & ~(EGL_MULTISAMPLE_RESOLVE_BOX_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT | EGL_VG_ALPHA_FORMAT_PRE_BIT | EGL_VG_COLORSPACE_LINEAR_BIT | EGL_WINDOW_BIT))
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.surfaceType = value;
					}
					break;
					case EGL_TRANSPARENT_TYPE:
					{
						if (value != EGL_NONE && value != EGL_TRANSPARENT_TYPE)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.transparentType = value;
					}
					break;
					case EGL_TRANSPARENT_RED_VALUE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.transparentRedValue = value;
					}
					break;
					case EGL_TRANSPARENT_GREEN_VALUE:
					{
						if (value < 0)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}

						config.transparentGreenValue = value;
					}
					break;
					case EGL_TRANSPARENT_BLUE_VALUE:
					{
						if (value < 0)
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

			// Check, if this configuration exists.
			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			EGLint configIndex = 0;

			while (walkerConfig && configIndex < config_size)
			{
				if (config.alphaMaskSize != EGL_DONT_CARE && config.alphaMaskSize != walkerConfig->alphaMaskSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.alphaSize != EGL_DONT_CARE && config.alphaSize != walkerConfig->alphaSize)
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
				if (config.blueSize != EGL_DONT_CARE && config.blueSize != walkerConfig->blueSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.bufferSize != EGL_DONT_CARE && config.bufferSize != walkerConfig->bufferSize)
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
				if (config.conformant != EGL_DONT_CARE && config.conformant != walkerConfig->conformant)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.depthSize != EGL_DONT_CARE && config.depthSize != walkerConfig->depthSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.greenSize != EGL_DONT_CARE && config.greenSize != walkerConfig->greenSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.level != EGL_DONT_CARE && config.level != walkerConfig->level)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.luminanceSize != EGL_DONT_CARE && config.luminanceSize != walkerConfig->luminanceSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.matchNativePixmap != EGL_DONT_CARE && config.matchNativePixmap != walkerConfig->matchNativePixmap)
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
				if (config.redSize != EGL_DONT_CARE && config.redSize != walkerConfig->redSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.sampleBuffers != EGL_DONT_CARE && config.sampleBuffers != walkerConfig->sampleBuffers)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.samples != EGL_DONT_CARE && config.samples != walkerConfig->samples)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.stencilSize != EGL_DONT_CARE && config.stencilSize != walkerConfig->stencilSize)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.renderableType != EGL_DONT_CARE && config.renderableType != walkerConfig->renderableType)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.surfaceType != EGL_DONT_CARE && !(config.surfaceType & walkerConfig->surfaceType))
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
				if (config.transparentType != EGL_DONT_CARE && config.transparentType != walkerConfig->transparentType)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}
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

				//

				if (config.drawToWindow != EGL_DONT_CARE && config.drawToWindow != walkerConfig->drawToWindow)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				if (config.drawToPixmap != EGL_DONT_CARE && config.drawToPixmap != walkerConfig->drawToPixmap)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				if (config.drawToPBuffer != EGL_DONT_CARE && config.drawToPBuffer != walkerConfig->drawToPBuffer)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				if (config.doubleBuffer != EGL_DONT_CARE && config.doubleBuffer != walkerConfig->doubleBuffer)
				{
					walkerConfig = walkerConfig->next;

					continue;
				}

				//

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

EGLContext _eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	if (!attrib_list)
	{
		g_localStorage.error = EGL_BAD_PARAMETER;

		return EGL_FALSE;
	}

	if (g_localStorage.api == EGL_NONE)
	{
		g_localStorage.error = EGL_BAD_MATCH;

		return EGL_NO_CONTEXT;
	}

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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
					EGLint glx_attrib_list[] = {
							GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
							GLX_CONTEXT_MINOR_VERSION_ARB, 0,
							GLX_CONTEXT_FLAGS_ARB, 0,
							GLX_CONTEXT_PROFILE_MASK_ARB, 0,
							GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, GLX_NO_RESET_NOTIFICATION_ARB,
							0
					};

					EGLint attribListIndex = 0;

					while (attrib_list[attribListIndex] != EGL_NONE)
					{
						EGLint value = attrib_list[attribListIndex + 1];

						switch (attrib_list[attribListIndex])
						{
							case EGL_CONTEXT_MAJOR_VERSION:
							{
								if (value < 1)
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}

								glx_attrib_list[1] = value;
							}
							break;
							case EGL_CONTEXT_MINOR_VERSION:
							{
								if (value < 0)
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}

								glx_attrib_list[3] = value;
							}
							break;
							case EGL_CONTEXT_OPENGL_PROFILE_MASK:
							{
								if (value == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)
								{
									glx_attrib_list[7] = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
								}
								else if (value == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
								{
									glx_attrib_list[7] = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
								}
								else
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}
							}
							break;
							case EGL_CONTEXT_OPENGL_DEBUG:
							{
								if (value == EGL_TRUE)
								{
									glx_attrib_list[5] |= GLX_CONTEXT_DEBUG_BIT_ARB;
								}
								else if (value == EGL_FALSE)
								{
									glx_attrib_list[5] &= ~GLX_CONTEXT_DEBUG_BIT_ARB;
								}
								else
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}
							}
							break;
							case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
							{
								if (value == EGL_TRUE)
								{
									glx_attrib_list[5] |= GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
								}
								else if (value == EGL_FALSE)
								{
									glx_attrib_list[5] &= ~GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
								}
								else
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}
							}
							break;
							case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
							{
								if (value == EGL_TRUE)
								{
									glx_attrib_list[5] |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
								}
								else if (value == EGL_FALSE)
								{
									glx_attrib_list[5] &= ~GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
								}
								else
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
								}
							}
							break;
							case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
							{
								if (value == EGL_NO_RESET_NOTIFICATION)
								{
									glx_attrib_list[9] = GLX_NO_RESET_NOTIFICATION_ARB;
								}
								else if (value == EGL_LOSE_CONTEXT_ON_RESET)
								{
									glx_attrib_list[9] = GLX_LOSE_CONTEXT_ON_RESET_ARB;
								}
								else
								{
									g_localStorage.error = EGL_BAD_ATTRIBUTE;

									return EGL_FALSE;
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

						attribListIndex += 2;

						// More than 14 entries can not exist.
						if (attribListIndex >= 7 * 2)
						{
							g_localStorage.error = EGL_BAD_ATTRIBUTE;

							return EGL_FALSE;
						}
					}

					EGLContextImpl* sharedCtx = 0;

					if (share_context != EGL_NO_CONTEXT)
					{
						EGLDisplayImpl* sharedWalkerDpy = g_localStorage.rootDpy;

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
					memcpy(newCtx->glxAttribList, glx_attrib_list, GLX_ATTRIB_LIST_SIZE * sizeof(EGLint));

					newCtx->initialized = EGL_TRUE;
					newCtx->destroy = EGL_FALSE;
					newCtx->configId = walkerConfig->configId;
					newCtx->sharedCtx = sharedCtx;
					newCtx->rootGlxCtx = 0;

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

EGLSurface _eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
{
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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
					if (attrib_list)
					{
						EGLint indexAttribList = 0;

						while (attrib_list[indexAttribList] != EGL_NONE)
						{
							EGLint value = attrib_list[indexAttribList + 1];

							switch (attrib_list[indexAttribList])
							{
								case EGL_GL_COLORSPACE:
								{
									if (value == EGL_GL_COLORSPACE_LINEAR)
									{
										// Do nothing.
									}
									else if (value == EGL_GL_COLORSPACE_SRGB)
									{
										g_localStorage.error = EGL_BAD_MATCH;

										return EGL_NO_SURFACE;
									}
									else
									{
										g_localStorage.error = EGL_BAD_ATTRIBUTE;

										return EGL_NO_SURFACE;
									}
								}
								break;
								case EGL_RENDER_BUFFER:
								{
									if (value == EGL_SINGLE_BUFFER)
									{
										if (walkerConfig->doubleBuffer)
										{
											g_localStorage.error = EGL_BAD_MATCH;

											return EGL_NO_SURFACE;
										}
									}
									else if (value == EGL_BACK_BUFFER)
									{
										if (!walkerConfig->doubleBuffer)
										{
											g_localStorage.error = EGL_BAD_MATCH;

											return EGL_NO_SURFACE;
										}
									}
									else
									{
										g_localStorage.error = EGL_BAD_ATTRIBUTE;

										return EGL_NO_SURFACE;
									}
								}
								break;
								case EGL_VG_ALPHA_FORMAT:
								{
									g_localStorage.error = EGL_BAD_MATCH;

									return EGL_NO_SURFACE;
								}
								break;
								case EGL_VG_COLORSPACE:
								{
									g_localStorage.error = EGL_BAD_MATCH;

									return EGL_NO_SURFACE;
								}
								break;
							}

							indexAttribList += 2;

							// More than 4 entries can not exist.
							if (indexAttribList >= 4 * 2)
							{
								g_localStorage.error = EGL_BAD_ATTRIBUTE;

								return EGL_NO_SURFACE;
							}
						}
					}

					//

					EGLint numberPixelFormats;

					GLXFBConfig* fbConfigs = glXGetFBConfigs(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id), &numberPixelFormats);

					if (!fbConfigs || numberPixelFormats == 0)
					{
						if (fbConfigs)
						{
							XFree(fbConfigs);
						}

						return EGL_NO_SURFACE;
					}

					XVisualInfo* visualInfo;

					GLXFBConfig config = 0;

					for (EGLint currentPixelFormat = 0; currentPixelFormat < numberPixelFormats; currentPixelFormat++)
					{
						visualInfo = glXGetVisualFromFBConfig(walkerDpy->display_id, fbConfigs[currentPixelFormat]);

						if (!visualInfo)
						{
							XFree(fbConfigs);

							return EGL_FALSE;
						}

						if (walkerConfig->nativeVisualId == visualInfo->visualid)
						{
							config = fbConfigs[currentPixelFormat];

							XFree(visualInfo);

							break;
						}

						XFree(visualInfo);
					}

					XFree(fbConfigs);

					if (!config)
					{
						return EGL_NO_SURFACE;
					}

					//

					EGLSurfaceImpl* newSurface = (EGLSurfaceImpl*)malloc(sizeof(EGLSurfaceImpl));

					if (!newSurface)
					{
						g_localStorage.error = EGL_BAD_ALLOC;

						return EGL_NO_SURFACE;
					}

					newSurface->drawToWindow = EGL_TRUE;
					newSurface->drawToPixmap = EGL_FALSE;
					newSurface->drawToPixmap = EGL_FALSE;
					newSurface->doubleBuffer = walkerConfig->doubleBuffer;
					newSurface->configId = walkerConfig->configId;

					newSurface->initialized = EGL_TRUE;
					newSurface->destroy = EGL_FALSE;
					newSurface->win = win;
					newSurface->config = config;

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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

					_eglInternalCleanup();

					return EGL_TRUE;
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

EGLBoolean _eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

					// Nothing to release.

					_eglInternalCleanup();

					return EGL_TRUE;
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

EGLBoolean _eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->display_id == display_id)
		{
			return (EGLDisplay)walkerDpy;
		}

		walkerDpy = walkerDpy->next;
	}

	EGLDisplayImpl* newDpy = (EGLDisplayImpl*)malloc(sizeof(EGLDisplayImpl));

	if (!newDpy)
	{
		return EGL_NO_DISPLAY;
	}

	newDpy->initialized = EGL_FALSE;
	newDpy->destroy = EGL_FALSE;
	newDpy->display_id = display_id;
	newDpy->rootSurface = 0;
	newDpy->rootCtx = 0;
	newDpy->rootConfig = 0;
	newDpy->currentDraw = EGL_NO_SURFACE;
	newDpy->currentRead = EGL_NO_SURFACE;
	newDpy->currentCtx = EGL_NO_CONTEXT;
	newDpy->next = g_localStorage.rootDpy;

	g_localStorage.rootDpy = newDpy;

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
	return (__eglMustCastToProperFunctionPointerType )glXGetProcAddress((const GLubyte *)procname);
}

EGLBoolean _eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}


			walkerDpy->initialized = EGL_TRUE;

			// Create configuration list.

			EGLint numberPixelFormats;

			GLXFBConfig* fbConfigs = glXGetFBConfigs(walkerDpy->display_id, DefaultScreen(walkerDpy->display_id), &numberPixelFormats);

			if (!fbConfigs || numberPixelFormats == 0)
			{
				if (fbConfigs)
				{
					XFree(fbConfigs);
				}

				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			EGLint attribute;

			XVisualInfo* visualInfo;

			EGLConfigImpl* lastConfig = 0;
			for (EGLint currentPixelFormat = 0; currentPixelFormat < numberPixelFormats; currentPixelFormat++)
			{
				EGLint value;

				attribute = GLX_VISUAL_ID;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				if (!value)
				{
					continue;
				}

				// No check for OpenGL.

				attribute = GLX_RENDER_TYPE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				if (!(value & GLX_RGBA_BIT))
				{
					continue;
				}

				attribute = GLX_TRANSPARENT_TYPE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				if (value == GLX_TRANSPARENT_INDEX)
				{
					continue;
				}

				//

				EGLConfigImpl* newConfig = (EGLConfigImpl*)malloc(sizeof(EGLConfigImpl));
				if (!newConfig)
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				_eglInternalSetDefaultConfig(newConfig);

				// Store in the same order as received.
				newConfig->next = 0;
				if (lastConfig != 0)
				{
					lastConfig->next = newConfig;
				}
				else
				{
					walkerDpy->rootConfig = newConfig;
				}
				lastConfig = newConfig;

				//

				attribute = GLX_DRAWABLE_TYPE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &value))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				newConfig->drawToWindow = value & GLX_WINDOW_BIT ? EGL_TRUE : EGL_FALSE;
				newConfig->drawToPixmap = value & GLX_PIXMAP_BIT ? EGL_TRUE : EGL_FALSE;
				newConfig->drawToPBuffer = value & GLX_PBUFFER_BIT ? EGL_TRUE : EGL_FALSE;

				attribute = GLX_DOUBLEBUFFER;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->doubleBuffer))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				//

				newConfig->conformant = EGL_OPENGL_BIT;
				newConfig->renderableType = EGL_OPENGL_BIT;
				newConfig->surfaceType = 0;
				if (newConfig->drawToWindow)
				{
					newConfig->surfaceType |= EGL_WINDOW_BIT;
				}
				if (newConfig->drawToPixmap)
				{
					newConfig->surfaceType |= EGL_PIXMAP_BIT;
				}
				if (newConfig->drawToPBuffer)
				{
					newConfig->surfaceType |= EGL_PBUFFER_BIT;
				}
				newConfig->colorBufferType = EGL_RGB_BUFFER;
				newConfig->configId = currentPixelFormat;

				attribute = GLX_BUFFER_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bufferSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_RED_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->redSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_GREEN_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->greenSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_BLUE_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->blueSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_ALPHA_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->alphaSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_DEPTH_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->depthSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_STENCIL_SIZE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->stencilSize))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}


				//

				attribute = GLX_SAMPLE_BUFFERS;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->sampleBuffers))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_SAMPLES;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->samples))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				//

				attribute = GLX_BIND_TO_TEXTURE_RGB_EXT;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bindToTextureRGB))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				newConfig->bindToTextureRGB = newConfig->bindToTextureRGB ? EGL_TRUE : EGL_FALSE;

				attribute = GLX_BIND_TO_TEXTURE_RGBA_EXT;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->bindToTextureRGBA))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				newConfig->bindToTextureRGBA = newConfig->bindToTextureRGBA ? EGL_TRUE : EGL_FALSE;

				//

				attribute = GLX_MAX_PBUFFER_PIXELS;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferPixels))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_MAX_PBUFFER_WIDTH;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferWidth))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_MAX_PBUFFER_HEIGHT;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->maxPBufferHeight))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				//

				attribute = GLX_TRANSPARENT_TYPE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentType))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}
				newConfig->transparentType = newConfig->transparentType == GLX_TRANSPARENT_RGB ? EGL_TRANSPARENT_RGB : EGL_NONE;

				attribute = GLX_TRANSPARENT_RED_VALUE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentRedValue))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_TRANSPARENT_GREEN_VALUE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentGreenValue))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				attribute = GLX_TRANSPARENT_BLUE_VALUE;
				if (glXGetFBConfigAttrib(walkerDpy->display_id, fbConfigs[currentPixelFormat], attribute, &newConfig->transparentBlueValue))
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				//

				visualInfo = glXGetVisualFromFBConfig(walkerDpy->display_id, fbConfigs[currentPixelFormat]);

				if (!visualInfo)
				{
					XFree(fbConfigs);

					g_localStorage.error = EGL_NOT_INITIALIZED;

					return EGL_FALSE;
				}

				newConfig->nativeVisualId = visualInfo->visualid;

				XFree(visualInfo);

				// FIXME: Query and save more values.
			}

			XFree(fbConfigs);

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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	if ((ctx == EGL_NO_CONTEXT && (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE)) || (ctx != EGL_NO_CONTEXT && (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE)))
	{
		g_localStorage.error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_NOT_INITIALIZED;

				return EGL_FALSE;
			}

			EGLSurfaceImpl* currentDraw = EGL_NO_SURFACE;
			EGLSurfaceImpl* currentRead = EGL_NO_SURFACE;
			EGLContextImpl* currentCtx = EGL_NO_CONTEXT;

			GLXDrawable windowsSurface = 0;
			GLXContext windowsCtx = 0;

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
				windowsSurface = currentDraw->win;
			}

			if (currentCtx != EGL_NO_CONTEXT)
			{
				GLXContextImpl* glxCtx = currentCtx->rootGlxCtx;

				while (glxCtx)
				{
					if (glxCtx->surface == currentDraw)
					{
						break;
					}

					glxCtx = glxCtx->next;
				}

				if (!glxCtx)
				{
					glxCtx = (GLXContextImpl*)malloc(sizeof(GLXContextImpl));

					if (!glxCtx)
					{
						return EGL_FALSE;
					}

					// Gather shared context, if one exists.
					GLXContextImpl* sharedGlxCtx = 0;
					if (currentCtx->sharedCtx)
					{
						EGLContextImpl* sharedWalkerCtx = currentCtx->sharedCtx;

						EGLContextImpl* beforeSharedWalkerCtx = 0;

						while (sharedWalkerCtx)
						{
							// Check, if already created.
							if (sharedWalkerCtx->rootGlxCtx)
							{
								sharedGlxCtx = sharedWalkerCtx->rootGlxCtx;

								break;
							}

							beforeSharedWalkerCtx = sharedWalkerCtx;
							sharedWalkerCtx = sharedWalkerCtx->sharedCtx;

							// No created shared context found.
							if (!sharedWalkerCtx)
							{
								sharedGlxCtx = (GLXContextImpl*)malloc(sizeof(GLXContextImpl));

								if (!sharedGlxCtx)
								{
									free(glxCtx);

									return EGL_FALSE;
								}

								glXCreateContextAttribsARB(walkerDpy->display_id, currentDraw->config, 0, True, beforeSharedWalkerCtx->glxAttribList);

								if (!sharedGlxCtx->ctx)
								{
									free(sharedGlxCtx);

									free(glxCtx);

									return EGL_FALSE;
								}

								sharedGlxCtx->surface = currentDraw;

								sharedGlxCtx->next = beforeSharedWalkerCtx->rootGlxCtx;
								beforeSharedWalkerCtx->rootGlxCtx = sharedGlxCtx;
							}
						}
					}
					else
					{
						// Use own context as shared context, if one exits.

						sharedGlxCtx = currentCtx->rootGlxCtx;
					}

					glxCtx->ctx = glXCreateContextAttribsARB(walkerDpy->display_id, currentDraw->config, sharedGlxCtx ? sharedGlxCtx->ctx : 0, True, currentCtx->glxAttribList);

					if (!glxCtx->ctx)
					{
						free(glxCtx);

						return EGL_FALSE;
					}

					glxCtx->surface = currentDraw;

					glxCtx->next = currentCtx->rootGlxCtx;
					currentCtx->rootGlxCtx = glxCtx;
				}

				windowsCtx = glxCtx->ctx;
			}

			result = (EGLBoolean)glXMakeCurrent(walkerDpy->display_id, windowsSurface, windowsCtx);

			if (!result)
			{
				g_localStorage.error = EGL_BAD_MATCH;

				return EGL_FALSE;
			}

			walkerDpy->currentDraw = currentDraw;
			walkerDpy->currentRead = currentRead;
			walkerDpy->currentCtx = currentCtx;

			g_localStorage.currentCtx = currentCtx;

			_eglInternalCleanup();

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;

	return EGL_FALSE;
}

EGLBoolean _eglQueryContext (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

					glXSwapBuffers(walkerDpy->display_id, walkerSurface->win);

					return EGL_TRUE;
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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
			if (!walkerDpy->initialized || walkerDpy->destroy)
			{
				g_localStorage.error = EGL_BAD_DISPLAY;

				return EGL_FALSE;
			}

			walkerDpy->initialized = EGL_FALSE;
			walkerDpy->destroy = EGL_TRUE;

			_eglInternalCleanup();

			return EGL_TRUE;
		}

		walkerDpy = walkerDpy->next;
	}

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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
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
	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if ((EGLDisplay)walkerDpy == dpy)
		{
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

			glXSwapIntervalEXT(walkerDpy->display_id, walkerDpy->currentDraw->win, interval);

			return EGL_TRUE;
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
	if (api == EGL_OPENGL_API)
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

	EGLDisplayImpl* walkerDpy = g_localStorage.rootDpy;

	while (walkerDpy)
	{
		if (walkerDpy->currentCtx == g_localStorage.currentCtx)
		{
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

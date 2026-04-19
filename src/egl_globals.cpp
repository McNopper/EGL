#include "egl_common.h"

thread_local LocalStorage g_localStorage =
    { EGL_SUCCESS, EGL_OPENGL_ES_API, EGL_NO_CONTEXT_IMPL };

GlobalStorage g_globalStorage;

EGLint g_GL_max_supported_version[2] = { 0, 0 };
EGLint g_ES_max_supported_version[2] = { 0, 0 };

EGLBoolean _eglInternalInit()
{
	auto dummy = g_globalStorage.dummy_read();
	EGLBoolean r = __internalInit(&dummy, g_GL_max_supported_version, g_ES_max_supported_version);
	g_globalStorage.dummy_write(dummy);

	return r;
}

void _eglInternalTerminate()
{
	auto dummy = g_globalStorage.dummy_read();
	__internalTerminate(&dummy);
	g_globalStorage.dummy_write(dummy);
}

void _eglInternalCleanup()
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

					// Free remaining syncs (EGL 1.5 §3.8)
					EGLSyncImpl* walkerSync = walkerDpy->rootSync;
					while (walkerSync)
					{
						EGLSyncImpl* del = walkerSync;
						walkerSync = walkerSync->next;
						if (glDeleteSync_PTR && del->glSync)
							glDeleteSync_PTR(del->glSync);
						delete del;
					}
					walkerDpy->rootSync = nullptr;

					// Free remaining images
					EGLImageImpl* walkerImage = walkerDpy->rootImage;
					while (walkerImage)
					{
						EGLImageImpl* del = walkerImage;
						walkerImage = walkerImage->next;
						delete del;
					}
					walkerDpy->rootImage = nullptr;

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

extern "C"
{

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

} // extern "C"

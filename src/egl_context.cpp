#include "egl_common.h"

extern "C"
{

EGLContext _eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	static const EGLint emptyAttrib[] = { EGL_NONE };
	if (!attrib_list)
		attrib_list = emptyAttrib;

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
		if (requested_version[0] > g_GL_max_supported_version[0] ||
			(requested_version[0] == g_GL_max_supported_version[0] && requested_version[1] > g_GL_max_supported_version[1]))
			return EGL_NO_CONTEXT;
	}
	else if (g_localStorage.api == EGL_OPENGL_ES_API)
	{
		if (requested_version[0] > g_ES_max_supported_version[0] ||
			(requested_version[0] == g_ES_max_supported_version[0] && requested_version[1] > g_ES_max_supported_version[1]))
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

				return EGL_NO_CONTEXT;
			}

			EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

			while (walkerConfig)
			{
				if ((EGLConfig)walkerConfig == config)
				{
					EGLint target_attrib_list[CONTEXT_ATTRIB_LIST_SIZE];

					const EGLint esBit = (requested_version[0] == 1) ? EGL_OPENGL_ES_BIT :
						(requested_version[0] == 2) ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT;
					if (g_localStorage.api == EGL_OPENGL_ES_API && (walkerConfig->conformant & esBit) == 0)
					{
						return EGL_NO_CONTEXT;
					}
					if (!__processAttribList(g_localStorage.api, target_attrib_list, attrib_list, &g_localStorage.error))
					{
						return EGL_NO_CONTEXT;
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

										return EGL_NO_CONTEXT;
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

							return EGL_NO_CONTEXT;
						}
					}

					EGLContextImpl* newCtx = (EGLContextImpl*)malloc(sizeof(EGLContextImpl));

					if (!newCtx)
					{
						g_localStorage.error = EGL_BAD_ALLOC;

						return EGL_NO_CONTEXT;
					}

					// Move the atttibutes for later creation.
					memcpy(newCtx->attribList, target_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

					newCtx->initialized = EGL_TRUE;
					newCtx->destroy = EGL_FALSE;
					newCtx->configId = walkerConfig->configId;
					newCtx->clientAPI = g_localStorage.api;
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
	{
		_eglInternalCleanup();
		return EGL_TRUE;
	}

	g_localStorage.error = EGL_BAD_DISPLAY;
	return EGL_FALSE;
}

EGLContext _eglGetCurrentContext(void)
{
	return g_localStorage.currentCtx;
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
								*value = walkerCtx->clientAPI;
							}

							return EGL_TRUE;
						}
						break;
						case EGL_CONTEXT_CLIENT_VERSION:
						{
							if (value)
								*value = walkerCtx->attribList[1];

							return EGL_TRUE;
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

				EGLBoolean result = EGL_TRUE;

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

	if (!success)
		g_localStorage.error = EGL_BAD_DISPLAY;

	return success;
}

} // extern "C"

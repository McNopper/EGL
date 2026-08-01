#include "egl_common.h"

thread_local LocalStorage g_localStorage =
    {EGL_SUCCESS, EGL_OPENGL_ES_API, EGL_NO_CONTEXT_IMPL, nullptr, EGL_NO_SURFACE_IMPL, EGL_NO_SURFACE_IMPL};

GlobalStorage g_globalStorage;

EGLint g_GL_max_supported_version[2] = {0, 0};
EGLint g_ES_max_supported_version[2] = {0, 0};

EGLBoolean _eglInternalInit()
{
    // The whole read-modify-write has to be atomic; otherwise two concurrent
    // eglGetDisplay calls both bootstrap and one dummy window / GL context leaks.
    guard_t _{g_globalStorage.bootstrapMutex()};

    auto       dummy = g_globalStorage.dummy_read();
    EGLBoolean r     = __internalInit(&dummy, g_GL_max_supported_version, g_ES_max_supported_version);
    g_globalStorage.dummy_write(dummy);

    return r;
}

void _eglInternalTerminate()
{
    guard_t _{g_globalStorage.bootstrapMutex()};

    auto dummy = g_globalStorage.dummy_read();
    __internalTerminate(&dummy);
    g_globalStorage.dummy_write(dummy);
}

void _eglInternalCleanup()
{
    auto             _wl     = g_globalStorage.placeRootDpy_writelock();
    EGLDisplayImpl** linkDpy = &g_globalStorage.rootDpy;

    while (*linkDpy)
    {
        EGLDisplayImpl* walkerDpy = *linkDpy;

        EGLSurfaceImpl** linkSurface = &walkerDpy->rootSurface;

        while (*linkSurface)
        {
            EGLSurfaceImpl* walkerSurface = *linkSurface;

            // Only objects nobody has current any more may go away (EGL 1.5 3.5.6).
            if (walkerSurface->destroy && walkerSurface->refCount == 0)
            {
                *linkSurface = walkerSurface->next;

                // Deferred native teardown: the drawable stays alive as long as
                // the surface is current to any thread.
                __destroySurface(walkerDpy->display_id, walkerSurface);

                free(walkerSurface);

                continue;
            }

            linkSurface = &walkerSurface->next;
        }

        EGLContextImpl** linkCtx = &walkerDpy->rootCtx;

        while (*linkCtx)
        {
            EGLContextImpl* walkerCtx = *linkCtx;

            if (walkerCtx->destroy && walkerCtx->refCount == 0)
            {
                *linkCtx = walkerCtx->next;

                // Freeing the context.
                while (walkerCtx->rootCtxList)
                {
                    EGLContextListImpl* deleteCtxList = walkerCtx->rootCtxList;

                    walkerCtx->rootCtxList = walkerCtx->rootCtxList->next;

                    __deleteContext(walkerDpy, &deleteCtxList->nativeContextContainer);

                    free(deleteCtxList);
                }

                free(walkerCtx);

                continue;
            }

            linkCtx = &walkerCtx->next;
        }

        if (walkerDpy->destroy)
        {
            if (walkerDpy->rootSurface == 0 && walkerDpy->rootCtx == 0 && walkerDpy->currentDraw == EGL_NO_SURFACE && walkerDpy->currentRead == EGL_NO_SURFACE && walkerDpy->currentCtx == EGL_NO_CONTEXT)
            {
                EGLConfigImpl* walkerConfig = walkerDpy->rootConfig;

                while (walkerConfig)
                {
                    EGLConfigImpl* deleteConfig = walkerConfig;

                    walkerConfig = walkerConfig->next;

                    free(deleteConfig);
                }
                walkerDpy->rootConfig = 0;

                // Free remaining syncs (EGL 1.5 §3.8)
                EGLSyncImpl* walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    EGLSyncImpl* del = walkerSync;
                    walkerSync       = walkerSync->next;
                    // Deleting a GL sync object needs a current GL context; without
                    // one the handle is simply dropped rather than passed to GL.
                    if (glDeleteSync_PTR && del->glSync && g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL)
                        glDeleteSync_PTR(del->glSync);
                    delete del;
                }
                walkerDpy->rootSync = nullptr;

                // Free remaining images
                EGLImageImpl* walkerImage = walkerDpy->rootImage;
                while (walkerImage)
                {
                    EGLImageImpl* del = walkerImage;
                    walkerImage       = walkerImage->next;
                    delete del;
                }
                walkerDpy->rootImage = nullptr;

                //

                *linkDpy = walkerDpy->next;

                delete walkerDpy;

                continue;
            }
        }

        linkDpy = &walkerDpy->next;
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

        config->alphaSize     = 0;
        config->alphaMaskSize = 0;

        config->bindToTextureRGB  = EGL_DONT_CARE;
        config->bindToTextureRGBA = EGL_DONT_CARE;
        config->blueSize          = 0;
        config->bufferSize        = 0;

        config->colorBufferType = EGL_RGB_BUFFER; // EGL 1.5 Table 3.4 default
        config->configCaveat    = EGL_DONT_CARE;
        config->configId        = EGL_DONT_CARE;
        config->conformant      = 0;

        config->depthSize = 0;

        config->greenSize = 0;

        config->level         = 0;
        config->luminanceSize = 0;

        config->matchNativePixmap = EGL_NONE;
        config->maxPBufferHeight  = EGL_DONT_CARE;
        config->maxPBufferPixels  = EGL_DONT_CARE;
        config->maxPBufferWidth   = EGL_DONT_CARE;
        config->maxSwapInterval   = EGL_DONT_CARE;
        config->minSwapInterval   = EGL_DONT_CARE;

        config->nativeRenderable = EGL_DONT_CARE;
        config->nativeVisualId   = 0;
        config->nativeVisualType = EGL_NONE;

        config->redSize        = 0;
        config->renderableType = EGL_OPENGL_ES_BIT;

        config->sampleBuffers = 0;
        config->samples       = 0;
        config->stencilSize   = 0;
        config->surfaceType   = EGL_WINDOW_BIT;

        config->srgbCapable = EGL_FALSE;

        config->transparentBlueValue  = EGL_DONT_CARE;
        config->transparentGreenValue = EGL_DONT_CARE;
        config->transparentRedValue   = EGL_DONT_CARE;
        config->transparentType       = EGL_NONE;

        //

        config->drawToWindow  = EGL_TRUE;
        config->drawToPixmap  = EGL_FALSE;
        config->drawToPBuffer = EGL_FALSE;
        config->doubleBuffer  = EGL_TRUE;

        config->next = 0;
    }

} // extern "C"

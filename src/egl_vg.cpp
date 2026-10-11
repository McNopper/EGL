#include "egl_common.h"
#include "eglvg.h"

/*
 * OpenVG provider registry and the EGL side of the provider seam.
 *
 * The provider pointer is registered once at startup (McNopper/OpenVG_Library
 * registers itself automatically), so no locking is required beyond that
 * point being earlier than the first EGL call.
 */

extern "C"
{

    static const EGLVGProvider* s_vgProvider = 0;

    EGLBoolean eglSetVGProvider(const EGLVGProvider* provider)
    {
        s_vgProvider = provider;

        g_localStorage.error = EGL_SUCCESS;

        return EGL_TRUE;
    }

    const EGLVGProvider* _eglGetVGProvider(void)
    {
        return s_vgProvider;
    }

    EGLBoolean _eglVGCreateContext(EGLContextImpl* walkerCtx)
    {
        const EGLVGProvider* provider = s_vgProvider;

        if (!provider || !walkerCtx)
        {
            return EGL_FALSE;
        }

        walkerCtx->vgContext = 0;

        return provider->createContext(&walkerCtx->vgContext);
    }

    void _eglVGDestroyContext(EGLContextImpl* walkerCtx)
    {
        const EGLVGProvider* provider = s_vgProvider;

        if (provider && walkerCtx && walkerCtx->vgContext)
        {
            provider->destroyContext(walkerCtx->vgContext);

            walkerCtx->vgContext = 0;
        }
    }

    void _eglVGFreeSurfaceBuffer(EGLSurfaceImpl* walkerSurface)
    {
        if (walkerSurface && walkerSurface->vgBuffer)
        {
            free(walkerSurface->vgBuffer);

            walkerSurface->vgBuffer = 0;
            walkerSurface->vgStride = 0;
        }
    }

    EGLBoolean _eglVGMakeCurrent(EGLContextImpl* walkerCtx, EGLSurfaceImpl* walkerSurface)
    {
        const EGLVGProvider* provider = s_vgProvider;

        if (!provider)
        {
            return EGL_FALSE;
        }

        if (walkerCtx == EGL_NO_CONTEXT_IMPL)
        {
            return provider->makeCurrent(0, 0, 0, 0, 0, 0);
        }

        if (!walkerSurface || !walkerSurface->drawToWindow)
        {
            return EGL_FALSE;
        }

        // The pixel buffer is allocated lazily: GL-only surfaces never need one.
        if (!walkerSurface->vgBuffer)
        {
            walkerSurface->vgStride = walkerSurface->width * 4;

            walkerSurface->vgBuffer = malloc((size_t)walkerSurface->vgStride * (size_t)walkerSurface->height);

            if (!walkerSurface->vgBuffer)
            {
                walkerSurface->vgStride = 0;

                return EGL_FALSE;
            }
        }

        return provider->makeCurrent(walkerCtx->vgContext, walkerSurface->vgBuffer, EGL_VG_BUFFER_FORMAT_SARGB_8888, walkerSurface->width, walkerSurface->height, walkerSurface->vgStride);
    }

    void _eglVGNotifySwap(EGLContextImpl* walkerCtx)
    {
        const EGLVGProvider* provider = s_vgProvider;

        if (provider && provider->notifySwap && walkerCtx && walkerCtx->vgContext)
        {
            provider->notifySwap(walkerCtx->vgContext);
        }
    }

}

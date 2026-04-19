#include "egl_common.h"

extern "C"
{

EGLImage _eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attrib_list)
{
    (void)attrib_list;
    switch (target)
    {
    case EGL_GL_TEXTURE_2D:
    case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case EGL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
    case EGL_GL_TEXTURE_3D:
    case 0x30C3: // EGL_GL_TEXTURE_2D_ARRAY (EGL 1.5)
    case EGL_GL_RENDERBUFFER:
        break;
    default:
        g_localStorage.error = EGL_BAD_PARAMETER;
        return EGL_NO_IMAGE;
    }

    if (!buffer)
    {
        g_localStorage.error = EGL_BAD_PARAMETER;
        return EGL_NO_IMAGE;
    }

    // EGL 1.5 §3.10.2: ctx must be EGL_NO_CONTEXT or a valid context belonging to dpy.
    // All GL-object targets require a valid context; EGL_NO_CONTEXT is only legal for
    // targets that don't need one (none defined by EGL 1.5 core), so reject it.
    if (ctx == EGL_NO_CONTEXT)
    {
        g_localStorage.error = EGL_BAD_CONTEXT;
        return EGL_NO_IMAGE;
    }

    auto _rl = g_globalStorage.placeRootDpy_readlock();
    EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
    while (walkerDpy)
    {
        if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
        {
            if (!walkerDpy->initialized || walkerDpy->destroy)
            {
                g_localStorage.error = EGL_NOT_INITIALIZED;
                return EGL_NO_IMAGE;
            }

            // Validate that ctx belongs to this display.
            bool ctxFound = false;
            EGLContextImpl* walkerCtx = walkerDpy->rootCtx;
            while (walkerCtx)
            {
                if (reinterpret_cast<EGLContext>(walkerCtx) == ctx)
                {
                    if (!walkerCtx->initialized || walkerCtx->destroy)
                    {
                        g_localStorage.error = EGL_BAD_CONTEXT;
                        return EGL_NO_IMAGE;
                    }
                    ctxFound = true;
                    break;
                }
                walkerCtx = walkerCtx->next;
            }
            if (!ctxFound)
            {
                g_localStorage.error = EGL_BAD_CONTEXT;
                return EGL_NO_IMAGE;
            }

            EGLImageImpl* newImage = new EGLImageImpl();
            if (!newImage)
            {
                g_localStorage.error = EGL_BAD_ALLOC;
                return EGL_NO_IMAGE;
            }
            newImage->target = target;
            newImage->buffer = buffer;
            std::lock_guard<std::mutex> lk(walkerDpy->mutex);
            newImage->next      = walkerDpy->rootImage;
            walkerDpy->rootImage = newImage;
            return reinterpret_cast<EGLImage>(newImage);
        }
        walkerDpy = walkerDpy->next;
    }
    g_localStorage.error = EGL_BAD_DISPLAY;
    return EGL_NO_IMAGE;
}

EGLBoolean _eglDestroyImage(EGLDisplay dpy, EGLImage image)
{
    auto _rl = g_globalStorage.placeRootDpy_readlock();
    EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
    while (walkerDpy)
    {
        if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
        {
            if (!walkerDpy->initialized || walkerDpy->destroy)
            {
                g_localStorage.error = EGL_NOT_INITIALIZED;
                return EGL_FALSE;
            }
            std::lock_guard<std::mutex> lk(walkerDpy->mutex);
            EGLImageImpl* prev = nullptr;
            EGLImageImpl* walker = walkerDpy->rootImage;
            while (walker)
            {
                if (reinterpret_cast<EGLImage>(walker) == image)
                {
                    if (prev)
                        prev->next = walker->next;
                    else
                        walkerDpy->rootImage = walker->next;
                    delete walker;
                    return EGL_TRUE;
                }
                prev   = walker;
                walker = walker->next;
            }
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        walkerDpy = walkerDpy->next;
    }
    g_localStorage.error = EGL_BAD_DISPLAY;
    return EGL_FALSE;
}

} // extern "C"

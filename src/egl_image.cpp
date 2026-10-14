#include "egl_common.h"
#if defined(EGL_LINUX_ENABLE_GLES) || defined(EGL_WAYLAND_ENABLE_GLES)
#include "egl_linux_gles.h"
#define EGL_HAS_GLES_DELEGATION
#endif
#ifdef EGL_WIN_ENABLE_ANGLE
#include "egl_windows_angle.h"
#endif
#include <new>
#include <vector>

extern "C"
{

    // Returns the image node for `image`, or nullptr when the handle is
    // unknown. No error is set - the not-found code stays at the call site.
    // The caller holds the display mutex: eglDestroyImage unlinks and deletes
    // under it.
    EGLImageImpl* _eglFindImage(EGLDisplayImpl* walkerDpy, EGLImage image)
    {
        EGLImageImpl* walker = walkerDpy->rootImage;

        while (walker)
        {
            if (reinterpret_cast<EGLImage>(walker) == image)
            {
                return walker;
            }

            walker = walker->next;
        }

        return nullptr;
    }

    EGLImage _eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib* attrib_list)
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

        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_NO_IMAGE;
        }
        // Hold the display mutex across the whole lookup and insert: the
        // initialized flag, rootCtx and rootImage are all mutated by the
        // other entry points under this mutex, so walking them while holding
        // only the global read lock was a data race.
        std::lock_guard<std::mutex> lk(walkerDpy->mutex);

        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_NO_IMAGE;
        }

        // Validate that ctx belongs to this display.
        EGLContextImpl* walkerCtx = _eglFindContext(walkerDpy, ctx);
        if (!walkerCtx)
        {
            g_localStorage.error = EGL_BAD_CONTEXT;
            return EGL_NO_IMAGE;
        }
        if (!walkerCtx->initialized || walkerCtx->destroy)
        {
            g_localStorage.error = EGL_BAD_CONTEXT;
            return EGL_NO_IMAGE;
        }

        EGLImageImpl* newImage = new (std::nothrow) EGLImageImpl();
        if (!newImage)
        {
            g_localStorage.error = EGL_BAD_ALLOC;
            return EGL_NO_IMAGE;
        }
        newImage->target = target;
        newImage->buffer = buffer;

#if defined(EGL_HAS_GLES_DELEGATION) || defined(EGL_WIN_ENABLE_ANGLE)
        // Contexts whose GL runs in a delegated stack (the system GLES stack
        // on Linux, ANGLE on Windows) create their images there as well: that
        // stack's image consumption entry points dereference the handle with
        // their own object model. Contexts of this library's own GL backends
        // (GLX, WGL) keep the bookkeeping image; their consumption entry
        // points are not handed out at all (see _eglGetProcAddress).
        EGLContextListImpl* ctxList = _eglFindContextList(walkerCtx, g_localStorage.currentDraw);

        // The image source attributes are small tokens, levels and offsets;
        // the narrowing to EGLint is lossless for them.
        std::vector<EGLint> converted;

        if (attrib_list)
        {
            for (const EGLAttrib* walker = attrib_list; walker[0] != EGL_NONE; walker += 2)
            {
                converted.push_back(static_cast<EGLint>(walker[0]));
                converted.push_back(static_cast<EGLint>(walker[1]));
            }
        }

        converted.push_back(EGL_NONE);

#ifdef EGL_HAS_GLES_DELEGATION
        if (ctxList && ctxList->nativeContextContainer.backend == EGL_BACKEND_GLES)
        {
            EGLint glesError = EGL_SUCCESS;

            if (gles_createImageKHR(ctxList->nativeContextContainer.glesCtx, target, buffer, converted.data(), &newImage->glesImage, &glesError) != EGL_TRUE)
            {
                delete newImage;

                g_localStorage.error = glesError != EGL_SUCCESS ? glesError : EGL_BAD_ALLOC;

                return EGL_NO_IMAGE;
            }
        }
#endif
#ifdef EGL_WIN_ENABLE_ANGLE
        if (ctxList && ctxList->nativeContextContainer.backend == EGL_BACKEND_ANGLE)
        {
            EGLint angleError = EGL_SUCCESS;

            if (angle_createImageKHR(ctxList->nativeContextContainer.angleCtx, target, buffer, converted.data(), &newImage->angleImage, &angleError) != EGL_TRUE)
            {
                delete newImage;

                g_localStorage.error = angleError != EGL_SUCCESS ? angleError : EGL_BAD_ALLOC;

                return EGL_NO_IMAGE;
            }
        }
#endif
#endif

        newImage->next       = walkerDpy->rootImage;
        walkerDpy->rootImage = newImage;
        g_localStorage.error = EGL_SUCCESS;
        return reinterpret_cast<EGLImage>(newImage);
    }

    EGLBoolean _eglDestroyImage(EGLDisplay dpy, EGLImage image)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_FALSE;
        }
        if (!walkerDpy->initialized || walkerDpy->destroy)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_FALSE;
        }
        std::lock_guard<std::mutex> lk(walkerDpy->mutex);
        EGLImageImpl*               prev   = nullptr;
        EGLImageImpl*               walker = walkerDpy->rootImage;
        while (walker)
        {
            if (reinterpret_cast<EGLImage>(walker) == image)
            {
#if defined(EGL_HAS_GLES_DELEGATION) || defined(EGL_WIN_ENABLE_ANGLE)
                // A delegated image goes away with the stack that created
                // it; this node only tracked it.
#endif
#ifdef EGL_HAS_GLES_DELEGATION
                if (walker->glesImage)
                {
                    gles_destroyImageKHR(walker->glesImage);
                }
#endif
#ifdef EGL_WIN_ENABLE_ANGLE
                if (walker->angleImage)
                {
                    angle_destroyImageKHR(walker->angleImage);
                }
#endif
                if (prev)
                    prev->next = walker->next;
                else
                    walkerDpy->rootImage = walker->next;
                delete walker;
                g_localStorage.error = EGL_SUCCESS;
                return EGL_TRUE;
            }
            prev   = walker;
            walker = walker->next;
        }
        g_localStorage.error = EGL_BAD_PARAMETER;
        return EGL_FALSE;
    }

    // EGL_KHR_image_base: eglCreateImageKHR is identical to eglCreateImage
    // except for the attribute list type; the EGLint pairs widen one to one.
    EGLImageKHR _eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list)
    {
        std::vector<EGLAttrib> converted;

        if (attrib_list)
        {
            for (const EGLint* walker = attrib_list; walker[0] != EGL_NONE; walker += 2)
            {
                converted.push_back(static_cast<EGLAttrib>(walker[0]));
                converted.push_back(static_cast<EGLAttrib>(walker[1]));
            }
        }

        converted.push_back(EGL_NONE);

        return _eglCreateImage(dpy, ctx, target, buffer, converted.data());
    }

    EGLBoolean _eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
    {
        return _eglDestroyImage(dpy, image);
    }

    // GL_OES_EGL_image consumption entry points. They cannot be forwarded to
    // the client loader: that implementation could not interpret this
    // library's image handles. Consumption runs through the delegated stack
    // when both the current context and the image live there; contexts of
    // this library's own GL backends (GLX, WGL) have no EGLImage interop
    // (neither defines one), and the call is accepted without effect for
    // them - the operation is not advertised for such contexts.
    void _glEGLImageTargetTexture2DOES(GLenum target, void* image)
    {
#if defined(EGL_HAS_GLES_DELEGATION) || defined(EGL_WIN_ENABLE_ANGLE)
        EGLImageImpl* walkerImage = reinterpret_cast<EGLImageImpl*>(image);

        if (walkerImage && g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL)
        {
            EGLContextListImpl* ctxList = _eglFindContextList(g_localStorage.currentCtx, g_localStorage.currentDraw);

            if (!ctxList)
            {
                return;
            }

#ifdef EGL_HAS_GLES_DELEGATION
            if (ctxList->nativeContextContainer.backend == EGL_BACKEND_GLES && walkerImage->glesImage)
            {
                gles_imageTargetTexture2D(target, walkerImage->glesImage);
            }
#endif
#ifdef EGL_WIN_ENABLE_ANGLE
            if (ctxList->nativeContextContainer.backend == EGL_BACKEND_ANGLE && walkerImage->angleImage)
            {
                angle_imageTargetTexture2D(target, walkerImage->angleImage);
            }
#endif
        }
#endif
    }

    void _glEGLImageTargetRenderbufferStorageOES(GLenum target, void* image)
    {
#if defined(EGL_HAS_GLES_DELEGATION) || defined(EGL_WIN_ENABLE_ANGLE)
        EGLImageImpl* walkerImage = reinterpret_cast<EGLImageImpl*>(image);

        if (walkerImage && g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL)
        {
            EGLContextListImpl* ctxList = _eglFindContextList(g_localStorage.currentCtx, g_localStorage.currentDraw);

            if (!ctxList)
            {
                return;
            }

#ifdef EGL_HAS_GLES_DELEGATION
            if (ctxList->nativeContextContainer.backend == EGL_BACKEND_GLES && walkerImage->glesImage)
            {
                gles_imageTargetRenderbufferStorage(target, walkerImage->glesImage);
            }
#endif
#ifdef EGL_WIN_ENABLE_ANGLE
            if (ctxList->nativeContextContainer.backend == EGL_BACKEND_ANGLE && walkerImage->angleImage)
            {
                angle_imageTargetRenderbufferStorage(target, walkerImage->angleImage);
            }
#endif
        }
#endif
    }

} // extern "C"

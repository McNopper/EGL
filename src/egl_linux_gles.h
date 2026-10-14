/**
 * System libEGL/libGLESv2 backend for OpenGL ES on Linux/X11 and Wayland.
 *
 * Loads the system EGL (libEGL.so.1) and GLES (libGLESv2.so.2) libraries
 * dynamically at runtime and delegates ES context/surface lifecycle to them.
 * The desktop GL path remains owned by GLX inside egl_x11_glx.cpp; this
 * file only handles the ES branch.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#ifndef EGL_LINUX_GLES_H
#define EGL_LINUX_GLES_H

#include <EGL/egl.h>
#include "egl_internal.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Load libEGL.so.1 / libGLESv2.so.2 and create a system EGLDisplay backed
     * by the given native display. Reports the highest ES version accepted in
     * es_max_supported (major, minor). Returns EGL_FALSE if the libraries are
     * not present or fail to initialise; in that case the ES path is silently
     * disabled and the desktop GL path continues to work normally.
     */
    EGLBoolean gles_init(void* nativeDisplay, EGLint* es_max_supported);

    /**
     * Load libEGL.so.1 / libGLESv2.so.2 and create a system EGLDisplay backed
     * by the given Wayland display. Same semantics as gles_init() but uses
     * EGL_PLATFORM_WAYLAND_EXT. Returns EGL_FALSE if the libraries are absent.
     */
    EGLBoolean gles_init_wayland(void* wlDisplay, EGLint* es_max_supported);

    /** Tear down the system EGL display and unload its libraries. */
    void gles_terminate(void);

    /** Returns EGL_TRUE if gles_init succeeded and the GLES path is usable. */
    EGLBoolean gles_isAvailable(void);

    /** Convenience fill for the seven format fields of an EGLConfig. */
    static inline GlesSurfaceFormat glesMakeFormat(EGLint red, EGLint green, EGLint blue, EGLint alpha,
                                                   EGLint depth, EGLint stencil, EGLint samples)
    {
        GlesSurfaceFormat format;
        format.red     = red;
        format.green   = green;
        format.blue    = blue;
        format.alpha   = alpha;
        format.depth   = depth;
        format.stencil = stencil;
        format.samples = samples;
        return format;
    }

    /**
     * Create a system EGL window surface for the given native window,
     * format and colorspace. out_surface receives the opaque EGLSurface
     * pointer.
     */
    EGLBoolean gles_createWindowSurface(void* win, const GlesSurfaceFormat* format, EGLint colorspace, void** out_surface);

    /**
     * Create a system EGL pbuffer surface of the requested size and format.
     * largestPbuffer maps to EGL_LARGEST_PBUFFER; a non-default sRGB color
     * space is passed through. Needs the pbuffer-capable system config -
     * returns EGL_FALSE before any allocation when the system stack has none.
     */
    EGLBoolean gles_createPbufferSurface(EGLint width, EGLint height, EGLBoolean largestPbuffer, EGLint colorspace, const GlesSurfaceFormat* format, void** out_surface);

    /** Query a surface attribute (e.g. EGL_WIDTH) of a delegated surface. */
    EGLBoolean gles_querySurface(void* surface, EGLint attribute, EGLint* value);

    /**
     * Forward an eglSurfaceAttrib (e.g. EGL_SWAP_BEHAVIOR) to the system
     * EGL surface behind the delegated surface.
     */
    EGLBoolean gles_surfaceAttrib(void* surface, EGLint attribute, EGLint value);

    /**
     * Create a system EGL window surface on Wayland for the given wl_surface
     * and size. The native window is mirrored through libwayland-egl
     * (wl_egl_window_create) - the system EGL expects that library's
     * wl_egl_window, whose struct layout is private. out_surface receives a
     * handle for gles_makeCurrent / gles_swapBuffers / gles_destroySurface.
     */
    EGLBoolean gles_createWindowSurfaceWayland(void* wlSurface, int width, int height, const GlesSurfaceFormat* format, EGLint colorspace, void** out_surface);

    /**
     * Mirror a native-window resize into the libwayland-egl window behind a
     * Wayland surface (EGL 1.5 section 3.10.1.1). No-op when unchanged.
     */
    EGLBoolean gles_resizeWindowWayland(void* surface, int width, int height);

    /**
     * Create a system EGL ES context from the system config that matches the
     * given format. major/minor select the requested ES version. share may be
     * NULL. Contexts are always created from the same system config as the
     * surface they will bind, because EGL requires that pairing.
     */
    EGLBoolean gles_createContext(const GlesSurfaceFormat* format, EGLint major, EGLint minor, void* share, void** out_context);

    EGLBoolean gles_makeCurrent(void* surface, void* context);
    EGLBoolean gles_swapBuffers(void* surface);
    EGLBoolean gles_swapInterval(EGLint interval);
    EGLBoolean gles_destroySurface(void* surface);
    EGLBoolean gles_destroyContext(void* context);

    /**
     * Create a system EGLImage in a delegated ES context. The system stack's
     * image consumption entry points dereference the handle with its own
     * object model, so images of such contexts must live there. out_error
     * receives the system EGL error code when creation fails.
     */
    EGLBoolean gles_createImageKHR(void* context, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list, void** out_image, EGLint* out_error);

    /** Destroy a system EGLImage. */
    EGLBoolean gles_destroyImageKHR(void* image);

    /**
     * Consume a system EGLImage in the current system context
     * (GL_OES_EGL_image). No-ops when the system GLES library does not
     * export the entry point.
     */
    void gles_imageTargetTexture2D(EGLenum target, void* image);

    /** Same as gles_imageTargetTexture2D for renderbuffers. */
    void gles_imageTargetRenderbufferStorage(EGLenum target, void* image);

#ifdef __cplusplus
}
#endif
#endif  // EGL_LINUX_GLES_H

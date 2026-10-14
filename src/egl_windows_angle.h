/**
 * ANGLE backend for OpenGL ES on Windows.
 *
 * Loads ANGLE's libEGL.dll / libGLESv2.dll dynamically at runtime and
 * delegates ES context/surface lifecycle calls to it. The desktop OpenGL
 * path remains owned by WGL inside egl_windows.cpp; this file only handles
 * the ES branch.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#ifndef EGL_WINDOWS_ANGLE_H
#define EGL_WINDOWS_ANGLE_H

#include <windows.h>
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Load ANGLE DLLs and create an ANGLE EGLDisplay backed by D3D11.
     * Reports the highest ES version ANGLE accepts in es_max_supported (major,
     * minor). Returns EGL_FALSE if ANGLE is not present or fails to initialize;
     * in that case the ES path is silently disabled and the desktop GL path
     * continues to work normally.
     */
    EGLBoolean angle_init(EGLint* es_max_supported);

    /** Tear down the ANGLE display and unload its DLLs. */
    void angle_terminate(void);

    /** Returns EGL_TRUE if angle_init succeeded and ANGLE is usable. */
    EGLBoolean angle_isAvailable(void);

    /**
     * Create an ANGLE window surface for the given HWND. Does NOT call
     * SetPixelFormat — ANGLE manages the pixel format internally via D3D11.
     */
    EGLBoolean angle_createWindowSurface(HWND hwnd, void** out_surface);

    /**
     * Create an ANGLE ES context. major/minor select the requested ES version.
     * share may be NULL.
     */
    EGLBoolean angle_createContext(EGLint major, EGLint minor, void* share, void** out_context);

    EGLBoolean angle_makeCurrent(void* surface, void* context);
    EGLBoolean angle_swapBuffers(void* surface);
    EGLBoolean angle_swapInterval(EGLint interval);
    EGLBoolean angle_destroySurface(void* surface);
    EGLBoolean angle_destroyContext(void* context);

    /**
     * Create an ANGLE EGLImage in a delegated ES context. ANGLE's image
     * consumption entry points dereference the handle with its own object
     * model, so images of such contexts must live there. out_error receives
     * ANGLE's EGL error code when creation fails.
     */
    EGLBoolean angle_createImageKHR(void* context, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list, void** out_image, EGLint* out_error);

    /** Destroy an ANGLE EGLImage. */
    EGLBoolean angle_destroyImageKHR(void* image);

    /**
     * Consume an ANGLE EGLImage in the current ANGLE context
     * (GL_OES_EGL_image). No-ops when ANGLE's GLES library does not export
     * the entry point.
     */
    void angle_imageTargetTexture2D(EGLenum target, void* image);

    /** Same as angle_imageTargetTexture2D for renderbuffers. */
    void angle_imageTargetRenderbufferStorage(EGLenum target, void* image);

#ifdef __cplusplus
}
#endif
#endif  // EGL_WINDOWS_ANGLE_H

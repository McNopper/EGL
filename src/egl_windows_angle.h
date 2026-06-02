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

#pragma once

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

#ifdef __cplusplus
}
#endif

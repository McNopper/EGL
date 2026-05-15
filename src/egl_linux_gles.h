/**
 * System libEGL/libGLESv2 backend for OpenGL ES on Linux/X11.
 *
 * Loads the system EGL (libEGL.so.1) and GLES (libGLESv2.so.2) libraries
 * dynamically at runtime and delegates ES context/surface lifecycle to them.
 * The desktop GL path remains owned by GLX inside egl_x11_glx.cpp; this
 * file only handles the ES branch.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#pragma once

#include <X11/Xlib.h>
#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load libEGL.so.1 / libGLESv2.so.2 and create a system EGLDisplay backed
 * by the given X11 display. Reports the highest ES version accepted in
 * es_max_supported (major, minor). Returns EGL_FALSE if the libraries are
 * not present or fail to initialise; in that case the ES path is silently
 * disabled and the desktop GL path continues to work normally.
 */
EGLBoolean gles_init(Display* x11Display, EGLint* es_max_supported);

/** Tear down the system EGL display and unload its libraries. */
void gles_terminate(void);

/** Returns EGL_TRUE if gles_init succeeded and the GLES path is usable. */
EGLBoolean gles_isAvailable(void);

/**
 * Create a system EGL window surface for the given X11 Window.
 * out_surface receives the opaque EGLSurface pointer.
 */
EGLBoolean gles_createWindowSurface(Window win, void** out_surface);

/**
 * Create a system EGL ES context. major/minor select the requested ES version.
 * share may be NULL.
 */
EGLBoolean gles_createContext(EGLint major, EGLint minor, void* share, void** out_context);

EGLBoolean gles_makeCurrent(void* surface, void* context);
EGLBoolean gles_swapBuffers(void* surface);
EGLBoolean gles_swapInterval(EGLint interval);
EGLBoolean gles_destroySurface(void* surface);
EGLBoolean gles_destroyContext(void* context);

#ifdef __cplusplus
}
#endif

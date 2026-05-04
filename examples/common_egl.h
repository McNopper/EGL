/**
 * Shared EGL application helpers.
 *
 * egl_app_create  — opens display, picks config, creates window/surface/context
 * egl_app_run     — pumps events and calls the frame callback each iteration
 * egl_app_destroy — tears everything down in the correct order
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#pragma once

#include "common.h"
#include <EGL/eglext.h>

struct EGLApp
{
    EGLNativeDisplayType native_dpy;
    EGLDisplay           dpy;
    EGLSurface           surf;
    EGLContext           ctx;
    __NativeWindow*      win;
};

/**
 * Create a fully initialized EGL application context.
 *
 * config_attribs  — EGL config selection attributes, terminated by EGL_NONE
 * surface_attribs — passed to eglCreateWindowSurface, terminated by EGL_NONE
 * ext_required    — if non-NULL, fails early when the extension is absent
 * title           — window title
 * width, height   — initial window size in pixels
 *
 * Returns NULL on any failure; all resources are cleaned up before returning.
 */
EGLApp* egl_app_create(const EGLint* config_attribs,
                       const EGLint* surface_attribs,
                       const char*   ext_required,
                       const char*   title,
                       int width, int height,
                       EGLenum       api = EGL_OPENGL_API);

/**
 * Run the render loop.
 * Calls frame_cb(app, user) each iteration until the window is closed.
 */
void egl_app_run(EGLApp* app,
                 void (*frame_cb)(EGLApp* app, void* user),
                 void* user);

/**
 * Destroy surfaces, context, window and display.
 * Safe to call with NULL.
 */
void egl_app_destroy(EGLApp* app);

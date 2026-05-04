/**
 * EGL Green Window — minimal SDR green window example.
 *
 * Builds twice when ANGLE is enabled on Windows:
 *   - green_window_GL_Windows_WGL_VK.exe   — desktop OpenGL via WGL
 *   - green_window_ES_Windows_ANGLE.exe    — OpenGL ES via ANGLE
 *
 * Identical visual result; only the API/backend differs.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "../common_egl.h"

#include <GL/gl.h>
#include <stdio.h>

#ifndef GREEN_WINDOW_API
#define GREEN_WINDOW_API EGL_OPENGL_API
#endif

#ifndef GREEN_WINDOW_RENDERABLE_BIT
#define GREEN_WINDOW_RENDERABLE_BIT EGL_OPENGL_BIT
#endif

#ifndef GREEN_WINDOW_TITLE
#define GREEN_WINDOW_TITLE "EGL Green Window (GL)"
#endif

static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, GREEN_WINDOW_RENDERABLE_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_DEPTH_SIZE,      24,
    EGL_NONE
};

static const EGLint k_surface_attribs[] = {
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
    EGL_NONE
};

static void frame(EGLApp* app, void*)
{
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  nullptr, GREEN_WINDOW_TITLE, 800, 600,
                                  GREEN_WINDOW_API);
    if (!app)
        return 1;

    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}

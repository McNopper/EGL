/**
 * EGL Display P3 — vivid green (fully-saturated P3 green)
 * (Windows only)
 *
 * Display P3 uses the sRGB transfer function (same encoding as sRGB) but a
 * wider color gamut. A pure (0, 1, 0) green here is the Display P3 primary
 * green, which is more saturated than the sRGB primary green.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "../common_egl.h"

#include <GL/gl.h>
#include <stdio.h>

static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_DEPTH_SIZE, 24,
    EGL_NONE
};

static const EGLint k_surface_attribs[] = {
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_DISPLAY_P3_EXT,
    EGL_NONE
};

static void frame(EGLApp* app, void*)
{
    /* Pure Display P3 green primary — wider gamut than sRGB green */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_display_p3",
                                  "EGL Display P3", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=1.0 (fully-saturated Display P3 primary green)\n");
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}


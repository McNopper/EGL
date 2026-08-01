/**
 * EGL sRGB — SDR green window (EGL_GL_COLORSPACE_SRGB)
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "../common_egl.h"

#include <GL/gl.h>
#include <stdio.h>

// EGL has no config attribute for sRGB framebuffer capability, and drivers commonly expose
// it on 8 bit per component formats only. Requesting an alpha channel keeps the total colour
// bit count (EGL 1.5 sorting rule 3) from promoting a 10 bit config ahead of an 8 bit one.
static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_DEPTH_SIZE,      24,
    EGL_NONE
};

static const EGLint k_surface_attribs[] = {
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_SRGB,
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
                                  nullptr, "EGL sRGB (SDR)", 800, 600);
    if (!app)
        return 1;

    printf("Colorspace: EGL_GL_COLORSPACE_SRGB (SDR, sRGB gamma)\n");
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}

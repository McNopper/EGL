/**
 * EGL scRGB Linear — ~500 nits green
 *
 * WGL pixel format is 8-bit. The fp16 HDR precision comes from the Vulkan
 * swapchain (R16G16B16A16_SFLOAT / EXTENDED_SRGB_LINEAR) via GL-Vulkan interop.
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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT,
    EGL_NONE
};

static void frame(EGLApp* app, void* user)
{
    float clear_g = *(float*)user;
    glClearColor(0.0f, clear_g, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    /* scRGB linear: 1.0 = 80 nits SDR reference white */
    float clear_g = 500.0f / 80.0f;

    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_scrgb_linear",
                                  "EGL scRGB Linear (HDR)", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=%.3f (~500 nits, scRGB linear)\n", clear_g);
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, &clear_g);
    egl_app_destroy(app);
    return 0;
}

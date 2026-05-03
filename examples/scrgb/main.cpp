/**
 * EGL scRGB (gamma) — ~500 nits green
 *
 * WGL pixel format is 8-bit. The fp16 HDR precision comes from the Vulkan
 * swapchain (R16G16B16A16_SFLOAT / EXTENDED_SRGB_NONLINEAR) via GL-Vulkan interop.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "../common_egl.h"

#include <GL/gl.h>
#include <math.h>
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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_SCRGB_EXT,
    EGL_NONE
};

/* scRGB gamma: sRGB transfer function extended to HDR range. 1.0 = 80 nits. */
static float scrgb_encode(float nits)
{
    float L = nits / 80.0f;
    if (L <= 0.0031308f)
        return 12.92f * L;
    return 1.055f * powf(L, 1.0f / 2.4f) - 0.055f;
}

static void frame(EGLApp* app, void* user)
{
    float clear_g = *(float*)user;
    glClearColor(0.0f, clear_g, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    float clear_g = scrgb_encode(500.0f);

    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_scrgb",
                                  "EGL scRGB (HDR)", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=%.3f (~500 nits, scRGB gamma)\n", clear_g);
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, &clear_g);
    egl_app_destroy(app);
    return 0;
}

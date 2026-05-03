/**
 * EGL Display P3 Linear — vivid green at reference white
 *
 * WGL pixel format is 8-bit. The fp16 precision comes from the Vulkan
 * swapchain (R16G16B16A16_SFLOAT / DISPLAY_P3_LINEAR) via GL-Vulkan interop.
 *
 * Linear Display P3: 1.0 = reference white (~80 nits SDR). Values are linear
 * light in the Display P3 color gamut with no transfer function applied.
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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT,
    EGL_NONE
};

static void frame(EGLApp* app, void*)
{
    /* Display P3 green primary at reference white, linear light */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_display_p3_linear",
                                  "EGL Display P3 Linear", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=1.0 (Display P3 green primary at reference white, linear light)\n");
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}

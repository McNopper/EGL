/**
 * EGL Display P3 Passthrough — vivid green (Display P3, no compositor conversion)
 * (Windows only)
 *
 * Like EGL_GL_COLORSPACE_DISPLAY_P3_EXT but signals that the compositor should
 * pass framebuffer values through without applying any additional color
 * conversion. The content is still encoded in Display P3 (sRGB transfer
 * function, P3 gamut); color management is the application's responsibility.
 *
 * On this Vulkan-backed implementation both DISPLAY_P3_EXT and
 * DISPLAY_P3_PASSTHROUGH_EXT resolve to the same swapchain
 * (VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT / R8G8B8A8_UNORM) because the
 * compositor distinction does not apply at the Vulkan swapchain level.
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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT,
    EGL_NONE
};

static void frame(EGLApp* app, void*)
{
    /* Display P3 green primary — compositor passes values through unchanged */
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_p3_passthrough",
                                  "EGL Display P3 Passthrough", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=1.0 (Display P3 primary green, passthrough — no compositor conversion)\n");
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}

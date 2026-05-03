/**
 * EGL BT.2020 HLG — ~500 nits green
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
    EGL_RED_SIZE,   10,
    EGL_GREEN_SIZE, 10,
    EGL_BLUE_SIZE,  10,
    EGL_DEPTH_SIZE, 24,
    EGL_NONE
};

static const EGLint k_surface_attribs[] = {
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_BT2020_HLG_EXT,
    EGL_NONE
};

static float hlg_encode(float E)
{
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;
    if (E <= 1.0f / 12.0f)
        return sqrtf(3.0f * E);
    return a * logf(12.0f * E - b) + c;
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
    float clear_g = hlg_encode(0.5f);

    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_bt2020_hlg",
                                  "EGL BT.2020 HLG (HDR)", 800, 600);
    if (!app)
        return 1;

    printf("Clear: G=%.4f (HLG-encoded ~500 nits)\n", clear_g);
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, &clear_g);
    egl_app_destroy(app);
    return 0;
}

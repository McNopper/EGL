/**
 * EGL BT.2020 PQ (HDR10) — ~500 nits green with SMPTE 2086 metadata
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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_BT2020_PQ_EXT,
    EGL_NONE
};

static float pq_encode(float nits)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    float Y  = nits / 10000.0f;
    float Ym = powf(Y, m1);
    return powf((c1 + c2 * Ym) / (1.0f + c3 * Ym), m2);
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
    float clear_g = pq_encode(500.0f);

    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs,
                                  "EGL_EXT_gl_colorspace_bt2020_pq",
                                  "EGL BT.2020 PQ (HDR)", 800, 600);
    if (!app)
        return 1;

    /* SMPTE 2086 HDR metadata */
    static const EGLint smpte2086[] = {
        EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT, (EGLint)(0.708f  * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT, (EGLint)(0.292f  * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT, (EGLint)(0.170f  * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT, (EGLint)(0.797f  * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT, (EGLint)(0.131f  * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT, (EGLint)(0.046f  * 50000),
        EGL_SMPTE2086_WHITE_POINT_X_EXT,      (EGLint)(0.3127f * 50000),
        EGL_SMPTE2086_WHITE_POINT_Y_EXT,      (EGLint)(0.3290f * 50000),
        EGL_SMPTE2086_MAX_LUMINANCE_EXT,      (EGLint)(1000.0f * 10000),
        EGL_SMPTE2086_MIN_LUMINANCE_EXT,      (EGLint)(0.001f  * 10000),
        EGL_NONE
    };
    for (int i = 0; smpte2086[i] != EGL_NONE; i += 2)
        eglSurfaceAttrib(app->dpy, app->surf, smpte2086[i], smpte2086[i + 1]);

    printf("Clear: G=%.4f (PQ-encoded 500 nits)\n", clear_g);
    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, &clear_g);
    egl_app_destroy(app);
    return 0;
}

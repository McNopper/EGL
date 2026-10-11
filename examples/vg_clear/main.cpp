/**
 * EGL VG Clear - minimal OpenVG smoke example for the VG provider seam.
 *
 * Registers a stub OpenVG provider that fills its framebuffer with green and
 * renders it through eglBindAPI(EGL_OPENVG_API) - proving the EGL side of the
 * OpenVG client API (configs, contexts, surfaces, presentation) without any
 * OpenVG implementation. McNopper/OpenVG_Library registers the real provider
 * in exactly this way.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "../common_egl.h"

#include "../../src/eglvg.h"

#include <stdio.h>

static void*  g_framebuffer = nullptr;
static EGLint g_width       = 0;
static EGLint g_height      = 0;
static EGLint g_stride      = 0;

static EGLBoolean EGLAPIENTRY vgCreateContext(void** outContext)
{
    static int s_context;

    *outContext = &s_context;

    return EGL_TRUE;
}

static EGLBoolean EGLAPIENTRY vgDestroyContext(void* context)
{
    (void)context;

    return EGL_TRUE;
}

static EGLBoolean EGLAPIENTRY vgMakeCurrent(void* context, void* framebuffer, EGLint format, EGLint width, EGLint height, EGLint strideBytes)
{
    (void)context;
    (void)format;

    g_framebuffer = framebuffer;
    g_width       = width;
    g_height      = height;
    g_stride      = strideBytes;

    // The stub "renders" once at bind time: green in VG_sARGB_8888 (B,G,R,A).
    if (g_framebuffer)
    {
        for (EGLint row = 0; row < g_height; ++row)
        {
            unsigned char* pixels = reinterpret_cast<unsigned char*>(g_framebuffer) + static_cast<size_t>(row) * static_cast<size_t>(g_stride);

            for (EGLint column = 0; column < g_width; ++column)
            {
                pixels[column * 4 + 0] = 0x00; // blue
                pixels[column * 4 + 1] = 0xFF; // green
                pixels[column * 4 + 2] = 0x00; // red
                pixels[column * 4 + 3] = 0xFF; // alpha
            }
        }
    }

    return EGL_TRUE;
}

static void EGLAPIENTRY vgNotifySwap(void* context)
{
    (void)context;
}

static const EGLVGProvider k_provider = {vgCreateContext, vgDestroyContext, vgMakeCurrent, vgNotifySwap};

static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_ALPHA_SIZE,      8,
    EGL_NONE};

static const EGLint k_surface_attribs[] = {
    EGL_NONE};

static void frame(EGLApp* app, void*)
{
    eglSwapBuffers(app->dpy, app->surf);
}

int main(void)
{
    if (!eglSetVGProvider(&k_provider))
        return 1;

    EGLApp* app = egl_app_create(k_config_attribs, k_surface_attribs, nullptr, "EGL VG Clear", 800, 600, EGL_OPENVG_API);
    if (!app)
        return 1;

    printf("Press Escape or close the window to exit.\n");

    egl_app_run(app, frame, nullptr);
    egl_app_destroy(app);
    return 0;
}

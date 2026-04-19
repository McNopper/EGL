/**
 * EGL BT.2020 PQ (HDR10) — ~500 nits green with SMPTE 2086 metadata
 * (Windows only)
 */

#include "../common.h"

#include <GL/gl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <math.h>
#include <stdio.h>

static const int WIDTH  = 800;
static const int HEIGHT = 600;

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

static const EGLint k_context_attribs[] = { EGL_NONE };

static float pq_encode(float nits) {
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    float Y  = nits / 10000.0f;
    float Ym = powf(Y, m1);
    return powf((c1 + c2 * Ym) / (1.0f + c3 * Ym), m2);
}


int main(void)
{
    float clear_g = pq_encode(500.0f);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d\n", major, minor);

    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!ext_supported(exts, "EGL_EXT_gl_colorspace_bt2020_pq")) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_bt2020_pq not supported\n");
        eglTerminate(dpy);
        return 1;
    }
    printf("EGL_EXT_gl_colorspace_bt2020_pq: supported\n");

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, k_config_attribs, &cfg, 1, &ncfg);
    if (!ncfg) {
        fprintf(stderr, "No matching EGL config\n");
        eglTerminate(dpy);
        return 1;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "EGLbt2020pq";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, "EGLbt2020pq", "EGL BT.2020 PQ (HDR)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, k_surface_attribs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_bt2020_pq not supported (eglCreateWindowSurface failed: 0x%x)\n", eglGetError());
        DestroyWindow(hwnd);
        eglTerminate(dpy);
        return 1;
    }

    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, k_context_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed\n");
        eglDestroySurface(dpy, surf);
        DestroyWindow(hwnd);
        eglTerminate(dpy);
        return 1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        eglDestroyContext(dpy, ctx);
        eglDestroySurface(dpy, surf);
        DestroyWindow(hwnd);
        eglTerminate(dpy);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* SMPTE 2086 HDR metadata */
    const EGLint smpte2086[] = {
        EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT, (EGLint)(0.708f * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT, (EGLint)(0.292f * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT, (EGLint)(0.170f * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT, (EGLint)(0.797f * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT, (EGLint)(0.131f * 50000),
        EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT, (EGLint)(0.046f * 50000),
        EGL_SMPTE2086_WHITE_POINT_X_EXT,      (EGLint)(0.3127f * 50000),
        EGL_SMPTE2086_WHITE_POINT_Y_EXT,      (EGLint)(0.3290f * 50000),
        EGL_SMPTE2086_MAX_LUMINANCE_EXT,      (EGLint)(1000.0f * 10000),
        EGL_SMPTE2086_MIN_LUMINANCE_EXT,      (EGLint)(0.001f * 10000),
        EGL_NONE
    };
    for (int i = 0; smpte2086[i] != EGL_NONE; i += 2)
        eglSurfaceAttrib(dpy, surf, smpte2086[i], smpte2086[i+1]);

    printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("Clear: G=%.4f (PQ-encoded 500 nits)\n", clear_g);
    printf("Press Escape or close the window to exit.\n");

    MSG msg = {};
    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        glClearColor(0.0f, clear_g, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, surf);
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    DestroyWindow(hwnd);
    return 0;
}
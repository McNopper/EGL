/**
 * EGL scRGB Linear — ~500 nits green
 * (Windows only)
 *
 * WGL pixel format is 8-bit (standard). The fp16 HDR precision comes from the
 * Vulkan swapchain (R16G16B16A16_SFLOAT / EXTENDED_SRGB_LINEAR) via GL-Vulkan interop.
 */

#include "../common.h"

#include <GL/gl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <stdio.h>

static const int WIDTH  = 800;
static const int HEIGHT = 600;

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

static const EGLint k_context_attribs[] = { EGL_NONE };


int main(void)
{
    /* scRGB linear: 1.0 = 80 nits SDR reference white */
    const float clear_g = 500.0f / 80.0f;

    /* --- EGL init + extension check (before window creation) --- */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d\n", major, minor);

    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!ext_supported(exts, "EGL_EXT_gl_colorspace_scrgb_linear")) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_scrgb_linear not supported\n");
        eglTerminate(dpy);
        return 1;
    }
    printf("EGL_EXT_gl_colorspace_scrgb_linear: supported\n");

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, k_config_attribs, &cfg, 1, &ncfg);
    if (!ncfg) {
        fprintf(stderr, "No matching EGL config\n");
        eglTerminate(dpy);
        return 1;
    }

    /* --- Create Win32 window (hidden until surface is ready) --- */
    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "EGLscRGBLinear";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, "EGLscRGBLinear", "EGL scRGB Linear (HDR)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);

    /* --- Create surface, context, make current --- */
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, k_surface_attribs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_scrgb_linear not supported (eglCreateWindowSurface failed: 0x%x)\n", eglGetError());
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

    printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("Clear: G=%.3f (~500 nits, scRGB linear)\n", clear_g);
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
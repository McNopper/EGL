/**
 * EGL BT.2020 HLG — ~500 nits green
 * (Windows only)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_BT2020_HLG_EXT,
    EGL_NONE
};

static const EGLint k_context_attribs[] = { EGL_NONE };

static float hlg_encode(float E) {
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;
    if (E <= 1.0f / 12.0f)
        return sqrtf(3.0f * E);
    return a * logf(12.0f * E - b) + c;
}

static bool g_running = true;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: case WM_DESTROY:
        g_running = false; PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_running = false; PostQuitMessage(0); } return 0;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static bool ext_supported(const char* exts, const char* ext)
{
    if (!exts) return false;
    const char* p = strstr(exts, ext);
    if (!p) return false;
    char after = p[strlen(ext)];
    return after == ' ' || after == '\0';
}

int main(void)
{
    float clear_g = hlg_encode(0.5f);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d\n", major, minor);

    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!ext_supported(exts, "EGL_EXT_gl_colorspace_bt2020_hlg")) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_bt2020_hlg not supported\n");
        eglTerminate(dpy);
        return 1;
    }
    printf("EGL_EXT_gl_colorspace_bt2020_hlg: supported\n");

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
    wc.lpszClassName = "EGLbt2020hlg";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, "EGLbt2020hlg", "EGL BT.2020 HLG (HDR)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, k_surface_attribs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_bt2020_hlg not supported (eglCreateWindowSurface failed: 0x%x)\n", eglGetError());
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
    printf("Clear: G=%.4f (HLG-encoded ~500 nits)\n", clear_g);
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
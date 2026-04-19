/**
 * EGL Linear — SDR green window (EGL_GL_COLORSPACE_LINEAR)
 * (Windows only)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <EGL/egl.h>

#include <stdio.h>

static const int WIDTH  = 800;
static const int HEIGHT = 600;

static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_DEPTH_SIZE,      24,
    EGL_NONE
};

/* EGL_GL_COLORSPACE_LINEAR is the default, but request it explicitly. */
static const EGLint k_surface_attribs[] = {
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
    EGL_NONE
};

static const EGLint k_context_attribs[] = { EGL_NONE };

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

int main(void)
{
    /* --- EGL initialisation (before window creation) --- */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d\n", major, minor);

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, k_config_attribs, &cfg, 1, &ncfg);
    if (!ncfg) {
        fprintf(stderr, "No matching EGL config\n");
        eglTerminate(dpy);
        return 1;
    }

    /* --- Create Win32 window --- */
    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "EGLLinear";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, "EGLLinear", "EGL Linear (SDR)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* --- Create surface, context, make current --- */
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, k_surface_attribs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface failed (0x%x)\n", eglGetError());
        eglTerminate(dpy);
        return 1;
    }

    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, k_context_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed\n");
        eglTerminate(dpy);
        return 1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "eglMakeCurrent failed\n");
        eglTerminate(dpy);
        return 1;
    }

    printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    printf("Colorspace: EGL_GL_COLORSPACE_LINEAR (SDR, raw linear)\n");
    printf("Press Escape or close the window to exit.\n");

    MSG msg = {};
    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
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

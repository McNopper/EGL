/**
 * EGL green window example.
 *
 * Creates a native window, initialises EGL 1.5, and renders a solid green
 * background until the window is closed or Escape is pressed.
 *
 * Platforms: Windows (Win32/WGL) and Linux (X11/GLX).
 */

/* ── Platform includes ─────────────────────────────────────────────── */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <GL/gl.h>
#else
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/keysym.h>
#  include <GL/gl.h>
#endif

#include <EGL/egl.h>
#include <stdio.h>

static const int WIDTH  = 800;
static const int HEIGHT = 600;

/* EGL configuration: OpenGL window surface, 8-bit RGB, 24-bit depth */
static const EGLint k_config_attribs[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE,        8,
    EGL_GREEN_SIZE,      8,
    EGL_BLUE_SIZE,       8,
    EGL_DEPTH_SIZE,      24,
    EGL_NONE
};

/* Request an OpenGL context (no specific version; EGL picks the highest available) */
static const EGLint k_context_attribs[] = {
    EGL_NONE
};

/* ══════════════════════════════════════════════════════════════════════
   Windows
   ══════════════════════════════════════════════════════════════════════ */
#ifdef _WIN32

static bool g_running = true;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_running = false; PostQuitMessage(0); }
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

int main(void)
{
    /* --- Create Win32 window --- */
    HINSTANCE hInst = GetModuleHandle(NULL);

    WNDCLASSEX wc   = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "EGLGreenWindow";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0, "EGLGreenWindow", "EGL Green Window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right  - rect.left,
        rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* --- EGL initialisation --- */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        MessageBox(NULL, "eglInitialize failed", "EGL Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    printf("EGL %d.%d initialised\n", major, minor);

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg  = NULL;
    EGLint    ncfg = 0;
    eglChooseConfig(dpy, k_config_attribs, &cfg, 1, &ncfg);
    if (!ncfg) {
        MessageBox(NULL, "No matching EGL config", "EGL Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, NULL);
    if (surf == EGL_NO_SURFACE) {
        MessageBox(NULL, "eglCreateWindowSurface failed", "EGL Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    EGLContext ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, k_context_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        MessageBox(NULL, "eglCreateContext failed", "EGL Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        MessageBox(NULL, "eglMakeCurrent failed", "EGL Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* --- Render loop --- */
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

    /* --- Cleanup --- */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    DestroyWindow(hwnd);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
   X11
   ══════════════════════════════════════════════════════════════════════ */
#else

int main(void)
{
    /* --- Open X11 display and create window --- */
    Display* xdpy = XOpenDisplay(NULL);
    if (!xdpy) { fprintf(stderr, "Cannot open X display\n"); return 1; }

    Window root = DefaultRootWindow(xdpy);

    XSetWindowAttributes swa = {};
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    Window xwin = XCreateWindow(
        xdpy, root,
        0, 0, (unsigned)WIDTH, (unsigned)HEIGHT, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &swa);

    Atom wmDelete = XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xdpy, xwin, &wmDelete, 1);
    XStoreName(xdpy, xwin, "EGL Green Window");
    XMapWindow(xdpy, xwin);

    /* --- EGL initialisation --- */
    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)xdpy);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d initialised\n", major, minor);

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg  = NULL;
    EGLint    ncfg = 0;
    eglChooseConfig(dpy, k_config_attribs, &cfg, 1, &ncfg);
    if (!ncfg) { fprintf(stderr, "No matching EGL config\n"); return 1; }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)xwin, NULL);
    EGLContext ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, k_context_attribs);
    eglMakeCurrent(dpy, surf, surf, ctx);

    /* --- Render loop --- */
    bool running = true;
    while (running) {
        while (XPending(xdpy)) {
            XEvent ev;
            XNextEvent(xdpy, &ev);
            if (ev.type == KeyPress) {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape) running = false;
            } else if (ev.type == ClientMessage) {
                if ((Atom)ev.xclient.data.l[0] == wmDelete) running = false;
            }
        }

        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, surf);
    }

    /* --- Cleanup --- */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    XDestroyWindow(xdpy, xwin);
    XCloseDisplay(xdpy);
    return 0;
}

#endif

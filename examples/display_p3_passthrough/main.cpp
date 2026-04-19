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
    EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT,
    EGL_NONE
};

static const EGLint k_context_attribs[] = { EGL_NONE };


int main(void)
{
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    printf("EGL %d.%d\n", major, minor);

    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (!ext_supported(exts, "EGL_EXT_gl_colorspace_p3_passthrough")) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_p3_passthrough not supported\n");
        eglTerminate(dpy);
        return 1;
    }
    printf("EGL_EXT_gl_colorspace_p3_passthrough: supported\n");

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
    wc.lpszClassName = "EGLDisplayP3PT";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, "EGLDisplayP3PT", "EGL Display P3 Passthrough",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)hwnd, k_surface_attribs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL_EXT_gl_colorspace_p3_passthrough not supported (eglCreateWindowSurface failed: 0x%x)\n", eglGetError());
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
    printf("Clear: G=1.0 (Display P3 primary green, passthrough — no compositor conversion)\n");
    printf("Press Escape or close the window to exit.\n");

    MSG msg = {};
    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        /* Display P3 green primary — compositor passes values through unchanged */
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

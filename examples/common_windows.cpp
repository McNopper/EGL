/**
 * Platform window helpers — Windows implementation.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "common.h"

struct __NativeWindow
{
    HWND hwnd;
    bool running;
};

static LRESULT CALLBACK s_wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    __NativeWindow* win = reinterpret_cast<__NativeWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_CLOSE:
        case WM_DESTROY:
            if (win) win->running = false;
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE)
            {
                if (win) win->running = false;
                PostQuitMessage(0);
            }
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
}

EGLNativeDisplayType __openDisplay()
{
    return EGL_DEFAULT_DISPLAY;
}

__NativeWindow* __createWindow(EGLNativeDisplayType, EGLint,
                                int width, int height, const char* title)
{
    HINSTANCE hInst = GetModuleHandle(nullptr);

    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = s_wndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "EGLWindow";
    RegisterClassEx(&wc);

    RECT rect = { 0, 0, (LONG)width, (LONG)height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(0, "EGLWindow", title,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd)
        return nullptr;

    __NativeWindow* win = new __NativeWindow{ hwnd, true };
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(win));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return win;
}

EGLNativeWindowType __nativeWindowHandle(__NativeWindow* win)
{
    return static_cast<EGLNativeWindowType>(win->hwnd);
}

bool __processEvents(__NativeWindow* win)
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return win->running;
}

void __destroyWindow(__NativeWindow* win)
{
    if (!win)
        return;
    DestroyWindow(win->hwnd);
    delete win;
}

void __closeDisplay(EGLNativeDisplayType) {}

const char* __osName()       { return "Windows"; }
const char* __windowingName() { return "WGL"; }
const char* __backendName()  { return "VK"; }

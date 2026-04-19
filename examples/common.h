/**
 * Shared helpers for all EGL examples.
 * (Windows only)
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

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

/**
 * Shared helpers for all EGL examples.
 *
 * main.cpp calls __openDisplay / __createWindow / __processEvents / __destroyWindow / __closeDisplay.
 * The implementations live in common_windows.cpp and common_x11.cpp — selected by CMake.
 * No platform guards belong here or in main.cpp.
 */

#pragma once

#include <EGL/egl.h>
#include <cstring>

// Opaque platform window — full definition is in the platform-specific common_*.cpp.
struct __NativeWindow;

// Open the native display (X11: XOpenDisplay; Windows: returns EGL_DEFAULT_DISPLAY).
EGLNativeDisplayType __openDisplay();

// Create a native window of the given size.
// visual_id comes from eglGetConfigAttrib(EGL_NATIVE_VISUAL_ID) and is used on X11.
__NativeWindow* __createWindow(EGLNativeDisplayType dpy, EGLint visual_id,
                               int width, int height, const char* title);

// Return the EGLNativeWindowType handle suitable for eglCreateWindowSurface.
EGLNativeWindowType __nativeWindowHandle(__NativeWindow* win);

// Pump pending events.  Returns false when the window should close.
bool __processEvents(__NativeWindow* win);

// Destroy the window and free its resources.
void __destroyWindow(__NativeWindow* win);

// Close / release the native display (X11: XCloseDisplay; Windows: no-op).
void __closeDisplay(EGLNativeDisplayType dpy);


// Return the OS name, e.g. "Windows" or "Linux".
const char* __osName();

// Return the windowing system name, e.g. "WGL", "X11", "Wayland".
const char* __windowingName();

// Return the rendering/present backend name, e.g. "VK", "GLX", "GLX+VK".
const char* __backendName();

static inline bool ext_supported(const char* exts, const char* ext)
{
    if (!exts) return false;
    const char* p = strstr(exts, ext);
    if (!p) return false;
    char after = p[strlen(ext)];
    return after == ' ' || after == '\0';
}


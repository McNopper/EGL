/**
 * System libEGL/libGLESv2 backend for OpenGL ES on Linux/X11 and Wayland.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "egl_linux_gles.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

// EGL constants — values are stable across implementations (Khronos EGL spec).
#ifndef EGL_PLATFORM_X11_EXT
#  define EGL_PLATFORM_X11_EXT 0x31D5
#endif
#ifndef EGL_PLATFORM_WAYLAND_EXT
#  define EGL_PLATFORM_WAYLAND_EXT 0x31D6
#endif

// System EGL function pointer types.
// We use void* for opaque EGLDisplay/EGLConfig/etc. to avoid header conflicts.
typedef void*     (*PFN_eglGetDisplay)(void*);
typedef void*     (*PFN_eglGetPlatformDisplayEXT)(unsigned int platform, void* native, const int* attribs);
typedef int       (*PFN_eglInitialize)(void* dpy, int* major, int* minor);
typedef int       (*PFN_eglTerminate)(void* dpy);
typedef int       (*PFN_eglChooseConfig)(void* dpy, const int* attrib_list, void** configs, int config_size, int* num_config);
typedef int       (*PFN_eglBindAPI)(unsigned int api);
typedef void*     (*PFN_eglCreateContext)(void* dpy, void* config, void* share, const int* attrib_list);
typedef void*     (*PFN_eglCreateWindowSurface)(void* dpy, void* config, void* win, const int* attrib_list);
typedef int       (*PFN_eglMakeCurrent)(void* dpy, void* draw, void* read, void* ctx);
typedef int       (*PFN_eglSwapBuffers)(void* dpy, void* surface);
typedef int       (*PFN_eglSwapInterval)(void* dpy, int interval);
typedef int       (*PFN_eglDestroyContext)(void* dpy, void* ctx);
typedef int       (*PFN_eglDestroySurface)(void* dpy, void* surface);
typedef int       (*PFN_eglGetError)(void);
typedef void*     (*PFN_eglGetProcAddress)(const char* procname);

// EGL_OPENGL_ES_API = 0x30A0
#define SYSGL_EGL_OPENGL_ES_API  0x30A0u
// EGL_NONE          = 0x3038
#define SYSGL_EGL_NONE           0x3038
// EGL_OPENGL_ES3_BIT = 0x0040,  EGL_OPENGL_ES2_BIT = 0x0004
#define SYSGL_EGL_OPENGL_ES3_BIT 0x0040
#define SYSGL_EGL_OPENGL_ES2_BIT 0x0004
// Attribute tokens
#define SYSGL_EGL_SURFACE_TYPE          0x3033
#define SYSGL_EGL_WINDOW_BIT            0x0004
#define SYSGL_EGL_RENDERABLE_TYPE       0x3040
#define SYSGL_EGL_RED_SIZE              0x3024
#define SYSGL_EGL_GREEN_SIZE            0x3023
#define SYSGL_EGL_BLUE_SIZE             0x3022
#define SYSGL_EGL_ALPHA_SIZE            0x3021
#define SYSGL_EGL_CONTEXT_MAJOR_VERSION 0x3098
#define SYSGL_EGL_CONTEXT_MINOR_VERSION 0x30FB

namespace
{

struct GlesState
{
    void* libEGL   { nullptr };
    void* libGLES  { nullptr };

    PFN_eglGetDisplay            eglGetDisplay           { nullptr };
    PFN_eglGetPlatformDisplayEXT eglGetPlatformDisplayEXT{ nullptr };
    PFN_eglInitialize            eglInitialize           { nullptr };
    PFN_eglTerminate             eglTerminate            { nullptr };
    PFN_eglChooseConfig          eglChooseConfig         { nullptr };
    PFN_eglBindAPI               eglBindAPI              { nullptr };
    PFN_eglCreateContext         eglCreateContext        { nullptr };
    PFN_eglCreateWindowSurface   eglCreateWindowSurface  { nullptr };
    PFN_eglMakeCurrent           eglMakeCurrent          { nullptr };
    PFN_eglSwapBuffers           eglSwapBuffers          { nullptr };
    PFN_eglSwapInterval          eglSwapInterval         { nullptr };
    PFN_eglDestroyContext        eglDestroyContext       { nullptr };
    PFN_eglDestroySurface        eglDestroySurface       { nullptr };
    PFN_eglGetError              eglGetError             { nullptr };
    PFN_eglGetProcAddress        eglGetProcAddress       { nullptr };

    void*  display { nullptr };
    void*  config  { nullptr };
    bool   ready   { false };
    int    esMax[2]{ 0, 0 };
};

GlesState g;

template <typename T>
static bool resolveSym(void* lib, T& out, const char* name)
{
    out = reinterpret_cast<T>(dlsym(lib, name));
    return out != nullptr;
}

static bool chooseDefaultConfig(int* outVersion)
{
    static const int cfgAttribs3[] = {
        SYSGL_EGL_SURFACE_TYPE,    SYSGL_EGL_WINDOW_BIT,
        SYSGL_EGL_RENDERABLE_TYPE, SYSGL_EGL_OPENGL_ES3_BIT,
        SYSGL_EGL_RED_SIZE,        8,
        SYSGL_EGL_GREEN_SIZE,      8,
        SYSGL_EGL_BLUE_SIZE,       8,
        SYSGL_EGL_ALPHA_SIZE,      8,
        SYSGL_EGL_NONE
    };

    int num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs3, &g.config, 1, &num) && num > 0)
    {
        outVersion[0] = 3;
        outVersion[1] = 0;
        return true;
    }

    static const int cfgAttribs2[] = {
        SYSGL_EGL_SURFACE_TYPE,    SYSGL_EGL_WINDOW_BIT,
        SYSGL_EGL_RENDERABLE_TYPE, SYSGL_EGL_OPENGL_ES2_BIT,
        SYSGL_EGL_RED_SIZE,        8,
        SYSGL_EGL_GREEN_SIZE,      8,
        SYSGL_EGL_BLUE_SIZE,       8,
        SYSGL_EGL_ALPHA_SIZE,      8,
        SYSGL_EGL_NONE
    };

    num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs2, &g.config, 1, &num) && num > 0)
    {
        outVersion[0] = 2;
        outVersion[1] = 0;
        return true;
    }

    return false;
}

} // anonymous namespace

extern "C" {

static EGLBoolean gles_init_platform(unsigned int platform, void* nativeDisplay, EGLint* es_max_supported)
{
    if (g.ready)
    {
        if (es_max_supported)
        {
            es_max_supported[0] = (EGLint)g.esMax[0];
            es_max_supported[1] = (EGLint)g.esMax[1];
        }
        return EGL_TRUE;
    }

    g.libEGL  = dlopen("libEGL.so.1",    RTLD_LAZY | RTLD_LOCAL);
    if (!g.libEGL)
        g.libEGL = dlopen("libEGL.so",   RTLD_LAZY | RTLD_LOCAL);
    g.libGLES = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!g.libGLES)
        g.libGLES = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);

    if (!g.libEGL || !g.libGLES)
    {
        gles_terminate();
        return EGL_FALSE;
    }

    bool ok =
        resolveSym(g.libEGL, g.eglGetDisplay,            "eglGetDisplay")            &&
        resolveSym(g.libEGL, g.eglInitialize,            "eglInitialize")            &&
        resolveSym(g.libEGL, g.eglTerminate,             "eglTerminate")             &&
        resolveSym(g.libEGL, g.eglChooseConfig,          "eglChooseConfig")          &&
        resolveSym(g.libEGL, g.eglBindAPI,               "eglBindAPI")               &&
        resolveSym(g.libEGL, g.eglCreateContext,         "eglCreateContext")         &&
        resolveSym(g.libEGL, g.eglCreateWindowSurface,   "eglCreateWindowSurface")   &&
        resolveSym(g.libEGL, g.eglMakeCurrent,           "eglMakeCurrent")           &&
        resolveSym(g.libEGL, g.eglSwapBuffers,           "eglSwapBuffers")           &&
        resolveSym(g.libEGL, g.eglSwapInterval,          "eglSwapInterval")          &&
        resolveSym(g.libEGL, g.eglDestroyContext,        "eglDestroyContext")        &&
        resolveSym(g.libEGL, g.eglDestroySurface,        "eglDestroySurface")        &&
        resolveSym(g.libEGL, g.eglGetError,              "eglGetError");

    if (!ok)
    {
        gles_terminate();
        return EGL_FALSE;
    }

    resolveSym(g.libEGL, g.eglGetPlatformDisplayEXT, "eglGetPlatformDisplayEXT");

    if (g.eglGetPlatformDisplayEXT && nativeDisplay)
        g.display = g.eglGetPlatformDisplayEXT(platform, nativeDisplay, nullptr);
    if (!g.display)
        g.display = g.eglGetDisplay(nativeDisplay);
    if (!g.display)
    {
        gles_terminate();
        return EGL_FALSE;
    }

    int sysMajor = 0, sysMinor = 0;
    if (!g.eglInitialize(g.display, &sysMajor, &sysMinor))
    {
        g.display = nullptr;
        gles_terminate();
        return EGL_FALSE;
    }

    if (!chooseDefaultConfig(g.esMax))
    {
        g.eglTerminate(g.display);
        g.display = nullptr;
        gles_terminate();
        return EGL_FALSE;
    }

    g.ready = true;
    if (es_max_supported)
    {
        es_max_supported[0] = (EGLint)g.esMax[0];
        es_max_supported[1] = (EGLint)g.esMax[1];
    }
    return EGL_TRUE;
}

EGLBoolean gles_init(void* nativeDisplay, EGLint* es_max_supported)
{
    return gles_init_platform(EGL_PLATFORM_X11_EXT, nativeDisplay, es_max_supported);
}

EGLBoolean gles_init_wayland(void* wlDisplay, EGLint* es_max_supported)
{
    return gles_init_platform(EGL_PLATFORM_WAYLAND_EXT, wlDisplay, es_max_supported);
}

void gles_terminate(void)
{
    if (g.display && g.eglTerminate)
    {
        g.eglTerminate(g.display);
        g.display = nullptr;
    }
    if (g.libGLES) { dlclose(g.libGLES); g.libGLES = nullptr; }
    if (g.libEGL)  { dlclose(g.libEGL);  g.libEGL  = nullptr; }
    g = GlesState{};
}

EGLBoolean gles_isAvailable(void)
{
    return g.ready ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean gles_createWindowSurface(void* win, void** out_surface)
{
    if (!g.ready || !out_surface)
        return EGL_FALSE;

    static const int surfaceAttribs[] = { SYSGL_EGL_NONE };
    *out_surface = g.eglCreateWindowSurface(g.display, g.config, win, surfaceAttribs);
    return *out_surface ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean gles_createWindowSurfaceWayland(void* wlEglWin, void** out_surface)
{
    if (!g.ready || !out_surface)
        return EGL_FALSE;

    static const int surfaceAttribs[] = { SYSGL_EGL_NONE };
    *out_surface = g.eglCreateWindowSurface(g.display, g.config, wlEglWin, surfaceAttribs);
    return *out_surface ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean gles_createContext(EGLint major, EGLint minor, void* share, void** out_context)
{
    if (!g.ready || !out_context)
        return EGL_FALSE;
    if (major < 2) major = 2;

    g.eglBindAPI(SYSGL_EGL_OPENGL_ES_API);

    const int ctxAttribs[] = {
        SYSGL_EGL_CONTEXT_MAJOR_VERSION, (int)major,
        SYSGL_EGL_CONTEXT_MINOR_VERSION, (int)minor,
        SYSGL_EGL_NONE
    };
    *out_context = g.eglCreateContext(g.display, g.config, share, ctxAttribs);
    return *out_context ? EGL_TRUE : EGL_FALSE;
}

EGLBoolean gles_makeCurrent(void* surface, void* context)
{
    if (!g.ready)
        return EGL_FALSE;
    if (!context && !surface)
        return (EGLBoolean)g.eglMakeCurrent(g.display, nullptr, nullptr, nullptr);
    return (EGLBoolean)g.eglMakeCurrent(g.display, surface, surface, context);
}

EGLBoolean gles_swapBuffers(void* surface)
{
    if (!g.ready || !surface)
        return EGL_FALSE;
    return (EGLBoolean)g.eglSwapBuffers(g.display, surface);
}

EGLBoolean gles_swapInterval(EGLint interval)
{
    if (!g.ready)
        return EGL_FALSE;
    return (EGLBoolean)g.eglSwapInterval(g.display, (int)interval);
}

EGLBoolean gles_destroySurface(void* surface)
{
    if (!g.ready || !surface)
        return EGL_FALSE;
    return (EGLBoolean)g.eglDestroySurface(g.display, surface);
}

EGLBoolean gles_destroyContext(void* context)
{
    if (!g.ready || !context)
        return EGL_FALSE;
    return (EGLBoolean)g.eglDestroyContext(g.display, context);
}

} // extern "C"

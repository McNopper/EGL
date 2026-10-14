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
#define EGL_PLATFORM_X11_EXT 0x31D5
#endif
#ifndef EGL_PLATFORM_WAYLAND_EXT
#define EGL_PLATFORM_WAYLAND_EXT 0x31D8
#endif

// System EGL function pointer types.
// We use void* for opaque EGLDisplay/EGLConfig/etc. to avoid header conflicts.
typedef void* (*PFN_eglGetDisplay)(void*);
typedef void* (*PFN_eglGetPlatformDisplayEXT)(unsigned int platform, void* native, const int* attribs);
typedef int (*PFN_eglInitialize)(void* dpy, int* major, int* minor);
typedef int (*PFN_eglTerminate)(void* dpy);
typedef int (*PFN_eglChooseConfig)(void* dpy, const int* attrib_list, void** configs, int config_size, int* num_config);
typedef int (*PFN_eglBindAPI)(unsigned int api);
typedef void* (*PFN_eglCreateContext)(void* dpy, void* config, void* share, const int* attrib_list);
typedef void* (*PFN_eglCreateImageKHR)(void* dpy, void* ctx, unsigned int target, void* buffer, const int* attrib_list);
typedef int (*PFN_eglDestroyImageKHR)(void* dpy, void* image);
typedef void (*PFN_glEGLImageTargetTexture2DOES)(unsigned int target, void* image);
typedef void (*PFN_glEGLImageTargetRenderbufferStorageOES)(unsigned int target, void* image);
typedef void* (*PFN_eglCreateWindowSurface)(void* dpy, void* config, void* win, const int* attrib_list);
typedef void* (*PFN_eglCreatePbufferSurface)(void* dpy, void* config, const int* attrib_list);
typedef int (*PFN_eglQuerySurface)(void* dpy, void* surface, int attribute, int* value);
typedef int (*PFN_eglMakeCurrent)(void* dpy, void* draw, void* read, void* ctx);
typedef int (*PFN_eglSwapBuffers)(void* dpy, void* surface);
typedef int (*PFN_eglSwapInterval)(void* dpy, int interval);
typedef int (*PFN_eglDestroyContext)(void* dpy, void* ctx);
typedef int (*PFN_eglDestroySurface)(void* dpy, void* surface);
typedef int (*PFN_eglGetError)(void);

// EGL_OPENGL_ES_API = 0x30A0
#define SYSGL_EGL_OPENGL_ES_API 0x30A0u
// EGL_NONE          = 0x3038
#define SYSGL_EGL_NONE 0x3038
// EGL_OPENGL_ES3_BIT = 0x0040,  EGL_OPENGL_ES2_BIT = 0x0004
#define SYSGL_EGL_OPENGL_ES3_BIT 0x0040
#define SYSGL_EGL_OPENGL_ES2_BIT 0x0004
// Attribute tokens
#define SYSGL_EGL_SURFACE_TYPE 0x3033
#define SYSGL_EGL_WINDOW_BIT 0x0004
#define SYSGL_EGL_PBUFFER_BIT 0x0001
#define SYSGL_EGL_WIDTH 0x3057
#define SYSGL_EGL_HEIGHT 0x3056
#define SYSGL_EGL_LARGEST_PBUFFER 0x3058
#define SYSGL_EGL_GL_COLORSPACE 0x309D
// Values from EGL 1.5 egl.h - wrong values here pass silently (the surface
// creates linear) and only show up as wrong framebuffer encodings.
#define SYSGL_EGL_GL_COLORSPACE_SRGB 0x3089
#define SYSGL_EGL_GL_COLORSPACE_LINEAR 0x308A
#define SYSGL_EGL_RENDERABLE_TYPE 0x3040
#define SYSGL_EGL_RED_SIZE 0x3024
#define SYSGL_EGL_GREEN_SIZE 0x3023
#define SYSGL_EGL_BLUE_SIZE 0x3022
#define SYSGL_EGL_ALPHA_SIZE 0x3021
#define SYSGL_EGL_CONTEXT_MAJOR_VERSION 0x3098
#define SYSGL_EGL_CONTEXT_MINOR_VERSION 0x30FB

namespace
{

struct GlesState
{
    void* libEGL{nullptr};
    void* libGLES{nullptr};
    void* libWaylandEgl{nullptr};

    PFN_eglGetDisplay            eglGetDisplay{nullptr};
    PFN_eglGetPlatformDisplayEXT eglGetPlatformDisplayEXT{nullptr};
    PFN_eglInitialize            eglInitialize{nullptr};
    PFN_eglTerminate             eglTerminate{nullptr};
    PFN_eglChooseConfig          eglChooseConfig{nullptr};
    PFN_eglBindAPI               eglBindAPI{nullptr};
    PFN_eglCreateContext         eglCreateContext{nullptr};
    PFN_eglCreateWindowSurface   eglCreateWindowSurface{nullptr};
    PFN_eglCreatePbufferSurface  eglCreatePbufferSurface{nullptr};
    PFN_eglQuerySurface          eglQuerySurface{nullptr};
    PFN_eglMakeCurrent           eglMakeCurrent{nullptr};
    PFN_eglSwapBuffers           eglSwapBuffers{nullptr};
    PFN_eglSwapInterval          eglSwapInterval{nullptr};
    PFN_eglDestroyContext        eglDestroyContext{nullptr};
    PFN_eglDestroySurface        eglDestroySurface{nullptr};
    PFN_eglCreateImageKHR        eglCreateImageKHR{nullptr};
    PFN_eglDestroyImageKHR       eglDestroyImageKHR{nullptr};
    PFN_glEGLImageTargetTexture2DOES        glEGLImageTargetTexture2DOES{nullptr};
    PFN_glEGLImageTargetRenderbufferStorageOES glEGLImageTargetRenderbufferStorageOES{nullptr};
    PFN_eglGetError              eglGetError{nullptr};
    void*                        (*eglGetProcAddress)(const char*){nullptr};

    void* display{nullptr};
    void* config{nullptr};
    void* pbufferConfig{nullptr};
    bool  ready{false};
    int   esMax[2]{0, 0};
};

// libwayland-egl: the wl_egl_window of the Wayland platform is created and
// owned by that library - wayland-egl-core.h declares the create / destroy /
// resize entry points and keeps the struct layout private. The ES delegation
// therefore mirrors the application's native window through those entry
// points instead of handing our own struct to the system EGL, which would
// read it with Mesa's layout.
typedef void* (*PFN_wl_egl_window_create)(void*, int, int);
typedef void (*PFN_wl_egl_window_destroy)(void*);
typedef void (*PFN_wl_egl_window_resize)(void*, int, int, int, int);

PFN_wl_egl_window_create  wlEglWindowCreate{nullptr};
PFN_wl_egl_window_destroy wlEglWindowDestroy{nullptr};
PFN_wl_egl_window_resize  wlEglWindowResize{nullptr};

// Handle handed back to the caller: the system surface plus, on Wayland, the
// libwayland-egl window that must mirror the native window's size.
struct GlesSurface
{
    void* eglSurface{nullptr};
    void* wlEglWindow{nullptr}; // NULL outside Wayland
    int   width{0};
    int   height{0};
};

GlesState g;

// Requested-format -> system config cache. The system config query runs once
// per distinct format; surface creation happens per test case.
struct ConfigCacheEntry
{
    GlesSurfaceFormat format;
    void*             config;
};
static ConfigCacheEntry s_configCache[16];
static int s_configCacheCount = 0;

template <typename T>
static bool resolveSym(void* lib, T& out, const char* name)
{
    out = reinterpret_cast<T>(dlsym(lib, name));
    return out != nullptr;
}

// A system config matching the caller's EGLConfig, queried with
// WINDOW|PBUFFER so the same config serves window surfaces, pbuffers and the
// contexts bound to them - EGL requires context and surface to come from the
// same config, and for a given requested format both sides resolve to this
// very entry. Falls back to the delegation's default config when the system
// stack has no such format.
static void* systemConfigFor(const GlesSurfaceFormat* format)
{
    if (!format)
        return g.config;

    for (int i = 0; i < s_configCacheCount; i++)
    {
        if (s_configCache[i].format.red == format->red &&
            s_configCache[i].format.green == format->green &&
            s_configCache[i].format.blue == format->blue &&
            s_configCache[i].format.alpha == format->alpha &&
            s_configCache[i].format.depth == format->depth &&
            s_configCache[i].format.stencil == format->stencil &&
            s_configCache[i].format.samples == format->samples)
            return s_configCache[i].config;
    }

    const EGLint renderable = g.esMax[0] >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, renderable,
        EGL_RED_SIZE, format->red,
        EGL_GREEN_SIZE, format->green,
        EGL_BLUE_SIZE, format->blue,
        EGL_ALPHA_SIZE, format->alpha,
        EGL_DEPTH_SIZE, format->depth,
        EGL_STENCIL_SIZE, format->stencil,
        format->samples > 0 ? EGL_SAMPLE_BUFFERS : EGL_NONE, format->samples > 0 ? 1 : 0,
        format->samples > 0 ? EGL_SAMPLES : EGL_NONE, format->samples,
        EGL_NONE};

    void* config = nullptr;
    EGLint num = 0;
    if (g.eglChooseConfig(g.display, attribs, &config, 1, &num) && num > 0 && config)
    {
        if (s_configCacheCount < (int)(sizeof(s_configCache) / sizeof(s_configCache[0])))
        {
            s_configCache[s_configCacheCount].format = *format;
            s_configCache[s_configCacheCount].config = config;
            s_configCacheCount++;
        }
        return config;
    }

    // Consume the probe error, like chooseDefaultConfig does.
    if (g.eglGetError)
        g.eglGetError();
    return g.config;
}

static bool loadWaylandEgl()
{
    if (wlEglWindowCreate)
        return true;
    if (!g.libWaylandEgl)
    {
        g.libWaylandEgl = dlopen("libwayland-egl.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!g.libWaylandEgl)
            g.libWaylandEgl = dlopen("libwayland-egl.so", RTLD_LAZY | RTLD_LOCAL);
    }
    return g.libWaylandEgl &&
           resolveSym(g.libWaylandEgl, wlEglWindowCreate, "wl_egl_window_create") &&
           resolveSym(g.libWaylandEgl, wlEglWindowDestroy, "wl_egl_window_destroy") &&
           resolveSym(g.libWaylandEgl, wlEglWindowResize, "wl_egl_window_resize");
}

static bool chooseDefaultConfig(int* outVersion)
{
    static const int cfgAttribs3[] = {
        SYSGL_EGL_SURFACE_TYPE, SYSGL_EGL_WINDOW_BIT,
        SYSGL_EGL_RENDERABLE_TYPE, SYSGL_EGL_OPENGL_ES3_BIT,
        SYSGL_EGL_RED_SIZE, 8,
        SYSGL_EGL_GREEN_SIZE, 8,
        SYSGL_EGL_BLUE_SIZE, 8,
        SYSGL_EGL_ALPHA_SIZE, 8,
        SYSGL_EGL_NONE};

    int num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs3, &g.config, 1, &num) && num > 0)
    {
        outVersion[0] = 3;
        outVersion[1] = 0;
        return true;
    }

    static const int cfgAttribs2[] = {
        SYSGL_EGL_SURFACE_TYPE, SYSGL_EGL_WINDOW_BIT,
        SYSGL_EGL_RENDERABLE_TYPE, SYSGL_EGL_OPENGL_ES2_BIT,
        SYSGL_EGL_RED_SIZE, 8,
        SYSGL_EGL_GREEN_SIZE, 8,
        SYSGL_EGL_BLUE_SIZE, 8,
        SYSGL_EGL_ALPHA_SIZE, 8,
        SYSGL_EGL_NONE};

    // Consume the ES3 failure so the application does not observe a stale error
    // code from a probe it never made.
    if (g.eglGetError)
        g.eglGetError();

    num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs2, &g.config, 1, &num) && num > 0)
    {
        outVersion[0] = 2;
        outVersion[1] = 0;
        return true;
    }

    return false;
}

// A config that can also back pbuffers, for the delegated pbuffer surfaces.
// Optional: without one, window surfaces still delegate; pbuffer surfaces of
// ES contexts fall back to GLX.
static void choosePbufferConfig()
{
    static const int cfgAttribs[] = {
        SYSGL_EGL_SURFACE_TYPE, SYSGL_EGL_WINDOW_BIT | SYSGL_EGL_PBUFFER_BIT,
        SYSGL_EGL_RENDERABLE_TYPE, g.esMax[0] >= 3 ? SYSGL_EGL_OPENGL_ES3_BIT : SYSGL_EGL_OPENGL_ES2_BIT,
        SYSGL_EGL_RED_SIZE, 8,
        SYSGL_EGL_GREEN_SIZE, 8,
        SYSGL_EGL_BLUE_SIZE, 8,
        SYSGL_EGL_ALPHA_SIZE, 8,
        SYSGL_EGL_NONE};

    int num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs, &g.pbufferConfig, 1, &num) && num > 0)
        return;

    // Consume the probe error, like chooseDefaultConfig does.
    if (g.eglGetError)
        g.eglGetError();
    g.pbufferConfig = nullptr;
}

} // anonymous namespace

extern "C"
{

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

        g.libEGL = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!g.libEGL)
            g.libEGL = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
        g.libGLES = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (!g.libGLES)
            g.libGLES = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);

        if (!g.libEGL || !g.libGLES)
        {
            gles_terminate();
            return EGL_FALSE;
        }

        bool ok =
            resolveSym(g.libEGL, g.eglGetDisplay, "eglGetDisplay") &&
            resolveSym(g.libEGL, g.eglInitialize, "eglInitialize") &&
            resolveSym(g.libEGL, g.eglTerminate, "eglTerminate") &&
            resolveSym(g.libEGL, g.eglChooseConfig, "eglChooseConfig") &&
            resolveSym(g.libEGL, g.eglBindAPI, "eglBindAPI") &&
            resolveSym(g.libEGL, g.eglCreateContext, "eglCreateContext") &&
            resolveSym(g.libEGL, g.eglCreateWindowSurface, "eglCreateWindowSurface") &&
            resolveSym(g.libEGL, g.eglCreatePbufferSurface, "eglCreatePbufferSurface") &&
            resolveSym(g.libEGL, g.eglQuerySurface, "eglQuerySurface") &&
            resolveSym(g.libEGL, g.eglMakeCurrent, "eglMakeCurrent") &&
            resolveSym(g.libEGL, g.eglSwapBuffers, "eglSwapBuffers") &&
            resolveSym(g.libEGL, g.eglSwapInterval, "eglSwapInterval") &&
            resolveSym(g.libEGL, g.eglDestroyContext, "eglDestroyContext") &&
            resolveSym(g.libEGL, g.eglDestroySurface, "eglDestroySurface");

        if (!ok)
        {
            gles_terminate();
            return EGL_FALSE;
        }

        // When this library is itself libEGL.so (shared builds), the dlopen
        // above returns OUR OWN module and the delegation would call back into
        // this very library - eglGetDisplay then self-deadlocks in the one-time
        // bootstrap. Detect the self-load and treat the system GLES backend as
        // unavailable; a static build keeps delegating to the real system EGL.
        PFN_eglInitialize selfInitialize = reinterpret_cast<PFN_eglInitialize>(dlsym(RTLD_DEFAULT, "eglInitialize"));
        if (selfInitialize && selfInitialize == g.eglInitialize)
        {
            gles_terminate();
            return EGL_FALSE;
        }
        // Optional: only used to consume error codes between config probes. A
        // stripped libEGL missing it is no reason to disable the whole ES path.
        resolveSym(g.libEGL, g.eglGetError, "eglGetError");

        // Optional: EGLImage delegation. GLVND libEGL exports only the core
        // ABI symbols - extension entry points like eglCreateImageKHR live
        // behind eglGetProcAddress - so resolve them both ways.
        resolveSym(g.libEGL, g.eglGetProcAddress, "eglGetProcAddress");
        resolveSym(g.libEGL, g.eglCreateImageKHR, "eglCreateImageKHR");
        resolveSym(g.libEGL, g.eglDestroyImageKHR, "eglDestroyImageKHR");
        if (!g.eglCreateImageKHR && g.eglGetProcAddress)
            g.eglCreateImageKHR = reinterpret_cast<PFN_eglCreateImageKHR>(g.eglGetProcAddress("eglCreateImageKHR"));
        if (!g.eglDestroyImageKHR && g.eglGetProcAddress)
            g.eglDestroyImageKHR = reinterpret_cast<PFN_eglDestroyImageKHR>(g.eglGetProcAddress("eglDestroyImageKHR"));

        // Optional: image consumption entry points of the system GLES
        // library (GL_OES_EGL_image); they operate on the current context,
        // which is a system context whenever the delegation is in use. Same
        // GLVND rule: extension functions resolve through eglGetProcAddress.
        resolveSym(g.libGLES, g.glEGLImageTargetTexture2DOES, "glEGLImageTargetTexture2DOES");
        resolveSym(g.libGLES, g.glEGLImageTargetRenderbufferStorageOES, "glEGLImageTargetRenderbufferStorageOES");
        if (!g.glEGLImageTargetTexture2DOES && g.eglGetProcAddress)
            g.glEGLImageTargetTexture2DOES = reinterpret_cast<PFN_glEGLImageTargetTexture2DOES>(g.eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!g.glEGLImageTargetRenderbufferStorageOES && g.eglGetProcAddress)
            g.glEGLImageTargetRenderbufferStorageOES = reinterpret_cast<PFN_glEGLImageTargetRenderbufferStorageOES>(g.eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));

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
            gles_terminate();
            return EGL_FALSE;
        }

        choosePbufferConfig();

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
        // The system configs belong to the terminated display.
        s_configCacheCount = 0;

        // eglTerminate only MARKS the display's resources for deletion — a context
        // that is still current keeps them, and the driver libraries, alive past the
        // dlclose below. Release it first.
        if (g.display && g.eglMakeCurrent)
        {
            g.eglMakeCurrent(g.display, nullptr, nullptr, nullptr);
        }
        if (g.display && g.eglTerminate)
        {
            g.eglTerminate(g.display);
            g.display = nullptr;
        }
        if (g.libGLES)
        {
            dlclose(g.libGLES);
            g.libGLES = nullptr;
        }
        if (g.libWaylandEgl)
        {
            dlclose(g.libWaylandEgl);
            g.libWaylandEgl = nullptr;
            wlEglWindowCreate  = nullptr;
            wlEglWindowDestroy = nullptr;
            wlEglWindowResize  = nullptr;
        }
        if (g.libEGL)
        {
            dlclose(g.libEGL);
            g.libEGL = nullptr;
        }
        g = GlesState{};
    }

    EGLBoolean gles_isAvailable(void)
    {
        return g.ready ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean gles_createWindowSurface(void* win, const GlesSurfaceFormat* format, EGLint colorspace, void** out_surface)
    {
        if (!g.ready || !out_surface)
            return EGL_FALSE;

        void* config = systemConfigFor(format);

        int attribs[4];
        int count = 0;

        if (colorspace == SYSGL_EGL_GL_COLORSPACE_SRGB)
        {
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE;
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE_SRGB;
        }
        attribs[count++] = SYSGL_EGL_NONE;

        void* eglSurface = g.eglCreateWindowSurface(g.display, config, win, attribs);
        if (!eglSurface)
            return EGL_FALSE;

        auto* s        = new GlesSurface{};
        s->eglSurface  = eglSurface;
        *out_surface   = s;
        return EGL_TRUE;
    }

    EGLBoolean gles_createPbufferSurface(const EGLint width, const EGLint height, const EGLBoolean largestPbuffer, const EGLint colorspace, const GlesSurfaceFormat* format, void** out_surface)
    {
        if (!g.ready || !out_surface)
            return EGL_FALSE;

        void* config = systemConfigFor(format);
        if (!config)
            return EGL_FALSE;

        int attribs[8];
        int count = 0;

        attribs[count++] = SYSGL_EGL_WIDTH;
        attribs[count++] = width;
        attribs[count++] = SYSGL_EGL_HEIGHT;
        attribs[count++] = height;
        attribs[count++] = SYSGL_EGL_LARGEST_PBUFFER;
        attribs[count++] = largestPbuffer ? 1 : 0;
        if (colorspace == SYSGL_EGL_GL_COLORSPACE_SRGB)
        {
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE;
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE_SRGB;
        }
        attribs[count++] = SYSGL_EGL_NONE;

        void* eglSurface = g.eglCreatePbufferSurface(g.display, config, attribs);
        if (!eglSurface)
            return EGL_FALSE;

        auto* s        = new GlesSurface{};
        s->eglSurface  = eglSurface;
        *out_surface   = s;
        return EGL_TRUE;
    }

    EGLBoolean gles_querySurface(void* surface, const EGLint attribute, EGLint* value)
    {
        if (!g.ready || !surface || !value)
            return EGL_FALSE;

        auto* s = static_cast<GlesSurface*>(surface);

        return g.eglQuerySurface(g.display, s->eglSurface, attribute, value) ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean gles_createWindowSurfaceWayland(void* wlSurface, int width, int height, const GlesSurfaceFormat* format, EGLint colorspace, void** out_surface)
    {
        if (!g.ready || !out_surface || !wlSurface)
            return EGL_FALSE;
        if (!loadWaylandEgl())
            return EGL_FALSE;

        // The system EGL expects a wl_egl_window created by libwayland-egl;
        // pass it that instead of our own native-window struct.
        void* wlEglWindow = wlEglWindowCreate(wlSurface, width, height);
        if (!wlEglWindow)
            return EGL_FALSE;

        void* config = systemConfigFor(format);

        int attribs[4];
        int count = 0;

        if (colorspace == SYSGL_EGL_GL_COLORSPACE_SRGB)
        {
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE;
            attribs[count++] = SYSGL_EGL_GL_COLORSPACE_SRGB;
        }
        attribs[count++] = SYSGL_EGL_NONE;

        void* eglSurface = g.eglCreateWindowSurface(g.display, config, wlEglWindow, attribs);
        if (!eglSurface)
        {
            wlEglWindowDestroy(wlEglWindow);
            return EGL_FALSE;
        }

        auto* s        = new GlesSurface{};
        s->eglSurface  = eglSurface;
        s->wlEglWindow = wlEglWindow;
        s->width       = width;
        s->height      = height;
        *out_surface   = s;
        return EGL_TRUE;
    }

    EGLBoolean gles_resizeWindowWayland(void* surface, int width, int height)
    {
        auto* s = static_cast<GlesSurface*>(surface);
        if (!s || !s->wlEglWindow || width <= 0 || height <= 0)
            return EGL_FALSE;

        // EGL 1.5 section 3.10.1.1: the swap detects a native-window resize.
        if (width != s->width || height != s->height)
        {
            wlEglWindowResize(s->wlEglWindow, width, height, 0, 0);
            s->width  = width;
            s->height = height;
        }
        return EGL_TRUE;
    }

    EGLBoolean gles_createContext(const GlesSurfaceFormat* format, EGLint major, EGLint minor, void* share, void** out_context)
    {
        if (!g.ready || !out_context)
            return EGL_FALSE;
        if (major < 2)
            major = 2;

        void* config = systemConfigFor(format);

        g.eglBindAPI(SYSGL_EGL_OPENGL_ES_API);

        const int ctxAttribs[] = {
            SYSGL_EGL_CONTEXT_MAJOR_VERSION, (int)major,
            SYSGL_EGL_CONTEXT_MINOR_VERSION, (int)minor,
            SYSGL_EGL_NONE};
        *out_context = g.eglCreateContext(g.display, config, share, ctxAttribs);
        return *out_context ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean gles_makeCurrent(void* surface, void* context)
    {
        if (!g.ready)
            return EGL_FALSE;
        if (!context && !surface)
            return (EGLBoolean)g.eglMakeCurrent(g.display, nullptr, nullptr, nullptr);
        auto* s = static_cast<GlesSurface*>(surface);
        return (EGLBoolean)g.eglMakeCurrent(g.display, s->eglSurface, s->eglSurface, context);
    }

    EGLBoolean gles_swapBuffers(void* surface)
    {
        if (!g.ready || !surface)
            return EGL_FALSE;
        auto* s = static_cast<GlesSurface*>(surface);
        return (EGLBoolean)g.eglSwapBuffers(g.display, s->eglSurface);
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
        auto* s = static_cast<GlesSurface*>(surface);
        if (s->wlEglWindow)
            wlEglWindowDestroy(s->wlEglWindow);
        void* eglSurface = s->eglSurface;
        delete s;
        return (EGLBoolean)g.eglDestroySurface(g.display, eglSurface);
    }

    EGLBoolean gles_destroyContext(void* context)
    {
        if (!g.ready || !context)
            return EGL_FALSE;
        return (EGLBoolean)g.eglDestroyContext(g.display, context);
    }

    EGLBoolean gles_createImageKHR(void* context, EGLenum target, EGLClientBuffer buffer, const EGLint* attrib_list, void** out_image, EGLint* out_error)
    {
        if (!g.ready || !out_image || !g.eglCreateImageKHR)
            return EGL_FALSE;

        void* image = g.eglCreateImageKHR(g.display, context, target, buffer, attrib_list);

        if (out_error)
            *out_error = g.eglGetError ? g.eglGetError() : EGL_SUCCESS;

        if (!image)
            return EGL_FALSE;

        *out_image = image;

        return EGL_TRUE;
    }

    EGLBoolean gles_destroyImageKHR(void* image)
    {
        if (!g.ready || !image || !g.eglDestroyImageKHR)
            return EGL_FALSE;

        return (EGLBoolean)g.eglDestroyImageKHR(g.display, image);
    }

    void gles_imageTargetTexture2D(EGLenum target, void* image)
    {
        if (!g.ready || !g.glEGLImageTargetTexture2DOES)
            return;

        g.glEGLImageTargetTexture2DOES(target, image);
    }

    void gles_imageTargetRenderbufferStorage(EGLenum target, void* image)
    {
        if (!g.ready || !g.glEGLImageTargetRenderbufferStorageOES)
            return;

        g.glEGLImageTargetRenderbufferStorageOES(target, image);
    }

} // extern "C"

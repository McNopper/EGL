/**
 * ANGLE backend for OpenGL ES on Windows — implementation.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "egl_windows_angle.h"

#include <stdio.h>

// Use ANGLE's own EGL headers via the vcpkg include path, but rename the
// well-known EGL types to ANGLE-prefixed aliases so they don't collide with
// this library's public EGL types when both end up in the same translation
// unit. ANGLE's libEGL.dll exports the same symbol names as our library, but
// here we resolve them dynamically via GetProcAddress so there is no static
// link conflict.

// We don't include ANGLE's egl.h to avoid type collisions. Instead we use
// opaque void* and the standard EGL constants (which match ANGLE's values).

// EGL constants — values are stable across implementations (Khronos EGL spec).
#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3208
#endif

// ANGLE function pointer signatures — EGLAPIENTRY is __stdcall on Windows.
typedef void*(EGLAPIENTRY* PFN_eglGetPlatformDisplayEXT)(EGLenum, void*, const EGLint*);
typedef void*(EGLAPIENTRY* PFN_eglGetDisplay)(void*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglInitialize)(void*, EGLint*, EGLint*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglTerminate)(void*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglChooseConfig)(void*, const EGLint*, void**, EGLint, EGLint*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglBindAPI)(EGLenum);
typedef void*(EGLAPIENTRY* PFN_eglCreateContext)(void*, void*, void*, const EGLint*);
typedef void*(EGLAPIENTRY* PFN_eglCreateWindowSurface)(void*, void*, EGLNativeWindowType, const EGLint*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglMakeCurrent)(void*, void*, void*, void*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglSwapBuffers)(void*, void*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglSwapInterval)(void*, EGLint);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglDestroyContext)(void*, void*);
typedef EGLBoolean(EGLAPIENTRY* PFN_eglDestroySurface)(void*, void*);
typedef EGLint(EGLAPIENTRY* PFN_eglGetError)(void);

namespace
{

struct AngleState
{
    HMODULE libEGL{nullptr};
    HMODULE libGLESv2{nullptr};

    PFN_eglGetPlatformDisplayEXT eglGetPlatformDisplayEXT{nullptr};
    PFN_eglGetDisplay            eglGetDisplay{nullptr};
    PFN_eglInitialize            eglInitialize{nullptr};
    PFN_eglTerminate             eglTerminate{nullptr};
    PFN_eglChooseConfig          eglChooseConfig{nullptr};
    PFN_eglBindAPI               eglBindAPI{nullptr};
    PFN_eglCreateContext         eglCreateContext{nullptr};
    PFN_eglCreateWindowSurface   eglCreateWindowSurface{nullptr};
    PFN_eglMakeCurrent           eglMakeCurrent{nullptr};
    PFN_eglSwapBuffers           eglSwapBuffers{nullptr};
    PFN_eglSwapInterval          eglSwapInterval{nullptr};
    PFN_eglDestroyContext        eglDestroyContext{nullptr};
    PFN_eglDestroySurface        eglDestroySurface{nullptr};
    PFN_eglGetError              eglGetError{nullptr};

    void*  display{nullptr};
    void*  config{nullptr};
    bool   ready{false};
    EGLint esMax[2]{0, 0};
};

AngleState g;

template <typename T>
bool resolve(HMODULE m, T& out, const char* name)
{
    out = reinterpret_cast<T>(GetProcAddress(m, name));
    return out != nullptr;
}

bool chooseDefaultConfig()
{
    static const EGLint cfgAttribs[] = {
        0x3033 /*EGL_SURFACE_TYPE*/, 0x0004 /*EGL_WINDOW_BIT*/,
        0x3040 /*EGL_RENDERABLE_TYPE*/, 0x0040 /*EGL_OPENGL_ES3_BIT*/,
        0x3024 /*EGL_RED_SIZE*/, 8,
        0x3023 /*EGL_GREEN_SIZE*/, 8,
        0x3022 /*EGL_BLUE_SIZE*/, 8,
        0x3021 /*EGL_ALPHA_SIZE*/, 8,
        0x3038 /*EGL_NONE*/
    };

    EGLint num = 0;
    if (g.eglChooseConfig(g.display, cfgAttribs, &g.config, 1, &num) && num != 0)
    {
        return true;
    }

    // Fall back to ES2.
    static const EGLint cfgAttribs2[] = {
        0x3033, 0x0004,
        0x3040, 0x0004 /*EGL_OPENGL_ES2_BIT*/,
        0x3024, 8,
        0x3023, 8,
        0x3022, 8,
        0x3021, 8,
        0x3038};
    num = 0;
    if (!g.eglChooseConfig(g.display, cfgAttribs2, &g.config, 1, &num) || num == 0)
    {
        return false;
    }
    return true;
}

// Probe the highest ES version ANGLE actually accepts by creating a real context
// for it, the same way the GLX path probes desktop GL. ANGLE's D3D11 backend
// commonly supports 3.1/3.2, and the core rejects any request above
// g_ES_max_supported_version, so reporting a hard-coded 3.0 would lock those out.
bool probeESVersion(EGLint* outVersion)
{
    static const EGLint k_versions[][2] = {{3, 2}, {3, 1}, {3, 0}, {2, 0}};

    if (g.eglBindAPI)
    {
        g.eglBindAPI(EGL_OPENGL_ES_API);
    }

    for (const auto& version : k_versions)
    {
        const EGLint ctxAttribs[] = {
            0x3098 /*EGL_CONTEXT_MAJOR_VERSION*/, version[0],
            0x30FB /*EGL_CONTEXT_MINOR_VERSION*/, version[1],
            0x3038 /*EGL_NONE*/
        };

        void* ctx = g.eglCreateContext(g.display, g.config, nullptr, ctxAttribs);
        if (ctx)
        {
            g.eglDestroyContext(g.display, ctx);
            outVersion[0] = version[0];
            outVersion[1] = version[1];
            return true;
        }

        // Consume the failure so the next probe (and the application) does not
        // observe a stale error code.
        if (g.eglGetError)
        {
            g.eglGetError();
        }
    }

    return false;
}

} // anonymous namespace

extern "C"
{

    EGLBoolean angle_init(EGLint* es_max_supported)
    {
        if (g.ready)
        {
            if (es_max_supported)
            {
                es_max_supported[0] = g.esMax[0];
                es_max_supported[1] = g.esMax[1];
            }
            return EGL_TRUE;
        }

        g.libEGL    = LoadLibraryA("libEGL.dll");
        g.libGLESv2 = LoadLibraryA("libGLESv2.dll");
        if (!g.libEGL || !g.libGLESv2)
        {
            // ANGLE not available — leave ES disabled.
            if (g.libEGL)
            {
                FreeLibrary(g.libEGL);
                g.libEGL = nullptr;
            }
            if (g.libGLESv2)
            {
                FreeLibrary(g.libGLESv2);
                g.libGLESv2 = nullptr;
            }
            return EGL_FALSE;
        }

        bool ok =
            resolve(g.libEGL, g.eglGetPlatformDisplayEXT, "eglGetPlatformDisplayEXT") &&
            resolve(g.libEGL, g.eglGetDisplay, "eglGetDisplay") &&
            resolve(g.libEGL, g.eglInitialize, "eglInitialize") &&
            resolve(g.libEGL, g.eglTerminate, "eglTerminate") &&
            resolve(g.libEGL, g.eglChooseConfig, "eglChooseConfig") &&
            resolve(g.libEGL, g.eglBindAPI, "eglBindAPI") &&
            resolve(g.libEGL, g.eglCreateContext, "eglCreateContext") &&
            resolve(g.libEGL, g.eglCreateWindowSurface, "eglCreateWindowSurface") &&
            resolve(g.libEGL, g.eglMakeCurrent, "eglMakeCurrent") &&
            resolve(g.libEGL, g.eglSwapBuffers, "eglSwapBuffers") &&
            resolve(g.libEGL, g.eglSwapInterval, "eglSwapInterval") &&
            resolve(g.libEGL, g.eglDestroyContext, "eglDestroyContext") &&
            resolve(g.libEGL, g.eglDestroySurface, "eglDestroySurface");

        // Optional: only used to consume error codes during version probing. A
        // stripped libEGL missing it is no reason to disable the whole ES path.
        resolve(g.libEGL, g.eglGetError, "eglGetError");

        if (!ok)
        {
            angle_terminate();
            return EGL_FALSE;
        }

        // Request a D3D11 ANGLE display. EGL_DEFAULT_DISPLAY is fine here — ANGLE
        // resolves the actual D3D11 device internally.
        const EGLint platformAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            0x3038 /*EGL_NONE*/
        };

        g.display = g.eglGetPlatformDisplayEXT(
            EGL_PLATFORM_ANGLE_ANGLE,
            reinterpret_cast<void*>(static_cast<intptr_t>(0) /*EGL_DEFAULT_DISPLAY*/),
            platformAttribs);

        if (!g.display)
        {
            // Fall back to plain eglGetDisplay.
            g.display = g.eglGetDisplay(reinterpret_cast<void*>(static_cast<intptr_t>(0)));
            if (!g.display)
            {
                angle_terminate();
                return EGL_FALSE;
            }
        }

        EGLint angleMajor = 0, angleMinor = 0;
        if (!g.eglInitialize(g.display, &angleMajor, &angleMinor))
        {
            g.display = nullptr;
            angle_terminate();
            return EGL_FALSE;
        }

        if (!chooseDefaultConfig() || !probeESVersion(g.esMax))
        {
            angle_terminate();
            return EGL_FALSE;
        }

        g.ready = true;
        if (es_max_supported)
        {
            es_max_supported[0] = g.esMax[0];
            es_max_supported[1] = g.esMax[1];
        }
        return EGL_TRUE;
    }

    void angle_terminate(void)
    {
        // eglTerminate only MARKS the display's resources for deletion — a context
        // that is still current keeps them, and the driver DLL, alive past the
        // FreeLibrary below. Release it first.
        if (g.display && g.eglMakeCurrent)
        {
            g.eglMakeCurrent(g.display, nullptr, nullptr, nullptr);
        }
        if (g.display && g.eglTerminate)
        {
            g.eglTerminate(g.display);
            g.display = nullptr;
        }
        if (g.libGLESv2)
        {
            FreeLibrary(g.libGLESv2);
            g.libGLESv2 = nullptr;
        }
        if (g.libEGL)
        {
            FreeLibrary(g.libEGL);
            g.libEGL = nullptr;
        }
        g = AngleState{};
    }

    EGLBoolean angle_isAvailable(void)
    {
        return g.ready ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean angle_createWindowSurface(HWND hwnd, void** out_surface)
    {
        if (!g.ready || !out_surface)
            return EGL_FALSE;
        static const EGLint surfaceAttribs[] = {0x3038 /*EGL_NONE*/};
        *out_surface                         = g.eglCreateWindowSurface(g.display, g.config,
                                                                        reinterpret_cast<EGLNativeWindowType>(hwnd),
                                                                        surfaceAttribs);
        return *out_surface ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean angle_createContext(EGLint major, EGLint minor, void* share, void** out_context)
    {
        if (!g.ready || !out_context)
            return EGL_FALSE;
        if (major < 2)
            major = 2;

        g.eglBindAPI(EGL_OPENGL_ES_API);

        const EGLint ctxAttribs[] = {
            0x3098 /*EGL_CONTEXT_MAJOR_VERSION*/, major,
            0x30FB /*EGL_CONTEXT_MINOR_VERSION*/, minor,
            0x3038 /*EGL_NONE*/
        };
        *out_context = g.eglCreateContext(g.display, g.config, share, ctxAttribs);
        return *out_context ? EGL_TRUE : EGL_FALSE;
    }

    EGLBoolean angle_makeCurrent(void* surface, void* context)
    {
        if (!g.ready)
            return EGL_FALSE;
        if (!context && !surface)
        {
            return g.eglMakeCurrent(g.display, nullptr, nullptr, nullptr);
        }
        return g.eglMakeCurrent(g.display, surface, surface, context);
    }

    EGLBoolean angle_swapBuffers(void* surface)
    {
        if (!g.ready || !surface)
            return EGL_FALSE;
        return g.eglSwapBuffers(g.display, surface);
    }

    EGLBoolean angle_swapInterval(EGLint interval)
    {
        if (!g.ready)
            return EGL_FALSE;
        return g.eglSwapInterval(g.display, interval);
    }

    EGLBoolean angle_destroySurface(void* surface)
    {
        if (!g.ready || !surface)
            return EGL_FALSE;
        return g.eglDestroySurface(g.display, surface);
    }

    EGLBoolean angle_destroyContext(void* context)
    {
        if (!g.ready || !context)
            return EGL_FALSE;
        return g.eglDestroyContext(g.display, context);
    }

} // extern "C"

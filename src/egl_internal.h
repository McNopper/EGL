/**
 * EGL windows desktop implementation.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) since 2014 Norbert Nopper
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef EGL_INTERNAL_H_
#define EGL_INTERNAL_H_

#define _EGL_VENDOR "Norbert Nopper"

#define _EGL_VERSION "1.5 Version 0.3.3"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mutex>

// HDR colorspace support bitmask flags (universal — value 0 on non-Windows platforms)
#define EGL_HDR_CS_SCRGB_LINEAR_BIT      (1u << 0)
#define EGL_HDR_CS_SCRGB_BIT             (1u << 1)
#define EGL_HDR_CS_BT2020_PQ_BIT         (1u << 2)
#define EGL_HDR_CS_BT2020_LINEAR_BIT     (1u << 3)
#define EGL_HDR_CS_BT2020_HLG_BIT        (1u << 4)
#define EGL_HDR_CS_DISPLAY_P3_BIT        (1u << 5)
#define EGL_HDR_CS_DISPLAY_P3_LINEAR_BIT (1u << 6)

#if defined(_WIN32) || defined(__VC32__) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__) /* Win32 and WinCE */

#include <windows.h>

#define WIN32_LEAN_AND_MEAN
#include <GL/gl.h>
#include "wglext.h"

// Forward declaration; full definition is in egl_windows_vk.h
typedef struct _NativeHDRSurfaceContainer NativeHDRSurfaceContainer;

#define CONTEXT_ATTRIB_LIST_SIZE 13

// Backend tag identifies which native subsystem owns a context/surface on
// Windows. WGL is the default desktop OpenGL path; ANGLE is the OpenGL ES
// path delegated to Google ANGLE's libEGL.dll / libGLESv2.dll.
typedef enum {
    EGL_BACKEND_WGL   = 0,
    EGL_BACKEND_ANGLE = 1
} NativeBackend;

typedef struct _NativeSurfaceContainer {
    HDC hdc;
    NativeHDRSurfaceContainer* hdr;  // NULL for sRGB/linear; non-NULL for HDR Vulkan surfaces
    NativeBackend backend;           // 0 = WGL (default), 1 = ANGLE
    void* angleSurface;              // EGLSurface from ANGLE when backend == ANGLE
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    HGLRC ctx;
    NativeBackend backend;           // 0 = WGL (default), 1 = ANGLE
    void* angleCtx;                  // EGLContext from ANGLE when backend == ANGLE
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    HWND hwnd;
    HDC  hdc;
    HGLRC ctx;
    void* placeholder;
} NativeLocalStorageContainer;

typedef HPBUFFERARB NativePbufferType;

#elif defined(__QNX__)

// QNX — Screen API — future egl_qnx.cpp backend
#include <screen/screen.h>
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    screen_window_t window;   // screen_window_t
    screen_context_t ctx;     // screen_context_t (needed for surface ops)
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    screen_context_t ctx;     // screen_context_t
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    screen_display_t display; // screen_display_t
    screen_context_t ctx;
} NativeLocalStorageContainer;

typedef screen_pixmap_t NativePbufferType;

#elif defined(__EMSCRIPTEN__)

// WebAssembly / Emscripten — WebGL — future egl_emscripten.cpp backend
#include <emscripten/html5.h>
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    int             target;   // canvas target (0 = default canvas)
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    int display;              // unused; WebGL has no explicit display
} NativeLocalStorageContainer;

typedef int NativePbufferType;

#elif defined(WL_EGL_PLATFORM)

// Wayland — GLX via XWayland for GL context + Vulkan for all presentation.
// wl_egl_window is our native implementation (eglplatform.h maps EGLNativeWindowType to it).
#include <X11/X.h>
#include <GL/glx.h>
#define CONTEXT_ATTRIB_LIST_SIZE 11

struct wl_display;
struct wl_compositor;
struct wl_surface;

// Our native wl_egl_window (matches EGLNativeWindowType from eglplatform.h)
struct wl_egl_window {
    struct wl_surface* surface;
    int                width;
    int                height;
};

// Forward declaration; full definition is in egl_linux_vk.h
typedef struct _NativeHDRSurfaceContainer NativeHDRSurfaceContainer;

typedef struct _NativeSurfaceContainer {
    GLXPbuffer             glxPbuffer;  // offscreen GL drawable (XWayland)
    GLXFBConfig            glxConfig;
    struct wl_egl_window*  eglWindow;   // Wayland window; eglWindow->surface used for VK
    NativeHDRSurfaceContainer* vk;      // always non-NULL for Wayland surfaces
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    GLXContext ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    Display*   x11Display;  // XWayland display (for GLX)
    Window     x11Window;   // dummy X11 window (for bootstrap)
    GLXContext ctx;
} NativeLocalStorageContainer;

typedef GLXPbuffer NativePbufferType;

#elif defined(__GBM__)

// DRM/KMS via GBM — future egl_gbm.cpp backend
struct gbm_device;
struct gbm_surface;
struct gbm_bo;
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    struct gbm_surface* surface;
    unsigned int        fb;   // DRM framebuffer id
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    void* ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    struct gbm_device* device;
    int                drmFd; // DRM device file descriptor
} NativeLocalStorageContainer;

typedef struct gbm_bo* NativePbufferType;

#elif defined(__ANDROID__) || defined(ANDROID)

// Android — EGL passthrough via ANativeWindow — future egl_android.cpp backend
struct ANativeWindow;
struct egl_native_pixmap_t;
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    struct ANativeWindow* window;
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    void* ctx;                // EGLContext (Android system EGL)
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    void* display;            // EGLDisplay (Android system EGL)
} NativeLocalStorageContainer;

typedef struct egl_native_pixmap_t* NativePbufferType;

#elif defined(USE_X11)

// X11 explicit — GLX backend (egl_x11_glx.cpp)
#include <X11/X.h>
#include <GL/glx.h>
#define CONTEXT_ATTRIB_LIST_SIZE 11

// Forward declaration; full definition is in egl_linux_vk.h (only with LINUX_VK)
typedef struct _NativeHDRSurfaceContainer NativeHDRSurfaceContainer;

typedef struct _NativeSurfaceContainer {
    GLXDrawable drawable;
    GLXFBConfig config;
    NativeHDRSurfaceContainer* hdr;  // NULL = SDR (GLX); non-NULL = Vulkan HDR
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    GLXContext ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    Display*   display;
    Window     window;
    GLXContext ctx;
} NativeLocalStorageContainer;

typedef GLXPbuffer NativePbufferType;

#elif defined(__unix__)

// Generic Unix fallback (Linux+X11, FreeBSD, OpenBSD, etc.) — GLX backend (egl_x11_glx.cpp)
#include <X11/X.h>
#include <GL/glx.h>
#define CONTEXT_ATTRIB_LIST_SIZE 11

// Forward declaration; full definition is in egl_linux_vk.h (only with LINUX_VK)
typedef struct _NativeHDRSurfaceContainer NativeHDRSurfaceContainer;

typedef struct _NativeSurfaceContainer {
    GLXDrawable drawable;
    GLXFBConfig config;
    NativeHDRSurfaceContainer* hdr;  // NULL = SDR (GLX); non-NULL = Vulkan HDR
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    GLXContext ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    Display*   display;
    Window     window;
    GLXContext ctx;
} NativeLocalStorageContainer;

typedef GLXPbuffer NativePbufferType;

#elif defined(__APPLE__)

// macOS / iOS — CGL / EAGL / Metal — future egl_apple.cpp backend
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    void* view;               // NSView* (macOS) or CAEAGLLayer* (iOS), opaque to avoid ObjC headers
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    void* ctx;                // NSOpenGLContext* (macOS) or EAGLContext* (iOS)
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    void* display;            // CGDirectDisplayID cast to void*
} NativeLocalStorageContainer;

typedef void* NativePbufferType;

#elif defined(__Fuchsia__)

// Fuchsia OS — future egl_fuchsia.cpp backend
#include <stdint.h>
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    khronos_uintptr_t window;
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    void* ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    void* display;
} NativeLocalStorageContainer;

typedef khronos_uintptr_t NativePbufferType;

#elif defined(OHOS) || defined(__OHOS__)

// HarmonyOS (OpenHarmony) — future egl_harmonyos.cpp backend
#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {
    void* window;             // OHNativeWindow*
} NativeSurfaceContainer;

typedef struct _NativeContextContainer {
    void* ctx;
} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {
    void* display;
} NativeLocalStorageContainer;

typedef void* NativePbufferType;

#else
#error "Platform not recognized. Supported: _WIN32, __QNX__, __EMSCRIPTEN__, WL_EGL_PLATFORM, __GBM__, __ANDROID__, USE_X11, __unix__, __APPLE__, __Fuchsia__, OHOS"
#endif

#include <EGL/egl.h>

//

typedef struct _EGLConfigImpl
{

    // Returns the number of bits in the alpha mask buffer.
    EGLint alphaMaskSize;

    // Returns the number of bits of alpha stored in the color buffer.
    EGLint alphaSize;

    // Returns EGL_TRUE if color buffers can be bound to an RGB texture, EGL_FALSE otherwise.
    EGLint bindToTextureRGB;

    // Returns EGL_TRUE if color buffers can be bound to an RGBA texture, EGL_FALSE otherwise.
    EGLint bindToTextureRGBA;

    // Returns the number of bits of blue stored in the color buffer.
    EGLint blueSize;

    // Returns the depth of the color buffer. It is the sum of EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, and EGL_ALPHA_SIZE.
    EGLint bufferSize;

    // Returns the color buffer type. Possible types are EGL_RGB_BUFFER and EGL_LUMINANCE_BUFFER.
    EGLint colorBufferType;

    // Returns the caveats for the frame buffer configuration. Possible caveat values are EGL_NONE, EGL_SLOW_CONFIG, and EGL_NON_CONFORMANT.
    EGLint configCaveat;

    // Returns the ID of the frame buffer configuration.
    EGLint configId;

    // Returns a bitmask indicating which client API contexts created with respect to this config are conformant.
    EGLint conformant;

    // Returns the number of bits in the depth buffer.
    EGLint depthSize;

    // Returns the number of bits of green stored in the color buffer.
    EGLint greenSize;

    // Returns the frame buffer level. Level zero is the default frame buffer. Positive levels correspond to frame buffers that overlay the default buffer and negative levels correspond to frame buffers that underlay the default buffer.
    EGLint level;

    // Returns the number of bits of luminance stored in the luminance buffer.
    EGLint luminanceSize;

    // Input only: Must be followed by the handle of a valid native pixmap, cast to EGLint, or EGL_NONE.
    EGLint matchNativePixmap;

    // Returns the maximum height of a pixel buffer surface in pixels.
    EGLint maxPBufferHeight;

    // Returns the maximum size of a pixel buffer surface in pixels.
    EGLint maxPBufferPixels;

    // Returns the maximum width of a pixel buffer surface in pixels.
    EGLint maxPBufferWidth;

    // Returns the maximum value that can be passed to eglSwapInterval.
    EGLint maxSwapInterval;

    // Returns the minimum value that can be passed to eglSwapInterval.
    EGLint minSwapInterval;

    // Returns EGL_TRUE if native rendering APIs can render into the surface, EGL_FALSE otherwise.
    EGLint nativeRenderable;

    // Returns the ID of the associated native visual.
    EGLint nativeVisualId;

    // Returns the type of the associated native visual.
    EGLint nativeVisualType;

    // Returns the number of bits of red stored in the color buffer.
    EGLint redSize;

    // Returns a bitmask indicating the types of supported client API contexts.
    EGLint renderableType;

    // Returns the number of multisample buffers.
    EGLint sampleBuffers;

    // Returns the number of samples per pixel.
    EGLint samples;

    // Returns the number of bits in the stencil buffer.
    EGLint stencilSize;

    // Returns a bitmask indicating the types of supported EGL surfaces.
    EGLint surfaceType;

    // Returns the transparent blue value.
    EGLint transparentBlueValue;

    // Returns the transparent green value.
    EGLint transparentGreenValue;

    // Returns the transparent red value.
    EGLint transparentRedValue;

    // Returns the type of supported transparency. Possible transparency values are: EGL_NONE, and EGL_TRANSPARENT_RGB.
    EGLint transparentType;

    // Own data.

    EGLint drawToWindow;
    EGLint drawToPixmap;
    EGLint drawToPBuffer;
    EGLint doubleBuffer;

    struct _EGLConfigImpl* next;

} EGLConfigImpl;

typedef struct _EGLSurfaceImpl
{

    EGLBoolean initialized;
    EGLBoolean destroy;

    EGLBoolean drawToWindow;
    EGLBoolean drawToPixmap;
    EGLBoolean drawToPBuffer;
    EGLBoolean doubleBuffer;
    EGLint configId;

    EGLint width;
    EGLint height;

    EGLint swapBehavior;
    EGLint multisampleResolve;
    EGLint mipmapLevel;
    EGLBoolean mipmapTexture;
    EGLBoolean largestPbuffer;
    EGLint textureFormat;
    EGLint textureTarget;

    // EGL_KHR_gl_colorspace / EGL_EXT_gl_colorspace_*: EGL_GL_COLORSPACE_SRGB, EGL_GL_COLORSPACE_LINEAR, or an HDR colorspace (EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT, etc.)
    EGLint glColorspace;

    // EGL_EXT_surface_SMPTE2086_metadata (values per EGL spec: primaries * 50000, luminance * 10000)
    EGLint smpte2086DisplayPrimaryRx;
    EGLint smpte2086DisplayPrimaryRy;
    EGLint smpte2086DisplayPrimaryGx;
    EGLint smpte2086DisplayPrimaryGy;
    EGLint smpte2086DisplayPrimaryBx;
    EGLint smpte2086DisplayPrimaryBy;
    EGLint smpte2086WhitePointX;
    EGLint smpte2086WhitePointY;
    EGLint smpte2086MaxLuminance;
    EGLint smpte2086MinLuminance;

    // EGL_EXT_surface_CTA861_3_metadata
    EGLint cta861MaxContentLightLevel;
    EGLint cta861MaxFrameAverageLightLevel;

    union {
        EGLNativeWindowType win;
        NativePbufferType pbuf;
        EGLNativePixmapType pixmap;
    };

    NativeSurfaceContainer nativeSurfaceContainer;

    struct _EGLSurfaceImpl* next;

} EGLSurfaceImpl;

typedef struct _EGLContextListImpl
{

    EGLSurfaceImpl* surface;

    NativeContextContainer nativeContextContainer;

    struct _EGLContextListImpl* next;

} EGLContextListImpl;

typedef struct _EGLContextImpl
{

    EGLBoolean initialized;
    EGLBoolean destroy;

    EGLint configId;
    EGLenum clientAPI;

    struct _EGLContextImpl* sharedCtx;

    EGLContextListImpl* rootCtxList;

    EGLint attribList[CONTEXT_ATTRIB_LIST_SIZE];

    struct _EGLContextImpl* next;

} EGLContextImpl;

typedef struct _EGLImageImpl
{

    EGLenum target;

    EGLClientBuffer buffer;

    struct _EGLImageImpl* next;

} EGLImageImpl;

typedef struct _EGLSyncImpl
{

    EGLenum type;

    void* glSync;

    struct _EGLSyncImpl* next;

} EGLSyncImpl;

typedef struct _EGLDisplayImpl
{
    std::mutex mutex;

    EGLBoolean initialized;
    EGLBoolean destroy;

    EGLNativeDisplayType display_id;

    EGLSurfaceImpl* rootSurface;
    EGLContextImpl* rootCtx;
    EGLConfigImpl* rootConfig;
    EGLSyncImpl* rootSync;
    EGLImageImpl* rootImage;

    EGLBoolean srgbFramebufferSupported;

    uint32_t supportedHDRColorspaces;  // bitmask of EGL_HDR_CS_*_BIT flags

    EGLSurfaceImpl* currentDraw;
    EGLSurfaceImpl* currentRead;
    EGLContextImpl* currentCtx;

    struct _EGLDisplayImpl* next;

} EGLDisplayImpl;

typedef struct _LocalStorage
{
    EGLint error;

    EGLenum api;

    EGLContextImpl* currentCtx;
} LocalStorage;

//
#if __cplusplus
extern "C" {
#endif
void _eglInternalSetDefaultConfig(EGLConfigImpl* config);
#if __cplusplus
}
#endif

//

EGLBoolean __internalInit(NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* GL_max_supported, EGLint* ES_max_supported);

EGLBoolean __internalTerminate(NativeLocalStorageContainer* nativeLocalStorageContainer);

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer);

EGLBoolean __processAttribList(EGLenum api, EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error);

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error);

EGLBoolean __createPbufferSurface(EGLSurfaceImpl* newSurface, const EGLint* attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error);

EGLBoolean __createPixmapSurface(EGLSurfaceImpl* newSurface, EGLNativePixmapType pixmap, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error);

EGLBoolean __destroySurface(EGLNativeDisplayType dpy, const EGLSurfaceImpl* surface);

EGLBoolean __copyBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* surface, EGLNativePixmapType target);

__eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname);

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error);

EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeContextContainer, const EGLint* attribList);

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer);

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface);

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval);

EGLBoolean __bindTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer);

EGLBoolean __releaseTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer);

EGLBoolean __getPlatformDependentHandles(void* out, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer);

// ── Platform inline helpers ──────────────────────────────────────────────────
// These keep all platform #ifdef logic inside this header so that the
// platform-agnostic core files (egl_display.cpp etc.) stay portable.
//
//  __getDefaultNativeDisplay  — maps EGL_DEFAULT_DISPLAY to the OS display
//  __matchPlatformDisplay     — validates eglGetPlatformDisplay platform token

#ifdef __cplusplus
#include <EGL/eglext.h>

#if defined(_WIN32) || defined(__VC32__) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer* c)
    { return reinterpret_cast<EGLNativeDisplayType>(c->hdc); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum platform, const void* native_display, EGLNativeDisplayType* out)
{
    if (platform == EGL_PLATFORM_DEVICE_EXT && !native_display)
        { *out = EGL_DEFAULT_DISPLAY; return EGL_TRUE; }
    return EGL_FALSE;
}

#elif defined(__QNX__)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer* c)
    { return reinterpret_cast<EGLNativeDisplayType>(c->display); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum, const void*, EGLNativeDisplayType*)
    { return EGL_FALSE; }

#elif defined(__EMSCRIPTEN__)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer*)
    { return reinterpret_cast<EGLNativeDisplayType>(0); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum, const void*, EGLNativeDisplayType*)
    { return EGL_FALSE; }

#elif defined(WL_EGL_PLATFORM)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer*)
    { return reinterpret_cast<EGLNativeDisplayType>(0); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum platform, const void* native_display, EGLNativeDisplayType* out)
{
    if (platform == EGL_PLATFORM_WAYLAND_EXT || platform == EGL_PLATFORM_WAYLAND_KHR)
        { *out = reinterpret_cast<EGLNativeDisplayType>(const_cast<void*>(native_display)); return EGL_TRUE; }
    return EGL_FALSE;
}

#elif defined(__GBM__)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer*)
    { return reinterpret_cast<EGLNativeDisplayType>(0); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum platform, const void* native_display, EGLNativeDisplayType* out)
{
    if (platform == EGL_PLATFORM_GBM_MESA || platform == EGL_PLATFORM_GBM_KHR)
        { *out = reinterpret_cast<EGLNativeDisplayType>(native_display); return EGL_TRUE; }
    return EGL_FALSE;
}

#elif defined(__ANDROID__) || defined(ANDROID)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer*)
    { return reinterpret_cast<EGLNativeDisplayType>(0); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum platform, const void* native_display, EGLNativeDisplayType* out)
{
    if (platform == EGL_PLATFORM_ANDROID_KHR)
        { *out = reinterpret_cast<EGLNativeDisplayType>(native_display); return EGL_TRUE; }
    return EGL_FALSE;
}

#elif defined(USE_X11) || defined(__unix__)

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer* c)
    { return reinterpret_cast<EGLNativeDisplayType>(c->display); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum platform, const void* native_display, EGLNativeDisplayType* out)
{
    if (platform == EGL_PLATFORM_X11_EXT || platform == EGL_PLATFORM_X11_KHR)
        { *out = reinterpret_cast<EGLNativeDisplayType>(const_cast<void*>(native_display)); return EGL_TRUE; }
    return EGL_FALSE;
}

#else

static inline EGLNativeDisplayType __getDefaultNativeDisplay(const NativeLocalStorageContainer*)
    { return reinterpret_cast<EGLNativeDisplayType>(0); }

static inline EGLBoolean __matchPlatformDisplay(EGLenum, const void*, EGLNativeDisplayType*)
    { return EGL_FALSE; }

#endif
#endif // __cplusplus

#endif /* EGL_INTERNAL_H_ */

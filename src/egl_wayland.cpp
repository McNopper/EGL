/**
 * EGL Wayland backend — GLX (XWayland) for GL context + Vulkan for HDR presentation.
 *
 * Design:
 *   - walkerDpy->display_id  = wl_display* (Wayland compositor connection)
 *   - s_x11Display           = X11 Display* opened on XWayland ($DISPLAY)
 *   - All GL operations use s_x11Display + GLXPbuffer (offscreen; no real X window).
 *   - swapBuffers presents through Vulkan (VK_KHR_wayland_surface) when the
 *     driver provides GL/Vulkan interop, and through wl_shm (software copy)
 *     for linear / sRGB / OpenVG surfaces otherwise.
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

#include "egl_common.h"
#include "eglctxinternals.h"
#include "egl_linux_vk.h"
#ifdef EGL_WAYLAND_ENABLE_GLES
#include "egl_linux_gles.h"
#endif

#include <wayland-client.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

// ── Function pointer type declarations ────────────────────────────────────────

typedef GLXContext (*PFNGLXCREATENEWCONTEXTPROC)(Display*, GLXFBConfig, int, GLXContext, Bool);
typedef Bool (*PFNGLXMAKECONTEXTCURRENTPROC)(Display*, GLXDrawable, GLXDrawable, GLXContext);
typedef void (*PFNGLXDESTROYCONTEXTPROC)(Display*, GLXContext);
typedef void (*PFNGLXSWAPBUFFERSPROC)(Display*, GLXDrawable);
typedef GLXFBConfig* (*PFNGLXCHOOSEFBCONFIGPROC)(Display*, int, const int*, int*);
typedef GLXFBConfig* (*PFNGLXGETFBCONFIGSPROC)(Display*, int, int*);
typedef int (*PFNGLXGETFBCONFIGATTRIBPROC)(Display*, GLXFBConfig, int, int*);
typedef XVisualInfo* (*PFNGLXGETVISUALFROMFBCONFIGPROC)(Display*, GLXFBConfig);
typedef GLXPbuffer (*PFNGLXCREATEPBUFFERPROC)(Display*, GLXFBConfig, const int*);
typedef void (*PFNGLXDESTROYPBUFFERPROC)(Display*, GLXPbuffer);
typedef const char* (*PFNGLXQUERYEXTENSIONSSTRINGPROC)(Display*, int);
typedef __GLXextFuncPtr (*PFNGLXGETPROCADDRESSARBPROC)(const GLubyte*);
typedef void (*PFNGLXBINDTEXIMAGEEXTPROC)(Display*, GLXDrawable, int, const int*);
typedef void (*PFNGLXRELEASETEXIMAGEEXTPROC)(Display*, GLXDrawable, int);

// ── Globals ───────────────────────────────────────────────────────────────────

static void*    s_libGL         = nullptr;
static Display* s_x11Display    = nullptr; // XWayland X11 display; all GLX ops use this
static Window   s_dummyWindow   = 0;
static Colormap s_dummyColormap = 0;

// Core GL sync (defined extern in egl_common.h)
__PFN_glFinish         glFinish_PTR         = nullptr;
__PFN_glFenceSync      glFenceSync_PTR      = nullptr;
__PFN_glDeleteSync     glDeleteSync_PTR     = nullptr;
__PFN_glClientWaitSync glClientWaitSync_PTR = nullptr;
__PFN_glWaitSync       glWaitSync_PTR       = nullptr;
__PFN_glGetSynciv      glGetSynciv_PTR      = nullptr;

// Readback for the software wl_shm presentation path.
typedef void (*PFNGLREADPIXELSPROC)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
static PFNGLREADPIXELSPROC s_glReadPixels = nullptr;

// Core GLX functions
static PFNGLXGETPROCADDRESSARBPROC     s_glXGetProcAddressARB     = nullptr;
static PFNGLXCREATENEWCONTEXTPROC      s_glXCreateNewContext      = nullptr;
static PFNGLXMAKECONTEXTCURRENTPROC    s_glXMakeContextCurrent    = nullptr;
static PFNGLXDESTROYCONTEXTPROC        s_glXDestroyContext        = nullptr;
static PFNGLXCHOOSEFBCONFIGPROC        s_glXChooseFBConfig        = nullptr;
static PFNGLXGETFBCONFIGSPROC          s_glXGetFBConfigs          = nullptr;
static PFNGLXGETFBCONFIGATTRIBPROC     s_glXGetFBConfigAttrib     = nullptr;
static PFNGLXGETVISUALFROMFBCONFIGPROC s_glXGetVisualFromFBConfig = nullptr;
static PFNGLXCREATEPBUFFERPROC         s_glXCreatePbuffer         = nullptr;
static PFNGLXDESTROYPBUFFERPROC        s_glXDestroyPbuffer        = nullptr;
static PFNGLXQUERYEXTENSIONSSTRINGPROC s_glXQueryExtensionsString = nullptr;

// GLX extensions
static PFNGLXCREATECONTEXTATTRIBSARBPROC s_glXCreateContextAttribsARB = nullptr;
static PFNGLXBINDTEXIMAGEEXTPROC         s_glXBindTexImageEXT         = nullptr;
static PFNGLXRELEASETEXIMAGEEXTPROC      s_glXReleaseTexImageEXT      = nullptr;

// GLX_EXT_texture_from_pixmap buffer enumerants (0x20DE / 0x20E2 from glxext.h)
#ifndef GLX_FRONT_LEFT_EXT
#define GLX_FRONT_LEFT_EXT 0x20DE
#endif
#ifndef GLX_BACK_LEFT_EXT
#define GLX_BACK_LEFT_EXT 0x20E2
#endif

// ── X error handler (needed for GLX version probing) ─────────────────────────

static int s_glxErrorOccurred = 0;
static int glxErrorHandler(Display*, XErrorEvent*)
{
    s_glxErrorOccurred = 1;
    return 0;
}

// ── Helper: look up a GLXFBConfig by its GLX_FBCONFIG_ID ─────────────────────

static GLXFBConfig __glxFBConfigById(int screen, int id)
{
    const int    attribs[] = {GLX_FBCONFIG_ID, id, None};
    int          n         = 0;
    GLXFBConfig* fbs       = s_glXChooseFBConfig(s_x11Display, screen, attribs, &n);
    if (!fbs)
        return nullptr;
    if (n == 0)
    {
        // A non-NULL array with no entries still has to be released.
        XFree(fbs);
        return nullptr;
    }
    GLXFBConfig fb = fbs[0];
    XFree(fbs);
    return fb;
}

// ── Helper: load a symbol from libGL via dlsym ────────────────────────────────

template <typename T>
static void loadSym(T& dst, const char* name)
{
    dst = reinterpret_cast<T>(dlsym(s_libGL, name));
}

// ── Platform lifecycle ────────────────────────────────────────────────────────

EGLBoolean __internalInit(NativeLocalStorageContainer* c, EGLint* GL_max, EGLint* ES_max)
{
    if (!c)
        return EGL_FALSE;
    if (c->x11Display && c->ctx)
        return EGL_TRUE;
    if (c->x11Display || c->ctx)
        return EGL_FALSE;

    // Load libGL dynamically
    s_libGL = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!s_libGL)
        s_libGL = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!s_libGL)
        return EGL_FALSE;

    loadSym(s_glXGetProcAddressARB, "glXGetProcAddressARB");
    loadSym(s_glXCreateNewContext, "glXCreateNewContext");
    loadSym(s_glXMakeContextCurrent, "glXMakeContextCurrent");
    loadSym(s_glXDestroyContext, "glXDestroyContext");
    loadSym(s_glXChooseFBConfig, "glXChooseFBConfig");
    loadSym(s_glXGetFBConfigs, "glXGetFBConfigs");
    loadSym(s_glXGetFBConfigAttrib, "glXGetFBConfigAttrib");
    loadSym(s_glXGetVisualFromFBConfig, "glXGetVisualFromFBConfig");
    loadSym(s_glXCreatePbuffer, "glXCreatePbuffer");
    loadSym(s_glXDestroyPbuffer, "glXDestroyPbuffer");
    loadSym(s_glXQueryExtensionsString, "glXQueryExtensionsString");

    if (!s_glXGetProcAddressARB || !s_glXChooseFBConfig ||
        !s_glXGetFBConfigAttrib || !s_glXGetVisualFromFBConfig ||
        !s_glXCreateNewContext || !s_glXMakeContextCurrent)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    glFinish_PTR         = reinterpret_cast<__PFN_glFinish>(s_glXGetProcAddressARB((const GLubyte*)"glFinish"));
    glFenceSync_PTR      = reinterpret_cast<__PFN_glFenceSync>(s_glXGetProcAddressARB((const GLubyte*)"glFenceSync"));
    glDeleteSync_PTR     = reinterpret_cast<__PFN_glDeleteSync>(s_glXGetProcAddressARB((const GLubyte*)"glDeleteSync"));
    glClientWaitSync_PTR = reinterpret_cast<__PFN_glClientWaitSync>(s_glXGetProcAddressARB((const GLubyte*)"glClientWaitSync"));
    glWaitSync_PTR       = reinterpret_cast<__PFN_glWaitSync>(s_glXGetProcAddressARB((const GLubyte*)"glWaitSync"));
    glGetSynciv_PTR      = reinterpret_cast<__PFN_glGetSynciv>(s_glXGetProcAddressARB((const GLubyte*)"glGetSynciv"));
    s_glReadPixels       = reinterpret_cast<PFNGLREADPIXELSPROC>(s_glXGetProcAddressARB((const GLubyte*)"glReadPixels"));

    // Open XWayland X11 display (uses $DISPLAY set by Wayland compositor / XWayland)
    s_x11Display = XOpenDisplay(getenv("DISPLAY"));
    if (!s_x11Display)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    int screen = DefaultScreen(s_x11Display);

    // Choose a minimal FBConfig for the dummy context
    static const int kDummyFBAttribs[] = {
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, // Pbuffer-only; no real X window needed
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        None};

    int          nfb = 0;
    GLXFBConfig* fbs = s_glXChooseFBConfig(s_x11Display, screen, kDummyFBAttribs, &nfb);
    if (!fbs || nfb == 0)
    {
        // glXChooseFBConfig may hand back a non-NULL zero-length allocation.
        if (fbs)
            XFree(fbs);
        XCloseDisplay(s_x11Display);
        s_x11Display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }
    GLXFBConfig chosenFB = fbs[0];
    XFree(fbs);

    XVisualInfo* vi = s_glXGetVisualFromFBConfig(s_x11Display, chosenFB);
    if (!vi)
    {
        XCloseDisplay(s_x11Display);
        s_x11Display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    // Create a 1×1 dummy window (needed for glXMakeContextCurrent with legacy context)
    s_dummyColormap = XCreateColormap(s_x11Display,
                                      RootWindow(s_x11Display, vi->screen),
                                      vi->visual, AllocNone);
    XSetWindowAttributes swa;
    swa.colormap     = s_dummyColormap;
    swa.border_pixel = 0;
    swa.event_mask   = 0;
    s_dummyWindow    = XCreateWindow(s_x11Display,
                                     RootWindow(s_x11Display, vi->screen),
                                     0, 0, 1, 1, 0,
                                     vi->depth, InputOutput, vi->visual,
                                     CWBorderPixel | CWColormap | CWEventMask, &swa);
    XFree(vi);

    if (!s_dummyWindow)
    {
        XFreeColormap(s_x11Display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(s_x11Display);
        s_x11Display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    s_glXCreateContextAttribsARB = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB"));
    s_glXBindTexImageEXT = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXBindTexImageEXT"));
    s_glXReleaseTexImageEXT = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXReleaseTexImageEXT"));

    // Create initial legacy GLX context for bootstrap
    c->ctx = s_glXCreateNewContext(s_x11Display, chosenFB, GLX_RGBA_TYPE, nullptr, True);
    if (!c->ctx)
    {
        XDestroyWindow(s_x11Display, s_dummyWindow);
        s_dummyWindow = 0;
        XFreeColormap(s_x11Display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(s_x11Display);
        s_x11Display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    if (!s_glXMakeContextCurrent(s_x11Display, s_dummyWindow, s_dummyWindow, c->ctx))
    {
        s_glXDestroyContext(s_x11Display, c->ctx);
        c->ctx = nullptr;
        XDestroyWindow(s_x11Display, s_dummyWindow);
        s_dummyWindow = 0;
        XFreeColormap(s_x11Display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(s_x11Display);
        s_x11Display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    // ── Probe max OpenGL version ──────────────────────────────────────────────

    auto* oldHandler = XSetErrorHandler(glxErrorHandler);

    int attrib_list[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
        GLX_CONTEXT_MINOR_VERSION_ARB, 0,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        0};

    static const int kGLVersions[][2] = {
        {4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, {3, 3}, {3, 2}, {3, 1}, {3, 0}, {2, 1}, {2, 0}, {1, 5}, {1, 4}, {1, 3}, {1, 2}, {1, 1}, {1, 0}, {0, 0}};

    GLXContext testctx = nullptr;
    GL_max[0]          = 0;
    GL_max[1]          = 0;

    if (s_glXCreateContextAttribsARB)
    {
        for (int i = 0; kGLVersions[i][0] && !testctx; ++i)
        {
            s_glxErrorOccurred = 0;
            attrib_list[1]     = kGLVersions[i][0];
            attrib_list[3]     = kGLVersions[i][1];
            XSync(s_x11Display, False);
            testctx = s_glXCreateContextAttribsARB(s_x11Display, chosenFB, nullptr, True, attrib_list);
            XSync(s_x11Display, False);
            if (s_glxErrorOccurred)
                testctx = nullptr;
            if (testctx)
            {
                GL_max[0] = kGLVersions[i][0];
                GL_max[1] = kGLVersions[i][1];
            }
        }
        if (testctx)
        {
            s_glXDestroyContext(s_x11Display, testctx);
            testctx = nullptr;
        }
    }

    // ── Probe max OpenGL ES version ───────────────────────────────────────────

    attrib_list[5] = GLX_CONTEXT_ES2_PROFILE_BIT_EXT;

    static const int kESVersions[][2] = {
        {3, 2}, {3, 1}, {3, 0}, {2, 0}, {1, 1}, {1, 0}, {0, 0}};

    ES_max[0] = 0;
    ES_max[1] = 0;

    if (s_glXCreateContextAttribsARB)
    {
        for (int i = 0; kESVersions[i][0] && !testctx; ++i)
        {
            s_glxErrorOccurred = 0;
            attrib_list[1]     = kESVersions[i][0];
            attrib_list[3]     = kESVersions[i][1];
            XSync(s_x11Display, False);
            testctx = s_glXCreateContextAttribsARB(s_x11Display, chosenFB, nullptr, True, attrib_list);
            XSync(s_x11Display, False);
            if (s_glxErrorOccurred)
                testctx = nullptr;
            if (testctx)
            {
                ES_max[0] = kESVersions[i][0];
                ES_max[1] = kESVersions[i][1];
            }
        }
        if (testctx)
        {
            s_glXDestroyContext(s_x11Display, testctx);
            testctx = nullptr;
        }
    }

    XSetErrorHandler(oldHandler);

    s_glXMakeContextCurrent(s_x11Display, None, None, nullptr);

    c->x11Display = s_x11Display;
    c->x11Window  = s_dummyWindow;

    // Init Vulkan for surface presentation (always needed on Wayland)
    __vkInit(); // non-fatal — surfaces will fail gracefully if Vulkan unavailable

    return EGL_TRUE;
}

EGLBoolean __internalTerminate(NativeLocalStorageContainer* c)
{
    if (!c)
        return EGL_FALSE;

    // This is reachable after a FAILED __internalInit, in which case neither the
    // GLX entry points nor the X11 Display were ever set up.
    if (s_glXMakeContextCurrent && s_x11Display)
        s_glXMakeContextCurrent(s_x11Display, None, None, nullptr);

    if (c->ctx)
    {
        if (s_glXDestroyContext && s_x11Display)
            s_glXDestroyContext(s_x11Display, c->ctx);
        c->ctx = nullptr;
    }
    if (s_dummyWindow && s_x11Display)
    {
        XDestroyWindow(s_x11Display, s_dummyWindow);
        s_dummyWindow = 0;
        c->x11Window  = 0;
    }
    if (s_dummyColormap && s_x11Display)
    {
        XFreeColormap(s_x11Display, s_dummyColormap);
        s_dummyColormap = 0;
    }

    // Tear the backends down BEFORE the Display is closed and libGL is unloaded:
    // the Vulkan/GL interop entry points still point into libGL, and the system EGL
    // was handed this exact Display*.
    __vkTerm();

#ifdef EGL_WAYLAND_ENABLE_GLES
    gles_terminate();
#endif

    if (s_x11Display)
    {
        XCloseDisplay(s_x11Display);
        s_x11Display  = nullptr;
        c->x11Display = nullptr;
    }
    if (s_libGL)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
    }

    return EGL_TRUE;
}

// ── Context ───────────────────────────────────────────────────────────────────

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
    (void)walkerDpy;
    if (!nativeContextContainer)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (nativeContextContainer->backend == EGL_BACKEND_GLES)
        return gles_destroyContext(nativeContextContainer->glesCtx);
#endif

    s_glXDestroyContext(s_x11Display, nativeContextContainer->ctx);
    return EGL_TRUE;
}

EGLBoolean __processAttribList(EGLenum api, EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error)
{
    if (!target_attrib_list || !attrib_list || !error)
        return EGL_FALSE;

    const EGLint defaultProfile = (api == EGL_OPENGL_ES_API)
                                      ? GLX_CONTEXT_ES2_PROFILE_BIT_EXT
                                      : GLX_CONTEXT_CORE_PROFILE_BIT_ARB;

    EGLint tmpl[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 1,
        GLX_CONTEXT_MINOR_VERSION_ARB, 0,
        GLX_CONTEXT_FLAGS_ARB, 0,
        GLX_CONTEXT_PROFILE_MASK_ARB, defaultProfile,
        GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, GLX_NO_RESET_NOTIFICATION_ARB,
        0};

    EGLint idx = 0;
    while (attrib_list[idx] != EGL_NONE)
    {
        EGLint value = attrib_list[idx + 1];

        switch (attrib_list[idx])
        {
        case EGL_CONTEXT_MAJOR_VERSION:
            if (value < 1)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            tmpl[1] = value;
            break;
        case EGL_CONTEXT_MINOR_VERSION:
            if (value < 0)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            tmpl[3] = value;
            break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK:
            if (value == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)
                tmpl[7] = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
            else if (value == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
                tmpl[7] = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        case EGL_CONTEXT_OPENGL_DEBUG:
            if (value == EGL_TRUE)
                tmpl[5] |= GLX_CONTEXT_DEBUG_BIT_ARB;
            else if (value == EGL_FALSE)
                tmpl[5] &= ~GLX_CONTEXT_DEBUG_BIT_ARB;
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
            if (value == EGL_TRUE)
                tmpl[5] |= GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
            else if (value == EGL_FALSE)
                tmpl[5] &= ~GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
            if (value == EGL_TRUE)
                tmpl[5] |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
            else if (value == EGL_FALSE)
                tmpl[5] &= ~GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
            if (value == EGL_NO_RESET_NOTIFICATION)
                tmpl[9] = GLX_NO_RESET_NOTIFICATION_ARB;
            else if (value == EGL_LOSE_CONTEXT_ON_RESET)
                tmpl[9] = GLX_LOSE_CONTEXT_ON_RESET_ARB;
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        default:
            *error = EGL_BAD_ATTRIBUTE;
            return EGL_FALSE;
        }

        idx += 2;
        // Fixed-slot template: nothing grows with the input length, so this only
        // caps the accepted attribute count. There are exactly 7 distinct EGL
        // context attributes and the switch above handles all of them, so a fully
        // specified legal list must pass - the check has to fire after the 7th
        // pair, not at it.
        if (idx > CONTEXT_ATTRIB_LIST_MAX_PAIRS * 2)
        {
            *error = EGL_BAD_ATTRIBUTE;
            return EGL_FALSE;
        }
    }

    memcpy(target_attrib_list, tmpl, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));
    return EGL_TRUE;
}

EGLBoolean __createContext(NativeContextContainer*       nativeContextContainer,
                           const EGLDisplayImpl*         walkerDpy,
                           const NativeSurfaceContainer* nativeSurfaceContainer,
                           const NativeContextContainer* sharedNativeContextContainer,
                           const EGLint*                 attribList)
{
    (void)walkerDpy;
    if (!nativeContextContainer || !nativeSurfaceContainer)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable())
    {
        EGLint major = 2, minor = 0;
        if (attribList)
        {
            for (EGLint i = 0; attribList[i] != 0; i += 2)
            {
                if (attribList[i] == GLX_CONTEXT_MAJOR_VERSION_ARB)
                    major = attribList[i + 1];
                else if (attribList[i] == GLX_CONTEXT_MINOR_VERSION_ARB)
                    minor = attribList[i + 1];
            }
        }
        void* shareCtx = nullptr;
        if (sharedNativeContextContainer)
        {
            if (sharedNativeContextContainer->backend != EGL_BACKEND_GLES)
                return EGL_FALSE;
            shareCtx = sharedNativeContextContainer->glesCtx;
        }

        void* ctx = nullptr;
        if (gles_createContext(&nativeSurfaceContainer->glesFormat, major, minor, shareCtx, &ctx) != EGL_TRUE)
            return EGL_FALSE;

        nativeContextContainer->backend = EGL_BACKEND_GLES;
        nativeContextContainer->glesCtx = ctx;
        nativeContextContainer->ctx     = nullptr;
        return EGL_TRUE;
    }
#endif

    if (!s_glXCreateContextAttribsARB)
        return EGL_FALSE;

    GLXContext share = sharedNativeContextContainer ? sharedNativeContextContainer->ctx : nullptr;

    auto* oldHandler   = XSetErrorHandler(glxErrorHandler);
    s_glxErrorOccurred = 0;

    XSync(s_x11Display, False);
    nativeContextContainer->ctx = s_glXCreateContextAttribsARB(
        s_x11Display, nativeSurfaceContainer->glxConfig, share, True, attribList);
    XSync(s_x11Display, False);

    XSetErrorHandler(oldHandler);

    if (s_glxErrorOccurred)
        nativeContextContainer->ctx = nullptr;

    nativeContextContainer->backend = EGL_BACKEND_GLX;
    nativeContextContainer->glesCtx = nullptr;
    return nativeContextContainer->ctx != nullptr;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl*         walkerDpy,
                         const NativeSurfaceContainer* nativeSurfaceContainer,
                         const NativeContextContainer* nativeContextContainer)
{
    (void)walkerDpy;

    if (!nativeContextContainer)
    {
#ifdef EGL_WAYLAND_ENABLE_GLES
        if (gles_isAvailable())
            gles_makeCurrent(nullptr, nullptr);
#endif
        return (EGLBoolean)s_glXMakeContextCurrent(s_x11Display, None, None, nullptr);
    }

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (nativeContextContainer->backend == EGL_BACKEND_GLES)
    {
        if (!nativeSurfaceContainer || nativeSurfaceContainer->backend != EGL_BACKEND_GLES)
            return EGL_FALSE;
        return gles_makeCurrent(nativeSurfaceContainer->glesSurface, nativeContextContainer->glesCtx);
    }
    if (nativeSurfaceContainer && nativeSurfaceContainer->backend == EGL_BACKEND_GLES)
        return EGL_FALSE;
#endif

    // Use GLXPbuffer as the read+draw surface for all GL operations
    if (!nativeSurfaceContainer)
        return EGL_FALSE;
    return (EGLBoolean)s_glXMakeContextCurrent(s_x11Display,
                                               nativeSurfaceContainer->glxPbuffer,
                                               nativeSurfaceContainer->glxPbuffer,
                                               nativeContextContainer->ctx);
}

// ── Surfaces ──────────────────────────────────────────────────────────────────

// ── Software presentation over wl_shm ────────────────────────────────────────
//
// Window presentation normally goes through Vulkan (see egl_linux_vk.h), which
// needs GL/Vulkan external-memory interop. A driver without those extensions
// (Mesa's lavapipe, for instance) cannot use it, and every window surface would
// fail to create. Linear and sRGB color spaces can be presented in software
// with the same visible result: the GLXPbuffer (or the OpenVG pixel buffer) is
// copied into WL_SHM_FORMAT_XRGB8888 buffers and committed to the
// application's wl_surface. HDR color spaces cannot be carried by wl_shm and
// are rejected like any color space the surface cannot present (EGL_BAD_MATCH,
// EGL_KHR_gl_colorspace).

struct ShmSlot
{
    struct wl_buffer* buf;
    void*             data;
    bool              busy;
};

struct _NativeShmPresenter
{
    struct wl_display*     wlDpy;
    struct wl_surface*     wlSurface;
    struct wl_event_queue* queue; // private: buffer release events must never
                                  // reach the application's own listeners
    struct wl_shm*         shm;
    struct wl_shm_pool*    pool;
    int                    poolFd;
    size_t                 slotStride; // mmap offsets must be page-aligned, so
                                        // a slot is stride-sized, w*h*4 usable
    int                    width;
    int                    height;
    ShmSlot                slot[2]; // one buffer may still be held by the
                                    // compositor when the next frame is ready
    void*                  scratch; // GL readback staging (bottom-up rows)
    size_t                 scratchBytes;
};

// Staging buffer for the GL readback, sized to the current window.
static void* __shmPresenterScratch(_NativeShmPresenter* p, int w, int h)
{
    const size_t bytes = (size_t)w * (size_t)h * 4;
    if (p->scratchBytes < bytes)
    {
        free(p->scratch);
        p->scratch = malloc(bytes);
        p->scratchBytes = p->scratch ? bytes : 0;
    }
    return p->scratch;
}

static void __shmBufferRelease(void* data, struct wl_buffer* /*buf*/)
{
    static_cast<ShmSlot*>(data)->busy = false;
}

static const struct wl_buffer_listener s_shmBufferListener = {__shmBufferRelease};

static void __shmGlobal(void* data, struct wl_registry* reg, uint32_t name, const char* iface, uint32_t /*ver*/)
{
    auto* p = static_cast<_NativeShmPresenter*>(data);
    if (!p->shm && strcmp(iface, "wl_shm") == 0)
        p->shm = static_cast<struct wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
}

static const struct wl_registry_listener s_shmRegistryListener = {
    __shmGlobal,
    [](void*, struct wl_registry*, uint32_t) {}};

static EGLBoolean __shmAllocBuffers(_NativeShmPresenter* p, int w, int h)
{
    const size_t slotBytes = (size_t)w * (size_t)h * 4;

    // mmap offsets must be page-aligned, so the pool slots are padded to the
    // page size; the buffers themselves keep the tight w*4 row stride.
    const long   pageSize   = sysconf(_SC_PAGESIZE);
    const size_t slotStride = ((slotBytes + (size_t)pageSize - 1) / (size_t)pageSize) * (size_t)pageSize;
    p->slotStride = slotStride;

    p->poolFd = memfd_create("egl-wl-shm", 0);
    if (p->poolFd < 0)
    {
        return EGL_FALSE;
    }
    if (ftruncate(p->poolFd, (off_t)(slotStride * 2)) != 0)
    {
        close(p->poolFd);
        p->poolFd = -1;
        return EGL_FALSE;
    }

    p->pool = wl_shm_create_pool(p->shm, p->poolFd, (int)(slotStride * 2));
    if (!p->pool)
    {
        close(p->poolFd);
        p->poolFd = -1;
        return EGL_FALSE;
    }

    for (int i = 0; i < 2; ++i)
    {
        p->slot[i].data = mmap(nullptr, slotBytes, PROT_READ | PROT_WRITE, MAP_SHARED, p->poolFd, (off_t)((size_t)i * slotStride));
        if (p->slot[i].data == MAP_FAILED)
        {
            p->slot[i].data = nullptr;
            return EGL_FALSE;
        }
        p->slot[i].buf = wl_shm_pool_create_buffer(p->pool, (int)((size_t)i * slotStride), w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
        if (!p->slot[i].buf)
        {
            return EGL_FALSE;
        }
        p->slot[i].busy = false;
        wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(p->slot[i].buf), p->queue);
        wl_buffer_add_listener(p->slot[i].buf, &s_shmBufferListener, &p->slot[i]);
    }

    p->width  = w;
    p->height = h;
    return EGL_TRUE;
}

static void __shmFreeBuffers(_NativeShmPresenter* p)
{
    const size_t slotBytes = (size_t)p->width * (size_t)p->height * 4;

    for (int i = 0; i < 2; ++i)
    {
        if (p->slot[i].buf)
            wl_buffer_destroy(p->slot[i].buf);
        if (p->slot[i].data)
            munmap(p->slot[i].data, slotBytes);
        p->slot[i].buf  = nullptr;
        p->slot[i].data = nullptr;
        p->slot[i].busy = false;
    }
    if (p->pool)
        wl_shm_pool_destroy(p->pool);
    if (p->poolFd >= 0)
        close(p->poolFd);
    p->pool   = nullptr;
    p->poolFd = -1;
}

static void __shmPresenterDestroy(NativeShmPresenter* p)
{
    if (!p)
        return;
    __shmFreeBuffers(p);
    free(p->scratch);
    if (p->shm)
        wl_shm_destroy(p->shm);
    // Only safe once every proxy that was attached to it is gone.
    if (p->queue)
        wl_event_queue_destroy(p->queue);
    free(p);
}

static NativeShmPresenter* __shmPresenterCreate(const EGLDisplayImpl* walkerDpy, EGLNativeWindowType win, int w, int h)
{
    auto* eglWin = reinterpret_cast<struct wl_egl_window*>(win);
    if (!walkerDpy || !eglWin || !eglWin->surface || w <= 0 || h <= 0)
        return nullptr;

    auto* p = static_cast<_NativeShmPresenter*>(calloc(1, sizeof(_NativeShmPresenter)));
    if (!p)
        return nullptr;

    p->wlDpy     = reinterpret_cast<struct wl_display*>(walkerDpy->display_id);
    p->wlSurface = eglWin->surface;
    p->poolFd    = -1;

    // Bind wl_shm on a private queue (see the note in __initialize): the
    // application's default event queue must never be dispatched from inside
    // EGL entry points.
    p->queue = wl_display_create_queue(p->wlDpy);
    if (!p->queue)
    {
        free(p);
        return nullptr;
    }

    struct wl_registry* reg = wl_display_get_registry(p->wlDpy);
    wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(reg), p->queue);
    wl_registry_add_listener(reg, &s_shmRegistryListener, p);
    wl_display_roundtrip_queue(p->wlDpy, p->queue);
    wl_registry_destroy(reg);

    if (!p->shm || __shmAllocBuffers(p, w, h) != EGL_TRUE)
    {
        __shmPresenterDestroy(p);
        return nullptr;
    }

    return p;
}

static EGLBoolean __shmPresenterPresent(NativeShmPresenter* p, const void* src, EGLint srcStride, bool srcBottomUp, int w, int h)
{
    if (!p || !src || w <= 0 || h <= 0)
        return EGL_FALSE;

    // EGL 1.5 section 3.10.1.1: detect a native-window resize and resize the
    // surface before copying its pixels to the native window.
    if (w != p->width || h != p->height)
    {
        __shmFreeBuffers(p);
        if (__shmAllocBuffers(p, w, h) != EGL_TRUE)
            return EGL_FALSE;
    }

    // Collect buffer releases without dispatching the application's queue.
    wl_display_dispatch_queue_pending(p->wlDpy, p->queue);

    ShmSlot* slot = nullptr;
    for (int i = 0; i < 2; ++i)
        if (!p->slot[i].busy)
        {
            slot = &p->slot[i];
            break;
        }
    if (!slot)
    {
        // Both buffers are still held by the compositor; their release events
        // arrive on the private queue only, so wait for them there. The
        // compositor dying ends the wait with an error instead of a hang.
        if (wl_display_dispatch_queue(p->wlDpy, p->queue) < 0)
            return EGL_FALSE;
        for (int i = 0; i < 2; ++i)
            if (!p->slot[i].busy)
            {
                slot = &p->slot[i];
                break;
            }
        if (!slot)
            return EGL_FALSE;
    }

    // WL_SHM_FORMAT_XRGB8888 is B,G,R,X per pixel - byte-identical to the
    // VG_sARGB_8888 and BGRA GL sources. Only the row order differs: Wayland
    // is top-down, GL reads back and OpenVG renders bottom-up.
    auto* dst = static_cast<char*>(slot->data);
    for (int row = 0; row < h; ++row)
    {
        const char* srcRow = static_cast<const char*>(src) + (srcBottomUp ? (size_t)(h - 1 - row) : (size_t)row) * (size_t)srcStride;
        memcpy(dst + (size_t)row * (size_t)w * 4, srcRow, (size_t)w * 4);
    }

    slot->busy = true;
    wl_surface_attach(p->wlSurface, slot->buf, 0, 0);
    wl_surface_damage(p->wlSurface, 0, 0, w, h);
    wl_surface_commit(p->wlSurface);

    if (wl_display_flush(p->wlDpy) < 0)
        return EGL_FALSE;

    return EGL_TRUE;
}

EGLBoolean __createWindowSurface(EGLSurfaceImpl*       newSurface,
                                 EGLNativeWindowType   win,
                                 const EGLint*         attrib_list,
                                 const EGLDisplayImpl* walkerDpy,
                                 const EGLConfigImpl*  walkerConfig,
                                 EGLint*               error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
        return EGL_FALSE;

    int         screen = DefaultScreen(s_x11Display);
    GLXFBConfig fb     = __glxFBConfigById(screen, walkerConfig->configId);
    if (!fb)
    {
        *error = EGL_BAD_CONFIG;
        return EGL_FALSE;
    }

    struct wl_egl_window* eglWin = reinterpret_cast<struct wl_egl_window*>(win);
    if (!eglWin || !eglWin->surface)
    {
        *error = EGL_BAD_NATIVE_WINDOW;
        return EGL_FALSE;
    }

    EGLint parsedColorspace = EGL_GL_COLORSPACE_LINEAR;

    if (attrib_list)
    {
        EGLint i = 0;
        while (attrib_list[i] != EGL_NONE)
        {
            EGLint value = attrib_list[i + 1];
            switch (attrib_list[i])
            {
            case EGL_GL_COLORSPACE:
                if (value == EGL_GL_COLORSPACE_LINEAR ||
                    value == EGL_GL_COLORSPACE_SRGB ||
                    value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
                    value == EGL_GL_COLORSPACE_SCRGB_EXT ||
                    value == EGL_GL_COLORSPACE_BT2020_PQ_EXT ||
                    value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
                    value == EGL_GL_COLORSPACE_BT2020_HLG_EXT ||
                    value == EGL_GL_COLORSPACE_DISPLAY_P3_EXT ||
                    value == EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT ||
                    value == EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT)
                    parsedColorspace = value;
                else
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
                break;
            case EGL_RENDER_BUFFER:
                break; // always double-buffered (Vulkan swapchain)
            case EGL_VG_ALPHA_FORMAT:
            case EGL_VG_COLORSPACE:
                *error = EGL_BAD_MATCH;
                return EGL_FALSE;
            default:
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            i += 2;
            // The 8-pair input cap must fire after the 8th pair, not at it.
            if (i > WINDOW_ATTRIB_LIST_MAX_PAIRS * 2)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
        }
    }

    uint32_t w = (uint32_t)(eglWin->width > 0 ? eglWin->width : 1);
    uint32_t h = (uint32_t)(eglWin->height > 0 ? eglWin->height : 1);

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable())
    {
        GlesSurfaceFormat format = glesMakeFormat(walkerConfig->redSize, walkerConfig->greenSize,
                                                  walkerConfig->blueSize, walkerConfig->alphaSize,
                                                  walkerConfig->depthSize, walkerConfig->stencilSize,
                                                  walkerConfig->samples);
        void* glesSurf = nullptr;
        if (gles_createWindowSurfaceWayland(static_cast<void*>(eglWin->surface), (int)w, (int)h, &format, parsedColorspace, &glesSurf) != EGL_TRUE || !glesSurf)
        {
            *error = EGL_BAD_ALLOC;
            return EGL_FALSE;
        }

        newSurface->drawToWindow                       = EGL_TRUE;
        newSurface->drawToPixmap                       = EGL_FALSE;
        newSurface->drawToPBuffer                      = EGL_FALSE;
        newSurface->doubleBuffer                       = walkerConfig->doubleBuffer;
        newSurface->configId                           = walkerConfig->configId;
        newSurface->width                              = (EGLint)w;
        newSurface->height                             = (EGLint)h;
        newSurface->swapBehavior                       = EGL_BUFFER_DESTROYED;
        newSurface->multisampleResolve                 = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
        newSurface->mipmapLevel                        = 0;
        newSurface->mipmapTexture                      = EGL_FALSE;
        newSurface->largestPbuffer                     = EGL_FALSE;
        newSurface->textureFormat                      = EGL_NO_TEXTURE;
        newSurface->textureTarget                      = EGL_NO_TEXTURE;
        newSurface->glColorspace                       = parsedColorspace;
        newSurface->initialized                        = EGL_TRUE;
        newSurface->destroy                            = EGL_FALSE;
        newSurface->win                                = win;
        newSurface->nativeSurfaceContainer.glxPbuffer  = 0;
        newSurface->nativeSurfaceContainer.glxConfig   = nullptr;
        newSurface->nativeSurfaceContainer.eglWindow   = eglWin;
        newSurface->nativeSurfaceContainer.vk          = nullptr;
        newSurface->nativeSurfaceContainer.shmPresent  = nullptr;
        newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLES;
        newSurface->nativeSurfaceContainer.glesSurface = glesSurf;
        newSurface->nativeSurfaceContainer.glesFormat  = format;
        newSurface->nativeSurfaceContainer.glColorspace = parsedColorspace;
        return EGL_TRUE;
    }
#endif

    // Create a GLXPbuffer of the same size — used as the GL rendering surface.
    const int pbufAttribs[] = {
        GLX_PBUFFER_WIDTH, (int)w,
        GLX_PBUFFER_HEIGHT, (int)h,
        GLX_LARGEST_PBUFFER, False,
        GLX_PRESERVED_CONTENTS, False,
        None};

    GLXPbuffer pbuf = 0;
    if (s_glXCreatePbuffer)
        pbuf = s_glXCreatePbuffer(s_x11Display, fb, pbufAttribs);
    if (!pbuf)
    {
        *error = EGL_BAD_ALLOC;
        return EGL_FALSE;
    }

    // Presentation goes through a Vulkan swapchain when the driver provides
    // GL/Vulkan interop; linear and sRGB additionally have a software wl_shm
    // path. An HDR color space is carried by the swapchain only. A config that
    // allows OpenVG always presents in software: the VG pixel buffer is client
    // memory and the X11 and Windows backends copy it with XPutImage / GDI as
    // well, never through Vulkan.
    NativeShmPresenter*        shm = nullptr;
    NativeHDRSurfaceContainer* vk  = nullptr;

    const bool hdrColorspace = (parsedColorspace != EGL_GL_COLORSPACE_LINEAR && parsedColorspace != EGL_GL_COLORSPACE_SRGB);
    const bool vgConfig      = (walkerConfig->renderableType & EGL_OPENVG_BIT) != 0;

    if (!vgConfig)
    {
        vk = static_cast<NativeHDRSurfaceContainer*>(malloc(sizeof(NativeHDRSurfaceContainer)));
        // walkerDpy->display_id is the wl_display* — used by __vkCreateHDRSurface for VK_KHR_wayland_surface
        if (!vk || __vkCreateHDRSurface(vk, win, walkerDpy->display_id, parsedColorspace, w, h) != EGL_TRUE)
        {
            free(vk);
            vk = nullptr;
        }
    }

    if (!vk && !hdrColorspace)
        shm = __shmPresenterCreate(walkerDpy, win, (int)w, (int)h);

    if (!vk && !shm)
    {
        s_glXDestroyPbuffer(s_x11Display, pbuf);
        *error = EGL_BAD_ALLOC;
        return EGL_FALSE;
    }

    newSurface->drawToWindow                       = EGL_TRUE;
    newSurface->drawToPixmap                       = EGL_FALSE;
    newSurface->drawToPBuffer                      = EGL_FALSE;
    newSurface->doubleBuffer                       = walkerConfig->doubleBuffer;
    newSurface->configId                           = walkerConfig->configId;
    newSurface->width                              = (EGLint)w;
    newSurface->height                             = (EGLint)h;
    newSurface->swapBehavior                       = EGL_BUFFER_DESTROYED;
    newSurface->multisampleResolve                 = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
    newSurface->mipmapLevel                        = 0;
    newSurface->mipmapTexture                      = EGL_FALSE;
    newSurface->largestPbuffer                     = EGL_FALSE;
    newSurface->textureFormat                      = EGL_NO_TEXTURE;
    newSurface->textureTarget                      = EGL_NO_TEXTURE;
    newSurface->glColorspace                       = parsedColorspace;
    newSurface->initialized                        = EGL_TRUE;
    newSurface->destroy                            = EGL_FALSE;
    newSurface->win                                = win;
    newSurface->nativeSurfaceContainer.glxPbuffer  = pbuf;
    newSurface->nativeSurfaceContainer.glxConfig   = fb;
    newSurface->nativeSurfaceContainer.eglWindow   = eglWin;
    newSurface->nativeSurfaceContainer.vk          = vk;
    newSurface->nativeSurfaceContainer.shmPresent  = shm;
    newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLX;
    newSurface->nativeSurfaceContainer.glesSurface = nullptr;

    return EGL_TRUE;
}

EGLBoolean __createPbufferSurface(EGLSurfaceImpl*       newSurface,
                                  const EGLint*         attrib_list,
                                  const EGLDisplayImpl* walkerDpy,
                                  const EGLConfigImpl*  walkerConfig,
                                  EGLint*               error)
{
    (void)walkerDpy;
    if (!newSurface || !walkerConfig || !error)
        return EGL_FALSE;

    int         screen = DefaultScreen(s_x11Display);
    GLXFBConfig fb     = __glxFBConfigById(screen, walkerConfig->configId);
    if (!fb)
    {
        *error = EGL_BAD_CONFIG;
        return EGL_FALSE;
    }

    int        width = 0, height = 0;
    EGLBoolean largestPbuf    = EGL_FALSE;
    EGLBoolean mipmapTex      = EGL_FALSE;
    EGLint     texFormat      = EGL_NO_TEXTURE;
    EGLint     texTarget      = EGL_NO_TEXTURE;
    EGLint     pbufColorspace = EGL_GL_COLORSPACE_LINEAR;

    if (attrib_list)
    {
        EGLint i = 0;
        while (attrib_list[i] != EGL_NONE)
        {
            EGLint value = attrib_list[i + 1];
            switch (attrib_list[i])
            {
            case EGL_WIDTH: width = value; break;
            case EGL_HEIGHT: height = value; break;
            case EGL_LARGEST_PBUFFER: largestPbuf = (EGLBoolean)value; break;
            case EGL_MIPMAP_TEXTURE: mipmapTex = (EGLBoolean)value; break;
            case EGL_GL_COLORSPACE:
                if (value == EGL_GL_COLORSPACE_LINEAR || value == EGL_GL_COLORSPACE_SRGB)
                    pbufColorspace = value;
                else
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
                break;
            case EGL_TEXTURE_FORMAT:
                if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_RGB && value != EGL_TEXTURE_RGBA)
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
                texFormat = value;
                break;
            case EGL_TEXTURE_TARGET:
                if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_2D)
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
                texTarget = value;
                break;
            case EGL_VG_ALPHA_FORMAT:
            case EGL_VG_COLORSPACE:
                *error = EGL_BAD_MATCH;
                return EGL_FALSE;
            default:
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            i += 2;
        }
    }

#ifdef EGL_WAYLAND_ENABLE_GLES
    // ES contexts run in the delegated GLES stack; their pbuffers must live
    // there as well, or the delegated context cannot bind them. Pbuffers
    // bound to a texture (EGL_TEXTURE_FORMAT) stay on GLX: bind-to-texture
    // has no delegated equivalent and keeps working with GLX contexts.
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable() && texFormat == EGL_NO_TEXTURE)
    {
        GlesSurfaceFormat format = glesMakeFormat(walkerConfig->redSize, walkerConfig->greenSize,
                                                  walkerConfig->blueSize, walkerConfig->alphaSize,
                                                  walkerConfig->depthSize, walkerConfig->stencilSize,
                                                  walkerConfig->samples);
        void* surf = nullptr;
        if (gles_createPbufferSurface(width, height, largestPbuf, pbufColorspace, &format, &surf) != EGL_TRUE)
        {
            *error = EGL_BAD_ALLOC;
            return EGL_FALSE;
        }

        // Read back the allocated dimensions like the GLX path does below.
        EGLint actualW = width;
        EGLint actualH = height;
        gles_querySurface(surf, EGL_WIDTH, &actualW);
        gles_querySurface(surf, EGL_HEIGHT, &actualH);

        newSurface->drawToWindow                       = EGL_FALSE;
        newSurface->drawToPixmap                       = EGL_FALSE;
        newSurface->drawToPBuffer                      = EGL_TRUE;
        newSurface->doubleBuffer                       = walkerConfig->doubleBuffer;
        newSurface->configId                           = walkerConfig->configId;
        newSurface->width                              = actualW;
        newSurface->height                             = actualH;
        newSurface->swapBehavior                       = EGL_BUFFER_DESTROYED;
        newSurface->multisampleResolve                 = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
        newSurface->mipmapLevel                        = 0;
        newSurface->mipmapTexture                      = EGL_FALSE;
        newSurface->largestPbuffer                     = largestPbuf;
        newSurface->textureFormat                      = texFormat;
        newSurface->textureTarget                      = texTarget;
        newSurface->glColorspace                       = pbufColorspace;
        newSurface->initialized                        = EGL_TRUE;
        newSurface->destroy                            = EGL_FALSE;
        newSurface->pbuf                               = 0;
        newSurface->nativeSurfaceContainer.drawable    = 0;
        newSurface->nativeSurfaceContainer.config      = nullptr;
        newSurface->nativeSurfaceContainer.hdr         = nullptr;
        newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLES;
        newSurface->nativeSurfaceContainer.glesSurface = surf;
        newSurface->nativeSurfaceContainer.glesFormat  = format;
        newSurface->nativeSurfaceContainer.glColorspace = pbufColorspace;

        return EGL_TRUE;
    }
#endif

    const int pbufAttribs[] = {
        GLX_PBUFFER_WIDTH, width,
        GLX_PBUFFER_HEIGHT, height,
        GLX_LARGEST_PBUFFER, largestPbuf ? True : False,
        GLX_PRESERVED_CONTENTS, False,
        None};

    GLXPbuffer pbuf = s_glXCreatePbuffer(s_x11Display, fb, pbufAttribs);
    if (!pbuf)
    {
        *error = EGL_BAD_ALLOC;
        return EGL_FALSE;
    }

    unsigned int actualW = (unsigned int)width;
    unsigned int actualH = (unsigned int)height;
    glXQueryDrawable(s_x11Display, pbuf, GLX_WIDTH, &actualW);
    glXQueryDrawable(s_x11Display, pbuf, GLX_HEIGHT, &actualH);

    newSurface->drawToWindow                       = EGL_FALSE;
    newSurface->drawToPixmap                       = EGL_FALSE;
    newSurface->drawToPBuffer                      = EGL_TRUE;
    newSurface->doubleBuffer                       = walkerConfig->doubleBuffer;
    newSurface->configId                           = walkerConfig->configId;
    newSurface->width                              = (EGLint)actualW;
    newSurface->height                             = (EGLint)actualH;
    newSurface->swapBehavior                       = EGL_BUFFER_DESTROYED;
    newSurface->multisampleResolve                 = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
    newSurface->mipmapLevel                        = 0;
    newSurface->mipmapTexture                      = mipmapTex;
    newSurface->largestPbuffer                     = largestPbuf;
    newSurface->textureFormat                      = texFormat;
    newSurface->textureTarget                      = texTarget;
    newSurface->glColorspace                       = pbufColorspace;
    newSurface->initialized                        = EGL_TRUE;
    newSurface->destroy                            = EGL_FALSE;
    newSurface->pbuf                               = (GLXPbuffer)pbuf;
    newSurface->nativeSurfaceContainer.glxPbuffer  = pbuf;
    newSurface->nativeSurfaceContainer.glxConfig   = fb;
    newSurface->nativeSurfaceContainer.eglWindow   = nullptr;
    newSurface->nativeSurfaceContainer.vk          = nullptr;
    newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLX;
    newSurface->nativeSurfaceContainer.glesSurface = nullptr;

    return EGL_TRUE;
}

EGLBoolean __createPixmapSurface(EGLSurfaceImpl* /*newSurface*/,
                                 EGLNativePixmapType /*pixmap*/,
                                 const EGLint* /*attrib_list*/,
                                 const EGLDisplayImpl* /*walkerDpy*/,
                                 const EGLConfigImpl* /*walkerConfig*/,
                                 EGLint* error)
{
    // Wayland composited model does not expose X pixmaps to EGL clients.
    *error = EGL_BAD_NATIVE_PIXMAP;
    return EGL_FALSE;
}

EGLBoolean __destroySurface(EGLNativeDisplayType dpyType, const EGLSurfaceImpl* surface)
{
    (void)dpyType;
    if (!surface)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (surface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
        return gles_destroySurface(surface->nativeSurfaceContainer.glesSurface);
#endif

    if (surface->nativeSurfaceContainer.vk)
    {
        __vkDestroyHDRSurface(surface->nativeSurfaceContainer.vk);
        free(surface->nativeSurfaceContainer.vk);
    }

    __shmPresenterDestroy(surface->nativeSurfaceContainer.shmPresent);

    if (surface->drawToPBuffer && s_glXDestroyPbuffer)
        s_glXDestroyPbuffer(s_x11Display, surface->nativeSurfaceContainer.glxPbuffer);
    else if (!surface->drawToPixmap && surface->drawToWindow && s_glXDestroyPbuffer)
        s_glXDestroyPbuffer(s_x11Display, surface->nativeSurfaceContainer.glxPbuffer);

    return EGL_TRUE;
}

EGLBoolean __copyBuffers(const EGLDisplayImpl* /*walkerDpy*/,
                         const EGLSurfaceImpl* /*surface*/,
                         EGLNativePixmapType /*target*/)
{
    // eglCopyBuffers to an X pixmap is not supported on the Wayland backend.
    return EGL_FALSE;
}

// ── Proc address ──────────────────────────────────────────────────────────────

__eglMustCastToProperFunctionPointerType __getProcAddress(const char* procname)
{
    if (s_glXGetProcAddressARB)
    {
        auto ptr = s_glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(procname));
        if (ptr)
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(ptr);
    }
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(dlsym(s_libGL, procname));
}

// ── Display initialisation: enumerate FBConfigs → EGL configs ─────────────────

EGLBoolean __initialize(EGLDisplayImpl*                    walkerDpy,
                        const NativeLocalStorageContainer* nativeLocalStorageContainer,
                        EGLint*                            error)
{
    if (!walkerDpy || !nativeLocalStorageContainer || !error)
        return EGL_FALSE;

    int screen = DefaultScreen(s_x11Display);

    const char* glxExts = s_glXQueryExtensionsString
                              ? s_glXQueryExtensionsString(s_x11Display, screen)
                              : "";

    bool esSupported = (strstr(glxExts, "GLX_EXT_create_context_es2_profile") != nullptr ||
                        strstr(glxExts, "GLX_EXT_create_context_es_profile") != nullptr);
#ifdef EGL_WAYLAND_ENABLE_GLES
    {
        EGLint             glesMax[2] = {0, 0};
        struct wl_display* wlDpy      = reinterpret_cast<struct wl_display*>(walkerDpy->display_id);
        if (gles_init_wayland(wlDpy, glesMax) == EGL_TRUE)
        {
            esSupported = true;
            if (glesMax[0] > g_ES_max_supported_version[0] ||
                (glesMax[0] == g_ES_max_supported_version[0] &&
                 glesMax[1] > g_ES_max_supported_version[1]))
            {
                g_ES_max_supported_version[0] = glesMax[0];
                g_ES_max_supported_version[1] = glesMax[1];
            }
        }
    }
#endif
    // Advertise exactly what the native layer can deliver (see the X11
    // backend): ES 1.x exists only behind a real GLES1 implementation.
    const EGLint esMask = (esSupported
                               ? ((g_ES_max_supported_version[0] >= 3 ? EGL_OPENGL_ES3_BIT : 0) |
                                  (g_ES_max_supported_version[0] >= 2 ? EGL_OPENGL_ES2_BIT : 0))
                               : 0)
#ifdef EGL_LINUX_ENABLE_GLES
                              | (gles_isAvailable() ? EGL_OPENGL_ES_BIT : 0)
#endif
        ;

    walkerDpy->srgbFramebufferSupported =
        (strstr(glxExts, "GLX_ARB_framebuffer_sRGB") != nullptr ||
         strstr(glxExts, "GLX_EXT_framebuffer_sRGB") != nullptr)
            ? EGL_TRUE
            : EGL_FALSE;

    // Query HDR colorspace support.
    // walkerDpy->display_id is the wl_display* — pass it plus a temporary wl_surface.
    // We create a temp wl_surface from the wl_compositor if available.
    {
        struct wl_display*    wlDpy      = reinterpret_cast<struct wl_display*>(walkerDpy->display_id);
        struct wl_surface*    tmpSurface = nullptr;
        struct wl_compositor* compositor = nullptr;

        // Mini registry scan to get wl_compositor for the temp surface
        struct RegistryCtx
        {
            struct wl_compositor* compositor;
        };
        static const wl_registry_listener regListener = {
            // global
            [](void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t ver)
            {
                auto* ctx = static_cast<RegistryCtx*>(data);
                if (!ctx->compositor && strcmp(iface, "wl_compositor") == 0)
                    ctx->compositor = static_cast<struct wl_compositor*>(
                        wl_registry_bind(reg, name, &wl_compositor_interface,
                                         ver < 4 ? ver : 4));
            },
            // global_remove
            [](void*, wl_registry*, uint32_t) {}};

        RegistryCtx ctx = {};

        // Never dispatch the application's default event queue from inside
        // eglInitialize: wl_display_roundtrip would re-entrantly deliver the app's
        // own xdg_toplevel.configure / pointer / ping events to its listeners, and it
        // races with an app dispatching on another thread. Everything below runs on a
        // private queue instead; proxies created from a proxy inherit its queue, but
        // set it explicitly so the ownership is obvious.
        struct wl_event_queue* queue = wl_display_create_queue(wlDpy);
        if (!queue)
        {
            walkerDpy->supportedHDRColorspaces = 0;
        }
        else
        {
            wl_registry* reg = wl_display_get_registry(wlDpy);
            wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(reg), queue);
            wl_registry_add_listener(reg, &regListener, &ctx);
            wl_display_roundtrip_queue(wlDpy, queue);
            compositor = ctx.compositor;
            wl_registry_destroy(reg);

            if (compositor)
            {
                wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(compositor), queue);
                tmpSurface = wl_compositor_create_surface(compositor);
                if (tmpSurface)
                    wl_proxy_set_queue(reinterpret_cast<struct wl_proxy*>(tmpSurface), queue);
                wl_display_roundtrip_queue(wlDpy, queue);
            }

            if (tmpSurface)
            {
                struct wl_egl_window tmpWin;
                tmpWin.surface = tmpSurface;
                tmpWin.width   = 256;
                tmpWin.height  = 256;
                walkerDpy->supportedHDRColorspaces =
                    __vkQueryHDRColorspaces(walkerDpy->display_id,
                                            reinterpret_cast<EGLNativeWindowType>(&tmpWin));
                wl_surface_destroy(tmpSurface);
            }
            else
            {
                walkerDpy->supportedHDRColorspaces = 0;
            }

            if (compositor)
                wl_compositor_destroy(compositor);

            // Only safe once every proxy that was attached to it is gone.
            wl_event_queue_destroy(queue);
        }
    }

    // Enumerate FBConfigs — same as X11 backend
    int          nfb = 0;
    GLXFBConfig* fbs = s_glXGetFBConfigs(s_x11Display, screen, &nfb);
    if (!fbs || nfb == 0)
    {
        // The array can be non-NULL with zero elements.
        if (fbs)
            XFree(fbs);
        *error = EGL_NOT_INITIALIZED;
        return EGL_FALSE;
    }

    EGLConfigImpl* lastConfig = nullptr;

    for (int i = 0; i < nfb; ++i)
    {
        int renderType = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_RENDER_TYPE, &renderType);
        if (!(renderType & GLX_RGBA_BIT))
            continue;

        int drawableType = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_DRAWABLE_TYPE, &drawableType);
        if (!drawableType)
            continue;

        EGLConfigImpl* newConfig = reinterpret_cast<EGLConfigImpl*>(malloc(sizeof(EGLConfigImpl)));
        if (!newConfig)
        {
            *error = EGL_NOT_INITIALIZED;
            XFree(fbs);
            return EGL_FALSE;
        }
        _eglInternalSetDefaultConfig(newConfig);

        newConfig->next = nullptr;
        if (lastConfig)
            lastConfig->next = newConfig;
        else
            walkerDpy->rootConfig = newConfig;
        lastConfig = newConfig;

        int db = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_DOUBLEBUFFER, &db);
        newConfig->doubleBuffer = db ? EGL_TRUE : EGL_FALSE;

        // EGL 1.5 section 3.5.1: window surfaces are back-buffered, so only a
        // double-buffered fbconfig may advertise EGL_WINDOW_BIT (same rule as
        // the X11 backend - see the comment there).
        newConfig->drawToWindow  = ((drawableType & GLX_WINDOW_BIT) && newConfig->doubleBuffer) ? EGL_TRUE : EGL_FALSE;
        newConfig->drawToPixmap  = EGL_FALSE; // Wayland: pixmaps not exposed
        newConfig->drawToPBuffer = (drawableType & GLX_PBUFFER_BIT) ? EGL_TRUE : EGL_FALSE;
        newConfig->surfaceType   = 0;
        if (newConfig->drawToWindow)
            newConfig->surfaceType |= EGL_WINDOW_BIT;
        if (newConfig->drawToPBuffer)
            newConfig->surfaceType |= EGL_PBUFFER_BIT;

        // The OpenVG provider (see eglvg.h) renders into these configs as well.

#ifdef EGL_ENABLE_OPENVG
        const EGLint vg_mask = _eglGetVGProvider() ? EGL_OPENVG_BIT : 0;
#else
        const EGLint vg_mask = 0;
#endif

        newConfig->conformant      = EGL_OPENGL_BIT | esMask | vg_mask;
        newConfig->renderableType  = EGL_OPENGL_BIT | esMask | vg_mask;
        newConfig->colorBufferType = EGL_RGB_BUFFER;

        int fbid = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_FBCONFIG_ID, &fbid);
        newConfig->configId = fbid;

        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_RED_SIZE, &newConfig->redSize);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_GREEN_SIZE, &newConfig->greenSize);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_BLUE_SIZE, &newConfig->blueSize);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_ALPHA_SIZE, &newConfig->alphaSize);
        newConfig->bufferSize = newConfig->redSize + newConfig->greenSize + newConfig->blueSize + newConfig->alphaSize;

        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_DEPTH_SIZE, &newConfig->depthSize);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_STENCIL_SIZE, &newConfig->stencilSize);

        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_SAMPLE_BUFFERS, &newConfig->sampleBuffers);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_SAMPLES, &newConfig->samples);

        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_MAX_PBUFFER_WIDTH, &newConfig->maxPBufferWidth);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_MAX_PBUFFER_HEIGHT, &newConfig->maxPBufferHeight);
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_MAX_PBUFFER_PIXELS, &newConfig->maxPBufferPixels);

        // Bind-to-texture: GLX_EXT_texture_from_pixmap carries the per-fbconfig
        // capability; without the extension a config can never be bound.
        newConfig->bindToTextureRGB  = EGL_FALSE;
        newConfig->bindToTextureRGBA = EGL_FALSE;
        if (s_glXBindTexImageEXT)
        {
            int bindRgb = 0, bindRgba = 0;
            s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &bindRgb);
            s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &bindRgba);
            newConfig->bindToTextureRGB  = bindRgb ? EGL_TRUE : EGL_FALSE;
            newConfig->bindToTextureRGBA = bindRgba ? EGL_TRUE : EGL_FALSE;
        }

        int transType = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_TRANSPARENT_TYPE, &transType);
        if (transType == GLX_TRANSPARENT_RGB)
        {
            newConfig->transparentType = EGL_TRANSPARENT_RGB;
            s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_TRANSPARENT_RED_VALUE, &newConfig->transparentRedValue);
            s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_TRANSPARENT_GREEN_VALUE, &newConfig->transparentGreenValue);
            s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_TRANSPARENT_BLUE_VALUE, &newConfig->transparentBlueValue);
        }
        else
        {
            newConfig->transparentType = EGL_NONE;
        }

        int caveat = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_CONFIG_CAVEAT, &caveat);
        if (caveat == GLX_SLOW_CONFIG)
            newConfig->configCaveat = EGL_SLOW_CONFIG;
        else if (caveat == GLX_NON_CONFORMANT_CONFIG)
            newConfig->configCaveat = EGL_NON_CONFORMANT_CONFIG;
        else
            newConfig->configCaveat = EGL_NONE;

        int vid = 0;
        s_glXGetFBConfigAttrib(s_x11Display, fbs[i], GLX_VISUAL_ID, &vid);
        newConfig->nativeVisualId   = vid;
        newConfig->nativeVisualType = vid ? EGL_TRUE : EGL_NONE;
        newConfig->nativeRenderable = EGL_TRUE;

        newConfig->minSwapInterval   = 0;
        newConfig->maxSwapInterval   = 1;
        newConfig->level             = 0;
        newConfig->matchNativePixmap = EGL_NONE;
    }

    XFree(fbs);
    return EGL_TRUE;
}

// ── Swap ──────────────────────────────────────────────────────────────────────

EGLBoolean __queryBufferAge(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint* age)
{
    (void)walkerDpy;
    if (!walkerSurface || !age)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
    {
        // The delegated stack tracks the back-buffer age itself.
        return gles_querySurface(walkerSurface->nativeSurfaceContainer.glesSurface, EGL_BUFFER_AGE_EXT, age);
    }
#endif

    return EGL_FALSE;
}

EGLBoolean __windowSurfaceSize(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint* width, EGLint* height)
{
    (void)walkerDpy;
    if (!walkerSurface || !width || !height || !walkerSurface->win)
        return EGL_FALSE;

    // Our wl_egl_window tracks the size the client gave the native window;
    // the swap paths mirror it into the delegated surface / shm presenter.
    const auto* eglWin = reinterpret_cast<const struct wl_egl_window*>(walkerSurface->win);
    if (eglWin->width <= 0 || eglWin->height <= 0)
        return EGL_FALSE;

    *width  = eglWin->width;
    *height = eglWin->height;
    return EGL_TRUE;
}

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface)
{
    (void)walkerDpy;
    if (!walkerSurface)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
    {
        // EGL 1.5 section 3.10.1.1: the swap detects a native-window resize and
        // mirrors it into the libwayland-egl window behind the delegated surface.
        auto* eglWin = reinterpret_cast<struct wl_egl_window*>(walkerSurface->win);
        gles_resizeWindowWayland(walkerSurface->nativeSurfaceContainer.glesSurface,
                                 eglWin->width > 0 ? eglWin->width : 1,
                                 eglWin->height > 0 ? eglWin->height : 1);
        return gles_swapBuffers(walkerSurface->nativeSurfaceContainer.glesSurface);
    }
#endif

    // Software presentation through wl_shm (no GL/Vulkan interop available).
    if (walkerSurface->nativeSurfaceContainer.shmPresent)
    {
        auto* p = walkerSurface->nativeSurfaceContainer.shmPresent;

        // EGL 1.5 section 3.10.3: posting performs an implicit flush; the
        // readback must also see every rendered command.
        if (glFinish_PTR)
            glFinish_PTR();

        if (!s_glReadPixels)
            return EGL_FALSE;

        // EGL 1.5 section 3.10.1.1: the swap detects a native-window resize.
        auto* eglWin = reinterpret_cast<struct wl_egl_window*>(walkerSurface->win);
        const int w  = eglWin->width > 0 ? eglWin->width : 1;
        const int h  = eglWin->height > 0 ? eglWin->height : 1;

        void* scratch = __shmPresenterScratch(p, w, h);
        if (!scratch)
            return EGL_FALSE;

        // GL_BGRA / UNSIGNED_BYTE yields B,G,R,X bytes per pixel - the
        // XRGB8888 memory layout wl_shm expects (the alpha byte is ignored).
        s_glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, scratch);

        return __shmPresenterPresent(p, scratch, w * 4, true, w, h);
    }

    // All Wayland window surfaces use Vulkan for presentation.
    if (walkerSurface->nativeSurfaceContainer.vk)
    {
        __vkUpdateHDRMetadata(walkerSurface->nativeSurfaceContainer.vk, walkerSurface);
        return __vkPresent(walkerSurface->nativeSurfaceContainer.vk);
    }

    // Pbuffer surfaces have no presentation target; just flush GL.
    if (glFinish_PTR)
        glFinish_PTR();
    return EGL_TRUE;
}

#ifdef EGL_ENABLE_OPENVG
EGLBoolean __presentVGBuffer(const EGLDisplayImpl* /*walkerDpy*/, const EGLSurfaceImpl* walkerSurface)
{
    if (!walkerSurface || !walkerSurface->vgBuffer)
        return EGL_FALSE;

    NativeShmPresenter* p = walkerSurface->nativeSurfaceContainer.shmPresent;
    if (!p)
        return EGL_FALSE;

    const int width  = walkerSurface->width;
    const int height = walkerSurface->height;
    if (width <= 0 || height <= 0)
        return EGL_FALSE;

    // The VG pixel buffer is VG_sARGB_8888 (B,G,R,X) - the XRGB8888 wl_shm
    // layout - and OpenVG renders bottom-up, Wayland is top-down. Same
    // straight row-flipped copy as the X11 and Windows backends.
    return __shmPresenterPresent(p, walkerSurface->vgBuffer, walkerSurface->vgStride, true, width, height);
}
#endif

EGLBoolean __swapInterval(const EGLDisplayImpl* /*walkerDpy*/, EGLint interval)
{
#ifdef EGL_WAYLAND_ENABLE_GLES
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable())
        return gles_swapInterval(interval);
#endif
    // Swap interval for Vulkan is controlled by the swapchain present mode.
    return EGL_TRUE;
}

// ── Texture binding ───────────────────────────────────────────────────────────

EGLBoolean __bindTexImage(const EGLDisplayImpl* walkerDpy,
                          const EGLSurfaceImpl* walkerSurface,
                          EGLint                buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
#ifdef EGL_WAYLAND_ENABLE_GLES
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
        return EGL_FALSE;
#endif
    if (!s_glXBindTexImageEXT)
        return EGL_FALSE;

    int glxBuffer = (buffer == EGL_BACK_BUFFER) ? GLX_BACK_LEFT_EXT : GLX_FRONT_LEFT_EXT;
    s_glXBindTexImageEXT(s_x11Display, (GLXDrawable)walkerSurface->pbuf, glxBuffer, nullptr);
    return EGL_TRUE;
}

EGLBoolean __releaseTexImage(const EGLDisplayImpl* walkerDpy,
                             const EGLSurfaceImpl* walkerSurface,
                             EGLint                buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
#ifdef EGL_WAYLAND_ENABLE_GLES
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
        return EGL_FALSE;
#endif
    if (!s_glXReleaseTexImageEXT)
        return EGL_FALSE;

    int glxBuffer = (buffer == EGL_BACK_BUFFER) ? GLX_BACK_LEFT_EXT : GLX_FRONT_LEFT_EXT;
    s_glXReleaseTexImageEXT(s_x11Display, (GLXDrawable)walkerSurface->pbuf, glxBuffer);
    return EGL_TRUE;
}

// ── Platform-dependent handle export ─────────────────────────────────────────

EGLBoolean __getPlatformDependentHandles(void*                         out,
                                         const EGLDisplayImpl*         walkerDpy,
                                         const NativeSurfaceContainer* nativeSurfaceContainer,
                                         const NativeContextContainer* nativeContextContainer)
{
    if (!out || !walkerDpy || !nativeSurfaceContainer || !nativeContextContainer)
        return EGL_FALSE;

#ifdef EGL_WAYLAND_ENABLE_GLES
    if (nativeContextContainer->backend == EGL_BACKEND_GLES)
        return EGL_FALSE;
#endif

    EGLContextInternals* h = reinterpret_cast<EGLContextInternals*>(out);
    h->display             = s_x11Display;
    h->surface.drawable    = nativeSurfaceContainer->glxPbuffer;
    h->surface.config      = nativeSurfaceContainer->glxConfig;
    h->context             = nativeContextContainer->ctx;
    return EGL_TRUE;
}

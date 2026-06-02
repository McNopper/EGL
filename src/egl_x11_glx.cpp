/**
 * EGL X11/GLX backend — Linux, FreeBSD, and any __unix__ platform with X11.
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
#include <EGL/eglctxinternals.h>

#ifdef LINUX_VK
#include "egl_linux_vk.h"
#endif

#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

#ifdef EGL_LINUX_ENABLE_GLES
#include "egl_linux_gles.h"
#endif

// ── Function pointer type declarations ────────────────────────────────────────
// Core GLX functions (not in glxext.h)
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
typedef GLXPixmap (*PFNGLXCREATEPIXMAPPROC)(Display*, GLXFBConfig, Pixmap, const int*);
typedef void (*PFNGLXDESTROYPIXMAPPROC)(Display*, GLXPixmap);
typedef GLXDrawable (*PFNGLXGETCURRENTDRAWABLEPROC)(void);
typedef const char* (*PFNGLXQUERYEXTENSIONSSTRINGPROC)(Display*, int);
typedef __GLXextFuncPtr (*PFNGLXGETPROCADDRESSARBPROC)(const GLubyte*);
// Extension function pointers — types provided by <GL/glxext.h> (included via <GL/glx.h>)

// ── Globals ───────────────────────────────────────────────────────────────────

static void*    s_libGL         = nullptr;
static Colormap s_dummyColormap = 0;

// Core GL sync (defined extern in egl_common.h)
__PFN_glFinish         glFinish_PTR         = nullptr;
__PFN_glFenceSync      glFenceSync_PTR      = nullptr;
__PFN_glDeleteSync     glDeleteSync_PTR     = nullptr;
__PFN_glClientWaitSync glClientWaitSync_PTR = nullptr;
__PFN_glWaitSync       glWaitSync_PTR       = nullptr;
__PFN_glGetSynciv      glGetSynciv_PTR      = nullptr;

// Core GLX functions
static PFNGLXGETPROCADDRESSARBPROC     s_glXGetProcAddressARB     = nullptr;
static PFNGLXCREATENEWCONTEXTPROC      s_glXCreateNewContext      = nullptr;
static PFNGLXMAKECONTEXTCURRENTPROC    s_glXMakeContextCurrent    = nullptr;
static PFNGLXDESTROYCONTEXTPROC        s_glXDestroyContext        = nullptr;
static PFNGLXSWAPBUFFERSPROC           s_glXSwapBuffers           = nullptr;
static PFNGLXCHOOSEFBCONFIGPROC        s_glXChooseFBConfig        = nullptr;
static PFNGLXGETFBCONFIGSPROC          s_glXGetFBConfigs          = nullptr;
static PFNGLXGETFBCONFIGATTRIBPROC     s_glXGetFBConfigAttrib     = nullptr;
static PFNGLXGETVISUALFROMFBCONFIGPROC s_glXGetVisualFromFBConfig = nullptr;
static PFNGLXCREATEPBUFFERPROC         s_glXCreatePbuffer         = nullptr;
static PFNGLXDESTROYPBUFFERPROC        s_glXDestroyPbuffer        = nullptr;
static PFNGLXCREATEPIXMAPPROC          s_glXCreatePixmap          = nullptr;
static PFNGLXDESTROYPIXMAPPROC         s_glXDestroyPixmap         = nullptr;
static PFNGLXGETCURRENTDRAWABLEPROC    s_glXGetCurrentDrawable    = nullptr;
static PFNGLXQUERYEXTENSIONSSTRINGPROC s_glXQueryExtensionsString = nullptr;

// GLX extensions
static PFNGLXCREATECONTEXTATTRIBSARBPROC s_glXCreateContextAttribsARB = nullptr;
static PFNGLXSWAPINTERVALEXTPROC         s_glXSwapIntervalEXT         = nullptr;
static PFNGLXSWAPINTERVALMESAPROC        s_glXSwapIntervalMESA        = nullptr;
static PFNGLXBINDTEXIMAGEEXTPROC         s_glXBindTexImageEXT         = nullptr;
static PFNGLXRELEASETEXIMAGEEXTPROC      s_glXReleaseTexImageEXT      = nullptr;

// ── X error handler (needed for GLX version probing) ─────────────────────────

static int s_glxErrorOccurred = 0;
static int glxErrorHandler(Display*, XErrorEvent*)
{
    s_glxErrorOccurred = 1;
    return 0;
}

// ── Helper: look up a GLXFBConfig by its GLX_FBCONFIG_ID ─────────────────────

static GLXFBConfig __glxFBConfigById(Display* dpy, int screen, int id)
{
    const int    attribs[] = {GLX_FBCONFIG_ID, id, None};
    int          n         = 0;
    GLXFBConfig* fbs       = s_glXChooseFBConfig(dpy, screen, attribs, &n);
    if (!fbs || n == 0)
        return nullptr;
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
    if (c->display && c->ctx)
        return EGL_TRUE;
    if (c->display || c->ctx)
        return EGL_FALSE;

    // Load libGL dynamically (mirrors Windows LoadLibrary("opengl32.dll"))
    s_libGL = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!s_libGL)
        s_libGL = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!s_libGL)
        return EGL_FALSE;

    loadSym(s_glXGetProcAddressARB, "glXGetProcAddressARB");
    loadSym(s_glXCreateNewContext, "glXCreateNewContext");
    loadSym(s_glXMakeContextCurrent, "glXMakeContextCurrent");
    loadSym(s_glXDestroyContext, "glXDestroyContext");
    loadSym(s_glXSwapBuffers, "glXSwapBuffers");
    loadSym(s_glXChooseFBConfig, "glXChooseFBConfig");
    loadSym(s_glXGetFBConfigs, "glXGetFBConfigs");
    loadSym(s_glXGetFBConfigAttrib, "glXGetFBConfigAttrib");
    loadSym(s_glXGetVisualFromFBConfig, "glXGetVisualFromFBConfig");
    loadSym(s_glXCreatePbuffer, "glXCreatePbuffer");
    loadSym(s_glXDestroyPbuffer, "glXDestroyPbuffer");
    loadSym(s_glXCreatePixmap, "glXCreatePixmap");
    loadSym(s_glXDestroyPixmap, "glXDestroyPixmap");
    loadSym(s_glXGetCurrentDrawable, "glXGetCurrentDrawable");
    loadSym(s_glXQueryExtensionsString, "glXQueryExtensionsString");

    if (!s_glXGetProcAddressARB || !s_glXChooseFBConfig ||
        !s_glXGetFBConfigAttrib || !s_glXGetVisualFromFBConfig ||
        !s_glXCreateNewContext || !s_glXMakeContextCurrent)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    // GLX sync pointers (safe before a current context)
    glFinish_PTR         = reinterpret_cast<__PFN_glFinish>(s_glXGetProcAddressARB((const GLubyte*)"glFinish"));
    glFenceSync_PTR      = reinterpret_cast<__PFN_glFenceSync>(s_glXGetProcAddressARB((const GLubyte*)"glFenceSync"));
    glDeleteSync_PTR     = reinterpret_cast<__PFN_glDeleteSync>(s_glXGetProcAddressARB((const GLubyte*)"glDeleteSync"));
    glClientWaitSync_PTR = reinterpret_cast<__PFN_glClientWaitSync>(s_glXGetProcAddressARB((const GLubyte*)"glClientWaitSync"));
    glWaitSync_PTR       = reinterpret_cast<__PFN_glWaitSync>(s_glXGetProcAddressARB((const GLubyte*)"glWaitSync"));
    glGetSynciv_PTR      = reinterpret_cast<__PFN_glGetSynciv>(s_glXGetProcAddressARB((const GLubyte*)"glGetSynciv"));

    // Open X display
    c->display = XOpenDisplay(nullptr);
    if (!c->display)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    int screen = DefaultScreen(c->display);

    // Choose a minimal FBConfig for the dummy context
    static const int kDummyFBAttribs[] = {
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DOUBLEBUFFER, True,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        None};

    int          nfb = 0;
    GLXFBConfig* fbs = s_glXChooseFBConfig(c->display, screen, kDummyFBAttribs, &nfb);
    if (!fbs || nfb == 0)
    {
        XCloseDisplay(c->display);
        c->display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }
    GLXFBConfig chosenFB = fbs[0];
    XFree(fbs);

    XVisualInfo* vi = s_glXGetVisualFromFBConfig(c->display, chosenFB);
    if (!vi)
    {
        XCloseDisplay(c->display);
        c->display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    // Create colormap and 1×1 dummy window (needed for GLX context current)
    s_dummyColormap = XCreateColormap(c->display,
                                      RootWindow(c->display, vi->screen),
                                      vi->visual, AllocNone);
    XSetWindowAttributes swa;
    swa.colormap     = s_dummyColormap;
    swa.border_pixel = 0;
    swa.event_mask   = 0;
    c->window        = XCreateWindow(c->display,
                                     RootWindow(c->display, vi->screen),
                                     0, 0, 1, 1, 0,
                                     vi->depth, InputOutput, vi->visual,
                                     CWBorderPixel | CWColormap | CWEventMask, &swa);
    XFree(vi);

    if (!c->window)
    {
        XFreeColormap(c->display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(c->display);
        c->display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    // Load GLX extension pointers (safe before making a context current)
    s_glXCreateContextAttribsARB = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB"));
    s_glXSwapIntervalEXT = reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT"));
    s_glXSwapIntervalMESA = reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA"));
    s_glXBindTexImageEXT = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXBindTexImageEXT"));
    s_glXReleaseTexImageEXT = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(
        s_glXGetProcAddressARB((const GLubyte*)"glXReleaseTexImageEXT"));

    // Create an initial legacy context to bootstrap extension loading
    c->ctx = s_glXCreateNewContext(c->display, chosenFB, GLX_RGBA_TYPE, nullptr, True);
    if (!c->ctx)
    {
        XDestroyWindow(c->display, c->window);
        c->window = 0;
        XFreeColormap(c->display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(c->display);
        c->display = nullptr;
        dlclose(s_libGL);
        s_libGL = nullptr;
        return EGL_FALSE;
    }

    if (!s_glXMakeContextCurrent(c->display, c->window, c->window, c->ctx))
    {
        s_glXDestroyContext(c->display, c->ctx);
        c->ctx = nullptr;
        XDestroyWindow(c->display, c->window);
        c->window = 0;
        XFreeColormap(c->display, s_dummyColormap);
        s_dummyColormap = 0;
        XCloseDisplay(c->display);
        c->display = nullptr;
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

    // Known valid (major, minor) pairs in descending order
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
            XSync(c->display, False);
            testctx = s_glXCreateContextAttribsARB(c->display, chosenFB, nullptr, True, attrib_list);
            XSync(c->display, False);
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
            s_glXDestroyContext(c->display, testctx);
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
            XSync(c->display, False);
            testctx = s_glXCreateContextAttribsARB(c->display, chosenFB, nullptr, True, attrib_list);
            XSync(c->display, False);
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
            s_glXDestroyContext(c->display, testctx);
            testctx = nullptr;
        }
    }

    XSetErrorHandler(oldHandler);

    // Release dummy context (stays bound during library lifetime via dummy window)
    s_glXMakeContextCurrent(c->display, None, None, nullptr);

#ifdef LINUX_VK
    __vkInit(); // non-fatal — HDR features simply disabled if Vulkan unavailable
#endif

#ifdef EGL_LINUX_ENABLE_GLES
    {
        EGLint glesMax[2] = {0, 0};
        if (gles_init(c->display, glesMax) == EGL_TRUE)
        {
            // Prefer system GLES version over GLX ES emulation
            if (glesMax[0] > ES_max[0] ||
                (glesMax[0] == ES_max[0] && glesMax[1] > ES_max[1]))
            {
                ES_max[0] = glesMax[0];
                ES_max[1] = glesMax[1];
            }
        }
    }
#endif

    return EGL_TRUE;
}

EGLBoolean __internalTerminate(NativeLocalStorageContainer* c)
{
    if (!c)
        return EGL_FALSE;

    s_glXMakeContextCurrent(c->display, None, None, nullptr);

    if (c->ctx)
    {
        s_glXDestroyContext(c->display, c->ctx);
        c->ctx = nullptr;
    }
    if (c->window)
    {
        XDestroyWindow(c->display, c->window);
        c->window = 0;
    }
    if (s_dummyColormap)
    {
        XFreeColormap(c->display, s_dummyColormap);
        s_dummyColormap = 0;
    }
    if (c->display)
    {
        XCloseDisplay(c->display);
        c->display = nullptr;
    }
    if (s_libGL)
    {
        dlclose(s_libGL);
        s_libGL = nullptr;
    }

#ifdef LINUX_VK
    __vkTerm();
#endif

#ifdef EGL_LINUX_ENABLE_GLES
    gles_terminate();
#endif

    return EGL_TRUE;
}

// ── Context ───────────────────────────────────────────────────────────────────

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
    if (!walkerDpy || !nativeContextContainer)
        return EGL_FALSE;

    Display* dpy = reinterpret_cast<Display*>(walkerDpy->display_id);
#ifdef EGL_LINUX_ENABLE_GLES
    if (nativeContextContainer->backend == EGL_BACKEND_GLES)
        return gles_destroyContext(nativeContextContainer->glesCtx);
#endif
    s_glXDestroyContext(dpy, nativeContextContainer->ctx);
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
        if (idx >= 7 * 2)
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
    if (!walkerDpy || !nativeContextContainer || !nativeSurfaceContainer)
        return EGL_FALSE;

    if (!s_glXCreateContextAttribsARB)
        return EGL_FALSE;

    Display*   dpy   = reinterpret_cast<Display*>(walkerDpy->display_id);
    GLXContext share = sharedNativeContextContainer ? sharedNativeContextContainer->ctx : nullptr;

#ifdef EGL_LINUX_ENABLE_GLES
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
                return EGL_FALSE; // Cannot share a GLX context with a GLES context
            shareCtx = sharedNativeContextContainer->glesCtx;
        }

        void* ctx = nullptr;
        if (gles_createContext(major, minor, shareCtx, &ctx) != EGL_TRUE)
            return EGL_FALSE;

        nativeContextContainer->backend = EGL_BACKEND_GLES;
        nativeContextContainer->glesCtx = ctx;
        nativeContextContainer->ctx     = nullptr;
        return EGL_TRUE;
    }
#endif

    nativeContextContainer->backend = EGL_BACKEND_GLX;
    nativeContextContainer->glesCtx = nullptr;

    auto* oldHandler   = XSetErrorHandler(glxErrorHandler);
    s_glxErrorOccurred = 0;

    XSync(dpy, False);
    nativeContextContainer->ctx = s_glXCreateContextAttribsARB(
        dpy, nativeSurfaceContainer->config, share, True, attribList);
    XSync(dpy, False);

    XSetErrorHandler(oldHandler);

    if (s_glxErrorOccurred)
        nativeContextContainer->ctx = nullptr;

    return nativeContextContainer->ctx != nullptr;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl*         walkerDpy,
                         const NativeSurfaceContainer* nativeSurfaceContainer,
                         const NativeContextContainer* nativeContextContainer)
{
    if (!walkerDpy)
        return EGL_FALSE;

    Display* dpy = reinterpret_cast<Display*>(walkerDpy->display_id);

#ifdef EGL_LINUX_ENABLE_GLES
    if (nativeContextContainer && nativeContextContainer->backend == EGL_BACKEND_GLES)
    {
        if (!nativeSurfaceContainer || nativeSurfaceContainer->backend != EGL_BACKEND_GLES)
            return EGL_FALSE;
        return gles_makeCurrent(nativeSurfaceContainer->glesSurface,
                                nativeContextContainer->glesCtx);
    }
    if (nativeContextContainer && nativeSurfaceContainer &&
        nativeSurfaceContainer->backend == EGL_BACKEND_GLES)
    {
        // GLX context on GLES surface — mismatch
        return EGL_FALSE;
    }
    if (!nativeContextContainer && gles_isAvailable())
        gles_makeCurrent(nullptr, nullptr);
#endif

    if (!nativeContextContainer)
        return (EGLBoolean)s_glXMakeContextCurrent(dpy, None, None, nullptr);

    return (EGLBoolean)s_glXMakeContextCurrent(dpy,
                                               nativeSurfaceContainer->drawable,
                                               nativeSurfaceContainer->drawable,
                                               nativeContextContainer->ctx);
}

// ── Surfaces ──────────────────────────────────────────────────────────────────

EGLBoolean __createWindowSurface(EGLSurfaceImpl*       newSurface,
                                 EGLNativeWindowType   win,
                                 const EGLint*         attrib_list,
                                 const EGLDisplayImpl* walkerDpy,
                                 const EGLConfigImpl*  walkerConfig,
                                 EGLint*               error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
        return EGL_FALSE;

    Display* dpy    = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      screen = DefaultScreen(dpy);

    GLXFBConfig fb = __glxFBConfigById(dpy, screen, walkerConfig->configId);
    if (!fb)
    {
        *error = EGL_BAD_CONFIG;
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
                    value == EGL_GL_COLORSPACE_SRGB)
                    parsedColorspace = value;
#ifdef LINUX_VK
                else if (value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
                         value == EGL_GL_COLORSPACE_SCRGB_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_PQ_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_HLG_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT)
                    parsedColorspace = value;
#endif
                else
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
                break;
            case EGL_RENDER_BUFFER:
                // GLX always double-buffers per FBConfig; ignore
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
            if (i >= 8 * 2)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
        }
    }

#ifdef EGL_LINUX_ENABLE_GLES
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable())
    {
        void* surf = nullptr;
        if (gles_createWindowSurface(reinterpret_cast<void*>(static_cast<uintptr_t>(win)), &surf) != EGL_TRUE)
        {
            *error = EGL_BAD_ALLOC;
            return EGL_FALSE;
        }

        Window       root;
        int          x, y;
        unsigned int w, h, bw, depth;
        XGetGeometry(dpy, (Drawable)win, &root, &x, &y, &w, &h, &bw, &depth);

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
        newSurface->nativeSurfaceContainer.drawable    = 0;
        newSurface->nativeSurfaceContainer.config      = nullptr;
        newSurface->nativeSurfaceContainer.hdr         = nullptr;
        newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLES;
        newSurface->nativeSurfaceContainer.glesSurface = surf;
        return EGL_TRUE;
    }
#endif

    // Query window geometry
    Window       root;
    int          x, y;
    unsigned int w, h, bw, depth;
    XGetGeometry(dpy, (Drawable)win, &root, &x, &y, &w, &h, &bw, &depth);

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
    newSurface->nativeSurfaceContainer.drawable    = (GLXDrawable)win;
    newSurface->nativeSurfaceContainer.config      = fb;
    newSurface->nativeSurfaceContainer.hdr         = nullptr;
    newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLX;
    newSurface->nativeSurfaceContainer.glesSurface = nullptr;

#ifdef LINUX_VK
    {
        VkFormat        fmt;
        VkColorSpaceKHR cs;
        if (__vkIsReady() && _eglHDRColorspaceToVk(parsedColorspace, &fmt, &cs))
        {
            auto* hdr = static_cast<NativeHDRSurfaceContainer*>(
                malloc(sizeof(NativeHDRSurfaceContainer)));
            if (hdr)
            {
                if (__vkCreateHDRSurface(hdr, win, walkerDpy->display_id,
                                         parsedColorspace, w, h) == EGL_TRUE)
                    newSurface->nativeSurfaceContainer.hdr = hdr;
                else
                    free(hdr);
            }
        }
    }
#endif

    return EGL_TRUE;
}

EGLBoolean __createPbufferSurface(EGLSurfaceImpl*       newSurface,
                                  const EGLint*         attrib_list,
                                  const EGLDisplayImpl* walkerDpy,
                                  const EGLConfigImpl*  walkerConfig,
                                  EGLint*               error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
        return EGL_FALSE;

    Display* dpy    = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      screen = DefaultScreen(dpy);

    GLXFBConfig fb = __glxFBConfigById(dpy, screen, walkerConfig->configId);
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

    const int pbufAttribs[] = {
        GLX_PBUFFER_WIDTH, width,
        GLX_PBUFFER_HEIGHT, height,
        GLX_LARGEST_PBUFFER, largestPbuf ? True : False,
        GLX_PRESERVED_CONTENTS, False,
        None};

    GLXPbuffer pbuf = s_glXCreatePbuffer(dpy, fb, pbufAttribs);
    if (!pbuf)
    {
        *error = EGL_BAD_ALLOC;
        return EGL_FALSE;
    }

    // Read back actual dimensions (driver may have clamped them)
    unsigned int actualW = (unsigned int)width;
    unsigned int actualH = (unsigned int)height;
    glXQueryDrawable(dpy, pbuf, GLX_WIDTH, &actualW);
    glXQueryDrawable(dpy, pbuf, GLX_HEIGHT, &actualH);

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
    newSurface->nativeSurfaceContainer.drawable    = (GLXDrawable)pbuf;
    newSurface->nativeSurfaceContainer.config      = fb;
    newSurface->nativeSurfaceContainer.hdr         = nullptr;
    newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLX;
    newSurface->nativeSurfaceContainer.glesSurface = nullptr;

    return EGL_TRUE;
}

EGLBoolean __createPixmapSurface(EGLSurfaceImpl*       newSurface,
                                 EGLNativePixmapType   pixmap,
                                 const EGLint*         attrib_list,
                                 const EGLDisplayImpl* walkerDpy,
                                 const EGLConfigImpl*  walkerConfig,
                                 EGLint*               error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
        return EGL_FALSE;
    if (!pixmap)
    {
        *error = EGL_BAD_NATIVE_PIXMAP;
        return EGL_FALSE;
    }

    Display* dpy    = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      screen = DefaultScreen(dpy);

    GLXFBConfig fb = __glxFBConfigById(dpy, screen, walkerConfig->configId);
    if (!fb)
    {
        *error = EGL_BAD_CONFIG;
        return EGL_FALSE;
    }

    EGLint glColorspace = EGL_GL_COLORSPACE_LINEAR;
    if (attrib_list)
    {
        EGLint i = 0;
        while (attrib_list[i] != EGL_NONE)
        {
            EGLint value = attrib_list[i + 1];
            switch (attrib_list[i])
            {
            case EGL_GL_COLORSPACE:
                if (value == EGL_GL_COLORSPACE_LINEAR || value == EGL_GL_COLORSPACE_SRGB)
                    glColorspace = value;
                else
                {
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
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

    GLXPixmap glxPix = s_glXCreatePixmap(dpy, fb, (Pixmap)pixmap, nullptr);
    if (!glxPix)
    {
        *error = EGL_BAD_NATIVE_PIXMAP;
        return EGL_FALSE;
    }

    // Query pixmap dimensions via X
    Window       root;
    int          x, y;
    unsigned int w, h, bw, depth;
    XGetGeometry(dpy, (Drawable)pixmap, &root, &x, &y, &w, &h, &bw, &depth);

    newSurface->drawToWindow                       = EGL_FALSE;
    newSurface->drawToPixmap                       = EGL_TRUE;
    newSurface->drawToPBuffer                      = EGL_FALSE;
    newSurface->doubleBuffer                       = EGL_FALSE;
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
    newSurface->glColorspace                       = glColorspace;
    newSurface->initialized                        = EGL_TRUE;
    newSurface->destroy                            = EGL_FALSE;
    newSurface->pixmap                             = pixmap;
    newSurface->nativeSurfaceContainer.drawable    = (GLXDrawable)glxPix;
    newSurface->nativeSurfaceContainer.config      = fb;
    newSurface->nativeSurfaceContainer.hdr         = nullptr;
    newSurface->nativeSurfaceContainer.backend     = EGL_BACKEND_GLX;
    newSurface->nativeSurfaceContainer.glesSurface = nullptr;

    return EGL_TRUE;
}

EGLBoolean __destroySurface(EGLNativeDisplayType dpyType, const EGLSurfaceImpl* surface)
{
    if (!surface)
        return EGL_FALSE;

    Display* dpy = reinterpret_cast<Display*>(dpyType);

#ifdef EGL_LINUX_ENABLE_GLES
    if (surface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
        return gles_destroySurface(surface->nativeSurfaceContainer.glesSurface);
#endif

#ifdef LINUX_VK
    if (surface->nativeSurfaceContainer.hdr)
    {
        __vkDestroyHDRSurface(surface->nativeSurfaceContainer.hdr);
        free(surface->nativeSurfaceContainer.hdr);
    }
#endif

    if (surface->drawToPBuffer && s_glXDestroyPbuffer)
        s_glXDestroyPbuffer(dpy, (GLXPbuffer)surface->pbuf);
    else if (surface->drawToPixmap && s_glXDestroyPixmap)
        s_glXDestroyPixmap(dpy, (GLXPixmap)surface->nativeSurfaceContainer.drawable);
    // Window surfaces: the X Window is owned by the application, not by us

    return EGL_TRUE;
}

EGLBoolean __copyBuffers(const EGLDisplayImpl* walkerDpy,
                         const EGLSurfaceImpl* surface,
                         EGLNativePixmapType   target)
{
    (void)surface;
    if (!target)
        return EGL_FALSE;

    Display* dpy = reinterpret_cast<Display*>(walkerDpy->display_id);

    // Read current GL viewport dimensions
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int width  = vp[2];
    int height = vp[3];
    if (width <= 0 || height <= 0)
        return EGL_FALSE;

    // Read framebuffer pixels (BGRA, bottom-up)
    GLsizei stride = (width * 4 + 3) & ~3;
    auto*   pixels = static_cast<GLubyte*>(malloc((size_t)stride * (size_t)height));
    if (!pixels)
        return EGL_FALSE;

    // GL_BGRA = 0x80E1
    glReadPixels(0, 0, width, height, 0x80E1, GL_UNSIGNED_BYTE, pixels);

    // Flip rows (GL is bottom-up, X is top-down)
    auto* flipped = static_cast<GLubyte*>(malloc((size_t)stride * (size_t)height));
    if (!flipped)
    {
        free(pixels);
        return EGL_FALSE;
    }
    for (int row = 0; row < height; ++row)
        memcpy(flipped + (size_t)row * (size_t)stride,
               pixels + (size_t)(height - 1 - row) * (size_t)stride,
               (size_t)stride);
    free(pixels);

    // Query the pixmap's depth to create a matching XImage
    Window       root;
    int          px, py;
    unsigned int pw, ph, pbw, pdepth;
    XGetGeometry(dpy, (Drawable)target, &root, &px, &py, &pw, &ph, &pbw, &pdepth);

    XImage* img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
                               pdepth, ZPixmap, 0,
                               reinterpret_cast<char*>(flipped),
                               (unsigned)width, (unsigned)height,
                               32, stride);
    if (!img)
    {
        free(flipped);
        return EGL_FALSE;
    }

    GC gc = XCreateGC(dpy, (Drawable)target, 0, nullptr);
    XPutImage(dpy, (Drawable)target, gc, img, 0, 0, 0, 0, (unsigned)width, (unsigned)height);
    XFreeGC(dpy, gc);

    // XDestroyImage frees img->data (flipped) as well
    img->data = reinterpret_cast<char*>(flipped); // ensure it's set
    XDestroyImage(img);

    return EGL_TRUE;
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

    Display* dpy    = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      screen = DefaultScreen(dpy);

    // Query extension support
    const char* glxExts = s_glXQueryExtensionsString ? s_glXQueryExtensionsString(dpy, screen) : "";

    const bool esSupported = (strstr(glxExts, "GLX_EXT_create_context_es2_profile") != nullptr ||
                              strstr(glxExts, "GLX_EXT_create_context_es_profile") != nullptr)
#ifdef EGL_LINUX_ENABLE_GLES
                             || gles_isAvailable()
#endif
        ;
    const EGLint esMask = esSupported
                              ? (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT)
                              : 0;

    walkerDpy->srgbFramebufferSupported =
        (strstr(glxExts, "GLX_ARB_framebuffer_sRGB") != nullptr ||
         strstr(glxExts, "GLX_EXT_framebuffer_sRGB") != nullptr)
            ? EGL_TRUE
            : EGL_FALSE;

    walkerDpy->supportedHDRColorspaces =
#ifdef LINUX_VK
        __vkQueryHDRColorspaces(walkerDpy->display_id,
                                (EGLNativeWindowType)(uintptr_t)nativeLocalStorageContainer->window);
#else
        0; // X11/GLX has no HDR colorspace support without Vulkan
#endif

    // Enumerate all FBConfigs
    int          nfb = 0;
    GLXFBConfig* fbs = s_glXGetFBConfigs(dpy, screen, &nfb);
    if (!fbs || nfb == 0)
    {
        *error = EGL_NOT_INITIALIZED;
        return EGL_FALSE;
    }

    EGLConfigImpl* lastConfig = nullptr;

    for (int i = 0; i < nfb; ++i)
    {
        // Only expose RGBA configs
        int renderType = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_RENDER_TYPE, &renderType);
        if (!(renderType & GLX_RGBA_BIT))
            continue;

        // Only expose configs that support at least one drawable type
        int drawableType = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_DRAWABLE_TYPE, &drawableType);
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

        // Surface type bitmask
        newConfig->drawToWindow  = (drawableType & GLX_WINDOW_BIT) ? EGL_TRUE : EGL_FALSE;
        newConfig->drawToPixmap  = (drawableType & GLX_PIXMAP_BIT) ? EGL_TRUE : EGL_FALSE;
        newConfig->drawToPBuffer = (drawableType & GLX_PBUFFER_BIT) ? EGL_TRUE : EGL_FALSE;
        newConfig->surfaceType   = 0;
        if (newConfig->drawToWindow)
            newConfig->surfaceType |= EGL_WINDOW_BIT;
        if (newConfig->drawToPixmap)
            newConfig->surfaceType |= EGL_PIXMAP_BIT;
        if (newConfig->drawToPBuffer)
            newConfig->surfaceType |= EGL_PBUFFER_BIT;

        // Renderable type
        newConfig->conformant      = EGL_OPENGL_BIT | esMask;
        newConfig->renderableType  = EGL_OPENGL_BIT | esMask;
        newConfig->colorBufferType = EGL_RGB_BUFFER;

        // FBConfig ID (used to retrieve the GLXFBConfig later)
        int fbid = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_FBCONFIG_ID, &fbid);
        newConfig->configId = fbid;

        // Double-buffer
        int db = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_DOUBLEBUFFER, &db);
        newConfig->doubleBuffer = db ? EGL_TRUE : EGL_FALSE;

        // Color channel sizes
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_RED_SIZE, &newConfig->redSize);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_GREEN_SIZE, &newConfig->greenSize);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_BLUE_SIZE, &newConfig->blueSize);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_ALPHA_SIZE, &newConfig->alphaSize);
        newConfig->bufferSize = newConfig->redSize + newConfig->greenSize + newConfig->blueSize + newConfig->alphaSize;

        // Depth / stencil
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_DEPTH_SIZE, &newConfig->depthSize);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_STENCIL_SIZE, &newConfig->stencilSize);

        // Multisample
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_SAMPLE_BUFFERS, &newConfig->sampleBuffers);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_SAMPLES, &newConfig->samples);

        // Max pbuffer dimensions
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_MAX_PBUFFER_WIDTH, &newConfig->maxPBufferWidth);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_MAX_PBUFFER_HEIGHT, &newConfig->maxPBufferHeight);
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_MAX_PBUFFER_PIXELS, &newConfig->maxPBufferPixels);

        // Transparency
        int transType = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_TRANSPARENT_TYPE, &transType);
        if (transType == GLX_TRANSPARENT_RGB)
        {
            newConfig->transparentType = EGL_TRANSPARENT_RGB;
            s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_TRANSPARENT_RED_VALUE, &newConfig->transparentRedValue);
            s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_TRANSPARENT_GREEN_VALUE, &newConfig->transparentGreenValue);
            s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_TRANSPARENT_BLUE_VALUE, &newConfig->transparentBlueValue);
        }
        else
        {
            newConfig->transparentType = EGL_NONE;
        }

        // Config caveat
        int caveat = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_CONFIG_CAVEAT, &caveat);
        if (caveat == GLX_SLOW_CONFIG)
            newConfig->configCaveat = EGL_SLOW_CONFIG;
        else if (caveat == GLX_NON_CONFORMANT_CONFIG)
            newConfig->configCaveat = EGL_NON_CONFORMANT_CONFIG;
        else
            newConfig->configCaveat = EGL_NONE;

        // Visual ID → nativeVisualId
        int vid = 0;
        s_glXGetFBConfigAttrib(dpy, fbs[i], GLX_VISUAL_ID, &vid);
        newConfig->nativeVisualId   = vid;
        newConfig->nativeVisualType = vid ? EGL_TRUE : EGL_NONE;
        newConfig->nativeRenderable = EGL_TRUE;

        // Swap interval range (GLX has no direct query; use sensible desktop defaults)
        newConfig->minSwapInterval = 0;
        newConfig->maxSwapInterval = 1;

        newConfig->level             = 0;
        newConfig->matchNativePixmap = EGL_NONE;

        // Bind-to-texture: GLX_EXT_texture_from_pixmap
        // Conservatively leave as EGL_DONT_CARE (set by _eglInternalSetDefaultConfig)
    }

    XFree(fbs);
    return EGL_TRUE;
}

// ── Swap ──────────────────────────────────────────────────────────────────────

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;

#ifdef LINUX_VK
    if (walkerSurface->nativeSurfaceContainer.hdr)
    {
        __vkUpdateHDRMetadata(walkerSurface->nativeSurfaceContainer.hdr, walkerSurface);
        return __vkPresent(walkerSurface->nativeSurfaceContainer.hdr);
    }
#endif

#ifdef EGL_LINUX_ENABLE_GLES
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_GLES)
        return gles_swapBuffers(walkerSurface->nativeSurfaceContainer.glesSurface);
#endif

    Display* dpy = reinterpret_cast<Display*>(walkerDpy->display_id);
    s_glXSwapBuffers(dpy, walkerSurface->nativeSurfaceContainer.drawable);
    return EGL_TRUE;
}

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval)
{
    if (!walkerDpy)
        return EGL_FALSE;

    Display* dpy = reinterpret_cast<Display*>(walkerDpy->display_id);

#ifdef EGL_LINUX_ENABLE_GLES
    if (g_localStorage.api == EGL_OPENGL_ES_API && gles_isAvailable())
        return gles_swapInterval(interval);
#endif

    if (s_glXSwapIntervalEXT && s_glXGetCurrentDrawable)
    {
        GLXDrawable drawable = s_glXGetCurrentDrawable();
        if (drawable)
        {
            s_glXSwapIntervalEXT(dpy, drawable, (int)interval);
            return EGL_TRUE;
        }
    }
    if (s_glXSwapIntervalMESA)
        return s_glXSwapIntervalMESA((int)interval) == 0 ? EGL_TRUE : EGL_FALSE;

    return EGL_FALSE;
}

// ── Texture binding ───────────────────────────────────────────────────────────

EGLBoolean __bindTexImage(const EGLDisplayImpl* walkerDpy,
                          const EGLSurfaceImpl* walkerSurface,
                          EGLint                buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
    if (!s_glXBindTexImageEXT)
        return EGL_FALSE;

    Display* dpy       = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      glxBuffer = (buffer == EGL_BACK_BUFFER) ? GLX_BACK_LEFT_EXT : GLX_FRONT_LEFT_EXT;
    s_glXBindTexImageEXT(dpy, (GLXDrawable)walkerSurface->pbuf, glxBuffer, nullptr);
    return EGL_TRUE;
}

EGLBoolean __releaseTexImage(const EGLDisplayImpl* walkerDpy,
                             const EGLSurfaceImpl* walkerSurface,
                             EGLint                buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
    if (!s_glXReleaseTexImageEXT)
        return EGL_FALSE;

    Display* dpy       = reinterpret_cast<Display*>(walkerDpy->display_id);
    int      glxBuffer = (buffer == EGL_BACK_BUFFER) ? GLX_BACK_LEFT_EXT : GLX_FRONT_LEFT_EXT;
    s_glXReleaseTexImageEXT(dpy, (GLXDrawable)walkerSurface->pbuf, glxBuffer);
    return EGL_TRUE;
}

// ── Platform-dependent handle export ─────────────────────────────────────────

EGLBoolean __getPlatformDependentHandles(void*                         out,
                                         const EGLDisplayImpl*         walkerDpy,
                                         const NativeSurfaceContainer* nativeSurfaceContainer,
                                         const NativeContextContainer* nativeContextContainer)
{
    if (!nativeSurfaceContainer || !nativeContextContainer)
        return EGL_FALSE;

    EGLContextInternals* h = reinterpret_cast<EGLContextInternals*>(out);
    h->display             = reinterpret_cast<Display*>(walkerDpy->display_id);
    h->surface.drawable    = nativeSurfaceContainer->drawable;
    h->surface.config      = nativeSurfaceContainer->config;
    h->context             = nativeContextContainer->ctx;
    return EGL_TRUE;
}

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

#include "egl_windows_vk.h"
#include "egl_common.h"
#include "../../EGL/include/EGL/eglctxinternals.h"
#ifdef EGL_WIN_ENABLE_ANGLE
#include "egl_windows_angle.h"
#endif

HMODULE opengl32dll = NULL;

typedef HGLRC(__stdcall* __PFN_wglCreateContext)(HDC);
typedef BOOL(__stdcall* __PFN_wglDeleteContext)(HGLRC);
typedef BOOL(__stdcall* __PFN_wglMakeCurrent)(HDC, HGLRC);
typedef PROC(__stdcall* __PFN_wglGetProcAddress)(LPCSTR);

__PFN_glFinish         glFinish_PTR         = NULL;
__PFN_glFenceSync      glFenceSync_PTR      = NULL;
__PFN_glDeleteSync     glDeleteSync_PTR     = NULL;
__PFN_glClientWaitSync glClientWaitSync_PTR = NULL;
__PFN_glWaitSync       glWaitSync_PTR       = NULL;
__PFN_glGetSynciv      glGetSynciv_PTR      = NULL;

__PFN_wglCreateContext  wglCreateContext_PTR  = NULL;
__PFN_wglDeleteContext  wglDeleteContext_PTR  = NULL;
__PFN_wglMakeCurrent    wglMakeCurrent_PTR    = NULL;
__PFN_wglGetProcAddress wglGetProcAddress_PTR = NULL;

PFNWGLCHOOSEPIXELFORMATARBPROC      wglChoosePixelFormatARB      = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;
PFNWGLCREATECONTEXTATTRIBSARBPROC   wglCreateContextAttribsARB   = NULL;
PFNWGLGETEXTENSIONSSTRINGARBPROC    wglGetExtensionsStringARB    = NULL;
PFNWGLSWAPINTERVALEXTPROC           wglSwapIntervalEXT           = NULL;

PFNWGLCREATEPBUFFERARBPROC    wglCreatePbufferARB       = NULL;
PFNWGLGETPBUFFERDCARBPROC     wglGetPbufferDCARB        = NULL;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB    = NULL;
PFNWGLDESTROYPBUFFERARBPROC   wglDestroyPbufferARB      = NULL;
PFNWGLBINDTEXIMAGEARBPROC     wglBindTexImageARB_PTR    = NULL;
PFNWGLRELEASETEXIMAGEARBPROC  wglReleaseTexImageARB_PTR = NULL;

EGLBoolean __internalInit(NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* GL_max_supported, EGLint* ES_max_supported)
{
    if (!nativeLocalStorageContainer)
    {
        return EGL_FALSE;
    }

    if (nativeLocalStorageContainer->hdc && nativeLocalStorageContainer->ctx)
    {
        return EGL_TRUE;
    }

    if (nativeLocalStorageContainer->hdc)
    {
        return EGL_FALSE;
    }

    if (nativeLocalStorageContainer->ctx)
    {
        return EGL_FALSE;
    }

    //

    opengl32dll = LoadLibrary("opengl32.dll");

    if (!opengl32dll)
    {
        return EGL_FALSE;
    }

    wglCreateContext_PTR  = reinterpret_cast<__PFN_wglCreateContext>(GetProcAddress(opengl32dll, "wglCreateContext"));
    wglDeleteContext_PTR  = reinterpret_cast<__PFN_wglDeleteContext>(GetProcAddress(opengl32dll, "wglDeleteContext"));
    wglMakeCurrent_PTR    = reinterpret_cast<__PFN_wglMakeCurrent>(GetProcAddress(opengl32dll, "wglMakeCurrent"));
    wglGetProcAddress_PTR = reinterpret_cast<__PFN_wglGetProcAddress>(GetProcAddress(opengl32dll, "wglGetProcAddress"));

    // Without these four there is no way to bring up any context at all — bail out
    // cleanly instead of calling through a NULL pointer further down.
    if (!wglCreateContext_PTR || !wglDeleteContext_PTR || !wglMakeCurrent_PTR || !wglGetProcAddress_PTR)
    {
        FreeLibrary(opengl32dll);
        opengl32dll = NULL;

        wglCreateContext_PTR  = NULL;
        wglDeleteContext_PTR  = NULL;
        wglMakeCurrent_PTR    = NULL;
        wglGetProcAddress_PTR = NULL;

        return EGL_FALSE;
    }

    //

    nativeLocalStorageContainer->hwnd = CreateWindowA("STATIC", "dummy", 0, 0, 0, 1, 1, NULL, NULL, NULL, NULL);

    //

    if (!nativeLocalStorageContainer->hwnd)
    {
        return EGL_FALSE;
    }

    nativeLocalStorageContainer->hdc = GetDC(nativeLocalStorageContainer->hwnd);

    if (!nativeLocalStorageContainer->hdc)
    {
        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    PIXELFORMATDESCRIPTOR dummyPfd = {};
    dummyPfd.nSize                 = sizeof(dummyPfd);
    dummyPfd.nVersion              = 1;
    dummyPfd.dwFlags               = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    dummyPfd.iPixelType            = PFD_TYPE_RGBA;
    dummyPfd.cColorBits            = 32;
    dummyPfd.cAlphaBits            = 8;
    dummyPfd.cDepthBits            = 24;

    EGLint dummyPixelFormat = ChoosePixelFormat(nativeLocalStorageContainer->hdc, &dummyPfd);

    if (dummyPixelFormat == 0)
    {
        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;

        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    if (!SetPixelFormat(nativeLocalStorageContainer->hdc, dummyPixelFormat, &dummyPfd))
    {
        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;

        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    nativeLocalStorageContainer->ctx = wglCreateContext_PTR(nativeLocalStorageContainer->hdc);

    if (!nativeLocalStorageContainer->ctx)
    {
        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;

        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    if (!wglMakeCurrent_PTR(nativeLocalStorageContainer->hdc, nativeLocalStorageContainer->ctx))
    {
        wglDeleteContext_PTR(nativeLocalStorageContainer->ctx);
        nativeLocalStorageContainer->ctx = 0;

        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;

        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    wglChoosePixelFormatARB =
        (PFNWGLCHOOSEPIXELFORMATARBPROC)
            __getProcAddress("wglChoosePixelFormatARB");
    wglGetPixelFormatAttribivARB =
        (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
            __getProcAddress("wglGetPixelFormatAttribivARB");
    wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)
            __getProcAddress("wglCreateContextAttribsARB");
    wglSwapIntervalEXT =
        reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(__getProcAddress("wglSwapIntervalEXT"));
    wglGetExtensionsStringARB =
        (PFNWGLGETEXTENSIONSSTRINGARBPROC)
            __getProcAddress("wglGetExtensionsStringARB");
    glFinish_PTR         = reinterpret_cast<__PFN_glFinish>(__getProcAddress("glFinish"));
    glFenceSync_PTR      = reinterpret_cast<__PFN_glFenceSync>(__getProcAddress("glFenceSync"));
    glDeleteSync_PTR     = reinterpret_cast<__PFN_glDeleteSync>(__getProcAddress("glDeleteSync"));
    glClientWaitSync_PTR = reinterpret_cast<__PFN_glClientWaitSync>(__getProcAddress("glClientWaitSync"));
    glWaitSync_PTR       = reinterpret_cast<__PFN_glWaitSync>(__getProcAddress("glWaitSync"));
    glGetSynciv_PTR      = reinterpret_cast<__PFN_glGetSynciv>(__getProcAddress("glGetSynciv"));

    wglCreatePbufferARB       = reinterpret_cast<PFNWGLCREATEPBUFFERARBPROC>(__getProcAddress("wglCreatePbufferARB"));
    wglGetPbufferDCARB        = reinterpret_cast<PFNWGLGETPBUFFERDCARBPROC>(__getProcAddress("wglGetPbufferDCARB"));
    wglReleasePbufferDCARB    = reinterpret_cast<PFNWGLRELEASEPBUFFERDCARBPROC>(__getProcAddress("wglReleasePbufferDCARB"));
    wglDestroyPbufferARB      = reinterpret_cast<PFNWGLDESTROYPBUFFERARBPROC>(__getProcAddress("wglDestroyPbufferARB"));
    wglBindTexImageARB_PTR    = reinterpret_cast<PFNWGLBINDTEXIMAGEARBPROC>(__getProcAddress("wglBindTexImageARB"));
    wglReleaseTexImageARB_PTR = reinterpret_cast<PFNWGLRELEASETEXIMAGEARBPROC>(__getProcAddress("wglReleaseTexImageARB"));

    wglMakeCurrent_PTR(NULL, NULL);

    // WGL_ARB_create_context is required to probe (and later create) any versioned
    // context. __getProcAddress can legitimately return NULL on a software, RDP or
    // stripped driver, so fail cleanly here instead of calling through it below.
    if (!wglCreateContextAttribsARB)
    {
        wglDeleteContext_PTR(nativeLocalStorageContainer->ctx);
        nativeLocalStorageContainer->ctx = 0;

        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;

        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;

        return EGL_FALSE;
    }

    EGLint attrib_list[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 1,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0};

    HGLRC testctx = NULL;
    // Probe the highest supported desktop GL version. The minor counter is reset
    // for every major so that drivers exposing only e.g. GL 3.3 (not 4.x) are
    // detected; the matching version is captured on the first successful create.
    EGLint GL_major = 0, GL_minor = 0;
    for (EGLint major = 4; major >= 1 && !testctx; --major)
    {
        for (EGLint minor = 9; minor >= 0 && !testctx; --minor)
        {
            attrib_list[1] = major;
            attrib_list[3] = minor;
            testctx        = wglCreateContextAttribsARB(nativeLocalStorageContainer->hdc, NULL, attrib_list);
            if (testctx)
            {
                GL_major = major;
                GL_minor = minor;
            }
        }
    }

    if (testctx)
    {
        wglDeleteContext_PTR(testctx);
        testctx = NULL;
    }
    GL_max_supported[0] = GL_major;
    GL_max_supported[1] = GL_minor;

    attrib_list[5]  = WGL_CONTEXT_ES_PROFILE_BIT_EXT;
    EGLint ES_major = 0, ES_minor = 0;
    for (EGLint major = 3; major >= 1 && !testctx; --major)
    {
        for (EGLint minor = 9; minor >= 0 && !testctx; --minor)
        {
            attrib_list[1] = major;
            attrib_list[3] = minor;
            testctx        = wglCreateContextAttribsARB(nativeLocalStorageContainer->hdc, NULL, attrib_list);
            if (testctx)
            {
                ES_major = major;
                ES_minor = minor;
            }
        }
    }

    if (testctx)
    {
        wglDeleteContext_PTR(testctx);
        testctx = NULL;
    }
    ES_max_supported[0] = ES_major;
    ES_max_supported[1] = ES_minor;

#ifdef EGL_WIN_ENABLE_ANGLE
    // Try to initialize ANGLE for OpenGL ES. Non-fatal if unavailable.
    {
        EGLint angleES[2] = {0, 0};
        if (angle_init(angleES) == EGL_TRUE)
        {
            // Prefer ANGLE's ES version if higher than what WGL exposes.
            if (angleES[0] > ES_max_supported[0] ||
                (angleES[0] == ES_max_supported[0] && angleES[1] > ES_max_supported[1]))
            {
                ES_max_supported[0] = angleES[0];
                ES_max_supported[1] = angleES[1];
            }
        }
    }
#endif

    // Initialize Vulkan HDR backend (non-fatal if unavailable)
    __vkInit();

    return EGL_TRUE;
}

EGLBoolean __internalTerminate(NativeLocalStorageContainer* nativeLocalStorageContainer)
{
    if (!nativeLocalStorageContainer)
    {
        return EGL_FALSE;
    }

    if (wglMakeCurrent_PTR)
    {
        wglMakeCurrent_PTR(0, 0);
    }

    if (nativeLocalStorageContainer->ctx)
    {
        if (wglDeleteContext_PTR)
        {
            wglDeleteContext_PTR(nativeLocalStorageContainer->ctx);
        }
        nativeLocalStorageContainer->ctx = 0;
    }

    if (nativeLocalStorageContainer->hdc)
    {
        ReleaseDC(nativeLocalStorageContainer->hwnd, nativeLocalStorageContainer->hdc);
        nativeLocalStorageContainer->hdc = 0;
    }

    if (nativeLocalStorageContainer->hwnd)
    {
        DestroyWindow(nativeLocalStorageContainer->hwnd);
        nativeLocalStorageContainer->hwnd = 0;
    }

    __vkTerm();

#ifdef EGL_WIN_ENABLE_ANGLE
    angle_terminate();
#endif

    if (opengl32dll)
    {
        FreeLibrary(opengl32dll);
        opengl32dll = NULL;
    }

    // Every entry point resolved out of opengl32.dll is stale now.
    wglCreateContext_PTR  = NULL;
    wglDeleteContext_PTR  = NULL;
    wglMakeCurrent_PTR    = NULL;
    wglGetProcAddress_PTR = NULL;

    wglChoosePixelFormatARB      = NULL;
    wglGetPixelFormatAttribivARB = NULL;
    wglCreateContextAttribsARB   = NULL;
    wglGetExtensionsStringARB    = NULL;
    wglSwapIntervalEXT           = NULL;

    wglCreatePbufferARB       = NULL;
    wglGetPbufferDCARB        = NULL;
    wglReleasePbufferDCARB    = NULL;
    wglDestroyPbufferARB      = NULL;
    wglBindTexImageARB_PTR    = NULL;
    wglReleaseTexImageARB_PTR = NULL;

    glFinish_PTR         = NULL;
    glFenceSync_PTR      = NULL;
    glDeleteSync_PTR     = NULL;
    glClientWaitSync_PTR = NULL;
    glWaitSync_PTR       = NULL;
    glGetSynciv_PTR      = NULL;

    return EGL_TRUE;
}

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
    if (!walkerDpy || !nativeContextContainer)
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (nativeContextContainer->backend == EGL_BACKEND_ANGLE)
    {
        return angle_destroyContext(nativeContextContainer->angleCtx);
    }
#endif

    return wglDeleteContext_PTR(nativeContextContainer->ctx);
}

EGLBoolean __processAttribList(EGLenum api, EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error)
{
    if (!target_attrib_list || !attrib_list || !error)
    {
        return EGL_FALSE;
    }

    const EGLint defaultProfileMask     = ((api == EGL_OPENGL_ES_API) ? WGL_CONTEXT_ES_PROFILE_BIT_EXT : WGL_CONTEXT_CORE_PROFILE_BIT_ARB);
    EGLint       template_attrib_list[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 1,
        WGL_CONTEXT_MINOR_VERSION_ARB, 0,
        WGL_CONTEXT_LAYER_PLANE_ARB, 0,
        WGL_CONTEXT_FLAGS_ARB, 0,
        WGL_CONTEXT_PROFILE_MASK_ARB, defaultProfileMask,
        WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, WGL_NO_RESET_NOTIFICATION_ARB,
        0};

    EGLint attribListIndex = 0;

    while (attrib_list[attribListIndex] != EGL_NONE)
    {
        EGLint value = attrib_list[attribListIndex + 1];

        switch (attrib_list[attribListIndex])
        {
        case EGL_CONTEXT_MAJOR_VERSION:
        {
            if (value < 1)
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }

            template_attrib_list[1] = value;
        }
        break;
        case EGL_CONTEXT_MINOR_VERSION:
        {
            if (value < 0)
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }

            template_attrib_list[3] = value;
        }
        break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK:
        {
            if (value == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)
            {
                template_attrib_list[9] = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
            }
            else if (value == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
            {
                template_attrib_list[9] = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        break;
        case EGL_CONTEXT_OPENGL_DEBUG:
        {
            if (value == EGL_TRUE)
            {
                template_attrib_list[7] |= WGL_CONTEXT_DEBUG_BIT_ARB;
            }
            else if (value == EGL_FALSE)
            {
                template_attrib_list[7] &= ~WGL_CONTEXT_DEBUG_BIT_ARB;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        break;
        case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
        {
            if (value == EGL_TRUE)
            {
                template_attrib_list[7] |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
            }
            else if (value == EGL_FALSE)
            {
                template_attrib_list[7] &= ~WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        break;
        case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
        {
            if (value == EGL_TRUE)
            {
                template_attrib_list[7] |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
            }
            else if (value == EGL_FALSE)
            {
                template_attrib_list[7] &= ~WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        break;
        case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
        {
            if (value == EGL_NO_RESET_NOTIFICATION)
            {
                template_attrib_list[11] = WGL_NO_RESET_NOTIFICATION_ARB;
            }
            else if (value == EGL_LOSE_CONTEXT_ON_RESET)
            {
                template_attrib_list[11] = WGL_LOSE_CONTEXT_ON_RESET_ARB;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
        break;
        default:
        {
            *error = EGL_BAD_ATTRIBUTE;

            return EGL_FALSE;
        }
        }

        attribListIndex += 2;

        // More than 14 entries can not exist.
        if (attribListIndex >= 7 * 2)
        {
            *error = EGL_BAD_ATTRIBUTE;

            return EGL_FALSE;
        }
    }

    memcpy(target_attrib_list, template_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

    return EGL_TRUE;
}

EGLBoolean __createPbufferSurface(EGLSurfaceImpl* newSurface, const EGLint* attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
    {
        return EGL_FALSE;
    }

    int iattribs[] = {
        WGL_DRAW_TO_PBUFFER_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_COLOR_BITS_ARB, 32,
        WGL_RED_BITS_EXT, 8,
        WGL_GREEN_BITS_EXT, 8,
        WGL_BLUE_BITS_EXT, 8,
        WGL_ALPHA_BITS_EXT, 8,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, 0,
        WGL_SAMPLES_ARB, 0,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_FALSE, // default: linear per spec
        // WGL_STEREO_ARB, 0 ? GL_TRUE:GL_FALSE,
        0};

    EGLint pbuf_attribs[] = {
        WGL_PBUFFER_LARGEST_EXT, GL_FALSE,
        0};

    int        width              = 0;
    int        height             = 0;
    EGLBoolean mipmapTexture      = EGL_FALSE;
    EGLint     textureFormat      = EGL_NO_TEXTURE;
    EGLint     textureTarget      = EGL_NO_TEXTURE;
    EGLint     pbufColorspace     = EGL_GL_COLORSPACE_LINEAR;
    EGLint     currentAttribIndex = 0;
    while (attrib_list[currentAttribIndex] != EGL_NONE)
    {
        EGLint attrib = attrib_list[currentAttribIndex];
        EGLint value  = attrib_list[currentAttribIndex + 1];
        switch (attrib)
        {
        case EGL_HEIGHT:
            height = value;
            break;
        case EGL_WIDTH:
            width = value;
            break;
        case EGL_LARGEST_PBUFFER:
            pbuf_attribs[1] = value;
            break;
        case EGL_GL_COLORSPACE:
            if (value == EGL_GL_COLORSPACE_LINEAR)
            {
                iattribs[29]   = GL_FALSE;
                pbufColorspace = EGL_GL_COLORSPACE_LINEAR;
            }
            else if (value == EGL_GL_COLORSPACE_SRGB)
            {
                // sRGB is a per config capability. Requesting it on a config that cannot
                // present it is an EGL_BAD_MATCH rather than a silent downgrade to linear.
                if (!walkerConfig->srgbCapable)
                {
                    *error = EGL_BAD_MATCH;

                    return EGL_FALSE;
                }

                iattribs[29]   = GL_TRUE;
                pbufColorspace = EGL_GL_COLORSPACE_SRGB;
            }
            else if (value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
                     value == EGL_GL_COLORSPACE_SCRGB_EXT ||
                     value == EGL_GL_COLORSPACE_BT2020_PQ_EXT ||
                     value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
                     value == EGL_GL_COLORSPACE_BT2020_HLG_EXT ||
                     value == EGL_GL_COLORSPACE_DISPLAY_P3_EXT ||
                     value == EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT ||
                     value == EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT)
            {
                // HDR colorspaces stored but no Vulkan surface for offscreen buffers
                iattribs[29]   = GL_FALSE;
                pbufColorspace = value;
            }
            else
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            break;
        case EGL_MIPMAP_TEXTURE:
            mipmapTexture = (EGLBoolean)value;
            break;
        case EGL_TEXTURE_FORMAT:
            if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_RGB && value != EGL_TEXTURE_RGBA)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            textureFormat = value;
            break;
        case EGL_TEXTURE_TARGET:
            if (value != EGL_NO_TEXTURE && value != EGL_TEXTURE_2D)
            {
                *error = EGL_BAD_ATTRIBUTE;
                return EGL_FALSE;
            }
            textureTarget = value;
            break;
        case EGL_VG_ALPHA_FORMAT:
        case EGL_VG_COLORSPACE:
            *error = EGL_BAD_MATCH;
            return EGL_FALSE;
        }

        currentAttribIndex += 2;
    }

    iattribs[9]  = walkerConfig->bufferSize;
    iattribs[11] = walkerConfig->redSize;
    iattribs[13] = walkerConfig->greenSize;
    iattribs[15] = walkerConfig->blueSize;
    iattribs[17] = walkerConfig->alphaSize;
    iattribs[19] = walkerConfig->depthSize;
    iattribs[21] = walkerConfig->stencilSize;
    iattribs[23] = walkerConfig->sampleBuffers;
    iattribs[25] = walkerConfig->samples;

    HDC hdc = walkerDpy->display_id;

    // WGL_ARB_pbuffer / WGL_ARB_pixel_format are optional; a software, RDP or
    // stripped driver may not expose them at all.
    if (!wglChoosePixelFormatARB || !wglCreatePbufferARB || !wglGetPbufferDCARB)
    {
        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    int  pformat     = 0;
    UINT max_formats = 1;
    if (!wglChoosePixelFormatARB(hdc, iattribs, NULL, max_formats, &pformat, &max_formats))
    {
        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    if (max_formats == 0)
    {
        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    // not sure im getting 1st arg ok (HDC)
    HPBUFFERARB pbuf = wglCreatePbufferARB(hdc, pformat, width, height, pbuf_attribs);
    if (!pbuf)
    {
        *error = EGL_BAD_ALLOC;

        return EGL_FALSE;
    }

    hdc = wglGetPbufferDCARB(pbuf);

    if (!hdc)
    {
        if (wglDestroyPbufferARB)
        {
            wglDestroyPbufferARB(pbuf);
        }

        *error = EGL_BAD_NATIVE_WINDOW;

        return EGL_FALSE;
    }

    newSurface->drawToWindow               = EGL_FALSE;
    newSurface->drawToPixmap               = EGL_FALSE;
    newSurface->drawToPBuffer              = EGL_TRUE;
    newSurface->doubleBuffer               = (EGLBoolean)iattribs[7];
    newSurface->configId                   = pformat;
    newSurface->width                      = width;
    newSurface->height                     = height;
    newSurface->swapBehavior               = EGL_BUFFER_DESTROYED;
    newSurface->multisampleResolve         = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
    newSurface->mipmapLevel                = 0;
    newSurface->mipmapTexture              = mipmapTexture;
    newSurface->largestPbuffer             = (EGLBoolean)pbuf_attribs[1];
    newSurface->textureFormat              = textureFormat;
    newSurface->textureTarget              = textureTarget;
    newSurface->glColorspace               = pbufColorspace;
    newSurface->initialized                = EGL_TRUE;
    newSurface->destroy                    = EGL_FALSE;
    newSurface->pbuf                       = pbuf;
    newSurface->nativeSurfaceContainer.hdc = hdc;
    newSurface->nativeSurfaceContainer.hdr = nullptr;

    return EGL_TRUE;
}

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint* attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (g_localStorage.api == EGL_OPENGL_ES_API && angle_isAvailable())
    {
        // ANGLE owns the HWND's pixel format. Do NOT call SetPixelFormat here.
        void* angleSurf = nullptr;
        if (angle_createWindowSurface(win, &angleSurf) != EGL_TRUE)
        {
            *error = EGL_BAD_NATIVE_WINDOW;
            return EGL_FALSE;
        }

        RECT rect = {0};
        GetClientRect(win, &rect);

        newSurface->drawToWindow                        = EGL_TRUE;
        newSurface->drawToPixmap                        = EGL_FALSE;
        newSurface->drawToPBuffer                       = EGL_FALSE;
        newSurface->doubleBuffer                        = EGL_TRUE;
        newSurface->configId                            = 0;
        newSurface->width                               = rect.right - rect.left;
        newSurface->height                              = rect.bottom - rect.top;
        newSurface->swapBehavior                        = EGL_BUFFER_DESTROYED;
        newSurface->multisampleResolve                  = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
        newSurface->mipmapLevel                         = 0;
        newSurface->mipmapTexture                       = EGL_FALSE;
        newSurface->largestPbuffer                      = EGL_FALSE;
        newSurface->textureFormat                       = EGL_NO_TEXTURE;
        newSurface->textureTarget                       = EGL_NO_TEXTURE;
        newSurface->glColorspace                        = EGL_GL_COLORSPACE_LINEAR;
        newSurface->initialized                         = EGL_TRUE;
        newSurface->destroy                             = EGL_FALSE;
        newSurface->win                                 = win;
        newSurface->nativeSurfaceContainer.hdc          = nullptr;
        newSurface->nativeSurfaceContainer.hdr          = nullptr;
        newSurface->nativeSurfaceContainer.backend      = EGL_BACKEND_ANGLE;
        newSurface->nativeSurfaceContainer.angleSurface = angleSurf;
        return EGL_TRUE;
    }
#endif

    HDC hdc = GetDC(win);

    if (!hdc)
    {
        *error = EGL_BAD_NATIVE_WINDOW;

        return EGL_FALSE;
    }

    // FIXME Check more values.
    EGLint template_attrib_list[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_DOUBLE_BUFFER_ARB, walkerConfig->doubleBuffer ? GL_TRUE : GL_FALSE,
        WGL_COLOR_BITS_ARB, 32,
        WGL_RED_BITS_EXT, 8,
        WGL_GREEN_BITS_EXT, 8,
        WGL_BLUE_BITS_EXT, 8,
        WGL_ALPHA_BITS_EXT, 8,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, 0,
        WGL_SAMPLES_ARB, 0,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_FALSE, // default: linear per spec
        // WGL_STEREO_ARB, 0 ? GL_TRUE:GL_FALSE,
        0};

    EGLint parsedColorspace = EGL_GL_COLORSPACE_LINEAR;

    if (attrib_list)
    {
        EGLint indexAttribList = 0;

        while (attrib_list[indexAttribList] != EGL_NONE)
        {
            EGLint value = attrib_list[indexAttribList + 1];

            switch (attrib_list[indexAttribList])
            {
            case EGL_GL_COLORSPACE:
            {
                if (value == EGL_GL_COLORSPACE_LINEAR)
                {
                    template_attrib_list[29] = GL_FALSE;
                    parsedColorspace         = EGL_GL_COLORSPACE_LINEAR;
                }
                else if (value == EGL_GL_COLORSPACE_SRGB)
                {
                    // sRGB is a per config capability. Requesting it on a config that cannot
                    // present it is an EGL_BAD_MATCH rather than a silent downgrade to linear.
                    if (!walkerConfig->srgbCapable)
                    {
                        ReleaseDC(win, hdc);

                        *error = EGL_BAD_MATCH;

                        return EGL_FALSE;
                    }

                    template_attrib_list[29] = GL_TRUE;
                    parsedColorspace         = EGL_GL_COLORSPACE_SRGB;
                }
                else if (value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
                         value == EGL_GL_COLORSPACE_SCRGB_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_PQ_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
                         value == EGL_GL_COLORSPACE_BT2020_HLG_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT ||
                         value == EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT)
                {
                    // HDR colorspaces use Vulkan for presentation; WGL framebuffer stays linear
                    template_attrib_list[29] = GL_FALSE;
                    parsedColorspace         = value;
                }
                else
                {
                    ReleaseDC(win, hdc);
                    *error = EGL_BAD_ATTRIBUTE;
                    return EGL_FALSE;
                }
            }
            break;
            case EGL_RENDER_BUFFER:
            {
                if (value == EGL_SINGLE_BUFFER)
                {
                    template_attrib_list[7] = GL_FALSE;
                }
                else if (value == EGL_BACK_BUFFER)
                {
                    template_attrib_list[7] = GL_TRUE;
                }
                else
                {
                    ReleaseDC(win, hdc);

                    *error = EGL_BAD_ATTRIBUTE;

                    return EGL_FALSE;
                }
            }
            break;
            case EGL_VG_ALPHA_FORMAT:
            {
                ReleaseDC(win, hdc);

                *error = EGL_BAD_MATCH;

                return EGL_FALSE;
            }
            case EGL_VG_COLORSPACE:
            {
                ReleaseDC(win, hdc);

                *error = EGL_BAD_MATCH;

                return EGL_FALSE;
            }
            }

            indexAttribList += 2;

            // More than 8 entries can not exist.
            if (indexAttribList >= 8 * 2)
            {
                ReleaseDC(win, hdc);

                *error = EGL_BAD_ATTRIBUTE;

                return EGL_FALSE;
            }
        }
    }

    // Create out of EGL configuration an array of WGL configuration and use it.
    // see https://www.opengl.org/registry/specs/ARB/wgl_pixel_format.txt

    template_attrib_list[9]  = walkerConfig->bufferSize;
    template_attrib_list[11] = walkerConfig->redSize;
    template_attrib_list[13] = walkerConfig->greenSize;
    template_attrib_list[15] = walkerConfig->blueSize;
    template_attrib_list[17] = walkerConfig->alphaSize;
    template_attrib_list[19] = walkerConfig->depthSize;
    template_attrib_list[21] = walkerConfig->stencilSize;
    template_attrib_list[23] = walkerConfig->sampleBuffers;
    template_attrib_list[25] = walkerConfig->samples;
    //

    UINT wgl_max_formats = 1;
    INT  wgl_formats     = 0;
    UINT wgl_num_formats = 0;

    // WGL_ARB_pixel_format is optional; a software, RDP or stripped driver may not
    // expose it at all.
    if (!wglChoosePixelFormatARB)
    {
        ReleaseDC(win, hdc);

        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    if (!wglChoosePixelFormatARB(hdc, template_attrib_list, 0, wgl_max_formats, &wgl_formats, &wgl_num_formats))
    {
        ReleaseDC(win, hdc);

        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    if (wgl_num_formats == 0)
    {
        ReleaseDC(win, hdc);

        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    PIXELFORMATDESCRIPTOR pfd;

    if (!DescribePixelFormat(hdc, wgl_formats, sizeof(PIXELFORMATDESCRIPTOR), &pfd))
    {
        ReleaseDC(win, hdc);

        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    // A window's pixel format can only ever be set ONCE. Re-creating a surface on the
    // same HWND, having two surfaces on one HWND, or being handed a window another
    // toolkit already owns must therefore not blindly call SetPixelFormat: query what
    // the window already has and only set it when it has none.
    int currentPixelFormat = GetPixelFormat(hdc);

    if (currentPixelFormat == 0)
    {
        if (!SetPixelFormat(hdc, wgl_formats, &pfd))
        {
            ReleaseDC(win, hdc);

            *error = EGL_BAD_MATCH;

            return EGL_FALSE;
        }
    }
    else if (currentPixelFormat != wgl_formats)
    {
        ReleaseDC(win, hdc);

        *error = EGL_BAD_MATCH;

        return EGL_FALSE;
    }

    newSurface->drawToWindow  = EGL_TRUE;
    newSurface->drawToPixmap  = EGL_FALSE;
    newSurface->drawToPBuffer = EGL_FALSE;
    newSurface->doubleBuffer  = (EGLBoolean)template_attrib_list[7];
    newSurface->configId      = wgl_formats;

    RECT rect = {0};
    GetClientRect(win, &rect);
    newSurface->width              = rect.right - rect.left;
    newSurface->height             = rect.bottom - rect.top;
    newSurface->swapBehavior       = EGL_BUFFER_DESTROYED;
    newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
    newSurface->mipmapLevel        = 0;
    newSurface->mipmapTexture      = EGL_FALSE;
    newSurface->largestPbuffer     = EGL_FALSE;
    newSurface->textureFormat      = EGL_NO_TEXTURE;
    newSurface->textureTarget      = EGL_NO_TEXTURE;
    newSurface->glColorspace       = parsedColorspace;

    newSurface->initialized                         = EGL_TRUE;
    newSurface->destroy                             = EGL_FALSE;
    newSurface->win                                 = win;
    newSurface->nativeSurfaceContainer.hdc          = hdc;
    newSurface->nativeSurfaceContainer.hdr          = nullptr;
    newSurface->nativeSurfaceContainer.backend      = EGL_BACKEND_WGL;
    newSurface->nativeSurfaceContainer.angleSurface = nullptr;

    // For HDR colorspaces, create a Vulkan HDR surface
    {
        VkFormat        vkFmt;
        VkColorSpaceKHR vkCS;
        if (_eglHDRColorspaceToVk(parsedColorspace, &vkFmt, &vkCS) && __vkIsReady())
        {
            NativeHDRSurfaceContainer* hdrContainer = reinterpret_cast<NativeHDRSurfaceContainer*>(malloc(sizeof(NativeHDRSurfaceContainer)));
            if (!hdrContainer)
            {
                // The application explicitly asked for an HDR colorspace — silently
                // handing back an SDR surface would be worse than failing.
                ReleaseDC(win, hdc);
                *error = EGL_BAD_ALLOC;
                return EGL_FALSE;
            }

            if (__vkCreateHDRSurface(hdrContainer, win, parsedColorspace,
                                     (uint32_t)(rect.right - rect.left),
                                     (uint32_t)(rect.bottom - rect.top)) == EGL_TRUE)
            {
                newSurface->nativeSurfaceContainer.hdr = hdrContainer;
            }
            else
            {
                free(hdrContainer);
                ReleaseDC(win, hdc);
                *error = EGL_BAD_MATCH;
                return EGL_FALSE;
            }
        }
    }

    return EGL_TRUE;
}

EGLBoolean __destroySurface(EGLNativeDisplayType dpy, const EGLSurfaceImpl* surface)
{
    if (!surface)
    {
        return EGL_FALSE;
    }
    const NativeSurfaceContainer* nativeSurfaceContainer = &surface->nativeSurfaceContainer;

#ifdef EGL_WIN_ENABLE_ANGLE
    if (nativeSurfaceContainer->backend == EGL_BACKEND_ANGLE)
    {
        if (nativeSurfaceContainer->angleSurface)
            angle_destroySurface(nativeSurfaceContainer->angleSurface);
        return EGL_TRUE;
    }
#endif

    if (surface->nativeSurfaceContainer.hdr)
    {
        __vkDestroyHDRSurface(surface->nativeSurfaceContainer.hdr);
        free(surface->nativeSurfaceContainer.hdr);
    }

    if (surface->drawToWindow)
        ReleaseDC(surface->win, nativeSurfaceContainer->hdc);
    else if (surface->drawToPBuffer)
    {
        if (wglReleasePbufferDCARB)
        {
            wglReleasePbufferDCARB(surface->pbuf, surface->nativeSurfaceContainer.hdc);
        }
        if (wglDestroyPbufferARB)
        {
            wglDestroyPbufferARB(surface->pbuf);
        }
    }
    else if (surface->drawToPixmap)
    {
        // A memory DC must not be deleted while the application's bitmap is still
        // selected into it. Select a private scratch bitmap back in first so the
        // pixmap is released before the DC goes away.
        HBITMAP scratch = CreateBitmap(1, 1, 1, 1, NULL);

        if (scratch)
        {
            SelectObject(nativeSurfaceContainer->hdc, scratch);
        }

        DeleteDC(nativeSurfaceContainer->hdc);

        if (scratch)
        {
            DeleteObject(scratch);
        }
    }

    return EGL_TRUE;
}

EGLBoolean __createPixmapSurface(EGLSurfaceImpl* newSurface, EGLNativePixmapType pixmap,
                                 const EGLint* attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
    if (!newSurface || !walkerDpy || !walkerConfig || !error)
        return EGL_FALSE;

    if (!pixmap)
    {
        *error = EGL_BAD_NATIVE_PIXMAP;
        return EGL_FALSE;
    }

    EGLint glColorspace = EGL_GL_COLORSPACE_LINEAR;
    if (attrib_list)
    {
        EGLint i = 0;
        while (attrib_list[i] != EGL_NONE)
        {
            EGLint attrib = attrib_list[i];
            EGLint value  = attrib_list[i + 1];
            switch (attrib)
            {
            case EGL_GL_COLORSPACE:
                if (value == EGL_GL_COLORSPACE_LINEAR || value == EGL_GL_COLORSPACE_SRGB ||
                    value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT || value == EGL_GL_COLORSPACE_SCRGB_EXT ||
                    value == EGL_GL_COLORSPACE_BT2020_PQ_EXT || value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
                    value == EGL_GL_COLORSPACE_BT2020_HLG_EXT ||
                    value == EGL_GL_COLORSPACE_DISPLAY_P3_EXT || value == EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT ||
                    value == EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT)
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

    BITMAP bm;
    memset(&bm, 0, sizeof(bm));
    if (!GetObject(pixmap, sizeof(bm), &bm))
    {
        *error = EGL_BAD_NATIVE_PIXMAP;
        return EGL_FALSE;
    }

    HDC screenDC = GetDC(NULL);
    HDC memDC    = CreateCompatibleDC(screenDC);
    ReleaseDC(NULL, screenDC);
    if (!memDC)
    {
        *error = EGL_BAD_ALLOC;
        return EGL_FALSE;
    }

    SelectObject(memDC, pixmap);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    DescribePixelFormat(walkerDpy->display_id, walkerConfig->configId, sizeof(pfd), &pfd);

    if (!SetPixelFormat(memDC, walkerConfig->configId, &pfd))
    {
        DeleteDC(memDC);
        *error = EGL_BAD_MATCH;
        return EGL_FALSE;
    }

    newSurface->drawToWindow       = EGL_FALSE;
    newSurface->drawToPixmap       = EGL_TRUE;
    newSurface->drawToPBuffer      = EGL_FALSE;
    newSurface->doubleBuffer       = EGL_FALSE;
    newSurface->configId           = walkerConfig->configId;
    newSurface->width              = bm.bmWidth;
    newSurface->height             = bm.bmHeight;
    newSurface->swapBehavior       = EGL_BUFFER_DESTROYED;
    newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
    newSurface->mipmapLevel        = 0;
    newSurface->mipmapTexture      = EGL_FALSE;
    newSurface->largestPbuffer     = EGL_FALSE;
    newSurface->textureFormat      = EGL_NO_TEXTURE;
    newSurface->textureTarget      = EGL_NO_TEXTURE;
    newSurface->glColorspace       = glColorspace;

    newSurface->initialized                = EGL_TRUE;
    newSurface->destroy                    = EGL_FALSE;
    newSurface->pixmap                     = pixmap;
    newSurface->nativeSurfaceContainer.hdc = memDC;
    newSurface->nativeSurfaceContainer.hdr = nullptr;

    return EGL_TRUE;
}

EGLBoolean __copyBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* surface, EGLNativePixmapType target)
{
    (void)walkerDpy;
    (void)surface;

    if (!target)
        return EGL_FALSE;

    BITMAP bm;
    memset(&bm, 0, sizeof(bm));
    if (!GetObject(target, sizeof(bm), &bm))
        return EGL_FALSE;

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint width  = viewport[2];
    GLint height = viewport[3];

    if (width <= 0 || height <= 0)
        return EGL_FALSE;

    GLsizei  stride = (width * 4 + 3) & ~3;
    GLubyte* pixels = reinterpret_cast<GLubyte*>(malloc((size_t)stride * height));
    if (!pixels)
        return EGL_FALSE;

    // GL_BGRA = 0x80E1; bottom-up origin matches positive-height DIB
    glReadPixels(0, 0, width, height, 0x80E1, GL_UNSIGNED_BYTE, pixels);

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = height; // positive = bottom-up, matches GL origin
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(NULL);
    HDC memDC    = CreateCompatibleDC(screenDC);
    ReleaseDC(NULL, screenDC);
    if (!memDC)
    {
        free(pixels);

        return EGL_FALSE;
    }

    // MSDN forbids the target bitmap from being selected into the device context
    // passed to SetDIBits — doing so makes the call fail and eglCopyBuffers would
    // silently copy nothing while still reporting success. The DC is only needed for
    // its colour context here, so leave the bitmap unselected.
    int scanLines = SetDIBits(memDC, target, 0, (UINT)height, pixels, &bi, DIB_RGB_COLORS);

    DeleteDC(memDC);
    free(pixels);

    return (scanLines != 0) ? EGL_TRUE : EGL_FALSE;
}

__eglMustCastToProperFunctionPointerType __getProcAddress(const char* procname)
{
    __eglMustCastToProperFunctionPointerType ptr = NULL;

    if (wglGetProcAddress_PTR)
    {
        ptr = reinterpret_cast<__eglMustCastToProperFunctionPointerType>(wglGetProcAddress_PTR(procname));
    }
    if (ptr != NULL)
        return ptr;
    // https://www.khronos.org/opengl/wiki/Talk:Platform_specifics:_Windows
    if (!opengl32dll)
        return NULL;
    return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(GetProcAddress(opengl32dll, procname));
}

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error)
{
    if (!walkerDpy || !nativeLocalStorageContainer || !error)
    {
        return EGL_FALSE;
    }

    // Create configuration list.

    EGLint numberPixelFormats;

    // WGL_ARB_pixel_format is what the whole config enumeration is built on; without
    // it there is nothing to enumerate.
    if (!wglGetPixelFormatAttribivARB)
    {
        *error = EGL_NOT_INITIALIZED;

        return EGL_FALSE;
    }

    EGLint attribute = WGL_NUMBER_PIXEL_FORMATS_ARB;
    if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, 1, 0, 1, &attribute, &numberPixelFormats))
    {
        *error = EGL_NOT_INITIALIZED;

        return EGL_FALSE;
    }

    // wglGetExtensionsStringARB is optional and can legitimately return NULL on a
    // software / RDP / stripped driver — treat that as "no WGL extensions at all"
    // rather than handing NULL to strstr.
    const char* extensions_str = wglGetExtensionsStringARB
                                     ? wglGetExtensionsStringARB(nativeLocalStorageContainer->hdc)
                                     : NULL;
    if (!extensions_str)
    {
        extensions_str = "";
    }

    const int render_texture_supported = strstr(extensions_str, "WGL_ARB_render_texture") != NULL;
    const int ES_supported             = strstr(extensions_str, "WGL_EXT_create_context_es_profile") != NULL;
    EGLint    ES_mask                  = ES_supported * (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT);
#ifdef EGL_WIN_ENABLE_ANGLE
    // ANGLE provides real ES contexts independent of WGL — advertise the bits
    // on every config so eglChooseConfig with EGL_OPENGL_ES*_BIT can match.
    if (angle_isAvailable())
        ES_mask |= (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT);
#endif

    walkerDpy->srgbFramebufferSupported =
        (strstr(extensions_str, "WGL_ARB_framebuffer_sRGB") != NULL ||
         strstr(extensions_str, "WGL_EXT_framebuffer_sRGB") != NULL)
            ? EGL_TRUE
            : EGL_FALSE;

    // Query HDR colorspace support from the dummy window's display
    walkerDpy->supportedHDRColorspaces = __vkQueryHDRColorspaces(nativeLocalStorageContainer->hwnd);

    EGLConfigImpl* lastConfig = 0;
    for (EGLint currentPixelFormat = 1; currentPixelFormat <= numberPixelFormats; currentPixelFormat++)
    {
        EGLint value;

        attribute = WGL_SUPPORT_OPENGL_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &value))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }
        if (!value)
        {
            continue;
        }

        attribute = WGL_PIXEL_TYPE_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &value))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }
        if (value != WGL_TYPE_RGBA_ARB)
        {
            continue;
        }

        //

        EGLConfigImpl* newConfig = reinterpret_cast<EGLConfigImpl*>(malloc(sizeof(EGLConfigImpl)));
        if (!newConfig)
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }
        _eglInternalSetDefaultConfig(newConfig);

        // Store in the same order as received.
        newConfig->next = 0;
        if (lastConfig != 0)
        {
            lastConfig->next = newConfig;
        }
        else
        {
            walkerDpy->rootConfig = newConfig;
        }
        lastConfig = newConfig;

        //

        attribute = WGL_DRAW_TO_WINDOW_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->drawToWindow))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_DRAW_TO_BITMAP_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->drawToPixmap))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_DRAW_TO_PBUFFER_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->drawToPBuffer))
        {
            newConfig->drawToPBuffer = EGL_FALSE;
        }

        attribute = WGL_DOUBLE_BUFFER_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->doubleBuffer))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        //
        newConfig->conformant     = (EGL_OPENGL_BIT | ES_mask);
        newConfig->renderableType = (EGL_OPENGL_BIT | ES_mask);
        newConfig->surfaceType    = 0;
        if (newConfig->drawToWindow)
        {
            newConfig->surfaceType |= EGL_WINDOW_BIT;
        }
        if (newConfig->drawToPixmap)
        {
            newConfig->surfaceType |= EGL_PIXMAP_BIT;
        }
        if (newConfig->drawToPBuffer)
        {
            newConfig->surfaceType |= EGL_PBUFFER_BIT;
        }
        newConfig->colorBufferType = EGL_RGB_BUFFER;
        newConfig->configId        = currentPixelFormat;

        attribute = WGL_COLOR_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bufferSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_RED_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->redSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_GREEN_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->greenSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_BLUE_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->blueSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_ALPHA_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->alphaSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_DEPTH_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->depthSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_STENCIL_BITS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->stencilSize))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        //

        attribute = WGL_SAMPLE_BUFFERS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->sampleBuffers))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_SAMPLES_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->samples))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        //

        // sRGB capability is per pixel format, not per driver: NVIDIA for example exposes it
        // on every 8 bit per component format and on none of the 10 bit ones. Without the
        // extension the attribute cannot be queried at all, so report EGL_FALSE.
        newConfig->srgbCapable = EGL_FALSE;
        if (walkerDpy->srgbFramebufferSupported)
        {
            attribute = WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB;
            if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &value))
            {
                *error = EGL_NOT_INITIALIZED;

                return EGL_FALSE;
            }

            newConfig->srgbCapable = value ? EGL_TRUE : EGL_FALSE;
        }

        //

        // Without WGL_ARB_render_texture the attribute cannot be queried at all.
        // Report EGL_FALSE instead of leaving the EGL_DONT_CARE (-1) default, which
        // is truthy and would normalize to EGL_TRUE — advertising a capability that
        // eglBindTexImage then has to reject.
        attribute                   = WGL_BIND_TO_TEXTURE_RGB_ARB;
        newConfig->bindToTextureRGB = EGL_FALSE;
        if (render_texture_supported)
        {
            if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGB))
            {
                *error = EGL_NOT_INITIALIZED;

                return EGL_FALSE;
            }
            newConfig->bindToTextureRGB = newConfig->bindToTextureRGB ? EGL_TRUE : EGL_FALSE;
        }

        attribute                    = WGL_BIND_TO_TEXTURE_RGBA_ARB;
        newConfig->bindToTextureRGBA = EGL_FALSE;
        if (render_texture_supported)
        {
            if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGBA))
            {
                *error = EGL_NOT_INITIALIZED;

                return EGL_FALSE;
            }
            newConfig->bindToTextureRGBA = newConfig->bindToTextureRGBA ? EGL_TRUE : EGL_FALSE;
        }

        //

        attribute = WGL_MAX_PBUFFER_PIXELS_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferPixels))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_MAX_PBUFFER_WIDTH_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferWidth))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_MAX_PBUFFER_HEIGHT_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferHeight))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        //

        attribute = WGL_TRANSPARENT_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentType))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }
        newConfig->transparentType = newConfig->transparentType ? EGL_TRANSPARENT_RGB : EGL_NONE;

        attribute = WGL_TRANSPARENT_RED_VALUE_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentRedValue))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_TRANSPARENT_GREEN_VALUE_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentGreenValue))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        attribute = WGL_TRANSPARENT_BLUE_VALUE_ARB;
        if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentBlueValue))
        {
            *error = EGL_NOT_INITIALIZED;

            return EGL_FALSE;
        }

        newConfig->matchNativePixmap = EGL_NONE;
        newConfig->nativeRenderable  = EGL_DONT_CARE; // ???

        // Query configCaveat from acceleration type.
        int accelValue = 0;
        attribute      = WGL_ACCELERATION_ARB;
        if (wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &accelValue))
        {
            if (accelValue == WGL_NO_ACCELERATION_ARB)
                newConfig->configCaveat = EGL_SLOW_CONFIG;
            else if (accelValue == WGL_GENERIC_ACCELERATION_ARB)
                newConfig->configCaveat = EGL_SLOW_CONFIG;
            else
                newConfig->configCaveat = EGL_NONE;
        }
        else
        {
            newConfig->configCaveat = EGL_NONE;
        }

        // WGL has no concept of overlay/underlay levels.
        newConfig->level = 0;

        // Reasonable desktop defaults for swap interval range.
        newConfig->minSwapInterval = 0;
        newConfig->maxSwapInterval = 1;
    }

    return EGL_TRUE;
}

EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeContextContainer, const EGLint* attribList)
{
    if (!walkerDpy || !nativeContextContainer || !nativeSurfaceContainer)
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (g_localStorage.api == EGL_OPENGL_ES_API && angle_isAvailable())
    {
        // Extract requested ES version from the WGL-style attrib list that
        // __processAttribList produced.
        EGLint major = 2, minor = 0;
        if (attribList)
        {
            for (EGLint i = 0; attribList[i] != 0; i += 2)
            {
                if (attribList[i] == WGL_CONTEXT_MAJOR_VERSION_ARB)
                    major = attribList[i + 1];
                else if (attribList[i] == WGL_CONTEXT_MINOR_VERSION_ARB)
                    minor = attribList[i + 1];
            }
        }
        void* share = nullptr;
        if (sharedNativeContextContainer && sharedNativeContextContainer->backend == EGL_BACKEND_ANGLE)
            share = sharedNativeContextContainer->angleCtx;

        void* ctx = nullptr;
        if (angle_createContext(major, minor, share, &ctx) != EGL_TRUE)
            return EGL_FALSE;

        nativeContextContainer->backend  = EGL_BACKEND_ANGLE;
        nativeContextContainer->angleCtx = ctx;
        nativeContextContainer->ctx      = nullptr;
        return EGL_TRUE;
    }
#endif

    nativeContextContainer->backend  = EGL_BACKEND_WGL;
    nativeContextContainer->angleCtx = nullptr;
    nativeContextContainer->ctx      = nullptr;

    // WGL_ARB_create_context is optional; without it no versioned context exists.
    if (!wglCreateContextAttribsARB)
    {
        return EGL_FALSE;
    }

    nativeContextContainer->ctx = wglCreateContextAttribsARB(nativeSurfaceContainer->hdc, sharedNativeContextContainer ? sharedNativeContextContainer->ctx : 0, attribList);

    return nativeContextContainer->ctx != 0;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
    if (!walkerDpy || (nativeContextContainer && !nativeSurfaceContainer))
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (nativeContextContainer && nativeContextContainer->backend == EGL_BACKEND_ANGLE)
    {
        // Backend must match — an ANGLE context cannot be made current on a
        // WGL surface (and the WGL surface's `angleSurface` would be nullptr).
        if (!nativeSurfaceContainer || nativeSurfaceContainer->backend != EGL_BACKEND_ANGLE)
            return EGL_FALSE;
        return angle_makeCurrent(nativeSurfaceContainer->angleSurface,
                                 nativeContextContainer->angleCtx);
    }
    if (nativeContextContainer && nativeSurfaceContainer &&
        nativeSurfaceContainer->backend == EGL_BACKEND_ANGLE)
    {
        // Inverse mismatch: WGL context with ANGLE surface.
        return EGL_FALSE;
    }
    if (!nativeContextContainer)
    {
        // Detach from both backends to be safe.
        if (angle_isAvailable())
            angle_makeCurrent(nullptr, nullptr);
    }
#endif

    if (!nativeContextContainer)
        return (EGLBoolean)wglMakeCurrent_PTR(NULL, NULL);

    BOOL res = (EGLBoolean)wglMakeCurrent_PTR(nativeSurfaceContainer->hdc, nativeContextContainer->ctx);
    return res;
}

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface)
{
    if (!walkerDpy || !walkerSurface)
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (walkerSurface->nativeSurfaceContainer.backend == EGL_BACKEND_ANGLE)
        return angle_swapBuffers(walkerSurface->nativeSurfaceContainer.angleSurface);
#endif

    if (walkerSurface->nativeSurfaceContainer.hdr)
    {
        __vkUpdateHDRMetadata(walkerSurface->nativeSurfaceContainer.hdr, walkerSurface);
        return __vkPresent(walkerSurface->nativeSurfaceContainer.hdr);
    }

    return (EGLBoolean)SwapBuffers(walkerSurface->nativeSurfaceContainer.hdc);
}

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval)
{
    if (!walkerDpy)
    {
        return EGL_FALSE;
    }

#ifdef EGL_WIN_ENABLE_ANGLE
    if (g_localStorage.api == EGL_OPENGL_ES_API && angle_isAvailable())
        return angle_swapInterval(interval);
#endif

    // WGL_EXT_swap_control is optional.
    if (!wglSwapIntervalEXT)
    {
        return EGL_FALSE;
    }

    return (EGLBoolean)wglSwapIntervalEXT(interval);
}

EGLBoolean __bindTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
    if (!wglBindTexImageARB_PTR)
        return EGL_FALSE;

    int wglBuffer = (buffer == EGL_BACK_BUFFER) ? WGL_BACK_LEFT_ARB : WGL_FRONT_LEFT_ARB;
    return (EGLBoolean)wglBindTexImageARB_PTR(walkerSurface->pbuf, wglBuffer);
}

EGLBoolean __releaseTexImage(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface, EGLint buffer)
{
    if (!walkerDpy || !walkerSurface)
        return EGL_FALSE;
    if (!wglReleaseTexImageARB_PTR)
        return EGL_FALSE;

    int wglBuffer = (buffer == EGL_BACK_BUFFER) ? WGL_BACK_LEFT_ARB : WGL_FRONT_LEFT_ARB;
    return (EGLBoolean)wglReleaseTexImageARB_PTR(walkerSurface->pbuf, wglBuffer);
}

EGLBoolean __getPlatformDependentHandles(void* out, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
    if (!out || !walkerDpy || !nativeSurfaceContainer || !nativeContextContainer)
        return EGL_FALSE;

    EGLContextInternals* handles = reinterpret_cast<EGLContextInternals*>(out);

    handles->display = walkerDpy->display_id;
    handles->context = nativeContextContainer->ctx;
    handles->surface = nativeSurfaceContainer->hdc;

    return EGL_TRUE;
}

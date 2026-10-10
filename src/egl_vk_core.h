/**
 * EGL Vulkan HDR shared core — platform-independent half of the HDR present
 * path, shared by the Windows (WGL + Vulkan) and Linux (X11/Wayland + Vulkan)
 * backends.
 *
 * The platform backends implement the two seam hooks declared below
 * (__vkRecreateSwapchain, __vkInitGLSide) and own the handle-export machinery
 * (Win32 HANDLE vs fd; the ownership semantics differ and are deliberately not
 * unified).
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

#ifndef EGL_VK_CORE_H
#define EGL_VK_CORE_H

// Same platform selection as the backend headers: the Vulkan platform
// extension headers need the native types in scope first.
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(USE_X11)
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#elif defined(WL_EGL_PLATFORM)
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif

#include <vulkan/vulkan.h>
#include "egl_internal.h"
#include <GL/gl.h>
#if defined(__linux__)
#include <GL/glext.h>
#endif

// ---- Vulkan extension function pointer types ----

typedef VkResult(VKAPI_PTR* PFN_vkSetHdrMetadataEXT_t)(VkDevice, uint32_t, const VkSwapchainKHR*, const VkHdrMetadataEXT*);

// ---- GL interop function pointer types (system <GL/glext.h> wins where it
//      already defines them) ----

#ifndef GL_EXT_memory_object
typedef void(APIENTRY* PFNGLCREATEMEMORYOBJECTSEXTPROC)(GLsizei n, GLuint* memoryObjects);
typedef void(APIENTRY* PFNGLDELETEMEMORYOBJECTSEXTPROC)(GLsizei n, const GLuint* memoryObjects);
typedef void(APIENTRY* PFNGLTEXSTORAGEMEM2DEXTPROC)(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, unsigned long long offset);
#endif

#ifndef GL_EXT_semaphore
typedef void(APIENTRY* PFNGLGENSEMAPHORESEXTPROC)(GLsizei n, GLuint* semaphores);
typedef void(APIENTRY* PFNGLDELETESEMAPHORESEXTPROC)(GLsizei n, const GLuint* semaphores);
typedef void(APIENTRY* PFNGLSIGNALSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* dstLayouts);
typedef void(APIENTRY* PFNGLWAITSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* srcLayouts);
#endif

#ifndef GL_ARB_framebuffer_object
typedef void(APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void(APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void(APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void(APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void(APIENTRY* PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
#endif

// ---- GL interop constants (system definitions win where available) ----

#ifndef GL_LAYOUT_GENERAL_EXT
#define GL_LAYOUT_GENERAL_EXT 0x958Du
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8u
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9u
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0u
#endif

// ---- Shared Vulkan/GL state (one device shared across all surfaces) ----
// Defined in egl_vk_core.cpp; assigned by __vkInit/__vkTerm/__vkLoadGLInterop
// in the platform backends.

extern VkInstance                       g_vkInstance;
extern VkPhysicalDevice                 g_vkPhysDevice;
extern VkDevice                         g_vkDevice;
extern uint32_t                         g_vkQueueFamily;
extern VkQueue                          g_vkQueue;
extern PFN_vkSetHdrMetadataEXT_t        g_pfnSetHdrMetadata;

extern PFNGLCREATEMEMORYOBJECTSEXTPROC g_pfnCreateMemObjs;
extern PFNGLDELETEMEMORYOBJECTSEXTPROC g_pfnDeleteMemObjs;
extern PFNGLTEXSTORAGEMEM2DEXTPROC     g_pfnTexStorageMem2D;
extern PFNGLGENSEMAPHORESEXTPROC       g_pfnGenSemaphores;
extern PFNGLDELETESEMAPHORESEXTPROC    g_pfnDeleteSemaphores;
extern PFNGLSIGNALSEMAPHOREEXTPROC     g_pfnSignalSemaphore;
extern PFNGLWAITSEMAPHOREEXTPROC       g_pfnWaitSemaphore;
extern PFNGLGENFRAMEBUFFERSPROC        g_pfnGenFBOs;
extern PFNGLDELETEFRAMEBUFFERSPROC     g_pfnDeleteFBOs;
extern PFNGLBINDFRAMEBUFFERPROC        g_pfnBindFBO;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC   g_pfnFBOTex2D;
extern PFNGLBLITFRAMEBUFFERPROC        g_pfnBlitFBO;
extern bool                            g_glInteropLoaded;

// ---- Shared functions ----

bool _eglHDRColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs);

bool __vkHdrMetadataEqual(const VkHdrMetadataEXT* a, const VkHdrMetadataEXT* b);

void __vkUpdateHDRMetadata(NativeHDRSurfaceContainer* hdr, const EGLSurfaceImpl* surf);

VkCompositeAlphaFlagBitsKHR __vkPickCompositeAlpha(const VkSurfaceCapabilitiesKHR& caps);

bool __vkPickImageUsage(const VkSurfaceCapabilitiesKHR& caps, VkImageUsageFlags* usage);

EGLBoolean __vkCreateFrameObjects(NativeHDRSurfaceContainer* hdr, uint32_t imgCount);

void __vkDestroyFrameObjects(NativeHDRSurfaceContainer* hdr);

EGLBoolean __vkPresent(NativeHDRSurfaceContainer* hdr);

// ---- Platform seam: implemented by each backend ----

// Called from __vkPresent on OUT_OF_DATE / SUBOPTIMAL.
EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore);

// Called from __vkPresent when the GL side has to be (re)bound after the
// swapchain changed.
bool __vkInitGLSide(NativeHDRSurfaceContainer* hdr);

// Probe extent of the HDR capability checks: the tiny dummy surface the
// swapchain probe is built against before any real surface exists.
#define VK_HDR_PROBE_EXTENT 256

#endif // EGL_VK_CORE_H

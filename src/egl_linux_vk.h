/**
 * EGL Linux Vulkan HDR backend — shared by egl_x11_glx.cpp (LINUX_VK) and egl_wayland.cpp.
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

#ifndef __egl_linux_vk_h_
#define __egl_linux_vk_h_

#if defined(USE_X11)
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#elif defined(WL_EGL_PLATFORM)
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif

#include <vulkan/vulkan.h>
#include "egl_internal.h"
#include <GL/gl.h>

// Full definition of the HDR surface container (forward-declared in egl_internal.h)
struct _NativeHDRSurfaceContainer
{
    VkSurfaceKHR     vkSurface;
    VkSwapchainKHR   vkSwapchain;
    VkFormat         vkFormat;
    VkColorSpaceKHR  vkColorSpace;
    uint32_t         imageCount;
    VkImage*         swapchainImages;
    VkCommandPool    cmdPool;
    VkCommandBuffer* cmdBuffers;
    VkFence*         fences;

    VkImage        renderImage;
    VkDeviceMemory renderMemory;
    GLuint         glTexture;
    GLuint         glMemoryObject;
    GLuint         blitFbo;

    VkSemaphore acquireSemaphore;
    VkSemaphore glDoneSemaphore;
    VkSemaphore blitDoneSemaphore;
    GLuint      glDoneSemObj;

    uint32_t     width;
    uint32_t     height;
    VkDeviceSize renderMemorySize;

    bool glInteropReady;
    int  pendingMemFd; // -1 = not yet consumed by GL
    int  pendingSemFd; // -1 = not yet consumed by GL

    // HDR mastering/content-light metadata (EGL_EXT_surface_SMPTE2086/CTA861_3),
    // applied to the swapchain via vkSetHdrMetadataEXT when present and changed.
    VkHdrMetadataEXT hdrMetadata;
    bool             hasHdrMetadata;
    bool             hdrMetadataDirty;

#if defined(USE_X11)
    Display* x11Display;
    Window   x11Window;
#elif defined(WL_EGL_PLATFORM)
    struct wl_display* wlDisplay;
    struct wl_surface* wlSurface;
#endif
};

// Vulkan HDR backend interface

// Map an HDR EGL colorspace to Vulkan format + colorspace.  Returns false for SDR.
bool _eglHDRColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs);

// Map any EGL colorspace (SDR or HDR) to Vulkan format + colorspace.
// Used by Wayland backend where all surfaces are VK-backed.
bool _eglColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs);

bool       __vkIsReady();
EGLBoolean __vkInit();
void       __vkTerm();

// Create a swapchain + GL/Vulkan interop objects for one window surface.
// nativeDisplay = EGLNativeDisplayType  (Display* on X11, wl_display* on Wayland)
// nativeWindow  = EGLNativeWindowType   (Window on X11, wl_egl_window* on Wayland)
EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr,
                                EGLNativeWindowType        nativeWindow,
                                EGLNativeDisplayType       nativeDisplay,
                                EGLint eglCS, uint32_t w, uint32_t h);

void       __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr);
EGLBoolean __vkPresent(NativeHDRSurfaceContainer* hdr);

// Copy the surface's SMPTE2086/CTA861 metadata into the HDR container. Only flags
// it for submission when the colorspace actually consumes it (HDR10 PQ / HLG) and
// the application supplied non-zero values.
void __vkUpdateHDRMetadata(NativeHDRSurfaceContainer* hdr, const EGLSurfaceImpl* surf);

// Query which HDR colorspaces are usable on a given native display + window.
// Returns a bitmask of EGL_HDR_CS_*_BIT values; 0 = none / Vulkan unavailable.
uint32_t __vkQueryHDRColorspaces(EGLNativeDisplayType display, EGLNativeWindowType window);

#endif /* __egl_linux_vk_h_ */

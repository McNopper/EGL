#include "egl_vk_core.h"
/**
 * EGL Linux Vulkan HDR backend — X11 (Xlib) and Wayland.
 *
 * Shared by egl_x11_glx.cpp (built with -DLINUX_VK) for optional HDR on X11,
 * and by egl_wayland.cpp (always included) for mandatory VK presentation on Wayland.
 *
 * Interop model:
 *   GL renders to a normal framebuffer.
 *   On swap, we blit FBO 0 → a Vulkan-exported GL texture via GL_EXT_memory_object.
 *   A GL semaphore (GL_EXT_semaphore) signals Vulkan that the blit is done.
 *   Vulkan blits the texture into the HDR swapchain image and presents.
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

#include "egl_linux_vk.h"
#include "egl_common.h"
#include <GL/glext.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <EGL/eglext.h>

extern __eglMustCastToProperFunctionPointerType __getProcAddress(const char* procname);

// ---- Vulkan extension function pointer types ----

typedef VkResult(VKAPI_PTR* PFN_vkGetMemoryFdKHR_t)(VkDevice, const VkMemoryGetFdInfoKHR*, int*);
typedef VkResult(VKAPI_PTR* PFN_vkGetSemaphoreFdKHR_t)(VkDevice, const VkSemaphoreGetFdInfoKHR*, int*);

// ---- GL interop function pointer types (use system <GL/glext.h> definitions where available) ----

#ifndef GL_EXT_memory_object
typedef void(APIENTRY* PFNGLIMPORTMEMORYFDEXTPROC)(GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
#endif

#ifndef GL_EXT_semaphore
typedef void(APIENTRY* PFNGLIMPORTSEMAPHOREFDEXTPROC)(GLuint semaphore, GLenum handleType, GLint fd);
#endif

// GL FBO function pointer types (GL 3.0 core — may be in system headers)

// GL interop constants — use system definitions where available
#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586u
#endif
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

// ---- Vulkan singleton globals (one device shared across all surfaces) ----

static PFN_vkGetMemoryFdKHR_t    g_pfnGetMemFd       = nullptr;
static PFN_vkGetSemaphoreFdKHR_t g_pfnGetSemFd       = nullptr;

// ---- GL interop function pointers (loaded once after first GL context is current) ----

static PFNGLIMPORTMEMORYFDEXTPROC      g_pfnImportMemFd      = nullptr;
static PFNGLIMPORTSEMAPHOREFDEXTPROC   g_pfnImportSemFd      = nullptr;


bool _eglColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs)
{
    if (_eglHDRColorspaceToVk(eglCS, fmt, cs))
        return true;
    switch (eglCS)
    {
    case EGL_GL_COLORSPACE_LINEAR:
        *fmt = VK_FORMAT_R8G8B8A8_UNORM;
        *cs  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        return true;
    case EGL_GL_COLORSPACE_SRGB:
        *fmt = VK_FORMAT_R8G8B8A8_SRGB;
        *cs  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        return true;
    default:
        return false;
    }
}


// ---- __vkInit: create VkInstance + VkDevice (called once per process) ----

EGLBoolean __vkInit()
{
    if (g_vkInstance != VK_NULL_HANDLE)
        return EGL_TRUE;

    // VK_KHR_surface and the WSI extension are required, but
    // VK_EXT_swapchain_colorspace is not. Making it mandatory meant
    // vkCreateInstance failed outright on drivers without it, and on Wayland every
    // window surface is Vulkan-presented, so that took all presentation down with
    // it. Enable it only when the instance actually exposes it - the HDR/P3
    // entries in __vkQueryHDRColorspaces then simply fail their swapchain probe
    // and stay unadvertised.
    static const char* k_requiredInstExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(USE_X11)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(WL_EGL_PLATFORM)
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
    };

    std::vector<const char*> instExts(k_requiredInstExts,
                                      k_requiredInstExts + sizeof(k_requiredInstExts) / sizeof(k_requiredInstExts[0]));

    {
        uint32_t instExtCount = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr) == VK_SUCCESS && instExtCount > 0)
        {
            std::vector<VkExtensionProperties> instExtProps(instExtCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExtProps.data());
            for (uint32_t i = 0; i < instExtCount; ++i)
            {
                if (strcmp(instExtProps[i].extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0)
                {
                    instExts.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                    break;
                }
            }
        }
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType             = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName  = "EGL";
    appInfo.apiVersion        = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instCI    = {};
    instCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo        = &appInfo;
    instCI.enabledExtensionCount   = (uint32_t)instExts.size();
    instCI.ppEnabledExtensionNames = instExts.data();

    if (vkCreateInstance(&instCI, nullptr, &g_vkInstance) != VK_SUCCESS)
        return EGL_FALSE;

    // Pick discrete GPU, fall back to first enumerated device
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(g_vkInstance, &devCount, nullptr);
    if (devCount == 0)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(g_vkInstance, &devCount, devices.data());

    g_vkPhysDevice = devices[0];
    for (auto& d : devices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            g_vkPhysDevice = d;
            break;
        }
    }

    // Find a graphics queue family
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vkPhysDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vkPhysDevice, &qfCount, qfProps.data());

    g_vkQueueFamily = UINT32_MAX;
    {
        auto it = std::find_if(qfProps.begin(), qfProps.end(),
                               [](const VkQueueFamilyProperties& p)
                               { return (p.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0; });
        if (it != qfProps.end())
            g_vkQueueFamily = static_cast<uint32_t>(it - qfProps.begin());
    }
    if (g_vkQueueFamily == UINT32_MAX)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }

    // Check available device extensions. The swapchain and external memory/semaphore
    // extensions are the whole point of this backend — if any of them is missing the
    // device would silently be created without it and every later swapchain or interop
    // call would be made against a device that never enabled it, so fail cleanly here.
    const char* requiredDevExts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    };
    // Optional: vkSetHdrMetadataEXT is already guarded by its null function pointer.
    const char* optionalDevExts[] = {
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
    };

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, availExts.data());

    auto extAvailable = [&availExts](const char* name) -> bool
    {
        return std::any_of(availExts.begin(), availExts.end(),
                           [name](const VkExtensionProperties& e)
                           { return strcmp(name, e.extensionName) == 0; });
    };

    std::vector<const char*> enabledDevExts;
    for (const auto* req : requiredDevExts)
    {
        if (!extAvailable(req))
        {
            vkDestroyInstance(g_vkInstance, nullptr);
            g_vkInstance   = VK_NULL_HANDLE;
            g_vkPhysDevice = VK_NULL_HANDLE;
            return EGL_FALSE;
        }
        enabledDevExts.push_back(req);
    }
    for (const auto* opt : optionalDevExts)
    {
        if (extAvailable(opt))
            enabledDevExts.push_back(opt);
    }

    float                   qPriority = 1.0f;
    VkDeviceQueueCreateInfo qCI       = {};
    qCI.sType                         = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex              = g_vkQueueFamily;
    qCI.queueCount                    = 1;
    qCI.pQueuePriorities              = &qPriority;

    VkDeviceCreateInfo devCI      = {};
    devCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount    = 1;
    devCI.pQueueCreateInfos       = &qCI;
    devCI.enabledExtensionCount   = (uint32_t)enabledDevExts.size();
    devCI.ppEnabledExtensionNames = enabledDevExts.data();

    if (vkCreateDevice(g_vkPhysDevice, &devCI, nullptr, &g_vkDevice) != VK_SUCCESS)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }

    vkGetDeviceQueue(g_vkDevice, g_vkQueueFamily, 0, &g_vkQueue);

    g_pfnSetHdrMetadata = (PFN_vkSetHdrMetadataEXT_t)vkGetDeviceProcAddr(g_vkDevice, "vkSetHdrMetadataEXT");
    g_pfnGetMemFd       = (PFN_vkGetMemoryFdKHR_t)vkGetDeviceProcAddr(g_vkDevice, "vkGetMemoryFdKHR");
    g_pfnGetSemFd       = (PFN_vkGetSemaphoreFdKHR_t)vkGetDeviceProcAddr(g_vkDevice, "vkGetSemaphoreFdKHR");

    return EGL_TRUE;
}

// ---- __vkTerm: destroy VkDevice + VkInstance ----

void __vkTerm()
{
    if (g_vkDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(g_vkDevice);
        vkDestroyDevice(g_vkDevice, nullptr);
        g_vkDevice = VK_NULL_HANDLE;
    }
    if (g_vkInstance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
    }
    g_vkPhysDevice      = VK_NULL_HANDLE;
    g_vkQueueFamily     = UINT32_MAX;
    g_vkQueue           = VK_NULL_HANDLE;
    g_pfnSetHdrMetadata = nullptr;
    g_pfnGetMemFd       = nullptr;
    g_pfnGetSemFd       = nullptr;

    // The GL interop entry points point into libGL, which __internalTerminate unloads
    // right after us. Drop them all so a later init/swap cycle re-resolves them
    // instead of calling through stale pointers.
    g_pfnCreateMemObjs    = nullptr;
    g_pfnDeleteMemObjs    = nullptr;
    g_pfnImportMemFd      = nullptr;
    g_pfnTexStorageMem2D  = nullptr;
    g_pfnGenSemaphores    = nullptr;
    g_pfnDeleteSemaphores = nullptr;
    g_pfnImportSemFd      = nullptr;
    g_pfnSignalSemaphore  = nullptr;
    g_pfnWaitSemaphore    = nullptr;
    g_pfnGenFBOs          = nullptr;
    g_pfnDeleteFBOs       = nullptr;
    g_pfnBindFBO          = nullptr;
    g_pfnFBOTex2D         = nullptr;
    g_pfnBlitFBO          = nullptr;
    g_glInteropLoaded     = false;
}

// ---- Swapchain parameter helpers ----

// VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR is not guaranteed to be supported, so pick the
// first bit the surface actually reports — several Wayland compositors offer only
// INHERIT, which would otherwise make the whole backend unusable there. Preference

// We blit into the swapchain images, so TRANSFER_DST is as mandatory for us as

// ---- __vkQueryHDRColorspaces: query HDR formats for a native display + window ----

uint32_t __vkQueryHDRColorspaces(EGLNativeDisplayType display, EGLNativeWindowType window)
{
    if (g_vkInstance == VK_NULL_HANDLE)
        return 0;

    VkSurfaceKHR tmpSurface = VK_NULL_HANDLE;

#if defined(USE_X11)
    {
        Display*                   x11Dpy = reinterpret_cast<Display*>(display);
        Window                     x11Win = (Window)(uintptr_t)window;
        VkXlibSurfaceCreateInfoKHR sci    = {};
        sci.sType                         = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        sci.dpy                           = x11Dpy;
        sci.window                        = x11Win;
        if (vkCreateXlibSurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
            return 0;
    }
#elif defined(WL_EGL_PLATFORM)
    {
        struct wl_display*            wlDpy  = reinterpret_cast<struct wl_display*>(display);
        struct wl_egl_window*         eglWin = reinterpret_cast<struct wl_egl_window*>(window);
        VkWaylandSurfaceCreateInfoKHR sci    = {};
        sci.sType                            = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        sci.display                          = wlDpy;
        sci.surface                          = eglWin ? eglWin->surface : nullptr;
        if (!sci.surface || vkCreateWaylandSurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
            return 0;
    }
#else
    (void)display;
    (void)window;
    return 0;
#endif

    // The queue family we present from must support this surface — on a multi-GPU
    // laptop it may not, and everything below would then report bogus capabilities.
    // __vkCreateHDRSurface performs the same check before it commits to a swapchain.
    {
        VkBool32 presentOK = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(g_vkPhysDevice, g_vkQueueFamily, tmpSurface, &presentOK) != VK_SUCCESS ||
            !presentOK)
        {
            vkDestroySurfaceKHR(g_vkInstance, tmpSurface, nullptr);
            return 0;
        }
    }

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, formats.data());

    // Check exact format+colorspace pairs we actually request at swapchain creation time.
    struct HdrEntry
    {
        VkFormat        fmt;
        VkColorSpaceKHR cs;
        uint32_t        bit;
    };
    static const HdrEntry k_entries[] = {
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, EGL_HDR_CS_SCRGB_LINEAR_BIT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, EGL_HDR_CS_SCRGB_BIT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT, EGL_HDR_CS_BT2020_PQ_BIT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_BT2020_LINEAR_EXT, EGL_HDR_CS_BT2020_LINEAR_BIT},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT, EGL_HDR_CS_BT2020_HLG_BIT},
        // Display-P3 family. Without these the EGL_HDR_CS_DISPLAY_P3* bits were
        // never set, so EGL_EXT_gl_colorspace_display_p3{,_linear,_passthrough}
        // were never advertised even though _eglHDRColorspaceToVk and surface
        // creation both fully support them - the display_p3 examples could never
        // run. The format+colorspace pair must match _eglHDRColorspaceToVk above.
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, EGL_HDR_CS_DISPLAY_P3_BIT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT, EGL_HDR_CS_DISPLAY_P3_LINEAR_BIT},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT, EGL_HDR_CS_DISPLAY_P3_PASSTHROUGH_BIT},
    };

    VkSurfaceCapabilitiesKHR caps = {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, tmpSurface, &caps) != VK_SUCCESS)
    {
        vkDestroySurfaceKHR(g_vkInstance, tmpSurface, nullptr);
        return 0;
    }
    VkImageUsageFlags imgUsage = 0;
    if (!__vkPickImageUsage(caps, &imgUsage))
    {
        vkDestroySurfaceKHR(g_vkInstance, tmpSurface, nullptr);
        return 0;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX || extent.width == 0)
        extent.width = VK_HDR_PROBE_EXTENT;
    if (extent.height == UINT32_MAX || extent.height == 0)
        extent.height = VK_HDR_PROBE_EXTENT;
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    uint32_t bits = 0;
    for (auto& entry : k_entries)
    {
        // First confirm the driver lists this exact format+colorspace pair.
        bool listed = std::any_of(formats.begin(), formats.end(),
                                  [&entry](const VkSurfaceFormatKHR& f)
                                  { return f.format == entry.fmt && f.colorSpace == entry.cs; });
        if (!listed)
            continue;

        // Confirm by attempting a real test swapchain — some drivers list pairs
        // they cannot actually create (e.g. NVIDIA lists BT2020_LINEAR but fails).
        VkSwapchainCreateInfoKHR swCI = {};
        swCI.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swCI.surface                  = tmpSurface;
        swCI.minImageCount            = imgCount;
        swCI.imageFormat              = entry.fmt;
        swCI.imageColorSpace          = entry.cs;
        swCI.imageExtent              = extent;
        swCI.imageArrayLayers         = 1;
        swCI.imageUsage               = imgUsage;
        swCI.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
        swCI.preTransform             = caps.currentTransform;
        swCI.compositeAlpha           = __vkPickCompositeAlpha(caps);
        swCI.presentMode              = VK_PRESENT_MODE_FIFO_KHR;
        swCI.clipped                  = VK_TRUE;

        VkSwapchainKHR testSwap = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(g_vkDevice, &swCI, nullptr, &testSwap) == VK_SUCCESS)
        {
            bits |= entry.bit;
            vkDestroySwapchainKHR(g_vkDevice, testSwap, nullptr);
        }
    }

    vkDestroySurfaceKHR(g_vkInstance, tmpSurface, nullptr);
    return bits;
}

// ---- __vkLoadGLInterop: load GL interop + FBO function pointers (once) ----

static void __vkLoadGLInterop()
{
    if (g_glInteropLoaded)
        return;

    g_pfnCreateMemObjs    = (PFNGLCREATEMEMORYOBJECTSEXTPROC)__getProcAddress("glCreateMemoryObjectsEXT");
    g_pfnDeleteMemObjs    = (PFNGLDELETEMEMORYOBJECTSEXTPROC)__getProcAddress("glDeleteMemoryObjectsEXT");
    g_pfnImportMemFd      = (PFNGLIMPORTMEMORYFDEXTPROC)__getProcAddress("glImportMemoryFdEXT");
    g_pfnTexStorageMem2D  = (PFNGLTEXSTORAGEMEM2DEXTPROC)__getProcAddress("glTexStorageMem2DEXT");
    g_pfnGenSemaphores    = (PFNGLGENSEMAPHORESEXTPROC)__getProcAddress("glGenSemaphoresEXT");
    g_pfnDeleteSemaphores = (PFNGLDELETESEMAPHORESEXTPROC)__getProcAddress("glDeleteSemaphoresEXT");
    g_pfnImportSemFd      = (PFNGLIMPORTSEMAPHOREFDEXTPROC)__getProcAddress("glImportSemaphoreFdEXT");
    g_pfnSignalSemaphore  = (PFNGLSIGNALSEMAPHOREEXTPROC)__getProcAddress("glSignalSemaphoreEXT");
    g_pfnWaitSemaphore    = (PFNGLWAITSEMAPHOREEXTPROC)__getProcAddress("glWaitSemaphoreEXT");
    g_pfnGenFBOs          = (PFNGLGENFRAMEBUFFERSPROC)__getProcAddress("glGenFramebuffers");
    g_pfnDeleteFBOs       = (PFNGLDELETEFRAMEBUFFERSPROC)__getProcAddress("glDeleteFramebuffers");
    g_pfnBindFBO          = (PFNGLBINDFRAMEBUFFERPROC)__getProcAddress("glBindFramebuffer");
    g_pfnFBOTex2D         = (PFNGLFRAMEBUFFERTEXTURE2DPROC)__getProcAddress("glFramebufferTexture2D");
    g_pfnBlitFBO          = (PFNGLBLITFRAMEBUFFERPROC)__getProcAddress("glBlitFramebuffer");

    g_glInteropLoaded = (g_pfnCreateMemObjs != nullptr &&
                         g_pfnDeleteMemObjs != nullptr &&
                         g_pfnImportMemFd != nullptr &&
                         g_pfnTexStorageMem2D != nullptr &&
                         g_pfnGenSemaphores != nullptr &&
                         g_pfnDeleteSemaphores != nullptr &&
                         g_pfnImportSemFd != nullptr &&
                         g_pfnSignalSemaphore != nullptr &&
                         g_pfnWaitSemaphore != nullptr &&
                         g_pfnGenFBOs != nullptr &&
                         g_pfnDeleteFBOs != nullptr &&
                         g_pfnBindFBO != nullptr &&
                         g_pfnFBOTex2D != nullptr &&
                         g_pfnBlitFBO != nullptr);
}

// ---- Per-image / per-frame object lifetime ----

// Destroy every per-image and per-frame object and reset the counts to zero, so a
// partially built (or already torn down) container is always self-consistent: the

// Build the per-image and per-frame objects for a freshly created swapchain.
// imageCount / frameCount are only advanced once arrays of that size definitely
// exist and are fully initialised, so every early return leaves the container in a

// ---- __vkDestroyHDRSurface: tear down all Vulkan + GL HDR objects ----

void __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr)
{
    if (!hdr)
        return;

    if (g_vkDevice != VK_NULL_HANDLE)
    {
        // The render fences do not cover presentation: the last vkQueuePresentKHR may
        // still be waiting on a render-finished semaphore. A full idle is the only
        // point at which the swapchain, its semaphores and the surface are all free.
        vkDeviceWaitIdle(g_vkDevice);
    }

    // Release unconsumed fds (GL never imported them)
    if (hdr->pendingMemFd >= 0)
    {
        close(hdr->pendingMemFd);
        hdr->pendingMemFd = -1;
    }
    if (hdr->pendingSemFd >= 0)
    {
        close(hdr->pendingSemFd);
        hdr->pendingSemFd = -1;
    }

    // GL cleanup (best-effort; requires current context)
    if (hdr->blitFbo && g_pfnDeleteFBOs)
        g_pfnDeleteFBOs(1, &hdr->blitFbo);
    if (hdr->glTexture)
        glDeleteTextures(1, &hdr->glTexture);
    if (hdr->glMemoryObject && g_pfnDeleteMemObjs)
        g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
    if (hdr->glDoneSemObj && g_pfnDeleteSemaphores)
        g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj);

    // Vulkan cleanup
    __vkDestroyFrameObjects(hdr); // frees the per-image/per-frame arrays too
    if (g_vkDevice != VK_NULL_HANDLE)
    {
        if (hdr->glDoneSemaphore)
            vkDestroySemaphore(g_vkDevice, hdr->glDoneSemaphore, nullptr);
        if (hdr->cmdPool)
            vkDestroyCommandPool(g_vkDevice, hdr->cmdPool, nullptr);
        if (hdr->renderMemory)
            vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr);
        if (hdr->renderImage)
            vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr);
        if (hdr->vkSwapchain)
            vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr);
    }
    if (g_vkInstance != VK_NULL_HANDLE && hdr->vkSurface)
        vkDestroySurfaceKHR(g_vkInstance, hdr->vkSurface, nullptr);

    memset(hdr, 0, sizeof(*hdr));
    hdr->pendingMemFd = -1;
    hdr->pendingSemFd = -1;
}

// ---- __vkRecreateSwapchain: rebuild swapchain + render image on resize ----

EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE)
        return EGL_FALSE;

#if defined(USE_X11)
    if (!hdr->x11Display || !hdr->x11Window)
        return EGL_FALSE;
#elif defined(WL_EGL_PLATFORM)
    if (!hdr->wlDisplay || !hdr->wlSurface)
        return EGL_FALSE;
#endif

    if (drainGLSemaphore && hdr->glDoneSemaphore != VK_NULL_HANDLE)
    {
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo         si    = {};
        si.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount      = 1;
        si.pWaitSemaphores         = &hdr->glDoneSemaphore;
        si.pWaitDstStageMask       = &stage;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue);
    }
    else if (hdr->fences && hdr->frameCount > 0)
    {
        vkWaitForFences(g_vkDevice, hdr->frameCount, hdr->fences, VK_TRUE, UINT64_MAX);
    }

    if (hdr->blitFbo && g_pfnDeleteFBOs)
    {
        g_pfnDeleteFBOs(1, &hdr->blitFbo);
        hdr->blitFbo = 0;
    }
    if (hdr->glTexture)
    {
        glDeleteTextures(1, &hdr->glTexture);
        hdr->glTexture = 0;
    }
    if (hdr->glMemoryObject && g_pfnDeleteMemObjs)
    {
        g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
        hdr->glMemoryObject = 0;
    }
    if (hdr->glDoneSemObj && g_pfnDeleteSemaphores)
    {
        g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj);
        hdr->glDoneSemObj = 0;
    }

    if (hdr->pendingMemFd >= 0)
    {
        close(hdr->pendingMemFd);
        hdr->pendingMemFd = -1;
    }
    if (hdr->pendingSemFd >= 0)
    {
        close(hdr->pendingSemFd);
        hdr->pendingSemFd = -1;
    }

    if (hdr->renderMemory)
    {
        vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr);
        hdr->renderMemory = VK_NULL_HANDLE;
    }
    if (hdr->renderImage)
    {
        vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr);
        hdr->renderImage = VK_NULL_HANDLE;
    }
    free(hdr->swapchainImages);
    hdr->swapchainImages = nullptr;

    // Query current window dimensions
    uint32_t newW = 0, newH = 0;
#if defined(USE_X11)
    {
        XWindowAttributes xwa = {};
        XGetWindowAttributes(hdr->x11Display, hdr->x11Window, &xwa);
        newW = (uint32_t)xwa.width;
        newH = (uint32_t)xwa.height;
    }
#elif defined(WL_EGL_PLATFORM)
    {
        // VK_KHR_wayland_surface mandates currentExtent == (UINT32_MAX, UINT32_MAX),
        // so the capabilities can never report a resize here. The wl_egl_window the
        // application resizes is the only source of the current size.
        if (hdr->wlEglWindow)
        {
            newW = (uint32_t)hdr->wlEglWindow->width;
            newH = (uint32_t)hdr->wlEglWindow->height;
        }
        else
        {
            newW = hdr->width;
            newH = hdr->height;
        }
    }
#endif

    if (newW == 0 || newH == 0)
    {
        if (hdr->vkSwapchain)
        {
            vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr);
            hdr->vkSwapchain = VK_NULL_HANDLE;
        }
        hdr->glInteropReady = false;
        return EGL_TRUE;
    }
    hdr->width  = newW;
    hdr->height = newH;

    VkSwapchainKHR oldSwap = hdr->vkSwapchain;
    {
        VkSurfaceCapabilitiesKHR caps = {};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, hdr->vkSurface, &caps) != VK_SUCCESS)
            return EGL_FALSE;
        VkImageUsageFlags imgUsage = 0;
        if (!__vkPickImageUsage(caps, &imgUsage))
            return EGL_FALSE;
        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
            imgCount = caps.maxImageCount;
        VkExtent2D extent = {newW, newH};
        if (caps.currentExtent.width != UINT32_MAX)
            extent = caps.currentExtent;

        VkSwapchainCreateInfoKHR swCI = {};
        swCI.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swCI.surface                  = hdr->vkSurface;
        swCI.minImageCount            = imgCount;
        swCI.imageFormat              = hdr->vkFormat;
        swCI.imageColorSpace          = hdr->vkColorSpace;
        swCI.imageExtent              = extent;
        swCI.imageArrayLayers         = 1;
        swCI.imageUsage               = imgUsage;
        swCI.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
        swCI.preTransform             = caps.currentTransform;
        swCI.compositeAlpha           = __vkPickCompositeAlpha(caps);
        swCI.presentMode              = VK_PRESENT_MODE_FIFO_KHR;
        swCI.clipped                  = VK_TRUE;
        swCI.oldSwapchain             = oldSwap;
        if (vkCreateSwapchainKHR(g_vkDevice, &swCI, nullptr, &hdr->vkSwapchain) != VK_SUCCESS)
        {
            hdr->vkSwapchain = VK_NULL_HANDLE;
            if (oldSwap)
            {
                vkDeviceWaitIdle(g_vkDevice);
                vkDestroySwapchainKHR(g_vkDevice, oldSwap, nullptr);
            }
            return EGL_FALSE;
        }
        // The blit destination must follow the images we really got, not the window
        // size, which can already have moved on again during a live resize.
        hdr->swapchainExtent = extent;
    }
    if (oldSwap)
    {
        // The presentation engine may still own images of the old swapchain — the
        // render fences say nothing about that, so idle the device before destroying it.
        vkDeviceWaitIdle(g_vkDevice);
        vkDestroySwapchainKHR(g_vkDevice, oldSwap, nullptr);
    }

    uint32_t newImgCount = 0;
    if (vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &newImgCount, nullptr) != VK_SUCCESS || newImgCount == 0)
        return EGL_FALSE;

    // The per-image and per-frame objects belong to the swapchain that has just been
    // destroyed, so rebuild them all. Tearing down first keeps imageCount/frameCount
    // in step with the arrays: no early return below can leave a count describing
    // more entries than were actually allocated.
    __vkDestroyFrameObjects(hdr);

    hdr->swapchainImages = reinterpret_cast<VkImage*>(malloc(newImgCount * sizeof(VkImage)));
    if (!hdr->swapchainImages)
        return EGL_FALSE;
    if (vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &newImgCount, hdr->swapchainImages) != VK_SUCCESS)
        return EGL_FALSE;

    if (!__vkCreateFrameObjects(hdr, newImgCount))
        return EGL_FALSE;

    // Recreate exportable render image
    {
        VkExternalMemoryImageCreateInfo emici = {};
        emici.sType                           = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emici.handleTypes                     = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkImageCreateInfo imgCI = {};
        imgCI.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgCI.pNext             = &emici;
        imgCI.imageType         = VK_IMAGE_TYPE_2D;
        imgCI.format            = hdr->vkFormat;
        imgCI.extent            = {newW, newH, 1};
        imgCI.mipLevels         = 1;
        imgCI.arrayLayers       = 1;
        imgCI.samples           = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling            = VK_IMAGE_TILING_OPTIMAL;
        imgCI.usage             = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgCI.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
        imgCI.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_vkDevice, &imgCI, nullptr, &hdr->renderImage) != VK_SUCCESS)
            return EGL_FALSE;

        VkMemoryRequirements memReqs = {};
        vkGetImageMemoryRequirements(g_vkDevice, hdr->renderImage, &memReqs);
        hdr->renderMemorySize = memReqs.size;

        VkExportMemoryAllocateInfo emai = {};
        emai.sType                      = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        emai.handleTypes                = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkPhysicalDeviceMemoryProperties memProps = {};
        vkGetPhysicalDeviceMemoryProperties(g_vkPhysDevice, &memProps);
        uint32_t memTypeIdx = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            if ((memReqs.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memTypeIdx = i;
                break;
            }
        if (memTypeIdx == UINT32_MAX)
            return EGL_FALSE;

        VkMemoryAllocateInfo mai = {};
        mai.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext                = &emai;
        mai.allocationSize       = memReqs.size;
        mai.memoryTypeIndex      = memTypeIdx;
        if (vkAllocateMemory(g_vkDevice, &mai, nullptr, &hdr->renderMemory) != VK_SUCCESS)
            return EGL_FALSE;

        if (vkBindImageMemory(g_vkDevice, hdr->renderImage, hdr->renderMemory, 0) != VK_SUCCESS)
            return EGL_FALSE;
    }

    // Transition renderImage UNDEFINED → GENERAL
    {
        VkCommandBuffer             initCmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cbAI    = {};
        cbAI.sType                          = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool                    = hdr->cmdPool;
        cbAI.level                          = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount             = 1;
        if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, &initCmd) != VK_SUCCESS)
            return EGL_FALSE;

        VkCommandBufferBeginInfo bi = {};
        bi.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(initCmd, &bi) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
            return EGL_FALSE;
        }

        VkImageMemoryBarrier barrier = {};
        barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                = hdr->renderImage;
        barrier.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(initCmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (vkEndCommandBuffer(initCmd) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
            return EGL_FALSE;
        }

        VkSubmitInfo si       = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &initCmd;
        if (vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
            return EGL_FALSE;
        }
        vkQueueWaitIdle(g_vkQueue);
        vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
    }

    if (!g_pfnGetMemFd)
        return EGL_FALSE;
    {
        VkMemoryGetFdInfoKHR hInfo = {};
        hInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        hInfo.memory               = hdr->renderMemory;
        hInfo.handleType           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetMemFd(g_vkDevice, &hInfo, &hdr->pendingMemFd) != VK_SUCCESS)
            return EGL_FALSE;
    }

    if (!g_pfnGetSemFd)
        return EGL_FALSE;
    {
        VkSemaphoreGetFdInfoKHR shInfo = {};
        shInfo.sType                   = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        shInfo.semaphore               = hdr->glDoneSemaphore;
        shInfo.handleType              = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetSemFd(g_vkDevice, &shInfo, &hdr->pendingSemFd) != VK_SUCCESS || hdr->pendingSemFd < 0)
            return EGL_FALSE;
    }

    hdr->glInteropReady   = false;
    hdr->hdrMetadataDirty = true; // re-apply metadata to the new swapchain
    return EGL_TRUE;
}

// ---- __vkCreateHDRSurface: create swapchain + GL/Vulkan interop objects ----

EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr,
                                EGLNativeWindowType        nativeWindow,
                                EGLNativeDisplayType       nativeDisplay,
                                EGLint eglCS, uint32_t w, uint32_t h)
{
    if (!hdr || g_vkInstance == VK_NULL_HANDLE || g_vkDevice == VK_NULL_HANDLE)
    {
        return EGL_FALSE;
    }

    memset(hdr, 0, sizeof(*hdr));
    hdr->width        = w;
    hdr->height       = h;
    hdr->pendingMemFd = -1;
    hdr->pendingSemFd = -1;

#if defined(USE_X11)
    hdr->x11Display = reinterpret_cast<Display*>(nativeDisplay);
    hdr->x11Window  = (Window)(uintptr_t)nativeWindow;
#elif defined(WL_EGL_PLATFORM)
    hdr->wlDisplay = reinterpret_cast<struct wl_display*>(nativeDisplay);
    {
        struct wl_egl_window* eglWin = reinterpret_cast<struct wl_egl_window*>(nativeWindow);
        hdr->wlEglWindow             = eglWin;
        hdr->wlSurface               = eglWin ? eglWin->surface : nullptr;
    }
#endif

    if (!_eglColorspaceToVk(eglCS, &hdr->vkFormat, &hdr->vkColorSpace))
    {
        return EGL_FALSE;
    }

    // Build all Vulkan + GL interop objects. Any failure returns EGL_FALSE and the
    // wrapper below tears the partial state down via __vkDestroyHDRSurface.
    auto build = [&]() -> EGLBoolean
    {

    // 1. Create VkSurfaceKHR
#if defined(USE_X11)
        {
            VkXlibSurfaceCreateInfoKHR sci = {};
            sci.sType                      = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            sci.dpy                        = hdr->x11Display;
            sci.window                     = hdr->x11Window;
            if (vkCreateXlibSurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
                return EGL_FALSE;
        }
#elif defined(WL_EGL_PLATFORM)
        {
            VkWaylandSurfaceCreateInfoKHR sci = {};
            sci.sType                         = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            sci.display                       = hdr->wlDisplay;
            sci.surface                       = hdr->wlSurface;
            if (!hdr->wlSurface || vkCreateWaylandSurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
                return EGL_FALSE;
        }
#else
        return EGL_FALSE;
#endif

        {
            VkBool32 presentOK = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(g_vkPhysDevice, g_vkQueueFamily, hdr->vkSurface, &presentOK) != VK_SUCCESS ||
                !presentOK)
                return EGL_FALSE;
        }

        {
            uint32_t fmtCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(fmtCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, formats.data());
            bool found = std::any_of(formats.begin(), formats.end(),
                                     [hdr](const VkSurfaceFormatKHR& f)
                                     { return f.format == hdr->vkFormat && f.colorSpace == hdr->vkColorSpace; });
            if (!found)
                return EGL_FALSE;
        }

        // 2. Create VkSwapchainKHR
        {
            VkSurfaceCapabilitiesKHR caps = {};
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, hdr->vkSurface, &caps) != VK_SUCCESS)
                return EGL_FALSE;
            VkImageUsageFlags imgUsage = 0;
            if (!__vkPickImageUsage(caps, &imgUsage))
                return EGL_FALSE;
            uint32_t imgCount = caps.minImageCount + 1;
            if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
                imgCount = caps.maxImageCount;
            VkExtent2D extent = {w, h};
            if (caps.currentExtent.width != UINT32_MAX)
                extent = caps.currentExtent;

            VkSwapchainCreateInfoKHR swCI = {};
            swCI.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swCI.surface                  = hdr->vkSurface;
            swCI.minImageCount            = imgCount;
            swCI.imageFormat              = hdr->vkFormat;
            swCI.imageColorSpace          = hdr->vkColorSpace;
            swCI.imageExtent              = extent;
            swCI.imageArrayLayers         = 1;
            swCI.imageUsage               = imgUsage;
            swCI.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
            swCI.preTransform             = caps.currentTransform;
            swCI.compositeAlpha           = __vkPickCompositeAlpha(caps);
            swCI.presentMode              = VK_PRESENT_MODE_FIFO_KHR;
            swCI.clipped                  = VK_TRUE;
            if (vkCreateSwapchainKHR(g_vkDevice, &swCI, nullptr, &hdr->vkSwapchain) != VK_SUCCESS)
                return EGL_FALSE;
            // The blit destination follows the images we really got, which need not
            // match the requested w/h.
            hdr->swapchainExtent = extent;
        }

        // 3. Retrieve swapchain images
        uint32_t imgCount = 0;
        if (vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &imgCount, nullptr) != VK_SUCCESS || imgCount == 0)
            return EGL_FALSE;
        hdr->swapchainImages = reinterpret_cast<VkImage*>(malloc(imgCount * sizeof(VkImage)));
        if (!hdr->swapchainImages)
            return EGL_FALSE;
        if (vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &imgCount, hdr->swapchainImages) != VK_SUCCESS)
            return EGL_FALSE;

        // 4. Create command pool + per-image and per-frame objects
        {
            VkCommandPoolCreateInfo cpCI = {};
            cpCI.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cpCI.queueFamilyIndex        = g_vkQueueFamily;
            cpCI.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(g_vkDevice, &cpCI, nullptr, &hdr->cmdPool) != VK_SUCCESS)
                return EGL_FALSE;

            if (!__vkCreateFrameObjects(hdr, imgCount))
                return EGL_FALSE;
        }

        // 5. Create exportable interop VkImage + allocate exportable memory
        {
            VkExternalMemoryImageCreateInfo emici = {};
            emici.sType                           = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            emici.handleTypes                     = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

            VkImageCreateInfo imgCI = {};
            imgCI.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imgCI.pNext             = &emici;
            imgCI.imageType         = VK_IMAGE_TYPE_2D;
            imgCI.format            = hdr->vkFormat;
            imgCI.extent            = {w, h, 1};
            imgCI.mipLevels         = 1;
            imgCI.arrayLayers       = 1;
            imgCI.samples           = VK_SAMPLE_COUNT_1_BIT;
            imgCI.tiling            = VK_IMAGE_TILING_OPTIMAL;
            imgCI.usage             = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            imgCI.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
            imgCI.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(g_vkDevice, &imgCI, nullptr, &hdr->renderImage) != VK_SUCCESS)
                return EGL_FALSE;

            VkMemoryRequirements memReqs = {};
            vkGetImageMemoryRequirements(g_vkDevice, hdr->renderImage, &memReqs);
            hdr->renderMemorySize = memReqs.size;

            VkExportMemoryAllocateInfo emai = {};
            emai.sType                      = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
            emai.handleTypes                = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

            VkPhysicalDeviceMemoryProperties memProps = {};
            vkGetPhysicalDeviceMemoryProperties(g_vkPhysDevice, &memProps);
            uint32_t memTypeIdx = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            {
                if ((memReqs.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                {
                    memTypeIdx = i;
                    break;
                }
            }
            if (memTypeIdx == UINT32_MAX)
                return EGL_FALSE;

            VkMemoryAllocateInfo mai = {};
            mai.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.pNext                = &emai;
            mai.allocationSize       = memReqs.size;
            mai.memoryTypeIndex      = memTypeIdx;
            if (vkAllocateMemory(g_vkDevice, &mai, nullptr, &hdr->renderMemory) != VK_SUCCESS)
                return EGL_FALSE;

            if (vkBindImageMemory(g_vkDevice, hdr->renderImage, hdr->renderMemory, 0) != VK_SUCCESS)
                return EGL_FALSE;
        }

        // 6. Transition renderImage from UNDEFINED to GENERAL
        {
            VkCommandBuffer             initCmd = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo cbAI    = {};
            cbAI.sType                          = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbAI.commandPool                    = hdr->cmdPool;
            cbAI.level                          = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbAI.commandBufferCount             = 1;
            if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, &initCmd) != VK_SUCCESS)
                return EGL_FALSE;

            VkCommandBufferBeginInfo bi = {};
            bi.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(initCmd, &bi) != VK_SUCCESS)
            {
                vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
                return EGL_FALSE;
            }

            VkImageMemoryBarrier barrier = {};
            barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout            = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                = hdr->renderImage;
            barrier.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(initCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
            if (vkEndCommandBuffer(initCmd) != VK_SUCCESS)
            {
                vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
                return EGL_FALSE;
            }

            VkSubmitInfo si       = {};
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &initCmd;
            if (vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS)
            {
                vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
                return EGL_FALSE;
            }
            vkQueueWaitIdle(g_vkQueue);
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
        }

        // 7. Export renderImage memory as fd
        {
            if (!g_pfnGetMemFd)
                return EGL_FALSE;

            VkMemoryGetFdInfoKHR hInfo = {};
            hInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
            hInfo.memory               = hdr->renderMemory;
            hInfo.handleType           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
            if (g_pfnGetMemFd(g_vkDevice, &hInfo, &hdr->pendingMemFd) != VK_SUCCESS)
                return EGL_FALSE;
        }

        // 8. Create the exportable GL-done semaphore. The acquire and render-finished
        // semaphores are per frame / per image and were already created in step 4.
        {
            VkExportSemaphoreCreateInfo esci = {};
            esci.sType                       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            esci.handleTypes                 = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

            VkSemaphoreCreateInfo semCI = {};
            semCI.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semCI.pNext                 = &esci;
            if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->glDoneSemaphore) != VK_SUCCESS)
                return EGL_FALSE;

            if (!g_pfnGetSemFd)
                return EGL_FALSE;
            VkSemaphoreGetFdInfoKHR shInfo = {};
            shInfo.sType                   = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
            shInfo.semaphore               = hdr->glDoneSemaphore;
            shInfo.handleType              = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            if (g_pfnGetSemFd(g_vkDevice, &shInfo, &hdr->pendingSemFd) != VK_SUCCESS || hdr->pendingSemFd < 0)
                return EGL_FALSE;
        }

        return EGL_TRUE;
    }; // end build lambda

    if (!build())
    {
        __vkDestroyHDRSurface(hdr);
        return EGL_FALSE;
    }

    return EGL_TRUE;
}

// ---- __vkInitGLSide: lazily create GL interop objects (called on first present) ----

bool __vkInitGLSide(NativeHDRSurfaceContainer* hdr)
{
    __vkLoadGLInterop();
    if (!g_glInteropLoaded)
        return false;

    while (glGetError() != GL_NO_ERROR)
    {
    } // clear any pre-existing GL error

    // glImportMemoryFdEXT transfers ownership of the fd to GL on success, so we
    // set it to -1 afterwards. If the import fails the fd is NOT consumed — close
    // it ourselves to avoid leaking it.
    g_pfnCreateMemObjs(1, &hdr->glMemoryObject);
    g_pfnImportMemFd(hdr->glMemoryObject, hdr->renderMemorySize,
                     GL_HANDLE_TYPE_OPAQUE_FD_EXT, hdr->pendingMemFd);
    if (glGetError() != GL_NO_ERROR)
    {
        if (hdr->pendingMemFd >= 0)
        {
            close(hdr->pendingMemFd);
            hdr->pendingMemFd = -1;
        }
        if (hdr->glMemoryObject)
        {
            g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
            hdr->glMemoryObject = 0;
        }
        return false;
    }
    hdr->pendingMemFd = -1; // consumed by GL

    glGenTextures(1, &hdr->glTexture);
    glBindTexture(GL_TEXTURE_2D, hdr->glTexture);
    GLenum glFmt = (hdr->vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
                       ? 0x881Au  // GL_RGBA16F
                       : 0x8059u; // GL_RGB10_A2
    g_pfnTexStorageMem2D(GL_TEXTURE_2D, 1, glFmt,
                         static_cast<GLsizei>(hdr->width), static_cast<GLsizei>(hdr->height),
                         hdr->glMemoryObject, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR)
    {
        if (hdr->glTexture)
        {
            glDeleteTextures(1, &hdr->glTexture);
            hdr->glTexture = 0;
        }
        if (hdr->glMemoryObject)
        {
            g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
            hdr->glMemoryObject = 0;
        }
        return false;
    }

    g_pfnGenFBOs(1, &hdr->blitFbo);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, hdr->blitFbo);
    g_pfnFBOTex2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                  GL_TEXTURE_2D, hdr->glTexture, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    g_pfnGenSemaphores(1, &hdr->glDoneSemObj);
    g_pfnImportSemFd(hdr->glDoneSemObj, GL_HANDLE_TYPE_OPAQUE_FD_EXT, hdr->pendingSemFd);
    if (glGetError() != GL_NO_ERROR)
    {
        if (hdr->pendingSemFd >= 0)
        {
            close(hdr->pendingSemFd);
            hdr->pendingSemFd = -1;
        }
        if (hdr->glDoneSemObj)
        {
            g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj);
            hdr->glDoneSemObj = 0;
        }
        if (hdr->blitFbo)
        {
            g_pfnDeleteFBOs(1, &hdr->blitFbo);
            hdr->blitFbo = 0;
        }
        if (hdr->glTexture)
        {
            glDeleteTextures(1, &hdr->glTexture);
            hdr->glTexture = 0;
        }
        if (hdr->glMemoryObject)
        {
            g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
            hdr->glMemoryObject = 0;
        }
        return false;
    }
    hdr->pendingSemFd = -1; // consumed by GL

    hdr->glInteropReady = true;
    return true;
}

// ---- __vkUpdateHDRMetadata: pull SMPTE2086/CTA861 metadata from the surface ----


// ---- __vkPresent: blit GL default FBO → interop image → swapchain, then present ----



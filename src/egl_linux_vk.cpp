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

typedef VkResult (VKAPI_PTR *PFN_vkSetHdrMetadataEXT_t)(VkDevice, uint32_t, const VkSwapchainKHR*, const VkHdrMetadataEXT*);
typedef VkResult (VKAPI_PTR *PFN_vkGetMemoryFdKHR_t)(VkDevice, const VkMemoryGetFdInfoKHR*, int*);
typedef VkResult (VKAPI_PTR *PFN_vkGetSemaphoreFdKHR_t)(VkDevice, const VkSemaphoreGetFdInfoKHR*, int*);

// ---- GL interop function pointer types (use system <GL/glext.h> definitions where available) ----

#ifndef GL_EXT_memory_object
typedef void (APIENTRY* PFNGLCREATEMEMORYOBJECTSEXTPROC)(GLsizei n, GLuint* memoryObjects);
typedef void (APIENTRY* PFNGLDELETEMEMORYOBJECTSEXTPROC)(GLsizei n, const GLuint* memoryObjects);
typedef void (APIENTRY* PFNGLIMPORTMEMORYFDEXTPROC)(GLuint memory, GLuint64 size, GLenum handleType, GLint fd);
typedef void (APIENTRY* PFNGLTEXSTORAGEMEM2DEXTPROC)(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, GLuint64 offset);
#endif

#ifndef GL_EXT_semaphore
typedef void (APIENTRY* PFNGLGENSEMAPHORESEXTPROC)(GLsizei n, GLuint* semaphores);
typedef void (APIENTRY* PFNGLDELETESEMAPHORESEXTPROC)(GLsizei n, const GLuint* semaphores);
typedef void (APIENTRY* PFNGLIMPORTSEMAPHOREFDEXTPROC)(GLuint semaphore, GLenum handleType, GLint fd);
typedef void (APIENTRY* PFNGLSIGNALSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* dstLayouts);
typedef void (APIENTRY* PFNGLWAITSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* srcLayouts);
#endif

// GL FBO function pointer types (GL 3.0 core — may be in system headers)
#ifndef GL_ARB_framebuffer_object
typedef void (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRY* PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
#endif

// GL interop constants — use system definitions where available
#ifndef GL_HANDLE_TYPE_OPAQUE_FD_EXT
#  define GL_HANDLE_TYPE_OPAQUE_FD_EXT    0x9586u
#endif
#ifndef GL_LAYOUT_GENERAL_EXT
#  define GL_LAYOUT_GENERAL_EXT           0x958Du
#endif
#ifndef GL_READ_FRAMEBUFFER
#  define GL_READ_FRAMEBUFFER             0x8CA8u
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#  define GL_DRAW_FRAMEBUFFER             0x8CA9u
#endif
#ifndef GL_COLOR_ATTACHMENT0
#  define GL_COLOR_ATTACHMENT0            0x8CE0u
#endif

// ---- Vulkan singleton globals (one device shared across all surfaces) ----

static VkInstance                g_vkInstance    = VK_NULL_HANDLE;
static VkPhysicalDevice          g_vkPhysDevice  = VK_NULL_HANDLE;
static VkDevice                  g_vkDevice      = VK_NULL_HANDLE;
static uint32_t                  g_vkQueueFamily = UINT32_MAX;
static VkQueue                   g_vkQueue       = VK_NULL_HANDLE;
static PFN_vkSetHdrMetadataEXT_t g_pfnSetHdrMetadata = nullptr;
static PFN_vkGetMemoryFdKHR_t    g_pfnGetMemFd       = nullptr;
static PFN_vkGetSemaphoreFdKHR_t g_pfnGetSemFd       = nullptr;

// ---- GL interop function pointers (loaded once after first GL context is current) ----

static PFNGLCREATEMEMORYOBJECTSEXTPROC  g_pfnCreateMemObjs    = nullptr;
static PFNGLDELETEMEMORYOBJECTSEXTPROC  g_pfnDeleteMemObjs    = nullptr;
static PFNGLIMPORTMEMORYFDEXTPROC       g_pfnImportMemFd      = nullptr;
static PFNGLTEXSTORAGEMEM2DEXTPROC      g_pfnTexStorageMem2D  = nullptr;
static PFNGLGENSEMAPHORESEXTPROC        g_pfnGenSemaphores    = nullptr;
static PFNGLDELETESEMAPHORESEXTPROC     g_pfnDeleteSemaphores = nullptr;
static PFNGLIMPORTSEMAPHOREFDEXTPROC    g_pfnImportSemFd      = nullptr;
static PFNGLSIGNALSEMAPHOREEXTPROC      g_pfnSignalSemaphore  = nullptr;
static PFNGLWAITSEMAPHOREEXTPROC        g_pfnWaitSemaphore    = nullptr;
static PFNGLGENFRAMEBUFFERSPROC         g_pfnGenFBOs          = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC      g_pfnDeleteFBOs       = nullptr;
static PFNGLBINDFRAMEBUFFERPROC         g_pfnBindFBO          = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC    g_pfnFBOTex2D         = nullptr;
static PFNGLBLITFRAMEBUFFERPROC         g_pfnBlitFBO          = nullptr;
static bool                             g_glInteropLoaded     = false;

// ---- Colorspace mapping helpers ----

bool _eglHDRColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs)
{
    switch (eglCS)
    {
        case EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT:
            *fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            *cs  = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
            return true;
        case EGL_GL_COLORSPACE_SCRGB_EXT:
            *fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            *cs  = VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT;
            return true;
        case EGL_GL_COLORSPACE_BT2020_PQ_EXT:
            *fmt = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            *cs  = VK_COLOR_SPACE_HDR10_ST2084_EXT;
            return true;
        case EGL_GL_COLORSPACE_BT2020_LINEAR_EXT:
            *fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            *cs  = VK_COLOR_SPACE_BT2020_LINEAR_EXT;
            return true;
        case EGL_GL_COLORSPACE_BT2020_HLG_EXT:
            *fmt = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            *cs  = VK_COLOR_SPACE_HDR10_HLG_EXT;
            return true;
        case EGL_GL_COLORSPACE_DISPLAY_P3_EXT:
        case EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT:
            *fmt = VK_FORMAT_R8G8B8A8_UNORM;
            *cs  = VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
            return true;
        case EGL_GL_COLORSPACE_DISPLAY_P3_LINEAR_EXT:
            *fmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            *cs  = VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT;
            return true;
        default:
            return false;
    }
}

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

static uint32_t _vkColorspaceToBit(VkColorSpaceKHR vkCS)
{
    switch (vkCS)
    {
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:    return EGL_HDR_CS_SCRGB_LINEAR_BIT;
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return EGL_HDR_CS_SCRGB_BIT;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:            return EGL_HDR_CS_BT2020_PQ_BIT;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:           return EGL_HDR_CS_BT2020_LINEAR_BIT;
        case VK_COLOR_SPACE_HDR10_HLG_EXT:               return EGL_HDR_CS_BT2020_HLG_BIT;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:    return EGL_HDR_CS_DISPLAY_P3_BIT;
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:       return EGL_HDR_CS_DISPLAY_P3_LINEAR_BIT;
        default: return 0;
    }
}

// Forward declaration
static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore);

// ---- __vkInit: create VkInstance + VkDevice (called once per process) ----

EGLBoolean __vkInit()
{
    if (g_vkInstance != VK_NULL_HANDLE)
        return EGL_TRUE;

    const char* instExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(USE_X11)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(WL_EGL_PLATFORM)
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
        VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
    };

    VkApplicationInfo appInfo = {};
    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "EGL";
    appInfo.apiVersion       = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instCI = {};
    instCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo        = &appInfo;
    instCI.enabledExtensionCount   = (uint32_t)(sizeof(instExts) / sizeof(instExts[0]));
    instCI.ppEnabledExtensionNames = instExts;

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
            [](const VkQueueFamilyProperties& p){ return (p.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0; });
        if (it != qfProps.end())
            g_vkQueueFamily = static_cast<uint32_t>(it - qfProps.begin());
    }
    if (g_vkQueueFamily == UINT32_MAX)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }

    // Enable device extensions — silently skip unsupported ones
    const char* wantedDevExts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
    };

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, availExts.data());

    std::vector<const char*> enabledDevExts;
    for (const auto* req : wantedDevExts)
    {
        if (std::any_of(availExts.begin(), availExts.end(),
                [req](const VkExtensionProperties& e){ return strcmp(req, e.extensionName) == 0; }))
            enabledDevExts.push_back(req);
    }

    float qPriority = 1.0f;
    VkDeviceQueueCreateInfo qCI = {};
    qCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = g_vkQueueFamily;
    qCI.queueCount       = 1;
    qCI.pQueuePriorities = &qPriority;

    VkDeviceCreateInfo devCI = {};
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
    g_pfnGetMemFd       = (PFN_vkGetMemoryFdKHR_t)   vkGetDeviceProcAddr(g_vkDevice, "vkGetMemoryFdKHR");
    g_pfnGetSemFd       = (PFN_vkGetSemaphoreFdKHR_t) vkGetDeviceProcAddr(g_vkDevice, "vkGetSemaphoreFdKHR");

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
}

// ---- __vkQueryHDRColorspaces: query HDR formats for a native display + window ----

uint32_t __vkQueryHDRColorspaces(EGLNativeDisplayType display, EGLNativeWindowType window)
{
    if (g_vkInstance == VK_NULL_HANDLE)
        return 0;

    VkSurfaceKHR tmpSurface = VK_NULL_HANDLE;

#if defined(USE_X11)
    {
        Display* x11Dpy = reinterpret_cast<Display*>(display);
        Window   x11Win = (Window)(uintptr_t)window;
        VkXlibSurfaceCreateInfoKHR sci = {};
        sci.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        sci.dpy    = x11Dpy;
        sci.window = x11Win;
        if (vkCreateXlibSurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
            return 0;
    }
#elif defined(WL_EGL_PLATFORM)
    {
        struct wl_display*    wlDpy  = reinterpret_cast<struct wl_display*>(display);
        struct wl_egl_window* eglWin = reinterpret_cast<struct wl_egl_window*>(window);
        VkWaylandSurfaceCreateInfoKHR sci = {};
        sci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        sci.display = wlDpy;
        sci.surface = eglWin ? eglWin->surface : nullptr;
        if (!sci.surface || vkCreateWaylandSurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
            return 0;
    }
#else
    (void)display; (void)window;
    return 0;
#endif

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, formats.data());

    // Check exact format+colorspace pairs we actually request at swapchain creation time.
    struct HdrEntry { VkFormat fmt; VkColorSpaceKHR cs; uint32_t bit; };
    static const HdrEntry k_entries[] = {
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,    EGL_HDR_CS_SCRGB_LINEAR_BIT  },
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, EGL_HDR_CS_SCRGB_BIT         },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT,            EGL_HDR_CS_BT2020_PQ_BIT     },
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_BT2020_LINEAR_EXT,           EGL_HDR_CS_BT2020_LINEAR_BIT },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT,               EGL_HDR_CS_BT2020_HLG_BIT    },
    };

    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, tmpSurface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width  == UINT32_MAX || extent.width  == 0) extent.width  = 256;
    if (extent.height == UINT32_MAX || extent.height == 0) extent.height = 256;
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    uint32_t bits = 0;
    for (auto& entry : k_entries)
    {
        // First confirm the driver lists this exact format+colorspace pair.
        bool listed = std::any_of(formats.begin(), formats.end(),
            [&entry](const VkSurfaceFormatKHR& f){ return f.format == entry.fmt && f.colorSpace == entry.cs; });
        if (!listed)
            continue;

        // Confirm by attempting a real test swapchain — some drivers list pairs
        // they cannot actually create (e.g. NVIDIA lists BT2020_LINEAR but fails).
        VkSwapchainCreateInfoKHR swCI = {};
        swCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swCI.surface          = tmpSurface;
        swCI.minImageCount    = imgCount;
        swCI.imageFormat      = entry.fmt;
        swCI.imageColorSpace  = entry.cs;
        swCI.imageExtent      = extent;
        swCI.imageArrayLayers = 1;
        swCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swCI.preTransform     = caps.currentTransform;
        swCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swCI.clipped          = VK_TRUE;

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

    g_pfnCreateMemObjs    = (PFNGLCREATEMEMORYOBJECTSEXTPROC) __getProcAddress("glCreateMemoryObjectsEXT");
    g_pfnDeleteMemObjs    = (PFNGLDELETEMEMORYOBJECTSEXTPROC) __getProcAddress("glDeleteMemoryObjectsEXT");
    g_pfnImportMemFd      = (PFNGLIMPORTMEMORYFDEXTPROC)      __getProcAddress("glImportMemoryFdEXT");
    g_pfnTexStorageMem2D  = (PFNGLTEXSTORAGEMEM2DEXTPROC)     __getProcAddress("glTexStorageMem2DEXT");
    g_pfnGenSemaphores    = (PFNGLGENSEMAPHORESEXTPROC)        __getProcAddress("glGenSemaphoresEXT");
    g_pfnDeleteSemaphores = (PFNGLDELETESEMAPHORESEXTPROC)     __getProcAddress("glDeleteSemaphoresEXT");
    g_pfnImportSemFd      = (PFNGLIMPORTSEMAPHOREFDEXTPROC)    __getProcAddress("glImportSemaphoreFdEXT");
    g_pfnSignalSemaphore  = (PFNGLSIGNALSEMAPHOREEXTPROC)      __getProcAddress("glSignalSemaphoreEXT");
    g_pfnWaitSemaphore    = (PFNGLWAITSEMAPHOREEXTPROC)        __getProcAddress("glWaitSemaphoreEXT");
    g_pfnGenFBOs          = (PFNGLGENFRAMEBUFFERSPROC)         __getProcAddress("glGenFramebuffers");
    g_pfnDeleteFBOs       = (PFNGLDELETEFRAMEBUFFERSPROC)      __getProcAddress("glDeleteFramebuffers");
    g_pfnBindFBO          = (PFNGLBINDFRAMEBUFFERPROC)         __getProcAddress("glBindFramebuffer");
    g_pfnFBOTex2D         = (PFNGLFRAMEBUFFERTEXTURE2DPROC)    __getProcAddress("glFramebufferTexture2D");
    g_pfnBlitFBO          = (PFNGLBLITFRAMEBUFFERPROC)         __getProcAddress("glBlitFramebuffer");

    g_glInteropLoaded = (g_pfnCreateMemObjs    != nullptr &&
                         g_pfnDeleteMemObjs    != nullptr &&
                         g_pfnImportMemFd      != nullptr &&
                         g_pfnTexStorageMem2D  != nullptr &&
                         g_pfnGenSemaphores    != nullptr &&
                         g_pfnDeleteSemaphores != nullptr &&
                         g_pfnImportSemFd      != nullptr &&
                         g_pfnSignalSemaphore  != nullptr &&
                         g_pfnWaitSemaphore    != nullptr &&
                         g_pfnGenFBOs          != nullptr &&
                         g_pfnDeleteFBOs       != nullptr &&
                         g_pfnBindFBO          != nullptr &&
                         g_pfnFBOTex2D         != nullptr &&
                         g_pfnBlitFBO          != nullptr);
}

// ---- __vkDestroyHDRSurface: tear down all Vulkan + GL HDR objects ----

void __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr)
{
    if (!hdr)
        return;

    if (g_vkDevice != VK_NULL_HANDLE)
    {
        if (hdr->fences && hdr->imageCount > 0)
            vkWaitForFences(g_vkDevice, hdr->imageCount, hdr->fences, VK_TRUE, UINT64_MAX);
    }

    // Release unconsumed fds (GL never imported them)
    if (hdr->pendingMemFd >= 0) { close(hdr->pendingMemFd); hdr->pendingMemFd = -1; }
    if (hdr->pendingSemFd >= 0) { close(hdr->pendingSemFd); hdr->pendingSemFd = -1; }

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
    if (g_vkDevice != VK_NULL_HANDLE)
    {
        if (hdr->acquireSemaphore)  vkDestroySemaphore(g_vkDevice, hdr->acquireSemaphore, nullptr);
        if (hdr->glDoneSemaphore)   vkDestroySemaphore(g_vkDevice, hdr->glDoneSemaphore, nullptr);
        if (hdr->blitDoneSemaphore) vkDestroySemaphore(g_vkDevice, hdr->blitDoneSemaphore, nullptr);
        if (hdr->fences)
        {
            for (uint32_t i = 0; i < hdr->imageCount; i++)
                if (hdr->fences[i]) vkDestroyFence(g_vkDevice, hdr->fences[i], nullptr);
        }
        if (hdr->cmdBuffers && hdr->cmdPool)
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, hdr->imageCount, hdr->cmdBuffers);
        if (hdr->cmdPool)      vkDestroyCommandPool(g_vkDevice, hdr->cmdPool, nullptr);
        if (hdr->renderMemory) vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr);
        if (hdr->renderImage)  vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr);
        if (hdr->vkSwapchain)  vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr);
    }
    if (g_vkInstance != VK_NULL_HANDLE && hdr->vkSurface)
        vkDestroySurfaceKHR(g_vkInstance, hdr->vkSurface, nullptr);

    free(hdr->swapchainImages);
    free(hdr->cmdBuffers);
    free(hdr->fences);
    memset(hdr, 0, sizeof(*hdr));
    hdr->pendingMemFd = -1;
    hdr->pendingSemFd = -1;
}

// ---- __vkRecreateSwapchain: rebuild swapchain + render image on resize ----

static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore)
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
        VkSubmitInfo si = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores    = &hdr->glDoneSemaphore;
        si.pWaitDstStageMask  = &stage;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue);
    }
    else if (hdr->fences && hdr->imageCount > 0)
    {
        vkWaitForFences(g_vkDevice, hdr->imageCount, hdr->fences, VK_TRUE, UINT64_MAX);
    }

    if (hdr->blitFbo && g_pfnDeleteFBOs)           { g_pfnDeleteFBOs(1, &hdr->blitFbo); hdr->blitFbo = 0; }
    if (hdr->glTexture)                             { glDeleteTextures(1, &hdr->glTexture); hdr->glTexture = 0; }
    if (hdr->glMemoryObject && g_pfnDeleteMemObjs)  { g_pfnDeleteMemObjs(1, &hdr->glMemoryObject); hdr->glMemoryObject = 0; }
    if (hdr->glDoneSemObj && g_pfnDeleteSemaphores) { g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj); hdr->glDoneSemObj = 0; }

    if (hdr->pendingMemFd >= 0) { close(hdr->pendingMemFd); hdr->pendingMemFd = -1; }
    if (hdr->pendingSemFd >= 0) { close(hdr->pendingSemFd); hdr->pendingSemFd = -1; }

    if (hdr->renderMemory) { vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr); hdr->renderMemory = VK_NULL_HANDLE; }
    if (hdr->renderImage)  { vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr); hdr->renderImage = VK_NULL_HANDLE; }
    free(hdr->swapchainImages); hdr->swapchainImages = nullptr;

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
        VkSurfaceCapabilitiesKHR queryCaps = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, hdr->vkSurface, &queryCaps);
        newW = queryCaps.currentExtent.width;
        newH = queryCaps.currentExtent.height;
        if (newW == UINT32_MAX) newW = hdr->width;
        if (newH == UINT32_MAX) newH = hdr->height;
    }
#endif

    if (newW == 0 || newH == 0)
    {
        if (hdr->vkSwapchain) { vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr); hdr->vkSwapchain = VK_NULL_HANDLE; }
        hdr->glInteropReady = false;
        return EGL_TRUE;
    }
    hdr->width  = newW;
    hdr->height = newH;

    VkSwapchainKHR oldSwap = hdr->vkSwapchain;
    {
        VkSurfaceCapabilitiesKHR caps = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, hdr->vkSurface, &caps);
        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
        VkExtent2D extent = {newW, newH};
        if (caps.currentExtent.width != UINT32_MAX) extent = caps.currentExtent;

        VkSwapchainCreateInfoKHR swCI = {};
        swCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swCI.surface          = hdr->vkSurface;
        swCI.minImageCount    = imgCount;
        swCI.imageFormat      = hdr->vkFormat;
        swCI.imageColorSpace  = hdr->vkColorSpace;
        swCI.imageExtent      = extent;
        swCI.imageArrayLayers = 1;
        swCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swCI.preTransform     = caps.currentTransform;
        swCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swCI.clipped          = VK_TRUE;
        swCI.oldSwapchain     = oldSwap;
        if (vkCreateSwapchainKHR(g_vkDevice, &swCI, nullptr, &hdr->vkSwapchain) != VK_SUCCESS)
        {
            hdr->vkSwapchain = VK_NULL_HANDLE;
            if (oldSwap) vkDestroySwapchainKHR(g_vkDevice, oldSwap, nullptr);
            return EGL_FALSE;
        }
    }
    if (oldSwap) vkDestroySwapchainKHR(g_vkDevice, oldSwap, nullptr);

    uint32_t newImgCount = 0;
    vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &newImgCount, nullptr);
    hdr->swapchainImages = reinterpret_cast<VkImage*>(malloc(newImgCount * sizeof(VkImage)));
    if (!hdr->swapchainImages) return EGL_FALSE;
    vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &newImgCount, hdr->swapchainImages);

    if (newImgCount != hdr->imageCount)
    {
        if (hdr->cmdBuffers && hdr->cmdPool)
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, hdr->imageCount, hdr->cmdBuffers);
        free(hdr->cmdBuffers);
        if (hdr->fences)
        {
            for (uint32_t i = 0; i < hdr->imageCount; i++)
                if (hdr->fences[i]) vkDestroyFence(g_vkDevice, hdr->fences[i], nullptr);
        }
        free(hdr->fences);

        hdr->cmdBuffers = reinterpret_cast<VkCommandBuffer*>(malloc(newImgCount * sizeof(VkCommandBuffer)));
        hdr->fences     = reinterpret_cast<VkFence*>(malloc(newImgCount * sizeof(VkFence)));
        if (!hdr->cmdBuffers || !hdr->fences) return EGL_FALSE;

        VkCommandBufferAllocateInfo cbAI = {};
        cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool        = hdr->cmdPool;
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = newImgCount;
        if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, hdr->cmdBuffers) != VK_SUCCESS)
            return EGL_FALSE;

        VkFenceCreateInfo fCI = {};
        fCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < newImgCount; i++)
            if (vkCreateFence(g_vkDevice, &fCI, nullptr, &hdr->fences[i]) != VK_SUCCESS)
                return EGL_FALSE;
    }
    hdr->imageCount = newImgCount;

    // Recreate exportable render image
    {
        VkExternalMemoryImageCreateInfo emici = {};
        emici.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkImageCreateInfo imgCI = {};
        imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgCI.pNext         = &emici;
        imgCI.imageType     = VK_IMAGE_TYPE_2D;
        imgCI.format        = hdr->vkFormat;
        imgCI.extent        = {newW, newH, 1};
        imgCI.mipLevels     = 1;
        imgCI.arrayLayers   = 1;
        imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgCI.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_vkDevice, &imgCI, nullptr, &hdr->renderImage) != VK_SUCCESS)
            return EGL_FALSE;

        VkMemoryRequirements memReqs = {};
        vkGetImageMemoryRequirements(g_vkDevice, hdr->renderImage, &memReqs);
        hdr->renderMemorySize = memReqs.size;

        VkExportMemoryAllocateInfo emai = {};
        emai.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkPhysicalDeviceMemoryProperties memProps = {};
        vkGetPhysicalDeviceMemoryProperties(g_vkPhysDevice, &memProps);
        uint32_t memTypeIdx = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
            if ((memReqs.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            { memTypeIdx = i; break; }
        if (memTypeIdx == UINT32_MAX) return EGL_FALSE;

        VkMemoryAllocateInfo mai = {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext           = &emai;
        mai.allocationSize  = memReqs.size;
        mai.memoryTypeIndex = memTypeIdx;
        if (vkAllocateMemory(g_vkDevice, &mai, nullptr, &hdr->renderMemory) != VK_SUCCESS)
            return EGL_FALSE;

        vkBindImageMemory(g_vkDevice, hdr->renderImage, hdr->renderMemory, 0);
    }

    // Transition renderImage UNDEFINED → GENERAL
    {
        VkCommandBuffer initCmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cbAI = {};
        cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool        = hdr->cmdPool;
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, &initCmd) != VK_SUCCESS)
            return EGL_FALSE;

        VkCommandBufferBeginInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(initCmd, &bi);

        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = hdr->renderImage;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(initCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(initCmd);

        VkSubmitInfo si = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &initCmd;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue);
        vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
    }

    if (!g_pfnGetMemFd) return EGL_FALSE;
    {
        VkMemoryGetFdInfoKHR hInfo = {};
        hInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        hInfo.memory     = hdr->renderMemory;
        hInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetMemFd(g_vkDevice, &hInfo, &hdr->pendingMemFd) != VK_SUCCESS)
            return EGL_FALSE;
    }

    if (!g_pfnGetSemFd) return EGL_FALSE;
    {
        VkSemaphoreGetFdInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetSemFd(g_vkDevice, &shInfo, &hdr->pendingSemFd) != VK_SUCCESS || hdr->pendingSemFd < 0)
            return EGL_FALSE;
    }

    hdr->glInteropReady = false;
    return EGL_TRUE;
}

// ---- __vkCreateHDRSurface: create swapchain + GL/Vulkan interop objects ----

EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr,
                                  EGLNativeWindowType nativeWindow,
                                  EGLNativeDisplayType nativeDisplay,
                                  EGLint eglCS, uint32_t w, uint32_t h)
{
    if (!hdr || g_vkInstance == VK_NULL_HANDLE || g_vkDevice == VK_NULL_HANDLE)
        return EGL_FALSE;

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
        hdr->wlSurface = eglWin ? eglWin->surface : nullptr;
    }
#endif

    if (!_eglColorspaceToVk(eglCS, &hdr->vkFormat, &hdr->vkColorSpace))
        return EGL_FALSE;

    // 1. Create VkSurfaceKHR
#if defined(USE_X11)
    {
        VkXlibSurfaceCreateInfoKHR sci = {};
        sci.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        sci.dpy    = hdr->x11Display;
        sci.window = hdr->x11Window;
        if (vkCreateXlibSurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
            goto cleanup;
    }
#elif defined(WL_EGL_PLATFORM)
    {
        VkWaylandSurfaceCreateInfoKHR sci = {};
        sci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        sci.display = hdr->wlDisplay;
        sci.surface = hdr->wlSurface;
        if (!hdr->wlSurface || vkCreateWaylandSurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
            goto cleanup;
    }
#else
    goto cleanup;
#endif

    {
        VkBool32 presentOK = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vkPhysDevice, g_vkQueueFamily, hdr->vkSurface, &presentOK);
        if (!presentOK)
            goto cleanup;
    }

    {
        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, formats.data());
        bool found = std::any_of(formats.begin(), formats.end(),
            [hdr](const VkSurfaceFormatKHR& f){ return f.format == hdr->vkFormat && f.colorSpace == hdr->vkColorSpace; });
        if (!found)
            goto cleanup;
    }

    // 2. Create VkSwapchainKHR
    {
        VkSurfaceCapabilitiesKHR caps = {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, hdr->vkSurface, &caps);
        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
            imgCount = caps.maxImageCount;
        VkExtent2D extent = {w, h};
        if (caps.currentExtent.width != UINT32_MAX)
            extent = caps.currentExtent;

        VkSwapchainCreateInfoKHR swCI = {};
        swCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swCI.surface          = hdr->vkSurface;
        swCI.minImageCount    = imgCount;
        swCI.imageFormat      = hdr->vkFormat;
        swCI.imageColorSpace  = hdr->vkColorSpace;
        swCI.imageExtent      = extent;
        swCI.imageArrayLayers = 1;
        swCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swCI.preTransform     = caps.currentTransform;
        swCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        swCI.clipped          = VK_TRUE;
        if (vkCreateSwapchainKHR(g_vkDevice, &swCI, nullptr, &hdr->vkSwapchain) != VK_SUCCESS)
            goto cleanup;
    }

    // 3. Retrieve swapchain images
    vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &hdr->imageCount, nullptr);
    hdr->swapchainImages = reinterpret_cast<VkImage*>(malloc(hdr->imageCount * sizeof(VkImage)));
    if (!hdr->swapchainImages) goto cleanup;
    vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &hdr->imageCount, hdr->swapchainImages);

    // 4. Create command pool + command buffers + fences
    {
        VkCommandPoolCreateInfo cpCI = {};
        cpCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpCI.queueFamilyIndex = g_vkQueueFamily;
        cpCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(g_vkDevice, &cpCI, nullptr, &hdr->cmdPool) != VK_SUCCESS)
            goto cleanup;

        hdr->cmdBuffers = reinterpret_cast<VkCommandBuffer*>(malloc(hdr->imageCount * sizeof(VkCommandBuffer)));
        hdr->fences     = reinterpret_cast<VkFence*>(malloc(hdr->imageCount * sizeof(VkFence)));
        if (!hdr->cmdBuffers || !hdr->fences) goto cleanup;

        VkCommandBufferAllocateInfo cbAI = {};
        cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool        = hdr->cmdPool;
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = hdr->imageCount;
        if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, hdr->cmdBuffers) != VK_SUCCESS)
            goto cleanup;

        VkFenceCreateInfo fCI = {};
        fCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < hdr->imageCount; i++)
            if (vkCreateFence(g_vkDevice, &fCI, nullptr, &hdr->fences[i]) != VK_SUCCESS)
                goto cleanup;
    }

    // 5. Create exportable interop VkImage + allocate exportable memory
    {
        VkExternalMemoryImageCreateInfo emici = {};
        emici.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkImageCreateInfo imgCI = {};
        imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgCI.pNext         = &emici;
        imgCI.imageType     = VK_IMAGE_TYPE_2D;
        imgCI.format        = hdr->vkFormat;
        imgCI.extent        = {w, h, 1};
        imgCI.mipLevels     = 1;
        imgCI.arrayLayers   = 1;
        imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgCI.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_vkDevice, &imgCI, nullptr, &hdr->renderImage) != VK_SUCCESS)
            goto cleanup;

        VkMemoryRequirements memReqs = {};
        vkGetImageMemoryRequirements(g_vkDevice, hdr->renderImage, &memReqs);
        hdr->renderMemorySize = memReqs.size;

        VkExportMemoryAllocateInfo emai = {};
        emai.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

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
            goto cleanup;

        VkMemoryAllocateInfo mai = {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext           = &emai;
        mai.allocationSize  = memReqs.size;
        mai.memoryTypeIndex = memTypeIdx;
        if (vkAllocateMemory(g_vkDevice, &mai, nullptr, &hdr->renderMemory) != VK_SUCCESS)
            goto cleanup;

        vkBindImageMemory(g_vkDevice, hdr->renderImage, hdr->renderMemory, 0);
    }

    // 6. Transition renderImage from UNDEFINED to GENERAL
    {
        VkCommandBuffer initCmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cbAI = {};
        cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAI.commandPool        = hdr->cmdPool;
        cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAI.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, &initCmd) != VK_SUCCESS)
            goto cleanup;

        VkCommandBufferBeginInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(initCmd, &bi);

        VkImageMemoryBarrier barrier = {};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = hdr->renderImage;
        barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(initCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(initCmd);

        VkSubmitInfo si = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &initCmd;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue);
        vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, 1, &initCmd);
    }

    // 7. Export renderImage memory as fd
    {
        if (!g_pfnGetMemFd)
            goto cleanup;

        VkMemoryGetFdInfoKHR hInfo = {};
        hInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        hInfo.memory     = hdr->renderMemory;
        hInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetMemFd(g_vkDevice, &hInfo, &hdr->pendingMemFd) != VK_SUCCESS)
            goto cleanup;
    }

    // 8. Create semaphores
    {
        VkExportSemaphoreCreateInfo esci = {};
        esci.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        esci.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

        VkSemaphoreCreateInfo semCI = {};
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semCI.pNext = &esci;
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->glDoneSemaphore) != VK_SUCCESS)
            goto cleanup;

        if (!g_pfnGetSemFd)
            goto cleanup;
        VkSemaphoreGetFdInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (g_pfnGetSemFd(g_vkDevice, &shInfo, &hdr->pendingSemFd) != VK_SUCCESS || hdr->pendingSemFd < 0)
            goto cleanup;

        semCI.pNext = nullptr;
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->acquireSemaphore) != VK_SUCCESS)
            goto cleanup;
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->blitDoneSemaphore) != VK_SUCCESS)
            goto cleanup;
    }

    return EGL_TRUE;

cleanup:
    __vkDestroyHDRSurface(hdr);
    return EGL_FALSE;
}

// ---- __vkInitGLSide: lazily create GL interop objects (called on first present) ----

static bool __vkInitGLSide(NativeHDRSurfaceContainer* hdr)
{
    __vkLoadGLInterop();
    if (!g_glInteropLoaded)
        return false;

    g_pfnCreateMemObjs(1, &hdr->glMemoryObject);
    // FD is consumed by the driver on import; set to -1 so destroy does not double-close.
    g_pfnImportMemFd(hdr->glMemoryObject, hdr->renderMemorySize,
                     GL_HANDLE_TYPE_OPAQUE_FD_EXT, hdr->pendingMemFd);
    hdr->pendingMemFd = -1;

    glGenTextures(1, &hdr->glTexture);
    glBindTexture(GL_TEXTURE_2D, hdr->glTexture);
    GLenum glFmt = (hdr->vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
                   ? 0x881Au  // GL_RGBA16F
                   : 0x8059u; // GL_RGB10_A2
    g_pfnTexStorageMem2D(GL_TEXTURE_2D, 1, glFmt,
                         static_cast<GLsizei>(hdr->width), static_cast<GLsizei>(hdr->height),
                         hdr->glMemoryObject, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_pfnGenFBOs(1, &hdr->blitFbo);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, hdr->blitFbo);
    g_pfnFBOTex2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                  GL_TEXTURE_2D, hdr->glTexture, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    g_pfnGenSemaphores(1, &hdr->glDoneSemObj);
    // FD is consumed by the driver on import.
    g_pfnImportSemFd(hdr->glDoneSemObj, GL_HANDLE_TYPE_OPAQUE_FD_EXT, hdr->pendingSemFd);
    hdr->pendingSemFd = -1;

    hdr->glInteropReady = true;
    return true;
}

// ---- __vkPresent: blit GL default FBO → interop image → swapchain, then present ----

EGLBoolean __vkPresent(NativeHDRSurfaceContainer* hdr)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE)
        return EGL_FALSE;

    if (!hdr->vkSwapchain)
        return EGL_TRUE;

    if (!hdr->glInteropReady && !__vkInitGLSide(hdr))
        return EGL_FALSE;

    // Step 1: blit from default GL framebuffer (FBO 0) to interop texture
    g_pfnBindFBO(GL_READ_FRAMEBUFFER, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, hdr->blitFbo);
    g_pfnBlitFBO(0, 0, static_cast<GLint>(hdr->width), static_cast<GLint>(hdr->height),
                  0, 0, static_cast<GLint>(hdr->width), static_cast<GLint>(hdr->height),
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_pfnBindFBO(GL_READ_FRAMEBUFFER, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    // Step 2: GL signals semaphore to notify Vulkan
    GLenum dstLayout = GL_LAYOUT_GENERAL_EXT;
    g_pfnSignalSemaphore(hdr->glDoneSemObj, 0, nullptr, 1, &hdr->glTexture, &dstLayout);
    if (glFinish_PTR) glFinish_PTR();

    // Step 3: Acquire next swapchain image
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(g_vkDevice, hdr->vkSwapchain, UINT64_MAX,
                                          hdr->acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        __vkRecreateSwapchain(hdr, hdr->glInteropReady);
        return EGL_TRUE;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        return EGL_FALSE;
    bool needsRecreate = (res == VK_SUBOPTIMAL_KHR);

    // Step 4: Record + submit blit command buffer
    vkWaitForFences(g_vkDevice, 1, &hdr->fences[imageIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(g_vkDevice, 1, &hdr->fences[imageIndex]);
    vkResetCommandBuffer(hdr->cmdBuffers[imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(hdr->cmdBuffers[imageIndex], &beginInfo);

    auto barrier = [&](VkImage img, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b = {};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask       = srcAccess;
        b.dstAccessMask       = dstAccess;
        b.oldLayout           = oldLayout;
        b.newLayout           = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(hdr->cmdBuffers[imageIndex], srcStage, dstStage,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(hdr->renderImage,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    barrier(hdr->swapchainImages[imageIndex],
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blitRegion = {};
    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.srcOffsets[1]  = {(int32_t)hdr->width, (int32_t)hdr->height, 1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[1]  = {(int32_t)hdr->width, (int32_t)hdr->height, 1};
    vkCmdBlitImage(hdr->cmdBuffers[imageIndex],
                   hdr->renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   hdr->swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blitRegion, VK_FILTER_NEAREST);

    barrier(hdr->swapchainImages[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    barrier(hdr->renderImage,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    vkEndCommandBuffer(hdr->cmdBuffers[imageIndex]);

    VkSemaphore waitSems[]        = {hdr->acquireSemaphore, hdr->glDoneSemaphore};
    VkPipelineStageFlags stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSubmitInfo submitInfo = {};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 2;
    submitInfo.pWaitSemaphores      = waitSems;
    submitInfo.pWaitDstStageMask    = stages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &hdr->cmdBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &hdr->blitDoneSemaphore;
    vkQueueSubmit(g_vkQueue, 1, &submitInfo, hdr->fences[imageIndex]);

    // Step 5: Present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &hdr->blitDoneSemaphore;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &hdr->vkSwapchain;
    presentInfo.pImageIndices      = &imageIndex;
    res = vkQueuePresentKHR(g_vkQueue, &presentInfo);
    if (res == VK_SUBOPTIMAL_KHR)     needsRecreate = true;
    if (res == VK_ERROR_OUT_OF_DATE_KHR) needsRecreate = true;

    if (needsRecreate)
        __vkRecreateSwapchain(hdr, false);

    return (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
           ? EGL_TRUE : EGL_FALSE;
}

bool __vkIsReady()
{
    return g_vkDevice != VK_NULL_HANDLE;
}

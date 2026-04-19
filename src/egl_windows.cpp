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

#include "egl_internal.h"
#include "../../EGL/include/EGL/eglctxinternals.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <EGL/eglext.h>

HMODULE opengl32dll = NULL;

typedef void(*__PFN_glFinish)();
typedef void* (*__PFN_glFenceSync)(GLenum condition, GLbitfield flags);
typedef void  (*__PFN_glDeleteSync)(void* sync);
typedef GLenum(*__PFN_glClientWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glGetSynciv)(void* sync, GLenum pname, GLsizei count, GLsizei* length, GLint* values);

typedef HGLRC(__stdcall *__PFN_wglCreateContext)(HDC);
typedef BOOL(__stdcall *__PFN_wglDeleteContext)(HGLRC);
typedef BOOL(__stdcall *__PFN_wglMakeCurrent)(HDC,HGLRC);
typedef PROC(__stdcall *__PFN_wglGetProcAddress)(LPCSTR);

__PFN_glFinish glFinish_PTR = NULL;
__PFN_glFenceSync glFenceSync_PTR = NULL;
__PFN_glDeleteSync glDeleteSync_PTR = NULL;
__PFN_glClientWaitSync glClientWaitSync_PTR = NULL;
__PFN_glWaitSync glWaitSync_PTR = NULL;
__PFN_glGetSynciv glGetSynciv_PTR = NULL;

__PFN_wglCreateContext wglCreateContext_PTR = NULL;
__PFN_wglDeleteContext wglDeleteContext_PTR = NULL;
__PFN_wglMakeCurrent wglMakeCurrent_PTR = NULL;
__PFN_wglGetProcAddress wglGetProcAddress_PTR = NULL;

PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = NULL;
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;

PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB = NULL;
PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB = NULL;
PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB = NULL;
PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB = NULL;
PFNWGLBINDTEXIMAGEARBPROC wglBindTexImageARB_PTR = NULL;
PFNWGLRELEASETEXIMAGEARBPROC wglReleaseTexImageARB_PTR = NULL;


// ============================================================
// Vulkan HDR backend (Windows-only)
// ============================================================

// ---- Vulkan function pointer types needed for extensions ----
typedef VkResult (VKAPI_PTR *PFN_vkSetHdrMetadataEXT_t)(VkDevice, uint32_t, const VkSwapchainKHR*, const VkHdrMetadataEXT*);
typedef VkResult (VKAPI_PTR *PFN_vkGetMemoryWin32HandleKHR_t)(VkDevice, const VkMemoryGetWin32HandleInfoKHR*, HANDLE*);
typedef VkResult (VKAPI_PTR *PFN_vkGetSemaphoreWin32HandleKHR_t)(VkDevice, const VkSemaphoreGetWin32HandleInfoKHR*, HANDLE*);

// ---- GL interop function pointer types ----
typedef void (APIENTRY* PFNGLCREATEMEMORYOBJECTSEXTPROC)(GLsizei n, GLuint* memoryObjects);
typedef void (APIENTRY* PFNGLDELETEMEMORYOBJECTSEXTPROC)(GLsizei n, const GLuint* memoryObjects);
typedef void (APIENTRY* PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)(GLuint memory, unsigned long long size, GLenum handleType, void* handle);
typedef void (APIENTRY* PFNGLTEXSTORAGEMEM2DEXTPROC)(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLuint memory, unsigned long long offset);
typedef void (APIENTRY* PFNGLGENSEMAPHORESEXTPROC)(GLsizei n, GLuint* semaphores);
typedef void (APIENTRY* PFNGLDELETESEMAPHORESEXTPROC)(GLsizei n, const GLuint* semaphores);
typedef void (APIENTRY* PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC)(GLuint semaphore, GLenum handleType, void* handle);
typedef void (APIENTRY* PFNGLSIGNALSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* dstLayouts);
typedef void (APIENTRY* PFNGLWAITSEMAPHOREEXTPROC)(GLuint semaphore, GLuint numBufferBarriers, const GLuint* buffers, GLuint numTextureBarriers, const GLuint* textures, const GLenum* srcLayouts);

// GL FBO function pointer types (GL 3.0 core, may need proc address on Windows)
typedef void (APIENTRY* PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (APIENTRY* PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void (APIENTRY* PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRY* PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

// GL constant definitions needed for interop
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT   0x9587u
#define GL_LAYOUT_GENERAL_EXT             0x958Du
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT    0x958Eu
#define GL_READ_FRAMEBUFFER               0x8CA8u
#define GL_DRAW_FRAMEBUFFER               0x8CA9u
#define GL_COLOR_ATTACHMENT0              0x8CE0u

// ---- Vulkan singleton globals (one device shared across all displays) ----
static VkInstance                   g_vkInstance     = VK_NULL_HANDLE;
static VkPhysicalDevice             g_vkPhysDevice   = VK_NULL_HANDLE;
static VkDevice                     g_vkDevice       = VK_NULL_HANDLE;
static uint32_t                     g_vkQueueFamily  = UINT32_MAX;
static VkQueue                      g_vkQueue        = VK_NULL_HANDLE;
static PFN_vkSetHdrMetadataEXT_t    g_pfnSetHdrMetadata   = nullptr;
static PFN_vkGetMemoryWin32HandleKHR_t g_pfnGetMemWin32  = nullptr;
static PFN_vkGetSemaphoreWin32HandleKHR_t g_pfnGetSemWin32 = nullptr;

// ---- GL interop function pointers (loaded once after first GL context) ----
static PFNGLCREATEMEMORYOBJECTSEXTPROC         g_pfnCreateMemObjs    = nullptr;
static PFNGLDELETEMEMORYOBJECTSEXTPROC         g_pfnDeleteMemObjs    = nullptr;
static PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC     g_pfnImportMemWin32   = nullptr;
static PFNGLTEXSTORAGEMEM2DEXTPROC             g_pfnTexStorageMem2D  = nullptr;
static PFNGLGENSEMAPHORESEXTPROC               g_pfnGenSemaphores    = nullptr;
static PFNGLDELETESEMAPHORESEXTPROC            g_pfnDeleteSemaphores = nullptr;
static PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC  g_pfnImportSemWin32   = nullptr;
static PFNGLSIGNALSEMAPHOREEXTPROC             g_pfnSignalSemaphore  = nullptr;
static PFNGLWAITSEMAPHOREEXTPROC               g_pfnWaitSemaphore    = nullptr;
static PFNGLGENFRAMEBUFFERSPROC                g_pfnGenFBOs          = nullptr;
static PFNGLDELETEFRAMEBUFFERSPROC             g_pfnDeleteFBOs       = nullptr;
static PFNGLBINDFRAMEBUFFERPROC                g_pfnBindFBO          = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC           g_pfnFBOTex2D         = nullptr;
static PFNGLBLITFRAMEBUFFERPROC                g_pfnBlitFBO          = nullptr;
static bool                                    g_glInteropLoaded     = false;

// ---- NativeHDRSurfaceContainer (full definition) ----
struct _NativeHDRSurfaceContainer {
    VkSurfaceKHR     vkSurface;
    VkSwapchainKHR   vkSwapchain;
    VkFormat         vkFormat;
    VkColorSpaceKHR  vkColorSpace;
    uint32_t         imageCount;
    VkImage*         swapchainImages;
    VkCommandPool    cmdPool;
    VkCommandBuffer* cmdBuffers;
    VkFence*         fences;

    VkImage          renderImage;
    VkDeviceMemory   renderMemory;
    GLuint           glTexture;
    GLuint           glMemoryObject;
    GLuint           blitFbo;

    VkSemaphore      acquireSemaphore;
    VkSemaphore      glDoneSemaphore;
    VkSemaphore      blitDoneSemaphore;
    GLuint           glDoneSemObj;

    uint32_t         width;
    uint32_t         height;
    VkDeviceSize     renderMemorySize;  // cached from vkGetImageMemoryRequirements, used by GL import
    HWND             hwnd;              // owning Win32 window, used for swapchain recreation on resize

    // GL-side objects are created lazily on first present (GL context must be current)
    bool             glInteropReady;
    HANDLE           pendingMemHandle;  // exported Vk memory handle, pending GL import
    HANDLE           pendingSemHandle;  // exported Vk semaphore handle, pending GL import
};

// Make the typedef match the forward declaration in egl_internal.h
typedef struct _NativeHDRSurfaceContainer NativeHDRSurfaceContainer;

// ---- Helpers ----

static bool _eglHDRColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs)
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
        default: return 0;
    }
}

// Forward declaration (destroy is called inside create's error path)
static void __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr);
// Forward declaration (called from __vkPresent on OUT_OF_DATE / SUBOPTIMAL)
static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore);

// ---- __vkInit: create VkInstance + VkDevice (called once from __internalInit) ----
static EGLBoolean __vkInit()
{
    if (g_vkInstance != VK_NULL_HANDLE)
        return EGL_TRUE;

    const char* instExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
    };

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "EGL";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instCI = {};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount = (uint32_t)(sizeof(instExts) / sizeof(instExts[0]));
    instCI.ppEnabledExtensionNames = instExts;

    if (vkCreateInstance(&instCI, nullptr, &g_vkInstance) != VK_SUCCESS)
        return EGL_FALSE;

    // Pick discrete GPU, fallback to first device
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

    // Find graphics queue family
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vkPhysDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vkPhysDevice, &qfCount, qfProps.data());

    g_vkQueueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++)
    {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            g_vkQueueFamily = i;
            break;
        }
    }
    if (g_vkQueueFamily == UINT32_MAX)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }

    // Check available device extensions, enable the ones we need
    const char* wantedDevExts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        VK_EXT_HDR_METADATA_EXTENSION_NAME,
    };

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(g_vkPhysDevice, nullptr, &extCount, availExts.data());

    std::vector<const char*> enabledDevExts;
    for (auto req : wantedDevExts)
    {
        for (auto& avail : availExts)
        {
            if (strcmp(req, avail.extensionName) == 0)
            {
                enabledDevExts.push_back(req);
                break;
            }
        }
    }

    float qPriority = 1.0f;
    VkDeviceQueueCreateInfo qCI = {};
    qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = g_vkQueueFamily;
    qCI.queueCount = 1;
    qCI.pQueuePriorities = &qPriority;

    VkDeviceCreateInfo devCI = {};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &qCI;
    devCI.enabledExtensionCount = (uint32_t)enabledDevExts.size();
    devCI.ppEnabledExtensionNames = enabledDevExts.data();

    if (vkCreateDevice(g_vkPhysDevice, &devCI, nullptr, &g_vkDevice) != VK_SUCCESS)
    {
        vkDestroyInstance(g_vkInstance, nullptr);
        g_vkInstance = VK_NULL_HANDLE;
        return EGL_FALSE;
    }

    vkGetDeviceQueue(g_vkDevice, g_vkQueueFamily, 0, &g_vkQueue);

    // Load device extension function pointers
    g_pfnSetHdrMetadata = (PFN_vkSetHdrMetadataEXT_t)vkGetDeviceProcAddr(g_vkDevice, "vkSetHdrMetadataEXT");
    g_pfnGetMemWin32    = (PFN_vkGetMemoryWin32HandleKHR_t)vkGetDeviceProcAddr(g_vkDevice, "vkGetMemoryWin32HandleKHR");
    g_pfnGetSemWin32    = (PFN_vkGetSemaphoreWin32HandleKHR_t)vkGetDeviceProcAddr(g_vkDevice, "vkGetSemaphoreWin32HandleKHR");

    return EGL_TRUE;
}

// ---- __vkTerm: destroy VkDevice + VkInstance ----
static void __vkTerm()
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
    g_vkPhysDevice  = VK_NULL_HANDLE;
    g_vkQueueFamily = UINT32_MAX;
    g_vkQueue       = VK_NULL_HANDLE;
    g_pfnSetHdrMetadata = nullptr;
    g_pfnGetMemWin32    = nullptr;
    g_pfnGetSemWin32    = nullptr;
}

// ---- __vkQueryHDRColorspaces: query HDR formats for a given HWND ----
static uint32_t __vkQueryHDRColorspaces(HWND hwnd)
{
    if (g_vkInstance == VK_NULL_HANDLE)
        return 0;

    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandle(nullptr);
    sci.hwnd      = hwnd;

    VkSurfaceKHR tmpSurface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
        return 0;

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, tmpSurface, &fmtCount, formats.data());

    // Check the exact format+colorspace pairs we actually request at swapchain creation time.
    // A colorspace advertised with a different format would fail in __vkCreateHDRSurface.
    struct HdrEntry { VkFormat fmt; VkColorSpaceKHR cs; uint32_t bit; };
    static const HdrEntry k_entries[] = {
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,    EGL_HDR_CS_SCRGB_LINEAR_BIT  },
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT, EGL_HDR_CS_SCRGB_BIT         },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT,            EGL_HDR_CS_BT2020_PQ_BIT     },
        { VK_FORMAT_R16G16B16A16_SFLOAT,      VK_COLOR_SPACE_BT2020_LINEAR_EXT,           EGL_HDR_CS_BT2020_LINEAR_BIT },
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT,               EGL_HDR_CS_BT2020_HLG_BIT    },
    };

    // Get surface capabilities for swapchain test.
    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vkPhysDevice, tmpSurface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX || extent.width == 0)  extent.width  = 256;
    if (extent.height == UINT32_MAX || extent.height == 0) extent.height = 256;
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    uint32_t bits = 0;
    for (auto& entry : k_entries)
    {
        // First check that the driver lists this exact format+colorspace pair.
        bool listed = false;
        for (auto& f : formats)
            if (f.format == entry.fmt && f.colorSpace == entry.cs) { listed = true; break; }
        if (!listed)
            continue;

        // Confirm by attempting a real test swapchain — some drivers list pairs they
        // can't actually create (e.g. NVIDIA lists BT2020_LINEAR but fails on creation).
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

// ---- __vkLoadGLInterop: load GL interop + FBO function pointers ----
static void __vkLoadGLInterop()
{
    if (g_glInteropLoaded)
        return;

    g_pfnCreateMemObjs   = (PFNGLCREATEMEMORYOBJECTSEXTPROC)       __getProcAddress("glCreateMemoryObjectsEXT");
    g_pfnDeleteMemObjs   = (PFNGLDELETEMEMORYOBJECTSEXTPROC)       __getProcAddress("glDeleteMemoryObjectsEXT");
    g_pfnImportMemWin32  = (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)   __getProcAddress("glImportMemoryWin32HandleEXT");
    g_pfnTexStorageMem2D = (PFNGLTEXSTORAGEMEM2DEXTPROC)           __getProcAddress("glTexStorageMem2DEXT");
    g_pfnGenSemaphores   = (PFNGLGENSEMAPHORESEXTPROC)             __getProcAddress("glGenSemaphoresEXT");
    g_pfnDeleteSemaphores= (PFNGLDELETESEMAPHORESEXTPROC)          __getProcAddress("glDeleteSemaphoresEXT");
    g_pfnImportSemWin32  = (PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC)__getProcAddress("glImportSemaphoreWin32HandleEXT");
    g_pfnSignalSemaphore = (PFNGLSIGNALSEMAPHOREEXTPROC)           __getProcAddress("glSignalSemaphoreEXT");
    g_pfnWaitSemaphore   = (PFNGLWAITSEMAPHOREEXTPROC)             __getProcAddress("glWaitSemaphoreEXT");
    g_pfnGenFBOs         = (PFNGLGENFRAMEBUFFERSPROC)              __getProcAddress("glGenFramebuffers");
    g_pfnDeleteFBOs      = (PFNGLDELETEFRAMEBUFFERSPROC)           __getProcAddress("glDeleteFramebuffers");
    g_pfnBindFBO         = (PFNGLBINDFRAMEBUFFERPROC)              __getProcAddress("glBindFramebuffer");
    g_pfnFBOTex2D        = (PFNGLFRAMEBUFFERTEXTURE2DPROC)         __getProcAddress("glFramebufferTexture2D");
    g_pfnBlitFBO         = (PFNGLBLITFRAMEBUFFERPROC)              __getProcAddress("glBlitFramebuffer");

    g_glInteropLoaded = (g_pfnCreateMemObjs   != nullptr &&
                         g_pfnImportMemWin32  != nullptr &&
                         g_pfnTexStorageMem2D != nullptr &&
                         g_pfnDeleteMemObjs   != nullptr &&
                         g_pfnGenSemaphores   != nullptr &&
                         g_pfnImportSemWin32  != nullptr &&
                         g_pfnSignalSemaphore != nullptr &&
                         g_pfnWaitSemaphore   != nullptr &&
                         g_pfnDeleteSemaphores!= nullptr &&
                         g_pfnGenFBOs         != nullptr &&
                         g_pfnBindFBO         != nullptr &&
                         g_pfnFBOTex2D        != nullptr &&
                         g_pfnBlitFBO         != nullptr);
}

// ---- __vkDestroyHDRSurface: tear down all Vulkan + GL HDR objects ----
static void __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr)
{
    if (!hdr)
        return;

    if (g_vkDevice != VK_NULL_HANDLE)
    {
        // Wait only for this surface's own fences — avoids stalling the whole device
        if (hdr->fences && hdr->imageCount > 0)
            vkWaitForFences(g_vkDevice, hdr->imageCount, hdr->fences, VK_TRUE, UINT64_MAX);
    }

    // GL cleanup (requires a GL context to be current; best-effort)
    if (hdr->pendingMemHandle) { CloseHandle(hdr->pendingMemHandle); hdr->pendingMemHandle = nullptr; }
    if (hdr->pendingSemHandle) { CloseHandle(hdr->pendingSemHandle); hdr->pendingSemHandle = nullptr; }
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
}

// ---- __vkRecreateSwapchain: rebuild swapchain + render image on resize ----
// drainGLSemaphore: true when called after GL has signaled glDoneSemaphore but
// before Vulkan consumed it (i.e. vkAcquireNextImageKHR returned OUT_OF_DATE).
static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE || !hdr->hwnd)
        return EGL_FALSE;

    // Drain the GL-signaled semaphore before touching Vulkan objects; then wait
    // for all per-image fences to ensure no command buffer is still in flight.
    if (drainGLSemaphore && hdr->glDoneSemaphore != VK_NULL_HANDLE)
    {
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo si = {};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores    = &hdr->glDoneSemaphore;
        si.pWaitDstStageMask  = &stage;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue); // also implicitly covers all in-flight fences
    }
    else if (hdr->fences && hdr->imageCount > 0)
    {
        vkWaitForFences(g_vkDevice, hdr->imageCount, hdr->fences, VK_TRUE, UINT64_MAX);
    }

    // Destroy GL-side objects tied to the old render image dimensions.
    // Keep glDoneSemaphore (VkSemaphore) and glDoneSemObj — we'll re-export and re-import.
    if (hdr->blitFbo && g_pfnDeleteFBOs)           { g_pfnDeleteFBOs(1, &hdr->blitFbo); hdr->blitFbo = 0; }
    if (hdr->glTexture)                             { glDeleteTextures(1, &hdr->glTexture); hdr->glTexture = 0; }
    if (hdr->glMemoryObject && g_pfnDeleteMemObjs)  { g_pfnDeleteMemObjs(1, &hdr->glMemoryObject); hdr->glMemoryObject = 0; }
    if (hdr->glDoneSemObj && g_pfnDeleteSemaphores) { g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj); hdr->glDoneSemObj = 0; }

    // Close any unconsumed pending Win32 handles from the old allocation.
    if (hdr->pendingMemHandle) { CloseHandle(hdr->pendingMemHandle); hdr->pendingMemHandle = nullptr; }
    if (hdr->pendingSemHandle) { CloseHandle(hdr->pendingSemHandle); hdr->pendingSemHandle = nullptr; }

    // Destroy old render image and memory (size is changing).
    if (hdr->renderMemory) { vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr); hdr->renderMemory = VK_NULL_HANDLE; }
    if (hdr->renderImage)  { vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr); hdr->renderImage = VK_NULL_HANDLE; }
    free(hdr->swapchainImages); hdr->swapchainImages = nullptr;

    // Query new client area.
    RECT cr = {};
    GetClientRect(hdr->hwnd, &cr);
    uint32_t newW = (uint32_t)(cr.right  - cr.left);
    uint32_t newH = (uint32_t)(cr.bottom - cr.top);
    if (newW == 0 || newH == 0)
    {
        // Minimized: retire the stale swapchain; present will be skipped until restored.
        if (hdr->vkSwapchain) { vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr); hdr->vkSwapchain = VK_NULL_HANDLE; }
        hdr->glInteropReady = false;
        return EGL_TRUE;
    }
    hdr->width  = newW;
    hdr->height = newH;

    // Recreate swapchain, passing the old one for efficient resource recycling.
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

    // Retrieve new swapchain images; reallocate command buffers + fences if count changed.
    uint32_t newImgCount = 0;
    vkGetSwapchainImagesKHR(g_vkDevice, hdr->vkSwapchain, &newImgCount, nullptr);
    hdr->swapchainImages = (VkImage*)malloc(newImgCount * sizeof(VkImage));
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

        hdr->cmdBuffers = (VkCommandBuffer*)malloc(newImgCount * sizeof(VkCommandBuffer));
        hdr->fences     = (VkFence*)malloc(newImgCount * sizeof(VkFence));
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

    // Recreate render image at new dimensions.
    {
        VkExternalMemoryImageCreateInfo emici = {};
        emici.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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
        emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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

    // Transition renderImage UNDEFINED → GENERAL.
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

    // Export new Win32 memory handle for deferred GL import.
    if (!g_pfnGetMemWin32) return EGL_FALSE;
    {
        VkMemoryGetWin32HandleInfoKHR hInfo = {};
        hInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        hInfo.memory     = hdr->renderMemory;
        hInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetMemWin32(g_vkDevice, &hInfo, &hdr->pendingMemHandle) != VK_SUCCESS)
            return EGL_FALSE;
    }

    // Re-export semaphore Win32 handle from the existing glDoneSemaphore.
    // The previous handle was consumed by GL import; vkGetSemaphoreWin32HandleKHR
    // returns a fresh HANDLE to the same underlying NT semaphore object.
    if (!g_pfnGetSemWin32) return EGL_FALSE;
    {
        VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
            return EGL_FALSE;
    }

    // Reset GL interop flag: __vkInitGLSide will recreate GL objects on next present.
    hdr->glInteropReady = false;
    return EGL_TRUE;
}

// ---- __vkCreateHDRSurface: create swapchain + GL/Vulkan interop objects ----
static EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr, HWND win, EGLint eglCS, uint32_t w, uint32_t h)
{
    if (!hdr || g_vkInstance == VK_NULL_HANDLE || g_vkDevice == VK_NULL_HANDLE)
        return EGL_FALSE;

    memset(hdr, 0, sizeof(*hdr));
    hdr->width  = w;
    hdr->height = h;
    hdr->hwnd   = win;

    if (!_eglHDRColorspaceToVk(eglCS, &hdr->vkFormat, &hdr->vkColorSpace))
        return EGL_FALSE;

    // 1. Create VkSurfaceKHR
    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandle(nullptr);
    sci.hwnd      = win;
    if (vkCreateWin32SurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
        goto cleanup;

    // Check present support
    {
        VkBool32 presentOK = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vkPhysDevice, g_vkQueueFamily, hdr->vkSurface, &presentOK);
        if (!presentOK)
            goto cleanup;
    }

    // Verify the requested color space is available
    {
        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_vkPhysDevice, hdr->vkSurface, &fmtCount, formats.data());
        bool found = false;
        for (auto& f : formats)
            if (f.format == hdr->vkFormat && f.colorSpace == hdr->vkColorSpace) { found = true; break; }
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
    hdr->swapchainImages = (VkImage*)malloc(hdr->imageCount * sizeof(VkImage));
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

        hdr->cmdBuffers = (VkCommandBuffer*)malloc(hdr->imageCount * sizeof(VkCommandBuffer));
        hdr->fences     = (VkFence*)malloc(hdr->imageCount * sizeof(VkFence));
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
        emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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
        emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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

    // 6. Transition renderImage from UNDEFINED to GENERAL (initial layout for GL interop)
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

    // 7. Export renderImage memory as Win32 handle (GL import deferred to first present)
    {
        if (!g_pfnGetMemWin32)
            goto cleanup;

        VkMemoryGetWin32HandleInfoKHR hInfo = {};
        hInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        hInfo.memory     = hdr->renderMemory;
        hInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetMemWin32(g_vkDevice, &hInfo, &hdr->pendingMemHandle) != VK_SUCCESS)
            goto cleanup;
    }

    // 8. Create blit FBO — deferred to __vkInitGLSide (requires a current GL context)

    // 9. Create semaphores: export glDoneSemaphore handle for deferred GL import
    {
        VkExportSemaphoreCreateInfo esci = {};
        esci.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        esci.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        VkSemaphoreCreateInfo semCI = {};
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semCI.pNext = &esci;
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->glDoneSemaphore) != VK_SUCCESS)
            goto cleanup;

        // Export Win32 handle — GL import is deferred to __vkInitGLSide
        if (!g_pfnGetSemWin32)
            goto cleanup;
        VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
            goto cleanup;

        // Non-exported semaphores for acquire + blit-to-present sync
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

    // Import Vulkan memory into a GL memory object
    g_pfnCreateMemObjs(1, &hdr->glMemoryObject);
    g_pfnImportMemWin32(hdr->glMemoryObject, hdr->renderMemorySize,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingMemHandle);
    hdr->pendingMemHandle = nullptr; // consumed by import

    // Create interop texture backed by the imported memory
    glGenTextures(1, &hdr->glTexture);
    glBindTexture(GL_TEXTURE_2D, hdr->glTexture);
    GLenum glFmt = (hdr->vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
                   ? 0x881A  // GL_RGBA16F
                   : 0x8059; // GL_RGB10_A2
    g_pfnTexStorageMem2D(GL_TEXTURE_2D, 1, glFmt,
                         (GLsizei)hdr->width, (GLsizei)hdr->height,
                         hdr->glMemoryObject, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create blit FBO
    g_pfnGenFBOs(1, &hdr->blitFbo);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, hdr->blitFbo);
    g_pfnFBOTex2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                  GL_TEXTURE_2D, hdr->glTexture, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    // Import Vulkan semaphore into GL
    g_pfnGenSemaphores(1, &hdr->glDoneSemObj);
    g_pfnImportSemWin32(hdr->glDoneSemObj,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingSemHandle);
    hdr->pendingSemHandle = nullptr; // consumed by import

    hdr->glInteropReady = true;
    return true;
}

// ---- __vkPresent: blit GL default FBO to interop image to swapchain, then present ----
static EGLBoolean __vkPresent(NativeHDRSurfaceContainer* hdr)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE)
        return EGL_FALSE;

    // No swapchain: window was minimized during a previous recreation.
    if (!hdr->vkSwapchain)
        return EGL_TRUE;

    // Lazily initialise GL interop objects on first call (requires a current GL context)
    if (!hdr->glInteropReady && !__vkInitGLSide(hdr))
        return EGL_FALSE;

    // Step 1: blit from default GL framebuffer (FBO 0) to interop texture
    g_pfnBindFBO(GL_READ_FRAMEBUFFER, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, hdr->blitFbo);
    g_pfnBlitFBO(0, 0, (GLint)hdr->width, (GLint)hdr->height,
                  0, 0, (GLint)hdr->width, (GLint)hdr->height,
                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_pfnBindFBO(GL_READ_FRAMEBUFFER, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    // Step 2: GL signals semaphore - releases renderImage to Vulkan with GENERAL layout
    GLenum dstLayout = GL_LAYOUT_GENERAL_EXT;
    g_pfnSignalSemaphore(hdr->glDoneSemObj, 0, nullptr, 1, &hdr->glTexture, &dstLayout);
    if (glFinish_PTR) glFinish_PTR();

    // Step 3: Acquire next swapchain image
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(g_vkDevice, hdr->vkSwapchain, UINT64_MAX,
                                          hdr->acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // glDoneSemaphore was signaled by GL (step 2) but never consumed — drain it.
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

    // renderImage: GENERAL to TRANSFER_SRC
    barrier(hdr->renderImage,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // swapchain image: UNDEFINED to TRANSFER_DST
    barrier(hdr->swapchainImages[imageIndex],
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Blit
    VkImageBlit blitRegion = {};
    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.srcOffsets[1]  = {(int32_t)hdr->width, (int32_t)hdr->height, 1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[1]  = {(int32_t)hdr->width, (int32_t)hdr->height, 1};
    vkCmdBlitImage(hdr->cmdBuffers[imageIndex],
                   hdr->renderImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   hdr->swapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blitRegion, VK_FILTER_NEAREST);

    // swapchain image: TRANSFER_DST to PRESENT_SRC
    barrier(hdr->swapchainImages[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    // renderImage: TRANSFER_SRC to GENERAL (ready for next GL frame)
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
    if (res == VK_SUBOPTIMAL_KHR) needsRecreate = true;
    if (res == VK_ERROR_OUT_OF_DATE_KHR) needsRecreate = true;

    // Recreate after a successful present so the next frame starts at the right size.
    // Semaphores were consumed by the submit/present — no drain needed.
    if (needsRecreate)
        __vkRecreateSwapchain(hdr, false);

    return (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
           ? EGL_TRUE : EGL_FALSE;
}

static LRESULT CALLBACK __DummyWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
     return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

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

	wglCreateContext_PTR = (__PFN_wglCreateContext)GetProcAddress(opengl32dll, "wglCreateContext");
	wglDeleteContext_PTR = (__PFN_wglDeleteContext)GetProcAddress(opengl32dll, "wglDeleteContext");
	wglMakeCurrent_PTR = (__PFN_wglMakeCurrent)GetProcAddress(opengl32dll, "wglMakeCurrent");
	wglGetProcAddress_PTR = (__PFN_wglGetProcAddress)GetProcAddress(opengl32dll, "wglGetProcAddress");

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
	dummyPfd.nSize = sizeof(dummyPfd);
	dummyPfd.nVersion = 1;
	dummyPfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	dummyPfd.iPixelType = PFD_TYPE_RGBA;
	dummyPfd.cColorBits = 32;
	dummyPfd.cAlphaBits = 8;
	dummyPfd.cDepthBits = 24;

	EGLint dummyPixelFormat = ChoosePixelFormat(nativeLocalStorageContainer->hdc, &dummyPfd);

	if (dummyPixelFormat == 0)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	if (!SetPixelFormat(nativeLocalStorageContainer->hdc, dummyPixelFormat, &dummyPfd))
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	nativeLocalStorageContainer->ctx = wglCreateContext_PTR(nativeLocalStorageContainer->hdc);

	if (!nativeLocalStorageContainer->ctx)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	if (!wglMakeCurrent_PTR(nativeLocalStorageContainer->hdc, nativeLocalStorageContainer->ctx))
	{
		wglDeleteContext_PTR(nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;

		ReleaseDC(0, nativeLocalStorageContainer->hdc);
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
      (PFNWGLSWAPINTERVALEXTPROC)__getProcAddress("wglSwapIntervalEXT");
	wglGetExtensionsStringARB =
      (PFNWGLGETEXTENSIONSSTRINGARBPROC)
      __getProcAddress("wglGetExtensionsStringARB");
	glFinish_PTR = (__PFN_glFinish)__getProcAddress("glFinish");
	glFenceSync_PTR = (__PFN_glFenceSync)__getProcAddress("glFenceSync");
	glDeleteSync_PTR = (__PFN_glDeleteSync)__getProcAddress("glDeleteSync");
	glClientWaitSync_PTR = (__PFN_glClientWaitSync)__getProcAddress("glClientWaitSync");
	glWaitSync_PTR = (__PFN_glWaitSync)__getProcAddress("glWaitSync");
	glGetSynciv_PTR = (__PFN_glGetSynciv)__getProcAddress("glGetSynciv");

	wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)__getProcAddress("wglCreatePbufferARB");
	wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)__getProcAddress("wglGetPbufferDCARB");
	wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)__getProcAddress("wglReleasePbufferDCARB");
	wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)__getProcAddress("wglDestroyPbufferARB");
	wglBindTexImageARB_PTR = (PFNWGLBINDTEXIMAGEARBPROC)__getProcAddress("wglBindTexImageARB");
	wglReleaseTexImageARB_PTR = (PFNWGLRELEASETEXIMAGEARBPROC)__getProcAddress("wglReleaseTexImageARB");

	wglMakeCurrent_PTR(NULL, NULL);

	EGLint attrib_list[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 1,
		WGL_CONTEXT_MINOR_VERSION_ARB, 0,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		0
	};

	HGLRC testctx = NULL;
	EGLint GL_major = 4, GL_minor = 6;
	for (; GL_major >= 1 && !testctx; --GL_major)
	{
		for (; GL_minor >= 0 && !testctx; --GL_minor)
		{
			attrib_list[1] = GL_major;
			attrib_list[3] = GL_minor;
			testctx = wglCreateContextAttribsARB(nativeLocalStorageContainer->hdc, NULL, attrib_list);
		}
	}
	++GL_major;
	++GL_minor;

	if (testctx)
	{
		wglDeleteContext_PTR(testctx);
		testctx = NULL;
	}
	else
	{
		GL_major = 0;
		GL_minor = 0;
	}
	GL_max_supported[0] = GL_major;
	GL_max_supported[1] = GL_minor;


	attrib_list[5] = WGL_CONTEXT_ES_PROFILE_BIT_EXT;
	EGLint ES_major = 3, ES_minor = 2;
	for (; ES_major >= 1 && !testctx; --ES_major)
	{
		for (; ES_minor >= 0 && !testctx; --ES_minor)
		{
			attrib_list[1] = ES_major;
			attrib_list[3] = ES_minor;
			testctx = wglCreateContextAttribsARB(nativeLocalStorageContainer->hdc, NULL, attrib_list);
		}
	}
	++ES_major;
	++ES_minor;

	if (testctx)
	{
		wglDeleteContext_PTR(testctx);
		testctx = NULL;
	}
	else
	{
		ES_major = 0;
		ES_minor = 0;
	}
	ES_max_supported[0] = ES_major;
	ES_max_supported[1] = ES_minor;

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

	wglMakeCurrent_PTR(0, 0);

	if (nativeLocalStorageContainer->ctx)
	{
		wglDeleteContext_PTR(nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;
	}

	if (nativeLocalStorageContainer->hdc)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;
	}

	if (nativeLocalStorageContainer->hwnd)
	{
		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;
	}

	UnregisterClass("DummyWindow", NULL);

	__vkTerm();

	FreeLibrary(opengl32dll);

	return EGL_TRUE;
}

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || !nativeContextContainer)
	{
		return EGL_FALSE;
	}

	return wglDeleteContext_PTR(nativeContextContainer->ctx);
}

EGLBoolean __processAttribList(EGLenum api, EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error)
{
	if (!target_attrib_list || !attrib_list || !error)
	{
		return EGL_FALSE;
	}

	const EGLint defaultProfileMask = ((api == EGL_OPENGL_ES_API) ? WGL_CONTEXT_ES_PROFILE_BIT_EXT : WGL_CONTEXT_CORE_PROFILE_BIT_ARB);
	EGLint template_attrib_list[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 1,
			WGL_CONTEXT_MINOR_VERSION_ARB, 0,
			WGL_CONTEXT_LAYER_PLANE_ARB, 0,
			WGL_CONTEXT_FLAGS_ARB, 0,
			WGL_CONTEXT_PROFILE_MASK_ARB, defaultProfileMask,
			WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, WGL_NO_RESET_NOTIFICATION_ARB,
			0
	};

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
			break;
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

EGLBoolean __createPbufferSurface(EGLSurfaceImpl* newSurface, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
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
			WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_FALSE,  // default: linear per spec
			//WGL_STEREO_ARB, 0 ? GL_TRUE:GL_FALSE,
			0
	};

	EGLint pbuf_attribs[] = {
		WGL_PBUFFER_LARGEST_EXT, GL_FALSE,
		0
	};

	int width = 0;
	int height = 0;
	EGLBoolean mipmapTexture = EGL_FALSE;
	EGLint textureFormat = EGL_NO_TEXTURE;
	EGLint textureTarget = EGL_NO_TEXTURE;
	EGLint pbufColorspace = EGL_GL_COLORSPACE_LINEAR;
	EGLint currentAttribIndex = 0;
	while (attrib_list[currentAttribIndex] != EGL_NONE)
	{
		EGLint attrib = attrib_list[currentAttribIndex];
		EGLint value = attrib_list[currentAttribIndex + 1];
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
				iattribs[29] = GL_FALSE;
				pbufColorspace = EGL_GL_COLORSPACE_LINEAR;
			}
			else if (value == EGL_GL_COLORSPACE_SRGB)
			{
				// Only request sRGB pixel format if the driver supports it;
				// otherwise silently fall back to linear per spec.
				iattribs[29] = walkerDpy->srgbFramebufferSupported ? GL_TRUE : GL_FALSE;
				pbufColorspace = EGL_GL_COLORSPACE_SRGB;
			}
			else if (value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
			         value == EGL_GL_COLORSPACE_SCRGB_EXT         ||
			         value == EGL_GL_COLORSPACE_BT2020_PQ_EXT     ||
			         value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
			         value == EGL_GL_COLORSPACE_BT2020_HLG_EXT)
			{
				// HDR colorspaces stored but no Vulkan surface for offscreen buffers
				iattribs[29] = GL_FALSE;
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

	iattribs[9] = walkerConfig->bufferSize;
	iattribs[11] = walkerConfig->redSize;
	iattribs[13] = walkerConfig->blueSize;
	iattribs[15] = walkerConfig->greenSize;
	iattribs[17] = walkerConfig->alphaSize;
	iattribs[19] = walkerConfig->depthSize;
	iattribs[21] = walkerConfig->stencilSize;
	iattribs[23] = walkerConfig->sampleBuffers;
	iattribs[25] = walkerConfig->samples;

	HDC hdc = walkerDpy->display_id;

	int pformat;
	UINT max_formats = 1;
	if (!wglChoosePixelFormatARB(hdc, iattribs, NULL, max_formats, &pformat, &max_formats))
		return EGL_FALSE;

	// not sure im getting 1st arg ok (HDC)
	HPBUFFERARB pbuf = wglCreatePbufferARB(hdc, pformat, width, height, pbuf_attribs);
	if (!pbuf)
		return EGL_FALSE;

	hdc = wglGetPbufferDCARB(pbuf);

	if (!hdc)
	{
		*error = EGL_BAD_NATIVE_WINDOW;

		return EGL_FALSE;
	}

	newSurface->drawToWindow = EGL_FALSE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->drawToPBuffer = EGL_TRUE;
	newSurface->doubleBuffer = (EGLBoolean)iattribs[7];
	newSurface->configId = pformat;
	newSurface->width = width;
	newSurface->height = height;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = mipmapTexture;
	newSurface->largestPbuffer = (EGLBoolean)pbuf_attribs[1];
	newSurface->textureFormat = textureFormat;
	newSurface->textureTarget = textureTarget;
	newSurface->glColorspace = pbufColorspace;
	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->pbuf = pbuf;
	newSurface->nativeSurfaceContainer.hdc = hdc;
	newSurface->nativeSurfaceContainer.hdr = nullptr;

	return EGL_TRUE;
}

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
	if (!newSurface || !walkerDpy || !walkerConfig || !error)
	{
		return EGL_FALSE;
	}

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
			WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_FALSE,  // default: linear per spec
			//WGL_STEREO_ARB, 0 ? GL_TRUE:GL_FALSE,
			0
	};

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
						parsedColorspace = EGL_GL_COLORSPACE_LINEAR;
					}
					else if (value == EGL_GL_COLORSPACE_SRGB)
					{
						// Only request sRGB pixel format if the driver supports it;
						// otherwise silently fall back to linear per spec.
						template_attrib_list[29] = walkerDpy->srgbFramebufferSupported ? GL_TRUE : GL_FALSE;
						parsedColorspace = EGL_GL_COLORSPACE_SRGB;
					}
					else if (value == EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT ||
					         value == EGL_GL_COLORSPACE_SCRGB_EXT         ||
					         value == EGL_GL_COLORSPACE_BT2020_PQ_EXT     ||
					         value == EGL_GL_COLORSPACE_BT2020_LINEAR_EXT ||
					         value == EGL_GL_COLORSPACE_BT2020_HLG_EXT)
					{
						// HDR colorspaces use Vulkan for presentation; WGL framebuffer stays linear
						template_attrib_list[29] = GL_FALSE;
						parsedColorspace = value;
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
				break;
				case EGL_VG_COLORSPACE:
				{
					ReleaseDC(win, hdc);

					*error = EGL_BAD_MATCH;

					return EGL_FALSE;
				}
				break;
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

	template_attrib_list[9] = walkerConfig->bufferSize;
	template_attrib_list[11] = walkerConfig->redSize;
	template_attrib_list[13] = walkerConfig->blueSize;
	template_attrib_list[15] = walkerConfig->greenSize;
	template_attrib_list[17] = walkerConfig->alphaSize;
	template_attrib_list[19] = walkerConfig->depthSize;
	template_attrib_list[21] = walkerConfig->stencilSize;
	template_attrib_list[23] = walkerConfig->sampleBuffers;
	template_attrib_list[25] = walkerConfig->samples;
	//

	UINT wgl_max_formats = 1;
	INT wgl_formats;
	UINT wgl_num_formats;

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

	if (!SetPixelFormat(hdc, wgl_formats, &pfd))
	{
		ReleaseDC(win, hdc);

		*error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	newSurface->drawToWindow = EGL_TRUE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->drawToPBuffer = EGL_FALSE;
	newSurface->doubleBuffer = (EGLBoolean)template_attrib_list[7];
	newSurface->configId = wgl_formats;

	RECT rect = { 0 };
	GetClientRect(win, &rect);
	newSurface->width = rect.right - rect.left;
	newSurface->height = rect.bottom - rect.top;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = EGL_FALSE;
	newSurface->largestPbuffer = EGL_FALSE;
	newSurface->textureFormat = EGL_NO_TEXTURE;
	newSurface->textureTarget = EGL_NO_TEXTURE;
	newSurface->glColorspace = parsedColorspace;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->win = win;
	newSurface->nativeSurfaceContainer.hdc = hdc;
	newSurface->nativeSurfaceContainer.hdr = nullptr;

	// For HDR colorspaces, create a Vulkan HDR surface
	{
		VkFormat vkFmt; VkColorSpaceKHR vkCS;
		if (_eglHDRColorspaceToVk(parsedColorspace, &vkFmt, &vkCS) && g_vkDevice != VK_NULL_HANDLE)
		{
			NativeHDRSurfaceContainer* hdrContainer = (NativeHDRSurfaceContainer*)malloc(sizeof(NativeHDRSurfaceContainer));
			if (hdrContainer)
			{
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

	if (surface->nativeSurfaceContainer.hdr)
	{
		__vkDestroyHDRSurface(surface->nativeSurfaceContainer.hdr);
		free(surface->nativeSurfaceContainer.hdr);
	}

	if (surface->drawToWindow)
		ReleaseDC(surface->win, nativeSurfaceContainer->hdc);
	else if (surface->drawToPBuffer)
	{
		wglReleasePbufferDCARB(surface->pbuf, surface->nativeSurfaceContainer.hdc);
		wglDestroyPbufferARB(surface->pbuf);
	}
	else if (surface->drawToPixmap)
	{
		DeleteDC(nativeSurfaceContainer->hdc);
	}

	return EGL_TRUE;
}

EGLBoolean __createPixmapSurface(EGLSurfaceImpl* newSurface, EGLNativePixmapType pixmap,
	const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
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
					    value == EGL_GL_COLORSPACE_BT2020_HLG_EXT)
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
	HDC memDC = CreateCompatibleDC(screenDC);
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

	newSurface->drawToWindow = EGL_FALSE;
	newSurface->drawToPixmap = EGL_TRUE;
	newSurface->drawToPBuffer = EGL_FALSE;
	newSurface->doubleBuffer = EGL_FALSE;
	newSurface->configId = walkerConfig->configId;
	newSurface->width = bm.bmWidth;
	newSurface->height = bm.bmHeight;
	newSurface->swapBehavior = EGL_BUFFER_DESTROYED;
	newSurface->multisampleResolve = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
	newSurface->mipmapLevel = 0;
	newSurface->mipmapTexture = EGL_FALSE;
	newSurface->largestPbuffer = EGL_FALSE;
	newSurface->textureFormat = EGL_NO_TEXTURE;
	newSurface->textureTarget = EGL_NO_TEXTURE;
	newSurface->glColorspace = glColorspace;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->pixmap = pixmap;
	newSurface->nativeSurfaceContainer.hdc = memDC;
	newSurface->nativeSurfaceContainer.hdr = nullptr;

	return EGL_TRUE;
}

EGLBoolean __copyBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* surface, EGLNativePixmapType target)
{
	(void)walkerDpy; (void)surface;

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

	GLsizei stride = (width * 4 + 3) & ~3;
	GLubyte* pixels = (GLubyte*)malloc((size_t)stride * height);
	if (!pixels)
		return EGL_FALSE;

	// GL_BGRA = 0x80E1; bottom-up origin matches positive-height DIB
	glReadPixels(0, 0, width, height, 0x80E1, GL_UNSIGNED_BYTE, pixels);

	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth       = width;
	bi.bmiHeader.biHeight      = height;  // positive = bottom-up, matches GL origin
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	HDC screenDC = GetDC(NULL);
	HDC memDC    = CreateCompatibleDC(screenDC);
	ReleaseDC(NULL, screenDC);
	HGDIOBJ oldBmp = SelectObject(memDC, target);
	SetDIBits(memDC, target, 0, (UINT)height, pixels, &bi, DIB_RGB_COLORS);
	SelectObject(memDC, oldBmp);
	DeleteDC(memDC);
	free(pixels);

	return EGL_TRUE;
}

__eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname)
{
	__eglMustCastToProperFunctionPointerType ptr = NULL;
	ptr = (__eglMustCastToProperFunctionPointerType) wglGetProcAddress_PTR(procname);
	if (ptr != NULL)
		return ptr;
	// https://www.khronos.org/opengl/wiki/Talk:Platform_specifics:_Windows
	return (__eglMustCastToProperFunctionPointerType) GetProcAddress(opengl32dll, procname);
}

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error)
{
	if (!walkerDpy || !nativeLocalStorageContainer || !error)
	{
		return EGL_FALSE;
	}

	// Create configuration list.

	EGLint numberPixelFormats;

	EGLint attribute = WGL_NUMBER_PIXEL_FORMATS_ARB;
	if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, 1, 0, 1, &attribute, &numberPixelFormats))
	{
		*error = EGL_NOT_INITIALIZED;

		return EGL_FALSE;
	}

	const char* extensions_str = wglGetExtensionsStringARB(nativeLocalStorageContainer->hdc);

	const int render_texture_supported = strstr(extensions_str, "WGL_ARB_render_texture") != NULL;
	const int ES_supported = strstr(extensions_str, "WGL_EXT_create_context_es_profile") != NULL;
	const EGLint ES_mask = ES_supported * (EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT);

	walkerDpy->srgbFramebufferSupported =
		(strstr(extensions_str, "WGL_ARB_framebuffer_sRGB") != NULL ||
		 strstr(extensions_str, "WGL_EXT_framebuffer_sRGB") != NULL) ? EGL_TRUE : EGL_FALSE;

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

		EGLConfigImpl* newConfig = (EGLConfigImpl*)malloc(sizeof(EGLConfigImpl));
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
		newConfig->conformant = (EGL_OPENGL_BIT | ES_mask);
		newConfig->renderableType = (EGL_OPENGL_BIT | ES_mask);
		newConfig->surfaceType = 0;
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
		newConfig->configId = currentPixelFormat;

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

		attribute = WGL_BIND_TO_TEXTURE_RGB_ARB;
		if (render_texture_supported &&
        !wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGB))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGB = newConfig->bindToTextureRGB ? EGL_TRUE : EGL_FALSE;

		attribute = WGL_BIND_TO_TEXTURE_RGBA_ARB;
		if (render_texture_supported &&
        !wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGBA))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGBA = newConfig->bindToTextureRGBA ? EGL_TRUE : EGL_FALSE;

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
		newConfig->nativeRenderable = EGL_DONT_CARE; // ???

		// Query configCaveat from acceleration type.
		int accelValue = 0;
		attribute = WGL_ACCELERATION_ARB;
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

EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeSurfaceContainer, const EGLint* attribList)
{
	if (!walkerDpy || !nativeContextContainer || !nativeSurfaceContainer)
	{
		return EGL_FALSE;
	}

	nativeContextContainer->ctx = wglCreateContextAttribsARB(nativeSurfaceContainer->hdc, sharedNativeSurfaceContainer ? sharedNativeSurfaceContainer->ctx : 0, attribList);
	DWORD err = GetLastError();

	return nativeContextContainer->ctx != 0;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || (nativeContextContainer && !nativeSurfaceContainer))
	{
		return EGL_FALSE;
	}

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

	if (walkerSurface->nativeSurfaceContainer.hdr)
		return __vkPresent(walkerSurface->nativeSurfaceContainer.hdr);

	return (EGLBoolean)SwapBuffers(walkerSurface->nativeSurfaceContainer.hdc);
}

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval)
{
	if (!walkerDpy)
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

EGLBoolean __getPlatformDependentHandles(void* _out, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
	if (!nativeSurfaceContainer || !nativeContextContainer)
		return EGL_FALSE;

	EGLContextInternals* out = (EGLContextInternals*) _out;

	out->display = walkerDpy->display_id;
	out->context = nativeContextContainer->ctx;
	out->surface = nativeSurfaceContainer->hdc;

	return EGL_TRUE;
}

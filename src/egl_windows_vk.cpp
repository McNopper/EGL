#include "egl_windows_vk.h"
#include "egl_common.h"
#include <vector>
#include <algorithm>
#include <EGL/eglext.h>

extern __eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname);

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

// ---- Helpers ----

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

static uint32_t _vkColorspaceToBit(VkColorSpaceKHR vkCS)
{
    switch (vkCS)
    {
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:    return EGL_HDR_CS_SCRGB_LINEAR_BIT;
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return EGL_HDR_CS_SCRGB_BIT;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:            return EGL_HDR_CS_BT2020_PQ_BIT;
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:           return EGL_HDR_CS_BT2020_LINEAR_BIT;
        case VK_COLOR_SPACE_HDR10_HLG_EXT:              return EGL_HDR_CS_BT2020_HLG_BIT;
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:   return EGL_HDR_CS_DISPLAY_P3_BIT;
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:      return EGL_HDR_CS_DISPLAY_P3_LINEAR_BIT;
        default: return 0;
    }
}

// Forward declaration (called from __vkPresent on OUT_OF_DATE / SUBOPTIMAL)
static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore);

// ---- __vkInit: create VkInstance + VkDevice (called once from __internalInit) ----
EGLBoolean __vkInit()
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
    for (const auto* req : wantedDevExts)
    {
        if (std::any_of(availExts.begin(), availExts.end(),
                [req](const VkExtensionProperties& e){ return strcmp(req, e.extensionName) == 0; }))
            enabledDevExts.push_back(req);
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
    g_vkPhysDevice  = VK_NULL_HANDLE;
    g_vkQueueFamily = UINT32_MAX;
    g_vkQueue       = VK_NULL_HANDLE;
    g_pfnSetHdrMetadata = nullptr;
    g_pfnGetMemWin32    = nullptr;
    g_pfnGetSemWin32    = nullptr;
}

// ---- __vkQueryHDRColorspaces: query HDR formats for a given HWND ----
uint32_t __vkQueryHDRColorspaces(HWND hwnd)
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
        bool listed = std::any_of(formats.begin(), formats.end(),
            [&entry](const VkSurfaceFormatKHR& f){ return f.format == entry.fmt && f.colorSpace == entry.cs; });
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
void __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr)
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
static EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE || !hdr->hwnd)
        return EGL_FALSE;

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

    if (hdr->pendingMemHandle) { CloseHandle(hdr->pendingMemHandle); hdr->pendingMemHandle = nullptr; }
    if (hdr->pendingSemHandle) { CloseHandle(hdr->pendingSemHandle); hdr->pendingSemHandle = nullptr; }

    if (hdr->renderMemory) { vkFreeMemory(g_vkDevice, hdr->renderMemory, nullptr); hdr->renderMemory = VK_NULL_HANDLE; }
    if (hdr->renderImage)  { vkDestroyImage(g_vkDevice, hdr->renderImage, nullptr); hdr->renderImage = VK_NULL_HANDLE; }
    free(hdr->swapchainImages); hdr->swapchainImages = nullptr;

    RECT cr = {};
    GetClientRect(hdr->hwnd, &cr);
    uint32_t newW = (uint32_t)(cr.right  - cr.left);
    uint32_t newH = (uint32_t)(cr.bottom - cr.top);
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

    if (!g_pfnGetMemWin32) return EGL_FALSE;
    {
        VkMemoryGetWin32HandleInfoKHR hInfo = {};
        hInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        hInfo.memory     = hdr->renderMemory;
        hInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetMemWin32(g_vkDevice, &hInfo, &hdr->pendingMemHandle) != VK_SUCCESS)
            return EGL_FALSE;
    }

    if (!g_pfnGetSemWin32) return EGL_FALSE;
    {
        VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
            return EGL_FALSE;
    }

    hdr->glInteropReady = false;
    return EGL_TRUE;
}

// ---- __vkCreateHDRSurface: create swapchain + GL/Vulkan interop objects ----
EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr, HWND win, EGLint eglCS, uint32_t w, uint32_t h)
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

    // 7. Export renderImage memory as Win32 handle
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

    // 9. Create semaphores
    {
        VkExportSemaphoreCreateInfo esci = {};
        esci.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
        esci.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        VkSemaphoreCreateInfo semCI = {};
        semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semCI.pNext = &esci;
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->glDoneSemaphore) != VK_SUCCESS)
            goto cleanup;

        if (!g_pfnGetSemWin32)
            goto cleanup;
        VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
        shInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        shInfo.semaphore  = hdr->glDoneSemaphore;
        shInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
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
    g_pfnImportMemWin32(hdr->glMemoryObject, hdr->renderMemorySize,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingMemHandle);
    hdr->pendingMemHandle = nullptr;

    glGenTextures(1, &hdr->glTexture);
    glBindTexture(GL_TEXTURE_2D, hdr->glTexture);
    GLenum glFmt = (hdr->vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
                   ? 0x881A  // GL_RGBA16F
                   : 0x8059; // GL_RGB10_A2
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
    g_pfnImportSemWin32(hdr->glDoneSemObj,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingSemHandle);
    hdr->pendingSemHandle = nullptr;

    hdr->glInteropReady = true;
    return true;
}

// ---- __vkPresent: blit GL default FBO to interop image to swapchain, then present ----
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

    // Step 2: GL signals semaphore
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
    if (res == VK_SUBOPTIMAL_KHR) needsRecreate = true;
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

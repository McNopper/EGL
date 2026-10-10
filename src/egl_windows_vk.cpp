#include "egl_vk_core.h"
#include "egl_windows_vk.h"
#include "egl_common.h"
#include <vector>
#include <algorithm>
#include <string.h>
#include <EGL/eglext.h>

extern __eglMustCastToProperFunctionPointerType __getProcAddress(const char* procname);

// ---- Vulkan function pointer types needed for extensions ----
typedef VkResult(VKAPI_PTR* PFN_vkGetMemoryWin32HandleKHR_t)(VkDevice, const VkMemoryGetWin32HandleInfoKHR*, HANDLE*);
typedef VkResult(VKAPI_PTR* PFN_vkGetSemaphoreWin32HandleKHR_t)(VkDevice, const VkSemaphoreGetWin32HandleInfoKHR*, HANDLE*);

// ---- GL interop function pointer types ----
typedef void(APIENTRY* PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)(GLuint memory, unsigned long long size, GLenum handleType, void* handle);
typedef void(APIENTRY* PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC)(GLuint semaphore, GLenum handleType, void* handle);

// GL FBO function pointer types (GL 3.0 core, may need proc address on Windows)

// GL constant definitions needed for interop
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT 0x9587u
#define GL_LAYOUT_GENERAL_EXT 0x958Du
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT 0x958Eu
#define GL_READ_FRAMEBUFFER 0x8CA8u
#define GL_DRAW_FRAMEBUFFER 0x8CA9u
#define GL_COLOR_ATTACHMENT0 0x8CE0u

// ---- Vulkan singleton globals (one device shared across all displays) ----
static PFN_vkGetMemoryWin32HandleKHR_t    g_pfnGetMemWin32    = nullptr;
static PFN_vkGetSemaphoreWin32HandleKHR_t g_pfnGetSemWin32    = nullptr;

// ---- GL interop function pointers (loaded once after first GL context) ----
static PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC    g_pfnImportMemWin32   = nullptr;
static PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC g_pfnImportSemWin32   = nullptr;


// ---- __vkInit: create VkInstance + VkDevice (called once from __internalInit) ----
EGLBoolean __vkInit()
{
    if (g_vkInstance != VK_NULL_HANDLE)
        return EGL_TRUE;

    // VK_KHR_surface and the WSI extension are required, but
    // VK_EXT_swapchain_colorspace is not. Making it mandatory meant
    // vkCreateInstance failed outright on drivers without it, which disabled the
    // whole Vulkan presentation path. Enable it only when the instance actually
    // exposes it - the HDR/P3 entries in __vkQueryHDRColorspaces then simply fail
    // their swapchain probe and stay unadvertised.
    static const char* k_requiredInstExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
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
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
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
    g_vkPhysDevice      = VK_NULL_HANDLE;
    g_vkQueueFamily     = UINT32_MAX;
    g_vkQueue           = VK_NULL_HANDLE;
    g_pfnSetHdrMetadata = nullptr;
    g_pfnGetMemWin32    = nullptr;
    g_pfnGetSemWin32    = nullptr;

    // The GL interop entry points point into opengl32.dll, which __internalTerminate
    // unloads right after us. Drop them all so a later init/swap cycle re-resolves
    // them instead of calling through stale pointers.
    g_pfnCreateMemObjs    = nullptr;
    g_pfnDeleteMemObjs    = nullptr;
    g_pfnImportMemWin32   = nullptr;
    g_pfnTexStorageMem2D  = nullptr;
    g_pfnGenSemaphores    = nullptr;
    g_pfnDeleteSemaphores = nullptr;
    g_pfnImportSemWin32   = nullptr;
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
// first bit the surface actually reports. Preference order keeps the opaque, fully

// We blit into the swapchain images, so TRANSFER_DST is as mandatory for us as

// ---- __vkQueryHDRColorspaces: query HDR formats for a given HWND ----
uint32_t __vkQueryHDRColorspaces(HWND hwnd)
{
    if (g_vkInstance == VK_NULL_HANDLE)
        return 0;

    VkWin32SurfaceCreateInfoKHR sci = {};
    sci.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance                   = GetModuleHandle(nullptr);
    sci.hwnd                        = hwnd;

    VkSurfaceKHR tmpSurface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(g_vkInstance, &sci, nullptr, &tmpSurface) != VK_SUCCESS)
        return 0;

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

    // Check the exact format+colorspace pairs we actually request at swapchain creation time.
    // A colorspace advertised with a different format would fail in __vkCreateHDRSurface.
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

    // Get surface capabilities for swapchain test.
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
        // First check that the driver lists this exact format+colorspace pair.
        bool listed = std::any_of(formats.begin(), formats.end(),
                                  [&entry](const VkSurfaceFormatKHR& f)
                                  { return f.format == entry.fmt && f.colorSpace == entry.cs; });
        if (!listed)
            continue;

        // Confirm by attempting a real test swapchain — some drivers list pairs they
        // can't actually create (e.g. NVIDIA lists BT2020_LINEAR but fails on creation).
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

// ---- __vkLoadGLInterop: load GL interop + FBO function pointers ----
static void __vkLoadGLInterop()
{
    if (g_glInteropLoaded)
        return;

    g_pfnCreateMemObjs    = (PFNGLCREATEMEMORYOBJECTSEXTPROC)__getProcAddress("glCreateMemoryObjectsEXT");
    g_pfnDeleteMemObjs    = (PFNGLDELETEMEMORYOBJECTSEXTPROC)__getProcAddress("glDeleteMemoryObjectsEXT");
    g_pfnImportMemWin32   = (PFNGLIMPORTMEMORYWIN32HANDLEEXTPROC)__getProcAddress("glImportMemoryWin32HandleEXT");
    g_pfnTexStorageMem2D  = (PFNGLTEXSTORAGEMEM2DEXTPROC)__getProcAddress("glTexStorageMem2DEXT");
    g_pfnGenSemaphores    = (PFNGLGENSEMAPHORESEXTPROC)__getProcAddress("glGenSemaphoresEXT");
    g_pfnDeleteSemaphores = (PFNGLDELETESEMAPHORESEXTPROC)__getProcAddress("glDeleteSemaphoresEXT");
    g_pfnImportSemWin32   = (PFNGLIMPORTSEMAPHOREWIN32HANDLEEXTPROC)__getProcAddress("glImportSemaphoreWin32HandleEXT");
    g_pfnSignalSemaphore  = (PFNGLSIGNALSEMAPHOREEXTPROC)__getProcAddress("glSignalSemaphoreEXT");
    g_pfnWaitSemaphore    = (PFNGLWAITSEMAPHOREEXTPROC)__getProcAddress("glWaitSemaphoreEXT");
    g_pfnGenFBOs          = (PFNGLGENFRAMEBUFFERSPROC)__getProcAddress("glGenFramebuffers");
    g_pfnDeleteFBOs       = (PFNGLDELETEFRAMEBUFFERSPROC)__getProcAddress("glDeleteFramebuffers");
    g_pfnBindFBO          = (PFNGLBINDFRAMEBUFFERPROC)__getProcAddress("glBindFramebuffer");
    g_pfnFBOTex2D         = (PFNGLFRAMEBUFFERTEXTURE2DPROC)__getProcAddress("glFramebufferTexture2D");
    g_pfnBlitFBO          = (PFNGLBLITFRAMEBUFFERPROC)__getProcAddress("glBlitFramebuffer");

    g_glInteropLoaded = (g_pfnCreateMemObjs != nullptr &&
                         g_pfnImportMemWin32 != nullptr &&
                         g_pfnTexStorageMem2D != nullptr &&
                         g_pfnDeleteMemObjs != nullptr &&
                         g_pfnGenSemaphores != nullptr &&
                         g_pfnImportSemWin32 != nullptr &&
                         g_pfnSignalSemaphore != nullptr &&
                         g_pfnWaitSemaphore != nullptr &&
                         g_pfnDeleteSemaphores != nullptr &&
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

    // GL cleanup (requires a GL context to be current; best-effort)
    if (hdr->pendingMemHandle)
    {
        CloseHandle(hdr->pendingMemHandle);
        hdr->pendingMemHandle = nullptr;
    }
    if (hdr->pendingSemHandle)
    {
        CloseHandle(hdr->pendingSemHandle);
        hdr->pendingSemHandle = nullptr;
    }
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
}

// ---- __vkRecreateSwapchain: rebuild swapchain + render image on resize ----
EGLBoolean __vkRecreateSwapchain(NativeHDRSurfaceContainer* hdr, bool drainGLSemaphore)
{
    if (!hdr || g_vkDevice == VK_NULL_HANDLE || !hdr->hwnd)
        return EGL_FALSE;

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

    if (hdr->pendingMemHandle)
    {
        CloseHandle(hdr->pendingMemHandle);
        hdr->pendingMemHandle = nullptr;
    }
    if (hdr->pendingSemHandle)
    {
        CloseHandle(hdr->pendingSemHandle);
        hdr->pendingSemHandle = nullptr;
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

    RECT cr = {};
    GetClientRect(hdr->hwnd, &cr);
    uint32_t newW = (uint32_t)(cr.right - cr.left);
    uint32_t newH = (uint32_t)(cr.bottom - cr.top);
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
        // The blit destination must follow the images we really got, not the client
        // rect, which can already have moved on again during a live resize.
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

    {
        VkExternalMemoryImageCreateInfo emici = {};
        emici.sType                           = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        emici.handleTypes                     = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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
        emai.handleTypes                = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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

    // Transition renderImage UNDEFINED → GENERAL.
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

    if (!g_pfnGetMemWin32)
        return EGL_FALSE;
    {
        VkMemoryGetWin32HandleInfoKHR hInfo = {};
        hInfo.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        hInfo.memory                        = hdr->renderMemory;
        hInfo.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetMemWin32(g_vkDevice, &hInfo, &hdr->pendingMemHandle) != VK_SUCCESS)
            return EGL_FALSE;
    }

    if (!g_pfnGetSemWin32)
        return EGL_FALSE;
    {
        VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
        shInfo.sType                            = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        shInfo.semaphore                        = hdr->glDoneSemaphore;
        shInfo.handleType                       = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
            return EGL_FALSE;
    }

    hdr->glInteropReady   = false;
    hdr->hdrMetadataDirty = true; // re-apply metadata to the new swapchain
    return EGL_TRUE;
}

// ---- __vkCreateHDRSurface: create swapchain + GL/Vulkan interop objects ----
EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr, HWND win, EGLint eglCS, uint32_t w, uint32_t h)
{
    if (!hdr || g_vkInstance == VK_NULL_HANDLE || g_vkDevice == VK_NULL_HANDLE)
    {
        return EGL_FALSE;
    }

    memset(hdr, 0, sizeof(*hdr));
    hdr->width  = w;
    hdr->height = h;
    hdr->hwnd   = win;

    if (!_eglHDRColorspaceToVk(eglCS, &hdr->vkFormat, &hdr->vkColorSpace))
    {
        return EGL_FALSE;
    }

    // Build all Vulkan + GL interop objects. Any failure returns EGL_FALSE and the
    // wrapper below tears the partial state down via __vkDestroyHDRSurface.
    auto build = [&]() -> EGLBoolean
    {
        // 1. Create VkSurfaceKHR
        VkWin32SurfaceCreateInfoKHR sci = {};
        sci.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        sci.hinstance                   = GetModuleHandle(nullptr);
        sci.hwnd                        = win;
        if (vkCreateWin32SurfaceKHR(g_vkInstance, &sci, nullptr, &hdr->vkSurface) != VK_SUCCESS)
            return EGL_FALSE;

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
            emici.handleTypes                     = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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
            emai.handleTypes                = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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

        // 7. Export renderImage memory as Win32 handle
        {
            if (!g_pfnGetMemWin32)
                return EGL_FALSE;

            VkMemoryGetWin32HandleInfoKHR hInfo = {};
            hInfo.sType                         = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
            hInfo.memory                        = hdr->renderMemory;
            hInfo.handleType                    = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            if (g_pfnGetMemWin32(g_vkDevice, &hInfo, &hdr->pendingMemHandle) != VK_SUCCESS)
                return EGL_FALSE;
        }

        // 9. Create the exportable GL-done semaphore. The acquire and render-finished
        // semaphores are per frame / per image and were already created in step 4.
        {
            VkExportSemaphoreCreateInfo esci = {};
            esci.sType                       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
            esci.handleTypes                 = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

            VkSemaphoreCreateInfo semCI = {};
            semCI.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semCI.pNext                 = &esci;
            if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->glDoneSemaphore) != VK_SUCCESS)
                return EGL_FALSE;

            if (!g_pfnGetSemWin32)
                return EGL_FALSE;
            VkSemaphoreGetWin32HandleInfoKHR shInfo = {};
            shInfo.sType                            = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
            shInfo.semaphore                        = hdr->glDoneSemaphore;
            shInfo.handleType                       = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            if (g_pfnGetSemWin32(g_vkDevice, &shInfo, &hdr->pendingSemHandle) != VK_SUCCESS || !hdr->pendingSemHandle)
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

    // Import the exported Vulkan memory into a GL memory object. The GL takes its
    // own reference to the NT handle, so the exported handle stays in the container
    // and is closed on teardown/recreate (never nulled-and-leaked here).
    g_pfnCreateMemObjs(1, &hdr->glMemoryObject);
    g_pfnImportMemWin32(hdr->glMemoryObject, hdr->renderMemorySize,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingMemHandle);
    if (glGetError() != GL_NO_ERROR)
    {
        // Check here rather than after the texture call: glTexStorageMem2DEXT on an
        // unbacked memory object would fail too and the error would be blamed on it.
        if (hdr->glMemoryObject)
        {
            g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
            hdr->glMemoryObject = 0;
        }
        return false;
    }

    glGenTextures(1, &hdr->glTexture);
    glBindTexture(GL_TEXTURE_2D, hdr->glTexture);
    GLenum glFmt = (hdr->vkFormat == VK_FORMAT_R16G16B16A16_SFLOAT)
                       ? 0x881A  // GL_RGBA16F
                       : 0x8059; // GL_RGB10_A2
    g_pfnTexStorageMem2D(GL_TEXTURE_2D, 1, glFmt,
                         static_cast<GLsizei>(hdr->width), static_cast<GLsizei>(hdr->height),
                         hdr->glMemoryObject, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glGetError() != GL_NO_ERROR)
    {
        // Backing-store creation failed — drop the partial objects (handles are
        // retained for teardown) and let the next present retry.
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
    g_pfnImportSemWin32(hdr->glDoneSemObj,
                        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, hdr->pendingSemHandle);

    if (glGetError() != GL_NO_ERROR)
    {
        if (hdr->glDoneSemObj)
        {
            g_pfnDeleteSemaphores(1, &hdr->glDoneSemObj);
            hdr->glDoneSemObj = 0;
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
        if (hdr->glMemoryObject)
        {
            g_pfnDeleteMemObjs(1, &hdr->glMemoryObject);
            hdr->glMemoryObject = 0;
        }
        return false;
    }

    hdr->glInteropReady = true;
    return true;
}

// ---- __vkHdrMetadataEqual: compare payload fields, not raw bytes ----
// VkHdrMetadataEXT contains padding, and C does not require struct assignment to
// preserve padding bytes. memcmp over the whole object can therefore report a
// spurious difference after `hdrMetadata = m` and re-send the metadata on every




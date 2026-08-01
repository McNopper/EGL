#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
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
    // Per-swapchain-image objects. All of these arrays have exactly imageCount
    // entries; imageCount is only non-zero while arrays of that size really exist.
    uint32_t         imageCount;
    VkImage*         swapchainImages;
    VkCommandPool    cmdPool;
    VkCommandBuffer* cmdBuffers;
    // One render-finished semaphore per swapchain image (the canonical WSI pattern):
    // a single shared one would be re-signalled while an earlier present still waits.
    VkSemaphore* renderFinishedSemaphores;
    // Non-owning aliases into fences[]: which frame slot last submitted work for
    // this image, so a re-used image is never recorded into while still in flight.
    VkFence* imagesInFlight;

    // Per-frame-in-flight objects. imageCount + 1 slots, indexed by frameIndex.
    // The acquire semaphore must have no pending signal/wait when it is passed to
    // vkAcquireNextImageKHR, which the matching per-frame fence guarantees.
    uint32_t     frameCount;
    uint32_t     frameIndex;
    VkSemaphore* acquireSemaphores;
    VkFence*     fences;

    VkImage        renderImage;
    VkDeviceMemory renderMemory;
    GLuint         glTexture;
    GLuint         glMemoryObject;
    GLuint         blitFbo;

    VkSemaphore glDoneSemaphore;
    GLuint      glDoneSemObj;

    uint32_t width;
    uint32_t height;
    // The extent the swapchain images were actually created with. During a live
    // resize this can differ from width/height, which still describe the GL side.
    VkExtent2D   swapchainExtent;
    VkDeviceSize renderMemorySize;
    HWND         hwnd;

    bool   glInteropReady;
    HANDLE pendingMemHandle;
    HANDLE pendingSemHandle;

    // HDR mastering/content-light metadata (EGL_EXT_surface_SMPTE2086/CTA861_3),
    // applied to the swapchain via vkSetHdrMetadataEXT when present and changed.
    VkHdrMetadataEXT hdrMetadata;
    bool             hasHdrMetadata;
    bool             hdrMetadataDirty;
};

// Vulkan HDR backend interface
bool       _eglHDRColorspaceToVk(EGLint eglCS, VkFormat* fmt, VkColorSpaceKHR* cs);
bool       __vkIsReady();
EGLBoolean __vkInit();
void       __vkTerm();
EGLBoolean __vkCreateHDRSurface(NativeHDRSurfaceContainer* hdr, HWND win, EGLint eglCS, uint32_t w, uint32_t h);
void       __vkDestroyHDRSurface(NativeHDRSurfaceContainer* hdr);
EGLBoolean __vkPresent(NativeHDRSurfaceContainer* hdr);
uint32_t   __vkQueryHDRColorspaces(HWND hwnd);
// Copy the surface's SMPTE2086/CTA861 metadata into the HDR container. Only flags
// it for submission when the colorspace actually consumes it (HDR10 PQ / HLG) and
// the application supplied non-zero values.
void __vkUpdateHDRMetadata(NativeHDRSurfaceContainer* hdr, const EGLSurfaceImpl* surf);

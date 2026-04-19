#pragma once
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "egl_internal.h"
#include <GL/gl.h>

// Full definition of the HDR surface container (forward-declared in egl_internal.h)
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
    VkDeviceSize     renderMemorySize;
    HWND             hwnd;

    bool             glInteropReady;
    HANDLE           pendingMemHandle;
    HANDLE           pendingSemHandle;
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

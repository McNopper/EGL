/**
 * EGL Vulkan HDR shared core - the platform-independent half of the HDR
 * present path, shared by the Windows (WGL + Vulkan) and Linux
 * (X11/Wayland + Vulkan) backends. See egl_vk_core.h for the platform seam.
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

#include "egl_vk_core.h"
// _NativeHDRSurfaceContainer is defined per platform - its handle-export fields
// are the seam. The core reads only the shared fields, but the complete type is
// needed to compile against it, so the platform header is pulled per build
// configuration (the core is only ever built inside one of them).
#if defined(_WIN32)
#include "egl_windows_vk.h"
#elif defined(__linux__)
#include "egl_linux_vk.h"
#endif
#include "egl_common.h"
#include <vector>
#include <algorithm>
#include <string.h>
#include <EGL/eglext.h>

extern __eglMustCastToProperFunctionPointerType __getProcAddress(const char* procname);

// ---- Shared Vulkan/GL state (one device shared across all surfaces) ----

VkInstance                         g_vkInstance        = VK_NULL_HANDLE;
VkPhysicalDevice                   g_vkPhysDevice      = VK_NULL_HANDLE;
VkDevice                           g_vkDevice          = VK_NULL_HANDLE;
uint32_t                           g_vkQueueFamily     = UINT32_MAX;
VkQueue                            g_vkQueue           = VK_NULL_HANDLE;
PFN_vkSetHdrMetadataEXT_t          g_pfnSetHdrMetadata = nullptr;
PFNGLCREATEMEMORYOBJECTSEXTPROC        g_pfnCreateMemObjs    = nullptr;
PFNGLDELETEMEMORYOBJECTSEXTPROC        g_pfnDeleteMemObjs    = nullptr;
PFNGLTEXSTORAGEMEM2DEXTPROC            g_pfnTexStorageMem2D  = nullptr;
PFNGLGENSEMAPHORESEXTPROC              g_pfnGenSemaphores    = nullptr;
PFNGLDELETESEMAPHORESEXTPROC           g_pfnDeleteSemaphores = nullptr;
PFNGLSIGNALSEMAPHOREEXTPROC            g_pfnSignalSemaphore  = nullptr;
PFNGLWAITSEMAPHOREEXTPROC              g_pfnWaitSemaphore    = nullptr;
PFNGLGENFRAMEBUFFERSPROC               g_pfnGenFBOs          = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC            g_pfnDeleteFBOs       = nullptr;
PFNGLBINDFRAMEBUFFERPROC               g_pfnBindFBO          = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC          g_pfnFBOTex2D         = nullptr;
PFNGLBLITFRAMEBUFFERPROC               g_pfnBlitFBO          = nullptr;
bool                                   g_glInteropLoaded     = false;

// ---- Shared logic ----

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

bool __vkHdrMetadataEqual(const VkHdrMetadataEXT* a, const VkHdrMetadataEXT* b)
{
    return a->displayPrimaryRed.x == b->displayPrimaryRed.x && a->displayPrimaryRed.y == b->displayPrimaryRed.y &&
           a->displayPrimaryGreen.x == b->displayPrimaryGreen.x && a->displayPrimaryGreen.y == b->displayPrimaryGreen.y &&
           a->displayPrimaryBlue.x == b->displayPrimaryBlue.x && a->displayPrimaryBlue.y == b->displayPrimaryBlue.y &&
           a->whitePoint.x == b->whitePoint.x && a->whitePoint.y == b->whitePoint.y &&
           a->maxLuminance == b->maxLuminance && a->minLuminance == b->minLuminance &&
           a->maxContentLightLevel == b->maxContentLightLevel && a->maxFrameAverageLightLevel == b->maxFrameAverageLightLevel;
}

void __vkUpdateHDRMetadata(NativeHDRSurfaceContainer* hdr, const EGLSurfaceImpl* surf)
{
    if (!hdr || !surf)
        return;

    // Mastering/content-light metadata is only consumed by the HDR10 PQ and HLG
    // colorspaces; for scRGB / linear / Display-P3 there is nothing to signal.
    if (hdr->vkColorSpace != VK_COLOR_SPACE_HDR10_ST2084_EXT &&
        hdr->vkColorSpace != VK_COLOR_SPACE_HDR10_HLG_EXT)
    {
        hdr->hasHdrMetadata = false;
        return;
    }

    // The application must have supplied metadata (any non-zero value).
    EGLint anySet = surf->smpte2086DisplayPrimaryRx | surf->smpte2086DisplayPrimaryRy |
                    surf->smpte2086DisplayPrimaryGx | surf->smpte2086DisplayPrimaryGy |
                    surf->smpte2086DisplayPrimaryBx | surf->smpte2086DisplayPrimaryBy |
                    surf->smpte2086WhitePointX | surf->smpte2086WhitePointY |
                    surf->smpte2086MaxLuminance | surf->smpte2086MinLuminance |
                    surf->cta861MaxContentLightLevel | surf->cta861MaxFrameAverageLightLevel;
    if (anySet == 0)
    {
        hdr->hasHdrMetadata = false;
        return;
    }

    // EGL stores chromaticities scaled by EGL_METADATA_SCALING_EXT (50000) and
    // luminance scaled by 10000 (matching the examples); CTA861 light levels are nits.
    VkHdrMetadataEXT m          = {};
    m.sType                     = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    m.displayPrimaryRed         = {surf->smpte2086DisplayPrimaryRx / 50000.0f, surf->smpte2086DisplayPrimaryRy / 50000.0f};
    m.displayPrimaryGreen       = {surf->smpte2086DisplayPrimaryGx / 50000.0f, surf->smpte2086DisplayPrimaryGy / 50000.0f};
    m.displayPrimaryBlue        = {surf->smpte2086DisplayPrimaryBx / 50000.0f, surf->smpte2086DisplayPrimaryBy / 50000.0f};
    m.whitePoint                = {surf->smpte2086WhitePointX / 50000.0f, surf->smpte2086WhitePointY / 50000.0f};
    m.maxLuminance              = surf->smpte2086MaxLuminance / 10000.0f;
    m.minLuminance              = surf->smpte2086MinLuminance / 10000.0f;
    m.maxContentLightLevel      = (float)surf->cta861MaxContentLightLevel;
    m.maxFrameAverageLightLevel = (float)surf->cta861MaxFrameAverageLightLevel;

    if (!hdr->hasHdrMetadata || !__vkHdrMetadataEqual(&m, &hdr->hdrMetadata))
    {
        hdr->hdrMetadata      = m;
        hdr->hdrMetadataDirty = true;
    }
    hdr->hasHdrMetadata = true;
}

VkCompositeAlphaFlagBitsKHR __vkPickCompositeAlpha(const VkSurfaceCapabilitiesKHR& caps)
{
    static const VkCompositeAlphaFlagBitsKHR k_order[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (auto bit : k_order)
    {
        if (caps.supportedCompositeAlpha & bit)
            return bit;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

bool __vkPickImageUsage(const VkSurfaceCapabilitiesKHR& caps, VkImageUsageFlags* usage)
{
    const VkImageUsageFlags wanted = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((caps.supportedUsageFlags & wanted) != wanted)
        return false;
    *usage = wanted;
    return true;
}

void __vkDestroyFrameObjects(NativeHDRSurfaceContainer* hdr)
{
    if (hdr->fences)
    {
        if (g_vkDevice != VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < hdr->frameCount; i++)
                if (hdr->fences[i])
                    vkDestroyFence(g_vkDevice, hdr->fences[i], nullptr);
        }
        free(hdr->fences);
        hdr->fences = nullptr;
    }
    if (hdr->acquireSemaphores)
    {
        if (g_vkDevice != VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < hdr->frameCount; i++)
                if (hdr->acquireSemaphores[i])
                    vkDestroySemaphore(g_vkDevice, hdr->acquireSemaphores[i], nullptr);
        }
        free(hdr->acquireSemaphores);
        hdr->acquireSemaphores = nullptr;
    }
    hdr->frameCount = 0;
    hdr->frameIndex = 0;

    if (hdr->renderFinishedSemaphores)
    {
        if (g_vkDevice != VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < hdr->imageCount; i++)
                if (hdr->renderFinishedSemaphores[i])
                    vkDestroySemaphore(g_vkDevice, hdr->renderFinishedSemaphores[i], nullptr);
        }
        free(hdr->renderFinishedSemaphores);
        hdr->renderFinishedSemaphores = nullptr;
    }
    if (hdr->cmdBuffers)
    {
        if (g_vkDevice != VK_NULL_HANDLE && hdr->cmdPool && hdr->imageCount > 0)
            vkFreeCommandBuffers(g_vkDevice, hdr->cmdPool, hdr->imageCount, hdr->cmdBuffers);
        free(hdr->cmdBuffers);
        hdr->cmdBuffers = nullptr;
    }
    free(hdr->imagesInFlight);
    hdr->imagesInFlight = nullptr;
    free(hdr->swapchainImages);
    hdr->swapchainImages = nullptr;
    hdr->imageCount      = 0;
}

EGLBoolean __vkCreateFrameObjects(NativeHDRSurfaceContainer* hdr, uint32_t imgCount)
{
    hdr->cmdBuffers               = reinterpret_cast<VkCommandBuffer*>(malloc(imgCount * sizeof(VkCommandBuffer)));
    hdr->renderFinishedSemaphores = reinterpret_cast<VkSemaphore*>(malloc(imgCount * sizeof(VkSemaphore)));
    hdr->imagesInFlight           = reinterpret_cast<VkFence*>(malloc(imgCount * sizeof(VkFence)));
    if (!hdr->cmdBuffers || !hdr->renderFinishedSemaphores || !hdr->imagesInFlight)
        return EGL_FALSE;
    memset(hdr->cmdBuffers, 0, imgCount * sizeof(VkCommandBuffer));
    memset(hdr->renderFinishedSemaphores, 0, imgCount * sizeof(VkSemaphore));
    memset(hdr->imagesInFlight, 0, imgCount * sizeof(VkFence));

    VkCommandBufferAllocateInfo cbAI = {};
    cbAI.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool                 = hdr->cmdPool;
    cbAI.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount          = imgCount;
    if (vkAllocateCommandBuffers(g_vkDevice, &cbAI, hdr->cmdBuffers) != VK_SUCCESS)
        return EGL_FALSE;

    // From here on all three per-image arrays exist and are valid for imgCount entries.
    hdr->imageCount = imgCount;

    VkSemaphoreCreateInfo semCI = {};
    semCI.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < imgCount; i++)
    {
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->renderFinishedSemaphores[i]) != VK_SUCCESS)
            return EGL_FALSE;
    }

    // One more frame slot than there are images, so the acquire semaphore of the
    // frame we are about to start can never still be pending from an earlier frame.
    const uint32_t frames  = imgCount + 1;
    hdr->acquireSemaphores = reinterpret_cast<VkSemaphore*>(malloc(frames * sizeof(VkSemaphore)));
    hdr->fences            = reinterpret_cast<VkFence*>(malloc(frames * sizeof(VkFence)));
    if (!hdr->acquireSemaphores || !hdr->fences)
        return EGL_FALSE;
    memset(hdr->acquireSemaphores, 0, frames * sizeof(VkSemaphore));
    memset(hdr->fences, 0, frames * sizeof(VkFence));
    hdr->frameCount = frames;
    hdr->frameIndex = 0;

    VkFenceCreateInfo fCI = {};
    fCI.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fCI.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < frames; i++)
    {
        if (vkCreateSemaphore(g_vkDevice, &semCI, nullptr, &hdr->acquireSemaphores[i]) != VK_SUCCESS)
            return EGL_FALSE;
        if (vkCreateFence(g_vkDevice, &fCI, nullptr, &hdr->fences[i]) != VK_SUCCESS)
            return EGL_FALSE;
    }

    return EGL_TRUE;
}

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
    // Flip Y during the GL->GL blit: GL's framebuffer origin is bottom-left
    // while Vulkan stores image memory top-down. Swapping dst Y coords here
    // ensures the swapchain image (which Vulkan blits 1:1 afterwards) ends
    // up right-side up on screen.
    g_pfnBlitFBO(0, 0, static_cast<GLint>(hdr->width), static_cast<GLint>(hdr->height),
                 0, static_cast<GLint>(hdr->height), static_cast<GLint>(hdr->width), 0,
                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g_pfnBindFBO(GL_READ_FRAMEBUFFER, 0);
    g_pfnBindFBO(GL_DRAW_FRAMEBUFFER, 0);

    // Step 2: GL signals the semaphore to notify Vulkan.
    GLenum dstLayout = GL_LAYOUT_GENERAL_EXT;
    g_pfnSignalSemaphore(hdr->glDoneSemObj, 0, nullptr, 1, &hdr->glTexture, &dstLayout);
    if (glFinish_PTR)
        glFinish_PTR();

    // Step 3: wait for this frame slot to become free, then acquire into its own
    // semaphore. VUID-vkAcquireNextImageKHR-semaphore-01779 requires the semaphore
    // to have no pending signal or wait operation, which only a host wait BEFORE the
    // acquire — on the fence of the frame that last used this slot — can guarantee.
    if (hdr->frameCount == 0 || !hdr->fences || !hdr->acquireSemaphores)
        return EGL_FALSE;
    const uint32_t frame = hdr->frameIndex;
    if (hdr->fences[frame])
        vkWaitForFences(g_vkDevice, 1, &hdr->fences[frame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult res        = vkAcquireNextImageKHR(g_vkDevice, hdr->vkSwapchain, UINT64_MAX,
                                                hdr->acquireSemaphores[frame], VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (!__vkRecreateSwapchain(hdr, hdr->glInteropReady) && hdr->vkSwapchain)
        {
            vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr);
            hdr->vkSwapchain = VK_NULL_HANDLE; // leave a no-op state, never half-built
        }
        return EGL_TRUE;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        return EGL_FALSE;
    bool needsRecreate = (res == VK_SUBOPTIMAL_KHR);

    // Any bail-out from here on must drain the acquire semaphore again, otherwise the
    // next frame using this slot would hand a still-signalled semaphore to the acquire.
    auto drainAcquire = [&]()
    {
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo         si    = {};
        si.sType                   = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount      = 1;
        si.pWaitSemaphores         = &hdr->acquireSemaphores[frame];
        si.pWaitDstStageMask       = &stage;
        vkQueueSubmit(g_vkQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vkQueue);
    };

    // Apply HDR mastering metadata to the (valid) swapchain when it changed.
    if (hdr->hasHdrMetadata && hdr->hdrMetadataDirty && g_pfnSetHdrMetadata)
    {
        g_pfnSetHdrMetadata(g_vkDevice, 1, &hdr->vkSwapchain, &hdr->hdrMetadata);
        hdr->hdrMetadataDirty = false;
    }

    // Step 4: the acquired image may still be in flight from an earlier frame that
    // used a different slot — its command buffer must not be re-recorded until then.
    if (hdr->imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(g_vkDevice, 1, &hdr->imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    hdr->imagesInFlight[imageIndex] = hdr->fences[frame];

    // Step 5: Record + submit blit command buffer. The fence is only reset right
    // before the submit, so every early return above leaves it signalled.
    vkResetCommandBuffer(hdr->cmdBuffers[imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(hdr->cmdBuffers[imageIndex], &beginInfo) != VK_SUCCESS)
    {
        drainAcquire();
        return EGL_FALSE;
    }

    auto barrier = [&](VkImage img, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b = {};
        b.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask        = srcAccess;
        b.dstAccessMask        = dstAccess;
        b.oldLayout            = oldLayout;
        b.newLayout            = newLayout;
        b.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        b.image                = img;
        b.subresourceRange     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
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

    // The source is the interop image, which tracks width/height; the destination is
    // a swapchain image, which tracks the extent the swapchain was created with. The
    // two disagree during a live resize and the destination must never be exceeded.
    VkImageBlit blitRegion    = {};
    blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.srcOffsets[1]  = {(int32_t)hdr->width, (int32_t)hdr->height, 1};
    blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blitRegion.dstOffsets[1]  = {(int32_t)hdr->swapchainExtent.width, (int32_t)hdr->swapchainExtent.height, 1};
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

    if (vkEndCommandBuffer(hdr->cmdBuffers[imageIndex]) != VK_SUCCESS)
    {
        drainAcquire();
        return EGL_FALSE;
    }

    // Signal the render-finished semaphore belonging to THIS image: a single shared
    // one would be re-signalled by the next frame while this frame's present is
    // still waiting on it (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    VkSemaphore          waitSems[] = {hdr->acquireSemaphores[frame], hdr->glDoneSemaphore};
    VkPipelineStageFlags stages[]   = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSubmitInfo         submitInfo = {};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 2;
    submitInfo.pWaitSemaphores      = waitSems;
    submitInfo.pWaitDstStageMask    = stages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &hdr->cmdBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &hdr->renderFinishedSemaphores[imageIndex];

    vkResetFences(g_vkDevice, 1, &hdr->fences[frame]);
    if (vkQueueSubmit(g_vkQueue, 1, &submitInfo, hdr->fences[frame]) != VK_SUCCESS)
    {
        // The fence was reset but will never be signalled. Replace it with a fresh
        // signalled one so the next frame in this slot does not wait forever, and
        // drop every in-flight alias to the fence we are about to destroy.
        vkQueueWaitIdle(g_vkQueue);
        vkDestroyFence(g_vkDevice, hdr->fences[frame], nullptr);
        hdr->fences[frame] = VK_NULL_HANDLE;

        VkFenceCreateInfo fCI = {};
        fCI.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fCI.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(g_vkDevice, &fCI, nullptr, &hdr->fences[frame]);

        memset(hdr->imagesInFlight, 0, hdr->imageCount * sizeof(VkFence));
        drainAcquire();
        hdr->frameIndex = (frame + 1) % hdr->frameCount;
        return EGL_FALSE;
    }

    // Step 6: Present
    VkPresentInfoKHR presentInfo   = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &hdr->renderFinishedSemaphores[imageIndex];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &hdr->vkSwapchain;
    presentInfo.pImageIndices      = &imageIndex;
    res                            = vkQueuePresentKHR(g_vkQueue, &presentInfo);
    if (res == VK_SUBOPTIMAL_KHR)
        needsRecreate = true;
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
        needsRecreate = true;

    hdr->frameIndex = (frame + 1) % hdr->frameCount;

    if (needsRecreate)
    {
        if (!__vkRecreateSwapchain(hdr, false) && hdr->vkSwapchain)
        {
            vkDestroySwapchainKHR(g_vkDevice, hdr->vkSwapchain, nullptr);
            hdr->vkSwapchain = VK_NULL_HANDLE; // leave a no-op state, never half-built
        }
    }

    return (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
               ? EGL_TRUE
               : EGL_FALSE;
}

bool __vkIsReady()
{
    return g_vkDevice != VK_NULL_HANDLE;
}


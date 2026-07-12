#include "Ink/Render/Renderer.h"

#include "RendererInternal.h"

#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Synchronous canvas readback (docs/Ink/ROADMAP.md Lot 10) — the headless
//  thumbnail path. A view rendered through the normal frame protocol is copied
//  back to the CPU: wait for the queue (the last submitted frame may still be
//  writing the image), transition display → TRANSFER_SRC, copy into a cached
//  host buffer, transition back to the sampled layout the UI expects, map and
//  return the bytes. The display image is RGBA8 with sRGB-encoded values
//  (kDisplayFormat — the present shader does the encode), so the result is
//  PNG-ready as-is. Save/export only: this blocks, never a per-frame path.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink {

using detail::RendererImpl;
using detail::ViewImpl;

bool Renderer::ReadViewPixels(View* view, std::vector<std::uint8_t>& rgba,
                              std::uint32_t& width, std::uint32_t& height) {
    RendererImpl& r = *impl_;
    if (!view || !view->impl_) return false;
    ViewImpl& v = *view->impl_;
    rhi::Image& img = v.display;
    // Never rendered (or targets not created yet): nothing to read. A copy out
    // of UNDEFINED would be garbage.
    if (!img || img.layout == VK_IMAGE_LAYOUT_UNDEFINED) return false;

    // The frame that drew this view was submitted by EndFrame; make sure it
    // (and anything else on the shared queue) has finished with the image.
    vkQueueWaitIdle(r.device.queue());

    const VkDeviceSize byteSize =
        (VkDeviceSize)img.width * (VkDeviceSize)img.height * 4;
    rhi::Buffer staging = rhi::CreateReadbackBuffer(
        r.device, byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!staging || !staging.mapped) {
        rhi::DestroyBuffer(r.device, staging);
        return false;
    }

    // One image barrier, mirroring the graph's TransitionImage bookkeeping
    // (the Image tracks its own layout/stage/access).
    auto transition = [&](VkCommandBuffer cmd, VkImageLayout layout,
                          VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
        VkImageMemoryBarrier2 b{};
        b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = img.stage == VK_PIPELINE_STAGE_2_NONE
                              ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : img.stage;
        b.srcAccessMask = img.access;
        b.dstStageMask  = stage;
        b.dstAccessMask = access;
        b.oldLayout     = img.layout;
        b.newLayout     = layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image            = img.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        img.layout = layout;
        img.stage  = stage;
        img.access = access;
    };

    const bool ok = rhi::ImmediateSubmit(r.device, [&](VkCommandBuffer cmd) {
        transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset       = { 0, 0, 0 };
        region.imageExtent       = { img.width, img.height, 1 };
        vkCmdCopyImageToBuffer(cmd, img.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging.buffer, 1, &region);
        // Back to the sampled layout the UI reads every frame.
        transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });

    if (ok) {
        rhi::InvalidateBuffer(r.device, staging);
        width  = img.width;
        height = img.height;
        rgba.resize((std::size_t)byteSize);
        std::memcpy(rgba.data(), staging.mapped, (std::size_t)byteSize);
    }
    rhi::DestroyBuffer(r.device, staging);
    return ok;
}

} // namespace Ink

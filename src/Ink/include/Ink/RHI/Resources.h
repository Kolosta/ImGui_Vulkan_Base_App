#pragma once

#include "Ink/RHI/Device.h"
#include <functional>

VK_DEFINE_HANDLE(VmaAllocation)

namespace Ink::rhi {

// ─────────────────────────────────────────────────────────────────────────────
//  Buffer / Image — RAII-free plain structs created and destroyed through the
//  helpers below (ownership is explicit at the call sites: GpuScene, the view
//  targets and the per-frame garbage lists). An Image carries its CURRENT
//  synchronization state (layout/stage/access), updated by the render graph
//  when it emits barriers — that is what lets the graph derive sync2 barriers
//  without a global registry.
// ─────────────────────────────────────────────────────────────────────────────

struct Buffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize  size       = 0;
    void*         mapped     = nullptr;   // non-null for host-visible buffers

    explicit operator bool() const { return buffer != VK_NULL_HANDLE; }
};

// Device-local buffer (filled via staging upload).
Buffer CreateDeviceBuffer(Device& dev, VkDeviceSize size, VkBufferUsageFlags usage);
// Host-visible, persistently-mapped buffer (per-frame data: overlay vertices).
Buffer CreateHostBuffer(Device& dev, VkDeviceSize size, VkBufferUsageFlags usage);
void   DestroyBuffer(Device& dev, Buffer& b);

struct Image {
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView   view       = VK_NULL_HANDLE;
    VkFormat      format     = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0, height = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    // Current sync state, maintained by the graph's TransitionImage.
    VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage  = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2        access = VK_ACCESS_2_NONE;

    explicit operator bool() const { return image != VK_NULL_HANDLE; }
};

Image CreateImage2D(Device& dev, std::uint32_t w, std::uint32_t h, VkFormat fmt,
                    VkImageUsageFlags usage,
                    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
void  DestroyImage(Device& dev, Image& img);

// Record + submit a one-shot command buffer and WAIT for it (init-time uploads
// only — the per-frame path never blocks; see docs/Ink/ARCHITECTURE.md §4).
bool ImmediateSubmit(Device& dev,
                     const std::function<void(VkCommandBuffer)>& record);

// Upload `size` bytes into a device-local buffer through a temporary staging
// buffer + ImmediateSubmit. Init-time only.
bool UploadToBuffer(Device& dev, Buffer& dst, const void* data, VkDeviceSize size);

} // namespace Ink::rhi

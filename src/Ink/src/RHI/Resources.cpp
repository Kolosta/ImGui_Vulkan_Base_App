#include "Ink/RHI/Resources.h"

#include <vk_mem_alloc.h>
#include <cstring>

namespace Ink::rhi {

Buffer CreateDeviceBuffer(Device& dev, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size  = size;
    bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(dev.allocator(), &bi, &ai, &b.buffer, &b.allocation,
                        nullptr) != VK_SUCCESS)
        b = {};
    return b;
}

Buffer CreateHostBuffer(Device& dev, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size  = size;
    bi.usage = usage;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(dev.allocator(), &bi, &ai, &b.buffer, &b.allocation,
                        &info) != VK_SUCCESS)
        return {};
    b.mapped = info.pMappedData;
    return b;
}

void DestroyBuffer(Device& dev, Buffer& b) {
    if (b.buffer)
        vmaDestroyBuffer(dev.allocator(), b.buffer, b.allocation);
    b = {};
}

Image CreateImage2D(Device& dev, std::uint32_t w, std::uint32_t h, VkFormat fmt,
                    VkImageUsageFlags usage, VkSampleCountFlagBits samples) {
    Image img;
    img.format  = fmt;
    img.width   = w;
    img.height  = h;
    img.samples = samples;

    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = fmt;
    ii.extent        = { w, h, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = samples;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(dev.allocator(), &ii, &ai, &img.image, &img.allocation,
                       nullptr) != VK_SUCCESS)
        return {};

    VkImageViewCreateInfo vi{};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = fmt;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(dev.vk(), &vi, nullptr, &img.view) != VK_SUCCESS) {
        vmaDestroyImage(dev.allocator(), img.image, img.allocation);
        return {};
    }
    return img;
}

void DestroyImage(Device& dev, Image& img) {
    if (img.view)  vkDestroyImageView(dev.vk(), img.view, nullptr);
    if (img.image) vmaDestroyImage(dev.allocator(), img.image, img.allocation);
    img = {};
}

bool ImmediateSubmit(Device& dev,
                     const std::function<void(VkCommandBuffer)>& record) {
    VkCommandPoolCreateInfo pi{};
    pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pi.queueFamilyIndex = dev.queueFamily();
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(dev.vk(), &pi, nullptr, &pool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo bi{};
    bi.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    bi.commandPool        = pool;
    bi.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bi.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev.vk(), &bi, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cbi{};
    cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = cmd;
    VkSubmitInfo2 si{};
    si.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos    = &cbi;
    const bool ok = vkQueueSubmit2(dev.queue(), 1, &si, VK_NULL_HANDLE) == VK_SUCCESS;
    if (ok) vkQueueWaitIdle(dev.queue());   // init-time only — never per frame

    vkDestroyCommandPool(dev.vk(), pool, nullptr);
    return ok;
}

bool UploadToBuffer(Device& dev, Buffer& dst, const void* data, VkDeviceSize size) {
    if (!dst || size == 0 || size > dst.size) return false;
    Buffer staging = CreateHostBuffer(dev, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging) return false;
    std::memcpy(staging.mapped, data, (size_t)size);
    const bool ok = ImmediateSubmit(dev, [&](VkCommandBuffer cmd) {
        VkBufferCopy region{ 0, 0, size };
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
    });
    DestroyBuffer(dev, staging);
    return ok;
}

} // namespace Ink::rhi

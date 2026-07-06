#include "Compositor/GPU/Allocator.h"

#include <vk_mem_alloc.h>

namespace Comp {

bool Allocator::Create(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                       uint32_t apiVersion) {
    if (alloc_) return true;
    VmaAllocatorCreateInfo ci{};
    ci.instance         = instance;
    ci.physicalDevice   = phys;
    ci.device           = device;
    ci.vulkanApiVersion = apiVersion;
    return vmaCreateAllocator(&ci, &alloc_) == VK_SUCCESS;
}

void Allocator::Destroy() {
    if (alloc_) {
        vmaDestroyAllocator(alloc_);
        alloc_ = nullptr;
    }
}

bool Allocator::CreateImage(uint32_t w, uint32_t h, VkFormat fmt,
                            VkImageUsageFlags usage,
                            VkImage& outImage, VmaAllocation& outAlloc) {
    if (!alloc_) return false;
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    return vmaCreateImage(alloc_, &ici, &aci, &outImage, &outAlloc, nullptr) == VK_SUCCESS;
}

void Allocator::DestroyImage(VkImage image, VmaAllocation alloc) {
    if (alloc_ && image) vmaDestroyImage(alloc_, image, alloc);
}

bool Allocator::CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkBuffer& outBuf, VmaAllocation& outAlloc,
                                 void** outMapped) {
    if (!alloc_ || size == 0) return false;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    if (vmaCreateBuffer(alloc_, &bci, &aci, &outBuf, &outAlloc, &info) != VK_SUCCESS)
        return false;
    if (outMapped) *outMapped = info.pMappedData;
    return true;
}

bool Allocator::CreateDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkBuffer& outBuf, VmaAllocation& outAlloc) {
    if (!alloc_ || size == 0) return false;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;

    // Device-local, not host-mapped: written by a compute shader (SSBO), read as a
    // vertex buffer. No HOST_ACCESS flag → VMA places it in device-local memory.
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;

    return vmaCreateBuffer(alloc_, &bci, &aci, &outBuf, &outAlloc, nullptr) == VK_SUCCESS;
}

void Allocator::DestroyBuffer(VkBuffer buf, VmaAllocation alloc) {
    if (alloc_ && buf) vmaDestroyBuffer(alloc_, buf, alloc);
}

void Allocator::Flush(VmaAllocation alloc, VkDeviceSize offset, VkDeviceSize size) {
    if (alloc_ && alloc) vmaFlushAllocation(alloc_, alloc, offset, size);
}

} // namespace Comp

#pragma once

#include <vulkan/vulkan.h>

// Forward declaration of VMA's opaque handles, so consumers of this header don't
// need the (heavy) vk_mem_alloc.h. Only Allocator.cpp / VmaImpl.cpp (and later
// the GPU resource wrappers) include the full VMA header.
typedef struct VmaAllocator_T*  VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Comp {

// ── Allocator — thin wrapper over a VmaAllocator ──────────────────────────────
// One per Engine, created from the shared instance / physical-device / device.
// Image and buffer helpers (GpuImage / GpuBuffer) build on top of this in later
// lots; for now it just owns the allocator handle.
class Allocator {
public:
    // Create the VMA allocator. `apiVersion` is the Vulkan version the device was
    // created with (the Compositor requires 1.3). Returns false on failure.
    bool Create(VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                uint32_t apiVersion);
    void Destroy();

    bool         Valid()  const { return alloc_ != nullptr; }
    VmaAllocator Handle() const { return alloc_; }

    // Create a device-local image (e.g. a color-attachment + sampled view target).
    // Returns false on failure. Destroy with DestroyImage.
    bool CreateImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                     VkImage& outImage, VmaAllocation& outAlloc);
    void DestroyImage(VkImage image, VmaAllocation alloc);

    // Create a host-visible, persistently-mapped buffer (e.g. a per-view vertex
    // buffer). `*outMapped` is the CPU pointer; call Flush after writing if the
    // memory isn't coherent (Flush is a no-op on coherent memory). Destroy with
    // DestroyBuffer.
    bool CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& outBuf, VmaAllocation& outAlloc, void** outMapped);
    // Create a DEVICE-LOCAL buffer (not host-mapped): e.g. a compute-written SSBO
    // read back as a vertex buffer (Lot 13-4c). Destroy with DestroyBuffer.
    bool CreateDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkBuffer& outBuf, VmaAllocation& outAlloc);
    void DestroyBuffer(VkBuffer buf, VmaAllocation alloc);
    void Flush(VmaAllocation alloc, VkDeviceSize offset, VkDeviceSize size);

private:
    VmaAllocator alloc_ = nullptr;
};

} // namespace Comp

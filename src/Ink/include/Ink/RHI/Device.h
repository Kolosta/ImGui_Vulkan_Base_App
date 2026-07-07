#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

// Forward-declare the VMA allocator handle so this header does not pull
// vk_mem_alloc.h into every consumer (it is included by the RHI .cpp only).
VK_DEFINE_HANDLE(VmaAllocator)

namespace Ink::rhi {

// ─────────────────────────────────────────────────────────────────────────────
//  Device — Ink's handle bundle around the application's shared Vulkan device
//  (shared-device model: the app creates instance/device/queue, Ink adopts
//  them; see docs/Ink/ARCHITECTURE.md §7). Owns what Ink adds on top: the VMA
//  allocator, a linear-clamp sampler for canvas textures, and the queue's
//  timestamp period. Vulkan 1.3 features (dynamic rendering, sync2) are a
//  precondition checked by the caller.
// ─────────────────────────────────────────────────────────────────────────────
class Device {
public:
    struct CreateInfo {
        VkInstance       instance       = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice         device         = VK_NULL_HANDLE;
        VkQueue          queue          = VK_NULL_HANDLE;
        std::uint32_t    queueFamily    = 0;
    };

    bool Initialize(const CreateInfo& ci);
    void Shutdown();

    VkDevice         vk()             const { return device_; }
    VkPhysicalDevice physical()       const { return physicalDevice_; }
    VkQueue          queue()          const { return queue_; }
    std::uint32_t    queueFamily()    const { return queueFamily_; }
    VmaAllocator     allocator()      const { return allocator_; }
    VkSampler        linearSampler()  const { return linearSampler_; }
    // Nanoseconds per timestamp tick (device limit), for GPU timing stats.
    float            timestampPeriodNs() const { return timestampPeriodNs_; }
    // Max supported MSAA sample count for color render targets, clamped to 4
    // (the engine's target — docs/Ink/ARCHITECTURE.md §6).
    VkSampleCountFlagBits colorSamples() const { return colorSamples_; }

private:
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    std::uint32_t    queueFamily_    = 0;
    VmaAllocator     allocator_      = nullptr;
    VkSampler        linearSampler_  = VK_NULL_HANDLE;
    float            timestampPeriodNs_ = 0.0f;
    VkSampleCountFlagBits colorSamples_ = VK_SAMPLE_COUNT_1_BIT;
};

} // namespace Ink::rhi

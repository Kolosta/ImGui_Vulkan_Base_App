#include "Ink/RHI/Device.h"

#include <vk_mem_alloc.h>

namespace Ink::rhi {

bool Device::Initialize(const CreateInfo& ci) {
    instance_       = ci.instance;
    physicalDevice_ = ci.physicalDevice;
    device_         = ci.device;
    queue_          = ci.queue;
    queueFamily_    = ci.queueFamily;
    if (!instance_ || !physicalDevice_ || !device_ || !queue_) return false;

    // VMA — one allocator for every Ink buffer/image (pools, targets, staging).
    VmaAllocatorCreateInfo ai{};
    ai.instance         = instance_;
    ai.physicalDevice   = physicalDevice_;
    ai.device           = device_;
    ai.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&ai, &allocator_) != VK_SUCCESS) return false;

    // Linear-clamp sampler shared by every sampled canvas image (present
    // input + the app-side ImGui binding of the display texture).
    VkSamplerCreateInfo si{};
    si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter    = VK_FILTER_LINEAR;
    si.minFilter    = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f;
    if (vkCreateSampler(device_, &si, nullptr, &linearSampler_) != VK_SUCCESS)
        return false;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    timestampPeriodNs_ = props.limits.timestampPeriod;

    // MSAA target: ×4 when the device supports it for color, else ×1.
    const VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts;
    colorSamples_ = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT
                                                     : VK_SAMPLE_COUNT_1_BIT;
    return true;
}

void Device::Shutdown() {
    if (linearSampler_) { vkDestroySampler(device_, linearSampler_, nullptr);
                          linearSampler_ = VK_NULL_HANDLE; }
    if (allocator_)     { vmaDestroyAllocator(allocator_); allocator_ = nullptr; }
    // Adopted handles (instance/device/queue) belong to the application.
    instance_ = VK_NULL_HANDLE; physicalDevice_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE; queue_ = VK_NULL_HANDLE;
}

} // namespace Ink::rhi

#pragma once

#include "Ink/RHI/Device.h"
#include <string>
#include <vector>

namespace Ink::rhi {

// ─────────────────────────────────────────────────────────────────────────────
//  Pipeline helpers for dynamic rendering (no VkRenderPass anywhere): load a
//  SPIR-V module and build a graphics pipeline from a compact description.
//  Every pipeline uses dynamic viewport/scissor.
// ─────────────────────────────────────────────────────────────────────────────

// Load "<dir>/<name>.spv". VK_NULL_HANDLE on failure.
VkShaderModule LoadShaderModule(Device& dev, const std::string& path);

struct VertexAttribute {
    std::uint32_t location = 0;
    VkFormat      format   = VK_FORMAT_R32G32_SFLOAT;
    std::uint32_t offset   = 0;
};

struct GraphicsPipelineDesc {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    // One vertex binding (stride 0 = no vertex input at all — fullscreen pass).
    std::uint32_t                vertexStride = 0;
    std::vector<VertexAttribute> attributes;
    VkFormat              colorFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples     = VK_SAMPLE_COUNT_1_BIT;
    // Premultiplied-alpha "over" blending (src=ONE, dst=ONE_MINUS_SRC_ALPHA);
    // false = opaque overwrite (present pass).
    bool blendPremultiplied = true;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

VkPipeline CreateGraphicsPipeline(Device& dev, const GraphicsPipelineDesc& desc);

} // namespace Ink::rhi

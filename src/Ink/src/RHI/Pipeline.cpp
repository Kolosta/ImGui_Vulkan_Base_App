#include "Ink/RHI/Pipeline.h"

#include <cstdint>
#include <fstream>
#include <vector>

namespace Ink::rhi {

VkShaderModule LoadShaderModule(Device& dev, const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return VK_NULL_HANDLE;
    const std::streamsize size = f.tellg();
    if (size <= 0 || (size % 4) != 0) return VK_NULL_HANDLE;
    std::vector<std::uint32_t> words((size_t)size / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(words.data()), size);

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)size;
    ci.pCode    = words.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev.vk(), &ci, nullptr, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return mod;
}

VkPipeline CreateGraphicsPipeline(Device& dev, const GraphicsPipelineDesc& desc) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = desc.vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = desc.frag;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = desc.vertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (const VertexAttribute& a : desc.attributes)
        attrs.push_back({ a.location, 0, a.format, a.offset });

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertexStride > 0) {
        vin.vertexBindingDescriptionCount   = 1;
        vin.pVertexBindingDescriptions      = &binding;
        vin.vertexAttributeDescriptionCount = (std::uint32_t)attrs.size();
        vin.pVertexAttributeDescriptions    = attrs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;    // 2D: winding varies, never cull
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = desc.samples;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blendPremultiplied) {
        // Premultiplied over: out = src + dst·(1−src.a) — the engine's one
        // compositing operator inside a target (docs/Ink/RENDER_GRAPH.md).
        blend.blendEnable         = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp        = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    // A mask-writing pipeline emits no colour (only the stencil is touched).
    if (desc.stencil == StencilMode::WriteMask)
        blend.colorWriteMask = 0;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blend;

    // Stencil (clip masks). WriteMask: always pass, replace stencil with the
    // pipeline's `stencilRef` (0 erases a mask). TestEqual: keep the buffer,
    // draw only where stencil == `stencilRef`. Mode None with a declared
    // stencilFormat = the pass has a stencil attachment this pipeline ignores.
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    if (desc.stencil != StencilMode::None) {
        ds.stencilTestEnable = VK_TRUE;
        VkStencilOpState op{};
        if (desc.stencil == StencilMode::WriteMask) {
            op.compareOp   = VK_COMPARE_OP_ALWAYS;
            op.passOp      = VK_STENCIL_OP_REPLACE;
            op.failOp      = VK_STENCIL_OP_KEEP;
            op.depthFailOp = VK_STENCIL_OP_KEEP;
            op.reference   = desc.stencilRef;
            op.writeMask   = 0xFF;
            op.compareMask = 0xFF;
        } else {   // TestEqual
            op.compareOp   = VK_COMPARE_OP_EQUAL;
            op.passOp      = VK_STENCIL_OP_KEEP;
            op.failOp      = VK_STENCIL_OP_KEEP;
            op.depthFailOp = VK_STENCIL_OP_KEEP;
            op.reference   = desc.stencilRef;
            op.writeMask   = 0x00;
            op.compareMask = 0xFF;
        }
        ds.front = ds.back = op;
    }

    const VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dynStates;

    // Dynamic rendering: the pipeline binds to a color format, not a pass.
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount    = 1;
    rendering.pColorAttachmentFormats = &desc.colorFormat;
    if (desc.stencilFormat != VK_FORMAT_UNDEFINED)
        rendering.stencilAttachmentFormat = desc.stencilFormat;

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.pNext               = &rendering;
    ci.stageCount          = 2;
    ci.pStages             = stages;
    ci.pVertexInputState   = &vin;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState      = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState   = &ms;
    ci.pColorBlendState    = &cb;
    ci.pDynamicState       = &dyn;
    ci.layout              = desc.layout;
    // Required whenever the pass carries a stencil attachment, even if this
    // pipeline's stencil test is disabled (dynamic-rendering VUs).
    if (desc.stencilFormat != VK_FORMAT_UNDEFINED)
        ci.pDepthStencilState = &ds;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev.vk(), VK_NULL_HANDLE, 1, &ci, nullptr,
                                  &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pipeline;
}

} // namespace Ink::rhi

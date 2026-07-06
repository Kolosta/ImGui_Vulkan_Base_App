#include "Compositor/Engine.h"
#include "../Internal.h"

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor · Pipelines — every VkPipeline / VkPipelineLayout the engine owns,
//  plus shader-module loading and the stencil-format probe. Built once at
//  Initialize; the render passes (Passes/RenderView.cpp) only bind them.
//    · blit            — composite a view target onto the swapchain (main pass)
//    · shape/backdrop  — P1 document geometry (+ DST-OVER page substrate, Lot 7)
//    · coverage x3     — P2 Mask/Coverage (stencil-mask / pattern-fill / stroke-fill)
//    · iso/erase       — P4 Composite isolation + P2 subtractive (dst-out, Lot 7)
//    · blend           — P4 Composite blend modes ({iso,backdrop}, Lot 4b)
// (Renderer::Vertex + vulkan.h come transitively via Engine.h -> IViewRenderer.h.)
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {
VkShaderModule Engine::LoadShader(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "[compositor] missing shader: %s\n", path.c_str()); return VK_NULL_HANDLE; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> code((size_t)sz);
    size_t rd = std::fread(code.data(), 1, (size_t)sz, f);
    std::fclose(f);
    if (rd != (size_t)sz || sz <= 0) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = (size_t)sz;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device_, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}

void Engine::CreateCompositeStatics() {
    // Descriptor set layout: one combined image sampler (the view target).
    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo slci{};
    slci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slci.bindingCount = 1;
    slci.pBindings    = &b;
    Check(vkCreateDescriptorSetLayout(device_, &slci, nullptr, &blitSetLayout_),
          "vkCreateDescriptorSetLayout");

    // One descriptor set per view target; free individually on resize/evict.
    VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets       = 64;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &ps;
    Check(vkCreateDescriptorPool(device_, &dpci, nullptr, &blitDescPool_),
          "vkCreateDescriptorPool");

    // Pipeline layout: the set + a vec4 NDC-rect push constant (vertex stage).
    VkPushConstantRange pc{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4 };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &blitSetLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &blitPipeLayout_),
          "vkCreatePipelineLayout");

    blitVert_ = LoadShader(shaderDir_ + "/compositor/blit.vert.spv");
    blitFrag_ = LoadShader(shaderDir_ + "/compositor/blit.frag.spv");

    // Overlay (Lot 12): no descriptor sets, no push — vertices carry NDC + colour.
    VkPipelineLayoutCreateInfo oplci{};
    oplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    Check(vkCreatePipelineLayout(device_, &oplci, nullptr, &overlayPipeLayout_),
          "vkCreatePipelineLayout (overlay)");
    overlayVert_ = LoadShader(shaderDir_ + "/compositor/overlay.vert.spv");
    overlayFrag_ = LoadShader(shaderDir_ + "/compositor/overlay.frag.spv");
}

void Engine::CreateOverlayPipeline(VkRenderPass renderPass) {
    if (overlayPipeline_ != VK_NULL_HANDLE && overlayPipelineRP_ == renderPass) return;
    if (overlayPipeline_) { vkDestroyPipeline(device_, overlayPipeline_, nullptr); overlayPipeline_ = VK_NULL_HANDLE; }
    if (!overlayVert_ || !overlayFrag_ || !overlayPipeLayout_ || renderPass == VK_NULL_HANDLE) return;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = overlayVert_; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = overlayFrag_; stages[1].pName = "main";

    // Vertex input = OverlayVertex { float x,y; uint32 rgba }. rgba unpacks to a
    // normalised vec4 via R8G8B8A8_UNORM (0xAABBGGRR → x=R … w=A, ImGui packing).
    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = sizeof(float) * 2 + sizeof(uint32_t);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;  attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R8G8B8A8_UNORM; attrs[1].offset = sizeof(float) * 2;
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 2; vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
    gp.layout = overlayPipeLayout_; gp.renderPass = renderPass; gp.subpass = 0;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &overlayPipeline_),
          "vkCreateGraphicsPipelines (overlay)");
    overlayPipelineRP_ = renderPass;
}

void Engine::EnsureCompositePipeline(VkRenderPass renderPass) {
    if (blitPipeline_ != VK_NULL_HANDLE && blitPipelineRP_ == renderPass) return;
    if (blitPipeline_) { vkDestroyPipeline(device_, blitPipeline_, nullptr); blitPipeline_ = VK_NULL_HANDLE; }
    if (!blitVert_ || !blitFrag_ || !blitPipeLayout_ || renderPass == VK_NULL_HANDLE) return;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = blitVert_;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = blitFrag_;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &ds;
    gp.layout              = blitPipeLayout_;
    gp.renderPass          = renderPass;
    gp.subpass             = 0;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &blitPipeline_),
          "vkCreateGraphicsPipelines");
    blitPipelineRP_ = renderPass;
}

void Engine::CreateShapePipeline() {
    // Pipeline layout: no descriptor sets, just the 32-byte camera push constant.
    VkPushConstantRange pc{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShapePush) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &shapePipeLayout_),
          "vkCreatePipelineLayout (shape)");

    shapeVert_ = LoadShader(shaderDir_ + "/compositor/shape.vert.spv");
    shapeFrag_ = LoadShader(shaderDir_ + "/compositor/shape.frag.spv");
    if (!shapeVert_ || !shapeFrag_) return;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shapeVert_;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shapeFrag_;
    stages[1].pName  = "main";

    // Vertex input: Renderer::Vertex { vec2 pos; vec4 color; } stride 24.
    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Renderer::Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;       attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = sizeof(float) * 2;
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount   = 1;
    vin.pVertexBindingDescriptions      = &bind;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight-alpha colour; alpha kept opaque (backdrop alpha = 1) so the canvas
    // stays fully opaque for the composite.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    // Depth-stencil state: tests OFF for the base draw, but the pipeline must
    // declare it to be compatible with the rendering's stencil attachment.
    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.stencilTestEnable = VK_FALSE;

    // Dynamic rendering: no render pass object — declare the attachment formats.
    VkFormat colorFmt = kColorFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount    = 1;
    rci.pColorAttachmentFormats = &colorFmt;
    rci.stencilAttachmentFormat = stencilFormat_;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext               = &rci;
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &dss;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &ds;
    gp.layout              = shapePipeLayout_;
    gp.renderPass          = VK_NULL_HANDLE;   // dynamic rendering
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &shapePipeline_),
          "vkCreateGraphicsPipelines (shape)");

    // Page-substrate variant: DST-OVER. Drawn after the object stack so it slides
    // UNDER everything already there (objects + erase holes), filling the page
    // background without ever being cut by an erase.
    //   out.rgb = dst.rgb + src.rgb·(1 − dst.a);  out.a = dst.a + src.a·(1 − dst.a)
    VkPipelineColorBlendAttachmentState bba{};
    bba.blendEnable         = VK_TRUE;
    bba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    bba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    bba.colorBlendOp        = VK_BLEND_OP_ADD;
    bba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    bba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    bba.alphaBlendOp        = VK_BLEND_OP_ADD;
    bba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo bcb{};
    bcb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    bcb.attachmentCount = 1; bcb.pAttachments = &bba;
    gp.pColorBlendState = &bcb;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &backdropPipeline_),
          "vkCreateGraphicsPipelines (backdrop)");
}

void Engine::CreateFillPipelines() {
    // Stencil pass layout: pos-only camera push (ShapePush, 32B, vertex stage).
    VkPushConstantRange spc{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShapePush) };
    VkPipelineLayoutCreateInfo splci{};
    splci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    splci.pushConstantRangeCount = 1;
    splci.pPushConstantRanges    = &spc;
    Check(vkCreatePipelineLayout(device_, &splci, nullptr, &fillStencilPipeLayout_),
          "vkCreatePipelineLayout (fill-stencil)");

    // Cover pass layout: camera + fill colour (FillCoverPush, 48B, vertex stage).
    VkPushConstantRange cpc{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(FillCoverPush) };
    VkPipelineLayoutCreateInfo cplci{};
    cplci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    cplci.pushConstantRangeCount = 1;
    cplci.pPushConstantRanges    = &cpc;
    Check(vkCreatePipelineLayout(device_, &cplci, nullptr, &fillCoverPipeLayout_),
          "vkCreatePipelineLayout (fill-cover)");

    fillStencilVert_ = LoadShader(shaderDir_ + "/compositor/fill_stencil.vert.spv");
    fillCoverVert_   = LoadShader(shaderDir_ + "/compositor/fill_cover.vert.spv");
    fillCoverFrag_   = LoadShader(shaderDir_ + "/compositor/fill_cover.frag.spv");

    // Vertex input = FanVertex { float x, y } — position only, stride 8.
    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = sizeof(FanVertex); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attr{};
    attr.location = 0; attr.binding = 0; attr.format = VK_FORMAT_R32G32_SFLOAT; attr.offset = 0;
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 1; vin.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkFormat colorFmt = kColorFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &colorFmt;
    rci.stencilAttachmentFormat = stencilFormat_;

    // ── 1) STENCIL pass: non-zero winding. Front faces INCR_WRAP, back DECR_WRAP,
    //       compareOp ALWAYS (write regardless), NO colour write. The overlapping
    //       fan triangles net to non-zero inside, zero outside; holes (reverse
    //       winding) cancel. Reference/compareMask irrelevant (it counts, not tests).
    if (fillStencilVert_) {
        VkPipelineShaderStageCreateInfo st{};
        st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st.stage = VK_SHADER_STAGE_VERTEX_BIT; st.module = fillStencilVert_; st.pName = "main";

        VkStencilOpState front{};
        front.failOp = VK_STENCIL_OP_KEEP; front.depthFailOp = VK_STENCIL_OP_KEEP;
        front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP; front.compareOp = VK_COMPARE_OP_ALWAYS;
        front.compareMask = 0xFF; front.writeMask = 0xFF; front.reference = 0;
        VkStencilOpState back = front;
        back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = front; dss.back = back;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_FALSE; cba.colorWriteMask = 0;   // stencil only, no colour
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gp.pNext = &rci;
        gp.stageCount = 1; gp.pStages = &st;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
        gp.layout = fillStencilPipeLayout_; gp.renderPass = VK_NULL_HANDLE;
        Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &fillStencilPipeline_),
              "vkCreateGraphicsPipelines (fill-stencil)");
    }

    // ── 2) COVER pass: draw the bbox quad where stencil != 0 (interior), ONCE, with
    //       the fill colour (straight-alpha over). passOp REPLACE ref 0 clears the
    //       stencil on covered pixels so the next object starts clean (no separate
    //       clear). Outside the interior stencil == 0 → NOT_EQUAL fails → nothing.
    if (fillCoverVert_ && fillCoverFrag_) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = fillCoverVert_; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fillCoverFrag_; stages[1].pName = "main";

        VkStencilOpState test{};
        test.failOp = VK_STENCIL_OP_KEEP; test.depthFailOp = VK_STENCIL_OP_KEEP;
        test.passOp = VK_STENCIL_OP_REPLACE; test.compareOp = VK_COMPARE_OP_NOT_EQUAL;
        test.compareMask = 0xFF; test.writeMask = 0xFF; test.reference = 0;   // ref 0 → reset
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = test; dss.back = test;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gp.pNext = &rci;
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
        gp.layout = fillCoverPipeLayout_; gp.renderPass = VK_NULL_HANDLE;
        Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &fillCoverPipeline_),
              "vkCreateGraphicsPipelines (fill-cover)");
    }
}

void Engine::CreateCoveragePipelines() {
    // One 96-byte push layout (camera + pattern params), VERTEX+FRAGMENT, no sets.
    VkPushConstantRange pc{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(PatternPush) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &coverPipeLayout_),
          "vkCreatePipelineLayout (cover)");

    patternVert_ = LoadShader(shaderDir_ + "/compositor/pattern_fill.vert.spv");
    patternFrag_ = LoadShader(shaderDir_ + "/compositor/pattern_fill.frag.spv");

    // Shared state. Vertex input = Renderer::Vertex { vec2 pos; vec4 color; }.
    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Renderer::Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;       attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = sizeof(float) * 2;
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount   = 1;
    vin.pVertexBindingDescriptions      = &bind;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The stencil reference is set dynamically per surface.
    VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                              VK_DYNAMIC_STATE_STENCIL_REFERENCE };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 3; ds.pDynamicStates = dyn;

    VkFormat colorFmt = kColorFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount    = 1;
    rci.pColorAttachmentFormats = &colorFmt;
    rci.stencilAttachmentFormat = stencilFormat_;

    // Straight-alpha colour blend (kept opaque alpha, like the base pipeline).
    VkPipelineColorBlendAttachmentState cbaBlend{};
    cbaBlend.blendEnable         = VK_TRUE;
    cbaBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbaBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbaBlend.colorBlendOp        = VK_BLEND_OP_ADD;
    cbaBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbaBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbaBlend.alphaBlendOp        = VK_BLEND_OP_ADD;
    cbaBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    auto buildPipeline = [&](VkShaderModule vert, VkShaderModule frag,
                             const VkPipelineDepthStencilStateCreateInfo& dss,
                             VkColorComponentFlags writeMask,
                             VkPipeline& out) {
        if (!vert || !frag) return;
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vert; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

        VkPipelineColorBlendAttachmentState cba = cbaBlend;
        cba.colorWriteMask = writeMask;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.pNext               = &rci;
        gp.stageCount          = 2;
        gp.pStages             = stages;
        gp.pVertexInputState   = &vin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState      = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState   = &ms;
        gp.pDepthStencilState  = &dss;
        gp.pColorBlendState    = &cb;
        gp.pDynamicState       = &ds;
        gp.layout              = coverPipeLayout_;
        gp.renderPass          = VK_NULL_HANDLE;
        Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &out),
              "vkCreateGraphicsPipelines (cover)");
    };

    // 1) stencil-mask: write the cover/ribbon to the stencil (REPLACE ref), no colour.
    VkStencilOpState write{};
    write.failOp = VK_STENCIL_OP_KEEP; write.depthFailOp = VK_STENCIL_OP_KEEP;
    write.passOp = VK_STENCIL_OP_REPLACE; write.compareOp = VK_COMPARE_OP_ALWAYS;
    write.compareMask = 0xFF; write.writeMask = 0xFF; write.reference = 0;
    VkPipelineDepthStencilStateCreateInfo dssWrite{};
    dssWrite.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dssWrite.stencilTestEnable = VK_TRUE; dssWrite.front = write; dssWrite.back = write;
    buildPipeline(shapeVert_, shapeFrag_, dssWrite, 0, stencilMaskPipeline_);

    // 2/3) test EQUAL ref, no stencil write — draw only the covered pixels, once.
    VkStencilOpState test{};
    test.failOp = VK_STENCIL_OP_KEEP; test.depthFailOp = VK_STENCIL_OP_KEEP;
    test.passOp = VK_STENCIL_OP_KEEP; test.compareOp = VK_COMPARE_OP_EQUAL;
    test.compareMask = 0xFF; test.writeMask = 0x00; test.reference = 0;
    VkPipelineDepthStencilStateCreateInfo dssTest{};
    dssTest.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dssTest.stencilTestEnable = VK_TRUE; dssTest.front = test; dssTest.back = test;
    const VkColorComponentFlags rgba = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    buildPipeline(patternVert_, patternFrag_, dssTest, rgba, patternFillPipeline_);
    buildPipeline(shapeVert_,   shapeFrag_,   dssTest, rgba, strokeFillPipeline_);
}

void Engine::CreateBlendPipeline() {
    // Set layout: two combined image samplers (iso = src, backdrop = dst).
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0; b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1; b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1] = b[0]; b[1].binding = 1;
    VkDescriptorSetLayoutCreateInfo slci{};
    slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slci.bindingCount = 2; slci.pBindings = b;
    Check(vkCreateDescriptorSetLayout(device_, &slci, nullptr, &blendSetLayout_),
          "vkCreateDescriptorSetLayout (blend)");

    VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(BlendPushC) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = &blendSetLayout_;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &blendPipeLayout_),
          "vkCreatePipelineLayout (blend)");

    blendFrag_ = LoadShader(shaderDir_ + "/compositor/iso_blend.frag.spv");
    if (!isoVert_ || !blendFrag_) return;   // reuse iso_composite.vert

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = isoVert_;   stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = blendFrag_; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // No fixed-function blend: the shader outputs the final composited colour.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkFormat colorFmt = kColorFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &colorFmt;
    rci.stencilAttachmentFormat = stencilFormat_;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext = &rci; gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
    gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
    gp.layout = blendPipeLayout_; gp.renderPass = VK_NULL_HANDLE;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &blendPipeline_),
          "vkCreateGraphicsPipelines (blend)");
}

void Engine::CreatePickingPipeline() {
    // Layout: the camera+id push (vertex stage), no descriptor sets.
    VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PickPush) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &pickPipeLayout_),
          "vkCreatePipelineLayout (pick)");

    pickVert_ = LoadShader(shaderDir_ + "/compositor/picking.vert.spv");
    pickFrag_ = LoadShader(shaderDir_ + "/compositor/picking.frag.spv");
    if (!pickVert_ || !pickFrag_) return;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = pickVert_; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = pickFrag_; stages[1].pName = "main";

    // Vertex input = Renderer::Vertex { vec2 pos; vec4 color; } (color ignored).
    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Renderer::Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT;       attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = sizeof(float) * 2;
    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount   = 1;
    vin.pVertexBindingDescriptions      = &bind;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;   // no depth/stencil

    // Integer target: blending is invalid for a UINT format → disabled.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable    = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;   // single R32UI channel
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkFormat colorFmt = kPickFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount    = 1;
    rci.pColorAttachmentFormats = &colorFmt;   // no stencil in the id-pass

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext               = &rci;
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &dss;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &ds;
    gp.layout              = pickPipeLayout_;
    gp.renderPass          = VK_NULL_HANDLE;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pickPipeline_),
          "vkCreateGraphicsPipelines (pick)");
}

void Engine::CreateIsoCompositePipeline() {
    VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(IsoPushC) };
    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &blitSetLayout_;     // 1 combined image sampler
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(device_, &plci, nullptr, &isoCompPipeLayout_),
          "vkCreatePipelineLayout (iso)");

    isoVert_ = LoadShader(shaderDir_ + "/compositor/iso_composite.vert.spv");
    isoFrag_ = LoadShader(shaderDir_ + "/compositor/iso_composite.frag.spv");
    if (!isoVert_ || !isoFrag_) return;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = isoVert_; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = isoFrag_; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // Drawn into the canvas pass (has a stencil attachment) → declare format, test off.
    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // The frag un-premultiplies the isolation texel to STRAIGHT, so composite it
    // straight-over (SRC_ALPHA) — rgb ends up × alpha exactly once total, no per-group
    // darkening. Alpha is over too (src.a + dst·(1−src.a)).
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
    VkFormat colorFmt = kColorFormat;
    VkPipelineRenderingCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1; rci.pColorAttachmentFormats = &colorFmt;
    rci.stencilAttachmentFormat = stencilFormat_;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.pNext = &rci; gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
    gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
    gp.layout = isoCompPipeLayout_; gp.renderPass = VK_NULL_HANDLE;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &isoCompPipeline_),
          "vkCreateGraphicsPipelines (iso)");

    // P2 subtractive / erase (Lot 7): identical pipeline, dst-out blend. The frag
    // outputs (rgb, src.a·opacity); src colour factor ZERO ignores rgb, and both
    // colour and alpha keep dst·(1 − src.a) → canvas.a → canvas.a·(1 − src.a·op),
    // canvas.rgb preserved → a hole down to transparency.
    VkPipelineColorBlendAttachmentState eba{};
    eba.blendEnable         = VK_TRUE;
    eba.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    eba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    eba.colorBlendOp        = VK_BLEND_OP_ADD;
    eba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    eba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    eba.alphaBlendOp        = VK_BLEND_OP_ADD;
    eba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo ecb{};
    ecb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ecb.attachmentCount = 1; ecb.pAttachments = &eba;
    gp.pColorBlendState = &ecb;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &eraseCompPipeline_),
          "vkCreateGraphicsPipelines (erase)");
}

VkFormat Engine::ChooseStencilFormat() const {
    const VkFormat cands[] = { VK_FORMAT_D24_UNORM_S8_UINT,
                               VK_FORMAT_D32_SFLOAT_S8_UINT,
                               VK_FORMAT_S8_UINT };
    for (VkFormat f : cands) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    return VK_FORMAT_D24_UNORM_S8_UINT;
}

} // namespace Comp

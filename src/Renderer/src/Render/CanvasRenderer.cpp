#include "Renderer/Render/CanvasRenderer.h"
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace Renderer {

// Sentinel build signature meaning "the last build left shapes DEFERRED (budget ran
// out), re-attempt next frame". It never equals a real FNV-1a signature (which starts
// from the FNV offset basis and mixes data), so a deferred view always re-builds.
static constexpr uint64_t kDeferredSig = 0ull;

static void CheckVk(VkResult err, const char* what) {
    if (err != VK_SUCCESS)
        fprintf(stderr, "[Renderer] %s failed: VkResult %d\n", what, (int)err);
}

void CanvasRenderer::Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                VkQueue queue, uint32_t queueFamily,
                                VkCommandPool commandPool, VkSampler sampler,
                                const std::string& shaderDir) {
    device_         = device;
    physicalDevice_ = physicalDevice;
    queue_          = queue;
    queueFamily_    = queueFamily;
    commandPool_    = commandPool;
    sampler_        = sampler;

    stencilFormat_ = ChooseStencilFormat();
    CreateRenderPass();
    CreatePipeline(shaderDir);
    CreatePatternPipelines(shaderDir);   // Phase 2: stencil mask + instanced pattern
    CreateBaseMeshes();                   // unit disc / triangle / quad

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = commandPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(device_, &ai, &oneShotCmd_), "vkAllocateCommandBuffers(oneshot)");

    // Offscreen submission slots: one command buffer + fence (created SIGNALLED so
    // the first acquire doesn't block) + semaphore each. RenderView round-robins
    // through these, submitting offscreen passes that signal `sem` (no CPU wait).
    for (int i = 0; i < kSubmitSlots; ++i) {
        VkCommandBufferAllocateInfo sai{};
        sai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        sai.commandPool = commandPool_; sai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        sai.commandBufferCount = 1;
        CheckVk(vkAllocateCommandBuffers(device_, &sai, &slots_[i].cmd), "vkAllocateCommandBuffers(slot)");
        VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CheckVk(vkCreateFence(device_, &fci, nullptr, &slots_[i].fence), "vkCreateFence(slot)");
        VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        CheckVk(vkCreateSemaphore(device_, &sci, nullptr, &slots_[i].sem), "vkCreateSemaphore(slot)");
    }

    initialized_ = (renderPass_ && pipeline_ && oneShotCmd_ && slots_[0].cmd);
}

void CanvasRenderer::Shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);

    for (auto& [key, t] : targets_) DestroyTarget(t);
    targets_.clear();

    DestroyVertexBuffer();
    if (baseMeshVbo_)    vkDestroyBuffer(device_, baseMeshVbo_, nullptr);
    if (baseMeshMemory_) vkFreeMemory(device_, baseMeshMemory_, nullptr);

    if (oneShotCmd_)     vkFreeCommandBuffers(device_, commandPool_, 1, &oneShotCmd_);
    cmd_ = VK_NULL_HANDLE;   // alias of a slot cmd; freed via slots below
    for (int i = 0; i < kSubmitSlots; ++i) {
        if (slots_[i].cmd)   vkFreeCommandBuffers(device_, commandPool_, 1, &slots_[i].cmd);
        if (slots_[i].fence) vkDestroyFence(device_, slots_[i].fence, nullptr);
        if (slots_[i].sem)   vkDestroySemaphore(device_, slots_[i].sem, nullptr);
        slots_[i] = SubmitSlot{};
    }
    if (pipeline_)       vkDestroyPipeline(device_, pipeline_, nullptr);
    if (stencilMaskPipeline_) vkDestroyPipeline(device_, stencilMaskPipeline_, nullptr);
    if (patternFillPipeline_) vkDestroyPipeline(device_, patternFillPipeline_, nullptr);
    if (patternInstPipeline_) vkDestroyPipeline(device_, patternInstPipeline_, nullptr);
    if (decorInstPipeline_)   vkDestroyPipeline(device_, decorInstPipeline_, nullptr);
    if (strokeFillPipeline_)  vkDestroyPipeline(device_, strokeFillPipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_)     vkDestroyRenderPass(device_, renderPass_, nullptr);

    cmd_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;
    stencilMaskPipeline_ = VK_NULL_HANDLE;
    patternFillPipeline_ = VK_NULL_HANDLE;
    patternInstPipeline_ = VK_NULL_HANDLE;
    decorInstPipeline_   = VK_NULL_HANDLE;
    baseMeshVbo_ = VK_NULL_HANDLE;
    baseMeshMemory_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    initialized_ = false;
}

// Pick a stencil-capable depth/stencil format the device supports as a render
// target. Prefer pure S8; fall back to the ubiquitous combined formats.
VkFormat CanvasRenderer::ChooseStencilFormat() const {
    const VkFormat cands[] = { VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT,
                               VK_FORMAT_D32_SFLOAT_S8_UINT };
    for (VkFormat f : cands) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, f, &p);
        if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    return VK_FORMAT_D24_UNORM_S8_UINT;   // last resort
}

// ── Render pass: colour attachment (cleared, left SHADER_READ_ONLY so ImGui can
//    sample it) + a stencil attachment for the per-surface pattern clip mask.
void CanvasRenderer::CreateRenderPass() {
    VkAttachmentDescription att[2]{};
    VkAttachmentDescription& color = att[0];
    color.format         = colorFormat_;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription& ds = att[1];
    ds.format         = stencilFormat_;
    ds.samples        = VK_SAMPLE_COUNT_1_BIT;
    ds.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // depth unused
    ds.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ds.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;       // mask starts at 0
    ds.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ds.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    ds.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference dsRef{};
    dsRef.attachment = 1;
    dsRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &colorRef;
    sub.pDepthStencilAttachment = &dsRef;

    // Two dependencies: external→subpass (acquire as colour + stencil attachment)
    // and subpass→external (release for the fragment-shader read by ImGui).
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments    = att;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies   = deps;
    CheckVk(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_), "vkCreateRenderPass");
}

VkShaderModule CanvasRenderer::LoadShader(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open()) {
        fprintf(stderr, "[Renderer] cannot open shader: %s\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    size_t size = (size_t)f.tellg();
    std::vector<char> code(size);
    f.seekg(0);
    f.read(code.data(), (std::streamsize)size);
    f.close();

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(device_, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}

void CanvasRenderer::CreatePipeline(const std::string& shaderDir) {
    VkShaderModule vert = LoadShader(shaderDir + "/shape.vert.spv");
    VkShaderModule frag = LoadShader(shaderDir + "/shape.frag.spv");
    if (!vert || !frag) {
        if (vert) vkDestroyShaderModule(device_, vert, nullptr);
        if (frag) vkDestroyShaderModule(device_, frag, nullptr);
        return;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName  = "main";

    // Vertex layout: vec2 pos (loc 0) + vec4 color (loc 1), interleaved.
    VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset   = offsetof(Vertex, x);
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset   = offsetof(Vertex, r);

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

    // Standard straight-alpha blend.
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    // Depth/stencil state: the render pass now has a stencil attachment, so a
    // depth-stencil state is required. The base pipeline ignores both.
    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable  = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.stencilTestEnable= VK_FALSE;

    VkPushConstantRange pcr{};
    // VERTEX | FRAGMENT: the camera subset is read by every vertex shader; the
    // pattern block (bytes 32..95) is read by pattern_fill.frag. Shaders that
    // declare only the camera subset are valid against this larger range.
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    CheckVk(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_),
            "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    gp.layout              = pipelineLayout_;
    gp.renderPass          = renderPass_;
    gp.subpass             = 0;
    CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

// ── Phase 2 pipelines: stencil-mask-write + instanced pattern ─────────────────
void CanvasRenderer::CreatePatternPipelines(const std::string& shaderDir) {
    if (!pipelineLayout_) return;
    VkShaderModule shapeVert = LoadShader(shaderDir + "/shape.vert.spv");
    VkShaderModule patVert   = LoadShader(shaderDir + "/pattern.vert.spv");
    VkShaderModule frag      = LoadShader(shaderDir + "/shape.frag.spv");
    VkShaderModule fillVert  = LoadShader(shaderDir + "/pattern_fill.vert.spv");
    VkShaderModule fillFrag  = LoadShader(shaderDir + "/pattern_fill.frag.spv");
    if (!shapeVert || !patVert || !frag || !fillVert || !fillFrag) {
        if (shapeVert) vkDestroyShaderModule(device_, shapeVert, nullptr);
        if (patVert)   vkDestroyShaderModule(device_, patVert, nullptr);
        if (frag)      vkDestroyShaderModule(device_, frag, nullptr);
        if (fillVert)  vkDestroyShaderModule(device_, fillVert, nullptr);
        if (fillFrag)  vkDestroyShaderModule(device_, fillFrag, nullptr);
        return;
    }

    // Shared fixed-function state (viewport/scissor + stencil reference dynamic).
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
    VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                              VK_DYNAMIC_STATE_STENCIL_REFERENCE };
    VkPipelineDynamicStateCreateInfo dynS{};
    dynS.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynS.dynamicStateCount = 3; dynS.pDynamicStates = dyn;

    // ── (1) Stencil-mask-write: draw the cut polygon, write REPLACE ref, no colour.
    {
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = shapeVert; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = frag;      st[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Vertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, r) };
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
        vin.vertexAttributeDescriptionCount = 2; vin.pVertexAttributeDescriptions = attrs;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0;            // write nothing to colour, only the stencil
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkStencilOpState so{};
        so.failOp = VK_STENCIL_OP_REPLACE; so.passOp = VK_STENCIL_OP_REPLACE;
        so.depthFailOp = VK_STENCIL_OP_REPLACE; so.compareOp = VK_COMPARE_OP_ALWAYS;
        so.compareMask = 0xFF; so.writeMask = 0xFF; so.reference = 0;   // dynamic
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = so; dss.back = so;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dynS;
        gp.layout = pipelineLayout_; gp.renderPass = renderPass_; gp.subpass = 0;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                &stencilMaskPipeline_), "vkCreateGraphicsPipelines(mask)");
    }

    // ── (2) Instanced pattern: unit base mesh × per-instance, stencil-test EQUAL ref.
    {
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = patVert; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = frag;    st[1].pName = "main";

        VkVertexInputBindingDescription binds[2]{};
        binds[0].binding = 0; binds[0].stride = sizeof(float) * 2;        // base unit pos
        binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binds[1].binding = 1; binds[1].stride = sizeof(PatternInstance);
        binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        VkVertexInputAttributeDescription attrs[5]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       0 };                                  // base pos
        attrs[1] = { 2, 1, VK_FORMAT_R32G32_SFLOAT,       offsetof(PatternInstance, cx) };
        attrs[2] = { 3, 1, VK_FORMAT_R32G32_SFLOAT,       offsetof(PatternInstance, sx) };
        attrs[3] = { 4, 1, VK_FORMAT_R32_SFLOAT,          offsetof(PatternInstance, rot) };
        attrs[4] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(PatternInstance, r) };
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 2; vin.pVertexBindingDescriptions = binds;
        vin.vertexAttributeDescriptionCount = 5; vin.pVertexAttributeDescriptions = attrs;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkStencilOpState so{};
        so.failOp = VK_STENCIL_OP_KEEP; so.passOp = VK_STENCIL_OP_KEEP;
        so.depthFailOp = VK_STENCIL_OP_KEEP; so.compareOp = VK_COMPARE_OP_EQUAL;
        so.compareMask = 0xFF; so.writeMask = 0x00; so.reference = 0;   // dynamic
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = so; dss.back = so;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dynS;
        gp.layout = pipelineLayout_; gp.renderPass = renderPass_; gp.subpass = 0;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                &patternInstPipeline_), "vkCreateGraphicsPipelines(pattern)");

        // (2b) Curve decorators: same instanced layout, but NO stencil test (they
        // are not clipped to a surface). Reuses every struct above; only the
        // depth-stencil state differs.
        VkPipelineDepthStencilStateCreateInfo dssNo{};
        dssNo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dssNo.stencilTestEnable = VK_FALSE;
        gp.pDepthStencilState = &dssNo;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                &decorInstPipeline_), "vkCreateGraphicsPipelines(decor)");
    }

    // ── (3) Procedural fill: a cover quad (the cut polygon) + pattern_fill.frag,
    //        stencil-test EQUAL ref. ONE Vertex binding (no instancing).
    {
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = fillVert; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = fillFrag; st[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Vertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, r) };
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
        vin.vertexAttributeDescriptionCount = 2; vin.pVertexAttributeDescriptions = attrs;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkStencilOpState so{};
        so.failOp = VK_STENCIL_OP_KEEP; so.passOp = VK_STENCIL_OP_KEEP;
        so.depthFailOp = VK_STENCIL_OP_KEEP; so.compareOp = VK_COMPARE_OP_EQUAL;
        so.compareMask = 0xFF; so.writeMask = 0x00; so.reference = 0;   // dynamic
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = so; dss.back = so;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dynS;
        gp.layout = pipelineLayout_; gp.renderPass = renderPass_; gp.subpass = 0;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                &patternFillPipeline_), "vkCreateGraphicsPipelines(fill)");
    }

    // ── (4) Transparent-stroke fill (Lot A): the stroke's bbox quad with the BASE
    //        shaders (flat per-vertex colour) + blend, stencil-test EQUAL ref. Drawn
    //        ONCE per stroke after its ribbon coverage was written to the stencil, so
    //        each covered pixel blends exactly once (no alpha doubling on the
    //        overlapping ribbon). Same single-Vertex binding as (3).
    {
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   st[0].module = shapeVert; st[0].pName = "main";
        st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = frag;      st[1].pName = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding = 0; bind.stride = sizeof(Vertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x) };
        attrs[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, r) };
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vin.vertexBindingDescriptionCount = 1; vin.pVertexBindingDescriptions = &bind;
        vin.vertexAttributeDescriptionCount = 2; vin.pVertexAttributeDescriptions = attrs;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkStencilOpState so{};
        so.failOp = VK_STENCIL_OP_KEEP; so.passOp = VK_STENCIL_OP_KEEP;
        so.depthFailOp = VK_STENCIL_OP_KEEP; so.compareOp = VK_COMPARE_OP_EQUAL;
        so.compareMask = 0xFF; so.writeMask = 0x00; so.reference = 0;   // dynamic
        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.stencilTestEnable = VK_TRUE; dss.front = so; dss.back = so;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &dss;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dynS;
        gp.layout = pipelineLayout_; gp.renderPass = renderPass_; gp.subpass = 0;
        CheckVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr,
                &strokeFillPipeline_), "vkCreateGraphicsPipelines(strokeFill)");
    }

    vkDestroyShaderModule(device_, shapeVert, nullptr);
    vkDestroyShaderModule(device_, patVert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    vkDestroyShaderModule(device_, fillVert, nullptr);
    vkDestroyShaderModule(device_, fillFrag, nullptr);
}

// Build the three static unit base meshes (positions only) into one device buffer.
void CanvasRenderer::CreateBaseMeshes() {
    std::vector<float> v;   // x,y pairs
    auto kind = [&](PatternElementKind k) { return (int)k; };

    // Disc: radius-0.5 fan as a triangle list (centre + ring), ~20 segments.
    {
        baseRange_[kind(PatternElementKind::Disc)].first = (uint32_t)(v.size() / 2);
        const int N = 20; const float R = 0.5f;
        for (int i = 0; i < N; ++i) {
            float a0 = (float)i / N * 6.2831853f, a1 = (float)(i + 1) / N * 6.2831853f;
            v.insert(v.end(), { 0.0f, 0.0f,
                                std::cos(a0)*R, std::sin(a0)*R,
                                std::cos(a1)*R, std::sin(a1)*R });
        }
        baseRange_[kind(PatternElementKind::Disc)].count =
            (uint32_t)(v.size() / 2) - baseRange_[kind(PatternElementKind::Disc)].first;
    }
    // Triangle: unit 8:6:5 scalene, centroid-centred, longest side == 1 (scaled by
    // the instance `size`, matching AppendFillLayer's `s8 = size`).
    {
        baseRange_[kind(PatternElementKind::Triangle)].first = (uint32_t)(v.size() / 2);
        const float s8 = 1.0f, s6 = 0.75f, s5 = 0.625f;
        float cx = (s8*s8 + s6*s6 - s5*s5) / (2.0f * s8);
        float cy = std::sqrt(std::max(0.0f, s6*s6 - cx*cx));
        float ex = (0 + s8 + cx) / 3.0f, ey = (0 + 0 + cy) / 3.0f;
        v.insert(v.end(), { 0 - ex, 0 - ey,  s8 - ex, 0 - ey,  cx - ex, cy - ey });
        baseRange_[kind(PatternElementKind::Triangle)].count =
            (uint32_t)(v.size() / 2) - baseRange_[kind(PatternElementKind::Triangle)].first;
    }
    // Quad: unit 1×1 centred — ticks/bars/pickets/cross-arms (sx=length on X,
    // sy=thickness on Y; instance rot aligns +X with the glyph direction).
    {
        baseRange_[kind(PatternElementKind::Quad)].first = (uint32_t)(v.size() / 2);
        v.insert(v.end(), { -0.5f,-0.5f,  0.5f,-0.5f,  0.5f,0.5f,
                            -0.5f,-0.5f,  0.5f,0.5f,  -0.5f,0.5f });
        baseRange_[kind(PatternElementKind::Quad)].count =
            (uint32_t)(v.size() / 2) - baseRange_[kind(PatternElementKind::Quad)].first;
    }
    // HalfDisc: radius-0.5 half-disc, flat diameter on local X, bulge toward +Y —
    // HalfDots (instance rot = tangent angle puts the flat edge on the line).
    {
        baseRange_[kind(PatternElementKind::HalfDisc)].first = (uint32_t)(v.size() / 2);
        const int N = 12; const float R = 0.5f;
        for (int i = 0; i < N; ++i) {
            float a0 = (float)i / N * 3.14159265f, a1 = (float)(i + 1) / N * 3.14159265f;
            v.insert(v.end(), { 0.0f, 0.0f,
                                std::cos(a0)*R, std::sin(a0)*R,
                                std::cos(a1)*R, std::sin(a1)*R });
        }
        baseRange_[kind(PatternElementKind::HalfDisc)].count =
            (uint32_t)(v.size() / 2) - baseRange_[kind(PatternElementKind::HalfDisc)].first;
    }

    const VkDeviceSize bytes = v.size() * sizeof(float);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes; bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(device_, &bi, nullptr, &baseMeshVbo_), "vkCreateBuffer(baseMesh)");
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, baseMeshVbo_, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CheckVk(vkAllocateMemory(device_, &ai, nullptr, &baseMeshMemory_), "vkAllocateMemory(baseMesh)");
    vkBindBufferMemory(device_, baseMeshVbo_, baseMeshMemory_, 0);
    void* map = nullptr; vkMapMemory(device_, baseMeshMemory_, 0, bytes, 0, &map);
    memcpy(map, v.data(), (size_t)bytes); vkUnmapMemory(device_, baseMeshMemory_);
}

// ── Per-frame ────────────────────────────────────────────────────────────────
void CanvasRenderer::BeginFrame() {
    ++frame_;
    cache_.BeginFrame();
    glyphCache_.BeginFrame();
    metricsAccum_ = Metrics{};      // reset; RenderView accumulates into it
    // Any offscreen-done semaphores from last frame that the main pass DIDN'T consume
    // (e.g. the window was minimized after the views rendered) are still signalled —
    // re-signalling them this frame would be invalid. Drain them with a one-shot wait
    // so they return to the unsignalled state. The common path is empty (the main
    // pass waited on all of them), so this is a no-op.
    if (!framePendingWaits_.empty()) {
        std::vector<VkPipelineStageFlags> stages(framePendingWaits_.size(),
                                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = (uint32_t)framePendingWaits_.size();
        si.pWaitSemaphores    = framePendingWaits_.data();
        si.pWaitDstStageMask  = stages.data();
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);
    }
    framePendingWaits_.clear();
}

void CanvasRenderer::EndFrame() {
    // Evict targets not touched this frame (their zone was joined/closed).
    for (auto it = targets_.begin(); it != targets_.end(); ) {
        if (it->second.lastUsedFrame != frame_) {
            DestroyTarget(it->second);
            it = targets_.erase(it);
        } else {
            ++it;
        }
    }
    cache_.Evict();                 // drop shapes not drawn this frame
    glyphCache_.Evict();            // drop glyph shapes not rendered recently
    EvictGlyphTextures();           // release glyph textures from closed panels
    metrics_ = metricsAccum_;       // publish the frame's metrics
}

uint32_t CanvasRenderer::FindMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0;
}

void CanvasRenderer::CreateTargetImages(RenderTarget& t, uint32_t w, uint32_t h) {
    VkImageCreateInfo ii{};
    ii.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.extent        = { w, h, 1 };
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.format        = colorFormat_;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(device_, &ii, nullptr, &t.image), "vkCreateImage(target)");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, t.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(device_, &ai, nullptr, &t.memory), "vkAllocateMemory(target)");
    vkBindImageMemory(device_, t.image, t.memory, 0);

    VkImageViewCreateInfo vi{};
    vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image                       = t.image;
    vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    vi.format                      = colorFormat_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(device_, &vi, nullptr, &t.view), "vkCreateImageView(target)");

    // Stencil attachment (the pattern clip mask), same size as the colour target.
    VkImageCreateInfo si{};
    si.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    si.imageType     = VK_IMAGE_TYPE_2D;
    si.extent        = { w, h, 1 };
    si.mipLevels     = 1;
    si.arrayLayers   = 1;
    si.format        = stencilFormat_;
    si.tiling        = VK_IMAGE_TILING_OPTIMAL;
    si.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    si.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    si.samples       = VK_SAMPLE_COUNT_1_BIT;
    si.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateImage(device_, &si, nullptr, &t.stencilImage), "vkCreateImage(stencil)");
    VkMemoryRequirements sreq; vkGetImageMemoryRequirements(device_, t.stencilImage, &sreq);
    VkMemoryAllocateInfo sai{};
    sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; sai.allocationSize = sreq.size;
    sai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(device_, &sai, nullptr, &t.stencilMemory), "vkAllocateMemory(stencil)");
    vkBindImageMemory(device_, t.stencilImage, t.stencilMemory, 0);
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (stencilFormat_ != VK_FORMAT_S8_UINT) aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageViewCreateInfo svi{};
    svi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    svi.image                       = t.stencilImage;
    svi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    svi.format                      = stencilFormat_;
    svi.subresourceRange.aspectMask = aspect;
    svi.subresourceRange.levelCount = 1;
    svi.subresourceRange.layerCount = 1;
    CheckVk(vkCreateImageView(device_, &svi, nullptr, &t.stencilView), "vkCreateImageView(stencil)");

    VkImageView attViews[2] = { t.view, t.stencilView };
    VkFramebufferCreateInfo fi{};
    fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass      = renderPass_;
    fi.attachmentCount = 2;
    fi.pAttachments    = attViews;
    fi.width           = w;
    fi.height          = h;
    fi.layers          = 1;
    CheckVk(vkCreateFramebuffer(device_, &fi, nullptr, &t.framebuffer),
            "vkCreateFramebuffer(target)");

    t.descriptor = ImGui_ImplVulkan_AddTexture(
        sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t.textureId  = (ImTextureID)t.descriptor;
    t.width  = w;
    t.height = h;
    // No per-target fence: submission fences live in the renderer's slot ring; the
    // target only remembers (non-owned) its last submit's slot fence for rebuilds.
}

void CanvasRenderer::DestroyTarget(RenderTarget& t) {
    if (t.descriptor)   ImGui_ImplVulkan_RemoveTexture(t.descriptor);
    if (t.framebuffer)  vkDestroyFramebuffer(device_, t.framebuffer, nullptr);
    if (t.view)         vkDestroyImageView(device_, t.view, nullptr);
    if (t.image)        vkDestroyImage(device_, t.image, nullptr);
    if (t.memory)       vkFreeMemory(device_, t.memory, nullptr);
    if (t.stencilView)  vkDestroyImageView(device_, t.stencilView, nullptr);
    if (t.stencilImage) vkDestroyImage(device_, t.stencilImage, nullptr);
    if (t.stencilMemory)vkFreeMemory(device_, t.stencilMemory, nullptr);
    DestroyTargetVertexBuffer(t);                       // persistent per-view vbo
    DestroyTargetPatternBuffers(t);                     // per-view instance + mask
    t = RenderTarget{};
}

RenderTarget& CanvasRenderer::AcquireTarget(const void* key, uint32_t w, uint32_t h) {
    RenderTarget& t = targets_[key];
    if (!t.valid() || t.width != w || t.height != h) {
        if (t.valid()) {
            // Resize: the old images may still be in flight; wait then recreate.
            vkDeviceWaitIdle(device_);
            DestroyTarget(t);
        }
        CreateTargetImages(t, w, h);
    }
    return t;
}

void CanvasRenderer::EnsureVertexCapacity(VkDeviceSize bytes) {
    if (bytes <= vboCapacity_ && vbo_) return;
    DestroyVertexBuffer();

    // Round up to reduce reallocations as the document grows.
    VkDeviceSize cap = 1;
    while (cap < bytes) cap <<= 1;
    if (cap < 4096) cap = 4096;

    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = cap;
    bi.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(device_, &bi, nullptr, &vbo_), "vkCreateBuffer(vbo)");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, vbo_, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = FindMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CheckVk(vkAllocateMemory(device_, &ai, nullptr, &vboMemory_), "vkAllocateMemory(vbo)");
    vkBindBufferMemory(device_, vbo_, vboMemory_, 0);
    vboCapacity_ = cap;
}

void CanvasRenderer::DestroyVertexBuffer() {
    if (vbo_)       vkDestroyBuffer(device_, vbo_, nullptr);
    if (vboMemory_) vkFreeMemory(device_, vboMemory_, nullptr);
    vbo_ = VK_NULL_HANDLE;
    vboMemory_ = VK_NULL_HANDLE;
    vboCapacity_ = 0;
}

void CanvasRenderer::MakeHostBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                                    VkDeviceSize bytes, VkBufferUsageFlags usage) {
    if (buf) vkDestroyBuffer(device_, buf, nullptr);
    if (mem) vkFreeMemory(device_, mem, nullptr);
    buf = VK_NULL_HANDLE; mem = VK_NULL_HANDLE; cap = 0;

    VkDeviceSize c = 1;
    while (c < bytes) c <<= 1;
    if (c < 4096) c = 4096;

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = c; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(device_, &bi, nullptr, &buf), "vkCreateBuffer(host)");
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CheckVk(vkAllocateMemory(device_, &ai, nullptr, &mem), "vkAllocateMemory(host)");
    vkBindBufferMemory(device_, buf, mem, 0);
    cap = c;
}

void CanvasRenderer::EnsureTargetVertexCapacity(RenderTarget& t, VkDeviceSize bytes) {
    if (bytes <= t.vboCapacity && t.vbo) return;
    MakeHostBuffer(t.vbo, t.vboMemory, t.vboCapacity, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void CanvasRenderer::EnsureTargetDecorCapacity(RenderTarget& t, VkDeviceSize bytes) {
    if (bytes <= t.decorInstCapacity && t.decorInstVbo) return;
    MakeHostBuffer(t.decorInstVbo, t.decorInstMemory, t.decorInstCapacity, bytes,
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void CanvasRenderer::EnsureTargetMaskCapacity(RenderTarget& t, VkDeviceSize bytes) {
    if (bytes <= t.maskCapacity && t.maskVbo) return;
    MakeHostBuffer(t.maskVbo, t.maskMemory, t.maskCapacity, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void CanvasRenderer::DestroyTargetVertexBuffer(RenderTarget& t) {
    if (t.vbo)       vkDestroyBuffer(device_, t.vbo, nullptr);
    if (t.vboMemory) vkFreeMemory(device_, t.vboMemory, nullptr);
    t.vbo = VK_NULL_HANDLE;
    t.vboMemory = VK_NULL_HANDLE;
    t.vboCapacity = 0;
    t.vertexCount = 0;
    t.hasGeometry = false;
    t.buildSig = 0;
    t.segs.clear();
}

void CanvasRenderer::DestroyTargetPatternBuffers(RenderTarget& t) {
    if (t.decorInstVbo)    vkDestroyBuffer(device_, t.decorInstVbo, nullptr);
    if (t.decorInstMemory) vkFreeMemory(device_, t.decorInstMemory, nullptr);
    if (t.maskVbo)    vkDestroyBuffer(device_, t.maskVbo, nullptr);
    if (t.maskMemory) vkFreeMemory(device_, t.maskMemory, nullptr);
    t.decorInstVbo = VK_NULL_HANDLE; t.decorInstMemory = VK_NULL_HANDLE;
    t.decorInstCapacity = 0; t.decorInstCount = 0;
    t.maskVbo = VK_NULL_HANDLE; t.maskMemory = VK_NULL_HANDLE; t.maskCapacity = 0;
    t.maskVertexCount = 0;
}

// FNV-1a over what RenderView would draw — the per-shape cache hash encodes
// geometry/paint/transform/marks/fillLayers (global-state-free now), and the detail
// bucket + quantised cull rect are mixed in explicitly via args. Cheap "did anything
// visible change?" check; equal signature ⇒ reuse the per-view buffer.
uint64_t CanvasRenderer::BuildSignature(
        const Document& doc,
        const std::vector<Tessellator::PagePlacement>* placements,
        bool includeLoose, int detailBucket,
        const Tessellator::CullRect& cullQuantised) const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mixu64 = [&](uint64_t v) { mix(&v, sizeof v); };
    auto mixf   = [&](float v)    { mix(&v, sizeof v); };
    auto mixi   = [&](int v)      { mix(&v, sizeof v); };

    mixi(detailBucket);                       // per-view detail (not a global)
    // The visible cull rect changes the built mesh (off-screen shapes are skipped),
    // so it MUST be in the signature; it is pre-quantised to the margin grid by the
    // caller, so a small pan keeps the same signature (buffer reused) while crossing
    // a margin step rebuilds with the new visible set.
    mixf(cullQuantised.min.x); mixf(cullQuantised.min.y);
    mixf(cullQuantised.max.x); mixf(cullQuantised.max.y);
    uint8_t il = includeLoose ? 1 : 0; mix(&il, 1);

    for (size_t i = 0; i < doc.artboards.size(); ++i) {
        const Artboard& ab = doc.artboards[i];
        Vec2 origin = ab.pos; bool visible = true;
        if (placements && i < placements->size()) {
            origin = (*placements)[i].origin; visible = (*placements)[i].visible;
        }
        uint8_t vis[2] = { (uint8_t)(visible ? 1 : 0), (uint8_t)(ab.pageVisible ? 1 : 0) };
        mix(vis, 2);
        if (!visible) continue;
        mixu64(ab.id); mixf(origin.x); mixf(origin.y); mixf(ab.size.x); mixf(ab.size.y);
        for (const Shape& s : ab.shapes) {
            mixu64(s.id);
            mixu64(Tessellator::HashShape(s, Vec2{0, 0}));
        }
    }
    if (includeLoose) {
        for (const Shape& s : doc.looseShapes) {
            mixu64(s.id);
            mixu64(Tessellator::HashShape(s, Vec2{0, 0}));
        }
    }
    return h;
}

// Record the procedural draw list (base triangles → per-page procedural fill
// patterns → instanced curve decorators) into `cmd`, inside an already-open render
// pass. Shared by RenderView (whole document, per-page scissors) and RenderGlyphCached
// (a single full-scissor page). `segs` are the pages (empty → one full-mesh draw).
void CanvasRenderer::RecordDrawList(VkCommandBuffer cmd, const RenderTarget& t,
                                    const std::vector<Tessellator::PageSeg>& segs,
                                    const Camera& cam, uint32_t w, uint32_t h, int n) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport vpDyn{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &vpDyn);

    const float zN = cam.zoom * (float)n;   // px per doc-unit at N× size
    PushConstants pc{};
    pc.pan[0]     = cam.panX;
    pc.pan[1]     = cam.panY;
    pc.target[0]  = (float)w;            // N× rendered size
    pc.target[1]  = (float)h;
    pc.zoom       = zN;
    pc.unitScale  = cam.unitScale;
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, offsetof(PushConstants, pColor), &pc);   // camera block only

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &offset);

    // Project a world doc-rect to target pixels, matching shape.vert:
    //   screen_px = (doc * unitScale - pan) * zoomN
    auto rectScissor = [&](Vec2 mn, Vec2 mx, const VkRect2D* clamp) -> VkRect2D {
        float x0 = (mn.x * cam.unitScale - cam.panX) * zN;
        float y0 = (mn.y * cam.unitScale - cam.panY) * zN;
        float x1 = (mx.x * cam.unitScale - cam.panX) * zN;
        float y1 = (mx.y * cam.unitScale - cam.panY) * zN;
        float lo_x = std::max(0.0f, std::min(x0, x1));
        float lo_y = std::max(0.0f, std::min(y0, y1));
        float hi_x = std::min((float)w, std::max(x0, x1));
        float hi_y = std::min((float)h, std::max(y0, y1));
        if (clamp) {   // intersect with the page rect so a surface stays on page
            lo_x = std::max(lo_x, (float)clamp->offset.x);
            lo_y = std::max(lo_y, (float)clamp->offset.y);
            hi_x = std::min(hi_x, (float)(clamp->offset.x + clamp->extent.width));
            hi_y = std::min(hi_y, (float)(clamp->offset.y + clamp->extent.height));
        }
        VkRect2D r{};
        r.offset = { (int32_t)lo_x, (int32_t)lo_y };
        r.extent = { (uint32_t)std::max(0.0f, hi_x - lo_x),
                     (uint32_t)std::max(0.0f, hi_y - lo_y) };
        return r;
    };
    auto pageScissor = [&](const Tessellator::PageSeg& s) {
        return rectScissor(s.min, { s.min.x + s.size.x, s.min.y + s.size.y }, nullptr);
    };

    // Draw a page's procedural fill patterns AFTER its base triangles: per
    // surface-layer, write its cut polygon into the stencil (REPLACE ref) under a
    // bbox scissor, then draw the SAME cover polygon with pattern_fill.frag
    // (stencil-test EQUAL ref) which paints the motif from the pushed params.
    auto drawPatterns = [&](const Tessellator::ObjDraw& o, const VkRect2D* pageClamp) {
        // Skip safely if the pattern pipelines failed to build (missing shaders).
        if (o.patterns.empty() || !stencilMaskPipeline_ || !patternFillPipeline_) return;
        for (const Tessellator::SurfaceDraw& d : o.patterns) {
            if (d.coverVertexCount == 0) continue;
            VkRect2D sc = rectScissor(d.bbMin, d.bbMax, pageClamp);
            if (sc.extent.width == 0 || sc.extent.height == 0) continue;
            vkCmdSetScissor(cmd, 0, 1, &sc);
            VkDeviceSize co = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &t.maskVbo, &co);
            // 1) Stencil write: rasterise the cover polygon, REPLACE ref.
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stencilMaskPipeline_);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
            vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);
            metricsAccum_.drawCalls++;
            // 2) Procedural fill: same cover polygon, motif painted by the FS,
            //    stencil-test EQUAL ref → clipped exactly to the cut contour.
            PushConstants pp{};
            pp.pan[0]=cam.panX; pp.pan[1]=cam.panY; pp.target[0]=(float)w; pp.target[1]=(float)h;
            pp.zoom = cam.zoom*(float)n; pp.unitScale = cam.unitScale;
            pp.pColor[0]=d.params.color.r; pp.pColor[1]=d.params.color.g;
            pp.pColor[2]=d.params.color.b; pp.pColor[3]=d.params.color.a;
            pp.pKind=(float)d.params.kind; pp.pSpacing=d.params.spacing; pp.pSize=d.params.size;
            pp.pAngle=d.params.angle; pp.pOffset[0]=d.params.offset.x; pp.pOffset[1]=d.params.offset.y;
            { uint32_t sd=d.params.seed; std::memcpy(&pp.pSeed,&sd,sizeof(float)); }
            pp.pDash=d.params.dash; pp.pDashGap=d.params.dashGap;
            pp.pAltPhase=d.params.altPhase?1.0f:0.0f;
            pp.pCenter[0]=d.params.center.x; pp.pCenter[1]=d.params.center.y;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, patternFillPipeline_);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
            vkCmdPushConstants(cmd, pipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConstants), &pp);
            vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);
            metricsAccum_.drawCalls++;
        }
        // Restore the base pipeline + its vertex buffer + the camera-only push
        // for subsequent pages' base draws.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkDeviceSize bo = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &bo);
        PushConstants cpc{};
        cpc.pan[0]=cam.panX; cpc.pan[1]=cam.panY; cpc.target[0]=(float)w; cpc.target[1]=(float)h;
        cpc.zoom=cam.zoom*(float)n; cpc.unitScale=cam.unitScale;
        vkCmdPushConstants(cmd, pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, offsetof(PushConstants, pColor), &cpc);
    };

    // Draw an object's TRANSPARENT strokes AFTER its base + patterns: per stroke,
    // write the ribbon coverage into the stencil (REPLACE ref) under a bbox scissor,
    // then draw the bbox QUAD ONCE (stencil-test EQUAL ref) with the stroke colour —
    // each covered pixel blends exactly once, so the overlapping ribbon never doubles
    // the alpha (the dark-seam artifact). Opaque strokes are baked into the base mesh
    // and never reach here.
    auto drawStrokes = [&](const Tessellator::ObjDraw& o, const VkRect2D* pageClamp) {
        if (o.strokes.empty() || !stencilMaskPipeline_ || !strokeFillPipeline_) return;
        for (const Tessellator::StrokeDraw& d : o.strokes) {
            if (d.coverVertexCount == 0) continue;
            VkRect2D sc = rectScissor(d.bbMin, d.bbMax, pageClamp);
            if (sc.extent.width == 0 || sc.extent.height == 0) continue;
            vkCmdSetScissor(cmd, 0, 1, &sc);
            VkDeviceSize co = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &t.maskVbo, &co);
            // 1) Coverage: rasterise the (overlapping) ribbon into the stencil, REPLACE
            //    ref. Overlap is harmless here — REPLACE just writes the ref again.
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stencilMaskPipeline_);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
            vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);
            metricsAccum_.drawCalls++;
            // 2) Colour: the bbox quad (6 verts) ONCE, stencil-test EQUAL ref → only
            //    the covered pixels, each blended exactly once. NEVER redraw the ribbon
            //    here (it overlaps → would re-introduce the alpha doubling).
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, strokeFillPipeline_);
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
            vkCmdPushConstants(cmd, pipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, offsetof(PushConstants, pColor), &pc);   // camera only; colour is per-vertex
            vkCmdDraw(cmd, 6, 1, d.quadFirstVertex, 0);
            metricsAccum_.drawCalls++;
        }
        // Restore base pipeline + vbo + camera push for the next object/page.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkDeviceSize bo = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &bo);
        vkCmdPushConstants(cmd, pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, offsetof(PushConstants, pColor), &pc);
    };

    // Draw a page's instanced curve decorators AFTER its base triangles +
    // procedural fills: one vkCmdDraw(baseVerts, instanceCount) per glyph batch,
    // no stencil. The unit base mesh is binding 0, the per-view instances binding 1.
    auto drawDecor = [&](const Tessellator::ObjDraw& o, const VkRect2D* pageClamp) {
        if (o.decor.empty() || !decorInstPipeline_ || t.decorInstCount == 0) return;
        VkRect2D sc = pageClamp ? *pageClamp : VkRect2D{ {0,0}, {w,h} };
        if (sc.extent.width == 0 || sc.extent.height == 0) return;
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, decorInstPipeline_);
        vkCmdPushConstants(cmd, pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, offsetof(PushConstants, pColor), &pc);   // camera only
        VkBuffer vbs[2] = { baseMeshVbo_, t.decorInstVbo };
        VkDeviceSize offs[2] = { 0, 0 };
        vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs);
        for (const Tessellator::DecorDraw& d : o.decor) {
            if (d.instanceCount == 0) continue;
            const BaseRange& br = baseRange_[(int)d.kind];
            if (br.count == 0) continue;
            vkCmdDraw(cmd, br.count, d.instanceCount, br.first, d.firstInstance);
            metricsAccum_.drawCalls++;
        }
        // Restore the base pipeline + its vertex buffer for the next page's base.
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkDeviceSize bo = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &bo);
    };

    // Each page is clipped to its own rect; the white backdrop draws first, then each
    // object's base → its patterns → its decorators (true document z-order: a lower
    // object's pattern/decorator never paints over a higher object).
    for (const Tessellator::PageSeg& s : segs) {
        VkRect2D full{ {0, 0}, {w, h} };
        VkRect2D sc = s.fullScissor ? full : pageScissor(s);
        if (!s.fullScissor && (sc.extent.width == 0 || sc.extent.height == 0)) continue;
        const VkRect2D* clamp = s.fullScissor ? nullptr : &sc;

        vkCmdSetScissor(cmd, 0, 1, &sc);
        if (s.backdropCount > 0) {
            vkCmdDraw(cmd, s.backdropCount, 1, s.backdropFirst, 0);
            metricsAccum_.drawCalls++;
        }
        for (const Tessellator::ObjDraw& o : s.objects) {
            vkCmdSetScissor(cmd, 0, 1, &sc);   // re-assert page scissor (drawPatterns left a bbox)
            if (o.baseCount > 0) {
                vkCmdDraw(cmd, o.baseCount, 1, o.baseFirst, 0);
                metricsAccum_.drawCalls++;
            }
            drawPatterns(o, clamp);   // bbox scissor; restores base pipeline + vbo + push
            drawStrokes(o, clamp);    // transparent strokes (stencil); restores base
            drawDecor(o, clamp);      // restores base pipeline + vbo
        }
    }
}

ImTextureID CanvasRenderer::RenderView(const void* key, const Document& doc,
                                       const Camera& cam, int widthPx, int heightPx,
                                       ImVec4 clearColor,
                                       const std::vector<Tessellator::PagePlacement>* placements,
                                       bool includeLoose, bool focused) {
    if (!initialized_ || widthPx <= 0 || heightPx <= 0) return ImTextureID(0);

    // SSAA: render into a target N× larger on each axis. The linear sampler
    // downscales it at the ImGui::Image blit, supersampling the result. Drop the
    // factor for this view if it would exceed the per-view dimension cap.
    int n = ssaaFactor_;
    while (n > 1 && ((uint32_t)widthPx * n > kMaxTargetDim ||
                     (uint32_t)heightPx * n > kMaxTargetDim))
        --n;
    const uint32_t w = (uint32_t)widthPx * (uint32_t)n;   // rendered size (px)
    const uint32_t h = (uint32_t)heightPx * (uint32_t)n;

    RenderTarget& t = AcquireTarget(key, w, h);
    t.lastUsedFrame = frame_;

    // Build the document into the target's OWN persistent buffer, but only when
    // its content signature changed. The camera lives in the vertex shader, so a
    // static/pan/zoom frame reuses the buffer untouched: no tessellation, no copy,
    // no upload — just a re-record with a new push constant + scissors. The detail
    // bucket must be set BEFORE the signature so a bucket-crossing zoom rebuilds.
    const float effZoom = cam.zoom * cam.unitScale * (float)n;
    Tessellator::SetDetailScale(Tessellator::DetailScaleForZoom(effZoom));
    const int detailBucket = Tessellator::DetailBucketIndex(effZoom);

    // Visible world rect (doc-units): invert screen_px = (doc*unitScale - pan)*zN, i.e.
    // doc = (px/zN + pan)/unitScale, over the canvas corners (0,0)..(w,h). Expand by a
    // margin so a shape just off-screen is still tessellated (it slides in on a small
    // pan without a rebuild), then SNAP min/max to the margin grid so sub-margin pans
    // keep the same quantised rect (signature stable → buffer reused). The same rect
    // bounds the geometry clip in Lot 3.
    const float zN = cam.zoom * (float)n;
    const float us = (std::fabs(cam.unitScale) > 1e-6f) ? cam.unitScale : 1.0f;
    auto toDoc = [&](float px, float py) {
        return Vec2{ (px / zN + cam.panX) / us, (py / zN + cam.panY) / us };
    };
    Vec2 vMin = toDoc(0.0f, 0.0f);
    Vec2 vMax = toDoc((float)w, (float)h);
    if (vMin.x > vMax.x) std::swap(vMin.x, vMax.x);
    if (vMin.y > vMax.y) std::swap(vMin.y, vMax.y);
    // A GENEROUS margin (a full viewport on each side) snapped to a coarse grid: the
    // quantised rect then stays the same across a wide pan AND a fair zoom range, so a
    // continuous zoom/pan rebuilds only when it leaves that padded box — not every
    // frame (that was the zoom stutter). It still tessellates a bounded super-set of
    // the visible set, so a giant off-screen extent is excluded (the win), while the
    // rebuild cadence is low. The same rect bounds the geometry clip in Lot 3.
    const float marginX = std::max(1e-3f, (vMax.x - vMin.x) * 1.0f);
    const float marginY = std::max(1e-3f, (vMax.y - vMin.y) * 1.0f);
    Tessellator::CullRect cull;
    cull.min = { std::floor((vMin.x - marginX) / marginX) * marginX,
                 std::floor((vMin.y - marginY) / marginY) * marginY };
    cull.max = { std::ceil ((vMax.x + marginX) / marginX) * marginX,
                 std::ceil ((vMax.y + marginY) / marginY) * marginY };

    const uint64_t newSig =
        BuildSignature(doc, placements, includeLoose, detailBucket, cull);

    metricsAccum_.views++;
    t.wantSig = newSig;

    // Decide whether to (re)build this frame. A rebuild is wanted when the signature
    // changed or the previous build left shapes DEFERRED (budget ran out). For a
    // non-focused view we GATE the cadence: re-attempt at most every kThrottleFrames
    // frames, so many open viewports don't all re-tessellate on the same frame. The
    // focused view never gates (immediate, crisp). The buffer is re-recorded with the
    // current camera regardless, so a gated view still pans/zooms smoothly (free).
    constexpr uint64_t kThrottleFrames = 4;
    // Rebuild when the signature changed, the buffer is missing, OR the previous build
    // deferred shapes (buildSig left as the kDeferredSig sentinel).
    bool doBuild = !(t.hasGeometry && t.buildSig == newSig && t.vbo != VK_NULL_HANDLE)
                   || (t.buildSig == kDeferredSig);
    if (doBuild && !focused && t.hasGeometry
        && (frame_ - t.lastRebuildFrame) < kThrottleFrames)
        doBuild = false;   // gated: keep the existing buffer, re-record with new cam

    if (doBuild) {
        t.lastRebuildFrame = frame_;
        // Budget this view's rebuild: a focused view gets a generous budget (finish
        // promptly), a background view a smaller one (spread). minRebuilds=1 ensures
        // forward progress even for one huge shape. The budget caps how many vertices
        // are re-tessellated THIS frame; the rest draw stale and finish next frame.
        cache_.BeginBudget(focused ? (size_t)600000 : (size_t)150000, 1);
        // REBUILD: only the VISIBLE set (per-view cull rect, margin-expanded). Fully
        // off-screen shapes are skipped before any flatten/upload — the dominant
        // multi-viewport cost. On-screen tris are still GPU-scissored per page. The
        // quantised cull rect is in the signature, so a static/sub-margin-pan frame
        // reuses the buffer untouched (no tessellation, no upload). Surface patterns
        // are a cover polygon + motif params (drawn procedurally), not per-element
        // triangles — no per-element CPU cost at any density.
        //
        // We are about to OVERWRITE this view's host-visible buffers; a previous
        // frame's offscreen draw may still be reading them. Wait ONLY on this view's
        // last submit (not the whole queue) — rare (only on an actual rebuild),
        // usually already done. Static/sub-margin frames skip the rebuild + the wait.
        if (t.lastSubmitFence != VK_NULL_HANDLE)
            vkWaitForFences(device_, 1, &t.lastSubmitFence, VK_TRUE, UINT64_MAX);
        auto t0 = std::chrono::high_resolution_clock::now();
        scratchMesh_.clear();
        scratchCover_.clear();
        scratchDecor_.clear();
        t.segs = Tessellator::BuildDocumentSegmented(
            doc, scratchMesh_, effZoom, placements, includeLoose,
            &cache_, /*cull=*/&cull, /*outCover=*/&scratchCover_,
            /*outDecor=*/&scratchDecor_);
        auto t1 = std::chrono::high_resolution_clock::now();
        metricsAccum_.tessMs += std::chrono::duration<float, std::milli>(t1 - t0).count();

        const VkDeviceSize vbytes = scratchMesh_.vertices.size() * sizeof(Vertex);
        if (vbytes > 0) {
            EnsureTargetVertexCapacity(t, vbytes);
            void* map = nullptr;
            vkMapMemory(device_, t.vboMemory, 0, vbytes, 0, &map);
            memcpy(map, scratchMesh_.vertices.data(), (size_t)vbytes);
            vkUnmapMemory(device_, t.vboMemory);
        }
        const VkDeviceSize mbytes = scratchCover_.vertices.size() * sizeof(Vertex);
        if (mbytes > 0) {
            EnsureTargetMaskCapacity(t, mbytes);
            void* map = nullptr;
            vkMapMemory(device_, t.maskMemory, 0, mbytes, 0, &map);
            memcpy(map, scratchCover_.vertices.data(), (size_t)mbytes);
            vkUnmapMemory(device_, t.maskMemory);
        }
        const VkDeviceSize dbytes = scratchDecor_.size() * sizeof(PatternInstance);
        if (dbytes > 0) {
            EnsureTargetDecorCapacity(t, dbytes);
            void* map = nullptr;
            vkMapMemory(device_, t.decorInstMemory, 0, dbytes, 0, &map);
            memcpy(map, scratchDecor_.data(), (size_t)dbytes);
            vkUnmapMemory(device_, t.decorInstMemory);
        }
        t.vertexCount     = (uint32_t)scratchMesh_.vertices.size();
        t.maskVertexCount = (uint32_t)scratchCover_.vertices.size();
        t.decorInstCount  = (uint32_t)scratchDecor_.size();
        t.hasGeometry = (t.vertexCount > 0) || t.maskVertexCount > 0 || t.decorInstCount > 0;
        // If the budget left shapes deferred (drawn stale this frame), leave the build
        // signature as the sentinel so the NEXT frame re-enters and finishes them.
        t.buildSig    = (cache_.deferredShapes > 0) ? kDeferredSig : newSig;

        metricsAccum_.shapesDrawn  += cache_.drawnShapes;
        metricsAccum_.shapesCached += cache_.cachedShapes;
        metricsAccum_.shapesBuilt  += cache_.builtShapes;
        metricsAccum_.shapesCulled += cache_.culledShapes;
    }
    metricsAccum_.triangles += (int)t.vertexCount / 3;

    // Acquire the next offscreen submission slot (round-robin). If it's still in
    // flight from a few frames ago, wait its fence (rare with kSubmitSlots in flight).
    SubmitSlot& slot = slots_[nextSlot_];
    nextSlot_ = (nextSlot_ + 1) % kSubmitSlots;
    vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &slot.fence);
    cmd_ = slot.cmd;   // this view records into its slot's command buffer

    // Record this view's offscreen pass into the slot's command buffer.
    vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &bi);

    VkClearValue clears[2]{};
    clears[0].color = { { clearColor.x, clearColor.y, clearColor.z, clearColor.w } };
    clears[1].depthStencil = { 0.0f, 0u };   // stencil mask starts cleared to 0

    VkRenderPassBeginInfo rp{};
    rp.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass               = renderPass_;
    rp.framebuffer              = t.framebuffer;
    rp.renderArea.extent        = { w, h };
    rp.clearValueCount          = 2;
    rp.pClearValues             = clears;
    vkCmdBeginRenderPass(cmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    if (t.hasGeometry)
        RecordDrawList(cmd_, t, t.segs, cam, w, h, n);

    vkCmdEndRenderPass(cmd_);
    vkEndCommandBuffer(cmd_);

    // Submit the offscreen pass SIGNALLING the slot's semaphore (no CPU wait): the
    // main swapchain pass waits on it (FRAGMENT_SHADER) before ImGui samples this
    // target, so the CPU never blocks here. `slot.fence` guards slot + buffer reuse.
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &slot.sem;
    vkQueueSubmit(queue_, 1, &si, slot.fence);
    t.lastSubmitFence = slot.fence;          // a rebuild waits this before overwriting buffers
    framePendingWaits_.push_back(slot.sem);  // the main pass will wait on it this frame

    return t.textureId;
}

bool CanvasRenderer::RenderToRGBA(const Document& doc, const Camera& cam,
                                  int w, int h, ImVec4 clearColor,
                                  std::vector<unsigned char>& outRGBA) {
    if (!initialized_ || w <= 0 || h <= 0) return false;
    const uint32_t W = (uint32_t)w, H = (uint32_t)h;

    // Tessellate the document (full quality at this size).
    scratchMesh_.clear();
    const float effZoom = cam.zoom * cam.unitScale;
    // Detail tracks the on-screen scale (the flattener reads gDetailScale as px per
    // doc-unit); BuildDocument doesn't set it, so do it here for the readback render.
    const float savedRgbaD = Tessellator::GetDetailScale();
    Tessellator::SetDetailScale(std::clamp(effZoom, 1.0f, 4096.0f));
    // Correct inter-page z-order: an object never paints over a foreign page.
    Tessellator::BuildDocument(doc, scratchMesh_, effZoom);
    Tessellator::SetDetailScale(savedRgbaD);

    // One-off colour image with COLOR_ATTACHMENT + TRANSFER_SRC for readback, plus
    // a throwaway stencil image (the shared render pass requires it; this path bakes
    // patterns as triangles, so the stencil is cleared and unused).
    VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; VkFramebuffer fb = VK_NULL_HANDLE;
    VkImage simg = VK_NULL_HANDLE; VkDeviceMemory smem = VK_NULL_HANDLE; VkImageView sview = VK_NULL_HANDLE;
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D; ii.extent = { W, H, 1 };
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.format = colorFormat_;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device_, &ii, nullptr, &img) != VK_SUCCESS) return false;
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device_, img, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device_, &ai, nullptr, &mem);
        vkBindImageMemory(device_, img, mem, 0);
        VkImageViewCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = colorFormat_;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device_, &vi, nullptr, &view);
        // Throwaway stencil.
        VkImageCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        si.imageType = VK_IMAGE_TYPE_2D; si.extent = { W, H, 1 };
        si.mipLevels = 1; si.arrayLayers = 1; si.format = stencilFormat_;
        si.tiling = VK_IMAGE_TILING_OPTIMAL; si.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        si.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        si.samples = VK_SAMPLE_COUNT_1_BIT; si.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateImage(device_, &si, nullptr, &simg);
        VkMemoryRequirements sreq; vkGetImageMemoryRequirements(device_, simg, &sreq);
        VkMemoryAllocateInfo sai{}; sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        sai.allocationSize = sreq.size;
        sai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device_, &sai, nullptr, &smem);
        vkBindImageMemory(device_, simg, smem, 0);
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        if (stencilFormat_ != VK_FORMAT_S8_UINT) aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        VkImageViewCreateInfo svi{}; svi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        svi.image = simg; svi.viewType = VK_IMAGE_VIEW_TYPE_2D; svi.format = stencilFormat_;
        svi.subresourceRange = { aspect, 0, 1, 0, 1 };
        vkCreateImageView(device_, &svi, nullptr, &sview);
        VkImageView attv[2] = { view, sview };
        VkFramebufferCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_; fi.attachmentCount = 2; fi.pAttachments = attv;
        fi.width = W; fi.height = H; fi.layers = 1;
        vkCreateFramebuffer(device_, &fi, nullptr, &fb);
    }

    // Host-readable staging buffer.
    VkDeviceSize bytes = (VkDeviceSize)W * H * 4;
    VkBuffer sb; VkDeviceMemory sm;
    {
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device_, &bi, nullptr, &sb);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, sb, &req);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &ai, nullptr, &sm);
        vkBindBufferMemory(device_, sb, sm, 0);
    }

    const VkDeviceSize vbytes = scratchMesh_.vertices.size() * sizeof(Vertex);
    if (vbytes > 0) {
        EnsureVertexCapacity(vbytes);
        void* map = nullptr; vkMapMemory(device_, vboMemory_, 0, vbytes, 0, &map);
        memcpy(map, scratchMesh_.vertices.data(), (size_t)vbytes);
        vkUnmapMemory(device_, vboMemory_);
    }

    vkResetCommandBuffer(oneShotCmd_, 0);
    VkCommandBufferBeginInfo cbi{}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(oneShotCmd_, &cbi);

    VkClearValue clears[2]{};
    clears[0].color = { { clearColor.x, clearColor.y, clearColor.z, clearColor.w } };
    clears[1].depthStencil = { 0.0f, 0u };
    VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_; rp.framebuffer = fb; rp.renderArea.extent = { W, H };
    rp.clearValueCount = 2; rp.pClearValues = clears;
    vkCmdBeginRenderPass(oneShotCmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (vbytes > 0) {
        vkCmdBindPipeline(oneShotCmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkViewport vp{ 0,0,(float)W,(float)H,0,1 }; vkCmdSetViewport(oneShotCmd_, 0, 1, &vp);
        VkRect2D sc{ {0,0}, {W,H} }; vkCmdSetScissor(oneShotCmd_, 0, 1, &sc);
        PushConstants pc{}; pc.pan[0]=cam.panX; pc.pan[1]=cam.panY;
        pc.target[0]=(float)W; pc.target[1]=(float)H; pc.zoom=cam.zoom; pc.unitScale=cam.unitScale;
        vkCmdPushConstants(oneShotCmd_, pipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, offsetof(PushConstants, pColor), &pc);
        VkDeviceSize off=0; vkCmdBindVertexBuffers(oneShotCmd_, 0, 1, &vbo_, &off);
        vkCmdDraw(oneShotCmd_, (uint32_t)scratchMesh_.vertices.size(), 1, 0, 0);
    }
    vkCmdEndRenderPass(oneShotCmd_);

    // Transition SHADER_READ_ONLY (render-pass finalLayout) → TRANSFER_SRC, copy.
    VkImageMemoryBarrier b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.image = img; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(oneShotCmd_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,0,nullptr,0,nullptr,1,&b);
    VkBufferImageCopy rgn{}; rgn.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    rgn.imageExtent = { W, H, 1 };
    vkCmdCopyImageToBuffer(oneShotCmd_, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sb, 1, &rgn);
    vkEndCommandBuffer(oneShotCmd_);

    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &oneShotCmd_;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    outRGBA.resize((size_t)bytes);
    void* src = nullptr; vkMapMemory(device_, sm, 0, bytes, 0, &src);
    memcpy(outRGBA.data(), src, (size_t)bytes);
    vkUnmapMemory(device_, sm);
    // colorFormat_ is B8G8R8A8_UNORM → swap B/R to RGBA for PNG.
    if (colorFormat_ == VK_FORMAT_B8G8R8A8_UNORM)
        for (size_t i = 0; i + 3 < outRGBA.size(); i += 4)
            std::swap(outRGBA[i], outRGBA[i + 2]);

    vkDestroyBuffer(device_, sb, nullptr);  vkFreeMemory(device_, sm, nullptr);
    vkDestroyFramebuffer(device_, fb, nullptr); vkDestroyImageView(device_, view, nullptr);
    vkDestroyImage(device_, img, nullptr);  vkFreeMemory(device_, mem, nullptr);
    vkDestroyImageView(device_, sview, nullptr); vkDestroyImage(device_, simg, nullptr);
    vkFreeMemory(device_, smem, nullptr);
    return true;
}

// ── Cached offscreen glyph texture (SSAA), for Symbol Viewer / placement ghost ──
ImTextureID CanvasRenderer::RenderGlyphCached(uint64_t key, uint64_t contentHash,
                                              const std::vector<Shape>& shapes,
                                              int widthPx, int heightPx, float padFrac,
                                              ImVec4 clearColor, bool exactFit,
                                              const Vec2* frameMin, const Vec2* frameMax) {
    if (!initialized_ || widthPx <= 0 || heightPx <= 0) return ImTextureID(0);
    int n = ssaaFactor_;
    while (n > 1 && ((uint32_t)widthPx * n > kMaxTargetDim ||
                     (uint32_t)heightPx * n > kMaxTargetDim)) --n;
    const uint32_t w = (uint32_t)widthPx * (uint32_t)n;
    const uint32_t h = (uint32_t)heightPx * (uint32_t)n;

    GlyphTex& gt = glyphTex_[key];
    gt.lastUsedFrame = frame_;
    const bool sizeOk = gt.t.valid() && gt.t.width == w && gt.t.height == h;
    if (sizeOk && gt.contentHash == contentHash) return gt.t.textureId;  // cache hit

    // (Re)allocate the target if needed.
    if (!sizeOk) {
        if (gt.t.valid()) { vkDeviceWaitIdle(device_); DestroyTarget(gt.t); }
        CreateTargetImages(gt.t, w, h);
    }
    gt.contentHash = contentHash;

    // Frame: either the EXPLICIT bounds the caller passed (so the texture matches the
    // exact rect it will blit to — stroke-padded, no offset/clip), or the auto union
    // bounds of all shapes (local doc-units). Camera: screen = (doc*unitScale-pan)*zoom.
    Vec2 mn{1e30f,1e30f}, mx{-1e30f,-1e30f}; bool any = false;
    if (frameMin && frameMax) {
        mn = *frameMin; mx = *frameMax; any = true;
    } else {
        for (const Shape& s : shapes) {
            Vec2 a, b; if (Tessellator::WorldBounds(s, 1.0f, a, b)) {
                mn.x=std::min(mn.x,a.x); mn.y=std::min(mn.y,a.y);
                mx.x=std::max(mx.x,b.x); mx.y=std::max(mx.y,b.y); any=true; }
        }
    }
    Camera cam; cam.unitScale = 1.0f;
    if (any) {
        float gw = std::max(0.01f, mx.x-mn.x), gh = std::max(0.01f, mx.y-mn.y);
        if (exactFit) {
            // Map [mn,mx] onto the WHOLE texture so it spans exactly the content bbox
            // in world units (caller blits at d2s(mn)..d2s(mx), pixel-aligned). Uses a
            // single (uniform) zoom = the X fit; the caller sizes wpx/hpx to the bbox
            // ratio, so X and Y fit match → no letterbox, no offset. pan = mn (no
            // centring): screen = (doc - mn)*zoom → mn→0, mx→(w,h).
            cam.zoom = ((float)w / gw) / (float)n;
            cam.panX = mn.x;
            cam.panY = mn.y;
        } else {
            float pad = padFrac;
            float zx = (w * (1.0f - 2*pad)) / gw, zy = (h * (1.0f - 2*pad)) / gh;
            float z = std::min(zx, zy);
            cam.zoom = z / (float)n;     // RenderView multiplies by n internally below
            // Centre the content: pan so the content centre maps to the target centre.
            Vec2 c{ (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
            cam.panX = c.x - ((float)widthPx  * 0.5f) / (cam.zoom);
            cam.panY = c.y - ((float)heightPx * 0.5f) / (cam.zoom);
        }
    } else { cam.zoom = 1.0f; }

    // Build the shapes through the SAME procedural path as the document: wrap them in
    // a throwaway one-page Document and segment-build with the glyph cache. This gives
    // GPU-fast fill patterns (cover + stencil + FS) and instanced decorators — so a
    // patterned/decorated glyph or LIVE PREVIEW renders fluidly at any density
    // (the legacy O(steps²) per-element bake is gone). The FS is doc-space, so the
    // fit camera draws the motif at the correct on-screen size with no special scale.
    Document gdoc;
    Artboard gpage; gpage.pos = {0,0}; gpage.size = { std::max(0.01f, mx.x-mn.x),
                                                       std::max(0.01f, mx.y-mn.y) };
    gpage.shapes = shapes;
    gdoc.artboards.push_back(std::move(gpage));

    scratchMesh_.clear(); scratchCover_.clear(); scratchDecor_.clear();
    std::vector<Tessellator::PageSeg> segs = Tessellator::BuildDocumentSegmented(
        gdoc, scratchMesh_, /*zoom=*/cam.zoom * (float)n, /*placements=*/nullptr,
        /*includeLoose=*/false, &glyphCache_, /*cull=*/nullptr,
        /*outCover=*/&scratchCover_, /*outDecor=*/&scratchDecor_);
    if (!segs.empty()) segs[0].fullScissor = true;   // glyph draws unclipped

    const VkDeviceSize vbytes = scratchMesh_.vertices.size() * sizeof(Vertex);
    if (vbytes > 0) {
        EnsureTargetVertexCapacity(gt.t, vbytes);
        void* map=nullptr; vkMapMemory(device_, gt.t.vboMemory, 0, vbytes, 0, &map);
        memcpy(map, scratchMesh_.vertices.data(), (size_t)vbytes); vkUnmapMemory(device_, gt.t.vboMemory);
    }
    const VkDeviceSize mbytes = scratchCover_.vertices.size() * sizeof(Vertex);
    if (mbytes > 0) {
        EnsureTargetMaskCapacity(gt.t, mbytes);
        void* map=nullptr; vkMapMemory(device_, gt.t.maskMemory, 0, mbytes, 0, &map);
        memcpy(map, scratchCover_.vertices.data(), (size_t)mbytes); vkUnmapMemory(device_, gt.t.maskMemory);
    }
    const VkDeviceSize dbytes = scratchDecor_.size() * sizeof(PatternInstance);
    if (dbytes > 0) {
        EnsureTargetDecorCapacity(gt.t, dbytes);
        void* map=nullptr; vkMapMemory(device_, gt.t.decorInstMemory, 0, dbytes, 0, &map);
        memcpy(map, scratchDecor_.data(), (size_t)dbytes); vkUnmapMemory(device_, gt.t.decorInstMemory);
    }
    gt.t.vertexCount     = (uint32_t)scratchMesh_.vertices.size();
    gt.t.maskVertexCount = (uint32_t)scratchCover_.vertices.size();
    gt.t.decorInstCount  = (uint32_t)scratchDecor_.size();
    gt.t.hasGeometry = (gt.t.vertexCount > 0) || gt.t.maskVertexCount > 0 || gt.t.decorInstCount > 0;
    gt.t.segs = std::move(segs);

    vkResetCommandBuffer(oneShotCmd_, 0);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(oneShotCmd_, &bi);
    VkClearValue gclears[2]{};
    gclears[0].color = { { clearColor.x, clearColor.y, clearColor.z, clearColor.w } };
    gclears[1].depthStencil = { 0.0f, 0u };
    VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = renderPass_; rp.framebuffer = gt.t.framebuffer; rp.renderArea.extent = { w, h };
    rp.clearValueCount = 2; rp.pClearValues = gclears;
    vkCmdBeginRenderPass(oneShotCmd_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    if (gt.t.hasGeometry) RecordDrawList(oneShotCmd_, gt.t, gt.t.segs, cam, w, h, n);
    vkCmdEndRenderPass(oneShotCmd_);
    vkEndCommandBuffer(oneShotCmd_);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &oneShotCmd_;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    return gt.t.textureId;
}

void CanvasRenderer::EvictGlyphTextures() {
    for (auto it = glyphTex_.begin(); it != glyphTex_.end(); ) {
        if (frame_ - it->second.lastUsedFrame > 60) {     // ~unused for 60 frames
            vkDeviceWaitIdle(device_); DestroyTarget(it->second.t);
            it = glyphTex_.erase(it);
        } else ++it;
    }
}

} // namespace Renderer

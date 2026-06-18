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

#include "CanvasRendererInternal.h"

namespace Renderer {

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

} // namespace Renderer

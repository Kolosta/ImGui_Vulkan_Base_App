#include "Ink/Render/Renderer.h"

#include "Ink/RHI/Pipeline.h"
#include "Render/RendererInternal.h"

#include <chrono>
#include <cstring>
#include <unordered_set>

namespace Ink {

using detail::RendererImpl;
using detail::ViewImpl;
using detail::FrameSlot;
using detail::IsoTarget;
using detail::ScopeRun;
using detail::PushCamera;
using detail::PushComposite;
using detail::kFramesInFlight;
using detail::kContentFormat;
using detail::kDisplayFormat;
using detail::kStencilFormat;

// ─────────────────────────────────────────────────────────────────────────────
//  Init helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

using Clock = std::chrono::steady_clock;
float MsSince(Clock::time_point t0) {
    return (float)std::chrono::duration<double, std::milli>(Clock::now() - t0)
        .count();
}

bool CreateLayoutsAndPool(RendererImpl& r) {
    VkDevice dev = r.device.vk();

    // Scene set: instance / item / paint tables, read by the vertex stage.
    VkDescriptorSetLayoutBinding sceneBindings[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        sceneBindings[i].binding         = i;
        sceneBindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneBindings[i].descriptorCount = 1;
        sceneBindings[i].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 3;
    sl.pBindings    = sceneBindings;
    if (vkCreateDescriptorSetLayout(dev, &sl, nullptr, &r.sceneSetLayout) != VK_SUCCESS)
        return false;

    // Present set: the resolved linear canvas, sampled by the fragment stage.
    VkDescriptorSetLayoutBinding presentBinding{};
    presentBinding.binding         = 0;
    presentBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    presentBinding.descriptorCount = 1;
    presentBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo pl{};
    pl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    pl.bindingCount = 1;
    pl.pBindings    = &presentBinding;
    if (vkCreateDescriptorSetLayout(dev, &pl, nullptr, &r.presentSetLayout) != VK_SUCCESS)
        return false;

    // Composite set: source + backdrop, both sampled by the fragment stage.
    VkDescriptorSetLayoutBinding compBindings[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        compBindings[i].binding         = i;
        compBindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        compBindings[i].descriptorCount = 1;
        compBindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo cl{};
    cl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    cl.bindingCount = 2;
    cl.pBindings    = compBindings;
    if (vkCreateDescriptorSetLayout(dev, &cl, nullptr, &r.compositeSetLayout) != VK_SUCCESS)
        return false;

    // Pipeline layouts. Content and overlay share the PushCamera block.
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size       = sizeof(PushCamera);

    VkPipelineLayoutCreateInfo li{};
    li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount         = 1;
    li.pSetLayouts            = &r.sceneSetLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges    = &push;
    if (vkCreatePipelineLayout(dev, &li, nullptr, &r.contentLayout) != VK_SUCCESS)
        return false;

    li.setLayoutCount = 0;
    li.pSetLayouts    = nullptr;
    if (vkCreatePipelineLayout(dev, &li, nullptr, &r.overlayLayout) != VK_SUCCESS)
        return false;

    li.setLayoutCount         = 1;
    li.pSetLayouts            = &r.presentSetLayout;
    li.pushConstantRangeCount = 0;
    li.pPushConstantRanges    = nullptr;
    if (vkCreatePipelineLayout(dev, &li, nullptr, &r.presentLayout) != VK_SUCCESS)
        return false;

    // Composite layout: the 2-sampler set + the PushComposite (fragment).
    VkPushConstantRange compPush{};
    compPush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compPush.size       = sizeof(PushComposite);
    li.setLayoutCount         = 1;
    li.pSetLayouts            = &r.compositeSetLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges    = &compPush;
    if (vkCreatePipelineLayout(dev, &li, nullptr, &r.compositeLayout) != VK_SUCCESS)
        return false;

    // Ink's own descriptor pool. Composite sets are allocated per frame per
    // scope and retired via the garbage ring, so size generously.
    VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         128 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
    };
    VkDescriptorPoolCreateInfo di{};
    di.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    di.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    di.maxSets       = 512;
    di.poolSizeCount = 2;
    di.pPoolSizes    = sizes;
    return vkCreateDescriptorPool(dev, &di, nullptr, &r.descriptorPool) == VK_SUCCESS;
}

bool CreatePipelines(RendererImpl& r) {
    auto load = [&](const char* name) {
        return rhi::LoadShaderModule(r.device, r.shaderDir + "/" + name);
    };
    VkShaderModule vecV  = load("vector.vert.spv");
    VkShaderModule vecF  = load("vector.frag.spv");
    VkShaderModule primV = load("prim.vert.spv");
    VkShaderModule primF = load("prim.frag.spv");
    VkShaderModule presV = load("present.vert.spv");
    VkShaderModule presF = load("present.frag.spv");
    VkShaderModule isoV  = load("iso.vert.spv");
    VkShaderModule isoF  = load("iso.frag.spv");
    const VkShaderModule all[] = { vecV, vecF, primV, primF, presV, presF,
                                   isoV, isoF };
    bool ok = true;
    for (VkShaderModule m : all) ok = ok && m != VK_NULL_HANDLE;

    if (ok) {
        // Every pipeline used inside the content pass declares the stencil
        // format — the pass always binds the clip-mask stencil attachment
        // (dynamic-rendering VUs require matching formats even when a
        // pipeline's own stencil test is off).
        rhi::GraphicsPipelineDesc d;
        d.vert          = vecV;
        d.frag          = vecF;
        d.vertexStride  = sizeof(ContentVertex);
        d.attributes    = { { 0, VK_FORMAT_R32G32_SFLOAT, 0 } };
        d.colorFormat   = kContentFormat;
        d.stencilFormat = kStencilFormat;
        d.samples       = r.device.colorSamples();
        d.layout        = r.contentLayout;
        r.contentPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        // Clip-masked content: draws only where the mask (stencil == 1) is.
        d.stencil    = rhi::StencilMode::TestEqual;
        d.stencilRef = 1;
        r.contentClipPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        // Clip-mask writer / eraser: same content vertex program, colour off,
        // stencil ← 1 (write) or ← 0 (clear, so sequential masks never leak).
        d.stencil = rhi::StencilMode::WriteMask;
        r.clipMaskPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        d.stencilRef = 0;
        r.clipClearPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        // Translucent-stroke self-overlap dedup: colour ON, stencil NOT_EQUAL
        // + REPLACE with a per-draw dynamic reference (the segment's tag).
        d.stencil = rhi::StencilMode::TestNotEqualWrite;
        r.strokeDedupPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        d.stencil    = rhi::StencilMode::None;
        d.stencilRef = 1;

        d.vert         = primV;
        d.frag         = primF;
        d.vertexStride = sizeof(OverlayList::Vertex);
        d.attributes   = { { 0, VK_FORMAT_R32G32_SFLOAT, 0 },
                           { 1, VK_FORMAT_R32G32B32A32_SFLOAT, 8 } };
        d.layout       = r.overlayLayout;
        r.overlayPipeline = rhi::CreateGraphicsPipeline(r.device, d);
        d.stencilFormat = VK_FORMAT_UNDEFINED;

        // Composite: fullscreen, writes the parent iso (×1) with the blend
        // result — its own maths, so fixed-function blending is OFF.
        d.vert               = isoV;
        d.frag               = isoF;
        d.vertexStride       = 0;
        d.attributes.clear();
        d.colorFormat        = kContentFormat;
        d.samples            = VK_SAMPLE_COUNT_1_BIT;
        d.blendPremultiplied = false;
        d.layout             = r.compositeLayout;
        r.compositePipeline = rhi::CreateGraphicsPipeline(r.device, d);

        d.vert               = presV;
        d.frag               = presF;
        d.colorFormat        = kDisplayFormat;
        d.layout             = r.presentLayout;
        r.presentPipeline = rhi::CreateGraphicsPipeline(r.device, d);

        ok = r.contentPipeline && r.contentClipPipeline && r.clipMaskPipeline &&
             r.clipClearPipeline && r.strokeDedupPipeline &&
             r.overlayPipeline && r.compositePipeline && r.presentPipeline;
    }
    for (VkShaderModule m : all)
        if (m) vkDestroyShaderModule(r.device.vk(), m, nullptr);
    return ok;
}

bool CreateFrameSlots(RendererImpl& r) {
    VkDevice dev = r.device.vk();
    for (FrameSlot& s : r.slots) {
        VkCommandPoolCreateInfo pi{};
        pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex = r.device.queueFamily();
        if (vkCreateCommandPool(dev, &pi, nullptr, &s.pool) != VK_SUCCESS)
            return false;
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = s.pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(dev, &ai, &s.cb) != VK_SUCCESS)
            return false;
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(dev, &fi, nullptr, &s.fence) != VK_SUCCESS)
            return false;
        VkQueryPoolCreateInfo qi{};
        qi.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qi.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 2;
        if (vkCreateQueryPool(dev, &qi, nullptr, &s.queries) != VK_SUCCESS)
            return false;
    }
    return true;
}

// (Re)allocate a VIEW's scene descriptor set: binding 0 = the view's
// anchor-rebased instance table, 1/2 = the global item/paint tables. The old
// set may be referenced by an in-flight frame — retire it through the garbage
// ring instead of updating it in place.
void RepointViewSet(RendererImpl& r, ViewImpl& v) {
    if (!r.gpu.StyleTablesReady() || !v.instanceBuf) return;
    if (v.sceneSet) {
        VkDescriptorSet old = v.sceneSet;
        RendererImpl* self = &r;
        r.Defer([self, old]() mutable {
            vkFreeDescriptorSets(self->device.vk(), self->descriptorPool, 1, &old);
        });
    }
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = r.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &r.sceneSetLayout;
    if (vkAllocateDescriptorSets(r.device.vk(), &ai, &v.sceneSet) != VK_SUCCESS) {
        v.sceneSet = VK_NULL_HANDLE;
        return;
    }
    const rhi::Buffer* buffers[3] = { &v.instanceBuf, &r.gpu.ItemTable(),
                                      &r.gpu.PaintTable() };
    VkDescriptorBufferInfo infos[3]{};
    VkWriteDescriptorSet   writes[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        infos[i] = { buffers[i]->buffer, 0, VK_WHOLE_SIZE };
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = v.sceneSet;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &infos[i];
    }
    vkUpdateDescriptorSets(r.device.vk(), 3, writes, 0, nullptr);
    v.styleGen = r.styleGen;
}

// Steady-state signature of a view (docs/Ink/RENDER_GRAPH.md §2): camera +
// tier + anchor + size + background + overlay bytes + scene version. Equal
// signature = the cached display image is still exact, skip every pass.
std::uint64_t ViewSignature(const ViewImpl& v, std::uint64_t sceneVersion,
                            int tier) {
    struct {
        std::uint32_t w, h;
        double panX, panY, zoom, anchorX, anchorY;
        Color bg;
        std::uint64_t scene;
        std::uint64_t previewFilter;
        int tier;
        int pad_;
    } key{ v.width, v.height, v.panX, v.panY, v.zoom, v.anchorX, v.anchorY,
           v.background, sceneVersion, v.previewFilterGen, tier, 0 };
    std::uint64_t h = HashBytes(&key, sizeof key);
    const auto& ov = v.overlay.Vertices();
    if (!ov.empty())
        h = HashBytes(ov.data(), ov.size() * sizeof(OverlayList::Vertex), h);
    return h ? h : 1;   // 0 is reserved for "never rendered"
}

// Grow-and-fill a host-visible ring buffer entry (overlay verts / indirect
// commands). Retires the old buffer through the garbage ring.
void FillHostRingBuffer(RendererImpl& r, rhi::Buffer& buf, const void* data,
                        std::size_t bytes, VkBufferUsageFlags usage) {
    if (bytes == 0) return;
    if (!buf || buf.size < bytes) {
        if (buf) {
            rhi::Buffer old = buf;
            RendererImpl* self = &r;
            r.Defer([self, old]() mutable { rhi::DestroyBuffer(self->device, old); });
        }
        buf = rhi::CreateHostBuffer(r.device, bytes * 2, usage);
    }
    if (buf.mapped) std::memcpy(buf.mapped, data, bytes);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  View target lifecycle
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// A present-layout set (1 sampler) sampling `view`.
VkDescriptorSet MakePresentSet(RendererImpl& r, VkImageView view) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = r.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &r.presentSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(r.device.vk(), &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkDescriptorImageInfo img{ r.device.linearSampler(), view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &img;
    vkUpdateDescriptorSets(r.device.vk(), 1, &w, 0, nullptr);
    return set;
}

// Build one isolation target (msaa + ping-pong linear pair + stencil + the
// two present-layout sets). Returns false on any allocation failure.
bool CreateIso(RendererImpl& r, IsoTarget& iso, std::uint32_t w,
               std::uint32_t h) {
    iso.msaa = rhi::CreateImage2D(r.device, w, h, kContentFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                  r.device.colorSamples());
    const VkImageUsageFlags linUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT;
    iso.linear  = rhi::CreateImage2D(r.device, w, h, kContentFormat, linUsage);
    iso.linearB = rhi::CreateImage2D(r.device, w, h, kContentFormat, linUsage);
    iso.stencil = rhi::CreateImage2D(r.device, w, h, kStencilFormat,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                     r.device.colorSamples());
    if (!iso.msaa || !iso.linear || !iso.linearB || !iso.stencil) return false;
    iso.setA = MakePresentSet(r, iso.linear.view);
    iso.setB = MakePresentSet(r, iso.linearB.view);
    iso.cur  = 0;
    return iso.setA && iso.setB;
}

void RetireIso(RendererImpl& r, IsoTarget& iso) {
    IsoTarget hold = iso;
    r.Defer([self = &r, hold]() mutable {
        if (hold.setA) vkFreeDescriptorSets(self->device.vk(), self->descriptorPool, 1, &hold.setA);
        if (hold.setB) vkFreeDescriptorSets(self->device.vk(), self->descriptorPool, 1, &hold.setB);
        rhi::DestroyImage(self->device, hold.msaa);
        rhi::DestroyImage(self->device, hold.linear);
        rhi::DestroyImage(self->device, hold.linearB);
        rhi::DestroyImage(self->device, hold.stencil);
    });
    iso = IsoTarget{};
}

} // namespace

void RendererImpl::RetireViewTargets(ViewImpl& v) {
    if (!v.HasTargets() && !v.iso[0].msaa) return;
    // Everything a previously-submitted frame may still reference is retired
    // through the current slot's garbage (freed after this slot's fence).
    for (std::uint32_t i = 0; i <= v.isoLevels; ++i)
        if (v.iso[i].msaa) RetireIso(*this, v.iso[i]);
    v.isoLevels = 0;

    rhi::Image display = v.display;
    std::uint64_t tex = v.texture;
    RendererImpl* self = this;
    Defer([self, display, tex]() mutable {
        if (tex && self->hooks.destroy) self->hooks.destroy(self->hooks.user, tex);
        rhi::DestroyImage(self->device, display);
    });
    v.display = {};
    v.texture = 0;
}

void RendererImpl::CreateViewTargets(ViewImpl& v, std::uint32_t w, std::uint32_t h) {
    RetireViewTargets(v);
    v.width = w; v.height = h;

    // iso[0] = the main content target; extra levels are added on demand when
    // the scene's composite depth is known (EnsureIsoLevels in EndFrame).
    if (!CreateIso(*this, v.iso[0], w, h)) { RetireViewTargets(v); return; }
    v.isoLevels = 0;

    v.display = rhi::CreateImage2D(device, w, h, kDisplayFormat,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!v.display) { RetireViewTargets(v); return; }

    // The UI-facing handle of the display image (ImTextureID app-side).
    if (hooks.create)
        v.texture = hooks.create(hooks.user, device.linearSampler(),
                                 v.display.view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    v.forceDirty = true;
}

// Ensure `levels` isolation levels above iso[0] exist (grown on demand as the
// scene's composite depth rises; never shrunk mid-session). Clamped.
void RendererImpl::EnsureIsoLevels(ViewImpl& v, std::uint32_t levels) {
    levels = std::min(levels, ViewImpl::kMaxIsoLevels);
    for (std::uint32_t i = v.isoLevels + 1; i <= levels; ++i)
        if (!CreateIso(*this, v.iso[i], v.width, v.height)) { levels = i - 1; break; }
    if (levels > v.isoLevels) v.isoLevels = levels;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer
// ─────────────────────────────────────────────────────────────────────────────

Renderer::Renderer()  = default;
Renderer::~Renderer() { if (impl_) Shutdown(); }

bool Renderer::Initialize(const InitInfo& info) {
    impl_ = std::make_unique<RendererImpl>();
    RendererImpl& r = *impl_;
    r.hooks     = info.textures;
    r.shaderDir = info.shaderDir;

    rhi::Device::CreateInfo dci;
    dci.instance       = info.instance;
    dci.physicalDevice = info.physicalDevice;
    dci.device         = info.device;
    dci.queue          = info.queue;
    dci.queueFamily    = info.queueFamily;

    const bool ok = r.device.Initialize(dci) &&
                    CreateLayoutsAndPool(r) &&
                    CreatePipelines(r) &&
                    CreateFrameSlots(r) &&
                    r.gpu.Initialize(r.device);
    if (!ok) { Shutdown(); return false; }
    return true;
}

void Renderer::Shutdown() {
    if (!impl_) return;
    RendererImpl& r = *impl_;
    VkDevice dev = r.device.vk();
    if (dev) {
        vkDeviceWaitIdle(dev);
        // Views: run every pending retirement now (device is idle).
        for (auto& [key, view] : r.views) {
            if (view->impl_) {
                ViewImpl& vi = *view->impl_;
                for (std::uint32_t i = 0; i <= vi.isoLevels; ++i) {
                    IsoTarget& iso = vi.iso[i];
                    if (iso.setA) vkFreeDescriptorSets(dev, r.descriptorPool, 1, &iso.setA);
                    if (iso.setB) vkFreeDescriptorSets(dev, r.descriptorPool, 1, &iso.setB);
                    rhi::DestroyImage(r.device, iso.msaa);
                    rhi::DestroyImage(r.device, iso.linear);
                    rhi::DestroyImage(r.device, iso.linearB);
                    rhi::DestroyImage(r.device, iso.stencil);
                }
                if (vi.texture && r.hooks.destroy)
                    r.hooks.destroy(r.hooks.user, vi.texture);
                rhi::DestroyImage(r.device, vi.display);
                for (auto& vb : vi.overlayVb) rhi::DestroyBuffer(r.device, vb);
                for (auto& ib : vi.indirect)  rhi::DestroyBuffer(r.device, ib);
                rhi::DestroyBuffer(r.device, vi.instanceBuf);
                if (vi.sceneSet)
                    vkFreeDescriptorSets(dev, r.descriptorPool, 1, &vi.sceneSet);
                delete view->impl_;
                view->impl_ = nullptr;
            }
        }
        r.views.clear();
        for (FrameSlot& s : r.slots) {
            for (auto& fn : s.garbage) fn();
            s.garbage.clear();
            if (s.queries) vkDestroyQueryPool(dev, s.queries, nullptr);
            if (s.fence)   vkDestroyFence(dev, s.fence, nullptr);
            if (s.pool)    vkDestroyCommandPool(dev, s.pool, nullptr);
            s = {};
        }
        r.gpu.Shutdown(r.device);
        auto destroyPipeline = [&](VkPipeline& p) {
            if (p) { vkDestroyPipeline(dev, p, nullptr); p = VK_NULL_HANDLE; } };
        destroyPipeline(r.contentPipeline);
        destroyPipeline(r.contentClipPipeline);
        destroyPipeline(r.clipMaskPipeline);
        destroyPipeline(r.clipClearPipeline);
        destroyPipeline(r.strokeDedupPipeline);
        destroyPipeline(r.overlayPipeline);
        destroyPipeline(r.compositePipeline);
        destroyPipeline(r.presentPipeline);
        auto destroyLayout = [&](VkPipelineLayout& l) {
            if (l) { vkDestroyPipelineLayout(dev, l, nullptr); l = VK_NULL_HANDLE; } };
        destroyLayout(r.contentLayout);
        destroyLayout(r.overlayLayout);
        destroyLayout(r.presentLayout);
        destroyLayout(r.compositeLayout);
        if (r.descriptorPool)
            vkDestroyDescriptorPool(dev, r.descriptorPool, nullptr);
        if (r.sceneSetLayout)
            vkDestroyDescriptorSetLayout(dev, r.sceneSetLayout, nullptr);
        if (r.presentSetLayout)
            vkDestroyDescriptorSetLayout(dev, r.presentSetLayout, nullptr);
        if (r.compositeSetLayout)
            vkDestroyDescriptorSetLayout(dev, r.compositeSetLayout, nullptr);
        r.device.Shutdown();
    }
    impl_.reset();
}

void Renderer::SetDocument(Document* document) {
    RendererImpl& r = *impl_;
    r.document     = document;
    r.forceCompile = true;
}

void Renderer::BeginFrame() {
    RendererImpl& r = *impl_;
    r.slot = (std::uint32_t)(r.frameIndex % kFramesInFlight);
    FrameSlot& s = r.slots[r.slot];
    VkDevice dev = r.device.vk();

    if (s.armed) {
        vkWaitForFences(dev, 1, &s.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(dev, 1, &s.fence);
        s.armed = false;
        if (s.queried) {
            std::uint64_t ticks[2] = { 0, 0 };
            if (vkGetQueryPoolResults(dev, s.queries, 0, 2, sizeof ticks, ticks,
                                      sizeof(std::uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
                r.lastGpuMs = (float)((double)(ticks[1] - ticks[0]) *
                                      (double)r.device.timestampPeriodNs() / 1.0e6);
            s.queried = false;
        }
        for (auto& fn : s.garbage) fn();
        s.garbage.clear();
    }

    vkResetCommandPool(dev, s.pool, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.cb, &bi);
    vkCmdResetQueryPool(s.cb, s.queries, 0, 2);
    vkCmdWriteTimestamp2(s.cb, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, s.queries, 0);

    r.stats = Stats{};
    r.stats.gpuMs = r.lastGpuMs;
    r.frameOpen = true;
}

View* Renderer::AcquireView(const void* key) {
    RendererImpl& r = *impl_;
    auto it = r.views.find(key);
    if (it == r.views.end()) {
        auto view = std::unique_ptr<View>(new View());
        view->impl_ = new ViewImpl();
        view->impl_->owner = &r;
        it = r.views.emplace(key, std::move(view)).first;
    }
    ViewImpl& v = *it->second->impl_;
    v.usedThisFrame = true;
    v.lastUsedFrame = r.frameIndex;
    return it->second.get();
}

void Renderer::EndFrame() {
    RendererImpl& r = *impl_;
    if (!r.frameOpen) return;
    FrameSlot& s = r.slots[r.slot];
    GpuScene::DeferFn defer = [&r](std::function<void()> fn) {
        r.Defer(std::move(fn));
    };

    // ── Phase 1: scene compile (document → drawables, change-gated) ──────────
    bool sceneChanged = false;
    if (r.document) {
        const auto t0 = Clock::now();
        sceneChanged = r.scene.Compile(*r.document, r.forceCompile);
        r.forceCompile = false;
        r.stats.compileMs = MsSince(t0);
    }
    const auto& drawables = r.scene.Drawables();
    r.stats.instances = (std::uint32_t)drawables.size();

    // ── Phase 2: global style tables (painter-order items/paints) ───────────
    if (sceneChanged) {
        if (r.gpu.SyncStyleTables(r.device, drawables, defer))
            ++r.styleGen;
        ++r.sceneGen;
    }

    // ── Phase 3: per-view prepare (anchor, instances, geometry, commands) ───
    struct ViewJob {
        ViewImpl*  v;
        PushCamera world, px;
    };
    std::vector<ViewJob> jobs;
    const auto tGeom = Clock::now();
    for (auto& [key, view] : r.views) {
        ViewImpl& v = *view->impl_;
        if (!v.usedThisFrame || !v.HasTargets()) { v.overlay.Clear(); continue; }
        ++r.stats.views;

        // Zoom tier with hysteresis (no re-tessellation thrash at boundaries).
        v.tier = v.tierInit ? GeometryCache::StableTier(v.tier, v.zoom)
                            : GeometryCache::TierFromZoom(v.zoom);
        v.tierInit = true;
        const int tier = v.tier;

        // Anchor snap (docs/Ink/GEOMETRY.md §6): the camera-relative origin
        // follows the view centre on a grid of ~4 viewport extents, so it
        // moves rarely; when it does, this view's instances rebase.
        {
            const double extent =
                (double)std::max(v.width, v.height) / (v.zoom > 0 ? v.zoom : 1.0);
            const double grid = extent * 4.0;
            const double cx = v.panX + 0.5 * (double)v.width / v.zoom;
            const double cy = v.panY + 0.5 * (double)v.height / v.zoom;
            const double ax = std::floor(cx / grid + 0.5) * grid;
            const double ay = std::floor(cy / grid + 0.5) * grid;
            if (ax != v.anchorX || ay != v.anchorY) {
                v.anchorX = ax;
                v.anchorY = ay;
                v.instancesDirty = true;
            }
        }
        if (v.sceneGen != r.sceneGen) {
            v.sceneGen = r.sceneGen;
            v.instancesDirty = true;
        }

        const std::uint64_t sig = ViewSignature(v, r.scene.Version(), tier);
        if (!v.forceDirty && sig == v.lastSignature && !v.instancesDirty) {
            v.overlay.Clear();
            continue;
        }
        v.lastSignature = sig;
        v.forceDirty    = false;

        // Rebase this view's instance table when needed (anchor/scene).
        if (v.instancesDirty) {
            const bool recreated = r.gpu.SyncViewInstances(
                r.device, drawables, { v.anchorX, v.anchorY }, v.instanceBuf,
                defer);
            v.instancesDirty = false;
            if (recreated || v.sceneSet == VK_NULL_HANDLE ||
                v.styleGen != r.styleGen)
                RepointViewSet(r, v);
        } else if (v.styleGen != r.styleGen || v.sceneSet == VK_NULL_HANDLE) {
            RepointViewSet(r, v);
        }

        // The view rect in doc space — culling drops DRAWS, never inputs
        // (docs/Ink/GEOMETRY.md §7).
        const double vx0 = v.panX, vy0 = v.panY;
        const double vx1 = v.panX + (double)v.width / v.zoom;
        const double vy1 = v.panY + (double)v.height / v.zoom;
        auto culled = [&](const Drawable& d) {
            const geom::LocalBounds& lb =
                r.cache.GetLocalBounds(*d.path, d.pathHash, tier, d.boolProg);
            if (!lb.valid) return true;
            double inflate = 0.0;
            if (d.isStroke) {
                const double w = GeometryCache::EffectiveWidth(d.stroke, tier);
                inflate = w * (0.5 * std::max(2.0, d.stroke.miterLimit) + 1.0);
            }
            double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
            const DVec2 corners[4] = {
                { lb.min.x - inflate, lb.min.y - inflate },
                { lb.max.x + inflate, lb.min.y - inflate },
                { lb.max.x + inflate, lb.max.y + inflate },
                { lb.min.x - inflate, lb.max.y + inflate } };
            for (const DVec2& c : corners) {
                const DVec2 p = d.world.Apply(c);
                bx0 = std::min(bx0, p.x); by0 = std::min(by0, p.y);
                bx1 = std::max(bx1, p.x); by1 = std::max(by1, p.y);
            }
            return bx1 < vx0 || bx0 > vx1 || by1 < vy0 || by0 > vy1;
        };

        // Composite scopes: assign iso levels + build the post-order run list.
        r.EnsureIsoLevels(v, (std::uint32_t)r.scene.MaxScopeDepth());
        BuildScopePlan(r.scene, v);

        // Build one command run PER scope (only that scope's drawables), in
        // painter order, sliced into SEGMENTS wherever the stencil role
        // changes (mask writes / clears / clipped content — the clip pass).
        // Each segment merges consecutive same-mesh drawables; firstInstance
        // is the GLOBAL drawable index (the instance table is global).
        auto& cmds = v.indirectScratch;
        auto& segs = v.segScratch;
        cmds.clear();
        segs.clear();
        MeshRange last{};
        auto emitDrawable = [&](std::uint32_t i) {
            const Drawable& d = drawables[i];
            std::uint64_t productKey = 0;
            const geom::Mesh* mesh =
                d.isStroke ? r.cache.GetStroke(*d.path, d.pathHash, tier,
                                               d.stroke, productKey, d.boolProg)
                           : r.cache.GetFill(*d.path, d.pathHash, tier, d.rule,
                                             productKey, d.boolProg);
            if (!mesh) return;
            const MeshRange range =
                r.gpu.EnsureResident(r.device, productKey, *mesh, r.cache, defer);
            if (range.indexCount == 0) return;
            if (!cmds.empty() && !segs.empty() &&
                cmds.size() > segs.back().cmdOffset &&   // same segment
                range.firstIndex == last.firstIndex &&
                range.indexCount == last.indexCount &&
                range.vertexOffset == last.vertexOffset &&
                cmds.back().firstInstance + cmds.back().instanceCount == i) {
                ++cmds.back().instanceCount;
            } else {
                VkDrawIndexedIndirectCommand c{};
                c.indexCount    = range.indexCount;
                c.instanceCount = 1;
                c.firstIndex    = range.firstIndex;
                c.vertexOffset  = range.vertexOffset;
                c.firstInstance = i;
                cmds.push_back(c);
            }
            last = range;
            r.stats.triangles += range.indexCount / 3;
        };
        for (ScopeRun& run : v.scopeRuns) {
            if (run.phase != detail::ScopePhase::Content)
                continue;   // composites draw no scene commands
            run.segOffset = (std::uint32_t)segs.size();
            ClipRole cur = ClipRole::None;
            bool open = false;
            // Self-overlap dedup tags for TRANSLUCENT strokes (a stroke's own
            // join/segment triangles overlap on the inner side of a turn and
            // would blend twice). Each such stroke draws in its OWN segment
            // through the dedup pipeline, tagged 2..255 (1 belongs to the clip
            // mask; the stencil clears per content pass, so tags only cycle —
            // and could alias — past 254 translucent strokes in one scope).
            std::uint32_t nextTag = 2;
            const bool preview = !v.previewOwners.empty();
            auto inPreview = [&](NodeId id) {
                return v.previewOwners.find(id) != v.previewOwners.end();
            };
            for (std::uint32_t i = 0; i < (std::uint32_t)drawables.size(); ++i) {
                const Drawable& d = drawables[i];
                if (d.scope != run.scope) continue;
                ClipRole role = d.clip;
                if (preview) {
                    // A node's thumbnail shows its OWN subtree in isolation.
                    // A SCOPE clip/mask (its scope has a clip mask) is honoured
                    // only when the scope's node is itself in the preview set;
                    // inherited from an ancestor outside the set it is dropped
                    // (a clipped child shows its raw content) and the
                    // ancestor's mask geometry is skipped. A SELF clip — the
                    // pattern stencil, whose scope carries NO clip mask —
                    // belongs to its OWNER and is kept whenever the owner
                    // previews, so pattern fills stay cut at their fill-clip
                    // edge (Bounds/Contour/Stroke) exactly as on the canvas.
                    const auto& sc = r.scene.Scopes()[d.scope];
                    const bool scopeClip  = sc.hasClipMask;
                    const bool scopeInSet = sc.node != kNullNode &&
                                            inPreview(sc.node);
                    if (d.isClipSource) {
                        if (scopeClip ? !scopeInSet : !inPreview(d.owner))
                            continue;
                    } else if (!inPreview(d.owner)) {
                        continue;                          // not our subtree
                    }
                    // Per-piece narrowing (fill vignettes): only ONE paint
                    // piece of the owners renders — pattern expansions carry
                    // their host fill's index, masks included.
                    if (v.previewPiece >= 0 &&
                        (d.ownerPiece != (std::uint8_t)v.previewPiece ||
                         d.ownerPieceStroke != v.previewPieceStroke))
                        continue;
                    if (scopeClip && !scopeInSet)
                        role = ClipRole::None;   // inherited scope clip only
                }
                // Mask geometry is never culled (its coverage defines the
                // clip); ordinary content culls against the view rect.
                if (role == ClipRole::None && !d.isClipSource && culled(d))
                    continue;
                // A translucent stroke outside any clip role gets a private
                // dedup segment (its instanced merge is intentionally broken:
                // distinct copies must still blend over EACH OTHER, so each
                // carries its own tag). Clipped translucent strokes keep the
                // clip pipeline — the stencil can't express both tests at
                // once; documented v1 limit.
                const bool dedup = d.isStroke && !d.isClipSource &&
                                   role == ClipRole::None &&
                                   d.color.a < 0.999f;
                if (!open || role != cur || dedup ||
                    (!segs.empty() && segs.back().stencilTag != 0)) {
                    detail::CmdSegment seg;
                    seg.cmdOffset  = (std::uint32_t)cmds.size();
                    seg.role       = role;
                    if (dedup) {
                        seg.stencilTag = nextTag;
                        nextTag = nextTag >= 255 ? 2 : nextTag + 1;
                    }
                    segs.push_back(seg);
                    cur = role;
                    open = true;
                    last = MeshRange{};   // never merge across segments
                }
                emitDrawable(i);
                segs.back().cmdCount =
                    (std::uint32_t)cmds.size() - segs.back().cmdOffset;
            }
            run.segCount = (std::uint32_t)segs.size() - run.segOffset;
        }
        FillHostRingBuffer(r, v.indirect[r.slot], cmds.data(),
                           cmds.size() * sizeof(VkDrawIndexedIndirectCommand),
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // Overlay vertices → this slot's ring buffer.
        const auto& overlayVerts = v.overlay.Vertices();
        v.overlayVertexCount = (std::uint32_t)overlayVerts.size();
        FillHostRingBuffer(r, v.overlayVb[r.slot], overlayVerts.data(),
                           v.overlay.ByteSize(),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        // Camera blocks, computed in double, narrowed per view. Content
        // coordinates arrive ANCHOR-relative from the instance table, so the
        // offset uses (anchor − pan) — small at any zoom (GEOMETRY.md §6).
        ViewJob job;
        job.v = &v;
        job.world.sx = (float)(2.0 * v.zoom / (double)v.width);
        job.world.sy = (float)(2.0 * v.zoom / (double)v.height);
        job.world.ox = (float)((v.anchorX - v.panX) * 2.0 * v.zoom /
                                   (double)v.width - 1.0);
        job.world.oy = (float)((v.anchorY - v.panY) * 2.0 * v.zoom /
                                   (double)v.height - 1.0);
        job.px = PushCamera{ 2.0f / (float)v.width, 2.0f / (float)v.height,
                             -1.0f, -1.0f };
        jobs.push_back(job);
    }
    r.stats.geomMs = MsSince(tGeom);

    // ── Phase 4: staged uploads (once, before any pass) ─────────────────────
    {
        const auto t0 = Clock::now();
        r.gpu.FlushUploads(r.device, s.cb, r.slot, defer);
        r.stats.syncMs = MsSince(t0);
    }

    // ── Phase 5: record the dirty views through the graph ───────────────────
    const auto tRec = Clock::now();
    for (const ViewJob& job : jobs) {
        ViewImpl& v = *job.v;

        graph::RenderGraph g;
        VkBuffer overlayBuffer    = v.overlayVb[r.slot].buffer;
        const std::uint32_t nOverlay = v.overlayVertexCount;

        // Content + compositing: play every scope (post-order) into the iso
        // targets; the root content ends up in iso[0].Cur().
        detail::PlayScopes(r, v, r.slot, g, job.world, job.px, overlayBuffer,
                           nOverlay);

        // Present: sRGB-encode iso[0]'s current linear into the display image.
        IsoTarget& main = v.iso[0];
        graph::RenderGraph::ColorTarget present{};
        present.image = &v.display;
        present.clear = true;   // fullscreen overwrite; clear beats a load
        VkDescriptorSet presentSet = main.CurSet();
        rhi::Image* presentSrc = &main.Cur();
        g.AddRenderPass("present", present, { presentSrc },
                        [&r, presentSet](VkCommandBuffer cmd) {
            detail::RecordPresentPass(r, cmd, presentSet);
        });
        g.ExportSampled(&v.display);
        g.Execute(s.cb);

        v.overlay.Clear();
        ++r.stats.viewsRendered;
        // Content indirect commands + one composite per scope + overlay +
        // present. (indirectScratch holds every scope's commands back-to-back.)
        r.stats.drawCalls += (std::uint32_t)v.indirectScratch.size() +
                             (std::uint32_t)v.scopeRuns.size() +
                             (nOverlay ? 1u : 0u) + 1u;
    }
    r.stats.recordMs = MsSince(tRec);

    vkCmdWriteTimestamp2(s.cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, s.queries, 1);
    vkEndCommandBuffer(s.cb);

    VkCommandBufferSubmitInfo cbi{};
    cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = s.cb;
    VkSubmitInfo2 si{};
    si.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos    = &cbi;
    if (vkQueueSubmit2(r.device.queue(), 1, &si, s.fence) == VK_SUCCESS) {
        s.armed   = true;
        s.queried = true;
    }

    // Evict views whose zone disappeared (unused for 2+ frames).
    for (auto it = r.views.begin(); it != r.views.end();) {
        ViewImpl& v = *it->second->impl_;
        if (!v.usedThisFrame && r.frameIndex - v.lastUsedFrame >= 2) {
            r.RetireViewTargets(v);
            auto deferBuffer = [&r](rhi::Buffer& b) {
                if (!b) return;
                rhi::Buffer old = b;
                r.Defer([dev = &r.device, old]() mutable {
                    rhi::DestroyBuffer(*dev, old);
                });
                b = {};
            };
            for (auto& vb : v.overlayVb) deferBuffer(vb);
            for (auto& ib : v.indirect)  deferBuffer(ib);
            deferBuffer(v.instanceBuf);
            if (v.sceneSet) {
                VkDescriptorSet old = v.sceneSet;
                r.Defer([self = &r, old]() mutable {
                    vkFreeDescriptorSets(self->device.vk(),
                                         self->descriptorPool, 1, &old);
                });
                v.sceneSet = VK_NULL_HANDLE;
            }
            delete it->second->impl_;
            it->second->impl_ = nullptr;
            it = r.views.erase(it);
        } else {
            v.usedThisFrame = false;
            ++it;
        }
    }

    ++r.frameIndex;
    r.frameOpen = false;
    r.published = r.stats;   // readable by the app during the next UI build
}

const Stats& Renderer::GetStats() const { return impl_->published; }
Rect Renderer::SceneBounds() const { return impl_->scene.Bounds(); }

NodeId Renderer::PickAt(DVec2 docPoint, const PickOptions& opt) const {
    return PickTop(impl_->scene, docPoint, opt);
}
std::vector<NodeId> Renderer::PickInBox(DVec2 boxMin, DVec2 boxMax) const {
    return PickBox(impl_->scene, boxMin, boxMax);
}
bool Renderer::NodeBounds(NodeId id, DRect& out) const {
    return impl_->scene.NodeBounds(id, out);
}

DRect Renderer::PreviewPieces(NodeId id, double tolerance,
                              std::vector<PreviewPiece>& out) const {
    out.clear();
    DRect bb;
    RendererImpl& r = *impl_;
    if (!r.document) return bb;

    // The layer subtree of `id` — the set of nodes whose drawables belong to
    // this thumbnail (an instance's/array's copies stamp `owner` = the node,
    // which is already in the set; pattern cells stamp the host too).
    std::unordered_set<NodeId> subtree;
    {
        std::vector<NodeId> stack{ id };
        while (!stack.empty()) {
            const NodeId cur = stack.back(); stack.pop_back();
            if (!subtree.insert(cur).second) continue;
            if (const Node* n = r.document->Find(cur))
                for (NodeId c : n->children) stack.push_back(c);
        }
    }

    // Walk the compiled drawables in painter order: each already carries the
    // RESOLVED geometry (pattern motif copies, instance/array copies, boolean
    // outlines) with its world + colour. Flatten at the requested tolerance
    // (per-tier for boolean programs) and map to document space.
    // Tier chosen so `tolerance` (doc units) matches the flatten error.
    const int tier = GeometryCache::TierFromZoom(
        tolerance > 1e-9 ? GeometryCache::kTolerancePx / tolerance : 1.0);
    for (const Drawable& d : r.scene.Drawables()) {
        if (d.isClipSource) continue;                 // masks never paint
        if (subtree.find(d.owner) == subtree.end() &&
            subtree.find(d.node) == subtree.end()) continue;
        if (!d.path) continue;
        const auto& polys = r.cache.GetFlattened(*d.path, d.pathHash, tier,
                                                 d.boolProg);
        for (const geom::Polyline& pl : polys) {
            if (pl.points.size() < 2) continue;
            PreviewPiece pc;
            pc.closed = pl.closed;
            pc.isStroke = d.isStroke;
            pc.color = d.color;
            pc.pts.reserve(pl.points.size());
            for (const DVec2& p : pl.points) {
                const DVec2 w = d.world.Apply(p);
                pc.pts.push_back(w);
                bb.Grow(w);
            }
            out.push_back(std::move(pc));
        }
    }
    return bb;
}

} // namespace Ink

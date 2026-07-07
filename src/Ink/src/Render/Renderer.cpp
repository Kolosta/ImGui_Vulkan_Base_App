#include "Ink/Render/Renderer.h"

#include "Ink/RHI/Pipeline.h"
#include "Render/RendererInternal.h"

#include <chrono>
#include <cstring>

namespace Ink {

using detail::RendererImpl;
using detail::ViewImpl;
using detail::FrameSlot;
using detail::PushCamera;
using detail::kFramesInFlight;
using detail::kContentFormat;
using detail::kDisplayFormat;

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

    // Ink's own descriptor pool (the app pool only carries sampler slots).
    VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         64 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
    };
    VkDescriptorPoolCreateInfo di{};
    di.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    di.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    di.maxSets       = 96;
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
    const VkShaderModule all[] = { vecV, vecF, primV, primF, presV, presF };
    bool ok = true;
    for (VkShaderModule m : all) ok = ok && m != VK_NULL_HANDLE;

    if (ok) {
        rhi::GraphicsPipelineDesc d;
        d.vert         = vecV;
        d.frag         = vecF;
        d.vertexStride = sizeof(ContentVertex);
        d.attributes   = { { 0, VK_FORMAT_R32G32_SFLOAT, 0 } };
        d.colorFormat  = kContentFormat;
        d.samples      = r.device.colorSamples();
        d.layout       = r.contentLayout;
        r.contentPipeline = rhi::CreateGraphicsPipeline(r.device, d);

        d.vert         = primV;
        d.frag         = primF;
        d.vertexStride = sizeof(OverlayList::Vertex);
        d.attributes   = { { 0, VK_FORMAT_R32G32_SFLOAT, 0 },
                           { 1, VK_FORMAT_R32G32B32A32_SFLOAT, 8 } };
        d.layout       = r.overlayLayout;
        r.overlayPipeline = rhi::CreateGraphicsPipeline(r.device, d);

        d.vert               = presV;
        d.frag               = presF;
        d.vertexStride       = 0;              // fullscreen triangle
        d.attributes.clear();
        d.colorFormat        = kDisplayFormat;
        d.samples            = VK_SAMPLE_COUNT_1_BIT;
        d.blendPremultiplied = false;           // opaque overwrite
        d.layout             = r.presentLayout;
        r.presentPipeline = rhi::CreateGraphicsPipeline(r.device, d);

        ok = r.contentPipeline && r.overlayPipeline && r.presentPipeline;
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

// (Re)allocate the scene descriptor set pointing at the CURRENT table
// buffers. The old set may be referenced by an in-flight frame — retire it
// through the garbage ring instead of updating it in place.
void RepointSceneSet(RendererImpl& r) {
    if (!r.gpu.TablesReady()) return;
    if (r.sceneSet) {
        VkDescriptorSet old = r.sceneSet;
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
    if (vkAllocateDescriptorSets(r.device.vk(), &ai, &r.sceneSet) != VK_SUCCESS) {
        r.sceneSet = VK_NULL_HANDLE;
        return;
    }
    const rhi::Buffer* buffers[3] = { &r.gpu.InstanceTable(), &r.gpu.ItemTable(),
                                      &r.gpu.PaintTable() };
    VkDescriptorBufferInfo infos[3]{};
    VkWriteDescriptorSet   writes[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
        infos[i] = { buffers[i]->buffer, 0, VK_WHOLE_SIZE };
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = r.sceneSet;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo     = &infos[i];
    }
    vkUpdateDescriptorSets(r.device.vk(), 3, writes, 0, nullptr);
}

// Steady-state signature of a view (docs/Ink/RENDER_GRAPH.md §2): camera +
// tier + size + background + overlay bytes + scene version. Equal signature =
// the cached display image is still exact, skip every pass.
std::uint64_t ViewSignature(const ViewImpl& v, std::uint64_t sceneVersion,
                            int tier) {
    struct {
        std::uint32_t w, h;
        double panX, panY, zoom;
        Color bg;
        std::uint64_t scene;
        int tier;
        int pad_;
    } key{ v.width, v.height, v.panX, v.panY, v.zoom, v.background,
           sceneVersion, tier, 0 };
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

void RendererImpl::RetireViewTargets(ViewImpl& v) {
    if (!v.HasTargets() && !v.msaa) return;
    // Everything a previously-submitted frame may still reference is retired
    // through the current slot's garbage: it is freed only after this slot's
    // fence has been waited, which (fences signal in submission order on the
    // queue) also covers the app's UI submit of the previous frame.
    rhi::Image msaa = v.msaa, linear = v.linear, display = v.display;
    VkDescriptorSet set = v.presentSet;
    std::uint64_t   tex = v.texture;
    RendererImpl*   self = this;
    Defer([self, msaa, linear, display, set, tex]() mutable {
        if (tex && self->hooks.destroy) self->hooks.destroy(self->hooks.user, tex);
        if (set) vkFreeDescriptorSets(self->device.vk(), self->descriptorPool, 1, &set);
        rhi::DestroyImage(self->device, msaa);
        rhi::DestroyImage(self->device, linear);
        rhi::DestroyImage(self->device, display);
    });
    v.msaa = {}; v.linear = {}; v.display = {};
    v.presentSet = VK_NULL_HANDLE;
    v.texture = 0;
}

void RendererImpl::CreateViewTargets(ViewImpl& v, std::uint32_t w, std::uint32_t h) {
    RetireViewTargets(v);
    v.width = w; v.height = h;

    v.msaa = rhi::CreateImage2D(device, w, h, kContentFormat,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                device.colorSamples());
    v.linear = rhi::CreateImage2D(device, w, h, kContentFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT);
    v.display = rhi::CreateImage2D(device, w, h, kDisplayFormat,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!v.msaa || !v.linear || !v.display) { RetireViewTargets(v); return; }

    // Present descriptor: samples the resolved linear canvas.
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &presentSetLayout;
    if (vkAllocateDescriptorSets(device.vk(), &ai, &v.presentSet) != VK_SUCCESS) {
        RetireViewTargets(v); return;
    }
    VkDescriptorImageInfo img{ device.linearSampler(), v.linear.view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = v.presentSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img;
    vkUpdateDescriptorSets(device.vk(), 1, &write, 0, nullptr);

    // The UI-facing handle of the display image (ImTextureID app-side).
    if (hooks.create)
        v.texture = hooks.create(hooks.user, device.linearSampler(),
                                 v.display.view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    v.forceDirty = true;
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
                r.RetireViewTargets(*view->impl_);
                for (auto& vb : view->impl_->overlayVb)
                    rhi::DestroyBuffer(r.device, vb);
                for (auto& ib : view->impl_->indirect)
                    rhi::DestroyBuffer(r.device, ib);
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
        destroyPipeline(r.overlayPipeline);
        destroyPipeline(r.presentPipeline);
        auto destroyLayout = [&](VkPipelineLayout& l) {
            if (l) { vkDestroyPipelineLayout(dev, l, nullptr); l = VK_NULL_HANDLE; } };
        destroyLayout(r.contentLayout);
        destroyLayout(r.overlayLayout);
        destroyLayout(r.presentLayout);
        if (r.descriptorPool)
            vkDestroyDescriptorPool(dev, r.descriptorPool, nullptr);
        if (r.sceneSetLayout)
            vkDestroyDescriptorSetLayout(dev, r.sceneSetLayout, nullptr);
        if (r.presentSetLayout)
            vkDestroyDescriptorSetLayout(dev, r.presentSetLayout, nullptr);
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

    // ── Phase 2: GPU tables (painter-order instance/item/paint arrays) ──────
    if (sceneChanged) {
        const bool recreated = r.gpu.SyncTables(r.device, drawables, defer);
        if (recreated || r.sceneSet == VK_NULL_HANDLE)
            RepointSceneSet(r);
    }

    // ── Phase 3: per-view prepare (geometry residency + indirect commands) ──
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

        const int tier = GeometryCache::TierFromZoom(v.zoom);
        const std::uint64_t sig = ViewSignature(v, r.scene.Version(), tier);
        if (!v.forceDirty && sig == v.lastSignature) { v.overlay.Clear(); continue; }
        v.lastSignature = sig;
        v.forceDirty    = false;

        // Build the view's indirect command list: one command per run of
        // consecutive drawables sharing a mesh range (instance index ==
        // drawable index, so runs merge for free — the grid is one command).
        auto& cmds = v.indirectScratch;
        cmds.clear();
        MeshRange lastRange{};
        for (std::uint32_t i = 0; i < (std::uint32_t)drawables.size(); ++i) {
            const Drawable& d = drawables[i];
            std::uint64_t productKey = 0;
            const geom::Mesh* mesh =
                d.isStroke ? r.cache.GetStroke(*d.path, d.pathHash, tier,
                                               d.stroke, productKey)
                           : r.cache.GetFill(*d.path, d.pathHash, tier, d.rule,
                                             productKey);
            if (!mesh) continue;
            const MeshRange range =
                r.gpu.EnsureResident(r.device, productKey, *mesh, r.cache, defer);
            if (range.indexCount == 0) continue;

            if (!cmds.empty() &&
                range.firstIndex == lastRange.firstIndex &&
                range.indexCount == lastRange.indexCount &&
                range.vertexOffset == lastRange.vertexOffset &&
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
            lastRange = range;
            r.stats.triangles += range.indexCount / 3;
        }
        v.indirectCount = (std::uint32_t)cmds.size();
        FillHostRingBuffer(r, v.indirect[r.slot], cmds.data(),
                           cmds.size() * sizeof(VkDrawIndexedIndirectCommand),
                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // Overlay vertices → this slot's ring buffer.
        const auto& overlayVerts = v.overlay.Vertices();
        v.overlayVertexCount = (std::uint32_t)overlayVerts.size();
        FillHostRingBuffer(r, v.overlayVb[r.slot], overlayVerts.data(),
                           v.overlay.ByteSize(),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        // Camera blocks, computed in double, narrowed per view
        // (world → NDC for content; view px → NDC for the overlay).
        ViewJob job;
        job.v = &v;
        job.world.sx = (float)(2.0 * v.zoom / (double)v.width);
        job.world.sy = (float)(2.0 * v.zoom / (double)v.height);
        job.world.ox = (float)(-v.panX * 2.0 * v.zoom / (double)v.width - 1.0);
        job.world.oy = (float)(-v.panY * 2.0 * v.zoom / (double)v.height - 1.0);
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
        graph::RenderGraph::ColorTarget content{};
        content.image         = &v.msaa;
        content.clear         = true;
        content.clearColor[0] = v.background.r;
        content.clearColor[1] = v.background.g;
        content.clearColor[2] = v.background.b;
        content.clearColor[3] = v.background.a;
        content.resolveTo     = &v.linear;
        VkBuffer indirectBuffer   = v.indirect[r.slot].buffer;
        const std::uint32_t nCmds = v.indirectCount;
        VkBuffer overlayBuffer    = v.overlayVb[r.slot].buffer;
        const std::uint32_t nOverlay = v.overlayVertexCount;
        const PushCamera world = job.world, px = job.px;
        g.AddRenderPass("content", content, {},
                        [&r, world, px, indirectBuffer, nCmds, overlayBuffer,
                         nOverlay](VkCommandBuffer cmd) {
            detail::RecordContentPass(r, cmd, world, indirectBuffer, nCmds);
            detail::RecordOverlayPass(r, cmd, px, overlayBuffer, nOverlay);
        });

        graph::RenderGraph::ColorTarget present{};
        present.image = &v.display;
        present.clear = true;   // fullscreen overwrite; clear beats a load
        VkDescriptorSet presentSet = v.presentSet;
        g.AddRenderPass("present", present, { &v.linear },
                        [&r, presentSet](VkCommandBuffer cmd) {
            detail::RecordPresentPass(r, cmd, presentSet);
        });
        g.ExportSampled(&v.display);
        g.Execute(s.cb);

        v.overlay.Clear();
        ++r.stats.viewsRendered;
        r.stats.drawCalls += nCmds + (nOverlay ? 1u : 0u) + 1u;
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
            for (auto& vb : v.overlayVb) {
                rhi::Buffer old = vb;
                r.Defer([dev = &r.device, old]() mutable {
                    rhi::DestroyBuffer(*dev, old);
                });
                vb = {};
            }
            for (auto& ib : v.indirect) {
                rhi::Buffer old = ib;
                r.Defer([dev = &r.device, old]() mutable {
                    rhi::DestroyBuffer(*dev, old);
                });
                ib = {};
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

} // namespace Ink

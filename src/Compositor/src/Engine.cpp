#include "Compositor/Engine.h"
#include "Internal.h"

#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor · Engine — the engine's lifecycle and per-frame spine. The bulk of
//  the work lives in sibling translation units (one Engine class split by concern,
//  see CLAUDE.md "File Organisation"):
//    · Pipelines/Pipelines.cpp  — all VkPipelines + shader loading
//    · GPU/Targets.cpp          — per-view target / iso / backdrop / vbo / slot
//    · Frame/Signature.cpp      — the content signature (rebuild-or-reuse)
//    · Passes/RenderView.cpp    — the P1→P2→P4 render orchestrator
//  Shared helpers + push-constant layouts live in Internal.h.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Engine::Initialize(VkInstance instance,
                        VkDevice device, VkPhysicalDevice physicalDevice,
                        VkQueue queue, uint32_t queueFamily,
                        VkCommandPool commandPool, VkSampler sampler,
                        const std::string& shaderDir) {
    instance_       = instance;
    device_         = device;
    physicalDevice_ = physicalDevice;
    queue_          = queue;
    queueFamily_    = queueFamily;
    appCommandPool_ = commandPool;
    sampler_        = sampler;
    shaderDir_      = shaderDir;

    if (!allocator_.Create(instance_, physicalDevice_, device_, VK_API_VERSION_1_3))
        std::fprintf(stderr, "[compositor] VMA allocator creation failed\n");

    // GPU timing support (Lot 13-0): timestampPeriod ns/tick, 0 if unsupported.
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);
        if (props.limits.timestampComputeAndGraphics)
            timestampPeriodNs_ = props.limits.timestampPeriod;
    }

    // Our own command pool: per-buffer reset so we can re-record slots each frame.
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    Check(vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_), "vkCreateCommandPool");

    stencilFormat_ = ChooseStencilFormat();

    CreateCompositeStatics();
    CreateShapePipeline();
    CreateFillPipelines();          // Lot 13-4a: stencil-then-cover base fill
    CreateCoveragePipelines();
    CreateIsoCompositePipeline();
    CreateBlendPipeline();
    CreatePickingPipeline();

    initialized_ = true;
}

void Engine::Shutdown() {
    // (The Application waits the device idle before calling this.)
    for (auto& kv : targets_) DestroyTarget(kv.second);
    targets_.clear();
    for (auto& kv : glyphTex_) {
        GlyphTex& g = kv.second;
        if (g.imguiTex) ImGui_ImplVulkan_RemoveTexture(g.imguiTex);
        if (g.view) vkDestroyImageView(device_, g.view, nullptr);
        if (g.image) allocator_.DestroyImage(g.image, g.alloc);
        if (g.vbo) allocator_.DestroyBuffer(g.vbo, g.vboAlloc);
    }
    glyphTex_.clear();

    for (SubmitSlot& s : slots_) {
        if (s.sem)   vkDestroySemaphore(device_, s.sem, nullptr);
        if (s.fence) vkDestroyFence(device_, s.fence, nullptr);
        if (s.tsPool) vkDestroyQueryPool(device_, s.tsPool, nullptr);
        // command buffers are freed with the pool
    }
    slots_.clear();

    if (blitPipeline_)   { vkDestroyPipeline(device_, blitPipeline_, nullptr); blitPipeline_ = VK_NULL_HANDLE; }
    blitPipelineRP_ = VK_NULL_HANDLE;
    if (blitVert_)       { vkDestroyShaderModule(device_, blitVert_, nullptr); blitVert_ = VK_NULL_HANDLE; }
    if (blitFrag_)       { vkDestroyShaderModule(device_, blitFrag_, nullptr); blitFrag_ = VK_NULL_HANDLE; }
    if (blitPipeLayout_) { vkDestroyPipelineLayout(device_, blitPipeLayout_, nullptr); blitPipeLayout_ = VK_NULL_HANDLE; }
    if (blitDescPool_)   { vkDestroyDescriptorPool(device_, blitDescPool_, nullptr); blitDescPool_ = VK_NULL_HANDLE; }
    if (blitSetLayout_)  { vkDestroyDescriptorSetLayout(device_, blitSetLayout_, nullptr); blitSetLayout_ = VK_NULL_HANDLE; }

    if (overlayPipeline_)   { vkDestroyPipeline(device_, overlayPipeline_, nullptr); overlayPipeline_ = VK_NULL_HANDLE; }
    overlayPipelineRP_ = VK_NULL_HANDLE;
    if (overlayVert_)       { vkDestroyShaderModule(device_, overlayVert_, nullptr); overlayVert_ = VK_NULL_HANDLE; }
    if (overlayFrag_)       { vkDestroyShaderModule(device_, overlayFrag_, nullptr); overlayFrag_ = VK_NULL_HANDLE; }
    if (overlayPipeLayout_) { vkDestroyPipelineLayout(device_, overlayPipeLayout_, nullptr); overlayPipeLayout_ = VK_NULL_HANDLE; }
    if (overlayVbo_) { allocator_.DestroyBuffer(overlayVbo_, overlayVboAlloc_); overlayVbo_ = VK_NULL_HANDLE; overlayVboAlloc_ = nullptr; overlayVboMapped_ = nullptr; }
    if (overlayIbo_) { allocator_.DestroyBuffer(overlayIbo_, overlayIboAlloc_); overlayIbo_ = VK_NULL_HANDLE; overlayIboAlloc_ = nullptr; overlayIboMapped_ = nullptr; }

    if (backdropPipeline_) { vkDestroyPipeline(device_, backdropPipeline_, nullptr); backdropPipeline_ = VK_NULL_HANDLE; }
    if (shapePipeline_)   { vkDestroyPipeline(device_, shapePipeline_, nullptr); shapePipeline_ = VK_NULL_HANDLE; }
    if (shapeVert_)       { vkDestroyShaderModule(device_, shapeVert_, nullptr); shapeVert_ = VK_NULL_HANDLE; }
    if (shapeFrag_)       { vkDestroyShaderModule(device_, shapeFrag_, nullptr); shapeFrag_ = VK_NULL_HANDLE; }
    if (shapePipeLayout_) { vkDestroyPipelineLayout(device_, shapePipeLayout_, nullptr); shapePipeLayout_ = VK_NULL_HANDLE; }

    // Stencil-then-cover base fill (Lot 13-4a).
    if (fillStencilPipeline_) { vkDestroyPipeline(device_, fillStencilPipeline_, nullptr); fillStencilPipeline_ = VK_NULL_HANDLE; }
    if (fillCoverPipeline_)   { vkDestroyPipeline(device_, fillCoverPipeline_, nullptr); fillCoverPipeline_ = VK_NULL_HANDLE; }
    if (fillStencilVert_)     { vkDestroyShaderModule(device_, fillStencilVert_, nullptr); fillStencilVert_ = VK_NULL_HANDLE; }
    if (fillCoverVert_)       { vkDestroyShaderModule(device_, fillCoverVert_, nullptr); fillCoverVert_ = VK_NULL_HANDLE; }
    if (fillCoverFrag_)       { vkDestroyShaderModule(device_, fillCoverFrag_, nullptr); fillCoverFrag_ = VK_NULL_HANDLE; }
    if (fillStencilPipeLayout_) { vkDestroyPipelineLayout(device_, fillStencilPipeLayout_, nullptr); fillStencilPipeLayout_ = VK_NULL_HANDLE; }
    if (fillCoverPipeLayout_)   { vkDestroyPipelineLayout(device_, fillCoverPipeLayout_, nullptr); fillCoverPipeLayout_ = VK_NULL_HANDLE; }

    if (stencilMaskPipeline_) { vkDestroyPipeline(device_, stencilMaskPipeline_, nullptr); stencilMaskPipeline_ = VK_NULL_HANDLE; }
    if (patternFillPipeline_) { vkDestroyPipeline(device_, patternFillPipeline_, nullptr); patternFillPipeline_ = VK_NULL_HANDLE; }
    if (strokeFillPipeline_)  { vkDestroyPipeline(device_, strokeFillPipeline_, nullptr); strokeFillPipeline_ = VK_NULL_HANDLE; }
    if (patternVert_)         { vkDestroyShaderModule(device_, patternVert_, nullptr); patternVert_ = VK_NULL_HANDLE; }
    if (patternFrag_)         { vkDestroyShaderModule(device_, patternFrag_, nullptr); patternFrag_ = VK_NULL_HANDLE; }
    if (coverPipeLayout_)     { vkDestroyPipelineLayout(device_, coverPipeLayout_, nullptr); coverPipeLayout_ = VK_NULL_HANDLE; }

    if (eraseCompPipeline_) { vkDestroyPipeline(device_, eraseCompPipeline_, nullptr); eraseCompPipeline_ = VK_NULL_HANDLE; }
    if (isoCompPipeline_)   { vkDestroyPipeline(device_, isoCompPipeline_, nullptr); isoCompPipeline_ = VK_NULL_HANDLE; }
    if (isoVert_)           { vkDestroyShaderModule(device_, isoVert_, nullptr); isoVert_ = VK_NULL_HANDLE; }
    if (isoFrag_)           { vkDestroyShaderModule(device_, isoFrag_, nullptr); isoFrag_ = VK_NULL_HANDLE; }
    if (isoCompPipeLayout_) { vkDestroyPipelineLayout(device_, isoCompPipeLayout_, nullptr); isoCompPipeLayout_ = VK_NULL_HANDLE; }

    if (blendPipeline_)   { vkDestroyPipeline(device_, blendPipeline_, nullptr); blendPipeline_ = VK_NULL_HANDLE; }
    if (blendFrag_)       { vkDestroyShaderModule(device_, blendFrag_, nullptr); blendFrag_ = VK_NULL_HANDLE; }
    if (blendPipeLayout_) { vkDestroyPipelineLayout(device_, blendPipeLayout_, nullptr); blendPipeLayout_ = VK_NULL_HANDLE; }

    if (pickPipeline_)    { vkDestroyPipeline(device_, pickPipeline_, nullptr); pickPipeline_ = VK_NULL_HANDLE; }
    if (pickVert_)        { vkDestroyShaderModule(device_, pickVert_, nullptr); pickVert_ = VK_NULL_HANDLE; }
    if (pickFrag_)        { vkDestroyShaderModule(device_, pickFrag_, nullptr); pickFrag_ = VK_NULL_HANDLE; }
    if (pickPipeLayout_)  { vkDestroyPipelineLayout(device_, pickPipeLayout_, nullptr); pickPipeLayout_ = VK_NULL_HANDLE; }
    if (blendSetLayout_)  { vkDestroyDescriptorSetLayout(device_, blendSetLayout_, nullptr); blendSetLayout_ = VK_NULL_HANDLE; }

    if (cmdPool_) { vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }

    allocator_.Destroy();
    initialized_ = false;
}

// ── Per-frame spine ────────────────────────────────────────────────────────────

void Engine::BeginFrame() {
    ++frame_;
    frameWaits_.clear();
    viewsThisFrame_ = 0;
    metrics_ = Metrics{};
    cache_.BeginFrame();
    for (auto& kv : targets_) kv.second.placed = false;
}

void Engine::PlaceView(const void* key, ImVec4 ndcRect) {
    auto it = targets_.find(key);
    if (it == targets_.end()) return;
    ViewTarget& t = it->second;
    t.ndc[0] = ndcRect.x; t.ndc[1] = ndcRect.y;
    t.ndc[2] = ndcRect.z; t.ndc[3] = ndcRect.w;
    t.placed = true;
}

uint64_t Engine::Pick(const void* key, int px, int py) {
    auto it = targets_.find(key);
    if (it == targets_.end()) return 0;          // view not rendered yet
    ViewTarget& t = it->second;

    // 1) Harvest a finished readback (non-blocking — never stall the UI thread).
    if (t.pickPending && t.pickSubmitFence != VK_NULL_HANDLE &&
        vkGetFenceStatus(device_, t.pickSubmitFence) == VK_SUCCESS) {
        uint32_t objId = 0;
        if (t.pickReadMapped) std::memcpy(&objId, t.pickReadMapped, sizeof(uint32_t));
        uint64_t shapeId = 0;
        if (objId != 0 && objId <= t.pickIdsInFlight.size())
            shapeId = t.pickIdsInFlight[objId - 1];
        t.pickLastX = t.pickInFlightX; t.pickLastY = t.pickInFlightY;
        t.pickLastId = shapeId;
        t.pickPending = false;
    }

    // 2) Ask the next frame to sample this pixel (the id-pass + copy run in RenderView).
    t.pickRequested = true;
    t.pickX = px; t.pickY = py;

    // 3) Return the freshest result we have for this exact pixel (else 0 → the caller
    //    can fall back to its CPU hit-test until the async result lands).
    if (t.pickLastX == px && t.pickLastY == py) return t.pickLastId;
    return 0;
}

void Engine::SubmitOverlay(const std::vector<Renderer::IViewRenderer::OverlayVertex>& verts,
                           const std::vector<uint32_t>& indices) {
    overlayIndexCount_ = 0;
    if (verts.empty() || indices.empty() || !initialized_) return;
    const VkDeviceSize vbytes = verts.size() * sizeof(verts[0]);
    const VkDeviceSize ibytes = indices.size() * sizeof(uint32_t);
    if (overlayVbo_ == VK_NULL_HANDLE || overlayVboCap_ < vbytes) {
        if (overlayVbo_) allocator_.DestroyBuffer(overlayVbo_, overlayVboAlloc_);
        VkDeviceSize cap = 4096; while (cap < vbytes) cap *= 2;
        allocator_.CreateHostBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    overlayVbo_, overlayVboAlloc_, &overlayVboMapped_);
        overlayVboCap_ = cap;
    }
    if (overlayIbo_ == VK_NULL_HANDLE || overlayIboCap_ < ibytes) {
        if (overlayIbo_) allocator_.DestroyBuffer(overlayIbo_, overlayIboAlloc_);
        VkDeviceSize cap = 4096; while (cap < ibytes) cap *= 2;
        allocator_.CreateHostBuffer(cap, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    overlayIbo_, overlayIboAlloc_, &overlayIboMapped_);
        overlayIboCap_ = cap;
    }
    std::memcpy(overlayVboMapped_, verts.data(), (size_t)vbytes);
    std::memcpy(overlayIboMapped_, indices.data(), (size_t)ibytes);
    allocator_.Flush(overlayVboAlloc_, 0, vbytes);
    allocator_.Flush(overlayIboAlloc_, 0, ibytes);
    overlayIndexCount_ = (uint32_t)indices.size();
}

void Engine::CompositeMainPass(VkCommandBuffer cmd, uint32_t fbW, uint32_t fbH,
                               VkRenderPass renderPass) {
    if (!initialized_ || fbW == 0 || fbH == 0) return;
    EnsureCompositePipeline(renderPass);
    if (!blitPipeline_) return;

    VkViewport vp{ 0.0f, 0.0f, (float)fbW, (float)fbH, 0.0f, 1.0f };
    VkRect2D   sc{ { 0, 0 }, { fbW, fbH } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline_);

    for (auto& kv : targets_) {
        ViewTarget& t = kv.second;
        if (!t.placed || t.lastUsedFrame != frame_ || t.desc == VK_NULL_HANDLE) continue;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeLayout_,
                                0, 1, &t.desc, 0, nullptr);
        vkCmdPushConstants(cmd, blitPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 4, t.ndc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
        metrics_.drawCalls++;
    }

    // Editor overlays (Lot 12): OVER the canvas, UNDER ImGui (this pass runs before
    // ImGui's draw data in the same swapchain render pass).
    if (overlayIndexCount_ > 0) {
        CreateOverlayPipeline(renderPass);
        if (overlayPipeline_) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline_);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &overlayVbo_, &off);
            vkCmdBindIndexBuffer(cmd, overlayIbo_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, overlayIndexCount_, 1, 0, 0, 0);
            metrics_.drawCalls++;
        }
    }
}

void Engine::EndFrame() {
    // Evict targets for zones that didn't render this frame (closed/joined). Their
    // last offscreen submit completed frames ago, so destruction is safe.
    for (auto it = targets_.begin(); it != targets_.end(); ) {
        if (it->second.lastUsedFrame != frame_) {
            DestroyTarget(it->second);
            it = targets_.erase(it);
        } else {
            ++it;
        }
    }
    cache_.Evict();   // drop per-shape cache entries not touched recently
}

// ── One-shot paths (Lot 2+: thumbnails / Symbol Viewer) ────────────────────────

bool Engine::RenderToRGBA(const Renderer::Document& /*doc*/, const Renderer::Camera& /*cam*/,
                          int /*w*/, int /*h*/, ImVec4 /*clearColor*/,
                          std::vector<unsigned char>& /*outRGBA*/) {
    return false;
}

ImTextureID Engine::RenderGlyphCached(uint64_t key, uint64_t contentHash,
                                      const std::vector<Renderer::Shape>& shapes,
                                      int widthPx, int heightPx, float padFrac,
                                      ImVec4 clearColor, bool exactFit,
                                      const Renderer::Vec2* frameMin,
                                      const Renderer::Vec2* frameMax) {
    if (!initialized_ || widthPx <= 0 || heightPx <= 0 || !shapePipeline_) return (ImTextureID)0;
    const uint32_t w = (uint32_t)widthPx, h = (uint32_t)heightPx;   // glyph: no SSAA, kept small

    GlyphTex& gt = glyphTex_[key];
    gt.lastUsedFrame = frame_;
    const bool sizeOk = gt.image != VK_NULL_HANDLE && gt.w == w && gt.h == h;
    if (sizeOk && gt.contentHash == contentHash && gt.imguiTex) return (ImTextureID)gt.imguiTex;

    if (!sizeOk) {
        vkDeviceWaitIdle(device_);
        if (gt.imguiTex) { ImGui_ImplVulkan_RemoveTexture(gt.imguiTex); gt.imguiTex = VK_NULL_HANDLE; }
        if (gt.view) { vkDestroyImageView(device_, gt.view, nullptr); gt.view = VK_NULL_HANDLE; }
        if (gt.image) { allocator_.DestroyImage(gt.image, gt.alloc); gt.image = VK_NULL_HANDLE; gt.alloc = nullptr; }
        allocator_.CreateImage(w, h, kColorFormat,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               gt.image, gt.alloc);
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = gt.image; vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = kColorFormat;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(device_, &vci, nullptr, &gt.view), "vkCreateImageView (glyph)");
        gt.imguiTex = ImGui_ImplVulkan_AddTexture(sampler_, gt.view,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        gt.w = w; gt.h = h;
    }
    gt.contentHash = contentHash;

    // Frame: explicit caller bounds, or the auto union of all shapes (local doc-units).
    Renderer::Vec2 mn{ 1e30f, 1e30f }, mx{ -1e30f, -1e30f }; bool any = false;
    if (frameMin && frameMax) { mn = *frameMin; mx = *frameMax; any = true; }
    else for (const Renderer::Shape& s : shapes) {
        Renderer::Vec2 a, b;
        if (Renderer::Tessellator::WorldBounds(s, 1.0f, a, b)) {
            mn.x = std::min(mn.x, a.x); mn.y = std::min(mn.y, a.y);
            mx.x = std::max(mx.x, b.x); mx.y = std::max(mx.y, b.y); any = true;
        }
    }
    Renderer::Camera cam; cam.unitScale = 1.0f;
    if (any) {
        float gw = std::max(0.01f, mx.x - mn.x), gh = std::max(0.01f, mx.y - mn.y);
        if (exactFit) {
            cam.zoom = (float)w / gw; cam.panX = mn.x; cam.panY = mn.y;
        } else {
            float pad = padFrac;
            float z = std::min((w * (1.0f - 2 * pad)) / gw, (h * (1.0f - 2 * pad)) / gh);
            cam.zoom = z;
            Renderer::Vec2 c{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
            cam.panX = c.x - ((float)w * 0.5f) / cam.zoom;
            cam.panY = c.y - ((float)h * 0.5f) / cam.zoom;
        }
    } else cam.zoom = 1.0f;

    // Tessellate the shapes via the document path (one throwaway page), base mesh only.
    Renderer::Document gdoc;
    Renderer::Artboard gp; gp.pos = { 0, 0 };
    gp.size = { std::max(0.01f, mx.x - mn.x), std::max(0.01f, mx.y - mn.y) };
    gp.shapes = shapes;
    gdoc.artboards.push_back(std::move(gp));
    scratchMesh_.clear();
    std::vector<Renderer::Tessellator::PageSeg> segs = Renderer::Tessellator::BuildDocumentSegmented(
        gdoc, scratchMesh_, cam.zoom, nullptr, /*includeLoose=*/false, &glyphCache_,
        nullptr, /*outCover=*/nullptr, /*outDecor=*/nullptr);
    const VkDeviceSize bytes = scratchMesh_.vertices.size() * sizeof(Renderer::Vertex);
    if (bytes == 0) return (ImTextureID)gt.imguiTex;   // nothing to draw → keep cleared
    if (gt.vbo == VK_NULL_HANDLE || gt.vboCap < bytes) {
        if (gt.vbo) allocator_.DestroyBuffer(gt.vbo, gt.vboAlloc);
        VkDeviceSize cap = 4096; while (cap < bytes) cap *= 2;
        allocator_.CreateHostBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    gt.vbo, gt.vboAlloc, &gt.vboMapped);
        gt.vboCap = cap;
    }
    std::memcpy(gt.vboMapped, scratchMesh_.vertices.data(), (size_t)bytes);
    allocator_.Flush(gt.vboAlloc, 0, bytes);

    // Record + submit a one-shot pass synchronously.
    SubmitSlot& slot = AcquireSlot();
    vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &slot.fence);
    VkCommandBuffer cmd = slot.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier toAttach{};
    toAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toAttach.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toAttach.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAttach.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toAttach.image = gt.image; toAttach.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toAttach.srcQueueFamilyIndex = toAttach.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toAttach);

    VkRenderingAttachmentInfo c{};
    c.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    c.imageView = gt.view; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    c.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    c.clearValue.color = { { clearColor.x, clearColor.y, clearColor.z, clearColor.w } };
    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = { { 0, 0 }, { w, h } };
    ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &c;
    vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{ 0, 0, (float)w, (float)h, 0, 1 };
    VkRect2D sc{ { 0, 0 }, { w, h } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shapePipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gt.vbo, &off);
    ShapePush push{};
    push.pan[0] = cam.panX; push.pan[1] = cam.panY;
    push.target[0] = (float)w; push.target[1] = (float)h;
    push.zoom = cam.zoom; push.unitScale = cam.unitScale;
    vkCmdPushConstants(cmd, shapePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShapePush), &push);
    vkCmdDraw(cmd, (uint32_t)scratchMesh_.vertices.size(), 1, 0, 0);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier toRead{};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.image = gt.image; toRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toRead.srcQueueFamilyIndex = toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    Check(vkQueueSubmit(queue_, 1, &si, slot.fence), "vkQueueSubmit (glyph)");
    vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, UINT64_MAX);   // synchronous: ready to sample
    return (ImTextureID)gt.imguiTex;
}

} // namespace Comp

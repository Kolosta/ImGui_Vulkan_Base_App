#include "Compositor/Engine.h"
#include "../Internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Lot 13-0 profiling: a tiny wall-clock timer (ms) for the per-stage CPU breakdown.
namespace {
using Clock = std::chrono::steady_clock;
inline Clock::time_point Now() { return Clock::now(); }
inline float Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<float, std::milli>(b - a).count();
}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - Passes/RenderView: the render orchestrator for one view.
//
//  Rebuilds (or reuses) the per-view geometry, then records the offscreen pass
//  that walks the logical stages on the view's VMA target:
//    P1 Content     - page substrate + document geometry (fills / strokes)
//    P2 Mask/Cover  - stencil-masked patterns, covered transparent strokes,
//                     and the subtractive erase (dst-out)
//    P4 Composite   - per-object isolation: opacity, blend modes, erase
//  ImGui never touches this - the result is composited onto the swapchain by
//  Engine::CompositeMainPass. Pipelines are built in Pipelines/Pipelines.cpp;
//  target/iso/backdrop resources in GPU/Targets.cpp.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

ImTextureID Engine::RenderView(const void* key, const Renderer::Document& doc,
                               const Renderer::Camera& cam, int widthPx, int heightPx,
                               ImVec4 clearColor,
                               const std::vector<Renderer::Tessellator::PagePlacement>* placements,
                               bool includeLoose, bool /*focused*/) {
    if (!initialized_ || widthPx <= 0 || heightPx <= 0) return (ImTextureID)0;
    const uint32_t lw = (uint32_t)widthPx, lh = (uint32_t)heightPx;   // logical px

    // Supersample: render the offscreen image SSAA× larger on each axis; the
    // composite's linear sampler downscales it at blit time (anti-aliasing), like
    // the legacy renderer. The camera maps doc→NDC with the LOGICAL size, so the
    // content position/size is unchanged — only the raster resolution increases.
    int ssaa = kSSAA;
    while (ssaa > 1 && (lw * (uint32_t)ssaa > kMaxDim || lh * (uint32_t)ssaa > kMaxDim))
        --ssaa;
    const uint32_t w = lw * (uint32_t)ssaa, h = lh * (uint32_t)ssaa;  // physical px

    ViewTarget& t = AcquireTarget(key, w, h);
    t.lastUsedFrame = frame_;
    metrics_.views++;

    // Rebuild the per-view geometry only when the content signature changes (pan/
    // zoom within a detail bucket reuse the buffer — like the legacy renderer).
    // Lot 13-0: time the signature walk (O(N) every frame — a prime suspect).
    const int detailBucket = Renderer::Tessellator::DetailBucketIndex(cam.zoom);
    auto tSig0 = Now();
    // One O(N) walk yields BOTH the rebuild gate (sig) and the per-shape change diff
    // (Lot 13-1a) — the diff tells us how many shapes actually changed, so we can see
    // whether a rebuild is inherent or a full-VBO over-rebuild (and drive the pool).
    DirtyTracker::Diff diff{};
    const uint64_t sig = BuildSignatureAndDiff(doc, placements, includeLoose,
                                               detailBucket, t.dirty, diff);
    metrics_.sigMs += Ms(tSig0, Now());
    metrics_.shapesDirty += diff.Dirty();
    if (!t.hasGeom || t.buildSig != sig) {
        scratchMesh_.clear();
        scratchCover_.clear();
        // P2 Mask/Coverage build: outCover non-null ⇒ procedural patterns emit cut
        // polygons + a SurfaceDraw, transparent strokes emit ribbon coverage + a
        // StrokeDraw (both into the cover mesh); outDecor null ⇒ decorators stay
        // baked into the base mesh. base = fills + OPAQUE strokes + baked decor.
        auto tTess0 = Now();
        // Lot 13-4a/b: build the PURE solid fills FIRST, ear-clip-free (trivial winding
        // fans + cover quads). Their ids are then SKIPPED in BuildDocumentSegmented so
        // the ear-clip never runs for them (the FPS win) — the Tessellator still emits
        // an empty ObjDraw per skipped shape (baseCount 0) to hold its z-order/group
        // slot, and the renderer fills the base via fan+cover in drawObjectContent.
        // Patterns/strokes/mixed objects stay on the baked path (parity).
        // Incremental fill build (Lot 13-1b-3): reuse the PREVIOUS build's FillObjects
        // for unchanged shapes (no re-flatten), (re)flatten only dirty/new ones. Build
        // the id→prev map from the current (soon-to-be-replaced) fillPages, then rebuild.
        std::unordered_map<uint64_t, Comp::FillObject> prevFillById;
        for (const Comp::FillPage& fp : t.fillPages)
            for (const Comp::FillObject& fo : fp.objects) prevFillById[fo.shapeId] = fo;
        scratchFans_.clear();
        t.fillPages = Comp::BuildDocumentFills(doc, scratchFans_, cam.zoom,
                                               placements, includeLoose,
                                               &t.dirty.dirtyIds, &prevFillById);
        std::unordered_set<uint64_t> skipIds;
        for (const Comp::FillPage& fp : t.fillPages)
            for (const Comp::FillObject& fo : fp.objects) skipIds.insert(fo.shapeId);
        t.segs = Renderer::Tessellator::BuildDocumentSegmented(
            doc, scratchMesh_, cam.zoom, placements, includeLoose, &cache_,
            /*cull=*/nullptr, /*outCover=*/&scratchCover_, /*outDecor=*/nullptr,
            /*skipShapeIds=*/&skipIds);
        metrics_.tessMs += Ms(tTess0, Now());
        // Cache stats (built vs served-from-cache vs culled) → the HUD, so we can see
        // whether the per-frame cost is (re)tessellation or the O(N) walk / upload.
        metrics_.shapesBuilt  += cache_.builtShapes;
        metrics_.shapesCached += cache_.cachedShapes;
        metrics_.shapesCulled += cache_.culledShapes;
        metrics_.shapesDrawn  += cache_.drawnShapes;
        // Opacity is NOT baked: an object with opacity<1 is rendered at FULL opacity
        // into the isolation layer, then composited with its opacity (Lot 4a-2).

        auto tUp0 = Now();
        const VkDeviceSize bytes = scratchMesh_.vertices.size() * sizeof(Renderer::Vertex);
        t.vertexCount = (uint32_t)scratchMesh_.vertices.size();
        if (bytes > 0) {
            EnsureTargetVbo(t, bytes);
            if (t.vboMapped) {
                std::memcpy(t.vboMapped, scratchMesh_.vertices.data(), (size_t)bytes);
                allocator_.Flush(t.vboAlloc, 0, bytes);
            }
        }
        const VkDeviceSize cbytes = scratchCover_.vertices.size() * sizeof(Renderer::Vertex);
        t.maskVertexCount = (uint32_t)scratchCover_.vertices.size();
        if (cbytes > 0) {
            EnsureTargetMaskVbo(t, cbytes);
            if (t.maskMapped) {
                std::memcpy(t.maskMapped, scratchCover_.vertices.data(), (size_t)cbytes);
                allocator_.Flush(t.maskAlloc, 0, cbytes);
            }
        }
        // Stencil-then-cover fill stream (Lot 13-4a/b + 13-1b-3): each pure-fill object
        // owns a PERSISTENT pool slice = its winding fans + its 6-vert cover quad
        // (contiguous). `fromScratch` (set by the incremental BuildDocumentFills) tells
        // us whether this object was (re)flattened THIS build:
        //   • fromScratch → fanFirst/coverFirst index the fresh scratch stream; WRITE
        //     its slice into the pool and rewrite the offsets pool-relative.
        //   • reused      → offsets are ALREADY pool-relative (copied from last build);
        //     just TOUCH the slot (no re-upload, no re-flatten). This is the tess win:
        //     an unchanged object is neither flattened nor uploaded.
        // Objects gone this build aren't fed → freed in EndFrame.
        if (!t.fillPool.Valid())
            t.fillPool.Init(&allocator_, (uint32_t)sizeof(Comp::FanVertex),
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        t.fillPool.BeginFrame();
        for (Comp::FillPage& fp : t.fillPages) {
            for (Comp::FillObject& fo : fp.objects) {
                if (fo.fromScratch) {
                    const uint32_t sliceCount = fo.fanCount + 6u;
                    uint32_t poolFirst = t.fillPool.Write(fo.shapeId,
                        scratchFans_.data() + fo.fanFirst, sliceCount);
                    fo.fanFirst   = poolFirst;              // fans then the quad
                    fo.coverFirst = poolFirst + fo.fanCount;
                } else {
                    // Reused: keep its pool-relative offsets; just mark the slot alive.
                    const Comp::ShapePool::Slot* sl = t.fillPool.Touch(fo.shapeId);
                    if (!sl) {
                        // Slot vanished (evicted / never existed) — flatten fallback is
                        // impossible here (not in scratch), so drop its geometry safely.
                        fo.fanCount = 0;
                    }
                }
            }
        }
        t.fillPool.EndFrame();
        t.fillPool.FlushWrites();
        metrics_.uploadMs += Ms(tUp0, Now());
        t.buildSig = sig;
        t.hasGeom  = true;
    }
    metrics_.triangles += (int)(t.vertexCount / 3);

    SubmitSlot& slot = AcquireSlot();
    auto tWait0 = Now();
    vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, UINT64_MAX);   // last frame's use
    metrics_.gpuWaitMs += Ms(tWait0, Now());   // CPU blocked on the GPU (GPU-bound signal)
    // Read this slot's PREVIOUS GPU render time now that its fence is signalled (Lot
    // 13-0). No stall — the work completed frames ago.
    if (slot.tsPool && slot.tsValid) {
        uint64_t ts[2] = { 0, 0 };
        if (vkGetQueryPoolResults(device_, slot.tsPool, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && ts[1] > ts[0])
            metrics_.gpuMs += (float)((ts[1] - ts[0]) * (double)timestampPeriodNs_ * 1e-6);
    }
    vkResetFences(device_, 1, &slot.fence);
    auto tRec0 = Now();   // command-buffer recording starts here

    VkCommandBuffer cmd = slot.cmd;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    if (slot.tsPool) {   // GPU timing: reset + start timestamp (Lot 13-0)
        vkCmdResetQueryPool(cmd, slot.tsPool, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.tsPool, 0);
    }

    // UNDEFINED → COLOR_ATTACHMENT_OPTIMAL (colour) + → DEPTH_STENCIL_ATTACHMENT (stencil)
    VkImageMemoryBarrier toAttach[2]{};
    toAttach[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toAttach[0].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toAttach[0].newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAttach[0].srcAccessMask       = 0;
    toAttach[0].dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toAttach[0].image               = t.image;
    toAttach[0].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toAttach[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttach[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttach[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toAttach[1].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toAttach[1].newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toAttach[1].srcAccessMask       = 0;
    toAttach[1].dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toAttach[1].image               = t.stencil;
    toAttach[1].subresourceRange    = { StencilAspect(stencilFormat_), 0, 1, 0, 1 };
    toAttach[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttach[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, nullptr, 0, nullptr, 2, toAttach);

    // Canvas pass: (re)opened by beginCanvas(). loadOp = CLEAR on the first open,
    // LOAD on a re-open after an isolation pass (preserving what's drawn so far).
    bool canvasBegun = false;
    const VkViewport vpFull{ 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    auto beginCanvas = [&]() {
        VkRenderingAttachmentInfo c{};
        c.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        c.imageView   = t.view;
        c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        c.loadOp      = canvasBegun ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        c.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        c.clearValue.color = { { clearColor.x, clearColor.y, clearColor.z, clearColor.w } };
        VkRenderingAttachmentInfo s{};
        s.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        s.imageView   = t.stencilView;
        s.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        s.loadOp      = canvasBegun ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        s.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        s.clearValue.depthStencil.stencil = 0;
        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea           = { { 0, 0 }, { w, h } };
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &c;
        ri.pStencilAttachment   = &s;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdSetViewport(cmd, 0, 1, &vpFull);
        canvasBegun = true;
    };

    // Hierarchical compositing (Lot 11-4e): (re)open the render pass for isolation
    // LEVEL `depth` (0 = the canvas; ≥1 = isoLevels[depth-1]). `load` keeps the
    // existing contents (re-open after a deeper isolation); else clears (first open
    // of a fresh level over its page region). The level's images must already be in
    // COLOR/DS layout (the caller transitions them).
    auto beginLevel = [&](int depth, bool load, const VkRect2D& area) {
        if (depth == 0) { beginCanvas(); return; }
        ViewTarget::IsoLevel& L = t.isoLevels[(size_t)depth - 1];
        VkRenderingAttachmentInfo c{};
        c.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        c.imageView = L.colorView; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        c.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        c.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };   // transparent isolation
        VkRenderingAttachmentInfo s{};
        s.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        s.imageView = L.stencilView; s.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        s.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        s.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        s.clearValue.depthStencil.stencil = 0;
        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = area; ri.layerCount = 1; ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &c; ri.pStencilAttachment = &s;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdSetViewport(cmd, 0, 1, &vpFull);
    };

    if (t.hasGeom && t.vertexCount > 0 && t.vbo != VK_NULL_HANDLE && shapePipeline_) {
        // Project a doc-rect to PHYSICAL px like shape.vert (px = (doc·unitScale −
        // pan)·zoom·ssaa), clamped to the target and optionally to a page rect.
        const float zN = cam.zoom * (float)ssaa;
        auto rectScissor = [&](Renderer::Vec2 mn, Renderer::Vec2 mx,
                               const VkRect2D* clamp) -> VkRect2D {
            float x0 = (mn.x * cam.unitScale - cam.panX) * zN;
            float y0 = (mn.y * cam.unitScale - cam.panY) * zN;
            float x1 = (mx.x * cam.unitScale - cam.panX) * zN;
            float y1 = (mx.y * cam.unitScale - cam.panY) * zN;
            if (x1 < x0) std::swap(x0, x1);
            if (y1 < y0) std::swap(y0, y1);
            int ix0 = std::max(0, (int)std::floor(x0));
            int iy0 = std::max(0, (int)std::floor(y0));
            int ix1 = std::min((int)w, (int)std::ceil(x1));
            int iy1 = std::min((int)h, (int)std::ceil(y1));
            if (clamp) {
                ix0 = std::max(ix0, (int)clamp->offset.x);
                iy0 = std::max(iy0, (int)clamp->offset.y);
                ix1 = std::min(ix1, (int)(clamp->offset.x + (int)clamp->extent.width));
                iy1 = std::min(iy1, (int)(clamp->offset.y + (int)clamp->extent.height));
            }
            if (ix1 <= ix0 || iy1 <= iy0) return VkRect2D{ { 0, 0 }, { 0, 0 } };
            return VkRect2D{ { ix0, iy0 },
                             { (uint32_t)(ix1 - ix0), (uint32_t)(iy1 - iy0) } };
        };
        auto pageScissor = [&](const Renderer::Tessellator::PageSeg& seg) -> VkRect2D {
            if (seg.fullScissor) return VkRect2D{ { 0, 0 }, { w, h } };
            return rectScissor(seg.min,
                               Renderer::Vec2{ seg.min.x + seg.size.x, seg.min.y + seg.size.y },
                               nullptr);
        };

        ShapePush cam32{};
        cam32.pan[0] = cam.panX; cam32.pan[1] = cam.panY;
        cam32.target[0] = (float)lw; cam32.target[1] = (float)lh;   // logical → SSAA-invariant NDC
        cam32.zoom = cam.zoom; cam32.unitScale = cam.unitScale;
        VkDeviceSize off = 0;

        auto bindBase = [&]() {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shapePipeline_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &off);
            vkCmdPushConstants(cmd, shapePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(ShapePush), &cam32);
        };

        const bool canPattern = stencilMaskPipeline_ && patternFillPipeline_ &&
                                t.maskVbo != VK_NULL_HANDLE;
        const bool canStroke  = stencilMaskPipeline_ && strokeFillPipeline_ &&
                                t.maskVbo != VK_NULL_HANDLE;
        // Stencil-then-cover routing (Lot 13-4a): map shapeId → its ear-clip-free fan+
        // cover. A PURE solid-fill object is drawn via fan (stencil, non-zero winding)
        // + cover quad (stencil != 0) instead of its baked base triangles — no
        // ear-clip. Only objects present here are routed; the rest keep the baked base.
        const VkBuffer fillBuf = t.fillPool.Buffer();
        const bool canFill = fillStencilPipeline_ && fillCoverPipeline_ &&
                             fillBuf != VK_NULL_HANDLE;
        std::unordered_map<uint64_t, const Comp::FillObject*> fillById;
        if (canFill) {
            for (const Comp::FillPage& fp : t.fillPages)
                for (const Comp::FillObject& fo : fp.objects)
                    fillById[fo.shapeId] = &fo;
        }

        // Draw one routed object's fill via stencil-then-cover into the active pass.
        // Fan → stencil (non-zero winding, no colour); cover bbox quad → colour where
        // stencil != 0, resetting it to 0 (so the next object's stencil starts clean).
        FillCoverPush fillPush{};
        fillPush.pan[0] = cam.panX; fillPush.pan[1] = cam.panY;
        fillPush.target[0] = (float)lw; fillPush.target[1] = (float)lh;
        fillPush.zoom = cam.zoom; fillPush.unitScale = cam.unitScale;
        auto drawFillCover = [&](const Comp::FillObject& fo, const VkRect2D& pageSc) {
            if (fo.fanCount == 0) return;
            // Scissor = the object's OWN world bbox, clamped to its page. Clearing /
            // filling only this rect (not the whole page) keeps the cost local and
            // never disturbs a neighbour's in-progress stencil.
            VkRect2D sc = rectScissor(Renderer::Vec2{ fo.bbMinX, fo.bbMinY },
                                      Renderer::Vec2{ fo.bbMaxX, fo.bbMaxY }, &pageSc);
            if (sc.extent.width == 0 || sc.extent.height == 0) return;
            vkCmdBindVertexBuffers(cmd, 0, 1, &fillBuf, &off);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            // 0) CLEAR the stencil to 0 over this object's bbox first. The pattern/
            // stroke coverage pipelines leave non-zero stencil refs behind (their cover
            // tests EQUAL but never resets), and the winding fan below only ADDS to the
            // stencil — so a residue from a lower object would make the cover quad
            // (tested != 0) paint this object's colour over the lower object's whole
            // bbox. Clearing the bbox to 0 gives the fan a clean base. (Same mechanism
            // the page backdrop uses to reset its region.)
            {
                VkClearAttachment ca{};
                ca.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
                ca.clearValue.depthStencil.stencil = 0;
                VkClearRect cr{};
                cr.rect = sc; cr.baseArrayLayer = 0; cr.layerCount = 1;
                vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
            }
            // 1) fan → stencil (winding), no colour.
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fillStencilPipeline_);
            vkCmdPushConstants(cmd, fillStencilPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(ShapePush), &cam32);
            vkCmdDraw(cmd, fo.fanCount, 1, fo.fanFirst, 0);
            // 2) cover quad → colour where stencil != 0 (resets stencil to 0).
            fillPush.color[0] = fo.r; fillPush.color[1] = fo.g;
            fillPush.color[2] = fo.b; fillPush.color[3] = fo.a;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fillCoverPipeline_);
            vkCmdPushConstants(cmd, fillCoverPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(FillCoverPush), &fillPush);
            vkCmdDraw(cmd, 6, 1, fo.coverFirst, 0);
            metrics_.drawCalls += 2;
        };

        // Render one object's content (base → patterns → strokes) at FULL opacity,
        // into whatever pass is active. `baseSc` clips the base; patterns/strokes use
        // their own bbox scissor clamped to it.
        auto drawObjectContent = [&](const Renderer::Tessellator::ObjDraw& obj,
                                     const VkRect2D& baseSc) {
            // Routed pure solid fill → stencil-then-cover, skipping the baked base.
            const Comp::FillObject* routed = nullptr;
            if (canFill && obj.shapeId) {
                auto it = fillById.find(obj.shapeId);
                if (it != fillById.end()) routed = it->second;
            }
            if (routed) {
                drawFillCover(*routed, baseSc);
            } else if (obj.baseCount) {
                bindBase();
                vkCmdSetScissor(cmd, 0, 1, &baseSc);
                vkCmdDraw(cmd, obj.baseCount, 1, obj.baseFirst, 0);
                metrics_.drawCalls++;
            }
            if (canPattern && !obj.patterns.empty()) {
                vkCmdBindVertexBuffers(cmd, 0, 1, &t.maskVbo, &off);
                for (const auto& d : obj.patterns) {
                    if (d.coverVertexCount == 0) continue;
                    VkRect2D sc = rectScissor(d.bbMin, d.bbMax, &baseSc);
                    if (sc.extent.width == 0 || sc.extent.height == 0) continue;
                    vkCmdSetScissor(cmd, 0, 1, &sc);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stencilMaskPipeline_);
                    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
                    vkCmdPushConstants(cmd, coverPipeLayout_,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(ShapePush), &cam32);
                    vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);

                    PatternPush pp{};
                    pp.pan[0] = cam.panX; pp.pan[1] = cam.panY;
                    pp.target[0] = (float)lw; pp.target[1] = (float)lh;
                    pp.zoom = cam.zoom; pp.unitScale = cam.unitScale;
                    pp.pColor[0] = d.params.color.r; pp.pColor[1] = d.params.color.g;
                    pp.pColor[2] = d.params.color.b; pp.pColor[3] = d.params.color.a;
                    pp.pKind = (float)d.params.kind; pp.pSpacing = d.params.spacing;
                    pp.pSize = d.params.size; pp.pAngle = d.params.angle;
                    pp.pOffset[0] = d.params.offset.x; pp.pOffset[1] = d.params.offset.y;
                    { uint32_t sd = d.params.seed; std::memcpy(&pp.pSeed, &sd, sizeof(float)); }
                    pp.pDash = d.params.dash; pp.pDashGap = d.params.dashGap;
                    pp.pAltPhase = d.params.altPhase ? 1.0f : 0.0f;
                    pp.pCenter[0] = d.params.center.x; pp.pCenter[1] = d.params.center.y;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, patternFillPipeline_);
                    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
                    vkCmdPushConstants(cmd, coverPipeLayout_,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(PatternPush), &pp);
                    vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);
                    metrics_.drawCalls += 2;
                }
            }
            if (canStroke && !obj.strokes.empty()) {
                vkCmdBindVertexBuffers(cmd, 0, 1, &t.maskVbo, &off);
                for (const auto& d : obj.strokes) {
                    if (d.coverVertexCount == 0) continue;
                    VkRect2D sc = rectScissor(d.bbMin, d.bbMax, &baseSc);
                    if (sc.extent.width == 0 || sc.extent.height == 0) continue;
                    vkCmdSetScissor(cmd, 0, 1, &sc);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, stencilMaskPipeline_);
                    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
                    vkCmdPushConstants(cmd, coverPipeLayout_,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(ShapePush), &cam32);
                    vkCmdDraw(cmd, d.coverVertexCount, 1, d.coverFirstVertex, 0);

                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, strokeFillPipeline_);
                    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d.stencilRef);
                    vkCmdPushConstants(cmd, coverPipeLayout_,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(ShapePush), &cam32);
                    vkCmdDraw(cmd, 6, 1, d.quadFirstVertex, 0);
                    metrics_.drawCalls += 2;
                }
            }
        };

        // P4 Composite isolation (Lot 4a-2 / 4b / 7): an object with opacity<1, a
        // non-Normal blend mode, OR erase is rendered at FULL opacity into the
        // isolation layer (its own pass, page-clipped), then composited onto the
        // canvas. erase (Lot 7) takes priority over the blend mode and uses the
        // dst-out pipeline (subtract coverage); a blend mode copies the canvas page
        // region into a backdrop and runs the blend shader; Normal just composites
        // with opacity (correct self-overlap).
        // ── Hierarchical compositing (Lot 11-4e), Affinity/PS-style ─────────────
        // `isolateOnto`: render `fillIso` into isolation level `depth+1` at full
        // opacity (its own page-clipped pass), then composite that level onto the
        // TARGET level `depth` with `opacity`/`blend`. The active pass for the target
        // (already open) is paused and re-opened (LOAD) around this. A blend reads the
        // TARGET (everything already composited below, at this level) as its backdrop,
        // so blend works across the whole layer hierarchy — a group's member blends
        // with the members below it; the group blends with what's below it on the page.
        std::function<void(const std::function<void()>&, const VkRect2D&, float, uint8_t, int)>
        isolateOnto = [&](const std::function<void()>& fillIso, const VkRect2D& sc,
                          float compOpacity, uint8_t compBlend, int depth) {
            const bool erase = (compBlend == kBlendErase) && eraseCompPipeline_;
            const bool blend = !erase && compBlend != 0 && compBlend != kBlendErase && blendPipeline_;
            ViewTarget::IsoLevel& Iso = EnsureIsoLevel(t, depth + 1, blend);  // the isolation
            // Target images = the level we composite ONTO (level 0 = canvas).
            VkImage     dstColor   = depth == 0 ? t.image       : t.isoLevels[(size_t)depth - 1].color;
            VkImage     dstStencil = depth == 0 ? t.stencil     : t.isoLevels[(size_t)depth - 1].stencil;

            vkCmdEndRendering(cmd);   // pause the TARGET pass

            // Isolation level → COLOR/DS (UNDEFINED: cleared next).
            VkImageMemoryBarrier ib[2]{};
            ib[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ib[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ib[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            ib[0].image = Iso.color; ib[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            ib[0].srcQueueFamilyIndex = ib[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib[1] = ib[0];
            ib[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ib[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            ib[1].image = Iso.stencil;
            ib[1].subresourceRange = { StencilAspect(stencilFormat_), 0, 1, 0, 1 };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                                 0, nullptr, 0, nullptr, 2, ib);

            beginLevel(depth + 1, /*load=*/false, sc);   // fresh transparent isolation
            fillIso();
            vkCmdEndRendering(cmd);

            const float ndc0 = (float)sc.offset.x / (float)w * 2.0f - 1.0f;
            const float ndc1 = (float)sc.offset.y / (float)h * 2.0f - 1.0f;
            const float ndc2 = (float)((int)sc.offset.x + (int)sc.extent.width) / (float)w * 2.0f - 1.0f;
            const float ndc3 = (float)((int)sc.offset.y + (int)sc.extent.height) / (float)h * 2.0f - 1.0f;

            if (!blend) {
                // Normal/erase: iso → SHADER_READ; target colour/stencil → read|write
                // (preserved across the LOAD); composite onto the target.
                VkImageMemoryBarrier rb[3]{};
                rb[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                rb[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                rb[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                rb[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                rb[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                rb[0].image = Iso.color; rb[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                rb[0].srcQueueFamilyIndex = rb[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                rb[1] = rb[0];
                rb[1].oldLayout = rb[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                rb[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                rb[1].image = dstColor;
                rb[2] = rb[1];
                rb[2].oldLayout = rb[2].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                rb[2].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                rb[2].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                rb[2].image = dstStencil;
                rb[2].subresourceRange = { StencilAspect(stencilFormat_), 0, 1, 0, 1 };
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                    0, nullptr, 0, nullptr, 3, rb);

                beginLevel(depth, /*load=*/true, { { 0, 0 }, { w, h } });
                IsoPushC ipc{};
                ipc.ndc[0] = ndc0; ipc.ndc[1] = ndc1; ipc.ndc[2] = ndc2; ipc.ndc[3] = ndc3;
                ipc.opacity = compOpacity;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  erase ? eraseCompPipeline_ : isoCompPipeline_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, isoCompPipeLayout_,
                                        0, 1, &Iso.desc, 0, nullptr);
                vkCmdPushConstants(cmd, isoCompPipeLayout_,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(IsoPushC), &ipc);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                vkCmdDraw(cmd, 6, 1, 0, 0);
                metrics_.drawCalls++;
                return;
            }

            // Blend: the shader needs the iso (src) AND the TARGET below (backdrop).
            // Copy the target's region into the iso level's backdrop, then composite.
            VkImageMemoryBarrier cb[3]{};
            cb[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            cb[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            cb[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            cb[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            cb[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            cb[0].image = Iso.color; cb[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            cb[0].srcQueueFamilyIndex = cb[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            cb[1] = cb[0];   // target colour → TRANSFER_SRC (read by the copy)
            cb[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            cb[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            cb[1].image = dstColor;
            cb[2] = cb[0];   // iso backdrop → TRANSFER_DST
            cb[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            cb[2].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            cb[2].srcAccessMask = 0;
            cb[2].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            cb[2].image = Iso.backdrop;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 3, cb);

            VkImageCopy cp{};
            cp.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            cp.dstSubresource = cp.srcSubresource;
            cp.srcOffset = { (int)sc.offset.x, (int)sc.offset.y, 0 };
            cp.dstOffset = cp.srcOffset;
            cp.extent = { sc.extent.width, sc.extent.height, 1 };
            vkCmdCopyImage(cmd, dstColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           Iso.backdrop, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);

            VkImageMemoryBarrier rb2[3]{};
            rb2[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rb2[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rb2[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            rb2[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rb2[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            rb2[0].image = dstColor; rb2[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            rb2[0].srcQueueFamilyIndex = rb2[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rb2[1] = rb2[0];   // iso backdrop → SHADER_READ
            rb2[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            rb2[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            rb2[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            rb2[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            rb2[1].image = Iso.backdrop;
            rb2[2] = rb2[0];   // target stencil → read|write (preserve across LOAD)
            rb2[2].oldLayout = rb2[2].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            rb2[2].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            rb2[2].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            rb2[2].image = dstStencil;
            rb2[2].subresourceRange = { StencilAspect(stencilFormat_), 0, 1, 0, 1 };
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                0, nullptr, 0, nullptr, 3, rb2);

            beginLevel(depth, /*load=*/true, { { 0, 0 }, { w, h } });
            BlendPushC bpc{};
            bpc.ndc[0] = ndc0; bpc.ndc[1] = ndc1; bpc.ndc[2] = ndc2; bpc.ndc[3] = ndc3;
            bpc.opacity = compOpacity;
            bpc.mode = (int32_t)compBlend;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeLayout_,
                                    0, 1, &Iso.blendDesc, 0, nullptr);
            vkCmdPushConstants(cmd, blendPipeLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(BlendPushC), &bpc);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdDraw(cmd, 6, 1, 0, 0);
            metrics_.drawCalls++;
        };

        // Compose objects [lo, hi) onto isolation level `depth` (0 = canvas). Objects
        // whose group nesting is deeper than `depth` are gathered into runs and each
        // run is isolated one level deeper, recursively — so a group composites its
        // children (each with their own blend, against each other) then merges onto
        // its parent with the group's blend/opacity. Members are contiguous (see
        // Document::MakeGroupContiguous), so a run is a simple [a,b) range.
        std::function<void(const std::vector<Renderer::Tessellator::ObjDraw>&, size_t, size_t, int, const VkRect2D&)>
        composeRange = [&](const std::vector<Renderer::Tessellator::ObjDraw>& objs,
                           size_t lo, size_t hi, int depth, const VkRect2D& sc) {
            for (size_t i = lo; i < hi; ) {
                const auto& o = objs[i];
                // Clamp recursion depth (pathological / cyclic nesting in an old .acu
                // could otherwise blow the CPU stack AND exhaust iso levels).
                const bool grouped = (int)o.groups.size() > depth &&
                                     depth < Engine::kMaxIsoDepth && isoCompPipeline_;
                if (!grouped) {
                    // Plain object at this level: isolate for opacity/blend/erase, else draw.
                    const bool needsIso =
                        (o.opacity < 0.999f && isoCompPipeline_) ||
                        (o.blendMode != 0 && o.blendMode != kBlendErase && blendPipeline_) ||
                        (o.blendMode == kBlendErase && eraseCompPipeline_);
                    if (needsIso)
                        isolateOnto([&]{ drawObjectContent(o, sc); }, sc, o.opacity, o.blendMode, depth);
                    else
                        drawObjectContent(o, sc);
                    ++i;
                    continue;
                }
                // Run of consecutive objects sharing the group at THIS depth.
                const uint64_t gid = o.groups[(size_t)depth].id;
                const float    gOp = o.groups[(size_t)depth].opacity;
                const uint8_t  gBl = o.groups[(size_t)depth].blend;
                size_t j = i + 1;
                while (j < hi && (int)objs[j].groups.size() > depth &&
                       objs[j].groups[(size_t)depth].id == gid) ++j;
                // Isolate the group's children one level deeper (they blend against
                // each other), then composite the group onto this level with its params.
                isolateOnto([&]{ composeRange(objs, i, j, depth + 1, sc); }, sc, gOp, gBl, depth);
                i = j;
            }
        };

        beginCanvas();

        if (t.segs.empty()) {
            VkRect2D full{ { 0, 0 }, { w, h } };
            bindBase();
            vkCmdSetScissor(cmd, 0, 1, &full);
            vkCmdDraw(cmd, t.vertexCount, 1, 0, 0);
            metrics_.drawCalls++;
        }

        for (const auto& seg : t.segs) {
            VkRect2D pageSc = pageScissor(seg);
            if (pageSc.extent.width == 0 || pageSc.extent.height == 0) continue;  // page off-screen

            // The page background is a display SUBSTRATE, not a layer: an erase object
            // must cut the object stack down to the page (revealing the substrate),
            // never punch through it. So the substrate is drawn LAST, UNDER the stack
            // (DST-OVER) — and the page region starts transparent so erase holes leave
            // α<1 for the substrate to fill. Order per page: clear page → objects →
            // substrate. (Off-page keeps the canvas clear colour, untouched.)
            bool hasBackdrop = seg.backdropCount > 0;
            if (hasBackdrop) {
                VkClearAttachment ca{};
                ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                ca.colorAttachment = 0;
                ca.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
                VkClearRect cr{};
                cr.rect = pageSc; cr.baseArrayLayer = 0; cr.layerCount = 1;
                vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
            }

            // Compose the page's object stack with full hierarchical blend (Lot
            // 11-4e): each object/group blends with everything below it at its level;
            // a group isolates + composites its children recursively, then merges onto
            // the page with the group's blend/opacity.
            composeRange(seg.objects, 0, seg.objects.size(), /*depth=*/0, pageSc);

            // Substrate last, sliding UNDER the stack (DST-OVER): fills the page bg
            // beneath the objects and beneath any erase hole, never cut by an erase.
            if (hasBackdrop) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, backdropPipeline_);
                vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &off);
                vkCmdPushConstants(cmd, shapePipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(ShapePush), &cam32);
                vkCmdSetScissor(cmd, 0, 1, &pageSc);
                vkCmdDraw(cmd, seg.backdropCount, 1, seg.backdropFirst, 0);
                metrics_.drawCalls++;
            }
        }
        vkCmdEndRendering(cmd);

        // ── Picking id-pass (Lot 8) ────────────────────────────────────────────
        // Only when this view was asked to pick. A separate pass into the R32UI id
        // image: each object draws its base geometry with a 1-based id (painter
        // order, so the topmost object wins per pixel). The requested pixel is then
        // copied to the host readback buffer for next-frame resolution.
        if (t.pickRequested && pickPipeline_) {
            EnsurePickTarget(t);
            // id image UNDEFINED → COLOR_ATTACHMENT (cleared to 0 = background).
            VkImageMemoryBarrier pb{};
            pb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            pb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            pb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            pb.image = t.pickImg;
            pb.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            pb.srcQueueFamilyIndex = pb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &pb);

            VkRenderingAttachmentInfo pc{};
            pc.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            pc.imageView = t.pickView; pc.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            pc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; pc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            pc.clearValue.color.uint32[0] = 0u;   // 0 = nothing
            VkRenderingInfo pri{};
            pri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            pri.renderArea = { { 0, 0 }, { w, h } };
            pri.layerCount = 1; pri.colorAttachmentCount = 1; pri.pColorAttachments = &pc;
            vkCmdBeginRendering(cmd, &pri);
            vkCmdSetViewport(cmd, 0, 1, &vpFull);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pickPipeline_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &t.vbo, &off);

            t.pickIds.clear();
            PickPush ppc{};
            ppc.pan[0] = cam.panX; ppc.pan[1] = cam.panY;
            ppc.target[0] = (float)lw; ppc.target[1] = (float)lh;
            ppc.zoom = cam.zoom; ppc.unitScale = cam.unitScale;
            // Same painter order as the colour pass: per page, objects in document
            // order; the id is a 1-based running index into pickIds.
            for (const auto& seg : t.segs) {
                VkRect2D pageSc = pageScissor(seg);
                if (pageSc.extent.width == 0 || pageSc.extent.height == 0) continue;
                for (const auto& obj : seg.objects) {
                    if (obj.baseCount == 0) continue;   // no fillable area → not pickable here
                    t.pickIds.push_back(obj.shapeId);
                    ppc.objId = (uint32_t)t.pickIds.size();   // 1-based
                    vkCmdPushConstants(cmd, pickPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(PickPush), &ppc);
                    vkCmdSetScissor(cmd, 0, 1, &pageSc);
                    vkCmdDraw(cmd, obj.baseCount, 1, obj.baseFirst, 0);
                    metrics_.drawCalls++;
                }
            }
            vkCmdEndRendering(cmd);

            // id image COLOR_ATTACHMENT → TRANSFER_SRC, copy the requested pixel out.
            VkImageMemoryBarrier cb{};
            cb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            cb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            cb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            cb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            cb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            cb.image = t.pickImg;
            cb.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            cb.srcQueueFamilyIndex = cb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &cb);

            int sx = t.pickX * ssaa, sy = t.pickY * ssaa;   // logical → SSAA px
            sx = std::min(std::max(sx, 0), (int)w - 1);
            sy = std::min(std::max(sy, 0), (int)h - 1);
            VkBufferImageCopy cp{};
            cp.bufferOffset = 0;
            cp.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            cp.imageOffset = { sx, sy, 0 };
            cp.imageExtent = { 1, 1, 1 };
            vkCmdCopyImageToBuffer(cmd, t.pickImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   t.pickReadBuf, 1, &cp);
            t.pickIdsInFlight = t.pickIds;   // resolve against THIS frame's map
        }
    } else {
        beginCanvas();          // no geometry: just clear the canvas
        vkCmdEndRendering(cmd);
    }

    // COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (sampled by the composite)
    VkImageMemoryBarrier toRead{};
    toRead.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    toRead.image               = t.image;
    toRead.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toRead);

    if (slot.tsPool) {   // GPU timing: end timestamp (Lot 13-0)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.tsPool, 1);
        slot.tsValid = true;
    }
    vkEndCommandBuffer(cmd);
    metrics_.recordMs += Ms(tRec0, Now());   // CPU time building the command buffer

    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &slot.sem;
    Check(vkQueueSubmit(queue_, 1, &si, slot.fence), "vkQueueSubmit (offscreen)");

    // Picking: this submit carries the id-pass + the pixel copy. Remember its fence
    // so Pick() (next frame) waits on it before reading the host buffer. One sample
    // per request — the request is consumed here.
    if (t.pickRequested && pickPipeline_) {
        t.pickPending     = true;
        t.pickSubmitFence = slot.fence;
        t.pickInFlightX   = t.pickX;
        t.pickInFlightY   = t.pickY;
        t.pickRequested   = false;
    }

    frameWaits_.push_back(slot.sem);
    ++viewsThisFrame_;
    // The editor reports the destination rect via PlaceView; nothing to blit here.
    return (ImTextureID)1;   // non-null = success (the editor doesn't draw it)
}

} // namespace Comp

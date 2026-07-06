#pragma once

#include "Renderer/Document/Document.h"
#include "Renderer/Tessellation/Tessellator.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  IViewRenderer — the per-view renderer contract.
//
//  Both the legacy `CanvasRenderer` (VkRenderPass + manual allocation) and the
//  new `Comp::Engine` (modern Vulkan: dynamic rendering + VMA) implement this so
//  the Application can switch between them at runtime. Exactly ONE renderer is
//  alive at a time (clean swap): the inactive engine is not instantiated, so it
//  costs nothing and cannot influence the active one.
//
//  Both render the vector document EXCLUSIVELY through Vulkan into an offscreen
//  texture (one per Viewport zone); the Application blits each texture with
//  ImGui::Image. ImGui never draws the vector artwork — only the editor chrome.
//
//  Lifecycle (shares the Application's Vulkan device/queue/pools):
//    Initialize(...) once  →  per frame: BeginFrame(); RenderView(...) per zone;
//    EndFrame();  →  Shutdown() at teardown.
//  RenderView() must run OUTSIDE the main swapchain pass (it begins its own
//  offscreen pass), i.e. before ImGui::Render()/the main pass.
// ─────────────────────────────────────────────────────────────────────────────

// Per-view camera, mirrors the Viewport's D2S mapping:
//   screen_px = (doc * unitScale - pan) * zoom
// `unitScale` is the document-unit→ruler-pixel factor (the active ruler unit's
// pxPer). `pan` is in the SAME pre-scaled space zoom operates on, matching the
// ImGui-side camera so rulers/guides line up with the Vulkan output.
struct Camera {
    float panX = 0.0f, panY = 0.0f;
    float zoom = 1.0f;
    float unitScale = 1.0f;
};

class IViewRenderer {
public:
    virtual ~IViewRenderer() = default;

    // Which concrete engine an instance is — used by the global switch.
    enum class Kind { Legacy, Compositor };

    // Real-time metrics (for the viewport overlay). Shared type so the numbers
    // are directly comparable across both engines.
    struct Metrics {
        int   views        = 0;    // viewports rendered this frame
        int   triangles    = 0;    // triangles submitted this frame (all views)
        int   drawCalls    = 0;    // vkCmdDraw calls this frame
        int   shapesDrawn  = 0;    // shapes that produced geometry
        int   shapesCached = 0;    // shapes served from the tessellation cache
        int   shapesBuilt  = 0;    // shapes (re)tessellated this frame
        int   shapesCulled = 0;    // shapes skipped by frustum culling
        float tessMs       = 0.0f; // CPU time spent tessellating this frame
        float gpuWaitMs    = 0.0f; // CPU time blocked on the GPU this frame
        // Finer CPU breakdown (Lot 13-0: find the real bottleneck before the GPU rework).
        float sigMs        = 0.0f; // CPU time hashing the content signature (O(N) walk)
        float uploadMs     = 0.0f; // CPU time copying the vertex buffers to the GPU
        float recordMs     = 0.0f; // CPU time recording the render command buffer
        float gpuMs        = 0.0f; // GPU time for the offscreen render (timestamp query)
        // Per-shape dirty diff (Lot 13-1a): how many shapes actually CHANGED between
        // the last two builds of a view (added + modified + removed). On a static
        // scene this is 0 even when a full rebuild fires; a large number vs. a small
        // one tells whether the rebuild cost is inherent or a full-VBO over-rebuild.
        int   shapesDirty  = 0;
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // `instance` is needed by engines that use VMA (the Compositor); the legacy
    // CanvasRenderer ignores it.
    virtual void Initialize(VkInstance instance,
                            VkDevice device, VkPhysicalDevice physicalDevice,
                            VkQueue queue, uint32_t queueFamily,
                            VkCommandPool commandPool, VkSampler sampler,
                            const std::string& shaderDir) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;

    // ── Per-frame ─────────────────────────────────────────────────────────────
    virtual void BeginFrame() = 0;

    // The offscreen-done semaphores the main swapchain pass must WAIT on this
    // frame (stage FRAGMENT_SHADER), so ImGui samples a finished texture without
    // a CPU stall. Cleared at the next BeginFrame.
    virtual const std::vector<VkSemaphore>& FrameWaitSemaphores() const = 0;

    // Render `doc` for one view into an offscreen target of `widthPx`×`heightPx`
    // LOGICAL pixels, keyed by `key` (a stable per-leaf identity — the Viewport
    // passes its EditorState ptr). Returns the ImTextureID to display, or 0 if
    // the size is degenerate (the caller then draws its own fallback).
    virtual ImTextureID RenderView(const void* key, const Document& doc,
                                   const Camera& cam, int widthPx, int heightPx,
                                   ImVec4 clearColor,
                                   const std::vector<Tessellator::PagePlacement>* placements = nullptr,
                                   bool includeLoose = true,
                                   bool focused = true) = 0;

    virtual void EndFrame() = 0;

    virtual const Metrics& GetMetrics() const = 0;

    // ── Canvas presentation ───────────────────────────────────────────────────
    // When true, the engine composites its rendered views onto the swapchain
    // ITSELF, via its own Vulkan pass (CompositeMainPass) — ImGui must NOT blit
    // the canvas. The editor then only reports each view's destination rect via
    // PlaceView and draws nothing for the canvas. When false (legacy), RenderView
    // returns an ImTextureID the editor blits with ImGui.
    virtual bool PresentsViaSwapchain() const { return false; }

    // Report a rendered view's destination rectangle in NORMALISED DEVICE COORDS
    // (xmin, ymin, xmax, ymax; y down, matching ImGui's projection) for the main
    // window. Only meaningful when PresentsViaSwapchain() is true. No-op default.
    virtual void PlaceView(const void* key, ImVec4 ndcRect) { (void)key; (void)ndcRect; }

    // Record the canvas composite into the main swapchain render pass (between
    // vkCmdBeginRenderPass and ImGui's draw data, so the canvas sits under the
    // chrome). Pure Vulkan — never ImGui. No-op default (legacy doesn't use it).
    virtual void CompositeMainPass(VkCommandBuffer cmd, uint32_t fbW, uint32_t fbH,
                                   VkRenderPass renderPass) {
        (void)cmd; (void)fbW; (void)fbH; (void)renderPass;
    }

    // ── Editor overlays (Lot 12: full-GPU canvas overlays) ──────────────────────
    // The editor draws its canvas overlays (selection outlines, handles, grid,
    // page outlines, 2D cursor, …) by tessellating them into COLOURED TRIANGLES in
    // NORMALISED DEVICE COORDS (x,y in [-1,1], y down, matching the main-window
    // projection) and submitting the list each frame; the engine renders it in
    // CompositeMainPass, OVER the canvas and UNDER ImGui. State (selection/tools)
    // stays in the editor. Engines that present via swapchain own this; the legacy
    // engine ignores it (the editor keeps its ImGui overlay path there).
    struct OverlayVertex {
        float    x, y;       // NDC position (y down)
        uint32_t rgba;       // 0xAABBGGRR straight-alpha colour (ImGui packing)
    };
    // Submit this frame's overlay triangles (indexed). Cleared/consumed by the
    // engine at composite time. No-op default.
    virtual void SubmitOverlay(const std::vector<OverlayVertex>& /*verts*/,
                               const std::vector<uint32_t>& /*indices*/) {}

    // ── Picking (GPU id-pass, async readback) ──────────────────────────────────
    // Resolve the object under (`px`,`py`) — LOGICAL pixels of the view `key`,
    // top-left origin, same space as RenderView's widthPx/heightPx — to a
    // `Shape::id` (0 = nothing / not yet available). Engines that render an id
    // buffer (the Compositor's PickingPass) read back the pixel id from the LAST
    // rendered frame (async, no stall) and map it to the shape id; the result is
    // for the frame the cursor was over on the previous frame, which is correct
    // for click selection. Default 0 → the caller falls back to its CPU hit-test
    // (the legacy engine has no id-pass). Requesting a pick also tells the engine
    // which pixel to read next frame.
    virtual uint64_t Pick(const void* key, int px, int py) {
        (void)key; (void)px; (void)py; return 0;
    }

    // ── One-shot offscreen renders (independent of the per-view frame loop) ────
    // Render `doc` with `cam` into a one-off `w`×`h` image and read it back as
    // RGBA8 (row-major, top-left origin). Synchronous; used for .acu thumbnails.
    virtual bool RenderToRGBA(const Document& doc, const Camera& cam, int w, int h,
                              ImVec4 clearColor, std::vector<unsigned char>& outRGBA) = 0;

    // Render a set of `shapes` (geometry in local doc-units) into a cached
    // offscreen texture, auto-framed to the content. Rebuilt only when
    // `contentHash` changes for `key` (Symbol Viewer thumbnails / placement
    // ghost). Returns the ImTextureID (0 on failure). See CanvasRenderer for the
    // exactFit / frameMin / frameMax semantics.
    virtual ImTextureID RenderGlyphCached(uint64_t key, uint64_t contentHash,
                                          const std::vector<Shape>& shapes,
                                          int widthPx, int heightPx, float padFrac,
                                          ImVec4 clearColor, bool exactFit = false,
                                          const Vec2* frameMin = nullptr,
                                          const Vec2* frameMax = nullptr) = 0;
};

} // namespace Renderer

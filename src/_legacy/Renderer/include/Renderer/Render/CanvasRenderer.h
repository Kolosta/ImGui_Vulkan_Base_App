#pragma once

#include "Renderer/IViewRenderer.h"
#include "Renderer/Document/Document.h"
#include "Renderer/Render/RenderTarget.h"
#include "Renderer/Tessellation/Tessellator.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  CanvasRenderer — the Vulkan-only vector renderer.
//
//  The vector document is rendered EXCLUSIVELY through Vulkan (no ImGui draw
//  lists): each Viewport zone gets an offscreen RenderTarget; the renderer
//  tessellates the document on the CPU, uploads triangles, and draws them into
//  that target with its own pipeline + render pass. The Application then blits
//  each target with ImGui::Image — so ImGui only ever sees a finished texture.
//
//  Lifecycle (shares the Application's Vulkan device/queue/pools):
//    Initialize(...) once  →  per frame: BeginFrame(); RenderView(...) per zone;
//    EndFrame();  →  Shutdown() at teardown.
//
//  RenderView() must run OUTSIDE the main swapchain render pass (it begins its
//  own render pass), i.e. before ImGui::Render()/the main pass — see
//  Application::Update/Render integration.
// ─────────────────────────────────────────────────────────────────────────────

// `Camera` and `Metrics` are defined in IViewRenderer.h (shared contract).

class CanvasRenderer : public IViewRenderer {
public:
    void Initialize(VkInstance instance,
                    VkDevice device, VkPhysicalDevice physicalDevice,
                    VkQueue queue, uint32_t queueFamily,
                    VkCommandPool commandPool, VkSampler sampler,
                    const std::string& shaderDir) override;
    void Shutdown() override;
    bool IsInitialized() const override { return initialized_; }

    // Begin a render frame: advances the frame counter (drives target eviction).
    void BeginFrame() override;

    // The offscreen-done semaphores the main swapchain pass must WAIT on this frame
    // (one per view re-submitted this frame), so ImGui samples a finished texture
    // without a CPU stall. The Application appends these to its main submit's
    // pWaitSemaphores (stage FRAGMENT_SHADER). Cleared at the next BeginFrame.
    const std::vector<VkSemaphore>& FrameWaitSemaphores() const override { return framePendingWaits_; }

    // SSAA (supersampling) factor: the offscreen target is rendered this many
    // times larger on each axis, then downscaled by the linear sampler at blit
    // time. 1 = off, 2 = 4× pixels (default). This is the anti-aliasing: the
    // alpha blend happens at high resolution, so transparency stays exact and
    // curves/strokes are smooth and zoom-stable. Clamped to [1, 4].
    void SetSSAAFactor(int n) { ssaaFactor_ = n < 1 ? 1 : (n > 4 ? 4 : n); }
    int  GetSSAAFactor() const { return ssaaFactor_; }

    // Render `doc` for one view into an offscreen target of `widthPx`×`heightPx`
    // LOGICAL pixels (the renderer multiplies by the SSAA factor internally),
    // keyed by `key` (a stable per-leaf identity — we use the EditorState ptr).
    // Returns the ImTextureID to display, or 0 if the size is degenerate.
    // The clear colour is the canvas backdrop behind the artboards.
    // `placements` (optional, parallel to doc.artboards) relocates/hides each
    // page for THIS view only (per-viewport page layout, Lot 3). null = each
    // page at its own ab.pos, all visible.
    // `focused` = this view is the one the user is interacting with. A non-focused
    // view throttles its rebuild CADENCE (detail/cull-margin rebuilds gated to every
    // few frames; structural changes still pass immediately) — same detail, just
    // refreshed less often — so many open viewports don't all re-tessellate at once.
    ImTextureID RenderView(const void* key, const Document& doc,
                           const Camera& cam, int widthPx, int heightPx,
                           ImVec4 clearColor,
                           const std::vector<Tessellator::PagePlacement>* placements = nullptr,
                           bool includeLoose = true,   // draw page-less objects?
                           bool focused = true) override;

    // End the frame: destroy targets not used this frame (closed/joined zones).
    void EndFrame() override;

    // ── Real-time metrics (for the viewport overlay) ──────────────────────────
    // `Metrics` is the shared IViewRenderer::Metrics (comparable across engines).
    const Metrics& GetMetrics() const override { return metrics_; }
    // NOTE: per-view CPU frustum culling was removed from RenderView (it churned
    // the persistent per-view buffer on every pan/zoom). Off-screen geometry is
    // built once and discarded by the GPU scissor/clip (un-rasterised ≈ free), so
    // shapesCulled now stays 0. The flag is kept for the one-shot build paths.
    void SetCullingEnabled(bool on) { cullEnabled_ = on; }
    bool IsCullingEnabled() const { return cullEnabled_; }

    // Render `doc` with `cam` into a one-off `w`×`h` image and read it back as
    // RGBA8 (row-major, top-left origin). Synchronous; used for .acu thumbnails.
    // Returns false on failure. Independent of the per-view targets / frame loop.
    bool RenderToRGBA(const Document& doc, const Camera& cam, int w, int h,
                      ImVec4 clearColor, std::vector<unsigned char>& outRGBA) override;

    // Render a set of `shapes` (a symbol + optional companions, geometry in local
    // doc-units) into a cached offscreen texture of `widthPx`×`heightPx` LOGICAL
    // pixels, auto-framed to the content with `padFrac` margin, SSAA-smoothed like
    // a viewport. Keyed by `key` (caller-stable: e.g. hash of code+scale+size). The
    // texture is REBUILT only when `contentHash` changes for that key — so a grid
    // of thumbnails costs one render each, not per frame. Returns the ImTextureID
    // (0 on failure). `clearColor` is the card backdrop (use white for map paper).
    // `exactFit` (live previews): map the content bbox onto the WHOLE texture with
    // independent X/Y scale and NO centring/aspect-preservation, so the texture spans
    // exactly [bbMin,bbMax] in world units → a caller blitting at d2s(bbMin)..d2s(bbMax)
    // is pixel-aligned with the committed object (no offset/stretch). Default false =
    // fit + centre (thumbnails/ghosts), preserving aspect with `padFrac` margin.
    // `frameMin`/`frameMax` (optional, used with exactFit): frame the texture to
    // THESE world bounds instead of the shapes' own WorldBounds. The caller passes
    // the SAME (stroke-padded) bounds it blits to, so the rendered content fills the
    // texture exactly edge-to-edge — no offset, and the stroke that spills past the
    // construction line is no longer clipped by a tighter auto-frame.
    ImTextureID RenderGlyphCached(uint64_t key, uint64_t contentHash,
                                  const std::vector<Shape>& shapes,
                                  int widthPx, int heightPx, float padFrac,
                                  ImVec4 clearColor, bool exactFit = false,
                                  const Vec2* frameMin = nullptr,
                                  const Vec2* frameMax = nullptr) override;
    // Drop glyph textures not requested since `keepFrames` frames ago (call once
    // per frame from the owner so closed panels release VRAM).
    void EvictGlyphTextures();

private:
    struct PushConstants {
        // Camera block (bytes 0..31) — byte-identical to shape.vert/pattern.vert.
        float pan[2];
        float target[2];
        float zoom;
        float unitScale;
        float pad[2];
        // Procedural fill-pattern block (bytes 32..95), read by pattern_fill.frag.
        // Set per surface-layer; ignored by the base/mask/instanced shaders (which
        // declare only the camera subset). 96 bytes total (< 128 guaranteed limit).
        float pColor[4];   // 32  straight RGBA
        float pKind;       // 48  1 Dots 2 Lines 3 Triangles 4 RandomDots 5 Grid 6 CrossHatch
        float pSpacing;    // 52  doc-units (×avgScale)
        float pSize;       // 56  doc-units (×avgScale)
        float pAngle;      // 60  radians (fl.angleDeg·π/180 + shape.rotate)
        float pOffset[2];  // 64  doc-units (×avgScale), un-rotated
        float pSeed;       // 72  uintBitsToFloat(seed)
        float pDash;       // 76  doc-units, 0 = solid
        float pDashGap;    // 80
        float pAltPhase;   // 84  0/1
        float pCenter[2];  // 88  surface bbox centre, world doc-units (lattice origin)
    };

    // Record the procedural draw list (base + procedural fills + instanced decor)
    // into an already-open render pass. Shared by RenderView + RenderGlyphCached.
    void RecordDrawList(VkCommandBuffer cmd, const RenderTarget& t,
                        const std::vector<Tessellator::PageSeg>& segs,
                        const Camera& cam, uint32_t w, uint32_t h, int n);

    // Pipeline / render pass setup.
    void CreateRenderPass();
    void CreatePipeline(const std::string& shaderDir);
    void CreatePatternPipelines(const std::string& shaderDir);   // Phase 2
    void CreateBaseMeshes();                                      // unit disc/tri/quad
    VkShaderModule LoadShader(const std::string& path);
    VkFormat ChooseStencilFormat() const;

    // Target lifecycle.
    RenderTarget& AcquireTarget(const void* key, uint32_t w, uint32_t h);
    void CreateTargetImages(RenderTarget& t, uint32_t w, uint32_t h);
    void DestroyTarget(RenderTarget& t);

    // Dynamic vertex buffer (grown as needed, host-visible for simplicity).
    // The shared `vbo_` serves the one-shot paths (RenderToRGBA / glyph cache);
    // each per-view RenderTarget owns its OWN persistent vbo (see below).
    void EnsureVertexCapacity(VkDeviceSize bytes);
    void DestroyVertexBuffer();
    // Per-target persistent vertex buffer (Phase 1): grown to hold a view's whole
    // built mesh, refilled only when the view's content signature changes.
    void EnsureTargetVertexCapacity(RenderTarget& t, VkDeviceSize bytes);
    void DestroyTargetVertexBuffer(RenderTarget& t);
    // Per-target decor-instance + mask buffers (Phase 3/4), rebuilt with the vbo.
    void EnsureTargetDecorCapacity(RenderTarget& t, VkDeviceSize bytes);
    void EnsureTargetMaskCapacity(RenderTarget& t, VkDeviceSize bytes);
    void DestroyTargetPatternBuffers(RenderTarget& t);
    // Generic host-visible buffer (used by the per-target stream allocators).
    void MakeHostBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& cap,
                        VkDeviceSize bytes, VkBufferUsageFlags usage);
    // FNV-1a content signature of what RenderView would draw for `doc` (shape ids +
    // their cache hashes + page placements + includeLoose + the detail bucket + the
    // quantised visible cull rect). The bucket + cull rect are passed in (not read
    // from mutable globals) so the signature is self-contained and stable. Cheap: no
    // tessellation, no WorldBounds. Equal signature ⇒ reuse the buffer (incl. a small
    // pan within the cull margin). Crossing a margin step or a detail bucket rebuilds.
    uint64_t BuildSignature(const Document& doc,
                            const std::vector<Tessellator::PagePlacement>* placements,
                            bool includeLoose, int detailBucket,
                            const Tessellator::CullRect& cullQuantised) const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

private:
    bool initialized_ = false;

    VkDevice         device_         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    VkSampler        sampler_        = VK_NULL_HANDLE;   // owned by Application

    VkFormat         colorFormat_    = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat         stencilFormat_  = VK_FORMAT_UNDEFINED;   // chosen at Initialize
    VkRenderPass     renderPass_     = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;
    // Pattern pipelines: write a surface's cut polygon to the stencil, then draw the
    // pattern stencil-tested EQUAL to that surface's reference. Fills use the
    // PROCEDURAL pipeline (a cover quad + pattern_fill.frag); the INSTANCED pipeline
    // is kept for Phase 4 (curve decorators).
    VkPipeline       stencilMaskPipeline_ = VK_NULL_HANDLE;
    VkPipeline       patternFillPipeline_ = VK_NULL_HANDLE;   // procedural fills
    VkPipeline       patternInstPipeline_ = VK_NULL_HANDLE;   // instanced (stencil-clipped)
    VkPipeline       decorInstPipeline_   = VK_NULL_HANDLE;   // instanced curve decorators
    VkPipeline       strokeFillPipeline_  = VK_NULL_HANDLE;   // flat colour, stencil EQUAL (Lot A)

    // Offscreen submission slots (ring): each RenderView that re-submits picks the
    // next slot, records into its command buffer, submits SIGNALLING `sem` (no CPU
    // wait), and pushes `sem` into framePendingWaits_ for the main pass to wait on.
    // `fence` recycles the slot a few frames later (checked non-blocking). This is
    // what removes the per-view vkWaitForFences CPU stall. cmd_ kept for the one-shot
    // RenderToRGBA path (which still waits its own fence).
    static constexpr int kSubmitSlots = 4;
    struct SubmitSlot {
        VkCommandBuffer cmd  = VK_NULL_HANDLE;
        VkFence         fence= VK_NULL_HANDLE;   // slot in-flight guard
        VkSemaphore     sem  = VK_NULL_HANDLE;   // offscreen-done → main pass waits
        bool            pending = false;         // submitted, not yet recycled
    };
    SubmitSlot       slots_[kSubmitSlots]{};
    int              nextSlot_ = 0;
    std::vector<VkSemaphore> framePendingWaits_;   // collected by the Application
    VkCommandBuffer  cmd_            = VK_NULL_HANDLE;   // current view's slot cmd (set per RenderView)
    VkCommandBuffer  oneShotCmd_     = VK_NULL_HANDLE;   // dedicated for RenderToRGBA / glyph (synchronous)

    // Host-visible vertex buffer, refilled per view.
    VkBuffer         vbo_            = VK_NULL_HANDLE;
    VkDeviceMemory   vboMemory_      = VK_NULL_HANDLE;
    VkDeviceSize     vboCapacity_    = 0;

    // Static unit base meshes for instanced patterns (disc / triangle / quad),
    // packed in one device buffer; each kind is a [firstVertex,count] sub-range.
    VkBuffer         baseMeshVbo_    = VK_NULL_HANDLE;
    VkDeviceMemory   baseMeshMemory_ = VK_NULL_HANDLE;
    struct BaseRange { uint32_t first = 0, count = 0; };
    BaseRange        baseRange_[4]{};   // indexed by PatternElementKind (Disc..HalfDisc)

    std::unordered_map<const void*, RenderTarget> targets_;
    uint64_t frame_ = 0;

    int ssaaFactor_ = 2;   // supersampling factor (see SetSSAAFactor)
    // Hard cap on a supersampled target's largest dimension (px). Above this the
    // effective factor for that view drops so a huge zone can't blow up VRAM.
    static constexpr uint32_t kMaxTargetDim = 8192;

    Mesh scratchMesh_;   // reused per RenderView rebuild to avoid reallocations
    Mesh scratchCover_;  // pattern cover/cut-polygon (stencil) triangles
    std::vector<PatternInstance> scratchDecor_;  // curve-decorator instances

    Tessellator::Cache cache_;     // per-shape tessellation cache (persists)
    Tessellator::Cache glyphCache_;// separate cache for RenderGlyphCached's shapes
    bool     cullEnabled_ = true;  // frustum culling on by default
    Metrics  metrics_{};           // accumulated over the frame
    Metrics  metricsAccum_{};      // building up during the frame, published at End

    // Cached offscreen glyph textures (Symbol Viewer thumbnails / placement ghost),
    // each an SSAA render of a shape set, rebuilt only when its content hash flips.
    struct GlyphTex {
        RenderTarget t;
        uint64_t     contentHash = 0;
        uint64_t     lastUsedFrame = 0;
    };
    std::unordered_map<uint64_t, GlyphTex> glyphTex_;
};

} // namespace Renderer

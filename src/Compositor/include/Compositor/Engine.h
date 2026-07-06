#pragma once

#include "Renderer/IViewRenderer.h"
#include "Compositor/GPU/Allocator.h"
#include "Compositor/GPU/ShapePool.h"
#include "Compositor/Geometry/FillGeometry.h"
#include "Compositor/Frame/DirtyTracker.h"
#include <unordered_map>

namespace Comp {

// ─────────────────────────────────────────────────────────────────────────────
//  Comp::Engine — the second, from-scratch Vulkan render engine.
//
//  Built in parallel to the legacy `Renderer::CanvasRenderer`, on a modern base
//  (dynamic rendering + synchronization2 + descriptor indexing + VMA, linear
//  premultiplied working space) and organised in logical passes (Content / Mask /
//  Filter / Composite / Output, + async Picking). Both engines implement the
//  shared `Renderer::IViewRenderer` contract so the Application can clean-swap
//  between them at runtime (exactly one alive at a time).
//
//  ImGui never touches the canvas: each view is rendered to an offscreen VkImage
//  (Vulkan) and composited onto the swapchain by THIS engine's own Vulkan pass
//  (CompositeMainPass). ImGui only draws the chrome around the canvas.
//
//  See docs/Vulkan/COMPOSITOR_PIPELINE.md for the full architecture + roadmap.
//
//  Lot 1b: per-view offscreen target (VMA image) cleared via dynamic rendering +
//  a Vulkan composite onto the swapchain. Document geometry lands in Lot 2.
// ─────────────────────────────────────────────────────────────────────────────
class Engine final : public Renderer::IViewRenderer {
public:
    void Initialize(VkInstance instance,
                    VkDevice device, VkPhysicalDevice physicalDevice,
                    VkQueue queue, uint32_t queueFamily,
                    VkCommandPool commandPool, VkSampler sampler,
                    const std::string& shaderDir) override;
    void Shutdown() override;
    bool IsInitialized() const override { return initialized_; }

    void BeginFrame() override;
    const std::vector<VkSemaphore>& FrameWaitSemaphores() const override { return frameWaits_; }
    ImTextureID RenderView(const void* key, const Renderer::Document& doc,
                           const Renderer::Camera& cam, int widthPx, int heightPx,
                           ImVec4 clearColor,
                           const std::vector<Renderer::Tessellator::PagePlacement>* placements,
                           bool includeLoose, bool focused) override;
    void EndFrame() override;
    const Metrics& GetMetrics() const override { return metrics_; }

    bool RenderToRGBA(const Renderer::Document& doc, const Renderer::Camera& cam,
                      int w, int h, ImVec4 clearColor,
                      std::vector<unsigned char>& outRGBA) override;
    ImTextureID RenderGlyphCached(uint64_t key, uint64_t contentHash,
                                  const std::vector<Renderer::Shape>& shapes,
                                  int widthPx, int heightPx, float padFrac,
                                  ImVec4 clearColor, bool exactFit,
                                  const Renderer::Vec2* frameMin,
                                  const Renderer::Vec2* frameMax) override;

    // ── Canvas presentation (pure Vulkan; never ImGui) ────────────────────────
    bool PresentsViaSwapchain() const override { return true; }
    void PlaceView(const void* key, ImVec4 ndcRect) override;
    void CompositeMainPass(VkCommandBuffer cmd, uint32_t fbW, uint32_t fbH,
                           VkRenderPass renderPass) override;

    // ── Picking (GPU id-pass, async readback) ─────────────────────────────────
    uint64_t Pick(const void* key, int px, int py) override;

    // ── Editor overlays (Lot 12) ──────────────────────────────────────────────
    void SubmitOverlay(const std::vector<Renderer::IViewRenderer::OverlayVertex>& verts,
                       const std::vector<uint32_t>& indices) override;

private:
    // Per-view offscreen target: a VMA image rendered to (dynamic rendering) and
    // then sampled by the composite pass. Keyed by the Viewport's EditorState ptr.
    struct ViewTarget {
        VkImage         image = VK_NULL_HANDLE;
        VmaAllocation   alloc = nullptr;
        VkImageView     view  = VK_NULL_HANDLE;
        VkDescriptorSet desc  = VK_NULL_HANDLE;   // combined image sampler (blit)
        // Stencil attachment (Lot 3b): the P2 Mask/Coverage stage rasterises each
        // surface's contour / stroke ribbon here, then draws the content masked.
        VkImage         stencil      = VK_NULL_HANDLE;
        VmaAllocation   stencilAlloc = nullptr;
        VkImageView     stencilView  = VK_NULL_HANDLE;
        // ── Isolation levels (Lot 11-4e: hierarchical blend, Affinity/PS-style) ──
        // Compositing is recursive: each container (page = level 0 = the canvas
        // itself; a group = level 1; a nested group = level 2; …) composites its
        // children onto its OWN target, then merges that target onto its parent with
        // the container's blend/opacity. A child's blend reads the target it is being
        // drawn into (everything below it in the SAME level) as its backdrop. So we
        // need one isolation target PER DEPTH. Level 0 is the canvas (t.image /
        // t.stencil); levels ≥1 live in `isoLevels`, allocated lazily up to the
        // depth actually used. Each level owns: a colour image (+ view + a sampler
        // descriptor for the composite), its own stencil, and a backdrop copy (+ a
        // {level, backdrop} descriptor for the blend shader).
        struct IsoLevel {
            VkImage         color      = VK_NULL_HANDLE;
            VmaAllocation   colorAlloc = nullptr;
            VkImageView     colorView  = VK_NULL_HANDLE;
            VkDescriptorSet desc       = VK_NULL_HANDLE;   // sampler(color) — opacity composite
            VkImage         stencil      = VK_NULL_HANDLE;
            VmaAllocation   stencilAlloc = nullptr;
            VkImageView     stencilView  = VK_NULL_HANDLE;
            VkImage         backdrop      = VK_NULL_HANDLE;
            VmaAllocation   backdropAlloc = nullptr;
            VkImageView     backdropView  = VK_NULL_HANDLE;
            VkDescriptorSet blendDesc     = VK_NULL_HANDLE; // {color, backdrop} — blend composite
        };
        std::vector<IsoLevel> isoLevels;
        uint32_t        w = 0, h = 0;
        uint64_t        lastUsedFrame = 0;
        bool            placed = false;           // got an NDC dst rect this frame?
        float           ndc[4] = { 0, 0, 0, 0 };

        // Persistent per-view document geometry (rebuilt only when the content
        // signature changes — pan/zoom reuse it, like the legacy renderer).
        VkBuffer        vbo      = VK_NULL_HANDLE;
        VmaAllocation   vboAlloc = nullptr;
        void*           vboMapped = nullptr;
        VkDeviceSize    vboCap   = 0;
        uint32_t        vertexCount = 0;
        // Cover mesh (Lot 3b): cut-polygons (pattern contours) + transparent-stroke
        // ribbon coverage + bbox quads — written to / tested against the stencil.
        VkBuffer        maskVbo      = VK_NULL_HANDLE;
        VmaAllocation   maskAlloc    = nullptr;
        void*           maskMapped   = nullptr;
        VkDeviceSize    maskCap      = 0;
        uint32_t        maskVertexCount = 0;
        // Stencil-then-cover base fills (Lot 13-4a): per-object winding fans + cover
        // quads (position-only FanVertex), built WITHOUT ear-clipping. Only PURE
        // solid-fill objects live here (they're skipped in the baked `segs`); the
        // renderer draws each object's fan → stencil (non-zero) then its cover quad.
        // Lot 13-1b: these now live in a PERSISTENT per-shape pool — a pure-fill object
        // owns a stable slice (its fans + its 6-vert cover quad, contiguous), so an
        // edit re-uploads only that object's slice and unchanged objects are never
        // re-copied. `fillPages` holds the per-object draw metadata; each FillObject's
        // fanFirst/coverFirst are POOL-relative element offsets (rewritten on upload).
        ShapePool                   fillPool;      // FanVertex pool (base fills)
        std::vector<Comp::FillPage> fillPages;     // pure-fill objects, page order
        uint64_t        buildSig = 0;
        bool            hasGeom  = false;
        // Per-shape change tracker (Lot 13-1a): last build's per-shape hashes for this
        // view, so a rebuild reports how many shapes actually changed (HUD; drives the
        // incremental pool in 13-1b).
        Comp::DirtyTracker dirty;
        // Per-page draw structure (backdrop + objects, in document order) — drives
        // the per-page scissor + per-object draws. Rebuilt with the vbo.
        std::vector<Renderer::Tessellator::PageSeg> segs;

        // ── Picking id-pass (Lot 8) ────────────────────────────────────────────
        // An R32UI id buffer rendered alongside the colour pass: each object draws
        // its 1-based objectId; objectId→Shape::id is `pickIds` (index = id−1).
        // Lazily allocated on the first Pick request for this view.
        VkImage         pickImg    = VK_NULL_HANDLE;
        VmaAllocation   pickAlloc  = nullptr;
        VkImageView     pickView   = VK_NULL_HANDLE;
        std::vector<uint64_t> pickIds;          // objectId−1 → Shape::id (this frame)
        // 1-pixel readback: a host-visible buffer the id pixel is copied into, with
        // a fence so the next frame can read last frame's result without a stall.
        VkBuffer        pickReadBuf   = VK_NULL_HANDLE;
        VmaAllocation   pickReadAlloc = nullptr;
        void*           pickReadMapped = nullptr;
        VkFence         pickFence     = VK_NULL_HANDLE;   // reserved (unused: we wait on the submit fence)
        VkFence         pickSubmitFence = VK_NULL_HANDLE; // the slot fence of the submit that did the copy
        bool            pickPending   = false;   // a readback is in flight
        bool            pickRequested = false;   // a Pick() asked to sample this frame
        int             pickX = 0, pickY = 0;    // logical px requested
        std::vector<uint64_t> pickIdsInFlight;   // map matching the in-flight readback
        int             pickInFlightX = 0, pickInFlightY = 0;  // px the in-flight copy sampled
        int             pickLastX = -1, pickLastY = -1;        // px of the last resolved result
        uint64_t        pickLastId = 0;          // last resolved Shape::id (cached for repeats)
    };

    // One offscreen submission per view per frame (binary semaphore the main pass
    // waits on + a fence guarding cross-frame command-buffer reuse).
    struct SubmitSlot {
        VkCommandBuffer cmd   = VK_NULL_HANDLE;
        VkFence         fence = VK_NULL_HANDLE;
        VkSemaphore     sem   = VK_NULL_HANDLE;
        // GPU timing (Lot 13-0): 2 timestamps (start/end of the offscreen render). The
        // previous frame's slot result is read back after its fence, so no stall.
        VkQueryPool     tsPool  = VK_NULL_HANDLE;
        bool            tsValid = false;   // a pair was written and can be read
    };
    float          timestampPeriodNs_ = 0.0f;   // ns per GPU timestamp tick (from device limits)

    ViewTarget& AcquireTarget(const void* key, uint32_t w, uint32_t h);
    void        CreateTargetImages(ViewTarget& t, uint32_t w, uint32_t h);
    // Lazily allocate isolation level `depth` (1-based; the canvas is level 0) and
    // return it. Levels grow with hierarchy depth (Lot 11-4e). `withBackdrop` also
    // allocates the backdrop copy + blend descriptor (only a level that hosts a
    // non-Normal blend needs it). Returns a reference into t.isoLevels.
    ViewTarget::IsoLevel& EnsureIsoLevel(ViewTarget& t, int depth, bool withBackdrop);
    void        EnsurePickTarget(ViewTarget& t); // lazy: only when a view is picked (Lot 8)
    void        EnsureTargetVbo(ViewTarget& t, VkDeviceSize bytes);
    void        EnsureTargetMaskVbo(ViewTarget& t, VkDeviceSize bytes);
    void        DestroyTarget(ViewTarget& t);
    SubmitSlot& AcquireSlot();                 // slot for the next view this frame
    void        EnsureCompositePipeline(VkRenderPass renderPass);
    void        CreateCompositeStatics();      // set layout / pool / pipe layout / shaders
    void        CreateShapePipeline();         // document-geometry pipeline (dynamic rendering)
    void        CreateFillPipelines();         // Lot 13-4a: stencil-then-cover base fill (no ear-clip)
    void        CreateCoveragePipelines();     // P2 Mask/Coverage: stencil-mask / pattern / stroke
    void        CreateIsoCompositePipeline();   // P4 Composite: isolated-layer → canvas, ×opacity
                                                // (also builds eraseCompPipeline_, dst-out, Lot 7)
    void        CreateBlendPipeline();          // P4 Composite: {iso,backdrop} → blend → canvas
    void        CreatePickingPipeline();        // Picking id-pass → R32UI id buffer (Lot 8)
    VkFormat    ChooseStencilFormat() const;   // a supported depth-stencil format
    VkShaderModule LoadShader(const std::string& path);

    // FNV-1a signature of what RenderView would build for `doc` (shape ids + cache
    // hashes + placements + includeLoose + detail bucket). Equal sig ⇒ reuse the
    // per-view vertex buffer (pan/zoom within a detail bucket don't rebuild). The
    // SAME O(N) walk also feeds `dt` the per-shape hashes (Lot 13-1a), so one pass
    // yields both the rebuild gate AND the per-shape change diff; `outDiff` returns
    // the diff (added/changed/removed) for the HUD / the incremental pool (13-1b).
    uint64_t BuildSignatureAndDiff(const Renderer::Document& doc,
                                   const std::vector<Renderer::Tessellator::PagePlacement>* placements,
                                   bool includeLoose, int detailBucket,
                                   DirtyTracker& dt, DirtyTracker::Diff& outDiff) const;

    static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr int      kSSAA   = 2;       // supersampling factor (anti-aliasing)
    static constexpr uint32_t kMaxDim = 8192;    // cap on a supersampled target axis
    static constexpr int      kMaxIsoDepth = 8;  // max hierarchy isolation depth (Lot 11-4e)

    VkFormat stencilFormat_ = VK_FORMAT_UNDEFINED;   // chosen at Initialize (Lot 3b)

    bool             initialized_    = false;

    VkInstance       instance_       = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    VkCommandPool    appCommandPool_ = VK_NULL_HANDLE;   // shared (one-shots; unused yet)
    VkSampler        sampler_        = VK_NULL_HANDLE;   // owned by Application
    std::string      shaderDir_;

    Allocator        allocator_;            // VMA

    VkCommandPool    cmdPool_        = VK_NULL_HANDLE;   // our own (RESET bit)
    std::vector<SubmitSlot> slots_;         // grows to max views/frame
    int              viewsThisFrame_ = 0;   // reset each BeginFrame

    // Composite (blit) pipeline — samples a view target, draws an NDC quad into
    // the main pass. Built lazily for the main render pass (rebuilt if it changes).
    VkDescriptorSetLayout blitSetLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool      blitDescPool_   = VK_NULL_HANDLE;
    VkPipelineLayout      blitPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        blitVert_       = VK_NULL_HANDLE;
    VkShaderModule        blitFrag_       = VK_NULL_HANDLE;
    VkPipeline            blitPipeline_   = VK_NULL_HANDLE;
    VkRenderPass          blitPipelineRP_ = VK_NULL_HANDLE;   // RP the pipeline was built for

    // Document-geometry pipeline: per-vertex coloured triangles, dynamic rendering
    // into a view target's colour image (no render pass object). Built once.
    VkPipelineLayout      shapePipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        shapeVert_       = VK_NULL_HANDLE;
    VkShaderModule        shapeFrag_       = VK_NULL_HANDLE;
    VkPipeline            shapePipeline_   = VK_NULL_HANDLE;
    // Page substrate (white/grid/transparent backdrop), drawn AFTER the object stack
    // with a DST-OVER blend so it fills UNDER the objects and under any erase holes —
    // the page background is a display substrate, not a layer the erase can cut. Same
    // layout/shaders as shapePipeline_, only the blend differs (Lot 7).
    VkPipeline            backdropPipeline_ = VK_NULL_HANDLE;

    // Stencil-then-cover base fill (Lot 13-4a): a PURE solid fill's flattened
    // contour is fanned trivially into the stencil (non-zero winding: front faces
    // INCR_WRAP, back DECR_WRAP), then a bbox quad is drawn once where the stencil
    // != 0 with the object's uniform colour (which also resets the stencil to 0 on
    // covered pixels). No ear-clipping — the cost is O(contour), so editing a very
    // heavy path no longer stalls. Stencil pass = pos-only camera push; cover pass =
    // camera + colour (FillCoverPush, 48B) shared layout.
    VkPipelineLayout      fillStencilPipeLayout_ = VK_NULL_HANDLE;  // ShapePush (32B)
    VkPipelineLayout      fillCoverPipeLayout_   = VK_NULL_HANDLE;  // FillCoverPush (48B)
    VkShaderModule        fillStencilVert_ = VK_NULL_HANDLE;
    VkShaderModule        fillCoverVert_   = VK_NULL_HANDLE;
    VkShaderModule        fillCoverFrag_   = VK_NULL_HANDLE;
    VkPipeline            fillStencilPipeline_ = VK_NULL_HANDLE;   // winding write, no colour
    VkPipeline            fillCoverPipeline_   = VK_NULL_HANDLE;   // cover quad, stencil != 0

    // P2 Mask/Coverage stage (Lot 3b). One 96-byte push layout (camera + pattern
    // params) shared by all three; the stencil reference is dynamic per surface.
    VkPipelineLayout      coverPipeLayout_     = VK_NULL_HANDLE;
    VkShaderModule        patternVert_         = VK_NULL_HANDLE;
    VkShaderModule        patternFrag_         = VK_NULL_HANDLE;
    VkPipeline            stencilMaskPipeline_ = VK_NULL_HANDLE;   // write cover → stencil (REPLACE)
    VkPipeline            patternFillPipeline_ = VK_NULL_HANDLE;   // procedural motif, stencil EQUAL
    VkPipeline            strokeFillPipeline_  = VK_NULL_HANDLE;   // bbox quad once, stencil EQUAL

    // P4 Composite isolation (Lot 4a-2): sample an isolated object layer, ×opacity,
    // blend over the canvas. Reuses blitSetLayout_ (1 combined image sampler).
    VkPipelineLayout      isoCompPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        isoVert_           = VK_NULL_HANDLE;
    VkShaderModule        isoFrag_           = VK_NULL_HANDLE;
    VkPipeline            isoCompPipeline_   = VK_NULL_HANDLE;

    // P2 subtractive / erase (Lot 7): same isolated-layer + layout + shaders as the
    // iso composite, but a dst-out blend state (subtract coverage from the canvas).
    VkPipeline            eraseCompPipeline_ = VK_NULL_HANDLE;

    // P4 Composite blend (Lot 4b): {iso, backdrop} → blend(mode) → canvas. The mode
    // is a push constant; the shader does the blend (so one pipeline for all modes).
    VkDescriptorSetLayout blendSetLayout_  = VK_NULL_HANDLE;   // 2 combined image samplers
    VkPipelineLayout      blendPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        blendFrag_       = VK_NULL_HANDLE;
    VkPipeline            blendPipeline_   = VK_NULL_HANDLE;

    // Picking id-pass (Lot 8): object geometry → R32UI id buffer. Push = ShapePush
    // camera (32B) + a uint object id; no descriptor sets. One pipeline, drawn into
    // a per-view id image; the topmost object's id wins per pixel (painter order).
    static constexpr VkFormat kPickFormat = VK_FORMAT_R32_UINT;
    VkPipelineLayout      pickPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        pickVert_       = VK_NULL_HANDLE;
    VkShaderModule        pickFrag_       = VK_NULL_HANDLE;
    VkPipeline            pickPipeline_   = VK_NULL_HANDLE;

    // Editor overlays (Lot 12): a per-frame coloured-triangle list (NDC) submitted by
    // the editor, drawn in CompositeMainPass over the canvas + under ImGui. Built into
    // the main render pass (rebuilt if it changes), like the blit pipeline.
    void        CreateOverlayPipeline(VkRenderPass renderPass);
    VkPipelineLayout      overlayPipeLayout_ = VK_NULL_HANDLE;
    VkShaderModule        overlayVert_       = VK_NULL_HANDLE;
    VkShaderModule        overlayFrag_       = VK_NULL_HANDLE;
    VkPipeline            overlayPipeline_   = VK_NULL_HANDLE;
    VkRenderPass          overlayPipelineRP_ = VK_NULL_HANDLE;
    VkBuffer      overlayVbo_ = VK_NULL_HANDLE;  VmaAllocation overlayVboAlloc_ = nullptr;
    void*         overlayVboMapped_ = nullptr;   VkDeviceSize  overlayVboCap_   = 0;
    VkBuffer      overlayIbo_ = VK_NULL_HANDLE;  VmaAllocation overlayIboAlloc_ = nullptr;
    void*         overlayIboMapped_ = nullptr;   VkDeviceSize  overlayIboCap_   = 0;
    uint32_t      overlayIndexCount_ = 0;

    Renderer::Tessellator::Cache cache_;     // per-shape tessellation cache
    Renderer::Mesh               scratchMesh_;  // base mesh, reused each rebuild
    Renderer::Mesh               scratchCover_; // cover mesh (cut-polys/ribbon/quads)
    std::vector<Comp::FanVertex> scratchFans_;  // Lot 13-4a: stencil-then-cover fill fans+quads

    // One-shot glyph/thumbnail render cache (RenderGlyphCached, Lot 11-5 previews +
    // .acu thumbnails / Symbol Viewer). Each entry is a small offscreen image whose
    // shapes are auto-framed and rendered synchronously, exposed to ImGui as a texture.
    struct GlyphTex {
        VkImage       image  = VK_NULL_HANDLE;
        VmaAllocation alloc  = nullptr;
        VkImageView   view   = VK_NULL_HANDLE;
        VkBuffer      vbo      = VK_NULL_HANDLE;
        VmaAllocation vboAlloc = nullptr;
        void*         vboMapped = nullptr;
        VkDeviceSize  vboCap   = 0;
        VkDescriptorSet imguiTex = VK_NULL_HANDLE;   // ImGui_ImplVulkan_AddTexture → ImTextureID
        uint32_t      w = 0, h = 0;
        uint64_t      contentHash = 0;
        uint64_t      lastUsedFrame = 0;
    };
    std::unordered_map<uint64_t, GlyphTex> glyphTex_;
    Renderer::Tessellator::Cache glyphCache_;   // tessellation cache for glyph renders

    std::unordered_map<const void*, ViewTarget> targets_;
    std::vector<VkSemaphore> frameWaits_;   // offscreen-done sems (one per view this frame)
    uint64_t frame_ = 0;
    Metrics  metrics_{};
};

} // namespace Comp

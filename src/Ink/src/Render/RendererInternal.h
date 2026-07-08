#pragma once

// Internal shared state of the Renderer: the view implementation, the frame
// ring and the pass entry points (each pass lives in its own .cpp under
// Passes/). Included by Renderer.cpp, View.cpp and the pass files only.

#include "Ink/Core/Math.h"
#include "Ink/Document/Document.h"
#include "Ink/Geometry/GeometryCache.h"
#include "Ink/Graph/RenderGraph.h"
#include "Ink/RHI/Device.h"
#include "Ink/RHI/Resources.h"
#include "Ink/Render/GpuScene.h"
#include "Ink/Render/Renderer.h"
#include "Ink/Render/Stats.h"
#include "Ink/Scene/Scene.h"
#include "Ink/View/OverlayList.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Ink::detail {

inline constexpr std::uint32_t kFramesInFlight = 2;

// Formats of the per-view target chain (docs/Ink/RENDER_GRAPH.md §4):
// MSAA linear-premultiplied content → resolved → sRGB-encoded display.
inline constexpr VkFormat kContentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat kDisplayFormat = VK_FORMAT_R8G8B8A8_UNORM;

// MSAA stencil format for clip masks (docs/Ink/RENDER_GRAPH.md §ClipPass).
inline constexpr VkFormat kStencilFormat = VK_FORMAT_S8_UINT;

// The camera push-constant block shared by the content and overlay pipelines:
// ndc = pos · scale + offset (world → NDC for content, px → NDC for overlay).
struct PushCamera {
    float sx, sy, ox, oy;
};

// Composite push constants (iso.frag): group opacity + blend-mode index.
struct PushComposite {
    float        opacity;
    std::uint32_t blend;
};

// A unit of isolation (RENDER_GRAPH.md §2: reserve once, never grow
// mid-recording). Content is drawn MSAA and resolved into `linear`;
// compositing a child onto this target ping-pongs linear→linearB (the
// composite reads the backdrop AND writes, so it cannot alias one image),
// then `cur` flips. iso[0] is the view's main content; iso[1..] are the
// reserved isolation levels for composite scopes.
struct IsoTarget {
    rhi::Image      msaa;      // kContentFormat, device MSAA count
    rhi::Image      linear;    // resolve target A (sampled)
    rhi::Image      linearB;   // ping-pong B (sampled)
    rhi::Image      stencil;   // kStencilFormat MSAA (clip mask, wired for Lot 4+)
    VkDescriptorSet setA = VK_NULL_HANDLE;   // present-layout set sampling linear
    VkDescriptorSet setB = VK_NULL_HANDLE;   // present-layout set sampling linearB
    int             cur  = 0;  // 0 = linear is current, 1 = linearB
    rhi::Image&      Cur()    { return cur ? linearB : linear; }
    rhi::Image&      Other()  { return cur ? linear  : linearB; }
    VkDescriptorSet  CurSet() const { return cur ? setB : setA; }
};

// One recorded scope in playback order (post-order: a child scope renders
// and composites before its parent continues). A run draws its own content
// commands into iso[level], optionally masked by a clip stencil, then (for
// level > 0) composites iso[level].linear onto iso[parentLevel] with the
// scope's opacity + blend.
struct ScopeRun {
    ScopeId       scope       = kRootScope;
    std::uint32_t level       = 0;        // iso[] index it renders into
    std::uint32_t parentLevel = 0;        // iso[] index it composites onto
    std::uint32_t cmdOffset   = 0;        // into ViewImpl::indirectScratch
    std::uint32_t cmdCount    = 0;
    std::uint32_t clipOffset  = 0;        // clip-source commands (stencil write)
    std::uint32_t clipCount   = 0;
    bool          clip        = false;
    float         opacity     = 1.0f;
    std::uint32_t blend       = 0;
    bool          composites  = false;    // level > 0 → a composite step follows
};

struct ViewImpl {
    RendererImpl* owner = nullptr;

    // App-set state.
    std::uint32_t width = 0, height = 0;
    double panX = 0.0, panY = 0.0, zoom = 1.0;
    Color  background{ 0.1f, 0.1f, 0.1f, 1.0f };
    OverlayList overlay;

    // Target chain. iso[0] is the main content; iso[1..isoLevels] are the
    // reserved composite-isolation levels (sized to the scene's max depth).
    static constexpr std::uint32_t kMaxIsoLevels = 8;   // clamp (RENDER_GRAPH §2)
    IsoTarget  iso[kMaxIsoLevels + 1];
    std::uint32_t isoLevels = 0;      // reserved levels above iso[0]
    rhi::Image display;   // kDisplayFormat ×1, sampled by the UI
    VkDescriptorSet presentSet = VK_NULL_HANDLE;  // samples iso[0].linear
    std::uint64_t   texture    = 0;               // app-side handle of `display`

    // Per-slot overlay vertex buffers (host-visible ring).
    rhi::Buffer   overlayVb[kFramesInFlight];
    std::uint32_t overlayVertexCount = 0;

    // The view's draw commands (built in scope order): one host-visible ring
    // buffer per slot holds every command back-to-back; each ScopeRun points
    // into it. Mesh ranges depend on the view's zoom tier, so commands are
    // per-view while item/paint tables and geometry stay global.
    rhi::Buffer   indirect[kFramesInFlight];
    std::vector<VkDrawIndexedIndirectCommand> indirectScratch;
    // One entry per composite scope + clip run to play back this frame.
    std::vector<ScopeRun> scopeRuns;

    // Camera-relative precision (docs/Ink/GEOMETRY.md §6): this view's double
    // anchor (snapped so it moves rarely) and its anchor-rebased instance
    // table + the descriptor set pointing at it (binding 0) and the global
    // item/paint tables (1, 2).
    double          anchorX = 0.0, anchorY = 0.0;
    rhi::Buffer     instanceBuf;
    VkDescriptorSet sceneSet = VK_NULL_HANDLE;
    bool            instancesDirty = true;
    std::uint64_t   sceneGen = 0;   // last RendererImpl::sceneGen consumed
    std::uint64_t   styleGen = 0;   // last RendererImpl::styleGen pointed at

    // Zoom tier with hysteresis (GeometryCache::StableTier).
    int  tier     = 0;
    bool tierInit = false;

    // Frame bookkeeping.
    std::uint64_t lastSignature = 0;
    bool          forceDirty    = true;    // set on create/resize
    std::uint64_t lastUsedFrame = 0;
    bool          usedThisFrame = false;

    bool HasTargets() const { return (bool)display; }
};

struct FrameSlot {
    VkCommandPool   pool    = VK_NULL_HANDLE;
    VkCommandBuffer cb      = VK_NULL_HANDLE;
    VkFence         fence   = VK_NULL_HANDLE;
    bool            armed   = false;   // a submit is in flight on this slot
    VkQueryPool     queries = VK_NULL_HANDLE;   // 2 timestamps (frame span)
    bool            queried = false;
    // Deferred destruction: runs once this slot's fence has been waited.
    std::vector<std::function<void()>> garbage;
};

struct RendererImpl {
    rhi::Device  device;
    TextureHooks hooks;
    std::string  shaderDir;

    // The content chain: app-owned Document → compiled Scene → CPU geometry
    // cache → GPU residency/tables.
    Document*     document = nullptr;
    Scene         scene;
    GeometryCache cache;
    GpuScene      gpu;
    bool          forceCompile = true;   // set by SetDocument

    // Pipelines (created once — target formats are fixed).
    VkDescriptorSetLayout sceneSetLayout    = VK_NULL_HANDLE;
    VkDescriptorSetLayout presentSetLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;  // 2 samplers
    VkPipelineLayout contentLayout   = VK_NULL_HANDLE;
    VkPipelineLayout overlayLayout   = VK_NULL_HANDLE;
    VkPipelineLayout presentLayout   = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout = VK_NULL_HANDLE;
    VkPipeline contentPipeline     = VK_NULL_HANDLE;
    VkPipeline contentClipPipeline = VK_NULL_HANDLE;  // stencil TestEqual
    VkPipeline clipMaskPipeline    = VK_NULL_HANDLE;  // stencil WriteMask
    VkPipeline overlayPipeline     = VK_NULL_HANDLE;
    VkPipeline presentPipeline     = VK_NULL_HANDLE;
    VkPipeline compositePipeline   = VK_NULL_HANDLE;  // iso composite (blend)
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    // Generations: bumped when the drawable list changes / when a global
    // style table buffer is recreated — views resync lazily against them.
    std::uint64_t sceneGen = 1;
    std::uint64_t styleGen = 1;

    // Frame ring.
    FrameSlot     slots[kFramesInFlight];
    std::uint32_t slot       = 0;
    std::uint64_t frameIndex = 0;
    bool          frameOpen  = false;

    std::unordered_map<const void*, std::unique_ptr<View>> views;

    Stats stats;       // work-in-progress counters of the OPEN frame
    Stats published;   // last completed frame — what GetStats() returns
    float lastGpuMs = 0.0f;

    // View target lifecycle (Renderer.cpp): create/retire the image chain.
    void CreateViewTargets(ViewImpl& v, std::uint32_t w, std::uint32_t h);
    void RetireViewTargets(ViewImpl& v);   // push onto the current slot's garbage
    // Grow the isolation-level targets to `levels` above iso[0] (on demand).
    void EnsureIsoLevels(ViewImpl& v, std::uint32_t levels);
    // Defer a destruction until the current slot's submit has completed.
    void Defer(std::function<void()> fn) { slots[slot].garbage.push_back(std::move(fn)); }
};

// Pass entry points (Passes/*.cpp). All record inside an open dynamic
// rendering scope set up by the graph.
//
// Content: draws `commandCount` indirect commands starting at byte
// `firstByte` in `indirect`, with `pipeline` (plain or stencil-test-equal).
void RecordContentPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& worldToNdc, VkBuffer indirect,
                       std::uint32_t firstByte, std::uint32_t commandCount,
                       VkDescriptorSet sceneSet, VkPipeline pipeline);
void RecordOverlayPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& pxToNdc, VkBuffer vertexBuffer,
                       std::uint32_t vertexCount);
void RecordPresentPass(RendererImpl& r, VkCommandBuffer cmd,
                       VkDescriptorSet presentSet);
// Composite: fullscreen draw sampling source+backdrop (compositeSet) with the
// scope's opacity + blend.
void RecordCompositePass(RendererImpl& r, VkCommandBuffer cmd,
                         VkDescriptorSet compositeSet, const PushComposite& pc);

// Scope playback (ScopePlayback.cpp): assign iso levels to composite scopes
// and build ViewImpl::scopeRuns in post-order. Returns the number of iso
// levels used (0 = flat scene, only iso[0]). Must not exceed v.isoLevels.
std::uint32_t BuildScopePlan(const Scene& scene, ViewImpl& v);

// Record every scope run of `v` into the frame graph `g` for this view: each
// composite scope renders into its iso level, resolves, and composites onto
// its parent; the root content + overlay land in iso[0], then present.
void PlayScopes(RendererImpl& r, ViewImpl& v, std::uint32_t slot,
                graph::RenderGraph& g, const PushCamera& world,
                const PushCamera& px, VkBuffer overlayBuffer,
                std::uint32_t overlayCount);

} // namespace Ink::detail

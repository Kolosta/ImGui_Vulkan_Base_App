#pragma once

#include "Ink/Core/Math.h"
#include "Ink/Render/Stats.h"
#include "Ink/Scene/Picking.h"   // PickOptions (editing queries)
#include "Ink/View/View.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ink {

class Document;

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — the engine root (docs/Ink/ARCHITECTURE.md §7). Adopts the
//  application's shared Vulkan 1.3 device, owns the RHI device wrapper, the
//  Scene/GeometryCache/GpuScene content chain, the pipelines and the
//  per-frame loop; hands out one View per Viewport zone.
//
//  Frame protocol (single graphics queue):
//    BeginFrame()                  — before the UI build (fence/slot rotation)
//    AcquireView(key) + View setup — during the UI build, per Viewport zone
//    EndFrame()                    — after the UI build: records every dirty
//                                    view through the render graph and submits
//  The main swapchain pass needs NO semaphore: the graph's final barriers
//  order the canvas writes before any later fragment sampling on the same
//  queue (submission-order scopes) — the app submits ImGui afterwards as
//  usual. Idle views (unchanged signature) are skipped entirely: a static
//  canvas re-presents its cached texture at zero record cost.
// ─────────────────────────────────────────────────────────────────────────────

// How the engine registers a sampled canvas texture with the UI layer without
// depending on it (the app wraps ImGui_ImplVulkan_Add/RemoveTexture; a
// headless run — ink_bench — passes none).
struct TextureHooks {
    void* user = nullptr;
    std::uint64_t (*create)(void* user, VkSampler sampler, VkImageView view,
                            VkImageLayout layout) = nullptr;
    void (*destroy)(void* user, std::uint64_t texture) = nullptr;
};

class Renderer {
public:
    struct InitInfo {
        VkInstance       instance       = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice         device         = VK_NULL_HANDLE;
        VkQueue          queue          = VK_NULL_HANDLE;
        std::uint32_t    queueFamily    = 0;
        std::string      shaderDir;     // "<exe>/shaders/ink"
        TextureHooks     textures;      // optional (headless: leave null)
    };

    Renderer();
    ~Renderer();

    // Requires a device created with the Vulkan 1.3 features (dynamic
    // rendering + synchronization2). Loads shaders and builds the pipelines.
    // False = engine unavailable (the app keeps running; the Viewport shows
    // its placeholder).
    bool Initialize(const InitInfo& info);
    void Shutdown();   // call with the device idle, before it is destroyed

    // The document to render (owned by the application — App::Project). The
    // engine compiles it into its Scene when its ChangeLog is non-empty;
    // nullptr renders background + overlays only.
    void SetDocument(Document* document);

    void BeginFrame();
    void EndFrame();

    // The view for a zone key (the leaf's EditorState address). Created on
    // first use; evicted (targets freed) after going unused for a few frames.
    View* AcquireView(const void* key);

    const Stats& GetStats() const;
    // Compiled-scene bounds (doc units) — drives the Viewport's fit-view.
    Rect SceneBounds() const;

    // ── Editing queries on the compiled Scene (docs/Ink/ROADMAP.md Lot 8) ─────
    // The topmost object at a document-space point (exact CPU hit-test), or
    // kNullNode. `tolerance` is in document units, `zoom` in view-px/doc-unit.
    NodeId PickAt(DVec2 docPoint, const PickOptions& opt) const;
    // Distinct objects whose rendered bounds intersect the document-space box.
    std::vector<NodeId> PickInBox(DVec2 boxMin, DVec2 boxMax) const;
    // A single object's rendered document-space bounds (selection outline /
    // fit-selection). False when it produced nothing in the last compile.
    bool NodeBounds(NodeId id, DRect& out) const;

    // ── Vector thumbnail (Outliner Layers preview) ────────────────────────
    // A flattened, resolved snapshot of a node's rendered content, straight
    // from the compiled Scene — so it INCLUDES pattern fills, instances,
    // array/along-path copies and boolean results, exactly what the canvas
    // draws. Each piece is a closed/open polygon in DOCUMENT space with its
    // display colour (linear-straight). `tolerance` bounds curve flattening.
    struct PreviewPiece {
        std::vector<DVec2> pts;
        bool  closed   = false;
        bool  isStroke = false;
        Color color;                // linear straight
    };
    // Fill `out` with the pieces whose owner is `id` or one of its descendants
    // (the node's whole rendered subtree). Returns the covered doc-space bbox.
    DRect PreviewPieces(NodeId id, double tolerance,
                        std::vector<PreviewPiece>& out) const;

    // ── Synchronous canvas readback (docs/Ink/ROADMAP.md Lot 10) ──────────
    // Copy a view's presented canvas back to the CPU: `rgba` receives
    // width×height×4 bytes, RGBA8, sRGB-encoded, top-left origin — exactly
    // what the UI samples, PNG-ready. BLOCKS until the GPU is idle (a save /
    // export operation, never a per-frame path). False when the view has not
    // rendered yet.
    bool ReadViewPixels(View* view, std::vector<std::uint8_t>& rgba,
                        std::uint32_t& width, std::uint32_t& height);

private:
    std::unique_ptr<detail::RendererImpl> impl_;
};

} // namespace Ink

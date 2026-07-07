#pragma once

#include "Ink/Core/Math.h"
#include "Ink/Geometry/GeometryCache.h"
#include "Ink/RHI/Resources.h"
#include "Ink/Scene/Scene.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  GpuScene — the persistent GPU residency of the compiled scene
//  (docs/Ink/RENDER_GRAPH.md §3): growable vertex/index pools holding the
//  GeometryCache products, and the instance/item/paint tables (SSBOs) built
//  from the Scene's drawables in painter order.
//
//  Everything updates through STAGED uploads recorded once per frame into the
//  frame's command buffer (FlushUploads), fenced by the frame ring — the CPU
//  never stalls on the GPU for content changes. Buffer growth retires the old
//  buffer through the caller's defer hook (the frame garbage ring).
//
//  A draw is one VkDrawIndexedIndirectCommand per run of consecutive
//  drawables sharing a mesh range; the per-view command lists are built by
//  the Renderer at record time (mesh ranges depend on the view's zoom tier).
// ─────────────────────────────────────────────────────────────────────────────

// GPU-facing records. Layouts match the std430 structs in vector.vert —
// 4-byte scalars only, no implicit padding.
struct ContentVertex {
    float x = 0.0f, y = 0.0f;   // node-local units
};

struct InstanceRecord {
    float m[6];                 // row-major 2×3: node-local → ANCHOR-relative
    std::uint32_t itemIndex = 0;
    std::uint32_t pad_      = 0;
};
// Translations are rebased against the view's double anchor before narrowing
// (docs/Ink/GEOMETRY.md §6) — the instance table is PER VIEW so precision
// holds at any zoom; items/paints/geometry stay global.

struct ItemRecord {
    std::uint32_t paintIndex = 0;
    std::uint32_t flags      = 0;
    std::uint32_t pad_[2]    = { 0, 0 };
};

struct PaintRecord {
    float rgba[4] = { 0, 0, 0, 1 };   // linear, premultiplied
};

// A contiguous mesh range in the pools (vertex offset in vertices).
struct MeshRange {
    std::uint32_t firstIndex   = 0;
    std::uint32_t indexCount   = 0;
    std::int32_t  vertexOffset = 0;
};

class GpuScene {
public:
    // Deferred-destruction hook: the Renderer's frame garbage ring.
    using DeferFn = std::function<void(std::function<void()>)>;

    static constexpr std::uint32_t kStagingRing = 2;   // == frames in flight

    bool Initialize(rhi::Device& dev);
    void Shutdown(rhi::Device& dev);

    // Ensure the cache product `key` is resident in the pools (queues the
    // upload on first sight; pool growth re-queues every resident mesh from
    // `cache`). Empty range (indexCount 0) on failure.
    MeshRange EnsureResident(rhi::Device& dev, std::uint64_t key,
                             const geom::Mesh& mesh, const GeometryCache& cache,
                             const DeferFn& defer);

    // Rebuild the GLOBAL item/paint tables from the drawables (painter order)
    // and queue their upload. Also refreshes the drawable → item index map
    // used by the per-view instance builds. Returns true when a table BUFFER
    // was recreated (view descriptor sets must be re-pointed).
    bool SyncStyleTables(rhi::Device& dev, const std::vector<Drawable>& drawables,
                         const DeferFn& defer);

    // Build ONE VIEW's instance table: world transforms rebased against the
    // view anchor (double subtract, then narrow) in drawable order. `buf` is
    // the view-owned device buffer (grown here; old one deferred). Returns
    // true when the buffer was recreated.
    bool SyncViewInstances(rhi::Device& dev,
                           const std::vector<Drawable>& drawables,
                           DVec2 anchor, rhi::Buffer& buf, const DeferFn& defer);

    // Record every queued upload into `cmd` — call ONCE per frame, BEFORE any
    // render pass. `slot` picks the staging ring entry (fenced by the frame
    // ring). Returns the number of bytes uploaded.
    std::size_t FlushUploads(rhi::Device& dev, VkCommandBuffer cmd,
                             std::uint32_t slot, const DeferFn& defer);

    const rhi::Buffer& VertexPool() const { return vertexPool_; }
    const rhi::Buffer& IndexPool()  const { return indexPool_; }
    const rhi::Buffer& ItemTable()  const { return itemTable_; }
    const rhi::Buffer& PaintTable() const { return paintTable_; }
    bool StyleTablesReady() const { return (bool)itemTable_ && (bool)paintTable_; }

private:
    struct PendingCopy {
        VkBuffer     dst = VK_NULL_HANDLE;
        VkDeviceSize dstOffset = 0;
        std::size_t  srcOffset = 0;   // into pendingData_
        std::size_t  size = 0;
    };
    void Queue(VkBuffer dst, VkDeviceSize dstOffset, const void* data,
               std::size_t size);
    bool GrowPools(rhi::Device& dev, std::uint32_t vtxNeeded,
                   std::uint32_t idxNeeded, const GeometryCache& cache,
                   const DeferFn& defer);
    // Ensure `buf` holds ≥ `bytes` (recreate + defer old). Sets `recreated`.
    bool EnsureTable(rhi::Device& dev, rhi::Buffer& buf, VkDeviceSize bytes,
                     const DeferFn& defer, bool& recreated);

    // Pools (device-local) + bump allocation state.
    rhi::Buffer   vertexPool_, indexPool_;
    std::uint32_t vtxUsed_ = 0, vtxCap_ = 0;
    std::uint32_t idxUsed_ = 0, idxCap_ = 0;
    std::unordered_map<std::uint64_t, MeshRange> resident_;

    // Global style tables (device-local) + drawable → item index map.
    rhi::Buffer itemTable_, paintTable_;
    std::vector<std::uint32_t> itemOfDrawable_;

    // Per-frame staged uploads.
    std::vector<std::uint8_t> pendingData_;
    std::vector<PendingCopy>  pendingCopies_;
    rhi::Buffer               staging_[kStagingRing];
};

} // namespace Ink

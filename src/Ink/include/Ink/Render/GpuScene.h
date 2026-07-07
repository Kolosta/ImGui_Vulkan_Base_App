#pragma once

#include "Ink/Core/Math.h"
#include "Ink/RHI/Resources.h"
#include <cstdint>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  GpuScene — the persistent GPU residency of the scene
//  (docs/Ink/RENDER_GRAPH.md §3): vertex/index pools, the instance/item/paint
//  tables (SSBOs) and the prebuilt indirect draw commands.
//
//  A DRAW is one VkDrawIndexedIndirectCommand per batch: a mesh range drawn
//  for a contiguous run of instance records. The vertex shader chases
//  instance → item → paint, so one object or 10 000 instances is the same
//  code path — the instancing claim of the spec, live from Lot 1.
//
//  Lot 1 feeds it a static demo scene (UploadStatic, built by DemoScene.cpp).
//  The Scene compiler (Lot 2) replaces that with incremental dirty-range
//  updates behind this same data model.
// ─────────────────────────────────────────────────────────────────────────────

// GPU-facing records. Layouts match the std430 structs in vector.vert —
// 4-byte scalars only, no implicit padding.
struct ContentVertex {
    float x = 0.0f, y = 0.0f;   // definition-local units
};

struct InstanceRecord {
    float m[6];                 // row-major 2×3: local → document
    std::uint32_t itemIndex = 0;
    std::uint32_t pad_      = 0;
};

struct ItemRecord {
    std::uint32_t paintIndex = 0;
    std::uint32_t flags      = 0;
    std::uint32_t pad_[2]    = { 0, 0 };
};

struct PaintRecord {
    float rgba[4] = { 0, 0, 0, 1 };   // linear, premultiplied
};

// A contiguous mesh range in the pools.
struct MeshRange {
    std::uint32_t firstIndex  = 0;
    std::uint32_t indexCount  = 0;
    std::int32_t  vertexOffset = 0;
};

// One indirect draw: a mesh range × a run of instances.
struct Batch {
    MeshRange     mesh;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
};

class GpuScene {
public:
    bool Initialize(rhi::Device& dev);
    void Shutdown(rhi::Device& dev);

    // Upload a full static scene (Lot 1 demo path). Builds the pools, tables
    // and the indirect command buffer in one shot.
    bool UploadStatic(rhi::Device& dev,
                      const std::vector<ContentVertex>&  vertices,
                      const std::vector<std::uint32_t>&  indices,
                      const std::vector<InstanceRecord>& instances,
                      const std::vector<ItemRecord>&     items,
                      const std::vector<PaintRecord>&    paints,
                      const std::vector<Batch>&          batches,
                      Rect bounds);

    const rhi::Buffer& VertexPool()     const { return vertexPool_; }
    const rhi::Buffer& IndexPool()      const { return indexPool_; }
    const rhi::Buffer& InstanceTable()  const { return instanceTable_; }
    const rhi::Buffer& ItemTable()      const { return itemTable_; }
    const rhi::Buffer& PaintTable()     const { return paintTable_; }
    const rhi::Buffer& IndirectBuffer() const { return indirect_; }

    std::uint32_t BatchCount()    const { return batchCount_; }
    std::uint32_t InstanceCount() const { return instanceCount_; }
    std::uint32_t TriangleCount() const { return triangleCount_; }
    // Content version: mixed into view signatures so a scene change re-renders
    // every view (bumped by each upload).
    std::uint64_t Version()       const { return version_; }
    Rect          Bounds()        const { return bounds_; }

private:
    rhi::Buffer vertexPool_, indexPool_;
    rhi::Buffer instanceTable_, itemTable_, paintTable_;
    rhi::Buffer indirect_;
    std::uint32_t batchCount_    = 0;
    std::uint32_t instanceCount_ = 0;
    std::uint32_t triangleCount_ = 0;
    std::uint64_t version_       = 0;
    Rect          bounds_{};
};

// Lot 1 demo content (DemoScene.cpp): a page substrate, a dozen filled +
// "stroked" shapes and a 1 000-instance grid, all through the real pools.
bool UploadDemoScene(rhi::Device& dev, GpuScene& scene);

} // namespace Ink

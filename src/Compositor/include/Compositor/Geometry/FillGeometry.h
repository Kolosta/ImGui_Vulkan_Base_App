#pragma once

#include "Renderer/Document/Document.h"
#include "Renderer/Tessellation/Tessellator.h"   // Renderer::Tessellator::PagePlacement
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - Geometry/FillGeometry: stencil-then-cover fill geometry.
//
//  The Compositor's own base-fill geometry, built WITHOUT ear-clipping (the CPU
//  bottleneck the legacy Tessellator hits when editing heavy objects). A closed
//  filled region is NOT triangulated; it is drawn by the "stencil-then-cover"
//  method (NV path rendering / Skia): the flattened contour is fanned trivially
//  from a pivot into the stencil buffer under a non-zero winding rule (front
//  faces INCR_WRAP, back faces DECR_WRAP — overlaps and holes resolve by
//  winding), then a bounding quad is drawn once with the fill colour where the
//  stencil is non-zero. No O(n^2) triangulation, so editing a very heavy path is
//  flat-cost on the CPU (just the flatten, which moves to a compute shader later).
//
//  This module deliberately flattens the SAME way the legacy Tessellator does
//  (it reuses the Tessellator's PURE flatten helpers — OutlinePartSub*, not its
//  ear-clip or its Vulkan renderer) so the two engines agree on curve sampling
//  during the transition. Lot 13-4b lifts the flatten onto the GPU and drops the
//  dependency entirely (Lot 13-5).
//
//  Output is WORLD doc-units, page-local origin already applied, matching the
//  base vertex stream's coordinate space — the same shape.vert camera projects
//  both. Only SOLID fills go through here; patterns / strokes / decorators still
//  travel the Tessellator's cover/instance streams for now (migrated in a later
//  sub-lot). An object with no solid fill contributes no FillObject.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

// A stencil-then-cover fan vertex: position only (world doc-units). The colour is
// a per-object push constant on the cover quad, not per vertex, so the fan carries
// no colour. Matches fill_stencil.vert (location 0: vec2 pos).
struct FanVertex {
    float x, y;
};

// One filled object's coverage: a contiguous run of fan triangles in the build's
// fan-vertex stream (3 verts per triangle, non-indexed), plus the world bbox for
// the cover quad + scissor, and the straight RGBA fill colour. The renderer writes
// the fan to the stencil (non-zero winding), then draws the bbox quad ONCE where
// the stencil != 0 with `color`. `stencilRef` is unused by the winding rule (it
// counts, not compares) but kept for parity with the existing stencil dance.
struct FillObject {
    uint64_t shapeId    = 0;   // owning object (picking / dirty tracking)
    uint32_t fanFirst   = 0;   // stencil fan verts, into the build's fan stream
    uint32_t fanCount   = 0;   // multiple of 3
    uint32_t coverFirst = 0;   // cover bbox quad (6 verts), into the SAME fan stream
    float    bbMinX = 0, bbMinY = 0;   // world doc-units
    float    bbMaxX = 0, bbMaxY = 0;
    float    r = 0, g = 0, b = 0, a = 1;   // straight RGBA (fill, layer opacity folded)
    // Incremental build flag (Lot 13-1b-3): true → this object was (re)flattened THIS
    // build, so fanFirst/coverFirst index the fresh scratch fan stream and the pool
    // must Write its slice. false → REUSED unchanged: fanFirst/coverFirst are already
    // POOL-relative (from a prior build), nothing was flattened, the pool just Touches
    // it. Set by BuildDocumentFills; consumed by the renderer's upload step.
    bool     fromScratch = true;
};

// One page's fill slice: the objects (document order) whose solid fills render on
// this page. Parallel to Tessellator::PageSeg (same page order), so the renderer
// pairs them for scissor + z-order. `pageIndex` indexes doc.artboards (-1 = the
// loose, page-less slice drawn unclipped).
struct FillPage {
    int                     pageIndex = -1;
    std::vector<FillObject> objects;
};

// Build the whole document's SOLID base-fill coverage into `outFans` (fan-vertex
// stream) + the returned per-page slices, WITHOUT ear-clipping. `zoom` drives the
// curve flatten detail (same quantised bucket the Tessellator uses, so pan/zoom
// within a bucket is free). `placements` overrides page display origin/visibility
// exactly like Tessellator::BuildDocumentSegmented; pass null to use each page's
// own pos and show all. `includeLoose` appends the page-less objects as the last
// FillPage (pageIndex -1). This is a pure CPU walk; it allocates only `outFans`
// and the slices, and never triangulates an interior.
// INCREMENTAL build (Lot 13-1b-3): only objects whose id is in `dirtyIds` (or absent
// from `prevById`) are (re)flattened into `outFans` (fromScratch=true); an unchanged
// object is COPIED from `prevById` verbatim (its pool-relative offsets preserved,
// fromScratch=false) and NOT flattened — so `tess` becomes O(dirty), not O(all).
// Pass `dirtyIds=nullptr` for a FULL rebuild (every object flattened; the default,
// unchanged behaviour). `prevById` maps Shape::id → last build's FillObject; on a
// full rebuild it is ignored. `outFans` only ever receives the dirty objects' data.
std::vector<FillPage> BuildDocumentFills(
    const Renderer::Document& doc,
    std::vector<FanVertex>& outFans,
    float zoom = 1.0f,
    const std::vector<Renderer::Tessellator::PagePlacement>* placements = nullptr,
    bool includeLoose = true,
    const std::unordered_set<uint64_t>* dirtyIds = nullptr,
    const std::unordered_map<uint64_t, FillObject>* prevById = nullptr);

} // namespace Comp

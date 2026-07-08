#pragma once

#include "Ink/Document/PathData.h"
#include "Ink/Document/Style.h"
#include <vector>

namespace Ink::geom {

// ─────────────────────────────────────────────────────────────────────────────
//  The geometry kernel (docs/Ink/GEOMETRY.md): PathData → polylines →
//  fill/stroke triangle meshes. Windowless, deterministic, unit-tested.
//  Everything runs in node-local space: doubles in, f32 vertices out (local
//  coordinates are small by construction — the world transform is applied
//  per instance on the GPU).
// ─────────────────────────────────────────────────────────────────────────────

// One flattened subpath (node-local doubles; `closed` mirrors the source).
struct Polyline {
    std::vector<DVec2> points;
    bool closed = false;
};

// Adaptive cubic-Bézier flattening: max chord-distance error ≤ `tolerance`
// (node-local units — derived from the view's zoom tier by the cache).
std::vector<Polyline> Flatten(const PathData& path, double tolerance);

// Triangle mesh in node-local f32 (positions only; paints are per-item).
struct Mesh {
    std::vector<float>         positions;   // x0,y0,x1,y1,…
    std::vector<std::uint32_t> indices;
    std::uint32_t VertexCount() const { return (std::uint32_t)positions.size() / 2; }
    bool Empty() const { return indices.empty(); }
};

// Fill triangulation with holes (ear clipping + hole bridging; subpaths are
// classified outer/hole by winding + containment under `rule`). Known v1
// limit (docs/Ink/GEOMETRY.md §1): self-intersecting subpaths are not
// resolved exactly.
Mesh TriangulateFill(const std::vector<Polyline>& polylines, FillRule rule);

// Full stroke tessellation (docs/Ink/GEOMETRY.md §2): Center/Inside/Outside
// alignment (open paths per the walk-direction rule), Butt/Round/Square caps,
// Miter(limit)/Round/Bevel joins, dash patterns. `tolerance` (node-local
// units) bounds the arc-flattening error of round caps/joins. Stroke width is
// taken as-is (WidthSpace resolution happens in the GeometryCache).
Mesh TessellateStroke(const std::vector<Polyline>& polylines,
                      const Stroke& stroke, double tolerance);

// AABB of the flattened points alone (style-independent; the caller inflates
// by stroke bands — used by view culling).
struct LocalBounds;
LocalBounds PolylineBounds(const std::vector<Polyline>& polylines);

// Conservative node-local bounds of the flattened path, inflated by the
// widest enabled stroke band.
struct LocalBounds {
    DVec2 min{ 0, 0 }, max{ 0, 0 };
    bool  valid = false;
};
LocalBounds ComputeBounds(const std::vector<Polyline>& polylines,
                          const Style& style);

// Signed polygon area (positive = counter-clockwise). Exposed for tests and
// the winding classification.
double SignedArea(const std::vector<DVec2>& ring);

// Boolean operation on two polygon sets (docs/Ink/GEOMETRY.md §Boolean).
// `subject` and `clip` are each a set of closed rings (outer + holes by
// winding). Returns the result rings. v1 uses Greiner–Hormann with a tiny
// perturbation on degenerate (vertex-on-edge) intersections; exact on
// non-degenerate input, robust (never hangs) otherwise.
enum class BoolOp { Union, Subtract, Intersect, Xor };
std::vector<std::vector<DVec2>>
BooleanPolygons(const std::vector<std::vector<DVec2>>& subject,
                const std::vector<std::vector<DVec2>>& clip, BoolOp op);

} // namespace Ink::geom

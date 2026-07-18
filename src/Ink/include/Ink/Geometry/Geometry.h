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
// Stroke MARKS (IOF_CORE_PLAN Phase A): a non-Neutral mark re-phases the dash
// run, and each of a mark's Fusion-mode OBJECTS is triangulated INTO this mesh
// (one drawing, one alpha — no double transparency, like the legacy ticks).
// Blend / Subtract objects are handled by the Scene (their own drawables).
// `source` (optional) is the un-flattened path, used only to resolve
// node-PINNED marks; polyline index == mark.sub.
Mesh TessellateStroke(const std::vector<Polyline>& polylines,
                      const Stroke& stroke, double tolerance,
                      const PathData* source = nullptr);

// The FIXED, fine node-local tolerance at which a mark's HOST spine is
// flattened for PLACEMENT (point + tangent along the curve). Fixed — not per
// tier — so a mark's objects land at exactly the same spot whatever the zoom,
// and so every mode (Fusion in the stroke mesh, Blend/Cut as their own
// drawables) places identically. Tangents are SMOOTHED over this spine
// (angle-interpolated per vertex), so it only bounds the POSITION error of a
// placed object against the (finer, per-tier) rendered stroke.
inline constexpr double kMarkPlaceTolerance = 0.01;

// The fixed sampling tolerance of a DERIVED Bend/Follow ring built by the
// Scene for a Blend/Subtract object (the Scene has no zoom tier; the ring is
// built once per scene compile — and PER FRAME while a mark ghost live-moves,
// so it must stay cheap to build AND to ear-clip). Fusion rings are built per
// tier with the tier's own tolerance instead — vector-exact at any zoom. A
// documented limit: a Blend/Cut ring reads faceted only at extreme zoom.
inline constexpr double kMarkRingTolerance = 0.005;

// The node-local, ORIGIN-CENTRED PARAMETRIC PathData of a primitive mark object
// (Circle → Ellipse, Rectangle → Rect, Diamond → Polygon) at its resolved size
// (`strokeWidth` resolves a percentage). Placed by MarkPlaceMatrix and
// re-tessellated per zoom tier by the GeometryCache — vector-exact at any zoom.
// Empty for an Instance (routed as a node instead).
PathData MarkPrimitiveShape(const MarkObject& obj, double strokeWidth);

// The LINE shape (a segment ACROSS the stroke), built RELATIVE to a frame
// whose origin is the placement point (already offset across by `offset`) and
// whose +y is the left normal. `halfLen` = half the line length, `halfThick`
// = half its thickness, `offset` = the resolved across-offset, `dir` = the
// SIDE direction sign (+1 left / −1 right; used even when |offset| is 0 so a
// 0 % Line still reaches to the right side of the line). `centred` straddles
// the stroke; otherwise the line STARTS at the offset point and reaches out by
// the full length in `dir`. `join` extends the near end back to the stroke.
// (The far-side CLIP is applied by the caller in node space along the path.)
PathData MarkLineShape(double halfLen, double halfThick, double offset,
                       double dir, bool centred, bool join);

// Clip a CONVEX polygon (node-local points) by a HALF-PLANE: keep the vertices
// on the side `keepNormal` points to, measured from the line through `lineP`.
// A single-plane Sutherland–Hodgman pass — used to cut a repeat Line's
// overflow at the path (the far side of the stroke).
std::vector<DVec2> ClipConvexHalfPlane(const std::vector<DVec2>& poly,
                                       DVec2 lineP, DVec2 keepNormal);

// Clip a polygon (node-local) so nothing crosses to the FAR side of the path —
// the cut follows the path CURVE exactly (vectorial), not a straight tangent.
// `path` is the flattened spine (node-local); `atArc` the placement's arc; the
// FAR side is `farSign` × the left normal (farSign = −dir). A far-side region is
// built from the real path over a local span and boolean-subtracted from the
// polygon; returns the resulting rings (possibly several). `ext` bounds the
// region (a few line extents). Falls back to the input on a degenerate path.
std::vector<std::vector<DVec2>>
ClipPolygonToPathSide(const std::vector<DVec2>& poly, const Polyline& path,
                      double atArc, double farSign, double ext);

// The node-local placement matrix of a mark on the (aligned) `spine` of subpath
// `mark.sub`: translate to the mark point (honouring side + resolved offset),
// rotate to the tangent + the object's own rotation, and — for Bend — shear
// along the tangent to lean with the local slope. `strokeWidth` resolves the
// offset. Tangents are sampled SMOOTHLY (angle-interpolated over the spine's
// vertices), so the frame turns continuously as the mark slides — no facet
// jumps. `bendHalfExtent` (> 0) overrides the along-curve half-length the Bend
// shear is measured over (used by INSTANCES, whose `size` is a scale factor,
// not a length). Returns identity if the spine is degenerate.
DMat23 MarkPlaceMatrix(const Polyline& spine, const StrokeMark& mark,
                       const MarkObject& obj, double strokeWidth,
                       double bendHalfExtent = -1.0);

// True for the mark bend modes that CURVE the outline along the line (Bend and
// Follow); Hard keeps a rigid placed primitive.
bool BendsAlongCurve(MarkBend b);

// Bend/Follow geometry: the primitive's outline placed point-by-point through
// the curve's smooth arc-length frame, so it truly bends with the line (a
// Follow rectangle's long edges curve; Bend keeps them straight between
// curve-placed corners). A ring in node-local space (already placed).
// `tolerance` bounds the sampling density (pass the zoom tier's tolerance for
// vector-smooth results at any zoom; ring size is hard-capped). Returns false
// for a Hard object or an Instance.
bool MarkFollowContour(const Polyline& spine, const StrokeMark& mark,
                       const MarkObject& obj, double strokeWidth,
                       double tolerance, std::vector<DVec2>& outRing);

// Bend/Follow an ARBITRARY path (an INSTANCE mark object's target) through
// the same smooth curve frame the primitive rings use: every flattened point
// (scaled by `targetScale`, spun by the object's rotation) maps u→arc step,
// v→local normal, so the geometry truly bends with the line — perpendicular
// bounding sides on Bend, fully curved edges on Follow (which additionally
// resamples straight segments). Returns the bent path in node-local space
// (closed flags preserved); empty for a Hard object or a degenerate spine.
PathData MarkBendPath(const Polyline& spine, const StrokeMark& mark,
                      const MarkObject& obj, double strokeWidth,
                      const PathData& target, double targetScale,
                      double tolerance);

// One placement of a repeat run: the arc position of the OBJECT CENTRE, its
// resolved across-the-line offset (node-local units, + = left normal), and the
// SIDE direction sign (+1 = left normal, −1 = right, 0 = centred). `dir` is
// non-zero even when the offset MAGNITUDE is 0 — a Line placement needs to
// know which way to reach out at a 0 % offset.
struct RepeatPlacement {
    double at = 0.0;
    double offset = 0.0;
    double dir = 0.0;
};
// Every object placement of `rep` along `spine` (subpath `sub` of the
// stroke): distribution (pitch / gap / count / density) + phase + groups,
// Inside/Outside resolved per placement (winding on closed subpaths, local
// curvature on open ones), and the stroke's Object/Between repeat-anchor
// marks re-phasing the run piecewise (whole steps stretched between anchors).
std::vector<RepeatPlacement> RepeatObjectPlacements(const Polyline& spine,
                                                    const Stroke& stroke,
                                                    const StrokeRepeat& rep,
                                                    int sub);

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
// `jitter` scales the degeneracy-breaking nudge (0 = base): a boolean CHAIN
// passes its step index so an operand that shares edges with an earlier
// step's result (already nudged by the same base constant) is displaced
// DIFFERENTLY — otherwise the coincidence reappears exactly. Result rings are
// closed, sliver-free and oriented by NESTING PARITY (outers CCW, holes CW),
// so they feed the NonZero fill and the aligned stroker directly.
std::vector<std::vector<DVec2>>
BooleanPolygons(const std::vector<std::vector<DVec2>>& subject,
                const std::vector<std::vector<DVec2>>& clip, BoolOp op,
                int jitter = 0);

// A non-destructive boolean PIPELINE evaluated at view tolerance: the render
// path re-runs the ops per zoom tier so the result stays vector-smooth at any
// zoom (the Scene keeps one coarse evaluation for picking/bounds only). The
// host and operands flatten at the SAME tolerance; each operand is expressed
// in host-local space via `rel`.
struct BoolStep {
    BoolOp          op = BoolOp::Union;
    const PathData* operand = nullptr;   // borrowed; valid until next compile
    DMat23          rel;                 // operand-local → host-local
};
struct BoolProgram {
    const PathData*       host = nullptr;
    std::vector<BoolStep> steps;
    std::uint64_t         hash = 0;      // host + operands + ops + rels
};
std::vector<Polyline> EvaluateBoolean(const BoolProgram& prog, double tolerance);

} // namespace Ink::geom

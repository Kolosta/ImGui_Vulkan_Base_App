#pragma once

#include "Ink/Document/Types.h"
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  PathData — the one geometry type (docs/Ink/DOCUMENT_MODEL.md §3): ordered
//  subpaths of cubic-Bézier anchors, in NODE-LOCAL double coordinates. No
//  Mesh/Curve split — a polygon is a path whose anchors carry no handles.
// ─────────────────────────────────────────────────────────────────────────────

// The editing semantics of an anchor's handles (how the editor constrains
// them); the model just stores the handles themselves.
enum class AnchorKind : std::uint8_t { Corner = 0, Smooth = 1, Symmetric = 2 };

// How a subpath interprets its anchors (the legacy/Blender spline types):
//   Bezier → anchors lie ON the curve with optional in/out cubic handles.
//   Nurbs  → anchors are CONTROL POINTS off the curve (a rational uniform
//            B-spline of degree orderU−1; handles ignored, weights apply).
//   Poly   → a straight polyline through the anchors (handles ignored).
enum class SplineType : std::uint8_t { Bezier = 0, Nurbs = 1, Poly = 2 };

struct Anchor {
    DVec2 pos;                    // node-local
    DVec2 in{ 0, 0 };             // incoming handle, RELATIVE to pos
    DVec2 out{ 0, 0 };            // outgoing handle, RELATIVE to pos
    bool  hasIn  = false;
    bool  hasOut = false;
    AnchorKind kind = AnchorKind::Corner;
    // NURBS rational weight (>1 pulls the curve toward this control point;
    // the classic conic forms — exact circles/arcs — use √2/2 corners).
    double weight = 1.0;
};

struct Subpath {
    std::vector<Anchor> anchors;
    bool closed = false;
    SplineType    spline = SplineType::Bezier;
    // NURBS parameters (spline == Nurbs): order = degree + 1, capped at the
    // control-point count. `nurbsEndpoint` clamps an OPEN curve to meet its
    // first/last control point; `nurbsBezier` gives interior knots full
    // multiplicity (the polygon acts as consecutive rational Bézier segments
    // — the exact-circle/arc form).
    std::uint8_t  orderU = 4;
    bool nurbsEndpoint = true;
    bool nurbsBezier   = false;
};

struct PathData {
    std::vector<Subpath> subpaths;

    bool Empty() const {
        for (const Subpath& s : subpaths)
            if (s.anchors.size() >= 2) return false;
        return true;
    }

    // Content hash — the GeometryCache key component (docs/Ink/GEOMETRY.md
    // §3): identical geometry shares one cache entry (and one GPU mesh),
    // whatever node it belongs to.
    std::uint64_t Hash() const {
        std::uint64_t h = 0xA11CE0FULL;
        for (const Subpath& s : subpaths) {
            h = HashBytes(&s.closed, sizeof s.closed, h);
            const std::uint8_t sf =
                (std::uint8_t)(((std::uint8_t)s.spline) |
                               (s.nurbsEndpoint ? 8 : 0) |
                               (s.nurbsBezier ? 16 : 0));
            h = HashBytes(&sf, 1, h);
            h = HashBytes(&s.orderU, 1, h);
            for (const Anchor& a : s.anchors) {
                h = HashDouble(a.pos.x, h); h = HashDouble(a.pos.y, h);
                if (a.hasIn)  { h = HashDouble(a.in.x, h);  h = HashDouble(a.in.y, h); }
                if (a.hasOut) { h = HashDouble(a.out.x, h); h = HashDouble(a.out.y, h); }
                const std::uint8_t flags =
                    (std::uint8_t)((a.hasIn ? 1 : 0) | (a.hasOut ? 2 : 0) |
                                   ((std::uint8_t)a.kind << 2));
                h = HashBytes(&flags, 1, h);
                if (s.spline == SplineType::Nurbs) h = HashDouble(a.weight, h);
            }
        }
        return h ? h : 1;
    }

    // Convenience builders (used by the demo document, benches and tests).
    static PathData Rect(double x, double y, double w, double hgt);
    static PathData Ellipse(double cx, double cy, double rx, double ry);
    static PathData Polygon(const std::vector<DVec2>& points, bool closed = true);
    // A rational-NURBS EXACT circle: the classic 8-point square hull, order 3,
    // full-multiplicity (Bézier) periodic knots, √2/2 corner weights.
    static PathData NurbsCircle(double cx, double cy, double r);
    // An open uniform NURBS path over `points` control points (order 4,
    // clamped ends).
    static PathData Nurbs(const std::vector<DVec2>& points, bool closed = false);
};

} // namespace Ink

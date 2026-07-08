#include "Ink/Scene/Picking.h"

#include "Ink/Geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Ink {
namespace {

// Invert an affine 2×3 (non-degenerate; identity fallback).
DMat23 InvertAffine(const DMat23& m) {
    const double det = m.m[0] * m.m[4] - m.m[1] * m.m[3];
    DMat23 r;
    if (std::abs(det) < 1e-18) return r;
    const double inv = 1.0 / det;
    r.m[0] =  m.m[4] * inv;
    r.m[1] = -m.m[1] * inv;
    r.m[3] = -m.m[3] * inv;
    r.m[4] =  m.m[0] * inv;
    r.m[2] = -(r.m[0] * m.m[2] + r.m[1] * m.m[5]);
    r.m[5] = -(r.m[3] * m.m[2] + r.m[4] * m.m[5]);
    return r;
}

// Largest row scale of the world transform (world units per local unit).
double WorldScale(const DMat23& m) {
    const double sx = std::sqrt(m.m[0] * m.m[0] + m.m[3] * m.m[3]);
    const double sy = std::sqrt(m.m[1] * m.m[1] + m.m[4] * m.m[4]);
    return std::max(sx, sy);
}

// Node-local control-point bounds (conservative: a cubic lies inside the
// convex hull of its control points).
DRect LocalBounds(const PathData& path) {
    DRect b;
    for (const Subpath& sp : path.subpaths)
        for (const Anchor& a : sp.anchors) {
            b.Grow(a.pos);
            if (a.hasIn)  b.Grow({ a.pos.x + a.in.x,  a.pos.y + a.in.y });
            if (a.hasOut) b.Grow({ a.pos.x + a.out.x, a.pos.y + a.out.y });
        }
    return b;
}

// Even-odd / non-zero point test over flattened polylines. Open subpaths are
// closed for the fill test (the fill of an open path paints as if closed).
bool FillHit(const std::vector<geom::Polyline>& polys, DVec2 p, FillRule rule) {
    int winding = 0;
    bool odd = false;
    for (const geom::Polyline& pl : polys) {
        const std::size_t n = pl.points.size();
        if (n < 3) continue;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const DVec2 a = pl.points[j], b = pl.points[i];
            if ((a.y <= p.y) != (b.y <= p.y)) {
                const double x = a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (x > p.x) {
                    odd = !odd;
                    winding += (b.y > a.y) ? 1 : -1;
                }
            }
        }
    }
    return rule == FillRule::EvenOdd ? odd : winding != 0;
}

// Squared distance from `p` to the segment ab.
double SegDist2(DVec2 p, DVec2 a, DVec2 b) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double wx = p.x - a.x, wy = p.y - a.y;
    const double vv = vx * vx + vy * vy;
    double t = vv > 0.0 ? (wx * vx + wy * vy) / vv : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = wx - t * vx, dy = wy - t * vy;
    return dx * dx + dy * dy;
}

bool StrokeHit(const std::vector<geom::Polyline>& polys, DVec2 p, double halfWidth) {
    const double r2 = halfWidth * halfWidth;
    for (const geom::Polyline& pl : polys) {
        const std::size_t n = pl.points.size();
        if (n < 2) continue;
        const std::size_t last = pl.closed ? n : n - 1;
        for (std::size_t i = 0; i < last; ++i)
            if (SegDist2(p, pl.points[i], pl.points[(i + 1) % n]) <= r2)
                return true;
    }
    return false;
}

} // namespace

NodeId PickTop(const Scene& scene, DVec2 point, const PickOptions& opt) {
    const auto& drawables = scene.Drawables();
    for (auto it = drawables.rbegin(); it != drawables.rend(); ++it) {
        const Drawable& d = *it;
        if (d.isClipSource) continue;
        if (d.owner == kNullNode) continue;   // page substrate — not an object
        if (!d.path || d.path->Empty()) continue;

        const double scale = WorldScale(d.world);
        if (scale < 1e-18) continue;
        const double localTol = opt.tolerance / scale;

        // Stroke half-width in LOCAL units. Document widths are local by
        // definition; Viewport widths are pixels → doc units through the
        // zoom, then local units through the world scale.
        double halfWidth = 0.0;
        if (d.isStroke) {
            if (d.stroke.widthSpace == WidthSpace::Document)
                halfWidth = d.stroke.width * 0.5;
            else if (opt.zoom > 0.0)
                halfWidth = d.stroke.width / opt.zoom / scale * 0.5;
        }

        // Quick reject on the local control-point box.
        const DVec2 local = InvertAffine(d.world).Apply(point);
        DRect box = LocalBounds(*d.path);
        box.Inflate(halfWidth + localTol);
        if (!box.Contains(local)) continue;

        // Exact test on the flattened outline. Flatten tolerance tracks the
        // pick tolerance (finer than it, floored to stay cheap).
        const double flatTol = std::clamp(localTol * 0.5, 0.01, 1.0);
        const auto polys = geom::Flatten(*d.path, flatTol);
        const bool hit = d.isStroke
            ? StrokeHit(polys, local, halfWidth + localTol)
            : FillHit(polys, local, d.rule);
        if (hit) return d.owner;
    }
    return kNullNode;
}

std::vector<NodeId> PickBox(const Scene& scene, DVec2 boxMin, DVec2 boxMax) {
    DRect box;
    box.Grow(boxMin);
    box.Grow(boxMax);
    std::vector<NodeId> out;
    std::unordered_set<NodeId> seen;
    for (const Drawable& d : scene.Drawables()) {
        if (d.isClipSource || d.owner == kNullNode) continue;
        if (!seen.insert(d.owner).second) continue;
        DRect nb;
        if (scene.NodeBounds(d.owner, nb) && box.Intersects(nb))
            out.push_back(d.owner);
    }
    return out;
}

} // namespace Ink

#include "Ink/Geometry/Geometry.h"

#include <cmath>

namespace Ink::geom {

namespace {

// Distance from point p to the segment a–b.
double DistToSegment(DVec2 p, DVec2 a, DVec2 b) {
    const double abx = b.x - a.x, aby = b.y - a.y;
    const double len2 = abx * abx + aby * aby;
    double t = 0.0;
    if (len2 > 0.0)
        t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

// Recursive de Casteljau subdivision of the cubic (p0,c1,c2,p3): emit points
// (excluding p0) until the control polygon is within `tol` of the chord.
void FlattenCubic(DVec2 p0, DVec2 c1, DVec2 c2, DVec2 p3, double tol,
                  int depth, std::vector<DVec2>& out) {
    const double err = std::max(DistToSegment(c1, p0, p3),
                                DistToSegment(c2, p0, p3));
    if (err <= tol || depth >= 24) {
        out.push_back(p3);
        return;
    }
    auto mid = [](DVec2 a, DVec2 b) {
        return DVec2{ (a.x + b.x) * 0.5, (a.y + b.y) * 0.5 };
    };
    const DVec2 p01 = mid(p0, c1), p12 = mid(c1, c2), p23 = mid(c2, p3);
    const DVec2 p012 = mid(p01, p12), p123 = mid(p12, p23);
    const DVec2 p0123 = mid(p012, p123);
    FlattenCubic(p0, p01, p012, p0123, tol, depth + 1, out);
    FlattenCubic(p0123, p123, p23, p3, tol, depth + 1, out);
}

// Emit the segment a→b (line or cubic) into `out`, excluding the start point.
void FlattenSegment(const Anchor& a, const Anchor& b, double tol,
                    std::vector<DVec2>& out) {
    if (!a.hasOut && !b.hasIn) {
        out.push_back(b.pos);
        return;
    }
    const DVec2 c1 = a.hasOut ? DVec2{ a.pos.x + a.out.x, a.pos.y + a.out.y } : a.pos;
    const DVec2 c2 = b.hasIn  ? DVec2{ b.pos.x + b.in.x,  b.pos.y + b.in.y }  : b.pos;
    FlattenCubic(a.pos, c1, c2, b.pos, tol, 0, out);
}

} // namespace

std::vector<Polyline> Flatten(const PathData& path, double tolerance) {
    const double tol = tolerance > 0.0 ? tolerance : 1e-3;
    std::vector<Polyline> out;
    for (const Subpath& sp : path.subpaths) {
        if (sp.anchors.size() < 2) continue;
        Polyline pl;
        pl.closed = sp.closed;
        pl.points.push_back(sp.anchors.front().pos);
        for (std::size_t i = 1; i < sp.anchors.size(); ++i)
            FlattenSegment(sp.anchors[i - 1], sp.anchors[i], tol, pl.points);
        if (sp.closed)
            FlattenSegment(sp.anchors.back(), sp.anchors.front(), tol, pl.points);
        // A closed polyline keeps first != last (the seam segment's end point
        // duplicates the start — drop it).
        if (pl.closed && pl.points.size() > 1) {
            const DVec2 f = pl.points.front(), l = pl.points.back();
            if (std::abs(f.x - l.x) < 1e-12 && std::abs(f.y - l.y) < 1e-12)
                pl.points.pop_back();
        }
        if (pl.points.size() >= 2) out.push_back(std::move(pl));
    }
    return out;
}

double SignedArea(const std::vector<DVec2>& ring) {
    double a = 0.0;
    const std::size_t n = ring.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        a += (ring[j].x * ring[i].y - ring[i].x * ring[j].y);
    return a * 0.5;
}

LocalBounds ComputeBounds(const std::vector<Polyline>& polylines,
                          const Style& style) {
    LocalBounds b;
    for (const Polyline& pl : polylines) {
        for (const DVec2& p : pl.points) {
            if (!b.valid) { b.min = b.max = p; b.valid = true; continue; }
            b.min.x = std::min(b.min.x, p.x); b.min.y = std::min(b.min.y, p.y);
            b.max.x = std::max(b.max.x, p.x); b.max.y = std::max(b.max.y, p.y);
        }
    }
    if (!b.valid) return b;
    double inflate = 0.0;
    for (const Stroke& s : style.strokes)
        if (s.enabled) inflate = std::max(inflate, s.width);   // full width:
    // covers Center (w/2) and the Lot 3 Inside/Outside bands (w) alike.
    b.min.x -= inflate; b.min.y -= inflate;
    b.max.x += inflate; b.max.y += inflate;
    return b;
}

} // namespace Ink::geom

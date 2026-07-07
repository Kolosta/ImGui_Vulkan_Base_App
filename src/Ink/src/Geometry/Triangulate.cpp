#include "Ink/Geometry/Geometry.h"

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Fill triangulation (docs/Ink/GEOMETRY.md §1).
//
//  1. Classify the flattened subpaths into OUTER rings and their HOLES from
//     winding + containment depth under the fill rule.
//  2. For each outer+holes group, eliminate the holes by bridging each hole's
//     rightmost vertex to a visible outer vertex (two duplicated vertices per
//     bridge), producing one simple polygon.
//  3. Ear-clip that polygon (doubly-linked ring; an ear = a convex corner
//     whose triangle contains no other reflex vertex).
//
//  Robustness stance (v1): degenerate input (duplicate points, zero-area
//  spikes) is filtered; if ear clipping stalls on a residual non-simple
//  polygon it force-cuts to guarantee termination — a self-intersecting
//  subpath renders approximately (documented limit) instead of hanging.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::geom {
namespace {

constexpr double kEps = 1e-12;

double Cross(DVec2 o, DVec2 a, DVec2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool PointInTriangle(DVec2 p, DVec2 a, DVec2 b, DVec2 c) {
    const double d1 = Cross(a, b, p);
    const double d2 = Cross(b, c, p);
    const double d3 = Cross(c, a, p);
    const bool hasNeg = (d1 < -kEps) || (d2 < -kEps) || (d3 < -kEps);
    const bool hasPos = (d1 > kEps) || (d2 > kEps) || (d3 > kEps);
    return !(hasNeg && hasPos);
}

// Even-odd point-in-polygon (containment classification between rings).
bool PointInRing(DVec2 p, const std::vector<DVec2>& ring) {
    bool in = false;
    const std::size_t n = ring.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const DVec2 a = ring[i], b = ring[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            in = !in;
    }
    return in;
}

// Drop consecutive duplicates and collinear spikes.
std::vector<DVec2> CleanRing(const std::vector<DVec2>& in) {
    std::vector<DVec2> out;
    out.reserve(in.size());
    for (const DVec2& p : in) {
        if (!out.empty() && std::abs(out.back().x - p.x) < kEps &&
            std::abs(out.back().y - p.y) < kEps)
            continue;
        out.push_back(p);
    }
    while (out.size() > 1 && std::abs(out.front().x - out.back().x) < kEps &&
           std::abs(out.front().y - out.back().y) < kEps)
        out.pop_back();
    return out;
}

// ── Ear clipping over an index-linked ring ───────────────────────────────────

struct EarRing {
    const std::vector<DVec2>*  pts;
    std::vector<std::uint32_t> next, prev;   // linked ring over point indices
    std::vector<std::uint32_t> map;          // ring index → output vertex index
};

bool IsEar(const EarRing& r, std::uint32_t i) {
    const auto& P = *r.pts;
    const DVec2 a = P[r.prev[i]], b = P[i], c = P[r.next[i]];
    if (Cross(a, b, c) <= kEps) return false;   // reflex or degenerate (CCW ring)
    // No other ring vertex inside the candidate triangle. Vertices that
    // COINCIDE with a corner are skipped — hole bridging duplicates its two
    // bridge endpoints, and a duplicate sitting exactly on a/b/c must not
    // veto the ear (it lies on the boundary, not inside).
    auto coincides = [](DVec2 p, DVec2 q) {
        return std::abs(p.x - q.x) < kEps && std::abs(p.y - q.y) < kEps;
    };
    for (std::uint32_t j = r.next[r.next[i]]; j != r.prev[i]; j = r.next[j]) {
        const DVec2 p = P[j];
        if (coincides(p, a) || coincides(p, b) || coincides(p, c)) continue;
        if (PointInTriangle(p, a, b, c)) return false;
    }
    return true;
}

void EarClip(EarRing& r, std::uint32_t start, std::uint32_t count, Mesh& out) {
    std::uint32_t remaining = count;
    std::uint32_t i = start;
    std::uint32_t sinceLastEar = 0;
    while (remaining > 3) {
        if (IsEar(r, i)) {
            out.indices.push_back(r.map[r.prev[i]]);
            out.indices.push_back(r.map[i]);
            out.indices.push_back(r.map[r.next[i]]);
            r.next[r.prev[i]] = r.next[i];
            r.prev[r.next[i]] = r.prev[i];
            i = r.next[i];
            --remaining;
            sinceLastEar = 0;
        } else {
            i = r.next[i];
            if (++sinceLastEar > remaining) {
                // Non-simple residue (self-intersection): force a cut so the
                // loop terminates — approximate output beats a hang (v1).
                out.indices.push_back(r.map[r.prev[i]]);
                out.indices.push_back(r.map[i]);
                out.indices.push_back(r.map[r.next[i]]);
                r.next[r.prev[i]] = r.next[i];
                r.prev[r.next[i]] = r.prev[i];
                i = r.next[i];
                --remaining;
                sinceLastEar = 0;
            }
        }
    }
    out.indices.push_back(r.map[r.prev[i]]);
    out.indices.push_back(r.map[i]);
    out.indices.push_back(r.map[r.next[i]]);
}

// ── Hole bridging ────────────────────────────────────────────────────────────
// Merge `hole` (CW) into `poly` (CCW) by connecting the hole's rightmost
// vertex M to a visible vertex of `poly` (ray cast +x from M), duplicating
// both endpoints — the classic two-way bridge.
void BridgeHole(std::vector<DVec2>& poly, const std::vector<DVec2>& hole) {
    if (hole.size() < 3) return;

    std::size_t mIdx = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
        if (hole[i].x > hole[mIdx].x) mIdx = i;
    const DVec2 M = hole[mIdx];

    // Nearest +x intersection of the ray from M with poly's edges.
    double bestX = 1e300;
    std::size_t bestEdge = (std::size_t)-1;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const DVec2 a = poly[j], b = poly[i];
        if ((a.y > M.y) == (b.y > M.y)) continue;
        const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x >= M.x - kEps && x < bestX) { bestX = x; bestEdge = j; }
    }
    // Bridge target: the endpoint of the hit edge with the larger x (visible
    // side); fall back to the globally nearest vertex right of M.
    std::size_t target;
    if (bestEdge != (std::size_t)-1) {
        const std::size_t e0 = bestEdge, e1 = (bestEdge + 1) % n;
        target = (poly[e0].x > poly[e1].x) ? e0 : e1;
    } else {
        target = 0;
        double best = 1e300;
        for (std::size_t i = 0; i < n; ++i) {
            const double dx = poly[i].x - M.x, dy = poly[i].y - M.y;
            const double d = dx * dx + dy * dy;
            if (d < best) { best = d; target = i; }
        }
    }

    // Splice: poly[0..target], M, hole[m+1..], hole[..m], M', target'.
    std::vector<DVec2> merged;
    merged.reserve(poly.size() + hole.size() + 2);
    for (std::size_t i = 0; i <= target; ++i) merged.push_back(poly[i]);
    for (std::size_t k = 0; k <= hole.size(); ++k)
        merged.push_back(hole[(mIdx + k) % hole.size()]);   // M … around … M
    merged.push_back(poly[target]);
    for (std::size_t i = target + 1; i < poly.size(); ++i) merged.push_back(poly[i]);
    poly.swap(merged);
}

} // namespace

Mesh TriangulateFill(const std::vector<Polyline>& polylines, FillRule rule) {
    Mesh out;

    // Closed, cleaned rings only.
    struct Ring {
        std::vector<DVec2> pts;
        double area   = 0.0;     // signed
        int    depth  = 0;       // containment depth among the other rings
        int    parent = -1;      // innermost containing ring
        bool   isHole = false;
    };
    std::vector<Ring> rings;
    for (const Polyline& pl : polylines) {
        if (!pl.closed) continue;
        Ring r;
        r.pts = CleanRing(pl.points);
        if (r.pts.size() < 3) continue;
        r.area = SignedArea(r.pts);
        if (std::abs(r.area) < kEps) continue;
        rings.push_back(std::move(r));
    }
    if (rings.empty()) return out;

    // Containment depth + innermost parent (representative-point test — exact
    // for non-crossing rings, the v1 contract).
    for (std::size_t i = 0; i < rings.size(); ++i) {
        double parentArea = 1e300;
        for (std::size_t j = 0; j < rings.size(); ++j) {
            if (i == j) continue;
            if (std::abs(rings[j].area) <= std::abs(rings[i].area)) continue;
            if (PointInRing(rings[i].pts[0], rings[j].pts)) {
                ++rings[i].depth;
                if (std::abs(rings[j].area) < parentArea) {
                    parentArea = std::abs(rings[j].area);
                    rings[i].parent = (int)j;
                }
            }
        }
    }
    // Hole classification. EvenOdd: odd depth = hole. NonZero: a ring whose
    // winding OPPOSES its parent's is a hole; same-winding nesting re-fills.
    for (Ring& r : rings) {
        if (rule == FillRule::EvenOdd) {
            r.isHole = (r.depth % 2) == 1;
        } else {
            r.isHole = r.parent >= 0 &&
                       ((r.area > 0.0) != (rings[(std::size_t)r.parent].area > 0.0)) &&
                       !rings[(std::size_t)r.parent].isHole;
        }
    }
    // NonZero second pass: a "hole" whose parent is itself a hole re-fills.
    if (rule == FillRule::NonZero) {
        for (Ring& r : rings)
            if (r.isHole && r.parent >= 0 && rings[(std::size_t)r.parent].isHole)
                r.isHole = false;
    }

    // Group each outer with its direct holes, bridge, then ear-clip.
    for (std::size_t i = 0; i < rings.size(); ++i) {
        if (rings[i].isHole) continue;

        // Outer → CCW.
        std::vector<DVec2> poly = rings[i].pts;
        if (rings[i].area < 0.0) std::reverse(poly.begin(), poly.end());

        for (std::size_t j = 0; j < rings.size(); ++j) {
            if (!rings[j].isHole || rings[j].parent != (int)i) continue;
            std::vector<DVec2> hole = rings[j].pts;
            if (SignedArea(hole) > 0.0) std::reverse(hole.begin(), hole.end());
            BridgeHole(poly, hole);
        }

        if (poly.size() < 3) continue;
        const std::uint32_t base = out.VertexCount();
        for (const DVec2& p : poly) {
            out.positions.push_back((float)p.x);
            out.positions.push_back((float)p.y);
        }
        EarRing ring;
        ring.pts = &poly;
        const std::uint32_t n = (std::uint32_t)poly.size();
        ring.next.resize(n);
        ring.prev.resize(n);
        ring.map.resize(n);
        for (std::uint32_t k = 0; k < n; ++k) {
            ring.next[k] = (k + 1) % n;
            ring.prev[k] = (k + n - 1) % n;
            ring.map[k]  = base + k;
        }
        EarClip(ring, 0, n, out);
    }
    return out;
}

} // namespace Ink::geom

#include "Ink/Geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
//  Polygon boolean operations (docs/Ink/GEOMETRY.md §Boolean) via edge
//  splitting + inside/outside classification + re-chaining — a robust,
//  verifiable v1:
//
//   1. Split every edge of both polygons at all pairwise intersections, so no
//      two edges cross in the interior any more (they only meet at endpoints).
//   2. Keep each resulting directed edge by the operation's rule, tested at the
//      edge MIDPOINT against the other polygon (even-odd inside test):
//        Union     : keep A-edges outside B, B-edges outside A
//        Intersect : keep A-edges inside  B, B-edges inside  A
//        Subtract  : keep A-edges outside B, and B-edges inside A REVERSED
//        Xor       : Subtract(A,B) ∪ Subtract(B,A)
//   3. Chain the kept edges head-to-tail into closed rings.
//
//  Exact on non-degenerate input; degenerate coincidences (an edge exactly on
//  an edge) are a documented approximation. Never hangs.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::geom {
namespace {

constexpr double kEps = 1e-7;

using Poly  = std::vector<DVec2>;
using Polys = std::vector<Poly>;

bool NearlyEqual(DVec2 a, DVec2 b) {
    return std::abs(a.x - b.x) < kEps && std::abs(a.y - b.y) < kEps;
}

// Even-odd point-in-polygon over a set of rings.
bool Inside(DVec2 p, const Polys& poly) {
    bool in = false;
    for (const Poly& ring : poly) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const DVec2 a = ring[i], b = ring[j];
            if (((a.y > p.y) != (b.y > p.y)) &&
                (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
                in = !in;
        }
    }
    return in;
}

struct Edge { DVec2 a, b; };

// Signed area of a ring (shoelace; the sign is the ring's winding).
double RingArea(const Poly& r) {
    double a = 0.0;
    for (std::size_t i = 0, n = r.size(); i < n; ++i) {
        const DVec2& p = r[i];
        const DVec2& q = r[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

// Even-odd point-in-ring (one ring).
bool InsideSingle(DVec2 p, const Poly& ring) {
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

// Orient every ring by its NESTING PARITY: depth-even rings (outers, islands)
// get positive area, depth-odd rings (holes) negative. This is what the
// NonZero fill and the aligned stroker expect — and unlike a blanket
// "force everything CCW" it PRESERVES holes, which is exactly what a chained
// boolean feeds back in (the old normalisation turned a result's hole into a
// solid on the next step). The probe point sits slightly INSIDE its ring, off
// the longest edge, so rings that merely touch another ring's boundary (Xor
// crescents share the lens arc) still classify robustly.
void OrientByParity(Polys& rings) {
    if (rings.size() < 1) return;
    for (std::size_t i = 0; i < rings.size(); ++i) {
        Poly& r = rings[i];
        const std::size_t n = r.size();
        if (n < 3) continue;
        // Longest edge + its midpoint.
        std::size_t bi = 0;
        double bestLen = -1.0;
        for (std::size_t k = 0; k < n; ++k) {
            const DVec2 a = r[k], b = r[(k + 1) % n];
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double len = dx * dx + dy * dy;
            if (len > bestLen) { bestLen = len; bi = k; }
        }
        const DVec2 a = r[bi], b = r[(bi + 1) % n];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-12) continue;
        // Interior side of the walk: left of travel for a positive-area ring,
        // right otherwise (shoelace convention, orientation as stored NOW).
        const double sgn = RingArea(r) >= 0.0 ? 1.0 : -1.0;
        const double delta = std::max(len * 1e-7, 1e-9);
        const DVec2 probe{ (a.x + b.x) * 0.5 - sgn * (dy / len) * delta,
                           (a.y + b.y) * 0.5 + sgn * (dx / len) * delta };
        int depth = 0;
        for (std::size_t j = 0; j < rings.size(); ++j)
            if (j != i && InsideSingle(probe, rings[j])) ++depth;
        const bool wantPositive = (depth % 2) == 0;
        if ((RingArea(r) >= 0.0) != wantPositive)
            std::reverse(r.begin(), r.end());
    }
}

// Split every edge of `src` at its intersections with every edge of `other`.
std::vector<Edge> SplitEdges(const Polys& src, const Polys& other) {
    std::vector<Edge> out;
    for (const Poly& ring : src) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const DVec2 a = ring[i], b = ring[(i + 1) % n];
            // Collect split parameters t∈(0,1) along a→b.
            std::vector<double> ts;
            for (const Poly& oring : other) {
                const std::size_t m = oring.size();
                for (std::size_t k = 0; k < m; ++k) {
                    const DVec2 c = oring[k], d = oring[(k + 1) % m];
                    const double denom = (b.x - a.x) * (d.y - c.y) -
                                         (b.y - a.y) * (d.x - c.x);
                    if (std::abs(denom) < 1e-12) continue;
                    const double t = ((c.x - a.x) * (d.y - c.y) -
                                      (c.y - a.y) * (d.x - c.x)) / denom;
                    const double u = ((c.x - a.x) * (b.y - a.y) -
                                      (c.y - a.y) * (b.x - a.x)) / denom;
                    if (t > kEps && t < 1.0 - kEps && u > -kEps && u < 1.0 + kEps)
                        ts.push_back(t);
                }
            }
            std::sort(ts.begin(), ts.end());
            DVec2 prev = a;
            for (double t : ts) {
                const DVec2 mid{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
                if (!NearlyEqual(prev, mid)) out.push_back({ prev, mid });
                prev = mid;
            }
            if (!NearlyEqual(prev, b)) out.push_back({ prev, b });
        }
    }
    return out;
}

// Chain kept directed edges into closed rings by matching heads to tails.
// At a junction where several kept edges start (two boundaries crossing at an
// intersection vertex), prefer the edge that CONTINUES most straight ahead
// (smallest turn) — following the same boundary across the junction keeps the
// rings simple and avoids hopping onto the other polygon, which is what
// produced the "straight cut across a disc" artefacts.
//
// Only chains that actually CLOSE are emitted: an open chain (a junction whose
// twin endpoint was lost to numeric noise) rendered as if closed produces the
// classic "half-filled diagonal" garbage polygon. A small terminal gap (a
// junction split across two float paths) is snapped shut; anything larger is
// DROPPED — a missing sliver is invisible, a garbage ring is not.
Polys ChainEdges(std::vector<Edge> edges) {
    Polys rings;
    constexpr double kCloseEps = 1e-6;   // terminal-gap snap tolerance
    std::vector<bool> used(edges.size(), false);
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (used[start]) continue;
        Poly ring;
        std::size_t cur = start;
        DVec2 tail = edges[start].a;
        int guard = 0;
        while (cur != (std::size_t)-1 && !used[cur] && ++guard < 200000) {
            used[cur] = true;
            ring.push_back(edges[cur].a);
            tail = edges[cur].b;
            const DVec2 din{ edges[cur].b.x - edges[cur].a.x,
                             edges[cur].b.y - edges[cur].a.y };
            const double dinLen = std::sqrt(din.x * din.x + din.y * din.y);
            // Prefer the continuation with the SMALLEST absolute turn.
            std::size_t nxt = (std::size_t)-1;
            double bestAbsTurn = 1e300;
            for (std::size_t j = 0; j < edges.size(); ++j) {
                if (used[j] || !NearlyEqual(edges[j].a, tail)) continue;
                const DVec2 dout{ edges[j].b.x - edges[j].a.x,
                                  edges[j].b.y - edges[j].a.y };
                const double outLen = std::sqrt(dout.x*dout.x + dout.y*dout.y);
                double turn = 0.0;
                if (dinLen > 1e-18 && outLen > 1e-18)
                    turn = std::abs(std::atan2(
                        din.x * dout.y - din.y * dout.x,
                        din.x * dout.x + din.y * dout.y));
                if (nxt == (std::size_t)-1 || turn < bestAbsTurn) {
                    bestAbsTurn = turn; nxt = j;
                }
            }
            cur = nxt;
        }
        if (ring.size() < 3) continue;
        // Closed = the last tail returns to the ring start (within the snap
        // tolerance — the implicit closing edge bridges the residue).
        const double gx = tail.x - ring.front().x, gy = tail.y - ring.front().y;
        if (std::sqrt(gx * gx + gy * gy) > kCloseEps) continue;   // open: drop
        // Nudge-sized slivers carry no visible area — drop them too.
        if (std::abs(RingArea(ring)) < 1e-12) continue;
        rings.push_back(std::move(ring));
    }
    return rings;
}

// Keep an edge if its midpoint satisfies `wantInside` against `other`.
void KeepBy(const std::vector<Edge>& edges, const Polys& other,
            bool wantInside, bool reverse, std::vector<Edge>& out) {
    for (const Edge& e : edges) {
        const DVec2 mid{ (e.a.x + e.b.x) * 0.5, (e.a.y + e.b.y) * 0.5 };
        if (Inside(mid, other) == wantInside)
            out.push_back(reverse ? Edge{ e.b, e.a } : e);
    }
}

Polys BuildOp(const Polys& Araw, const Polys& Braw, BoolOp op) {
    // Orient both operands by NESTING PARITY (outers positive, holes
    // negative). The edge-keep + reverse-for-hole rules need a CONSISTENT
    // winding convention — the shape factories disagree (Rect/Polygon CCW,
    // Ellipse's arcs CW) — but a blanket force-CCW destroyed HOLES, which is
    // exactly what a chained boolean feeds back in as its next subject.
    Polys A = Araw; OrientByParity(A);
    Polys B = Braw; OrientByParity(B);
    const std::vector<Edge> ae = SplitEdges(A, B);
    const std::vector<Edge> be = SplitEdges(B, A);
    std::vector<Edge> kept;
    switch (op) {
    case BoolOp::Union:
        KeepBy(ae, B, false, false, kept);   // A outside B
        KeepBy(be, A, false, false, kept);   // B outside A
        break;
    case BoolOp::Intersect:
        KeepBy(ae, B, true, false, kept);    // A inside B
        KeepBy(be, A, true, false, kept);    // B inside A
        break;
    case BoolOp::Subtract:
        KeepBy(ae, B, false, false, kept);   // A outside B
        KeepBy(be, A, true, true, kept);     // B inside A, reversed (hole)
        break;
    case BoolOp::Xor: {
        Polys x = BuildOp(A, B, BoolOp::Subtract);
        Polys y = BuildOp(B, A, BoolOp::Subtract);
        x.insert(x.end(), y.begin(), y.end());
        return x;
    }
    }
    return ChainEdges(std::move(kept));
}

} // namespace

std::vector<std::vector<DVec2>>
BooleanPolygons(const std::vector<std::vector<DVec2>>& subject,
                const std::vector<std::vector<DVec2>>& clip, BoolOp op,
                int jitter) {
    if (subject.empty()) return op == BoolOp::Intersect ? Polys{} : clip;
    if (clip.empty())    return op == BoolOp::Intersect ? Polys{} : subject;
    // Break exact vertex-on-edge coincidences (a disc tangent to a rectangle
    // edge, snapped grids…) with a sub-visible deterministic nudge of the
    // clip set — the classic cure for the split/classify degeneracies. The
    // offset is far below any flattening tolerance AND below the tests'
    // area epsilon, so exact non-degenerate cases stay exact. `jitter`
    // (the chain's step index) scales it so an operand that shares edges
    // with an EARLIER step's result — itself already nudged by the base
    // constant — lands on a DIFFERENT offset instead of re-coinciding.
    Polys nudged = clip;
    const double scale = 1.0 + (double)std::max(0, jitter);
    const double nx = 1.180339887e-9 * scale, ny = 0.7071067811e-9 * scale;
    for (Poly& ring : nudged)
        for (DVec2& p : ring) { p.x += nx; p.y += ny; }
    Polys out = BuildOp(subject, nudged, op);
    // Final winding pass: outers CCW, holes CW — what the NonZero fill and
    // the aligned stroker consume, and what the next chained step expects.
    OrientByParity(out);
    return out;
}

std::vector<Polyline> EvaluateBoolean(const BoolProgram& prog,
                                      double tolerance) {
    std::vector<Polyline> out;
    if (!prog.host) return out;
    auto toRings = [](const std::vector<Polyline>& polys) {
        Polys rings;
        for (const Polyline& pl : polys)
            if (pl.closed && pl.points.size() >= 3) rings.push_back(pl.points);
        return rings;
    };
    Polys acc = toRings(Flatten(*prog.host, tolerance));
    for (std::size_t si = 0; si < prog.steps.size(); ++si) {
        const BoolStep& s = prog.steps[si];
        if (!s.operand) continue;
        Polys rings;
        for (Polyline& pl : Flatten(*s.operand, tolerance)) {
            if (!pl.closed || pl.points.size() < 3) continue;
            for (DVec2& p : pl.points) p = s.rel.Apply(p);
            rings.push_back(std::move(pl.points));
        }
        // The step index varies the degeneracy nudge: reusing the SAME
        // operand (or one sharing edges with an earlier result) must not
        // re-create the exact coincidence the base nudge just broke.
        acc = BooleanPolygons(acc, rings, s.op, (int)si);
        if (acc.empty()) break;
    }
    out.reserve(acc.size());
    for (auto& ring : acc) {
        Polyline pl;
        pl.points = std::move(ring);
        pl.closed = true;
        out.push_back(std::move(pl));
    }
    return out;
}

} // namespace Ink::geom

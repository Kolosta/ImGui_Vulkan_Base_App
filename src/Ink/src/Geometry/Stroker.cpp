#include "Ink/Geometry/Geometry.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Stroke tessellation (docs/Ink/GEOMETRY.md §2 — Lot 3, complete).
//
//  Pipeline per flattened subpath:
//    1. DASH SPLIT — the spine is cut by arc length into "on" pieces
//       (pattern + phase), so every dash carries real caps.
//    2. ALIGNMENT — Inside/Outside strokes become CENTER strokes of a spine
//       shifted by ±w/2 along per-vertex miter normals (clamped). Open paths:
//       Inside = the +normal side of the walk direction (the documented
//       right-hand rule); closed paths: the winding decides the interior.
//    3. CENTER STROKE — segment quads ± w/2, joins on the OUTER side of each
//       turn (Miter with limit → bevel fallback, Round fanned to `tolerance`,
//       Bevel), Butt/Round/Square caps on open ends.
//
//  Emitted with overlaps at tight geometry rather than exact outline unions
//  (documented v1 limit: a translucent stroke may darken locally).
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::geom {
namespace {

struct V2 { double x, y; };

V2 Sub(DVec2 a, DVec2 b) { return { a.x - b.x, a.y - b.y }; }
double Len(V2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
V2 Norm(V2 v) {
    const double l = Len(v);
    return l > 1e-12 ? V2{ v.x / l, v.y / l } : V2{ 0, 0 };
}
// 90° CCW rotation (the "left" normal in shoelace orientation).
V2 Perp(V2 d) { return { -d.y, d.x }; }
double Cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
double Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }

struct Emitter {
    Mesh* out;
    std::uint32_t V(double x, double y) {
        out->positions.push_back((float)x);
        out->positions.push_back((float)y);
        return out->VertexCount() - 1;
    }
    void Tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        out->indices.push_back(a);
        out->indices.push_back(b);
        out->indices.push_back(c);
    }
    // Fan an arc around `c` from direction a0 to a1 (radius r), sweeping the
    // SHORTER way whose sign is `sweepSign`. Chord error bounded by tol.
    void Arc(DVec2 c, V2 from, V2 to, double r, int sweepSign, double tol) {
        double a0 = std::atan2(from.y, from.x);
        double a1 = std::atan2(to.y, to.x);
        double sweep = a1 - a0;
        const double kTau = 6.28318530717958647692;
        while (sweep * sweepSign < 0.0)      sweep += sweepSign * kTau;
        while (sweep * sweepSign > kTau)     sweep -= sweepSign * kTau;
        const double err = tol > 0.0 ? tol : 0.25;
        double dphi = (r > err) ? 2.0 * std::acos(1.0 - err / r) : 1.0;
        int steps = (int)std::ceil(std::abs(sweep) / dphi);
        steps = steps < 1 ? 1 : (steps > 64 ? 64 : steps);
        const std::uint32_t centre = V(c.x, c.y);
        std::uint32_t prev = V(c.x + from.x * r, c.y + from.y * r);
        for (int i = 1; i <= steps; ++i) {
            const double a = a0 + sweep * (double)i / (double)steps;
            const std::uint32_t cur = V(c.x + std::cos(a) * r,
                                        c.y + std::sin(a) * r);
            Tri(centre, prev, cur);
            prev = cur;
        }
    }
};

// ── 1. Dash split ────────────────────────────────────────────────────────────
// Cut the spine into "on" pieces by arc length. Closed spines walk the seam
// once (the wrap point may split a dash — v1 seam behaviour).
std::vector<Polyline> DashSplit(const Polyline& pl,
                                const std::vector<double>& patternIn,
                                double offset) {
    std::vector<Polyline> out;
    std::vector<double> pattern;
    double period = 0.0;
    for (double d : patternIn)
        if (d > 1e-9) { pattern.push_back(d); period += d; }
    if (pattern.empty() || period <= 0.0) { out.push_back(pl); return out; }
    if (pattern.size() % 2 == 1) {   // SVG: odd counts repeat to even
        const std::size_t n = pattern.size();
        for (std::size_t i = 0; i < n; ++i) pattern.push_back(pattern[i]);
        period *= 2.0;
    }

    double phase = std::fmod(offset, period);
    if (phase < 0.0) phase += period;
    std::size_t seg = 0;                    // pattern entry (even = on)
    while (phase >= pattern[seg]) { phase -= pattern[seg]; seg = (seg + 1) % pattern.size(); }
    double remain = pattern[seg] - phase;   // length left in the current entry
    bool on = (seg % 2) == 0;

    Polyline cur;
    auto endPiece = [&]() {
        if (on && cur.points.size() >= 2) out.push_back(cur);
        cur.points.clear();
    };

    const std::size_t n = pl.points.size();
    const std::size_t segCount = pl.closed ? n : n - 1;
    DVec2 p = pl.points[0];
    if (on) cur.points.push_back(p);
    for (std::size_t s = 0; s < segCount; ++s) {
        DVec2 b = pl.points[(s + 1) % n];
        double segLen = Len(Sub(b, p));
        while (segLen > remain) {
            const double t = remain / segLen;
            const DVec2 cut{ p.x + (b.x - p.x) * t, p.y + (b.y - p.y) * t };
            if (on) { cur.points.push_back(cut); endPiece(); }
            else    { cur.points.clear(); cur.points.push_back(cut); }
            on = !on;
            p = cut;
            segLen -= remain;
            seg = (seg + 1) % pattern.size();
            remain = pattern[seg];
        }
        remain -= segLen;
        p = b;
        if (on) cur.points.push_back(p);
    }
    endPiece();
    return out;   // pieces are OPEN (caps close them visually)
}

// ── 2. Alignment: shift the spine by ±w/2 along clamped miter normals ───────
Polyline OffsetSpine(const Polyline& pl, double shift) {
    Polyline out;
    out.closed = pl.closed;
    const std::size_t n = pl.points.size();
    out.points.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        V2 nPrev{ 0, 0 }, nNext{ 0, 0 };
        if (i > 0 || pl.closed)
            nPrev = Perp(Norm(Sub(pl.points[i], pl.points[(i + n - 1) % n])));
        if (i + 1 < n || pl.closed)
            nNext = Perp(Norm(Sub(pl.points[(i + 1) % n], pl.points[i])));
        V2 m = Norm({ nPrev.x + nNext.x, nPrev.y + nNext.y });
        if (m.x == 0.0 && m.y == 0.0) m = (nNext.x || nNext.y) ? nNext : nPrev;
        // Miter scale 1/cos(θ/2), clamped ×4 (spikes are a rendering hazard).
        double cosHalf = Dot(m, (nNext.x || nNext.y) ? nNext : nPrev);
        cosHalf = cosHalf < 0.25 ? 0.25 : cosHalf;
        out.points.push_back({ pl.points[i].x + m.x * shift / cosHalf,
                               pl.points[i].y + m.y * shift / cosHalf });
    }
    return out;
}

// ── 3. Center stroke of one piece ────────────────────────────────────────────
void CenterStroke(const Polyline& pl, const Stroke& st, double halfW,
                  double tol, Emitter& em) {
    const std::size_t n = pl.points.size();
    if (n < 2 || halfW <= 0.0) return;
    const std::size_t segCount = pl.closed ? n : n - 1;

    // Per-segment unit direction + left normal.
    std::vector<V2> dir(segCount), nor(segCount);
    for (std::size_t s = 0; s < segCount; ++s) {
        dir[s] = Norm(Sub(pl.points[(s + 1) % n], pl.points[s]));
        nor[s] = Perp(dir[s]);
    }

    // Segment quads.
    for (std::size_t s = 0; s < segCount; ++s) {
        if (dir[s].x == 0.0 && dir[s].y == 0.0) continue;
        const DVec2 a = pl.points[s], b = pl.points[(s + 1) % n];
        const std::uint32_t aL = em.V(a.x + nor[s].x * halfW, a.y + nor[s].y * halfW);
        const std::uint32_t aR = em.V(a.x - nor[s].x * halfW, a.y - nor[s].y * halfW);
        const std::uint32_t bL = em.V(b.x + nor[s].x * halfW, b.y + nor[s].y * halfW);
        const std::uint32_t bR = em.V(b.x - nor[s].x * halfW, b.y - nor[s].y * halfW);
        em.Tri(aL, bL, bR);
        em.Tri(aL, bR, aR);
    }

    // Joins at interior vertices (and the seam of a closed piece): fill the
    // OUTER wedge between the adjacent segments' offset corners.
    const std::size_t firstJoin = pl.closed ? 0 : 1;
    for (std::size_t i = firstJoin; i < segCount; ++i) {
        const std::size_t sPrev = (i + segCount - 1) % segCount;
        const std::size_t sNext = i;
        const DVec2 p = pl.points[i];
        const double turn = Cross(dir[sPrev], dir[sNext]);
        if (std::abs(turn) < 1e-12) continue;   // straight — quads meet exactly
        // Outer side: left normal (+nor) when turning RIGHT (cross < 0).
        const double sign = turn < 0.0 ? 1.0 : -1.0;
        const V2 oPrev{ nor[sPrev].x * sign, nor[sPrev].y * sign };
        const V2 oNext{ nor[sNext].x * sign, nor[sNext].y * sign };
        const std::uint32_t cP = em.V(p.x + oPrev.x * halfW, p.y + oPrev.y * halfW);
        const std::uint32_t cN = em.V(p.x + oNext.x * halfW, p.y + oNext.y * halfW);
        const std::uint32_t cc = em.V(p.x, p.y);

        switch (st.join) {
        case JoinStyle::Round:
            // Sweep the SHORT way between the outer corners (never through
            // the stroke interior).
            em.Arc(p, oPrev, oNext, halfW,
                   Cross(oPrev, oNext) >= 0.0 ? 1 : -1, tol);
            break;
        case JoinStyle::Miter: {
            const V2 m = Norm({ oPrev.x + oNext.x, oPrev.y + oNext.y });
            const double cosHalf = Dot(m, oNext);
            if (cosHalf > 1e-6 && 1.0 / cosHalf <= st.miterLimit) {
                const std::uint32_t mp =
                    em.V(p.x + m.x * halfW / cosHalf, p.y + m.y * halfW / cosHalf);
                em.Tri(cc, cP, mp);
                em.Tri(cc, mp, cN);
                break;
            }
            [[fallthrough]];   // over the limit → bevel
        }
        case JoinStyle::Bevel:
        default:
            em.Tri(cc, cP, cN);
            break;
        }
    }

    // Caps on open ends.
    if (!pl.closed && st.cap != CapStyle::Butt) {
        auto cap = [&](DVec2 p, V2 d, V2 nrm) {   // d points OUT of the piece
            if (st.cap == CapStyle::Square) {
                const DVec2 q{ p.x + d.x * halfW, p.y + d.y * halfW };
                const std::uint32_t a = em.V(p.x + nrm.x * halfW, p.y + nrm.y * halfW);
                const std::uint32_t b = em.V(q.x + nrm.x * halfW, q.y + nrm.y * halfW);
                const std::uint32_t c = em.V(q.x - nrm.x * halfW, q.y - nrm.y * halfW);
                const std::uint32_t e = em.V(p.x - nrm.x * halfW, p.y - nrm.y * halfW);
                em.Tri(a, b, c);
                em.Tri(a, c, e);
            } else {   // Round: half-disc through the outward direction
                em.Arc(p, nrm, { -nrm.x, -nrm.y }, halfW,
                       Cross(nrm, d) > 0.0 ? 1 : -1, tol);
            }
        };
        cap(pl.points[0], { -dir[0].x, -dir[0].y }, nor[0]);
        cap(pl.points[n - 1], dir[segCount - 1], nor[segCount - 1]);
    }
}

} // namespace

LocalBounds PolylineBounds(const std::vector<Polyline>& polylines) {
    LocalBounds b;
    for (const Polyline& pl : polylines)
        for (const DVec2& p : pl.points) {
            if (!b.valid) { b.min = b.max = p; b.valid = true; continue; }
            b.min.x = std::min(b.min.x, p.x); b.min.y = std::min(b.min.y, p.y);
            b.max.x = std::max(b.max.x, p.x); b.max.y = std::max(b.max.y, p.y);
        }
    return b;
}

Mesh TessellateStroke(const std::vector<Polyline>& polylines,
                      const Stroke& stroke, double tolerance) {
    Mesh out;
    Emitter em{ &out };
    const double w = stroke.width;
    if (w <= 0.0) return out;
    const double tol = tolerance > 0.0 ? tolerance : 0.25;

    for (const Polyline& src : polylines) {
        if (src.points.size() < 2) continue;

        // Alignment → a center stroke on a shifted spine. Open paths: Inside
        // is the +normal (left in shoelace orientation = right-hand side on
        // the y-down canvas) — the documented walk-direction rule. Closed
        // paths: the winding decides which side the interior is.
        double side = 1.0;
        if (src.closed && stroke.align != StrokeAlign::Center)
            side = SignedArea(src.points) > 0.0 ? 1.0 : -1.0;
        double shift = 0.0;
        if (stroke.align == StrokeAlign::Inside)  shift =  side * w * 0.5;
        if (stroke.align == StrokeAlign::Outside) shift = -side * w * 0.5;

        const Polyline spine =
            (shift != 0.0) ? OffsetSpine(src, shift) : src;

        // Dashes cut the (aligned) spine into capped pieces.
        if (!stroke.dashPattern.empty()) {
            for (const Polyline& piece :
                 DashSplit(spine, stroke.dashPattern, stroke.dashOffset))
                CenterStroke(piece, stroke, w * 0.5, tol, em);
        } else {
            CenterStroke(spine, stroke, w * 0.5, tol, em);
        }
    }
    return out;
}

} // namespace Ink::geom

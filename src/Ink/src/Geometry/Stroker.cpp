#include "Ink/Geometry/Geometry.h"

#include <algorithm>
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
// The "on" (dash) INTERVALS in arc length over a spine of length `total` for a
// dash pattern + offset — the same phase DashSplit uses, but as [from,to]
// ranges so a gap can be subtracted from them WITHOUT re-phasing the pattern.
// An empty pattern → one interval [0,total] (solid).
std::vector<std::pair<double,double>>
DashIntervals(double total, const std::vector<double>& patternIn, double offset) {
    std::vector<std::pair<double,double>> out;
    std::vector<double> pattern;
    double period = 0.0;
    for (double d : patternIn) if (d > 1e-9) { pattern.push_back(d); period += d; }
    if (pattern.empty() || period <= 0.0 || total <= 0.0) {
        out.push_back({ 0.0, total }); return out;
    }
    if (pattern.size() % 2 == 1) {
        const std::size_t n = pattern.size();
        for (std::size_t i = 0; i < n; ++i) pattern.push_back(pattern[i]);
        period *= 2.0;
    }
    double phase = std::fmod(offset, period);
    if (phase < 0.0) phase += period;
    std::size_t seg = 0;
    while (phase >= pattern[seg]) { phase -= pattern[seg]; seg = (seg + 1) % pattern.size(); }
    double remain = pattern[seg] - phase;
    bool on = (seg % 2) == 0;
    double at = 0.0;
    while (at < total - 1e-9) {
        const double step = std::min(remain, total - at);
        if (on) out.push_back({ at, at + step });
        at += step;
        on = !on;
        seg = (seg + 1) % pattern.size();
        remain = pattern[seg];
    }
    return out;
}

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

// ── Marks: dash-phase re-phasing + Gap cuts (IOF_CORE_PLAN.md Phase A) ───────
// A mark's OBJECTS are emitted by the Scene; the stroker re-phases the dash run
// so a dash element / gap lands on a non-Neutral mark, and it CUTS the base
// line where a mark carries a Gap object (with the chosen end caps).

struct PhaseAnchor { double at; bool elementCentred; };
struct GapSpan { double from, to; GapCap capFrom, capTo; bool cutsObjects; };

// One stroked "on" interval [from,to] of the spine, each end tagged with its cap
// and whether it abuts a GAP (so the gap's cap wins over the stroke's own cap).
struct Iv { double from, to; GapCap capFrom, capTo; bool gapFrom, gapTo; };

double PolyTotal(const Polyline& pl) {
    const std::size_t n = pl.points.size();
    if (n < 2) return 0.0;
    const std::size_t sc = pl.closed ? n : n - 1;
    double total = 0.0;
    for (std::size_t i = 0; i < sc; ++i)
        total += Len(Sub(pl.points[(i + 1) % n], pl.points[i]));
    return total;
}

// Point + unit tangent at arc-length `d` along the polyline.
void ArcSampleAt(const Polyline& pl, double d, DVec2& outP, V2& outTan) {
    const std::size_t n = pl.points.size();
    const std::size_t sc = pl.closed ? n : n - 1;
    double acc = 0.0;
    for (std::size_t i = 0; i < sc; ++i) {
        const DVec2 a = pl.points[i], b = pl.points[(i + 1) % n];
        const double L = Len(Sub(b, a));
        if (L < 1e-12) continue;
        if (d <= acc + L) {
            const double u = (d - acc) / L;
            outP = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
            outTan = { (b.x - a.x) / L, (b.y - a.y) / L };
            return;
        }
        acc += L;
    }
    outP = pl.points[n - 1];
    outTan = Norm(Sub(pl.points[n - 1], pl.points[n >= 2 ? n - 2 : 0]));
}

// The OPEN sub-polyline of `pl` between arc lengths [from, to].
Polyline ExtractRun(const Polyline& pl, double from, double to) {
    Polyline run;
    const std::size_t n = pl.points.size();
    const std::size_t sc = pl.closed ? n : n - 1;
    double acc = 0.0;
    bool started = false;
    for (std::size_t i = 0; i < sc; ++i) {
        const DVec2 a = pl.points[i], b = pl.points[(i + 1) % n];
        const double segLen = Len(Sub(b, a));
        if (segLen < 1e-12) continue;
        const V2 dir{ (b.x - a.x) / segLen, (b.y - a.y) / segLen };
        const double segStart = acc, segEnd = acc + segLen;
        if (!started && from <= segEnd) {
            const double d = from > segStart ? from : segStart;
            run.points.push_back({ a.x + dir.x * (d - segStart),
                                   a.y + dir.y * (d - segStart) });
            started = true;
        }
        if (started) {
            if (to < segEnd) {
                run.points.push_back({ a.x + dir.x * (to - segStart),
                                       a.y + dir.y * (to - segStart) });
                break;
            }
            run.points.push_back(b);
        }
        acc = segEnd;
    }
    return run;
}

// Arc-length of the spine point closest to node-local position `p` (projects
// onto every segment) — used to PIN a dash anchor to a control point.
double ProjectArc(const Polyline& pl, DVec2 p, double fallback) {
    const std::size_t n = pl.points.size();
    const std::size_t sc = pl.closed ? n : n - 1;
    double acc = 0.0, best = 1e300, arc = fallback;
    for (std::size_t i = 0; i < sc; ++i) {
        const DVec2 a = pl.points[i], b = pl.points[(i + 1) % n];
        const double abx = b.x - a.x, aby = b.y - a.y;
        const double L2 = abx * abx + aby * aby;
        const double L = std::sqrt(L2);
        if (L < 1e-12) continue;
        double u = ((p.x - a.x) * abx + (p.y - a.y) * aby) / L2;
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
        const double dx = p.x - (a.x + abx * u), dy = p.y - (a.y + aby * u);
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; arc = acc + L * u; }
        acc += L;
    }
    return arc;
}

// The dash offset that lands a dash ELEMENT centre (or a GAP centre) exactly
// on run-local arc `aRun` (legacy DashLayout rule; exact dash/gap lengths are
// preserved — only the partials at the run ends absorb the leftover).
double AnchorDashOffset(const std::vector<double>& patternIn,
                        bool elementCentred, double aRun, double fallback) {
    std::vector<double> pat;
    double period = 0.0;
    for (double d : patternIn)
        if (d > 1e-9) { pat.push_back(d); period += d; }
    if (pat.empty() || period <= 0.0) return fallback;
    if (pat.size() % 2 == 1) period *= 2.0;   // odd counts repeat to even
    const double dashLen = pat[0];
    const double gapLen  = pat.size() > 1 ? pat[1] : pat[0];
    const double centre = elementCentred ? dashLen * 0.5
                                         : dashLen + gapLen * 0.5;
    return centre - aRun;   // DashSplit wraps negatives into the period
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

// Strokes `spine` (arc length `total`) with the stroke's OWN dash pattern, then
// opens the given `gaps` on top WITHOUT re-phasing the dashes: the dash motif's
// "on" intervals are computed over the whole spine, the gaps are subtracted, and
// each surviving run is stroked BUTT with an explicit per-end cap — the gap ends
// carry the GAP's cap (independent of the stroke), the other ends the stroke's
// own cap. A free function so `TessellateStroke` stays small and readable.
void StrokeDashesWithGaps(const Polyline& spine, const Stroke& stroke,
                          double w, double tol, double total, double offset,
                          const std::vector<GapSpan>& gaps, Emitter& em) {
    // On-intervals carry, per END, whether that end abuts a GAP (its cap is the
    // GAP's, independent of the stroke) or is an ordinary dash/spine end (the
    // stroke's own cap). Every interval is stroked BUTT, then each end gets
    // exactly one explicit cap — so a Butt gap shows no round bleeding in.
    std::vector<Iv> onIv;
    for (const auto& p : DashIntervals(total, stroke.dashPattern, offset))
        onIv.push_back({ p.first, p.second, GapCap::Butt, GapCap::Butt,
                         false, false });
    std::vector<GapSpan> sorted = gaps;
    std::sort(sorted.begin(), sorted.end(),
              [](const GapSpan& a, const GapSpan& b) { return a.from < b.from; });
    for (const GapSpan& g : sorted) {
        std::vector<Iv> next;
        for (const Iv& iv : onIv) {
            if (g.to <= iv.from || g.from >= iv.to) { next.push_back(iv); continue; }
            if (iv.from < g.from)
                next.push_back({ iv.from, g.from, iv.capFrom, g.capFrom,
                                 iv.gapFrom, true });
            if (g.to < iv.to)
                next.push_back({ g.to, iv.to, g.capTo, iv.capTo,
                                 true, iv.gapTo });
        }
        onIv.swap(next);
    }
    // The stroke's own cap style (for the non-gap ends).
    const GapCap strokeCap =
        stroke.cap == CapStyle::Round ? GapCap::Round
      : stroke.cap == CapStyle::Square ? GapCap::Square : GapCap::Butt;
    // Draw one cap at arc `at`, extending on the `intoSign` side.
    auto emitCap = [&](double at, double intoSign, GapCap cap) {
        if (cap == GapCap::Butt) return;
        DVec2 p; V2 t;
        ArcSampleAt(spine, std::clamp(at, 0.0, total), p, t);
        const V2 dir{ t.x * intoSign, t.y * intoSign };
        const V2 nrm = Perp(t);
        const double h = w * 0.5;
        if (cap == GapCap::Round) {
            em.Arc(p, nrm, { -nrm.x, -nrm.y }, h,
                   Cross(nrm, dir) > 0.0 ? 1 : -1, tol);
        } else {
            const DVec2 a{ p.x + nrm.x * h, p.y + nrm.y * h };
            const DVec2 b{ p.x - nrm.x * h, p.y - nrm.y * h };
            const DVec2 a2{ a.x + dir.x * h, a.y + dir.y * h };
            const DVec2 b2{ b.x + dir.x * h, b.y + dir.y * h };
            const std::uint32_t ia = em.V(a.x, a.y), ib = em.V(b.x, b.y);
            const std::uint32_t ia2 = em.V(a2.x, a2.y), ib2 = em.V(b2.x, b2.y);
            em.Tri(ia, ia2, ib2); em.Tri(ia, ib2, ib);
        }
    };
    for (const Iv& iv : onIv) {
        if (iv.to - iv.from < 1e-9) continue;
        Polyline run = ExtractRun(spine, iv.from, iv.to);
        if (run.points.size() < 2) continue;
        Stroke bs = stroke; bs.cap = CapStyle::Butt;   // caps added below
        CenterStroke(run, bs, w * 0.5, tol, em);
        // FROM end extends backwards (−1); TO end forwards (+1).
        emitCap(iv.from, -1.0, iv.gapFrom ? iv.capFrom : strokeCap);
        emitCap(iv.to,   +1.0, iv.gapTo   ? iv.capTo   : strokeCap);
    }
}

} // namespace

// True when a PRIMITIVE object is built as a DERIVED ring bent along the line —
// Bend and Follow both do (their corners sit on the curve with perpendicular
// transverse ends; Follow additionally resamples the long edges to the curve).
// Hard is a rigid primitive. Instances can't be ring-bent: they use an affine
// frame with a shear instead (see MarkPlaceMatrix / the Scene's instance path).
bool BendsAlongCurve(MarkBend b) {
    return b == MarkBend::Bend || b == MarkBend::Follow;
}

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
                      const Stroke& stroke, double tolerance,
                      const PathData* source) {
    Mesh out;
    Emitter em{ &out };
    const double w = stroke.width;
    if (w <= 0.0) return out;
    const double tol = tolerance > 0.0 ? tolerance : 0.25;

    // A mark's OBJECTS are placed on the ORIGINAL path flattened at a FIXED fine
    // tolerance (kMarkPlaceTolerance), NOT the per-tier / aligned spine — so a
    // Fusion object lands at exactly the same spot the Scene places a Blend/Cut
    // object, and stays put at any zoom. Falls back to the tier spine when the
    // source path is unavailable (a boolean-derived outline).
    std::vector<Polyline> placeSpines;
    if (source) placeSpines = Flatten(*source, kMarkPlaceTolerance);

    for (std::size_t subI = 0; subI < polylines.size(); ++subI) {
        const Polyline& src = polylines[subI];
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

        // ── This subpath's marks → dash re-phasing only ─────────────────────
        // A mark with a non-Neutral phase forces a dash element (Dash) or a
        // gap (Gap) to land centred on it; a node-pinned mark re-projects its
        // arc position from the control point. The MARK OBJECTS (shapes /
        // instances, add / subtract) are emitted by the Scene as their own
        // drawables in the stroke's isolated layer — NOT here (the stroker
        // only produces the base line).
        const double total = PolyTotal(spine);
        std::vector<PhaseAnchor> anchors;
        for (const StrokeMark& m : stroke.marks) {
            if (m.sub != (std::int32_t)subI || total < 1e-9 || !m.RePhases())
                continue;
            const double tc = m.t < 0.0 ? 0.0 : (m.t > 1.0 ? 1.0 : m.t);
            double at = tc * total;
            if (source && m.nodeAnchor >= 0) {
                // Map the flattened index back to its source subpath (Flatten
                // skips subpaths with < 2 anchors).
                std::size_t si = 0, seen = 0;
                for (; si < source->subpaths.size(); ++si) {
                    if (source->subpaths[si].anchors.size() < 2) continue;
                    if (seen == subI) break;
                    ++seen;
                }
                if (si < source->subpaths.size() &&
                    m.nodeAnchor <
                        (std::int32_t)source->subpaths[si].anchors.size())
                    at = ProjectArc(spine,
                        source->subpaths[si]
                            .anchors[(std::size_t)m.nodeAnchor].pos, at);
            }
            anchors.push_back({ at, m.phase == MarkPhase::Dash });
        }

        // ── Gap objects → cut the base line ─────────────────────────────────
        // A Gap mark opens the line over its length, centred on the mark, with
        // the chosen end caps. The kept runs are the complement of the gaps.
        std::vector<GapSpan> gaps;
        for (const StrokeMark& m : stroke.marks) {
            if (m.sub != (std::int32_t)subI || total < 1e-9) continue;
            const double tc = m.t < 0.0 ? 0.0 : (m.t > 1.0 ? 1.0 : m.t);
            const double d = tc * total;
            for (const MarkObject& o : m.objects) {
                if (o.shape != MarkShape::Gap) continue;
                const double half = std::max(1e-4, o.SizeUnits(w)) * 0.5;
                gaps.push_back({ std::max(0.0, d - half),
                                 std::min(total, d + half),
                                 o.gapStart, o.gapEnd, o.gapCutsObjects });
            }
        }

        if (gaps.empty()) {
            // No gaps → stroke the whole (possibly CLOSED) spine directly, so a
            // cyclic path keeps its start/end seam joined. ExtractRun would drop
            // the `closed` flag and leave the seam open.
            if (stroke.dashPattern.empty()) {
                CenterStroke(spine, stroke, w * 0.5, tol, em);
            } else {
                double offset = stroke.dashOffset;
                if (!anchors.empty())
                    offset = AnchorDashOffset(stroke.dashPattern,
                                              anchors.front().elementCentred,
                                              anchors.front().at, offset);
                for (const Polyline& piece :
                     DashSplit(spine, stroke.dashPattern, offset))
                    CenterStroke(piece, stroke, w * 0.5, tol, em);
            }
        } else {
            // A Gap object opens the line LOCALLY, ON TOP of the stroke's own
            // dashing — it does NOT re-phase the dashes. Compute the dash motif's
            // phase (possibly re-anchored by a Dash/Gap PHASE mark) and delegate
            // the interval subtraction + capping to StrokeDashesWithGaps.
            double offset = stroke.dashOffset;
            if (!anchors.empty())
                offset = AnchorDashOffset(stroke.dashPattern.empty()
                                              ? std::vector<double>{ 1, 0 }
                                              : stroke.dashPattern,
                                          anchors.front().elementCentred,
                                          anchors.front().at, offset);
            StrokeDashesWithGaps(spine, stroke, w, tol, total, offset, gaps, em);
        }

        // Fusion mark objects → triangulated INTO the stroke mesh (one alpha,
        // vector-smooth at any zoom). Placed on the fine `placeSpine` so the
        // spot matches the Scene's Blend/Cut placement exactly.
        const Polyline& placeSpine =
            (subI < placeSpines.size() && placeSpines[subI].points.size() >= 2)
                ? placeSpines[subI] : spine;
        // Arc length of the PLACEMENT spine (for gap-end sub-object placement).
        const double placeTotal = PolyTotal(placeSpine);
        // Bake ONE object's ring into the stroke mesh, if it is a stroke-coloured
        // Fusion primitive (Blend/Cut/Instance/recoloured are separate drawables).
        auto bakeFusion = [&](const StrokeMark& vm, const MarkObject& obj) {
            if (obj.mode != MarkObjectMode::Fusion ||
                obj.shape == MarkShape::Instance ||
                obj.shape == MarkShape::Gap ||
                !obj.useStrokeColor) return;
            std::vector<DVec2> ring;
            if (BendsAlongCurve(obj.bend)) {
                if (!MarkFollowContour(placeSpine, vm, obj, w, tol, ring))
                    return;
            } else {
                // Parametric primitive → flatten at tier tolerance → place.
                const PathData shape = MarkPrimitiveShape(obj, w);
                if (shape.Empty()) return;
                const DMat23 place = MarkPlaceMatrix(placeSpine, vm, obj, w);
                for (const Polyline& pl : Flatten(shape, tol))
                    for (const DVec2& q : pl.points)
                        ring.push_back(place.Apply(q));
            }
            if (ring.size() < 3) return;
            // Triangulate the (possibly CONCAVE for Follow) ring PROPERLY — a
            // centroid fan double-covers a concave ring and leaves residual
            // overlapping triangles. Merge into the stroke mesh with an offset.
            Polyline rp; rp.points = ring; rp.closed = true;
            const Mesh rm = TriangulateFill({ rp }, FillRule::NonZero);
            const std::uint32_t base = out.VertexCount();
            for (std::size_t i = 0; i + 1 < rm.positions.size(); i += 2)
                em.V(rm.positions[i], rm.positions[i + 1]);
            for (std::uint32_t idx : rm.indices)
                out.indices.push_back(base + idx);
        };
        for (const StrokeMark& m : stroke.marks) {
            if (m.sub != (std::int32_t)subI) continue;
            const double md = (m.t < 0 ? 0 : m.t > 1 ? 1 : m.t) * total;
            // A gap with cutsObjects removes any Fusion object whose mark point
            // falls inside it (a v1 approximation of "the gap also cuts marks").
            bool inCuttingGap = false;
            for (const GapSpan& g : gaps)
                if (g.cutsObjects && md >= g.from && md <= g.to)
                    inCuttingGap = true;
            for (const MarkObject& obj : m.objects) {
                if (obj.shape == MarkShape::Gap) {
                    // A Gap draws no fill, but bakes its stroke-coloured Fusion
                    // START/END marker sub-objects at the gap ends (virtual marks
                    // at tc±half), so they merge into the stroke mesh like any
                    // Fusion object — no double transparency.
                    if (placeTotal < 1e-9) continue;
                    const double half =
                        std::max(1e-4, obj.SizeUnits(w)) * 0.5;
                    const double tc = std::clamp(m.t, 0.0, 1.0) * placeTotal;
                    auto bakeEnd = [&](double endArc,
                                       const std::vector<MarkObject>& objs) {
                        StrokeMark vm = m;
                        vm.t = std::clamp(endArc / placeTotal, 0.0, 1.0);
                        for (const MarkObject& so : objs) bakeFusion(vm, so);
                    };
                    bakeEnd(tc - half, obj.gapStartObjects);
                    bakeEnd(tc + half, obj.gapEndObjects);
                    continue;
                }
                if (inCuttingGap) continue;
                bakeFusion(m, obj);
            }
        }
    }
    return out;
}

PathData MarkPrimitiveShape(const MarkObject& obj, double strokeWidth) {
    if (obj.shape == MarkShape::Instance) return PathData{};
    // `size` is HALF-extent along the tangent (rectangle length / circle radius
    // / diamond diagonal-half); `width` the rectangle half-height. The geometry
    // is parametric so the GeometryCache re-tessellates it per zoom tier.
    const double hu = std::max(1e-6, obj.SizeUnits(strokeWidth));
    if (obj.shape == MarkShape::Circle)
        return PathData::Ellipse(0, 0, hu, hu);
    if (obj.shape == MarkShape::Rectangle) {
        const double hv = std::max(1e-6, obj.WidthUnits(strokeWidth));
        return PathData::Rect(-hu, -hv, hu * 2.0, hv * 2.0);
    }
    // Diamond: a 4-point polygon, `size` = the half-diagonal.
    return PathData::Polygon({ { hu, 0 }, { 0, hu }, { -hu, 0 }, { 0, -hu } },
                             true);
}

DMat23 MarkPlaceMatrix(const Polyline& spine, const StrokeMark& mark,
                       const MarkObject& obj, double strokeWidth) {
    DMat23 id;
    if (spine.points.size() < 2) return id;
    const double total = PolyTotal(spine);
    if (total < 1e-9) return id;
    // The along-offset shifts the sample point up/down the curve; the side
    // offset shifts it across. Hard = a rigid frame. The Bend branch (a shear
    // that leans the shape with the local slope) is used ONLY by INSTANCES —
    // a node's arbitrary geometry can't be ring-bent, so it leans by an affine
    // instead. PRIMITIVE Bend/Follow objects never reach here (they build a
    // derived ring in MarkFollowContour with perpendicular transverse ends).
    double d0 = (mark.t < 0 ? 0 : mark.t > 1 ? 1 : mark.t) * total
                + obj.AlongUnits(strokeWidth);
    d0 = d0 < 0 ? 0 : (d0 > total ? total : d0);
    DVec2 p0; V2 t0;
    ArcSampleAt(spine, d0, p0, t0);
    const V2 n0 = Perp(t0);
    // Side across the line: the object's own side/offset when it overrides,
    // else the mark's.
    const MarkSide sd = obj.sideInherit ? mark.side : obj.side;
    const double soff = obj.sideInherit ? mark.OffsetUnits(strokeWidth)
                                        : obj.SideOffsetUnits(strokeWidth);
    double off = 0.0;
    if (sd == MarkSide::Left)  off =  soff;
    if (sd == MarkSide::Right) off = -soff;
    const DVec2 at{ p0.x + n0.x * off, p0.y + n0.y * off };
    // Frame axes: local +x → tangent, local +y → left normal, then object spin.
    const double ca = std::cos(obj.rotation), sa = std::sin(obj.rotation);
    double ux = t0.x * ca - n0.x * sa, uy = t0.y * ca - n0.y * sa;   // +x axis
    double vx = t0.x * sa + n0.x * ca, vy = t0.y * sa + n0.y * ca;   // +y axis
    // BEND: shear the transverse axis along the tangent by the slope the curve
    // gains over the shape's half-extent — the long edges lean with the line
    // while the shape stays affine. `shear = tan(Δθ)` where Δθ is the tangent
    // rotation from the mark point to the shape's leading end (arc d0+hu).
    if (obj.bend == MarkBend::Bend) {
        const double hu = std::max(1e-6, obj.SizeUnits(strokeWidth));
        DVec2 pa; V2 ta;
        ArcSampleAt(spine, std::clamp(d0 + hu, 0.0, total), pa, ta);
        double dth = std::atan2(Cross(t0, ta), Dot(t0, ta));   // signed turn
        dth = std::clamp(dth, -1.3, 1.3);                      // < 90°, stable
        const double shear = std::tan(dth);
        // v' = v + shear · (component of v along the ORIGINAL tangent axis) — add
        // a tangential lean proportional to how far across the line the point is.
        vx += shear * ux;  vy += shear * uy;
    }
    DMat23 m;
    m.m[0] = ux;  m.m[1] = vx;  m.m[2] = at.x;
    m.m[3] = uy;  m.m[4] = vy;  m.m[5] = at.y;
    return m;
}

bool MarkFollowContour(const Polyline& spine, const StrokeMark& mark,
                       const MarkObject& obj, double strokeWidth,
                       double tolerance, std::vector<DVec2>& outRing) {
    outRing.clear();
    // Bend AND Follow build a derived ring. BEND: corners sit on the curve so
    // the two TRANSVERSE ends stay perpendicular to the line, but the long edges
    // are STRAIGHT between corners. FOLLOW: the long edges are additionally
    // resampled to curve with the line. Instances never come here.
    if (!BendsAlongCurve(obj.bend) || obj.shape == MarkShape::Instance)
        return false;
    const bool follow = obj.bend == MarkBend::Follow;
    if (spine.points.size() < 2) return false;
    const double total = PolyTotal(spine);
    if (total < 1e-9) return false;
    const double d0 = std::clamp(
        (mark.t < 0 ? 0 : mark.t > 1 ? 1 : mark.t) * total
            + obj.AlongUnits(strokeWidth), 0.0, total);
    const MarkSide sd = obj.sideInherit ? mark.side : obj.side;
    const double soff = obj.sideInherit ? mark.OffsetUnits(strokeWidth)
                                        : obj.SideOffsetUnits(strokeWidth);
    double off = 0.0;
    if (sd == MarkSide::Left)  off =  soff;
    if (sd == MarkSide::Right) off = -soff;
    // A fixed fine sampling step so the curved edges read smooth at normal zoom.
    // The derived ring is figée (built ONCE, not per zoom tier), so the tolerance
    // is FLOORED — it must stay bounded (a tiny tolerance would build a huge ring
    // and the O(n²) fill triangulation would stall). A documented boolean-like
    // limit at extreme zoom.
    const double tol = std::max(std::min(tolerance > 0.0 ? tolerance : 0.25, 0.05),
                                0.02);
    const double hu = std::max(1e-6, obj.SizeUnits(strokeWidth));
    const double hv = obj.shape == MarkShape::Rectangle
                          ? std::max(1e-6, obj.WidthUnits(strokeWidth)) : hu;

    // Place a LOCAL shape point (u along the tangent, v across it): the point's
    // ALONG coordinate `u` maps to a real arc-length step d0+u on the curve, and
    // its ACROSS coordinate `v` steps along the LOCAL normal at THAT arc — so the
    // whole outline bends WITH the line. The object's own rotation is applied in
    // local (u,v) space first. A soft clamp keeps the concave side from folding
    // past the curvature centre (which would invert triangles).
    const double ca = std::cos(obj.rotation), sa = std::sin(obj.rotation);
    auto place = [&](double u, double v) -> DVec2 {
        const double ur = u * ca - v * sa, vr = u * sa + v * ca;
        const double du = std::clamp(d0 + ur, 0.0, total);
        DVec2 pu; V2 tu;
        ArcSampleAt(spine, du, pu, tu);
        const V2 nu = Perp(tu);
        double vv = vr + off;
        // Local curvature (central difference) → clamp the across-offset to just
        // inside the radius so the ring never crosses the curvature centre.
        const double ds = std::max(1e-4, hu * 0.5);
        DVec2 pA, pB; V2 tA, tB;
        ArcSampleAt(spine, std::min(total, du + ds), pA, tA);
        ArcSampleAt(spine, std::max(0.0,   du - ds), pB, tB);
        const double dTheta = std::atan2(Cross(tB, tA), Dot(tB, tA));
        const double curv = dTheta / (2.0 * ds);
        if (std::abs(curv) > 1e-9) {
            const double R = 1.0 / curv;
            if (R > 0.0 && vv >  0.95 * R) vv =  0.95 * R;
            if (R < 0.0 && vv <  0.95 * R) vv =  0.95 * R;
        }
        return { pu.x + nu.x * vv, pu.y + nu.y * vv };
    };
    // A shape EDGE from local (u0,v0) to (u1,v1). FOLLOW resamples it densely so
    // its curved image stays smooth; BEND keeps it a STRAIGHT segment between the
    // two (already curve-placed) corners. The last point is NOT emitted (the next
    // edge's first point continues the ring) — avoids a duplicate seam vertex.
    const double step = std::max(tol, 0.04);
    auto edge = [&](double u0, double v0, double u1, double v1) {
        int n = 1;
        if (follow) {
            const double len = std::hypot(u1 - u0, v1 - v0);
            n = (int)std::ceil(len / step);
            n = n < 1 ? 1 : (n > 4096 ? 4096 : n);
        }
        for (int i = 0; i < n; ++i) {
            const double f = (double)i / (double)n;
            outRing.push_back(place(u0 + (u1 - u0) * f, v0 + (v1 - v0) * f));
        }
    };
    if (obj.shape == MarkShape::Circle) {
        // One closed loop; sample its outline so the bent circle reads smooth.
        // Each point is placed through the curve frame at its own arc.
        int steps = (int)std::ceil(6.28318530717958 /
            (hu > tol ? 2.0 * std::acos(1.0 - tol / hu) : 1.0));
        steps = steps < 48 ? 48 : (steps > 512 ? 512 : steps);
        for (int i = 0; i < steps; ++i) {
            const double a = 6.28318530717958 * (double)i / (double)steps;
            outRing.push_back(place(std::cos(a) * hu, std::sin(a) * hu));
        }
    } else if (obj.shape == MarkShape::Rectangle) {
        // Corners CCW; every edge is resampled so both the transverse and the
        // longitudinal sides curve smoothly with the line.
        edge( hu, -hv,  hu,  hv);   // right transverse
        edge( hu,  hv, -hu,  hv);   // top longitudinal
        edge(-hu,  hv, -hu, -hv);   // left transverse
        edge(-hu, -hv,  hu, -hv);   // bottom longitudinal
    } else {   // Diamond
        edge( hu, 0, 0,  hu);
        edge( 0, hu, -hu, 0);
        edge(-hu, 0, 0, -hu);
        edge( 0, -hu, hu, 0);
    }
    return outRing.size() >= 3;
}

} // namespace Ink::geom

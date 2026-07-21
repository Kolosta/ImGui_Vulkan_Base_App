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

struct PhaseAnchor { double at; bool elementCentred; double forcedSize; };
struct GapSpan { double from, to; GapCap capFrom, capTo; bool cutsObjects; };

// ── Multi-anchor dash layout ─────────────────────────────────────────────────
// ON intervals with SEVERAL dash anchors (or a forced feature size): each
// anchor centres a dash ELEMENT (or a GAP) of its own/forced length; between
// two consecutive anchor features a WHOLE number of alternating runs fits,
// stretched per `fit` (scale both / keep the gaps / keep the dashes); outside
// the outer anchors the pattern runs unscaled and the spine ends absorb the
// partials. The pattern is reduced to its first dash/gap pair (documented v1).
std::vector<std::pair<double,double>>
AnchoredDashIntervals(double total, const std::vector<double>& patternIn,
                      std::vector<PhaseAnchor> anchors, DashFit fit) {
    std::vector<std::pair<double,double>> on;
    double D = 0.0, G = 0.0;
    {
        std::vector<double> pat;
        for (double d : patternIn)
            if (d > 1e-9) pat.push_back(d);
        if (pat.empty()) { on.push_back({ 0.0, total }); return on; }
        D = pat[0];
        G = pat.size() > 1 ? pat[1] : pat[0];
    }
    std::sort(anchors.begin(), anchors.end(),
              [](const PhaseAnchor& a, const PhaseAnchor& b) {
                  return a.at < b.at;
              });
    struct Feat { double a, b; bool gap; };
    std::vector<Feat> feats;
    for (const PhaseAnchor& an : anchors) {
        const double s = an.forcedSize > 1e-9
                             ? an.forcedSize
                             : (an.elementCentred ? D : G);
        feats.push_back({ an.at - s * 0.5, an.at + s * 0.5,
                          !an.elementCentred });
    }
    auto emitOn = [&](double a, double b) {
        a = std::max(0.0, a);
        b = std::min(total, b);
        if (b - a > 1e-9) on.push_back({ a, b });
    };
    // Fill [x0,x1] with alternating runs: the first run's type is `startGap`,
    // the last one's `endGap`; lengths from D/G stretched per `fit` so a
    // whole number of runs fits exactly.
    auto fillBetween = [&](double x0, double x1, bool startGap, bool endGap) {
        const double M = x1 - x0;
        if (M <= 1e-9) return;
        long nD = 0, nG = 0;
        if (startGap && endGap) {
            long k2 = std::lround((M - G) / (D + G));
            k2 = k2 < 0 ? 0 : k2;
            nD = k2; nG = k2 + 1;
        } else if (!startGap && !endGap) {
            long k2 = std::lround((M - D) / (D + G));
            k2 = k2 < 0 ? 0 : k2;
            nD = k2 + 1; nG = k2;
        } else {
            long m2 = std::lround(M / (D + G));
            m2 = m2 < 1 ? 1 : m2;
            nD = m2; nG = m2;
        }
        double d2 = D, g2 = G;
        const double ideal = (double)nD * D + (double)nG * G;
        if (fit == DashFit::ScaleDash && nD > 0 && M > (double)nG * G)
            d2 = (M - (double)nG * G) / (double)nD;
        else if (fit == DashFit::ScaleGap && nG > 0 && M > (double)nD * D)
            g2 = (M - (double)nD * D) / (double)nG;
        else if (ideal > 1e-9) {
            const double f = M / ideal;
            d2 = D * f;
            g2 = G * f;
        }
        bool isGap = startGap;
        double pos = x0;
        while (pos < x1 - 1e-9 && (nD > 0 || nG > 0)) {
            const double len = isGap ? g2 : d2;
            if (!isGap) { emitOn(pos, std::min(pos + len, x1)); --nD; }
            else --nG;
            pos += std::max(len, 1e-9);
            isGap = !isGap;
        }
    };
    // Unscaled runs OUTSIDE the outer anchors; the spine ends clip partials.
    auto fillBefore = [&](double edge2, bool featIsGap) {
        bool isGap = !featIsGap;
        double hi = edge2;
        int guard = 100000;
        while (hi > 1e-9 && guard-- > 0) {
            const double len = std::max(isGap ? G : D, 1e-9);
            if (!isGap) emitOn(hi - len, hi);
            hi -= len;
            isGap = !isGap;
        }
    };
    auto fillAfter = [&](double edge2, bool featIsGap) {
        bool isGap = !featIsGap;
        double lo = edge2;
        int guard = 100000;
        while (lo < total - 1e-9 && guard-- > 0) {
            const double len = std::max(isGap ? G : D, 1e-9);
            if (!isGap) emitOn(lo, lo + len);
            lo += len;
            isGap = !isGap;
        }
    };
    fillBefore(feats.front().a, feats.front().gap);
    for (std::size_t i = 0; i < feats.size(); ++i) {
        if (!feats[i].gap) emitOn(feats[i].a, feats[i].b);
        if (i + 1 < feats.size())
            fillBetween(feats[i].b, feats[i + 1].a,
                        !feats[i].gap, !feats[i + 1].gap);
    }
    fillAfter(feats.back().b, feats.back().gap);
    std::sort(on.begin(), on.end());
    return on;
}

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

// ── Smooth arc-length frame over a flattened spine ───────────────────────────
// Positions interpolate linearly (exact on the polyline); tangents interpolate
// by ANGLE between per-vertex smoothed tangents (each vertex tangent averages
// its two adjacent segment directions), so the frame's DIRECTION is continuous
// in arc length. Mark objects placed through this frame turn smoothly along
// the curve — no facet jumps while sliding, no zig-zag normals on dense
// Follow resampling — whatever the flatten density of the spine.
struct ArcFrame {
    std::vector<double> cum;    // cumulative arc length per vertex
    std::vector<DVec2>  pts;    // vertices (closed: the first re-appended)
    std::vector<double> ang;    // per-vertex tangent angle, UNWRAPPED
    double total = 0.0;

    void Build(const Polyline& pl) {
        const std::size_t n = pl.points.size();
        if (n < 2) return;
        const std::size_t sc = pl.closed ? n : n - 1;
        std::vector<V2> dir(sc);
        for (std::size_t i = 0; i < sc; ++i)
            dir[i] = Norm(Sub(pl.points[(i + 1) % n], pl.points[i]));
        auto vertexTangent = [&](std::size_t i) -> V2 {
            V2 a{ 0, 0 }, b{ 0, 0 };
            if (pl.closed) {
                a = dir[(i + sc - 1) % sc];
                b = dir[i % sc];
            } else {
                if (i > 0)  a = dir[i - 1];
                if (i < sc) b = dir[i];
            }
            V2 m = Norm({ a.x + b.x, a.y + b.y });
            if (m.x == 0.0 && m.y == 0.0) m = (b.x != 0.0 || b.y != 0.0) ? b : a;
            return m;
        };
        pts.reserve(sc + 1); cum.reserve(sc + 1); ang.reserve(sc + 1);
        constexpr double kTau = 6.28318530717958647692;
        double acc = 0.0, prev = 0.0;
        for (std::size_t i = 0; i <= sc; ++i) {
            pts.push_back(pl.points[i % n]);
            if (i > 0) acc += Len(Sub(pts[i], pts[i - 1]));
            cum.push_back(acc);
            const V2 t = vertexTangent((pl.closed && i == sc) ? 0 : i);
            double a = std::atan2(t.y, t.x);
            if (i > 0) {   // unwrap: interpolation always takes the short way
                while (a - prev >  kTau * 0.5) a -= kTau;
                while (a - prev < -kTau * 0.5) a += kTau;
            }
            ang.push_back(a);
            prev = a;
        }
        total = acc;
    }
    bool Valid() const { return pts.size() >= 2 && total > 1e-9; }
    // `smooth` picks the tangent model. SMOOTHED blends the per-vertex bisector
    // tangents across each segment: on a flattened CURVE that recovers the true
    // tangent, but on a HARD corner the bisector is not the curve's direction —
    // the angle then sweeps toward it as the sample nears the vertex, which
    // leans everything placed near a corner. SEGMENT (smooth = false) takes the
    // exact segment direction, so a mark is square to the edge it sits on right
    // up to the corner. Positions are identical either way.
    void Sample(double d, DVec2& outP, V2& outT, bool smooth = true) const {
        d = d < 0.0 ? 0.0 : (d > total ? total : d);
        std::size_t k = (std::size_t)(std::upper_bound(cum.begin(), cum.end(), d)
                                      - cum.begin());
        k = k == 0 ? 0 : k - 1;
        if (k >= pts.size() - 1) k = pts.size() - 2;
        const double L = cum[k + 1] - cum[k];
        const double u = L > 1e-12 ? (d - cum[k]) / L : 0.0;
        outP = { pts[k].x + (pts[k + 1].x - pts[k].x) * u,
                 pts[k].y + (pts[k + 1].y - pts[k].y) * u };
        if (smooth) {
            const double a = ang[k] + (ang[k + 1] - ang[k]) * u;
            outT = { std::cos(a), std::sin(a) };
        } else {
            outT = Norm(Sub(pts[k + 1], pts[k]));
            if (outT.x == 0.0 && outT.y == 0.0) {
                const double a = ang[k];
                outT = { std::cos(a), std::sin(a) };
            }
        }
    }
};

// Shared Bend/Follow placement: a local shape point (u along the tangent, v
// across it) maps through the smooth arc frame — u to a real arc-length step,
// v along the LOCAL normal at that arc — with the object's own rotation
// applied in local space first and the across-offset soft-clamped inside the
// local curvature radius (the geometry must never fold past the curvature
// centre, which would invert triangles).
struct CurvePlacer {
    ArcFrame af;
    double d0 = 0.0, off = 0.0, ca = 1.0, sa = 0.0, ds = 1e-4;

    bool Init(const Polyline& spine, const StrokeMark& mark,
              const MarkObject& obj, double strokeWidth, double halfExtent) {
        af.Build(spine);
        if (!af.Valid()) return false;
        d0 = (mark.t < 0 ? 0 : mark.t > 1 ? 1 : mark.t) * af.total
             + obj.AlongUnits(strokeWidth);
        d0 = std::clamp(d0, 0.0, af.total);
        const MarkSide sd = obj.sideInherit ? mark.side : obj.side;
        const double soff = obj.sideInherit ? mark.OffsetUnits(strokeWidth)
                                            : obj.SideOffsetUnits(strokeWidth);
        off = sd == MarkSide::Left ? soff : sd == MarkSide::Right ? -soff : 0.0;
        ca = std::cos(obj.rotation);
        sa = std::sin(obj.rotation);
        ds = std::max(1e-4, halfExtent * 0.5);
        return true;
    }
    DVec2 Place(double u, double v) const {
        const double ur = u * ca - v * sa, vr = u * sa + v * ca;
        const double du = std::clamp(d0 + ur, 0.0, af.total);
        DVec2 pu; V2 tu;
        af.Sample(du, pu, tu);
        const V2 nu = Perp(tu);
        double vv = vr + off;
        DVec2 pA, pB; V2 tA, tB;
        af.Sample(std::min(af.total, du + ds), pA, tA);
        af.Sample(std::max(0.0, du - ds), pB, tB);
        const double dTheta = std::atan2(Cross(tB, tA), Dot(tB, tA));
        const double curv = dTheta / (2.0 * ds);
        if (std::abs(curv) > 1e-9) {
            const double R = 1.0 / curv;
            if (R > 0.0 && vv >  0.95 * R) vv =  0.95 * R;
            if (R < 0.0 && vv <  0.95 * R) vv =  0.95 * R;
        }
        return { pu.x + nu.x * vv, pu.y + nu.y * vv };
    }
};

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

// Per-vertex side sign for an OPEN path stroked Inside / Outside. There is no
// enclosed side, so the LOCAL CURVATURE picks it: at each vertex the turn's
// sense says which side is convex — the same rule the joins use to find their
// outer wedge. A straight stretch has no turn of its own and inherits the sign
// it arrived with, so a run keeps one side until the path really does bend the
// other way. Returns +1/−1 per vertex; +1 means the +normal side.
std::vector<double> CurvatureSides(const Polyline& pl, bool outside) {
    const std::size_t n = pl.points.size();
    std::vector<double> sgn(n, 1.0);
    if (n < 3) return sgn;
    // Turn at vertex i (1 … n−2) — the endpoints inherit their neighbour.
    double carried = 0.0;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const V2 a = Norm(Sub(pl.points[i], pl.points[i - 1]));
        const V2 b = Norm(Sub(pl.points[i + 1], pl.points[i]));
        const double turn = Cross(a, b);
        if (std::abs(turn) > 1e-12) carried = turn < 0.0 ? 1.0 : -1.0;
        // `carried` is now the OUTER side; flip it for Inside.
        sgn[i] = carried == 0.0 ? 1.0 : (outside ? carried : -carried);
    }
    // Endpoints follow the first / last vertex that had a real turn.
    sgn[0] = sgn[1];
    sgn[n - 1] = sgn[n - 2];
    // A path that never turned at all: everything is still the default +1, so
    // fall back to the fixed side the old uniform offset used.
    if (carried == 0.0)
        for (double& v : sgn) v = outside ? -1.0 : 1.0;
    return sgn;
}

// Offset a spine by `amount` on a side that may FLIP along the path (`sgn` per
// vertex, ±1). Where the sign changes between two vertices, BOTH offset points
// of the boundary vertex are emitted — so the spine crosses the path along its
// own NORMAL there and the side swap reads as a perpendicular step, not a
// diagonal one. The spine stays a single continuous polyline, so dashes, marks
// and repeats keep running through the transition unbroken.
Polyline OffsetSpineSigned(const Polyline& pl, double amount,
                           const std::vector<double>& sgn) {
    Polyline out;
    out.closed = false;   // a flipping offset cannot close on itself
    const std::size_t n = pl.points.size();
    if (n < 2 || sgn.size() != n) return pl;
    // The miter-aware normal at vertex i (same construction as OffsetSpine).
    auto normalAt = [&](std::size_t i, double& cosHalf) {
        V2 nPrev{ 0, 0 }, nNext{ 0, 0 };
        if (i > 0)     nPrev = Perp(Norm(Sub(pl.points[i], pl.points[i - 1])));
        if (i + 1 < n) nNext = Perp(Norm(Sub(pl.points[i + 1], pl.points[i])));
        V2 m = Norm({ nPrev.x + nNext.x, nPrev.y + nNext.y });
        if (m.x == 0.0 && m.y == 0.0) m = (nNext.x || nNext.y) ? nNext : nPrev;
        cosHalf = Dot(m, (nNext.x || nNext.y) ? nNext : nPrev);
        if (cosHalf < 0.25) cosHalf = 0.25;
        return m;
    };
    out.points.reserve(n + 8);
    for (std::size_t i = 0; i < n; ++i) {
        double cosHalf = 1.0;
        const V2 m = normalAt(i, cosHalf);
        const double d = amount / cosHalf;
        // A sign change ARRIVING at this vertex: close out the previous side
        // first, so the pair of points spans the path along the normal.
        if (i > 0 && sgn[i] != sgn[i - 1])
            out.points.push_back({ pl.points[i].x + m.x * d * sgn[i - 1],
                                   pl.points[i].y + m.y * d * sgn[i - 1] });
        out.points.push_back({ pl.points[i].x + m.x * d * sgn[i],
                               pl.points[i].y + m.y * d * sgn[i] });
    }
    return out;
}

// Shorten an OPEN polyline by `tStart` / `tEnd` of arc length at its two ends,
// dropping any vertex the trim swallows. Unchanged if the trim would eat it.
Polyline TrimPolylineEnds(const Polyline& pl, double tStart, double tEnd) {
    if (pl.closed || pl.points.size() < 2) return pl;
    if (tStart <= 0.0 && tEnd <= 0.0) return pl;
    if (tStart < 0.0) tStart = 0.0;
    if (tEnd   < 0.0) tEnd   = 0.0;
    double total = 0.0;
    for (std::size_t i = 1; i < pl.points.size(); ++i)
        total += Len(Sub(pl.points[i], pl.points[i - 1]));
    if (total <= tStart + tEnd + 1e-9) return pl;
    Polyline out;
    out.closed = false;
    double acc = 0.0;
    const double lo = tStart, hi = total - tEnd;
    // Walk the segments, emitting the clipped run [lo, hi].
    for (std::size_t i = 1; i < pl.points.size(); ++i) {
        const DVec2 a = pl.points[i - 1], b = pl.points[i];
        const double L = Len(Sub(b, a));
        if (L < 1e-12) continue;
        const double s0 = acc, s1 = acc + L;
        acc = s1;
        if (s1 < lo || s0 > hi) continue;
        auto at = [&](double s) {
            const double u = (s - s0) / L;
            return DVec2{ a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
        };
        if (out.points.empty()) out.points.push_back(at(std::max(s0, lo)));
        out.points.push_back(at(std::min(s1, hi)));
    }
    return out.points.size() >= 2 ? out : pl;
}

// ── 3. Center stroke of one piece ────────────────────────────────────────────
// `offStart` / `offEnd` are the signed distances (along each end's own +normal)
// by which this spine was ALIGNED off the construction path — 0 for a Center
// stroke. A tilted butt cap pivots on the construction path's VERTEX, so it
// needs to know where that vertex is relative to the spine it is capping.
void CenterStroke(const Polyline& plIn, const Stroke& st, double halfW,
                  double tol, Emitter& em,
                  double offStart = 0.0, double offEnd = 0.0) {
    if (plIn.points.size() < 2 || halfW <= 0.0) return;
    // TILTED BUTT CAP. The end edge is the line through the construction
    // VERTEX turned by `capAngle` off the normal. Measured forward from the
    // spine's own end, that line meets the +normal rail at (halfW + off)·k and
    // the −normal rail at (off − halfW)·k. With no align offset those are ±
    // symmetric about the end (the classic centred tilt); with one, BOTH slide
    // by off·k — which is exactly the pivot moving off the stroke's middle onto
    // the vertex, out to the stroke's edge at 50 % and past it beyond.
    // Whichever rail ends up FURTHEST BACK sets how far the body must stop
    // short; the cap quad then fills from there out to the tilted edge.
    // Trimming only changes what is OUTLINED here — the caller's arc-length
    // work (dashes, marks, repeats) ran on the full spine and is untouched.
    const double tiltK =
        (st.cap == CapStyle::Butt && std::abs(st.capAngle) > 1e-9)
            ? std::tan(std::clamp(st.capAngle, -0.7853981634, 0.7853981634))
            : 0.0;
    // Per end: the two rail intersections, and the trim they imply.
    auto railA = [&](double off) { return (halfW + off) * tiltK; };
    auto railB = [&](double off) { return (off - halfW) * tiltK; };
    auto trimFor = [&](double off) {
        return -std::min(0.0, std::min(railA(off), railB(off)));
    };
    Polyline trimmed;
    if (tiltK != 0.0 && !plIn.closed)
        trimmed = TrimPolylineEnds(plIn, trimFor(offStart), trimFor(offEnd));
    const Polyline& pl = trimmed.points.size() >= 2 ? trimmed : plIn;

    const std::size_t n = pl.points.size();
    if (n < 2) return;
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

    // Caps on open ends. A plain Butt cap adds nothing (the quads already end
    // square) — unless it is TILTED, which is real geometry.
    const bool tiltedButt = tiltK != 0.0;
    if (!pl.closed && (st.cap != CapStyle::Butt || tiltedButt)) {
        const double taperLen = st.taperLength > 1e-9 ? st.taperLength
                                                      : halfW * 4.0;
        // Tilt: the body was trimmed back above so this quad can run each rail
        // out to where the tilted edge crosses it — an end cut at exactly
        // `capAngle`, pivoting on the CONSTRUCTION VERTEX.
        auto cap = [&](DVec2 p, V2 d, V2 nrm, double off) {  // d points OUT
            if (tiltedButt) {
                // Both rails measured from the TRIMMED end, so the shorter of
                // the two lands at 0 and the quad below never folds back.
                const double t = trimFor(off);
                const double eA = t + railA(off), eB = t + railB(off);
                const DVec2 p1{ p.x + nrm.x * halfW, p.y + nrm.y * halfW };
                const DVec2 p2{ p.x - nrm.x * halfW, p.y - nrm.y * halfW };
                const std::uint32_t i1 = em.V(p1.x, p1.y);
                const std::uint32_t i2 = em.V(p2.x, p2.y);
                const std::uint32_t ia = em.V(p1.x + d.x * eA, p1.y + d.y * eA);
                const std::uint32_t ib = em.V(p2.x + d.x * eB, p2.y + d.y * eB);
                em.Tri(i1, ia, ib);
                em.Tri(i1, ib, i2);
            } else if (st.cap == CapStyle::Square) {
                const DVec2 q{ p.x + d.x * halfW, p.y + d.y * halfW };
                const std::uint32_t a = em.V(p.x + nrm.x * halfW, p.y + nrm.y * halfW);
                const std::uint32_t b = em.V(q.x + nrm.x * halfW, q.y + nrm.y * halfW);
                const std::uint32_t c = em.V(q.x - nrm.x * halfW, q.y - nrm.y * halfW);
                const std::uint32_t e = em.V(p.x - nrm.x * halfW, p.y - nrm.y * halfW);
                em.Tri(a, b, c);
                em.Tri(a, c, e);
            } else if (st.cap == CapStyle::Taper) {
                // A triangle from the two rim points to a tip `taperLen` ahead
                // along the outward direction — the ISOM erosion-gully point.
                const std::uint32_t a = em.V(p.x + nrm.x * halfW, p.y + nrm.y * halfW);
                const std::uint32_t b = em.V(p.x - nrm.x * halfW, p.y - nrm.y * halfW);
                const std::uint32_t tip =
                    em.V(p.x + d.x * taperLen, p.y + d.y * taperLen);
                em.Tri(a, b, tip);
            } else {   // Round: half-disc through the outward direction
                em.Arc(p, nrm, { -nrm.x, -nrm.y }, halfW,
                       Cross(nrm, d) > 0.0 ? 1 : -1, tol);
            }
        };
        cap(pl.points[0], { -dir[0].x, -dir[0].y }, nor[0], offStart);
        cap(pl.points[n - 1], dir[segCount - 1], nor[segCount - 1], offEnd);
    }
}

// Strokes `spine` (arc length `total`) from PRECOMPUTED "on" intervals (the
// dash layout — plain, offset-anchored or multi-anchored — or one solid run),
// then opens the given `gaps` on top WITHOUT re-phasing: the gaps are
// subtracted from the intervals and each surviving run is stroked BUTT with an
// explicit per-end cap — the gap ends carry the GAP's cap (independent of the
// stroke), the other ends the stroke's own cap. A free function so
// `TessellateStroke` stays small and readable.
void StrokeIntervalsWithGaps(const Polyline& spine, const Stroke& stroke,
                             double w, double tol, double total,
                             const std::vector<std::pair<double,double>>& on,
                             const std::vector<GapSpan>& gaps, Emitter& em) {
    // On-intervals carry, per END, whether that end abuts a GAP (its cap is the
    // GAP's, independent of the stroke) or is an ordinary dash/spine end (the
    // stroke's own cap). Every interval is stroked BUTT, then each end gets
    // exactly one explicit cap — so a Butt gap shows no round bleeding in.
    std::vector<Iv> onIv;
    for (const auto& p : on)
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
        // Caps are added below, per end. A dashed run keeps SQUARE ends: the
        // butt tilt is a solid-stroke feature (tilting every dash boundary
        // would fight the explicit gap caps).
        Stroke bs = stroke; bs.cap = CapStyle::Butt; bs.capAngle = 0.0;
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
    if (source && (!stroke.marks.empty() || !stroke.repeats.empty()))
        placeSpines = Flatten(*source, kMarkPlaceTolerance);

    for (std::size_t subI = 0; subI < polylines.size(); ++subI) {
        const Polyline& src = polylines[subI];
        if (src.points.size() < 2) continue;

        // Alignment → a center stroke on a shifted spine (StrokeAlign). Left /
        // Right offset by a fixed sign along the walk direction. Inside /
        // Outside are shape-relative: a CLOSED path takes its winding, an OPEN
        // one has no interior so the LOCAL CURVATURE decides and the side SWAPS
        // at every inflection — `sgn` carries that per-vertex sign and the
        // offset crosses the path along its normal there.
        const double amt = stroke.AlignOffsetUnits();
        double shift = 0.0;
        std::vector<double> sgn;
        switch (stroke.align) {
        case StrokeAlign::Center: break;
        case StrokeAlign::Right:  shift =  amt; break;
        case StrokeAlign::Left:   shift = -amt; break;
        default: {
            const bool outside = stroke.align == StrokeAlign::Outside;
            if (src.closed) {
                const double side = SignedArea(src.points) > 0.0 ? 1.0 : -1.0;
                shift = (outside ? -side : side) * amt;
            } else {
                sgn = CurvatureSides(src, outside);
            }
            break;
        }
        }

        const Polyline spine =
            !sgn.empty()     ? OffsetSpineSigned(src, amt, sgn)
          : (shift != 0.0)   ? OffsetSpine(src, shift)
                             : src;

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
            anchors.push_back({ at, m.phase == MarkPhase::Dash,
                                m.anchorSize });
        }
        // SEVERAL anchors (or a forced feature size) → the multi-anchor
        // layout; a single plain anchor keeps the legacy offset re-phase.
        const bool multiAnchor =
            !stroke.dashPattern.empty() &&
            (anchors.size() > 1 ||
             (anchors.size() == 1 && anchors.front().forcedSize > 1e-9));

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

        if (gaps.empty() && !multiAnchor) {
            // No gaps → stroke the whole (possibly CLOSED) spine directly, so a
            // cyclic path keeps its start/end seam joined. ExtractRun would drop
            // the `closed` flag and leave the seam open.
            if (stroke.dashPattern.empty()) {
                // The alignment offset at each end — what a tilted butt cap
                // needs to find the construction vertex it pivots on.
                const double offS = sgn.empty() ? shift : amt * sgn.front();
                const double offE = sgn.empty() ? shift : amt * sgn.back();
                CenterStroke(spine, stroke, w * 0.5, tol, em, offS, offE);
            } else {
                double offset = stroke.dashOffset;
                if (!anchors.empty())
                    offset = AnchorDashOffset(stroke.dashPattern,
                                              anchors.front().elementCentred,
                                              anchors.front().at, offset);
                // Dashes keep SQUARE ends: tilting every dash boundary is not
                // what the cap angle is for (it caps the LINE, not each dash).
                Stroke ds = stroke; ds.capAngle = 0.0;
                for (const Polyline& piece :
                     DashSplit(spine, stroke.dashPattern, offset))
                    CenterStroke(piece, ds, w * 0.5, tol, em);
            }
        } else {
            // Gap objects open the line LOCALLY on top of the dashing (never
            // re-phasing it), and/or SEVERAL dash anchors lay the pattern out
            // piecewise. Build the ON intervals, then delegate the gap
            // subtraction + per-end capping to StrokeIntervalsWithGaps.
            std::vector<std::pair<double,double>> onIv;
            if (stroke.dashPattern.empty()) {
                onIv.push_back({ 0.0, total });
            } else if (multiAnchor) {
                onIv = AnchoredDashIntervals(total, stroke.dashPattern,
                                             anchors, stroke.dashFit);
            } else {
                double offset = stroke.dashOffset;
                if (!anchors.empty())
                    offset = AnchorDashOffset(stroke.dashPattern,
                                              anchors.front().elementCentred,
                                              anchors.front().at, offset);
                onIv = DashIntervals(total, stroke.dashPattern, offset);
            }
            StrokeIntervalsWithGaps(spine, stroke, w, tol, total, onIv, gaps,
                                    em);
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

        // ── REPEAT runs → stroke-coloured Fusion primitives baked into the
        // stroke mesh (one drawing, one alpha — no double transparency).
        // Blend / Cut / recoloured runs are emitted by the Scene instead.
        // The shape is triangulated ONCE and its mesh is stamped per placement
        // through a rigid frame (tangent + inclination) — cheap even for
        // thousands of ticks.
        for (const StrokeRepeat& rep : stroke.repeats) {
            if (!rep.enabled) continue;
            // ADD (Fusion) always fuses into the stroke mesh in the stroke
            // colour (the colour toggle is hidden for Add); Blend / Cut emit
            // separately (the Scene).
            if (rep.mode != MarkObjectMode::Fusion) continue;
            const auto places =
                RepeatObjectPlacements(placeSpine, stroke, rep, (int)subI);
            if (places.empty()) continue;
            const bool isLine = rep.shape == MarkShape::Line;
            // A non-Line shape is triangulated ONCE and stamped; a Line's
            // geometry depends on its per-placement offset (its start sits at
            // the offset point), so it is rebuilt per placement.
            Mesh shapeMesh;
            if (!isLine) {
                MarkObject obj;
                obj.shape = rep.shape;
                obj.size = rep.size;
                obj.width = rep.width;
                obj.sizePercent = rep.sizePercent;
                const PathData shape = MarkPrimitiveShape(obj, w);
                if (shape.Empty()) continue;
                shapeMesh = TriangulateFill(Flatten(shape, tol),
                                            FillRule::NonZero);
                if (shapeMesh.Empty()) continue;
            }
            ArcFrame raf;
            raf.Build(placeSpine);
            if (!raf.Valid()) continue;
            const double cca = std::cos(rep.rotation);
            const double ssa = std::sin(rep.rotation);
            const double lhu = std::max(1e-6, rep.SizeUnits(w));
            const double lhv = std::max(1e-6, rep.WidthUnits(w));
            const bool centred = rep.side == RepeatSide::Center;
            const bool repSmooth = rep.orient == MarkOrient::Smoothed;
            for (const RepeatPlacement& rp2 : places) {
                DVec2 p0; V2 t0;
                raf.Sample(std::clamp(rp2.at, 0.0, raf.total), p0, t0,
                           repSmooth);
                const V2 n0 = Perp(t0);
                const DVec2 at{ p0.x + n0.x * rp2.offset,
                                p0.y + n0.y * rp2.offset };
                // Frame: +x → tangent, +y → left normal, spun by `rotation`.
                const double ux = t0.x * cca - n0.x * ssa;
                const double uy = t0.y * cca - n0.y * ssa;
                const double vx = t0.x * ssa + n0.x * cca;
                const double vy = t0.y * ssa + n0.y * cca;
                if (isLine) {
                    // Build the Line's corners in node space.
                    const PathData lp = MarkLineShape(lhu, lhv, rp2.offset,
                                                      rp2.dir, centred,
                                                      rep.lineJoin);
                    if (lp.subpaths.empty()) continue;
                    std::vector<DVec2> corners;
                    for (const Anchor& a : lp.subpaths.front().anchors)
                        corners.push_back({ at.x + ux * a.pos.x + vx * a.pos.y,
                                            at.y + uy * a.pos.x + vy * a.pos.y });
                    if (corners.size() < 3) continue;
                    if (rep.lineClip && !centred) {
                        // Cut whatever crosses to the FAR side of the path,
                        // following the real curve (boolean subtract).
                        const double ext =
                            2.0 * lhu + lhv + std::abs(rp2.offset);
                        std::vector<Polyline> rings;
                        for (auto& r : ClipPolygonToPathSide(
                                 corners, placeSpine, rp2.at, -rp2.dir, ext)) {
                            Polyline pl; pl.points = std::move(r);
                            pl.closed = true;
                            rings.push_back(std::move(pl));
                        }
                        const Mesh cm = TriangulateFill(rings, FillRule::NonZero);
                        const std::uint32_t base = out.VertexCount();
                        for (std::size_t i2 = 0; i2 + 1 < cm.positions.size();
                             i2 += 2)
                            em.V(cm.positions[i2], cm.positions[i2 + 1]);
                        for (std::uint32_t idx : cm.indices)
                            out.indices.push_back(base + idx);
                        continue;
                    }
                    const std::uint32_t base = out.VertexCount();
                    for (const DVec2& c : corners) em.V(c.x, c.y);
                    for (std::size_t k = 1; k + 1 < corners.size(); ++k) {
                        out.indices.push_back(base);
                        out.indices.push_back(base + (std::uint32_t)k);
                        out.indices.push_back(base + (std::uint32_t)k + 1);
                    }
                    continue;
                }
                const std::uint32_t base = out.VertexCount();
                for (std::size_t i2 = 0; i2 + 1 < shapeMesh.positions.size();
                     i2 += 2) {
                    const double qx = shapeMesh.positions[i2];
                    const double qy = shapeMesh.positions[i2 + 1];
                    em.V(at.x + ux * qx + vx * qy, at.y + uy * qx + vy * qy);
                }
                for (std::uint32_t idx : shapeMesh.indices)
                    out.indices.push_back(base + idx);
            }
        }
    }
    return out;
}

PathData MarkPrimitiveShape(const MarkObject& obj, double strokeWidth) {
    if (obj.shape == MarkShape::Instance) return PathData{};
    // `size` is HALF-extent along the tangent (rectangle length / circle radius
    // / diamond diagonal-half); `width` the rectangle/triangle half-height. The
    // geometry is parametric so the GeometryCache re-tessellates it per zoom
    // tier.
    const double hu = std::max(1e-6, obj.SizeUnits(strokeWidth));
    if (obj.shape == MarkShape::Circle)
        return PathData::Ellipse(0, 0, hu, hu);
    if (obj.shape == MarkShape::Rectangle) {
        const double hv = std::max(1e-6, obj.WidthUnits(strokeWidth));
        return PathData::Rect(-hu, -hv, hu * 2.0, hv * 2.0);
    }
    if (obj.shape == MarkShape::Triangle) {
        // Isoceles: base across the full along-extent, apex on +y.
        const double hv = std::max(1e-6, obj.WidthUnits(strokeWidth));
        return PathData::Polygon({ { -hu, -hv }, { hu, -hv }, { 0, hv } },
                                 true);
    }
    if (obj.shape == MarkShape::HalfCircle) {
        // Chord ON the line (y = 0), Bézier dome on +y (two quarter arcs).
        PathData p;
        Subpath sp;
        sp.closed = true;
        const double k = 0.5522847498307936 * hu;   // circle kappa
        Anchor a0; a0.pos = { -hu, 0 }; a0.hasOut = true; a0.out = { 0, k };
        Anchor a1; a1.pos = { 0, hu };
        a1.hasIn = true;  a1.in  = { -k, 0 };
        a1.hasOut = true; a1.out = { k, 0 };
        Anchor a2; a2.pos = { hu, 0 }; a2.hasIn = true; a2.in = { 0, k };
        sp.anchors = { a0, a1, a2 };
        p.subpaths.push_back(std::move(sp));
        return p;
    }
    if (obj.shape == MarkShape::Line) {
        // A rigid rectangle CENTRED (the side-aware placement of a repeat Line
        // uses MarkLineShape instead — a mark-object Line just reads as a
        // rectangle across the line). `size` is the FULL length across the
        // line, `width` the FULL thickness along it.
        const double hv = std::max(1e-6, obj.WidthUnits(strokeWidth));
        return PathData::Rect(-hv * 0.5, -hu * 0.5, hv, hu);
    }
    // Diamond: a 4-point polygon, `size` = the half-diagonal.
    return PathData::Polygon({ { hu, 0 }, { 0, hu }, { -hu, 0 }, { 0, -hu } },
                             true);
}

PathData MarkLineShape(double lineLen, double lineThick, double offset,
                       double dir, bool centred, bool join) {
    // `lineLen` / `lineThick` are the FULL length / thickness (the values the
    // user sets), so half-extents are half of them.
    const double hu = std::max(1e-6, lineLen * 0.5);
    const double hv = std::max(1e-6, lineThick * 0.5);
    // Local frame: +y is the left normal, origin at the placement (offset)
    // point. Thickness spans x ∈ [−hv, hv]; length spans y (across the line).
    double yLo, yHi;
    if (centred) {
        yLo = -hu; yHi = hu;               // straddles the stroke (total lineLen)
    } else {
        const double d = dir >= 0.0 ? 1.0 : -1.0;   // side direction (≠ 0)
        double yNear = 0.0;                // the offset point
        double yFar  = d * 2.0 * hu;       // reach out by the full length
        if (join) yNear = -offset;         // back to the stroke (stroke at −off)
        yLo = std::min(yNear, yFar);  yHi = std::max(yNear, yFar);
    }
    return PathData::Rect(-hv, yLo, hv * 2.0, yHi - yLo);
}

std::vector<std::vector<DVec2>>
ClipPolygonToPathSide(const std::vector<DVec2>& poly, const Polyline& path,
                      double atArc, double farSign, double ext) {
    if (poly.size() < 3) return { poly };
    const double total = PolyTotal(path);
    if (total < 1e-9) return { poly };
    const double e = std::max(1e-3, ext);
    // A local span wide enough to bracket the line; the far side is closed by a
    // big offset so the region covers the whole far half near the line.
    const double span = 2.0 * e;
    const double reach = 6.0 * e;
    const double lo = std::clamp(atArc - span, 0.0, total);
    const double hi = std::clamp(atArc + span, 0.0, total);
    if (hi - lo < 1e-6) return { poly };
    const double sd = farSign >= 0.0 ? 1.0 : -1.0;
    // The region boundary FOLLOWS the ALREADY-flattened path over [lo,hi] (its
    // real vertices, not a coarse re-sample) — so the cut is as fine as the
    // path itself, curving exactly along it with no large facets.
    const Polyline run = ExtractRun(path, lo, hi);
    if (run.points.size() < 2) return { poly };
    const std::size_t rn = run.points.size();
    const V2 tFront = Norm(Sub(run.points[1], run.points[0]));
    const V2 tBack  = Norm(Sub(run.points[rn - 1], run.points[rn - 2]));
    const V2 nFront = Perp(tFront), nBack = Perp(tBack);
    std::vector<DVec2> region = run.points;
    // Close on the far side (path back → far-back → far-front → path front).
    region.push_back({ run.points[rn - 1].x + sd * nBack.x * reach,
                       run.points[rn - 1].y + sd * nBack.y * reach });
    region.push_back({ run.points[0].x + sd * nFront.x * reach,
                       run.points[0].y + sd * nFront.y * reach });
    auto res = BooleanPolygons({ poly }, { region }, BoolOp::Subtract);
    if (res.empty()) return { poly };   // boolean bailed → leave uncut
    return res;
}

std::vector<DVec2> ClipConvexHalfPlane(const std::vector<DVec2>& poly,
                                       DVec2 lineP, DVec2 keepNormal) {
    std::vector<DVec2> out;
    const std::size_t n = poly.size();
    if (n < 3) return poly;
    const double nl = std::hypot(keepNormal.x, keepNormal.y);
    if (nl < 1e-12) return poly;
    const DVec2 kn{ keepNormal.x / nl, keepNormal.y / nl };
    auto side = [&](const DVec2& p) {
        return (p.x - lineP.x) * kn.x + (p.y - lineP.y) * kn.y;   // ≥0 = keep
    };
    for (std::size_t i = 0; i < n; ++i) {
        const DVec2 a = poly[i], b = poly[(i + 1) % n];
        const double da = side(a), db = side(b);
        if (da >= 0.0) out.push_back(a);
        if ((da < 0.0) != (db < 0.0)) {          // edge crosses the line
            const double t = da / (da - db);
            out.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
        }
    }
    return out;
}

DMat23 MarkPlaceMatrix(const Polyline& spine, const StrokeMark& mark,
                       const MarkObject& obj, double strokeWidth,
                       double bendHalfExtent) {
    DMat23 id;
    ArcFrame af;
    af.Build(spine);
    if (!af.Valid()) return id;
    const double total = af.total;
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
    const bool smoothTan = obj.orient == MarkOrient::Smoothed;
    af.Sample(d0, p0, t0, smoothTan);
    V2 n0 = Perp(t0);
    // Side across the line: the object's own side/offset when it overrides,
    // else the mark's.
    const MarkSide sd = obj.sideInherit ? mark.side : obj.side;
    const double soff = obj.sideInherit ? mark.OffsetUnits(strokeWidth)
                                        : obj.SideOffsetUnits(strokeWidth);
    double off = 0.0;
    if (sd == MarkSide::Left)  off =  soff;
    if (sd == MarkSide::Right) off = -soff;
    // CHORD: pin the two transverse crossings (local ±hu, v=0) to the curve —
    // the frame's +x axis spans the CHORD between the arc points d0±hu (so its
    // magnitude compresses to the chord length), origin at the chord midpoint,
    // +y the chord's left normal. A diamond's two angles then land exactly on
    // the line, the shape spanning straight between them. Rigid otherwise.
    if (obj.bend == MarkBend::Chord) {
        const double hu = bendHalfExtent > 0.0
                              ? bendHalfExtent
                              : std::max(1e-6, obj.SizeUnits(strokeWidth));
        DVec2 pa, pb; V2 ta, tb;
        af.Sample(std::clamp(d0 + hu, 0.0, total), pa, ta, smoothTan);
        af.Sample(std::clamp(d0 - hu, 0.0, total), pb, tb, smoothTan);
        p0 = { (pa.x + pb.x) * 0.5, (pa.y + pb.y) * 0.5 };
        V2 chord{ (pa.x - pb.x) / (2.0 * hu), (pa.y - pb.y) / (2.0 * hu) };
        if (std::abs(chord.x) < 1e-12 && std::abs(chord.y) < 1e-12)
            chord = t0;
        t0 = chord;                 // +x axis (already scaled to the chord)
        n0 = Perp(Norm(chord));     // unit left normal (height preserved)
    }
    const DVec2 at{ p0.x + n0.x * off, p0.y + n0.y * off };
    // Frame axes: local +x → tangent (or chord), local +y → left normal, then
    // the object's own spin.
    const double ca = std::cos(obj.rotation), sa = std::sin(obj.rotation);
    double ux = t0.x * ca - n0.x * sa, uy = t0.y * ca - n0.y * sa;   // +x axis
    double vx = t0.x * sa + n0.x * ca, vy = t0.y * sa + n0.y * ca;   // +y axis
    // BEND: shear the transverse axis along the tangent by the slope the curve
    // gains over the shape's half-extent — the long edges lean with the line
    // while the shape stays affine. `shear = tan(Δθ)` where Δθ is the AVERAGE
    // tangent rotation over [d0−hu, d0+hu] (symmetric — the lean matches the
    // curve on both sides of the mark, and moves smoothly as the mark slides).
    // `bendHalfExtent` overrides hu for INSTANCES (their `size` is a scale
    // factor, not a length — the geometric extent is measured by the caller).
    if (obj.bend == MarkBend::Bend) {
        const double hu = bendHalfExtent > 0.0
                              ? bendHalfExtent
                              : std::max(1e-6, obj.SizeUnits(strokeWidth));
        DVec2 pa, pb; V2 ta, tb;
        af.Sample(std::clamp(d0 + hu, 0.0, total), pa, ta, smoothTan);
        af.Sample(std::clamp(d0 - hu, 0.0, total), pb, tb, smoothTan);
        double dth = 0.5 * std::atan2(Cross(tb, ta), Dot(tb, ta));  // half-turn
        dth = std::clamp(dth, -1.3, 1.3);                           // stable
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

void SampleSpineFrame(const Polyline& spine, double at, bool smooth,
                      DVec2& outP, DVec2& outT) {
    outP = spine.points.empty() ? DVec2{ 0, 0 } : spine.points.front();
    outT = { 1, 0 };
    ArcFrame af;
    af.Build(spine);
    if (!af.Valid()) return;
    V2 t;
    af.Sample(at, outP, t, smooth);
    outT = { t.x, t.y };
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
    // Sampling density follows the CALLER's tolerance: the stroker passes the
    // zoom tier's tolerance (Fusion rings re-tessellate per tier → vector-
    // smooth at any zoom); the Scene passes kMarkRingTolerance for its
    // compile-once Blend/Cut rings. Floored, and every loop below hard-caps
    // its point count so the O(n²) ring triangulation stays bounded.
    const double tol = std::max(tolerance > 0.0 ? tolerance : 0.25, 1e-4);
    const double hu = std::max(1e-6, obj.SizeUnits(strokeWidth));
    const double hv = obj.shape == MarkShape::Rectangle
                          ? std::max(1e-6, obj.WidthUnits(strokeWidth)) : hu;
    CurvePlacer cp;
    if (!cp.Init(spine, mark, obj, strokeWidth, hu)) return false;
    auto place = [&](double u, double v) { return cp.Place(u, v); };
    // A shape EDGE from local (u0,v0) to (u1,v1). FOLLOW resamples it (density
    // from the tolerance, hard-capped) so its curved image stays smooth; BEND
    // keeps it a STRAIGHT segment between the two (already curve-placed)
    // corners. The last point is NOT emitted (the next edge's first point
    // continues the ring) — avoids a duplicate seam vertex.
    const double step = std::max(4.0 * tol, 1e-3);
    auto edge = [&](double u0, double v0, double u1, double v1) {
        int n = 1;
        if (follow) {
            const double len = std::hypot(u1 - u0, v1 - v0);
            n = (int)std::ceil(len / step);
            // Cap tight: the ring is ear-clipped O(n²) — and re-built EVERY
            // FRAME while a Subtract ghost live-moves.
            n = n < 1 ? 1 : (n > 256 ? 256 : n);
        }
        for (int i = 0; i < n; ++i) {
            const double f = (double)i / (double)n;
            outRing.push_back(place(u0 + (u1 - u0) * f, v0 + (v1 - v0) * f));
        }
    };
    if (obj.shape == MarkShape::Circle) {
        // One closed loop; sample its outline so the bent circle reads smooth.
        // Each point is placed through the curve frame at its own arc; the
        // count tracks the chord error at the given tolerance.
        const double err = std::min(tol / hu, 0.999);
        int steps = (int)std::ceil(6.28318530717958 /
                                   (2.0 * std::acos(1.0 - err)));
        steps = steps < 24 ? 24 : (steps > 512 ? 512 : steps);
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
    } else if (obj.shape == MarkShape::Triangle) {
        const double hw2 = std::max(1e-6, obj.WidthUnits(strokeWidth));
        edge(-hu, -hw2,  hu, -hw2);  // base (along the line)
        edge( hu, -hw2,  0,  hw2);   // right slope
        edge( 0,  hw2, -hu, -hw2);   // left slope
    } else if (obj.shape == MarkShape::HalfCircle) {
        // Dome sampled through the curve frame + the flat chord back.
        const double err = std::min(tol / hu, 0.999);
        int steps = (int)std::ceil(3.14159265358979 /
                                   (2.0 * std::acos(1.0 - err)));
        steps = steps < 12 ? 12 : (steps > 256 ? 256 : steps);
        for (int i = 0; i <= steps; ++i) {
            const double a = 3.14159265358979 * (double)i / (double)steps;
            outRing.push_back(place(-std::cos(a) * hu, std::sin(a) * hu));
        }
        // chord hu → −hu (the next-first point closes the ring)
    } else {   // Diamond
        edge( hu, 0, 0,  hu);
        edge( 0, hu, -hu, 0);
        edge(-hu, 0, 0, -hu);
        edge( 0, -hu, hu, 0);
    }
    return outRing.size() >= 3;
}

PathData MarkBendPath(const Polyline& spine, const StrokeMark& mark,
                      const MarkObject& obj, double strokeWidth,
                      const PathData& target, double targetScale,
                      double tolerance) {
    PathData out;
    if (!BendsAlongCurve(obj.bend) || target.Empty()) return out;
    const bool follow = obj.bend == MarkBend::Follow;
    const double k = std::max(1e-6, targetScale);
    const double tol = std::max(tolerance > 0.0 ? tolerance : 0.25, 1e-4);
    // Flatten in TARGET-local units at a tolerance that lands at `tol` AFTER
    // the scale, then measure the scaled extent (the Bend-shear span and the
    // curvature-probe step derive from it).
    const auto flat = Flatten(target, tol / k);
    double ext = 1e-3;
    for (const Polyline& pl : flat)
        for (const DVec2& p : pl.points)
            ext = std::max(ext, std::hypot(p.x, p.y) * k);
    CurvePlacer cp;
    if (!cp.Init(spine, mark, obj, strokeWidth, ext)) return out;
    const double step = std::max(4.0 * tol, 1e-3);
    for (const Polyline& pl : flat) {
        const std::size_t n = pl.points.size();
        if (n < 2) continue;
        Subpath sp;
        sp.closed = pl.closed;
        const std::size_t sc = pl.closed ? n : n - 1;
        for (std::size_t i = 0; i < sc && sp.anchors.size() <= 4096; ++i) {
            const DVec2 a{ pl.points[i].x * k, pl.points[i].y * k };
            const DVec2 b{ pl.points[(i + 1) % n].x * k,
                           pl.points[(i + 1) % n].y * k };
            int steps = 1;
            if (follow) {   // resample straight runs so their image curves
                const double len = std::hypot(b.x - a.x, b.y - a.y);
                steps = (int)std::ceil(len / step);
                steps = steps < 1 ? 1 : (steps > 128 ? 128 : steps);
            }
            for (int s2 = 0; s2 < steps; ++s2) {
                const double f = (double)s2 / (double)steps;
                Anchor an;
                an.pos = cp.Place(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
                sp.anchors.push_back(an);
            }
        }
        if (!pl.closed) {   // the final endpoint of an open subpath
            Anchor an;
            an.pos = cp.Place(pl.points[n - 1].x * k, pl.points[n - 1].y * k);
            sp.anchors.push_back(an);
        }
        if (sp.anchors.size() >= 2) out.subpaths.push_back(std::move(sp));
    }
    return out;
}

std::vector<RepeatPlacement> RepeatObjectPlacements(const Polyline& spine,
                                                    const Stroke& stroke,
                                                    const StrokeRepeat& rep,
                                                    int sub) {
    std::vector<RepeatPlacement> out;
    if (!rep.enabled) return out;
    ArcFrame af;
    af.Build(spine);
    if (!af.Valid()) return out;
    const double total = af.total;
    const double w = stroke.width;
    const double hu = std::max(1e-6, rep.SizeUnits(w));
    const int    gN = rep.groupCount < 1 ? 1 : rep.groupCount;
    const double gHalf = rep.GroupHalfExtent(w);

    // Base pitch — group CENTRE-to-centre distance.
    double P;
    switch (rep.distribute) {
    case RepeatDistribute::Gap:     P = rep.gap + gHalf * 2.0;               break;
    case RepeatDistribute::Count:   P = total / std::max(1, rep.count);      break;
    case RepeatDistribute::Density: P = 100.0 / std::max(1e-3, rep.density); break;
    case RepeatDistribute::Pitch:
    default:                        P = rep.pitch;                           break;
    }
    P = std::max(P, 1e-3);
    // Trim the usable range at both ends. An OUTSIDE-measured trim is to the
    // group's outer EDGE, so the centre has to sit a further half-extent in —
    // the trim then reads as the clear gap before the first object starts.
    // A ZERO trim is "no trim" whatever the measure says: adding the half
    // extent there would be a phantom trim that shifts the whole run.
    const bool outsideTrim = rep.trimMeasure == RepeatTrimMeasure::Outside;
    const bool hasStart = rep.startTrim > 1e-9, hasEnd = rep.endTrim > 1e-9;
    const double loPad = (outsideTrim && hasStart) ? gHalf : 0.0;
    const double hiPad = (outsideTrim && hasEnd)   ? gHalf : 0.0;
    const double lo = std::clamp(rep.startTrim + loPad, 0.0, total);
    const double hi = std::clamp(total - rep.endTrim - hiPad, lo, total);
    // A trim PINS the run to its boundary: the first group lands exactly there,
    // which is what asking for a trim means. Untrimmed, there is nothing to pin
    // to, so the run centres itself in its first cell as before.
    const double lead = hasStart ? 0.0 : P * 0.5;
    const bool stretch = rep.fit == DashFit::ScaleBoth;  // else keep exact P

    // Repeat-anchor marks of THIS subpath → pinned group centres. A Between
    // anchor pins the MIDDLE of the space between two groups (one centre at
    // anchor ± P/2). A mark's `repeatGap` (> 0) overrides the pitch of the
    // segment that STARTS at it.
    struct Cst { double at; bool between; double pitch; };
    std::vector<Cst> csts;
    for (const StrokeMark& m : stroke.marks) {
        if (m.sub != sub || m.repeatAnchor == MarkRepeatAnchor::None) continue;
        const double tc = m.t < 0 ? 0 : m.t > 1 ? 1 : m.t;
        csts.push_back({ tc * total,
                         m.repeatAnchor == MarkRepeatAnchor::Between,
                         m.repeatGap > 1e-9 ? m.repeatGap : 0.0 });
    }
    std::sort(csts.begin(), csts.end(),
              [](const Cst& a, const Cst& b) { return a.at < b.at; });

    std::vector<double> centres;
    constexpr std::size_t kMaxPlacements = 20000;   // runaway-density guard
    auto push = [&](double c) {
        if (c >= lo - 1e-9 && c <= hi + 1e-9)
            centres.push_back(std::clamp(c, lo, hi));
    };
    if (csts.empty()) {
        for (double c = lo + rep.phase + lead;
             c <= hi + 1e-9 && centres.size() < kMaxPlacements; c += P)
            push(c);
    } else {
        // Piecewise: one centre pinned per anchor, then steps filling each
        // segment — stretched to a whole count (ScaleBoth) or kept at the
        // exact pitch (the other fits). The pitch of a segment is the pin's
        // repeatGap override when set, else the run pitch.
        std::vector<double> pins;
        for (const Cst& c : csts)
            pins.push_back(c.between ? c.at - P * 0.5 : c.at);
        for (double c = pins.front() - P;
             c >= lo - 1e-9 && centres.size() < kMaxPlacements; c -= P)
            push(c);
        for (std::size_t i = 0; i < pins.size(); ++i) {
            push(pins[i]);
            const double segP = csts[i].pitch > 0.0 ? csts[i].pitch : P;
            const double segFrom = pins[i] + (csts[i].between ? segP : 0.0);
            if (csts[i].between) push(segFrom);   // the twin across the gap
            if (i + 1 < pins.size()) {
                const double L = pins[i + 1] - segFrom;
                if (L > segP * 0.5) {
                    if (stretch) {
                        const long nSeg = std::max(1l, std::lround(L / segP));
                        const double Pp = L / (double)nSeg;
                        for (long j2 = 1; j2 < nSeg &&
                                          centres.size() < kMaxPlacements; ++j2)
                            push(segFrom + Pp * (double)j2);
                    } else {
                        for (double c = segFrom + segP;
                             c < pins[i + 1] - segP * 0.5 &&
                             centres.size() < kMaxPlacements; c += segP)
                            push(c);
                    }
                }
            }
        }
        const double lastP = csts.back().pitch > 0.0 ? csts.back().pitch : P;
        for (double c = pins.back() + (csts.back().between ? lastP : 0.0) + lastP;
             c <= hi + 1e-9 && centres.size() < kMaxPlacements; c += lastP)
            push(c);
        std::sort(centres.begin(), centres.end());
    }

    // Across-the-line offset per placement. Inside/Outside: interior winding
    // on a CLOSED subpath, the local curvature side on an open one.
    const double offU = rep.SideOffsetUnits(w);
    const bool closed = spine.closed;
    const double area = closed ? SignedArea(spine.points) : 0.0;
    // The SIDE direction sign at `at` (+1 = left normal, −1 = right, 0 =
    // centred) — separate from the offset MAGNITUDE, so a Line at a 0 % offset
    // still knows which way to reach.
    auto dirAt = [&](double at) -> double {
        switch (rep.side) {
        case RepeatSide::Left:  return  1.0;
        case RepeatSide::Right: return -1.0;
        case RepeatSide::Inside:
        case RepeatSide::Outside: {
            double s;
            if (closed) {
                s = area > 0.0 ? 1.0 : -1.0;   // interior = +normal when CCW
            } else {
                const double ds = std::max(1e-3, hu);
                DVec2 pA, pB; V2 tA, tB;
                af.Sample(std::min(total, at + ds), pA, tA);
                af.Sample(std::max(0.0,   at - ds), pB, tB);
                const double dTheta = std::atan2(Cross(tB, tA), Dot(tB, tA));
                s = dTheta > 1e-6 ? 1.0 : dTheta < -1e-6 ? -1.0 : 1.0;
            }
            return rep.side == RepeatSide::Inside ? s : -s;
        }
        case RepeatSide::Center:
        default: return 0.0;
        }
    };

    // Groups → objects (each object clamped to the trimmed range).
    out.reserve(std::min(centres.size() * (std::size_t)gN, kMaxPlacements));
    for (double c : centres) {
        for (int j2 = 0; j2 < gN; ++j2) {
            const double at =
                c + ((double)j2 - (double)(gN - 1) * 0.5) * rep.groupPitch;
            if (at < lo - 1e-9 || at > hi + 1e-9) continue;
            const double d = dirAt(at);
            out.push_back({ std::clamp(at, lo, hi), d * offU, d });
            if (out.size() >= kMaxPlacements) return out;
        }
    }
    return out;
}

} // namespace Ink::geom

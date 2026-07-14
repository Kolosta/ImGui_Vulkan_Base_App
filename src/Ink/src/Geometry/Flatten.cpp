#include "Ink/Geometry/Geometry.h"

#include <algorithm>
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

// ── Rational uniform B-spline (NURBS) evaluation — the legacy evaluator, in
// double precision, tolerance-driven. Weighted de Boor over a knot vector
// built from the subpath's options:
//   nurbsBezier   → interior knots at FULL multiplicity (= degree): the hull
//                   acts as consecutive rational Bézier segments (exact
//                   circles / arcs from the classic weighted hulls).
//   nurbsEndpoint → (open) clamped ends: the curve meets the first/last
//                   control point; off = floating uniform (stays inside hull).
//   closed        → periodic uniform (bezier closed wraps ONE point, uniform
//                   wraps `degree`). Adaptive chord-error sampling on the
//   EVALUATED points, so weights "just work" (samples densify where a heavy
//   weight bends the curve). Emits points EXCLUDING the start.
void FlattenNurbs(const Subpath& sp, double tol, std::vector<DVec2>& out,
                  DVec2& startOut) {
    const auto& ctrl = sp.anchors;
    const int n = (int)ctrl.size();
    if (n < 2) return;
    const int k = std::clamp((int)sp.orderU, 2, n);   // order = degree + 1
    const int deg = k - 1;

    struct H { double x, y, w; };
    auto homog = [](const Anchor& c) {
        const double w = c.weight > 1e-6 ? c.weight : 1.0;
        return H{ c.pos.x * w, c.pos.y * w, w };
    };
    std::vector<H> P;
    P.reserve((std::size_t)(n + deg + 1));
    for (const Anchor& c : ctrl) P.push_back(homog(c));
    if (sp.closed) {
        const int wrap = sp.nurbsBezier ? 1 : deg;
        for (int i = 0; i < wrap; ++i) P.push_back(homog(ctrl[(std::size_t)(i % n)]));
    }
    const int m = (int)P.size();
    if (m < k) return;

    std::vector<double> knot;
    if (sp.nurbsBezier) {
        const int segs = std::max(1, (m - 1) / deg);
        for (int i = 0; i < k; ++i) knot.push_back(0.0);
        for (int s = 1; s < segs; ++s)
            for (int r = 0; r < deg; ++r) knot.push_back((double)s);
        for (int i = 0; i < k; ++i) knot.push_back((double)segs);
        while ((int)knot.size() < m + k) knot.push_back((double)segs);
        knot.resize((std::size_t)(m + k));
    } else if (!sp.closed && sp.nurbsEndpoint) {
        for (int i = 0; i < k; ++i) knot.push_back(0.0);
        for (int i = 1; i <= m - k; ++i) knot.push_back((double)i);
        for (int i = 0; i < k; ++i) knot.push_back((double)(m - k + 1));
    } else {
        for (int i = 0; i < m + k; ++i) knot.push_back((double)i);
    }
    const double u0 = knot[(std::size_t)deg];
    const double u1 = knot[(std::size_t)m];

    auto deBoor = [&](double u) -> DVec2 {
        int s = deg;
        while (s < m - 1 && u >= knot[(std::size_t)(s + 1)]) ++s;
        H d[16];   // order clamped well below 16
        for (int j = 0; j <= deg; ++j) d[j] = P[(std::size_t)(s - deg + j)];
        for (int r = 1; r <= deg; ++r)
            for (int j = deg; j >= r; --j) {
                const int i = s - deg + j;
                const double denom =
                    knot[(std::size_t)(i + k - r)] - knot[(std::size_t)i];
                const double a =
                    denom > 1e-12 ? (u - knot[(std::size_t)i]) / denom : 0.0;
                const H& lo = d[j - 1];
                H& hi = d[j];
                hi = { lo.x * (1.0 - a) + hi.x * a,
                       lo.y * (1.0 - a) + hi.y * a,
                       lo.w * (1.0 - a) + hi.w * a };
            }
        const H& r = d[deg];
        const double w = std::abs(r.w) > 1e-12 ? r.w : 1.0;
        return { r.x / w, r.y / w };
    };

    // Coarse uniform seed (≈2 per control point) + adaptive refinement on the
    // evaluated chord error, iterative for bounded depth.
    startOut = deBoor(u0);
    const int seed = std::clamp((m - 1) * 2, 8, 512);
    struct Seg { double ua, ub; DVec2 pa, pb; int depth; };
    for (int i = 0; i < seed; ++i) {
        const double ua = u0 + (u1 - u0) * (double)i / (double)seed;
        const double ub = u0 + (u1 - u0) * (double)(i + 1) / (double)seed;
        const DVec2 pa = i == 0 ? startOut : out.empty() ? startOut : out.back();
        const DVec2 pb = deBoor(ub);
        Seg stack[16];
        int spN = 0;
        stack[spN++] = { ua, ub, pa, pb, 0 };
        while (spN > 0) {
            const Seg s = stack[--spN];
            bool split = false;
            if (s.depth < 12) {
                const double um = 0.5 * (s.ua + s.ub);
                const DVec2 pm = deBoor(um);
                const DVec2 mid{ 0.5 * (s.pa.x + s.pb.x), 0.5 * (s.pa.y + s.pb.y) };
                const double dx = pm.x - mid.x, dy = pm.y - mid.y;
                if (std::sqrt(dx * dx + dy * dy) > tol) {
                    stack[spN++] = { um, s.ub, pm, s.pb, s.depth + 1 };
                    stack[spN++] = { s.ua, um, s.pa, pm, s.depth + 1 };
                    split = true;
                }
            }
            if (!split) out.push_back(s.pb);
        }
    }
}

} // namespace

std::vector<Polyline> Flatten(const PathData& path, double tolerance) {
    const double tol = tolerance > 0.0 ? tolerance : 1e-3;
    std::vector<Polyline> out;
    for (const Subpath& sp : path.subpaths) {
        if (sp.anchors.size() < 2) continue;
        Polyline pl;
        pl.closed = sp.closed;
        if (sp.spline == SplineType::Nurbs) {
            DVec2 start{ 0, 0 };
            FlattenNurbs(sp, tol, pl.points, start);
            if (pl.points.empty()) continue;
            pl.points.insert(pl.points.begin(), start);
        } else if (sp.spline == SplineType::Poly) {
            for (const Anchor& a : sp.anchors) pl.points.push_back(a.pos);
            if (sp.closed) pl.points.push_back(sp.anchors.front().pos);
        } else {
            pl.points.push_back(sp.anchors.front().pos);
            for (std::size_t i = 1; i < sp.anchors.size(); ++i)
                FlattenSegment(sp.anchors[i - 1], sp.anchors[i], tol, pl.points);
            if (sp.closed)
                FlattenSegment(sp.anchors.back(), sp.anchors.front(), tol, pl.points);
        }
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
    for (const Stroke& s : style.strokes) {
        if (!s.enabled) continue;
        inflate = std::max(inflate, s.width);   // full width:
        // covers Center (w/2) and the Lot 3 Inside/Outside bands (w) alike.
        // A mark's OBJECTS reach past the band (a shape sticks out ~size, an
        // off-line mark by its resolved offset) — a conservative envelope.
        for (const StrokeMark& m : s.marks) {
            double reach = std::abs(m.OffsetUnits(s.width));
            for (const MarkObject& o : m.objects)
                reach = std::max(reach, std::max(o.size, o.width) * 1.6);
            inflate = std::max(inflate, s.width + reach);
        }
    }
    b.min.x -= inflate; b.min.y -= inflate;
    b.max.x += inflate; b.max.y += inflate;
    return b;
}

} // namespace Ink::geom

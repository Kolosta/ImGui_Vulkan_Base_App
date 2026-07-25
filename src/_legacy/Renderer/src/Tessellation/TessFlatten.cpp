#include "TessInternal.h"
#include <algorithm>
#include <cmath>

namespace Renderer {

// Flatten one edge between node a and node b: if either side has a handle, treat
// it as a cubic (missing handle = anchor itself), else a straight line. The
// `zoom` argument is ignored for the step count (detail is zoom-independent);
// kept for API/source compatibility.
void FlattenEdge(const Node& a, const Node& b, float /*zoom*/,
                        std::vector<Vec2>& out) {
    bool curved = a.hasOut || b.hasIn;
    if (!curved) { out.push_back(b.pos); return; }
    Vec2 c0 = a.hasOut ? a.hOut : a.pos;
    Vec2 c1 = b.hasIn  ? b.hIn  : b.pos;
    // Step count from BOTH the control-hull length (so long curves get more) AND
    // the curve's "bend" (max control-point deviation from the chord) so a tightly
    // curved short segment still gets enough steps to read as smooth. A high floor
    // (24) guarantees even a tiny, strongly-displayed curve never looks faceted —
    // the result is cached, so the extra triangles are free per frame.
    float approx = Len(c0 - a.pos) + Len(c1 - c0) + Len(b.pos - c1);
    Vec2 chord = b.pos - a.pos; float cl = Len(chord);
    float bend = 0.0f;
    if (cl > 1e-5f) {
        Vec2 n{ -chord.y / cl, chord.x / cl };
        bend = std::max(std::fabs(Dot(c0 - a.pos, n)), std::fabs(Dot(c1 - a.pos, n)));
    } else bend = approx;
    // Subdivision count from the SCREEN chord error, NOT from arc length in doc-units
    // (that made an IOF curve — authored in mm × a large scale — explode to hundreds
    // of segments per edge regardless of its on-screen size). `bend` is the sagitta
    // in doc-units → screen error ≈ bend × pxPerDocUnit; a cubic's flattening error
    // falls ~1/steps², so steps ≈ sqrt(bendPx / tolPx). gDetailScale is the bucketed
    // px-per-doc-unit (so the result is cached per zoom bucket). The cap is now LOW —
    // screen-error subdivision never needs the old 512. A straight edge stays 1 seg.
    const float pxPerUnit = std::max(gDetailScale, 0.05f);
    const float bendPx = std::max(0.0f, bend) * pxPerUnit;
    const float tolPx  = 0.3f / std::max(gQualityPerUnit / 4.0f, 0.05f);  // SetQuality scales it
    int byErr = (int)std::ceil(std::sqrt(bendPx / std::max(tolPx, 1e-3f)) * 2.0f);
    // ZOOM-INDEPENDENT smoothness floor: the screen-error count alone collapses to a
    // few facets when the curve is rendered SMALL (e.g. the Symbol Viewer glyphs /
    // thumbnails, drawn at a low target zoom) — yet a curve with real curvature must
    // read smooth at ANY size. Derive a floor from the RELATIVE bend (sagitta / chord):
    // a flatter arc needs few steps, a strongly curved one many, regardless of zoom.
    int curveFloor = 4;
    if (cl > 1e-5f) {
        float rel = bend / cl;                       // sagitta as a fraction of the chord
        // ~ steps so the chord error stays a small fraction of the bend: a quarter
        // circle (rel≈0.27) → ~16 steps; gentle arcs → fewer. Capped so a wild handle
        // doesn't explode it.
        curveFloor = std::clamp((int)std::ceil(rel * 60.0f) + 6, 6, 48);
    }
    // High cap: with the detail scale no longer clamped, a strongly-bent edge at deep
    // zoom legitimately needs many steps to stay sub-pixel. The cap only guards a
    // pathological single edge; deep-zoom cost is bounded by the visible cull/clip.
    int steps = std::clamp(std::max(byErr, curveFloor), 4, 512);
    Tessellator::FlattenCubic(a.pos, c0, c1, b.pos, steps, out);
}

// ── NURBS (rational B-spline) evaluation ──────────────────────────────────────
// A full rational, non-uniform B-spline (NURBS). The Node positions are CONTROL
// POINTS with per-point `weight`; the curve is evaluated by weighted de Boor in
// homogeneous coords (x·w, y·w, w) then dehomogenised (÷w). This supports EXACT
// conics (circles/arcs) when the control polygon + weights are the classic rational
// forms. Knot vector options (open curves):
//   • endpoint (clamped): degree+1 repeated knots at each end → the curve meets the
//     first/last control point and is tangent to the end edges (arcs, half-circles).
//   • !endpoint (floating): a uniform open knot vector → the curve floats inside the
//     hull (Blender's default NURBS), not touching the ends.
//   • bezier: interior knots take FULL multiplicity (= degree) → the polygon acts as
//     consecutive rational Bézier segments (the exact-circle / exact-arc form).
// Closed → periodic uniform knots (a smooth loop; exact circle from a square hull).
void EvalBSpline(const std::vector<Node>& ctrl, int order, bool closed,
                        bool endpoint, bool bezier, float zoom, std::vector<Vec2>& out) {
    const int n = (int)ctrl.size();
    if (n < 2) { for (const Node& c : ctrl) out.push_back(c.pos); return; }
    int k = std::clamp(order, 2, n);          // order = degree + 1, capped at n
    int deg = k - 1;

    struct H { float x, y, w; };
    auto homog = [](const Node& c) {
        float w = c.weight > 1e-4f ? c.weight : 1.0f;
        return H{ c.pos.x * w, c.pos.y * w, w };
    };
    // Control points (homogeneous). Closed → wrap points to close the loop:
    //   • bezier closed → append ONE point (the first) so the polygon forms whole
    //     rational-Bézier segments around the loop (8-pt square → 4 quarter-arcs);
    //   • uniform closed → append `deg` points for a periodic blend.
    std::vector<H> P; P.reserve(n + deg + 1);
    for (const Node& c : ctrl) P.push_back(homog(c));
    if (closed) {
        const int wrap = bezier ? 1 : deg;
        for (int i = 0; i < wrap; ++i) P.push_back(homog(ctrl[(size_t)(i % n)]));
    }
    const int m = (int)P.size();

    // ── Knot vector (size m + k) ──
    // Two independent axes drive the knots:
    //   bezier   → interior knots get FULL multiplicity (= degree): the polygon is
    //              consecutive rational Bézier segments (exact circles / arcs).
    //   endpoint → (open only) clamp the ends so the curve meets the first/last
    //              control point; off = floating uniform (curve stays inside hull).
    // Closed curves are PERIODIC: bezier gives an exact-circle loop (e.g. the
    // 8-point square hull → a true circle), uniform gives a smooth blended loop.
    std::vector<float> knot;
    if (bezier) {
        // Bézier knots over `m` control points (m already includes the periodic
        // wrap when closed). Breakpoints every `deg` controls, each ×deg; clamp the
        // two ends with k repeats.
        int segs = std::max(1, (m - 1) / deg);
        for (int i = 0; i < k; ++i) knot.push_back(0.0f);                 // start clamp
        for (int s = 1; s < segs; ++s)
            for (int r = 0; r < deg; ++r) knot.push_back((float)s);       // interior ×deg
        for (int i = 0; i < k; ++i) knot.push_back((float)segs);          // end clamp
        while ((int)knot.size() < m + k) knot.push_back((float)segs);
        knot.resize((size_t)(m + k));
    } else if (!closed && endpoint) {
        // Clamped uniform (open): deg+1 repeats at each end, single interior knots.
        for (int i = 0; i < k; ++i) knot.push_back(0.0f);
        for (int i = 1; i <= m - k; ++i) knot.push_back((float)i);
        for (int i = 0; i < k; ++i) knot.push_back((float)(m - k + 1));
    } else {
        // Uniform integer knots: periodic loop (closed) or floating open curve.
        for (int i = 0; i < m + k; ++i) knot.push_back((float)i);
    }
    const float u0 = knot[(size_t)deg];
    const float u1 = knot[(size_t)m];         // last valid parameter

    // Weighted de Boor at u → dehomogenised point.
    auto deBoor = [&](float u) -> Vec2 {
        int s = deg;
        while (s < m - 1 && u >= knot[(size_t)(s + 1)]) ++s;
        std::vector<H> d((size_t)k);
        for (int j = 0; j <= deg; ++j) d[(size_t)j] = P[(size_t)(s - deg + j)];
        for (int r = 1; r <= deg; ++r)
            for (int j = deg; j >= r; --j) {
                int i = s - deg + j;
                float denom = knot[(size_t)(i + k - r)] - knot[(size_t)i];
                float a = denom > 1e-6f ? (u - knot[(size_t)i]) / denom : 0.0f;
                const H& lo = d[(size_t)(j - 1)]; H& hi = d[(size_t)j];
                hi = { lo.x * (1.0f - a) + hi.x * a,
                       lo.y * (1.0f - a) + hi.y * a,
                       lo.w * (1.0f - a) + hi.w * a };
            }
        const H& r = d[(size_t)deg];
        float w = std::fabs(r.w) > 1e-6f ? r.w : 1.0f;
        return Vec2{ r.x / w, r.y / w };
    };

    (void)zoom;
    // ── Adaptive rational chord-error sampling ──────────────────────────────────
    // Subdivide on the SCREEN sagitta of EVALUATED points, not the control-hull
    // length. Measuring on de-Boor-evaluated points makes weights "just work": a high
    // weight pulls the curve so the measured sagitta rises exactly where it bends, so
    // we add samples there and nowhere else. tolPx mirrors FlattenEdge (SetQuality
    // scales it). gDetailScale = bucketed px per doc-unit (the screen scale).
    const float pxPerUnit = std::max(gDetailScale, 0.05f);
    const float tolPx     = 0.3f / std::max(gQualityPerUnit / 4.0f, 0.05f);
    const float tolDoc    = tolPx / pxPerUnit;        // chord tolerance in doc-units
    constexpr int kMaxSpanSamples = 1024;             // safety cap per curve
    constexpr int kMaxDepth = 12;                      // recursion depth guard

    // A coarse uniform seed (one knot-span resolution) so the recursion never steps
    // over a whole oscillation between two seed points; the recursion then refines.
    int segs = std::max(1, (m - 1));                  // ≈ one seed per control point
    int seed = std::clamp(segs * 2, 24, kMaxSpanSamples);

    // Emit points in (ua, ub] adaptively: if the midpoint deviates from the chord by
    // more than tolDoc, split; otherwise emit pb. Iterative (explicit stack, smaller
    // half pushed last so it's processed first → left-to-right output order) to avoid
    // deep recursion and a std::function allocation per curve.
    int emitted = 0;
    struct Seg { float ua, ub; Vec2 pa, pb; int depth; };
    Seg stack[kMaxDepth + 2];
    auto refine = [&](float ua, float ub, Vec2 pa, Vec2 pb) {
        int sp = 0;
        stack[sp++] = { ua, ub, pa, pb, 0 };
        while (sp > 0) {
            Seg s = stack[--sp];
            bool split = false;
            if (s.depth < kMaxDepth && emitted < kMaxSpanSamples) {
                float um = 0.5f * (s.ua + s.ub);
                Vec2  pm = deBoor(um);
                Vec2  mid{ 0.5f * (s.pa.x + s.pb.x), 0.5f * (s.pa.y + s.pb.y) };
                if (Len(pm - mid) > tolDoc) {
                    // Push RIGHT then LEFT so LEFT pops first (in-order emission).
                    stack[sp++] = { um, s.ub, pm, s.pb, s.depth + 1 };
                    stack[sp++] = { s.ua, um, s.pa, pm, s.depth + 1 };
                    split = true;
                }
            }
            if (!split) { out.push_back(s.pb); ++emitted; }
        }
    };

    // Sampling interval per curve type:
    //   • OPEN, touches its ends (clamped / bezier) → sample [u0, u1] INCLUSIVE, so the
    //     stroke begins/ends exactly at the first/last control point. To make the END
    //     CAPS read at the TRUE end tangent (a butt cap is perpendicular to the last
    //     chord — if that chord isn't tangent the cap tilts), force one extra sample a
    //     hair inside each end (u0+ε, u1−ε); the first/last chord is then tangent.
    //   • CLOSED → sample the FULL valid interval [u0, u1] INCLUSIVE, so BOTH curve
    //     extremities are emitted. The caller treats the polyline as cyclic and closes
    //     last→first. If the two ends COINCIDE (a true periodic loop, e.g. a full NURBS
    //     circle: deBoor(u1)==deBoor(u0)), drop the duplicate final sample so the close
    //     is seamless. If they DIFFER (an arc closed into a region, e.g. a half-circle:
    //     ends on the diameter), KEEP both — the cyclic close is then the straight
    //     chord between the real extremities (the diameter), exactly as expected.
    //     (Dropping it unconditionally cut the last extremity → the half-circle closed
    //     on a diagonal; sampling (u0,u1] instead left a straight seam on the circle.)
    //   • OPEN, floating (uniform, no endpoint) → sample the OPEN interval (the curve
    //     never reaches the ends).
    const bool touchEnds = !closed && (endpoint || bezier);
    const float span = u1 - u0;
    const float eps  = span * 1e-3f;
    const size_t startIdx = out.size();

    if (closed) {
        Vec2 p0 = deBoor(u0);
        out.push_back(p0);
        float prevU = u0; Vec2 prevP = p0;
        for (int i = 1; i <= seed; ++i) {
            float u = u0 + span * (float)i / (float)seed;
            if (u > u1) u = u1;
            Vec2 p = deBoor(u);
            refine(prevU, u, prevP, p);
            prevU = u; prevP = p;
        }
        // Only collapse the seam when the two ends are the SAME point (true loop).
        if (out.size() - startIdx >= 2 && Len(prevP - p0) < 1e-4f) out.pop_back();
        return;
    }

    // OPEN curves. For a curve that TOUCHES its ends (clamped / bezier), use COSINE
    // (Chebyshev-like) parameter spacing instead of uniform: samples bunch up toward
    // both ends, so the first/last chord aligns with the TRUE end tangent → butt/square
    // caps read perpendicular to the curve (flat), and the bunched points stay almost
    // collinear so no spurious join/corner appears near the end (the single eps point
    // did create such a corner). A floating (uniform) curve never reaches its ends, so
    // it keeps plain uniform sampling on the open interval.
    (void)eps;
    float prevU;
    if (touchEnds)  prevU = u0;                              // inclusive start
    else          { prevU = u0 + span / (float)seed;         // floating: open start
                    if (prevU >= u1) prevU = std::nextafter(u1, u0); }
    Vec2 prevP = deBoor(prevU);
    if (touchEnds) out.push_back(prevP);                     // emit the inclusive start
    const float kPiHalf = 3.14159265f;
    for (int i = 1; i <= seed; ++i) {
        float t = (float)i / (float)seed;                    // 0..1
        if (touchEnds)                                       // cosine ease toward ends
            t = 0.5f - 0.5f * std::cos(t * kPiHalf);
        float u = u0 + span * t;
        if (!touchEnds && u >= u1) u = std::nextafter(u1, u0);
        if (u > u1) u = u1;
        Vec2 p = deBoor(u);
        refine(prevU, u, prevP, p);
        prevU = u; prevP = p;
    }
    // EXACT end-tangent snap (clamped/bezier only): project the 2nd and 2nd-last
    // emitted points onto the curve's ANALYTIC end tangents (the dehomogenised end
    // control edges P[1]−P[0] and P[m-1]−P[m-2]). The first/last chord is then EXACTLY
    // tangent, so a butt/square cap reads perpendicular to the curve (flat) — fixing
    // the tilted half-circle caps. No vertex is added (no spurious join), the points
    // just slide a hair onto the tangent line through the (unchanged) end point.
    if (touchEnds && out.size() - startIdx >= 3) {
        auto deh = [](H h){ float w = std::fabs(h.w) > 1e-6f ? h.w : 1.0f;
                            return Vec2{ h.x / w, h.y / w }; };
        Vec2 ts = Norm(deh(P[1])     - deh(P[0]));
        Vec2 tl = Norm(deh(P[m - 1]) - deh(P[m - 2]));
        size_t a = startIdx, b = out.size();
        if ((ts.x != 0.0f || ts.y != 0.0f)) {                // start: out[a]→out[a+1]
            Vec2 d = out[a + 1] - out[a]; float t = d.x * ts.x + d.y * ts.y;
            out[a + 1] = Vec2{ out[a].x + ts.x * t, out[a].y + ts.y * t };
        }
        if ((tl.x != 0.0f || tl.y != 0.0f)) {                // end: out[b-1]→out[b-2]
            Vec2 d = out[b - 2] - out[b - 1]; float t = d.x * tl.x + d.y * tl.y;
            out[b - 2] = Vec2{ out[b - 1].x + tl.x * t, out[b - 1].y + tl.y * t };
        }
    }
}

} // namespace Renderer

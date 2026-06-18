#include "Renderer/Tessellation/Tessellator.h"
#include "TessInternal.h"
#include <algorithm>
#include <cmath>

namespace Renderer {

// ── Object-local ↔ world transform ────────────────────────────────────────────
// Geometry is PAGE-RELATIVE: origin + transform place the object within its
// page, then `pageOrigin` (the artboard's top-left) offsets it into the document.
Vec2 Tessellator::WorldTransform(const Shape& s, Vec2 local, Vec2 pageOrigin) {
    Vec2 d{ local.x - s.origin.x, local.y - s.origin.y };
    d.x *= s.transform.scale.x;
    d.y *= s.transform.scale.y;
    float c = std::cos(s.transform.rotate), sn = std::sin(s.transform.rotate);
    Vec2 r{ d.x * c - d.y * sn, d.x * sn + d.y * c };
    return { r.x + s.origin.x + s.transform.translate.x + pageOrigin.x,
             r.y + s.origin.y + s.transform.translate.y + pageOrigin.y };
}

Vec2 Tessellator::InverseTransform(const Shape& s, Vec2 world, Vec2 pageOrigin) {
    Vec2 p{ world.x - pageOrigin.x - s.origin.x - s.transform.translate.x,
            world.y - pageOrigin.y - s.origin.y - s.transform.translate.y };
    float c = std::cos(-s.transform.rotate), sn = std::sin(-s.transform.rotate);
    Vec2 r{ p.x * c - p.y * sn, p.x * sn + p.y * c };
    float sx = std::fabs(s.transform.scale.x) > 1e-6f ? s.transform.scale.x : 1e-6f;
    float sy = std::fabs(s.transform.scale.y) > 1e-6f ? s.transform.scale.y : 1e-6f;
    return { r.x / sx + s.origin.x, r.y / sy + s.origin.y };
}

// ── Bézier flattening ─────────────────────────────────────────────────────────
void Tessellator::FlattenCubic(Vec2 p0, Vec2 c0, Vec2 c1, Vec2 p1,
                               int steps, std::vector<Vec2>& out) {
    if (steps < 1) steps = 1;
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float u = 1.0f - t;
        float w0 = u * u * u, w1 = 3.0f * u * u * t,
              w2 = 3.0f * u * t * t, w3 = t * t * t;
        out.push_back(Vec2{
            w0 * p0.x + w1 * c0.x + w2 * c1.x + w3 * p1.x,
            w0 * p0.y + w1 * c0.y + w2 * c1.y + w3 * p1.y});
    }
}

// Build a part's object-local outline. Parametric primitives are flattened
// directly without mutating them. `sub` selects a subpath of a branched path
// (−1 = the whole nodes[] as a single strand; primitives ignore it).
std::vector<Vec2> Tessellator::OutlinePartLocal(const Part& part, float zoom,
                                                bool& closed, int sub, bool forceClosed) {
    std::vector<Vec2> poly;

    if (part.kind == ShapeKind::Rectangle) {
        closed = true;
        float x0 = part.pos.x, y0 = part.pos.y;
        float x1 = x0 + part.size.x, y1 = y0 + part.size.y;
        poly = { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} };
        return poly;
    }
    if (part.kind == ShapeKind::Ellipse) {
        closed = true;
        Vec2 c{ part.pos.x + part.size.x * 0.5f, part.pos.y + part.size.y * 0.5f };
        float rx = part.size.x * 0.5f, ry = part.size.y * 0.5f;
        // Chord-error segment count (exact sub-pixel circle at any zoom). For a circle
        // of radius R, an n-gon's max chord deviation is R(1 - cos(π/n)); inverting for
        // a screen tolerance tolPx gives n ≈ π / acos(1 - tolPx/(R·pxPerUnit)). Use the
        // larger semi-axis as R (worst case). Clamp the acos domain for tiny radii /
        // huge scale; floor 24 keeps small circles smooth, cap 1024 bounds extremes.
        const float pxPerUnit = std::max(gDetailScale, 0.05f);
        const float tolPx  = 0.3f / std::max(gQualityPerUnit / 4.0f, 0.05f);
        const float R      = std::max(rx, ry);
        const float arg    = std::clamp(1.0f - tolPx / std::max(R * pxPerUnit, 1e-3f),
                                        -1.0f, 1.0f);
        const float denom  = std::acos(arg);
        int segs = (denom > 1e-4f)
                       ? (int)std::clamp(std::ceil(3.14159265f / denom), 24.0f, 1024.0f)
                       : 1024;
        (void)zoom;
        poly.reserve((size_t)segs);
        for (int i = 0; i < segs; ++i) {
            float ang = (float)i / (float)segs * 6.2831853f;
            poly.push_back(Vec2{ c.x + std::cos(ang) * rx, c.y + std::sin(ang) * ry });
        }
        return poly;
    }

    const auto& nodes = part.path.nodes;
    closed = part.path.closed;
    // For FILLING an open curve, flatten it as a ring (the closing edge follows the
    // endpoints' outer handles — straight if none). The reported `closed` stays as
    // authored; only the geometry is closed.
    const bool ringForGeom = closed || forceClosed;
    if (nodes.empty()) return poly;

    // The flat node range for this subpath (whole array when sub < 0).
    int b = 0, e = (int)nodes.size();
    if (sub >= 0) part.path.subRange(sub, b, e);
    if (e - b < 1) return poly;

    // NURBS: control points off the curve → evaluate the rational B-spline.
    if (part.IsCurveLike() && part.spline == SplineType::Nurbs) {
        std::vector<Node> slice(nodes.begin() + b, nodes.begin() + e);
        // FILLING an OPEN NURBS (ringForGeom and not authored-closed):
        //   • Endpoint U on, OR straight-close requested → evaluate the curve OPEN
        //     (it honours weights/endpoint/bezier and touches the ends when clamped),
        //     and let the ring close with a STRAIGHT edge between the two extremities
        //     (the ear-clip closes last→first). This completes the area between the
        //     two non-cyclic ends exactly as asked.
        //   • Endpoint U off and follow-curve → evaluate as a PERIODIC loop (wrap the
        //     control polygon), so the closing boundary follows the curve's own
        //     weighted direction past the ends (the original "follow the weights").
        const bool fillingOpen = ringForGeom && !closed;
        const bool periodicClose =
            fillingOpen && !part.openFillStraight && !part.nurbsEndpoint;
        const bool evalClosed = closed || periodicClose;
        EvalBSpline(slice, part.orderU, evalClosed, part.nurbsEndpoint,
                    part.nurbsBezier, zoom, poly);
        return poly;
    }
    // Poly: straight polyline through the anchors. Bézier: cubic segments from the
    // in/out handles. The closing edge is added when the geometry is a ring.
    poly.push_back(nodes[(size_t)b].pos);
    if (part.IsCurveLike() && part.spline == SplineType::Poly) {
        for (int i = b + 1; i < e; ++i) poly.push_back(nodes[(size_t)i].pos);
        // Poly ring closes straight last→first (the ear-clip bridges it).
    } else {
        for (int i = b; i + 1 < e; ++i)
            FlattenEdge(nodes[(size_t)i], nodes[(size_t)(i + 1)], zoom, poly);
        // Close the ring for a FILL. follow-curve → the closing edge traces the end
        // handles (a smooth continuation); straight → a direct segment between the two
        // ends (the ear-clip bridges last→first, so emit nothing extra).
        if (ringForGeom && (e - b) >= 2 && !part.openFillStraight)
            FlattenEdge(nodes[(size_t)(e - 1)], nodes[(size_t)b], zoom, poly);
    }
    return poly;
}

std::vector<Vec2> Tessellator::OutlinePartSubFilled(const Shape& shape, const Part& part,
                                                    int sub, float zoom, Vec2 pageOrigin) {
    bool ignore = false;
    std::vector<Vec2> poly = OutlinePartLocal(part, zoom, ignore, sub, /*forceClosed=*/true);
    for (Vec2& p : poly) p = WorldTransform(shape, p, pageOrigin);
    return poly;
}

int Tessellator::SubpathCount(const Part& part) {
    if (part.kind == ShapeKind::Rectangle || part.kind == ShapeKind::Ellipse) return 1;
    return std::max(1, part.path.subCount());
}

std::vector<Vec2> Tessellator::OutlinePartSub(const Shape& shape, const Part& part,
                                              int sub, float zoom, bool& closed,
                                              Vec2 pageOrigin) {
    std::vector<Vec2> poly = OutlinePartLocal(part, zoom, closed, sub);
    for (Vec2& p : poly) p = WorldTransform(shape, p, pageOrigin);
    return poly;
}

std::vector<Vec2> Tessellator::OutlinePart(const Shape& shape, const Part& part,
                                           float zoom, bool& closed,
                                           Vec2 pageOrigin) {
    // Single-subpath path / primitive → whole strand (sub = −1). A branched path
    // would wrongly bridge strands as one polyline, so return subpath 0 only.
    int sub = (part.path.subCount() > 1) ? 0 : -1;
    std::vector<Vec2> poly = OutlinePartLocal(part, zoom, closed, sub);
    for (Vec2& p : poly) p = WorldTransform(shape, p, pageOrigin);
    return poly;
}

std::vector<Vec2> Tessellator::Outline(const Shape& shape, float zoom, bool& closed,
                                       Vec2 pageOrigin) {
    closed = false;
    if (shape.Empty()) return {};
    return OutlinePart(shape, shape.MainPart(), zoom, closed, pageOrigin);
}

bool Tessellator::WorldBounds(const Shape& shape, float zoom, Vec2& outMin, Vec2& outMax,
                              Vec2 pageOrigin) {
    bool any = false;
    outMin = { 1e30f, 1e30f }; outMax = { -1e30f, -1e30f };
    for (const Part& part : shape.parts) {
        const int subs = SubpathCount(part);
        for (int sp = 0; sp < subs; ++sp) {
            bool cl = false;
            std::vector<Vec2> poly = OutlinePartSub(shape, part, sp, zoom, cl, pageOrigin);
            for (const Vec2& p : poly) {
                any = true;
                outMin.x = std::min(outMin.x, p.x); outMin.y = std::min(outMin.y, p.y);
                outMax.x = std::max(outMax.x, p.x); outMax.y = std::max(outMax.y, p.y);
            }
        }
    }
    return any;
}

// ── Fills ─────────────────────────────────────────────────────────────────────
void Tessellator::FillConvexFan(const std::vector<Vec2>& poly, const Color& c, Mesh& out) {
    if (poly.size() < 3) return;
    for (size_t i = 1; i + 1 < poly.size(); ++i)
        PushTri(out, poly[0], poly[i], poly[i + 1], c);
}

// Douglas–Peucker simplification of an OPEN polyline [first..last], appending the
// KEPT interior indices (in order) to `keep`. Keeps any point whose perpendicular
// distance to the chord through its kept neighbours exceeds sqrt(tol2). Shape-
// preserving: it never cuts a corner farther than the tolerance, so a concave notch /
// a U's bars stay faithful (unlike dropping low-curvature points, which bridged across
// the shape). Iterative (explicit stack) to avoid deep recursion on a long contour.
static void DPSimplify(const std::vector<Vec2>& p, int first, int last, float tol2,
                       std::vector<int>& keep) {
    if (last <= first + 1) return;
    struct Span { int lo, hi; };
    std::vector<Span> stack;
    stack.push_back({ first, last });
    std::vector<int> mids;            // kept interior indices, collected then sorted
    while (!stack.empty()) {
        Span s = stack.back(); stack.pop_back();
        Vec2 a = p[(size_t)s.lo], b = p[(size_t)s.hi];
        Vec2 ab = b - a; float ab2 = Dot(ab, ab);
        int idxMax = -1; float dMax = 0.0f;
        for (int i = s.lo + 1; i < s.hi; ++i) {
            Vec2 ap = p[(size_t)i] - a;
            float t = ab2 > 1e-12f ? Dot(ap, ab) / ab2 : 0.0f;
            Vec2 proj = a + ab * t;
            Vec2 d = p[(size_t)i] - proj;
            float dd = Dot(d, d);
            if (dd > dMax) { dMax = dd; idxMax = i; }
        }
        if (idxMax >= 0 && dMax > tol2) {
            mids.push_back(idxMax);
            stack.push_back({ s.lo, idxMax });
            stack.push_back({ idxMax, s.hi });
        }
    }
    std::sort(mids.begin(), mids.end());
    keep.insert(keep.end(), mids.begin(), mids.end());
}

void Tessellator::FillPolygonEarClip(const std::vector<Vec2>& polyIn,
                                     const Color& c, Mesh& out) {
    if (polyIn.size() < 3) return;

    // SHAPE-PRESERVING simplification before the O(n²) ear-clip. The adaptive
    // flattener can hand us thousands of points at high zoom; ear-clipping them all
    // froze the rebuild. But CURVATURE-based dropping bridged across the shape (a U
    // got filled across its mouth). Douglas–Peucker at a SUB-PIXEL tolerance instead
    // removes only points that lie within ~⅓ px of the chord through their kept
    // neighbours — so the filled contour follows the construction line / inner / outer
    // edge faithfully at every zoom, while dense smooth runs are thinned for speed.
    const float tolDoc = (0.3f / std::max(gQualityPerUnit / 4.0f, 0.05f))
                         / std::max(gDetailScale, 0.05f);
    std::vector<Vec2> reduced;
    const std::vector<Vec2>* srcPtr = &polyIn;
    if (polyIn.size() > 64) {
        const int m = (int)polyIn.size();
        std::vector<int> keep;
        keep.reserve(polyIn.size());
        // Closed ring: anchor two far-apart points (0 and m/2) so DP can't collapse
        // the whole loop to a line, then simplify each half.
        keep.push_back(0);
        int mid = m / 2;
        DPSimplify(polyIn, 0, mid, tolDoc * tolDoc, keep);
        keep.push_back(mid);
        DPSimplify(polyIn, mid, m - 1, tolDoc * tolDoc, keep);
        keep.push_back(m - 1);   // ring closes m-1 → 0 implicitly
        reduced.reserve(keep.size());
        for (int i : keep) reduced.push_back(polyIn[(size_t)i]);
        srcPtr = &reduced;
    }
    const std::vector<Vec2>& polyRef = *srcPtr;
    const size_t n = polyRef.size();
    if (n < 3) return;
    if (n == 3) { PushTri(out, polyRef[0], polyRef[1], polyRef[2], c); return; }

    // Normalise to CCW (positive shoelace area), so the convex/ear tests below
    // use one consistent winding regardless of how the path was authored (the
    // document is Y-down, but the sign convention is internally consistent).
    std::vector<Vec2> poly = polyRef;
    float area2 = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& p = poly[i]; const Vec2& q = poly[(i + 1) % n];
        area2 += (p.x * q.y - q.x * p.y);
    }
    if (area2 < 0.0f) std::reverse(poly.begin(), poly.end());

    std::vector<int> idx(poly.size());
    for (int i = 0; i < (int)poly.size(); ++i) idx[i] = i;

    // CCW convex test (left turn). Strict point-in-triangle that EXCLUDES points
    // lying exactly on an edge (so collinear neighbours don't block an ear).
    auto isConvex = [&](Vec2 a, Vec2 b, Vec2 d) { return Cross(b - a, d - b) > 1e-6f; };
    auto inTri = [&](Vec2 p, Vec2 a, Vec2 b, Vec2 d) {
        float c1 = Cross(b - a, p - a);
        float c2 = Cross(d - b, p - b);
        float c3 = Cross(a - d, p - d);
        // Strictly inside (all same sign, none ~0) — points on the boundary are
        // NOT considered blocking.
        return (c1 > 1e-6f && c2 > 1e-6f && c3 > 1e-6f);
    };

    // Classic O(n²) ear-clip: take the FIRST valid ear each pass. (The smallest-area
    // variant scanned every ear each pass → O(n³) → the freeze; first-ear is correct
    // and fast, and the curvature reduction above keeps n bounded.)
    int guard = 0;
    const int guardMax = (int)n + 8;
    while (idx.size() > 3 && guard++ < guardMax) {
        bool clipped = false;
        int m = (int)idx.size();
        for (int i = 0; i < m; ++i) {
            int i0 = idx[(i + m - 1) % m], i1 = idx[i], i2 = idx[(i + 1) % m];
            Vec2 a = poly[i0], b = poly[i1], d = poly[i2];
            if (!isConvex(a, b, d)) continue;          // reflex/collinear → skip
            bool ear = true;
            for (int j = 0; j < m; ++j) {
                int ij = idx[j];
                if (ij == i0 || ij == i1 || ij == i2) continue;
                if (inTri(poly[ij], a, b, d)) { ear = false; break; }
            }
            if (!ear) continue;
            PushTri(out, a, b, d, c);
            idx.erase(idx.begin() + i);
            clipped = true;
            guard = 0;                 // progress made → reset the stall guard
            break;
        }
        if (!clipped) break;           // stalled (self-intersecting) → safe fallback
    }
    if (idx.size() == 3) {
        PushTri(out, poly[idx[0]], poly[idx[1]], poly[idx[2]], c);
        return;
    }
    // SHAPE-PRESERVING fallback: clipping stalled on a non-simple / degenerate ring.
    // Keep the ears already clipped (valid, INSIDE the shape) and emit nothing more —
    // never fan the convex hull (the old behaviour that filled the whole bbox and made
    // the fill "take all the space" near a concave edge). A partial fill is correct
    // where it exists; the missing sliver is invisible at worst, never wrong-region.
}

// ── Stroke geometry helpers (caps, joins, dashes, decorators) ─────────────────

// A wedge (pie slice) of a disc from angle a0 to a1 (CCW), `segPerRad` controls
// smoothness so round caps/joins are TRUE arcs, not a fixed 4/8-segment polygon.
static void FanArc(Mesh& out, Vec2 center, float radius, float a0, float a1,
                   float segPerRad, const Color& c) {
    if (radius <= 1e-6f) return;
    float sweep = a1 - a0;
    int k = (int)std::clamp(std::fabs(sweep) * segPerRad, 2.0f, 256.0f);
    for (int i = 0; i < k; ++i) {
        float t0 = a0 + sweep * (float)i / (float)k;
        float t1 = a0 + sweep * (float)(i + 1) / (float)k;
        PushTri(out, center,
                center + Vec2{std::cos(t0), std::sin(t0)} * radius,
                center + Vec2{std::cos(t1), std::sin(t1)} * radius, c);
    }
}

// Stroke ONE simple (already-dashed) polyline run with the given cap/join/align.
// `zoom` drives round-arc smoothness. `align` shifts the centerline normal so
// Inner/Outer push the whole width to one side of a closed contour.
static void StrokeRun(const std::vector<Vec2>& poly, bool closed, float width,
                      LineCap cap, LineJoin join, float miterLimit,
                      StrokeAlign align, float zoom, const Color& c, Mesh& out,
                      LineCap capStartOv = LineCap::Round,
                      LineCap capEndOv   = LineCap::Round,
                      bool overrideCaps  = false) {
    const size_t n = poly.size();
    if (n < 2 || width <= 0.0f) return;
    // Per-end cap: default both ends to `cap`; when `overrideCaps`, use the explicit
    // start/end caps (lets a junction end be Butt while the free end keeps its cap).
    const LineCap capStart = overrideCaps ? capStartOv : cap;
    const LineCap capEnd   = overrideCaps ? capEndOv   : cap;
    const float hw = width * 0.5f;
    // Smoothness of round caps/joins: zoom-independent (doc-unit radius), so the
    // stroke mesh is cacheable and zoom stays free. (void) the zoom param.
    (void)zoom;
    // Round cap/join smoothness from the ON-SCREEN radius (hw × px per doc-unit),
    // not the doc-unit radius — a tiny mm-sized cap needs few segments, a large one
    // more, capped low. Matches the screen-error flatten basis.
    const float radPx = hw * std::max(gDetailScale, 0.05f);
    const float segPerRad = std::clamp(radPx * 0.5f + 4.0f, 4.0f, 48.0f);

    // Per-side half-widths for stroke alignment.
    //   • CLOSED ring: Inner/Outer push the whole width to the inside / outside,
    //     resolved from the signed area so it's correct regardless of winding.
    //   • OPEN path: there is no inside, so Inner/Outer act as the two SIDES of the
    //     path — the whole width on the −normal (Inner / "right of travel") or the
    //     +normal (Outer / "left of travel") side. This is what lets a contour's
    //     decor or an offset line sit on one chosen side of an open curve, and maps
    //     consistently to the geometric inside/outside when the path is closed.
    float hwPos = hw, hwNeg = hw;   // along +normal / −normal
    if (align != StrokeAlign::Center) {
        bool wantInner = (align == StrokeAlign::Inner);
        bool plusFull;              // true → full width on the +normal side
        if (closed) {
            float area2 = 0.0f;
            for (size_t i = 0; i < n; ++i)
                area2 += Cross(poly[i], poly[(i + 1) % n]);
            bool plusIsInside = area2 > 0.0f;     // +normal (left) = inside of a CCW ring
            plusFull = (wantInner == plusIsInside);
        } else {
            // Open: Outer → +normal side, Inner → −normal side.
            plusFull = !wantInner;
        }
        hwPos = plusFull ? width : 0.0f;
        hwNeg = plusFull ? 0.0f : width;
    }

    const size_t segCount = closed ? n : n - 1;

    // Per-vertex band edges (shared between adjacent segments) instead of independent
    // per-segment quads. On the CONCAVE side of a turn the two segments' inner edges
    // CROSS when the turn is sharper than the half-width (a thick stroke on a tight
    // curve); independent quads then overlap and the inner contour zig-zags / lifts at
    // the ends. Sharing ONE inner point per vertex — the intersection of the two inner
    // offset edges, CLAMPED so it can't shoot past the adjacent segments — removes the
    // self-overlap so the inner edge stays clean. The convex side still gets its join
    // filler below (round/bevel/miter) to cover the outer wedge.
    auto segDir = [&](size_t i) { return Norm(poly[(i + 1) % n] - poly[i]); };
    // Edge offset point at vertex i on one side. `signedHalf` > 0 = +normal (left) side,
    // < 0 = −normal side. The point sits on the miter bisector at distance
    // |signedHalf|/cos(θ/2); on the CONCAVE side the miter is CLAMPED to the shorter
    // adjacent segment so it can't shoot past the neighbours (which is what made the
    // inner band edge self-overlap on a thick stroke / tight curve).
    auto edgePoint = [&](size_t i, Vec2 dIn, Vec2 dOut, float signedHalf) -> Vec2 {
        Vec2 v = poly[i];
        Vec2 nIn{ -dIn.y, dIn.x }, nOut{ -dOut.y, dOut.x };   // left normals
        float s = (signedHalf >= 0.0f) ? 1.0f : -1.0f;
        float half = std::fabs(signedHalf);
        Vec2 bis = (nIn + nOut) * s; float bl = Len(bis);
        if (bl < 1e-5f) return Vec2{ v.x + nOut.x * signedHalf, v.y + nOut.y * signedHalf };
        bis = bis * (1.0f / bl);
        float cosHalf = std::sqrt(std::max(0.0f, (1.0f + Dot(dIn, dOut)) * 0.5f));
        float mlen = (cosHalf > 1e-3f) ? half / cosHalf : half;
        float segIn  = (i > 0 || closed) ? Len(poly[i] - poly[(i + n - 1) % n]) : 1e30f;
        float segOut = Len(poly[(i + 1) % n] - poly[i]);
        float maxLen = std::min(segIn, segOut) * 0.99f + half;   // concave overshoot guard
        if (mlen > maxLen) mlen = maxLen;
        return Vec2{ v.x + bis.x * mlen, v.y + bis.y * mlen };
    };
    std::vector<Vec2> left(n), right(n);
    for (size_t i = 0; i < n; ++i) {
        Vec2 dIn  = (i > 0 || closed) ? segDir((i + n - 1) % n) : segDir(0);
        Vec2 dOut = (i + 1 < n || closed) ? segDir(i) : segDir(n - 2);
        if ((dIn.x == 0 && dIn.y == 0)) dIn = dOut;
        if ((dOut.x == 0 && dOut.y == 0)) dOut = dIn;
        left[i]  = edgePoint(i, dIn, dOut,  hwPos);   // +normal side
        right[i] = edgePoint(i, dIn, dOut, -hwNeg);   // −normal side (negative half)
    }
    for (size_t i = 0; i < segCount; ++i) {
        size_t j = (i + 1) % n;
        Vec2 a0 = left[i], a1 = right[i], b0 = left[j], b1 = right[j];
        PushTri(out, a0, b0, b1, c);
        PushTri(out, a0, b1, a1, c);
    }

    // Joins at interior vertices (and the closing vertex of a closed ring).
    auto joinAt = [&](Vec2 prev, Vec2 v, Vec2 next) {
        Vec2 d0 = Norm(v - prev), d1 = Norm(next - v);
        if ((d0.x == 0 && d0.y == 0) || (d1.x == 0 && d1.y == 0)) return;
        float turn = Cross(d0, d1);
        if (std::fabs(turn) < 1e-5f) return;                  // straight → no gap
        Vec2 n0 = Vec2{ -d0.y, d0.x }, n1 = Vec2{ -d1.y, d1.x };
        // Outer side of the corner is opposite the turn direction.
        float sgn = (turn > 0.0f) ? -1.0f : 1.0f;             // +normal vs −normal
        float hOut = (sgn > 0.0f) ? hwPos : hwNeg;
        if (hOut <= 1e-6f) return;
        Vec2 p0 = v + n0 * (sgn * hOut);   // outer corner of incoming segment
        Vec2 p1 = v + n1 * (sgn * hOut);   // outer corner of outgoing segment
        if (join == LineJoin::Round) {
            // The outer wedge of a (convex) corner is always the MINOR arc between
            // the two outer segment corners. Sweep exactly that — normalising the
            // delta to [−π, π] — so the fan fills the whole wedge from the vertex
            // out to the rim with no leftover pie-slice gap (the earlier sweep
            // could pick the wrong direction and leave a hole on the outside).
            float a0 = std::atan2(p0.y - v.y, p0.x - v.x);
            float a1 = std::atan2(p1.y - v.y, p1.x - v.x);
            float delta = a1 - a0;
            const float kPi = 3.14159265f, kTwoPi = 6.28318531f;
            while (delta >  kPi) delta -= kTwoPi;
            while (delta < -kPi) delta += kTwoPi;
            FanArc(out, v, hOut, a0, a0 + delta, segPerRad, c);
        } else if (join == LineJoin::Bevel) {
            PushTri(out, v, p0, p1, c);
        } else {  // Miter: intersect the two outer edges; fall back to bevel past limit.
            // Miter apex along the half-angle bisector.
            Vec2 mid = Norm(n0 + n1);
            float cosHalf = std::sqrt(std::max(0.0f, (1.0f + Dot(d0, d1)) * 0.5f));
            if (cosHalf < 1e-3f) { PushTri(out, v, p0, p1, c); return; }
            float miterLen = hOut / cosHalf;
            if (miterLen > miterLimit * hOut) { PushTri(out, v, p0, p1, c); return; }
            Vec2 apex = v + mid * (sgn * miterLen);
            PushTri(out, v, p0, apex, c);
            PushTri(out, v, apex, p1, c);
        }
    };
    if (closed) {
        for (size_t i = 0; i < n; ++i)
            joinAt(poly[(i + n - 1) % n], poly[i], poly[(i + 1) % n]);
    } else {
        for (size_t i = 1; i + 1 < n; ++i)
            joinAt(poly[i - 1], poly[i], poly[i + 1]);
    }

    // Caps on the two open ends. The cap spans the ACTUAL stroke edges (+normal·
    // hwPos … −normal·hwNeg), so an Inner/Outer-aligned (side) stroke caps on its
    // own body — not symmetric around the centreline (which left one side "cut").
    if (!closed) {
        // `fwd` is the segment's TRAVEL direction (the same orientation used for the
        // body's +normal). `outDir` is where the cap sticks out (−fwd at the start,
        // +fwd at the end). Using the body normal (not a per-end recomputed one)
        // keeps hwPos/hwNeg on the correct sides at BOTH ends — otherwise the cap
        // landed on the wrong side at the start.
        auto capAt = [&](Vec2 end, Vec2 fwd, Vec2 outDir, LineCap cap) {
            if (fwd.x == 0 && fwd.y == 0) return;
            Vec2 nrm = Vec2{ -fwd.y, fwd.x };    // body +normal (consistent both ends)
            Vec2 ePos = end + nrm * hwPos;       // edge on the +normal side
            Vec2 eNeg = end - nrm * hwNeg;       // edge on the −normal side
            float r   = (hwPos + hwNeg) * 0.5f;  // half the band width
            Vec2 mid  = (ePos + eNeg) * 0.5f;    // centre of the actual stroke band
            if (cap == LineCap::Round) {
                // Semicircle from the +normal edge to the −normal edge, bulging
                // toward the OUTWARD direction. Sweep sign chosen so the mid-arc
                // point lands on `outDir` (a plain ±π from `base` could face inward).
                float base = std::atan2(ePos.y - mid.y, ePos.x - mid.x);
                const float kPi = 3.14159265f;
                // +π rotates +normal toward outDir iff (normal × outDir) > 0.
                float sweep = (Cross(nrm, outDir) > 0.0f) ? kPi : -kPi;
                FanArc(out, mid, r, base, base + sweep, segPerRad, c);
            } else if (cap == LineCap::Square) {
                Vec2 e = outDir * r;
                PushTri(out, ePos, ePos + e, eNeg + e, c);
                PushTri(out, ePos, eNeg + e, eNeg, c);
            }
            // Butt: nothing — the segment already ends square at the point.
        };
        if (poly.size() > 1) {
            Vec2 fwdStart = Norm(poly[1] - poly[0]);
            Vec2 fwdEnd   = Norm(poly[n - 1] - poly[n - 2]);
            capAt(poly.front(), fwdStart, fwdStart * -1.0f, capStart);   // backward
            capAt(poly.back(),  fwdEnd,   fwdEnd,           capEnd);     // forward
        }
    }
}

// A phase ANCHOR (from a DashAnchor mark): an arc-length position where a dash/
// pattern ELEMENT (`elementCentred` = true) or a GAP must be centred. The dash
// layout is split at each anchor and each span re-phased so the boundary lands.
struct DashAnchor { float at; bool elementCentred; };

// ── Unified dash LAYOUT (the single source of truth for phase) ───────────────
// Returns the list of DASH intervals [start,end] (arc-length, doc-units) over a
// run of length L with the given dash/gap lengths. Anchors (sorted, in [0,L])
// split the run into spans; each span is phased independently so that, at every
// anchor and at the two run ends, a DASH centre (anchor elementCentred=true) or a
// GAP centre (false) lands exactly there. Dash/gap lengths are PRESERVED exactly
// (ISOM mm precision) — only the leftover at the ends/anchors is distributed as a
// symmetric partial gap. Per-dash decorators read the dash MIDPOINTS from this
// same list, so the pattern element is always centred in its dash (fixes drift).
static std::vector<std::pair<float,float>>
DashLayout(float L, float dashLen, float gapLen, const std::vector<DashAnchor>& anchors) {
    std::vector<std::pair<float,float>> dashes;
    const float period = dashLen + gapLen;
    if (period <= 1e-4f || L <= 1e-4f) { dashes.push_back({0.0f, L}); return dashes; }

    // PHASE reference: the arc-length of a DASH CENTRE. Without anchors, choose it so
    // the whole run is symmetric (a dash or gap centred at the run middle, ISOM
    // rule). With an anchor, lock a dash centre (elementCentred) or a gap centre (the
    // dash centre then sits half a period away) exactly on the anchor.
    float dashCentre;
    if (!anchors.empty()) {
        const DashAnchor& a = anchors.front();
        dashCentre = a.elementCentred ? a.at : a.at + period * 0.5f;
    } else {
        // Symmetric: fit a whole number of periods; centre a dash at the middle if
        // the count is odd-ish, else a gap — pick whichever keeps both ends equal.
        float mid = L * 0.5f;
        int k = (int)std::round((mid - dashLen * 0.5f) / period);
        dashCentre = dashLen * 0.5f + k * period;
        if (dashCentre > mid) dashCentre -= 0.0f;   // already nearest
    }
    // Tile dashes at exact `period` across [0,L] from the reference dash centre.
    // Exact dash/gap lengths preserved; only the partial dashes at the two ends are
    // clipped. Find the first dash centre ≤ 0's neighbourhood, then walk up.
    float firstCentre = dashCentre - std::ceil((dashCentre + dashLen * 0.5f) / period) * period;
    for (float ctr = firstCentre; ctr - dashLen * 0.5f < L + 1e-4f; ctr += period) {
        float ds = ctr - dashLen * 0.5f, de = ctr + dashLen * 0.5f;
        if (de <= 1e-4f || ds >= L - 1e-4f) continue;          // fully outside
        dashes.push_back({ std::max(0.0f, ds), std::min(L, de) });
    }
    if (dashes.empty()) dashes.push_back({ 0.0f, L });
    return dashes;
}

// Extract the sub-polyline of `poly` between arc-lengths [from,to] (declared
// later for the cut helpers — forward it here for dashing).
static std::vector<Vec2> ExtractRun(const std::vector<Vec2>& poly, bool closed,
                                    float from, float to);

// Split a polyline into dash runs using the unified DashLayout (so the base line
// and the per-dash decorator share one phase). `anchors` (arc-length) force a dash
// or gap centre at chosen points. Exact mm dash/gap preserved.
static std::vector<std::vector<Vec2>> DashRuns(const std::vector<Vec2>& poly,
                                               bool closed,
                                               const std::vector<float>& dash,
                                               bool centered,
                                               const std::vector<DashAnchor>& anchors = {}) {
    std::vector<std::vector<Vec2>> runs;
    if (poly.size() < 2 || dash.empty()) { runs.push_back(poly); return runs; }
    std::vector<float> pat = dash;
    if (pat.size() == 1) pat.push_back(pat[0]);
    float dashLen = pat[0], gapLen = pat.size() > 1 ? pat[1] : pat[0];
    if (dashLen + gapLen <= 1e-4f) { runs.push_back(poly); return runs; }
    (void)centered;

    size_t n = poly.size(), segCount = closed ? n : n - 1;
    float total = 0.0f;
    for (size_t i = 0; i < segCount; ++i) total += Len(poly[(i + 1) % n] - poly[i]);

    for (auto& iv : DashLayout(total, dashLen, gapLen, anchors)) {
        std::vector<Vec2> r = ExtractRun(poly, closed, iv.first, iv.second);
        if (r.size() >= 2) runs.push_back(std::move(r));
    }
    return runs;
}

// A resampled point + unit tangent along a polyline (used by every decorator).
struct Station { Vec2 p; Vec2 t; };

// Sample a polyline at arc-length `d` → point + unit tangent (small local walker,
// keeps DashElementStations self-contained).
static void ArcSample(const std::vector<Vec2>& poly, bool closed, float d,
                      Vec2& outP, Vec2& outTan) {
    size_t n = poly.size(), sc = closed ? n : n - 1; float acc = 0.0f;
    for (size_t i = 0; i < sc; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float L = Len(b - a); if (L < 1e-6f) continue;
        if (d <= acc + L) { float u = (d - acc) / L;
            outP = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
            outTan = { (b.x - a.x) / L, (b.y - a.y) / L }; return; }
        acc += L;
    }
    outP = poly[n - 1]; outTan = Norm(poly[n - 1] - poly[n >= 2 ? n - 2 : 0]);
}

// Dash-MIDPOINT stations for the per-dash decorator: one element at the centre of
// each dash from the SAME DashLayout the base line uses → the pattern element is
// always exactly centred in its dash (no drift). Empty if the part isn't dashed.
static std::vector<Station> DashElementStations(const std::vector<Vec2>& poly,
        bool closed, const std::vector<float>& dash, const std::vector<DashAnchor>& anchors) {
    std::vector<Station> st;
    if (poly.size() < 2 || dash.empty()) return st;
    std::vector<float> pat = dash; if (pat.size() == 1) pat.push_back(pat[0]);
    float dashLen = pat[0], gapLen = pat.size() > 1 ? pat[1] : pat[0];
    if (dashLen + gapLen <= 1e-4f) return st;
    size_t n = poly.size(), sc = closed ? n : n - 1; float total = 0.0f;
    for (size_t i = 0; i < sc; ++i) total += Len(poly[(i + 1) % n] - poly[i]);
    for (auto& iv : DashLayout(total, dashLen, gapLen, anchors)) {
        Station q; ArcSample(poly, closed, (iv.first + iv.second) * 0.5f, q.p, q.t);
        st.push_back(q);
    }
    return st;
}

// Resample a polyline to evenly-spaced stations (centre-to-centre `spacing`),
// returning point + unit tangent. Used to stamp periodic decorators along a line.
static std::vector<Station> Stations(const std::vector<Vec2>& poly, bool closed,
                                     float spacing, bool centered) {
    std::vector<Station> st;
    if (poly.size() < 2 || spacing <= 1e-4f) return st;
    size_t n = poly.size();
    size_t segCount = closed ? n : n - 1;
    float total = 0.0f;
    for (size_t i = 0; i < segCount; ++i) total += Len(poly[(i + 1) % n] - poly[i]);
    if (total < 1e-4f) return st;
    // Centre the run: first station at half the leftover so start/end match.
    float start = spacing * 0.5f;
    if (centered) {
        int count = std::max(1, (int)std::round(total / spacing));
        float used = count * spacing;
        start = (total - used) * 0.5f + spacing * 0.5f;
    }
    float target = start;
    float acc = 0.0f;
    for (size_t i = 0; i < segCount; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float segLen = Len(b - a);
        if (segLen < 1e-6f) continue;
        Vec2 dir = (b - a) * (1.0f / segLen);
        while (target <= acc + segLen + 1e-4f && target <= total - 1e-4f) {
            float d = target - acc;
            st.push_back({ a + dir * d, dir });
            target += spacing;
        }
        acc += segLen;
    }
    return st;
}

// Offset point at a vertex whose incoming/outgoing UNIT directions are d0/d1, by
// signed distance `d` along the left normal, using a proper MITER (so the offset edge
// stays parallel at distance |d| through the corner). Past `miterLimit` (sharp angle)
// the miter length is clamped so it folds to a bevel-length point instead of shooting
// out a spike that self-intersects. Shared by the stroke join math and OffsetPoly.
static Vec2 MiterOffsetPoint(Vec2 v, Vec2 d0, Vec2 d1, float d, float miterLimit) {
    Vec2 n0{ -d0.y, d0.x }, n1{ -d1.y, d1.x };       // left normals
    Vec2 bis = n0 + n1;
    float bl = Len(bis);
    if (bl < 1e-5f) {                                 // ~180° reversal → use n1
        return v + n1 * d;
    }
    bis = bis * (1.0f / bl);
    // cosHalf = cos of half the turn angle; miter length = d / cosHalf.
    float cosHalf = std::sqrt(std::max(0.0f, (1.0f + Dot(d0, d1)) * 0.5f));
    if (cosHalf < 1e-3f) return v + n1 * d;           // degenerate → plain normal
    float miterScale = 1.0f / cosHalf;
    if (miterScale > std::max(1.0f, miterLimit)) miterScale = std::max(1.0f, miterLimit);
    return v + bis * (d * miterScale);
}

// Offset a closed/open polyline by `d` along its per-vertex left normal, using miter
// joins (clamped by miterLimit). Replaces the old normal-AVERAGING (which under-shot
// at corners and collapsed at hard/sharp angles, breaking inner-side fill clips).
static std::vector<Vec2> OffsetPoly(const std::vector<Vec2>& poly, bool closed, float d,
                                    float miterLimit = 4.0f) {
    std::vector<Vec2> out;
    const size_t n = poly.size();
    if (n < 2) return out;
    auto segDir = [&](size_t i, size_t j) { return Norm(poly[j] - poly[i]); };
    for (size_t i = 0; i < n; ++i) {
        Vec2 dIn, dOut;
        if (i == 0 && !closed)            dIn = dOut = segDir(0, 1);
        else if (i + 1 == n && !closed)   dIn = dOut = segDir(n - 2, n - 1);
        else {
            dIn  = segDir((i + n - 1) % n, i);
            dOut = segDir(i, (i + 1) % n);
        }
        out.push_back(MiterOffsetPoint(poly[i], dIn, dOut, d, miterLimit));
    }
    return out;
}

// True for the periodic GLYPH decorators (stamped per station). The continuous /
// composite RAILS (DoubleLine/Railway/EdgeLines/Double*) are NOT glyphs.
static bool DecorIsGlyph(LineDecor d) {
    switch (d) {
        case LineDecor::Tags: case LineDecor::TagsBoth: case LineDecor::Dots:
        case LineDecor::HalfDots: case LineDecor::Ties: case LineDecor::Pylons:
        case LineDecor::Slashes: case LineDecor::Vee: case LineDecor::Crosses:
        case LineDecor::PairDots: case LineDecor::PairSlashes:
            return true;
        // Railway / Double* ALSO have a periodic glyph (cross-tie / picket / bar)
        // on top of their rails — those glyphs ARE instanced; the rails are baked.
        case LineDecor::Railway: case LineDecor::DoubleSlashes:
        case LineDecor::DoublePylons: case LineDecor::DoubleTicks:
            return true;
        default: return false;   // None / DoubleLine / EdgeLines: no glyph
    }
}

// Arc-length stations for a decorator, reproducing the legacy StrokeDecor logic:
// dash-midpoints for a per-dash element, else even spacing, anchor-aware. Shared by
// the baked (StrokeDecor) and instanced (StrokeDecorInstanced) paths so they agree.
static std::vector<Station> DecorStations(const std::vector<Vec2>& poly, bool closed,
                                          const StrokeStyle& s,
                                          const std::vector<DashAnchor>& anc) {
    const bool perDashElement = !s.dash.empty() &&
        (s.decor == LineDecor::Dots || s.decor == LineDecor::Slashes ||
         s.decor == LineDecor::HalfDots || s.decor == LineDecor::Tags ||
         s.decor == LineDecor::Crosses);
    if (perDashElement)
        return DashElementStations(poly, closed, s.dash, anc);
    float sp = std::max(s.decorSpacing, 1e-3f);
    auto base = Stations(poly, closed, sp, s.decorCentered);
    if (!anc.empty() && !closed && poly.size() >= 2) {
        size_t n = poly.size(), sc = n - 1; float total = 0.0f;
        for (size_t i = 0; i < sc; ++i) total += Len(poly[(i+1)%n] - poly[i]);
        float at = std::clamp(anc.front().at, 0.0f, total);
        if (!anc.front().elementCentred) at += sp * 0.5f;
        std::vector<Station> out2;
        for (float d = at; d <= total + 1e-3f; d += sp) {
            Station q; ArcSample(poly, closed, std::min(d, total), q.p, q.t); out2.push_back(q); }
        for (float d = at - sp; d >= -1e-3f; d -= sp) {
            Station q; ArcSample(poly, closed, std::max(d, 0.0f), q.p, q.t); out2.push_back(q); }
        if (!out2.empty()) return out2;
    }
    return base;
}

// Signed lateral offset (doc-units) from the construction line to the requested
// decor edge, derived from stroke width + align + contour winding — same rule as
// DeriveClipPoly for fills. 0 = construction line.
static float DecorEdgeOffset(const std::vector<Vec2>& poly, bool closed,
                             const StrokeStyle& s) {
    if (s.decorEdge == DecorEdge::Construction || s.width <= 0.0f) return 0.0f;
    const bool inner = (s.decorEdge == DecorEdge::InnerEdge);
    float dist = 0.0f;
    switch (s.align) {
        case StrokeAlign::Center: dist = s.width * 0.5f; break;
        case StrokeAlign::Inner:  dist = inner ? s.width : 0.0f; break;
        case StrokeAlign::Outer:  dist = inner ? 0.0f : s.width; break;
    }
    if (dist <= 1e-5f) return 0.0f;
    // OffsetPoly +normal = polygon LEFT; for a CCW ring (signed area>0) left = inside.
    float area2 = 0.0f; const size_t n = poly.size();
    if (closed && n >= 3) for (size_t i = 0; i < n; ++i) area2 += Cross(poly[i], poly[(i+1)%n]);
    const bool ccw = area2 > 0.0f;
    return inner ? (ccw ? +dist : -dist) : (ccw ? -dist : +dist);
}

// Emit one PatternInstance for a SEGMENT glyph (tick/bar/picket/cross-arm): a unit
// Quad scaled to length×thickness, rotated to `dir`, centred at `mid`.
static void PushSegInstance(std::vector<PatternInstance>& out, Vec2 mid, Vec2 dir,
                            float length, float thickness, const Color& c) {
    float rot = std::atan2(dir.y, dir.x);
    out.push_back(PatternInstance{ mid.x, mid.y, length, thickness, rot, c.r, c.g, c.b, c.a });
}

// Stamp the periodic decorator glyphs along the line. When the part is DASHED and
// uses a per-dash element decor, the element stations come from the SAME dash
// layout (dash midpoints) so each element sits exactly in its dash. `anchors`
// (optional) carries the DashAnchor phase pins.
static void StrokeDecor(const std::vector<Vec2>& poly, bool closed,
                        const StrokeStyle& s, float zoom, const Color& c, Mesh& out,
                        const std::vector<DashAnchor>* anchors = nullptr) {
    if (s.decor == LineDecor::None) return;
    float th = s.decorThickness > 1e-5f ? s.decorThickness : s.width;
    float size = s.decorSize;
    // For a dashed line whose decor is one element PER DASH (the spacing matches the
    // dash period), derive the element stations from the dash midpoints so they stay
    // centred in the dash at every length (no drift). Detected when a dash exists
    // and decorSpacing ≈ the dash period.
    const bool perDashElement = !s.dash.empty() &&
        (s.decor == LineDecor::Dots || s.decor == LineDecor::Slashes ||
         s.decor == LineDecor::HalfDots || s.decor == LineDecor::Tags ||
         s.decor == LineDecor::Crosses);
    std::vector<DashAnchor> noAnchors;
    const std::vector<DashAnchor>& anc = anchors ? *anchors : noAnchors;
    auto stationsFor = [&]() -> std::vector<Station> {
        if (perDashElement)
            return DashElementStations(poly, closed, s.dash, anc);
        float sp = std::max(s.decorSpacing, 1e-3f);
        auto base = Stations(poly, closed, sp, s.decorCentered);
        // NON-dashed pattern with a DashAnchor: rebuild the stations so a pattern
        // ELEMENT (anchor.elementCentred) lands exactly on the first anchor, then
        // step by `sp` both ways. (A gap-centre anchor offsets by half a period.)
        if (!perDashElement && !anc.empty() && !closed && poly.size() >= 2) {
            size_t n = poly.size(), sc = n - 1; float total = 0.0f;
            for (size_t i = 0; i < sc; ++i) total += Len(poly[(i+1)%n] - poly[i]);
            float at = std::clamp(anc.front().at, 0.0f, total);
            if (!anc.front().elementCentred) at += sp * 0.5f;   // gap → element half-step away
            std::vector<Station> out2;
            for (float d = at; d <= total + 1e-3f; d += sp) {
                Station q; ArcSample(poly, closed, std::min(d, total), q.p, q.t); out2.push_back(q); }
            for (float d = at - sp; d >= -1e-3f; d -= sp) {
                Station q; ArcSample(poly, closed, std::max(d, 0.0f), q.p, q.t); out2.push_back(q); }
            if (!out2.empty()) return out2;
        }
        return base;
    };

    // ── Composite decorators: twin rails + a stamped glyph ──
    if (s.decor == LineDecor::DoubleSlashes || s.decor == LineDecor::DoublePylons ||
        s.decor == LineDecor::DoubleTicks) {
        std::vector<Vec2> railA = OffsetPoly(poly, closed,  size * 0.5f);
        std::vector<Vec2> railB = OffsetPoly(poly, closed, -size * 0.5f);
        if (railA.size() >= 2) StrokeRun(railA, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        if (railB.size() >= 2) StrokeRun(railB, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        auto st = Stations(poly, closed, std::max(s.decorSpacing, 1e-3f), s.decorCentered);
        float ang = s.decorAngleDeg * 3.14159265f / 180.0f;
        for (const Station& q : st) {
            Vec2 t = q.t, nrm = Vec2{ -t.y, t.x };
            if (s.decor == LineDecor::DoublePylons) {        // bar across both rails
                std::vector<Vec2> seg = { q.p - nrm * (size * 0.5f), q.p + nrm * (size * 0.5f) };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f, StrokeAlign::Center, zoom, c, out);
            } else {                                         // oblique picket / tick
                float ca = std::cos(ang), sa = std::sin(ang);
                Vec2 odir{ t.x * ca - t.y * sa, t.x * sa + t.y * ca };
                std::vector<Vec2> seg = { q.p + nrm * (size * 0.5f),
                                          q.p + nrm * (size * 0.5f) + odir * (size * 0.8f) };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f, StrokeAlign::Center, zoom, c, out);
            }
        }
        return;
    }

    // ── Edge contours drawn IN ADDITION to the base (base NOT suppressed) ──
    if (s.decor == LineDecor::EdgeLines) {
        std::vector<Vec2> edgeA = OffsetPoly(poly, closed,  size * 0.5f);
        std::vector<Vec2> edgeB = OffsetPoly(poly, closed, -size * 0.5f);
        if (edgeA.size() >= 2) StrokeRun(edgeA, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        if (edgeB.size() >= 2) StrokeRun(edgeB, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        return;
    }

    // ── Continuous decorators (follow the whole curve) ──
    if (s.decor == LineDecor::DoubleLine || s.decor == LineDecor::Railway) {
        // Twin parallel rails at ±size/2 (the base line itself is drawn separately
        // by the caller; for DoubleLine the base is one rail and we add the other,
        // for symmetry we draw BOTH rails here at ±size/2 and the caller suppresses
        // its own base — see StrokeStyled).
        std::vector<Vec2> railA = OffsetPoly(poly, closed,  size * 0.5f);
        std::vector<Vec2> railB = OffsetPoly(poly, closed, -size * 0.5f);
        if (railA.size() >= 2) StrokeRun(railA, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        if (railB.size() >= 2) StrokeRun(railB, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
        if (s.decor == LineDecor::Railway) {
            // Cross-ties: short bars spanning both rails at each station.
            auto st = Stations(poly, closed, std::max(s.decorSpacing, 1e-3f), s.decorCentered);
            for (const Station& q : st) {
                Vec2 nrm = Vec2{ -q.t.y, q.t.x };
                std::vector<Vec2> seg = { q.p - nrm * (size * 0.5f), q.p + nrm * (size * 0.5f) };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f, StrokeAlign::Center, zoom, c, out);
            }
        }
        return;
    }

    // Decorator dots (wall dots, pair dots, half-discs) are tiny ISOM glyphs — a
    // modest segment count is plenty even when zoomed. Screen-radius basis (used only
    // by the baked thumbnail path now; viewports instance these glyphs).
    const float dotRadPx = size * 0.5f * std::max(gDetailScale, 0.05f);
    const float segPerRad = std::clamp(dotRadPx * 0.5f + 4.0f, 4.0f, 32.0f);
    // ── Groups of two dots (impassable wall 515): at each station, a pair of dots
    // straddling the line, the two dots `size` apart along the tangent. ──
    if (s.decor == LineDecor::PairDots) {
        auto st = stationsFor();   // anchor-aware: centre a group / a gap on the anchor
        for (const Station& q : st) {
            for (float sgn : { -0.5f, +0.5f }) {
                Vec2 cdot = q.p + q.t * (sgn * size);
                FanArc(out, cdot, size * 0.5f, 0.0f, 6.2831853f, segPerRad, c);
            }
        }
        return;
    }
    // ── Groups of two oblique pickets on one side (impassable fence 518) ──
    if (s.decor == LineDecor::PairSlashes) {
        float thp = s.decorThickness > 1e-5f ? s.decorThickness : s.width;
        float a = s.decorAngleDeg * 3.14159265f / 180.0f;
        float ca = std::cos(a), sa = std::sin(a);
        auto st = stationsFor();   // anchor-aware
        for (const Station& q : st) {
            Vec2 t = q.t, nrm = Vec2{ -t.y, t.x };
            // The picket direction = the tangent rotated by `a` (toward +normal).
            Vec2 odir{ t.x * ca - nrm.x * sa, t.y * ca - nrm.y * sa };
            for (float sgn : { -0.5f, +0.5f }) {
                Vec2 base = q.p + t * (sgn * size * 0.8f);
                std::vector<Vec2> seg = { base, base + odir * size };
                StrokeRun(seg, false, thp, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
            }
        }
        return;
    }

    auto stations = stationsFor();   // dash-midpoints when per-dash, else even spacing
    (void)zoom;
    float ang = s.decorAngleDeg * 3.14159265f / 180.0f;
    for (const Station& q : stations) {
        Vec2 t = q.t;
        Vec2 nrm = Vec2{ -t.y, t.x };               // left of travel
        switch (s.decor) {
            case LineDecor::Tags: {                 // one-sided perpendicular tick
                std::vector<Vec2> seg = { q.p, q.p + nrm * size };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::TagsBoth: {
                std::vector<Vec2> seg = { q.p - nrm * size, q.p + nrm * size };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::Ties: {                 // short cross-bar both sides
                std::vector<Vec2> seg = { q.p - nrm * size, q.p + nrm * size };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::Pylons: {               // pylon bar both sides
                std::vector<Vec2> seg = { q.p - nrm * size, q.p + nrm * size };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::Dots: {                 // filled dot straddling line
                FanArc(out, q.p, size * 0.5f, 0.0f, 6.2831853f, segPerRad, c);
                break; }
            case LineDecor::HalfDots: {             // half-disc on ONE side: flat edge ON the
                // line (along the tangent), bulge to the +normal side. Sweeping
                // from the tangent angle by +π keeps the diameter on the line.
                float tang = std::atan2(t.y, t.x);
                FanArc(out, q.p, size * 0.5f, tang, tang + 3.14159265f, segPerRad, c);
                break; }
            case LineDecor::Slashes: {              // oblique picket at angle
                float ca = std::cos(ang), sa = std::sin(ang);
                Vec2 odir{ t.x * ca - t.y * sa, t.x * sa + t.y * ca };
                std::vector<Vec2> seg = { q.p, q.p + odir * size };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::Vee: {                  // small downhill chevron
                Vec2 tip = q.p + nrm * size;
                std::vector<Vec2> seg = { q.p - t * (size * 0.5f), tip, q.p + t * (size * 0.5f) };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineDecor::Crosses: {              // × straddling the line
                float h = size * 0.5f;
                std::vector<Vec2> s1 = { q.p + (t - nrm) * h, q.p - (t - nrm) * h };
                std::vector<Vec2> s2 = { q.p + (t + nrm) * h, q.p - (t + nrm) * h };
                StrokeRun(s1, false, th, LineCap::Butt, LineJoin::Miter, 4.0f, StrokeAlign::Center, zoom, c, out);
                StrokeRun(s2, false, th, LineCap::Butt, LineJoin::Miter, 4.0f, StrokeAlign::Center, zoom, c, out);
                break; }
            default: break;
        }
    }
}

// Bake ONLY the continuous / composite RAILS of a decorator (DoubleLine, Railway,
// EdgeLines, Double*). The periodic glyphs (ties / pickets / bars) are emitted as
// instances by StrokeDecorInstanced — NOT here. No-op for plain glyph decorators.
static void StrokeDecorRails(const std::vector<Vec2>& poly, bool closed,
                             const StrokeStyle& s, float zoom, const Color& c, Mesh& out) {
    if (s.decor == LineDecor::None) return;
    float th = s.decorThickness > 1e-5f ? s.decorThickness : s.width;
    float size = s.decorSize;
    const bool twinRails = (s.decor == LineDecor::DoubleSlashes ||
                            s.decor == LineDecor::DoublePylons ||
                            s.decor == LineDecor::DoubleTicks ||
                            s.decor == LineDecor::DoubleLine ||
                            s.decor == LineDecor::Railway ||
                            s.decor == LineDecor::EdgeLines);
    if (!twinRails) return;
    std::vector<Vec2> railA = OffsetPoly(poly, closed,  size * 0.5f);
    std::vector<Vec2> railB = OffsetPoly(poly, closed, -size * 0.5f);
    if (railA.size() >= 2) StrokeRun(railA, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
    if (railB.size() >= 2) StrokeRun(railB, closed, th, s.cap, s.join, s.miterLimit, StrokeAlign::Center, zoom, c, out);
}

// Emit a decorator's PERIODIC glyphs as GPU instances (page-local) into `sink`,
// grouped into contiguous DecorBatch runs per element kind. Mirrors StrokeDecor's
// stationing + side logic so dashed / anchored / cut symbols match. Rails are NOT
// emitted here (see StrokeDecorRails). `c` is the decorator colour.
static void StrokeDecorInstanced(const std::vector<Vec2>& poly, bool closed,
                                 const StrokeStyle& s, float /*zoom*/, const Color& c,
                                 std::vector<PatternInstance>& inst,
                                 std::vector<DecorBatch>& batches,
                                 const std::vector<DashAnchor>* anchors = nullptr) {
    if (s.decor == LineDecor::None || !DecorIsGlyph(s.decor)) return;
    const float th   = s.decorThickness > 1e-5f ? s.decorThickness : s.width;
    const float size = s.decorSize;
    std::vector<DashAnchor> noAnchors;
    const std::vector<DashAnchor>& anc = anchors ? *anchors : noAnchors;

    // Lateral edge offset: walk the stations on the offset poly so the tangent
    // follows the painted edge (construction line when offset == 0).
    const float edgeOff = DecorEdgeOffset(poly, closed, s);
    std::vector<Vec2> work = (edgeOff != 0.0f) ? OffsetPoly(poly, closed, edgeOff) : poly;
    if (work.size() < 2) return;
    std::vector<Station> stations = DecorStations(work, closed, s, anc);
    if (stations.empty()) return;

    const float ang = s.decorAngleDeg * 3.14159265f / 180.0f;
    // A glyph kind may emit ≥1 instance/station; collect per-kind then flush as a
    // contiguous batch so the renderer issues one vkCmdDraw per kind.
    auto begin = [&](PatternElementKind k) {
        DecorBatch b; b.kind = k; b.firstInstance = (uint32_t)inst.size(); return b;
    };
    auto flush = [&](DecorBatch& b) {
        b.instanceCount = (uint32_t)inst.size() - b.firstInstance;
        if (b.instanceCount) batches.push_back(b);
    };
    // Side resolution: returns the normal-sign multipliers to emit at, per station.
    // Centered → {0}; One → {+1}; Both → {+1,-1}; Alternating → {±} by parity.
    auto sidesAt = [&](int idx) -> std::vector<float> {
        switch (s.decorSide) {
            case DecorSide::One:         return { +1.0f };
            case DecorSide::Both:        return { +1.0f, -1.0f };
            case DecorSide::Alternating: return { (idx & 1) ? -1.0f : +1.0f };
            case DecorSide::Centered: default: return { 0.0f };
        }
    };

    switch (s.decor) {
        case LineDecor::Dots: {
            DecorBatch b = begin(PatternElementKind::Disc);
            for (int i = 0; i < (int)stations.size(); ++i) {
                Vec2 nrm{ -stations[i].t.y, stations[i].t.x };
                for (float sg : sidesAt(i)) {
                    Vec2 p = stations[i].p + nrm * (sg * size * 0.5f);
                    inst.push_back({ p.x, p.y, size, size, 0.0f, c.r,c.g,c.b,c.a });
                }
            }
            flush(b); break; }
        case LineDecor::PairDots: {
            DecorBatch b = begin(PatternElementKind::Disc);
            for (const Station& q : stations)
                for (float sgn : { -0.5f, +0.5f }) {
                    Vec2 p = q.p + q.t * (sgn * size);
                    inst.push_back({ p.x, p.y, size, size, 0.0f, c.r,c.g,c.b,c.a });
                }
            flush(b); break; }
        case LineDecor::HalfDots: {
            DecorBatch b = begin(PatternElementKind::HalfDisc);
            for (const Station& q : stations) {
                float rot = std::atan2(q.t.y, q.t.x);   // flat edge on the line, bulge +n
                inst.push_back({ q.p.x, q.p.y, size, size, rot, c.r,c.g,c.b,c.a });
            }
            flush(b); break; }
        case LineDecor::Tags: {                          // one-sided tick (⊥)
            DecorBatch b = begin(PatternElementKind::Quad);
            for (int i = 0; i < (int)stations.size(); ++i) {
                Vec2 t = stations[i].t, nrm{ -t.y, t.x };
                for (float sg : sidesAt(i)) {
                    float s2 = (sg == 0.0f) ? 1.0f : sg;     // Tags default One → +n
                    Vec2 mid = stations[i].p + nrm * (s2 * size * 0.5f);
                    PushSegInstance(inst, mid, nrm * s2, size, th, c);
                }
            }
            flush(b); break; }
        case LineDecor::TagsBoth: case LineDecor::Ties:
        case LineDecor::Pylons: case LineDecor::DoubleTicks: {   // bar across ±n
            DecorBatch b = begin(PatternElementKind::Quad);
            for (const Station& q : stations) {
                Vec2 nrm{ -q.t.y, q.t.x };
                PushSegInstance(inst, q.p, nrm, size * 2.0f, th, c);
            }
            flush(b); break; }
        case LineDecor::DoublePylons: {                  // bar across both rails
            DecorBatch b = begin(PatternElementKind::Quad);
            for (const Station& q : stations) {
                Vec2 nrm{ -q.t.y, q.t.x };
                PushSegInstance(inst, q.p, nrm, size, th, c);
            }
            flush(b); break; }
        case LineDecor::Railway: {                        // cross-tie spanning rails
            DecorBatch b = begin(PatternElementKind::Quad);
            for (const Station& q : stations) {
                Vec2 nrm{ -q.t.y, q.t.x };
                PushSegInstance(inst, q.p, nrm, size, th, c);
            }
            flush(b); break; }
        case LineDecor::Slashes: case LineDecor::DoubleSlashes: {  // oblique picket
            DecorBatch b = begin(PatternElementKind::Quad);
            for (int i = 0; i < (int)stations.size(); ++i) {
                Vec2 t = stations[i].t;
                float ca = std::cos(ang), sa = std::sin(ang);
                Vec2 odir{ t.x*ca - t.y*sa, t.x*sa + t.y*ca };
                for (float sg : sidesAt(i)) {
                    (void)sg;   // Slashes default One; picket grows from the line
                    Vec2 mid = stations[i].p + odir * (size * 0.5f);
                    PushSegInstance(inst, mid, odir, size, th, c);
                }
            }
            flush(b); break; }
        case LineDecor::PairSlashes: {                    // 2 oblique pickets / group
            DecorBatch b = begin(PatternElementKind::Quad);
            float ca = std::cos(ang), sa = std::sin(ang);
            for (const Station& q : stations) {
                Vec2 t = q.t, nrm{ -t.y, t.x };
                Vec2 odir{ t.x*ca - nrm.x*sa, t.y*ca - nrm.y*sa };
                for (float sgn : { -0.5f, +0.5f }) {
                    Vec2 base = q.p + t * (sgn * size * 0.8f);
                    Vec2 mid = base + odir * (size * 0.5f);
                    PushSegInstance(inst, mid, odir, size, th, c);
                }
            }
            flush(b); break; }
        case LineDecor::Vee: {                            // chevron = 2 quad arms
            DecorBatch b = begin(PatternElementKind::Quad);
            for (const Station& q : stations) {
                Vec2 t = q.t, nrm{ -t.y, t.x };
                Vec2 tip = q.p + nrm * size;
                Vec2 a = q.p - t * (size * 0.5f), c2 = q.p + t * (size * 0.5f);
                Vec2 m1 = (a + tip) * 0.5f, m2 = (c2 + tip) * 0.5f;
                PushSegInstance(inst, m1, tip - a, Len(tip - a), th, c);
                PushSegInstance(inst, m2, tip - c2, Len(tip - c2), th, c);
            }
            flush(b); break; }
        case LineDecor::Crosses: {                        // × = 2 quad arms
            DecorBatch b = begin(PatternElementKind::Quad);
            float h = size * 0.5f;
            for (const Station& q : stations) {
                Vec2 t = q.t, nrm{ -t.y, t.x };
                Vec2 d1 = Norm(t - nrm), d2 = Norm(t + nrm);
                PushSegInstance(inst, q.p, d1, h * 2.0f, th, c);
                PushSegInstance(inst, q.p, d2, h * 2.0f, th, c);
            }
            flush(b); break; }
        default: break;
    }
}

// ── Manual line marks (slope ticks, crossings, bridges, pinned pylons) ────────

// Total arc length of a (possibly closed) polyline.
static float PolyLength(const std::vector<Vec2>& poly, bool closed) {
    if (poly.size() < 2) return 0.0f;
    const size_t n = poly.size();
    const size_t segCount = closed ? n : n - 1;
    float total = 0.0f;
    for (size_t i = 0; i < segCount; ++i) total += Len(poly[(i + 1) % n] - poly[i]);
    return total;
}

// Sample a polyline at arc-length `d` from the start, returning point + unit
// tangent. Clamps to the ends.
static Station SampleAt(const std::vector<Vec2>& poly, bool closed, float d) {
    const size_t n = poly.size();
    const size_t segCount = closed ? n : n - 1;
    float acc = 0.0f;
    for (size_t i = 0; i < segCount; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float segLen = Len(b - a);
        if (segLen < 1e-6f) continue;
        if (d <= acc + segLen) {
            Vec2 dir = (b - a) * (1.0f / segLen);
            return { a + dir * (d - acc), dir };
        }
        acc += segLen;
    }
    // Past the end: clamp to the last vertex with the last tangent.
    Vec2 a = poly[n >= 2 ? n - 2 : 0], b = poly[n - 1];
    Vec2 dir = Norm(b - a);
    return { poly[n - 1], dir };
}

// A cut interval along the line, in arc-length [from, to]. Crossing/Bridge marks
// open a gap there so the base stroke is interrupted (the line "passes under").
struct CutSpan { float from, to; };

// Sub-extract the polyline between arc-lengths [from, to] as its own run.
static std::vector<Vec2> ExtractRun(const std::vector<Vec2>& poly, bool closed,
                                    float from, float to) {
    std::vector<Vec2> run;
    if (to <= from) return run;
    const size_t n = poly.size();
    const size_t segCount = closed ? n : n - 1;
    float acc = 0.0f;
    bool started = false;
    for (size_t i = 0; i < segCount; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float segLen = Len(b - a);
        if (segLen < 1e-6f) continue;
        Vec2 dir = (b - a) * (1.0f / segLen);
        float segStart = acc, segEnd = acc + segLen;
        // Entry point.
        if (!started && from <= segEnd) {
            float d = std::max(from, segStart);
            run.push_back(a + dir * (d - segStart));
            started = true;
        }
        if (started) {
            if (to < segEnd) { run.push_back(a + dir * (to - segStart)); break; }
            run.push_back(b);
        }
        acc = segEnd;
    }
    return run;
}

// Draw a part's base stroke as the complement of the cut spans (the line is
// interrupted at each crossing/bridge gap). Dashing inside each kept run is the
// caller's job; here we just split. Returns the kept [from,to] runs in order.
static std::vector<CutSpan> KeptRuns(float total, const std::vector<CutSpan>& cuts) {
    std::vector<CutSpan> sorted = cuts;
    std::sort(sorted.begin(), sorted.end(),
              [](const CutSpan& a, const CutSpan& b) { return a.from < b.from; });
    std::vector<CutSpan> kept;
    float cursor = 0.0f;
    for (const CutSpan& c : sorted) {
        if (c.from > cursor) kept.push_back({ cursor, c.from });
        cursor = std::max(cursor, c.to);
    }
    if (cursor < total) kept.push_back({ cursor, total });
    return kept;
}

// Stamp the manual marks (ticks/crossing bars/bridge brackets/pylons) for one
// subpath onto the mesh, scaled like the rest of the style.
static void StrokeMarks(const std::vector<Vec2>& poly, bool closed, int sub,
                        const std::vector<LineMark>& marks, const StrokeStyle& s,
                        float avgScale, float zoom, const Color& c, Mesh& out) {
    if (marks.empty() || poly.size() < 2) return;
    float total = PolyLength(poly, closed);
    if (total < 1e-4f) return;
    float baseTh = s.width;   // already scaled by caller
    for (const LineMark& m : marks) {
        if (m.sub != sub) continue;
        float d = std::clamp(m.t, 0.0f, 1.0f) * total;
        Station q = SampleAt(poly, closed, d);
        Vec2 t = q.t, nrm = Vec2{ -t.y, t.x };   // left of travel
        float th = (m.thickness > 1e-5f ? m.thickness : baseTh / avgScale) * avgScale;
        float size = m.size * avgScale;
        float gap  = m.gap  * avgScale;
        float side = (m.side >= 0) ? 1.0f : -1.0f;
        switch (m.kind) {
            case LineMarkKind::SlopeTick: {     // short downhill tick on one side
                // Starts at the curve CENTRE; with Outside Measure the visible part
                // past the line edge is exactly `size`, so the drawn length is
                // halfBaseWidth + size. Butt ends (no rounded tip).
                float len = m.outsideMeasure ? (s.width * 0.5f + size) : size;
                std::vector<Vec2> seg = { q.p, q.p + nrm * (len * side) };
                StrokeRun(seg, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                break; }
            case LineMarkKind::Crossing: {      // 519: two ticks across the gap ends
                float half = gap * 0.5f;
                for (float sgn : { -1.0f, +1.0f }) {
                    Station e = SampleAt(poly, closed, std::clamp(d + sgn * half, 0.0f, total));
                    Vec2 en = Vec2{ -e.t.y, e.t.x };
                    std::vector<Vec2> bar = { e.p - en * size, e.p + en * size };
                    StrokeRun(bar, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                              StrokeAlign::Center, zoom, c, out);
                }
                break; }
            case LineMarkKind::Bridge: {        // 512: two facing brackets at the gap ends
                float half = gap * 0.5f;
                // Each bracket is a short segment ACROSS the line plus two stubs
                // turned INWARD (toward the gap centre) — the two entrances face.
                for (float sgn : { -1.0f, +1.0f }) {
                    Station e = SampleAt(poly, closed, std::clamp(d + sgn * half, 0.0f, total));
                    Vec2 et = e.t, en = Vec2{ -et.y, et.x };
                    Vec2 inward = et * (-sgn);   // points toward the gap centre
                    Vec2 top = e.p + en * size, bot = e.p - en * size;
                    std::vector<Vec2> bracket = {
                        top + inward * size * 0.6f, top, bot, bot + inward * size * 0.6f };
                    StrokeRun(bracket, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                              StrokeAlign::Center, zoom, c, out);
                }
                break; }
            case LineMarkKind::Pylon: {         // pinned pylon bar across the line
                std::vector<Vec2> bar = { q.p - nrm * size, q.p + nrm * size };
                StrokeRun(bar, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                          StrokeAlign::Center, zoom, c, out);
                // Square variant: a small box centred on the bar (inside-stroke),
                // its sides along the line tangent / normal. `gap` = box side.
                if (m.square && gap > 1e-4f) {
                    float h = gap * 0.5f;
                    Vec2 c0 = q.p - t * h - nrm * h;
                    Vec2 c1 = q.p + t * h - nrm * h;
                    Vec2 c2 = q.p + t * h + nrm * h;
                    Vec2 c3 = q.p - t * h + nrm * h;
                    // Inside-stroke square outline → inset the path by half thickness.
                    auto edge = [&](Vec2 A, Vec2 B) {
                        std::vector<Vec2> e = { A, B };
                        StrokeRun(e, false, th, LineCap::Butt, LineJoin::Miter, 4.0f,
                                  StrokeAlign::Center, zoom, c, out);
                    };
                    edge(c0, c1); edge(c1, c2); edge(c2, c3); edge(c3, c0);
                }
                break; }
        }
    }
}

// ── Stroke (public entry, plain center/round) ─────────────────────────────────
void Tessellator::StrokePolyline(const std::vector<Vec2>& poly, bool closed,
                                 float width, const Color& c, Mesh& out) {
    StrokeRun(poly, closed, width, LineCap::Round, LineJoin::Round, 4.0f,
              StrokeAlign::Center, 1.0f, c, out);
}

// Stroke a part's flattened outline honouring its full StrokeStyle (dash + cap +
// join + align), then stamp its decorator.
static void StrokeStyled(const std::vector<Vec2>& poly, bool closed,
                         const StrokeStyle& s, float scaledWidth, float zoom,
                         const Color& c, Mesh& out,
                         const std::vector<CutSpan>& cuts = {},
                         const std::vector<DashAnchor>& anchors = {},
                         const DecorSink* decorSink = nullptr,
                         bool overrideCaps = false,
                         LineCap capStartOv = LineCap::Round,
                         LineCap capEndOv   = LineCap::Round,
                         bool emitDecor = true) {
    // DoubleLine / Railway draw their OWN parallel rails in StrokeDecor; the base
    // centerline must NOT be drawn for them (it would add a 3rd line).
    const bool baseSuppressed = (s.decor == LineDecor::DoubleLine ||
                                 s.decor == LineDecor::Railway ||
                                 s.decor == LineDecor::DoubleSlashes ||
                                 s.decor == LineDecor::DoublePylons ||
                                 s.decor == LineDecor::DoubleTicks);
    // Taper cap is drawn by us (butt base + a triangular tip at each open end);
    // the StrokeRun gets butt so the tip joins cleanly.
    const bool taper = (s.cap == LineCap::Taper) && !closed && poly.size() >= 2;
    LineCap runCap = taper ? LineCap::Butt : s.cap;
    // A part with crossing/bridge marks is cut into KEPT runs at each gap (the
    // line passes "under"); each kept run is then dashed/stroked on its own.
    auto strokeBaseRun = [&](const std::vector<Vec2>& run, bool runClosed,
                             const std::vector<DashAnchor>& runAnchors) {
        if (run.size() < 2) return;
        // Align passes through for BOTH closed and open runs: StrokeRun interprets
        // Inner/Outer as inside/outside for a ring and as the two SIDES of an open
        // path (so Center / Side A / Side B work on bezier/NURBS curves too).
        if (s.dash.empty()) {
            StrokeRun(run, runClosed, scaledWidth, runCap, s.join, s.miterLimit,
                      s.align, zoom, c, out,
                      capStartOv, capEndOv, overrideCaps && !taper);
        } else {
            auto dr = DashRuns(run, runClosed, s.dash, s.decorCentered, runAnchors);
            for (auto& d : dr)
                StrokeRun(d, false, scaledWidth, runCap, s.join, s.miterLimit,
                          s.align, zoom, c, out);
        }
    };
    // The kept runs (whole poly when uncut). A crossing/bridge cuts the line into
    // independent pieces: each piece is stroked AND decorated on its own, so the
    // dots/pickets/ties restart symmetrically at a coherent distance from the gap
    // edges (the line behaves as if physically cut in two).
    std::vector<std::vector<Vec2>> runs;
    if (cuts.empty()) {
        // No cut → keep the original poly (and its closed-ness for area outlines).
        runs.push_back(poly);
    } else {
        float total = PolyLength(poly, closed);
        for (const CutSpan& k : KeptRuns(total, cuts)) {
            std::vector<Vec2> r = ExtractRun(poly, closed, k.from, k.to);
            if (r.size() >= 2) runs.push_back(std::move(r));
        }
    }
    const bool runsClosed = cuts.empty() && closed;
    // Anchors only apply to the WHOLE-curve (uncut) case — a cut already re-phases
    // each segment. Pass them to the single run; cut runs phase symmetrically.
    if (!baseSuppressed) {
        if (cuts.empty()) strokeBaseRun(poly, closed, anchors);
        else for (const auto& r : runs) strokeBaseRun(r, false, {});
    }
    if (taper) {
        // A sharp triangular tip past EACH end, length capTaper (auto-follows the
        // real endpoints since `poly` is the flattened curve).
        float tip = s.capTaper;                       // already scaled by caller
        float hw = scaledWidth * 0.5f;
        auto tipAt = [&](Vec2 end, Vec2 inward) {
            Vec2 d = Norm(end - inward); if (d.x==0 && d.y==0) return;
            Vec2 nrm = Vec2{ -d.y, d.x };
            PushTri(out, end + nrm*hw, end - nrm*hw, end + d*tip, c);
        };
        tipAt(poly.front(), poly[1]);
        tipAt(poly.back(),  poly[poly.size()-2]);
    }
    // Decorate each kept run independently (or the whole curve when uncut). When the
    // part is dashed AND uses a per-dash element decor, the decorator reads the dash
    // MIDPOINTS (shared phasing) so the element is always centred in its dash.
    //   • With a DecorSink (live viewport): continuous/composite RAILS are baked into
    //     `out`; the periodic GLYPHS are emitted as instances → no per-glyph triangles.
    //   • Without (thumbnails/glyph cache): bake everything as triangles (StrokeDecor).
    // `emitDecor=false` for a junction part's individual subpaths: their decorators
    // are emitted ONCE post-loop along the TRAVERSAL polys (continuous through the
    // junction), so every decorator follows the branch graph instead of restarting
    // per-strand. The ribbon above is still emitted (the stroke body).
    if (!emitDecor) return;
    for (const auto& r : runs) {
        const std::vector<DashAnchor>* a =
            (&r == &runs.front() && cuts.empty()) ? &anchors : nullptr;
        if (decorSink && decorSink->instances) {
            StrokeDecorRails(r, runsClosed, s, zoom, c, out);
            StrokeDecorInstanced(r, runsClosed, s, zoom, c,
                                 *decorSink->instances, *decorSink->batches, a);
        } else {
            StrokeDecor(r, runsClosed, s, zoom, c, out, a);
        }
    }
}

// ── Surface fill layers (infinite, clipped pattern fills) ─────────────────────

// Point-in-polygon (even-odd ray cast) for clipping dot/triangle screens.
static bool PointInPoly(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        bool cond = ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                       (poly[j].y - poly[i].y) + poly[i].x);
        if (cond) in = !in;
    }
    return in;
}

// Clip an infinite line (point a, direction d unit) to the polygon: collect the
// parameter t at each edge crossing, sort, and the INSIDE spans are the
// consecutive pairs. Returns the in/out span endpoints as points.
static std::vector<std::pair<Vec2,Vec2>> ClipLineToPoly(const std::vector<Vec2>& poly,
                                                        Vec2 a, Vec2 d, float tMin, float tMax) {
    std::vector<float> ts;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Vec2 e = poly[i] - poly[j];               // edge j→i
        float denom = Cross(d, e);
        if (std::fabs(denom) < 1e-9f) continue;   // parallel
        Vec2 w = poly[j] - a;
        float t = Cross(w, e) / denom;            // param along the infinite line
        float u = Cross(w, d) / denom;            // param along the edge [0,1]
        if (u >= -1e-4f && u <= 1.0f + 1e-4f && t >= tMin && t <= tMax) ts.push_back(t);
    }
    std::sort(ts.begin(), ts.end());
    std::vector<std::pair<Vec2,Vec2>> spans;
    for (size_t i = 0; i + 1 < ts.size(); i += 2) {
        if (ts[i+1] - ts[i] < 1e-4f) continue;
        spans.push_back({ a + d * ts[i], a + d * ts[i+1] });
    }
    return spans;
}

// Generate one FillLayer's pattern across `poly` (a flattened CLOSED contour, in
// WORLD doc-units) and clip it to the contour. `avgScale` scales the pattern's
// doc-unit dimensions with the object. Appends triangles to `out`.
static void AppendFillLayer(const std::vector<Vec2>& poly, const FillLayer& fl,
                            float avgScale, float baseAngle, float zoom, Mesh& out) {
    if (!fl.enabled || poly.size() < 3) return;
    Color col = fl.color;
    Vec2 mn{1e30f,1e30f}, mx{-1e30f,-1e30f};
    for (Vec2 p : poly) { mn.x=std::min(mn.x,p.x); mn.y=std::min(mn.y,p.y);
                          mx.x=std::max(mx.x,p.x); mx.y=std::max(mx.y,p.y); }
    const float spacing = std::max(0.05f, fl.spacing * avgScale);
    const float size    = std::max(0.02f, fl.size * avgScale);
    // The pattern rotates WITH the object (baseAngle = the shape's rotation), so a
    // surface's internal pattern (e.g. orchard/vineyard rows) turns when the
    // object is rotated, on top of the layer's own angleDeg.
    const float ang = fl.angleDeg * 3.14159265f / 180.0f + baseAngle;
    const float ca = std::cos(ang), sa = std::sin(ang);
    auto rot = [&](Vec2 v){ return Vec2{ v.x*ca - v.y*sa, v.x*sa + v.y*ca }; };
    const Vec2  off = rot(Vec2{ fl.offset.x * avgScale, fl.offset.y * avgScale });

    switch (fl.pattern) {
        case FillPattern::Solid: {
            Color c2 = col; c2.a *= fl.opacity;
            Tessellator::FillPolygonEarClip(poly, c2, out);
            break; }
        case FillPattern::Dots:
        case FillPattern::RandomDots: {
            const float segPerRad = std::clamp(size * 0.5f * 0.7f + 4.0f, 4.0f, 24.0f);
            // Walk a grid in the pattern's local (rotated) frame so angle works.
            // Iterate a bounding grid generously, jitter for RandomDots.
            float diag = Len(mx - mn) + spacing * 2.0f;
            int steps = (int)std::ceil(diag / spacing) + 2;
            Vec2 c0{ (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
            for (int gy = -steps; gy <= steps; ++gy)
            for (int gx = -steps; gx <= steps; ++gx) {
                Vec2 local{ gx*spacing + off.x, gy*spacing + off.y };
                if (fl.pattern == FillPattern::RandomDots) {
                    uint32_t h = fl.seed ^ (uint32_t)(gx*73856093) ^ (uint32_t)(gy*19349663);
                    h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15;
                    float jx = ((h & 0xFFFF)/65535.0f - 0.5f) * spacing * 0.8f;
                    float jy = (((h>>16)&0xFFFF)/65535.0f - 0.5f) * spacing * 0.8f;
                    local.x += jx; local.y += jy;
                }
                Vec2 p = c0 + rot(local);
                if (!PointInPoly(poly, p)) continue;
                FanArc(out, p, size * 0.5f, 0.0f, 6.2831853f, segPerRad, col);
            }
            break; }
        case FillPattern::Triangles: {
            float diag = Len(mx - mn) + spacing * 2.0f;
            int steps = (int)std::ceil(diag / spacing) + 2;
            Vec2 c0{ (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
            // ISOM boulder-field triangle: sides ratio 8:6:5 (a scalene triangle),
            // RANDOMLY oriented per instance (jittered position too) so the field
            // reads as scattered blocks, not a regular grid of identical glyphs.
            // Build the 8:6:5 triangle once (vertices A,B,C; longest side = 8).
            const float s8 = size, s6 = size * 0.75f, s5 = size * 0.625f;
            // Place A=(0,0), B=(s8,0); C from side lengths AC=s6, BC=s5.
            float cx = (s8*s8 + s6*s6 - s5*s5) / (2.0f * s8);
            float cy = std::sqrt(std::max(0.0f, s6*s6 - cx*cx));
            // Centre the triangle on its centroid so rotation is about the centre.
            Vec2 ce{ (0 + s8 + cx)/3.0f, (0 + 0 + cy)/3.0f };
            Vec2 tA{ 0 - ce.x, 0 - ce.y }, tB{ s8 - ce.x, 0 - ce.y }, tC{ cx - ce.x, cy - ce.y };
            for (int gy = -steps; gy <= steps; ++gy)
            for (int gx = -steps; gx <= steps; ++gx) {
                uint32_t h = fl.seed ^ (uint32_t)(gx*73856093) ^ (uint32_t)(gy*19349663);
                h ^= h>>13; h *= 0x5bd1e995u; h ^= h>>15;
                float jx = ((h & 0xFFFF)/65535.0f - 0.5f) * spacing * 0.5f;
                float jy = (((h>>16)&0xFFFF)/65535.0f - 0.5f) * spacing * 0.5f;
                Vec2 p = c0 + rot(Vec2{ gx*spacing + off.x + jx, gy*spacing + off.y + jy });
                if (!PointInPoly(poly, p)) continue;
                float ta = ((h>>8 & 0xFFFF)/65535.0f) * 6.2831853f;   // random orientation
                float ra = std::cos(ta), rb = std::sin(ta);
                auto place = [&](Vec2 v){ return Vec2{ p.x + v.x*ra - v.y*rb, p.y + v.x*rb + v.y*ra }; };
                PushTri(out, place(tA), place(tB), place(tC), col);
            }
            break; }
        case FillPattern::Lines:
        case FillPattern::CrossHatch:
        case FillPattern::Grid: {
            float diag = Len(mx - mn) + spacing * 2.0f;
            int steps = (int)std::ceil(diag / spacing) + 2;
            Vec2 c0{ (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
            int sets = (fl.pattern == FillPattern::Lines) ? 1 : 2;
            for (int set = 0; set < sets; ++set) {
                float a2 = ang + (set ? 1.5707963f : 0.0f);
                float c2a = std::cos(a2), s2a = std::sin(a2);
                Vec2 dir{ c2a, s2a };                  // line direction
                Vec2 nrm{ -s2a, c2a };                 // step direction
                const float dash = fl.dash * avgScale, dgap = fl.dashGap * avgScale;
                const bool  dashed = (dash > 1e-4f);
                const float period = dash + dgap;
                for (int k = -steps; k <= steps; ++k) {
                    Vec2 base = c0 + nrm * (k*spacing) + off;
                    auto spans = ClipLineToPoly(poly, base, dir, -diag, diag);
                    // Alternate the dash phase on every other line (indistinct
                    // marsh / vineyard rows): shift by half a period.
                    float phase = (fl.altPhase && (k & 1)) ? period * 0.5f : 0.0f;
                    for (auto& sp : spans) {
                        if (!dashed) {
                            std::vector<Vec2> seg = { sp.first, sp.second };
                            StrokeRun(seg, false, size, LineCap::Butt, LineJoin::Miter,
                                      4.0f, StrokeAlign::Center, zoom, col, out);
                        } else {
                            // Walk the span emitting dash segments with the phase.
                            float L = Len(sp.second - sp.first);
                            Vec2 d = Norm(sp.second - sp.first);
                            float t = std::fmod(phase, period);
                            // Start partway so the phase lands correctly.
                            float pos = -t;
                            while (pos < L) {
                                float a = std::max(0.0f, pos);
                                float b = std::min(L, pos + dash);
                                if (b > a) {
                                    std::vector<Vec2> seg = { sp.first + d*a, sp.first + d*b };
                                    StrokeRun(seg, false, size, LineCap::Butt, LineJoin::Miter,
                                              4.0f, StrokeAlign::Center, zoom, col, out);
                                }
                                pos += period;
                            }
                        }
                    }
                }
            }
            break; }
    }
}

// True for patterns drawn PROCEDURALLY (a cover quad + pattern_fill.frag, clipped by
// the stencil): everything except Solid, which is a single filled polygon baked into
// the mesh. Grid uses the dot lattice in the legacy bake; here it is line-based.
static bool PatternIsProcedural(FillPattern p) {
    return p != FillPattern::Solid;
}

// Map FillPattern → the kind code the fragment shader expects.
//   1 Dots  2 Lines  3 Triangles  4 RandomDots  5 Grid  6 CrossHatch
static uint8_t PatternKindCode(FillPattern p) {
    switch (p) {
        case FillPattern::Dots:       return 1;
        case FillPattern::Lines:      return 2;
        case FillPattern::Triangles:  return 3;
        case FillPattern::RandomDots: return 4;
        case FillPattern::Grid:       return 5;
        case FillPattern::CrossHatch: return 6;
        default:                      return 1;
    }
}

// Build the procedural motif params for one layer (everything except the lattice
// `center`). All doc-unit dims scale by `avgScale`; the angle adds the shape
// rotation. The caller fills `center` (the cut polygon's bbox centre).
static PatternParams FillLayerToParamsNoCenter(const FillLayer& fl, float avgScale,
                                               float baseAngle) {
    PatternParams pp;
    pp.kind     = PatternKindCode(fl.pattern);
    pp.color    = fl.color;
    pp.spacing  = std::max(0.05f, fl.spacing * avgScale);
    pp.size     = std::max(0.02f, fl.size * avgScale);
    pp.angle    = fl.angleDeg * 3.14159265f / 180.0f + baseAngle;
    pp.offset   = Vec2{ fl.offset.x * avgScale, fl.offset.y * avgScale };
    pp.seed     = fl.seed;
    pp.dash     = fl.dash * avgScale;
    pp.dashGap  = fl.dashGap * avgScale;
    pp.altPhase = fl.altPhase ? 1 : 0;
    return pp;
}

// Full params with the lattice `center` set to the caller-supplied anchor (the
// object origin, or {0,0} for the document anchor). NOT the cover bbox — a bbox
// anchor would slide the motif when the geometry is edited in place.
static PatternParams FillLayerToParams(const FillLayer& fl, float avgScale,
                                       float baseAngle, Vec2 anchorCenter) {
    PatternParams pp = FillLayerToParamsNoCenter(fl, avgScale, baseAngle);
    pp.center = anchorCenter;
    return pp;
}


// ── Public ──────────────────────────────────────────────────────────────────

// Derive a layer's CUT polygon from its fill polygon `fp` + clip mode + stroke.
// Construction = the centreline `fp`; SideA_Inner / SideB_Outer offset `fp` by the
// stroke's inner / outer half-extent, resolved against the contour winding so
// "inner" is geometrically inside regardless of CW/CCW authoring.
static std::vector<Vec2> DeriveClipPoly(const std::vector<Vec2>& fp, FillClip clip,
                                        const StrokeStyle& stroke, float avgScale) {
    if (clip == FillClip::Construction || !stroke.enabled || stroke.width <= 0.0f)
        return fp;
    const float w = stroke.width * avgScale;
    // Distance from the centreline to the requested edge, per stroke align.
    float dist = 0.0f;
    const bool inner = (clip == FillClip::SideA_Inner);
    switch (stroke.align) {
        case StrokeAlign::Center: dist = w * 0.5f; break;
        case StrokeAlign::Inner:  dist = inner ? w   : 0.0f; break;   // all width inside
        case StrokeAlign::Outer:  dist = inner ? 0.0f : w;   break;   // all width outside
    }
    if (dist <= 1e-5f) return fp;
    // OffsetPoly's +normal is the polygon's LEFT; for a CCW ring (signed area > 0)
    // the left points INSIDE. So inward = +dist when CCW, −dist when CW.
    float area2 = 0.0f;
    const size_t n = fp.size();
    for (size_t i = 0; i < n; ++i) area2 += Cross(fp[i], fp[(i + 1) % n]);
    const bool ccw = area2 > 0.0f;
    float signedD = inner ? (ccw ? +dist : -dist) : (ccw ? -dist : +dist);
    std::vector<Vec2> off = OffsetPoly(fp, /*closed=*/true, signedD, stroke.miterLimit);
    if (off.size() < 3) return fp;
    // Collapse / inversion guard: a thin shape inset past its own medial axis flips
    // the signed area (offset ring turns inside-out) → the inner fill would be drawn
    // wrong / invisible. Detect the sign flip (or near-zero area) and fall back to the
    // original contour rather than emitting a degenerate inner ring.
    float offArea = 0.0f;
    for (size_t i = 0; i < off.size(); ++i) offArea += Cross(off[i], off[(i + 1) % off.size()]);
    if (offArea * area2 <= 1e-6f) return fp;   // sign flipped or collapsed
    return off;
}

// Collect a subpath's crossing/bridge CUTS + DashAnchor anchors from its marks, in
// flattened `poly` (world) space. Shared by AppendShapeImpl and RefreshDecorInstances
// so the instanced decorators see the same run-cutting + phasing as the baked path.
static void CollectPartCutsAnchors(const Shape& shape, const Part& part, int sp,
                                   const std::vector<Vec2>& poly, bool closed,
                                   float avgScale, Vec2 pageOrigin,
                                   std::vector<CutSpan>& cuts,
                                   std::vector<DashAnchor>& anchors) {
    if (part.marks.empty()) return;
    float total = PolyLength(poly, closed);
    for (const LineMark& m : part.marks) {
        if (m.sub != sp) continue;
        if (m.kind == LineMarkKind::Crossing || m.kind == LineMarkKind::Bridge) {
            float d = std::clamp(m.t, 0.0f, 1.0f) * total;
            float half = std::max(m.gap * avgScale, 1e-3f) * 0.5f;
            cuts.push_back({ std::max(0.0f, d - half), std::min(total, d + half) });
        } else if (m.kind == LineMarkKind::DashAnchor) {
            float dpos = std::clamp(m.t, 0.0f, 1.0f) * total;
            if (m.nodeAnchor >= 0 && m.nodeAnchor < (int)part.path.nodes.size()) {
                Vec2 np = Tessellator::WorldTransform(shape,
                    part.path.nodes[(size_t)m.nodeAnchor].pos, pageOrigin);
                size_t n = poly.size(), sc = closed ? n : n - 1;
                float acc = 0.0f, bestD = 1e30f;
                for (size_t i = 0; i < sc; ++i) {
                    Vec2 a = poly[i], b = poly[(i+1)%n];
                    Vec2 ab{ b.x-a.x, b.y-a.y };
                    float L2 = ab.x*ab.x + ab.y*ab.y; if (L2 < 1e-9f) continue;
                    float u = std::clamp(((np.x-a.x)*ab.x+(np.y-a.y)*ab.y)/L2, 0.0f, 1.0f);
                    Vec2 pr{ a.x+ab.x*u, a.y+ab.y*u };
                    float d2 = (np.x-pr.x)*(np.x-pr.x)+(np.y-pr.y)*(np.y-pr.y);
                    if (d2 < bestD) { bestD = d2; dpos = acc + std::sqrt(L2)*u; }
                    acc += std::sqrt(L2);
                }
            }
            anchors.push_back({ dpos, m.side >= 0 });
        }
    }
}

// Shared body of AppendShape / AppendShapePatterned. `sink` null → patterns baked
// into `out` as triangles (legacy); else patterns are emitted as instances + masks
// (see PatternSink). A member so it can reach the private geometry helpers.
void Tessellator::AppendShapeImpl(const Shape& shape, Mesh& out, float zoom,
                                  Vec2 pageOrigin, const PatternSink* sink,
                                  const DecorSink* decorSink) {
    if (!shape.visible) return;
    const float avgScale = 0.5f *
        (std::fabs(shape.transform.scale.x) + std::fabs(shape.transform.scale.y));
    // The object's origin in the PAGE-LOCAL world frame (the frame cover verts +
    // pattern centre live in before the per-append page shift). = origin+translate,
    // independent of geometry → a stable lattice anchor under edit-mode point moves.
    const Vec2 objAnchor = WorldTransform(shape, shape.origin, Vec2{0, 0});
    for (const Part& part : shape.parts) {
        // Each subpath (strand) of a branched path is filled + stroked on its own
        // (segments never bridge across a subpath boundary).
        const int subs = SubpathCount(part);
        // MULTI-PATH (junction) parts: when several subpaths share a junction vertex,
        // their transparent stroke ribbons OVERLAP at the junction. Emitting one
        // StrokeRec per subpath makes each its own stencil ref → the overlap blends
        // twice (alpha doubling) + leaves a cap at the junction. Instead accumulate
        // ALL subpath ribbons of such a part into ONE coverage region (one StrokeRec),
        // so the whole multi-path curve is filled ONCE — no doubling, the overlapping
        // ribbon ends merge into a single uniform stroke. Non-junction parts keep the
        // per-subpath emission (byte-identical to before — no regression).
        bool partHasJunction = false;
        for (const Node& jn : part.path.nodes)
            if (jn.junctionId != 0) { partHasJunction = true; break; }
        Mesh partRibbon;            // accumulated transparent ribbon for a junction part
        bool partRibbonActive = false;
        for (int sp = 0; sp < subs; ++sp) {
            bool closed = false;
            std::vector<Vec2> poly = OutlinePartSub(shape, part, sp, zoom, closed, pageOrigin);
            if (poly.empty()) continue;
            // Compute the FILL polygon AT MOST ONCE (shared by the solid fill + the
            // surface layers). A closed contour fills directly; an OPEN curve/area
            // fills by closing virtually between its endpoints (the closing edge
            // follows the outer handles — OutlinePartSubFilled). The stroke stays
            // open. Plain polylines aren't filled.
            const bool wantsFill = (part.fill.enabled || !part.fillLayers.empty())
                                   && part.kind != ShapeKind::Polyline;
            std::vector<Vec2> fillPoly;          // built lazily, reused below
            const std::vector<Vec2>* fp = nullptr;
            if (wantsFill) {
                if (closed && poly.size() >= 3) fp = &poly;
                else if (!closed && part.IsCurveLike()) {
                    fillPoly = OutlinePartSubFilled(shape, part, sp, zoom, pageOrigin);
                    if (fillPoly.size() >= 3) fp = &fillPoly;
                }
            }
            if (fp && part.fill.enabled) {
                bool convex = (part.kind == ShapeKind::Rectangle ||
                               part.kind == ShapeKind::Ellipse ||
                               part.kind == ShapeKind::Triangle);
                if (convex) FillConvexFan(*fp, part.fill.color, out);
                else        FillPolygonEarClip(*fp, part.fill.color, out);
            }
            if (fp && !part.fillLayers.empty()) {
                const bool proc = (sink && sink->recs);
                // Each layer chooses its OWN cut edge (clip), so the cover polygon is
                // per-layer. Memoise the last clip mode so consecutive layers sharing
                // a clip reuse the same triangulated cover (the common case).
                int      memoClip = -1;
                uint32_t memoFirst = 0, memoCount = 0;
                std::vector<Vec2> memoClipPoly;
                for (const FillLayer& fl : part.fillLayers) {
                    if (!fl.enabled) continue;
                    if (proc && PatternIsProcedural(fl.pattern)) {
                        // Cover polygon for this layer's clip (reuse if unchanged).
                        if ((int)fl.clip != memoClip) {
                            memoClip = (int)fl.clip;
                            memoClipPoly = DeriveClipPoly(*fp, fl.clip, part.stroke, avgScale);
                            memoFirst = (uint32_t)sink->cover->vertices.size();
                            FillPolygonEarClip(memoClipPoly, Color{0,0,0,1}, *sink->cover);
                            memoCount = (uint32_t)sink->cover->vertices.size() - memoFirst;
                        }
                        if (memoCount == 0) continue;
                        // One PatternRec per layer; the fragment shader paints the
                        // motif, clipped to the cover/stencil. No per-element geometry.
                        PatternRec rec;
                        rec.coverFirst = memoFirst;
                        rec.coverCount = memoCount;
                        Vec2 anchorCenter = (fl.anchor == FillAnchor::DocumentOrigin)
                                          ? Vec2{0, 0} : objAnchor;
                        rec.params = FillLayerToParams(fl, avgScale,
                                                       shape.transform.rotate, anchorCenter);
                        sink->recs->push_back(rec);
                    } else {
                        // Solid (always), or the legacy (no-sink) path for thumbnails:
                        // bake the pattern as triangles into the base mesh.
                        AppendFillLayer(*fp, fl, avgScale, shape.transform.rotate, zoom, out);
                    }
                }
            }
            if (part.stroke.enabled && part.stroke.width > 0.0f) {
                // Scale every doc-unit dimension of the style by the object's avg
                // scale so a scaled object keeps proportional dashes/tags/dots.
                StrokeStyle st = part.stroke;
                st.width        *= avgScale;
                st.decorSpacing *= avgScale;
                st.decorSize    *= avgScale;
                st.decorThickness *= avgScale;
                st.capTaper       *= avgScale;
                for (float& d : st.dash) d *= avgScale;
                // Manual marks: crossing/bridge marks cut the base line; all marks
                // are stamped after. Cuts use UNSCALED gap (× avgScale inside the
                // helper via the same total arc length, so compute in poly space —
                // `poly` is already world-scaled, so scale the gap too).
                std::vector<CutSpan> cuts;
                std::vector<DashAnchor> anchors;   // dash/pattern phase anchors
                CollectPartCutsAnchors(shape, part, sp, poly, closed, avgScale,
                                       pageOrigin, cuts, anchors);
                // Transparent stroke + procedural sink → route the RIBBON (and its
                // same-coloured marks) into the stroke-coverage stream so the renderer
                // can stencil-then-fill-once (no alpha doubling on the overlapping
                // ribbon/joins/corners). Opaque strokes bake straight into `out` as
                // today — overlap is invisible at alpha=1, and it avoids the extra
                // pass. Instanced glyph decorators (decorSink) are unaffected either
                // way (separate non-overlapping instances).
                // Per-END cap override for a junction part: only the end that SITS ON
                // the shared vertex becomes Butt (flush, covered by the overlap); the
                // FREE end keeps its normal cap. Determined from THIS subpath's first /
                // last node junctionId. (overrideCaps=false → both ends use s.cap.)
                bool ovCaps = false; LineCap capS = st.cap, capE = st.cap;
                if (partHasJunction && !closed) {
                    int nb = 0, ne = (int)part.path.nodes.size();
                    part.path.subRange(sp, nb, ne);
                    if (nb >= 0 && ne > nb && ne <= (int)part.path.nodes.size()) {
                        bool startJ = part.path.nodes[(size_t)nb].junctionId != 0;
                        bool endJ   = part.path.nodes[(size_t)(ne - 1)].junctionId != 0;
                        if (startJ || endJ) {
                            ovCaps = true;
                            capS = startJ ? LineCap::Butt : st.cap;
                            capE = endJ   ? LineCap::Butt : st.cap;
                        }
                    }
                }
                const bool stencilStroke =
                    sink && sink->strokeCover && part.stroke.color.a < 0.999f;
                // Junction parts emit decorators ONCE post-loop along traversal polys.
                const bool subDecor = !partHasJunction;
                if (stencilStroke) {
                    Mesh ribbon;
                    StrokeStyled(poly, closed, st, st.width, zoom, part.stroke.color,
                                 ribbon, cuts, anchors, decorSink, ovCaps, capS, capE,
                                 subDecor);
                    if (!part.marks.empty())
                        StrokeMarks(poly, closed, sp, part.marks, st, avgScale, zoom,
                                    part.stroke.color, ribbon);
                    if (partHasJunction) {
                        // Accumulate into the part-wide ribbon (one StrokeRec later).
                        for (const Vertex& v : ribbon.vertices) partRibbon.vertices.push_back(v);
                        partRibbonActive = true;
                    } else if (!ribbon.vertices.empty()) {
                        StrokeRec sr;
                        sr.coverFirst = (uint32_t)sink->strokeCover->vertices.size();
                        sr.coverCount = (uint32_t)ribbon.vertices.size();
                        sr.color = part.stroke.color;
                        sr.bbMin = { 1e30f, 1e30f }; sr.bbMax = { -1e30f, -1e30f };
                        for (const Vertex& v : ribbon.vertices) {
                            sr.bbMin.x = std::min(sr.bbMin.x, v.x);
                            sr.bbMin.y = std::min(sr.bbMin.y, v.y);
                            sr.bbMax.x = std::max(sr.bbMax.x, v.x);
                            sr.bbMax.y = std::max(sr.bbMax.y, v.y);
                            sink->strokeCover->vertices.push_back(v);
                        }
                        sink->strokeRecs->push_back(sr);
                    }
                } else {
                    // Opaque: overlap is invisible at alpha=1, but a junction part still
                    // wants a BUTT cap on the junction-side end only (free end keeps it).
                    StrokeStyled(poly, closed, st, st.width, zoom, part.stroke.color, out,
                                 cuts, anchors, decorSink, ovCaps, capS, capE, subDecor);
                    if (!part.marks.empty())
                        StrokeMarks(poly, closed, sp, part.marks, st, avgScale, zoom,
                                    part.stroke.color, out);
                }
            }
        }   // subpath loop

        // ── Junction JOINS (round / bevel / miter), driven by the stroke join mode ──
        // At a shared vertex the branch ribbons leave a wedge gap on the outer side of
        // each pair of branches. Fill it with a join cover so the corner reads clean &
        // distinct per mode. Emitted into the SAME target as the ribbons (one coverage
        // region for transparent → filled once; baked into `out` for opaque).
        if (partHasJunction && part.stroke.enabled && part.stroke.width > 0.0f) {
            Mesh& jt = partRibbonActive ? partRibbon : out;
            const float hw = (part.stroke.width * avgScale) * 0.5f;
            // px-per-doc for round-arc smoothness (same basis as StrokeRun).
            const float radPx = hw * std::max(gDetailScale, 0.05f);
            const float segPerRad = std::clamp(radPx * 0.5f + 4.0f, 4.0f, 48.0f);
            const auto& nds = part.path.nodes;
            // Group node indices by junctionId.
            std::vector<uint32_t> seen;
            for (int i = 0; i < (int)nds.size(); ++i) {
                uint32_t jid = nds[(size_t)i].junctionId;
                if (jid == 0) continue;
                if (std::find(seen.begin(), seen.end(), jid) != seen.end()) continue;
                seen.push_back(jid);
                // World junction position (all coincident nodes share it).
                Vec2 jw = WorldTransform(shape, nds[(size_t)i].pos, pageOrigin);
                if (part.stroke.join == LineJoin::Round) {
                    // A disc of the stroke half-width fills any-degree junction smoothly.
                    FanArc(jt, jw, hw, 0.0f, 6.28318531f, segPerRad, part.stroke.color);
                    continue;
                }
                // Bevel / Miter: collect each branch's outgoing direction at the vertex,
                // take its two stroke-edge points (jw ± perp·hw), then fan them around
                // the junction sorted by angle so the wedge gaps are covered flat.
                struct EP { Vec2 p; float a; };
                std::vector<EP> edge;
                for (int k = 0; k < (int)nds.size(); ++k) {
                    if (nds[(size_t)k].junctionId != jid) continue;
                    // Outgoing direction: toward the branch's next node in its subpath.
                    int sub = part.path.subOf(k);
                    int sb = 0, se = (int)nds.size(); part.path.subRange(sub, sb, se);
                    Vec2 dirLocal{0,0};
                    if (k + 1 < se) dirLocal = nds[(size_t)(k+1)].pos - nds[(size_t)k].pos;
                    else if (k - 1 >= sb) dirLocal = nds[(size_t)k].pos - nds[(size_t)(k-1)].pos;
                    Vec2 dW = WorldTransform(shape, nds[(size_t)k].pos + dirLocal, pageOrigin) - jw;
                    float dl = std::hypot(dW.x, dW.y);
                    if (dl < 1e-5f) continue;
                    dW.x /= dl; dW.y /= dl;
                    Vec2 perp{ -dW.y, dW.x };
                    float ext = (part.stroke.join == LineJoin::Miter) ? hw * 1.6f : hw;
                    Vec2 p1 = jw + perp * hw, p2 = jw - perp * hw;
                    if (part.stroke.join == LineJoin::Miter) {
                        // Pull the edge points slightly outward along the branch so the
                        // bevel chamfer reads as a sharper (mitred) corner.
                        p1.x += dW.x * (ext - hw); p1.y += dW.y * (ext - hw);
                        p2.x += dW.x * (ext - hw); p2.y += dW.y * (ext - hw);
                    }
                    edge.push_back({ p1, std::atan2(p1.y - jw.y, p1.x - jw.x) });
                    edge.push_back({ p2, std::atan2(p2.y - jw.y, p2.x - jw.x) });
                }
                if (edge.size() >= 3) {
                    std::sort(edge.begin(), edge.end(),
                              [](const EP& a, const EP& b){ return a.a < b.a; });
                    // Fan from the junction centre to the angle-sorted edge ring.
                    for (size_t k = 0; k < edge.size(); ++k)
                        PushTri(jt, jw, edge[k].p, edge[(k + 1) % edge.size()].p,
                                part.stroke.color);
                }
            }
        }

        // ── Junction DECORATORS along TRAVERSAL polys ───────────────────────────
        // Every decorator (edge-lines, ties, dots, slashes, rails…) must follow the
        // branch graph as ONE continuous run that turns through the junction, not a
        // separate run per strand. Build maximal TRAVERSAL polylines that walk through
        // each junction picking the STRAIGHTEST continuation, then run the decorator
        // helpers ONCE per traversal poly. Their lateral offset (edge-line) is the
        // OffsetPoly of the traversal poly, so it naturally detours around the branch.
        if (partHasJunction && part.stroke.enabled && part.stroke.width > 0.0f &&
            part.stroke.decor != LineDecor::None) {
            // Re-derive the scaled style (same as the subpath loop used).
            StrokeStyle st = part.stroke;
            st.width *= avgScale; st.decorSpacing *= avgScale; st.decorSize *= avgScale;
            st.decorThickness *= avgScale; st.capTaper *= avgScale;
            for (float& d : st.dash) d *= avgScale;

            // 1) Flatten each subpath to a WORLD poly; record its two endpoints'
            //    junction ids (0 = free end).
            const int subs2 = SubpathCount(part);
            struct Strand { std::vector<Vec2> poly; uint32_t j0, j1; bool used=false; };
            std::vector<Strand> strands;
            for (int sp = 0; sp < subs2; ++sp) {
                bool cl=false;
                std::vector<Vec2> wp = OutlinePartSub(shape, part, sp, zoom, cl, pageOrigin);
                if (wp.size() < 2) continue;
                int nb=0,ne=(int)part.path.nodes.size(); part.path.subRange(sp, nb, ne);
                uint32_t j0 = (nb>=0&&nb<(int)part.path.nodes.size())? part.path.nodes[(size_t)nb].junctionId:0;
                uint32_t j1 = (ne-1>=0&&ne-1<(int)part.path.nodes.size())? part.path.nodes[(size_t)(ne-1)].junctionId:0;
                strands.push_back({ std::move(wp), j0, j1, false });
            }
            // 2) Greedily build traversal polys: start at a strand, then at each
            //    junction endpoint hop to the UNUSED strand whose direction is most
            //    aligned (straightest through), appending it (reversed if needed).
            auto dirAtEnd = [](const std::vector<Vec2>& p, bool atFront)->Vec2{
                if (p.size()<2) return {1,0};
                Vec2 d = atFront ? (p[0]-p[1]) : (p[p.size()-1]-p[p.size()-2]);
                float l=std::hypot(d.x,d.y); return l>1e-6f? Vec2{d.x/l,d.y/l}:Vec2{1,0};
            };
            std::vector<std::vector<Vec2>> traversals;
            for (size_t si=0; si<strands.size(); ++si) {
                if (strands[si].used) continue;
                strands[si].used = true;
                std::vector<Vec2> cur = strands[si].poly;
                uint32_t headJ = strands[si].j0, tailJ = strands[si].j1;
                // Extend the TAIL through junctions.
                bool grew = true;
                while (grew && tailJ != 0) {
                    grew = false;
                    Vec2 inDir = dirAtEnd(cur, /*atFront=*/false); // pointing outward at tail
                    int best=-1; bool bestRev=false; float bestDot=-2.0f;
                    for (size_t k=0;k<strands.size();++k){
                        if (strands[k].used) continue;
                        bool matchFront = (strands[k].j0==tailJ);
                        bool matchBack  = (strands[k].j1==tailJ);
                        if (!matchFront && !matchBack) continue;
                        // Candidate outgoing dir away from the junction.
                        Vec2 od = matchFront ? dirAtEnd(strands[k].poly,true)*-1.0f
                                             : dirAtEnd(strands[k].poly,false)*-1.0f;
                        float dt = inDir.x*od.x + inDir.y*od.y;   // straightest = max
                        if (dt>bestDot){ bestDot=dt; best=(int)k; bestRev=matchBack; }
                    }
                    if (best>=0){
                        strands[(size_t)best].used=true;
                        std::vector<Vec2> add = strands[(size_t)best].poly;
                        if (bestRev) std::reverse(add.begin(), add.end());
                        // Append (skip the shared junction point duplicate).
                        for (size_t q=1;q<add.size();++q) cur.push_back(add[q]);
                        tailJ = bestRev ? strands[(size_t)best].j0 : strands[(size_t)best].j1;
                        grew = true;
                    }
                }
                (void)headJ;
                traversals.push_back(std::move(cur));
            }
            // 3) Decorate each traversal poly once (continuous through the junction).
            Mesh& dt2 = partRibbonActive ? partRibbon : out;
            for (const std::vector<Vec2>& tp : traversals) {
                if (tp.size() < 2) continue;
                if (decorSink && decorSink->instances) {
                    StrokeDecorRails(tp, false, st, zoom, part.stroke.color, dt2);
                    StrokeDecorInstanced(tp, false, st, zoom, part.stroke.color,
                                         *decorSink->instances, *decorSink->batches, nullptr);
                } else {
                    StrokeDecor(tp, false, st, zoom, part.stroke.color, dt2, nullptr);
                }
            }
        }

        // Emit the merged junction-part ribbon as ONE StrokeRec (filled once → no
        // alpha doubling where the branches overlap at the shared vertex).
        if (partRibbonActive && !partRibbon.vertices.empty() &&
            sink && sink->strokeCover && sink->strokeRecs) {
            StrokeRec sr;
            sr.coverFirst = (uint32_t)sink->strokeCover->vertices.size();
            sr.coverCount = (uint32_t)partRibbon.vertices.size();
            sr.color = part.stroke.color;
            sr.bbMin = { 1e30f, 1e30f }; sr.bbMax = { -1e30f, -1e30f };
            for (const Vertex& v : partRibbon.vertices) {
                sr.bbMin.x = std::min(sr.bbMin.x, v.x);
                sr.bbMin.y = std::min(sr.bbMin.y, v.y);
                sr.bbMax.x = std::max(sr.bbMax.x, v.x);
                sr.bbMax.y = std::max(sr.bbMax.y, v.y);
                sink->strokeCover->vertices.push_back(v);
            }
            sink->strokeRecs->push_back(sr);
        }
    }
}

void Tessellator::AppendShape(const Shape& shape, Mesh& out, float zoom, Vec2 pageOrigin) {
    // Legacy path: patterns baked into `out` as triangles (no sink).
    AppendShapeImpl(shape, out, zoom, pageOrigin, /*sink=*/nullptr);
}

void Tessellator::AppendShapePatterned(const Shape& shape, Mesh& out, Mesh& cover,
                                       std::vector<PatternRec>& recs,
                                       std::vector<PatternInstance>& decorInst,
                                       std::vector<DecorBatch>& decorBatches,
                                       Mesh& strokeCover,
                                       std::vector<StrokeRec>& strokeRecs, float zoom) {
    PatternSink sink{ &cover, &recs, &strokeCover, &strokeRecs };
    DecorSink   dsink{ &decorInst, &decorBatches };
    AppendShapeImpl(shape, out, zoom, /*pageOrigin=*/Vec2{0, 0}, &sink, &dsink);
}

void Tessellator::RefreshDecorInstances(const Shape& s,
                                        std::vector<PatternInstance>& decorInst,
                                        std::vector<DecorBatch>& decorBatches) {
    // Re-run ONLY the instanced-decorator emission (re-walk the arc-length stations)
    // without touching the ribbon. Mirrors AppendShapeImpl's part/subpath loop +
    // StrokeStyled's run cutting so dashed / anchored / crossing-cut symbols match.
    decorInst.clear(); decorBatches.clear();
    if (!s.visible) return;
    const float avgScale = 0.5f *
        (std::fabs(s.transform.scale.x) + std::fabs(s.transform.scale.y));
    DecorSink dsink{ &decorInst, &decorBatches };
    for (const Part& part : s.parts) {
        if (!part.stroke.enabled || part.stroke.width <= 0.0f) continue;
        if (part.stroke.decor == LineDecor::None || !DecorIsGlyph(part.stroke.decor)) continue;
        StrokeStyle st = part.stroke;
        st.width *= avgScale; st.decorSpacing *= avgScale; st.decorSize *= avgScale;
        st.decorThickness *= avgScale;
        const int subs = SubpathCount(part);
        for (int sp = 0; sp < subs; ++sp) {
            bool closed = false;
            std::vector<Vec2> poly = OutlinePartSub(s, part, sp, 1.0f, closed, Vec2{0,0});
            if (poly.size() < 2) continue;
            // Mirror StrokeStyled: cut at crossing/bridge marks, decorate kept runs.
            std::vector<CutSpan> cuts; std::vector<DashAnchor> anchors;
            CollectPartCutsAnchors(s, part, sp, poly, closed, avgScale, Vec2{0,0}, cuts, anchors);
            std::vector<std::vector<Vec2>> runs;
            if (cuts.empty()) runs.push_back(poly);
            else { float total = PolyLength(poly, closed);
                   for (const CutSpan& k : KeptRuns(total, cuts)) {
                       std::vector<Vec2> r = ExtractRun(poly, closed, k.from, k.to);
                       if (r.size() >= 2) runs.push_back(std::move(r)); } }
            const bool runsClosed = cuts.empty() && closed;
            for (const auto& r : runs) {
                const std::vector<DashAnchor>* a =
                    (&r == &runs.front() && cuts.empty()) ? &anchors : nullptr;
                StrokeDecorInstanced(r, runsClosed, st, 1.0f, part.stroke.color,
                                     decorInst, decorBatches, a);
            }
        }
    }
}

bool Tessellator::RefreshPatternParams(const Shape& s, std::vector<PatternRec>& recs) {
    // Only safe when each patterned part has exactly ONE subpath — then rec[i]
    // maps 1:1 to the i-th procedural layer (no geometry-dependent subpath skipping
    // or empty-subpath culling to reproduce). Otherwise bail → full rebuild.
    const float avgScale = 0.5f *
        (std::fabs(s.transform.scale.x) + std::fabs(s.transform.scale.y));
    // Anchor is stable across a PARAM-only edit (a transform edit goes through the
    // full geom rebuild, not this path), so recompute center from the anchor rather
    // than preserving a (possibly bbox-derived) cached centre.
    const Vec2 objAnchor = WorldTransform(s, s.origin, Vec2{0, 0});
    size_t ri = 0;
    for (const Part& part : s.parts) {
        if (part.fillLayers.empty()) continue;
        if (SubpathCount(part) != 1) return false;   // multi-subpath → rebuild
        for (const FillLayer& fl : part.fillLayers) {
            if (!fl.enabled || !PatternIsProcedural(fl.pattern)) continue;
            if (ri >= recs.size()) return false;
            recs[ri].params = FillLayerToParamsNoCenter(fl, avgScale, s.transform.rotate);
            recs[ri].params.center = (fl.anchor == FillAnchor::DocumentOrigin)
                                   ? Vec2{0, 0} : objAnchor;
            ++ri;
        }
    }
    return ri == recs.size();   // counts must match exactly
}

// The page's white backdrop, drawn at `origin` (its display position — equals
// ab.pos unless a per-viewport layout relocates it).
static void BackdropAt(Vec2 origin, Vec2 size, Mesh& out) {
    PushTri(out, {origin.x, origin.y},
                 {origin.x + size.x, origin.y + size.y},
                 {origin.x + size.x, origin.y}, Color{1, 1, 1, 1});
    PushTri(out, {origin.x, origin.y},
                 {origin.x, origin.y + size.y},
                 {origin.x + size.x, origin.y + size.y}, Color{1, 1, 1, 1});
}
void Tessellator::BuildArtboardBackdrop(const Artboard& ab, Mesh& out) {
    BackdropAt(ab.pos, ab.size, out);
}

// Just this page's shapes (page-relative geometry → offset by the page origin).
void Tessellator::BuildArtboardShapes(const Artboard& ab, Mesh& out, float zoom) {
    for (const Shape& s : ab.shapes)
        AppendShape(s, out, zoom, ab.pos);
}

void Tessellator::BuildArtboard(const Artboard& ab, Mesh& out, float zoom) {
    // The artboard page itself is document content too (Vulkan-drawn): a white
    // rectangle filling the page bounds, beneath the shapes.
    BuildArtboardBackdrop(ab, out);
    BuildArtboardShapes(ab, out, zoom);
}

// Build the WHOLE document with correct inter-page z-order: an object never
// paints over a FOREIGN page's white. Painter's order:
//   1) every page's white backdrop (the floor),
//   2) for each page i: its shapes, THEN every OTHER page's white on top.
// So page i's shapes sit above page i's white but below every other page's
// white; overflow into the empty VOID stays visible (nothing covers it). Pages
// with clipContents are scissored to their own bounds by the renderer.
void Tessellator::BuildDocument(const Document& doc, Mesh& out, float zoom) {
    // Simple painter order (used where per-page scissor isn't available, e.g.
    // one-page thumbnails): each page's backdrop+shapes contiguous. Cross-page
    // overlap is handled by the segmented builder + scissor in the live view.
    for (const Artboard& ab : doc.artboards) {
        BuildArtboardBackdrop(ab, out);
        BuildArtboardShapes(ab, out, zoom);
    }
}

// ── Quality + hashing + cache ─────────────────────────────────────────────────
void  Tessellator::SetQuality(float q) { gQualityPerUnit = std::clamp(q, 0.02f, 4.0f); }
float Tessellator::GetQuality() { return gQualityPerUnit; }
void  Tessellator::SetDetailScale(float s) { gDetailScale = std::clamp(s, 0.02f, 4096.0f); }
float Tessellator::GetDetailScale() { return gDetailScale; }

// The detail bucket for a given on-screen zoom (px per doc-unit). Quantised to
// coarse ~2× buckets so the cache re-tessellates only on a real detail step (not
// every zoom frame). NOT clamped at the top: the flattener drives subdivision from
// the screen chord error (bend × this scale), so capping the scale would cap the
// error driver and leave visible facets past that zoom — the bug this replaces. The
// triangle cost at deep zoom is bounded instead by the per-view cull / visible-region
// clip, not by coarsening the curve. Never coarsens below 1× (authored detail).
// Single source of truth shared by BuildDocumentSegmented and the per-view build
// signature (via DetailBucketIndex) so the two stay in lockstep.
int Tessellator::DetailBucketIndex(float zoom) {
    float z = std::max(1.0f, zoom);
    int bucket = (int)std::floor(std::log2(z));               // 0,1,2,… per 2×
    return std::clamp(bucket, 0, kMaxDetailBucket);
}
float Tessellator::DetailScaleForZoom(float zoom) {
    return std::exp2((float)DetailBucketIndex(zoom));
}

// FNV-1a 64 over the shape's visual-defining fields. Any geometry/paint/transform
// edit flips it; pan/zoom (camera) is NOT part of it, so it's stable across them.
// pageOrigin is DELIBERATELY excluded: the cached mesh is baked page-LOCAL (origin
// {0,0}) and the page offset is added cheaply on append, so the SAME cache entry
// is shared by every viewport regardless of its per-view page layout (that was
// the multi-viewport thrash: different pageOrigins kept invalidating each other).
uint64_t Tessellator::HashShape(const Shape& s, Vec2 /*pageOrigin*/) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto f = [&](float v) { mix(&v, sizeof v); };
    if (!s.visible) { uint8_t z = 0; mix(&z, 1); return h; }
    f(s.origin.x); f(s.origin.y);
    f(s.transform.translate.x); f(s.transform.translate.y);
    f(s.transform.rotate); f(s.transform.scale.x); f(s.transform.scale.y);
    // NB: gDetailScale / gQualityPerUnit are DELIBERATELY excluded. They are per-view
    // detail (zoom/quality) and mixing them here made the same shape hash differently
    // in two viewports at different zooms → each frame each view rewrote the other's
    // cache entry (multi-viewport thrash). The detail axis now lives in the cache KEY
    // (shapeId, detailBucket) and the build signature, not in the per-shape hash.
    for (const Part& p : s.parts) {
        uint8_t meta[4] = { (uint8_t)p.kind, (uint8_t)p.type, (uint8_t)p.spline, (uint8_t)p.orderU };
        mix(meta, 4);
        uint8_t nopt[3] = { (uint8_t)(p.nurbsEndpoint?1:0), (uint8_t)(p.nurbsBezier?1:0),
                            (uint8_t)(p.openFillStraight?1:0) };
        mix(nopt, 3);   // NURBS knot options + open-fill close mode affect tessellation
        f(p.pos.x); f(p.pos.y); f(p.size.x); f(p.size.y);
        uint8_t fe = p.fill.enabled, se = p.stroke.enabled; mix(&fe,1); mix(&se,1);
        f(p.fill.color.r); f(p.fill.color.g); f(p.fill.color.b); f(p.fill.color.a);
        f(p.stroke.color.r); f(p.stroke.color.g); f(p.stroke.color.b); f(p.stroke.color.a);
        f(p.stroke.width); f(p.stroke.miterLimit); f(p.stroke.capTaper);
        uint8_t ss[7] = { (uint8_t)p.stroke.cap, (uint8_t)p.stroke.join,
                          (uint8_t)p.stroke.align, (uint8_t)p.stroke.decor,
                          (uint8_t)p.stroke.decorCentered,
                          (uint8_t)p.stroke.decorEdge, (uint8_t)p.stroke.decorSide };
        mix(ss, 7);
        { uint64_t sid = p.stroke.decorSourceShapeId; mix(&sid, sizeof sid); }
        f(p.stroke.decorSpacing); f(p.stroke.decorSize);
        f(p.stroke.decorAngleDeg); f(p.stroke.decorThickness);
        for (float d : p.stroke.dash) f(d);
        for (const Node& n : p.path.nodes) {
            f(n.pos.x); f(n.pos.y); f(n.hIn.x); f(n.hIn.y); f(n.hOut.x); f(n.hOut.y);
            uint8_t nf = (n.hasIn?1:0)|(n.hasOut?2:0)|((uint8_t)n.mode<<2); mix(&nf,1);
            f(n.weight);   // rational NURBS weight
            { uint32_t j = n.junctionId; mix(&j, sizeof j); }   // multi-path join
        }
        uint8_t cl = p.path.closed; mix(&cl, 1);
        for (int ss : p.path.subStart) { int32_t v = ss; mix(&v, sizeof v); }
        // Fill layers (so dragging a pattern offset / editing a screen invalidates
        // the cache and re-tessellates the surface).
        for (const FillLayer& fl : p.fillLayers) {
            uint8_t m2[4] = { (uint8_t)fl.pattern, (uint8_t)(fl.enabled?1:0),
                              (uint8_t)fl.clip, (uint8_t)fl.anchor }; mix(m2, 4);
            f(fl.color.r); f(fl.color.g); f(fl.color.b); f(fl.color.a);
            f(fl.opacity); f(fl.spacing); f(fl.size); f(fl.angleDeg);
            f(fl.offset.x); f(fl.offset.y);
            f(fl.dash); f(fl.dashGap); { uint8_t ap = fl.altPhase?1:0; mix(&ap,1); }
            uint32_t sd = fl.seed; mix(&sd, sizeof sd);
        }
        // Manual line marks (slope ticks / crossing / bridge / pinned pylons) —
        // editing one re-tessellates the part.
        for (const LineMark& m : p.marks) {
            uint8_t mm[4] = { (uint8_t)m.kind, (uint8_t)(m.side >= 0 ? 1 : 0),
                              (uint8_t)(m.outsideMeasure ? 1 : 0),
                              (uint8_t)(m.square ? 1 : 0) }; mix(mm, 4);
            int32_t sub = m.sub; mix(&sub, sizeof sub);
            int32_t na = m.nodeAnchor; mix(&na, sizeof na);   // DashAnchor pin
            f(m.t); f(m.gap); f(m.size); f(m.thickness);
        }
    }
    return h;
}

// Hash of ONLY what changes the baked tessellation: geometry/transform, fill &
// stroke (the stroke drives the inner/outer cut offset), marks, and per fill layer
// its pattern KIND + clip edge + enabled (which decide the cover triangulation) —
// plus a Solid layer's colour/opacity (Solid is baked into `verts`). The procedural
// motif params (spacing/size/angle/offset/seed/colour/dash of non-Solid layers) are
// EXCLUDED, so editing them reuses the cached cover and only refreshes params.
uint64_t Tessellator::GeomHashShape(const Shape& s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto f = [&](float v) { mix(&v, sizeof v); };
    if (!s.visible) { uint8_t z = 0; mix(&z, 1); return h; }
    f(s.origin.x); f(s.origin.y);
    f(s.transform.translate.x); f(s.transform.translate.y);
    f(s.transform.rotate); f(s.transform.scale.x); f(s.transform.scale.y);
    // gDetailScale / gQualityPerUnit deliberately excluded — detail is keyed by the
    // cache key (shapeId, detailBucket), not the hash (see HashShape for why).
    for (const Part& p : s.parts) {
        uint8_t meta[4] = { (uint8_t)p.kind, (uint8_t)p.type, (uint8_t)p.spline, (uint8_t)p.orderU };
        mix(meta, 4);
        uint8_t nopt[2] = { (uint8_t)(p.nurbsEndpoint?1:0), (uint8_t)(p.nurbsBezier?1:0) };
        mix(nopt, 2);
        f(p.pos.x); f(p.pos.y); f(p.size.x); f(p.size.y);
        uint8_t fe = p.fill.enabled, se = p.stroke.enabled; mix(&fe,1); mix(&se,1);
        f(p.fill.color.r); f(p.fill.color.g); f(p.fill.color.b); f(p.fill.color.a);
        f(p.stroke.color.r); f(p.stroke.color.g); f(p.stroke.color.b); f(p.stroke.color.a);
        f(p.stroke.width); f(p.stroke.miterLimit); f(p.stroke.capTaper);
        // Periodic-glyph decor params (spacing/angle/edge/side/source) are EXCLUDED:
        // they drive only the instance list, not the baked ribbon/rails. Keep what
        // the baked rails depend on: decor kind + size + thickness + centred.
        uint8_t ss[5] = { (uint8_t)p.stroke.cap, (uint8_t)p.stroke.join,
                          (uint8_t)p.stroke.align, (uint8_t)p.stroke.decor,
                          (uint8_t)p.stroke.decorCentered };
        mix(ss, 5);
        f(p.stroke.decorSize); f(p.stroke.decorThickness);
        for (float d : p.stroke.dash) f(d);
        for (const Node& n : p.path.nodes) {
            f(n.pos.x); f(n.pos.y); f(n.hIn.x); f(n.hIn.y); f(n.hOut.x); f(n.hOut.y);
            uint8_t nf = (n.hasIn?1:0)|(n.hasOut?2:0)|((uint8_t)n.mode<<2); mix(&nf,1);
            f(n.weight);
            { uint32_t j = n.junctionId; mix(&j, sizeof j); }   // multi-path join
        }
        uint8_t cl = p.path.closed; mix(&cl, 1);
        for (int ssv : p.path.subStart) { int32_t v = ssv; mix(&v, sizeof v); }
        for (const FillLayer& fl : p.fillLayers) {
            uint8_t m2[4] = { (uint8_t)fl.pattern, (uint8_t)(fl.enabled?1:0),
                              (uint8_t)fl.clip, (uint8_t)fl.anchor }; mix(m2, 4);
            if (fl.pattern == FillPattern::Solid) {   // Solid is baked into verts
                f(fl.color.r); f(fl.color.g); f(fl.color.b); f(fl.color.a); f(fl.opacity);
            }
        }
        for (const LineMark& m : p.marks) {
            uint8_t mm[4] = { (uint8_t)m.kind, (uint8_t)(m.side >= 0 ? 1 : 0),
                              (uint8_t)(m.outsideMeasure ? 1 : 0),
                              (uint8_t)(m.square ? 1 : 0) }; mix(mm, 4);
            int32_t sub = m.sub; mix(&sub, sizeof sub);
            int32_t na = m.nodeAnchor; mix(&na, sizeof na);
            f(m.t); f(m.gap); f(m.size); f(m.thickness);
        }
    }
    return h;
}

void Tessellator::Cache::Evict() {
    // Keep entries touched within a small grace window rather than only THIS exact
    // frame. The per-view renderer SKIPS BuildDocumentSegmented entirely on a
    // static/pan/zoom frame (content signature unchanged), so the cache is not
    // touched on those frames; a strict "!= frame" purge would then drop every
    // baked mesh and make the next real edit re-tessellate the WHOLE document. The
    // grace window lets the cache survive idle frames; genuinely deleted/scrolled
    // shapes are still dropped once they fall outside it.
    constexpr uint64_t kGrace = 120;   // ~2 s at 60 fps
    for (auto it = byId.begin(); it != byId.end(); ) {
        if (frame - it->second.lastUsedFrame > kGrace) it = byId.erase(it);
        else ++it;
    }
    // Safety LRU cap: the key is (shapeId, detailBucket), so a document zoomed across
    // many buckets could otherwise accumulate entries. Bound the map by dropping the
    // oldest-touched entries first if it grows past the cap.
    constexpr size_t kMaxEntries = 8192;
    if (byId.size() > kMaxEntries) {
        std::vector<std::pair<uint64_t, uint64_t>> ages;   // (lastUsedFrame, key)
        ages.reserve(byId.size());
        for (const auto& kv : byId) ages.push_back({ kv.second.lastUsedFrame, kv.first });
        size_t drop = byId.size() - kMaxEntries;
        std::nth_element(ages.begin(), ages.begin() + (long)drop, ages.end());
        for (size_t i = 0; i < drop; ++i) byId.erase(ages[i].second);
    }
}

// Per-view instanced-pattern output streams + the owning page segment, threaded
// through AppendShapeCachedCulled so a shape's cover polygon can be appended
// (page-shifted) and its SurfaceDraws resolved. null → legacy (patterns baked).
struct PatternOut {
    Mesh*                 cover   = nullptr;   // per-view cover-vertex stream
    std::vector<PatternInstance>* decor = nullptr;  // per-view decor-instance stream
    Tessellator::ObjDraw* obj     = nullptr;   // this object's pattern/decor sink
    uint8_t*              nextRef = nullptr;   // monotonic stencil reference
    // Transparent-stroke coverage goes into the SAME per-view cover stream (the
    // renderer's stencil-scratch buffer); non-null gates the stroke resolve.
    Mesh*                 strokeCover = nullptr;
};

// The cache map key packs the detail bucket into the low 8 bits of the shape id, so
// the SAME shape tessellated at two different on-screen zooms lives in two distinct
// entries (and viewports at the same bucket share one). Shape ids are well below
// 2^56, so the shift is safe; bucket is clamped to [0, kMaxDetailBucket].
static inline uint64_t MakeCacheKey(uint64_t shapeId, int bucketIdx) {
    return (shapeId << 8) | (uint64_t)(uint8_t)std::clamp(bucketIdx, 0,
                                       Tessellator::kMaxDetailBucket);
}

// Append ONE shape's world-space triangles to `out` — from the cache if its hash
// is unchanged, else freshly tessellated (and cached). Culls if its world bounds
// miss `cull`. `bucketIdx` is the detail bucket for this view (keys the cache so
// views at different zooms don't fight). When `pat` is set, the shape's fill
// patterns are appended to the per-view instance/mask streams (page-shifted) as
// SurfaceDraws instead of baked into `out`. Updates cache stats. Returns the # base
// vertices appended.
static uint32_t AppendShapeCachedCulled(const Shape& s, Mesh& out, Vec2 pageOrigin,
                                        Tessellator::Cache* cache, int bucketIdx,
                                        const Tessellator::CullRect* cull,
                                        PatternOut* pat = nullptr) {
    if (!s.visible) return 0;

    // 1) CULL FIRST (cheap) — a quick outline-only bounds pass, no triangle work.
    //    Bounds are computed page-LOCAL then shifted by pageOrigin to match the
    //    world cull rect. Off-screen shapes are skipped before any tessellation.
    if (cull) {
        Vec2 mn, mx;
        if (Tessellator::WorldBounds(s, 1.0f, mn, mx, Vec2{0, 0})) {
            mn.x += pageOrigin.x; mn.y += pageOrigin.y;
            mx.x += pageOrigin.x; mx.y += pageOrigin.y;
            if (!cull->Hit(mn, mx)) { if (cache) cache->culledShapes++; return 0; }
        }
    }

    const bool wantPatterns = (pat && pat->cover);

    // 2) From the cache when the content hash is unchanged; else (re)tessellate —
    //    always at page-LOCAL origin {0,0}, so the entry is shared by every
    //    viewport (the page offset is added on append below).
    Tessellator::CachedShape* entry = nullptr;
    if (cache) {
        uint64_t hash     = Tessellator::HashShape(s, Vec2{0, 0});
        uint64_t geomHash = Tessellator::GeomHashShape(s);
        entry = &cache->byId[MakeCacheKey(s.id, bucketIdx)];
        // FULL rebuild (re-flatten + ear-clip) only when the TESSELLATION changed:
        // geometry/clip/stroke/marks/Solid colour, the verts are empty, or we now
        // need the procedural data but the entry was a legacy bake.
        const bool needPatBuild = wantPatterns && !entry->patternedBuild;
        bool wantFullBuild = (entry->geomHash != geomHash || entry->verts.empty()
                              || needPatBuild);
        // Lot 4 budget: if a rebuild is wanted but the per-frame budget is spent AND
        // this entry already has a prior mesh, DEFER it — draw the stale mesh this
        // frame, count it, and let the view re-enter next frame to finish it. A shape
        // with no prior mesh (first sighting) always builds; minRebuilds guarantees
        // forward progress so a single huge shape can't starve everyone forever.
        if (wantFullBuild && !entry->verts.empty()
            && cache->builtThisBuild >= cache->minRebuilds
            && entry->verts.size() > cache->rebuildVertBudget) {
            wantFullBuild = false;
            cache->deferredShapes++;
        }
        if (wantFullBuild) {
            Mesh m, cover, strokeCover;
            entry->recs.clear(); entry->coverVerts.clear();
            entry->decorInstances.clear(); entry->decorBatches.clear();
            entry->strokeRecs.clear(); entry->strokeCoverVerts.clear();
            if (wantPatterns) {
                Tessellator::AppendShapePatterned(s, m, cover, entry->recs,
                    entry->decorInstances, entry->decorBatches,
                    strokeCover, entry->strokeRecs, 1.0f);
                entry->patternedBuild = true;
            } else {
                Tessellator::AppendShape(s, m, 1.0f, Vec2{0, 0});
                entry->patternedBuild = false;
            }
            entry->verts            = std::move(m.vertices);
            entry->coverVerts       = std::move(cover.vertices);
            entry->strokeCoverVerts = std::move(strokeCover.vertices);
            entry->hash       = hash;
            entry->geomHash   = geomHash;
            cache->builtShapes++;
            cache->builtThisBuild++;
            // Charge this rebuild against the per-frame budget (saturating subtract).
            size_t cost = entry->verts.size();
            cache->rebuildVertBudget = (cache->rebuildVertBudget > cost)
                                           ? cache->rebuildVertBudget - cost : 0;
        } else if (entry->hash != hash) {
            // PARAM-only edit (geometry unchanged): refresh motif params + decor
            // instances without re-tessellating the ribbon. Fill recs fall back to a
            // full rebuild if they can't be matched; decor always refreshes cheaply.
            bool ok = wantPatterns ? Tessellator::RefreshPatternParams(s, entry->recs)
                                   : false;
            if (!ok && wantPatterns) {
                Mesh m, cover, strokeCover;
                entry->recs.clear(); entry->coverVerts.clear();
                entry->decorInstances.clear(); entry->decorBatches.clear();
                entry->strokeRecs.clear(); entry->strokeCoverVerts.clear();
                Tessellator::AppendShapePatterned(s, m, cover, entry->recs,
                    entry->decorInstances, entry->decorBatches,
                    strokeCover, entry->strokeRecs, 1.0f);
                entry->patternedBuild = true;
                entry->verts            = std::move(m.vertices);
                entry->coverVerts       = std::move(cover.vertices);
                entry->strokeCoverVerts = std::move(strokeCover.vertices);
            } else if (wantPatterns) {
                // recs refreshed in place; decor regenerated cheaply (no re-flatten
                // of the ribbon — verts reused).
                Tessellator::RefreshDecorInstances(s, entry->decorInstances,
                                                   entry->decorBatches);
            }
            entry->hash = hash;
            cache->builtShapes++;
        } else {
            cache->cachedShapes++;
        }
        entry->lastUsedFrame = cache->frame;
    }

    const bool shift = (pageOrigin.x != 0.0f || pageOrigin.y != 0.0f);
    auto shiftV = [&](const Vertex& v) {
        return shift ? Vertex{ v.x + pageOrigin.x, v.y + pageOrigin.y, v.r, v.g, v.b, v.a }
                     : v;
    };

    uint32_t before = (uint32_t)out.vertices.size();
    if (entry) {
        out.vertices.reserve(out.vertices.size() + entry->verts.size());
        for (const Vertex& v : entry->verts) out.vertices.push_back(shiftV(v));

        // Resolve this shape's pattern recs into per-view SurfaceDraws. Each rec's
        // cover-polygon range is copied (page-shifted) into the per-view cover
        // stream ONCE per distinct range (several layers of a surface share it);
        // the bbox + page-shifted params follow. The fragment shader paints the
        // motif at draw time, clipped to the cover/stencil.
        if (wantPatterns && !entry->recs.empty()) {
            // Map a shape-local cover range start → its per-view start (so layers
            // sharing a cover polygon don't duplicate it).
            uint32_t lastSrc = 0xFFFFFFFFu, lastDst = 0; Vec2 lastMin{}, lastMax{};
            for (const PatternRec& rc : entry->recs) {
                uint32_t dstFirst; Vec2 bbMin, bbMax;
                if (rc.coverFirst == lastSrc) {
                    dstFirst = lastDst; bbMin = lastMin; bbMax = lastMax;
                } else {
                    dstFirst = (uint32_t)pat->cover->vertices.size();
                    bbMin = { 1e30f, 1e30f }; bbMax = { -1e30f, -1e30f };
                    for (uint32_t k = 0; k < rc.coverCount; ++k) {
                        Vertex sv = shiftV(entry->coverVerts[rc.coverFirst + k]);
                        bbMin.x = std::min(bbMin.x, sv.x); bbMin.y = std::min(bbMin.y, sv.y);
                        bbMax.x = std::max(bbMax.x, sv.x); bbMax.y = std::max(bbMax.y, sv.y);
                        pat->cover->vertices.push_back(sv);
                    }
                    lastSrc = rc.coverFirst; lastDst = dstFirst;
                    lastMin = bbMin; lastMax = bbMax;
                }
                Tessellator::SurfaceDraw d;
                d.coverFirstVertex = dstFirst;
                d.coverVertexCount = rc.coverCount;
                d.params = rc.params;
                d.params.center.x += pageOrigin.x; d.params.center.y += pageOrigin.y;
                d.bbMin = bbMin; d.bbMax = bbMax;
                d.stencilRef = *pat->nextRef;
                *pat->nextRef = (uint8_t)(*pat->nextRef >= 255 ? 1 : *pat->nextRef + 1);
                pat->obj->patterns.push_back(d);
            }
        }

        // Resolve transparent-stroke recs into per-view StrokeDraws. The ribbon
        // coverage is copied (page-shifted) into the SAME per-view cover stream as the
        // fill patterns (the renderer's stencil-scratch buffer); then 6 coloured verts
        // (the bbox quad) are appended for the single colour pass. The renderer writes
        // the ribbon to the stencil (REPLACE ref) and fills the quad ONCE (EQUAL ref),
        // so the overlapping ribbon never doubles the alpha.
        if (wantPatterns && pat->strokeCover && !entry->strokeRecs.empty()) {
            for (const StrokeRec& rc : entry->strokeRecs) {
                Tessellator::StrokeDraw d;
                d.coverFirstVertex = (uint32_t)pat->cover->vertices.size();
                d.coverVertexCount = rc.coverCount;
                Vec2 bbMin{ 1e30f, 1e30f }, bbMax{ -1e30f, -1e30f };
                for (uint32_t k = 0; k < rc.coverCount; ++k) {
                    Vertex sv = shiftV(entry->strokeCoverVerts[rc.coverFirst + k]);
                    bbMin.x = std::min(bbMin.x, sv.x); bbMin.y = std::min(bbMin.y, sv.y);
                    bbMax.x = std::max(bbMax.x, sv.x); bbMax.y = std::max(bbMax.y, sv.y);
                    pat->cover->vertices.push_back(sv);
                }
                d.bbMin = bbMin; d.bbMax = bbMax;
                // The bbox quad (2 tris = 6 verts) carries the stroke colour for the
                // single coloured fill pass.
                d.quadFirstVertex = (uint32_t)pat->cover->vertices.size();
                const Color& cc = rc.color;
                Vertex q00{ bbMin.x, bbMin.y, cc.r, cc.g, cc.b, cc.a };
                Vertex q10{ bbMax.x, bbMin.y, cc.r, cc.g, cc.b, cc.a };
                Vertex q11{ bbMax.x, bbMax.y, cc.r, cc.g, cc.b, cc.a };
                Vertex q01{ bbMin.x, bbMax.y, cc.r, cc.g, cc.b, cc.a };
                pat->cover->vertices.push_back(q00);
                pat->cover->vertices.push_back(q10);
                pat->cover->vertices.push_back(q11);
                pat->cover->vertices.push_back(q00);
                pat->cover->vertices.push_back(q11);
                pat->cover->vertices.push_back(q01);
                d.stencilRef = *pat->nextRef;
                *pat->nextRef = (uint8_t)(*pat->nextRef >= 255 ? 1 : *pat->nextRef + 1);
                pat->obj->strokes.push_back(d);
            }
        }

        // Resolve decorator batches into per-view DecorDraws: copy the page-local
        // instances (page-shifted) into the view's decor stream + a DecorDraw per batch.
        if (wantPatterns && pat->decor && !entry->decorBatches.empty()) {
            for (const DecorBatch& b : entry->decorBatches) {
                Tessellator::DecorDraw dd;
                dd.kind = b.kind;
                dd.firstInstance = (uint32_t)pat->decor->size();
                dd.instanceCount = b.instanceCount;
                for (uint32_t k = 0; k < b.instanceCount; ++k) {
                    PatternInstance pi = entry->decorInstances[b.firstInstance + k];
                    pi.cx += pageOrigin.x; pi.cy += pageOrigin.y;
                    pat->decor->push_back(pi);
                }
                pat->obj->decor.push_back(dd);
            }
        }
    } else {
        // No cache: bake everything (patterns as triangles) — keeps non-cached
        // callers (none in practice) correct.
        Tessellator::AppendShape(s, out, 1.0f, pageOrigin);
    }
    if (cache) cache->drawnShapes++;
    return (uint32_t)out.vertices.size() - before;
}

std::vector<Tessellator::PageSeg>
Tessellator::BuildDocumentSegmented(const Document& doc, Mesh& out, float zoom,
                                    const std::vector<PagePlacement>* placements,
                                    bool includeLoose, Cache* cache,
                                    const CullRect* cull,
                                    Mesh* outCover,
                                    std::vector<PatternInstance>* outDecor) {
    // Detail follows the on-screen magnification so curves stay smooth as you zoom,
    // QUANTISED to coarse ~2× buckets so a same-bucket zoom is free. gDetailScale is
    // the flattening parameter (read by the flatteners during THIS build); bucketIdx
    // is the cache-key detail axis — distinct entries per bucket so views at different
    // zooms don't fight (the multi-viewport thrash fix). gDetailScale is NOT a cache
    // identity any more (it was removed from the per-shape hashes).
    const int bucketIdx = DetailBucketIndex(zoom);
    gDetailScale = DetailScaleForZoom(zoom);
    if (cache) { cache->builtShapes = cache->cachedShapes =
                 cache->culledShapes = cache->drawnShapes = 0; }
    // Procedural patterns + instanced decor are emitted to the view streams only
    // when given; otherwise both bake into `out` (legacy thumbnail/glyph path).
    const bool procedural = (outCover != nullptr);
    uint8_t nextRef = 1;   // monotonic per-surface stencil reference (cleared to 0)
    std::vector<PageSeg> segs;
    segs.reserve(doc.artboards.size());
    for (size_t i = 0; i < doc.artboards.size(); ++i) {
        const Artboard& ab = doc.artboards[i];
        Vec2 origin = ab.pos;
        bool visible = true;
        if (placements && i < placements->size()) {
            origin  = (*placements)[i].origin;
            visible = (*placements)[i].visible;
        }
        if (!visible) continue;
        PageSeg seg;
        seg.min  = origin;
        seg.size = ab.size;
        // The page white backdrop, FIRST (under everything, no patterns).
        seg.backdropFirst = (uint32_t)out.vertices.size();
        BackdropAt(origin, ab.size, out);
        seg.backdropCount = (uint32_t)out.vertices.size() - seg.backdropFirst;
        // Each shape becomes one ObjDraw: its base range + its own patterns + decor.
        for (const Shape& s : ab.shapes) {
            ObjDraw obj;
            uint32_t first = (uint32_t)out.vertices.size();
            PatternOut po{ outCover, outDecor, &obj, &nextRef, outCover };
            uint32_t cnt = AppendShapeCachedCulled(s, out, origin, cache, bucketIdx,
                                                   cull, procedural ? &po : nullptr);
            obj.baseFirst = first; obj.baseCount = cnt;
            if (obj.baseCount > 0 || !obj.patterns.empty() || !obj.decor.empty()
                || !obj.strokes.empty())
                seg.objects.push_back(std::move(obj));
        }
        segs.push_back(std::move(seg));
    }
    if (includeLoose && !doc.looseShapes.empty()) {
        PageSeg seg;
        seg.fullScissor = true;   // loose objects: unclipped, no backdrop
        for (const Shape& s : doc.looseShapes) {
            ObjDraw obj;
            uint32_t first = (uint32_t)out.vertices.size();
            PatternOut po{ outCover, outDecor, &obj, &nextRef, outCover };
            uint32_t cnt = AppendShapeCachedCulled(s, out, Vec2{0, 0}, cache, bucketIdx,
                                                   cull, procedural ? &po : nullptr);
            obj.baseFirst = first; obj.baseCount = cnt;
            if (obj.baseCount > 0 || !obj.patterns.empty() || !obj.decor.empty()
                || !obj.strokes.empty())
                seg.objects.push_back(std::move(obj));
        }
        if (!seg.objects.empty()) segs.push_back(std::move(seg));
    }
    return segs;
}

} // namespace Renderer

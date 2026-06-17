#include "IofGlyph.h"
#include <cmath>
#include <utility>
#include <vector>

namespace App::Modules::IofMapping {

using Renderer::Shape;
using Renderer::Part;
using Renderer::Node;
using Renderer::Vec2;
using Renderer::Color;
using K  = Renderer::ShapeKind;
using PT = Renderer::PartType;
using ST = Renderer::SplineType;
using HM = Renderer::HandleMode;
using Cap   = Renderer::LineCap;
using Joi   = Renderer::LineJoin;
using Algn  = Renderer::StrokeAlign;
using Decor = Renderer::LineDecor;

namespace {

// Default extents (mm @ 1:15 000) for catalogue clicks. A line/area is created at
// a sensible default length/size; the user then edits the geometry. Areas use a
// SMALL RECTANGULAR footprint (wider than tall) so the surface + its preview read
// as a sample swatch, not a big square.
constexpr float kLineLen = 16.0f;   // default styled-line length
constexpr float kAreaW   = 12.0f;   // default area HALF-width  (rect, mm)
constexpr float kAreaH   = 8.0f;    // default area HALF-height (rect, mm)

Color RGB(IofRgb c, float a = 1.0f) { return Color{ c.r, c.g, c.b, a }; }

// ── Geometry coordinate convention ───────────────────────────────────────────
// The document is Y-DOWN (screen-like). "Up" (toward map north) is −Y. ISOM
// point symbols are described with their opening/apex direction; we honour that
// with −Y = up. A V "opening up" (e.g. a pit) therefore has its two top ends at
// y = −h and its apex at y = +h0 (lower on screen, i.e. the tip points down/into
// the hole) — matching the printed symbol where the V opens upward.

// ── Mesh part builders (geometry in local mm, centred near origin) ───────────
Part FilledDot(Vec2 c, float r, Color col) {
    Part p; p.kind = K::Ellipse; p.type = PT::Mesh;
    p.pos = { c.x - r, c.y - r }; p.size = { 2 * r, 2 * r };
    p.fill.enabled = true;  p.fill.color = col;
    p.stroke.enabled = false;
    return p;
}
Part Ring(Vec2 c, float r, float w, Color col) {
    Part p; p.kind = K::Ellipse; p.type = PT::Mesh;
    p.pos = { c.x - r, c.y - r }; p.size = { 2 * r, 2 * r };
    p.fill.enabled = false;
    p.stroke.enabled = true; p.stroke.width = w; p.stroke.color = col;
    p.stroke.cap = Cap::Butt; p.stroke.join = Joi::Miter; p.stroke.align = Algn::Center;
    return p;
}
Part FilledRect(Vec2 mn, Vec2 sz, Color col) {
    Part p; p.kind = K::Rectangle; p.type = PT::Mesh;
    p.pos = mn; p.size = sz;
    p.fill.enabled = true; p.fill.color = col; p.stroke.enabled = false;
    return p;
}

using FP = Renderer::FillPattern;
using FL = Renderer::FillLayer;

// A SURFACE part: a closed rectangle (the default area footprint) with NO base
// fill, carrying a stack of pattern fill layers. The patterns are generated
// infinitely and clipped to the contour by the renderer (movable/combinable per
// ISOM §2.11.4). `baseFill` (if a>0) is the flat background screen.
Part Surface(Vec2 mn, Vec2 sz, Color baseFill = {0,0,0,0}) {
    Part p; p.kind = K::Rectangle; p.type = PT::Mesh;
    p.pos = mn; p.size = sz;
    p.fill.enabled = baseFill.a > 0.001f; p.fill.color = baseFill;
    p.stroke.enabled = false;
    return p;
}
// Add a pattern layer to a surface part.
FL& AddLayer(Part& p, FP pat, Color col, float spacing, float size,
             float angleDeg = 0.0f, float opacity = 1.0f) {
    FL fl; fl.pattern = pat; fl.color = col; fl.spacing = spacing; fl.size = size;
    fl.angleDeg = angleDeg; fl.opacity = opacity;
    fl.anchor = Renderer::FillAnchor::DocumentOrigin;   // ISOM screens: doc-anchored
    p.fillLayers.push_back(fl);
    return p.fillLayers.back();
}
Part RectOutline(Vec2 mn, Vec2 sz, float w, Color col, bool fill, Color fillc) {
    Part p; p.kind = K::Rectangle; p.type = PT::Mesh;
    p.pos = mn; p.size = sz;
    p.fill.enabled = fill; p.fill.color = fillc;
    p.stroke.enabled = true; p.stroke.width = w; p.stroke.color = col;
    p.stroke.cap = Cap::Butt; p.stroke.join = Joi::Miter; p.stroke.align = Algn::Center;
    return p;
}
// An EDITABLE poly stroke (the building block for line symbols). Butt cap / miter
// join / center align by default (the ISOM line convention). Returns by value;
// the caller styles `.stroke` (dash, decor) further.
Part Stroke(const std::vector<Vec2>& pts, float w, Color col, bool closed = false) {
    Part p; p.kind = K::Curve; p.type = PT::Curve; p.spline = ST::Poly;
    for (Vec2 v : pts) { Node n(v); n.mode = HM::Vector; p.path.nodes.push_back(n); }
    p.path.closed = closed;
    p.fill.enabled = false;
    p.stroke.enabled = true; p.stroke.width = w; p.stroke.color = col;
    p.stroke.cap = Cap::Butt; p.stroke.join = Joi::Miter; p.stroke.align = Algn::Center;
    return p;
}
// Filled straight polygon (Poly), e.g. a solid triangle.
Part FilledPoly(const std::vector<Vec2>& pts, Color col) {
    Part p; p.kind = K::Path; p.type = PT::Curve; p.spline = ST::Poly;
    for (Vec2 v : pts) { Node n(v); n.mode = HM::Vector; p.path.nodes.push_back(n); }
    p.path.closed = true;
    p.fill.enabled = true; p.fill.color = col; p.stroke.enabled = false;
    return p;
}

// A CONVEX outline drawn as an INSIDE-stroke band: a filled ring between the given
// (outer) convex polygon `pts` and a copy inset by `th` toward the centroid. The
// outer edge is EXACTLY `pts` (OM measure) and nothing pokes outside — unlike a
// stroked-with-miter outline whose sharp corners overshoot. Returned as a single
// "keyhole" polygon (outer forward + inner reversed, bridged) so the plain
// ear-clip filler renders the band without needing even-odd hole support.
Part InsideBandPoly(const std::vector<Vec2>& pts, float th, Color col) {
    const size_t n = pts.size();
    if (n < 3) return FilledPoly(pts, col);
    Vec2 cen{0, 0};
    for (Vec2 v : pts) { cen.x += v.x; cen.y += v.y; }
    cen.x /= (float)n; cen.y /= (float)n;
    // Offset every EDGE inward by `th` (parallel), then intersect adjacent offset
    // edges → exact inset vertices so the band width is exactly `th` along each
    // edge normal (no apex over/under-inset). Inward normal = toward the centroid.
    auto inwardNormal = [&](Vec2 a, Vec2 b) {
        Vec2 d{ b.x - a.x, b.y - a.y };
        float L = std::sqrt(d.x * d.x + d.y * d.y);
        if (L < 1e-5f) return Vec2{0, 0};
        Vec2 nrm{ -d.y / L, d.x / L };
        Vec2 mid{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
        Vec2 toC{ cen.x - mid.x, cen.y - mid.y };
        if (nrm.x * toC.x + nrm.y * toC.y < 0) { nrm.x = -nrm.x; nrm.y = -nrm.y; }
        return nrm;
    };
    // Intersection of two lines (p0 along dir0) and (p1 along dir1).
    auto lineX = [](Vec2 p0, Vec2 d0, Vec2 p1, Vec2 d1, Vec2 fallback) {
        float den = d0.x * d1.y - d0.y * d1.x;
        if (std::fabs(den) < 1e-6f) return fallback;
        float t = ((p1.x - p0.x) * d1.y - (p1.y - p0.y) * d1.x) / den;
        return Vec2{ p0.x + d0.x * t, p0.y + d0.y * t };
    };
    std::vector<Vec2> inner(n);
    for (size_t i = 0; i < n; ++i) {
        size_t ip = (i + n - 1) % n, in = (i + 1) % n;
        Vec2 nPrev = inwardNormal(pts[ip], pts[i]);
        Vec2 nNext = inwardNormal(pts[i],  pts[in]);
        Vec2 aP{ pts[ip].x + nPrev.x * th, pts[ip].y + nPrev.y * th };
        Vec2 dP{ pts[i].x - pts[ip].x, pts[i].y - pts[ip].y };
        Vec2 aN{ pts[i].x + nNext.x * th, pts[i].y + nNext.y * th };
        Vec2 dN{ pts[in].x - pts[i].x, pts[in].y - pts[i].y };
        Vec2 fb{ pts[i].x + (nPrev.x + nNext.x) * 0.5f * th,
                 pts[i].y + (nPrev.y + nNext.y) * 0.5f * th };
        inner[i] = lineX(aP, dP, aN, dN, fb);
    }
    // Keyhole: outer 0..n-1, back to outer[0], bridge to inner[0], inner n-1..0.
    std::vector<Vec2> ring;
    ring.reserve(2 * n + 2);
    for (size_t i = 0; i < n; ++i) ring.push_back(pts[i]);
    ring.push_back(pts[0]);
    ring.push_back(inner[0]);
    for (size_t i = n; i-- > 0; ) ring.push_back(inner[i]);
    return FilledPoly(ring, col);
}

// An arc polyline (centre c, radius r), from angle a0 to a1 (radians), `segs`.
std::vector<Vec2> Arc(Vec2 c, float r, float a0, float a1, int segs = 24) {
    std::vector<Vec2> v; v.reserve((size_t)segs + 1);
    for (int i = 0; i <= segs; ++i) {
        float t = a0 + (a1 - a0) * (float)i / (float)segs;
        v.push_back({ c.x + std::cos(t) * r, c.y + std::sin(t) * r });
    }
    return v;
}

// A LOWER half-circle (opening UP) as an editable NURBS STROKE, inside-aligned.
// The control polygon is sampled on the CENTRELINE circle (radius = OM/2 − th/2)
// over the lower half; a clamped quadratic B-spline hugs it as a clean arc that
// passes through both diameter ends with a VERTICAL end tangent → the butt end
// caps read HORIZONTAL (the flat top). `width` = OM diameter, `th` = thickness,
// both mm. Inside align keeps the OM width exact (stroke grows inward).
Part NurbsHalfArc(float width, float th, float scale, Color col) {
    const float R  = std::max(0.01f, (width * 0.5f - th * 0.5f)) * scale;  // centreline R
    const float cy = -R * 0.5f;                  // diameter above origin, arc below
    const float cx = 0.0f;
    // Classic EXACT rational quadratic circle: two 90° arcs (right→bottom→left).
    // Control points = arc ends + tangent-corner per quarter; the corner controls
    // carry weight cos45° = √2/2. The first/last control edges are VERTICAL → the
    // butt end caps read HORIZONTAL (the flat top). Inside align keeps OM width.
    const float w = 0.70710678f;                 // √2/2
    auto cp = [&](float x, float y, float wt) {
        Node n({ cx + x, cy + y }); n.mode = HM::Vector; n.weight = wt; return n;
    };
    Part p; p.kind = K::Curve; p.type = PT::Curve; p.spline = ST::Nurbs;
    p.orderU = 3;                                 // quadratic
    p.path.nodes = {
        cp( R, 0.0f, 1.0f),                      // right end (on the diameter)
        cp( R,  R,   w),                         // corner (tangent intersection)
        cp(0.0f, R,  1.0f),                      // bottom (on circle)
        cp(-R,  R,   w),                         // corner
        cp(-R, 0.0f, 1.0f),                      // left end (on the diameter)
    };
    p.path.closed = false;
    // Endpoint-clamped + Bézier knots → two EXACT rational-quadratic quarter-arcs
    // meeting the end control points with vertical end tangents (horizontal caps).
    p.nurbsEndpoint = true; p.nurbsBezier = true;
    p.fill.enabled = false;
    p.stroke.enabled = true; p.stroke.width = th * scale; p.stroke.color = col;
    // The control circle is the OM edge → the stroke must grow INWARD. The arc winds
    // so its inside lands on the +normal side, so Outer puts the band inward here.
    p.stroke.cap = Cap::Butt; p.stroke.join = Joi::Round; p.stroke.align = Algn::Outer;
    return p;
}

// A "V" glyph (pit 112 / rocky pit 203.1 / water hole 303) traced EXACTLY from the
// ISOM SVG polygon. `svg` holds the six points in SVG units relative to the SVG
// centre (10,10), Y-down; `outerWidthUnits` is that polygon's outer width in SVG
// units, mapped to `outerWidthMm` (the OM width) → so the printed shape matches the
// SVG proportions precisely. Centred on the SVG centre (the placement origin).
Part VGlyphExact(const std::vector<Vec2>& svg, float outerWidthUnits,
                 float outerWidthMm, float scale, Color col) {
    const float u2mm = (outerWidthMm / outerWidthUnits) * scale;   // SVG unit → doc
    std::vector<Vec2> poly; poly.reserve(svg.size());
    for (Vec2 p : svg) poly.push_back({ p.x * u2mm, p.y * u2mm });
    return FilledPoly(poly, col);
}

// THE styled-line builder: one editable straight Poly curve of length `len`
// centred on the origin along +x, stroked at `w`, plus a decorator/dash so the
// pattern (tags / dots / ties / dashes…) regenerates when the curve is edited.
// Returns the single Part; caller adds it to the shape.
Part LineCurve(float len, float w, Color col) {
    return Stroke({ {-len * 0.5f, 0}, {len * 0.5f, 0} }, w, col);
}

}  // namespace

Shape BuildSymbolShape(const IofElement& e, float scale) {
    const float s = (scale > 0.01f) ? scale : 1.0f;
    const Color col   = RGB(LayerColor(e.color));
    const Color black = RGB(LayerColor(SpotColor::Black));
    const Color white{ 1, 1, 1, 1 };
    const float L  = kLineLen;            // line default length (mm, unscaled span)
    const float hw = kAreaW;              // area half-WIDTH  (rect)
    const float vh = kAreaH;              // area half-HEIGHT (rect)
    // Area footprint helpers: top-left and full size of the rectangular swatch.
    const Vec2  areaMin{ -hw, -vh };
    const Vec2  areaSz { 2 * hw, 2 * vh };

    Shape sh;
    sh.name = IofElementLabel(e);
    sh.isomCode = e.code;
    sh.origin = { 0, 0 };
    sh.lockScale = true;                  // ISOM symbols are fixed-size
    sh.lockRotation = e.northLocked;

    auto& P = sh.parts;
    auto mm = [&](float v) { return v * s; };   // scale a mm dimension

    // Helpers to push a styled line in one shot (length L centred, width w mm).
    auto pushLine = [&](float w) -> Part& {
        P.push_back(LineCurve(L, mm(w), col));
        return P.back();
    };

    switch (e.glyph) {
        // ── POINTS ───────────────────────────────────────────────────────────
        case IofGlyphKind::KnollDot:                 // 109 ø0.5 filled dot
            P.push_back(FilledDot({0,0}, mm(0.25f), col)); break;
        case IofGlyphKind::ElongatedKnoll: {         // 110 ONE filled oval 0.8 long × 0.4 high (rotatable)
            Part p; p.kind = K::Ellipse; p.type = PT::Mesh;
            p.pos = { mm(-0.4f), mm(-0.2f) }; p.size = { mm(0.8f), mm(0.4f) };
            p.fill.enabled = true; p.fill.color = col; p.stroke.enabled = false;
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::SmallDepression:          // 111 half-circle opening UP, ø0.8 OM / 0.18
            // Exact rational-NURBS half-circle, inside stroke, flat horizontal top.
            P.push_back(NurbsHalfArc(0.8f, 0.18f, s, col)); break;
        case IofGlyphKind::Pit:                      // 112 V opening UP, exact SVG polygon
            // SVG: 11.98 7.45, 10 12.55, 8.02 7.45, 9.03 7.45, 10 10.16, 10.97 7.45
            // (relative to the 10,10 centre). Outer width 3.96 units → 0.7 mm (OM).
            P.push_back(VGlyphExact(
                { {1.98f,-2.55f}, {0.0f,2.55f}, {-1.98f,-2.55f},
                  {-0.97f,-2.55f}, {0.0f,0.16f}, {0.97f,-2.55f} },
                3.96f, 0.7f, s, col)); break;
        case IofGlyphKind::RockyPit:                 // 203.1 V opening UP, exact SVG polygon
            // SVG: 11.98 7.45, 10 12.55, 8.02 7.45, 8.99 7.45, 10 10.24, 11.02 7.45.
            P.push_back(VGlyphExact(
                { {1.98f,-2.55f}, {0.0f,2.55f}, {-1.98f,-2.55f},
                  {-1.01f,-2.55f}, {0.0f,0.24f}, {1.02f,-2.55f} },
                3.96f, 0.7f, s, col)); break;
        case IofGlyphKind::DangerousPit: {           // 203.2 ring ø0.9 OUTER, stroke 0.35 (inside align)
            // Inside-stroke: the path radius IS the outer edge (0.45 = ø0.9); the
            // 0.35 stroke fills inward, leaving an empty ø0.2 centre.
            Part ring = Ring({0,0}, mm(0.45f), mm(0.35f), col);
            ring.stroke.align = Algn::Inner;
            P.push_back(std::move(ring)); break; }
        case IofGlyphKind::ProminentLandform: {      // 115 brown triangle outline 0.9 (OM → inside stroke), north
            // Inside-stroke band between the outer triangle (OM points) and an
            // inward inset of 0.18 — corners never poke outside (unlike a mitered
            // stroke). Outer points = the 0.9 triangle.
            P.push_back(InsideBandPoly(
                { {0,mm(-0.52f)}, {mm(0.45f),mm(0.26f)}, {mm(-0.45f),mm(0.26f)} },
                mm(0.18f), col)); break; }
        case IofGlyphKind::BoulderDot:               // 204 ø0.4
            P.push_back(FilledDot({0,0}, mm(0.2f), col)); break;
        case IofGlyphKind::LargeBoulderDot:          // 205 ø0.6
            P.push_back(FilledDot({0,0}, mm(0.3f), col)); break;
        case IofGlyphKind::BoulderTriangle:          // 207 filled triangle 0.8, north (apex up)
            P.push_back(FilledPoly({ {0,mm(-0.46f)}, {mm(0.4f),mm(0.3f)}, {mm(-0.4f),mm(0.3f)} }, col)); break;
        case IofGlyphKind::Waterhole:                // 303 blue V opening UP, exact SVG polygon
            // Same polygon as 112 (width 0.7 OM, thickness 0.18).
            P.push_back(VGlyphExact(
                { {1.98f,-2.55f}, {0.0f,2.55f}, {-1.98f,-2.55f},
                  {-0.97f,-2.55f}, {0.0f,0.16f}, {0.97f,-2.55f} },
                3.96f, 0.7f, s, col)); break;
        case IofGlyphKind::Well: {                   // 311 blue square outline 0.8 OM / 0.18 (inside stroke)
            Part sq = RectOutline({mm(-0.4f),mm(-0.4f)}, {mm(0.8f),mm(0.8f)}, mm(0.18f), col, false, col);
            sq.stroke.align = Algn::Inner;           // 0.8 measured to the outer edge (OM)
            P.push_back(std::move(sq)); break; }
        case IofGlyphKind::Spring:                   // 312 half-circle opening UP, ø0.9 OM / 0.18
            // Exact rational-NURBS half-circle, inside stroke, flat horizontal top.
            // The outflow stream is a SEPARATE symbol the user draws — not baked.
            P.push_back(NurbsHalfArc(0.9f, 0.18f, s, col)); break;
        case IofGlyphKind::ProminentWater: {         // 313 blue 5-arm asterisk (72°)
            for (int i = 0; i < 5; ++i) {
                float a = -1.5707963f + (float)i / 5.0f * 6.2831853f;   // first arm up
                P.push_back(Stroke({ {0,0}, {std::cos(a)*mm(0.45f), std::sin(a)*mm(0.45f)} }, mm(0.16f), col));
            }
            break; }
        case IofGlyphKind::KnollGreen:               // 418 green dot ø0.6 w/ white centre
            P.push_back(FilledDot({0,0}, mm(0.3f), col));
            P.push_back(FilledDot({0,0}, mm(0.1f), white)); break;
        case IofGlyphKind::LargeTree: {              // 417 green ring ø0.9 OM / 0.18 (inside stroke) + white mask
            P.push_back(FilledDot({0,0}, mm(0.55f), white));
            Part ring = Ring({0,0}, mm(0.45f), mm(0.18f), col);
            ring.stroke.align = Algn::Inner;         // ø0.9 to the outer edge (OM)
            P.push_back(std::move(ring)); break; }
        case IofGlyphKind::VegFeatureX:              // 419 green X 0.9 w/ white mask, north
            P.push_back(FilledDot({0,0}, mm(0.55f), white));
            P.push_back(Stroke({ {mm(-0.45f),mm(-0.45f)}, {mm(0.45f),mm(0.45f)} }, mm(0.18f), col));
            P.push_back(Stroke({ {mm(-0.45f),mm(0.45f)}, {mm(0.45f),mm(-0.45f)} }, mm(0.18f), col)); break;
        case IofGlyphKind::Cairn: {                  // 526 ring ø0.8 OM / 0.16 (inside) + centre dot 0.14
            Part ring = Ring({0,0}, mm(0.4f), mm(0.16f), col);
            ring.stroke.align = Algn::Inner;         // ø0.8 to the outer edge (OM)
            P.push_back(std::move(ring));
            P.push_back(FilledDot({0,0}, mm(0.07f), col)); break; }
        case IofGlyphKind::Tower:                    // 524 filled dot ø0.8 + cross tags out 0.3 each side, 0.16
            P.push_back(FilledDot({0,0}, mm(0.4f), col));
            P.push_back(Stroke({ {mm(-0.7f),0},{mm(0.7f),0} }, mm(0.16f), col));   // ø0.8 (0.4) + 0.3 out
            P.push_back(Stroke({ {0,mm(-0.7f)},{0,mm(0.7f)} }, mm(0.16f), col)); break;
        case IofGlyphKind::SmallTower:               // 525 T: top bar 1.0 long, vertical 1.0 OM tall, 0.16
            P.push_back(Stroke({ {mm(-0.5f),mm(-0.5f)},{mm(0.5f),mm(-0.5f)} }, mm(0.16f), col));  // top 1.0
            P.push_back(Stroke({ {0,mm(-0.5f)},{0,mm(0.5f)} }, mm(0.16f), col)); break;            // height 1.0 OM
        case IofGlyphKind::FodderRack: {             // 527 inverted-Y: 60° roof apex up + vertical post, north
            // Apex at top (0,−0.45); two roof legs spread to ±0.26 at y=0 giving a
            // 60° apex (half-angle 30°, tan30·0.45 ≈ 0.26); post drops to +0.45.
            const float h = mm(0.45f);
            const float dx = h * 0.5774f;            // tan(30°)
            P.push_back(Stroke({ {-dx,0},{0,-h},{dx,0} }, mm(0.16f), col));   // 60° roof
            P.push_back(Stroke({ {0,-h},{0,h} }, mm(0.16f), col));           // post through apex
            break; }
        case IofGlyphKind::FeatureRing:              // 530 ring ø0.8
            P.push_back(Ring({0,0}, mm(0.4f), mm(0.16f), col)); break;
        case IofGlyphKind::FeatureX:                 // 531 X 0.8 north
            P.push_back(Stroke({ {mm(-0.4f),mm(-0.4f)}, {mm(0.4f),mm(0.4f)} }, mm(0.16f), col));
            P.push_back(Stroke({ {mm(-0.4f),mm(0.4f)}, {mm(0.4f),mm(-0.4f)} }, mm(0.16f), col)); break;
        case IofGlyphKind::SpotHeight:               // 603 small dot ø0.3
            P.push_back(FilledDot({0,0}, mm(0.15f), col)); break;
        case IofGlyphKind::RegistrationMark:         // 602 large + cross, 4 mm / 0.1
            P.push_back(Stroke({ {mm(-2.0f),0},{mm(2.0f),0} }, mm(0.1f), col));
            P.push_back(Stroke({ {0,mm(-2.0f)},{0,mm(2.0f)} }, mm(0.1f), col)); break;
        case IofGlyphKind::MapIssuePoint:            // 702 short purple BAR (2.5 long × 0.6 thick)
            // Attaches to one END of the marked route (707); a perpendicular bar.
            P.push_back(Stroke({ {mm(-1.25f),0}, {mm(1.25f),0} }, mm(0.6f), col)); break;
        case IofGlyphKind::FirstAid:                 // 712 purple filled plus (4 mm / 1.33)
            P.push_back(FilledRect({mm(-0.665f),mm(-2.0f)}, {mm(1.33f),mm(4.0f)}, col));
            P.push_back(FilledRect({mm(-2.0f),mm(-0.665f)}, {mm(4.0f),mm(1.33f)}, col)); break;
        case IofGlyphKind::Refreshment: {            // 713 purple cup: tapered body + rim + base (§3.8)
            // Cup body trapezoid (wider at top), a rim line across the top, a base.
            P.push_back(Stroke({ {mm(-1.75f),mm(-1.75f)}, {mm(-1.05f),mm(1.75f)},
                                 {mm(1.05f),mm(1.75f)}, {mm(1.75f),mm(-1.75f)} }, mm(0.4f), col));
            // top rim (the cup mouth, slightly above the body top)
            P.push_back(Stroke({ {mm(-1.95f),mm(-1.75f)}, {mm(1.95f),mm(-1.75f)} }, mm(0.4f), col));
            break; }
        case IofGlyphKind::ContinuingPoint:          // 715 purple triangle INSIDE a circle ø5
            P.push_back(Ring({0,0}, mm(2.5f), mm(0.35f), col));
            P.push_back(Stroke({ {0,mm(-1.4f)}, {mm(1.2f),mm(0.7f)}, {mm(-1.2f),mm(0.7f)} }, mm(0.35f), col, true)); break;

        // ── COURSE points ──────────────────────────────────────────────────────
        case IofGlyphKind::Control:                  // 703 circle ø5.0 / 0.35
            P.push_back(Ring({0,0}, mm(2.5f), mm(0.35f), col)); break;
        case IofGlyphKind::Start:                     // 701 triangle ø6.0 / 0.35, north (apex up)
            P.push_back(Stroke({ {0,mm(-3.0f)}, {mm(2.6f),mm(1.5f)}, {mm(-2.6f),mm(1.5f)} }, mm(0.35f), col, true)); break;
        case IofGlyphKind::Finish:                   // 706 double circle ø4 / ø6, 0.35
            P.push_back(Ring({0,0}, mm(3.0f), mm(0.35f), col));
            P.push_back(Ring({0,0}, mm(2.0f), mm(0.35f), col)); break;

        // ── LINES (a SINGLE editable styled curve along +x; pattern via decor) ──
        case IofGlyphKind::Contour:        pushLine(0.14f); break;
        case IofGlyphKind::IndexContour:   pushLine(0.25f); break;
        case IofGlyphKind::DistinctBoundary: pushLine(0.10f); break;
        case IofGlyphKind::Watercourse:    pushLine(0.30f); break;
        case IofGlyphKind::SmallWatercourse: pushLine(0.18f); break;
        case IofGlyphKind::Road:           pushLine(0.35f); break;
        case IofGlyphKind::CourseLine:     pushLine(0.35f); break;
        case IofGlyphKind::MagneticNorth: { // 601 vertical line + arrowhead at top; black 0.10 / blue 0.12
            float w = (e.color == SpotColor::Blue) ? 0.12f : 0.10f;
            Part& l = pushLine(w);
            l.path.nodes.clear();             // re-orient vertical
            Node n0({0, mm(L*0.5f)}); n0.mode = HM::Vector;
            Node n1({0,-mm(L*0.5f)}); n1.mode = HM::Vector;
            l.path.nodes = { n0, n1 };
            // arrowhead at the top (north, −Y)
            P.push_back(Stroke({ {mm(-0.6f),mm(-L*0.5f+1.2f)}, {0,-mm(L*0.5f)}, {mm(0.6f),mm(-L*0.5f+1.2f)} }, mm(w), col));
            break; }
        case IofGlyphKind::FormLine: {     // 103 dashed brown 0.1 (dash 2.0 / gap 0.2)
            Part& l = pushLine(0.1f);
            l.stroke.dash = { mm(2.0f), mm(0.2f) }; break; }
        case IofGlyphKind::SeasonalChannel: { // 306 dashed blue 0.18 (1.25 / 0.25)
            Part& l = pushLine(0.18f);
            l.stroke.dash = { mm(1.25f), mm(0.25f) }; break; }
        case IofGlyphKind::VehicleTrack: { // 504 dashed 0.35 (3.0 / 0.25)
            Part& l = pushLine(0.35f);
            l.stroke.dash = { mm(3.0f), mm(0.25f) }; break; }
        case IofGlyphKind::Footpath: {     // 505 dashed 0.25 (2.0 / 0.25)
            Part& l = pushLine(0.25f);
            l.stroke.dash = { mm(2.0f), mm(0.25f) }; break; }
        case IofGlyphKind::SmallPath: {    // 506 dashed 0.18 (1.0 / 0.25)
            Part& l = pushLine(0.18f);
            l.stroke.dash = { mm(1.0f), mm(0.25f) }; break; }
        case IofGlyphKind::LessDistinctPath: { // 507 double-dash 0.18 (1.0 dashes, 0.25/0.8 gaps)
            Part& l = pushLine(0.18f);
            l.stroke.dash = { mm(1.0f), mm(0.25f), mm(1.0f), mm(0.8f) }; break; }
        case IofGlyphKind::NarrowRide: {   // 508 thin dashed 0.14 (2.0 / 0.25)
            Part& l = pushLine(0.14f);
            l.stroke.dash = { mm(2.0f), mm(0.25f) }; break; }
        case IofGlyphKind::SmallErosionGully: { // 108 dotted brown ø0.25 @ 0.45
            Part& l = pushLine(0.001f);          // base line invisible
            l.stroke.color = col;
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(0.45f);
            l.stroke.decorSize = mm(0.25f); l.stroke.decorThickness = mm(0.25f); break; }
        case IofGlyphKind::NarrowMarsh: {  // 309 dotted blue ø0.25 @ 0.45
            Part& l = pushLine(0.001f);
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(0.45f);
            l.stroke.decorSize = mm(0.25f); l.stroke.decorThickness = mm(0.25f); break; }
        case IofGlyphKind::VegBoundaryDots: { // 416 dotted black ø0.22 @ 0.45
            Part& l = pushLine(0.001f);
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(0.45f);
            l.stroke.decorSize = mm(0.22f); l.stroke.decorThickness = mm(0.22f); break; }
        case IofGlyphKind::VegBoundaryGreen: { // 416 alt: dark-green dashed 0.14 (dash 0.3 / gap 0.2)
            Part& l = pushLine(0.14f);
            l.stroke.color = RGB(LayerColor(SpotColor::Green));   // dark green
            l.stroke.dash = { mm(0.3f), mm(0.2f) }; break; }
        case IofGlyphKind::ErosionGully: {  // 107 solid line 0.25 tapering to a POINT (0.75) at BOTH ends
            // Taper cap auto-follows the real endpoints (no baked tips), so editing
            // the curve keeps the points at whatever the new ends are.
            Part& l = pushLine(0.25f);
            l.stroke.cap = Cap::Taper; l.stroke.capTaper = mm(0.75f); break; }
        case IofGlyphKind::EarthBank: {     // 104 line 0.25 + downhill tags (one side) 0.4 / 0.14
            Part& l = pushLine(0.25f);
            l.stroke.decor = Decor::Tags; l.stroke.decorSpacing = mm(0.5f);
            l.stroke.decorSize = mm(0.4f); l.stroke.decorThickness = mm(0.14f); break; }
        case IofGlyphKind::EarthWall: {     // 105.1 line 0.18 + dots straddling ø0.45 @ 2.0
            Part& l = pushLine(0.18f);
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.45f); break; }
        case IofGlyphKind::RetainingEarthWall: { // 105.2 line 0.18 + half-dots one side ø0.45 @ 1.0
            Part& l = pushLine(0.18f);
            l.stroke.decor = Decor::HalfDots; l.stroke.decorSpacing = mm(1.0f);
            l.stroke.decorSize = mm(0.45f); break; }
        case IofGlyphKind::RuinedEarthWall: { // 106 ruined: dots ø0.45 @ 2.0 CC, dash between (gap 0.35)
            // One dash period == one dot period (2.0) and both centred, so the dot
            // sits in the gap and the dash is centred between dots (no drift).
            Part& l = pushLine(0.18f);
            l.stroke.dash = { mm(1.3f), mm(0.7f) };   // period 2.0, gap leaves room for the dot
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.45f); break; }
        case IofGlyphKind::ImpassableCliff: { // 201 thick line 0.35 + downhill tags 0.4 (OM) / 0.12
            Part& l = pushLine(0.35f);
            l.stroke.cap = Cap::Butt;             // butt by default
            sh.allowCapEdit = true;               // mapper may pick butt/round ends
            l.stroke.decor = Decor::Tags; l.stroke.decorSpacing = mm(0.5f);
            l.stroke.decorSize = mm(0.4f); l.stroke.decorThickness = mm(0.12f); break; }
        case IofGlyphKind::Cliff: {         // 202 line 0.25 + short downhill tags 0.4 (OM) / 0.12
            Part& l = pushLine(0.25f);
            l.stroke.cap = Cap::Butt;             // butt by default
            sh.allowCapEdit = true;               // mapper may pick butt/round ends
            l.stroke.decor = Decor::Tags; l.stroke.decorSpacing = mm(0.5f);
            l.stroke.decorSize = mm(0.4f); l.stroke.decorThickness = mm(0.12f); break; }
        case IofGlyphKind::Trench: {        // 215 two parallel 0.10 lines, 0.10 apart
            Part& l = pushLine(0.10f);
            l.stroke.decor = Decor::DoubleLine; l.stroke.decorSize = mm(0.20f);
            l.stroke.decorThickness = mm(0.10f); break; }
        case IofGlyphKind::Wall: {          // 513.1 line 0.14 + dots straddling ø0.4 @ 2.0
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.4f); break; }
        case IofGlyphKind::RetainingWall: { // 513.2 line 0.14 + half-dots one side ø0.4 @ 1.0
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::HalfDots; l.stroke.decorSpacing = mm(1.0f);
            l.stroke.decorSize = mm(0.4f); break; }
        case IofGlyphKind::RuinedWall: {    // 514 ruined: dots ø0.4 @ 2.0 CC, dash between (centred)
            Part& l = pushLine(0.14f);
            l.stroke.dash = { mm(1.3f), mm(0.7f) };   // period 2.0
            l.stroke.decor = Decor::Dots; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.4f); break; }
        case IofGlyphKind::ImpassableWall: { // 515 ONE line 0.25 + GROUPS of two dots
            // A single editable line carrying the impassable-wall pattern via the
            // PairDots decorator: pairs of ø0.6 dots (0.6 apart) every 3.0 (CC).
            Part& l = pushLine(0.25f);
            l.stroke.decor = Decor::PairDots;
            l.stroke.decorSpacing = mm(3.0f);   // group-to-group
            l.stroke.decorSize = mm(0.6f);      // dot ø (also the in-pair spacing)
            break; }
        case IofGlyphKind::Fence: {         // 516 line 0.14 + oblique pickets 60° one side
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::Slashes; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.5f); l.stroke.decorThickness = mm(0.14f);
            l.stroke.decorAngleDeg = 60.0f; break; }
        case IofGlyphKind::RuinedFence: {   // 517 ruined: dashes with a picket per cycle (aligned to gaps)
            Part& l = pushLine(0.14f);
            l.stroke.dash = { mm(0.8f), mm(0.6f) };
            l.stroke.decor = Decor::Slashes; l.stroke.decorSpacing = mm(1.4f);
            l.stroke.decorSize = mm(0.4f); l.stroke.decorThickness = mm(0.14f);
            l.stroke.decorAngleDeg = 60.0f; break; }
        case IofGlyphKind::ImpassableFence: { // 518 ONE line 0.25 + GROUPS of two pickets
            // A single editable line carrying the impassable-fence pattern via the
            // PairSlashes decorator: two 60° pickets (0.4 long) per group, groups
            // every 2.5 (CC). One part, one point.
            Part& l = pushLine(0.25f);
            l.stroke.decor = Decor::PairSlashes;
            l.stroke.decorSpacing = mm(2.5f);   // group-to-group
            l.stroke.decorSize = mm(0.5f);      // picket length (in-pair spacing × 0.8)
            l.stroke.decorThickness = mm(0.14f);
            l.stroke.decorAngleDeg = 60.0f; break; }
        case IofGlyphKind::Railway: {       // 509 ONE line: black/white dash + edges
            // A single 0.35 band that alternates black (1.5) / white (1.0) dashes,
            // bordered by two 0.10 black edge contours CONTAINED within the band
            // (outer edge of each contour aligns with the band edge → total width
            // stays 0.35). One part, one point — edits like any other line symbol.
            Part& l = pushLine(0.35f);
            l.stroke.color = black;
            l.stroke.dash = { mm(1.5f), mm(1.0f) };   // black 1.5 / white 1.0
            l.stroke.decor = Decor::EdgeLines;
            l.stroke.decorSize = mm(0.25f);            // edge centres at ±0.125
            l.stroke.decorThickness = mm(0.10f);
            break; }
        case IofGlyphKind::PowerLine: {     // 510 single 0.14 line + pylon MARKS @ 6.0
            // The pylons are not baked geometry: they are Pylon line-marks placed
            // every 6.0 mm by default, draggable along the line with the object
            // tool and re-layoutable. Bar half-length = halfLine(0.07) + 0.3 OM.
            Part& l = pushLine(0.14f);
            for (float x = mm(6.0f); x < L * 0.5f; x += mm(6.0f)) {
                for (float xs : { -x, x }) {
                    Renderer::LineMark m; m.kind = Renderer::LineMarkKind::Pylon;
                    m.t = (xs + L * 0.5f) / L;            // arc-length fraction
                    m.size = mm(0.37f); m.thickness = mm(0.2f);
                    l.marks.push_back(m);
                }
            }
            { Renderer::LineMark m0; m0.kind = Renderer::LineMarkKind::Pylon;
              m0.t = 0.5f; m0.size = mm(0.37f); m0.thickness = mm(0.2f); l.marks.push_back(m0); }
            break; }
        case IofGlyphKind::MajorPowerLine: { // 511 twin rails + pylon MARKS @ 6.0
            // 511: a SINGLE curve carrying twin rails 0.4 CC apart (0.14 each) via
            // the DoubleLine decorator, plus Pylon line-marks (bar 0.2, overhanging
            // 0.3 OM beyond each rail → half-length = 0.2 rail + 0.3 = 0.5). The
            // pylons start at 6.0 mm spacing and are draggable / re-layoutable.
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::DoubleLine; l.stroke.decorSize = mm(0.4f);
            l.stroke.decorThickness = mm(0.14f);
            for (float x = mm(6.0f); x < L * 0.5f; x += mm(6.0f)) {
                for (float xs : { -x, x }) {
                    Renderer::LineMark m; m.kind = Renderer::LineMarkKind::Pylon;
                    m.t = (xs + L * 0.5f) / L;
                    m.size = mm(0.5f); m.thickness = mm(0.2f);
                    l.marks.push_back(m);
                }
            }
            { Renderer::LineMark m0; m0.kind = Renderer::LineMarkKind::Pylon;
              m0.t = 0.5f; m0.size = mm(0.5f); m0.thickness = mm(0.2f); l.marks.push_back(m0); }
            break; }
        case IofGlyphKind::ProminentLineFeature: { // 528 line 0.14 + arrow ticks 45°
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::Slashes; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.4f); l.stroke.decorThickness = mm(0.14f);
            l.stroke.decorAngleDeg = 45.0f; break; }
        case IofGlyphKind::ProminentUncrossableLine: { // 529 double line + arrow ticks
            Part& l = pushLine(0.14f);
            l.stroke.decor = Decor::DoubleTicks; l.stroke.decorSize = mm(0.6f);
            l.stroke.decorThickness = mm(0.14f); l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorAngleDeg = 45.0f; break; }
        case IofGlyphKind::WideRoad: {      // 502 two black edges + brown 50% infill
            // Brown filled band (width 0.3 + 2×0.14 mm) under two black edges.
            Part& band = pushLine(0.58f);   // 0.30 infill + 2×0.14 edges
            band.stroke.color = RGB(ScreenColor(SpotColor::Brown, 0.5f));   // opaque Brown 50%
            // Two black edges, 0.30 apart (centre-to-centre of the infill band).
            Part& edges = pushLine(0.14f);
            edges.stroke.color = black;
            edges.stroke.decor = Decor::DoubleLine; edges.stroke.decorSize = mm(0.44f);
            edges.stroke.decorThickness = mm(0.14f); break; }
        case IofGlyphKind::MarkedRoute: {   // 707 dashed purple (upper) 0.35
            Part& l = pushLine(0.35f);
            l.stroke.dash = { mm(2.0f), mm(0.5f) }; break; }
        case IofGlyphKind::OOBBoundary: {   // 708 solid purple 0.7 line
            pushLine(0.7f); break; }
        case IofGlyphKind::OOBRoute: {      // 711 purple line of × symbols
            Part& l = pushLine(0.001f);
            l.stroke.decor = Decor::Crosses; l.stroke.decorSpacing = mm(2.0f);
            l.stroke.decorSize = mm(0.6f); l.stroke.decorThickness = mm(0.35f); break; }

        // ── SPECIAL two-part / non-curve symbols ───────────────────────────────
        case IofGlyphKind::Stairway: {      // 532 two rails + perpendicular rungs
            P.push_back(Stroke({ {-L*0.5f,mm(-0.3f)}, {L*0.5f,mm(-0.3f)} }, mm(0.1f), col));
            P.push_back(Stroke({ {-L*0.5f,mm( 0.3f)}, {L*0.5f,mm( 0.3f)} }, mm(0.1f), col));
            for (float x = -L*0.5f + mm(0.6f); x < L*0.5f; x += mm(0.6f))
                P.push_back(Stroke({ {x,mm(-0.3f)},{x,mm(0.3f)} }, mm(0.1f), col));
            break; }
        case IofGlyphKind::BridgeTunnel: {  // 512 two-part: each entrance = a base bar + two 0.4 tags @ 60°
            // The two halves face each other across an adjustable gap (here the
            // default), the crossing feature passing between. 0.18 thick.
            const float a = 60.0f * 3.14159265f / 180.0f, tag = mm(0.4f);
            const float dxg = mm(0.6f);                 // half the default gap
            for (int side = -1; side <= 1; side += 2) {
                float bx = side * dxg;
                // short base bar across the line
                P.push_back(Stroke({ {bx, mm(-0.5f)}, {bx, mm(0.5f)} }, mm(0.18f), col));
                // two tags angling OUTWARD from the bar ends (toward the entrance)
                float ox = side * std::cos(a) * tag, oy = std::sin(a) * tag;
                P.push_back(Stroke({ {bx, mm(-0.5f)}, {bx + ox, mm(-0.5f) - oy} }, mm(0.18f), col));
                P.push_back(Stroke({ {bx, mm( 0.5f)}, {bx + ox, mm( 0.5f) + oy} }, mm(0.18f), col));
            }
            break; }
        case IofGlyphKind::CrossingPointFence: { // 519 crossing point: two parallel ticks 1.0, gap 0.6
            // A passage that cuts the line: two short bars 1.0 long, 0.18 thick,
            // 0.6 apart (CC), perpendicular to the line.
            P.push_back(Stroke({ {mm(-0.3f),mm(-0.5f)}, {mm(-0.3f),mm(0.5f)} }, mm(0.18f), col));
            P.push_back(Stroke({ {mm( 0.3f),mm(-0.5f)}, {mm( 0.3f),mm(0.5f)} }, mm(0.18f), col)); break; }

        // ── AREAS — surfaces with infinite, clipped, combinable FILL LAYERS ─────
        case IofGlyphKind::WaterArea: {          // 301 full blue + black bank line
            P.push_back(Surface(areaMin, areaSz, col));
            P.push_back(RectOutline(areaMin, areaSz, mm(0.12f), black, false, col)); break; }
        case IofGlyphKind::ShallowWater: {       // 302 opaque blue 50% + blue outline
            Part p = Surface(areaMin, areaSz, RGB(ScreenColor(e.color, 0.5f)));
            P.push_back(std::move(p));
            P.push_back(RectOutline(areaMin, areaSz, mm(0.18f), col, false, col)); break; }
        case IofGlyphKind::OpenLand:             // 401 full yellow
            P.push_back(Surface(areaMin, areaSz, col)); break;
        case IofGlyphKind::OpenLandDots: {       // 402/404 scattered trees/bushes (DIAGONAL 45° dot grid)
            // 402 open land (full yellow) + dots ø0.4 @ 0.7 CC; 404 ROUGH open
            // (yellow 50%) + dots ø0.5 @ 0.8 CC. Dots white (trees) or green 60%
            // (bushes) — the green variant lives on the rough-open (404).
            const bool rough = (e.code == 4040);
            Part p = Surface(areaMin, areaSz, rough ? RGB(ScreenColor(SpotColor::Yellow, 0.5f)) : col);
            Color dotCol = rough ? RGB(ScreenColor(SpotColor::Green, 0.6f)) : white;
            float cc = rough ? mm(0.8f) : mm(0.7f), dia = rough ? mm(0.5f) : mm(0.4f);
            AddLayer(p, FP::Dots, dotCol, cc, dia, 45.0f);   // 45° diagonal grid
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::RoughOpen:            // 403 opaque yellow 50%
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(e.color, 0.5f)))); break;
        case IofGlyphKind::CultivatedLand: {     // 412 yellow + regular black dot SQUARE grid ø0.2 @ 0.8 CC
            Part p = Surface(areaMin, areaSz, col);
            AddLayer(p, FP::Dots, black, mm(0.8f), mm(0.2f), 0.0f);
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::SandyGround: {        // 213 opaque yellow 50% + black dots ø0.16, 45° grid @ 0.45 CC
            Part p = Surface(areaMin, areaSz, RGB(ScreenColor(SpotColor::Yellow, 0.5f)));
            AddLayer(p, FP::Dots, black, mm(0.45f), mm(0.16f), 45.0f);
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::VegGreen1:            // 406 opaque green 30%
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(e.color, 0.3f)))); break;
        case IofGlyphKind::VegGreen2:            // 408 opaque green 60%
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(e.color, 0.6f)))); break;
        case IofGlyphKind::VegGreen3:            // 410 green 100%
            P.push_back(Surface(areaMin, areaSz, col)); break;
        case IofGlyphKind::VegStripes: {         // 407 (slow, denser) / 409 (walk, sparser) green stripes
            Part p = Surface(areaMin, areaSz);
            // 407 = 0.12 lines @ 0.84 CC ; 409 = 0.14 lines @ 0.42 CC (denser).
            float cc = (e.code == 4090) ? mm(0.42f) : mm(0.84f);
            float w  = (e.code == 4090) ? mm(0.14f) : mm(0.12f);
            AddLayer(p, FP::Lines, col, cc, w, 90.0f);
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::ForestWhite:          // 405 white (outline only for preview)
            P.push_back(RectOutline(areaMin, areaSz, mm(0.14f), RGB(LayerColor(SpotColor::Green)), true, white)); break;
        case IofGlyphKind::BareRock:             // 214 opaque black 35% (grey)
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(SpotColor::Black, 0.35f)))); break;
        case IofGlyphKind::Building:             // 521 solid black (variant: outline + black 50% — see note)
            P.push_back(Surface(areaMin, areaSz, col)); break;
        case IofGlyphKind::GiganticBoulder: {    // 206 solid black plan (small irregular blob)
            float r = mm(1.2f);
            P.push_back(FilledPoly({ {-r*0.5f,-r*0.4f}, {r*0.4f,-r*0.5f}, {r*0.5f,r*0.3f},
                                     {0,r*0.5f}, {-r*0.5f,r*0.2f} }, col)); break; }
        case IofGlyphKind::Ruin:                 // 523 small black square outline (dashed)
            P.push_back(RectOutline({mm(-0.5f),mm(-0.5f)}, {mm(1.0f),mm(1.0f)}, mm(0.16f), col, false, col)); break;
        case IofGlyphKind::Canopy: {             // 522 opaque black 20% + black outline 0.1
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(SpotColor::Black, 0.2f))));
            P.push_back(RectOutline(areaMin, areaSz, mm(0.1f), black, false, col)); break; }
        case IofGlyphKind::PavedArea: {          // 501 opaque brown 50% + black outline 0.1
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(SpotColor::Brown, 0.5f))));
            P.push_back(RectOutline(areaMin, areaSz, mm(0.1f), black, false, col)); break; }
        case IofGlyphKind::OutOfBounds: {        // 520 SOLID olive (Yellow100 + Green50) + black 0.18 outline
            // Out-of-bounds AREA = a flat olive, no hatch (the course OOB 709 uses
            // purple hatch; this is the map symbol 520). Olive ≈ CMYK 38,27,100,0.
            Color olive{ 0.62f * 1.0f, 0.73f, 0.0f, 1.0f };   // (1-0.38, 1-0.27, 1-1.0)
            P.push_back(Surface(areaMin, areaSz, olive));
            if (e.color == SpotColor::Yellow)              // 520 map symbol → black contour
                P.push_back(RectOutline(areaMin, areaSz, mm(0.18f), black, false, col));
            else {                                          // 709 course OOB → purple hatch
                Part h = Surface(areaMin, areaSz);
                AddLayer(h, FP::CrossHatch, col, mm(1.2f), mm(0.2f), 45.0f);
                P.push_back(std::move(h));
            }
            break; }
        case IofGlyphKind::MarshArea: {          // 307/308/310 blue line screens (all patterns)
            Part p = Surface(areaMin, areaSz);
            if (e.code == 3100) {                // 310 indistinct: dashed, phase-alternating, 0.3 CC
                FL& fl = AddLayer(p, FP::Lines, col, mm(0.3f), mm(0.1f), 0.0f);
                fl.dash = mm(0.9f); fl.dashGap = mm(0.25f); fl.altPhase = true;
            } else if (e.code == 3070) {         // 307 uncrossable: 0.25 lines @ 0.5 CC + black outline
                AddLayer(p, FP::Lines, col, mm(0.5f), mm(0.25f), 0.0f);
            } else {                             // 308 marsh: 0.1 lines @ 0.3 CC
                AddLayer(p, FP::Lines, col, mm(0.3f), mm(0.1f), 0.0f);
            }
            P.push_back(std::move(p));
            if (e.code == 3070)                  // 307 uncrossable: separate black contour stroke
                P.push_back(RectOutline(areaMin, areaSz, mm(0.12f), black, false, col));
            break; }
        case IofGlyphKind::StonyGround: {        // 113/114, 210/211/212 — DISTINCT densities
            Part p = Surface(areaMin, areaSz);
            // Centre-to-centre per ISOM (×2.2 to read at the preview scale):
            //   210 ~0.5, 211 ~0.36, 212 ~0.28 ; 113 ~0.55, 114 ~0.32.
            float cc = mm(0.5f);                 // TRUE centre-to-centre per ISOM
            switch (e.code) {
                case 2100: cc = mm(0.5f);  break;   // 210 stony slow  (0.45–0.6)
                case 2110: cc = mm(0.36f); break;   // 211 stony walk  (0.32–0.4)
                case 2120: cc = mm(0.28f); break;   // 212 stony fight (0.25–0.32)
                case 1130: cc = mm(0.55f); break;   // 113 broken ground (max 0.6)
                case 1140: cc = mm(0.32f); break;   // 114 very broken   (max 0.38)
                default: break;
            }
            AddLayer(p, FP::RandomDots, col, cc, mm(0.2f));
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::BoulderField: {       // 208/209 solid 8:6:5 triangles (random orient), true CC
            Part p = Surface(areaMin, areaSz);
            // 208 scattered CC max 1.2 / min 0.75; 209 dense CC max 0.6. Use a mid CC.
            float cc = (e.code == 2090) ? mm(0.55f) : mm(0.95f);
            AddLayer(p, FP::Triangles, col, cc, mm(0.8f), 0.0f);   // size = longest side (0.8)
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::Vineyard: {           // 414 green dashed rows @ 0.85 CC (phase-alternating)
            Part p = Surface(areaMin, areaSz);
            FL& fl = AddLayer(p, FP::Lines, RGB(LayerColor(SpotColor::Green)), mm(0.85f), mm(0.2f), 90.0f);
            fl.dash = mm(1.3f); fl.dashGap = mm(0.6f); fl.altPhase = true;
            P.push_back(std::move(p)); break; }
        case IofGlyphKind::Orchard: {            // 413 green dot rows ø0.45 @ 0.8 CC (square grid, rotatable)
            Part p = Surface(areaMin, areaSz);
            AddLayer(p, FP::Dots, RGB(LayerColor(SpotColor::Green)), mm(0.8f), mm(0.45f), 0.0f);
            P.push_back(std::move(p)); break; }

        // ── generic fallbacks ──────────────────────────────────────────────────
        case IofGlyphKind::GenericLine:
            pushLine(0.3f); break;
        case IofGlyphKind::GenericArea: {
            P.push_back(Surface(areaMin, areaSz, RGB(ScreenColor(e.color, 0.5f)))); break; }
        case IofGlyphKind::GenericPoint:
        default:
            P.push_back(FilledDot({0,0}, mm(0.5f), col)); break;
    }
    return sh;
}

Renderer::Shape PreviewShape(const IofElement& e, float scale) {
    Shape sh = BuildSymbolShape(e, scale);
    const float s = (scale > 0.01f) ? scale : 1.0f;
    auto mmv = [&](float v) { return v * s; };

    if (e.type == IofType::Line) {
        // Replace the long default segment with a VERY SHORT straight sample so the
        // pattern (dashes / tags / dots) reads at thumbnail size — the sample fills
        // the square width. Reshape only path-based parts; keep their styling.
        const float len = mmv(1.6f);              // short sample length (mm @ scale)
        for (Part& p : sh.parts) {
            if (p.path.nodes.size() >= 2) {
                p.path.nodes.resize(2);
                p.path.nodes[0].pos = { -len * 0.5f, 0 };
                p.path.nodes[1].pos = {  len * 0.5f, 0 };
                p.path.nodes[0].mode = p.path.nodes[1].mode = HM::Vector;
                p.path.nodes[0].hasIn = p.path.nodes[0].hasOut = false;
                p.path.nodes[1].hasIn = p.path.nodes[1].hasOut = false;
                p.path.closed = false;
                p.path.subStart.clear();
            }
        }
    } else if (e.type == IofType::Area) {
        // Shrink the surface to a SMALL swatch (~one pattern period) so the
        // texture's auto-fit zooms right IN — the pattern fills the whole cell.
        const Vec2 sm{ -mmv(0.7f), -mmv(0.7f) };
        const Vec2 ssz{ mmv(1.4f), mmv(1.4f) };
        for (Part& p : sh.parts) {
            if (p.kind == K::Rectangle) { p.pos = sm; p.size = ssz; }
        }
    }
    return sh;
}

}  // namespace App::Modules::IofMapping

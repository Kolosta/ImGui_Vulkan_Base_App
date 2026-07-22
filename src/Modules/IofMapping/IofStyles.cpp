#include "IofStyles.h"

#include <cmath>

namespace App::Modules::IofMapping {

namespace {

// sRGB → linear (Ink colours are linear-light straight alpha).
float SrgbToLin(float u) {
    return u <= 0.04045f ? u / 12.92f
                         : std::pow((u + 0.055f) / 1.055f, 2.4f);
}
Ink::Color ToInk(IofRgb c) {
    return { SrgbToLin(c.r), SrgbToLin(c.g), SrgbToLin(c.b), 1.0f };
}

// ── Style builders (core vocabulary only) ────────────────────────────────────

Ink::Stroke MakeStroke(Ink::Color col, double width) {
    Ink::Stroke s;
    s.paint.color = col;
    s.width = width;
    s.cap = Ink::CapStyle::Butt;
    s.join = Ink::JoinStyle::Round;
    return s;
}

void Dash(Ink::Stroke& s, std::initializer_list<double> pattern) {
    s.dashPattern.assign(pattern);
}

// Round-cap dotted line: tiny dashes at `pitch`, dot diameter = the width.
void Dotted(Ink::Stroke& s, double pitch) {
    s.cap = Ink::CapStyle::Round;
    s.dashPattern = { 0.02, pitch };
}

// Dots ASTRIDE the line at a fixed pitch (walls). `diameter` is the full dot
// size; `groupCount`/`groupPitch` stamp a group of dots per step (515);
// `trim` (> 0) keeps that much clear at both ends, measured from the dots'
// OUTER edge, and pins the first dot exactly there.
Ink::StrokeRepeat DotRun(double diameter, double pitch, int groupCount = 1,
                         double groupPitch = 0.0, double trim = 0.0) {
    Ink::StrokeRepeat r;
    r.shape = Ink::MarkShape::Circle;
    r.mode  = Ink::MarkObjectMode::Fusion;
    r.sizePercent = false;
    r.size  = diameter * 0.5;   // radius
    r.side  = Ink::RepeatSide::Center;
    r.sideOffset = 0.0;  r.offsetPercent = false;
    r.distribute = Ink::RepeatDistribute::Pitch;
    r.pitch = pitch;
    r.groupCount = groupCount;
    r.groupPitch = groupPitch;
    r.startTrim = trim;  r.endTrim = trim;
    r.trimMeasure = Ink::RepeatTrimMeasure::Outside;
    return r;
}

// N rays from the centre, evenly spread (313 prominent water = 5 arms / 72°).
Ink::PathData RayStar(double r, int rays) {
    Ink::PathData p;
    for (int i = 0; i < rays; ++i) {
        const double a = (3.14159265358979 * 2.0 * i) / rays - 1.5707963267948966;
        Ink::Subpath sp; sp.closed = false;
        Ink::Anchor a0; a0.pos = { 0, 0 };
        Ink::Anchor a1; a1.pos = { std::cos(a) * r, std::sin(a) * r };
        sp.anchors = { a0, a1 };
        p.subpaths.push_back(std::move(sp));
    }
    return p;
}

// Short TAGS reaching out on one side (cliffs / banks / fences). `len`/`thick`
// are the FULL length / thickness; `pitch` = centre-to-centre; `offsetPct` =
// the START offset as a % of the stroke width (50 = the stroke's OUTER edge),
// with a join back to the spine. `rotation` inclines the tag (fence pickets).
Ink::StrokeRepeat TagRun(double len, double thick, double pitch,
                         Ink::RepeatSide side, double offsetPct = 50.0,
                         double rotation = 0.0, int groupCount = 1,
                         double groupPitch = 0.0) {
    Ink::StrokeRepeat r;
    r.shape = Ink::MarkShape::Line;
    r.mode  = Ink::MarkObjectMode::Fusion;
    r.sizePercent = false;
    r.size  = len;              // FULL length across the line
    r.width = thick;            // FULL thickness
    r.side  = side;
    r.sideOffset = offsetPct;  r.offsetPercent = true;   // % of stroke width
    r.rotation = rotation;
    r.distribute = Ink::RepeatDistribute::Pitch;
    r.pitch = pitch;
    r.groupCount = groupCount;
    r.groupPitch = groupPitch;
    r.lineJoin = true;
    return r;
}

// HALF-CIRCLES riding the LEFT of the line, flat side toward it (513.2
// retaining wall, 105.2 retaining earth wall — same construction, different
// colour and sizes). `diameter` is the full half-circle width; `pitch` is
// centre to centre; `offset` lifts them off the line; `trim` is the clear arc
// length kept at BOTH ends, measured from the bumps' OUTER edge.
Ink::StrokeRepeat HalfCircleRun(double diameter, double pitch, double offset,
                                double trim) {
    Ink::StrokeRepeat r;
    r.shape = Ink::MarkShape::HalfCircle;
    r.mode  = Ink::MarkObjectMode::Fusion;
    r.sizePercent = false;
    r.size  = diameter * 0.5;   // radius
    r.width = 0.0;              // unused by a half-circle
    r.side  = Ink::RepeatSide::Left;
    r.sideOffset = offset;  r.offsetPercent = false;
    r.distribute = Ink::RepeatDistribute::Pitch;
    r.pitch = pitch;
    r.startTrim = trim;  r.endTrim = trim;
    r.trimMeasure = Ink::RepeatTrimMeasure::Outside;
    return r;
}

// Cross-TIES straddling the line (railway / pylon bars). `len`/`thick` full.
Ink::StrokeRepeat TieRun(double len, double thick, double pitch) {
    Ink::StrokeRepeat r = TagRun(len, thick, pitch, Ink::RepeatSide::Center,
                                 0.0);
    r.lineJoin = false;
    return r;
}

// ── Instanced-fill builders (area patterns) ──────────────────────────────────

// A scattered field of one primitive shape (stony ground, boulder fields…).
Ink::Fill ScatterFill(Ink::InstShape shape, double sizeA, double sizeB,
                      double sizeC, Ink::Color col, double minDist,
                      double maxDist, double rotJitter,
                      Ink::MarkObjectMode mode = Ink::MarkObjectMode::Fusion) {
    Ink::Fill f;
    f.kind = Ink::FillKind::Instanced;
    f.paint.color = col;
    Ink::InstancedFill& in = f.instanced;
    in.layout = Ink::InstLayout::Scatter;
    in.scatterMode = Ink::InstScatterMode::Distance;
    in.scatterMinDist = minDist;
    in.scatterMaxDist = maxDist;
    in.avoidCollisions = true;
    in.rotJitter = rotJitter;
    in.clip = Ink::PatternClip::Contour;
    in.anchor = Ink::PatternAnchor::Object;
    Ink::InstElement e;
    e.shape = shape;
    e.sizeA = sizeA;  e.sizeB = sizeB;  e.sizeC = sizeC;
    e.mode  = mode;
    e.useFillColor = true;
    in.elements.push_back(e);
    return f;
}

// A regular grid of one primitive shape (cultivated land, orchards…).
Ink::Fill GridFill(Ink::InstShape shape, double sizeA, double sizeB,
                   Ink::Color col, double spacingX, double spacingY,
                   Ink::PatternAnchor anchor = Ink::PatternAnchor::Object) {
    Ink::Fill f;
    f.kind = Ink::FillKind::Instanced;
    f.paint.color = col;
    Ink::InstancedFill& in = f.instanced;
    in.layout = Ink::InstLayout::Grid;
    in.gridAxes = 2;
    in.spacing[0] = spacingX;
    in.spacing[1] = spacingY;
    in.clip = Ink::PatternClip::Contour;
    in.anchor = anchor;
    Ink::InstElement e;
    e.shape = shape;
    e.sizeA = sizeA;  e.sizeB = sizeB;  e.sizeC = sizeA;
    e.useFillColor = true;
    in.elements.push_back(e);
    return f;
}

// A parallel-line pattern (marsh hatch, vineyard rows, vegetation stripes).
Ink::Fill LinesFill(double angleRad, double spacing, double width,
                    Ink::Color col) {
    Ink::Fill f;
    f.kind = Ink::FillKind::Instanced;
    f.paint.color = col;
    Ink::InstancedFill& in = f.instanced;
    in.layout = Ink::InstLayout::Grid;   // layout irrelevant for pure line-sets
    in.clip = Ink::PatternClip::Contour;
    in.anchor = Ink::PatternAnchor::Object;
    Ink::InstLineSet l;
    l.angle = angleRad;
    l.spacing = spacing;
    l.line.width = width;
    l.useFillColor = true;
    in.lines.push_back(l);
    return f;
}

Ink::Fill Solid(Ink::Color col) {
    Ink::Fill f;
    f.paint.color = col;
    return f;
}

// ── Geometry helpers (mm, centred on the origin) ─────────────────────────────

// The ISOM "pit" V — 112 pit, 303 waterhole, 203.1 rocky pit share it exactly,
// only the colour and line width differ. `width` is the separation of the two
// TOP points, `height` the apex-to-top drop. Wound top-right → apex → top-left
// so an INSIDE stroke lands in the V's own opening and the quoted width/height
// stay the OUTER size.
Ink::PathData PitV(double width, double height) {
    const double hw = width * 0.5, hh = height * 0.5;
    return Ink::PathData::Polygon(
        { { hw, -hh }, { 0.0, hh }, { -hw, -hh } }, false);
}

// Its stroke: mitred apex, inside alignment, and a TILTED butt cap so both arm
// ends are cut HORIZONTAL. The arms mirror each other in their own frames, so
// the single angle below serves both ends (see Ink::Stroke::capAngle).
Ink::Stroke PitStroke(Ink::Color col, double strokeW,
                      double width, double height) {
    Ink::Stroke st = MakeStroke(col, strokeW);
    st.join     = Ink::JoinStyle::Miter;
    st.cap      = Ink::CapStyle::Butt;
    st.align    = Ink::StrokeAlign::Inside;
    st.capAngle = -std::atan2(width * 0.5, height);
    return st;
}

// Open half-circle arc, radius r: chord on y = 0 from (-r,0) to (r,0), the dome
// at (0, dir·r) — dir < 0 domes UP on screen (map north). Two quarter Béziers.
Ink::PathData HalfArc(double r, double dir) {
    const double s = dir < 0 ? -1.0 : 1.0;
    const double k = 0.5522847498307936 * r;
    Ink::PathData p; Ink::Subpath sp; sp.closed = false;
    Ink::Anchor a0; a0.pos = { -r, 0 }; a0.hasOut = true; a0.out = { 0, s * k };
    Ink::Anchor a1; a1.pos = { 0, s * r };
    a1.hasIn = true;  a1.in  = { -k, 0 };
    a1.hasOut = true; a1.out = {  k, 0 };
    Ink::Anchor a2; a2.pos = { r, 0 }; a2.hasIn = true; a2.in = { 0, s * k };
    sp.anchors = { a0, a1, a2 };
    p.subpaths.push_back(std::move(sp));
    return p;
}

// A multi-arm asterisk: `arms` straight strokes through the centre.
Ink::PathData Asterisk(double r, int arms) {
    Ink::PathData p;
    for (int i = 0; i < arms; ++i) {
        const double a = (3.14159265358979 * 2.0 * i) / (arms * 2);
        Ink::Subpath sp;
        sp.closed = false;
        Ink::Anchor a0; a0.pos = { std::cos(a) * r, std::sin(a) * r };
        Ink::Anchor a1; a1.pos = { -a0.pos.x, -a0.pos.y };
        sp.anchors = { a0, a1 };
        p.subpaths.push_back(std::move(sp));
    }
    return p;
}

// An X (two crossed strokes at 45°).
// An X of two crossed 45° strokes, each `len` long — so the pair are exactly
// the DIAGONALS of a square of side len/√2. Taking the length (not a radius)
// is what the ISOM sizes quote, and it makes a stacked pair easy to size: a
// halo 2×w longer than the mark it carries shows a w-wide border all round.
Ink::PathData XCross(double len) {
    const double h = len * 0.35355339059327376;   // (len / 2) · cos 45°
    Ink::PathData p;
    for (int i = 0; i < 2; ++i) {
        Ink::Subpath sp;
        sp.closed = false;
        const double sx = i == 0 ? 1.0 : -1.0;
        Ink::Anchor a0; a0.pos = { -h * sx, -h };
        Ink::Anchor a1; a1.pos = {  h * sx,  h };
        sp.anchors = { a0, a1 };
        p.subpaths.push_back(std::move(sp));
    }
    return p;
}

// √2, for the X sizes quoted as a square's diagonal.
constexpr double kSqrt2 = 1.4142135623730951;
// √3 — an equilateral of side √3·R has circumradius R (715 inscribes one).
constexpr double kSqrt3 = 1.7320508075688772;

// Equilateral triangle outline, side `side`, apex to the north (−y).
Ink::PathData TriangleN(double side) {
    const double h = side * 0.8660254037844387;
    return Ink::PathData::Polygon(
        { { 0.0, -h * 2.0 / 3.0 },
          { side * 0.5, h / 3.0 },
          { -side * 0.5, h / 3.0 } }, true);
}

// A "+" cross polygon (filled), arm length `r`, thickness `t`.
Ink::PathData PlusShape(double r, double t) {
    const double h = t * 0.5;
    return Ink::PathData::Polygon(
        { { -h, -r }, { h, -r }, { h, -h }, { r, -h }, { r, h }, { h, h },
          { h, r }, { -h, r }, { -h, h }, { -r, h }, { -r, -h }, { -h, -h } },
        true);
}

}  // namespace

Ink::Color InkColor(SpotColor c, float screenPct) {
    return ToInk(screenPct >= 0.999f ? SpotRgb(c) : ScreenRgb(c, screenPct));
}
Ink::Color LayerInkColor(PrintLayer layer) { return ToInk(LayerRenderColor(layer)); }

// ─────────────────────────────────────────────────────────────────────────────
//  BuildSymbol — the per-element definition.
// ─────────────────────────────────────────────────────────────────────────────

SymbolDef BuildSymbol(const IofElement& e, float scaleF) {
    // `s` converts an ISOM millimetre cote to BASE units (px) at map scale.
    const double s = (scaleF <= 0.0f ? 1.0 : (double)scaleF) * kPxPerMm;
    SymbolDef def;

    const Ink::Color ink    = InkColor(e.color);
    const Ink::Color black  = InkColor(SpotColor::Black);
    const Ink::Color white  = ToInk({ 1, 1, 1 });
    const Ink::Color blue   = InkColor(SpotColor::Blue);
    const Ink::Color green  = InkColor(SpotColor::Green);
    const Ink::Color brown  = InkColor(SpotColor::Brown);
    const Ink::Color yellow = InkColor(SpotColor::Yellow);
    const Ink::Color purple = InkColor(SpotColor::Purple);
    // Screen tints and the two composite inks that have their own print layer.
    const Ink::Color green60   = ToInk(ScreenRgb(SpotColor::Green, 0.60f));
    const Ink::Color green30   = ToInk(ScreenRgb(SpotColor::Green, 0.30f));
    const Ink::Color yellow75  = ToInk(ScreenRgb(SpotColor::Yellow, 0.75f));
    const Ink::Color yellow50  = ToInk(ScreenRgb(SpotColor::Yellow, 0.50f));
    const Ink::Color olive     = ToInk(LayerRenderColor(PrintLayer::Yellow100Green50));
    const Ink::Color darkGreen = ToInk(LayerRenderColor(PrintLayer::DarkGreenLine));

    auto part = [&](Ink::PathData p, Ink::Style st, const char* nm) {
        def.parts.push_back({ std::move(p), std::move(st), nm });
    };
    auto strokeStyle = [&](const Ink::Stroke& st) {
        Ink::Style sty;
        sty.strokes.push_back(st);
        return sty;
    };
    auto fillStyle = [&](Ink::Color c) { return Ink::Style::Filled(c); };
    // Pin a paint to a SPECIFIC plate when its colour cannot name it — the four
    // whites are all white, the road-outline and cultivated-land blacks are both
    // K100. The hint rides the swatch id and BindSwatches resolves it.
    auto plateFill = [](Ink::Fill f, PrintLayer l) {
        f.paint.swatch = IofPlateHint(l);
        for (Ink::InstElement& e : f.instanced.elements) e.swatch = IofPlateHint(l);
        for (Ink::InstLineSet& ln : f.instanced.lines)   ln.swatch = IofPlateHint(l);
        return f;
    };
    auto plateStroke = [](Ink::Stroke st, PrintLayer l) {
        st.paint.swatch = IofPlateHint(l);
        return st;
    };

    // ── LINE symbols: build lineStyle, specimen = a short sample segment ─────
    auto line = [&](Ink::Stroke st) {
        def.isLine = true;
        def.lineStyle.strokes.push_back(std::move(st));
    };
    // ── AREA symbols: build areaStyle, specimen = a small square swatch ──────
    auto area = [&](std::initializer_list<Ink::Fill> fills,
                    const Ink::Stroke* outline = nullptr) {
        def.isArea = true;
        for (const Ink::Fill& f : fills) def.areaStyle.fills.push_back(f);
        if (outline) def.areaStyle.strokes.push_back(*outline);
    };

    // Scattered-tree dots (402 / 404): 0.4 circles ADDED over the yellow on a
    // 0.70 diamond lattice, its axes at ∓45°. Only the dot colour changes
    // between the white and the green-60 variants.
    auto treeDots = [&](Ink::Color col) {
        Ink::Fill f = GridFill(Ink::InstShape::Circle, 0.2 * s, 0.2 * s, col,
                               0.70 * s, 0.70 * s);
        f.instanced.axisAngle[0] = -0.7853981633974483;   // −45°
        f.instanced.axisAngle[1] =  0.7853981633974483;   // +45°
        return f;
    };
    // Runnability stripes (406 / 408 / 410 variants): 0.4-wide lines 1.5 apart
    // running along the direction that IS runnable through the vegetation.
    auto runStripes = [&](Ink::Color col) {
        return LinesFill(1.5707963267948966, 1.5 * s, 0.4 * s, col);
    };

    switch (e.code) {
        // ── Landforms ────────────────────────────────────────────────────────
        case 1010: line(MakeStroke(brown, 0.14 * s)); break;
        case 1020: line(MakeStroke(brown, 0.25 * s)); break;
        case 1030: { Ink::Stroke st = MakeStroke(brown, 0.14 * s);
                     Dash(st, { 2.0 * s, 0.25 * s }); line(st); break; }
        case 1040: {   // Earth bank: 0.25 line + 0.40-long / 0.14 tags, 0.50 C-C
            Ink::Stroke st = MakeStroke(brown, 0.25 * s);
            st.repeats.push_back(TagRun(0.40 * s, 0.14 * s, 0.50 * s,
                                        Ink::RepeatSide::Right, 50.0));
            line(st); break;
        }
        case 1051: {   // Earth wall: 0.18 line + 0.45 dots astride it every 2.0,
                       // 0.77 clear from the outer dot edge at each end.
            Ink::Stroke st = MakeStroke(brown, 0.18 * s);
            st.repeats.push_back(DotRun(0.45 * s, 2.0 * s, 1, 0.0, 0.77 * s));
            line(st); break;
        }
        case 1060: {   // Ruined earth wall: 105.1 broken by 0.35 gaps. NO trim —
                       // a trim would pin the run to its own boundary and pull
                       // the dots off the dashes. Untrimmed, the run starts
                       // half a PITCH in, so this phase walks it back to half a
                       // DASH in: every dot lands on a dash centre.
            Ink::Stroke st = MakeStroke(brown, 0.18 * s);
            const double pitch = 2.0, gap = 0.35, dash = pitch - gap;
            Dash(st, { dash * s, gap * s });
            Ink::StrokeRepeat dots = DotRun(0.45 * s, pitch * s);
            dots.phase = (dash * 0.5 - pitch * 0.5) * s;
            st.repeats.push_back(dots);
            line(st); break;
        }
        case 1052: {   // Retaining earth wall: the 513.2 construction in brown —
                       // 0.18 line + 0.45 half-circles on the left at 1.0
                       // centre-to-centre, 0.07 off the line, 0.58 clear from
                       // the bumps' outer edge at each end.
            Ink::Stroke st = MakeStroke(brown, 0.18 * s);
            st.repeats.push_back(
                HalfCircleRun(0.45 * s, 1.0 * s, 0.07 * s, 0.58 * s));
            line(st); break;
        }
        case 1070: {   // erosion gully — tapers to a point at the end
            Ink::Stroke st = MakeStroke(brown, 0.35 * s);
            st.cap = Ink::CapStyle::Taper;
            st.taperLength = 1.2 * s;
            line(st); break;
        }
        case 1080: { Ink::Stroke st = MakeStroke(brown, 0.25 * s);
                     Dotted(st, 0.45 * s); line(st); break; }
        case 1090:
            part(Ink::PathData::Ellipse(0, 0, 0.25 * s, 0.25 * s),
                 fillStyle(brown), "Small knoll");
            break;
        case 1100:   // Small elongated knoll — 0.8 × 0.4 brown ellipse
            part(Ink::PathData::Ellipse(0, 0, 0.4 * s, 0.2 * s),
                 fillStyle(brown), "Elongated knoll");
            break;
        // 113 / 114 broken ground: brown dots scattered at a controlled centre
        // spacing. The distance band IS the density spec — a min/max centre gap
        // of 0.5/0.6 gives ISOM's 3-4 dots per mm², 0.25/0.38 gives 7-9 — and
        // the blue-noise scatter is what keeps them from lining up into a
        // one-dot-wide row, which the spec forbids.
        case 1130:
            area({ ScatterFill(Ink::InstShape::Circle, 0.1 * s, 0, 0, brown,
                               0.5 * s, 0.6 * s, 0.0) });
            break;
        case 1140:
            area({ ScatterFill(Ink::InstShape::Circle, 0.1 * s, 0, 0, brown,
                               0.25 * s, 0.38 * s, 0.0) });
            break;
        case 1110: {  // Small depression — half-circle, dome UP (opening south)
            Ink::Stroke st = MakeStroke(brown, 0.18 * s);
            st.align = Ink::StrokeAlign::Inside;
            part(HalfArc(0.40 * s, 1.0), strokeStyle(st),
                 "Depression");
            break;
        }
        case 1120:   // Pit — 0.7 × 0.8 V, mitred apex, horizontal arm ends
            part(PitV(0.7 * s, 0.8 * s),
                 strokeStyle(PitStroke(brown, 0.18 * s, 0.7 * s, 0.8 * s)),
                 "Pit");
            break;
        case 1150: {   // Prominent landform — 0.9 mm equilateral brown triangle
            Ink::Stroke st = MakeStroke(brown, 0.18 * s);
            st.join  = Ink::JoinStyle::Miter;
            st.align = Ink::StrokeAlign::Inside;   // 0.9 mm is the OUTER side
            part(TriangleN(0.9 * s), strokeStyle(st), "Landform");
            break;
        }

        // ── Rock ────────────────────────────────────────────────────────────
        case 2010: {   // Impassable cliff: 0.35 line + 0.40/0.12 tags, 0.50 C-C
            Ink::Stroke st = MakeStroke(black, 0.35 * s);
            st.cap = Ink::CapStyle::Round;
            st.repeats.push_back(TagRun(0.40 * s, 0.12 * s, 0.50 * s,
                                        Ink::RepeatSide::Right, 50.0));
            line(st); break;
        }
        case 2020: {   // Cliff: 0.25 line + 0.40/0.12 tags, 0.50 C-C
            Ink::Stroke st = MakeStroke(black, 0.25 * s);
            st.repeats.push_back(TagRun(0.40 * s, 0.12 * s, 0.50 * s,
                                        Ink::RepeatSide::Right, 50.0));
            line(st); break;
        }
        case 2150: {   // Trench: black 0.30 back + a 0.10 ERASE line down the
                       // middle. The centre is a real gap that shows the ground
                       // under the trench — transparent, not a white line laid
                       // over it. The node isolates (a blend piece), so the
                       // erase cuts only the trench's own black.
            def.isLine = true;
            Ink::Stroke back = MakeStroke(black, 0.30 * s);
            back.join = Ink::JoinStyle::Miter;
            Ink::Stroke front = MakeStroke(white, 0.10 * s);
            front.join = Ink::JoinStyle::Miter;
            front.blend = Ink::BlendMode::Erase;
            def.lineStyle.strokes.push_back(back);
            def.lineStyle.strokes.push_back(front);
            break;
        }
        case 2031:   // Rocky pit or cave — the pit V in black, 0.16 mm
            part(PitV(0.7 * s, 0.8 * s),
                 strokeStyle(PitStroke(black, 0.16 * s, 0.7 * s, 0.8 * s)),
                 "Rocky pit");
            break;
        case 2040:
            part(Ink::PathData::Ellipse(0, 0, 0.20 * s, 0.20 * s),
                 fillStyle(black), "Boulder");
            break;
        case 2050:
            part(Ink::PathData::Ellipse(0, 0, 0.30 * s, 0.30 * s),
                 fillStyle(black), "Large boulder");
            break;
        case 2032: {   // Dangerous pit — 0.9 black ring, stroke INSIDE so 0.9
                       // is the outer diameter, 0.35 thick.
            Ink::Stroke st = MakeStroke(black, 0.35 * s);
            st.align = Ink::StrokeAlign::Inside;
            part(Ink::PathData::Ellipse(0, 0, 0.45 * s, 0.45 * s),
                 strokeStyle(st), "Dangerous pit");
            break;
        }
        case 2060:   // Gigantic boulder or rock pillar — plain black area
            area({ Solid(black) });
            break;
        case 2070: {
            Ink::PathData tri = TriangleN(0.8 * s);
            part(std::move(tri), fillStyle(black), "Boulder cluster");
            break;
        }
        case 2071: {   // The same cluster enlarged to 120 % (0.96 edge), which
                       // the spec allows for some clusters — kept as its own
                       // entry so both sizes are placeable.
            Ink::PathData tri = TriangleN(0.96 * s);
            part(std::move(tri), fillStyle(black), "Boulder cluster 120%");
            break;
        }
        case 2080:
            area({ ScatterFill(Ink::InstShape::Triangle, 0.75 * s, 0.6 * s,
                               0.6 * s, black, 1.2 * s, 2.2 * s, 0.6) });
            break;
        case 2100:
            area({ ScatterFill(Ink::InstShape::Circle, 0.105 * s, 0, 0, black,
                               0.55 * s, 1.0 * s, 0.0) });
            break;
        case 2130: {   // Sandy ground: 0.16 black dots on a REGULAR lattice, its
                       // two axes at ∓45° and 0.45 apart — a diamond grid.
            Ink::Fill dots = GridFill(Ink::InstShape::Circle, 0.08 * s, 0,
                                      black, 0.45 * s, 0.45 * s);
            dots.instanced.axisAngle[0] = -0.7853981633974483;   // −45°
            dots.instanced.axisAngle[1] =  0.7853981633974483;   // +45°
            area({ Solid(InkColor(SpotColor::Yellow, 0.5f)), dots });
            break;
        }
        case 2140: area({ Solid(ToInk(ScreenRgb(SpotColor::Black, 0.30f))) }); break;

        // ── Water ───────────────────────────────────────────────────────────
        case 3010: { Ink::Stroke bank = MakeStroke(black, 0.18 * s);
                     area({ Solid(blue) }, &bank); break; }
        case 3020: { Ink::Stroke bank = MakeStroke(blue, 0.18 * s);
                     area({ Solid(ToInk(ScreenRgb(SpotColor::Blue, 0.70f))) },
                          &bank); break; }
        case 3040: line(MakeStroke(blue, 0.30 * s)); break;
        case 3050: line(MakeStroke(blue, 0.18 * s)); break;
        case 3060: { Ink::Stroke st = MakeStroke(blue, 0.18 * s);
                     Dash(st, { 1.25 * s, 0.25 * s }); line(st); break; }
        case 3070: {   // Uncrossable marsh: BLACK 0.12 outline over blue lines
                       // 0.25 wide, 0.5 centre-to-centre.
            Ink::Stroke bank = MakeStroke(black, 0.12 * s);
            area({ LinesFill(0.0, 0.5 * s, 0.25 * s, blue) }, &bank);
            break;
        }
        case 3080:   // Marsh: blue lines 0.1 wide, 0.3 centre-to-centre
            area({ LinesFill(0.0, 0.3 * s, 0.10 * s, blue) });
            break;
        case 3090: { Ink::Stroke st = MakeStroke(blue, 0.25 * s);
                     Dotted(st, 0.45 * s); line(st); break; }
        case 3030:   // Waterhole — the pit V in blue, 0.18 mm
            part(PitV(0.7 * s, 0.8 * s),
                 strokeStyle(PitStroke(blue, 0.18 * s, 0.7 * s, 0.8 * s)),
                 "Waterhole");
            break;
        case 3100: {   // Indistinct marsh — 0.10 blue dashed horizontal lines,
                       // 0.90/0.25 dash, 0.30 centre-to-centre, each row half a
                       // dash period out of step with the previous one.
            Ink::Fill f = LinesFill(0.0, 0.30 * s, 0.10 * s, blue);
            f.instanced.lines[0].spacingMode = Ink::InstLineSpacing::Center;
            f.instanced.lines[0].line.dashPattern = { 0.90 * s, 0.25 * s };
            f.instanced.lines[0].stagger = 0.5;
            area({ f });
            break;
        }
        case 3110: {   // Well — 0.8 mm blue square, miter corners, stroke inside
            Ink::Stroke st = MakeStroke(blue, 0.18 * s);
            st.join  = Ink::JoinStyle::Miter;
            st.align = Ink::StrokeAlign::Inside;   // 0.8 mm is the OUTER side
            part(Ink::PathData::Rect(-0.4 * s, -0.4 * s, 0.8 * s, 0.8 * s),
                 strokeStyle(st), "Well");
            break;
        }
        case 3120: {  // Spring — half-circle, dome UP (opening south)
            Ink::Stroke st = MakeStroke(blue, 0.18 * s);
            st.align = Ink::StrokeAlign::Inside;
            part(HalfArc(0.45 * s, 1.0), strokeStyle(st),
                 "Spring");
            break;
        }
        case 3130:   // Prominent water — 5 arms at 72°, top one vertical,
                     // each 0.9 mm long, 0.16 mm wide.
            part(RayStar(0.45 * s, 5), strokeStyle(MakeStroke(blue, 0.16 * s)),
                 "Water feature");
            break;

        // ── Vegetation ───────────────────────────────────312───────────────────
        case 4010: area({ Solid(yellow) }); break;
        // 402 / 404 — scattered trees: the dots are PAINTED over the yellow,
        // not cut out of it, so the green-dot variant is possible at all.
        case 4020: area({ Solid(yellow75),
                          plateFill(treeDots(white), PrintLayer::WhiteOverYellow) }); break;
        case 4021: area({ Solid(yellow75), treeDots(green60) }); break;
        case 4040: area({ Solid(yellow50),
                          plateFill(treeDots(white), PrintLayer::WhiteOverYellow) }); break;
        case 4041: area({ Solid(yellow50), treeDots(green60) }); break;
        case 4030: area({ Solid(yellow50) }); break;
        case 4050: area({ plateFill(Solid(white),
                                    PrintLayer::WhiteOverGreenBrown) }); break;
        // 406 / 408 / 410 — vegetation tints, each with optional runnability
        // stripes marking the direction that stays runnable.
        case 4060: area({ Solid(green30) }); break;
        case 4061: area({ Solid(green30),
                          plateFill(runStripes(white), PrintLayer::WhiteOverGreenBrown) }); break;
        case 4080: area({ Solid(green60) }); break;
        case 4081: area({ Solid(green60),
                          plateFill(runStripes(white), PrintLayer::WhiteOverGreenBrown) }); break;
        case 4082: area({ Solid(green60), runStripes(green30) }); break;
        case 4100: area({ Solid(green) }); break;
        case 4101: area({ Solid(green),
                          plateFill(runStripes(white), PrintLayer::WhiteOverGreenBrown) }); break;
        case 4102: area({ Solid(green), runStripes(green30) }); break;
        case 4103: area({ Solid(green), runStripes(green60) }); break;
        case 4070:
            // Vertical green stripes on white (good visibility).
            area({ LinesFill(1.5707963267948966, 1.0 * s, 0.4 * s, green) });
            break;
        case 4120:
            area({ Solid(yellow),
                   plateFill(GridFill(Ink::InstShape::Circle, 0.10 * s, 0.10 * s,
                                      black, 0.8 * s, 0.8 * s,
                                      Ink::PatternAnchor::Document),
                             PrintLayer::BlackCultivated) });
            break;
        // 413 orchard — the same green dot grid over full or half yellow.
        case 4130:
        case 4131:
            area({ Solid(e.code == 4130 ? yellow : yellow50),
                   GridFill(Ink::InstShape::Circle, 0.20 * s, 0.20 * s, green,
                            0.8 * s, 0.8 * s, Ink::PatternAnchor::Document) });
            break;
        // 414 vineyard — 0.2 rows 0.85 apart, dashed 1.3 / 0.6, every other row
        // half a period out of step; over full or half yellow.
        case 4140:
        case 4141: {
            Ink::Fill rows =
                LinesFill(1.5707963267948966, 0.85 * s, 0.2 * s, green);
            rows.instanced.lines[0].line.dashPattern = { 1.3 * s, 0.6 * s };
            rows.instanced.lines[0].stagger = 0.5;
            area({ Solid(e.code == 4140 ? yellow : yellow50), rows });
            break;
        }
        case 4150: line(MakeStroke(black, 0.10 * s)); break;
        case 4160: { Ink::Stroke st = MakeStroke(black, 0.22 * s);
                     Dotted(st, 0.45 * s); line(st); break; }
        case 4161: { Ink::Stroke st = MakeStroke(darkGreen, 0.14 * s);
                     Dash(st, { 0.3 * s, 0.2 * s }); line(st); break; }
        // 417 large tree — 0.9 green ring, stroke INSIDE so 0.9 is the outer
        // size; the .1 variant sits on a 1.1 white disc that clears whatever
        // vegetation tint is underneath.
        case 4170: {   // 417 — always a white disc behind the
                       // green ring so the tree reads on any vegetation. The
                       // disc knocks out green and brown; the two former
                       // variants (with / without backing) are merged.
            Ink::Stroke ring = MakeStroke(green, 0.18 * s);
            ring.align = Ink::StrokeAlign::Inside;
            Ink::Style backing = fillStyle(white);
            backing.fills[0].paint.swatch =
                IofPlateHint(PrintLayer::WhiteOverGreenBrown);
            part(Ink::PathData::Ellipse(0, 0, 0.55 * s, 0.55 * s),
                 backing, "Large tree backing");
            part(Ink::PathData::Ellipse(0, 0, 0.45 * s, 0.45 * s),
                 strokeStyle(ring), "Large tree");
            break;
        }
        case 4180: {   // Prominent bush — a 0.6 green RING (inside, 0.2 thick)
                       // over a 0.4 white disc, not a solid green dot.
            Ink::Stroke ring = MakeStroke(green, 0.2 * s);
            ring.align = Ink::StrokeAlign::Inside;
            Ink::Style bushBk = fillStyle(white);
            bushBk.fills[0].paint.swatch =
                IofPlateHint(PrintLayer::WhiteOverGreenBrown);
            part(Ink::PathData::Ellipse(0, 0, 0.2 * s, 0.2 * s),
                 bushBk, "Bush backing");
            part(Ink::PathData::Ellipse(0, 0, 0.3 * s, 0.3 * s),
                 strokeStyle(ring), "Bush");
            break;
        }
        case 4190: {
            // The WHITE halo spans the diagonals of a 0.9 square; the GREEN
            // cross over it is one white width shorter at each end, so the halo
            // shows all round and the symbol stays legible on green. The halo
            // is added first — it renders underneath.
            const double halo = 0.9 * kSqrt2;
            part(XCross(halo * s),
                 strokeStyle(plateStroke(MakeStroke(white, 0.36 * s),
                                         PrintLayer::WhiteOverGreenBrown)),
                 "Vegetation X halo");
            part(XCross((halo - 0.18) * s),
                 strokeStyle(MakeStroke(green, 0.18 * s)), "Vegetation X");
            break;
        }

        // ── Man-made ────────────────────────────────────────────────────────
        case 5010: { Ink::Stroke edge =
                         plateStroke(MakeStroke(black, 0.12 * s),
                                     PrintLayer::Black100RoadOutline);
                     area({ Solid(ToInk(ScreenRgb(SpotColor::Brown, 0.5f))) },
                          &edge); break; }
        case 5020: {
            // Two black edges + brown 50 % infill: a wide black stroke UNDER a
            // narrower brown one (multi-stroke) — reads as 0.12 mm edges.
            def.isLine = true;
            def.lineStyle.strokes.push_back(
                plateStroke(MakeStroke(black, 0.74 * s),
                            PrintLayer::Black100RoadOutline));
            def.lineStyle.strokes.push_back(
                MakeStroke(ToInk(ScreenRgb(SpotColor::Brown, 0.5f)), 0.50 * s));
            break;
        }
        case 5030: line(MakeStroke(black, 0.35 * s)); break;
        case 5040: { Ink::Stroke st = MakeStroke(black, 0.35 * s);
                     Dash(st, { 3.0 * s, 0.25 * s }); line(st); break; }
        case 5050: { Ink::Stroke st = MakeStroke(black, 0.25 * s);
                     Dash(st, { 2.0 * s, 0.25 * s }); line(st); break; }
        case 5060: { Ink::Stroke st = MakeStroke(black, 0.18 * s);
                     Dash(st, { 1.0 * s, 0.25 * s }); line(st); break; }
        case 5070: { Ink::Stroke st = MakeStroke(black, 0.18 * s);
                     Dash(st, { 1.0 * s, 0.25 * s, 1.0 * s, 0.8 * s });
                     line(st); break; }
        // 508 narrow ride — the dashed black trace alone, or over a 0.45 band
        // of the vegetation it cuts through, so the ride reads as a clearing.
        case 5080:
        case 5081:
        case 5082:
        case 5083:
        case 5084: {
            def.isLine = true;
            if (e.code != 5080) {
                const Ink::Color bg = e.code == 5081 ? yellow
                                    : e.code == 5082 ? green30
                                    : e.code == 5083 ? green60 : white;
                def.lineStyle.strokes.push_back(MakeStroke(bg, 0.45 * s));
            }
            Ink::Stroke st = MakeStroke(black, 0.14 * s);
            Dash(st, { 2.0 * s, 0.25 * s });
            def.lineStyle.strokes.push_back(std::move(st));
            break;
        }
        case 5200: {   // Area that shall not be entered — olive (yellow 100 % +
                       // green 50 %) inside a 0.18 black edge.
            Ink::Stroke edge = MakeStroke(black, 0.18 * s);
            area({ Solid(olive) }, &edge);
            break;
        }
        case 5090: {   // Railway: black 0.45 (back) + white dashed 1.0/1.5 (front)
            def.isLine = true;
            def.lineStyle.strokes.push_back(MakeStroke(black, 0.45 * s));
            Ink::Stroke dash = plateStroke(MakeStroke(white, 0.25 * s),
                                           PrintLayer::WhiteRailway);
            Dash(dash, { 1.0 * s, 1.5 * s });
            def.lineStyle.strokes.push_back(dash);
            break;
        }
        case 5100: { Ink::Stroke st = MakeStroke(black, 0.14 * s);
                     st.repeats.push_back(TieRun(0.40 * s, 0.05 * s, 5.0 * s));
                     line(st); break; }
        case 5110: {
            def.isLine = true;
            Ink::Stroke base = MakeStroke(black, 0.40 * s);
            base.repeats.push_back(TieRun(0.55 * s, 0.06 * s, 5.0 * s));
            def.lineStyle.strokes.push_back(base);
            def.lineStyle.strokes.push_back(MakeStroke(white, 0.24 * s));
            break;
        }
        case 5131: {   // Wall: 0.14 line + 0.4 dots astride, 2.0 C-C
            Ink::Stroke st = MakeStroke(black, 0.14 * s);
            st.repeats.push_back(DotRun(0.4 * s, 2.0 * s));
            line(st); break;
        }
        case 5132: {   // Retaining wall: black line + 0.4 half-circles on the
                       // left at 1.0 centre-to-centre, 0.05 off the line,
                       // 0.6 clear at each end.
            Ink::Stroke st = MakeStroke(black, 0.18 * s);
            st.repeats.push_back(
                HalfCircleRun(0.4 * s, 1.0 * s, 0.05 * s, 0.6 * s));
            line(st); break;
        }
        case 5140: {   // Ruined wall: the 106 construction in black — 0.35 gaps,
                       // no trim, each 0.4 dot on a dash CENTRE.
            Ink::Stroke st = MakeStroke(black, 0.14 * s);
            const double pitch = 2.0, gap = 0.35, dash = pitch - gap;
            Dash(st, { dash * s, gap * s });
            Ink::StrokeRepeat dots = DotRun(0.4 * s, pitch * s);
            dots.phase = (dash * 0.5 - pitch * 0.5) * s;
            st.repeats.push_back(dots);
            line(st); break;
        }
        case 5150: {   // Impassable wall: 0.25 line + 0.6 dots in groups of 2
                       // (0.8 intra-group, 3.0 inter-group C-C)
            Ink::Stroke st = MakeStroke(black, 0.25 * s);
            st.repeats.push_back(DotRun(0.6 * s, 3.0 * s, 2, 0.8 * s));
            line(st); break;
        }
        case 5160: {   // Fence: 0.14 line + pickets inclined 30° from the normal
            Ink::Stroke st = MakeStroke(black, 0.14 * s);
            st.repeats.push_back(TagRun(0.40 * s, 0.14 * s, 2.0 * s,
                                        Ink::RepeatSide::Left, 50.0, 0.5235987755982988));
            line(st); break;
        }
        case 5170: {   // Ruined fence: 516 broken by 0.35 gaps. The dash is the
                       // rest of the 2.0 tag pitch, and the tags are phased to
                       // land on the dash CENTRES rather than in the gaps.
            Ink::Stroke st = MakeStroke(black, 0.14 * s);
            const double pitch = 2.0, gap = 0.35, dash = pitch - gap;
            Dash(st, { dash * s, gap * s });
            Ink::StrokeRepeat tags = TagRun(0.40 * s, 0.14 * s, pitch * s,
                                            Ink::RepeatSide::Left, 50.0, 0.5235987755982988);
            // The run's first tag would sit half a pitch in; the first dash's
            // centre is half a DASH in, so shift by the difference.
            tags.phase = (dash * 0.5 - pitch * 0.5) * s;
            st.repeats.push_back(tags);
            line(st); break;
        }
        case 5180: {   // Impassable fence: 0.25 line + double pickets at 30°
            Ink::Stroke st = MakeStroke(black, 0.25 * s);
            st.repeats.push_back(TagRun(0.40 * s, 0.14 * s, 2.5 * s,
                                        Ink::RepeatSide::Left, 50.0, 0.5235987755982988,
                                        2, 0.6 * s));
            line(st); break;
        }
        case 5210: area({ Solid(black) }); break;
        case 5211: {   // Large building / tramway: black 50% area, 0.1 black edge.
            Ink::Stroke edge = MakeStroke(black, 0.1 * s);
            area({ Solid(LayerInkColor(PrintLayer::Black50)) }, &edge);
            break;
        }
        case 5220: { Ink::Stroke edge = MakeStroke(black, 0.1 * s);
                     area({ Solid(ToInk(ScreenRgb(SpotColor::Black, 0.20f))) },
                          &edge); break; }
        case 5240: {
            part(Ink::PathData::Ellipse(0, 0, 0.40 * s, 0.40 * s),
                 fillStyle(black), "Tower");
            part(Asterisk(0.75 * s, 2), strokeStyle(MakeStroke(black, 0.14 * s)),
                 "Arms");
            break;
        }
        case 5230: {   // Ruin (area): no fill, just a dashed black edge.
            Ink::Stroke edge = MakeStroke(black, 0.16 * s);
            edge.cap = Ink::CapStyle::Butt;
            edge.join = Ink::JoinStyle::Miter;
            Dash(edge, { 0.5 * s, 0.25 * s });
            def.isArea = true;
            def.areaStyle.strokes.push_back(std::move(edge));
            break;
        }
        case 5231: {   // Ruin (point): a 0.8 black square, stroke inside so the
                       // 0.8 is the outer size.
            Ink::Stroke st = MakeStroke(black, 0.16 * s);
            st.join  = Ink::JoinStyle::Miter;
            st.align = Ink::StrokeAlign::Inside;
            part(Ink::PathData::Rect(-0.4 * s, -0.4 * s, 0.8 * s, 0.8 * s),
                 strokeStyle(st), "Ruin");
            break;
        }
        case 5250: {   // Small tower: a T — a 1.0 stem hanging from the centre
                       // of a 1.0 cross-bar. The bar is stroked to ONE side so
                       // its band sits below its construction line (wound right
                       // to left, which is the side Left resolves to).
            Ink::Stroke stem = MakeStroke(black, 0.16 * s);
            Ink::Stroke bar  = MakeStroke(black, 0.16 * s);
            bar.align = Ink::StrokeAlign::Left;
            part(Ink::PathData::Polygon({ { 0.0, -0.5 * s }, { 0.0, 0.5 * s } },
                                        false),
                 strokeStyle(stem), "Tower stem");
            part(Ink::PathData::Polygon({ { 0.5 * s, -0.5 * s },
                                          { -0.5 * s, -0.5 * s } }, false),
                 strokeStyle(bar), "Tower bar");
            break;
        }
        case 5260: {   // Cairn: 0.14 centre dot inside a 0.8 ring, the ring
                       // stroked inside so 0.8 is its outer diameter.
            Ink::Stroke ring = MakeStroke(black, 0.16 * s);
            ring.align = Ink::StrokeAlign::Inside;
            part(Ink::PathData::Ellipse(0, 0, 0.40 * s, 0.40 * s),
                 strokeStyle(ring), "Ring");
            part(Ink::PathData::Ellipse(0, 0, 0.07 * s, 0.07 * s),
                 fillStyle(black), "Dot");
            break;
        }
        case 5270: {   // Fodder rack: a 0.9 stem under a Λ whose two arms drop
                       // 30° below horizontal, their ends 0.9 apart — so the
                       // whole glyph fits a 0.9 square. The Λ is ONE broken
                       // line stroked INSIDE, which puts its band on the
                       // concave (lower) side of both arms at once.
            const double hw = 0.45 * s;              // half the end separation
            const double drop = hw * 0.5773502691896258;   // tan 30°
            const double topY = -0.45 * s;
            const double lw = 0.16 * s;
            Ink::Stroke stem = MakeStroke(black, lw);
            Ink::Stroke arms = MakeStroke(black, lw);
            arms.join  = Ink::JoinStyle::Miter;
            arms.align = Ink::StrokeAlign::Inside;
            // The stem is stroked CENTRED, so starting it on the apex would
            // push its two top corners half a width ABOVE the arms' band. Drop
            // its top by exactly that half width and they tuck under instead —
            // the apex itself, and so the 0.9 square, are unchanged.
            part(Ink::PathData::Polygon({ { 0.0, topY + lw * 0.5 },
                                          { 0.0, 0.45 * s } }, false),
                 strokeStyle(stem), "Rack stem");
            part(Ink::PathData::Polygon({ {  hw, topY + drop },
                                          { 0.0, topY },
                                          { -hw, topY + drop } }, false),
                 strokeStyle(arms), "Rack arms");
            break;
        }
        case 5280:
        case 5290: {   // Prominent (uncrossable) line feature: a black line with
                       // 0.4 tags leaning 45°, MIRRORED — one run on each side,
                       // so the pair reads symmetrically. 529 doubles the tags
                       // and thickens the line.
            const bool big = e.code == 5290;
            Ink::Stroke st = MakeStroke(black, big ? 0.25 * s : 0.14 * s);
            const int    gN = big ? 2 : 1;
            const double gP = big ? 0.6 * s : 0.0;
            st.repeats.push_back(TagRun(0.4 * s, 0.14 * s, 2.0 * s,
                                        Ink::RepeatSide::Left, 50.0,
                                        0.7853981633974483, gN, gP));
            st.repeats.push_back(TagRun(0.4 * s, 0.14 * s, 2.0 * s,
                                        Ink::RepeatSide::Right, 50.0,
                                        -0.7853981633974483, gN, gP));
            line(st); break;
        }
        case 5300:
            part(Ink::PathData::Ellipse(0, 0, 0.40 * s, 0.40 * s),
                 strokeStyle(MakeStroke(black, 0.16 * s)), "Feature ring");
            break;
        case 5310:
            part(XCross((0.8 * kSqrt2 - 0.32) * s),
                 strokeStyle(MakeStroke(black, 0.16 * s)), "Feature X");
            break;

        // ── Technical ───────────────────────────────────────────────────────
        case 6010: line(MakeStroke(black, 0.10 * s)); break;
        case 6011: line(MakeStroke(blue,  0.12 * s)); break;
        case 6020:
            part(PlusShape(2.0 * s, 0.1 * s), fillStyle(black), "Registration");
            break;
        case 6030:
            part(Ink::PathData::Ellipse(0, 0, 0.15 * s, 0.15 * s),
                 fillStyle(black), "Spot height");
            break;

        // ── Course overprint ────────────────────────────────────────────────
        case 7010: {   // Start: 6.0-side equilateral triangle, mitred corners,
                       // stroke centred on the construction line.
            Ink::Stroke st = MakeStroke(purple, 0.35 * s);
            st.join = Ink::JoinStyle::Miter;
            part(TriangleN(6.0 * s), strokeStyle(st), "Start");
            break;
        }
        case 7020:   // Map issue point: a 2.5 × 0.6 purple bar
            part(Ink::PathData::Rect(-1.25 * s, -0.3 * s, 2.5 * s, 0.6 * s),
                 fillStyle(purple), "Map issue");
            break;
        case 7030:   // Control point: 5.0 circle, stroke centred
            part(Ink::PathData::Ellipse(0, 0, 2.5 * s, 2.5 * s),
                 strokeStyle(MakeStroke(purple, 0.35 * s)), "Control");
            break;
        case 7050: line(MakeStroke(purple, 0.35 * s)); break;
        case 7060:   // Finish: 4.0 inside 6.0, both stroked centred
            part(Ink::PathData::Ellipse(0, 0, 2.0 * s, 2.0 * s),
                 strokeStyle(MakeStroke(purple, 0.35 * s)), "Inner");
            part(Ink::PathData::Ellipse(0, 0, 3.0 * s, 3.0 * s),
                 strokeStyle(MakeStroke(purple, 0.35 * s)), "Outer");
            break;
        case 7070: {   // Marked route: dashed 2.0 / 0.5
            Ink::Stroke st = MakeStroke(purple, 0.35 * s);
            Dash(st, { 2.0 * s, 0.5 * s });
            line(st); break;
        }
        case 7080: { Ink::Stroke st = MakeStroke(purple, 0.70 * s);
                     st.join = Ink::JoinStyle::Miter;
                     line(st); break; }
        case 7090:
        case 7091:
        case 7092: {   // Out-of-bounds area: purple cross-hatch at ∓45°, 1.2
                       // apart each way. .0 solid edge, .1 dashed edge, .2 none.
            Ink::Fill hatch =
                LinesFill(-0.7853981633974483, 1.2 * s, 0.2 * s, purple);
            Ink::InstLineSet back = hatch.instanced.lines[0];
            back.angle = 0.7853981633974483;
            hatch.instanced.lines.push_back(back);
            if (e.code == 7092) { area({ hatch }); break; }
            Ink::Stroke edge = MakeStroke(purple, 0.25 * s);
            if (e.code == 7091) Dash(edge, { 3.0 * s, 0.5 * s });
            area({ hatch }, &edge);
            break;
        }
        case 7150: {   // Continuing point after a map exchange: the 5.0 control
                       // circle with an equilateral triangle whose three points
                       // sit ON that circle (apex up).
            Ink::Stroke st = MakeStroke(purple, 0.35 * s);
            part(Ink::PathData::Ellipse(0, 0, 2.5 * s, 2.5 * s),
                 strokeStyle(st), "Continue ring");
            Ink::Stroke tri = st;
            tri.join = Ink::JoinStyle::Miter;
            // An equilateral of side √3·R has circumradius R.
            part(TriangleN(2.5 * kSqrt3 * s), strokeStyle(tri),
                 "Continue triangle");
            break;
        }
        case 7120:
            part(PlusShape(1.5 * s, 0.9 * s), fillStyle(purple), "First aid");
            break;

        default: break;
    }

    // Generic fallbacks so every catalogue entry has SOMETHING sensible.
    if (def.parts.empty() && !def.isLine && !def.isArea) {
        switch (e.type) {
            case IofType::Point:
                part(Ink::PathData::Ellipse(0, 0, 0.3 * s, 0.3 * s),
                     fillStyle(ink), "Point");
                break;
            case IofType::Line: line(MakeStroke(ink, 0.18 * s)); break;
            case IofType::Area: case IofType::Text:
                area({ Solid(ink) });
                break;
        }
    }

    // Line / area SPECIMENS: a SHORT sample segment (so the vignette zooms in
    // on the pattern, like the core stroke previews) / a small swatch, drawn
    // through the real pipeline. Lines are kept short so a thin ISOM stroke
    // (0.14 mm) still reads at the tile size — the pattern shows ~1-2 periods.
    if (def.isLine && def.parts.empty()) {
        Ink::PathData seg = Ink::PathData::Polygon(
            { { -1.5 * s, 0.0 }, { 1.5 * s, 0.0 } }, false);
        def.parts.push_back({ std::move(seg), def.lineStyle, "Sample" });
    }
    if (def.isArea && def.parts.empty()) {
        Ink::PathData sq =
            Ink::PathData::Rect(-3.5 * s, -3.5 * s, 7.0 * s, 7.0 * s);
        def.parts.push_back({ std::move(sq), def.areaStyle, "Swatch" });
    }

    // Course-planning symbols are entirely purple, but WHICH purple depends on
    // the symbol. Upper purple always stays at the very top of the print stack,
    // so the markings that must never be covered by other purple — map issue
    // (702), marked route (707), out-of-bounds (709), forbidden route (711) —
    // sit there; the rest (start, control, finish, crossing point, first aid…)
    // sit on lower purple. The two plates render the same purple, so the paint
    // has to name the plate; 715 (continuing point) is control-like → lower.
    {
        const int sym = e.code / 10;
        if (sym >= 701 && sym <= 715) {
            const bool upper = sym == 702 || sym == 707 ||
                               sym == 709 || sym == 711;
            const PrintLayer pl = upper ? PrintLayer::UpperPurple
                                        : PrintLayer::LowerPurple;
            auto stamp = [&](Ink::Style& st) {
                for (Ink::Fill& f : st.fills) {
                    f.paint.swatch = IofPlateHint(pl);
                    for (Ink::InstElement& ie : f.instanced.elements)
                        ie.swatch = IofPlateHint(pl);
                    for (Ink::InstLineSet& il : f.instanced.lines)
                        il.swatch = IofPlateHint(pl);
                }
                for (Ink::Stroke& s2 : st.strokes) {
                    s2.paint.swatch = IofPlateHint(pl);
                    for (Ink::StrokeRepeat& rp : s2.repeats)
                        rp.swatch = IofPlateHint(pl);
                    for (Ink::StrokeMark& m : s2.marks)
                        for (Ink::MarkObject& o : m.objects)
                            o.swatch = IofPlateHint(pl);
                }
            };
            for (SymbolPart& pt : def.parts) stamp(pt.style);
            stamp(def.lineStyle);
            stamp(def.areaStyle);
        }
    }
    return def;
}

}  // namespace App::Modules::IofMapping

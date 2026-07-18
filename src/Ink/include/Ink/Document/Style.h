#pragma once

#include "Ink/Document/Types.h"
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Style — unified fill + stroke (docs/Ink/DOCUMENT_MODEL.md §4): a node
//  carries ordered LISTS of fills and strokes; both take any Paint. A stroke
//  is geometry generation (GEOMETRY.md §2) painted by the same machinery as a
//  fill — there is no technical shape/stroke split.
//
//  Lot 2 scope: solid paints, Center stroke alignment, Butt caps, Bevel-ish
//  joins. Inside/Outside, Round/Square caps, Miter joins, dashes and
//  viewport-space widths complete in Lot 3; Pattern/Image/Gradient paints in
//  Lots 5/6/later. The enums already carry the full vocabulary so documents
//  built now stay valid.
// ─────────────────────────────────────────────────────────────────────────────

// Solid color paint (LINEAR-light, straight alpha — premultiplication happens
// when the scene builds the GPU paint table).
struct Paint {
    Color color{ 0, 0, 0, 1 };
};

enum class FillRule : std::uint8_t { NonZero = 0, EvenOdd = 1 };
enum class FillKind : std::uint8_t { Solid = 0, Pattern = 1 };

// Where a pattern fill is cut (the legacy Compositor "fill clip"): at the
// host's bounding box (fast, no per-cell clipping), exactly at the path
// contour, or at the inner/outer EDGE of the host's widest stroke so the
// pattern meets the stroke cleanly. Interior motif cells stay instanced; only
// the boundary cells are geometrically clipped.
enum class PatternClip : std::uint8_t {
    Bounds = 0,        // lattice over the local bbox (v1 behaviour)
    Contour = 1,       // cut exactly at the path outline
    StrokeInner = 2,   // cut at the inner edge of the widest stroke
    StrokeOuter = 3,   // cut at the outer edge of the widest stroke
};

// What the lattice is pinned to (legacy "anchor"): the OBJECT keeps the motif
// glued to the shape (it follows a move); the DOCUMENT pins the lattice to the
// document origin so a moving shape slides over a static field.
enum class PatternAnchor : std::uint8_t { Object = 0, Document = 1 };

// A pattern fill (docs/Ink/DOCUMENT_MODEL.md §Paints): the region is covered by
// INSTANCES of a motif node on a lattice — the same instancing machinery as
// InstanceNode, so a dense pattern is one instanced draw, not N geometries.
// The Scene expands it into grouped motif drawables; cells crossing the clip
// boundary become derived clipped geometry (Scene::EmitPattern).
struct PatternFill {
    NodeId motifRef = kNullNode;   // node whose geometry is the tile
    double spacingX = 40.0;        // lattice pitch (node-local units)
    double spacingY = 40.0;
    double phaseX   = 0.0;         // lattice origin offset
    double phaseY   = 0.0;
    double rotation = 0.0;         // LATTICE rotation (radians) — rotates the
                                   // whole grid (Affinity "angle")
    double motifRotation = 0.0;    // extra per-motif spin (radians), applied
                                   // on top of the lattice orientation
    double scale    = 1.0;         // per-motif uniform scale
    PatternClip   clip   = PatternClip::Bounds;
    PatternAnchor anchor = PatternAnchor::Object;
};

struct Fill {
    FillKind    kind    = FillKind::Solid;
    Paint       paint;             // Solid
    PatternFill pattern;           // Pattern
    FillRule    rule    = FillRule::NonZero;
    float       opacity = 1.0f;    // layer opacity (multiplies the paint /
                                   // every motif colour of a pattern)
    bool        enabled = true;
};

enum class StrokeAlign : std::uint8_t { Center = 0, Inside = 1, Outside = 2 };
enum class CapStyle    : std::uint8_t { Butt = 0, Round = 1, Square = 2 };
enum class JoinStyle   : std::uint8_t { Miter = 0, Round = 1, Bevel = 2 };
// Document: width in node-local units (scales with the object — the Blender
// semantics). Viewport: width in view pixels (non-scaling hairlines /
// annotations; resolved per zoom tier by the GeometryCache).
enum class WidthSpace  : std::uint8_t { Document = 0, Viewport = 1 };

// ── Stroke marks (docs/Ink/IOF_CORE_PLAN.md Phase A — the GENERIC core model):
// a MANUAL annotation the user places at an arc-length position on one subpath
// of the stroked path. A mark does TWO independent things (both optional):
//   1. re-PHASES the dash run — it forces a dash ELEMENT, a GAP, or nothing
//      (Neutral) to land centred on it (the legacy DashAnchor generalised);
//   2. carries a list of MARK OBJECTS stamped at its position — SVG-marker-like
//      shapes (circle / rectangle / diamond + their inverted forms) or an
//      INSTANCE of an existing node, each either ADDED to the stroke layer or
//      SUBTRACTED from it (a geometric erase — the shape cuts the stroke).
// The IOF-specific glyphs (slope ticks, bridges, pylons…) are NOT core: the
// module rebuilds them from these primitives (Phase C).

// Where the phase re-phasing lands (or none).
enum class MarkPhase : std::uint8_t {
    Neutral = 0,   // no dash re-phasing (a plain object anchor)
    Dash    = 1,   // force a dash ELEMENT centre here
    Gap     = 2,   // force a GAP centre here
};

// Which side of the line the objects sit on (Center straddles the line;
// Left/Right offset by `offset` doc-units along the ±normal).
enum class MarkSide : std::uint8_t { Center = 0, Left = 1, Right = 2 };

// A mark object's shape (SVG-markers vocabulary, w3.org/TR/svg-markers).
// `Instance` stamps an existing node's geometry; `Gap` cuts the LINE open over
// a length (not a filled shape) with a chosen end cap.
enum class MarkShape : std::uint8_t {
    Circle     = 0,
    Rectangle  = 1,
    Diamond    = 2,
    Instance   = 3,   // instance of `nodeRef`
    Gap        = 4,   // an opening cut in the line (length = size)
    Triangle   = 5,   // isoceles, apex on +y (across the line)
    HalfCircle = 6,   // flat side ON the line, dome on +y
    Line       = 7,   // a segment ACROSS the line: `size` = half-length,
                      // `width` = half-thickness. Unlike the others, in a
                      // side mode its START (not its centre) sits at the
                      // offset — it reaches OUT from there (see StrokeRepeat).
};
inline constexpr std::uint8_t kMarkShapeMax = 7;

// The end shape of a Gap opening (mirrors the stroke caps).
enum class GapCap : std::uint8_t { Butt = 0, Round = 1, Square = 2 };

// How a mark object combines with the stroke:
//   Fusion   — the object's geometry is FUSED into the stroke's own mesh
//              (one drawing, one alpha): translucent strokes don't double up,
//              exactly like the legacy slope ticks. The default.
//   Blend    — the object is a SEPARATE drawable composited against the
//              stroke's isolation layer with its own blend mode.
//   Subtract — an absolute geometric erase (dst-out): the shape CUTS the
//              stroke layer (RENDER_GRAPH.md §Erase).
enum class MarkObjectMode : std::uint8_t { Fusion = 0, Blend = 1, Subtract = 2 };

// How the object's geometry relates to the curve:
//   Hard   — a rigid shape merely rotated to the tangent (at the mark point).
//   Bend   — the shape is skewed along the tangent frame (a shear that
//            approximates the local slope).
//   Follow — the shape's outline is RESAMPLED along the curve so it truly
//            bends with the line (a rectangle's long edges curve).
//   Chord  — a RIGID shape placed on the CHORD between the two curve points
//            its transverse ends cross: those crossing midpoints sit exactly
//            on the stroke, the shape spanning straight between them (its
//            centre floats to the chord midpoint). The natural placement for
//            a diamond, whose two tangent angles then land on the line.
enum class MarkBend : std::uint8_t { Hard = 0, Bend = 1, Follow = 2, Chord = 3 };
inline constexpr std::uint8_t kMarkBendMax = 3;

// The default bend for a shape: Rectangle follows the curve, Diamond spans as
// a chord (its angles on the line), Circle and Instance are rigid.
inline MarkBend DefaultBendFor(MarkShape s) {
    if (s == MarkShape::Rectangle)  return MarkBend::Follow;
    if (s == MarkShape::Diamond)    return MarkBend::Chord;
    return MarkBend::Hard;
}

struct MarkObject {
    MarkShape       shape = MarkShape::Circle;
    MarkObjectMode  mode  = MarkObjectMode::Fusion;
    MarkBend        bend  = MarkBend::Hard;
    BlendMode       blend = BlendMode::Normal;   // BLEND mode only
    // Size, in % of the stroke width (default) or in node-local doc-units.
    // `size` = radius (circle) / diagonal-half (diamond) / HALF-LENGTH along
    // the tangent (rectangle); `width` = the rectangle's HALF-height across it.
    double          size  = 100.0;
    double          width = 100.0;  // Rectangle only
    bool            sizePercent = true;
    double          rotation = 0.0; // extra spin about the mark point (radians)
    // Shift the object ALONG the line, before (−) or after (+) the mark point
    // (in % of the stroke width when sizePercent, else doc-units) — a lateral
    // nudge to fine-tune its position on the stroke.
    double          alongOffset = 0.0;
    // Per-object side + offset ACROSS the line. `sideInherit` uses the mark's
    // own side; otherwise `side`/`sideOffset` override it (used by the gap-end
    // markers, whose objects each place independently).
    bool            sideInherit = true;
    MarkSide        side = MarkSide::Center;
    double          sideOffset = 50.0;  // % of stroke width (or doc-units)
    NodeId          nodeRef = kNullNode;   // shape == Instance
    // Blend mode: draw the object IN FRONT of the stroke (default — the blend
    // puts the mark over the stroke) or BEHIND it (front = false — the stroke
    // then composites over the mark, the reverse blend order). Since the blend
    // operator is not symmetric, `front` is exactly the "which is over which"
    // choice.
    bool            front = true;
    // Gap only: the end caps of the OPENING (not of the surrounding runs), the
    // cut-objects toggle, and the two independent lists of MARKER OBJECTS
    // stamped at the gap's START and END ends (full sub-marks — every object
    // field, but no nested gaps).
    GapCap          gapStart = GapCap::Butt;
    GapCap          gapEnd   = GapCap::Butt;
    bool            gapCutsObjects = false;
    std::vector<MarkObject> gapStartObjects;
    std::vector<MarkObject> gapEndObjects;
    // Fill colour of a primitive object (linear straight). Instance objects use
    // the referenced node's own style, so this is ignored for them.
    Color           color{ 0, 0, 0, 1 };
    bool            useStrokeColor = true;    // primitive: inherit stroke paint
    // Object opacity. For a Blend/recoloured primitive it multiplies the paint
    // alpha; for a SUBTRACT object it is the ERASE STRENGTH (dst·(1−a)): 1 cuts
    // the stroke fully, 0.5 leaves it half-transparent — the mark-move preview
    // uses this for its live partial erase. Paint-level (like `color`):
    // excluded from the geometry hash, so animating it never re-tessellates.
    // v1 limit: ignored by stroke-coloured FUSION objects (their triangles are
    // baked into the stroke mesh, one alpha for the whole stroke).
    float           opacity = 1.0f;

    // Size / width / along-offset / side-offset resolved to node-local units.
    double SizeUnits(double w) const {
        return sizePercent ? size * 0.01 * w : size;
    }
    double WidthUnits(double w) const {
        return sizePercent ? width * 0.01 * w : width;
    }
    double AlongUnits(double w) const {
        return sizePercent ? alongOffset * 0.01 * w : alongOffset;
    }
    double SideOffsetUnits(double w) const {
        return sizePercent ? sideOffset * 0.01 * w : sideOffset;
    }

    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[11] = {
            (std::uint8_t)shape, (std::uint8_t)mode, (std::uint8_t)bend,
            (std::uint8_t)blend, (std::uint8_t)(sizePercent ? 1 : 0),
            (std::uint8_t)(front ? 1 : 0), (std::uint8_t)gapStart,
            (std::uint8_t)gapEnd, (std::uint8_t)(gapCutsObjects ? 1 : 0),
            (std::uint8_t)(sideInherit ? 1 : 0), (std::uint8_t)side };
        h = HashBytes(packed, sizeof packed, h);
        const std::uint8_t uc = useStrokeColor ? 1 : 0;
        h = HashBytes(&uc, 1, h);
        h = HashDouble(sideOffset, h);
        h = HashDouble(alongOffset, h);
        h = HashDouble(size, h);
        h = HashDouble(width, h);
        h = HashDouble(rotation, h);
        h = HashBytes(&nodeRef, sizeof nodeRef, h);
        for (const MarkObject& o : gapStartObjects) h = o.Hash(h);
        for (const MarkObject& o : gapEndObjects)   h = o.Hash(h);
        return h;   // color excluded (a paint edit must NOT re-tessellate)
    }
};

// Repeat anchoring of a mark: force an OBJECT/GROUP centre of every repeat
// run (StrokeRepeat) onto this mark (Object), or the middle of the space
// BETWEEN two groups (Between). None = the run ignores this mark.
enum class MarkRepeatAnchor : std::uint8_t { None = 0, Object = 1, Between = 2 };

struct StrokeMark {
    std::int32_t sub  = 0;    // which FLATTENED subpath the mark lives on
    double       t    = 0.5;  // arc-length position along it, in [0,1]
    MarkPhase    phase = MarkPhase::Neutral;
    MarkSide     side  = MarkSide::Center;
    double       offset = 50.0;  // Left/Right: signed distance to the line
                                 // (default 50 % of the stroke width)
    bool         offsetPercent = true;  // offset is a % of the stroke width
                                        // (100 % = one full width) vs doc-units
    // If ≥ 0 the mark is PINNED to that control point of its subpath (t is
    // recomputed from the point as the curve is edited); −1 = free at `t`.
    std::int32_t nodeAnchor = -1;
    // Forced size (doc units) of the dash/gap this mark centres when it
    // re-phases (phase Dash/Gap): the centred element takes THIS length
    // instead of the pattern's own — a bigger/smaller feature at the mark.
    // 0 = the pattern's size.
    double anchorSize = 0.0;
    // Repeat-run anchoring (see MarkRepeatAnchor / StrokeRepeat).
    MarkRepeatAnchor repeatAnchor = MarkRepeatAnchor::None;
    // The forced group centre-to-centre SPACING (doc units) a repeat run takes
    // at THIS mark — the "Anchor Size" equivalent for repeats: the gap before
    // the next repeat object/group after this point. 0 = the run's own pitch.
    double repeatGap = 0.0;
    // The objects stamped at this mark (may be EMPTY — a pure dash/gap
    // re-phaser). SVG-marker shapes / node instances.
    std::vector<MarkObject> objects;

    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[4] = { (std::uint8_t)phase, (std::uint8_t)side,
                                         (std::uint8_t)(offsetPercent ? 1 : 0),
                                         (std::uint8_t)repeatAnchor };
        h = HashBytes(packed, sizeof packed, h);
        h = HashBytes(&sub, sizeof sub, h);
        h = HashBytes(&nodeAnchor, sizeof nodeAnchor, h);
        h = HashDouble(t, h);
        h = HashDouble(offset, h);
        h = HashDouble(anchorSize, h);
        h = HashDouble(repeatGap, h);
        for (const MarkObject& o : objects) h = o.Hash(h);
        return h;
    }
    // Does this mark re-phase the dash run? (Neutral marks don't.)
    bool RePhases() const { return phase != MarkPhase::Neutral; }
    // The offset resolved to node-local doc-units for a given stroke width.
    double OffsetUnits(double strokeWidth) const {
        return offsetPercent ? offset * 0.01 * strokeWidth : offset;
    }
};

// How a dash re-phasing with SEVERAL anchors stretches the pattern between
// two consecutive anchors so a whole number of runs fits: scale dashes and
// gaps together, keep the gaps and stretch the dashes, or the reverse. Also
// reused by StrokeRepeat for how its pitch stretches between repeat anchors.
enum class DashFit : std::uint8_t { ScaleBoth = 0, ScaleDash = 1, ScaleGap = 2 };

// ── Stroke repeats (the IOF fence-tick family) — a REPEATED OBJECT RUN along
// the stroke, part of the stroke STYLE (so any stroke reuses it and a new
// object previews it while being drawn). GROUPS of `groupCount` primitive
// objects (intra-group centre-to-centre `groupPitch`) are stamped along the
// line by the chosen distribution; each object is a HARD primitive (no
// bend/follow), rigidly inclined by `rotation` about its point (the IOF 60°
// fence ticks). Marks with a repeat anchor re-phase the run (an object/group
// or a between-groups gap lands centred on the mark; the pitch stretches
// piecewise between anchors so whole steps fit).

// Which side of the line a repeated object sits on. Center straddles the
// line; Left/Right are the walk-direction sides (open paths; Left = +normal,
// the left hand walking start→end). Inside/Outside resolve per placement:
// against the shape's INTERIOR on a closed subpath (winding), against the
// LOCAL CURVATURE side on an open one (falls back to Left on straights).
enum class RepeatSide : std::uint8_t {
    Center = 0, Left = 1, Right = 2, Inside = 3, Outside = 4,
};

// How the run's group centres are laid out along the line.
enum class RepeatDistribute : std::uint8_t {
    Pitch   = 0,   // fixed group centre-to-centre distance (doc units)
    Gap     = 1,   // fixed EDGE-to-edge distance between groups (doc units)
    Count   = 2,   // exactly N groups spread over the line
    Density = 3,   // groups per 100 doc units
};

struct StrokeRepeat {
    bool            enabled = true;
    MarkShape       shape   = MarkShape::Rectangle;   // primitives only
    MarkObjectMode  mode    = MarkObjectMode::Fusion; // fusion / blend / cut
    BlendMode       blend   = BlendMode::Normal;      // Blend mode only
    // Like MarkObject: size = half-extent along the line (radius); width = a
    // rectangle's half-height across it; % of the stroke width or doc units.
    double  size  = 100.0;
    double  width = 25.0;
    bool    sizePercent = true;
    double  rotation = 0.0;        // inclination about each point (radians)
    RepeatSide side = RepeatSide::Center;
    double  sideOffset = 50.0;     // % of the stroke width (or doc units)
    bool    offsetPercent = true;
    RepeatDistribute distribute = RepeatDistribute::Pitch;
    double  pitch   = 8.0;         // Pitch: group centre-to-centre (doc units)
    double  gap     = 4.0;         // Gap: edge-to-edge between groups
    int     count   = 10;          // Count: groups over the line
    double  density = 12.0;        // Density: groups per 100 doc units
    double  phase   = 0.0;         // start offset along the line (doc units)
    int     groupCount = 1;        // objects per group
    double  groupPitch = 2.0;      // object centre-to-centre inside a group
    double  startTrim = 0.0;       // skip this arc length at the start …
    double  endTrim   = 0.0;       // … and at the end
    DashFit fit = DashFit::ScaleBoth;   // how the pitch stretches between two
                                        // repeat anchors (ScaleBoth stretches
                                        // the pitch; the others keep it fixed)
    // Line shape only: draw a connector from the offset start back to the path
    // (`lineJoin`), and/or clip whatever crosses to the far side of the path
    // (`lineClip`, a documented straight-line-at-the-path approximation).
    bool    lineJoin = false;
    bool    lineClip = false;
    Color   color{ 0, 0, 0, 1 };   // recolour (linear straight)
    bool    useStrokeColor = true;
    float   opacity = 1.0f;        // paint alpha / Subtract erase strength

    // Geometry-affecting fields only (color/opacity are paint-level).
    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[7] = {
            (std::uint8_t)(enabled ? 1 : 0), (std::uint8_t)shape,
            (std::uint8_t)mode, (std::uint8_t)blend,
            (std::uint8_t)(sizePercent ? 1 : 0), (std::uint8_t)side,
            (std::uint8_t)distribute };
        h = HashBytes(packed, sizeof packed, h);
        const std::uint8_t p2[2] = { (std::uint8_t)(offsetPercent ? 1 : 0),
                                     (std::uint8_t)(useStrokeColor ? 1 : 0) };
        h = HashBytes(p2, sizeof p2, h);
        h = HashDouble(size, h);   h = HashDouble(width, h);
        h = HashDouble(rotation, h);
        h = HashDouble(sideOffset, h);
        h = HashDouble(pitch, h);  h = HashDouble(gap, h);
        h = HashBytes(&count, sizeof count, h);
        h = HashDouble(density, h);
        h = HashDouble(phase, h);
        h = HashBytes(&groupCount, sizeof groupCount, h);
        h = HashDouble(groupPitch, h);
        h = HashDouble(startTrim, h);  h = HashDouble(endTrim, h);
        const std::uint8_t p3[3] = { (std::uint8_t)fit,
                                     (std::uint8_t)(lineJoin ? 1 : 0),
                                     (std::uint8_t)(lineClip ? 1 : 0) };
        h = HashBytes(p3, sizeof p3, h);
        return h;
    }
    double SizeUnits(double w) const {
        return sizePercent ? size * 0.01 * w : size;
    }
    double WidthUnits(double w) const {
        return sizePercent ? width * 0.01 * w : width;
    }
    double SideOffsetUnits(double w) const {
        return offsetPercent ? sideOffset * 0.01 * w : sideOffset;
    }
    // The group's along-the-line half extent (centre of first object to
    // centre of last, plus one object half-extent each side).
    double GroupHalfExtent(double w) const {
        const int n = groupCount < 1 ? 1 : groupCount;
        return (double)(n - 1) * groupPitch * 0.5 + SizeUnits(w);
    }
};

struct Stroke {
    Paint       paint;
    double      width      = 1.0;
    StrokeAlign align      = StrokeAlign::Center;
    CapStyle    cap        = CapStyle::Butt;
    JoinStyle   join       = JoinStyle::Miter;
    double      miterLimit = 4.0;
    WidthSpace  widthSpace = WidthSpace::Document;
    // SVG-style dash pattern (on/off run lengths along the spine, node-local
    // units; empty = solid) + phase offset. Applied before outlining so every
    // dash gets real caps (docs/Ink/GEOMETRY.md §2).
    std::vector<double> dashPattern;
    double              dashOffset = 0.0;
    // Multi-anchor dash fitting priority (see DashFit).
    DashFit             dashFit = DashFit::ScaleBoth;
    bool                enabled    = true;
    // Manual marks along the stroke (ticks/crossings/bridges/pylons + dash
    // anchors). Cuts and re-phasing are applied by the stroker.
    std::vector<StrokeMark> marks;
    // Repeated object runs along the stroke (see StrokeRepeat).
    std::vector<StrokeRepeat> repeats;

    // Geometry-affecting parameters only (paints excluded — a color edit must
    // NOT re-tessellate; docs/Ink/GEOMETRY.md §3).
    std::uint64_t GeometryHash() const {
        std::uint64_t h = 0x57120CEULL;
        h = HashDouble(width, h);
        const std::uint8_t packed[5] = { (std::uint8_t)align, (std::uint8_t)cap,
                                         (std::uint8_t)join,
                                         (std::uint8_t)widthSpace,
                                         (std::uint8_t)dashFit };
        h = HashBytes(packed, sizeof packed, h);
        h = HashDouble(miterLimit, h);
        for (double d : dashPattern) h = HashDouble(d, h);
        if (!dashPattern.empty()) h = HashDouble(dashOffset, h);
        for (const StrokeMark& m : marks) h = m.Hash(h);
        for (const StrokeRepeat& r : repeats) h = r.Hash(h);
        return h;
    }
};

struct Style {
    std::vector<Fill>   fills;
    std::vector<Stroke> strokes;

    static Style Filled(const Color& linearStraight) {
        Style s;
        Fill f;
        f.paint.color = linearStraight;
        s.fills.push_back(f);
        return s;
    }
    static Style Stroked(const Color& linearStraight, double width) {
        Style s;
        Stroke st;
        st.paint.color = linearStraight;
        st.width = width;
        s.strokes.push_back(st);
        return s;
    }
    Style& WithStroke(const Color& linearStraight, double width) {
        Stroke st;
        st.paint.color = linearStraight;
        st.width = width;
        strokes.push_back(st);
        return *this;
    }
};

} // namespace Ink

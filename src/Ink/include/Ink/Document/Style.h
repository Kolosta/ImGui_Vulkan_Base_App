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
// `Instance` stamps an existing node's geometry at the mark instead of a
// primitive.
enum class MarkShape : std::uint8_t {
    Circle    = 0,
    Rectangle = 1,
    Diamond   = 2,
    Instance  = 3,   // instance of `nodeRef`
};

// How a mark object combines with the stroke:
//   Fusion   — the object's geometry is FUSED into the stroke's own mesh
//              (one drawing, one alpha): translucent strokes don't double up,
//              exactly like the legacy slope ticks. The default.
//   Blend    — the object is a SEPARATE drawable composited over the stroke's
//              isolation layer with its own blend mode (so it can Multiply /
//              Screen / … against the stroke below).
//   Subtract — an absolute geometric erase (dst-out): the shape CUTS the
//              stroke layer (RENDER_GRAPH.md §Erase).
enum class MarkObjectMode : std::uint8_t { Fusion = 0, Blend = 1, Subtract = 2 };

// Whether an object's geometry BENDS to follow the curve at the mark, or stays
// a hard (rigid) shape merely rotated to the tangent.
enum class MarkBend : std::uint8_t { Hard = 0, Bend = 1 };

struct MarkObject {
    MarkShape       shape = MarkShape::Circle;
    MarkObjectMode  mode  = MarkObjectMode::Fusion;
    MarkBend        bend  = MarkBend::Hard;
    BlendMode       blend = BlendMode::Normal;   // BLEND mode only
    // node-local units. `size` = radius (circle/diamond) or HALF-LENGTH along
    // the tangent (rectangle); `width` = the rectangle's HALF-height across it.
    double          size  = 6.0;
    double          width = 6.0;    // Rectangle only (half-height)
    double          rotation = 0.0; // extra spin about the mark point (radians)
    NodeId          nodeRef = kNullNode;   // shape == Instance
    // Fill colour of a primitive object (linear straight). Instance objects use
    // the referenced node's own style, so this is ignored for them.
    Color           color{ 0, 0, 0, 1 };
    bool            useStrokeColor = true;    // primitive: inherit stroke paint

    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[5] = { (std::uint8_t)shape, (std::uint8_t)mode,
                                         (std::uint8_t)bend, (std::uint8_t)blend,
                                         (std::uint8_t)(useStrokeColor ? 1 : 0) };
        h = HashBytes(packed, sizeof packed, h);
        h = HashDouble(size, h);
        h = HashDouble(width, h);
        h = HashDouble(rotation, h);
        h = HashBytes(&nodeRef, sizeof nodeRef, h);
        return h;   // color excluded (a paint edit must NOT re-tessellate)
    }
};

struct StrokeMark {
    std::int32_t sub  = 0;    // which FLATTENED subpath the mark lives on
    double       t    = 0.5;  // arc-length position along it, in [0,1]
    MarkPhase    phase = MarkPhase::Neutral;
    MarkSide     side  = MarkSide::Center;
    double       offset = 0.0;  // Left/Right: signed distance to the line
    bool         offsetPercent = true;  // offset is a % of the stroke width
                                        // (100 % = one full width) vs doc-units
    // If ≥ 0 the mark is PINNED to that control point of its subpath (t is
    // recomputed from the point as the curve is edited); −1 = free at `t`.
    std::int32_t nodeAnchor = -1;
    // The objects stamped at this mark (may be EMPTY — a pure dash/gap
    // re-phaser). SVG-marker shapes / node instances.
    std::vector<MarkObject> objects;

    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[3] = { (std::uint8_t)phase, (std::uint8_t)side,
                                         (std::uint8_t)(offsetPercent ? 1 : 0) };
        h = HashBytes(packed, sizeof packed, h);
        h = HashBytes(&sub, sizeof sub, h);
        h = HashBytes(&nodeAnchor, sizeof nodeAnchor, h);
        h = HashDouble(t, h);
        h = HashDouble(offset, h);
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
    bool                enabled    = true;
    // Manual marks along the stroke (ticks/crossings/bridges/pylons + dash
    // anchors). Cuts and re-phasing are applied by the stroker.
    std::vector<StrokeMark> marks;

    // Geometry-affecting parameters only (paints excluded — a color edit must
    // NOT re-tessellate; docs/Ink/GEOMETRY.md §3).
    std::uint64_t GeometryHash() const {
        std::uint64_t h = 0x57120CEULL;
        h = HashDouble(width, h);
        const std::uint8_t packed[4] = { (std::uint8_t)align, (std::uint8_t)cap,
                                         (std::uint8_t)join,
                                         (std::uint8_t)widthSpace };
        h = HashBytes(packed, sizeof packed, h);
        h = HashDouble(miterLimit, h);
        for (double d : dashPattern) h = HashDouble(d, h);
        if (!dashPattern.empty()) h = HashDouble(dashOffset, h);
        for (const StrokeMark& m : marks) h = m.Hash(h);
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

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

// ── Stroke marks (the legacy LineMark — docs/Ink/IOF_CORE_PLAN.md Phase A):
// a MANUAL glyph or phase pin the user places at an arc-length position on
// one subpath of the stroked path. It decorates or re-phases the line
// WITHOUT touching the path geometry, and is tessellated with the stroke
// (same mesh, same paint) so it rides the normal content pass.
enum class MarkKind : std::uint8_t {
    SlopeTick  = 0,   // short downhill tick on one side (contour slope hint)
    Crossing   = 1,   // a gap cut in the line + two ticks across the ends
    Bridge     = 2,   // a gap + two facing brackets (bridge/tunnel entrances)
    Pylon      = 3,   // crossbar across the line (optional box variant)
    DashAnchor = 4,   // NO geometry: forces a dash ELEMENT (side +1) or GAP
                      // (side −1) to land centred here — re-phases the run
};

struct StrokeMark {
    MarkKind     kind = MarkKind::SlopeTick;
    std::int32_t sub  = 0;    // which FLATTENED subpath the mark lives on
    double       t    = 0.5;  // arc-length position along it, in [0,1]
    std::int32_t side = +1;   // tick side: +1 = left of travel, −1 = right
                              // (DashAnchor: +1 = dash centred, −1 = gap)
    // Geometry params (node-local units); meaning depends on `kind`:
    //   SlopeTick : size = tick length.
    //   Crossing  : gap  = the cut length, size = tick half-length.
    //   Bridge    : gap  = the opening, size = bracket half-height.
    //   Pylon     : size = bar half-length; square+gap = box side.
    double gap       = 8.0;
    double size      = 6.0;
    double thickness = 0.0;   // 0 → reuse the base stroke width
    // SlopeTick: `size` measured from the stroke's OUTER edge (drawn length =
    // halfWidth + size) — the ISOM "outside measure" (OM) convention.
    bool outsideMeasure = false;
    // Pylon: a small square box (side = `gap`) centred on the bar.
    bool square = false;
    // DashAnchor: if ≥ 0 the anchor is PINNED to that control point of its
    // subpath (t recomputed from the point as the curve is edited); −1 = free.
    std::int32_t nodeAnchor = -1;

    std::uint64_t Hash(std::uint64_t h) const {
        const std::uint8_t packed[3] = { (std::uint8_t)kind,
                                         (std::uint8_t)(outsideMeasure ? 1 : 0),
                                         (std::uint8_t)(square ? 1 : 0) };
        h = HashBytes(packed, sizeof packed, h);
        h = HashBytes(&sub, sizeof sub, h);
        h = HashBytes(&side, sizeof side, h);
        h = HashBytes(&nodeAnchor, sizeof nodeAnchor, h);
        h = HashDouble(t, h);
        h = HashDouble(gap, h);
        h = HashDouble(size, h);
        h = HashDouble(thickness, h);
        return h;
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

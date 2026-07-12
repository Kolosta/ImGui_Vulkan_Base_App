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

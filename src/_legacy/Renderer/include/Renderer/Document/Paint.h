#pragma once

#include "Path.h"      // Vec2 (FillLayer.offset)
#include <cstdint>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  Paint — fill and stroke styles for a vector shape.
//
//  These are DOCUMENT DATA, authored by the user: the colours are NOT design-
//  system tokens (the design system styles the *editor chrome*, not the artwork
//  the user draws). Stored as straight RGBA floats in [0,1].
// ─────────────────────────────────────────────────────────────────────────────

struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

// A shape can be filled, stroked, or both. Each channel carries its own colour
// so fill and contour are independent (as the brief requires).
struct FillStyle {
    bool  enabled = true;
    Color color{0.85f, 0.85f, 0.88f, 1.0f};
};

// ─────────────────────────────────────────────────────────────────────────────
//  Surface fill LAYERS — a stack of pattern fills clipped to a closed contour.
//
//  A surface (closed Part) can carry several FillLayers that overlay (the ISOM
//  "screens" combine this way, §2.11.4: e.g. marsh blue lines over rough-open
//  yellow over green dots). Each layer's pattern is generated INFINITELY across
//  the contour's bounds and CLIPPED to it, with a draggable `offset` so the
//  pattern phase can be moved within the surface. Generated at tessellation time
//  (not baked), so editing the contour or the layer params reflows the pattern.
// ─────────────────────────────────────────────────────────────────────────────
enum class FillPattern : uint8_t {
    Solid    = 0,   // flat colour at `opacity` (a colour "screen %": 0.5 = 50%)
    Dots     = 1,   // a regular grid of filled dots (stony/broken ground, screens)
    Lines    = 2,   // parallel lines / hatching (marsh, vegetation stripes)
    Triangles= 3,   // a grid of small solid triangles (boulder field 8:6:5)
    RandomDots = 4, // pseudo-random scattered dots (natural stony/broken ground)
    Grid     = 5,   // crossed lines (cultivated land dot grid uses Dots; reserved)
    CrossHatch = 6, // two line sets crossing (out-of-bounds / OOB area)
};

// Where a fill PATTERN layer is cut off relative to the part's stroked contour. A
// pattern element straddling the boundary is clipped at the pixel by the GPU
// stencil, so nothing spills past the chosen edge. The base `fill` and the stroke
// are unaffected — only the pattern layer honours this, and EACH layer chooses its
// own edge (so a surface can, e.g., keep dots to the inner edge while a line screen
// runs to the construction line).
//   • Construction → cut at the path centreline (where the curve is defined).
//   • SideA_Inner  → cut at the stroke's INNER edge (inside the contour).
//   • SideB_Outer  → cut at the stroke's OUTER edge (outside the contour).
// The inner/outer offset is derived automatically from the stroke width + align
// (Center/Inner/Outer), so it follows the actual painted edge of the line.
enum class FillClip : uint8_t {
    Construction = 0,
    SideA_Inner  = 1,
    SideB_Outer  = 2,
};

// What the fill PATTERN's lattice is anchored to (its phase origin). Determines what
// happens to the motif when the object moves vs when its geometry is edited:
//   • ObjectOrigin → fixed to the object's origin. Moving the object (object mode)
//     moves the motif with it; editing the geometry (points) in edit mode does NOT
//     slide it. The natural default.
//   • DocumentOrigin → fixed in the page (document 0,0). The object can move/edit
//     over a STATIC motif field. ISOM/IOF screens use this.
enum class FillAnchor : uint8_t {
    ObjectOrigin   = 0,
    DocumentOrigin = 1,
};

struct FillLayer {
    bool        enabled = true;
    FillPattern pattern = FillPattern::Solid;
    Color       color{0, 0, 0, 1};
    float       opacity  = 1.0f;     // Solid screen %: 1.0 = full, 0.5 = 50%
    float       spacing  = 1.0f;     // centre-to-centre of dots/lines (doc-units)
    float       size     = 0.3f;     // dot ø / line width / triangle size (doc-units)
    float       angleDeg = 0.0f;     // pattern orientation (dots/lines/triangles/grid)
    Vec2        offset{0, 0};        // draggable pattern phase within the surface
    uint32_t    seed     = 1u;       // RandomDots jitter seed (stable per layer)
    // For Lines: optional dash along each line (dash/gap, doc-units; 0 = solid),
    // and phase-alternation (every other line shifted by half a dash period) —
    // covers ISOM indistinct marsh / vineyard dashed rows.
    float       dash     = 0.0f;
    float       dashGap  = 0.0f;
    bool        altPhase = false;
    // Where THIS layer's pattern is cut relative to the stroked contour. Per-layer
    // so each screen can run to a different edge. See FillClip above.
    FillClip    clip     = FillClip::Construction;
    // What the motif lattice is anchored to (object origin vs document). See FillAnchor.
    FillAnchor  anchor   = FillAnchor::ObjectOrigin;
};

// How an OPEN stroke terminates at its end points.
//   • Butt   → cut square exactly at the end point (no overshoot).
//   • Round  → a true vector half-disc of radius = half-width past the end.
//   • Square → a square that overshoots by half-width past the end point.
//   • Taper  → narrows to a POINT past the end (a sharp tip). The taper length is
//     `capTaper` doc-units; auto-follows the actual endpoints (erosion gully 107).
enum class LineCap : uint8_t {
    Butt   = 0,
    Round  = 1,
    Square = 2,
    Taper  = 3,
};

// How two stroke segments meet at an interior vertex (and at the closing vertex
// of a closed contour).
//   • Miter → outer edges extended to a sharp corner (limited by miterLimit).
//   • Round → a true vector arc filling the outer wedge.
//   • Bevel → a straight chamfer across the outer wedge.
enum class LineJoin : uint8_t {
    Miter = 0,
    Round = 1,
    Bevel = 2,
};

// Where the stroke sits relative to the geometric path. Meaningful for any path;
// Inner/Outer are only well-defined for a CLOSED (cyclic) contour (an open path
// has no inside) — open paths fall back to Center.
//   • Center → the stroke straddles the path (half-width each side). Default.
//   • Inner  → the whole width lies on the inside of a closed contour.
//   • Outer  → the whole width lies on the outside of a closed contour.
enum class StrokeAlign : uint8_t {
    Center = 0,
    Inner  = 1,
    Outer  = 2,
};

// A periodic glyph stamped ALONG a stroked path, so a single editable curve can
// carry an ISOM-style pattern (cliff tags, wall dots, railway ties…) that scales
// and follows the curve automatically. Drawn IN ADDITION to the base line.
//   • None     → plain line (dash[] still applies).
//   • Tags     → short perpendicular ticks on ONE side (earth bank, cliffs).
//   • TagsBoth → ticks on BOTH sides (fences when drawn that way).
//   • Dots     → filled dots straddling the line (walls).
//   • HalfDots → half-discs on one side only (retaining wall / retaining bank).
//   • Ties     → short cross-bars centred on the line (railway).
//   • Pylons   → small bars at wide spacing (power-line pylons).
//   • Slashes  → oblique strokes at an angle (fence pickets 60°, OOB route ×).
//   • Vee      → small downhill V chevrons (slope hints) — reserved.
//   • DoubleLine → a second continuous line parallel to the path, at ±size/2.
//   • Railway    → twin parallel rails (±size/2) PLUS short cross-ties on them.
//   • Crosses    → × marks straddling the line (OOB route 711).
// Continuous decorators (DoubleLine/Railway/Crosses) follow the WHOLE curve so a
// single editable path carries the full styled symbol; the pattern regenerates
// automatically when the curve is edited.
enum class LineDecor : uint8_t {
    None         = 0,
    Tags         = 1,
    TagsBoth     = 2,
    Dots         = 3,
    HalfDots     = 4,
    Ties         = 5,
    Pylons       = 6,
    Slashes      = 7,
    Vee          = 8,
    DoubleLine   = 9,
    Railway      = 10,
    Crosses      = 11,
    DoubleSlashes = 12,   // twin rails + oblique pickets (impassable fence 518)
    DoublePylons  = 13,   // twin rails + pylon bars (major power line 511)
    DoubleTicks   = 14,   // twin rails + arrow ticks (prominent uncrossable 529)
    // Two thin edge contours at ±size/2 drawn IN ADDITION to the base line (the
    // base is NOT suppressed). For the railway 509: a 0.35 black/white dashed band
    // (the base) bordered by two 0.10 black edges. decorThickness = edge width.
    EdgeLines     = 15,
    // Periodic GROUPS of two dots straddling the line (impassable wall 515): two
    // close dots, then a wide gap, repeating. decorSize = dot ø, decorSpacing =
    // group-to-group, decorThickness unused.
    PairDots      = 16,
    // Periodic GROUPS of two oblique pickets on ONE side (impassable fence 518):
    // two pickets close together, then a wide gap. decorSize = picket length,
    // decorAngleDeg = picket angle, decorSpacing = group-to-group.
    PairSlashes   = 17,
};

// Where a periodic decorator glyph sits LATERALLY relative to the stroked contour.
// The offset is derived from the stroke width + align (like the fill cut polygon),
// so a glyph can run on the construction line or on the painted inner / outer edge.
enum class DecorEdge : uint8_t {
    Construction = 0,   // the path centreline
    InnerEdge    = 1,   // the stroke's inner edge
    OuterEdge    = 2,   // the stroke's outer edge
};

// Which side(s) of the (edge-offset) line each station emits a glyph onto.
//   • One         → +normal side only.
//   • Both        → mirrored on both sides (one extra glyph per station).
//   • Centered    → the glyph straddles the line (dots, ties, crosses).
//   • Alternating → every other station flips +normal / −normal.
enum class DecorSide : uint8_t {
    One         = 0,
    Both        = 1,
    Centered    = 2,
    Alternating = 3,
};

// The side mode that reproduces a decorator's PRE-instancing geometry, so old files
// and existing IOF symbols (which never stored a side) render identically.
inline DecorSide DefaultSideForDecor(LineDecor d) {
    switch (d) {
        case LineDecor::Tags:
        case LineDecor::HalfDots:
        case LineDecor::Slashes:
        case LineDecor::Vee:
        case LineDecor::PairSlashes:
            return DecorSide::One;
        default:                      // TagsBoth/Ties/Pylons/Dots/Crosses/PairDots/…
            return DecorSide::Centered;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  LineMark — a MANUAL glyph the mapper places at a specific point along a line.
//
//  Unlike `LineDecor` (a periodic pattern auto-stamped along the whole curve),
//  a LineMark is a single, user-positioned annotation: the slope tick on a
//  contour (101/102/103), the crossing point on a wall/fence (519), a bridge
//  (512), or a pinned pylon on a power line (510/511). It is DOCUMENT DATA on the
//  Part, addressed by arc-length position so it follows the curve when edited.
// ─────────────────────────────────────────────────────────────────────────────
enum class LineMarkKind : uint8_t {
    SlopeTick = 0,  // short downhill tick (form-line / contour slope hint 101..103)
    Crossing  = 1,  // 519 crossing point: a gap in the line + two ticks across it
    Bridge    = 2,  // 512 bridge/tunnel: a gap + two facing brackets (two parts)
    Pylon     = 3,  // a PINNED pylon bar on a power line (510/511) — anchors relayout
    // A phase ANCHOR with NO geometry: it forces the dash/gap (or a pattern
    // element) to land CENTRED on this point, and re-phases the dash/pattern run on
    // each side independently out to the curve ends (or the next anchor). `side`
    // chooses what is centred: +1 = a dash/pattern ELEMENT, −1 = a GAP. Used to pin
    // a corner of a wall/fence/form-line on a dash so it reads cleanly.
    DashAnchor = 4,
};

struct LineMark {
    LineMarkKind kind = LineMarkKind::SlopeTick;
    int   sub  = 0;        // which subpath (strand) the mark lives on
    float t    = 0.5f;     // arc-length position along that subpath, in [0,1]
    int   side = +1;       // tick/bracket side: +1 = left of travel, -1 = right
    // Geometry params (doc-units), purpose depends on `kind`:
    //   SlopeTick : length  (default 0.4 OM); thickness via `thickness`.
    //   Crossing  : gap     = the cut length in the line; size = tick half-length.
    //   Bridge    : gap     = opening between the two parts; size = bracket length.
    //   Pylon     : size    = bar half-length each side.
    float gap       = 0.0f;
    float size      = 0.4f;
    float thickness = 0.0f;   // 0 → reuse the base line width
    // Outside Measure: for SlopeTick, `size` is measured from the OUTER edge of the
    // base line, not its centre — the tick starts at the centre and extends
    // (halfBaseWidth + size) so the visible part past the line edge is exactly
    // `size` (ISOM slope-line "OM" convention for 101/102/103).
    bool  outsideMeasure = false;
    // Pylon variant: a small square (the major-power-line "pylon with a box")
    // centred on the bar, side = `gap` doc-units, inside-stroke. false = plain bar.
    bool  square = false;
    // DashAnchor only: if ≥0, the anchor is PINNED to node `nodeAnchor` of its
    // subpath (it tracks that control point as the curve is edited) and `t` is
    // recomputed from the node each frame; −1 = a free anchor fixed at `t`.
    int   nodeAnchor = -1;
};

struct StrokeStyle {
    bool  enabled = false;
    Color color{0.05f, 0.05f, 0.06f, 1.0f};
    float width = 2.0f;   // in document units

    // ── Line styling (document data) ──
    LineCap     cap   = LineCap::Round;    // open-end shape (legacy default round)
    LineJoin    join  = LineJoin::Round;   // interior-corner shape
    StrokeAlign align = StrokeAlign::Center;
    float       miterLimit = 4.0f;         // miter→bevel fallback ratio
    float       capTaper   = 0.0f;         // LineCap::Taper tip length (doc-units)

    // Dash pattern (doc-units): alternating on/off lengths. Empty = solid.
    // A single value n is treated as {n, n}. Centred per the ISOM dashed-line
    // rule (start/end dashes equal) by the tessellator.
    std::vector<float> dash;

    // Periodic decorator stamped along the line (tags/dots/ties…), with its
    // spacing (centre-to-centre, doc-units), glyph size (length/diameter,
    // doc-units) and angle (degrees, for Slashes/Tags). decorThickness 0 →
    // reuse the base width for the decorator strokes.
    LineDecor decor          = LineDecor::None;
    float     decorSpacing   = 2.0f;
    float     decorSize      = 0.4f;
    float     decorAngleDeg  = 90.0f;
    float     decorThickness = 0.0f;
    // Phase the decorator so the first/last glyph land symmetrically (ISOM
    // "styled line" rule: end length = half the spacing). true = centred run.
    bool      decorCentered  = true;
    // Lateral placement of the periodic glyphs (Core curve-instancing): which edge
    // they sit on, and which side(s). DefaultSideForDecor() seeds `decorSide` so old
    // data renders unchanged. See DecorEdge / DecorSide above.
    DecorEdge decorEdge      = DecorEdge::Construction;
    DecorSide decorSide      = DecorSide::Centered;
    // 0 = stamp the built-in LineDecor glyph. Non-zero = instance this document
    // Shape's baked mesh along the curve (array-along-curve); spacing/edge/side/size
    // still apply, colour comes from the source object.
    uint64_t  decorSourceShapeId = 0;
};

} // namespace Renderer

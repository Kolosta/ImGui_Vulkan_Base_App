#pragma once

#include "Paint.h"
#include "Path.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  Shape — one drawable vector OBJECT on an artboard.
//
//  An object is an identity (id/name/collection) + an ORIGIN + an affine
//  TRANSFORM + a list of PARTS. Each Part is one piece of geometry with its own
//  fill and stroke. A freshly created primitive/curve is a single-part object;
//  Join merges several objects by concatenating their parts — so one object can
//  hold a rectangle, a line and an ellipse with different colours, yet select,
//  move and show in the Outliner as ONE item with ONE origin (exactly Blender's
//  "join" behaviour, adapted to 2D).
//
//  Geometry is authored in OBJECT-LOCAL doc-units; `origin` + `transform` place
//  the whole object (all its parts) on the artboard.
// ─────────────────────────────────────────────────────────────────────────────

enum class ShapeKind : uint8_t {
    Rectangle = 0,   // parametric: pos + size
    Ellipse   = 1,   // parametric: pos + size bounding box
    Triangle  = 2,   // editable Node[] path
    Polyline  = 3,   // editable Node[] path (straight by default)
    Curve     = 4,   // editable Node[] path (with handles)
    Path      = 5,   // generic editable Node[] path (a converted primitive)
};

// How an object's pixels fuse with what's already on the canvas below it (the
// standard SVG/Photoshop set). A render-time compositing param: the modern
// Compositor isolates the object then composites it with this mode; the legacy
// renderer ignores it (Normal only). See docs/Vulkan/COMPOSITOR_PIPELINE.md (Lot 4b).
enum class BlendMode : uint8_t {
    Normal = 0,
    Multiply, Screen, Overlay, Darken, Lighten,
    ColorDodge, ColorBurn, HardLight, SoftLight,
    Difference, Exclusion,
    Hue, Saturation, Color, Luminosity,
    // Erase / knock-out (Affinity's erase blend mode): the object SUBTRACTS coverage
    // from what's below (dst-out) instead of adding colour. It is a BLEND MODE, not a
    // separate flag. Appended last so existing .acu blend values keep their meaning.
    Erase,
};

inline const char* BlendModeName(BlendMode m) {
    switch (m) {
        case BlendMode::Normal:      return "Normal";
        case BlendMode::Multiply:    return "Multiply";
        case BlendMode::Screen:      return "Screen";
        case BlendMode::Overlay:     return "Overlay";
        case BlendMode::Darken:      return "Darken";
        case BlendMode::Lighten:     return "Lighten";
        case BlendMode::ColorDodge:  return "Color Dodge";
        case BlendMode::ColorBurn:   return "Color Burn";
        case BlendMode::HardLight:   return "Hard Light";
        case BlendMode::SoftLight:   return "Soft Light";
        case BlendMode::Difference:  return "Difference";
        case BlendMode::Exclusion:   return "Exclusion";
        case BlendMode::Hue:         return "Hue";
        case BlendMode::Saturation:  return "Saturation";
        case BlendMode::Color:       return "Color";
        case BlendMode::Luminosity:  return "Luminosity";
        case BlendMode::Erase:       return "Erase";
    }
    return "Normal";
}

// Geometry FAMILY of a Part — the dimension Blender keys object operators on
// (you cannot Join a Mesh to a Curve):
//   • Mesh  → straight edges + faces; supports vertex / edge / face editing.
//   • Curve → "curve-like": a free vector path edited point by point; edge/face
//     sub-modes don't apply. A curve-like part also carries a SplineType.
// Two objects can Join only if they share a family; cross-family needs a
// "Convert To" first.
enum class PartType : uint8_t {
    Mesh  = 0,
    Curve = 1,
};

inline const char* PartTypeName(PartType t) {
    return t == PartType::Mesh ? "Mesh" : "Curve";
}

// How a curve-like Part interprets its Node[] (Blender's spline types). Only
// meaningful when type == Curve.
//   • Bezier → anchors lie ON the curve; each anchor has optional in/out Bézier
//     handles (the existing Node model).
//   • Nurbs  → the Node positions are CONTROL POINTS off the curve, joined by a
//     control polygon; the smooth curve is a uniform B-spline of degree
//     (orderU − 1). Handles are ignored.
//   • Poly   → straight polyline through the anchors; handles ignored.
enum class SplineType : uint8_t {
    Bezier = 0,
    Nurbs  = 1,
    Poly   = 2,
};

inline const char* SplineTypeName(SplineType s) {
    switch (s) {
        case SplineType::Bezier: return "Bezier";
        case SplineType::Nurbs:  return "NURBS";
        case SplineType::Poly:   return "Poly";
    }
    return "Bezier";
}

// One piece of geometry inside an object, with its own paint. Parametric kinds
// (Rectangle/Ellipse) keep pos/size until edited; other kinds use `path`.
struct Part {
    ShapeKind   kind = ShapeKind::Rectangle;
    Vec2        pos{0, 0};      // parametric top-left
    Vec2        size{100, 100}; // parametric extents
    Path        path;           // editable geometry (non-parametric kinds)
    FillStyle   fill;
    StrokeStyle stroke;
    // Surface fill LAYERS — a stack of pattern fills (dots/lines/triangles/…)
    // clipped to this part's CLOSED contour, overlaid in order. Empty = just the
    // plain `fill` above. Used by area symbols (ISOM screens) and any surface that
    // needs a movable, infinite, combinable pattern. Drawn after `fill`.
    std::vector<FillLayer> fillLayers;
    // Manual line MARKS — single glyphs the mapper places at a specific point
    // along this part's path (slope tick, crossing point, bridge, pinned pylon).
    // Addressed by arc-length so they follow the curve when it is edited. Drawn
    // after the base line + decorator. See `LineMark` in Paint.h.
    std::vector<LineMark> marks;
    // Geometry family. Mesh (default) supports edge/face editing; Curve is
    // edited point-by-point. The Add menu seeds it; "Convert To" changes it;
    // Join requires a common family.
    PartType    type = PartType::Mesh;
    // For type == Curve: how the Node[] is interpreted (Bézier handles / NURBS
    // control polygon / straight poly). Ignored for Mesh. "Set Spline Type"
    // (Edit-mode menu) changes it.
    SplineType  spline = SplineType::Bezier;
    // NURBS degree+1 (order U). Clamped at use to [2, node count]. Order 2 = a
    // straight control polygon; higher = smoother. Ignored unless spline=Nurbs.
    int         orderU = 3;
    // NURBS knot-vector options (Blender's "Endpoint U" / "Bezier U"), open curves
    // only (a closed/cyclic NURBS is periodic and ignores them):
    //   • nurbsEndpoint → CLAMPED knots: the curve touches its first/last control
    //     point and is tangent to the end edges (needed for arcs/half-circles).
    //   • nurbsBezier   → interior knots get FULL multiplicity (degree), so the
    //     control polygon behaves as consecutive rational Bézier segments — the
    //     form that traces EXACT circles/arcs from a weighted square/triangle hull.
    // Default: endpoint clamped on (most useful), bezier off (smooth uniform).
    bool        nurbsEndpoint = true;
    bool        nurbsBezier   = false;
    // How an OPEN (non-cyclic) curve's FILL closes the gap between its two ends:
    //   • false (default) → follow the curve: the closing edge traces the end handles
    //     (Bézier) / the curve's own evaluation incl. weights (NURBS), so the filled
    //     boundary continues smoothly past the endpoints.
    //   • true            → a STRAIGHT segment directly between the two end points
    //     (NURBS: between the curve extremities under the current endpoint setting).
    // Ignored for closed/cyclic parts (they have no gap). Affects fill only, not the
    // stroke (which stays open).
    bool        openFillStraight = false;

    bool IsParametric() const {
        return kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse;
    }
    // True for the curve-like family — point editing, no edge/face sub-modes.
    bool IsCurveLike() const { return type == PartType::Curve; }
    // Bake a parametric primitive into the editable Node[] model (no-op if the
    // part already lives as a path). Defined in Shape.cpp.
    void EnsurePath();
};

// Affine placement of the object on the artboard. translate in doc-units, rotate
// in radians, scale per-axis. Applied about `origin`:
//   world = origin + R(rotate)·S(scale)·(local − origin) + translate
struct Transform {
    Vec2  translate{0, 0};
    float rotate = 0.0f;
    Vec2  scale{1, 1};
};

struct Shape {
    uint64_t          id   = 0;          // stable identity within a document
    std::string       name;
    uint64_t          collectionId = 0;  // owning collection (0 = document root)
    // Object PARENT (Blender Ctrl+P). 0 = no parent. A transform op applied to the
    // parent propagates the same world-space transform to all descendants, so the
    // children follow the parent rigidly while each keeps its own cached geometry +
    // independent transform. Purely a transform relationship; the Outliner shows it
    // as a nested row and the viewport draws a relationship line origin→origin.
    uint64_t          parentId = 0;
    Vec2              origin{0, 0};       // object origin (object-local doc-units)
    Transform         transform;          // placement on the artboard
    std::vector<Part> parts;              // ≥1 piece of geometry
    bool              visible = true;
    // Object OPACITY [0,1] (default 1 = opaque). A render-time compositing param,
    // NOT baked into the geometry: the modern Compositor isolates the object and
    // composites it with this opacity (correct self-overlap). The legacy renderer
    // ignores it (renders opaque) — this is a Compositor capability. See
    // docs/Vulkan/COMPOSITOR_PIPELINE.md (Lot 4).
    float             opacity = 1.0f;
    // How this object fuses with the canvas below (Normal = plain alpha over;
    // Erase = knock-out). Like opacity, a Compositor-only compositing param (the
    // legacy ignores it).
    BlendMode         blendMode = BlendMode::Normal;
    // Owning LAYER GROUP (Lot 11/11b), 0 = none. A SEPARATE link from collectionId:
    // a group is a LAYER concept (page-local z-stack), NOT an organisation node, so
    // it must not disturb the collection/page tree. The group itself is a Collection
    // with isLayerGroup=true that carries the group's compositing; this id points at
    // it. All objects of a group live on the SAME page (enforced at group time).
    uint64_t          groupId = 0;
    // Transform constraints (document data). Used by fixed-size / north-oriented
    // symbols (e.g. IOF/ISOM) AND by the per-axis padlocks in the Properties panel:
    // a locked component ignores the matching G/S op (the result is restored to the
    // operation's start value on that axis). Position and scale lock per-axis;
    // rotation locks as a whole.
    bool              lockPosX     = false;   // Move (G) is a no-op on X
    bool              lockPosY     = false;   // Move (G) is a no-op on Y
    bool              lockScaleX   = false;   // Scale (S) is a no-op on local X
    bool              lockScaleY   = false;   // Scale (S) is a no-op on local Y
    bool              lockRotation = false;   // Rotate (R) is a no-op on this shape
    // Whole-scale convenience: true iff BOTH scale axes are locked (used by modules
    // that set a single "fixed size" flag, and by the viewport's fast-path skip).
    bool LockScaleBoth() const { return lockScaleX && lockScaleY; }
    void SetLockScale(bool v) { lockScaleX = lockScaleY = v; }
    // ISOM symbol code ×10 (so 105.1 → 1051, 203.2 → 2032, 101 → 1010), 0 = none.
    // Set by the IOF module so a symbol carries its identity (catalogue lookup,
    // future re-styling). The visible geometry is baked into `parts` at creation.
    int               isomCode     = 0;
    // For module-managed symbols whose LINE STYLE is otherwise locked: allow the
    // user to still change the stroke CAP (some ISOM symbols, e.g. cliffs 201/202,
    // explicitly let the mapper choose butt vs round ends).
    bool              allowCapEdit = false;

    // Convenience accessors for the common single-part case.
    Part&       MainPart()       { return parts.front(); }
    const Part& MainPart() const { return parts.front(); }
    bool        Empty() const    { return parts.empty(); }

    // Bake every parametric part into editable nodes.
    void EnsurePath() { for (Part& p : parts) p.EnsurePath(); }

    // An object's TYPE is its first part's type (after a Join an object may
    // hold mixed parts; Family() is what gates further joins). Empty → Mesh.
    PartType Type() const {
        return parts.empty() ? PartType::Mesh : parts.front().type;
    }
    // The Join family an object belongs to (= PartType, since there are only the
    // two families): Mesh if any part is a Mesh, else Curve. A mixed object
    // can't have joined across families in the first place, so this is
    // well-defined in practice.
    PartType Family() const {
        for (const Part& p : parts)
            if (p.type == PartType::Mesh) return PartType::Mesh;
        return PartType::Curve;
    }
};

} // namespace Renderer

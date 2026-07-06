#pragma once

#include <Renderer/Document/Shape.h>
#include <imgui.h>
#include <cstdint>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  ViewportTools — the drawing-tool state machine for the Viewport editor.
//
//  Active-tool identity lives in the shared ToolManager (so the toolbar and the
//  shortcut system agree). This struct holds only the TRANSIENT state of an
//  in-progress gesture (the shape being dragged out, the polyline/curve points
//  placed so far). One gesture at a time, so a single instance on Application
//  is enough — every Viewport zone funnels its mouse through it, but only the
//  hovered zone drives it.
//
//  Tool ids (registered in RegisterDefaultShortcuts):
//    tool.select  tool.rect  tool.ellipse  tool.triangle  tool.polyline  tool.bezier
// ─────────────────────────────────────────────────────────────────────────────

enum class ToolGesture {
    None,
    DragRect,      // rectangle / ellipse / triangle: press-drag-release bbox
    Polyline,      // click to add points; double-click / Enter to finish
    Bezier,        // click to add anchors; drag to pull a tangent
    MoveObjects,   // Object Mode: drag the selection
    BoxSelect,     // Object/Edit Mode: rubber-band rectangle selection
};

// Blender-style interaction modes. Object = manipulate whole objects; Edit =
// manipulate the geometry (vertices/edges/faces) of the open objects.
enum class EditorMode { Object, Edit };

// Where rotate/scale pivot (Blender's "Transform Pivot Point").
enum class PivotMode {
    BoundingBoxCenter,  // centre of the selection's combined bbox
    Cursor2D,           // the 2D cursor
    IndividualOrigins,  // each object pivots around its own origin
    MedianPoint,        // average of object origins (default)
    ActiveElement,      // the active object's origin
};

// The reference frame a transform (and its X/Y axis constraints) operates in
// (Blender's "Transform Orientation"). Each yields an orthonormal basis in world
// doc-units; Move+X / Move+Y constrain motion to that basis' X / Y axis.
//   • Global → the document axes (1,0)/(0,1).
//   • Local  → the active object's own rotated axes.
//   • View   → the canvas/view axes (== Global until the canvas can rotate).
//   • Cursor → the 2D cursor's axes (== Global until the cursor carries a rotation).
//   • Parent → the active object's PARENT's local axes (Global if it has no parent).
enum class TransformOrientation {
    Global = 0,
    Local,
    View,
    Cursor,
    Parent,
};

inline const char* TransformOrientationName(TransformOrientation o) {
    switch (o) {
        case TransformOrientation::Global: return "Global";
        case TransformOrientation::Local:  return "Local";
        case TransformOrientation::View:   return "View";
        case TransformOrientation::Cursor: return "Cursor";
        case TransformOrientation::Parent: return "Parent";
    }
    return "Global";
}

// Axis a modal transform is constrained to (Blender's X/Y press during G/R/S).
// None = free; X / Y restrict to the orientation basis' corresponding axis.
enum class TransformAxis { None, X, Y };

// A modal G/R/S transform in progress (tool-independent, like Blender). Started
// by the G/R/S shortcuts; confirmed by LMB/Enter, cancelled by Esc/RMB. The
// preview applies live to the selected shapes; cancel restores the snapshot.
enum class TransformKind { None, Move, Rotate, Scale };

struct TransformOp {
    TransformKind kind  = TransformKind::None;
    const void*   owner = nullptr;       // the leaf that started it
    Renderer::Vec2 pivot{};              // world-space pivot (doc-units)
    Renderer::Vec2 startMouse{};         // mouse at start (doc-units, virtual)
    // VIRTUAL mouse position (doc-units): integrated from io.MouseDelta each
    // frame so it keeps travelling past the zone edge while the physical cursor
    // is wrapped to the opposite side. Drift-free (the warp frame's delta is
    // dropped). scale/rotate/move all read this instead of the physical mouse.
    Renderer::Vec2 virt{};
    bool           virtInit = false;     // virt seeded on the first update
    // DISPLAY virtual mouse: integrated WITHOUT the precision factor, so the
    // rotate/scale guide line + oriented cursor follow the cursor's REAL motion
    // (Shift refines the transform but not the pointer/line speed), and it is
    // continuous across edge wraps (so the line keeps its direction, not teleporting).
    Renderer::Vec2 virtDisplay{};
    // Snap SOURCES: the PRE-MOVE world points of the moving selection (captured ONCE
    // at op start — NOT live geometry, which already includes the move and fed back
    // into the snap → jitter). For Center/Median/Active this is a single point (the
    // pivot/active); for Closest it's every moving control point, and each frame the
    // one whose UNSNAPPED-moved position is nearest the cursor's target is the one
    // that snaps (so the geometrically-closest vertex aligns, with no constant offset).
    std::vector<Renderer::Vec2> snapSources;
    bool           snapSourceInit = false;
    // OBJECT-mode: snapshot of each affected shape's transform (by id).
    std::vector<uint64_t>            ids;
    std::vector<Renderer::Transform> snapshot;
    // EDIT-mode (element transform): when `element` is set, the op transforms the
    // selected VERTICES instead of object transforms. `vrefs`/`vsnap` snapshot
    // each selected node (whole Node, in object-local space) for apply + cancel.
    bool                             element = false;
    std::vector<Renderer::VertRef>   vrefs;
    std::vector<Renderer::Node>      vsnap;
    // EDIT-mode selected HANDLES transformed alongside the vertices (so a G/R/S moves
    // the WHOLE selection — vertices AND handles — by the same transform). Only the
    // handle ENDPOINT moves (per the node's HandleMode); `hsnap` is the node snapshot.
    // A handle whose node is also a selected vertex is skipped (it moves with the node).
    std::vector<Renderer::HandleRef> hrefs;
    std::vector<Renderer::Node>      hsnap;
    // Axis constraint (Blender X/Y during the op) + the orientation basis captured
    // at the moment the op started (so the constraint axes stay fixed even if the
    // active object rotates under a Local/Parent orientation mid-op). axisX/axisY
    // are unit vectors in world doc-units. axis==None → unconstrained.
    TransformAxis  axis = TransformAxis::None;
    Renderer::Vec2 axisX{1, 0};
    Renderer::Vec2 axisY{0, 1};
    bool Active() const { return kind != TransformKind::None; }
    void Reset() { kind = TransformKind::None; owner = nullptr; virt = {};
                   virtInit = false; virtDisplay = {}; ids.clear(); snapshot.clear();
                   element = false; vrefs.clear(); vsnap.clear();
                   hrefs.clear(); hsnap.clear();
                   snapSources.clear(); snapSourceInit = false;
                   axis = TransformAxis::None; axisX = {1, 0}; axisY = {0, 1}; }
};

// Transient Edit-Mode gesture: dragging a vertex/handle, or box-selecting
// elements. Element transforms via G/R/S reuse transformOp_ (element variant).
struct EditDragState {
    enum class Kind { None, Verts, HandleIn, HandleOut, Box } kind = Kind::None;
    const void*    owner = nullptr;
    Renderer::Vec2 dragStart{};        // virtual anchor at press (doc-units)
    Renderer::Vec2 moveAccum{};        // integrated virtual mouse (doc-units)
    bool           movedPastThreshold = false;
    // For HandleIn/Out: the vertex whose handle is being dragged.
    Renderer::VertRef handleVert;
    // For Verts: snapshot of each selected vertex's full node, to apply deltas
    // and to support cancel.
    std::vector<Renderer::VertRef> ids;
    std::vector<Renderer::Node>    snapshot;
    // When a plain click lands on an already-selected element AMONG MANY, the press
    // keeps the whole group (so a drag moves it). If the press becomes a CLICK (no
    // drag), the release reduces the selection to just these targets (Blender). Empty
    // = no pending reduction.
    std::vector<Renderer::VertRef> reduceTargets;
    void Reset() { kind = Kind::None; owner = nullptr; moveAccum = {};
                   movedPastThreshold = false; handleVert = {};
                   ids.clear(); snapshot.clear(); reduceTargets.clear(); }
    bool Active() const { return kind != Kind::None; }
};

struct ViewportToolState {
    ToolGesture gesture = ToolGesture::None;

    // The Viewport leaf (its EditorState*) that OWNS the active gesture. With
    // several Viewport zones open, HandleViewportTools runs once per leaf, but
    // only the owner advances the gesture — so a move started in one zone is not
    // re-driven by another zone's mouse mapping.
    const void* owner = nullptr;

    // Drag-create (rect/ellipse/triangle) AND box-select: the two corners
    // (start + current) of the drag, in doc-units.
    Renderer::Vec2 dragStart{};
    Renderer::Vec2 dragNow{};
    bool           dragging = false;
    bool           movedPastThreshold = false;  // distinguishes click vs drag

    // For MoveObjects: the original translate of each selected shape (by id), the
    // virtual-mouse anchor at press (`dragStart`), and `moveAccum` = the VIRTUAL
    // mouse position (doc-units) integrated from the per-frame mouse delta. The
    // displacement is moveAccum − dragStart; motion is integrated from
    // GestureMouseDelta() (which excludes warp jumps), so edge-wrapping never
    // jumps or drifts.
    std::vector<uint64_t>       moveIds;
    std::vector<Renderer::Vec2> moveOrigTranslate;
    Renderer::Vec2              moveAccum{};       // virtual mouse position (doc-units)
    bool                        boxAdditive = false;   // Shift held → add to selection

    // Multi-point tools (polyline/bezier): anchors placed so far, in doc-units.
    // For bezier, `tangents` holds the OUT handle offset pulled at each anchor.
    // `tangentsIn` holds the IN handle offset (toward the PREVIOUS point). The two
    // are ALIGNED (collinear, opposite directions) but NOT mirrored in length —
    // OpenOrienteering-Mapper style: the user drags the OUTER handle, the INNER
    // handle's length is derived from the chord to the previous point so the curve
    // stays smooth. points.size() == tangents.size() == tangentsIn.size().
    std::vector<Renderer::Vec2> points;
    std::vector<Renderer::Vec2> tangents;     // OUT handle offset (dragged)
    std::vector<Renderer::Vec2> tangentsIn;   // IN handle offset (auto, toward prev)
    // Per-point flag: true when the point's handles were COPIED from a followed
    // target curve (Shift) and must NOT be recomputed by RecomputeCurveInHandles
    // (they hold exact, possibly non-aligned, in/out handles). Parallel to points.
    std::vector<uint8_t>        followed;
    bool                        draggingTangent = false;
    // When continuing an EXISTING open path: the shape/part/end the new gesture
    // appends to, and whether it extends the path's START (prepend) vs END (append).
    // shapeContinue == 0 → a brand-new object (the default).
    uint64_t                    shapeContinue = 0;
    int                         partContinue  = -1;
    bool                        continueAtStart = false;
    // True when continuing a genuine OPEN-path endpoint → the drawn strand MERGES
    // into that subpath (the seed node is the existing endpoint, not duplicated).
    // False for an interior / cyclic point → a new subpath (multi-path branch).
    bool                        continueEndpoint = false;
    int                         continueNode = -1;   // the picked endpoint's flat index

    // The artboard index the current gesture targets (chosen at gesture start).
    int targetArtboard = -1;

    // Symbol-styled curve authoring (IOF line/area placement): when set, the
    // Bezier gesture builds a curve carrying THIS shape's part style (stroke /
    // fill / decor / marks) instead of a plain curve, and re-arms for the next one.
    bool            styleActive = false;
    bool            styleClosed = false;   // area symbol → closed + filled
    bool            styleLoose  = false;   // page-less (overprint)
    uint64_t        styleColl   = 0;       // target collection
    Renderer::Shape styleTemplate;         // the symbol shape (style source)
    Renderer::Shape stylePreview;          // compact preview (mini-ghost)
    bool            styleHasPreview = false;

    // ── Follow-curve (Shift): trace the drawn path EXACTLY along a target curve ──
    // OpenOrienteering-Mapper "follow path" while Shift is held:
    //   • the cursor projects onto the nearest target curve (LOCKED to one once an
    //     entry point is placed; no distance limit after). The PROVISIONAL run (the
    //     not-yet-committed segment from the last committed point, along the curve to
    //     the projected cursor) is published as prov* below and consumed by EVERY
    //     preview (transparent styled + blue rubber-band) and the commit, so they all
    //     agree and keep the last point's handle.
    //   • a click freezes the provisional run into the gesture; a click-DRAG keeps
    //     following the curve while also pulling the new point's OUT handle for a
    //     curved exit. Cyclic targets have NO ends — following never wraps the wrong
    //     way; the direction is the one the cursor moves along the curve.
    bool            followAvail   = false;  // a target / projection is available this frame
    bool            followLocked  = false;  // an entry point was placed → tracing the curve
    uint64_t        followShape   = 0;      // locked target shape id (0 = none)
    int             followPart    = -1;     // locked target part index
    int             followSub     = -1;     // locked target subpath index
    float           followAnchorArc = 0.0f;  // arc-length (world) of the locked anchor
    float           followCursorArc = 0.0f;  // arc-length (world) of the projected cursor
    bool            followCursorAscending = true; // last cursor motion went +arc (sign of travel)
    // The PROVISIONAL (not yet committed) run, world coords, parallel arrays like
    // points/tangents/tangentsIn. Rebuilt every follow frame; appended after the
    // committed points by BuildCurvePath / the previews. provFollowed marks which
    // provisional nodes carry exact copied handles.
    std::vector<Renderer::Vec2> provPoints;
    std::vector<Renderer::Vec2> provTangents;
    std::vector<Renderer::Vec2> provTangentsIn;
    std::vector<uint8_t>        provFollowed;

    void Reset() {
        gesture = ToolGesture::None;
        owner = nullptr;
        dragging = false;
        movedPastThreshold = false;
        moveIds.clear();
        moveOrigTranslate.clear();
        moveAccum = {};
        boxAdditive = false;
        points.clear();
        tangents.clear();
        tangentsIn.clear();
        followed.clear();
        draggingTangent = false;
        shapeContinue = 0;
        partContinue = -1;
        continueAtStart = false;
        continueEndpoint = false;
        continueNode = -1;
        targetArtboard = -1;
        styleActive = false;
        styleClosed = false;
        styleLoose = false;
        styleColl = 0;
        styleTemplate = {};
        stylePreview = {};
        styleHasPreview = false;
        followAvail = false;
        followLocked = false;
        followShape = 0;
        followPart = -1;
        followSub = -1;
        followAnchorArc = 0.0f;
        followCursorArc = 0.0f;
        followCursorAscending = true;
        provPoints.clear();
        provTangents.clear();
        provTangentsIn.clear();
        provFollowed.clear();
    }
    // Clear just the provisional follow run (between frames / when follow ends).
    void ClearProvFollow() {
        provPoints.clear(); provTangents.clear();
        provTangentsIn.clear(); provFollowed.clear();
    }
    bool Active() const { return gesture != ToolGesture::None; }
};

} // namespace App

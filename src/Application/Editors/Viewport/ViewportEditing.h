#pragma once

#include <Ink/Document/Document.h>
#include <imgui.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  ViewportEditing — the shared state of the Ink editing loop (docs/Ink/
//  ROADMAP.md Lot 8): selection, Object/Edit mode, transform orientation &
//  pivot, snap settings, the default style for new shapes, the modal
//  transform operation and the command-based document undo.
//
//  One EditContext lives on Application (the selection is per-document, not
//  per-viewport — every Viewport zone shows the same selection, like the
//  legacy stack and Blender).
// ─────────────────────────────────────────────────────────────────────────────

// Where rotate/scale pivot (Blender's "Transform Pivot Point").
enum class PivotMode : uint8_t {
    BoundingBoxCenter = 0,
    Cursor2D,            // needs the 2D cursor (returns in a later pass)
    IndividualOrigins,
    MedianPoint,
    ActiveElement,
};

// The basis transforms happen in (Blender's "Transform Orientation").
enum class TransformOrientation : uint8_t {
    Global = 0,   // document axes
    Local,        // active object's own rotation
    View,         // view axes (the 2D camera never rotates → same as Global)
    Cursor,       // 2D cursor axes (needs the 2D cursor — later pass)
    Parent,       // active object's parent axes (object parenting, Lot 7)
};

enum class EditorMode : uint8_t { Object = 0, Edit };

struct SnapSettings {
    // The legacy bar's full vocabulary; v1 implements Increment (the others
    // are greyed in the Snap dropdown until their passes land).
    enum class Mode : uint8_t { Increment = 0, Grid, Vertex, Edge, Face, EdgeCenter };
    enum class Base : uint8_t { Closest = 0, Pivot, Median, Active };

    bool  enabled = false;                // magnet: snap without holding Ctrl
    Mode  mode = Mode::Increment;
    Base  base = Base::Closest;
    bool  affectMove = true, affectRotate = true, affectScale = true;
    float rotIncrement = 5.0f;            // degrees
    float rotPrecisionIncrement = 1.0f;   // with Shift
    double moveIncrement  = 10.0;         // document units
    double movePrecision  = 1.0;          // with Shift
    double scaleIncrement = 0.1;
    double scalePrecision = 0.01;
};

// ── Command-based document undo ──────────────────────────────────────────────
// A reversible edit expressed over the Document's typed ops. The tool applies
// its change live, then pushes ONE command holding both directions; Undo/Redo
// replay them. Commands hold copies of only what changed (transforms, paths,
// subtree snapshots) — not whole-document snapshots.
struct DocCommand {
    std::string label;
    std::function<void(Ink::Document&)> undo;
    std::function<void(Ink::Document&)> redo;
};

class DocUndoStack {
public:
    void SetCapacity(int n) { capacity_ = n < 2 ? 2 : n; Trim(); }

    void Clear() { cmds_.clear(); index_ = 0; }

    // Push an ALREADY-APPLIED command (drops the redo tail).
    void Push(DocCommand cmd) {
        cmds_.resize((std::size_t)index_);
        cmds_.push_back(std::move(cmd));
        index_ = (int)cmds_.size();
        Trim();
    }

    bool CanUndo() const { return index_ > 0; }
    bool CanRedo() const { return index_ < (int)cmds_.size(); }

    // Returns the label of the step undone/redone ("" if none).
    std::string Undo(Ink::Document& doc) {
        if (!CanUndo()) return "";
        DocCommand& c = cmds_[(std::size_t)--index_];
        if (c.undo) c.undo(doc);
        return c.label;
    }
    std::string Redo(Ink::Document& doc) {
        if (!CanRedo()) return "";
        DocCommand& c = cmds_[(std::size_t)index_++];
        if (c.redo) c.redo(doc);
        return c.label;
    }

    const std::string& NextUndoLabel() const {
        static const std::string kEmpty;
        return CanUndo() ? cmds_[(std::size_t)index_ - 1].label : kEmpty;
    }
    const std::string& NextRedoLabel() const {
        static const std::string kEmpty;
        return CanRedo() ? cmds_[(std::size_t)index_].label : kEmpty;
    }

    // Introspection for the Dev panel.
    int Size() const { return (int)cmds_.size(); }
    int CurrentIndex() const { return index_; }
    const std::string& LabelAt(int i) const { return cmds_[(std::size_t)i].label; }

private:
    void Trim() {
        const int overflow = (int)cmds_.size() - capacity_;
        if (overflow <= 0) return;
        cmds_.erase(cmds_.begin(), cmds_.begin() + overflow);
        index_ = std::max(0, index_ - overflow);
    }
    std::vector<DocCommand> cmds_;
    int index_ = 0;        // commands [0, index_) are applied
    int capacity_ = 256;
};

// ── Modal transform operation (G / R / S, Blender-style) ────────────────────
struct TransformOp {
    enum class Kind : uint8_t { None = 0, Move, Rotate, Scale };
    Kind kind = Kind::None;
    bool editVerts = false;              // Edit Mode: act on selected anchors
    int  axis = -1;                      // −1 free, 0/1 = basis X/Y constraint
    Ink::DVec2 basisX{ 1, 0 }, basisY{ 0, 1 };   // orientation basis (doc space)
    Ink::DVec2 pivot{};                  // doc space
    Ink::DVec2 startDoc{};               // mouse at op start (doc space)
    // Accumulated doc-space motion since the op began, integrated from the
    // per-frame gesture delta so an edge-wrap of the cursor does NOT jump the
    // transform (the "effective cursor" = startDoc + gestureAccum).
    Ink::DVec2 gestureAccum{ 0, 0 };

    struct NodeOrig { Ink::NodeId id; Ink::Transform2D t; };
    std::vector<NodeOrig> nodes;         // Object Mode originals
    Ink::PathData origPath;              // Edit Mode: the before-path
    Ink::NodeId   editNode = Ink::kNullNode;

    const void* leaf = nullptr;          // EditorState* owning the gesture

    bool Active() const { return kind != Kind::None; }
};

// ── Canvas drag gestures (select tool) ───────────────────────────────────────
struct CanvasDrag {
    enum class Kind : uint8_t { None = 0, BoxSelect, DrawRect, DrawEllipse,
                                MoveVerts };
    Kind kind = Kind::None;
    Ink::DVec2 startDoc{};
    Ink::DVec2 curDoc{};
    const void* leaf = nullptr;
    bool  extend = false;                // Shift held at press (box select)
};

// ── The shared editing context ───────────────────────────────────────────────
struct EditContext {
    std::vector<Ink::NodeId> selection;      // insertion order
    Ink::NodeId active = Ink::kNullNode;     // last selected
    EditorMode  mode = EditorMode::Object;
    PivotMode   pivot = PivotMode::MedianPoint;
    TransformOrientation orientation = TransformOrientation::Global;
    SnapSettings snap;

    // Default style for NEW shapes (sRGB, converted at creation time; edited
    // by the top bar's fill/stroke swatches).
    ImVec4 defaultFill{ 0.75f, 0.75f, 0.78f, 1.0f };
    ImVec4 defaultStroke{ 0.10f, 0.11f, 0.12f, 1.0f };
    double defaultStrokeWidth = 2.0;
    bool   defaultFillEnabled = true;
    bool   defaultStrokeEnabled = true;

    // ── Edit Mode element selection ──────────────────────────────────────────
    // Points and the two handles of a point are selected INDIVIDUALLY (Blender-
    // style): an element is (subpath, anchor, part) with part ∈ {Point, In, Out}.
    // Selecting the point does not select its handles and vice-versa; a transform
    // acts on exactly the selected elements.
    enum class ElemPart : uint8_t { Point = 0, In = 1, Out = 2 };
    struct ElemRef {
        int sp = -1, a = -1; ElemPart part = ElemPart::Point;
        bool operator==(const ElemRef& o) const {
            return sp == o.sp && a == o.a && part == o.part;
        }
    };
    std::vector<ElemRef> elemSel;
    // The handle being dragged directly (single-handle drag). part != Point.
    ElemRef handleDrag;   // sp<0 = none

    bool ElemSelected(int sp, int a, ElemPart part) const {
        for (const ElemRef& e : elemSel)
            if (e.sp == sp && e.a == a && e.part == part) return true;
        return false;
    }
    void ElemToggle(int sp, int a, ElemPart part) {
        ElemRef r{ sp, a, part };
        auto it = std::find(elemSel.begin(), elemSel.end(), r);
        if (it != elemSel.end()) elemSel.erase(it); else elemSel.push_back(r);
    }
    void ElemSelectOnly(int sp, int a, ElemPart part) {
        elemSel.clear(); elemSel.push_back({ sp, a, part });
    }
    // Any element of this anchor selected (point or a handle)?
    bool AnchorTouched(int sp, int a) const {
        for (const ElemRef& e : elemSel) if (e.sp == sp && e.a == a) return true;
        return false;
    }

    // The 2D cursor (document space). New shapes spawn here; it is a snap /
    // pivot / orientation reference. Seeded to the first page centre on Reset.
    Ink::DVec2 cursor2D{ 960.0, 540.0 };
    bool       cursor2DValid = false;   // false until first placed / seeded

    bool IsSelected(Ink::NodeId id) const {
        return std::find(selection.begin(), selection.end(), id) !=
               selection.end();
    }
    void SelectOnly(Ink::NodeId id) {
        selection.clear();
        if (id != Ink::kNullNode) selection.push_back(id);
        active = id;
        elemSel.clear();
    }
    void SelectAdd(Ink::NodeId id) {
        if (id == Ink::kNullNode) return;
        if (!IsSelected(id)) selection.push_back(id);
        active = id;
    }
    void Deselect(Ink::NodeId id) {
        selection.erase(std::remove(selection.begin(), selection.end(), id),
                        selection.end());
        if (active == id)
            active = selection.empty() ? Ink::kNullNode : selection.back();
    }
    void Clear() {
        selection.clear();
        active = Ink::kNullNode;
        elemSel.clear();
    }
    // Drop ids that no longer exist (after undo/redo/delete).
    void Prune(const Ink::Document& doc) {
        selection.erase(std::remove_if(selection.begin(), selection.end(),
                            [&](Ink::NodeId id) { return !doc.Find(id); }),
                        selection.end());
        if (active != Ink::kNullNode && !doc.Find(active))
            active = selection.empty() ? Ink::kNullNode : selection.back();
        if (selection.empty()) elemSel.clear();
    }
};

} // namespace App

#include "Application.h"

#include <Shortcuts/ToolManager.h>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editing — object creation, the default style, document commands and
//  the simple editing actions (select-all, delete, duplicate, mode, Apply
//  Scale). The modal transforms and mouse routing live in ViewportInput.cpp;
//  the overlays in ViewportOverlays.cpp; the top bar in ViewportToolbar.cpp.
//  (docs/Ink/ROADMAP.md Lot 8.)
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
// sRGB (straight) → the engine's linear-straight document color.
Ink::Color DocColor(const ImVec4& c) {
    auto lin = [](float u) {
        return u <= 0.04045f ? u / 12.92f
                             : std::pow((u + 0.055f) / 1.055f, 2.4f);
    };
    return { lin(c.x), lin(c.y), lin(c.z), c.w };
}
} // namespace

Ink::Style Application::DefaultStyle() const {
    Ink::Style s;
    if (edit_.defaultFillEnabled) {
        Ink::Fill f;
        f.paint.color = DocColor(edit_.defaultFill);
        s.fills.push_back(f);
    }
    if (edit_.defaultStrokeEnabled) {
        Ink::Stroke st;
        st.paint.color = DocColor(edit_.defaultStroke);
        st.width = edit_.defaultStrokeWidth;
        st.align = Ink::StrokeAlign::Center;
        st.join  = Ink::JoinStyle::Round;
        st.cap   = Ink::CapStyle::Round;
        s.strokes.push_back(st);
    }
    // A shape with neither fill nor stroke would be invisible — guarantee one.
    if (s.fills.empty() && s.strokes.empty()) {
        Ink::Fill f; f.paint.color = DocColor(edit_.defaultFill);
        s.fills.push_back(f);
    }
    return s;
}

void Application::PushDocCommand(const std::string& label,
                                 std::function<void(Ink::Document&)> undo,
                                 std::function<void(Ink::Document&)> redo) {
    DocCommand c;
    c.label = label;
    c.undo  = std::move(undo);
    c.redo  = std::move(redo);
    docUndo_.Push(std::move(c));
    project_.dirty = true;
}

Ink::NodeId Application::SpawnShape(const char* kind) {
    if (!project_.document) return Ink::kNullNode;
    Ink::Document& doc = *project_.document;
    if (doc.Pages().empty()) return Ink::kNullNode;

    // Spawn at the 2D cursor (its default is the first page centre), so the new
    // shape lands where the user placed the cursor — Blender's Add semantics.
    const Ink::Page& page = doc.Pages().front();
    const Ink::NodeId parent = page.id;
    Ink::DVec2 at = edit_.cursor2D;
    if (!edit_.cursor2DValid)
        at = { page.pos.x + page.size.x * 0.5, page.pos.y + page.size.y * 0.5 };

    const double r = 80.0;
    Ink::PathData path;
    std::string name;
    if (!std::strcmp(kind, "rect")) {
        path = Ink::PathData::Rect(at.x - r, at.y - r, r * 2, r * 2);
        name = "Rectangle";
    } else if (!std::strcmp(kind, "ellipse")) {
        path = Ink::PathData::Ellipse(at.x, at.y, r, r);
        name = "Ellipse";
    } else if (!std::strcmp(kind, "triangle")) {
        path = Ink::PathData::Polygon({ { at.x, at.y - r },
                                        { at.x + r, at.y + r },
                                        { at.x - r, at.y + r } });
        name = "Triangle";
    } else if (!std::strcmp(kind, "curve")) {
        // An open Bézier curve: three smooth anchors with tangent handles.
        Ink::Subpath sp; sp.closed = false;
        auto anchor = [&](double x, double y, double ix, double iy) {
            Ink::Anchor an; an.pos = { x, y };
            an.in = { -ix, -iy }; an.out = { ix, iy };
            an.hasIn = an.hasOut = true; an.kind = Ink::AnchorKind::Symmetric;
            sp.anchors.push_back(an);
        };
        anchor(at.x - r,      at.y,        0,  r * 0.6);
        anchor(at.x,          at.y - r*0.6, r*0.6, 0);
        anchor(at.x + r,      at.y,        0, -r * 0.6);
        path.subpaths.push_back(std::move(sp));
        name = "Curve";
    } else {
        path = Ink::PathData::Rect(at.x - r, at.y - r, r * 2, r * 2);
        name = "Shape";
    }

    const Ink::NodeId id = doc.AddPath(parent, path, DefaultStyle(), name);
    if (id == Ink::kNullNode) return id;

    edit_.SelectOnly(id);
    // Undo restores the whole subtree verbatim; redo re-adds a fresh one is
    // wrong (ids differ) — so redo restores the SAME snapshot too.
    auto snap = doc.CopySubtree(id);
    PushDocCommand("Add " + name,
        [id](Ink::Document& d) { d.Remove(id); },
        [snap](Ink::Document& d) { d.RestoreSubtree(snap); });
    LogInfoAction("Add " + name);
    return id;
}

// ── Selection actions ─────────────────────────────────────────────────────────

void Application::Action_SelectAll() {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    edit_.Clear();
    for (const Ink::Page& p : doc.Pages())
        for (Ink::NodeId c : p.children) edit_.SelectAdd(c);
    LogInfoAction("Select All");
}

void Application::Action_DeselectAll() {
    edit_.Clear();
    LogInfoAction("Deselect All");
}

// Distinct anchors (subpath,anchor) touched by the element selection.
static std::vector<std::pair<int,int>> TouchedAnchors(const EditContext& e) {
    std::vector<std::pair<int,int>> out;
    for (const auto& r : e.elemSel) {
        auto k = std::make_pair(r.sp, r.a);
        if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    }
    return out;
}

// Set the handle type of every touched anchor (the V menu). `mode`:
//   0 Free (Corner, keep both handles free)   1 Aligned (Smooth)
//   2 Mirrored (Symmetric)   3 Aligned+Mirrored (Symmetric)
//   4 Vector (Corner, handles point at neighbours → straight)
void Application::Action_SetHandleType(int mode) {
    if (edit_.mode != EditorMode::Edit || !project_.document ||
        edit_.active == Ink::kNullNode || edit_.elemSel.empty()) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(edit_.active);
    if (!n) return;
    const Ink::PathData before = n->path;
    Ink::PathData p = before;
    for (auto [sp, ai] : TouchedAnchors(edit_)) {
        if (sp >= (int)p.subpaths.size() || ai >= (int)p.subpaths[sp].anchors.size()) continue;
        Ink::Subpath& subp = p.subpaths[sp];
        Ink::Anchor& an = subp.anchors[ai];
        if (mode == 4) {
            // Vector: handles aim at the neighbouring anchors (⅓ of the way).
            const int cnt = (int)subp.anchors.size();
            const Ink::Anchor& prev = subp.anchors[(ai - 1 + cnt) % cnt];
            const Ink::Anchor& next = subp.anchors[(ai + 1) % cnt];
            an.in  = { (prev.pos.x - an.pos.x) / 3.0, (prev.pos.y - an.pos.y) / 3.0 };
            an.out = { (next.pos.x - an.pos.x) / 3.0, (next.pos.y - an.pos.y) / 3.0 };
            an.hasIn = an.hasOut = true;
            an.kind = Ink::AnchorKind::Corner;
        } else {
            an.kind = mode == 1 ? Ink::AnchorKind::Smooth
                    : (mode == 2 || mode == 3) ? Ink::AnchorKind::Symmetric
                                               : Ink::AnchorKind::Corner;   // 0 Free
            // If it has no handles yet, seed a small tangent so it becomes editable.
            if (!an.hasIn && !an.hasOut && mode != 0) {
                an.out = { 20, 0 }; an.in = { -20, 0 }; an.hasIn = an.hasOut = true;
            }
            if (an.kind == Ink::AnchorKind::Symmetric && an.hasOut) {
                an.in = { -an.out.x, -an.out.y }; an.hasIn = true;
            } else if (an.kind == Ink::AnchorKind::Smooth && an.hasIn && an.hasOut) {
                const double li = std::hypot(an.in.x, an.in.y), lo = std::hypot(an.out.x, an.out.y);
                if (lo > 1e-9) an.in = { -an.out.x/lo*li, -an.out.y/lo*li };
            }
        }
    }
    doc.SetPath(edit_.active, p);
    const Ink::NodeId id = edit_.active; const Ink::PathData after = p;
    PushDocCommand("Set Handle Type",
        [id, before](Ink::Document& d) { d.SetPath(id, before); },
        [id, after](Ink::Document& d)  { d.SetPath(id, after); });
    LogInfoAction("Set Handle Type");
}

void Application::Action_RemoveHandles() {
    if (edit_.mode != EditorMode::Edit || !project_.document ||
        edit_.active == Ink::kNullNode || edit_.elemSel.empty()) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(edit_.active);
    if (!n) return;
    const Ink::PathData before = n->path;
    Ink::PathData p = before;
    for (auto [sp, ai] : TouchedAnchors(edit_)) {
        if (sp >= (int)p.subpaths.size() || ai >= (int)p.subpaths[sp].anchors.size()) continue;
        Ink::Anchor& an = p.subpaths[sp].anchors[ai];
        an.in = an.out = { 0, 0 }; an.hasIn = an.hasOut = false;
        an.kind = Ink::AnchorKind::Corner;
    }
    doc.SetPath(edit_.active, p);
    const Ink::NodeId id = edit_.active; const Ink::PathData after = p;
    PushDocCommand("Remove Handles",
        [id, before](Ink::Document& d) { d.SetPath(id, before); },
        [id, after](Ink::Document& d)  { d.SetPath(id, after); });
    LogInfoAction("Remove Handles");
}

void Application::Action_OpenHandleMenu() {
    if (edit_.mode != EditorMode::Edit || edit_.elemSel.empty()) return;
    handleMenuRequested_ = true;
    handleMenuPos_ = ImGui::GetIO().MousePos;
}

void Application::Action_DeleteVertices() {
    if (edit_.mode != EditorMode::Edit || !project_.document ||
        edit_.active == Ink::kNullNode || edit_.elemSel.empty()) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(edit_.active);
    if (!n) return;
    const Ink::PathData before = n->path;
    Ink::PathData p = before;
    // Remove touched anchors (highest index first so indices stay valid).
    auto anchors = TouchedAnchors(edit_);
    std::sort(anchors.begin(), anchors.end(), [](auto& a, auto& b){
        return a.first != b.first ? a.first > b.first : a.second > b.second; });
    for (auto [sp, ai] : anchors)
        if (sp < (int)p.subpaths.size() && ai < (int)p.subpaths[sp].anchors.size())
            p.subpaths[sp].anchors.erase(p.subpaths[sp].anchors.begin() + ai);
    p.subpaths.erase(std::remove_if(p.subpaths.begin(), p.subpaths.end(),
        [](const Ink::Subpath& s){ return s.anchors.size() < 2; }), p.subpaths.end());
    edit_.elemSel.clear();
    if (p.Empty()) { edit_.mode = EditorMode::Object; Action_DeleteSelection(); return; }
    doc.SetPath(edit_.active, p);
    const Ink::NodeId id = edit_.active; const Ink::PathData after = p;
    PushDocCommand("Delete Vertices",
        [id, before](Ink::Document& d) { d.SetPath(id, before); },
        [id, after](Ink::Document& d)  { d.SetPath(id, after); });
    LogInfoAction("Delete Vertices");
}

void Application::Action_DeleteSelection() {
    // In Edit mode, X deletes the selected anchors, not the object.
    if (edit_.mode == EditorMode::Edit) { Action_DeleteVertices(); return; }
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    // Snapshot each selected subtree so undo restores them exactly.
    std::vector<Ink::Document::SubtreeSnapshot> snaps;
    for (Ink::NodeId id : edit_.selection)
        if (doc.Find(id)) snaps.push_back(doc.CopySubtree(id));
    if (snaps.empty()) return;
    const int n = (int)snaps.size();
    for (Ink::NodeId id : edit_.selection) doc.Remove(id);
    edit_.Clear();

    PushDocCommand(n == 1 ? "Delete" : "Delete " + std::to_string(n),
        [snaps](Ink::Document& d) {
            // Restore in original order (parents already present as pages).
            for (const auto& s : snaps) d.RestoreSubtree(s);
        },
        [snaps](Ink::Document& d) {
            for (const auto& s : snaps)
                if (!s.nodes.empty()) d.Remove(s.nodes.front().id);
        });
    LogInfoAction("Delete");
}

void Application::Action_DuplicateSelection() {
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    std::vector<Ink::NodeId> copies;
    for (Ink::NodeId id : edit_.selection)
        if (Ink::NodeId c = doc.DuplicateSubtree(id); c != Ink::kNullNode)
            copies.push_back(c);
    if (copies.empty()) return;

    // Reselect the copies (Blender: the duplicate becomes the selection, ready
    // to grab-move). Nudge them slightly so they are visible.
    edit_.Clear();
    for (Ink::NodeId c : copies) {
        if (const Ink::Node* n = doc.Find(c)) {
            Ink::Transform2D t = n->transform;
            t.tx += 20.0; t.ty += 20.0;
            doc.SetTransform(c, t);
        }
        edit_.SelectAdd(c);
    }
    std::vector<Ink::Document::SubtreeSnapshot> snaps;
    for (Ink::NodeId c : copies) snaps.push_back(doc.CopySubtree(c));
    PushDocCommand(copies.size() == 1 ? "Duplicate"
                                      : "Duplicate " + std::to_string(copies.size()),
        [copies](Ink::Document& d) { for (Ink::NodeId c : copies) d.Remove(c); },
        [snaps](Ink::Document& d) { for (const auto& s : snaps) d.RestoreSubtree(s); });
    LogInfoAction("Duplicate");
}

// ── Mode ───────────────────────────────────────────────────────────────────────

void Application::Action_EnterEditMode() {
    if (edit_.active == Ink::kNullNode || !project_.document) return;
    const Ink::Node* n = project_.document->Find(edit_.active);
    if (!n || n->kind != Ink::NodeKind::Path) return;   // only paths editable
    edit_.mode = EditorMode::Edit;
    edit_.elemSel.clear();
    LogInfoAction("Enter Edit Mode");
}

void Application::Action_ExitEditMode() {
    edit_.mode = EditorMode::Object;
    edit_.elemSel.clear();
    LogInfoAction("Exit Edit Mode");
}

void Application::Action_ToggleEditMode() {
    if (edit_.mode == EditorMode::Edit) Action_ExitEditMode();
    else Action_EnterEditMode();
}

void Application::Action_ApplyScale() {
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    // Capture before/after transform + path for each scaled path node.
    struct Rec { Ink::NodeId id; Ink::Transform2D t0, t1; Ink::PathData p0, p1;
                 Ink::Style s0, s1; };
    std::vector<Rec> recs;
    for (Ink::NodeId id : edit_.selection) {
        const Ink::Node* n = doc.Find(id);
        if (!n || n->kind != Ink::NodeKind::Path) continue;
        if (n->transform.sx == 1.0 && n->transform.sy == 1.0) continue;
        Rec r; r.id = id; r.t0 = n->transform; r.p0 = n->path; r.s0 = n->style;
        doc.ApplyScale(id);
        const Ink::Node* m = doc.Find(id);
        r.t1 = m->transform; r.p1 = m->path; r.s1 = m->style;
        recs.push_back(std::move(r));
    }
    if (recs.empty()) return;
    PushDocCommand("Apply Scale",
        [recs](Ink::Document& d) {
            for (const Rec& r : recs) {
                d.SetPath(r.id, r.p0); d.SetStyle(r.id, r.s0);
                d.SetTransform(r.id, r.t0);
            }
        },
        [recs](Ink::Document& d) {
            for (const Rec& r : recs) {
                d.SetPath(r.id, r.p1); d.SetStyle(r.id, r.s1);
                d.SetTransform(r.id, r.t1);
            }
        });
    LogInfoAction("Apply Scale");
}

// ── Modal transform launchers (bound to G / R / S) ──────────────────────────────

void Application::Action_BeginMove() {
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Move, *hoveredViewport_);
}
void Application::Action_BeginRotate() {
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Rotate, *hoveredViewport_);
}
void Application::Action_BeginScale() {
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Scale, *hoveredViewport_);
}
void Application::Action_ConstrainAxisX() {
    if (transformOp_.Active()) transformOp_.axis = (transformOp_.axis == 0) ? -1 : 0;
}
void Application::Action_ConstrainAxisY() {
    if (transformOp_.Active()) transformOp_.axis = (transformOp_.axis == 1) ? -1 : 1;
}

void Application::Action_OpenAddMenu() {
    // Only ARM the menu here — Update() (root window scope) issues the single
    // OpenPopup and renders it, so open/render share one window scope.
    addMenuRequested_ = true;
    addMenuPos_ = ImGui::GetIO().MousePos;
}

void Application::Action_Cursor2DToOrigin() {
    if (!project_.document || project_.document->Pages().empty()) {
        edit_.cursor2D = { 0, 0 };
    } else {
        const Ink::Page& pg = project_.document->Pages().front();
        edit_.cursor2D = { pg.pos.x, pg.pos.y };   // page origin (top-left)
    }
    edit_.cursor2DValid = true;
    LogInfoAction("2D Cursor to Origin");
}

void Application::Action_Cursor2DToSelection() {
    Ink::DRect b;
    if (SelectionBounds(b)) { edit_.cursor2D = b.Center(); edit_.cursor2DValid = true;
        LogInfoAction("2D Cursor to Selection"); }
}

namespace {
// The node's local bbox centre (control-point hull — matches the picking hull).
Ink::DVec2 LocalCentre(const Ink::PathData& p) {
    Ink::DRect b;
    for (const Ink::Subpath& sp : p.subpaths)
        for (const Ink::Anchor& a : sp.anchors) b.Grow(a.pos);
    return b.valid ? b.Center() : Ink::DVec2{ 0, 0 };
}
} // namespace

// Move a path node's ORIGIN to `worldTarget`, keeping the geometry where it is:
// the transform translation moves, and the local anchors shift by the inverse of
// that move expressed in the node's local (rotation·scale) frame.
void Application::MoveOriginTo(Ink::NodeId id, Ink::DVec2 worldTarget) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;

    // Current origin in world = the node's world translation.
    const Ink::DMat23 w = doc.WorldTransform(id);
    const Ink::DVec2 curOrigin{ w.m[2], w.m[5] };
    const Ink::DVec2 dWorld{ worldTarget.x - curOrigin.x, worldTarget.y - curOrigin.y };
    if (std::abs(dWorld.x) < 1e-12 && std::abs(dWorld.y) < 1e-12) return;

    const Ink::PathData before = n->path;
    const Ink::Transform2D tBefore = n->transform;

    // Local delta = (R·S)⁻¹ · dWorld — the linear part of the LOCAL matrix
    // (the parent chain's linear part cancels since both origin and geometry
    // live under it).
    const Ink::Transform2D& t = n->transform;
    const double c = std::cos(t.rotation), s = std::sin(t.rotation);
    // local matrix linear part L = [c*sx, -s*sy; s*sx, c*sy]
    const double a11 = c * t.sx, a12 = -s * t.sy, a21 = s * t.sx, a22 = c * t.sy;
    const double det = a11 * a22 - a12 * a21;
    if (std::abs(det) < 1e-18) return;
    // Express dWorld in the PARENT frame first (strip the parent's linear part).
    // WorldTransform = P ∘ L; dWorld is in world, so dParent = P_linear⁻¹ · dWorld.
    // We approximate with the full world linear part: dLocal = W_linear⁻¹ · dWorld.
    const double w11 = w.m[0], w12 = w.m[1], w21 = w.m[3], w22 = w.m[4];
    const double wdet = w11 * w22 - w12 * w21;
    if (std::abs(wdet) < 1e-18) return;
    const Ink::DVec2 dLocal{ ( w22 * dWorld.x - w12 * dWorld.y) / wdet,
                             (-w21 * dWorld.x + w11 * dWorld.y) / wdet };

    Ink::PathData p = before;
    for (Ink::Subpath& sp : p.subpaths)
        for (Ink::Anchor& an : sp.anchors) { an.pos.x -= dLocal.x; an.pos.y -= dLocal.y; }

    // Move the origin: translate in the PARENT frame, i.e. add dWorld mapped
    // through the parent's inverse. For an unparented node the parent frame is
    // the page, whose linear part is identity → tx/ty += dWorld.
    Ink::Transform2D nt = tBefore;
    nt.tx += dWorld.x;  nt.ty += dWorld.y;

    doc.SetPath(id, p);
    doc.SetTransform(id, nt);
    const Ink::PathData after = p; const Ink::Transform2D tAfter = nt;
    PushDocCommand("Set Origin",
        [id, before, tBefore](Ink::Document& d) { d.SetPath(id, before); d.SetTransform(id, tBefore); },
        [id, after, tAfter](Ink::Document& d)   { d.SetPath(id, after);  d.SetTransform(id, tAfter); });
}

void Application::Action_OriginToGeometry() {
    if (!project_.document) return;
    for (Ink::NodeId id : edit_.selection) {
        const Ink::Node* n = project_.document->Find(id);
        if (!n || n->kind != Ink::NodeKind::Path) continue;
        const Ink::DMat23 w = project_.document->WorldTransform(id);
        MoveOriginTo(id, w.Apply(LocalCentre(n->path)));
    }
    LogInfoAction("Origin to Geometry");
}

void Application::Action_GeometryToOrigin() {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    for (Ink::NodeId id : edit_.selection) {
        const Ink::Node* n = doc.Find(id);
        if (!n || n->kind != Ink::NodeKind::Path) continue;
        const Ink::PathData before = n->path;
        const Ink::DVec2 c = LocalCentre(before);
        if (std::abs(c.x) < 1e-12 && std::abs(c.y) < 1e-12) continue;
        Ink::PathData p = before;
        for (Ink::Subpath& sp : p.subpaths)
            for (Ink::Anchor& an : sp.anchors) { an.pos.x -= c.x; an.pos.y -= c.y; }
        doc.SetPath(id, p);
        const Ink::PathData after = p;
        PushDocCommand("Geometry to Origin",
            [id, before](Ink::Document& d) { d.SetPath(id, before); },
            [id, after](Ink::Document& d)  { d.SetPath(id, after); });
    }
    LogInfoAction("Geometry to Origin");
}

void Application::Action_OriginTo2DCursor() {
    if (!edit_.cursor2DValid) return;
    for (Ink::NodeId id : edit_.selection) MoveOriginTo(id, edit_.cursor2D);
    LogInfoAction("Origin to 2D Cursor");
}

void Application::Action_SelectGroup() {
    if (!project_.document || edit_.active == Ink::kNullNode) return;
    const Ink::Node* n = project_.document->Find(edit_.active);
    if (!n || n->parent == Ink::kNullNode) return;
    edit_.SelectOnly(n->parent);
    LogInfoAction("Select Group");
}

// Blender's Ctrl+P: parent every other selected object to the ACTIVE one
// (world positions preserved). One undoable command via the shared drop op.
void Application::Action_ParentToActive() {
    if (!project_.document || edit_.active == Ink::kNullNode ||
        edit_.selection.size() < 2)
        return;
    std::vector<Ink::NodeId> children;
    for (Ink::NodeId id : edit_.selection)
        if (id != edit_.active && project_.document->Find(id))
            children.push_back(id);
    if (!children.empty()) OutlinerDropParentTo(children, edit_.active);
}

} // namespace App

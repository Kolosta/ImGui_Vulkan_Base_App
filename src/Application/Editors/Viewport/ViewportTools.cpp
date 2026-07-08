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

    // Spawn on the first page, centred on the hovered viewport (or page centre).
    const Ink::Page& page = doc.Pages().front();
    const Ink::NodeId parent = page.id;
    Ink::DVec2 at{ page.pos.x + page.size.x * 0.5,
                   page.pos.y + page.size.y * 0.5 };
    if (hoveredViewport_) {
        // Place at the view centre so the new shape lands where the user looks.
        // (The 2D-cursor placement returns with the cursor tool in a later pass.)
    }

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

void Application::Action_DeleteSelection() {
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
    edit_.vertSel.clear();
    LogInfoAction("Enter Edit Mode");
}

void Application::Action_ExitEditMode() {
    edit_.mode = EditorMode::Object;
    edit_.vertSel.clear();
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
    addMenuOpen_ = true;
    addMenuPos_  = ImGui::GetIO().MousePos;
}

} // namespace App

#include "Application.h"

#include <Ink/Scene/Picking.h>
#include <Shortcuts/ToolManager.h>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport input — the interactive editing loop (docs/Ink/ROADMAP.md Lot 8):
//  the transform frame (pivot + orientation basis), the modal G/R/S operation
//  (live preview, axis constraint, snapping, confirm/cancel) and the active
//  tool's mouse gestures (pick, box-select, draw rect/ellipse).
//
//  Ink is the source of truth for geometry: picking and selection bounds come
//  from the compiled Scene (Ink::PickTop / Scene::NodeBounds), so the editor
//  never re-derives geometry the engine already owns.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
Ink::DVec2 Rotate(Ink::DVec2 v, double c, double s) {
    return { v.x * c - v.y * s, v.x * s + v.y * c };
}
double SnapTo(double v, double inc) {
    return inc > 0.0 ? std::round(v / inc) * inc : v;
}
} // namespace

bool Application::SelectionBounds(Ink::DRect& out) const {
    if (!ink_ || edit_.selection.empty()) return false;
    out = {};
    Ink::DRect nb;
    for (Ink::NodeId id : edit_.selection)
        if (ink_->NodeBounds(id, nb)) {
            out.Grow(nb.min);
            out.Grow(nb.max);
        }
    return out.valid;
}

void Application::ComputeTransformFrame(Ink::DVec2& pivot,
                                        Ink::DVec2& bx, Ink::DVec2& by) const {
    bx = { 1, 0 };  by = { 0, 1 };
    // Orientation basis.
    if (project_.document && edit_.active != Ink::kNullNode &&
        (edit_.orientation == TransformOrientation::Local ||
         edit_.orientation == TransformOrientation::Parent)) {
        const Ink::Node* n = project_.document->Find(edit_.active);
        if (n) {
            Ink::NodeId src = edit_.orientation == TransformOrientation::Parent
                                  ? n->parentId : edit_.active;
            if (src != Ink::kNullNode) {
                const Ink::DMat23 w = project_.document->WorldTransform(src);
                const double a = std::atan2(w.m[3], w.m[0]);
                const double c = std::cos(a), s = std::sin(a);
                bx = { c, s };  by = { -s, c };
            }
        }
    }
    // Pivot.
    Ink::DRect b;
    const bool haveBounds = SelectionBounds(b);
    pivot = haveBounds ? b.Center() : Ink::DVec2{ 0, 0 };
    if (edit_.pivot == PivotMode::ActiveElement && project_.document &&
        edit_.active != Ink::kNullNode) {
        Ink::DRect ab;
        if (ink_->NodeBounds(edit_.active, ab)) pivot = ab.Center();
    } else if (edit_.pivot == PivotMode::MedianPoint && project_.document) {
        // Median of the selected object origins (world translation).
        Ink::DVec2 sum{ 0, 0 }; int n = 0;
        for (Ink::NodeId id : edit_.selection) {
            const Ink::DMat23 w = project_.document->WorldTransform(id);
            sum.x += w.m[2]; sum.y += w.m[5]; ++n;
        }
        if (n > 0) pivot = { sum.x / n, sum.y / n };
    }
}

// ── Modal transform ─────────────────────────────────────────────────────────

void Application::BeginTransform(TransformOp::Kind kind, EditorState& st) {
    if (edit_.selection.empty() && edit_.mode == EditorMode::Object) return;
    if (edit_.mode == EditorMode::Edit &&
        (edit_.active == Ink::kNullNode || edit_.vertSel.empty())) return;
    if (!project_.document) return;

    transformOp_ = TransformOp{};
    transformOp_.kind = kind;
    transformOp_.leaf = &st;
    transformOp_.editVerts = (edit_.mode == EditorMode::Edit);
    ComputeTransformFrame(transformOp_.pivot, transformOp_.basisX, transformOp_.basisY);

    const ImVec2 m = ImGui::GetIO().MousePos;
    transformOp_.startDoc = hoveredCam_.ScreenToDoc(m.x, m.y);

    Ink::Document& doc = *project_.document;
    if (transformOp_.editVerts) {
        const Ink::Node* n = doc.Find(edit_.active);
        if (n) { transformOp_.origPath = n->path; transformOp_.editNode = edit_.active; }
    } else {
        for (Ink::NodeId id : edit_.selection)
            if (const Ink::Node* n = doc.Find(id))
                transformOp_.nodes.push_back({ id, n->transform });
    }
    const char* name = kind == TransformOp::Kind::Move ? "Move"
                     : kind == TransformOp::Kind::Rotate ? "Rotate" : "Scale";
    LogInfoAction(name);
}

void Application::UpdateTransform(const ViewCam& cam) {
    if (!transformOp_.Active() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    const ImGuiIO& io = ImGui::GetIO();
    const Ink::DVec2 cur = cam.ScreenToDoc(io.MousePos.x, io.MousePos.y);
    const bool snap = edit_.snap.enabled ^ io.KeyCtrl;   // magnet XOR Ctrl
    const bool precise = io.KeyShift;

    if (transformOp_.kind == TransformOp::Kind::Move) {
        Ink::DVec2 d{ cur.x - transformOp_.startDoc.x,
                      cur.y - transformOp_.startDoc.y };
        // Axis constraint in the orientation basis.
        if (transformOp_.axis == 0) {
            const double t = d.x * transformOp_.basisX.x + d.y * transformOp_.basisX.y;
            d = { transformOp_.basisX.x * t, transformOp_.basisX.y * t };
        } else if (transformOp_.axis == 1) {
            const double t = d.x * transformOp_.basisY.x + d.y * transformOp_.basisY.y;
            d = { transformOp_.basisY.x * t, transformOp_.basisY.y * t };
        }
        if (snap && edit_.snap.affectMove) {
            const double inc = precise ? edit_.snap.movePrecision
                                       : edit_.snap.moveIncrement;
            d.x = SnapTo(d.x, inc);  d.y = SnapTo(d.y, inc);
        }
        if (transformOp_.editVerts) {
            Ink::PathData p = transformOp_.origPath;
            for (auto [sp, ai] : edit_.vertSel)
                if (sp < (int)p.subpaths.size() &&
                    ai < (int)p.subpaths[sp].anchors.size()) {
                    p.subpaths[sp].anchors[ai].pos.x += d.x;
                    p.subpaths[sp].anchors[ai].pos.y += d.y;
                }
            doc.SetPath(transformOp_.editNode, p);
        } else {
            for (const auto& o : transformOp_.nodes) {
                Ink::Transform2D t = o.t;
                t.tx += d.x;  t.ty += d.y;
                doc.SetTransform(o.id, t);
            }
        }
    } else if (transformOp_.kind == TransformOp::Kind::Rotate) {
        const Ink::DVec2 p = transformOp_.pivot;
        const double a0 = std::atan2(transformOp_.startDoc.y - p.y,
                                     transformOp_.startDoc.x - p.x);
        const double a1 = std::atan2(cur.y - p.y, cur.x - p.x);
        double ang = a1 - a0;
        if (snap && edit_.snap.affectRotate) {
            const double inc = (precise ? edit_.snap.rotPrecisionIncrement
                                        : edit_.snap.rotIncrement) * 3.14159265358979 / 180.0;
            ang = SnapTo(ang, inc);
        }
        const double c = std::cos(ang), s = std::sin(ang);
        for (const auto& o : transformOp_.nodes) {
            Ink::Transform2D t = o.t;
            const Ink::DVec2 rel{ o.t.tx - p.x, o.t.ty - p.y };
            const Ink::DVec2 rr = Rotate(rel, c, s);
            t.tx = p.x + rr.x;  t.ty = p.y + rr.y;
            t.rotation = o.t.rotation + ang;
            doc.SetTransform(o.id, t);
        }
    } else if (transformOp_.kind == TransformOp::Kind::Scale) {
        const Ink::DVec2 p = transformOp_.pivot;
        const double d0 = std::hypot(transformOp_.startDoc.x - p.x,
                                     transformOp_.startDoc.y - p.y);
        const double d1 = std::hypot(cur.x - p.x, cur.y - p.y);
        double f = d0 > 1e-9 ? d1 / d0 : 1.0;
        if (snap && edit_.snap.affectScale) {
            const double inc = precise ? edit_.snap.scalePrecision
                                       : edit_.snap.scaleIncrement;
            f = SnapTo(f, inc);
        }
        double fx = f, fy = f;
        if (transformOp_.axis == 0) fy = 1.0;
        else if (transformOp_.axis == 1) fx = 1.0;
        for (const auto& o : transformOp_.nodes) {
            Ink::Transform2D t = o.t;
            const Ink::DVec2 rel{ o.t.tx - p.x, o.t.ty - p.y };
            t.tx = p.x + rel.x * fx;  t.ty = p.y + rel.y * fy;
            t.sx = o.t.sx * fx;  t.sy = o.t.sy * fy;
            doc.SetTransform(o.id, t);
        }
    }
}

void Application::ConfirmTransform() {
    if (!transformOp_.Active() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    const char* label = transformOp_.kind == TransformOp::Kind::Move ? "Move"
                      : transformOp_.kind == TransformOp::Kind::Rotate ? "Rotate" : "Scale";

    if (transformOp_.editVerts) {
        const Ink::Node* n = doc.Find(transformOp_.editNode);
        Ink::PathData before = transformOp_.origPath;
        Ink::PathData after  = n ? n->path : before;
        const Ink::NodeId id = transformOp_.editNode;
        PushDocCommand(label,
            [id, before](Ink::Document& d) { d.SetPath(id, before); },
            [id, after](Ink::Document& d)  { d.SetPath(id, after); });
    } else {
        std::vector<TransformOp::NodeOrig> before = transformOp_.nodes;
        std::vector<TransformOp::NodeOrig> after;
        for (const auto& o : transformOp_.nodes)
            if (const Ink::Node* n = doc.Find(o.id))
                after.push_back({ o.id, n->transform });
        PushDocCommand(label,
            [before](Ink::Document& d) {
                for (const auto& o : before) d.SetTransform(o.id, o.t);
            },
            [after](Ink::Document& d) {
                for (const auto& o : after) d.SetTransform(o.id, o.t);
            });
    }
    transformOp_ = TransformOp{};
}

void Application::CancelTransform() {
    if (!transformOp_.Active() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    if (transformOp_.editVerts)
        doc.SetPath(transformOp_.editNode, transformOp_.origPath);
    else
        for (const auto& o : transformOp_.nodes) doc.SetTransform(o.id, o.t);
    transformOp_ = TransformOp{};
}

// ── Frame router ──────────────────────────────────────────────────────────────

void Application::HandleViewportInput(EditorState& st, const ViewCam& cam,
                                      bool hovered, const ImVec2& canvasMin,
                                      const ImVec2& canvasSize) {
    (void)canvasMin; (void)canvasSize;
    ImGuiIO& io = ImGui::GetIO();

    // A modal transform owns all input until it confirms or cancels. It can be
    // driven from any hovered viewport, but only the leaf that started it.
    if (transformOp_.Active()) {
        UpdateTransform(cam);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            CancelTransform();
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ConfirmTransform();
        return;
    }

    if (!hovered) return;

    // Exclude the floating overlays (tool palette) from canvas interaction.
    const ImVec2 mp = io.MousePos;
    for (const ImVec4& r : st.overlayRects)
        if (mp.x >= r.x && mp.x <= r.z && mp.y >= r.y && mp.y <= r.w) return;

    const Ink::DVec2 doc = cam.ScreenToDoc(mp.x, mp.y);
    const bool shift = io.KeyShift;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ToolMousePress(st, cam, doc, shift);
    else if (canvasDrag_.kind != CanvasDrag::Kind::None &&
             ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ToolMouseDrag(st, cam, doc);
    else if (canvasDrag_.kind != CanvasDrag::Kind::None &&
             ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        ToolMouseRelease(st, cam, doc);
}

// ── Active tool mouse gestures ────────────────────────────────────────────────

void Application::ToolMousePress(EditorState& st, const ViewCam& cam,
                                 Ink::DVec2 doc, bool shift) {
    const std::string tool = Shortcuts::Tools::ToolManager::Instance().GetActiveTool();

    if (tool == "tool.select") {
        if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode) {
            // Edit Mode: pick the nearest anchor of the active path.
            if (const Ink::Node* n = project_.document->Find(edit_.active)) {
                const Ink::DMat23 w = project_.document->WorldTransform(edit_.active);
                double best = 1e300; int bsp = -1, ba = -1;
                const double tol = 10.0 / cam.zoom;
                for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp)
                    for (int a = 0; a < (int)n->path.subpaths[sp].anchors.size(); ++a) {
                        const Ink::DVec2 wp = w.Apply(n->path.subpaths[sp].anchors[a].pos);
                        const double d2 = (wp.x-doc.x)*(wp.x-doc.x) + (wp.y-doc.y)*(wp.y-doc.y);
                        if (d2 < best) { best = d2; bsp = sp; ba = a; }
                    }
                if (bsp >= 0 && best <= tol * tol) {
                    if (!shift) edit_.vertSel.clear();
                    auto key = std::make_pair(bsp, ba);
                    if (edit_.VertSelected(bsp, ba))
                        edit_.vertSel.erase(std::remove(edit_.vertSel.begin(),
                            edit_.vertSel.end(), key), edit_.vertSel.end());
                    else edit_.vertSel.push_back(key);
                } else if (!shift) {
                    edit_.vertSel.clear();
                }
            }
            return;
        }

        // Object Mode: pick the topmost object.
        Ink::PickOptions opt; opt.tolerance = 4.0 / cam.zoom; opt.zoom = cam.zoom;
        // Compile is up to date (the engine recompiled this frame); pick on it.
        Ink::NodeId hit = ink_ ? ink_->PickAt(doc, opt) : Ink::kNullNode;
        if (hit != Ink::kNullNode) {
            if (shift) {
                if (edit_.IsSelected(hit)) edit_.Deselect(hit);
                else edit_.SelectAdd(hit);
            } else if (!edit_.IsSelected(hit)) {
                edit_.SelectOnly(hit);
            } else {
                edit_.active = hit;
            }
        } else if (!shift) {
            // Empty click → start a box-select rubber band.
            canvasDrag_ = CanvasDrag{};
            canvasDrag_.kind = CanvasDrag::Kind::BoxSelect;
            canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
            canvasDrag_.leaf = &st;
            canvasDrag_.extend = shift;
        }
    } else if (tool == "tool.rect" || tool == "tool.ellipse") {
        canvasDrag_ = CanvasDrag{};
        canvasDrag_.kind = tool == "tool.rect" ? CanvasDrag::Kind::DrawRect
                                               : CanvasDrag::Kind::DrawEllipse;
        canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
        canvasDrag_.leaf = &st;
    }
}

void Application::ToolMouseDrag(EditorState& st, const ViewCam& cam, Ink::DVec2 doc) {
    (void)st; (void)cam;
    if (canvasDrag_.kind != CanvasDrag::Kind::None) canvasDrag_.curDoc = doc;
}

void Application::ToolMouseRelease(EditorState& st, const ViewCam& cam, Ink::DVec2 doc) {
    (void)st; (void)cam;
    if (canvasDrag_.kind == CanvasDrag::Kind::None) return;
    canvasDrag_.curDoc = doc;

    if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect && ink_) {
        const Ink::DVec2 a = canvasDrag_.startDoc, b = canvasDrag_.curDoc;
        const double dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
        if (dx > 2.0 / cam.zoom || dy > 2.0 / cam.zoom) {
            auto hits = ink_->PickInBox({ std::min(a.x, b.x), std::min(a.y, b.y) },
                                        { std::max(a.x, b.x), std::max(a.y, b.y) });
            if (!canvasDrag_.extend) edit_.Clear();
            for (Ink::NodeId id : hits) edit_.SelectAdd(id);
        }
    } else if ((canvasDrag_.kind == CanvasDrag::Kind::DrawRect ||
                canvasDrag_.kind == CanvasDrag::Kind::DrawEllipse) &&
               project_.document && !project_.document->Pages().empty()) {
        const Ink::DVec2 a = canvasDrag_.startDoc, b = canvasDrag_.curDoc;
        const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
        const double w = std::abs(a.x - b.x), h = std::abs(a.y - b.y);
        if (w > 1.0 && h > 1.0) {
            Ink::Document& d = *project_.document;
            const Ink::NodeId page = d.Pages().front().id;
            Ink::PathData path = canvasDrag_.kind == CanvasDrag::Kind::DrawRect
                ? Ink::PathData::Rect(x, y, w, h)
                : Ink::PathData::Ellipse(x + w * 0.5, y + h * 0.5, w * 0.5, h * 0.5);
            const char* nm = canvasDrag_.kind == CanvasDrag::Kind::DrawRect
                                 ? "Rectangle" : "Ellipse";
            const Ink::NodeId id = d.AddPath(page, path, DefaultStyle(), nm);
            edit_.SelectOnly(id);
            auto snap = d.CopySubtree(id);
            PushDocCommand(std::string("Draw ") + nm,
                [id](Ink::Document& doc) { doc.Remove(id); },
                [snap](Ink::Document& doc) { doc.RestoreSubtree(snap); });
            LogInfoAction(std::string("Draw ") + nm);
        }
    }
    canvasDrag_ = CanvasDrag{};
}

} // namespace App

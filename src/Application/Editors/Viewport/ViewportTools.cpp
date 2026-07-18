#include "Application.h"

#include "ViewportMath.h"
#include "ViewportShapes.h"
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

Ink::Style Application::DefaultStyle() const {
    // The default style IS the fill/stroke stacks shown by the Stroke/Fill
    // editors (and their top-bar swatches). Either list may be EMPTY — an
    // explicit "no fill" / "no stroke" the user chose there.
    Ink::Style s;
    s.fills   = edit_.defaultFills;
    s.strokes = edit_.defaultStrokes;
    // Marks are PER-OBJECT annotations (placed at arc positions of one
    // specific path) — a new object never inherits them; only the stroke
    // STYLE (paint/width/dash/caps…) carries over.
    for (Ink::Stroke& st : s.strokes) st.marks.clear();
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
    Ink::DVec2 at = edit_.cursor2D;
    if (!edit_.cursor2DValid)
        at = { page.pos.x + page.size.x * 0.5, page.pos.y + page.size.y * 0.5 };

    // Geometry is built around the LOCAL ORIGIN (0,0) and the node's transform
    // places it at the cursor — so the object's origin sits ON the object
    // (Blender), never left behind at the world origin.
    std::string name;
    Ink::PathData path = BuildShapeGeometry(kind, 80.0, 80.0, name);
    const Ink::NodeId id = doc.AddPath(page.id, path, DefaultStyle(), name);
    if (id == Ink::kNullNode) return id;
    Ink::Transform2D t; t.tx = at.x; t.ty = at.y;
    doc.SetTransform(id, t);
    return FinishSpawn(id, name);
}

// Draw-on-create: build the shape to fill the dragged BOX (min..max in doc
// space) instead of a preset square at the cursor. The origin re-bases to the
// box centre so the object behaves like a spawned one.
Ink::NodeId Application::SpawnShapeInRect(const char* kind, Ink::DVec2 mn,
                                          Ink::DVec2 mx) {
    if (!project_.document) return Ink::kNullNode;
    Ink::Document& doc = *project_.document;
    if (doc.Pages().empty()) return Ink::kNullNode;
    const double cx = (mn.x + mx.x) * 0.5, cy = (mn.y + mx.y) * 0.5;
    const double hw = std::max(1.0, std::abs(mx.x - mn.x) * 0.5);
    const double hh = std::max(1.0, std::abs(mx.y - mn.y) * 0.5);
    std::string name;
    Ink::PathData path = BuildShapeGeometry(kind, hw, hh, name);
    const Ink::NodeId id =
        doc.AddPath(doc.Pages().front().id, path, DefaultStyle(), name);
    if (id == Ink::kNullNode) return id;
    Ink::Transform2D t; t.tx = cx; t.ty = cy;
    doc.SetTransform(id, t);
    return FinishSpawn(id, name);
}

// Commit a freshly built shape node: select it and push the add/undo command.
Ink::NodeId Application::FinishSpawn(Ink::NodeId id, const std::string& name) {
    Ink::Document& doc = *project_.document;
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

// ── Unified draw preview (shapes) ─────────────────────────────────────────────
// A DRAG shape (rectangle / ellipse / triangle / curve box) previews through a
// REAL node — identical geometry, centred origin and default fills+strokes to
// what a release spawns — at a reduced node OPACITY. The pipeline then renders
// the preview exactly like the finished object: fills cut at the contour and
// patterns instanced with the correct Object/Document anchor, just translucent.

void Application::UpdateShapePreview(Ink::Document& doc, const char* kind,
                                     Ink::DVec2 boxMin, Ink::DVec2 boxMax) {
    if (doc.Pages().empty()) return;
    const double cx = (boxMin.x + boxMax.x) * 0.5;
    const double cy = (boxMin.y + boxMax.y) * 0.5;
    const double hw = std::max(1.0, std::abs(boxMax.x - boxMin.x) * 0.5);
    const double hh = std::max(1.0, std::abs(boxMax.y - boxMin.y) * 0.5);
    // Geometry centred on the LOCAL origin, node transform at the box centre —
    // so pattern Object anchor pins to the shape centre, exactly like the
    // spawned object (SpawnShapeInRect).
    std::string name;
    Ink::PathData path = BuildShapeGeometry(kind, hw, hh, name);
    if (shapePreviewNode_ == Ink::kNullNode) {
        shapePreviewNode_ = doc.AddPath(doc.Pages().front().id, std::move(path),
                                        DefaultStyle(), "Preview");
        if (shapePreviewNode_ == Ink::kNullNode) return;
    } else if (doc.Find(shapePreviewNode_)) {
        doc.SetPath(shapePreviewNode_, std::move(path));
        doc.SetStyle(shapePreviewNode_, DefaultStyle());   // live default style
    } else {
        shapePreviewNode_ = Ink::kNullNode;                // vanished — recreate
        return;
    }
    Ink::Transform2D t; t.tx = cx; t.ty = cy;
    doc.SetTransform(shapePreviewNode_, t);
    doc.SetOpacity(shapePreviewNode_, kDrawPreviewAlpha);
}

void Application::ClearShapePreview() {
    if (shapePreviewNode_ == Ink::kNullNode) return;
    if (project_.document && project_.document->Find(shapePreviewNode_))
        project_.document->Remove(shapePreviewNode_);
    shapePreviewNode_ = Ink::kNullNode;
}

// ── Pen: draw-on-create live path construction ────────────────────────────────
// The legacy pen workflow on the Ink model. Anchors are laid in DOCUMENT
// coordinates on a node with an identity transform while drawing; on commit
// the origin re-bases to the geometry centre (MoveOriginTo), so the finished
// object behaves exactly like a spawned one.

void Application::BeginPenDraw(const char* kind) {
    penSpline_ = !std::strcmp(kind, "nurbs") ? Ink::SplineType::Nurbs
               : !std::strcmp(kind, "poly")  ? Ink::SplineType::Poly
                                             : Ink::SplineType::Bezier;
    // "free" = a custom AREA (Bézier spline, keeps a fill); the plain curve
    // kinds are strokes only.
    penIsArea_   = !std::strcmp(kind, "free");
    if (penIsArea_) penSpline_ = Ink::SplineType::Bezier;
    penActive_   = true;
    penDragging_ = false;
    penHasPending_ = false;
    penNode_     = Ink::kNullNode;   // created on the first frozen anchor
    LogInfoAction("Pen", "Click to place anchors; drag pulls handles; "
                         "Enter finishes, Esc cancels");
}

void Application::CommitPenDraw(bool keep) {
    penActive_ = false;
    penDragging_ = false;
    if (!project_.document) {
        penNode_ = penFillNode_ = Ink::kNullNode;
        penHasPending_ = false;
        return;
    }
    Ink::Document& doc = *project_.document;
    // The transient fill-preview node never survives the pen (the fills land
    // on the FINISHED node's own style below).
    if (penFillNode_ != Ink::kNullNode) {
        if (doc.Find(penFillNode_)) doc.Remove(penFillNode_);
        penFillNode_ = Ink::kNullNode;
    }
    // Freeze the pending anchor into the node (unless we are cancelling) so the
    // last placed point is part of the finished object.
    if (keep && penHasPending_) {
        if (penNode_ == Ink::kNullNode && !doc.Pages().empty()) {
            Ink::PathData p; Ink::Subpath sp; sp.spline = penSpline_;
            p.subpaths.push_back(std::move(sp));
            Ink::Style ps = DefaultStyle();
            ps.fills.clear();   // fills live on the fill-preview node until commit
            penNode_ = doc.AddPath(doc.Pages().front().id, std::move(p),
                                   std::move(ps), penIsArea_ ? "Free" : "Bézier");
        }
        if (const Ink::Node* pn = doc.Find(penNode_);
            pn && !pn->path.subpaths.empty()) {
            Ink::PathData p = pn->path;
            p.subpaths.front().anchors.push_back(penPending_);
            doc.SetPath(penNode_, p);
        }
    }
    penHasPending_ = false;
    if (penNode_ == Ink::kNullNode) return;
    const Ink::Node* n = doc.Find(penNode_);
    const bool tooShort = !n || n->path.Empty();
    if (!keep || tooShort) {
        doc.Remove(penNode_);
        penNode_ = Ink::kNullNode;
        return;
    }
    // An AREA gets its fills back on the finished object (they previewed on
    // the companion node while drawing).
    if (penIsArea_) {
        Ink::Style st2 = n->style;
        st2.fills = DefaultStyle().fills;
        doc.SetStyle(penNode_, st2);
    }
    // Origin onto the geometry centre, then ONE undo command for the whole
    // construction (remove/restore the finished subtree verbatim).
    Ink::DRect bb;
    if (ink_ && ink_->NodeBounds(penNode_, bb) && bb.valid)
        MoveOriginTo(penNode_, bb.Center());
    edit_.SelectOnly(penNode_);
    const Ink::NodeId id = penNode_;
    auto snap = doc.CopySubtree(id);
    PushDocCommand("Draw Path",
        [id](Ink::Document& d) { d.Remove(id); },
        [snap](Ink::Document& d) { d.RestoreSubtree(snap); });
    LogInfoAction("Draw Path");
    penNode_ = Ink::kNullNode;
}

void Application::UpdatePenFillPreview(Ink::Document& doc) {
    if (!penIsArea_ || penNode_ == Ink::kNullNode) return;
    const Ink::Node* pn = doc.Find(penNode_);
    if (!pn || pn->path.subpaths.empty()) return;
    // The preview path: frozen anchors + the pending one (its in-progress
    // handles included), CLOSED — the closing segment is the Bézier through
    // last.out / first.in, exactly what the finished close will look like.
    Ink::PathData fp = pn->path;
    Ink::Subpath& sp = fp.subpaths.front();
    if (penHasPending_) sp.anchors.push_back(penPending_);
    sp.closed = true;
    if (sp.anchors.size() < 2) {
        // Backspace shrank the path below a fillable area — drop the preview.
        if (penFillNode_ != Ink::kNullNode && doc.Find(penFillNode_)) {
            doc.Remove(penFillNode_);
            penFillNode_ = Ink::kNullNode;
        }
        return;
    }
    // Re-base the geometry onto its own centre and place the node transform
    // there — so the fill's pattern Object anchor pins to the shape centre,
    // exactly like the committed object (CommitPenDraw → MoveOriginTo(centre)).
    // With an identity transform the geometry sits at absolute coordinates and
    // an Object anchor would always read as a Document anchor. The node renders
    // at a reduced opacity so the fills preview translucent, like a drag shape.
    Ink::DRect cbb;
    for (const Ink::Subpath& s : fp.subpaths)
        for (const Ink::Anchor& a : s.anchors) cbb.Grow(a.pos);
    const Ink::DVec2 c = cbb.valid ? cbb.Center() : Ink::DVec2{ 0, 0 };
    for (Ink::Subpath& s : fp.subpaths)
        for (Ink::Anchor& a : s.anchors) { a.pos.x -= c.x; a.pos.y -= c.y; }
    Ink::Transform2D t; t.tx = c.x; t.ty = c.y;
    if (penFillNode_ == Ink::kNullNode) {
        if (doc.Pages().empty()) return;
        Ink::Style fs;
        fs.fills = DefaultStyle().fills;
        if (fs.fills.empty()) return;         // nothing to preview
        penFillNode_ = doc.AddPath(doc.Pages().front().id, std::move(fp),
                                   std::move(fs), "Fill Preview");
        // Just BELOW the pen node — the solid strokes render above the fill.
        doc.MoveTo(penFillNode_, doc.Pages().front().id,
                   doc.IndexInParent(penNode_));
        doc.SetTransform(penFillNode_, t);
        doc.SetOpacity(penFillNode_, kDrawPreviewAlpha);
    } else if (doc.Find(penFillNode_)) {
        doc.SetPath(penFillNode_, fp);
        doc.SetTransform(penFillNode_, t);
        doc.SetOpacity(penFillNode_, kDrawPreviewAlpha);
    } else {
        penFillNode_ = Ink::kNullNode;        // vanished (undo) — recreate later
    }
}

bool Application::HandlePenInput(EditorState& st, const ViewCam& cam,
                                 bool hovered) {
    (void)st;
    if (!penActive_) return false;
    if (!project_.document) { penActive_ = false; return false; }
    Ink::Document& doc = *project_.document;
    ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CommitPenDraw(false);
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
        (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        CommitPenDraw(true);
        return true;
    }
    const Ink::DVec2 docPos = cam.ScreenToDoc(io.MousePos.x, io.MousePos.y);

    // SHIFT engages follow-curve mode: the actual tracing (diamond, preview and
    // the click that freezes a traced piece) runs in the overlay phase, where the
    // overlay list is available (UpdatePenFollowCurve). The pen must NOT also
    // place a free point on that click, so it yields all placement while Shift is
    // held — the pen only traces along curves in follow mode.
    if (io.KeyShift) { penDragging_ = false; return true; }

    // Ensures the pen node exists (created lazily on the first frozen anchor).
    auto ensureNode = [&]() -> bool {
        if (penNode_ != Ink::kNullNode) return true;
        if (doc.Pages().empty()) return false;
        Ink::PathData p; Ink::Subpath sp; sp.spline = penSpline_;
        p.subpaths.push_back(std::move(sp));
        Ink::Style ps = DefaultStyle();
        ps.fills.clear();   // fills live on the fill-preview node until commit
        penNode_ = doc.AddPath(doc.Pages().front().id, std::move(p), std::move(ps),
                               penIsArea_ ? "Free"
                               : penSpline_ == Ink::SplineType::Nurbs ? "NURBS Path"
                               : penSpline_ == Ink::SplineType::Poly ? "Poly Line"
                                                                     : "Bézier");
        return true;
    };
    // Push the pending anchor into the node (freeze it — becomes opaque).
    auto freezePending = [&]() {
        if (!penHasPending_ || !ensureNode()) return;
        const Ink::Node* n = doc.Find(penNode_);
        if (!n || n->path.subpaths.empty()) return;
        Ink::PathData p = n->path;
        p.subpaths.front().anchors.push_back(penPending_);
        doc.SetPath(penNode_, p);
        penHasPending_ = false;
    };

    // Backspace drops the last point (the pending, else the last frozen anchor).
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (penHasPending_) { penHasPending_ = false; return true; }
        if (penNode_ != Ink::kNullNode) {
            const Ink::Node* n = doc.Find(penNode_);
            if (n && !n->path.subpaths.empty()) {
                Ink::PathData p = n->path;
                auto& an = p.subpaths.front().anchors;
                if (!an.empty()) {
                    // The last frozen anchor becomes the pending again (editable).
                    penPending_ = an.back(); penHasPending_ = true;
                    an.pop_back();
                }
                if (an.empty()) { doc.Remove(penNode_); penNode_ = Ink::kNullNode; }
                else            doc.SetPath(penNode_, p);
            }
        }
        return true;
    }

    // Dragging the pending anchor's handles (Bézier only): the pending is the
    // point just placed and is kept OUT of the node so its segment renders as a
    // TRANSLUCENT preview in the overlay. On RELEASE the pending is FROZEN into
    // the node (the segment becomes the solid, real stroke). Poly/NURBS take no
    // handles, so their point freezes immediately (no drag preview needed).
    if (penDragging_) {
        if (penHasPending_ && penSpline_ == Ink::SplineType::Bezier) {
            const Ink::DVec2 d{ docPos.x - penPending_.pos.x,
                                docPos.y - penPending_.pos.y };
            const bool has = std::abs(d.x) + std::abs(d.y) > 1e-9;
            penPending_.out = d;
            penPending_.in  = { -d.x, -d.y };
            penPending_.hasIn = penPending_.hasOut = has;
            penPending_.kind  = Ink::AnchorKind::Symmetric;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            penDragging_ = false;
            freezePending();          // release → the segment becomes solid
        }
        return true;
    }

    // ── Snap-to-close detection ───────────────────────────────────────────────
    // Within the FIRST anchor's close zone (≥3 frozen anchors in the node) the
    // path connects back. Ctrl suppresses it (a point lands near the start). The
    // preview (closing segment + a Free area's fill) is drawn in the overlay.
    const double kCloseZonePx = 9.0;
    bool snapClose = false;
    if (penNode_ != Ink::kNullNode && !penDragging_ && !io.KeyCtrl && !io.KeyShift) {
        const Ink::Node* n = doc.Find(penNode_);
        if (n && !n->path.subpaths.empty() &&
            n->path.subpaths.front().anchors.size() >= 3) {
            const Ink::DVec2 f = n->path.subpaths.front().anchors.front().pos;
            const Ink::Vec2 fv = cam.DocToView(f.x, f.y);
            const double dpx = std::hypot(io.MousePos.x - cam.canvasMin.x - fv.x,
                                          io.MousePos.y - cam.canvasMin.y - fv.y);
            snapClose = dpx <= kCloseZonePx;
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Snap-close: close the path (any pending is already frozen on release),
        // then commit.
        if (snapClose) {
            const Ink::Node* n = doc.Find(penNode_);
            if (n && !n->path.subpaths.empty()) {
                Ink::PathData cp = n->path;
                cp.subpaths.front().closed = true;
                doc.SetPath(penNode_, cp);
            }
            CommitPenDraw(true);
            return true;
        }
        // A normal click starts a NEW pending at the cursor. Bézier begins a drag
        // to pull its handles (the pending previews translucent, then freezes on
        // release). Poly/NURBS have no handles → freeze the point immediately.
        penPending_ = Ink::Anchor{};
        penPending_.pos = docPos;
        penHasPending_ = true;
        if (penSpline_ == Ink::SplineType::Bezier) {
            penDragging_ = true;
        } else {
            freezePending();   // no drag preview for handleless splines
        }
        return true;
    }
    return true;   // the pen owns viewport input while active
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
    // Line-mark tool with marks selected → X deletes those marks
    // (quasi-objects), never the host object.
    if (MarkToolArmed()) { DeleteSelectedMarks(); return; }
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

// Blender's Shift+D: DUPLICATE then GRAB — a real deep copy of the selection
// (fresh geometry, same collections + layer slot), selected and moved under
// the cursor until LMB confirms (Esc / RMB cancels the whole action).
void Application::Action_DuplicateGrab() {
    if (!project_.document || edit_.selection.empty() ||
        edit_.mode == EditorMode::Edit)
        return;
    Ink::Document& doc = *project_.document;
    std::vector<Ink::NodeId> prev(edit_.selection.begin(), edit_.selection.end());
    std::vector<Ink::NodeId> created;
    for (Ink::NodeId id : prev)
        if (Ink::NodeId c = doc.DuplicateSubtree(id); c != Ink::kNullNode)
            created.push_back(c);
    if (created.empty()) return;

    edit_.Clear();
    for (Ink::NodeId c : created) edit_.SelectAdd(c);
    edit_.active = created.front();

    Action_BeginMove();
    if (transformOp_.Active()) {
        transformOp_.spawned = created;
        transformOp_.spawnedPrevSel = std::move(prev);
    } else {
        std::vector<Ink::Document::SubtreeSnapshot> snaps;
        for (Ink::NodeId c : created) snaps.push_back(doc.CopySubtree(c));
        PushDocCommand("Duplicate",
            [created](Ink::Document& d) { for (Ink::NodeId c : created) d.Remove(c); },
            [snaps](Ink::Document& d) { for (const auto& s : snaps) d.RestoreSubtree(s); });
    }
    LogInfoAction("Duplicate");
}

// Blender's Alt+D: DUPLICATE LINKED — each copy is an Instance node sharing
// the source's data (editing the original updates every copy; transform is
// independent). The copies spawn under the cursor's grab: the move runs until
// LMB confirms; Esc / RMB cancels the WHOLE action (the copies are removed).
void Application::Action_DuplicateLinked() {
    if (!project_.document || edit_.selection.empty() ||
        edit_.mode == EditorMode::Edit)
        return;
    Ink::Document& doc = *project_.document;
    std::vector<Ink::NodeId> prev(edit_.selection.begin(), edit_.selection.end());
    std::vector<Ink::NodeId> created;
    for (Ink::NodeId id : prev) {
        const Ink::Node* n = doc.Find(id);
        if (!n) continue;
        // A linked duplicate of an instance shares the SAME data.
        const Ink::NodeId target =
            n->kind == Ink::NodeKind::Instance ? n->targetRef : id;
        if (!doc.Find(target)) continue;
        const Ink::NodeId parent =
            n->parent != Ink::kNullNode ? n->parent : n->page;
        const Ink::NodeId inst = doc.AddInstance(
            parent, target,
            (n->name.empty() ? std::string("Object") : n->name) + " linked");
        if (inst == Ink::kNullNode) continue;
        // Overlay the source exactly: the instance renders the target's DATA
        // only (the original's object transform is decoupled by default), so
        // its transform is COPIED once from the source here — that copy is
        // the whole link between their placements (Blender's Alt+D).
        doc.SetTransform(inst, n->transform);
        // Land the copy RIGHT AFTER the source in the layer tree, and in the
        // same collections (so it appears next to it in both Outliner views,
        // not orphaned at the top/root).
        doc.MoveTo(inst, parent, doc.IndexInParent(id) + 1);
        for (const Ink::Collection& col : doc.Collections())
            if (std::find(col.members.begin(), col.members.end(), id)
                    != col.members.end())
                doc.AddToCollection(col.id, inst);
        created.push_back(inst);
    }
    if (created.empty()) return;

    edit_.Clear();
    for (Ink::NodeId c : created) edit_.SelectAdd(c);
    edit_.active = created.front();

    // Grab the copies (Blender). If no viewport can host the modal op, the
    // copies stay in place — commit the creation immediately instead.
    Action_BeginMove();
    if (transformOp_.Active()) {
        transformOp_.spawned = created;
        transformOp_.spawnedPrevSel = std::move(prev);
    } else {
        std::vector<Ink::Document::SubtreeSnapshot> snaps;
        for (Ink::NodeId c : created) snaps.push_back(doc.CopySubtree(c));
        PushDocCommand("Duplicate Linked",
            [created](Ink::Document& d) { for (Ink::NodeId c : created) d.Remove(c); },
            [snaps](Ink::Document& d) { for (const auto& s : snaps) d.RestoreSubtree(s); });
    }
    LogInfoAction("Duplicate Linked");
}

// ── Mode ───────────────────────────────────────────────────────────────────────

void Application::Action_EnterEditMode() {
    if (edit_.active == Ink::kNullNode || !project_.document) return;
    const Ink::Node* n = project_.document->Find(edit_.active);
    if (!n || n->kind != Ink::NodeKind::Path) return;   // only paths editable
    // Through SetEditorMode ALWAYS: it owns the per-mode tool switch (a
    // creation tool must never stay active in a mode that doesn't have it).
    SetEditorMode(EditorMode::Edit);
}

void Application::Action_ExitEditMode() {
    SetEditorMode(EditorMode::Object);
}

void Application::Action_ToggleEditMode() {
    // Tab cycles Object ⇄ Edit. From Line-Mark mode, Tab returns to Object.
    if (edit_.mode == EditorMode::LineMark) { SetEditorMode(EditorMode::Object); return; }
    if (edit_.mode == EditorMode::Edit) Action_ExitEditMode();
    else Action_EnterEditMode();
}

void Application::Action_ToggleLineMarkMode() {
    // Shift+Tab enters / leaves the Line-Mark mode.
    SetEditorMode(edit_.mode == EditorMode::LineMark ? EditorMode::Object
                                                     : EditorMode::LineMark);
}

// The tools each editor mode offers. Select and the 2D Cursor are universal;
// the CREATION multi-tools (Shape / Curve) are Object-mode only for now —
// every mode gets its own additions as its workflow lands.
const std::vector<const char*>& Application::ToolsForMode(EditorMode mode) {
    static const std::vector<const char*> kObject = {
        "tool.select", "tool.cursor", "tool.shape", "tool.curve" };
    static const std::vector<const char*> kOther = {
        "tool.select", "tool.cursor" };
    return mode == EditorMode::Object ? kObject : kOther;
}

void Application::SetEditorMode(EditorMode mode) {
    if (edit_.mode == mode) return;
    // Leaving Line-Mark mode drops the mark selection + any in-progress gesture
    // (and any live style preview a ghost applied).
    if (edit_.mode == EditorMode::LineMark) {
        edit_.markSel.clear();
        markGrab_.Reset(); markBox_ = {}; markDrag_ = {};
        ClearMarkPreviewStyle();
    }
    // Leaving Object mode cancels any in-progress creation gesture (pen or
    // shape drag-box) — its tools don't exist in the other modes.
    if (edit_.mode == EditorMode::Object) {
        if (penActive_) CommitPenDraw(/*keep=*/false);
        canvasDrag_ = CanvasDrag{};
    }
    // Per-mode tool memory: remember the leaving mode's tool, restore the
    // entered mode's remembered one (validated — never an impossible tool).
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    edit_.toolByMode[(int)edit_.mode] = tm.GetActiveTool();
    edit_.mode = mode;
    edit_.elemSel.clear();
    {
        std::string next = edit_.toolByMode[(int)mode];
        const auto& allowed = ToolsForMode(mode);
        bool ok = false;
        for (const char* id : allowed) ok = ok || next == id;
        if (!ok || !tm.GetTool(next)) next = "tool.select";
        tm.SetActiveTool(next);
    }
    if (mode == EditorMode::LineMark) {
        // Line-Mark defaults: transform each mark about its OWN origin, in its
        // LOCAL axes (so R/S act along the object's length/width).
        edit_.pivot = PivotMode::IndividualOrigins;
        edit_.orientation = TransformOrientation::Local;
        LogInfoAction("Enter Line Mark Mode");
    } else {
        LogInfoAction(mode == EditorMode::Edit ? "Enter Edit Mode"
                                               : "Enter Object Mode");
    }
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
    // Line-mark tool with marks selected → G slides the marks along the curve.
    if (MarkToolArmed()) { BeginMarkTransform(TransformOp::Kind::Move); return; }
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Move, *hoveredViewport_);
}
void Application::Action_BeginRotate() {
    // Line-mark tool → R flips the selected marks' side (instantaneous).
    if (MarkToolArmed()) { BeginMarkTransform(TransformOp::Kind::Rotate); return; }
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Rotate, *hoveredViewport_);
}
void Application::Action_BeginScale() {
    // Line-mark tool → S scales the selected crossings' gap.
    if (MarkToolArmed()) { BeginMarkTransform(TransformOp::Kind::Scale); return; }
    if (hoveredViewport_) BeginTransform(TransformOp::Kind::Scale, *hoveredViewport_);
}
void Application::Action_ConstrainAxisX() {
    if (markGrab_.Active()) { markGrab_.axis = (markGrab_.axis == 0) ? -1 : 0; return; }
    if (transformOp_.Active()) transformOp_.axis = (transformOp_.axis == 0) ? -1 : 0;
}
void Application::Action_ConstrainAxisY() {
    if (markGrab_.Active()) { markGrab_.axis = (markGrab_.axis == 1) ? -1 : 1; return; }
    if (transformOp_.Active()) transformOp_.axis = (transformOp_.axis == 1) ? -1 : 1;
}

void Application::Action_OpenAddMenu() {
    // Only ARM the menu here — Update() (root window scope) issues the single
    // OpenPopup and renders it, so open/render share one window scope.
    addMenuRequested_ = true;
    addMenuPos_ = ImGui::GetIO().MousePos;
}

// ── Snap pie actions (Shift+S) ────────────────────────────────────────────────
// The legacy 2D snap pie: objects snap BY THEIR ORIGIN (the transform's
// translation), the grid step is the snap move increment.

namespace {
// A node's origin in document space (the translation of its world transform).
Ink::DVec2 NodeOriginWorld(const Ink::Document& doc, Ink::NodeId id) {
    const Ink::DMat23 w = doc.WorldTransform(id);
    return { w.m[2], w.m[5] };
}
} // namespace

// Translate `id` so its ORIGIN lands on the document point `to` (the delta
// converts through the parent's linear transform, so nested nodes land true).
void Application::SnapNodeOriginTo(Ink::NodeId id, Ink::DVec2 to) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    const Ink::DVec2 cur = NodeOriginWorld(doc, id);
    const Ink::DMat23 w  = doc.WorldTransform(id);
    Ink::DMat23 parentW  = w.Compose(vpm::InvertAffine(n->transform.Matrix()));
    parentW.m[2] = parentW.m[5] = 0.0;                 // linear part only
    const Ink::DVec2 dp =
        vpm::InvertAffine(parentW).Apply({ to.x - cur.x, to.y - cur.y });
    Ink::Transform2D t = n->transform;
    t.tx += dp.x;
    t.ty += dp.y;
    doc.SetTransform(id, t);
}

// Move every selected node's origin to targetFor(id), as ONE undo command.
void Application::SnapSelection(const char* label,
                                std::function<Ink::DVec2(Ink::NodeId)> targetFor) {
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    std::vector<std::pair<Ink::NodeId, Ink::Transform2D>> before;
    for (Ink::NodeId id : edit_.selection)
        if (const Ink::Node* n = doc.Find(id))
            before.emplace_back(id, n->transform);
    for (Ink::NodeId id : edit_.selection)
        SnapNodeOriginTo(id, targetFor(id));
    std::vector<std::pair<Ink::NodeId, Ink::Transform2D>> after;
    for (auto& [id, t] : before)
        if (const Ink::Node* n = doc.Find(id)) after.emplace_back(id, n->transform);
    PushDocCommand(label,
        [before](Ink::Document& d) {
            for (auto& [id, t] : before) d.SetTransform(id, t);
        },
        [after](Ink::Document& d) {
            for (auto& [id, t] : after) d.SetTransform(id, t);
        });
    LogInfoAction(label);
}

void Application::Action_SelectionToCursor() {
    const Ink::DVec2 c = edit_.cursor2D;
    SnapSelection("Selection to Cursor", [c](Ink::NodeId) { return c; });
}

void Application::Action_SelectionToActive() {
    if (!project_.document || edit_.active == Ink::kNullNode ||
        edit_.selection.size() < 2)
        return;
    const Ink::DVec2 a = NodeOriginWorld(*project_.document, edit_.active);
    const Ink::NodeId act = edit_.active;
    SnapSelection("Selection to Active", [a, act, this](Ink::NodeId id) {
        return id == act ? NodeOriginWorld(*project_.document, id) : a;
    });
}

double Application::SnapGridStep() const {
    const double inc = edit_.snap.moveIncrement;
    return inc > 1e-9 ? inc : 50.0;
}

void Application::Action_SelectionToGrid() {
    const double g = SnapGridStep();
    Application* app = this;
    SnapSelection("Selection to Grid", [g, app](Ink::NodeId id) {
        const Ink::DVec2 o = NodeOriginWorld(*app->project_.document, id);
        return Ink::DVec2{ std::round(o.x / g) * g, std::round(o.y / g) * g };
    });
}

void Application::Action_Cursor2DToActive() {
    if (!project_.document || edit_.active == Ink::kNullNode) return;
    edit_.cursor2D = NodeOriginWorld(*project_.document, edit_.active);
    edit_.cursor2DValid = true;
    LogInfoAction("Cursor to Active");
}

void Application::Action_Cursor2DToWorldOrigin() {
    edit_.cursor2D = { 0, 0 };
    edit_.cursor2DValid = true;
    LogInfoAction("Cursor to World Origin");
}

void Application::Action_Cursor2DToGrid() {
    const double g = SnapGridStep();
    edit_.cursor2D = { std::round(edit_.cursor2D.x / g) * g,
                       std::round(edit_.cursor2D.y / g) * g };
    edit_.cursor2DValid = true;
    LogInfoAction("Cursor to Grid");
}

void Application::Action_OpenSnapMenu() {
    // Only ARM the menu here — Update() (root window scope) issues the single
    // OpenPopup and renders it (the same rule as the Add menu).
    snapMenuRequested_ = true;
    snapMenuPos_ = ImGui::GetIO().MousePos;
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

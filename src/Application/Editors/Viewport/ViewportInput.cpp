#include "Application.h"

#include "ViewportMath.h"
#include <Ink/Scene/Picking.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Widgets/PopupMenu.h>   // UI::DrawTooltipTranslucent (object picker)
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport input router + active-tool mouse gestures (docs/Ink/ROADMAP.md
//  Lot 8): pick / box-select / draw rect-ellipse / edit-mode element picking
//  and the direct handle drag. The modal G/R/S transform itself lives in
//  ViewportModal.cpp.
//
//  Right-click NEVER acts on the canvas (no selection change, no tool click):
//  it only opens the context menu — except Shift+RMB, which places the 2D
//  cursor (legacy behaviour).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {


// ── Frame router ──────────────────────────────────────────────────────────────

void Application::HandleViewportInput(EditorState& st, const ViewCam& cam,
                                      bool hovered, const ImVec2& canvasMin,
                                      const ImVec2& canvasSize) {
    (void)canvasMin; (void)canvasSize;
    ImGuiIO& io = ImGui::GetIO();

    // The pen (draw-on-create) owns the viewport input while active.
    if (penActive_ && HandlePenInput(st, cam, hovered)) return;

    // A modal transform owns all input until it confirms or cancels. It can be
    // driven from any hovered viewport, but only the leaf that started it.
    if (transformOp_.Active()) {
        UpdateTransform(cam);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            CancelTransform();
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ConfirmTransform();
        return;
    }

    // The line-mark modal op (G/S on marks) owns input — it is driven from
    // HandleMarkTool (overlay phase), so the router must stay inert.
    if (markGrab_.Active()) return;

    // Esc (no modal op): disarm the module place tool first, else clear the
    // mark selection (Line-Mark mode), the selection (Object) or leave Edit.
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput) {
        if (modulePlace_.armed) CancelPlacement();
        else if (MarkModeActive()) edit_.markSel.clear();
        else if (edit_.mode == EditorMode::Edit) Action_ExitEditMode();
        else edit_.Clear();
    }
    // Line-Mark mode: all mouse handling lives in HandleMarkTool (overlay
    // phase — it needs the overlay list for ghosts/handles).
    if (MarkModeActive()) return;

    if (!hovered) return;
    // A popup is up: don't also drive the canvas underneath it. This includes
    // ANY open ImGui popup (a colour picker, a dropdown, a combo…), which can
    // OVERFLOW its editor onto the canvas — a click on it must never fall through
    // to select/deselect here. (The click belongs to the top-most UI.)
    if (addMenuOpen_ || viewportCtxOpen_ || handleMenuOpen_ ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel))
        return;
    // The Outliner's "pick a viewport to synchronise" action owns the mouse:
    // its own block confirms with LMB / cancels with RMB — the canvas tools
    // and the context menu must stay inert for the whole gesture.
    if (outlinerPickingState_) return;

    // Exclude the floating overlays (tool palette) from canvas interaction.
    // (These are LAST frame's rects — the palette draws after input — which is
    // exactly right: they are stable corner anchors.)
    const ImVec2 mp = io.MousePos;
    for (const ImVec4& r : st.overlayRects)
        if (mp.x >= r.x && mp.x <= r.z && mp.y >= r.y && mp.y <= r.w) return;

    const Ink::DVec2 doc = cam.ScreenToDoc(mp.x, mp.y);
    const bool shift = io.KeyShift;

    // Object eyedropper active (a Properties node picker): the next LEFT click
    // delivers the object under the cursor to the picker; a right click / Esc
    // cancels. Clicking empty canvas also cancels (no object). No selection
    // change happens either way.
    if (ObjectPickActive()) {
        Ink::PickOptions opt; opt.tolerance = 6.0 / cam.zoom; opt.zoom = cam.zoom;
        const Ink::NodeId hit = ink_ ? ink_->PickAt(doc, opt) : Ink::kNullNode;
        // Hovering a compatible object shows its NAME (same tooltip widget as
        // the rest of the app), so the user sees what the click will pick.
        if (hit != Ink::kNullNode && project_.document) {
            if (const Ink::Node* hn = project_.document->Find(hit))
                UI::DrawTooltip(hn->name.empty() ? "(unnamed)" : hn->name.c_str(),
                                ImGui::GetIO().MousePos);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hit != Ink::kNullNode) DeliverObjectPick(hit);
            else CancelObjectPick();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            CancelObjectPick();
        }
        return;
    }

    // Armed module "place symbol" tool: LEFT click drops the symbol at the
    // cursor (the module's callback creates + routes the object); RIGHT click
    // disarms (Esc is handled above). Repeat placements stay armed.
    if (modulePlace_.armed) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto req = modulePlace_.req;   // survives a disarm in onPlace
            if (req.onPlace) req.onPlace(doc.x, doc.y);
            if (!req.repeat) CancelPlacement();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            CancelPlacement();
        }
        return;
    }

    // Shift+RMB (press or drag): place the 2D cursor — the legacy gesture.
    // Checked BEFORE the context menu so the two never fight over the button.
    if (shift && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                  ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
        edit_.cursor2D = doc;
        edit_.cursor2DValid = true;
        return;
    }

    // Right-click: ONLY open the context menu — never a canvas click, never a
    // selection change. The menu is driven by the CURRENT SELECTION (Blender),
    // not by what happens to be under the cursor.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        viewportCtxNode_ = edit_.active;
        viewportCtxPos_  = mp;
        viewportCtxRequested_ = true;   // Update() opens + renders it (root scope)
        return;
    }

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

    // Shape / Curve creation tools (the palette multi-tools). Drag-box kinds
    // build the shape in the dragged box (PERSISTENT — the tool stays armed);
    // pen kinds start the pen and place the first anchor with THIS click (the
    // pen owns input from the next frame on).
    if (tool == "tool.shape" || tool == "tool.curve") {
        const std::string kind = tool == "tool.shape" ? toolShapeKind_
                                                      : toolCurveKind_;
        const bool penKind = kind == "free" || kind == "curve" ||
                             kind == "nurbs" || kind == "poly";
        if (penKind) {
            BeginPenDraw(kind.c_str());
            penPending_ = Ink::Anchor{};
            penPending_.pos = doc;
            penHasPending_ = true;
            penDragging_ = (penSpline_ == Ink::SplineType::Bezier);
        } else {
            canvasDrag_ = CanvasDrag{};
            canvasDrag_.kind = CanvasDrag::Kind::DrawShape;
            canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
            canvasDrag_.leaf = &st;
            canvasDrag_.shapeKind = kind;
        }
        return;
    }

    // 2D cursor tool: click places the cursor (Blender's Shift-RMB equivalent).
    if (tool == "tool.cursor") {
        edit_.cursor2D = doc;
        edit_.cursor2DValid = true;
        return;
    }

    if (tool == "tool.select") {
        if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode) {
            const Ink::Node* n = project_.document->Find(edit_.active);
            if (!n) return;
            const Ink::DMat23 w = project_.document->WorldTransform(edit_.active);
            const double tol = 9.0 / cam.zoom, tol2 = tol * tol;
            using ElemPart = EditContext::ElemPart;

            // Pick the nearest element (a handle of a touched anchor beats a
            // point; handles are only shown/pickable for touched anchors).
            int bsp = -1, ba = -1; ElemPart bpart = ElemPart::Point; double best = tol2;
            for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp)
                for (int a = 0; a < (int)n->path.subpaths[sp].anchors.size(); ++a) {
                    const Ink::Anchor& an = n->path.subpaths[sp].anchors[a];
                    const Ink::DVec2 pp = w.Apply(an.pos);
                    const double dp = (pp.x-doc.x)*(pp.x-doc.x) + (pp.y-doc.y)*(pp.y-doc.y);
                    if (dp < best) { best = dp; bsp = sp; ba = a; bpart = ElemPart::Point; }
                    if (edit_.AnchorTouched(sp, a)) {
                        if (an.hasIn) {
                            const Ink::DVec2 hp = w.Apply({ an.pos.x+an.in.x, an.pos.y+an.in.y });
                            const double d = (hp.x-doc.x)*(hp.x-doc.x) + (hp.y-doc.y)*(hp.y-doc.y);
                            if (d < best) { best = d; bsp = sp; ba = a; bpart = ElemPart::In; }
                        }
                        if (an.hasOut) {
                            const Ink::DVec2 hp = w.Apply({ an.pos.x+an.out.x, an.pos.y+an.out.y });
                            const double d = (hp.x-doc.x)*(hp.x-doc.x) + (hp.y-doc.y)*(hp.y-doc.y);
                            if (d < best) { best = d; bsp = sp; ba = a; bpart = ElemPart::Out; }
                        }
                    }
                }

            if (bsp >= 0) {
                // A handle drag starts immediately; a point selection may start a move.
                if (shift) edit_.ElemToggle(bsp, ba, bpart);
                else if (!edit_.ElemSelected(bsp, ba, bpart))
                    edit_.ElemSelectOnly(bsp, ba, bpart);
                if (bpart != ElemPart::Point) {
                    // Direct handle drag (one undo command on release).
                    edit_.handleDrag = { bsp, ba, bpart };
                    canvasDrag_ = CanvasDrag{};
                    canvasDrag_.kind = CanvasDrag::Kind::MoveVerts;
                    canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
                    canvasDrag_.leaf = &st;
                    transformOp_.origPath = n->path;
                    transformOp_.editNode = edit_.active;
                }
                return;
            }
            // Empty click → box-select over anchors. Shift EXTENDS the element
            // selection (Blender), a plain drag replaces it.
            if (!shift) edit_.elemSel.clear();
            canvasDrag_ = CanvasDrag{};
            canvasDrag_.kind = CanvasDrag::Kind::BoxSelect;
            canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
            canvasDrag_.leaf = &st;
            canvasDrag_.extend = shift;
            return;
        }

        // Object Mode: pick the topmost object.
        Ink::PickOptions opt; opt.tolerance = 4.0 / cam.zoom; opt.zoom = cam.zoom;
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
        } else {
            // Empty click → rubber-band. Shift EXTENDS the selection with the
            // boxed objects (Blender); a plain drag replaces it.
            if (!shift) edit_.Clear();
            canvasDrag_ = CanvasDrag{};
            canvasDrag_.kind = CanvasDrag::Kind::BoxSelect;
            canvasDrag_.startDoc = canvasDrag_.curDoc = doc;
            canvasDrag_.leaf = &st;
            canvasDrag_.extend = shift;
        }
    }
}

void Application::ToolMouseDrag(EditorState& st, const ViewCam& cam, Ink::DVec2 doc) {
    (void)st; (void)cam;
    if (canvasDrag_.kind == CanvasDrag::Kind::None) return;
    canvasDrag_.curDoc = doc;

    // Direct handle drag (Edit mode): move the selected in/out handle live,
    // honouring the anchor's kind (Smooth keeps the opposite collinear,
    // Symmetric mirrors it).
    if (canvasDrag_.kind == CanvasDrag::Kind::MoveVerts &&
        edit_.handleDrag.part != EditContext::ElemPart::Point && project_.document) {
        Ink::Document& d = *project_.document;
        const Ink::Node* n = d.Find(edit_.active);
        if (!n) return;
        const Ink::DMat23 w = d.WorldTransform(edit_.active);
        const Ink::DMat23 wi = vpm::InvertAffine(w);
        Ink::PathData p = n->path;
        auto& an = p.subpaths[edit_.handleDrag.sp].anchors[edit_.handleDrag.a];
        const bool inSide = (edit_.handleDrag.part == EditContext::ElemPart::In);
        const Ink::DVec2 local = wi.Apply(doc);
        const Ink::DVec2 rel{ local.x - an.pos.x, local.y - an.pos.y };
        if (inSide) { an.in = rel; an.hasIn = true; }
        else        { an.out = rel; an.hasOut = true; }
        // Keep the opposite handle consistent with the anchor kind.
        if (an.kind == Ink::AnchorKind::Symmetric) {
            if (inSide) { an.out = { -rel.x, -rel.y }; an.hasOut = true; }
            else        { an.in  = { -rel.x, -rel.y }; an.hasIn  = true; }
        } else if (an.kind == Ink::AnchorKind::Smooth) {
            const double len = std::hypot(rel.x, rel.y);
            Ink::DVec2& op = inSide ? an.out : an.in;
            const double olen = std::hypot(op.x, op.y);
            if (len > 1e-9 && olen > 1e-9) {
                op = { -rel.x / len * olen, -rel.y / len * olen };
                if (inSide) an.hasOut = true; else an.hasIn = true;
            }
        }
        d.SetPath(edit_.active, p);
    }
}

void Application::ToolMouseRelease(EditorState& st, const ViewCam& cam, Ink::DVec2 doc) {
    (void)st; (void)cam;
    if (canvasDrag_.kind == CanvasDrag::Kind::None) return;
    canvasDrag_.curDoc = doc;

    if (canvasDrag_.kind == CanvasDrag::Kind::MoveVerts && project_.document) {
        // Commit the handle drag as one undo command.
        Ink::Document& d = *project_.document;
        const Ink::Node* n = d.Find(transformOp_.editNode);
        if (n) {
            const Ink::NodeId id = transformOp_.editNode;
            Ink::PathData before = transformOp_.origPath, after = n->path;
            PushDocCommand("Edit Handle",
                [id, before](Ink::Document& dc) { dc.SetPath(id, before); },
                [id, after](Ink::Document& dc)  { dc.SetPath(id, after); });
        }
        edit_.handleDrag = {};
        transformOp_ = TransformOp{};
    } else if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect && edit_.mode == EditorMode::Edit &&
               project_.document && edit_.active != Ink::kNullNode) {
        // Edit-mode box select over anchor POINTS of the active path.
        const Ink::Node* n = project_.document->Find(edit_.active);
        if (n) {
            const Ink::DMat23 w = project_.document->WorldTransform(edit_.active);
            const Ink::DVec2 a = canvasDrag_.startDoc, b = canvasDrag_.curDoc;
            const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
            const double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            if (!canvasDrag_.extend) edit_.elemSel.clear();   // Shift extends
            for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp)
                for (int an = 0; an < (int)n->path.subpaths[sp].anchors.size(); ++an) {
                    const Ink::DVec2 wp = w.Apply(n->path.subpaths[sp].anchors[an].pos);
                    if (wp.x >= x0 && wp.x <= x1 && wp.y >= y0 && wp.y <= y1 &&
                        !edit_.ElemSelected(sp, an, EditContext::ElemPart::Point))
                        edit_.elemSel.push_back({ sp, an, EditContext::ElemPart::Point });
                }
        }
    } else if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect && ink_) {
        const Ink::DVec2 a = canvasDrag_.startDoc, b = canvasDrag_.curDoc;
        const double dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
        if (dx > 2.0 / cam.zoom || dy > 2.0 / cam.zoom) {
            auto hits = ink_->PickInBox({ std::min(a.x, b.x), std::min(a.y, b.y) },
                                        { std::max(a.x, b.x), std::max(a.y, b.y) });
            if (!canvasDrag_.extend) edit_.Clear();
            for (Ink::NodeId id : hits) edit_.SelectAdd(id);
        }
    } else if (canvasDrag_.kind == CanvasDrag::Kind::DrawShape) {
        // Shape/Curve tool: build the armed kind in the dragged box. A
        // near-zero drag (a plain click) falls back to a preset at the click.
        // The tool stays ARMED — the next drag draws another one.
        ClearShapePreview();          // drop the translucent preview node
        const Ink::DVec2 a = canvasDrag_.startDoc, b = canvasDrag_.curDoc;
        const std::string kind = canvasDrag_.shapeKind;
        const double w = std::abs(a.x - b.x), h = std::abs(a.y - b.y);
        if (w > 1.0 && h > 1.0)
            SpawnShapeInRect(kind.c_str(),
                             { std::min(a.x, b.x), std::min(a.y, b.y) },
                             { std::max(a.x, b.x), std::max(a.y, b.y) });
        else {
            edit_.cursor2D = a; edit_.cursor2DValid = true;
            SpawnShape(kind.c_str());
        }
    }
    canvasDrag_ = CanvasDrag{};
}

} // namespace App

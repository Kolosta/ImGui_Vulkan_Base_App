#include "Application.h"

#include "ViewportMath.h"
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport modal transform (G / R / S) — docs/Ink/ROADMAP.md Lot 8.
//
//  Mouse model (Blender's grab): for the whole operation the OS cursor is
//  captured in SDL RELATIVE mouse mode — it is hidden and frozen, and we
//  receive raw xrel/yrel deltas (accumulated in ProcessEvents). There is NO
//  cursor warp during the op, so the accumulation cannot drift, ever. From
//  that single motion source we derive:
//    • gestureAccum (doc space, precision-scaled) — drives the transform;
//    • virtPx (screen px, real speed, unbounded)  — the guide line's end;
//    • the DISPLAYED cursor = virtPx folded into the canvas rect.
//  Whenever virtPx is inside the canvas, displayed == virtPx exactly: the
//  cursor and the line tip are the same pixel by construction.
//
//  The cursor itself is the legacy icon set: the multi-directional glyph for
//  Move, the double-arrow rotated PARALLEL to the guide for Scale and TANGENT
//  (perpendicular to the radius) for Rotate, tinted by the C_Cursor_Color
//  token and drawn on the ImGui foreground list (window chrome, not canvas).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ── Mouse capture ─────────────────────────────────────────────────────────────

void Application::SetModalMouseCapture(bool on) {
    if (on == modalRelMode_) return;
    modalRelMode_ = on;
    modalRelAccum_ = ImVec2(0, 0);
    // Relative mode reports RAW deltas by default (no OS pointer speed /
    // acceleration) — the cursor would feel much faster than the user's
    // Windows setting. This hint applies the system scale to relative motion.
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
    if (window_) SDL_SetWindowRelativeMouseMode(window_, on);
}

// Fold an unbounded virtual point into the canvas rect (pure math — no warp).
ImVec2 Application::WrapPointInCanvas(ImVec2 p) const {
    const ImVec2 mn = canvasRectMin_, mx = canvasRectMax_;
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    if (w < 16.0f || h < 16.0f) return p;
    auto fold = [](float v, float lo, float span) {
        float t = std::fmod(v - lo, span);
        if (t < 0.0f) t += span;
        return lo + t;
    };
    return ImVec2(fold(p.x, mn.x, w), fold(p.y, mn.y, h));
}

// ── Selection bounds / transform frame ───────────────────────────────────────

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
    // Cursor orientation: the 2D cursor has no rotation → same as Global.

    // ── Edit mode: pivot from the SELECTED ELEMENTS (points + handles), not
    // the object. The 2D cursor / median / active rules apply to those points.
    if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode &&
        project_.document && !edit_.elemSel.empty()) {
        const Ink::Node* n = project_.document->Find(edit_.active);
        const Ink::DMat23 w = n ? project_.document->WorldTransform(edit_.active)
                                : Ink::DMat23{};
        auto elemWorld = [&](const EditContext::ElemRef& e) -> Ink::DVec2 {
            const Ink::Anchor& an = n->path.subpaths[e.sp].anchors[e.a];
            if (e.part == EditContext::ElemPart::In)
                return w.Apply({ an.pos.x + an.in.x, an.pos.y + an.in.y });
            if (e.part == EditContext::ElemPart::Out)
                return w.Apply({ an.pos.x + an.out.x, an.pos.y + an.out.y });
            return w.Apply(an.pos);
        };
        if (edit_.pivot == PivotMode::Cursor2D && edit_.cursor2DValid) {
            pivot = edit_.cursor2D;
        } else if (edit_.pivot == PivotMode::ActiveElement) {
            pivot = elemWorld(edit_.elemSel.back());   // last-picked element
        } else {
            Ink::DVec2 sum{ 0, 0 }; int cnt = 0;
            for (const auto& e : edit_.elemSel) {
                const Ink::DVec2 p = elemWorld(e);
                sum.x += p.x; sum.y += p.y; ++cnt;
            }
            if (cnt > 0) pivot = { sum.x / cnt, sum.y / cnt };
        }
        return;
    }

    // ── Object mode pivot ──
    Ink::DRect b;
    const bool haveBounds = SelectionBounds(b);
    pivot = haveBounds ? b.Center() : Ink::DVec2{ 0, 0 };
    if (edit_.pivot == PivotMode::Cursor2D && edit_.cursor2DValid) {
        pivot = edit_.cursor2D;
    } else if (edit_.pivot == PivotMode::ActiveElement && project_.document &&
               edit_.active != Ink::kNullNode) {
        Ink::DRect ab;
        if (ink_->NodeBounds(edit_.active, ab)) pivot = ab.Center();
    } else if (edit_.pivot == PivotMode::MedianPoint && project_.document) {
        Ink::DVec2 sum{ 0, 0 }; int n = 0;
        for (Ink::NodeId id : edit_.selection) {
            const Ink::DMat23 w = project_.document->WorldTransform(id);
            sum.x += w.m[2]; sum.y += w.m[5]; ++n;
        }
        if (n > 0) pivot = { sum.x / n, sum.y / n };
    }
}

// ── Modal lifecycle ───────────────────────────────────────────────────────────

void Application::BeginTransform(TransformOp::Kind kind, EditorState& st) {
    if (edit_.selection.empty() && edit_.mode == EditorMode::Object) return;
    if (edit_.mode == EditorMode::Edit &&
        (edit_.active == Ink::kNullNode || edit_.elemSel.empty())) return;
    if (!project_.document) return;

    transformOp_ = TransformOp{};
    transformOp_.kind = kind;
    transformOp_.leaf = &st;
    transformOp_.editVerts = (edit_.mode == EditorMode::Edit);
    ComputeTransformFrame(transformOp_.pivot, transformOp_.basisX, transformOp_.basisY);

    const ImVec2 m = ImGui::GetIO().MousePos;
    transformOp_.startDoc = hoveredCam_.ScreenToDoc(m.x, m.y);
    transformOp_.gestureAccum = { 0, 0 };
    transformOp_.virtPx = { (double)m.x, (double)m.y };
    SetModalMouseCapture(true);            // grab: raw deltas, no warps, no drift
    osCursorHidden_ = true;                // Update forces the None cursor too

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

// Release the capture and land the OS cursor where the DISPLAYED cursor was
// (the wrapped virtual position) — one warp AFTER the op, when nothing is
// accumulating any more, so its event timing cannot matter.
void Application::EndModalCapture() {
    const ImVec2 land = WrapPointInCanvas(
        ImVec2((float)transformOp_.virtPx.x, (float)transformOp_.virtPx.y));
    SetModalMouseCapture(false);
    if (window_) SDL_WarpMouseInWindow(window_, land.x, land.y);
    osCursorHidden_ = false;
}

void Application::UpdateTransform(const ViewCam& cam) {
    if (!transformOp_.Active() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const bool precise = io.KeyShift;

    // Drain the raw relative motion gathered by ProcessEvents. One source
    // feeds both accumulators: the transform (precision-scaled, doc space)
    // and the virtual display cursor (real speed, screen px, unbounded).
    const ImVec2 rel = modalRelAccum_;
    modalRelAccum_ = ImVec2(0, 0);
    transformOp_.virtPx.x += rel.x;
    transformOp_.virtPx.y += rel.y;
    const double pf = precise ? 0.1 : 1.0;
    transformOp_.gestureAccum.x += (double)rel.x * pf / cam.zoom;
    transformOp_.gestureAccum.y += (double)rel.y * pf / cam.zoom;
    const Ink::DVec2 cur{ transformOp_.startDoc.x + transformOp_.gestureAccum.x,
                          transformOp_.startDoc.y + transformOp_.gestureAccum.y };
    const bool snap = edit_.snap.enabled ^ io.KeyCtrl;   // magnet XOR Ctrl

    const Ink::DVec2 P = transformOp_.pivot;

    // Build a world-space point transform for the current op.
    Ink::DVec2 moveD{ cur.x - transformOp_.startDoc.x, cur.y - transformOp_.startDoc.y };
    double ang = 0.0, fx = 1.0, fy = 1.0;
    if (transformOp_.kind == TransformOp::Kind::Move) {
        if (transformOp_.axis == 0) {
            const double t = moveD.x*transformOp_.basisX.x + moveD.y*transformOp_.basisX.y;
            moveD = { transformOp_.basisX.x*t, transformOp_.basisX.y*t };
        } else if (transformOp_.axis == 1) {
            const double t = moveD.x*transformOp_.basisY.x + moveD.y*transformOp_.basisY.y;
            moveD = { transformOp_.basisY.x*t, transformOp_.basisY.y*t };
        }
        if (snap && edit_.snap.affectMove) {
            const double inc = precise ? edit_.snap.movePrecision : edit_.snap.moveIncrement;
            moveD.x = vpm::SnapTo(moveD.x, inc); moveD.y = vpm::SnapTo(moveD.y, inc);
        }
    } else if (transformOp_.kind == TransformOp::Kind::Rotate) {
        const double a0 = std::atan2(transformOp_.startDoc.y-P.y, transformOp_.startDoc.x-P.x);
        const double a1 = std::atan2(cur.y-P.y, cur.x-P.x);
        ang = a1 - a0;
        if (snap && edit_.snap.affectRotate) {
            const double inc = (precise ? edit_.snap.rotPrecisionIncrement
                                        : edit_.snap.rotIncrement) * 3.14159265358979 / 180.0;
            ang = vpm::SnapTo(ang, inc);
        }
    } else { // Scale
        const double d0 = std::hypot(transformOp_.startDoc.x-P.x, transformOp_.startDoc.y-P.y);
        const double d1 = std::hypot(cur.x-P.x, cur.y-P.y);
        double f = d0 > 1e-9 ? d1/d0 : 1.0;
        if (snap && edit_.snap.affectScale) {
            const double inc = precise ? edit_.snap.scalePrecision : edit_.snap.scaleIncrement;
            f = vpm::SnapTo(f, inc);
        }
        fx = fy = f;
        if (transformOp_.axis == 0) fy = 1.0; else if (transformOp_.axis == 1) fx = 1.0;
    }
    const double rc = std::cos(ang), rs = std::sin(ang);
    // Apply the op to a world point about an arbitrary pivot.
    auto xfAt = [&](Ink::DVec2 piv, Ink::DVec2 wp) -> Ink::DVec2 {
        if (transformOp_.kind == TransformOp::Kind::Move)
            return { wp.x + moveD.x, wp.y + moveD.y };
        const Ink::DVec2 rel2{ wp.x - piv.x, wp.y - piv.y };
        if (transformOp_.kind == TransformOp::Kind::Rotate) {
            const Ink::DVec2 rr = vpm::Rotate(rel2, rc, rs);
            return { piv.x + rr.x, piv.y + rr.y };
        }
        return { piv.x + rel2.x * fx, piv.y + rel2.y * fy };  // Scale
    };
    auto xf = [&](Ink::DVec2 wp) { return xfAt(P, wp); };

    if (transformOp_.editVerts) {
        // Transform the SELECTED ELEMENTS of the active path (Blender rules):
        //  • a selected POINT carries its anchor AND both handles through the
        //    transform, so rotate/scale re-orient the tangents too;
        //  • a selected HANDLE moves alone (its own element);
        //  • Individual Origins: each element transforms about ITS OWN anchor —
        //    the point stays put and only its handles rotate/scale around it.
        const Ink::Node* n = doc.Find(transformOp_.editNode);
        if (!n) return;
        const Ink::DMat23 w = doc.WorldTransform(transformOp_.editNode);
        const Ink::DMat23 wi = vpm::InvertAffine(w);
        const Ink::PathData& orig = transformOp_.origPath;   // source of truth
        const bool indiv = edit_.pivot == PivotMode::IndividualOrigins &&
                           transformOp_.kind != TransformOp::Kind::Move;
        Ink::PathData p = orig;
        for (const auto& e : edit_.elemSel) {
            if (e.sp >= (int)p.subpaths.size() ||
                e.a  >= (int)p.subpaths[e.sp].anchors.size()) continue;
            const Ink::Anchor& src = orig.subpaths[e.sp].anchors[e.a];
            Ink::Anchor& an = p.subpaths[e.sp].anchors[e.a];
            const Ink::DVec2 anchorW = w.Apply(src.pos);
            const Ink::DVec2 piv = indiv ? anchorW : P;

            if (e.part == EditContext::ElemPart::Point) {
                const Ink::DVec2 newPosW = xfAt(piv, anchorW);
                an.pos = wi.Apply(newPosW);
                // Handles follow the SAME transform (world), re-expressed
                // relative to the new anchor — so rotate/scale bend them too.
                if (src.hasIn) {
                    const Ink::DVec2 hw = xfAt(piv,
                        w.Apply({ src.pos.x + src.in.x, src.pos.y + src.in.y }));
                    const Ink::DVec2 hl = wi.Apply(hw);
                    an.in = { hl.x - an.pos.x, hl.y - an.pos.y };
                }
                if (src.hasOut) {
                    const Ink::DVec2 hw = xfAt(piv,
                        w.Apply({ src.pos.x + src.out.x, src.pos.y + src.out.y }));
                    const Ink::DVec2 hl = wi.Apply(hw);
                    an.out = { hl.x - an.pos.x, hl.y - an.pos.y };
                }
            } else if (e.part == EditContext::ElemPart::In && src.hasIn) {
                if (edit_.ElemSelected(e.sp, e.a, EditContext::ElemPart::Point))
                    continue;   // already carried by the point
                const Ink::DVec2 hw = xfAt(piv,
                    w.Apply({ src.pos.x + src.in.x, src.pos.y + src.in.y }));
                const Ink::DVec2 hl = wi.Apply(hw);
                an.in = { hl.x - an.pos.x, hl.y - an.pos.y };
            } else if (e.part == EditContext::ElemPart::Out && src.hasOut) {
                if (edit_.ElemSelected(e.sp, e.a, EditContext::ElemPart::Point))
                    continue;
                const Ink::DVec2 hw = xfAt(piv,
                    w.Apply({ src.pos.x + src.out.x, src.pos.y + src.out.y }));
                const Ink::DVec2 hl = wi.Apply(hw);
                an.out = { hl.x - an.pos.x, hl.y - an.pos.y };
            }
        }
        doc.SetPath(transformOp_.editNode, p);
        return;
    }

    for (const auto& o : transformOp_.nodes) {
        Ink::Transform2D t = o.t;
        const Ink::DVec2 no = xf({ o.t.tx, o.t.ty });
        t.tx = no.x;  t.ty = no.y;
        if (transformOp_.kind == TransformOp::Kind::Rotate) t.rotation = o.t.rotation + ang;
        else if (transformOp_.kind == TransformOp::Kind::Scale) { t.sx = o.t.sx*fx; t.sy = o.t.sy*fy; }
        doc.SetTransform(o.id, t);
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
    EndModalCapture();
    transformOp_ = TransformOp{};
}

void Application::CancelTransform() {
    if (!transformOp_.Active() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    if (transformOp_.editVerts)
        doc.SetPath(transformOp_.editNode, transformOp_.origPath);
    else
        for (const auto& o : transformOp_.nodes) doc.SetTransform(o.id, o.t);
    EndModalCapture();
    transformOp_ = TransformOp{};
}

// ── The transform cursor (legacy icon technique) ─────────────────────────────
// Draw the cursor icon centred on the wrapped virtual position, on the ImGui
// FOREGROUND draw list, tinted by C_Cursor_Color; the oriented variants rotate
// the just-emitted vertices about the centre (legacy ShowOrientedCursor).

namespace {
void DrawCursorIcon(const char* iconId, ImVec2 centre, float angleRad) {
    auto& im = VectorGraphics::IconManager::Instance();
    if (!im.HasIcon(iconId)) return;
    auto& ds = DS::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const int vtx0 = fg->VtxBuffer.Size;
    ImVec4 col = ds.GetColor(Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = col;
    im.RenderIcon(fg, iconId, ImVec2(centre.x - sz * 0.5f, centre.y - sz * 0.5f), sz, md);
    if (angleRad != 0.0f) {
        const int vtx1 = fg->VtxBuffer.Size;
        const float c = std::cos(angleRad), s = std::sin(angleRad);
        for (int i = vtx0; i < vtx1; ++i) {
            ImDrawVert& v = fg->VtxBuffer[i];
            const float dx = v.pos.x - centre.x, dy = v.pos.y - centre.y;
            v.pos.x = centre.x + dx * c - dy * s;
            v.pos.y = centre.y + dx * s + dy * c;
        }
    }
}
} // namespace

void Application::DrawTransformCursor(const ViewCam& cam) {
    if (!transformOp_.Active()) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const ImVec2 pos = WrapPointInCanvas(
        ImVec2((float)transformOp_.virtPx.x, (float)transformOp_.virtPx.y));

    if (transformOp_.kind == TransformOp::Kind::Move) {
        DrawCursorIcon("multi-directionnal-move-cur", pos, 0.0f);
        return;
    }
    // Angle of the pivot→virtual-cursor guide, in screen space.
    const Ink::Vec2 pv = cam.DocToView(transformOp_.pivot.x, transformOp_.pivot.y);
    const ImVec2 pivotPx(cam.canvasMin.x + pv.x, cam.canvasMin.y + pv.y);
    const float ang = std::atan2((float)transformOp_.virtPx.y - pivotPx.y,
                                 (float)transformOp_.virtPx.x - pivotPx.x);
    if (transformOp_.kind == TransformOp::Kind::Scale) {
        // Scale: arrows point ALONG the guide (grow/shrink direction).
        DrawCursorIcon("move-left-right-cur copy", pos, ang);
    } else {
        // Rotate: the up-down asset aligned to the radius reads as the TANGENT.
        DrawCursorIcon("move-up-down-cur", pos, ang);
    }
}

} // namespace App

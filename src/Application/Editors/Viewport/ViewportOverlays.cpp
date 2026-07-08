#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editor overlays (docs/Ink/ROADMAP.md Lot 8) — selection outlines,
//  bounding-box handles, Edit-Mode anchors, the modal-transform feedback (pivot
//  cross + axis line) and the in-progress gesture (box-select rubber band,
//  draw-shape preview). All drawn by Ink's OverlayPass (Vulkan), never ImGui;
//  every colour is a design-system token resolved app-side.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

Ink::Color Tint(DS::DesignSystem& ds, Tok token, float alpha) {
    try {
        const ImVec4 c = ds.GetColor(token);
        return Ink::SrgbToLinearPremultiplied(c.x, c.y, c.z, alpha);
    } catch (...) {
        return Ink::SrgbToLinearPremultiplied(0.9f, 0.6f, 0.1f, alpha);
    }
}
} // namespace

void Application::DrawEditOverlays(EditorState& st, const ViewCam& cam,
                                   Ink::OverlayList& ov, bool hovered) {
    (void)st;
    if (!ink_ || !project_.document) return;
    auto& ds = DS::DesignSystem::Instance();

    const Ink::Color selCol    = Tint(ds, Tok::S_Color_Accent_Default, 0.95f);
    const Ink::Color activeCol = Tint(ds, Tok::S_Color_Accent_Default, 1.0f);
    const Ink::Color handleCol = Tint(ds, Tok::S_Color_Text_Default, 1.0f);
    const Ink::Color subtleCol = Tint(ds, Tok::S_Color_Text_Subtle, 0.9f);

    auto rectOverlay = [&](const Ink::DRect& b, const Ink::Color& col, float th) {
        const Ink::Vec2 a = cam.DocToView(b.min.x, b.min.y);
        const Ink::Vec2 c = cam.DocToView(b.max.x, b.max.y);
        ov.AddRect({ std::min(a.x, c.x), std::min(a.y, c.y) },
                   { std::max(a.x, c.x), std::max(a.y, c.y) }, col, th);
    };

    // ── Object-Mode selection outlines + active-object handles ────────────────
    if (edit_.mode == EditorMode::Object) {
        Ink::DRect nb;
        for (Ink::NodeId id : edit_.selection)
            if (ink_->NodeBounds(id, nb))
                rectOverlay(nb, id == edit_.active ? activeCol : selCol,
                            id == edit_.active ? 2.0f : 1.5f);
        // Bounding-box handles on the active object's box (8 squares).
        Ink::DRect ab;
        if (edit_.active != Ink::kNullNode && ink_->NodeBounds(edit_.active, ab)) {
            const Ink::Vec2 mn = cam.DocToView(ab.min.x, ab.min.y);
            const Ink::Vec2 mx = cam.DocToView(ab.max.x, ab.max.y);
            const float hx = (mn.x + mx.x) * 0.5f, hy = (mn.y + mx.y) * 0.5f;
            const float pts[8][2] = {
                { mn.x, mn.y }, { hx, mn.y }, { mx.x, mn.y },
                { mn.x, hy },                 { mx.x, hy },
                { mn.x, mx.y }, { hx, mx.y }, { mx.x, mx.y } };
            const float hs = 3.5f;
            for (auto& p : pts) {
                ov.AddRectFilled({ p[0] - hs, p[1] - hs }, { p[0] + hs, p[1] + hs },
                                 handleCol);
                ov.AddRect({ p[0] - hs, p[1] - hs }, { p[0] + hs, p[1] + hs },
                           activeCol, 1.0f);
            }
        }
    }

    // ── Edit-Mode anchors of the active path ──────────────────────────────────
    if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode) {
        if (const Ink::Node* n = project_.document->Find(edit_.active)) {
            const Ink::DMat23 w = project_.document->WorldTransform(edit_.active);
            for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp) {
                const auto& subp = n->path.subpaths[sp];
                for (int a = 0; a < (int)subp.anchors.size(); ++a) {
                    const Ink::DVec2 dp = w.Apply(subp.anchors[a].pos);
                    const Ink::Vec2 v = cam.DocToView(dp.x, dp.y);
                    const bool sel = edit_.VertSelected(sp, a);
                    const float hs = sel ? 4.0f : 3.0f;
                    ov.AddRectFilled({ v.x - hs, v.y - hs }, { v.x + hs, v.y + hs },
                                     sel ? activeCol : handleCol);
                }
            }
        }
    }

    // ── Modal transform feedback: pivot cross + axis-constraint line ──────────
    if (transformOp_.Active()) {
        const Ink::Vec2 p = cam.DocToView(transformOp_.pivot.x, transformOp_.pivot.y);
        const float s = 10.0f;
        ov.AddLine({ p.x - s, p.y }, { p.x + s, p.y }, subtleCol, 1.0f);
        ov.AddLine({ p.x, p.y - s }, { p.x, p.y + s }, subtleCol, 1.0f);
        ov.AddCircle(p, 4.0f, activeCol, 1.5f);
        if (transformOp_.axis >= 0) {
            const Ink::DVec2 dir = transformOp_.axis == 0 ? transformOp_.basisX
                                                          : transformOp_.basisY;
            const double L = 100000.0;
            const Ink::Vec2 a = cam.DocToView(transformOp_.pivot.x - dir.x * L,
                                              transformOp_.pivot.y - dir.y * L);
            const Ink::Vec2 b = cam.DocToView(transformOp_.pivot.x + dir.x * L,
                                              transformOp_.pivot.y + dir.y * L);
            const Ink::Color axisCol = transformOp_.axis == 0
                ? Ink::SrgbToLinearPremultiplied(0.9f, 0.25f, 0.25f, 0.8f)
                : Ink::SrgbToLinearPremultiplied(0.35f, 0.75f, 0.3f, 0.8f);
            ov.AddLine({ a.x, a.y }, { b.x, b.y }, axisCol, 1.0f);
        }
    }

    // ── In-progress canvas gesture ────────────────────────────────────────────
    if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect) {
        const Ink::Vec2 a = cam.DocToView(canvasDrag_.startDoc.x, canvasDrag_.startDoc.y);
        const Ink::Vec2 b = cam.DocToView(canvasDrag_.curDoc.x, canvasDrag_.curDoc.y);
        const Ink::Color fill = Tint(ds, Tok::S_Color_Accent_Default, 0.12f);
        ov.AddRectFilled({ std::min(a.x, b.x), std::min(a.y, b.y) },
                         { std::max(a.x, b.x), std::max(a.y, b.y) }, fill);
        ov.AddRect({ std::min(a.x, b.x), std::min(a.y, b.y) },
                   { std::max(a.x, b.x), std::max(a.y, b.y) }, selCol, 1.0f);
    } else if (canvasDrag_.kind == CanvasDrag::Kind::DrawRect ||
               canvasDrag_.kind == CanvasDrag::Kind::DrawEllipse) {
        const Ink::Vec2 a = cam.DocToView(canvasDrag_.startDoc.x, canvasDrag_.startDoc.y);
        const Ink::Vec2 b = cam.DocToView(canvasDrag_.curDoc.x, canvasDrag_.curDoc.y);
        if (canvasDrag_.kind == CanvasDrag::Kind::DrawRect) {
            ov.AddRect({ std::min(a.x, b.x), std::min(a.y, b.y) },
                       { std::max(a.x, b.x), std::max(a.y, b.y) }, activeCol, 1.5f);
        } else {
            const Ink::Vec2 ctr{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
            ov.AddCircle(ctr, std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)) * 0.5f,
                         activeCol, 1.5f);
        }
    }
    (void)hovered;
}

} // namespace App

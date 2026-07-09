#include "Application.h"

#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editor overlays (docs/Ink/ROADMAP.md Lot 8) — 100% Vulkan via the
//  View's OverlayList (never ImGui inside the canvas):
//   • Object mode: selected-object CONTOUR (flattened outline), origin dots, and
//     dashed PARENT-relationship lines (Ink parentId, Lot 7).
//   • Edit mode: anchor points + handle lines/points, coloured by the anchor
//     kind (Corner/Smooth/Symmetric), and the edited object's contour.
//   • Modal transform: pivot cross, axis line, a line pivot→cursor, and a CUSTOM
//     cursor drawn in Vulkan (the OS cursor is hidden during the op).
//   • The 2D cursor, and the in-progress box-select / draw-shape gesture.
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
Ink::Color Rgb(float r, float g, float b, float a) {
    return Ink::SrgbToLinearPremultiplied(r, g, b, a);
}

// Anchor-kind colour (Blender handle-mode palette): Corner = white/subtle,
// Smooth (aligned) = cyan-ish, Symmetric (mirrored) = green.
Ink::Color AnchorKindColor(Ink::AnchorKind k, bool selected) {
    const float a = selected ? 1.0f : 0.9f;
    switch (k) {
        case Ink::AnchorKind::Smooth:    return Rgb(0.30f, 0.75f, 0.95f, a);
        case Ink::AnchorKind::Symmetric: return Rgb(0.45f, 0.85f, 0.40f, a);
        default:                         return Rgb(0.90f, 0.90f, 0.92f, a);
    }
}

// A dashed segment (view px) on the overlay list.
void DashLine(Ink::OverlayList& ov, Ink::Vec2 a, Ink::Vec2 b, const Ink::Color& col,
              float th, float dash = 6.0f, float gap = 4.0f) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const float ux = dx / len, uy = dy / len;
    for (float t = 0.0f; t < len; t += dash + gap) {
        const float t1 = std::min(t + dash, len);
        ov.AddLine({ a.x + ux * t, a.y + uy * t },
                   { a.x + ux * t1, a.y + uy * t1 }, col, th);
    }
}
} // namespace

void Application::DrawEditOverlays(EditorState& st, const ViewCam& cam,
                                   Ink::OverlayList& ov, bool hovered) {
    (void)st;
    if (!ink_ || !project_.document) return;
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();

    const Ink::Color selCol    = Tint(ds, Tok::S_Color_Accent_Default, 0.95f);
    const Ink::Color activeCol = Tint(ds, Tok::S_Color_Accent_Default, 1.0f);
    const Ink::Color handleCol = Tint(ds, Tok::S_Color_Text_Default, 1.0f);
    const Ink::Color subtleCol = Tint(ds, Tok::S_Color_Text_Subtle, 0.9f);

    // Draw an object's flattened contour (world space → view) in `col`.
    auto drawContour = [&](Ink::NodeId id, const Ink::Color& col, float th) {
        const Ink::Node* n = doc.Find(id);
        if (!n || n->kind != Ink::NodeKind::Path || n->path.Empty()) return;
        const Ink::DMat23 w = doc.WorldTransform(id);
        auto polys = Ink::geom::Flatten(n->path, 1.0);
        for (const auto& pl : polys) {
            const std::size_t m = pl.points.size();
            if (m < 2) continue;
            const std::size_t last = pl.closed ? m : m - 1;
            for (std::size_t i = 0; i < last; ++i) {
                const Ink::DVec2 p0 = w.Apply(pl.points[i]);
                const Ink::DVec2 p1 = w.Apply(pl.points[(i + 1) % m]);
                ov.AddLine(cam.DocToView(p0.x, p0.y), cam.DocToView(p1.x, p1.y), col, th);
            }
        }
    };
    auto originOf = [&](Ink::NodeId id) {
        const Ink::DMat23 w = doc.WorldTransform(id);
        return cam.DocToView(w.m[2], w.m[5]);
    };

    // ── Object mode ───────────────────────────────────────────────────────────
    if (edit_.mode == EditorMode::Object) {
        for (Ink::NodeId id : edit_.selection) {
            const bool active = (id == edit_.active);
            // Contour (paths) or bbox fallback (groups/instances).
            const Ink::Node* n = doc.Find(id);
            if (n && n->kind == Ink::NodeKind::Path)
                drawContour(id, active ? activeCol : selCol, active ? 2.0f : 1.5f);
            else {
                Ink::DRect nb;
                if (ink_->NodeBounds(id, nb)) {
                    const Ink::Vec2 a = cam.DocToView(nb.min.x, nb.min.y);
                    const Ink::Vec2 b = cam.DocToView(nb.max.x, nb.max.y);
                    ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                               { std::max(a.x,b.x), std::max(a.y,b.y) },
                               active ? activeCol : selCol, active ? 2.0f : 1.5f);
                }
            }
            // Origin dot (colour by loose / on-page).
            const Ink::Vec2 o = originOf(id);
            const Ink::Color oc = Tint(ds, n && n->parent != Ink::kNullNode
                ? Tok::S_State_Active_OnPage : Tok::S_State_Active_Loose, 1.0f);
            ov.AddCircleFilled(o, 3.5f, oc);
            ov.AddCircle(o, 3.5f, Rgb(0, 0, 0, 0.6f), 1.0f);
            // Parent-relationship line (Ink parentId, Lot 7): dashed child→parent.
            if (n && n->parentId != Ink::kNullNode && doc.Find(n->parentId)) {
                const Ink::Vec2 po = originOf(n->parentId);
                DashLine(ov, o, po, subtleCol, 1.0f);
                ov.AddCircle(po, 2.5f, subtleCol, 1.0f);
            }
        }
    }

    // ── Edit mode: contour + anchors + handles ────────────────────────────────
    if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode) {
        drawContour(edit_.active, Tint(ds, Tok::S_Color_Accent_Default, 0.55f), 1.5f);
        if (const Ink::Node* n = doc.Find(edit_.active)) {
            const Ink::DMat23 w = doc.WorldTransform(edit_.active);
            for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp) {
                const auto& subp = n->path.subpaths[sp];
                for (int a = 0; a < (int)subp.anchors.size(); ++a) {
                    const Ink::Anchor& an = subp.anchors[a];
                    const bool sel = edit_.VertSelected(sp, a);
                    const Ink::DVec2 dp = w.Apply(an.pos);
                    const Ink::Vec2 v = cam.DocToView(dp.x, dp.y);
                    // Handle lines + points (only for selected anchors, Blender-style).
                    if (sel) {
                        const Ink::Color hc = AnchorKindColor(an.kind, true);
                        if (an.hasIn) {
                            const Ink::DVec2 hp = w.Apply({ an.pos.x + an.in.x, an.pos.y + an.in.y });
                            const Ink::Vec2 hv = cam.DocToView(hp.x, hp.y);
                            ov.AddLine(v, hv, hc, 1.0f);
                            ov.AddCircleFilled(hv, 3.0f, hc);
                        }
                        if (an.hasOut) {
                            const Ink::DVec2 hp = w.Apply({ an.pos.x + an.out.x, an.pos.y + an.out.y });
                            const Ink::Vec2 hv = cam.DocToView(hp.x, hp.y);
                            ov.AddLine(v, hv, hc, 1.0f);
                            ov.AddCircleFilled(hv, 3.0f, hc);
                        }
                    }
                    // Anchor square: fill by kind, accent outline when selected.
                    const float hs = sel ? 4.0f : 3.0f;
                    ov.AddRectFilled({ v.x-hs, v.y-hs }, { v.x+hs, v.y+hs },
                                     AnchorKindColor(an.kind, sel));
                    if (sel) ov.AddRect({ v.x-hs, v.y-hs }, { v.x+hs, v.y+hs }, activeCol, 1.5f);
                }
            }
        }
    }

    // ── 2D cursor ─────────────────────────────────────────────────────────────
    if (edit_.cursor2DValid) {
        const Ink::Vec2 c = cam.DocToView(edit_.cursor2D.x, edit_.cursor2D.y);
        const Ink::Color cw = Rgb(1, 1, 1, 0.9f), cr = Rgb(0.85f, 0.15f, 0.15f, 0.9f);
        ov.AddCircle(c, 9.0f, cw, 1.5f);
        ov.AddCircle(c, 9.0f, cr, 0.8f);
        const float s = 12.0f;
        ov.AddLine({ c.x - s, c.y }, { c.x - 4, c.y }, cr, 1.0f);
        ov.AddLine({ c.x + 4, c.y }, { c.x + s, c.y }, cr, 1.0f);
        ov.AddLine({ c.x, c.y - s }, { c.x, c.y - 4 }, cr, 1.0f);
        ov.AddLine({ c.x, c.y + 4 }, { c.x, c.y + s }, cr, 1.0f);
    }

    // ── Modal transform feedback ──────────────────────────────────────────────
    if (transformOp_.Active()) {
        const Ink::Vec2 p = cam.DocToView(transformOp_.pivot.x, transformOp_.pivot.y);
        const float s = 10.0f;
        ov.AddLine({ p.x - s, p.y }, { p.x + s, p.y }, subtleCol, 1.0f);
        ov.AddLine({ p.x, p.y - s }, { p.x, p.y + s }, subtleCol, 1.0f);
        ov.AddCircle(p, 4.0f, activeCol, 1.5f);
        if (transformOp_.axis >= 0) {
            const Ink::DVec2 dir = transformOp_.axis == 0 ? transformOp_.basisX : transformOp_.basisY;
            const double L = 100000.0;
            const Ink::Vec2 a = cam.DocToView(transformOp_.pivot.x - dir.x*L, transformOp_.pivot.y - dir.y*L);
            const Ink::Vec2 b = cam.DocToView(transformOp_.pivot.x + dir.x*L, transformOp_.pivot.y + dir.y*L);
            const Ink::Color axisCol = transformOp_.axis == 0 ? Rgb(0.9f,0.25f,0.25f,0.8f)
                                                              : Rgb(0.35f,0.75f,0.3f,0.8f);
            ov.AddLine({ a.x, a.y }, { b.x, b.y }, axisCol, 1.0f);
        }
        // Line pivot → cursor + a custom Vulkan cursor (OS cursor is hidden).
        if (hovered) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const Ink::Vec2 cur{ m.x - cam.canvasMin.x, m.y - cam.canvasMin.y };
            DashLine(ov, p, cur, subtleCol, 1.0f, 5.0f, 4.0f);
            const float cs = 8.0f;
            ov.AddLine({ cur.x - cs, cur.y }, { cur.x + cs, cur.y }, activeCol, 1.5f);
            ov.AddLine({ cur.x, cur.y - cs }, { cur.x, cur.y + cs }, activeCol, 1.5f);
            ov.AddCircle(cur, 3.0f, activeCol, 1.2f);
        }
    }

    // ── In-progress canvas gesture ────────────────────────────────────────────
    if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect) {
        const Ink::Vec2 a = cam.DocToView(canvasDrag_.startDoc.x, canvasDrag_.startDoc.y);
        const Ink::Vec2 b = cam.DocToView(canvasDrag_.curDoc.x, canvasDrag_.curDoc.y);
        const Ink::Color fill = Tint(ds, Tok::S_Color_Accent_Default, 0.12f);
        ov.AddRectFilled({ std::min(a.x,b.x), std::min(a.y,b.y) },
                         { std::max(a.x,b.x), std::max(a.y,b.y) }, fill);
        ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                   { std::max(a.x,b.x), std::max(a.y,b.y) }, selCol, 1.0f);
    } else if (canvasDrag_.kind == CanvasDrag::Kind::DrawRect ||
               canvasDrag_.kind == CanvasDrag::Kind::DrawEllipse) {
        const Ink::Vec2 a = cam.DocToView(canvasDrag_.startDoc.x, canvasDrag_.startDoc.y);
        const Ink::Vec2 b = cam.DocToView(canvasDrag_.curDoc.x, canvasDrag_.curDoc.y);
        if (canvasDrag_.kind == CanvasDrag::Kind::DrawRect) {
            ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                       { std::max(a.x,b.x), std::max(a.y,b.y) }, activeCol, 1.5f);
        } else {
            const Ink::Vec2 ctr{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
            ov.AddCircle(ctr, std::max(std::abs(b.x-a.x), std::abs(b.y-a.y)) * 0.5f, activeCol, 1.5f);
        }
    }
    (void)handleCol;
}

} // namespace App

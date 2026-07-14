#include "Application.h"

#include "ViewportMath.h"
#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Line-mark tool (tool.linemark) — the legacy mark workflow on Ink strokes
//  (docs/Ink/IOF_CORE_PLAN.md Phase A): hovering a stroked line shows a
//  translucent GHOST of the would-be mark (kind = markPlaceKind_); clicking
//  drops it. A click on an existing handle selects it (Shift toggles, Alt
//  deletes) and ARMS a drag ALONG the curve (relative Δt, no teleport; the
//  side follows the cursor). Empty click starts a box-select. G slides the
//  selected marks, R flips their side (instant), S scales a crossing's gap —
//  all with Shift precision. Marks are style data → one SetStyle undo command
//  per gesture. Handles/ghosts draw ONLY while the tool is active, into the
//  Vulkan overlay list (never ImGui in the canvas).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

Ink::Color MkCol(Tok t, float a) {
    try {
        const ImVec4 c = DS::DesignSystem::Instance().GetColor(t);
        return Ink::SrgbToLinearPremultiplied(c.x, c.y, c.z, a);
    } catch (...) {
        return Ink::SrgbToLinearPremultiplied(0.9f, 0.6f, 0.1f, a);
    }
}

// A document paint is LINEAR straight — premultiply it for the overlay list.
Ink::Color Premul(const Ink::Color& c, float a) {
    return { c.r * a, c.g * a, c.b * a, a };
}

// One flattened subpath of a node, in WORLD space (view tolerance).
struct WorldPoly {
    std::vector<Ink::DVec2> pts;
    bool closed = false;
};

std::vector<WorldPoly> FlattenWorld(const Ink::Document& doc, Ink::NodeId id,
                                    double zoom) {
    std::vector<WorldPoly> out;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return out;
    const Ink::DMat23 w = doc.WorldTransform(id);
    const double ws =
        std::max(1e-6, std::sqrt(std::abs(w.m[0] * w.m[4] - w.m[1] * w.m[3])));
    const double tol = std::max(1e-4, 0.5 / (std::max(1e-6, zoom) * ws));
    for (const auto& pl : Ink::geom::Flatten(n->path, tol)) {
        WorldPoly wp;
        wp.closed = pl.closed;
        wp.pts.reserve(pl.points.size());
        for (const Ink::DVec2& p : pl.points) wp.pts.push_back(w.Apply(p));
        out.push_back(std::move(wp));
    }
    return out;
}

double PolyTotal(const WorldPoly& p) {
    const std::size_t n = p.pts.size();
    if (n < 2) return 0.0;
    const std::size_t sc = p.closed ? n : n - 1;
    double total = 0.0;
    for (std::size_t i = 0; i < sc; ++i)
        total += std::hypot(p.pts[(i + 1) % n].x - p.pts[i].x,
                            p.pts[(i + 1) % n].y - p.pts[i].y);
    return total;
}

void PointAtArc(const WorldPoly& p, double d, Ink::DVec2& outP, Ink::DVec2& outT) {
    const std::size_t n = p.pts.size();
    const std::size_t sc = p.closed ? n : n - 1;
    double acc = 0.0;
    for (std::size_t i = 0; i < sc; ++i) {
        const Ink::DVec2 a = p.pts[i], b = p.pts[(i + 1) % n];
        const double L = std::hypot(b.x - a.x, b.y - a.y);
        if (L < 1e-9) continue;
        if (d <= acc + L) {
            const double u = (d - acc) / L;
            outP = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
            outT = { (b.x - a.x) / L, (b.y - a.y) / L };
            return;
        }
        acc += L;
    }
    outP = p.pts[n - 1];
    outT = { 1, 0 };
}

void PointAtT(const WorldPoly& p, double t, Ink::DVec2& outP, Ink::DVec2& outT) {
    const double tc = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    PointAtArc(p, tc * PolyTotal(p), outP, outT);
}

enum class HState { Normal, Hover, Selected };

// The mark's clickable HANDLE in the legacy geometry-point style: a ringed
// dot (orange centre when selected, else the TYPE colour — violet dash pin /
// green gap pin / accent for glyph marks), plus a select/hover overlay: a
// ring for glyph marks, a tangent-aligned DIAMOND (dash) or SQUARE (gap) for
// dash anchors. `tv` = curve tangent at the point, view space.
void DrawHandle(Ink::OverlayList& ov, Ink::Vec2 sp, Ink::Vec2 tv,
                const Ink::StrokeMark& m, HState state) {
    const bool anchor = m.kind == Ink::MarkKind::DashAnchor;
    const bool dashMode = m.side >= 0;
    const Ink::Color typeCol = anchor
        ? MkCol(dashMode ? Tok::C_EditHandle_Vector : Tok::C_EditHandle_Mirrored,
                1.0f)
        : MkCol(Tok::S_Color_Accent_Default, 1.0f);
    const Ink::Color centre =
        state == HState::Selected ? MkCol(Tok::S_State_Active_OnPage, 1.0f)
                                  : typeCol;
    ov.AddCircleFilled(sp, 3.5f, centre);
    ov.AddCircle(sp, 3.5f, MkCol(Tok::C_EditHandle_VertexRing, 1.0f), 1.0f);
    if (state == HState::Normal) return;

    const float r = state == HState::Selected ? 8.0f : 7.0f;
    const float th = state == HState::Selected ? 2.0f : 1.5f;
    if (!anchor) { ov.AddCircle(sp, r, typeCol, th); return; }
    float tx = tv.x, ty = tv.y;
    const float tl = std::sqrt(tx * tx + ty * ty);
    if (tl < 1e-4f) { tx = 1.0f; ty = 0.0f; }
    else { tx /= tl; ty /= tl; }
    const float nx = -ty, ny = tx;
    auto P = [&](float a, float b) {
        return Ink::Vec2{ sp.x + tx * a + nx * b, sp.y + ty * a + ny * b };
    };
    if (dashMode) {   // diamond: vertices along ±tangent and ±normal
        ov.AddLine(P(r, 0), P(0, r), typeCol, th);
        ov.AddLine(P(0, r), P(-r, 0), typeCol, th);
        ov.AddLine(P(-r, 0), P(0, -r), typeCol, th);
        ov.AddLine(P(0, -r), P(r, 0), typeCol, th);
    } else {          // square aligned to the curve
        const float h = r * 0.72f;
        ov.AddLine(P(h, h), P(-h, h), typeCol, th);
        ov.AddLine(P(-h, h), P(-h, -h), typeCol, th);
        ov.AddLine(P(-h, -h), P(h, -h), typeCol, th);
        ov.AddLine(P(h, -h), P(h, h), typeCol, th);
    }
}

} // namespace

// Tool active + marks selected → G/R/S/X act on the marks, not the objects.
bool Application::MarkToolArmed() const {
    return Shortcuts::Tools::ToolManager::Instance().GetActiveTool() ==
               "tool.linemark" &&
           !edit_.markSel.empty();
}

void Application::HandleMarkTool(EditorState& st, const ViewCam& cam,
                                 Ink::OverlayList& ov, bool hovered) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const double zoom = std::max(1e-4, cam.zoom);
    const ImVec2 mp = io.MousePos;
    const Ink::DVec2 mdoc = cam.ScreenToDoc(mp.x, mp.y);
    const void* self = &st;
    const double precision = io.KeyShift ? 0.1 : 1.0;   // Shift precision-drag

    auto d2v = [&](Ink::DVec2 p) { return cam.DocToView(p.x, p.y); };

    // Every visible path node with at least one stroke, in document order.
    std::vector<Ink::NodeId> pathNodes;
    for (const Ink::Page& page : doc.Pages()) {
        std::vector<Ink::NodeId> stack(page.children.rbegin(),
                                       page.children.rend());
        while (!stack.empty()) {
            const Ink::NodeId id = stack.back();
            stack.pop_back();
            const Ink::Node* n = doc.Find(id);
            if (!n || !n->visible) continue;
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
                stack.push_back(*it);
            if (n->kind == Ink::NodeKind::Path && !n->style.strokes.empty())
                pathNodes.push_back(id);
        }
    }

    auto markOf = [&](const EditContext::MarkRef& r) -> const Ink::StrokeMark* {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            return nullptr;
        const auto& mk = n->style.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) return nullptr;
        return &mk[(std::size_t)r.index];
    };
    // World point + tangent of a mark (flattens its subpath).
    auto markWorld = [&](const EditContext::MarkRef& r, Ink::DVec2& p,
                         Ink::DVec2& tn) -> bool {
        const Ink::StrokeMark* m = markOf(r);
        if (!m) return false;
        auto polys = FlattenWorld(doc, r.node, zoom);
        if (m->sub < 0 || m->sub >= (int)polys.size()) return false;
        PointAtT(polys[(std::size_t)m->sub], m->t, p, tn);
        return true;
    };
    // One undo command over a set of touched nodes (style before → after).
    struct StylePair { Ink::NodeId id; Ink::Style before, after; };
    auto commitStyles = [&](std::vector<StylePair> ps, const char* label) {
        if (ps.empty()) return;
        for (StylePair& p : ps)
            if (const Ink::Node* n = doc.Find(p.id)) p.after = n->style;
        PushDocCommand(label,
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.before);
            },
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.after);
            });
        LogInfoAction(label);
    };
    // Snapshot the styles of every node in `refs` (unique), BEFORE mutating.
    auto snapshotStyles = [&](const std::vector<EditContext::MarkRef>& refs) {
        std::vector<StylePair> ps;
        for (const EditContext::MarkRef& r : refs) {
            bool seen = false;
            for (const StylePair& p : ps) seen = seen || p.id == r.node;
            if (seen) continue;
            if (const Ink::Node* n = doc.Find(r.node))
                ps.push_back({ r.node, n->style, n->style });
        }
        return ps;
    };
    // Translucent GHOST of a mark: the stroke's own colour at the placement
    // preview alpha, shapes mirroring the tessellator (curve-following ends).
    auto drawGhost = [&](Ink::NodeId nodeId, int strokeIdx,
                         const Ink::StrokeMark& m, const WorldPoly& poly) {
        const Ink::Node* n = doc.Find(nodeId);
        if (!n || strokeIdx < 0 || strokeIdx >= (int)n->style.strokes.size())
            return;
        const Ink::Stroke& sk = n->style.strokes[(std::size_t)strokeIdx];
        float alpha = 0.5f;
        try {
            alpha = std::max(0.35f, DS::DesignSystem::Instance().GetFloat(
                                        Tok::S_Config_PlacementPreviewAlpha));
        } catch (...) {}
        const Ink::Color col = Premul(sk.paint.color, alpha);
        const Ink::DMat23 w = doc.WorldTransform(nodeId);
        const double wsc =
            std::max(1e-6, std::sqrt(std::abs(w.m[0]*w.m[4] - w.m[1]*w.m[3])));
        const float thPx = (float)std::max(1.5,
            (m.thickness > 1e-9 ? m.thickness : sk.width) * wsc * zoom);
        const double total = PolyTotal(poly);
        if (total < 1e-9) return;
        const double d0 = (m.t < 0.0 ? 0.0 : (m.t > 1.0 ? 1.0 : m.t)) * total;
        auto sampleOff = [&](double off, Ink::DVec2& p, Ink::DVec2& tn) {
            double d = d0 + off;
            d = d < 0.0 ? 0.0 : (d > total ? total : d);
            PointAtArc(poly, d, p, tn);
        };
        auto seg = [&](Ink::DVec2 a, Ink::DVec2 b) {
            ov.AddLine(d2v(a), d2v(b), col, thPx);
        };
        Ink::DVec2 q, qt;
        sampleOff(0.0, q, qt);
        const Ink::DVec2 nrm{ -qt.y, qt.x };
        const double sgn = m.side >= 0 ? 1.0 : -1.0;
        // Mark params are node-local units → world via the node scale.
        const double size = m.size * wsc, gap = m.gap * wsc;
        const double baseW = sk.width * wsc;
        switch (m.kind) {
        case Ink::MarkKind::SlopeTick: {
            const double len = m.outsideMeasure ? (baseW * 0.5 + size) : size;
            seg(q, { q.x + nrm.x * len * sgn, q.y + nrm.y * len * sgn });
            break;
        }
        case Ink::MarkKind::Crossing: {
            for (double s2 : { -1.0, 1.0 }) {
                Ink::DVec2 e, et;
                sampleOff(s2 * gap * 0.5, e, et);
                const Ink::DVec2 en{ -et.y, et.x };
                seg({ e.x - en.x * size, e.y - en.y * size },
                    { e.x + en.x * size, e.y + en.y * size });
            }
            // Hint the erased span by tracing the curve translucently.
            const Ink::Color cut = Premul(sk.paint.color, 0.18f);
            Ink::DVec2 prev, pt;
            sampleOff(-gap * 0.5, prev, pt);
            for (int i = 1; i <= 12; ++i) {
                Ink::DVec2 cur, ct;
                sampleOff(-gap * 0.5 + gap * (double)i / 12.0, cur, ct);
                ov.AddLine(d2v(prev), d2v(cur), cut,
                           (float)std::max(1.0, baseW * zoom));
                prev = cur;
            }
            break;
        }
        case Ink::MarkKind::Bridge: {
            for (double s2 : { -1.0, 1.0 }) {
                Ink::DVec2 e, et;
                sampleOff(s2 * gap * 0.5, e, et);
                const Ink::DVec2 en{ -et.y, et.x };
                const Ink::DVec2 inward{ et.x * -s2, et.y * -s2 };
                const Ink::DVec2 top{ e.x + en.x * size, e.y + en.y * size };
                const Ink::DVec2 bot{ e.x - en.x * size, e.y - en.y * size };
                const double stub = size * 0.6;
                seg({ top.x + inward.x * stub, top.y + inward.y * stub }, top);
                seg(top, bot);
                seg(bot, { bot.x + inward.x * stub, bot.y + inward.y * stub });
            }
            break;
        }
        case Ink::MarkKind::Pylon: {
            seg({ q.x - nrm.x * size, q.y - nrm.y * size },
                { q.x + nrm.x * size, q.y + nrm.y * size });
            if (m.square && gap > 1e-6) {
                const double h = gap * 0.5;
                const Ink::DVec2 c0{ q.x - qt.x*h - nrm.x*h, q.y - qt.y*h - nrm.y*h };
                const Ink::DVec2 c1{ q.x + qt.x*h - nrm.x*h, q.y + qt.y*h - nrm.y*h };
                const Ink::DVec2 c2{ q.x + qt.x*h + nrm.x*h, q.y + qt.y*h + nrm.y*h };
                const Ink::DVec2 c3{ q.x - qt.x*h + nrm.x*h, q.y - qt.y*h + nrm.y*h };
                seg(c0, c1); seg(c1, c2); seg(c2, c3); seg(c3, c0);
            }
            break;
        }
        case Ink::MarkKind::DashAnchor: {
            // A phase pin has no geometry — show the handle in Hover state.
            const Ink::Vec2 c2 = d2v(q);
            const Ink::Vec2 t2 = d2v({ q.x + qt.x, q.y + qt.y });
            DrawHandle(ov, c2, { t2.x - c2.x, t2.y - c2.y }, m, HState::Hover);
            break;
        }
        }
    };

    // ── Modal G (slide along curve) / S (scale crossing gap) ─────────────────
    if (markGrab_.Active() && markGrab_.owner == nullptr && hovered)
        markGrab_.owner = self;
    if (markGrab_.Active() && markGrab_.owner == self) {
        const bool commit = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right);

        // Scale pivot = the anchor mark's centre (screen space).
        ImVec2 pivotS = markGrab_.startMouse;
        if (!markGrab_.refs.empty()) {
            Ink::DVec2 pp, ptn;
            if (markWorld(markGrab_.refs.front(), pp, ptn)) {
                const Ink::Vec2 v = d2v(pp);
                pivotS = ImVec2(v.x + cam.canvasMin.x, v.y + cam.canvasMin.y);
            }
        }
        double scaleF = 1.0;
        if (markGrab_.op == 2) {
            const double dA = std::hypot(markGrab_.startMouse.x - pivotS.x,
                                         markGrab_.startMouse.y - pivotS.y);
            const double dB = std::hypot(mp.x - pivotS.x, mp.y - pivotS.y);
            const double raw = dA > 1e-3 ? dB / dA : 1.0;
            scaleF = std::clamp(1.0 + (raw - 1.0) * precision, 0.05, 20.0);
        }
        // RELATIVE slide: Δt = mouse displacement since press, projected onto
        // the anchor mark's tangent, over its subpath arc length (no teleport).
        double deltaT = 0.0;
        if (markGrab_.op == 1 && !markGrab_.refs.empty()) {
            const EditContext::MarkRef& a = markGrab_.refs.front();
            if (const Ink::StrokeMark* am = markOf(a)) {
                auto polys = FlattenWorld(doc, a.node, zoom);
                if (am->sub >= 0 && am->sub < (int)polys.size()) {
                    const WorldPoly& poly = polys[(std::size_t)am->sub];
                    const double total = PolyTotal(poly);
                    Ink::DVec2 ap, atn;
                    PointAtT(poly, markGrab_.t0.front(), ap, atn);
                    const Ink::DVec2 sd =
                        cam.ScreenToDoc(markGrab_.startMouse.x,
                                        markGrab_.startMouse.y);
                    const double along = ((mdoc.x - sd.x) * atn.x +
                                          (mdoc.y - sd.y) * atn.y) * precision;
                    if (total > 1e-6) deltaT = along / total;
                }
            }
        }
        auto previewT = [&](std::size_t k) {
            return std::clamp(markGrab_.t0[k] + deltaT, 0.0, 1.0);
        };

        // Ghost each affected mark at its preview value (real marks stay).
        for (std::size_t k = 0; k < markGrab_.refs.size(); ++k) {
            const EditContext::MarkRef& r = markGrab_.refs[k];
            const Ink::StrokeMark* m = markOf(r);
            if (!m) continue;
            Ink::StrokeMark ghost = *m;
            if (markGrab_.op == 1) ghost.t = previewT(k);
            else if (ghost.kind == Ink::MarkKind::Crossing)
                ghost.gap = std::max(0.05, markGrab_.gap0[k] * scaleF);
            auto polys = FlattenWorld(doc, r.node, zoom);
            if (ghost.sub < 0 || ghost.sub >= (int)polys.size()) continue;
            drawGhost(r.node, r.stroke, ghost, polys[(std::size_t)ghost.sub]);
        }
        // Scale gizmo: pivot ring + dashed pivot→cursor line (accent).
        if (markGrab_.op == 2) {
            const Ink::Color gz = MkCol(Tok::S_Color_Accent_Default, 1.0f);
            const Ink::Vec2 pv{ pivotS.x - cam.canvasMin.x,
                                pivotS.y - cam.canvasMin.y };
            const Ink::Vec2 mv{ mp.x - cam.canvasMin.x, mp.y - cam.canvasMin.y };
            ov.AddCircle(pv, 5.0f, gz, 1.5f);
            const float len = std::hypot(mv.x - pv.x, mv.y - pv.y);
            const int segs = std::max(1, (int)(len / 8.0f));
            for (int i = 0; i < segs; i += 2) {
                const float t0 = (float)i / segs;
                const float t1 = (float)std::min(i + 1, segs) / segs;
                ov.AddLine({ pv.x + (mv.x - pv.x) * t0, pv.y + (mv.y - pv.y) * t0 },
                           { pv.x + (mv.x - pv.x) * t1, pv.y + (mv.y - pv.y) * t1 },
                           gz, 1.5f);
            }
        }

        if (cancel) { markGrab_.Reset(); return; }
        if (commit) {
            std::vector<StylePair> ps = snapshotStyles(markGrab_.refs);
            for (std::size_t k = 0; k < markGrab_.refs.size(); ++k) {
                const EditContext::MarkRef& r = markGrab_.refs[k];
                const Ink::Node* n = doc.Find(r.node);
                if (!n) continue;
                Ink::Style sty = n->style;
                if (r.stroke < 0 || r.stroke >= (int)sty.strokes.size()) continue;
                auto& mk = sty.strokes[(std::size_t)r.stroke].marks;
                if (r.index < 0 || r.index >= (int)mk.size()) continue;
                Ink::StrokeMark& m = mk[(std::size_t)r.index];
                if (markGrab_.op == 1) m.t = previewT(k);
                else if (m.kind == Ink::MarkKind::Crossing)
                    m.gap = std::max(0.05, markGrab_.gap0[k] * scaleF);
                doc.SetStyle(r.node, sty);
            }
            commitStyles(std::move(ps), markGrab_.op == 1 ? "Move Line Marks"
                                                          : "Scale Crossing");
            markGrab_.Reset();
        }
        return;
    }

    // ── Click-drag of a grabbed mark (armed by the press below) ──────────────
    if (markDrag_.armed || markDrag_.active) {
        const Ink::StrokeMark* m = markOf(markDrag_.ref);
        if (!m) { markDrag_ = {}; return; }
        if (markDrag_.armed &&
            std::hypot(mp.x - markDrag_.pressPos.x,
                       mp.y - markDrag_.pressPos.y) > 4.0f) {
            markDrag_.armed = false;
            markDrag_.active = true;
        }
        double deltaT = 0.0;
        auto polys = FlattenWorld(doc, markDrag_.ref.node, zoom);
        if (markDrag_.active && m->sub >= 0 && m->sub < (int)polys.size()) {
            const WorldPoly& poly = polys[(std::size_t)m->sub];
            const double total = PolyTotal(poly);
            Ink::DVec2 mpos, mtan;
            PointAtT(poly, m->t, mpos, mtan);
            const Ink::DVec2 sd =
                cam.ScreenToDoc(markDrag_.pressPos.x, markDrag_.pressPos.y);
            const double along = ((mdoc.x - sd.x) * mtan.x +
                                  (mdoc.y - sd.y) * mtan.y) * precision;
            if (total > 1e-6) deltaT = along / total;
            markDrag_.dragT = std::clamp(m->t + deltaT, 0.0, 1.0);
            // The mark's geometric side follows the cursor's side of the line
            // (never rewritten for dash anchors — side there means dash/gap).
            const double cross = mtan.x * (mdoc.y - mpos.y) -
                                 mtan.y * (mdoc.x - mpos.x);
            markDrag_.dragSide = cross >= 0.0 ? +1 : -1;
            Ink::StrokeMark ghost = *m;
            ghost.t = markDrag_.dragT;
            if (m->kind != Ink::MarkKind::DashAnchor)
                ghost.side = markDrag_.dragSide;
            drawGhost(markDrag_.ref.node, markDrag_.ref.stroke, ghost, poly);
            // Every OTHER selected mark ghosts shifted by the same Δt.
            for (const EditContext::MarkRef& r : edit_.markSel) {
                if (r == markDrag_.ref) continue;
                const Ink::StrokeMark* om = markOf(r);
                if (!om) continue;
                auto opolys = FlattenWorld(doc, r.node, zoom);
                if (om->sub < 0 || om->sub >= (int)opolys.size()) continue;
                Ink::StrokeMark og = *om;
                og.t = std::clamp(og.t + deltaT, 0.0, 1.0);
                drawGhost(r.node, r.stroke, og, opolys[(std::size_t)om->sub]);
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (markDrag_.active) {
                // Commit the group move: one undo command.
                std::vector<StylePair> ps = snapshotStyles(edit_.markSel);
                for (const EditContext::MarkRef& r : edit_.markSel) {
                    const Ink::Node* n = doc.Find(r.node);
                    if (!n) continue;
                    Ink::Style sty = n->style;
                    if (r.stroke < 0 || r.stroke >= (int)sty.strokes.size())
                        continue;
                    auto& mk = sty.strokes[(std::size_t)r.stroke].marks;
                    if (r.index < 0 || r.index >= (int)mk.size()) continue;
                    Ink::StrokeMark& sm = mk[(std::size_t)r.index];
                    if (r == markDrag_.ref) {
                        sm.t = markDrag_.dragT;
                        if (sm.kind != Ink::MarkKind::DashAnchor)
                            sm.side = markDrag_.dragSide;
                    } else {
                        sm.t = std::clamp(sm.t + deltaT, 0.0, 1.0);
                    }
                    doc.SetStyle(r.node, sty);
                }
                commitStyles(std::move(ps),
                             edit_.markSel.size() > 1 ? "Move Line Marks"
                                                      : "Move Line Mark");
            } else if (markDrag_.pendingToggle) {
                // Plain click on an already-sole-selected dash anchor: toggle
                // what it centres (dash ⇄ gap).
                std::vector<StylePair> ps = snapshotStyles({ markDrag_.ref });
                const Ink::Node* n = doc.Find(markDrag_.ref.node);
                if (n) {
                    Ink::Style sty = n->style;
                    auto& mk =
                        sty.strokes[(std::size_t)markDrag_.ref.stroke].marks;
                    Ink::StrokeMark& sm = mk[(std::size_t)markDrag_.ref.index];
                    sm.side = -sm.side;
                    doc.SetStyle(markDrag_.ref.node, sty);
                    commitStyles(std::move(ps), "Toggle Dash Anchor");
                }
            }
            markDrag_ = {};
        }
        return;
    }

    // ── Hit-test EXISTING marks (nearest handle within 9 px) ─────────────────
    const float kMarkPx = 9.0f;
    EditContext::MarkRef hit;
    float hitD = kMarkPx + 1.0f;
    for (Ink::NodeId id : pathNodes) {
        const Ink::Node* n = doc.Find(id);
        auto polys = FlattenWorld(doc, id, zoom);
        for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
            const auto& mk = n->style.strokes[(std::size_t)si].marks;
            for (int mi = 0; mi < (int)mk.size(); ++mi) {
                const Ink::StrokeMark& m = mk[(std::size_t)mi];
                if (m.sub < 0 || m.sub >= (int)polys.size()) continue;
                Ink::DVec2 p, tn;
                PointAtT(polys[(std::size_t)m.sub], m.t, p, tn);
                const Ink::Vec2 v = d2v(p);
                const float dpx = std::hypot(mp.x - (v.x + cam.canvasMin.x),
                                             mp.y - (v.y + cam.canvasMin.y));
                if (dpx < hitD) { hitD = dpx; hit = { id, si, mi }; }
            }
        }
    }

    // ── Handles for every mark: dash anchors always (invisible geometry),
    //    glyph marks when selected; hovered gets the Hover state. ─────────────
    for (Ink::NodeId id : pathNodes) {
        const Ink::Node* n = doc.Find(id);
        auto polys = FlattenWorld(doc, id, zoom);
        for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
            const auto& mk = n->style.strokes[(std::size_t)si].marks;
            for (int mi = 0; mi < (int)mk.size(); ++mi) {
                const Ink::StrokeMark& m = mk[(std::size_t)mi];
                if (m.sub < 0 || m.sub >= (int)polys.size()) continue;
                const EditContext::MarkRef ref{ id, si, mi };
                const bool seld = edit_.MarkSelected(ref);
                const bool hov = ref == hit;
                if (m.kind != Ink::MarkKind::DashAnchor && !seld && !hov)
                    continue;
                Ink::DVec2 p, tn;
                PointAtT(polys[(std::size_t)m.sub], m.t, p, tn);
                const Ink::Vec2 c = d2v(p);
                const Ink::Vec2 t2 = d2v({ p.x + tn.x, p.y + tn.y });
                DrawHandle(ov, c, { t2.x - c.x, t2.y - c.y }, m,
                           seld ? HState::Selected
                                : hov ? HState::Hover : HState::Normal);
            }
        }
    }

    // ── Box-select in progress ────────────────────────────────────────────────
    if (markBox_.active && markBox_.owner == self) {
        const Ink::Vec2 a = d2v(markBox_.start);
        const Ink::Vec2 b{ mp.x - cam.canvasMin.x, mp.y - cam.canvasMin.y };
        const Ink::Color boxC = MkCol(Tok::S_Color_Accent_Default, 1.0f);
        ov.AddRect({ std::min(a.x, b.x), std::min(a.y, b.y) },
                   { std::max(a.x, b.x), std::max(a.y, b.y) }, boxC, 1.5f);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const Ink::DVec2 mn{ std::min(markBox_.start.x, mdoc.x),
                                 std::min(markBox_.start.y, mdoc.y) };
            const Ink::DVec2 mx{ std::max(markBox_.start.x, mdoc.x),
                                 std::max(markBox_.start.y, mdoc.y) };
            if (!markBox_.additive) edit_.markSel.clear();
            for (Ink::NodeId id : pathNodes) {
                const Ink::Node* n = doc.Find(id);
                auto polys = FlattenWorld(doc, id, zoom);
                for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
                    const auto& mk = n->style.strokes[(std::size_t)si].marks;
                    for (int mi = 0; mi < (int)mk.size(); ++mi) {
                        const Ink::StrokeMark& m = mk[(std::size_t)mi];
                        if (m.sub < 0 || m.sub >= (int)polys.size()) continue;
                        Ink::DVec2 p, tn;
                        PointAtT(polys[(std::size_t)m.sub], m.t, p, tn);
                        const EditContext::MarkRef r{ id, si, mi };
                        if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y &&
                            p.y <= mx.y && !edit_.MarkSelected(r))
                            edit_.markSel.push_back(r);
                    }
                }
            }
            markBox_ = {};
        }
        return;
    }

    if (hit.node != Ink::kNullNode) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const bool soleSelected = edit_.MarkSelected(hit) &&
                                      edit_.markSel.size() == 1;
            const Ink::StrokeMark* hm = markOf(hit);
            if (io.KeyAlt) {
                // Alt+click DELETES the mark.
                std::vector<StylePair> ps = snapshotStyles({ hit });
                const Ink::Node* n = doc.Find(hit.node);
                if (n) {
                    Ink::Style sty = n->style;
                    auto& mk = sty.strokes[(std::size_t)hit.stroke].marks;
                    mk.erase(mk.begin() + hit.index);
                    doc.SetStyle(hit.node, sty);
                    edit_.MarkDeselect(hit);
                    commitStyles(std::move(ps), "Remove Line Mark");
                }
            } else if (io.KeyShift) {
                edit_.MarkToggle(hit);
                edit_.SelectAdd(hit.node);
            } else {
                if (!edit_.MarkSelected(hit)) edit_.MarkSelectOnly(hit);
                edit_.SelectAdd(hit.node);
                markDrag_ = {};
                markDrag_.armed = true;
                markDrag_.ref = hit;
                markDrag_.pressPos = mp;
                markDrag_.pendingToggle =
                    soleSelected && hm &&
                    hm->kind == Ink::MarkKind::DashAnchor;
            }
        }
        return;   // a mark is under the cursor → don't also place a new one
    }

    // ── Nearest stroked line → ghost + click places a NEW mark ───────────────
    struct Best {
        Ink::NodeId id = Ink::kNullNode;
        int stroke = -1, sub = -1;
        double t = 0.5;
        int side = +1;
        float dpx = 1e9f;
    } best;
    for (Ink::NodeId id : pathNodes) {
        const Ink::Node* n = doc.Find(id);
        int strokeIdx = -1;
        for (int si = 0; si < (int)n->style.strokes.size(); ++si)
            if (n->style.strokes[(std::size_t)si].enabled) { strokeIdx = si; break; }
        if (strokeIdx < 0) continue;
        auto polys = FlattenWorld(doc, id, zoom);
        for (int sub = 0; sub < (int)polys.size(); ++sub) {
            const WorldPoly& poly = polys[(std::size_t)sub];
            const std::size_t np = poly.pts.size();
            if (np < 2) continue;
            const std::size_t sc = poly.closed ? np : np - 1;
            const double total = PolyTotal(poly);
            double acc = 0.0;
            for (std::size_t i = 0; i < sc; ++i) {
                const Ink::DVec2 a = poly.pts[i], b = poly.pts[(i + 1) % np];
                const double abx = b.x - a.x, aby = b.y - a.y;
                const double segLen = std::hypot(abx, aby);
                if (segLen < 1e-9) continue;
                double u = ((mdoc.x - a.x) * abx + (mdoc.y - a.y) * aby) /
                           (segLen * segLen);
                u = std::clamp(u, 0.0, 1.0);
                const Ink::DVec2 proj{ a.x + abx * u, a.y + aby * u };
                const Ink::Vec2 v = d2v(proj);
                const float dpx = std::hypot(mp.x - (v.x + cam.canvasMin.x),
                                             mp.y - (v.y + cam.canvasMin.y));
                if (dpx < best.dpx) {
                    const double cross = abx * (mdoc.y - a.y) -
                                         aby * (mdoc.x - a.x);
                    best = { id, strokeIdx, sub,
                             total > 1e-6 ? (acc + segLen * u) / total : 0.0,
                             cross >= 0.0 ? +1 : -1, dpx };
                }
                acc += segLen;
            }
        }
    }

    const float kPickPx = 14.0f;
    if (best.id != Ink::kNullNode && best.dpx <= kPickPx) {
        const Ink::Node* n = doc.Find(best.id);
        const Ink::Stroke& sk = n->style.strokes[(std::size_t)best.stroke];
        // Sensible defaults scale with the stroke width (the module presets
        // the ISOM dimensions later).
        Ink::StrokeMark m;
        m.kind = markPlaceKind_;
        m.sub = best.sub;
        m.t = best.t;
        m.side = best.side;
        m.size = std::max(4.0, sk.width * 3.0);
        m.gap = std::max(6.0, sk.width * 4.0);
        auto polys = FlattenWorld(doc, best.id, zoom);
        if (best.sub >= 0 && best.sub < (int)polys.size())
            drawGhost(best.id, best.stroke, m, polys[(std::size_t)best.sub]);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // A DashAnchor placed near a control point PINS to it (it then
            // tracks that point as the curve is edited).
            if (m.kind == Ink::MarkKind::DashAnchor) {
                const Ink::DMat23 w = doc.WorldTransform(best.id);
                float bestNodeD = 12.0f;
                int bestNode = -1, srcSub = -1, seen = 0;
                for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp) {
                    if (n->path.subpaths[(std::size_t)sp].anchors.size() < 2)
                        continue;
                    if (seen++ == best.sub) { srcSub = sp; break; }
                }
                if (srcSub >= 0) {
                    const auto& an = n->path.subpaths[(std::size_t)srcSub].anchors;
                    for (int ni = 0; ni < (int)an.size(); ++ni) {
                        const Ink::DVec2 wp = w.Apply(an[(std::size_t)ni].pos);
                        const Ink::Vec2 v = d2v(wp);
                        const float dnp =
                            std::hypot(mp.x - (v.x + cam.canvasMin.x),
                                       mp.y - (v.y + cam.canvasMin.y));
                        if (dnp < bestNodeD) { bestNodeD = dnp; bestNode = ni; }
                    }
                }
                if (bestNode >= 0) m.nodeAnchor = bestNode;
            }
            std::vector<StylePair> ps =
                snapshotStyles({ { best.id, best.stroke, 0 } });
            Ink::Style sty = n->style;
            auto& mk = sty.strokes[(std::size_t)best.stroke].marks;
            const int newIdx = (int)mk.size();
            mk.push_back(m);
            doc.SetStyle(best.id, sty);
            edit_.SelectAdd(best.id);
            edit_.MarkSelectOnly({ best.id, best.stroke, newIdx });
            commitStyles(std::move(ps), "Add Line Mark");
        }
        return;
    }

    // ── Empty press: box-select (Shift adds); a plain click clears ───────────
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        markBox_ = { true, self, mdoc, io.KeyShift };
        if (!io.KeyShift) edit_.markSel.clear();
    }
}

// G slides / R flips (instant) / S scales the crossing gap of the selection.
void Application::BeginMarkTransform(TransformOp::Kind kind) {
    if (!project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;

    if (kind == TransformOp::Kind::Rotate) {
        // Instantaneous: flip the side of every FLIPPABLE selected mark
        // (only one-sided slope ticks — centred marks are unaffected).
        struct StylePair { Ink::NodeId id; Ink::Style before, after; };
        std::vector<StylePair> ps;
        bool any = false;
        for (const EditContext::MarkRef& r : edit_.markSel) {
            const Ink::Node* n = doc.Find(r.node);
            if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
                continue;
            bool seen = false;
            for (const StylePair& p : ps) seen = seen || p.id == r.node;
            if (!seen) ps.push_back({ r.node, n->style, n->style });
            Ink::Style sty = n->style;
            auto& mk = sty.strokes[(std::size_t)r.stroke].marks;
            if (r.index < 0 || r.index >= (int)mk.size()) continue;
            Ink::StrokeMark& m = mk[(std::size_t)r.index];
            if (m.kind != Ink::MarkKind::SlopeTick) continue;
            m.side = -m.side;
            doc.SetStyle(r.node, sty);
            any = true;
        }
        if (!any) return;
        for (StylePair& p : ps)
            if (const Ink::Node* n = doc.Find(p.id)) p.after = n->style;
        PushDocCommand("Flip Mark Side",
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.before);
            },
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.after);
            });
        LogInfoAction("Flip Mark Side");
        return;
    }

    // Move / Scale → arm the modal op (driven by the first hovered leaf).
    markGrab_.Reset();
    markGrab_.op = (kind == TransformOp::Kind::Scale) ? 2 : 1;
    markGrab_.owner = nullptr;
    markGrab_.startMouse = ImGui::GetIO().MousePos;
    for (const EditContext::MarkRef& r : edit_.markSel) {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            continue;
        const auto& mk = n->style.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) continue;
        // Scale only affects crossings; skip others so mixed selections work.
        if (markGrab_.op == 2 &&
            mk[(std::size_t)r.index].kind != Ink::MarkKind::Crossing)
            continue;
        markGrab_.refs.push_back(r);
        markGrab_.t0.push_back(mk[(std::size_t)r.index].t);
        markGrab_.gap0.push_back(mk[(std::size_t)r.index].gap);
    }
    if (markGrab_.refs.empty()) markGrab_.Reset();
}

void Application::DeleteSelectedMarks() {
    if (!project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;
    // Group per node; erase in DESCENDING mark index so indices stay valid.
    std::vector<EditContext::MarkRef> refs = edit_.markSel;
    std::sort(refs.begin(), refs.end(),
              [](const EditContext::MarkRef& a, const EditContext::MarkRef& b) {
                  if (a.node != b.node) return a.node < b.node;
                  if (a.stroke != b.stroke) return a.stroke < b.stroke;
                  return a.index > b.index;
              });
    struct StylePair { Ink::NodeId id; Ink::Style before, after; };
    std::vector<StylePair> ps;
    for (const EditContext::MarkRef& r : refs) {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            continue;
        bool seen = false;
        for (const StylePair& p : ps) seen = seen || p.id == r.node;
        if (!seen) ps.push_back({ r.node, n->style, n->style });
        Ink::Style sty = n->style;
        auto& mk = sty.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) continue;
        mk.erase(mk.begin() + r.index);
        doc.SetStyle(r.node, sty);
    }
    edit_.markSel.clear();
    if (ps.empty()) return;
    for (StylePair& p : ps)
        if (const Ink::Node* n = doc.Find(p.id)) p.after = n->style;
    PushDocCommand("Delete Line Marks",
        [ps](Ink::Document& d) {
            for (const StylePair& p : ps) d.SetStyle(p.id, p.before);
        },
        [ps](Ink::Document& d) {
            for (const StylePair& p : ps) d.SetStyle(p.id, p.after);
        });
    LogInfoAction("Delete Line Marks");
}

} // namespace App

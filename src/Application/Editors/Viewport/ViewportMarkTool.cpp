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
//  Line-mark tool (tool.linemark) — the GENERIC core mark workflow
//  (docs/Ink/IOF_CORE_PLAN.md Phase A). A mark is an arc-length anchor on a
//  stroked line that (a) optionally re-phases the dash run and (b) carries a
//  list of objects (SVG-marker shapes / node instances, added or subtracted).
//  This tool PLACES marks and edits their POSITION on the curve; the object
//  list, phase, side and offset are edited in the Properties "Marks" panel.
//
//  Hovering a stroked line shows a ghost dot; clicking drops a neutral mark
//  with one default object (the top-bar shape). A click on a mark handle
//  selects it (Shift toggles, Alt deletes) and arms a slide ALONG the curve;
//  G slides the selection, R cycles its side (Center→Left→Right), X deletes.
//  All edits go through SetStyle (one undo command each). Handles/ghosts draw
//  only while the tool is active, into the Vulkan overlay list.
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

// A dashed segment (view px) on the overlay list.
void DashLine(Ink::OverlayList& ov, Ink::Vec2 a, Ink::Vec2 b,
              const Ink::Color& col, float th, float dash = 5.0f,
              float gap = 4.0f) {
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

// A mark's clickable handle: a ringed dot at the mark point. A mark with a
// phase carries a small diamond (Dash) / square (Gap) around it; a Neutral
// mark shows just the dot. Selected/hover get an outer ring in the accent /
// active colour. The point is offset to the mark's side/offset in the caller.
void DrawHandle(Ink::OverlayList& ov, Ink::Vec2 sp, Ink::Vec2 tv,
                const Ink::StrokeMark& m, HState state) {
    const bool phased = m.phase != Ink::MarkPhase::Neutral;
    const bool dash = m.phase == Ink::MarkPhase::Dash;
    const Ink::Color typeCol = phased
        ? MkCol(dash ? Tok::C_EditHandle_Vector : Tok::C_EditHandle_Mirrored, 1.0f)
        : MkCol(Tok::S_Color_Accent_Default, 1.0f);
    const Ink::Color centre =
        state == HState::Selected ? MkCol(Tok::S_State_Active_OnPage, 1.0f)
                                  : typeCol;
    ov.AddCircleFilled(sp, 3.5f, centre);
    ov.AddCircle(sp, 3.5f, MkCol(Tok::C_EditHandle_VertexRing, 1.0f), 1.0f);

    if (phased) {
        // Tangent-aligned diamond (Dash) / square (Gap) — the phase indicator.
        float tx = tv.x, ty = tv.y;
        const float tl = std::sqrt(tx * tx + ty * ty);
        if (tl < 1e-4f) { tx = 1.0f; ty = 0.0f; } else { tx /= tl; ty /= tl; }
        const float nx = -ty, ny = tx, r = 6.0f;
        auto P = [&](float a, float b) {
            return Ink::Vec2{ sp.x + tx * a + nx * b, sp.y + ty * a + ny * b };
        };
        if (dash) {
            ov.AddLine(P(r, 0), P(0, r), typeCol, 1.2f);
            ov.AddLine(P(0, r), P(-r, 0), typeCol, 1.2f);
            ov.AddLine(P(-r, 0), P(0, -r), typeCol, 1.2f);
            ov.AddLine(P(0, -r), P(r, 0), typeCol, 1.2f);
        } else {
            const float h = r * 0.72f;
            ov.AddLine(P(h, h), P(-h, h), typeCol, 1.2f);
            ov.AddLine(P(-h, h), P(-h, -h), typeCol, 1.2f);
            ov.AddLine(P(-h, -h), P(h, -h), typeCol, 1.2f);
            ov.AddLine(P(h, -h), P(h, h), typeCol, 1.2f);
        }
    }
    if (state == HState::Normal) return;
    const float r = state == HState::Selected ? 8.0f : 7.0f;
    const float th = state == HState::Selected ? 2.0f : 1.5f;
    ov.AddCircle(sp, r, state == HState::Selected
                            ? MkCol(Tok::S_State_Active_OnPage, 1.0f)
                            : MkCol(Tok::S_Color_Accent_Default, 1.0f), th);
}

} // namespace

// Tool active + marks selected → G/R/X act on the marks, not the objects.
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
    const double precision = io.KeyShift ? 0.1 : 1.0;

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
    // World point (accounting for side/offset) + tangent of a mark.
    auto markWorld = [&](const EditContext::MarkRef& r, Ink::DVec2& p,
                         Ink::DVec2& tn) -> bool {
        const Ink::StrokeMark* m = markOf(r);
        if (!m) return false;
        auto polys = FlattenWorld(doc, r.node, zoom);
        if (m->sub < 0 || m->sub >= (int)polys.size()) return false;
        Ink::DVec2 base;
        PointAtT(polys[(std::size_t)m->sub], m->t, base, tn);
        const Ink::DVec2 nrm{ -tn.y, tn.x };
        double off = 0.0;
        if (m->side == Ink::MarkSide::Left)  off =  m->offset;
        if (m->side == Ink::MarkSide::Right) off = -m->offset;
        p = { base.x + nrm.x * off, base.y + nrm.y * off };
        return true;
    };

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
    // A ghost of a mark being placed / dragged: the point dot + a faint ring
    // for each of its objects (radius ≈ object size in view px).
    auto drawGhost = [&](Ink::NodeId nodeId, int strokeIdx,
                         const Ink::StrokeMark& m, const WorldPoly& poly) {
        const Ink::Node* n = doc.Find(nodeId);
        if (!n || strokeIdx < 0 || strokeIdx >= (int)n->style.strokes.size())
            return;
        const Ink::DMat23 w = doc.WorldTransform(nodeId);
        const double wsc =
            std::max(1e-6, std::sqrt(std::abs(w.m[0]*w.m[4] - w.m[1]*w.m[3])));
        const double total = PolyTotal(poly);
        if (total < 1e-9) return;
        Ink::DVec2 base, tn;
        PointAtArc(poly, (m.t < 0 ? 0 : m.t > 1 ? 1 : m.t) * total, base, tn);
        const Ink::DVec2 nrm{ -tn.y, tn.x };
        double off = 0.0;
        if (m.side == Ink::MarkSide::Left)  off =  m.offset;
        if (m.side == Ink::MarkSide::Right) off = -m.offset;
        const Ink::DVec2 at{ base.x + nrm.x * off, base.y + nrm.y * off };
        const Ink::Color col = MkCol(Tok::S_Color_Accent_Default, 0.7f);
        const Ink::Vec2 c = d2v(at);
        // A guide from the line to the offset point.
        if (m.side != Ink::MarkSide::Center)
            DashLine(ov, d2v(base), c, MkCol(Tok::S_Color_Text_Subtle, 0.6f), 1.0f);
        for (const Ink::MarkObject& o : m.objects) {
            const float rad = (float)(o.size * wsc * zoom);
            ov.AddCircle(c, std::max(2.0f, rad), col, 1.2f);
        }
        ov.AddCircleFilled(c, 3.0f, col);
    };

    // ── Modal G (slide along curve) ──────────────────────────────────────────
    if (markGrab_.Active() && markGrab_.owner == nullptr && hovered)
        markGrab_.owner = self;
    if (markGrab_.Active() && markGrab_.owner == self) {
        const bool commit = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        double deltaT = 0.0;
        if (!markGrab_.refs.empty()) {
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
        for (std::size_t k = 0; k < markGrab_.refs.size(); ++k) {
            const EditContext::MarkRef& r = markGrab_.refs[k];
            const Ink::StrokeMark* m = markOf(r);
            if (!m) continue;
            Ink::StrokeMark ghost = *m;
            ghost.t = previewT(k);
            auto polys = FlattenWorld(doc, r.node, zoom);
            if (ghost.sub < 0 || ghost.sub >= (int)polys.size()) continue;
            drawGhost(r.node, r.stroke, ghost, polys[(std::size_t)ghost.sub]);
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
                mk[(std::size_t)r.index].t = previewT(k);
                doc.SetStyle(r.node, sty);
            }
            commitStyles(std::move(ps), "Move Line Marks");
            markGrab_.Reset();
        }
        return;
    }

    // ── Click-drag of a grabbed mark ─────────────────────────────────────────
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
            Ink::StrokeMark ghost = *m;
            ghost.t = markDrag_.dragT;
            drawGhost(markDrag_.ref.node, markDrag_.ref.stroke, ghost, poly);
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
                    sm.t = (r == markDrag_.ref)
                               ? markDrag_.dragT
                               : std::clamp(sm.t + deltaT, 0.0, 1.0);
                    doc.SetStyle(r.node, sty);
                }
                commitStyles(std::move(ps),
                             edit_.markSel.size() > 1 ? "Move Line Marks"
                                                      : "Move Line Mark");
            }
            markDrag_ = {};
        }
        return;
    }

    // ── Hit-test existing marks (nearest handle within 9 px) ─────────────────
    const float kMarkPx = 9.0f;
    EditContext::MarkRef hit;
    float hitD = kMarkPx + 1.0f;
    for (Ink::NodeId id : pathNodes) {
        const Ink::Node* n = doc.Find(id);
        for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
            const auto& mk = n->style.strokes[(std::size_t)si].marks;
            for (int mi = 0; mi < (int)mk.size(); ++mi) {
                Ink::DVec2 p, tn;
                if (!markWorld({ id, si, mi }, p, tn)) continue;
                const Ink::Vec2 v = d2v(p);
                const float dpx = std::hypot(mp.x - (v.x + cam.canvasMin.x),
                                             mp.y - (v.y + cam.canvasMin.y));
                if (dpx < hitD) { hitD = dpx; hit = { id, si, mi }; }
            }
        }
    }

    // ── Handles for every mark (dot + phase indicator + selection ring) ──────
    for (Ink::NodeId id : pathNodes) {
        const Ink::Node* n = doc.Find(id);
        for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
            const auto& mk = n->style.strokes[(std::size_t)si].marks;
            for (int mi = 0; mi < (int)mk.size(); ++mi) {
                const EditContext::MarkRef ref{ id, si, mi };
                Ink::DVec2 p, tn;
                if (!markWorld(ref, p, tn)) continue;
                const bool seld = edit_.MarkSelected(ref);
                const bool hov = ref == hit;
                const Ink::Vec2 c = d2v(p);
                const Ink::Vec2 t2 = d2v({ p.x + tn.x, p.y + tn.y });
                DrawHandle(ov, c, { t2.x - c.x, t2.y - c.y },
                           mk[(std::size_t)mi],
                           seld ? HState::Selected
                                : hov ? HState::Hover : HState::Normal);
            }
        }
    }

    // ── Box-select ───────────────────────────────────────────────────────────
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
                for (int si = 0; si < (int)n->style.strokes.size(); ++si) {
                    const auto& mk = n->style.strokes[(std::size_t)si].marks;
                    for (int mi = 0; mi < (int)mk.size(); ++mi) {
                        Ink::DVec2 p, tn;
                        if (!markWorld({ id, si, mi }, p, tn)) continue;
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
            if (io.KeyAlt) {
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
            }
        }
        return;
    }

    // ── Nearest stroked line → ghost + click places a NEW mark ───────────────
    struct Best {
        Ink::NodeId id = Ink::kNullNode;
        int stroke = -1, sub = -1;
        double t = 0.5;
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
                if (dpx < best.dpx)
                    best = { id, strokeIdx, sub,
                             total > 1e-6 ? (acc + segLen * u) / total : 0.0,
                             dpx };
                acc += segLen;
            }
        }
    }

    const float kPickPx = 14.0f;
    if (best.id != Ink::kNullNode && best.dpx <= kPickPx) {
        const Ink::Node* n = doc.Find(best.id);
        const Ink::Stroke& sk = n->style.strokes[(std::size_t)best.stroke];
        // A neutral mark with one default object (the top-bar shape).
        Ink::StrokeMark m;
        m.sub = best.sub;
        m.t = best.t;
        m.side = Ink::MarkSide::Center;
        Ink::MarkObject obj;
        obj.shape = markPlaceShape_;
        obj.mode = markPlaceSubtract_ ? Ink::MarkObjectMode::Subtract
                                      : Ink::MarkObjectMode::Add;
        obj.size = std::max(4.0, sk.width * 3.0);
        m.objects.push_back(obj);
        auto polys = FlattenWorld(doc, best.id, zoom);
        if (best.sub >= 0 && best.sub < (int)polys.size())
            drawGhost(best.id, best.stroke, m, polys[(std::size_t)best.sub]);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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

// G slides the selection along the curve; R cycles its side; X deletes.
void Application::BeginMarkTransform(TransformOp::Kind kind) {
    if (!project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;

    if (kind == TransformOp::Kind::Rotate) {
        // Instant: cycle the side of every selected mark Center→Left→Right.
        struct StylePair { Ink::NodeId id; Ink::Style before, after; };
        std::vector<StylePair> ps;
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
            Ink::MarkSide& sd = mk[(std::size_t)r.index].side;
            sd = sd == Ink::MarkSide::Center ? Ink::MarkSide::Left
               : sd == Ink::MarkSide::Left   ? Ink::MarkSide::Right
                                             : Ink::MarkSide::Center;
            doc.SetStyle(r.node, sty);
        }
        if (ps.empty()) return;
        for (StylePair& p : ps)
            if (const Ink::Node* n = doc.Find(p.id)) p.after = n->style;
        PushDocCommand("Cycle Mark Side",
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.before);
            },
            [ps](Ink::Document& d) {
                for (const StylePair& p : ps) d.SetStyle(p.id, p.after);
            });
        LogInfoAction("Cycle Mark Side");
        return;
    }
    if (kind != TransformOp::Kind::Move) return;   // no S on marks

    // Move → arm the modal slide (driven by the first hovered leaf).
    markGrab_.Reset();
    markGrab_.op = 1;
    markGrab_.owner = nullptr;
    markGrab_.startMouse = ImGui::GetIO().MousePos;
    for (const EditContext::MarkRef& r : edit_.markSel) {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            continue;
        const auto& mk = n->style.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) continue;
        markGrab_.refs.push_back(r);
        markGrab_.t0.push_back(mk[(std::size_t)r.index].t);
    }
    if (markGrab_.refs.empty()) markGrab_.Reset();
}

void Application::DeleteSelectedMarks() {
    if (!project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;
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

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
//  Line-Mark MODE (EditorMode::LineMark) — the GENERIC core mark workflow
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

// A fresh mark object with the default dimensions (in % of the stroke width):
// a circle radius 100 %, a rectangle length 200 % × width 100 %, a diamond
// diagonal 100 %, with the shape's default bend (Rectangle/Diamond → Follow,
// Circle/Instance → Hard). Shared placement/Properties default.
Ink::MarkObject DefaultMarkObject(Ink::MarkShape shape) {
    Ink::MarkObject o;
    o.shape = shape;
    o.sizePercent = true;
    o.bend = Ink::DefaultBendFor(shape);
    if (shape == Ink::MarkShape::Rectangle) { o.size = 200.0; o.width = 100.0; }
    else                                    { o.size = 100.0; o.width = 100.0; }
    return o;
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

// The arc-length fraction t ∈ [0,1] of the polyline point CLOSEST to `q` — the
// robust "slide to the cursor" used by the legacy mark move (no tangent
// projection drift on curves).
double ClosestT(const WorldPoly& p, Ink::DVec2 q) {
    const std::size_t n = p.pts.size();
    if (n < 2) return 0.0;
    const std::size_t sc = p.closed ? n : n - 1;
    const double total = PolyTotal(p);
    if (total < 1e-9) return 0.0;
    double acc = 0.0, best = 1e300, bestArc = 0.0;
    for (std::size_t i = 0; i < sc; ++i) {
        const Ink::DVec2 a = p.pts[i], b = p.pts[(i + 1) % n];
        const double abx = b.x - a.x, aby = b.y - a.y;
        const double L2 = abx * abx + aby * aby;
        const double L = std::sqrt(L2);
        if (L < 1e-12) continue;
        double u = ((q.x - a.x) * abx + (q.y - a.y) * aby) / L2;
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
        const double dx = q.x - (a.x + abx * u), dy = q.y - (a.y + aby * u);
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; bestArc = acc + L * u; }
        acc += L;
    }
    return bestArc / total;
}

enum class HState { Normal, Hover, Selected };

// A mark's clickable construction handle (legacy look): the dash-phase decides
// the FILLED glyph — a violet DIAMOND (Dash), a green SQUARE (Gap), or a plain
// grey dot (Neutral). The glyph and the hover/select ring keep the TYPE colour
// in every state; only the CENTRE DOT turns active-orange when selected (the
// exact legacy convention). `tv` is the curve tangent (view space).
void DrawHandle(Ink::OverlayList& ov, Ink::Vec2 sp, Ink::Vec2 tv,
                const Ink::StrokeMark& m, HState state) {
    const bool dash = m.phase == Ink::MarkPhase::Dash;
    const bool gap  = m.phase == Ink::MarkPhase::Gap;
    const Ink::Color typeCol =
        dash ? MkCol(Tok::C_EditHandle_Vector, 1.0f)     // violet
      : gap  ? MkCol(Tok::C_EditHandle_Mirrored, 1.0f)   // green
             : MkCol(Tok::S_Color_Accent_Default, 1.0f); // Neutral = accent
    const Ink::Color grey = MkCol(Tok::S_Color_Text_Subtle, 1.0f);
    const Ink::Color ring = MkCol(Tok::C_EditHandle_VertexRing, 1.0f);
    const Ink::Color orange = MkCol(Tok::S_State_Active_OnPage, 1.0f);

    float tx = tv.x, ty = tv.y;
    const float tl = std::sqrt(tx * tx + ty * ty);
    if (tl < 1e-4f) { tx = 1.0f; ty = 0.0f; } else { tx /= tl; ty /= tl; }
    const float nx = -ty, ny = tx;
    auto P = [&](float a, float b) {
        return Ink::Vec2{ sp.x + tx * a + nx * b, sp.y + ty * a + ny * b };
    };
    if (dash) {                 // filled diamond (vertices on ±tangent/±normal)
        const float r = 4.5f;
        ov.AddTriangle(P(r, 0), P(0, r), P(-r, 0), typeCol);
        ov.AddTriangle(P(r, 0), P(-r, 0), P(0, -r), typeCol);
        ov.AddLine(P(r, 0), P(0, r), ring, 1.0f);
        ov.AddLine(P(0, r), P(-r, 0), ring, 1.0f);
        ov.AddLine(P(-r, 0), P(0, -r), ring, 1.0f);
        ov.AddLine(P(0, -r), P(r, 0), ring, 1.0f);
    } else if (gap) {           // filled square aligned to the curve
        const float h = 3.6f;
        ov.AddQuad(P(h, h), P(-h, h), P(-h, -h), P(h, -h), typeCol);
        ov.AddLine(P(h, h), P(-h, h), ring, 1.0f);
        ov.AddLine(P(-h, h), P(-h, -h), ring, 1.0f);
        ov.AddLine(P(-h, -h), P(h, -h), ring, 1.0f);
        ov.AddLine(P(h, -h), P(h, h), ring, 1.0f);
    } else {                    // Neutral: plain ringed dot in grey
        ov.AddCircleFilled(sp, 3.5f, grey);
        ov.AddCircle(sp, 3.5f, ring, 1.0f);
    }
    // Selected: an orange centre dot over the glyph (only the dot turns orange).
    if (state == HState::Selected)
        ov.AddCircleFilled(sp, 2.0f, orange);

    if (state == HState::Normal) return;
    // Hover/select ring in the TYPE colour, its SHAPE matching the glyph:
    // a diamond for Dash, a square for Gap, a circle for Neutral.
    const float rr = state == HState::Selected ? 8.5f : 7.5f;
    const float th = state == HState::Selected ? 2.0f : 1.5f;
    if (dash) {
        ov.AddLine(P(rr, 0), P(0, rr), typeCol, th);
        ov.AddLine(P(0, rr), P(-rr, 0), typeCol, th);
        ov.AddLine(P(-rr, 0), P(0, -rr), typeCol, th);
        ov.AddLine(P(0, -rr), P(rr, 0), typeCol, th);
    } else if (gap) {
        const float hh = rr * 0.8f;
        ov.AddLine(P(hh, hh), P(-hh, hh), typeCol, th);
        ov.AddLine(P(-hh, hh), P(-hh, -hh), typeCol, th);
        ov.AddLine(P(-hh, -hh), P(hh, -hh), typeCol, th);
        ov.AddLine(P(hh, -hh), P(hh, hh), typeCol, th);
    } else {
        ov.AddCircle(sp, rr, typeCol, th);
    }
}

} // namespace

// Line-Mark MODE active (the third editor mode).
bool Application::MarkModeActive() const {
    return edit_.mode == EditorMode::LineMark;
}
// Mode active + marks selected → G/R/S/X act on the marks, not the objects.
bool Application::MarkToolArmed() const {
    return MarkModeActive() && !edit_.markSel.empty();
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
    // World point (accounting for side/offset) + tangent of a mark. The offset
    // is node-local (percent of stroke width or doc-units), so it scales by the
    // node's world scale to reach world space.
    auto markWorld = [&](const EditContext::MarkRef& r, Ink::DVec2& p,
                         Ink::DVec2& tn) -> bool {
        const Ink::StrokeMark* m = markOf(r);
        if (!m) return false;
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            return false;
        auto polys = FlattenWorld(doc, r.node, zoom);
        if (m->sub < 0 || m->sub >= (int)polys.size()) return false;
        Ink::DVec2 base;
        PointAtT(polys[(std::size_t)m->sub], m->t, base, tn);
        const Ink::DVec2 nrm{ -tn.y, tn.x };
        const Ink::DMat23 w = doc.WorldTransform(r.node);
        const double wsc =
            std::max(1e-6, std::sqrt(std::abs(w.m[0]*w.m[4] - w.m[1]*w.m[3])));
        const double sw = n->style.strokes[(std::size_t)r.stroke].width;
        double off = 0.0;
        if (m->side == Ink::MarkSide::Left)  off =  m->OffsetUnits(sw);
        if (m->side == Ink::MarkSide::Right) off = -m->OffsetUnits(sw);
        p = { base.x + nrm.x * off * wsc, base.y + nrm.y * off * wsc };
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
    // A TRANSLUCENT preview of the mark's objects at position `m.t` — the real
    // filled shape (as it will render), the same look the legacy tool had while
    // sliding. Flattens the host path in LOCAL space, builds each object's
    // contour with the shared engine helper, then maps world→view.
    auto drawGhost = [&](Ink::NodeId nodeId, int strokeIdx,
                         const Ink::StrokeMark& m, const WorldPoly& worldPoly) {
        const Ink::Node* n = doc.Find(nodeId);
        if (!n || strokeIdx < 0 || strokeIdx >= (int)n->style.strokes.size())
            return;
        const Ink::Stroke& sk = n->style.strokes[(std::size_t)strokeIdx];
        const Ink::DMat23 w = doc.WorldTransform(nodeId);
        const double wsc =
            std::max(1e-6, std::sqrt(std::abs(w.m[0]*w.m[4] - w.m[1]*w.m[3])));
        const double localTol = std::max(1e-4, 0.5 / (std::max(1e-6, zoom) * wsc));
        // The PLACEMENT spine uses the same fixed fine tolerance as the render
        // (kMarkPlaceTolerance) so the ghost lands exactly where the object will.
        auto placePolys = Ink::geom::Flatten(n->path, Ink::geom::kMarkPlaceTolerance);
        if (m.sub < 0 || m.sub >= (int)placePolys.size()) return;
        const Ink::geom::Polyline& spine = placePolys[(std::size_t)m.sub];
        // The stroke paint (or the object's own) at a preview alpha.
        const float alpha = 0.55f;
        const Ink::Color base = sk.paint.color;
        // The mark point (with side/offset) in world space, for guides/hints.
        Ink::DVec2 markW{ 0, 0 }; bool haveMarkW = false;
        {
            const double total = PolyTotal(worldPoly);
            if (total > 1e-9) {
                Ink::DVec2 bp, bt;
                PointAtArc(worldPoly,
                           (m.t < 0 ? 0 : m.t > 1 ? 1 : m.t) * total, bp, bt);
                const Ink::DVec2 nrm{ -bt.y, bt.x };
                double off = 0.0;
                if (m.side == Ink::MarkSide::Left)  off =  m.OffsetUnits(sk.width);
                if (m.side == Ink::MarkSide::Right) off = -m.OffsetUnits(sk.width);
                markW = { bp.x + nrm.x * off * wsc, bp.y + nrm.y * off * wsc };
                haveMarkW = true;
                if (m.side != Ink::MarkSide::Center)
                    DashLine(ov, d2v(bp), d2v(markW),
                             MkCol(Tok::S_Color_Text_Subtle, 0.6f), 1.0f);
            }
        }
        for (const Ink::MarkObject& o : m.objects) {
            Ink::Color col{ base.r * alpha, base.g * alpha, base.b * alpha, alpha };
            if (!o.useStrokeColor && o.shape != Ink::MarkShape::Instance)
                col = { o.color.r * alpha, o.color.g * alpha,
                        o.color.b * alpha, alpha };
            if (o.shape == Ink::MarkShape::Instance) {
                // A translucent preview of the referenced node's geometry placed
                // at the mark (frame · scale · cancel-target-transform), exactly
                // as the Scene will render it.
                const Ink::Node* tgt = doc.Find(o.nodeRef);
                if (!tgt || tgt->kind != Ink::NodeKind::Path ||
                    tgt->path.Empty()) {
                    if (haveMarkW)
                        ov.AddCircle(d2v(markW), 6.0f,
                                     MkCol(Tok::S_Color_Accent_Default, 0.7f), 1.5f);
                    continue;
                }
                const Ink::DMat23 frame =
                    Ink::geom::MarkPlaceMatrix(spine, m, o, sk.width);
                const double k = o.sizePercent ? o.size * 0.01
                                               : std::max(1e-6, o.size);
                Ink::DMat23 scaleM; scaleM.m[0] = k; scaleM.m[4] = k;
                // The target's OWN transform is cancelled by the Scene, so its
                // local geometry lands under place = frame · scale.
                const Ink::DMat23 place = frame.Compose(scaleM);
                Ink::Color tcol = tgt->style.fills.empty()
                    ? MkCol(Tok::S_Color_Accent_Default, alpha)
                    : Ink::Color{ tgt->style.fills.front().paint.color.r * alpha,
                                  tgt->style.fills.front().paint.color.g * alpha,
                                  tgt->style.fills.front().paint.color.b * alpha,
                                  alpha };
                for (const auto& pl : Ink::geom::Flatten(tgt->path, localTol)) {
                    if (pl.points.size() < 3) continue;
                    Ink::DVec2 cn{ 0, 0 };
                    for (const Ink::DVec2& p : pl.points) { cn.x+=p.x; cn.y+=p.y; }
                    cn.x /= (double)pl.points.size();
                    cn.y /= (double)pl.points.size();
                    const Ink::Vec2 cvv = d2v(w.Apply(place.Apply(cn)));
                    for (std::size_t i = 0; i < pl.points.size(); ++i) {
                        const Ink::DVec2 aw = w.Apply(place.Apply(pl.points[i]));
                        const Ink::DVec2 bw = w.Apply(place.Apply(
                            pl.points[(i + 1) % pl.points.size()]));
                        ov.AddTriangle(cvv, d2v(aw), d2v(bw), tcol);
                    }
                }
                continue;
            }
            // The real filled shape (as it renders): a Follow object bends its
            // outline along the curve; Hard/Bend place a parametric primitive.
            std::vector<Ink::DVec2> ring;
            if (o.bend == Ink::MarkBend::Follow) {
                if (!Ink::geom::MarkFollowContour(spine, m, o, sk.width,
                                                  localTol, ring))
                    continue;
            } else {
                const Ink::PathData shape =
                    Ink::geom::MarkPrimitiveShape(o, sk.width);
                if (shape.Empty()) continue;
                const Ink::DMat23 place =
                    Ink::geom::MarkPlaceMatrix(spine, m, o, sk.width);
                for (const auto& pl : Ink::geom::Flatten(shape, localTol))
                    for (const Ink::DVec2& q : pl.points)
                        ring.push_back(place.Apply(q));
            }
            if (ring.size() < 3) continue;
            // Fan-fill in view space.
            Ink::DVec2 cen{ 0, 0 };
            for (const Ink::DVec2& p : ring) { cen.x += p.x; cen.y += p.y; }
            cen.x /= (double)ring.size(); cen.y /= (double)ring.size();
            const Ink::Vec2 cv = d2v(w.Apply(cen));
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Ink::DVec2 aw = w.Apply(ring[i]);
                const Ink::DVec2 bw = w.Apply(ring[(i + 1) % ring.size()]);
                ov.AddTriangle(cv, d2v(aw), d2v(bw), col);
            }
        }
    };

    // Find the nearest stroked line to the cursor → (node, stroke, sub, t).
    auto nearestLine = [&](Ink::NodeId& outId, int& outStroke, int& outSub,
                           double& outT) -> bool {
        float bestD = 1e9f; bool found = false;
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
                    if (dpx < bestD) {
                        bestD = dpx; found = true;
                        outId = id; outStroke = strokeIdx; outSub = sub;
                        outT = total > 1e-6 ? (acc + segLen * u) / total : 0.0;
                    }
                    acc += segLen;
                }
            }
        }
        return found && bestD <= 14.0f;
    };

    // ── Ctrl+V paste: a translucent preview of the clipboard marks follows the
    //    cursor; a click drops them on the nearest line, RMB/Esc cancels. ──────
    if (markPasteActive_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            markPasteActive_ = false;
            return;
        }
        Ink::NodeId id; int si, sub; double t;
        const bool onLine = nearestLine(id, si, sub, t);
        if (onLine) {
            // Anchor the paste on the clipboard's first mark; keep the others'
            // relative t so a multi-mark paste preserves its spacing.
            const double baseT = markClipboard_.front().t;
            auto polys = FlattenWorld(doc, id, zoom);
            for (const Ink::StrokeMark& cm : markClipboard_) {
                Ink::StrokeMark g = cm;
                g.sub = sub;
                g.t = std::clamp(t + (cm.t - baseT), 0.0, 1.0);
                if (sub >= 0 && sub < (int)polys.size())
                    drawGhost(id, si, g, polys[(std::size_t)sub]);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                struct SP { Ink::NodeId id; Ink::Style before, after; };
                const Ink::Node* n = doc.Find(id);
                if (n && si >= 0 && si < (int)n->style.strokes.size()) {
                    SP sp{ id, n->style, {} };
                    Ink::Style sty = n->style;
                    auto& mk = sty.strokes[(std::size_t)si].marks;
                    edit_.markSel.clear();
                    for (const Ink::StrokeMark& cm : markClipboard_) {
                        Ink::StrokeMark nm = cm;
                        nm.sub = sub;
                        nm.t = std::clamp(t + (cm.t - baseT), 0.0, 1.0);
                        edit_.markSel.push_back({ id, si, (int)mk.size() });
                        mk.push_back(nm);
                    }
                    doc.SetStyle(id, sty);
                    sp.after = doc.Find(id)->style;
                    PushDocCommand("Paste Line Marks",
                        [sp](Ink::Document& d) { d.SetStyle(sp.id, sp.before); },
                        [sp](Ink::Document& d) { d.SetStyle(sp.id, sp.after); });
                    LogInfoAction("Paste Line Marks");
                    edit_.SelectAdd(id);
                }
                markPasteActive_ = false;
            }
        }
        return;   // paste owns the input
    }

    // ── Modal G (slide) / R (rotate objects) / S (scale objects) ─────────────
    if (markGrab_.Active() && markGrab_.owner == nullptr && hovered)
        markGrab_.owner = self;
    if (markGrab_.Active() && markGrab_.owner == self) {
        const bool commit = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right);

        if (markGrab_.op == 1) {
            // Slide along the curve (each mark to the point nearest the cursor).
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            double deltaT = 0.0;
            if (!markGrab_.refs.empty()) {
                const EditContext::MarkRef& a = markGrab_.refs.front();
                if (const Ink::StrokeMark* am = markOf(a)) {
                    auto polys = FlattenWorld(doc, a.node, zoom);
                    if (am->sub >= 0 && am->sub < (int)polys.size()) {
                        const double target =
                            ClosestT(polys[(std::size_t)am->sub], mdoc);
                        deltaT = (target - markGrab_.t0.front()) * precision;
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

        // R / S: rotate or scale the objects of every selected mark by one
        // amount, taken from the mouse relative to the anchor mark's pivot.
        // The pivot is the anchor mark's point (Local Origin pivot).
        Ink::DVec2 pivotW{ 0, 0 }, dummyT;
        bool havePivot = markWorld(markGrab_.refs.front(), pivotW, dummyT);
        const Ink::Vec2 pv = havePivot ? d2v(pivotW) : Ink::Vec2{ 0, 0 };
        const ImVec2 pivotS{ pv.x + cam.canvasMin.x, pv.y + cam.canvasMin.y };
        double deltaRot = 0.0, factor = 1.0;
        if (markGrab_.op == 2) {
            const double a0 = std::atan2(markGrab_.startMouse.y - pivotS.y,
                                         markGrab_.startMouse.x - pivotS.x);
            const double a1 = std::atan2(mp.y - pivotS.y, mp.x - pivotS.x);
            deltaRot = (a1 - a0) * precision;
        } else {   // scale
            const double d0 = std::hypot(markGrab_.startMouse.x - pivotS.x,
                                         markGrab_.startMouse.y - pivotS.y);
            const double d1 = std::hypot(mp.x - pivotS.x, mp.y - pivotS.y);
            const double raw = d0 > 1e-3 ? d1 / d0 : 1.0;
            factor = std::clamp(1.0 + (raw - 1.0) * precision, 0.01, 100.0);
        }
        // Apply to a copy for the ghost, and (on commit) to the real style.
        auto apply = [&](Ink::MarkObject& o, std::size_t k) {
            if (markGrab_.op == 2) {
                o.rotation = markGrab_.rot0[k] + deltaRot;
            } else {
                // Axis: free scales both; X = length(size); Y = width.
                if (markGrab_.axis != 1) o.size  = std::max(0.0, markGrab_.size0[k] * factor);
                if (markGrab_.axis != 0) o.width = std::max(0.0, markGrab_.width0[k] * factor);
                // A circle / diamond has a single dimension → always size.
                if (o.shape == Ink::MarkShape::Circle ||
                    o.shape == Ink::MarkShape::Diamond ||
                    o.shape == Ink::MarkShape::Instance)
                    o.size = std::max(0.0, markGrab_.size0[k] * factor);
            }
        };
        // Ghost + pivot guide.
        if (havePivot) {
            DashLine(ov, pv, { mp.x - cam.canvasMin.x, mp.y - cam.canvasMin.y },
                     MkCol(Tok::S_Color_Accent_Default, 0.8f), 1.0f);
            ov.AddCircle(pv, 5.0f, MkCol(Tok::S_Color_Accent_Default, 1.0f), 1.5f);
        }
        for (std::size_t k = 0; k < markGrab_.refs.size(); ++k) {
            const EditContext::MarkRef& r = markGrab_.refs[k];
            const Ink::StrokeMark* m = markOf(r);
            if (!m || m->objects.empty()) continue;
            Ink::StrokeMark ghost = *m;
            for (Ink::MarkObject& go : ghost.objects) apply(go, k);
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
                for (Ink::MarkObject& o : mk[(std::size_t)r.index].objects)
                    apply(o, k);
                doc.SetStyle(r.node, sty);
            }
            commitStyles(std::move(ps),
                         markGrab_.op == 2 ? "Rotate Marks" : "Scale Marks");
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
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            // Slide to the curve point closest to the cursor (robust); the
            // grabbed mark's own t0 is its position at press.
            const double target = ClosestT(poly, mdoc);
            deltaT = (target - markDrag_.dragT0) * precision;
            markDrag_.dragT = std::clamp(markDrag_.dragT0 + deltaT, 0.0, 1.0);
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

    // ── Hovered construction line: highlight the nearest curve's outline so it
    //    reads as a placement target while in Line-Mark mode. ─────────────────
    if (hovered) {
        Ink::NodeId hid; int hsi, hsub; double ht;
        if (nearestLine(hid, hsi, hsub, ht)) {
            auto polys = FlattenWorld(doc, hid, zoom);
            const Ink::Color lc = MkCol(Tok::S_State_Active_OnPage, 0.7f);
            for (const WorldPoly& poly : polys) {
                const std::size_t np = poly.pts.size();
                const std::size_t last = poly.closed ? np : np - 1;
                for (std::size_t i = 0; i < last; ++i)
                    ov.AddLine(d2v(poly.pts[i]),
                               d2v(poly.pts[(i + 1) % np]), lc, 1.0f);
            }
        }
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
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
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
            } else if (io.KeyCtrl) {
                // Ctrl+Click cycles the dash phase (Neutral→Dash→Gap) — a plain
                // click is reserved for selection.
                std::vector<StylePair> ps = snapshotStyles({ hit });
                const Ink::Node* n = doc.Find(hit.node);
                if (n) {
                    Ink::Style sty = n->style;
                    auto& mk = sty.strokes[(std::size_t)hit.stroke].marks;
                    Ink::MarkPhase& ph = mk[(std::size_t)hit.index].phase;
                    ph = ph == Ink::MarkPhase::Neutral ? Ink::MarkPhase::Dash
                       : ph == Ink::MarkPhase::Dash    ? Ink::MarkPhase::Gap
                                                       : Ink::MarkPhase::Neutral;
                    doc.SetStyle(hit.node, sty);
                    commitStyles(std::move(ps), "Cycle Dash Phase");
                }
            } else if (io.KeyShift) {
                edit_.MarkToggle(hit);
                edit_.SelectAdd(hit.node);
            } else {
                // Plain click selects + arms a slide (a drag past the threshold
                // moves the mark along the curve).
                if (!edit_.MarkSelected(hit)) edit_.MarkSelectOnly(hit);
                edit_.SelectAdd(hit.node);
                markDrag_ = {};
                markDrag_.armed = true;
                markDrag_.ref = hit;
                markDrag_.pressPos = mp;
                if (const Ink::StrokeMark* hm = markOf(hit))
                    markDrag_.dragT0 = markDrag_.dragT = hm->t;
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
        const bool ctrl = io.KeyCtrl;
        // Default: a NEUTRAL mark with one default object (the top-bar shape,
        // fused into the stroke). Ctrl: an OBJECTLESS Dash tick — a pure
        // dash/gap re-phaser, no drawn shape.
        Ink::StrokeMark m;
        m.sub = best.sub;
        m.t = best.t;
        m.side = Ink::MarkSide::Center;
        (void)sk;
        if (ctrl) {
            m.phase = Ink::MarkPhase::Dash;   // objectless re-phaser
        } else {
            Ink::MarkObject obj = DefaultMarkObject(markPlaceShape_);
            obj.mode = markPlaceSubtract_ ? Ink::MarkObjectMode::Subtract
                                          : Ink::MarkObjectMode::Fusion;
            m.objects.push_back(obj);
        }
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
            commitStyles(std::move(ps),
                         ctrl ? "Add Dash Tick" : "Add Line Mark");
        }
        return;
    }

    // ── Empty press: box-select (Shift adds); a plain click clears ───────────
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        markBox_ = { true, self, mdoc, io.KeyShift };
        if (!io.KeyShift) edit_.markSel.clear();
    }
}

// G slides the selection along the curve; R rotates each mark's objects; S
// scales them (R+X/S+X → length axis, R+Y/S+Y → width axis). X deletes.
void Application::BeginMarkTransform(TransformOp::Kind kind) {
    if (!project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;

    markGrab_.Reset();
    markGrab_.op = kind == TransformOp::Kind::Rotate ? 2
                 : kind == TransformOp::Kind::Scale  ? 3 : 1;
    markGrab_.axis = -1;
    markGrab_.owner = nullptr;
    markGrab_.startMouse = ImGui::GetIO().MousePos;
    for (const EditContext::MarkRef& r : edit_.markSel) {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            continue;
        const auto& mk = n->style.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) continue;
        const Ink::StrokeMark& m = mk[(std::size_t)r.index];
        markGrab_.refs.push_back(r);
        markGrab_.t0.push_back(m.t);
        // Snapshot the FIRST object's rotation/size (the modal drives every
        // selected mark's objects by the same amount).
        double rot = 0, sz = 0, wd = 0;
        if (!m.objects.empty()) {
            rot = m.objects.front().rotation;
            sz  = m.objects.front().size;
            wd  = m.objects.front().width;
        }
        markGrab_.rot0.push_back(rot);
        markGrab_.size0.push_back(sz);
        markGrab_.width0.push_back(wd);
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

// V cycles the side of every selected mark Center → Left → Right (moved off R,
// which now rotates the mark objects).
void Application::Action_CycleMarkSide() {
    if (!MarkModeActive() || !project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;
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
        [ps](Ink::Document& d) { for (const StylePair& p : ps) d.SetStyle(p.id, p.before); },
        [ps](Ink::Document& d) { for (const StylePair& p : ps) d.SetStyle(p.id, p.after); });
    LogInfoAction("Cycle Mark Side");
}

// Ctrl+C copies the selected marks (their full data) into the mark clipboard.
void Application::Action_CopyMarks() {
    if (!MarkModeActive() || !project_.document || edit_.markSel.empty()) return;
    Ink::Document& doc = *project_.document;
    markClipboard_.clear();
    for (const EditContext::MarkRef& r : edit_.markSel) {
        const Ink::Node* n = doc.Find(r.node);
        if (!n || r.stroke < 0 || r.stroke >= (int)n->style.strokes.size())
            continue;
        const auto& mk = n->style.strokes[(std::size_t)r.stroke].marks;
        if (r.index < 0 || r.index >= (int)mk.size()) continue;
        markClipboard_.push_back(mk[(std::size_t)r.index]);
    }
    if (!markClipboard_.empty())
        LogInfoAction("Copy Line Marks");
}

// Ctrl+V arms a paste: the clipboard marks show as a translucent preview under
// the cursor; a click drops them on the nearest stroke, RMB/Esc cancels.
void Application::Action_PasteMarks() {
    if (!MarkModeActive() || markClipboard_.empty()) return;
    markPasteActive_ = true;
}

} // namespace App

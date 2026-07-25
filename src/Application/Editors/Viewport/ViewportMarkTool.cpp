#include "Application.h"

#include "ViewportMath.h"
#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <functional>
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
    // Rectangle length 200 %; a Gap opens 200 % of the stroke width by default.
    if (shape == Ink::MarkShape::Rectangle) { o.size = 200.0; o.width = 100.0; }
    else if (shape == Ink::MarkShape::Gap)  { o.size = 200.0; }
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
    const Ink::Color blue = MkCol(Tok::C_EditHandle_Free, 1.0f);  // Neutral dot
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
    } else {                    // Neutral: plain ringed dot in blue
        ov.AddCircleFilled(sp, 3.5f, blue);
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

// ── Live mark preview (temporary style) ──────────────────────────────────────
// While a Subtract/Gap mark is placed or moved, its ghost applies a TEMPORARY
// style to the host node so the REAL pipeline renders the effect: the stroke
// below turns semi-transparent (partial dst-out erase / a dimmed copy over the
// future gap span) instead of a painted approximation. Strictly frame-scoped:
// Update() calls ClearMarkPreviewStyle right after the Ink frame is recorded,
// so undo, hit-testing and saves only ever see the true document.

void Application::ApplyMarkPreviewStyle(Ink::NodeId node,
                                        const Ink::Style& preview) {
    if (!project_.document || node == Ink::kNullNode) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(node);
    if (!n) return;
    bool seen = false;
    for (const auto& p : markPreviewSaved_) seen = seen || p.first == node;
    if (!seen) markPreviewSaved_.push_back({ node, n->style });
    doc.SetStyle(node, preview);
}

void Application::ClearMarkPreviewStyle() {
    if (markPreviewSaved_.empty()) return;
    if (project_.document)
        for (auto it = markPreviewSaved_.rbegin();
             it != markPreviewSaved_.rend(); ++it)
            if (project_.document->Find(it->first))
                project_.document->SetStyle(it->first, it->second);
    markPreviewSaved_.clear();
}

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
    // A floating overlay (tool palette, the N side panel) owns the mouse there —
    // the mark tool must not preview/place/pick under it, so treat it as NOT
    // hovered. (These are last frame's rects, stable corner/edge anchors.)
    for (const ImVec4& r : st.overlayRects)
        if (mp.x >= r.x && mp.x <= r.z && mp.y >= r.y && mp.y <= r.w) {
            hovered = false;
            break;
        }
    // Any open ImGui popup (a colour picker, dropdown, combo…) can OVERFLOW onto
    // the canvas: a click on it must not deselect the mark being edited, so the
    // tool is NOT hovered while a popup is up.
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel))
        hovered = false;
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

    // A faint ghost of a mark's CURRENT (pre-move) position, so a drag shows
    // both where it WAS and where it WILL be. The real mark keeps its stored
    // `t` until commit, so markWorld gives the origin position.
    auto drawOldPos = [&](const EditContext::MarkRef& r) {
        Ink::DVec2 p, tn;
        if (!markWorld(r, p, tn)) return;
        const Ink::Vec2 c = d2v(p);
        const Ink::Color faint = MkCol(Tok::S_Color_Text_Subtle, 0.5f);
        for (int i = 0; i < 8; ++i) {   // dashed ring
            const float a0 = (float)i / 8.0f * 6.2831853f;
            const float a1 = a0 + 0.5f * 6.2831853f / 8.0f;
            ov.AddLine({ c.x + std::cos(a0) * 6.0f, c.y + std::sin(a0) * 6.0f },
                       { c.x + std::cos(a1) * 6.0f, c.y + std::sin(a1) * 6.0f },
                       faint, 1.0f);
        }
        ov.AddCircleFilled(c, 1.5f, faint);
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
        // A ghost drawn EARLIER THIS FRAME may have live-applied a preview
        // style; restore the true document first, or the snapshot (and the
        // commit built on it) would capture the preview — and the end-of-frame
        // restore would then silently UNDO the committed edit.
        ClearMarkPreviewStyle();
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
    // Preview of the mark's objects at position `m.t`. ADD (Fusion/Blend)
    // objects draw as a translucent filled shape; SUBTRACT objects draw a
    // dashed construction OUTLINE while the erase itself renders LIVE through
    // a temporary style (partial dst-out — the stroke below dims for real);
    // GAP objects dim the stroke over the exact future span the same live way.
    // `replaceIndex` (≥ 0) is the real mark this ghost REPLACES in the live
    // style (a move); −1 appends it (a placement / paste).
    auto drawGhost = [&](Ink::NodeId nodeId, int strokeIdx,
                         const Ink::StrokeMark& m, const WorldPoly& worldPoly,
                         int replaceIndex = -1) {
        const Ink::Node* n = doc.Find(nodeId);
        if (!n || strokeIdx < 0 || strokeIdx >= (int)n->style.strokes.size())
            return;
        // COPY (not a reference): the live preview below replaces the node's
        // style vectors — a reference into them would dangle.
        const Ink::Stroke sk = n->style.strokes[(std::size_t)strokeIdx];
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

        // Arc length of the placement spine (gap-end sub-object positions).
        double spineTot = 0.0;
        {
            const auto& pts = spine.points;
            const std::size_t nn = pts.size();
            const std::size_t sc = spine.closed ? nn : (nn ? nn - 1 : 0);
            for (std::size_t i = 0; i < sc; ++i)
                spineTot += std::hypot(pts[(i + 1) % nn].x - pts[i].x,
                                       pts[(i + 1) % nn].y - pts[i].y);
        }

        // ── LIVE pipeline preview (re-phasing + erase/gap) ───────────────────
        // Apply a temporary style so the REAL pipeline shows, live at the
        // preview position: the DASH RE-PHASING (any non-Neutral mark, whatever
        // its objects — dash/gap re-phase follows the ghost), a Subtract
        // object's PARTIAL erase, and a GAP swapped for a PARTIAL-ERASE BAND
        // over exactly the opening (its Subtract sub-objects erase at the gap
        // ends too). Fusion/Blend objects are ghosted separately, so they are
        // stripped here (a pipeline copy would double them). Nothing temporary
        // ever shows in the Stroke editor. Restored right after this frame
        // records (Update()) and before any commit (snapshotStyles).
        // Also live for a REPEAT ANCHOR (Object/Between): the temp mark at the
        // preview position re-phases the stroke's repeat runs, so moving it
        // shifts the repeats live — exactly like the dash/gap re-phasing.
        bool wantsLive = m.phase != Ink::MarkPhase::Neutral ||
                         m.repeatAnchor != Ink::MarkRepeatAnchor::None;
        for (const Ink::MarkObject& o : m.objects)
            wantsLive = wantsLive || o.shape == Ink::MarkShape::Gap ||
                        o.mode == Ink::MarkObjectMode::Subtract;
        if (wantsLive) {
            Ink::Style ps = n->style;
            Ink::Stroke& host = ps.strokes[(std::size_t)strokeIdx];
            Ink::StrokeMark pm = m;            // keeps position + phase
            std::vector<Ink::MarkObject> keep;
            std::vector<Ink::StrokeMark>  extra;   // gap-end erase sub-objects
            for (const Ink::MarkObject& o : m.objects) {
                if (o.shape == Ink::MarkShape::Gap) {
                    // The Gap's SIZE is the full opening length; a Rectangle's
                    // `size` is a HALF-extent — so halve it for the band.
                    Ink::MarkObject band;
                    band.shape = Ink::MarkShape::Rectangle;
                    band.mode  = Ink::MarkObjectMode::Subtract;
                    band.bend  = Ink::MarkBend::Follow;   // hug the curve
                    band.sizePercent = o.sizePercent;
                    band.size  = o.size * 0.5;
                    band.width = o.sizePercent ? 60.0 : sk.width * 0.6;
                    band.opacity = 0.45f;
                    keep.push_back(band);
                    // Subtract sub-objects at the gap ends erase live too.
                    if (spineTot > 1e-9) {
                        const double half =
                            std::max(1e-4, o.SizeUnits(sk.width)) * 0.5;
                        const double tc =
                            std::clamp(m.t, 0.0, 1.0) * spineTot;
                        auto addEnds =
                            [&](double endArc,
                                const std::vector<Ink::MarkObject>& objs) {
                            std::vector<Ink::MarkObject> subs;
                            for (const Ink::MarkObject& so : objs)
                                if (so.shape != Ink::MarkShape::Gap &&
                                    so.mode == Ink::MarkObjectMode::Subtract) {
                                    Ink::MarkObject c = so; c.opacity = 0.45f;
                                    subs.push_back(c);
                                }
                            if (!subs.empty()) {
                                Ink::StrokeMark vm = m;
                                vm.phase = Ink::MarkPhase::Neutral;
                                vm.objects = subs;
                                vm.t = std::clamp(endArc / spineTot, 0.0, 1.0);
                                extra.push_back(vm);
                            }
                        };
                        addEnds(tc - half, o.gapStartObjects);
                        addEnds(tc + half, o.gapEndObjects);
                    }
                } else if (o.mode == Ink::MarkObjectMode::Subtract) {
                    Ink::MarkObject c = o; c.opacity = 0.45f;
                    keep.push_back(c);
                }
                // Fusion / Blend objects: ghosted separately (stripped here).
            }
            pm.objects = std::move(keep);
            if (replaceIndex >= 0 && replaceIndex < (int)host.marks.size())
                host.marks[(std::size_t)replaceIndex] = pm;
            else
                host.marks.push_back(pm);
            for (const Ink::StrokeMark& e : extra) host.marks.push_back(e);
            ApplyMarkPreviewStyle(nodeId, ps);
        }
        // An OBJECTLESS mark has no shape to preview — draw a construction
        // glyph at its future position matching the handle CONVENTION: the
        // phase decides both COLOUR and SHAPE (Neutral = blue circle, Dash =
        // violet diamond, Gap = green square), traced dashed.
        if (m.objects.empty()) {
            const double total2 = PolyTotal(worldPoly);
            if (total2 > 1e-9) {
                Ink::DVec2 bp, bt;
                PointAtT(worldPoly, m.t, bp, bt);
                const Ink::DVec2 nrm{ -bt.y, bt.x };
                double off2 = 0.0;
                if (m.side == Ink::MarkSide::Left)
                    off2 =  m.OffsetUnits(sk.width) * wsc;
                if (m.side == Ink::MarkSide::Right)
                    off2 = -m.OffsetUnits(sk.width) * wsc;
                const Ink::Vec2 c2 =
                    d2v({ bp.x + nrm.x * off2, bp.y + nrm.y * off2 });
                const bool isDash = m.phase == Ink::MarkPhase::Dash;
                const bool isGap  = m.phase == Ink::MarkPhase::Gap;
                const Ink::Color cc2 =
                    isDash ? MkCol(Tok::C_EditHandle_Vector, 0.95f)
                  : isGap  ? MkCol(Tok::C_EditHandle_Mirrored, 0.95f)
                           : MkCol(Tok::C_EditHandle_Free, 0.95f);
                // Tangent frame so the glyph aligns to the curve like handles.
                Ink::Vec2 tv = d2v({ bp.x + bt.x, bp.y + bt.y });
                float tx = tv.x - d2v(bp).x, ty = tv.y - d2v(bp).y;
                const float tl = std::sqrt(tx * tx + ty * ty);
                if (tl < 1e-4f) { tx = 1.0f; ty = 0.0f; }
                else            { tx /= tl; ty /= tl; }
                const float nx2 = -ty, ny2 = tx;
                auto P2 = [&](float a, float b) {
                    return Ink::Vec2{ c2.x + tx * a + nx2 * b,
                                      c2.y + ty * a + ny2 * b };
                };
                auto dashSeg = [&](Ink::Vec2 a, Ink::Vec2 b) {
                    DashLine(ov, a, b, cc2, 1.3f, 3.5f, 2.5f);
                };
                const float rr2 = 7.0f;
                if (isDash) {          // diamond
                    dashSeg(P2(rr2, 0), P2(0, rr2));
                    dashSeg(P2(0, rr2), P2(-rr2, 0));
                    dashSeg(P2(-rr2, 0), P2(0, -rr2));
                    dashSeg(P2(0, -rr2), P2(rr2, 0));
                } else if (isGap) {    // square
                    const float hh2 = rr2 * 0.8f;
                    dashSeg(P2(hh2, hh2), P2(-hh2, hh2));
                    dashSeg(P2(-hh2, hh2), P2(-hh2, -hh2));
                    dashSeg(P2(-hh2, -hh2), P2(hh2, -hh2));
                    dashSeg(P2(hh2, -hh2), P2(hh2, hh2));
                } else {               // circle
                    for (int i2 = 0; i2 < 8; ++i2) {
                        const float a0 = (float)i2 / 8.0f * 6.2831853f;
                        const float a1 = a0 + 0.5f * 6.2831853f / 8.0f;
                        ov.AddLine({ c2.x + std::cos(a0) * rr2,
                                     c2.y + std::sin(a0) * rr2 },
                                   { c2.x + std::cos(a1) * rr2,
                                     c2.y + std::sin(a1) * rr2 }, cc2, 1.3f);
                    }
                }
                ov.AddCircleFilled(c2, 2.0f, cc2);
            }
        }
        // The side/offset guide: a dashed line from the spine point to the
        // offset mark point (world space).
        if (m.side != Ink::MarkSide::Center) {
            const double total = PolyTotal(worldPoly);
            if (total > 1e-9) {
                Ink::DVec2 bp, bt;
                PointAtArc(worldPoly,
                           (m.t < 0 ? 0 : m.t > 1 ? 1 : m.t) * total, bp, bt);
                const Ink::DVec2 nrm{ -bt.y, bt.x };
                const double off = m.side == Ink::MarkSide::Left
                    ?  m.OffsetUnits(sk.width) : -m.OffsetUnits(sk.width);
                const Ink::DVec2 markW{ bp.x + nrm.x * off * wsc,
                                        bp.y + nrm.y * off * wsc };
                DashLine(ov, d2v(bp), d2v(markW),
                         MkCol(Tok::S_Color_Text_Subtle, 0.6f), 1.0f);
            }
        }
        // Draws ONE object (not a Gap) as a translucent preview of exactly what
        // the Scene will render at mark `mk`. Recursed for gap start/end markers.
        std::function<void(const Ink::StrokeMark&, const Ink::MarkObject&)>
        drawObject = [&](const Ink::StrokeMark& mk, const Ink::MarkObject& o) {
            Ink::Color col{ base.r * alpha, base.g * alpha, base.b * alpha, alpha };
            if (!o.useStrokeColor && o.shape != Ink::MarkShape::Instance)
                col = { o.color.r * alpha, o.color.g * alpha,
                        o.color.b * alpha, alpha };
            if (o.shape == Ink::MarkShape::Instance) {
                // A translucent preview of the target's FULL rendering (fills
                // AND strokes), through the exact geometry the Scene emits:
                // Bend/Follow derive the bent path (MarkBendPath — the same
                // deformation the primitive rings get), Hard places the raw
                // geometry by the rigid frame.
                const Ink::Node* tgt = doc.Find(o.nodeRef);
                if (!tgt || tgt->kind != Ink::NodeKind::Path ||
                    tgt->path.Empty()) {
                    // No geometry: mark the placement with a ring at the frame
                    // origin (world), so a missing Instance is still visible.
                    const Ink::DMat23 fr =
                        Ink::geom::MarkPlaceMatrix(spine, mk, o, sk.width);
                    ov.AddCircle(d2v(w.Apply(fr.Apply({ 0, 0 }))), 6.0f,
                                 MkCol(Tok::S_Color_Accent_Default, 0.7f), 1.5f);
                    return;
                }
                const double k = o.sizePercent ? o.size * 0.01
                                               : std::max(1e-6, o.size);
                Ink::PathData gp;   // node-local, scale baked in
                if (Ink::geom::BendsAlongCurve(o.bend)) {
                    gp = Ink::geom::MarkBendPath(spine, mk, o, sk.width,
                                                 tgt->path, k, localTol);
                } else {
                    const Ink::DMat23 place =
                        Ink::geom::MarkPlaceMatrix(spine, mk, o, sk.width);
                    Ink::DMat23 scaleM; scaleM.m[0] = k; scaleM.m[4] = k;
                    const Ink::DMat23 pk = place.Compose(scaleM);
                    for (const auto& pl :
                         Ink::geom::Flatten(tgt->path, localTol / k)) {
                        if (pl.points.size() < 2) continue;
                        Ink::Subpath sp2;
                        sp2.closed = pl.closed;
                        for (const Ink::DVec2& q : pl.points) {
                            Ink::Anchor an2;
                            an2.pos = pk.Apply(q);
                            sp2.anchors.push_back(an2);
                        }
                        gp.subpaths.push_back(std::move(sp2));
                    }
                }
                if (gp.Empty()) return;
                const auto gpFlat = Ink::geom::Flatten(gp, localTol);
                auto vp2 = [&](const Ink::geom::Mesh& mesh,
                               std::uint32_t idx) {
                    const Ink::DVec2 lp{ mesh.positions[idx * 2],
                                         mesh.positions[idx * 2 + 1] };
                    return d2v(w.Apply(lp));
                };
                // FUSION shows the instance's SHAPE in the STROKE colour (its
                // own colours ignored — matches the Scene); BLEND shows its
                // real render (own colours).
                const bool fuse = o.mode == Ink::MarkObjectMode::Fusion;
                auto emitMesh = [&](const Ink::geom::Mesh& mesh,
                                    const Ink::Color& c) {
                    ov.BeginDedup();
                    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                        ov.AddTriangle(vp2(mesh, mesh.indices[i]),
                                       vp2(mesh, mesh.indices[i + 1]),
                                       vp2(mesh, mesh.indices[i + 2]), c);
                    ov.EndDedup();
                };
                if (fuse) {
                    // Silhouette fill + strokes, all in the stroke colour.
                    const Ink::Color kc = base;
                    const float fa = kc.a * alpha;
                    const Ink::Color tc2{ kc.r * fa, kc.g * fa, kc.b * fa, fa };
                    emitMesh(Ink::geom::TriangulateFill(gpFlat,
                                 Ink::FillRule::NonZero), tc2);
                    for (const Ink::Stroke& ts : tgt->style.strokes) {
                        if (!ts.enabled || ts.width <= 0.0) continue;
                        Ink::Stroke ss2 = ts;
                        ss2.width = ts.width * k;
                        ss2.marks.clear(); ss2.repeats.clear();
                        emitMesh(Ink::geom::TessellateStroke(gpFlat, ss2,
                                     localTol), tc2);
                    }
                    return;
                }
                // Blend: fills then strokes with the target's own colours.
                const Ink::geom::Mesh fm =
                    Ink::geom::TriangulateFill(gpFlat, Ink::FillRule::NonZero);
                for (const Ink::Fill& f : tgt->style.fills) {
                    if (!f.enabled || f.kind != Ink::FillKind::Solid) continue;
                    const Ink::Color fc = f.paint.color;
                    const float fa = fc.a * f.opacity * alpha;
                    emitMesh(fm, { fc.r * fa, fc.g * fa, fc.b * fa, fa });
                }
                for (const Ink::Stroke& ts : tgt->style.strokes) {
                    if (!ts.enabled || ts.width <= 0.0) continue;
                    Ink::Stroke ss2 = ts;
                    ss2.width = ts.width * k;
                    ss2.marks.clear(); ss2.repeats.clear();
                    const Ink::Color kc = ts.paint.color;
                    const float ea2 = kc.a * alpha;
                    emitMesh(Ink::geom::TessellateStroke(gpFlat, ss2, localTol),
                             { kc.r * ea2, kc.g * ea2, kc.b * ea2, ea2 });
                }
                return;
            }
            // The real filled shape (as it renders): Follow curves the outline
            // along the line (derived ring); Hard/Bend place a parametric
            // primitive by an affine transform (Bend adds a shear).
            std::vector<Ink::DVec2> ring;
            if (Ink::geom::BendsAlongCurve(o.bend)) {
                if (!Ink::geom::MarkFollowContour(spine, mk, o, sk.width,
                                                  localTol, ring))
                    return;
            } else {
                const Ink::PathData shape =
                    Ink::geom::MarkPrimitiveShape(o, sk.width);
                if (shape.Empty()) return;
                const Ink::DMat23 place =
                    Ink::geom::MarkPlaceMatrix(spine, mk, o, sk.width);
                for (const auto& pl : Ink::geom::Flatten(shape, localTol))
                    for (const Ink::DVec2& q : pl.points)
                        ring.push_back(place.Apply(q));
            }
            if (ring.size() < 3) return;
            // Triangulate the ring PROPERLY (a centroid fan double-covers a
            // concave Bend/Follow ring and doubles the transparency); draw the
            // resulting triangles in view space.
            Ink::geom::Polyline rp; rp.points = ring; rp.closed = true;
            const Ink::geom::Mesh rm =
                Ink::geom::TriangulateFill({ rp }, Ink::FillRule::NonZero);
            ov.BeginDedup();   // any residual self-overlap blends once
            for (std::size_t i = 0; i + 2 < rm.indices.size(); i += 3) {
                auto vp = [&](std::uint32_t idx) {
                    const Ink::DVec2 lp{ rm.positions[idx*2], rm.positions[idx*2+1] };
                    return d2v(w.Apply(lp));
                };
                ov.AddTriangle(vp(rm.indices[i]), vp(rm.indices[i+1]),
                               vp(rm.indices[i+2]), col);
            }
            ov.EndDedup();
        };

        // A SUBTRACT object previews as a dashed construction OUTLINE — the
        // erase itself already renders live through the temporary style above,
        // so no filled ghost is painted over it. The dash PHASE is continuous
        // around the whole ring (a per-segment dash restart on a densely
        // resampled Follow ring reads as a solid line), and the outline ring
        // uses a bounded tolerance (it is a guide — no need for the render
        // density, which also made the ghost laggy).
        auto drawCutOutline = [&](const Ink::StrokeMark& mk,
                                  const Ink::MarkObject& o) {
            if (o.shape == Ink::MarkShape::Instance) return;   // live erase only
            const double outlineTol = std::max(localTol, 0.02);
            std::vector<Ink::DVec2> ring;
            if (Ink::geom::BendsAlongCurve(o.bend)) {
                if (!Ink::geom::MarkFollowContour(spine, mk, o, sk.width,
                                                  outlineTol, ring))
                    return;
            } else {
                const Ink::PathData shape =
                    Ink::geom::MarkPrimitiveShape(o, sk.width);
                if (shape.Empty()) return;
                const Ink::DMat23 place =
                    Ink::geom::MarkPlaceMatrix(spine, mk, o, sk.width);
                for (const auto& pl : Ink::geom::Flatten(shape, outlineTol))
                    for (const Ink::DVec2& q : pl.points)
                        ring.push_back(place.Apply(q));
            }
            if (ring.size() < 2) return;
            const Ink::Color oc = MkCol(Tok::S_Color_Text_Subtle, 0.9f);
            const float dash = 4.0f, gapPx = 3.0f, period = dash + gapPx;
            float phase = 0.0f;   // runs continuously across ring segments
            Ink::Vec2 prev = d2v(w.Apply(ring[0]));
            for (std::size_t i = 1; i <= ring.size(); ++i) {
                const Ink::Vec2 cur = d2v(w.Apply(ring[i % ring.size()]));
                const float dx = cur.x - prev.x, dy = cur.y - prev.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1e-3f) {
                    const float ux = dx / len, uy = dy / len;
                    float p = 0.0f;
                    while (p < len) {
                        const float cyc = std::fmod(phase + p, period);
                        if (cyc < dash) {           // inside a dash run
                            const float run = std::min(dash - cyc, len - p);
                            ov.AddLine({ prev.x + ux * p, prev.y + uy * p },
                                       { prev.x + ux * (p + run),
                                         prev.y + uy * (p + run) }, oc, 1.2f);
                            p += run;
                        } else {                    // inside a gap run
                            p += std::min(period - cyc, len - p);
                        }
                    }
                    phase += len;
                }
                prev = cur;
            }
        };

        for (const Ink::MarkObject& o : m.objects) {
            if (o.shape == Ink::MarkShape::Gap) {
                // The opening itself previews LIVE (the dimmed companion stroke
                // in the temporary style). Here: only the start/end marker
                // sub-objects, as translucent shapes at the gap ends —
                // matching the Scene's stampEnd (arc of the LOCAL spine).
                double sTot = 0.0;
                {
                    const auto& pts = spine.points;
                    const std::size_t nn = pts.size();
                    const std::size_t sc = spine.closed ? nn : (nn ? nn - 1 : 0);
                    for (std::size_t i = 0; i < sc; ++i)
                        sTot += std::hypot(pts[(i+1)%nn].x - pts[i].x,
                                           pts[(i+1)%nn].y - pts[i].y);
                }
                if (sTot > 1e-9) {
                    const double sHalf =
                        std::max(1e-4, o.SizeUnits(sk.width)) * 0.5;
                    const double stc = std::clamp(m.t, 0.0, 1.0) * sTot;
                    auto stampEnd = [&](double endArc,
                                        const std::vector<Ink::MarkObject>& objs) {
                        Ink::StrokeMark vm = m;
                        vm.t = std::clamp(endArc / sTot, 0.0, 1.0);
                        for (const Ink::MarkObject& so : objs) {
                            if (so.shape == Ink::MarkShape::Gap) continue;
                            // A Subtract sub-object erases live (temp style
                            // above) and previews as a dashed outline, exactly
                            // like a top-level Cut object — never a filled ghost.
                            if (so.mode == Ink::MarkObjectMode::Subtract)
                                drawCutOutline(vm, so);
                            else
                                drawObject(vm, so);
                        }
                    };
                    stampEnd(stc - sHalf, o.gapStartObjects);
                    stampEnd(stc + sHalf, o.gapEndObjects);
                }
                continue;
            }
            if (o.mode == Ink::MarkObjectMode::Subtract) {
                drawCutOutline(m, o);   // live erase + construction outline
                continue;
            }
            drawObject(m, o);
        }
    };

    // Find the nearest stroked line to the cursor → (node, stroke, sub, t).
    auto nearestLine = [&](Ink::NodeId& outId, int& outStroke, int& outSub,
                           double& outT, float maxPx = 14.0f) -> bool {
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
        return found && bestD <= maxPx;
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
        // Paste snaps to the nearest stroke at ANY distance (like move), so the
        // preview always follows the cursor.
        const bool onLine = nearestLine(id, si, sub, t, 1e9f);
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
                ClearMarkPreviewStyle();   // drop this frame's ghost style first
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
                drawOldPos(r);            // where it was
                Ink::StrokeMark ghost = *m;
                ghost.t = previewT(k);
                auto polys = FlattenWorld(doc, r.node, zoom);
                if (ghost.sub < 0 || ghost.sub >= (int)polys.size()) continue;
                drawGhost(r.node, r.stroke, ghost, polys[(std::size_t)ghost.sub],
                          r.index);
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
            drawGhost(r.node, r.stroke, ghost, polys[(std::size_t)ghost.sub],
                      r.index);
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
            drawOldPos(markDrag_.ref);      // where it was
            Ink::StrokeMark ghost = *m;
            ghost.t = markDrag_.dragT;
            drawGhost(markDrag_.ref.node, markDrag_.ref.stroke, ghost, poly,
                      markDrag_.ref.index);
            for (const EditContext::MarkRef& r : edit_.markSel) {
                if (r == markDrag_.ref) continue;
                const Ink::StrokeMark* om = markOf(r);
                if (!om) continue;
                auto opolys = FlattenWorld(doc, r.node, zoom);
                if (om->sub < 0 || om->sub >= (int)opolys.size()) continue;
                drawOldPos(r);
                Ink::StrokeMark og = *om;
                og.t = std::clamp(og.t + deltaT, 0.0, 1.0);
                drawGhost(r.node, r.stroke, og, opolys[(std::size_t)om->sub],
                          r.index);
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
        } else if (!markPlaceEmpty_) {
            Ink::MarkObject obj = DefaultMarkObject(markPlaceShape_);
            obj.mode = markPlaceSubtract_ ? Ink::MarkObjectMode::Subtract
                                          : Ink::MarkObjectMode::Fusion;
            m.objects.push_back(obj);
        }
        // markPlaceEmpty_: a pure OBJECTLESS mark (position / phase / repeat
        // anchor) — the ghost draws its construction glyph.
        auto polys = FlattenWorld(doc, best.id, zoom);
        // Hovered only: the placement ghost now drives a LIVE style preview —
        // it must not fire while the pointer is on UI (palette, popups).
        if (hovered && best.sub >= 0 && best.sub < (int)polys.size())
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

// Cycles the dash re-phasing of the selected marks Neutral -> Dash -> Gap.
void Application::Action_CycleMarkPhase() {
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
        Ink::MarkPhase& ph = mk[(std::size_t)r.index].phase;
        ph = ph == Ink::MarkPhase::Neutral ? Ink::MarkPhase::Dash
           : ph == Ink::MarkPhase::Dash    ? Ink::MarkPhase::Gap
                                           : Ink::MarkPhase::Neutral;
        doc.SetStyle(r.node, sty);
    }
    if (ps.empty()) return;
    for (StylePair& p : ps)
        if (const Ink::Node* n = doc.Find(p.id)) p.after = n->style;
    PushDocCommand("Cycle Mark Phase",
        [ps](Ink::Document& d) { for (const StylePair& p : ps) d.SetStyle(p.id, p.before); },
        [ps](Ink::Document& d) { for (const StylePair& p : ps) d.SetStyle(p.id, p.after); });
    LogInfoAction("Cycle Mark Phase");
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

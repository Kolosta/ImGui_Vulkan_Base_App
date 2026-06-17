#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {

using Renderer::Vec2;

// A freshly created single-part shape with default paints (fill on, stroke off).
// Returns the shape; callers fill in its one Part's geometry. `type` is Mesh by
// default; curve seeds pass Curve plus the spline interpretation.
static Renderer::Shape MakeShape(Renderer::ShapeKind kind,
                                 Renderer::PartType type = Renderer::PartType::Mesh,
                                 Renderer::SplineType spline = Renderer::SplineType::Bezier) {
    Renderer::Shape s;
    Renderer::Part part;
    part.kind = kind;
    part.type = type;
    part.spline = spline;
    part.fill.enabled   = true;
    part.fill.color     = { 0.20f, 0.55f, 0.90f, 1.0f };
    part.stroke.enabled = false;
    part.stroke.color   = { 0.05f, 0.05f, 0.06f, 1.0f };
    part.stroke.width   = 2.0f;
    s.parts.push_back(part);
    return s;
}

// Allocate a junction-group id unique WITHIN a part (max existing + 1). Junction
// ids only need to be unique inside the part that owns the branches, so this keeps
// the multi-path model fully self-contained (no document-wide counter to persist).
static uint32_t AllocJunctionId(const Renderer::Part& part) {
    uint32_t mx = 0;
    for (const Renderer::Node& n : part.path.nodes) mx = std::max(mx, n.junctionId);
    return mx + 1;
}

void Application::ApplyDefaultColors(Renderer::Shape& s) const {
    for (Renderer::Part& p : s.parts) {
        p.fill.color   = defaultFill_;
        p.stroke.color = defaultStroke_;
    }
}

// Set a shape's origin to the geometric centre of its outline (Blender places a
// new object's origin at its centre, not a corner). Called right after building
// a shape's geometry, before it is added to the document.
static void CenterOrigin(Renderer::Shape& s) {
    Vec2 mn, mx;
    if (!Renderer::Tessellator::WorldBounds(s, 1.0f, mn, mx)) return;
    s.origin = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
}

// Pick the artboard whose bounds contain doc-point p (−1 if none). Used to bind
// a new gesture to a page so shapes belong somewhere sensible.
static int ArtboardAt(const Renderer::Document& doc, Vec2 p) {
    for (int i = 0; i < (int)doc.artboards.size(); ++i) {
        const auto& ab = doc.artboards[(size_t)i];
        if (p.x >= ab.pos.x && p.x <= ab.pos.x + ab.size.x &&
            p.y >= ab.pos.y && p.y <= ab.pos.y + ab.size.y)
            return i;
    }
    return doc.artboards.empty() ? -1 : 0;   // fall back to the first page
}

// ─────────────────────────────────────────────────────────────────────────────
//  Follow-curve (Shift) — trace the drawn path EXACTLY along a target curve.
//
//  A FollowCurve is one subpath of a target part, baked into WORLD-space Bézier
//  nodes (positions + absolute in/out handles). It supports:
//    • projecting a cursor point → nearest point on the curve (its arc-length),
//    • extracting the exact Bézier nodes between two arc-lengths (De Casteljau
//      split at the ends), so the drawn segment copies the target geometry 1:1.
//  OpenOrienteering Mapper's "follow path" behaves the same way.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct WNode { Vec2 pos, hIn, hOut; bool hasIn, hasOut; };

// Evaluate the cubic of node-segment [a,b] at parameter u (control points from the
// nodes' handles; a straight segment when a handle is absent).
static Vec2 CubicAt(const WNode& a, const WNode& b, float u) {
    Vec2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
    Vec2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
    float v = 1.0f - u;
    float b0 = v*v*v, b1 = 3*v*v*u, b2 = 3*v*u*u, b3 = u*u*u;
    return { b0*p0.x + b1*p1.x + b2*p2.x + b3*p3.x,
             b0*p0.y + b1*p1.y + b2*p2.y + b3*p3.y };
}

struct FollowCurve {
    std::vector<WNode> n;        // world Bézier nodes of the subpath
    bool               closed = false;
    bool valid() const { return n.size() >= 2; }
    int  segCount() const { return closed ? (int)n.size() : (int)n.size() - 1; }
    void seg(int k, WNode& a, WNode& b) const { a = n[(size_t)k]; b = n[(size_t)((k+1) % n.size())]; }

    // Build the world nodes of subpath `sub` of `part` on `shape` (page origin po).
    static FollowCurve Build(const Renderer::Shape& s, const Renderer::Part& partIn,
                             int sub, Vec2 po) {
        FollowCurve fc;
        Renderer::Part part = partIn; part.EnsurePath();   // bake parametric kinds
        int b = 0, e = (int)part.path.nodes.size();
        part.path.subRange(sub, b, e);
        fc.closed = part.path.closed;
        for (int i = b; i < e; ++i) {
            const Renderer::Node& nd = part.path.nodes[(size_t)i];
            WNode w;
            w.pos   = Renderer::Tessellator::WorldTransform(s, nd.pos, po);
            w.hasIn = nd.hasIn; w.hasOut = nd.hasOut;
            w.hIn   = Renderer::Tessellator::WorldTransform(s, nd.hasIn ? nd.hIn : nd.pos, po);
            w.hOut  = Renderer::Tessellator::WorldTransform(s, nd.hasOut ? nd.hOut : nd.pos, po);
            fc.n.push_back(w);
        }
        return fc;
    }

    // Project `p` onto the curve: returns the squared distance, and writes the
    // segment index + parameter + the closest point. Coarse flatten + refine.
    float Project(Vec2 p, int& segOut, float& uOut, Vec2& closest) const {
        const int FL = 16;
        float best = 1e30f; segOut = 0; uOut = 0; closest = n[0].pos;
        WNode a, b;
        for (int k = 0; k < segCount(); ++k) {
            seg(k, a, b);
            Vec2 prev = a.pos; float pu = 0.0f;
            for (int i = 1; i <= FL; ++i) {
                float u = (float)i / (float)FL;
                Vec2 cur = CubicAt(a, b, u);
                // closest point on the flat sub-segment [prev,cur]
                Vec2 ab{ cur.x - prev.x, cur.y - prev.y };
                float L2 = ab.x*ab.x + ab.y*ab.y;
                float t = L2 > 1e-9f ? std::clamp(((p.x-prev.x)*ab.x + (p.y-prev.y)*ab.y)/L2, 0.0f, 1.0f) : 0.0f;
                Vec2 c{ prev.x + ab.x*t, prev.y + ab.y*t };
                float d = (p.x-c.x)*(p.x-c.x) + (p.y-c.y)*(p.y-c.y);
                if (d < best) { best = d; segOut = k; uOut = pu + (u - pu) * t; closest = c; }
                prev = cur; pu = u;
            }
        }
        return best;
    }

    // Arc length (world) of a (segment, u) location, measured from the curve start.
    float ArcOf(int seg, float u) const {
        float acc = 0.0f; WNode a, b;
        for (int k = 0; k < seg; ++k) { this->seg(k, a, b); acc += SegLen(a, b, 1.0f); }
        this->seg(seg, a, b);
        return acc + SegLen(a, b, u);
    }
    // Project `p` and return its arc-length directly (+ the closest point).
    float ProjectArc(Vec2 p, Vec2& closest) const {
        int seg; float u; float d2 = Project(p, seg, u, closest); (void)d2;
        return ArcOf(seg, u);
    }
    float TotalArc() const {
        float acc = 0.0f; WNode a, b;
        for (int k = 0; k < segCount(); ++k) { seg(k, a, b); acc += SegLen(a, b, 1.0f); }
        return acc;
    }
    static float SegLen(const WNode& a, const WNode& b, float uEnd) {
        const int FL = 24; float acc = 0.0f; Vec2 prev = a.pos;
        for (int i = 1; i <= FL; ++i) {
            float u = uEnd * (float)i / (float)FL;
            Vec2 cur = CubicAt(a, b, u);
            acc += std::hypot(cur.x - prev.x, cur.y - prev.y); prev = cur;
        }
        return acc;
    }
    // Convert an arc-length back to (segment, u). Clamped to [0, TotalArc].
    void LocOfArc(float arc, int& segOut, float& uOut) const {
        float acc = 0.0f; WNode a, b;
        for (int k = 0; k < segCount(); ++k) {
            seg(k, a, b); float L = SegLen(a, b, 1.0f);
            if (arc <= acc + L || k == segCount() - 1) {
                segOut = k;
                // bisect u for the residual arc within this segment
                float target = std::clamp(arc - acc, 0.0f, L);
                float lo = 0.0f, hi = 1.0f;
                for (int it = 0; it < 18; ++it) {
                    float mid = 0.5f*(lo+hi);
                    (SegLen(a, b, mid) < target) ? lo = mid : hi = mid;
                }
                uOut = 0.5f*(lo+hi); return;
            }
            acc += L;
        }
        segOut = segCount() - 1; uOut = 1.0f;
    }

    // De Casteljau split of segment [a,b] at u → the LEFT sub-cubic's control points
    // (P0,P1,P2,P3). Used to cut a sub-curve cleanly at an interior parameter.
    static void SplitLeft(const WNode& a, const WNode& b, float u,
                          Vec2& P0, Vec2& P1, Vec2& P2, Vec2& P3) {
        Vec2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
        Vec2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
        auto L = [&](Vec2 A, Vec2 B){ return Vec2{ A.x+(B.x-A.x)*u, A.y+(B.y-A.y)*u }; };
        Vec2 q0 = L(p0,p1), q1 = L(p1,p2), q2 = L(p2,p3);
        Vec2 r0 = L(q0,q1), r1 = L(q1,q2);
        Vec2 s0 = L(r0,r1);
        P0 = p0; P1 = q0; P2 = r0; P3 = s0;
    }
    // The RIGHT sub-cubic of [a,b] from u to 1 (control points).
    static void SplitRight(const WNode& a, const WNode& b, float u,
                           Vec2& P0, Vec2& P1, Vec2& P2, Vec2& P3) {
        Vec2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
        Vec2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
        auto Lr = [&](Vec2 A, Vec2 B){ return Vec2{ A.x+(B.x-A.x)*u, A.y+(B.y-A.y)*u }; };
        Vec2 q0 = Lr(p0,p1), q1 = Lr(p1,p2), q2 = Lr(p2,p3);
        Vec2 r0 = Lr(q0,q1), r1 = Lr(q1,q2);
        Vec2 s0 = Lr(r0,r1);
        P0 = s0; P1 = r1; P2 = q2; P3 = p3;
    }
};

}  // namespace

// Add a shape authored in WORLD doc-units to artboard `abIndex`, converting it
// to PAGE-RELATIVE storage (Lot 2). The geometry/origin stay as authored; the
// page offset is absorbed into transform.translate so the shape stays visually
// put: world = ab.pos + (translate − ab.pos) + … = the authored world coords.
static uint64_t AddShapeWorld(Renderer::Document& doc, int abIndex, Renderer::Shape s) {
    if (abIndex >= 0 && abIndex < (int)doc.artboards.size()) {
        const Vec2 po = doc.artboards[(size_t)abIndex].pos;
        s.transform.translate.x -= po.x;
        s.transform.translate.y -= po.y;
    }
    return doc.AddShape(abIndex, std::move(s));
}

// Add a shape whose transform.translate is ALREADY expressed in the target's
// page-relative (display) frame — used by Shift+A, where translate was set to
// (cursor − pageDisplayOrigin − origin). abIndex >= 0 → onto that page (stored
// as-is); abIndex < 0 → a page-less LOOSE object (raw document space). Returns
// the new id and makes it the sole selection.
static uint64_t AddShapeWorldDisplay(Renderer::Document& doc, int abIndex, Renderer::Shape s) {
    if (abIndex >= 0 && abIndex < (int)doc.artboards.size())
        return doc.AddShape(abIndex, std::move(s));
    // Loose object: assign an id, push to looseShapes, select it.
    s.id = doc.AllocId();
    uint64_t id = s.id;
    doc.looseShapes.push_back(std::move(s));
    doc.SelectOnly(id);
    return id;
}

// Test whether world-point p hits a polyline (inside if closed, else near it).
static bool HitPoly(const std::vector<Vec2>& poly, bool closed, Vec2 p, float tol) {
    if (poly.size() < 2) return false;
    if (closed) {
        bool in = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            bool cond = ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
                (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                           (poly[j].y - poly[i].y) + poly[i].x);
            if (cond) in = !in;
        }
        if (in) return true;
    }
    size_t segs = closed ? poly.size() : poly.size() - 1;
    for (size_t i = 0; i < segs; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        Vec2 ab{ b.x - a.x, b.y - a.y };
        float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = len2 > 1e-6f
            ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f)
            : 0.0f;
        Vec2 c{ a.x + ab.x * t, a.y + ab.y * t };
        if (std::hypot(p.x - c.x, p.y - c.y) <= tol) return true;
    }
    return false;
}

// Hit-test every shape (topmost first) for the Select tool. A shape is hit if
// ANY of its parts is hit. Returns the shape id or 0. `pageOriginOf(abIndex)`
// gives the page's DISPLAY origin (per-viewport layout), so picking matches what
// is shown; `pageVisibleAt(abIndex)` skips pages not shown in this viewport (a
// hidden page is not pickable). Pass them the viewport's CurPageOrigin/Visible.
static uint64_t PickShape(Renderer::Document& doc, Vec2 p, float zoom,
                          const std::function<Vec2(int)>& pageOriginOf,
                          const std::function<bool(int)>& pageVisibleAt) {
    // Hit-test a shape against EVERY subpath (strand) of EVERY part, so a branched
    // object is pickable on any strand (not just the first).
    auto hitShape = [&](const Renderer::Shape& s, Vec2 po) -> bool {
        for (const Renderer::Part& part : s.parts) {
            float tol = std::max(part.stroke.enabled ? part.stroke.width : 0.0f, 6.0f / zoom);
            const int subs = Renderer::Tessellator::SubpathCount(part);
            for (int sp = 0; sp < subs; ++sp) {
                bool closed = false;
                std::vector<Vec2> poly =
                    Renderer::Tessellator::OutlinePartSub(s, part, sp, zoom, closed, po);
                if (HitPoly(poly, closed, p, tol)) return true;
            }
        }
        return false;
    };
    // Loose (page-less) objects first — they sit on top in raw document space.
    for (auto sit = doc.looseShapes.rbegin(); sit != doc.looseShapes.rend(); ++sit) {
        const Renderer::Shape& s = *sit;
        if (s.visible && hitShape(s, Vec2{0, 0})) return s.id;
    }
    for (int i = (int)doc.artboards.size() - 1; i >= 0; --i) {
        if (!pageVisibleAt(i)) continue;       // hidden page → nothing pickable
        auto& ab = doc.artboards[(size_t)i];
        const Vec2 po = pageOriginOf(i);   // display origin
        for (auto sit = ab.shapes.rbegin(); sit != ab.shapes.rend(); ++sit) {
            const Renderer::Shape& s = *sit;
            if (s.visible && hitShape(s, po)) return s.id;
        }
    }
    return 0;
}

void Application::HandleViewportTools(
    EditorState& st,
    const std::function<Vec2(ImVec2)>& s2d,
    const std::function<ImVec2(Vec2)>& d2s,
    float effZoom, bool hovered, ImDrawList* dl) {

    auto& ds   = DesignSystem::DesignSystem::Instance();
    auto& tm   = Shortcuts::Tools::ToolManager::Instance();
    const std::string tool = tm.GetActiveTool();
    ImGuiIO& io = ImGui::GetIO();
    Vec2 m = s2d(io.MousePos);
    const float zoom = std::max(0.0001f, effZoom);  // px per RAW doc-unit
    const void* self = &st;                          // this leaf's identity
    auto& doc = project_.document;

    // A modal G/R/S transform is driven directly by RenderViewport (top
    // priority), so HandleViewportTools is not called while one is active.

    // Hand tool + camera gestures are handled in RenderViewport; here we only
    // drive content tools. The Hand tool draws nothing.
    if (tool == "tool.hand") return;

    // ── Gesture ownership ────────────────────────────────────────────────────
    // With several Viewport zones open, this runs once per leaf. A gesture is
    // owned by the leaf that started it; OTHER leaves must not advance it (that
    // caused the "moves in both viewports / wrong mapping" bug). A leaf may only
    // start a new gesture, or drive the one it already owns.
    const bool owns   = toolState_.Active() && toolState_.owner == self;
    const bool foreign = toolState_.Active() && toolState_.owner != self;
    if (foreign) return;   // another zone is mid-gesture — stay out of its way

    // Overlay colours (chrome → design-system tokens, not document colours).
    ImU32 cAccent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    ImU32 cHandle = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorRing));
    ImU32 cHandleEdge = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_OriginOutline));

    const bool lpressed  = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool lreleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool ldouble   = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool enter     = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    const bool escape    = ImGui::IsKeyPressed(ImGuiKey_Escape);
    // Right-click PRESS cancels an active gesture (handled centrally in
    // RenderViewport so it also gates the context menu); Escape does too.
    if (escape && owns) { CancelViewportGesture(); return; }
    (void)owns;

    // ── Select tool (Object Mode): click-pick, grab-move, box-select ─────────
    // Edit-mode selection/editing is handled in Lot E; until then the select
    // tool behaves the same (object picking) so the build stays functional.
    if (tool == "tool.select") {
        // Start a gesture only if this leaf is hovered and none is active.
        if (lpressed && !toolState_.Active()) {
            uint64_t hit = PickShape(doc, m, zoom,
                [this](int ab){ return CurPageOrigin(ab); },
                [this](int ab){ return CurPageVisible(ab); });
            if (hit) {
                // Did the selection/active actually change? Re-clicking the SAME sole
                // active object is a no-op → don't log or label a redundant "Select".
                const std::vector<uint64_t> prevSel = doc.Selection();
                const uint64_t prevActive = doc.ActiveId();
                if (io.KeyShift)              doc.SelectToggle(hit);
                else if (!doc.IsSelected(hit)) doc.SelectOnly(hit);
                else                           doc.SetActive(hit);
                doc.SyncActivePageToSelection();   // active page = selected obj's page
                const bool changed = (doc.Selection() != prevSel) ||
                                     (doc.ActiveId() != prevActive);
                if (changed) {
                    // Selection is in the undo snapshot → label the step. (A drag
                    // overrides this with "Move" before it commits.)
                    MarkUndoLabel(io.KeyShift ? "Extend Selection" : "Select");
                    Renderer::Shape* sh = doc.FindShape(hit);
                    const std::string nm = sh ? (sh->name.empty() ? "Object" : sh->name) : "";
                    char d[160];
                    std::snprintf(d, sizeof d, "object=%s  id=%llu  selected=%d",
                                  nm.c_str(), (unsigned long long)hit,
                                  (int)doc.Selection().size());
                    LogInfoActionRich(io.KeyShift ? "Extend Selection" : "Select Object", d);
                }
                // Arm a move of the whole selection (commits past threshold).
                // dragStart = the VIRTUAL mouse at press (doc-units); moveAccum
                // accumulates warp compensation so the displacement is computed
                // from a continuous virtual mouse (no jump across edge wraps).
                toolState_.Reset();
                toolState_.gesture = ToolGesture::MoveObjects;
                toolState_.owner   = self;
                toolState_.dragStart = m;          // virtual anchor at press
                toolState_.moveAccum = m;          // virtual mouse, integrated below
                BeginGestureMouseTracking();        // seed real-motion reference
                // Move only VISIBLE selected objects; a hidden object stays put
                // even if selected/active (Blender-style).
                toolState_.moveIds.clear();
                toolState_.moveOrigTranslate.clear();
                for (uint64_t id : doc.Selection())
                    if (Renderer::Shape* s = doc.FindShape(id); s && s->visible) {
                        toolState_.moveIds.push_back(id);
                        toolState_.moveOrigTranslate.push_back(s->transform.translate);
                    }
            } else {
                // Press on empty canvas → box-select (Shift = add).
                toolState_.Reset();
                toolState_.gesture = ToolGesture::BoxSelect;
                toolState_.owner   = self;
                toolState_.dragStart = toolState_.dragNow = m;
                toolState_.boxAdditive = io.KeyShift;
                // Clicking empty canvas deselects all but KEEPS the active object
                // active (Blender-style: its origin stays visible). A box-select
                // that ends up selecting nothing therefore leaves the active put.
                if (!io.KeyShift) doc.DeselectAll();
                // Active page: clicking ON a page's white sets it active; clicking
                // outside any page clears the active page (→ Shift+A spawns loose).
                int pageHit = -1;
                for (int k = (int)doc.artboards.size() - 1; k >= 0; --k) {
                    if (!CurPageVisible(k)) continue;
                    Renderer::Vec2 po = CurPageOrigin(k);
                    const auto& ab = doc.artboards[(size_t)k];
                    if (m.x >= po.x && m.x <= po.x + ab.size.x &&
                        m.y >= po.y && m.y <= po.y + ab.size.y) { pageHit = k; break; }
                }
                if (pageHit >= 0) doc.SetActivePage(doc.artboards[(size_t)pageHit].id);
                else              doc.ClearActivePage();
            }
        }

        // ── Drive an armed move (owner only) ────────────────────────────────
        if (toolState_.gesture == ToolGesture::MoveObjects && toolState_.owner == self) {
            // moveAccum is the VIRTUAL mouse (doc-units), integrated from the
            // REAL pointer motion (GestureMouseDelta excludes warp jumps but
            // keeps fast motion) → continuous + drift-free across edge wraps.
            // Eased by the global Shift precision-drag factor (finer move).
            ImVec2 d = GestureMouseDelta();
            const float pf = PrecisionDragFactor();
            toolState_.moveAccum.x += d.x * pf / zoom;
            toolState_.moveAccum.y += d.y * pf / zoom;
            Vec2 disp{ toolState_.moveAccum.x - toolState_.dragStart.x,
                       toolState_.moveAccum.y - toolState_.dragStart.y };
            if (std::hypot(disp.x, disp.y) * zoom > 3.0f)
                toolState_.movedPastThreshold = true;
            if (toolState_.movedPastThreshold) {
                for (size_t i = 0; i < toolState_.moveIds.size(); ++i)
                    if (Renderer::Shape* s = doc.FindShape(toolState_.moveIds[i]))
                        s->transform.translate = {
                            toolState_.moveOrigTranslate[i].x + disp.x,
                            toolState_.moveOrigTranslate[i].y + disp.y };
                // Move cursor + wrap at THIS zone's edges (infinite drag); the
                // wrap drops the next delta frame so the object never teleports.
                ShowMoveCursor();
                WrapMouseInRect(gestureCanvasMin_, gestureCanvasMax_);
            }
            if (lreleased) {
                if (toolState_.movedPastThreshold) {
                    MaybeTransferMovedObjects(toolState_.moveIds);  // page transfer
                    project_.dirty = true;
                    // Log the click-drag move (the G op logs itself; this is the
                    // Select-tool drag path, which previously logged nothing).
                    Vec2 fdisp{ toolState_.moveAccum.x - toolState_.dragStart.x,
                                toolState_.moveAccum.y - toolState_.dragStart.y };
                    char dd[128];
                    std::snprintf(dd, sizeof dd, "value=(%.4g, %.4g)  objects=%d",
                                  fdisp.x, fdisp.y, (int)toolState_.moveIds.size());
                    MarkUndoLabel("Move");
                    LogInfoActionRich("Move", dd);
                }
                toolState_.Reset();
            }
            return;
        }

        // ── Drive a box-select (owner only) ─────────────────────────────────
        if (toolState_.gesture == ToolGesture::BoxSelect && toolState_.owner == self) {
            toolState_.dragNow = m;
            ImVec2 a = d2s(toolState_.dragStart), b = d2s(toolState_.dragNow);
            dl->AddRectFilled(a, b, (cAccent & 0x00FFFFFF) | 0x22000000);
            dl->AddRect(a, b, cAccent, 0.0f, 0, 1.0f);
            if (lreleased) {
                float x0 = std::min(toolState_.dragStart.x, toolState_.dragNow.x);
                float y0 = std::min(toolState_.dragStart.y, toolState_.dragNow.y);
                float x1 = std::max(toolState_.dragStart.x, toolState_.dragNow.x);
                float y1 = std::max(toolState_.dragStart.y, toolState_.dragNow.y);
                for (int ai = 0; ai < (int)doc.artboards.size(); ++ai) {
                    if (!CurPageVisible(ai)) continue;   // hidden page → not box-selectable
                    for (const Renderer::Shape& s : doc.artboards[(size_t)ai].shapes) {
                        if (!s.visible) continue;
                        bool cl = false;
                        std::vector<Vec2> poly =
                            Renderer::Tessellator::Outline(s, zoom, cl, CurPageOrigin(ai));
                        bool inside = false;
                        for (const Vec2& p : poly)
                            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) { inside = true; break; }
                        if (inside) doc.SelectAdd(s.id);
                    }
                }
                toolState_.Reset();
            }
            return;
        }
        return;
    }

    // ── 2D Cursor tool: a plain click or drag places the 2D cursor (Lot 5).
    // (Shift+RMB still moves it under any tool — handled in RenderViewport.)
    if (tool == "tool.cursor") {
        auto& doc = project_.document;
        // R (Action_BeginTransform) armed a cursor rotation: turn doc.cursorRotation
        // by the change in the cursor→mouse angle. LMB/Enter confirm, Esc/RMB cancel.
        if (cursorRotate_.Active()) {
            if (cursorRotate_.owner == nullptr && hovered) cursorRotate_.owner = self;
            if (cursorRotate_.owner == self) {
                ImVec2 cs = d2s(doc.cursor);
                float ang = std::atan2(io.MousePos.y - cs.y, io.MousePos.x - cs.x);
                if (!cursorRotate_.seeded) {
                    cursorRotate_.startAngle = ang; cursorRotate_.seeded = true;
                }
                float delta = (ang - cursorRotate_.startAngle) * PrecisionDragFactor();
                // Ctrl → 5° snapping, mirroring the object rotate.
                float newRot = cursorRotate_.startRot + delta;
                if (io.KeyCtrl) {
                    const float inc = 5.0f * 3.14159265358979f / 180.0f;
                    newRot = std::round(newRot / inc) * inc;
                }
                doc.cursorRotation = newRot;
                // Guide line cursor→mouse + the cursor's current X axis.
                ImU32 acc = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
                dl->AddLine(cs, io.MousePos, acc, 1.4f);
                ShowOrientedCursor("move-up-down-cur", ang);
                bool confirm = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                               ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                               ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
                bool cancel  = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                               ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                if (cancel) { doc.cursorRotation = cursorRotate_.startRot;
                              cursorRotate_.Reset();
                              rmbConsumedByTransform_ = true; }
                else if (confirm) {
                    MarkUndoLabel("Rotate 2D Cursor");
                    char d[96];
                    std::snprintf(d, sizeof d, "angle=%.3g\xC2\xB0",
                                  doc.cursorRotation * 180.0f / 3.14159265358979f);
                    LogInfoActionRich("Rotate 2D Cursor", d);
                    project_.dirty = true; cursorRotate_.Reset();
                }
            }
            return;
        }
        if (hovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))) {
            doc.cursor = m;
        }
        return;
    }
    // No other tools exist: object creation is done through the Shift+A menu.
}

// Drive the modal G/R/S transform: preview + apply on the owning leaf, confirm
// on LMB/Enter, cancel (revert) on Esc/RMB. Pivot per pivotMode_ (Individual
// Origins pivots each shape around its own origin).
void Application::UpdateTransformOp(
    EditorState& st,
    const std::function<Renderer::Vec2(ImVec2)>& s2d,
    const std::function<ImVec2(Renderer::Vec2)>& d2s,
    float effZoom, bool hovered, ImDrawList* dl) {
    if (!transformOp_.Active()) return;
    const void* self = &st;
    ImGuiIO& io = ImGui::GetIO();

    // Bind ownership + the mouse anchor to the HOVERED leaf (the zone the cursor
    // is actually in when G/R/S was pressed) — NOT merely the first leaf that
    // renders, which would wrongly anchor to viewport 1 and warp the cursor
    // there. Until a hovered leaf claims it, no leaf drives the op.
    if (transformOp_.owner == nullptr) {
        if (!hovered) return;
        transformOp_.owner = self;
        transformOp_.startMouse = s2d(io.MousePos);
        // Recompute the pivot HERE, in the owning viewport, so it uses THIS
        // viewport's page display origins (curPageViews_). Computing it at
        // Action_BeginTransform used whatever viewport rendered last — wrong in
        // multi-viewport where each can have a different page layout.
        transformOp_.pivot = transformOp_.element ? ComputeVertPivot() : ComputePivot();
    }
    if (transformOp_.owner != self) return;

    const float zoom = std::max(0.0001f, effZoom);

    // VIRTUAL mouse (doc-units): integrated from the REAL pointer motion
    // (GestureMouseDelta excludes any warp jump but keeps fast user motion), so
    // it travels continuously past the zone edge with no drift at any speed.
    if (!transformOp_.virtInit) {
        transformOp_.virt = transformOp_.startMouse;
        transformOp_.virtDisplay = transformOp_.startMouse;
        transformOp_.virtInit = true;
        BeginGestureMouseTracking();
    } else {
        // Shift precision-drag (global): scale the integrated motion, NOT the
        // cursor — the physical mouse keeps its speed but the object/vertex moves
        // finer (Blender-style). PrecisionDragFactor() is 1 normally, <1 with Shift.
        ImVec2 d = GestureMouseDelta();
        const float pf = PrecisionDragFactor();
        transformOp_.virt.x += d.x * pf / zoom;
        transformOp_.virt.y += d.y * pf / zoom;
        // The DISPLAY virtual mouse integrates the FULL motion (no precision), so the
        // guide line / oriented cursor follow the real pointer speed and stay
        // continuous across edge wraps (no teleport).
        transformOp_.virtDisplay.x += d.x / zoom;
        transformOp_.virtDisplay.y += d.y / zoom;
    }
    Renderer::Vec2 m = transformOp_.virt;        // the transform reads the virtual pos
    Renderer::Vec2 P = transformOp_.pivot;

    auto rotate = [](Renderer::Vec2 v, float a) {
        float c = std::cos(a), s = std::sin(a);
        return Renderer::Vec2{ v.x * c - v.y * s, v.x * s + v.y * c };
    };

    // ── Axis constraint toggle (Blender X / Y during the op) ──────────────────
    // X / Y restrict the op to the orientation basis' X / Y axis; pressing the same
    // key again frees it, the other key switches. Rotation is always about Z in 2D,
    // so axis keys don't apply to Rotate. Only when the op is keyboard-focused.
    if (transformOp_.kind != TransformKind::Rotate) {
        if (ImGui::IsKeyPressed(ImGuiKey_X, false))
            transformOp_.axis = (transformOp_.axis == TransformAxis::X)
                              ? TransformAxis::None : TransformAxis::X;
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
            transformOp_.axis = (transformOp_.axis == TransformAxis::Y)
                              ? TransformAxis::None : TransformAxis::Y;
    }
    const Renderer::Vec2 aX = transformOp_.axisX, aY = transformOp_.axisY;

    // Compute the transform amount from the mouse.
    float dx = m.x - transformOp_.startMouse.x;
    float dy = m.y - transformOp_.startMouse.y;
    float angle = 0.0f, scale = 1.0f;
    if (transformOp_.kind == TransformKind::Rotate) {
        float a0 = std::atan2(transformOp_.startMouse.y - P.y, transformOp_.startMouse.x - P.x);
        float a1 = std::atan2(m.y - P.y, m.x - P.x);
        angle = a1 - a0;
    } else if (transformOp_.kind == TransformKind::Scale) {
        float d0 = std::hypot(transformOp_.startMouse.x - P.x, transformOp_.startMouse.y - P.y);
        float d1 = std::hypot(m.x - P.x, m.y - P.y);
        scale = (d0 > 1e-4f) ? d1 / d0 : 1.0f;
    }

    // Per-axis scale in the orientation basis (set below when constrained). For an
    // unconstrained scale both components equal `scale`.
    Renderer::Vec2 scaleV{ scale, scale };

    // Apply a Move axis constraint: project the (dx,dy) displacement onto the
    // chosen basis axis so motion is locked to it (general for any orientation).
    if (transformOp_.kind == TransformKind::Move && transformOp_.axis != TransformAxis::None) {
        Renderer::Vec2 ax = (transformOp_.axis == TransformAxis::X) ? aX : aY;
        float proj = dx * ax.x + dy * ax.y;
        dx = ax.x * proj; dy = ax.y * proj;
    }
    // A Scale axis constraint scales ONLY along the chosen basis axis (the other
    // axis keeps factor 1), expressed in basis space and applied below.
    if (transformOp_.kind == TransformKind::Scale && transformOp_.axis != TransformAxis::None)
        scaleV = (transformOp_.axis == TransformAxis::X)
               ? Renderer::Vec2{ scale, 1.0f } : Renderer::Vec2{ 1.0f, scale };
    // Scale a world-relative vector per-axis IN THE ORIENTATION BASIS: project onto
    // (aX,aY), multiply by (scaleV.x,scaleV.y), recombine. For uniform scale this is
    // just rel*scale; for a constrained scale it stretches only along the chosen
    // basis axis (general for any orientation — used for vertex/point positions).
    auto scaleRelInBasis = [&](Renderer::Vec2 rel) -> Renderer::Vec2 {
        float u = rel.x * aX.x + rel.y * aX.y;   // component along basis X
        float v = rel.x * aY.x + rel.y * aY.y;   // component along basis Y
        u *= scaleV.x; v *= scaleV.y;
        return { aX.x * u + aY.x * v, aX.y * u + aY.y * v };
    };

    // ── Snapping (magnet always-on, or Ctrl for this drag) ────────────────────
    //   • Move   → snap the translated pivot onto the view grid (Increment/Grid).
    //   • Rotate → snap the angle to the configured increment (Shift = precision
    //              increment), default 45° / 5°.
    //   • Scale  → snap the factor to 0.1 steps.
    // Vertex/Edge/Face snap modes fall back to grid here (geometry snapping is a
    // later feature); Increment + Grid are the implemented behaviours. The Affect
    // toggles gate which transforms snap (SnapActiveFor).
    snapIndicator_ = SnapResult{};               // cleared unless a snap occurs
    if (SnapActiveFor(transformOp_.kind)) {
        if (transformOp_.kind == TransformKind::Move) {
            // Capture the snap SOURCES once from PRE-MOVE geometry (no live feedback).
            // Closest → every moving control point; else one point (pivot/median/active).
            if (!transformOp_.snapSourceInit) {
                if (snap_.base == SnapSettings::Base::Closest) {
                    transformOp_.snapSources = SnapBaseSources();
                } else {
                    std::vector<Renderer::Vec2> s = SnapBaseSources();
                    transformOp_.snapSources.assign(1, s.empty() ? P : s.front());
                }
                if (transformOp_.snapSources.empty()) transformOp_.snapSources.push_back(P);
                transformOp_.snapSourceInit = true;
            }
            // The cursor reference (drift-free, no precision) + the full move so far.
            const Renderer::Vec2 cursorDoc = transformOp_.virtDisplay;
            const Renderer::Vec2 mv{ cursorDoc.x - transformOp_.startMouse.x,
                                     cursorDoc.y - transformOp_.startMouse.y };
            const float g = SnapGridStep(effZoom);

            if (snap_.mode == SnapSettings::Mode::Increment) {
                // INCREMENT: ignore the cursor POSITION — round the relative
                // DISPLACEMENT to the nearest grid step (no in-between; jumps step to
                // step). No mark.
                if (g > 1e-6f) { dx = std::round(mv.x / g) * g;
                                 dy = std::round(mv.y / g) * g; }
            } else if (snap_.mode == SnapSettings::Mode::Grid) {
                // GRID: the base SOURCE lands on the grid intersection nearest the
                // cursor (no in-between). Source = the per-base point moved so far,
                // then snapped to the grid.
                Renderer::Vec2 src0 = transformOp_.snapSources.front();
                if (snap_.base == SnapSettings::Base::Closest) {
                    // Closest: the moving control point currently nearest the cursor.
                    float best = 1e30f;
                    for (const Renderer::Vec2& s0 : transformOp_.snapSources) {
                        float d = std::hypot(s0.x + mv.x - cursorDoc.x,
                                             s0.y + mv.y - cursorDoc.y);
                        if (d < best) { best = d; src0 = s0; }
                    }
                }
                if (g > 1e-6f) {
                    // Snap to the grid intersection nearest the CURSOR (not the moved
                    // source), then bring the source onto it.
                    Renderer::Vec2 gp{ std::round(cursorDoc.x / g) * g,
                                       std::round(cursorDoc.y / g) * g };
                    dx = gp.x - src0.x; dy = gp.y - src0.y;
                    snapIndicator_.snapped = true; snapIndicator_.showMark = true;
                    snapIndicator_.pos = gp;
                }
            } else {
                // GEOMETRY modes (Vertex/Edge/EdgeCenter/Face). The TARGET is ALWAYS
                // the geometry nearest the CURSOR within the radius (same for every
                // base). The base only changes the SOURCE that lands on it:
                //   • Pivot/Median/Active → the single base point;
                //   • Closest → the selection's PRE-MOVE vertex nearest the target.
                // No self-snap: exclude the moving objects (object mode) and reject any
                // candidate coinciding with the moving selection's CURRENT positions
                // (handles edit mode, where individual vertices can't be id-excluded).
                std::vector<uint64_t> exclude = transformOp_.ids;
                // Reject the WHOLE moving selection as snap targets (edit mode): ALL
                // its vertices (not just the base source — else a non-source moving
                // vertex stays a valid target and feeds back from the snapped geometry
                // → per-frame flicker with several vertices selected). Pre-move points
                // shifted by the cursor travel (mv), so they're drift-free / no feedback.
                std::vector<Renderer::Vec2> reject = MovingSelectionPoints();
                if (reject.empty())            // object mode → fall back to the sources
                    for (const Renderer::Vec2& s0 : transformOp_.snapSources) reject.push_back(s0);
                std::vector<Renderer::Vec2> rejectSegs = MovingSelectionEdges();
                for (Renderer::Vec2& p : reject)    { p.x += mv.x; p.y += mv.y; }
                for (Renderer::Vec2& p : rejectSegs){ p.x += mv.x; p.y += mv.y; }
                SnapResult sr = ComputeSnap(cursorDoc, effZoom, exclude, reject, rejectSegs);
                if (sr.snapped) {
                    Renderer::Vec2 src0 = transformOp_.snapSources.front();
                    if (snap_.base == SnapSettings::Base::Closest) {
                        // The selection's PRE-MOVE vertex nearest the (cursor-found)
                        // target snaps onto it (dx = target − s0). Measuring in PRE-MOVE
                        // space (not s0+mv) means dragging the selection so the target
                        // sits over a different original vertex picks THAT vertex —
                        // instead of always the one that started nearest the cursor.
                        float best = 1e30f;
                        for (const Renderer::Vec2& s0 : transformOp_.snapSources) {
                            float d = std::hypot(s0.x - sr.pos.x, s0.y - sr.pos.y);
                            if (d < best) { best = d; src0 = s0; }
                        }
                    }
                    dx = sr.pos.x - src0.x; dy = sr.pos.y - src0.y;
                    if (sr.showMark) { snapIndicator_.snapped = true;
                                       snapIndicator_.showMark = true;
                                       snapIndicator_.pos = sr.pos; }
                }
            }
        } else if (transformOp_.kind == TransformKind::Rotate) {
            float deg = io.KeyShift ? snap_.rotPrecisionIncrement : snap_.rotIncrement;
            if (deg < 0.01f) deg = 5.0f;
            const float inc = deg * 3.14159265358979f / 180.0f;
            angle = std::round(angle / inc) * inc;
        } else if (transformOp_.kind == TransformKind::Scale) {
            scale = std::round(scale / 0.1f) * 0.1f;
            if (std::fabs(scale) < 1e-3f) scale = 0.1f;            // never collapse to 0
        }
    }

    // ── EDIT-mode element transform: move/rotate/scale selected VERTICES around
    //    the world pivot, writing pos + handles back in object-local space. ────
    if (transformOp_.element) {
        for (size_t i = 0; i < transformOp_.vrefs.size(); ++i) {
            const Renderer::VertRef& v = transformOp_.vrefs[i];
            Renderer::Shape* sp = project_.document.FindShape(v.shape);
            if (!sp || v.part >= (int)sp->parts.size() || i >= transformOp_.vsnap.size()) continue;
            auto& ns = sp->parts[(size_t)v.part].path.nodes;
            if (v.node >= (int)ns.size()) continue;
            const Renderer::Node& snap = transformOp_.vsnap[i];
            // Display origin for THIS viewport (matches ComputeVertPivot), so the
            // pivot and the transformed points share the same space even when the
            // page is moved / not page 1 / under an auto layout.
            const Renderer::Vec2 po = CurPageOriginOfShape(v.shape);
            // Individual Origins: each vertex rotates/scales about ITS OWN position
            // (its pos stays; its handles pivot around it). Otherwise the shared pivot.
            const Renderer::Vec2 vp = (pivotMode_ == PivotMode::IndividualOrigins)
                ? Renderer::Tessellator::WorldTransform(*sp, snap.pos, po) : P;
            // Transform a single world point about the (per-element) pivot.
            auto xf = [&](Renderer::Vec2 localPt) -> Renderer::Vec2 {
                Renderer::Vec2 w = Renderer::Tessellator::WorldTransform(*sp, localPt, po);
                if (transformOp_.kind == TransformKind::Move) { w.x += dx; w.y += dy; }
                else {
                    Renderer::Vec2 rel{ w.x - vp.x, w.y - vp.y };
                    if (transformOp_.kind == TransformKind::Rotate) rel = rotate(rel, angle);
                    else rel = scaleRelInBasis(rel);   // per-axis basis scale (or uniform)
                    w = { vp.x + rel.x, vp.y + rel.y };
                }
                return Renderer::Tessellator::InverseTransform(*sp, w, po);
            };
            Renderer::Node nn = snap;
            nn.pos = xf(snap.pos);
            if (nn.hasIn)  nn.hIn  = xf(snap.hIn);
            if (nn.hasOut) nn.hOut = xf(snap.hOut);
            ns[(size_t)v.node] = nn;
        }
        // Selected HANDLES (whose node isn't a selected vertex): move only the chosen
        // endpoint by the same transform, then ApplyHandleMode propagates to the
        // opposite handle per the node's mode (Aligned/Mirrored/…).
        for (size_t i = 0; i < transformOp_.hrefs.size() && i < transformOp_.hsnap.size(); ++i) {
            const Renderer::HandleRef& h = transformOp_.hrefs[i];
            Renderer::Shape* sp = project_.document.FindShape(h.shape);
            if (!sp || h.part >= (int)sp->parts.size()) continue;
            auto& ns = sp->parts[(size_t)h.part].path.nodes;
            if (h.node >= (int)ns.size()) continue;
            const Renderer::Node& snap = transformOp_.hsnap[i];
            const Renderer::Vec2 po = CurPageOriginOfShape(h.shape);
            // Individual Origins: a handle rotates/scales about ITS ANCHOR (the vertex
            // it belongs to) — its attached node's position. Otherwise the shared pivot.
            const Renderer::Vec2 hp = (pivotMode_ == PivotMode::IndividualOrigins)
                ? Renderer::Tessellator::WorldTransform(*sp, snap.pos, po) : P;
            auto xf = [&](Renderer::Vec2 localPt) -> Renderer::Vec2 {
                Renderer::Vec2 w = Renderer::Tessellator::WorldTransform(*sp, localPt, po);
                if (transformOp_.kind == TransformKind::Move) { w.x += dx; w.y += dy; }
                else {
                    Renderer::Vec2 rel{ w.x - hp.x, w.y - hp.y };
                    if (transformOp_.kind == TransformKind::Rotate) rel = rotate(rel, angle);
                    else rel = scaleRelInBasis(rel);
                    w = { hp.x + rel.x, hp.y + rel.y };
                }
                return Renderer::Tessellator::InverseTransform(*sp, w, po);
            };
            Renderer::Node& live = ns[(size_t)h.node];
            // Start from the live node (the anchor/other handle may differ), but move
            // THIS handle's endpoint from its SNAPSHOT so the delta is from op start.
            if (h.outSide) { live.hOut = xf(snap.hOut); live.hasOut = true; }
            else           { live.hIn  = xf(snap.hIn);  live.hasIn  = true; }
            ApplyHandleMode(live, h.outSide);
        }
    } else
    // Apply to each shape from its snapshot.
    for (size_t i = 0; i < transformOp_.ids.size(); ++i) {
        Renderer::Shape* sp = project_.document.FindShape(transformOp_.ids[i]);
        if (!sp || i >= transformOp_.snapshot.size()) continue;
        // Honour per-shape transform locks: a fixed-size / north-oriented symbol
        // (e.g. ISOM) ignores the matching op (a mixed selection still moves the
        // unlocked ones).
        if (transformOp_.kind == TransformKind::Scale  && sp->lockScale)    continue;
        if (transformOp_.kind == TransformKind::Rotate && sp->lockRotation) continue;
        const Renderer::Transform& snap = transformOp_.snapshot[i];
        Renderer::Transform t = snap;
        // Display origin for THIS viewport (matches ComputePivot) — keeps the
        // pivot correct when the page is moved / not page 1 / auto-layout.
        const Renderer::Vec2 po = CurPageOriginOfShape(transformOp_.ids[i]);

        // Per-object pivot for Individual Origins; the shared pivot otherwise.
        Renderer::Vec2 piv = P;
        if (pivotMode_ == PivotMode::IndividualOrigins) {
            // The shape's origin in world space under the snapshot transform.
            Renderer::Shape tmp = *sp; tmp.transform = snap;
            piv = Renderer::Tessellator::WorldTransform(tmp, sp->origin, po);
        }

        if (transformOp_.kind == TransformKind::Move) {
            t.translate = { snap.translate.x + dx, snap.translate.y + dy };
        } else {
            // Origin-world position under the snapshot.
            Renderer::Shape tmp = *sp; tmp.transform = snap;
            Renderer::Vec2 Ow = Renderer::Tessellator::WorldTransform(tmp, sp->origin, po);
            Renderer::Vec2 rel{ Ow.x - piv.x, Ow.y - piv.y };
            if (transformOp_.kind == TransformKind::Rotate) {
                t.rotate = snap.rotate + angle;
                Renderer::Vec2 rr = rotate(rel, angle);
                Renderer::Vec2 Ow2{ piv.x + rr.x, piv.y + rr.y };
                t.translate = { snap.translate.x + (Ow2.x - Ow.x),
                                snap.translate.y + (Ow2.y - Ow.y) };
            } else { // Scale
                // Object SIZE: per-axis factor expressed in the object's OWN local
                // frame (so a Local/axis-aligned constraint maps exactly to scale.x/
                // scale.y; an unconstrained scale stays uniform). We rotate the basis
                // factors by −object rotation to read them in local axes.
                float orot = snap.rotate;
                float c = std::cos(orot), s = std::sin(orot);
                // Object local X axis in world = (c,s); local Y = (−s,c). Its scale
                // factor along basis = how much scaleV stretches that direction.
                auto factorAlong = [&](Renderer::Vec2 dir) {
                    float u = dir.x * aX.x + dir.y * aX.y;
                    float v = dir.x * aY.x + dir.y * aY.y;
                    // Magnitude of the scaled unit vector along this object axis.
                    return std::hypot(u * scaleV.x, v * scaleV.y);
                };
                float fx = factorAlong({ c, s });
                float fy = factorAlong({ -s, c });
                t.scale = { snap.scale.x * fx, snap.scale.y * fy };
                // Object POSITION: scale the origin offset per-axis in the basis.
                Renderer::Vec2 sr = scaleRelInBasis(rel);
                Renderer::Vec2 Ow2{ piv.x + sr.x, piv.y + sr.y };
                t.translate = { snap.translate.x + (Ow2.x - Ow.x),
                                snap.translate.y + (Ow2.y - Ow.y) };
            }
        }
        sp->transform = t;
    }

    // Pivot marker + DASHED guide line drawn to the VIRTUAL mouse (which
    // continues past the zone edge). Move shows the move cursor; rotate/scale
    // show an oriented directional cursor at the PHYSICAL mouse.
    ImU32 cAccent = ImGui::GetColorU32(
        DesignSystem::DesignSystem::Instance().GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    auto dashedLine = [&](ImVec2 a, ImVec2 b, ImU32 col) {
        float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1.0f) return;
        ImVec2 dir{ (b.x - a.x) / len, (b.y - a.y) / len };
        const float dash = 6.0f, gap = 4.0f; float t = 0.0f;
        while (t < len) {
            float t2 = std::min(t + dash, len);
            dl->AddLine(ImVec2(a.x + dir.x * t,  a.y + dir.y * t),
                        ImVec2(a.x + dir.x * t2, a.y + dir.y * t2), col, 1.4f);
            t = t2 + gap;
        }
    };
    // Axis constraint guide: a long line through the pivot along the constrained
    // basis axis (Blender's coloured axis line). X = positive/red, Y = green — via
    // the design tokens so it follows the theme. Drawn for Move + Scale.
    if (transformOp_.axis != TransformAxis::None &&
        transformOp_.kind != TransformKind::Rotate) {
        Renderer::Vec2 axW = (transformOp_.axis == TransformAxis::X)
                           ? transformOp_.axisX : transformOp_.axisY;
        // World axis direction → screen direction (through two world points).
        ImVec2 c0 = d2s(P);
        ImVec2 c1 = d2s({ P.x + axW.x, P.y + axW.y });
        ImVec2 sd{ c1.x - c0.x, c1.y - c0.y };
        float sl = std::hypot(sd.x, sd.y);
        if (sl > 1e-4f) {
            sd.x /= sl; sd.y /= sl;
            const float far = 4000.0f;
            ImU32 axCol = ImGui::GetColorU32(DesignSystem::DesignSystem::Instance()
                .GetColor(transformOp_.axis == TransformAxis::X
                          ? DesignSystem::Tok::S_Color_Negative_Default
                          : DesignSystem::Tok::S_Color_Positive_Default));
            dl->AddLine(ImVec2(c0.x - sd.x * far, c0.y - sd.y * far),
                        ImVec2(c0.x + sd.x * far, c0.y + sd.y * far), axCol, 1.2f);
        }
    }
    // Wrap the PHYSICAL cursor within THIS zone's canvas. WrapMouseInRect moves
    // our motion reference to the warp target, so the next GestureMouseDelta()
    // excludes the jump but keeps real motion → continuous + drift-free.
    if (transformOp_.kind == TransformKind::Move) {
        ShowMoveCursor();
        WrapMouseInRect(gestureCanvasMin_, gestureCanvasMax_);
    } else if (pivotMode_ != PivotMode::IndividualOrigins) {
        ImVec2 pp = d2s(P);
        // The guide line follows the DISPLAY virtual mouse: it moves at the cursor's
        // REAL speed (Shift refines the transform, not the line) AND keeps its
        // direction across an edge wrap (it does NOT teleport to the warped cursor).
        ImVec2 vm = d2s(transformOp_.virtDisplay);
        dl->AddCircle(pp, 5.0f, cAccent, 0, 1.5f);
        dashedLine(pp, vm, cAccent);
        // Angle of the pivot→display-mouse line (in screen space).
        float ang = std::atan2(vm.y - pp.y, vm.x - pp.x);
        if (transformOp_.kind == TransformKind::Scale) {
            // Scale: arrows POINT ALONG the line (grow/shrink direction).
            ShowOrientedCursor("move-left-right-cur copy", ang);
        } else {
            // Rotate: arrows are TANGENT to the rotation circle, i.e.
            // perpendicular to the radius. The up-down asset already points
            // vertically (90° to the left-right asset), so aligning it to the
            // radius direction makes it read as the tangent.
            ShowOrientedCursor("move-up-down-cur", ang);
        }
        WrapMouseInRect(gestureCanvasMin_, gestureCanvasMax_);
    }
    (void)zoom; (void)dx; (void)dy;

    // Confirm / cancel.
    bool confirm = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                   ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    bool rmbCancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    bool cancel  = ImGui::IsKeyPressed(ImGuiKey_Escape) || rmbCancel;
    if (cancel) {
        if (transformOp_.element) {
            // Restore each selected vertex's snapshot node.
            for (size_t i = 0; i < transformOp_.vrefs.size(); ++i) {
                const Renderer::VertRef& v = transformOp_.vrefs[i];
                Renderer::Shape* sp = project_.document.FindShape(v.shape);
                if (sp && v.part < (int)sp->parts.size() && i < transformOp_.vsnap.size()) {
                    auto& ns = sp->parts[(size_t)v.part].path.nodes;
                    if (v.node < (int)ns.size()) ns[(size_t)v.node] = transformOp_.vsnap[i];
                }
            }
            // Restore each selected handle's snapshot node (whole node, so the
            // mode-propagated opposite handle is restored too).
            for (size_t i = 0; i < transformOp_.hrefs.size() && i < transformOp_.hsnap.size(); ++i) {
                const Renderer::HandleRef& h = transformOp_.hrefs[i];
                Renderer::Shape* sp = project_.document.FindShape(h.shape);
                if (sp && h.part < (int)sp->parts.size()) {
                    auto& ns = sp->parts[(size_t)h.part].path.nodes;
                    if (h.node < (int)ns.size()) ns[(size_t)h.node] = transformOp_.hsnap[i];
                }
            }
        } else {
            for (size_t i = 0; i < transformOp_.ids.size(); ++i)
                if (Renderer::Shape* sp = project_.document.FindShape(transformOp_.ids[i]))
                    if (i < transformOp_.snapshot.size()) sp->transform = transformOp_.snapshot[i];
        }
        transformOp_.Reset();
        if (rmbCancel) rmbConsumedByTransform_ = true;  // suppress context menu
    } else if (confirm) {
        // A confirmed object MOVE may have dragged objects over another page.
        if (!transformOp_.element && transformOp_.kind == TransformKind::Move)
            MaybeTransferMovedObjects(transformOp_.ids);
        // Committing the extrude-move ACCEPTS the new point — the next E starts a
        // fresh chain (no cyclic revert of this now-placed point).
        extrudeJustCreated_ = false;
        project_.dirty = true;
        // Record the rich action (Info feed + operator panel) BEFORE Reset, so the
        // panel can re-apply with tweaked params. Build the detail + params per kind.
        PublishTransformOperator(dx, dy, angle, scale);
        transformOp_.Reset();
    }
}

// PRE-MOVE world snap-source point(s) of the moving selection under the current
// Snap Base. Computed from the op SNAPSHOTS (transformOp_.snapshot / vsnap), NOT the
// live geometry — so the sources are the ORIGINAL positions even when snap is toggled
// ON mid-drag (the live shape is already moved by then). The pivot is captured at op
// start, so it's already pre-move.
std::vector<Renderer::Vec2> Application::SnapBaseSources() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    std::vector<Renderer::Vec2> out;
    const bool element = transformOp_.element;

    // Pre-move world position of an object node, using its SNAPSHOT transform.
    auto objNodeWorld = [&](size_t i, Renderer::Vec2 localPt) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(transformOp_.ids[i]);
        if (!s || i >= transformOp_.snapshot.size())
            return localPt;
        Renderer::Shape tmp = *s; tmp.transform = transformOp_.snapshot[i];
        return Renderer::Tessellator::WorldTransform(
            tmp, localPt, CurPageOriginOfShape(transformOp_.ids[i]));
    };

    if (snap_.base == SnapSettings::Base::Active) {
        if (element) {
            // Active vertex: find its vsnap entry (pre-move node) by VertRef match.
            const Renderer::VertRef& av = doc.ActiveVert();
            for (size_t i = 0; i < transformOp_.vrefs.size() && i < transformOp_.vsnap.size(); ++i)
                if (transformOp_.vrefs[i] == av) {
                    if (Renderer::Shape* s = doc.FindShape(av.shape))
                        out.push_back(Renderer::Tessellator::WorldTransform(
                            *s, transformOp_.vsnap[i].pos, CurPageOriginOfShape(av.shape)));
                    break;
                }
        } else {
            // Active object: its origin under the snapshot transform.
            for (size_t i = 0; i < transformOp_.ids.size(); ++i)
                if (transformOp_.ids[i] == doc.ActiveId()) {
                    if (Renderer::Shape* a = doc.FindShape(doc.ActiveId()))
                        out.push_back(objNodeWorld(i, a->origin));
                    break;
                }
        }
        if (!out.empty()) return out;
    }
    if (snap_.base == SnapSettings::Base::Center ||
        snap_.base == SnapSettings::Base::Median) {
        // The op's pivot — captured at op start (pre-move), follows pivotMode_.
        out.push_back(transformOp_.pivot);
        return out;
    }
    // Closest: every moving CONTROL POINT at its PRE-MOVE world position.
    if (element) {
        for (size_t i = 0; i < transformOp_.vrefs.size() && i < transformOp_.vsnap.size(); ++i) {
            const Renderer::VertRef& v = transformOp_.vrefs[i];
            if (Renderer::Shape* s = doc.FindShape(v.shape))
                out.push_back(Renderer::Tessellator::WorldTransform(
                    *s, transformOp_.vsnap[i].pos, CurPageOriginOfShape(v.shape)));
        }
    } else {
        for (size_t i = 0; i < transformOp_.ids.size(); ++i) {
            Renderer::Shape* s = doc.FindShape(transformOp_.ids[i]);
            if (!s) continue;
            for (const Renderer::Part& part : s->parts) {
                Renderer::Part baked = part; baked.EnsurePath();
                for (const Renderer::Node& nd : baked.path.nodes)
                    out.push_back(objNodeWorld(i, nd.pos));
            }
        }
    }
    if (out.empty()) out.push_back(transformOp_.pivot);   // fallback
    return out;
}

// Moving-selection EDGES as PRE-MOVE world segment pairs (edit mode). Computed from
// the op SNAPSHOTS (transformOp_.vsnap), NOT live geometry — otherwise it reads the
// already-snapped positions and feeds back into the snap (flicker with multiple
// vertices selected). The CALLER shifts these by the cursor travel (mv).
std::vector<Renderer::Vec2> Application::MovingSelectionEdges() const {
    std::vector<Renderer::Vec2> segs;
    if (!transformOp_.element) return segs;     // object mode excludes whole shapes
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    // Pre-move world position of a selected vertex via its vsnap entry.
    auto preWorld = [&](uint64_t sh, int pa, int nd, Renderer::Vec2& out) -> bool {
        for (size_t i = 0; i < transformOp_.vrefs.size() && i < transformOp_.vsnap.size(); ++i)
            if (transformOp_.vrefs[i].shape == sh && transformOp_.vrefs[i].part == pa &&
                transformOp_.vrefs[i].node == nd) {
                Renderer::Shape* s = doc.FindShape(sh);
                if (!s) return false;
                out = Renderer::Tessellator::WorldTransform(
                    *s, transformOp_.vsnap[i].pos, CurPageOriginOfShape(sh));
                return true;
            }
        return false;     // not a selected (moving) vertex
    };
    // Unique (shape,part) groups of the selection.
    std::vector<std::pair<uint64_t,int>> parts;
    for (const Renderer::VertRef& v : transformOp_.vrefs) {
        std::pair<uint64_t,int> key{ v.shape, v.part };
        if (std::find(parts.begin(), parts.end(), key) == parts.end()) parts.push_back(key);
    }
    for (const auto& key : parts) {
        Renderer::Shape* s = doc.FindShape(key.first);
        if (!s || key.second >= (int)s->parts.size()) continue;
        const Renderer::Part& part = s->parts[(size_t)key.second];
        const int sc = std::max(1, part.path.subCount());
        for (int spi = 0; spi < sc; ++spi) {
            int b0 = 0, e0 = (int)part.path.nodes.size();
            part.path.subRange(spi, b0, e0);
            const bool cyc = part.path.closed;
            int segCount = cyc ? (e0 - b0) : (e0 - b0 - 1);
            for (int k = 0; k < segCount; ++k) {
                int ia = b0 + k, ib = b0 + ((k + 1) % (e0 - b0));
                Renderer::Vec2 wa, wb;
                // Both endpoints must be moving (selected) → a moving edge.
                if (!preWorld(key.first, key.second, ia, wa)) continue;
                if (!preWorld(key.first, key.second, ib, wb)) continue;
                segs.push_back(wa); segs.push_back(wb);
            }
        }
    }
    return segs;
}

// ALL the moving selection's PRE-MOVE world points (every selected vertex; edit mode
// only). Independent of the Snap Base, so the snap rejects the WHOLE moving selection
// as targets (else a moving vertex that isn't the base source feeds back → flicker).
std::vector<Renderer::Vec2> Application::MovingSelectionPoints() const {
    std::vector<Renderer::Vec2> pts;
    if (!transformOp_.element) return pts;
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    for (size_t i = 0; i < transformOp_.vrefs.size() && i < transformOp_.vsnap.size(); ++i) {
        const Renderer::VertRef& v = transformOp_.vrefs[i];
        if (Renderer::Shape* s = doc.FindShape(v.shape))
            pts.push_back(Renderer::Tessellator::WorldTransform(
                *s, transformOp_.vsnap[i].pos, CurPageOriginOfShape(v.shape)));
    }
    return pts;
}

// Build the Info-feed line + the operator redo panel for a just-confirmed G/R/S.
// Captures the affected ids + their pre-op snapshots so the panel can re-apply the
// transform with an edited value (Blender's adjust-last-operation box).
void Application::PublishTransformOperator(float dx, float dy, float angle, float scale) {
    using TO = TransformOrientation;
    const char* orientName = TransformOrientationName(transformOrientation_);
    const char* axisName = transformOp_.axis == TransformAxis::X ? "X"
                         : transformOp_.axis == TransformAxis::Y ? "Y" : "Free";
    char buf[160];
    std::vector<std::pair<std::string,std::string>> kv;
    const char* verb = "Transform";
    if (transformOp_.kind == TransformKind::Move) {
        verb = transformOp_.element ? "Move Vertices" : "Move";
        std::snprintf(buf, sizeof buf, "(%.4g, %.4g)", dx, dy);
        kv.push_back({ "value", buf });
    } else if (transformOp_.kind == TransformKind::Rotate) {
        verb = transformOp_.element ? "Rotate Vertices" : "Rotate";
        std::snprintf(buf, sizeof buf, "%.3g\xC2\xB0", angle * 180.0f / 3.14159265358979f);
        kv.push_back({ "angle", buf });
    } else if (transformOp_.kind == TransformKind::Scale) {
        verb = transformOp_.element ? "Scale Vertices" : "Scale";
        std::snprintf(buf, sizeof buf, "%.4g", scale);
        kv.push_back({ "factor", buf });
    }
    kv.push_back({ "orient", orientName });
    kv.push_back({ "axis", axisName });
    if (!transformOp_.element) {
        std::snprintf(buf, sizeof buf, "%d", (int)transformOp_.ids.size());
        kv.push_back({ "objects", buf });
    }
    LogInfoActionRich(verb, FormatActionDetail(kv));

    // Publish the operator-panel record. Works for BOTH object and EDIT-mode
    // (vertex) transforms — the rerun re-applies from the captured snapshot.
    OperatorRecord op;
    op.active = true;
    op.title  = verb;
    // Snapshot the op state needed to re-apply: ids/verts + pre-op state + pivot +
    // basis + orientation index. Captured by value into the rerun closure.
    const bool element = transformOp_.element;
    std::vector<uint64_t> ids = transformOp_.ids;
    std::vector<Renderer::Transform> snap = transformOp_.snapshot;
    std::vector<Renderer::VertRef> vrefs = transformOp_.vrefs;
    std::vector<Renderer::Node> vsnap = transformOp_.vsnap;
    Renderer::Vec2 pivot = transformOp_.pivot;
    Renderer::Vec2 aX = transformOp_.axisX, aY = transformOp_.axisY;
    TransformAxis axis = transformOp_.axis;
    TransformKind kind = transformOp_.kind;
    int orientIdx = (int)transformOrientation_;

    // The Move value is stored in the ORIENTATION BASIS (X along aX, Y along aY), so
    // editing X moves along the basis X axis and Y along basis Y — and changing the
    // Orientation param in the panel re-derives the axes (ApplyTransformFromSnapshot).
    if (kind == TransformKind::Move) {
        float vx = dx * aX.x + dy * aX.y;        // component along basis X
        float vy = dx * aY.x + dy * aY.y;        // component along basis Y
        OperatorParam px; px.kind = OperatorParam::Kind::Float; px.label = "X";
        px.fvalue = vx; op.params.push_back(px);
        OperatorParam py; py.kind = OperatorParam::Kind::Float; py.label = "Y";
        py.fvalue = vy; op.params.push_back(py);
    } else if (kind == TransformKind::Rotate) {
        OperatorParam pa; pa.kind = OperatorParam::Kind::Float; pa.label = "Angle";
        pa.fvalue = angle * 180.0f / 3.14159265358979f; op.params.push_back(pa);
    } else { // Scale
        OperatorParam ps; ps.kind = OperatorParam::Kind::Float; ps.label = "Factor";
        ps.fvalue = scale; op.params.push_back(ps);
    }
    OperatorParam po; po.kind = OperatorParam::Kind::Enum; po.label = "Orientation";
    po.value = orientIdx;
    po.options = { "Global","Local","View","Cursor","Parent" };
    op.params.push_back(po);
    const size_t orientParamIdx = op.params.size() - 1;   // the Orientation enum row

    op.rerun = [this, element, ids, snap, vrefs, vsnap, pivot, axis, kind,
                orientParamIdx]() {
        if (lastOperator_.params.empty()) return;
        // Re-derive the basis from the panel's CURRENT Orientation param, so changing
        // it in the box re-aims the X/Y axes (Blender's adjust-last-operation).
        TransformOrientation orient = TransformOrientation::Global;
        if (orientParamIdx < lastOperator_.params.size())
            orient = (TransformOrientation)lastOperator_.params[orientParamIdx].value;
        Renderer::Vec2 nX, nY; ComputeOrientationBasis(nX, nY, orient);
        if (element)
            ApplyElementTransformFromSnapshot(vrefs, vsnap, pivot, nX, nY, axis, kind,
                                              lastOperator_.params);
        else
            ApplyTransformFromSnapshot(ids, snap, pivot, nX, nY, axis, kind,
                                       lastOperator_.params);
    };
    SetLastOperator(std::move(op));
}

// Re-apply a Move/Rotate/Scale to the snapshot transforms, reading the amount from
// the operator panel's params. Mirrors UpdateTransformOp's per-object apply but
// driven by explicit values (so the redo box can adjust a finished transform). The
// basis (aX/aY) is re-derived by the caller from the panel's Orientation param, so
// changing it re-aims the X/Y axes. The Move value is in BASIS space (X along aX,
// Y along aY) → recomposed to world here. One undo step via MarkUndoLabel.
void Application::ApplyTransformFromSnapshot(
        const std::vector<uint64_t>& ids,
        const std::vector<Renderer::Transform>& snap,
        Renderer::Vec2 pivot, Renderer::Vec2 aX, Renderer::Vec2 aY,
        TransformAxis axis, TransformKind kind,
        const std::vector<OperatorParam>& params) {
    auto& doc = project_.document;
    float dx = 0, dy = 0, angle = 0, scale = 1;
    if (kind == TransformKind::Move) {
        // params are (X,Y) in the basis → world delta = X·aX + Y·aY.
        float vx = params.size() > 0 ? params[0].fvalue : 0.0f;
        float vy = params.size() > 1 ? params[1].fvalue : 0.0f;
        dx = vx * aX.x + vy * aY.x;
        dy = vx * aX.y + vy * aY.y;
    } else if (kind == TransformKind::Rotate) {
        angle = (params.size() > 0 ? params[0].fvalue : 0.0f) * 3.14159265358979f / 180.0f;
    } else {
        scale = params.size() > 0 ? params[0].fvalue : 1.0f;
    }
    auto rotate = [](Renderer::Vec2 v, float a) {
        float c = std::cos(a), s = std::sin(a);
        return Renderer::Vec2{ v.x * c - v.y * s, v.x * s + v.y * c };
    };
    Renderer::Vec2 scaleV{ scale, scale };
    if (kind == TransformKind::Scale && axis != TransformAxis::None)
        scaleV = (axis == TransformAxis::X) ? Renderer::Vec2{ scale, 1.0f }
                                            : Renderer::Vec2{ 1.0f, scale };
    auto scaleRelInBasis = [&](Renderer::Vec2 rel) {
        float u = rel.x * aX.x + rel.y * aX.y, v = rel.x * aY.x + rel.y * aY.y;
        u *= scaleV.x; v *= scaleV.y;
        return Renderer::Vec2{ aX.x * u + aY.x * v, aX.y * u + aY.y * v };
    };
    const Renderer::Vec2 P = pivot;
    for (size_t i = 0; i < ids.size() && i < snap.size(); ++i) {
        Renderer::Shape* sp = doc.FindShape(ids[i]);
        if (!sp) continue;
        if (kind == TransformKind::Scale  && sp->lockScale)    continue;
        if (kind == TransformKind::Rotate && sp->lockRotation) continue;
        const Renderer::Vec2 po = CurPageOriginOfShape(ids[i]);
        Renderer::Transform t = snap[i];
        if (kind == TransformKind::Move) {
            t.translate = { snap[i].translate.x + dx, snap[i].translate.y + dy };
        } else {
            Renderer::Shape tmp = *sp; tmp.transform = snap[i];
            Renderer::Vec2 Ow = Renderer::Tessellator::WorldTransform(tmp, sp->origin, po);
            Renderer::Vec2 rel{ Ow.x - P.x, Ow.y - P.y };
            if (kind == TransformKind::Rotate) {
                t.rotate = snap[i].rotate + angle;
                Renderer::Vec2 rr = rotate(rel, angle);
                t.translate = { snap[i].translate.x + (P.x + rr.x - Ow.x),
                                snap[i].translate.y + (P.y + rr.y - Ow.y) };
            } else {
                float orot = snap[i].rotate, c = std::cos(orot), s = std::sin(orot);
                auto factorAlong = [&](Renderer::Vec2 dir){
                    float u = dir.x*aX.x+dir.y*aX.y, v = dir.x*aY.x+dir.y*aY.y;
                    return std::hypot(u*scaleV.x, v*scaleV.y);
                };
                t.scale = { snap[i].scale.x * factorAlong({c,s}),
                            snap[i].scale.y * factorAlong({-s,c}) };
                Renderer::Vec2 sr = scaleRelInBasis(rel);
                t.translate = { snap[i].translate.x + (P.x + sr.x - Ow.x),
                                snap[i].translate.y + (P.y + sr.y - Ow.y) };
            }
        }
        sp->transform = t;
    }
    MarkUndoLabel(kind == TransformKind::Move ? "Move"
                : kind == TransformKind::Rotate ? "Rotate" : "Scale");
    project_.dirty = true;
}

// Re-apply an EDIT-mode (vertex) transform to the snapshotted nodes, reading the
// amount from the operator panel. Mirrors UpdateTransformOp's element branch but
// driven by explicit values. Move value is in BASIS space; basis re-derived by the
// caller. One undo step.
void Application::ApplyElementTransformFromSnapshot(
        const std::vector<Renderer::VertRef>& vrefs,
        const std::vector<Renderer::Node>& vsnap,
        Renderer::Vec2 pivot, Renderer::Vec2 aX, Renderer::Vec2 aY,
        TransformAxis axis, TransformKind kind,
        const std::vector<OperatorParam>& params) {
    auto& doc = project_.document;
    float dx = 0, dy = 0, angle = 0, scale = 1;
    if (kind == TransformKind::Move) {
        float vx = params.size() > 0 ? params[0].fvalue : 0.0f;
        float vy = params.size() > 1 ? params[1].fvalue : 0.0f;
        dx = vx * aX.x + vy * aY.x; dy = vx * aX.y + vy * aY.y;
    } else if (kind == TransformKind::Rotate) {
        angle = (params.size() > 0 ? params[0].fvalue : 0.0f) * 3.14159265358979f / 180.0f;
    } else {
        scale = params.size() > 0 ? params[0].fvalue : 1.0f;
    }
    auto rotate = [](Renderer::Vec2 v, float a) {
        float c = std::cos(a), s = std::sin(a);
        return Renderer::Vec2{ v.x * c - v.y * s, v.x * s + v.y * c };
    };
    Renderer::Vec2 scaleV{ scale, scale };
    if (kind == TransformKind::Scale && axis != TransformAxis::None)
        scaleV = (axis == TransformAxis::X) ? Renderer::Vec2{ scale, 1.0f }
                                            : Renderer::Vec2{ 1.0f, scale };
    auto scaleRelInBasis = [&](Renderer::Vec2 rel) {
        float u = rel.x*aX.x+rel.y*aX.y, v = rel.x*aY.x+rel.y*aY.y;
        u *= scaleV.x; v *= scaleV.y;
        return Renderer::Vec2{ aX.x*u + aY.x*v, aX.y*u + aY.y*v };
    };
    const Renderer::Vec2 P = pivot;
    for (size_t i = 0; i < vrefs.size() && i < vsnap.size(); ++i) {
        const Renderer::VertRef& vr = vrefs[i];
        Renderer::Shape* sp = doc.FindShape(vr.shape);
        if (!sp || vr.part >= (int)sp->parts.size()) continue;
        auto& ns = sp->parts[(size_t)vr.part].path.nodes;
        if (vr.node >= (int)ns.size()) continue;
        const Renderer::Vec2 po = CurPageOriginOfShape(vr.shape);
        auto xf = [&](Renderer::Vec2 localPt) {
            Renderer::Vec2 w = Renderer::Tessellator::WorldTransform(*sp, localPt, po);
            if (kind == TransformKind::Move) { w.x += dx; w.y += dy; }
            else {
                Renderer::Vec2 rel{ w.x - P.x, w.y - P.y };
                if (kind == TransformKind::Rotate) rel = rotate(rel, angle);
                else rel = scaleRelInBasis(rel);
                w = { P.x + rel.x, P.y + rel.y };
            }
            return Renderer::Tessellator::InverseTransform(*sp, w, po);
        };
        Renderer::Node nn = vsnap[i];
        nn.pos = xf(vsnap[i].pos);
        if (nn.hasIn)  nn.hIn  = xf(vsnap[i].hIn);
        if (nn.hasOut) nn.hOut = xf(vsnap[i].hOut);
        ns[(size_t)vr.node] = nn;
    }
    MarkUndoLabel(kind == TransformKind::Move ? "Move Vertices"
                : kind == TransformKind::Rotate ? "Rotate Vertices" : "Scale Vertices");
    project_.dirty = true;
}

// After a Move: re-parent any object whose ORIGIN now sits over a different
// page, keeping its visual position (Document::MoveShapeToArtboard adjusts the
// page-relative translate). Holding Alt skips the transfer (object stays on its
// page, just repositioned — the "move within/over without transferring" case).
// Alt (not Ctrl) so Ctrl stays free for grid snapping during the move.
void Application::MaybeTransferMovedObjects(const std::vector<uint64_t>& ids) {
    if (ImGui::GetIO().KeyAlt) return;           // Alt = keep current page
    auto& doc = project_.document;
    bool any = false;
    for (uint64_t id : ids) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) continue;
        int src = doc.ArtboardOfShape(id);   // −1 if loose
        const bool loose = (src < 0);
        // Detect the drop target in the OWNING viewport's DISPLAY space (the same
        // space the drop preview uses), so the result matches the preview even
        // under an auto page layout: the object's displayed origin, tested
        // against each page's DISPLAYED rect. A loose object sits at {0,0}.
        Renderer::Vec2 srcPo = loose ? Renderer::Vec2{0, 0} : CurPageOrigin(src);
        Renderer::Vec2 ow = Renderer::Tessellator::WorldTransform(*s, s->origin, srcPo);
        int dst = -1;
        for (int k = (int)doc.artboards.size() - 1; k >= 0; --k) {
            if (!CurPageVisible(k)) continue;
            Renderer::Vec2 po = CurPageOrigin(k);
            const auto& ab = doc.artboards[(size_t)k];
            if (ow.x >= po.x && ow.x <= po.x + ab.size.x &&
                ow.y >= po.y && ow.y <= po.y + ab.size.y) { dst = k; break; }
        }
        if (loose) {
            // A loose object dragged onto a page attaches to it (and leaves its
            // page-less collection → that page's root). Off any page → stays loose.
            if (dst >= 0) {
                doc.AttachShapeToPage(id, dst);
                if (Renderer::Shape* moved = doc.FindShape(id)) moved->collectionId = 0;
                any = true;
            }
            continue;
        }
        if (dst >= 0 && dst != src) {
            doc.MoveShapeToArtboard(id, dst, /*keepWorldPos=*/true);
            // An object under a page MUST belong to that page: changing its page
            // resets it to the new page's ROOT (drops its old-page collection).
            if (Renderer::Shape* moved = doc.FindShape(id)) moved->collectionId = 0;
            any = true;
        }
    }
    if (any) { MarkUndoLabel("Move to page"); project_.dirty = true; }
}

// ── Ultra-contextual status-bar hints (Lot 4) ────────────────────────────────
// Publish, every frame, the keys that are RELEVANT RIGHT NOW so the status bar
// shows nothing impossible in the current context (Blender-style).
void Application::PublishStatusHints() {
    using namespace Shortcuts;
    auto key = [](ImGuiKey k) {
        EventSignature s; s.type = EventType::KeyPress; s.key = k; return s;
    };
    auto modKey = [](ImGuiKey k, bool ctrl, bool shift) {
        EventSignature s; s.type = EventType::KeyPress; s.key = k;
        s.modifiers.ctrl = ctrl; s.modifiers.shift = shift; return s;
    };
    std::vector<ModalKeymapHint> hints;

    if (transformOp_.Active()) {
        // A modal G/R/S is running: advertise axis constraints, snap/precision and
        // confirm/cancel. Snap label matches the op (grid / 5° / 0.1×).
        const char* verb = transformOp_.kind == TransformKind::Move   ? "Move"
                         : transformOp_.kind == TransformKind::Rotate ? "Rotate"
                         : transformOp_.kind == TransformKind::Scale  ? "Scale"
                                                                      : "Transform";
        const char* snapLbl = transformOp_.kind == TransformKind::Rotate ? "Snap 5\xC2\xB0"
                            : transformOp_.kind == TransformKind::Scale  ? "Snap 0.1"
                                                                         : "Snap grid";
        // Axis keys apply to Move/Scale only (rotation is about Z in 2D). The label
        // shows the active constraint + orientation so the user knows the frame.
        if (transformOp_.kind != TransformKind::Rotate) {
            const char* orient = TransformOrientationName(transformOrientation_);
            std::string xl = std::string(verb) + " X";
            std::string yl = std::string(verb) + " Y";
            if (transformOp_.axis == TransformAxis::X) xl += " (" + std::string(orient) + ")";
            if (transformOp_.axis == TransformAxis::Y) yl += " (" + std::string(orient) + ")";
            hints.push_back({ key(ImGuiKey_X), xl });
            hints.push_back({ key(ImGuiKey_Y), yl });
        }
        hints.push_back({ modKey(ImGuiKey_LeftCtrl,  true,  false), snapLbl });
        hints.push_back({ modKey(ImGuiKey_LeftShift, false, true),  "Precision" });
        hints.push_back({ key(ImGuiKey_Enter),  "Confirm" });
        hints.push_back({ key(ImGuiKey_Escape), "Cancel" });
    }
    // else: leave empty → the status bar falls back to the generic context
    // actions (GetStatusBarActions), which already filter by the active
    // editor/mode/tool. (Edit-mode submodes, tool hints, between-editor RMB:
    // these are added here as the corresponding features land.)

    Shortcuts::ShortcutManager::Instance().SetTransientHints(std::move(hints));
}

// Revert + reset the in-progress gesture (Esc / right-click). Returns true if a
// gesture was actually cancelled.
bool Application::CancelViewportGesture() {
    if (!toolState_.Active()) return false;
    if (toolState_.gesture == ToolGesture::MoveObjects) {
        // Restore each moved shape's original translate.
        for (size_t i = 0; i < toolState_.moveIds.size(); ++i)
            if (Renderer::Shape* s = project_.document.FindShape(toolState_.moveIds[i]))
                if (i < toolState_.moveOrigTranslate.size())
                    s->transform.translate = toolState_.moveOrigTranslate[i];
    }
    // Drag-create / polyline / bezier: dropping the gesture discards the
    // in-progress geometry (nothing was committed to the document yet).
    toolState_.Reset();
    return true;
}

// Emit the exact Bézier nodes (world WNode list) of `fc` between arc-lengths
// [arc0, arc1] (arc1 may be < arc0 → reversed direction), De Casteljau-split at
// both ends. The first node's hIn / last node's hOut are unused by the caller.
// `arcFrom`→`arcTo` is a SIGNED, possibly UNWRAPPED interval (arcTo may be < arcFrom
// for a backward trace, and for a CYCLIC curve it may run past the seam — the curve
// has no ends, so we follow the cursor's direction across the join without wrapping
// the wrong way). The travel is clamped to one full lap. Sub-cubics are cut with De
// Casteljau at both ends; for a backward trace the node list + handles are flipped.
static std::vector<WNode> FollowExtract(const FollowCurve& fc, float arcFrom, float arcTo) {
    std::vector<WNode> out;
    if (!fc.valid()) return out;
    const float total = fc.TotalArc();
    if (total < 1e-5f) return out;
    const bool fwd = arcTo >= arcFrom;
    float a0 = std::min(arcFrom, arcTo), a1 = std::max(arcFrom, arcTo);
    float span = a1 - a0;
    if (!fc.closed) { a0 = std::clamp(a0, 0.0f, total); a1 = std::clamp(a1, 0.0f, total); }
    else            { span = std::min(span, total); a1 = a0 + span; }   // ≤ one lap
    if (a1 - a0 < 1e-5f) return out;

    auto pushCubic = [&](Vec2 P0, Vec2 P1, Vec2 P2, Vec2 P3) {
        if (out.empty()) {
            WNode s{}; s.pos = P0; s.hasOut = true; s.hOut = P1; s.hasIn = false; out.push_back(s);
        } else {
            out.back().hOut = P1; out.back().hasOut = true;
        }
        WNode e{}; e.pos = P3; e.hasIn = true; e.hIn = P2; e.hasOut = false; out.push_back(e);
    };
    // Walk forward from a0 to a1, emitting each (wrapped) node-segment's sub-cubic.
    const int sc = fc.segCount();
    auto wrapArc = [&](float a){ if (!fc.closed) return a;
        a = std::fmod(a, total); return a < 0 ? a + total : a; };
    float cur = a0;
    int guard = 0;
    while (cur < a1 - 1e-5f && guard++ < sc + 4) {
        int seg; float u;
        fc.LocOfArc(wrapArc(cur), seg, u);
        WNode A, B; fc.seg(seg, A, B);
        float segLen = FollowCurve::SegLen(A, B, 1.0f);
        if (segLen < 1e-5f) { cur += 1e-4f; continue; }
        // End parameter within THIS segment: either a1 falls inside it, or 1.0.
        float remain = a1 - cur;                      // arc still to cover
        float segRemainArc = FollowCurve::SegLen(A, B, 1.0f) - FollowCurve::SegLen(A, B, u);
        float hi;
        if (remain <= segRemainArc) {
            // a1 is inside this segment: bisect u for (SegLen up to hi) == arc-at-u + remain.
            float targetArc = FollowCurve::SegLen(A, B, u) + remain;
            float lo2 = u, hi2 = 1.0f;
            for (int it = 0; it < 18; ++it) { float mid = 0.5f*(lo2+hi2);
                (FollowCurve::SegLen(A, B, mid) < targetArc) ? lo2 = mid : hi2 = mid; }
            hi = 0.5f*(lo2+hi2);
        } else {
            hi = 1.0f;
        }
        // Sub-cubic of [A,B] restricted to [u,hi].
        Vec2 P0, P1, P2, P3;
        if (u <= 1e-6f && hi >= 1.0f - 1e-6f) {
            P0 = A.pos; P1 = A.hasOut ? A.hOut : A.pos;
            P2 = B.hasIn ? B.hIn : B.pos; P3 = B.pos;
        } else {
            Vec2 r0, r1, r2, r3; FollowCurve::SplitRight(A, B, u, r0, r1, r2, r3);
            WNode RA{ r0, {}, r1, false, true }, RB{ r3, r2, {}, true, false };
            float hi2 = (hi - u) / std::max(1e-5f, 1.0f - u);
            FollowCurve::SplitLeft(RA, RB, std::clamp(hi2, 0.0f, 1.0f), P0, P1, P2, P3);
        }
        pushCubic(P0, P1, P2, P3);
        cur += (hi >= 1.0f - 1e-6f) ? segRemainArc : remain;
    }
    if (!fwd) {
        std::reverse(out.begin(), out.end());
        for (WNode& w : out) { std::swap(w.hIn, w.hOut); std::swap(w.hasIn, w.hasOut); }
    }
    return out;
}

// Follow-curve (Shift), OpenOrienteering-Mapper style. Two phases (see the header):
//   • NOT LOCKED → blue diamond at the nearest curve ENTRY point (within a pickup
//     radius); preview is a STRAIGHT segment last-point → entry. Click places the
//     entry point ON the curve and locks onto it.
//   • LOCKED → project the cursor onto the LOCKED curve (no distance limit); preview
//     traces it from the anchor to the projection. Click freezes the traced piece
//     (exact target nodes) and re-anchors; a click-drag then pulls the new point's
//     OUT handle (its IN handle stays the curve's own — kept automatic).
// Returns true while following (caller suppresses its own placement that frame).
bool Application::UpdateFollowCurve(const std::function<ImVec2(Renderer::Vec2)>& d2s,
                                    Renderer::Vec2 mRaw, float effZoom, bool lpressed,
                                    ImDrawList* dl, bool& committed) {
    committed = false;
    toolState_.followAvail = false;
    toolState_.ClearProvFollow();                 // rebuilt below when following
    if (!ImGui::GetIO().KeyShift) { toolState_.followLocked = false; return false; }
    auto& doc = project_.document;
    const float zoom = std::max(1e-4f, effZoom);
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const ImU32 blue = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    auto blueDiamond = [&](Vec2 w){ ImVec2 c = d2s(w); const float r = 8.0f*gs, th = 2.0f*gs;
        dl->AddQuad(ImVec2(c.x,c.y-r), ImVec2(c.x+r,c.y), ImVec2(c.x,c.y+r), ImVec2(c.x-r,c.y), blue, th); };
    auto outOff = [](const WNode& w){ return w.hasOut ? Vec2{ w.hOut.x-w.pos.x, w.hOut.y-w.pos.y } : Vec2{0,0}; };
    auto inOff  = [](const WNode& w){ return w.hasIn  ? Vec2{ w.hIn.x -w.pos.x, w.hIn.y -w.pos.y } : Vec2{0,0}; };

    // Try to LOCK if not yet: either a curve is within the pickup radius (entry), OR
    // the last placed point already sits EXACTLY on a curve (e.g. snapped there) — in
    // which case Shift starts following that curve straight away (no linking segment).
    const float kPickPx = 24.0f, kOnCurvePx = 2.0f;
    if (!toolState_.followLocked) {
        const float kPickDoc2 = (kPickPx / zoom) * (kPickPx / zoom);
        FollowCurve bestFc; float bestD2 = kPickDoc2; bool found = false;
        int bestSeg = 0; float bestU = 0.0f; Vec2 bestPt{};
        uint64_t bestShape = 0; int bestPart = -1, bestSub = -1;
        // Also test the last placed point against curves so "already on a curve" locks.
        const bool haveLast = !toolState_.points.empty();
        const Vec2 lastPt = haveLast ? toolState_.points.back() : Vec2{};
        float lastBestD2 = (kOnCurvePx / zoom) * (kOnCurvePx / zoom);
        bool lastOn = false; FollowCurve lastFc; float lastArc = 0.0f;
        uint64_t lastShape = 0; int lastPart = -1, lastSub = -1;
        auto scan = [&](const Renderer::Shape& s) {
            if (!s.visible || s.id == toolState_.shapeContinue) return;
            Vec2 po = CurPageOriginOfShape(s.id);
            for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
                const Renderer::Part& part = s.parts[(size_t)pi];
                if (!part.IsCurveLike() && part.path.nodes.empty() && !part.IsParametric()) continue;
                int subs = std::max(1, Renderer::Tessellator::SubpathCount(part));
                for (int sub = 0; sub < subs; ++sub) {
                    FollowCurve fc = FollowCurve::Build(s, part, sub, po);
                    if (!fc.valid()) continue;
                    int seg; float u; Vec2 c;
                    float d2 = fc.Project(mRaw, seg, u, c);
                    if (d2 < bestD2) { bestD2 = d2; found = true; bestFc = fc;
                        bestSeg = seg; bestU = u; bestPt = c;
                        bestShape = s.id; bestPart = pi; bestSub = sub; }
                    if (haveLast) {
                        Vec2 lc; float la = fc.ProjectArc(lastPt, lc);
                        float ld2 = (lc.x-lastPt.x)*(lc.x-lastPt.x) + (lc.y-lastPt.y)*(lc.y-lastPt.y);
                        if (ld2 < lastBestD2) { lastBestD2 = ld2; lastOn = true; lastFc = fc;
                            lastArc = la; lastShape = s.id; lastPart = pi; lastSub = sub; }
                    }
                }
            }
        };
        for (const Renderer::Artboard& ab : doc.artboards)
            for (const Renderer::Shape& s : ab.shapes) scan(s);
        for (const Renderer::Shape& s : doc.looseShapes) scan(s);

        // Last point already on a curve → lock onto it immediately (anchor = there).
        if (lastOn) {
            toolState_.followLocked = true; toolState_.followShape = lastShape;
            toolState_.followPart = lastPart; toolState_.followSub = lastSub;
            toolState_.followAnchorArc = lastArc; toolState_.followCursorArc = lastArc;
            toolState_.followCursorAscending = true;
        } else if (found) {
            // NOT LOCKED yet: blue diamond at the entry point; the PROVISIONAL run is
            // just the entry point (a linking node). The link segment from the last
            // committed point uses that point's OWN handle (the styled/blue preview
            // builds it), so the construction line is NOT a forced straight line.
            toolState_.followAvail = true;
            toolState_.provPoints   = { bestPt };
            toolState_.provTangents = { Vec2{0,0} };
            toolState_.provTangentsIn = { Vec2{0,0} };
            toolState_.provFollowed = { 0 };
            blueDiamond(bestPt);
            if (lpressed) {
                auto& fol = toolState_.followed;
                fol.resize(toolState_.points.size(), 0);
                toolState_.points.push_back(bestPt);
                toolState_.tangents.push_back(Vec2{0,0});
                toolState_.tangentsIn.push_back(Vec2{0,0});
                fol.push_back(0);                  // linking entry node
                toolState_.followLocked = true; toolState_.followShape = bestShape;
                toolState_.followPart = bestPart; toolState_.followSub = bestSub;
                toolState_.followAnchorArc = bestFc.ArcOf(bestSeg, bestU);
                toolState_.followCursorArc = toolState_.followAnchorArc;
                toolState_.followCursorAscending = true;
                committed = true;
            }
            return true;
        } else {
            return false;                          // nothing to follow → normal drawing
        }
    }

    // ── LOCKED: follow the locked curve, no distance limit, in the cursor's dir ──
    Renderer::Shape* s = doc.FindShape(toolState_.followShape);
    if (!s || toolState_.followPart < 0 || toolState_.followPart >= (int)s->parts.size()) {
        toolState_.followLocked = false; return false;
    }
    FollowCurve fc = FollowCurve::Build(*s, s->parts[(size_t)toolState_.followPart],
                                        toolState_.followSub, CurPageOriginOfShape(s->id));
    if (!fc.valid()) { toolState_.followLocked = false; return false; }
    const float total = fc.TotalArc();
    Vec2 c; float rawArc = fc.ProjectArc(mRaw, c);
    // Unwrap the cursor arc against the previous one so a continuous mouse motion is
    // tracked even across a cyclic seam (pick the ±total shift that minimises the
    // jump). For an open curve there's no seam, so this is a no-op.
    float cursorArc = rawArc;
    if (fc.closed && total > 1e-4f) {
        float prev = toolState_.followCursorArc;
        float k = std::round((prev - rawArc) / total);
        cursorArc = rawArc + k * total;
    }
    toolState_.followAvail = true; toolState_.followCursorArc = cursorArc;

    std::vector<WNode> piece = FollowExtract(fc, toolState_.followAnchorArc, cursorArc);
    blueDiamond(c);
    // Publish the traced piece as the PROVISIONAL run (consumed by the previews).
    if (piece.size() >= 2) {
        toolState_.provPoints.clear(); toolState_.provTangents.clear();
        toolState_.provTangentsIn.clear(); toolState_.provFollowed.clear();
        for (size_t i = 0; i < piece.size(); ++i) {
            toolState_.provPoints.push_back(piece[i].pos);
            toolState_.provTangents.push_back(outOff(piece[i]));
            toolState_.provTangentsIn.push_back(inOff(piece[i]));
            toolState_.provFollowed.push_back(1);
        }
    }
    // Click: freeze the traced piece into the gesture and re-anchor at the cursor.
    if (lpressed && piece.size() >= 2) {
        auto& fol = toolState_.followed;
        fol.resize(toolState_.points.size(), 0);
        if (toolState_.points.empty()) {
            toolState_.points.push_back(piece.front().pos);
            toolState_.tangents.push_back(Vec2{0,0});
            toolState_.tangentsIn.push_back(Vec2{0,0});
            fol.push_back(1);
        }
        // The existing last point adopts the piece's first OUT (curve tangent) so the
        // link leaves it along the curve; its IN (set earlier) is preserved.
        toolState_.tangents.back() = outOff(piece.front());
        if (!fol.empty()) fol.back() = 1;
        for (size_t i = 1; i < piece.size(); ++i) {
            toolState_.points.push_back(piece[i].pos);
            toolState_.tangents.push_back(outOff(piece[i]));
            toolState_.tangentsIn.push_back(inOff(piece[i]));
            fol.push_back(1);
        }
        toolState_.followAnchorArc = cursorArc;     // continue from here
        committed = true;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Curve tool (tool.curve) — interactive Bézier authoring in Edit Mode.
//
//  points[i]   = anchor world (display) position.
//  tangents[i] = OUT-handle world OFFSET from the anchor ({0,0} = straight point).
//  draggingTangent = a point was just placed and LMB is still down (pulling its
//  tangent). Click = straight (Vector) point; click-drag = Bézier point.
//  Double-click / Enter finishes (open path); clicking the first anchor closes
//  the curve into a filled area; Esc / RMB cancels.
//  Shift = follow the nearest target curve (blue), copying its exact geometry.
// ─────────────────────────────────────────────────────────────────────────────
void Application::HandleCurveTool(
    EditorState& st,
    const std::function<Vec2(ImVec2)>& s2d,
    const std::function<ImVec2(Vec2)>& d2s,
    float effZoom, bool hovered, ImDrawList* dl) {

    auto& ds = DesignSystem::DesignSystem::Instance();
    ImGuiIO& io = ImGui::GetIO();
    const void* self = &st;
    const float zoom = std::max(0.0001f, effZoom);
    const Vec2 mRaw = s2d(io.MousePos);          // raw cursor (handles use this)

    const bool owns    = toolState_.Active() && toolState_.owner == self;
    const bool foreign = toolState_.Active() && toolState_.owner != self;
    if (foreign) return;

    // Snapped cursor for PLACING points (magnet / Ctrl, active snap mode). The host
    // being authored is excluded so the curve never snaps onto its own new points.
    // Handles still pull from the RAW cursor (`mRaw`), so snapping a point doesn't
    // stop the click-drag tangent. `m` is the placement position used below.
    const uint64_t snapExclude = toolState_.shapeContinue;
    const Vec2 m = CurveSnapPoint(mRaw, zoom, snapExclude);

    const bool lpressed  = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool lreleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool ldouble   = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool enter     = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    const bool escape    = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool rmb       = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // Overlay colours (chrome tokens). The curve tool builds Aligned (mirrored)
    // handles, so colour them like Edit Mode's Aligned handles — visible on the
    // white page (the old cursor-ring colour was white → invisible).
    ImU32 cAccent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    ImU32 cHandle = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_EditHandle_Aligned));

    // ── Not in a gesture: vertex-protect + start / continue ───────────────────
    if (!owns) {
        auto& doc = project_.document;
        // Snapping (magnet on / Ctrl held) now owns vertex magnetism: the new point
        // snaps onto a vertex via the snap system, so the "click to SELECT this
        // vertex" hint is suppressed while snapping (it would fight the snap).
        const bool snapping = snap_.enabled || io.KeyCtrl;
        const float vr = 9.0f;   // screen-px vertex pick / hint radius

        // Find the nearest editable vertex to the cursor (any visible curve-like
        // part), and draw a ring HINT on every vertex within the radius so the
        // user sees clicking will SELECT (not create). Snapping suppresses this and
        // lets the new point land (snapped) even on top of a vertex.
        Renderer::VertRef nearestV{}; float nearestD = vr + 1.0f; bool haveV = false;
        if (!snapping) {
            auto consider = [&](const Renderer::Shape& s, uint64_t sid, int pi,
                                const Renderer::Part& part) {
                if (!part.IsCurveLike()) return;
                Renderer::Vec2 po = CurPageOriginOfShape(sid);
                for (int ni = 0; ni < (int)part.path.nodes.size(); ++ni) {
                    ImVec2 sp = d2s(Renderer::Tessellator::WorldTransform(
                        s, part.path.nodes[(size_t)ni].pos, po));
                    float dpx = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y);
                    if (dpx <= vr) {
                        // Accent (not the white cursor-ring, which is invisible on the
                        // white page) so the "click here to select this point" hint
                        // reads clearly under the Curve tool.
                        ImU32 hint = ImGui::GetColorU32(
                            ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
                        dl->AddCircle(sp, vr + 2.0f, hint, 20, 2.0f);
                    }
                    if (dpx < nearestD) { nearestD = dpx; nearestV = { sid, pi, ni }; haveV = true; }
                }
            };
            for (const auto& ab : doc.artboards)
                for (const Renderer::Shape& s : ab.shapes)
                    if (s.visible) for (int pi = 0; pi < (int)s.parts.size(); ++pi)
                        consider(s, s.id, pi, s.parts[(size_t)pi]);
            for (const Renderer::Shape& s : doc.looseShapes)
                if (s.visible) for (int pi = 0; pi < (int)s.parts.size(); ++pi)
                    consider(s, s.id, pi, s.parts[(size_t)pi]);
        }

        if (hovered) ShowCrosshairCursor();   // "+" only over this canvas (see above)
        if (lpressed) {
            // 1) Click on a protected vertex (no Ctrl) → SELECT it. The NEXT click
            //    away will continue / branch the curve from here (case 2).
            if (haveV) {
                if (editorMode_ != EditorMode::Edit) editorMode_ = EditorMode::Edit;
                doc.VertSelectOnly(nearestV);
                extrudeJustCreated_ = false;
                return;
            }
            // 2) A SELECTED vertex + click away → continue authoring a curve from it,
            //    exactly like drawing a new symbol (preview, click / click+drag,
            //    double-click / Enter to finish) — NOT a single extrude.
            //      • an OPEN path's ENDPOINT → the gesture EXTENDS that path;
            //      • an interior point, or ANY point of a CYCLIC path → the gesture
            //        starts a NEW subpath (strand) of the same shape, as already done
            //        for multi-path curves.
            if (editorMode_ == EditorMode::Edit && doc.HasVertSelection()) {
                Renderer::VertRef av = doc.ActiveVert();
                Renderer::Shape* sh = doc.FindShape(av.shape);
                if (sh && av.part >= 0 && av.part < (int)sh->parts.size()) {
                    Renderer::Part& part = sh->parts[(size_t)av.part];
                    part.EnsurePath();
                    auto& nodes = part.path.nodes;
                    if (av.node >= 0 && av.node < (int)nodes.size()) {
                        // Endpoint of an OPEN path? (start or end of its subpath)
                        int sb = 0, se = (int)nodes.size();
                        int sub = part.path.subOf(av.node);
                        part.path.subRange(sub, sb, se);
                        const bool isStart = (av.node == sb);
                        const bool isEnd   = (av.node == se - 1);
                        const bool endpoint = (!part.path.closed) && (isStart || isEnd);
                        Vec2 po = CurPageOriginOfShape(av.shape);
                        Vec2 seed = Renderer::Tessellator::WorldTransform(
                            *sh, nodes[(size_t)av.node].pos, po);

                        toolState_.Reset();
                        toolState_.gesture = ToolGesture::Bezier;
                        toolState_.owner   = self;
                        toolState_.targetArtboard =
                            (av.shape && doc.ArtboardOfShape(av.shape) >= 0)
                                ? doc.ArtboardOfShape(av.shape)
                                : (doc.ActivePage()
                                       ? doc.ArtboardIndexById(doc.ActivePage()) : -1);
                        toolState_.points     = { seed, m };
                        toolState_.tangents   = { Vec2{0,0}, Vec2{0,0} };
                        toolState_.tangentsIn = { Vec2{0,0}, Vec2{0,0} };
                        toolState_.draggingTangent = true;     // pull the new point's handle
                        toolState_.shapeContinue   = av.shape;
                        toolState_.partContinue    = av.part;
                        // Prepend when continuing the path's START (so the new strand
                        // grows the right way); append otherwise / for a new subpath.
                        toolState_.continueAtStart  = endpoint && isStart && !isEnd;
                        toolState_.continueEndpoint = endpoint;   // merge vs new subpath
                        toolState_.continueNode     = av.node;
                        RecomputeCurveInHandles();
                        return;
                    }
                }
            }
            // 3) Otherwise start a fresh curve (the buffer gesture).
            toolState_.Reset();
            toolState_.gesture = ToolGesture::Bezier;
            toolState_.owner   = self;
            toolState_.targetArtboard = doc.ActivePage()
                ? doc.ArtboardIndexById(doc.ActivePage()) : -1;
            // Shift-start = begin in FOLLOW mode: arm the gesture with NO point yet
            // (the follow handler seeds the first point projected onto the target).
            if (io.KeyShift) {
                toolState_.draggingTangent = false;   // follow points aren't hand-pulled
            } else {
                toolState_.points     = { m };
                toolState_.tangents   = { Vec2{0, 0} };
                toolState_.tangentsIn = { Vec2{0, 0} };
                toolState_.draggingTangent = true;
            }
            // In EDIT MODE a fresh curve stays INSIDE the edited object as a new
            // subpath (a separate strand of the SAME object, with its style) — not a
            // brand-new object. Target its first curve-like part.
            if (editorMode_ == EditorMode::Edit) {
                uint64_t hostId = doc.ActiveId();
                if (!hostId && !doc.Selection().empty()) hostId = doc.Selection().front();
                if (Renderer::Shape* hs = hostId ? doc.FindShape(hostId) : nullptr) {
                    int cp = -1;
                    for (int pi = 0; pi < (int)hs->parts.size(); ++pi)
                        if (hs->parts[(size_t)pi].IsCurveLike()) { cp = pi; break; }
                    if (cp >= 0) {
                        toolState_.shapeContinue   = hostId;
                        toolState_.partContinue    = cp;
                        toolState_.continueEndpoint = false;   // → new subpath strand
                        toolState_.continueNode     = -1;
                        toolState_.targetArtboard   =
                            (doc.ArtboardOfShape(hostId) >= 0)
                                ? doc.ArtboardOfShape(hostId) : toolState_.targetArtboard;
                    }
                }
            }
        }
        // Show where the FIRST point would snap before the gesture even starts.
        DrawSnapIndicatorGlyph(d2s, dl, ds.GetGlobalScale());
        return;
    }

    // ── Cancel (Esc / RMB) ────────────────────────────────────────────────────
    if (escape || rmb) {
        // A styled-curve gesture also ends the infinite symbol placement and
        // restores the tool the user had before picking the symbol.
        if (toolState_.styleActive) { EndPlacement(); }
        else                          toolState_.Reset();
        rmbConsumedByTransform_ = true;            // swallow the context menu
        return;
    }

    // ── Follow-curve (Shift): trace the drawn path along a target curve ───────
    // While Shift is held and a target is under the cursor, UpdateFollowCurve owns
    // this frame: it draws the blue diamond + traced preview and, on a click, freezes
    // the traced piece (exact target nodes). We then skip the normal handle-drag /
    // point-add / rubber-band for the trailing segment (the committed segments below
    // still render). A double-click / Enter still finishes (handled above).
    //
    // EXCEPTION: while the user is DRAGGING a just-placed point's OUT handle (LMB
    // held after a follow click), let the normal handle-drag below run instead — so
    // a click-drag pulls the OUT handle, exactly like a free Bézier point. The
    // follow resumes on release (draggingTangent clears).
    // UpdateFollowCurve fills the PROVISIONAL run (toolState_.prov*) and draws the
    // blue diamond; it does NOT short-circuit the rest, so the transparent styled
    // preview + the rubber-band below still run (they read prov* via BuildCurvePath),
    // keeping the live preview visible the whole time the curve is followed.
    bool followCommitted = false;
    const bool following = UpdateFollowCurve(d2s, mRaw, zoom, lpressed && !ldouble,
                                             dl, followCommitted);
    // While following, a click is consumed by the follow handler — don't also let the
    // normal point-add below run on the same press.
    const bool suppressPointAdd = following;

    // ── Pulling the just-placed point's OUTER out-tangent ─────────────────────
    // The user drags the OUTER handle (the one pointing the way the curve goes).
    // The INNER handle (toward the previous point) is then derived to keep the
    // curve smooth (OpenOrienteering Mapper style) — see RecomputeCurveInHandles.
    // Suspended while following (the follow run owns the trailing segment).
    if (!following && toolState_.draggingTangent && !toolState_.points.empty()) {
        // The handle pulls from the RAW cursor — snapping moves the anchor, not the
        // tangent, so a snapped point can still be click-dragged into a Bézier point.
        toolState_.tangents.back() = { mRaw.x - toolState_.points.back().x,
                                       mRaw.y - toolState_.points.back().y };
        RecomputeCurveInHandles();
        if (lreleased) toolState_.draggingTangent = false;
    }

    // ── Finish (double-click / Enter) ─────────────────────────────────────────
    // An AREA symbol (styleClosed) finishes CLOSED on Enter/double-click; a normal
    // curve / line symbol finishes open.
    if ((ldouble || enter) && toolState_.points.size() >= 2) {
        FinishCurveGesture(/*closed=*/toolState_.styleClosed);
        return;
    }

    // ── Snap-to-close detection ───────────────────────────────────────────────
    // Within the close zone of the FIRST anchor (≥3 points) the in-progress
    // segment connects back; Ctrl suppresses linking so the point lands free.
    const float kCloseZonePx = 9.0f;
    bool snapClose = false;
    if (toolState_.points.size() >= 3 && !io.KeyCtrl && !following) {
        ImVec2 a = d2s(toolState_.points.front());
        snapClose = std::hypot(io.MousePos.x - a.x, io.MousePos.y - a.y) <= kCloseZonePx;
    }

    // ── Add a point on a fresh press ──────────────────────────────────────────
    // (ldouble also raises lpressed; the finish above already returned, so a
    // genuine single press reaches here.) Skipped while following — the follow
    // handler already consumed the click (froze the traced piece).
    if (lpressed && !ldouble && !suppressPointAdd) {
        // Click in the first-anchor close zone (no Ctrl) closes the curve.
        if (snapClose) { FinishCurveGesture(/*closed=*/true); return; }
        toolState_.points.push_back(m);
        toolState_.tangents.push_back(Vec2{0, 0});
        toolState_.tangentsIn.push_back(Vec2{0, 0});
        toolState_.draggingTangent = true;
        RecomputeCurveInHandles();
    }

    // ── Live styled preview (the actual symbol look) under the blue guide ──────
    DrawStyledCurvePreview(d2s, std::max(0.0001f, zoom), m, snapClose);

    // ── Preview (blue rubber-band guide) ──────────────────────────────────────
    const size_t np = toolState_.points.size();
    auto bez = [&](Vec2 p0, Vec2 t0out, Vec2 p1, Vec2 t1in) {
        // Cubic from p0 (out = p0+t0out) to p1 (in = p1+t1in). Straight if no tangents.
        Vec2 c0{ p0.x + t0out.x, p0.y + t0out.y };
        Vec2 c1{ p1.x + t1in.x,  p1.y + t1in.y };
        const int N = 24;
        ImVec2 prev = d2s(p0);
        for (int i = 1; i <= N; ++i) {
            float u = (float)i / (float)N, v = 1.0f - u;
            float b0=v*v*v, b1=3*v*v*u, b2=3*v*u*u, b3=u*u*u;
            Vec2 pt{ b0*p0.x + b1*c0.x + b2*c1.x + b3*p1.x,
                     b0*p0.y + b1*c0.y + b2*c1.y + b3*p1.y };
            ImVec2 cur = d2s(pt);
            dl->AddLine(prev, cur, cAccent, 2.0f);
            prev = cur;
        }
    };
    // IN handle of point i (toward the previous point); guards short vectors.
    auto inH = [&](size_t i) -> Vec2 {
        return (i < toolState_.tangentsIn.size()) ? toolState_.tangentsIn[i] : Vec2{0,0};
    };
    for (size_t i = 0; i + 1 < np; ++i) {
        Vec2 out_i{ toolState_.tangents[i].x, toolState_.tangents[i].y };
        Vec2 in_j = inH(i + 1);          // asymmetric (OOMapper), not a mirror
        bez(toolState_.points[i], out_i, toolState_.points[i+1], in_j);
    }
    if (following && !toolState_.provPoints.empty()) {
        // FOLLOW: the trailing run is the provisional curve-aligned path. Draw the
        // link from the last committed point (using ITS out handle) to the first
        // provisional node, then the provisional run itself.
        auto provIn = [&](size_t i){ return (i < toolState_.provTangentsIn.size())
                                     ? toolState_.provTangentsIn[i] : Vec2{0,0}; };
        if (np >= 1)
            bez(toolState_.points.back(), toolState_.tangents.back(),
                toolState_.provPoints.front(), provIn(0));
        for (size_t i = 0; i + 1 < toolState_.provPoints.size(); ++i)
            bez(toolState_.provPoints[i], toolState_.provTangents[i],
                toolState_.provPoints[i+1], provIn(i+1));
    } else if (np >= 1) {
        // Rubber segment from the last anchor: to the FIRST point when snapping closed
        // (the loop connects), otherwise to the mouse.
        Vec2 out_last{ toolState_.tangents.back().x, toolState_.tangents.back().y };
        if (snapClose) {
            Vec2 in_first = inH(0);
            bez(toolState_.points.back(), out_last, toolState_.points.front(), in_first);
        } else {
            bez(toolState_.points.back(), out_last, m, Vec2{0, 0});
        }
    }
    // Anchor dots + tangent handles (OUT = dragged, IN = auto, drawn separately so
    // the asymmetric lengths are visible).
    for (size_t i = 0; i < np; ++i) {
        ImVec2 a = d2s(toolState_.points[i]);
        dl->AddCircleFilled(a, 3.5f, cAccent);
        Vec2 tOut = toolState_.tangents[i];
        Vec2 tIn  = inH(i);
        if (std::hypot(tOut.x, tOut.y) > 1e-3f) {
            ImVec2 hOut = d2s({ toolState_.points[i].x + tOut.x,
                                toolState_.points[i].y + tOut.y });
            dl->AddLine(a, hOut, cHandle, 1.0f);
            dl->AddCircleFilled(hOut, 2.5f, cHandle);
        }
        if (std::hypot(tIn.x, tIn.y) > 1e-3f) {
            ImVec2 hIn = d2s({ toolState_.points[i].x + tIn.x,
                               toolState_.points[i].y + tIn.y });
            dl->AddLine(a, hIn, cHandle, 1.0f);
            dl->AddCircleFilled(hIn, 2.5f, cHandle);
        }
    }
    // Highlight the closeable first anchor — emphasised when the cursor is in the
    // close zone (snapClose), so the user sees the curve will connect.
    if (np >= 3) {
        ImVec2 a = d2s(toolState_.points.front());
        dl->AddCircle(a, snapClose ? 8.0f : 6.0f, cAccent, 16, snapClose ? 2.5f : 1.5f);
    }
    // The snap glyph (orange, mode-shaped) at the snapped cursor while authoring.
    DrawSnapIndicatorGlyph(d2s, dl, ds.GetGlobalScale());
    if (hovered) ShowCrosshairCursor();   // "+" only over this canvas (see above)
    (void)zoom;
}

// Recompute the auto IN handles from the points + dragged OUT handles, the way
// OpenOrienteering Mapper builds a curve as you drag: the user pulls the OUTER
// handle (`tangents[i]`, pointing the way the curve continues); the INNER handle
// (`tangentsIn[i]`, pointing back toward the previous point) is generated so the
// join stays smooth WITHOUT being a mere mirror.
//
//   • A point with NO dragged OUT handle is a straight (Vector) corner: no IN.
//   • A point WITH a dragged OUT handle gets an ALIGNED IN handle — collinear and
//     opposite to OUT — but its LENGTH is set from the chord to the PREVIOUS point
//     (OOMapper uses ~1/3 of that chord), so a long outer pull doesn't blow the
//     inner side out of shape. The previous point also receives a complementary
//     OUT handle along the same chord when it had none, so its segment is smooth.
void Application::RecomputeCurveInHandles() {
    auto& pts = toolState_.points;
    auto& out = toolState_.tangents;
    auto& in  = toolState_.tangentsIn;
    auto& fol = toolState_.followed;
    fol.resize(pts.size(), 0);                // keep the flag parallel to points
    auto isFollowed = [&](size_t i){ return i < fol.size() && fol[i]; };
    // Reset only the AUTO (non-followed) IN handles; followed points keep their
    // exact copied handles (in AND out) untouched.
    for (size_t i = 0; i < pts.size(); ++i)
        if (!isFollowed(i)) { if (i < in.size()) in[i] = Vec2{0, 0}; }
    auto len = [](Vec2 v){ return std::hypot(v.x, v.y); };
    const float kChordFrac = 1.0f / 3.0f;     // OOMapper's smooth-handle fraction
    for (size_t i = 1; i < pts.size(); ++i) {
        if (isFollowed(i)) continue;          // exact copied handles — don't touch
        Vec2 oi = out[i];
        if (len(oi) <= 1e-3f) continue;       // straight point → no inner handle
        // Chord back to the previous anchor.
        Vec2 chord{ pts[i-1].x - pts[i].x, pts[i-1].y - pts[i].y };
        float chordLen = len(chord);
        if (chordLen <= 1e-4f) continue;
        // IN direction = aligned-opposite to OUT; length from the chord.
        float ol = len(oi);
        Vec2 dirIn{ -oi.x / ol, -oi.y / ol };
        float inLen = chordLen * kChordFrac;
        in[i] = { dirIn.x * inLen, dirIn.y * inLen };
        // Give the PREVIOUS point a complementary OUT handle (toward this point)
        // when it had none, so the segment leaves it smoothly too — unless it is a
        // followed point (its OUT is exact and must be preserved).
        if (!isFollowed(i-1) && len(out[i-1]) <= 1e-3f) {
            Vec2 fwd{ -chord.x / chordLen, -chord.y / chordLen };   // prev → cur
            float pl = chordLen * kChordFrac;
            out[i-1] = { fwd.x * pl, fwd.y * pl };
        }
    }
}

// Build the in-progress curve into a real Bézier Shape and add it. Nodes are
// authored in absolute display/world coords; translate = −pageDisplayOrigin so
// the curve lands exactly where it was drawn (mirrors the Shift+A placement).
// Build the Bézier path for the current gesture (shared by finish + preview).
Renderer::Path Application::BuildCurvePath(bool closed,
                                          const Renderer::Vec2* provisional) const {
    Renderer::Path path;
    auto len = [](Vec2 v){ return std::hypot(v.x, v.y); };
    // tOut / tIn are the OUT / IN handle OFFSETS (asymmetric, OOMapper-style).
    auto pushNode = [&](Vec2 p, Vec2 tOut, Vec2 tIn) {
        Renderer::Node nd(p);
        bool hasO = len(tOut) > 1e-3f, hasI = len(tIn) > 1e-3f;
        if (hasO || hasI) {
            nd.hasOut = hasO; nd.hasIn = hasI;
            if (hasO) nd.hOut = { p.x + tOut.x, p.y + tOut.y };
            if (hasI) nd.hIn  = { p.x + tIn.x,  p.y + tIn.y };
            // Aligned (collinear in/out) by construction; but a FOLLOWED node copies
            // a target curve's handles which may be free (non-collinear) — detect
            // that and mark it Free so the geometry is preserved exactly on edit.
            bool freeHandles = false;
            if (hasO && hasI) {
                float lo = len(tOut), li = len(tIn);
                if (lo > 1e-4f && li > 1e-4f) {
                    float dot = (tOut.x*tIn.x + tOut.y*tIn.y) / (lo*li);
                    freeHandles = dot > -0.985f;   // not (close to) opposite → free
                }
            }
            nd.mode = freeHandles ? Renderer::HandleMode::Free
                                  : Renderer::HandleMode::Aligned;
        } else {
            nd.hasIn = nd.hasOut = false;
            nd.mode = Renderer::HandleMode::Vector;
        }
        path.nodes.push_back(nd);
    };
    const size_t n = toolState_.points.size();
    for (size_t i = 0; i < n; ++i) {
        Vec2 tIn = (i < toolState_.tangentsIn.size()) ? toolState_.tangentsIn[i] : Vec2{0,0};
        pushNode(toolState_.points[i], toolState_.tangents[i], tIn);
    }
    // FOLLOW provisional run (Shift): append the not-yet-committed traced nodes so
    // every preview (and any provisional build) shows the exact curve-aligned path,
    // keeping the committed last point's handle. Takes priority over a single
    // provisional mouse point (they aren't combined — follow defines the trailing run).
    if (!toolState_.provPoints.empty()) {
        for (size_t i = 0; i < toolState_.provPoints.size(); ++i) {
            Vec2 tOut = (i < toolState_.provTangents.size()) ? toolState_.provTangents[i] : Vec2{0,0};
            Vec2 tIn  = (i < toolState_.provTangentsIn.size()) ? toolState_.provTangentsIn[i] : Vec2{0,0};
            pushNode(toolState_.provPoints[i], tOut, tIn);
        }
    } else if (provisional) {
        pushNode(*provisional, Vec2{0, 0}, Vec2{0, 0});
    }
    path.closed = closed;
    return path;
}

// Snap the curve cursor to document geometry when snapping is on (magnet enabled
// OR Ctrl held), reusing the active snap mode + the same ComputeSnap search the
// transforms use. Excludes `exclude` (the gesture's host shape) so the curve never
// snaps onto its own freshly-placed points. Publishes snapIndicator_ so the orange
// glyph shows. Grid/Increment also snap (the grid is everywhere). Returns `world`
// unchanged when no snap applies, so click/drag for free handles still works.
Renderer::Vec2 Application::CurveSnapPoint(Renderer::Vec2 world, float effZoom,
                                           uint64_t exclude) {
    snapIndicator_ = SnapResult{};
    // Same gate as the transforms, minus the per-transform Affect toggles (which
    // don't apply to authoring): magnet on, or Ctrl held this frame.
    const bool on = snap_.enabled || ImGui::GetIO().KeyCtrl;
    if (!on) return world;
    std::vector<uint64_t> excl;
    if (exclude) excl.push_back(exclude);
    SnapResult sr = ComputeSnap(world, effZoom, excl);
    if (sr.snapped) {
        if (sr.showMark) { snapIndicator_.snapped = true;
                           snapIndicator_.showMark = true; snapIndicator_.pos = sr.pos; }
        return sr.pos;
    }
    return world;
}

void Application::FinishCurveGesture(bool closed) {
    using K = Renderer::ShapeKind;
    auto& doc = project_.document;
    if (toolState_.points.size() < 2) { toolState_.Reset(); return; }

    // ── Continuation: the gesture was started from a selected vertex of an EXISTING
    // shape → fold the drawn strand into that shape's part instead of making a new
    // object.
    //   • OPEN-path ENDPOINT  → MERGE: the seed node IS the existing endpoint (one
    //     unique vertex, no duplicate), and the rest extends the SAME subpath.
    //   • interior / cyclic point → a new subpath (multi-path branch). [Étape B will
    //     turn this into a true junction node; for now it's a separate strand.]
    if (toolState_.shapeContinue) {
        Renderer::Shape* host = doc.FindShape(toolState_.shapeContinue);
        if (host && toolState_.partContinue >= 0 &&
            toolState_.partContinue < (int)host->parts.size()) {
            Renderer::Part& part = host->parts[(size_t)toolState_.partContinue];
            part.EnsurePath();
            const Vec2 hostPo = CurPageOriginOfShape(toolState_.shapeContinue);
            Renderer::Path drawn = BuildCurvePath(false, nullptr);   // world coords
            auto toLocal = [&](Vec2 w){
                return Renderer::Tessellator::InverseTransform(*host, w, hostPo);
            };
            // Convert all drawn nodes to host-local once.
            std::vector<Renderer::Node> ln;
            ln.reserve(drawn.nodes.size());
            for (Renderer::Node nd : drawn.nodes) {
                nd.pos = toLocal(nd.pos);
                if (nd.hasIn)  nd.hIn  = toLocal(nd.hIn);
                if (nd.hasOut) nd.hOut = toLocal(nd.hOut);
                ln.push_back(nd);
            }

            int lastSel = -1;
            auto& nodes = part.path.nodes;
            if (toolState_.continueEndpoint && !ln.empty() &&
                toolState_.continueNode >= 0 && toolState_.continueNode < (int)nodes.size()) {
                // ── MERGE into the existing subpath (single shared vertex) ────────
                // ln[0] is the seed = the existing endpoint. Transfer its authored
                // handle onto that endpoint so the join is smooth, then splice the
                // REST (ln[1..]) contiguously into the subpath.
                const int ep = toolState_.continueNode;
                if (toolState_.continueAtStart) {
                    // Extending the START: the strand grows OUTWARD from node ep,
                    // so it must be PREPENDED in reverse, with in/out handles
                    // swapped (path traversal direction flips for these nodes).
                    // Endpoint's NEW handle toward the strand = its hIn (it now has
                    // a predecessor). Use ln[0].hOut (toward ln[1]) as that hIn.
                    Renderer::Node& epn = nodes[(size_t)ep];
                    if (ln[0].hasOut) { epn.hIn = ln[0].hOut; epn.hasIn = true;
                                        epn.mode = Renderer::HandleMode::Aligned; }
                    // Insert ln[k..1] at `ep` (deepest first) so the flat order reads
                    // ln[k] … ln[1] ep — i.e. the subpath start grows outward.
                    for (int i = (int)ln.size() - 1; i >= 1; --i) {
                        Renderer::Node nd = ln[(size_t)i];
                        std::swap(nd.hIn, nd.hOut);
                        std::swap(nd.hasIn, nd.hasOut);
                        nodes.insert(nodes.begin() + ep, nd);
                        part.path.OnNodeInserted(ep);  // boundary stays → joins subpath
                    }
                    lastSel = ep;     // the new outermost start node landed at `ep`
                } else {
                    // Extending the END: append ln[1..] right after node ep.
                    Renderer::Node& epn = nodes[(size_t)ep];
                    if (ln[0].hasOut) { epn.hOut = ln[0].hOut; epn.hasOut = true;
                                        epn.mode = Renderer::HandleMode::Aligned; }
                    int at = ep + 1;
                    for (size_t i = 1; i < ln.size(); ++i) {
                        nodes.insert(nodes.begin() + at, ln[i]);
                        part.path.OnNodeInsertedInclusive(at);  // joins THIS subpath
                        ++at;
                    }
                    lastSel = at - 1;
                }
            } else {
                // ── New subpath = a real JUNCTION BRANCH ─────────────────────────
                // The branch's first node SHARES the source vertex: same position +
                // a common junctionId, so edit mode shows ONE vertex carrying all the
                // branches' handles (≥3), and the two coincident nodes move together.
                // The tessellator still strokes each subpath, so it renders as one
                // continuous multi-path curve; the join fill covers the seam.
                int base = (int)nodes.size();
                if (!ln.empty() && toolState_.continueNode >= 0 &&
                    toolState_.continueNode < (int)nodes.size()) {
                    Renderer::Node& src = nodes[(size_t)toolState_.continueNode];
                    uint32_t jid = src.junctionId;
                    if (jid == 0) { jid = AllocJunctionId(part); src.junctionId = jid; }
                    ln[0].pos = src.pos;            // weld the branch start onto the vertex
                    ln[0].junctionId = jid;
                    // The branch leaves the junction along ln[0]'s OUT handle; keep its
                    // IN empty (it's an endpoint of the new strand on the junction side).
                    ln[0].hasIn = false;
                }
                for (const Renderer::Node& nd : ln) nodes.push_back(nd);
                if (base > 0) part.path.SplitAt(base);
                lastSel = (int)nodes.size() - 1;
            }
            MarkUndoLabel("Extend curve");
            if (lastSel >= 0 && lastSel < (int)nodes.size())
                doc.VertSelectOnly(Renderer::VertRef{ toolState_.shapeContinue,
                    toolState_.partContinue, lastSel });
            project_.dirty = true;
        }
        toolState_.Reset();
        return;
    }

    Renderer::Shape s = MakeShape(K::Curve, Renderer::PartType::Curve,
                                  Renderer::SplineType::Bezier);
    s.name = closed ? "Curve Area" : "Curve";
    s.MainPart().path = BuildCurvePath(closed, nullptr);
    if (closed) { s.MainPart().fill.enabled = true; s.MainPart().stroke.enabled = true; }
    else        { s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
    ApplyDefaultColors(s);   // plain curve uses the menu-bar default fill/stroke

    // Symbol-styled curve: copy the symbol's paint/decor/marks + identity onto the
    // drawn geometry, then re-arm placement so the user can draw the next one.
    const bool styled = toolState_.styleActive;
    Renderer::Shape tpl = toolState_.styleTemplate;   // copy before Reset
    Renderer::Shape tplPreview = toolState_.stylePreview;
    bool tplHasPreview = toolState_.styleHasPreview;
    bool styleLoose = toolState_.styleLoose;
    uint64_t styleColl = toolState_.styleColl;
    // The symbol's INTRINSIC kind (area vs line) — used to re-arm the next instance
    // with the SAME behaviour, regardless of how THIS curve was closed. (Closing a
    // line by clicking its first point must NOT make the next one an area.)
    bool styleArea = toolState_.styleClosed;
    if (styled) ApplySymbolStyle(s, tpl, closed);

    CenterOrigin(s);
    const int ab = toolState_.targetArtboard;
    Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Vec2{0, 0};
    s.transform.translate = { -dispPo.x, -dispPo.y };  // nodes already absolute
    if (styled) { s.collectionId = styleColl; }
    MarkUndoLabel(styled ? ("Add " + s.name) : (closed ? "Add curve area" : "Add curve"));
    AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
    toolState_.Reset();
    // Re-arm the same symbol so placement is "infinite" (Esc/RMB ends it). The
    // re-arm cleared the active tool already; just restore the placement state.
    if (styled) {
        RequestPlacementBaked(tpl, styleLoose, styleColl,
            styleArea ? Modules::ModuleHost::PlaceMode::DrawArea
                      : Modules::ModuleHost::PlaceMode::DrawLine);
        if (tplHasPreview) SetPlacementPreview(tplPreview);
    }
}

// Live styled preview while drawing a curve/area. Renders the ACTUAL look (core
// style or IOF symbol) under the blue rubber-band guide:
//   • committed segments → SOLID;
//   • the in-progress (last) segment → TRANSLUCENT;
//   • a surface → the provisional CLOSED area filled SOLID, only its trailing
//     stroke translucent.
void Application::DrawStyledCurvePreview(const std::function<ImVec2(Vec2)>& d2s,
                                         float effZoom, Vec2 mouse, bool snapClose) {
    if (toolState_.points.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float alpha = DesignSystem::DesignSystem::Instance()
                            .GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);
    const bool styled = toolState_.styleActive;
    // Only show the FILLED area preview once there are enough committed points to
    // form one (≥2, so the provisional mouse makes a triangle+). With 0–1 points a
    // closed loop would just be a degenerate line connecting both ends at the
    // cursor — show the plain open in-progress segment instead.
    const bool area = styled && toolState_.styleClosed && toolState_.points.size() >= 2;

    // Blit a shape through the PROCEDURAL offscreen renderer as a transparent texture
    // at a global alpha. This shows the FULL look (fill + motif + decorators), GPU-fast
    // at any density — no per-frame CPU pattern bake (the legacy O(steps²) grid froze
    // large patterned areas). The texture auto-frames to the shape's world bbox at
    // padFrac 0, so it spans exactly [bbMin,bbMax] → blit at d2s(bbMin)..d2s(bbMax),
    // pixel-aligned with the committed object. Re-rendered each frame the geometry
    // changes (cheap, cached when the mouse is still). The size is bucketed up so the
    // offscreen target isn't reallocated every frame as the area grows.
    auto blitTex = [&](const Renderer::Shape& sh, float a, uint64_t slotKey) {
        Renderer::Vec2 bmn, bmx;
        if (!Renderer::Tessellator::WorldBounds(sh, 1.0f, bmn, bmx, {0,0})) return;
        // WorldBounds frames the CONSTRUCTION line only. A centred stroke spills half
        // its width outside that contour, and periodic decorators/line marks reach
        // further still — without padding, the texture clips the outer half of the
        // stroke and the decorators. Grow the bbox by the worst-case outward reach
        // (in doc-units, matching WorldBounds' zoom=1 space) so exactFit still maps
        // 1:1 onto the padded box we blit to.
        // Stroke width / decor sizes are in the shape's LOCAL units; the bbox is in
        // WORLD units, so scale the outward reach by the shape's world scale (a
        // continued host can be non-unit-scaled). Use the larger axis to be safe.
        const float sScale = std::max(std::fabs(sh.transform.scale.x),
                                      std::fabs(sh.transform.scale.y));
        float pad = 0.0f;
        for (const Renderer::Part& pp : sh.parts) {
            if (!pp.stroke.enabled) continue;
            float reach = pp.stroke.width * 0.5f;                    // centred stroke half
            if (pp.stroke.decor != Renderer::LineDecor::None)
                reach += pp.stroke.decorSize;                        // glyph lateral reach
            for (const Renderer::LineMark& mk : pp.marks)
                reach = std::max(reach, pp.stroke.width * 0.5f + mk.size);
            pad = std::max(pad, reach * sScale);
        }
        bmn.x -= pad; bmn.y -= pad; bmx.x += pad; bmx.y += pad;
        float gw = std::max(0.01f, bmx.x - bmn.x), gh = std::max(0.01f, bmx.y - bmn.y);
        // Bucket the SCALE (px per doc-unit), shared by both axes, so the texture size
        // keeps the bbox ratio EXACTLY (wpx/hpx == gw/gh) → exactFit maps it 1:1 onto
        // the bbox with no letterbox/offset, and the target isn't reallocated every
        // frame as the area grows. The scale only steps in ~1.25× jumps.
        float ppuF = effZoom;
        float ppu = 1.0f; while (ppu < ppuF) ppu *= 1.25f;
        ppu = std::min(ppu, 4096.0f / std::max(gw, gh));   // cap target dimension
        int wpx = std::max(8, (int)std::lround(gw * ppu));
        int hpx = std::max(8, (int)std::lround(gh * ppu));
        // Frame the texture to the PADDED bounds (bmn..bmx) — the SAME rect we blit
        // to — so the rendered content (incl. the stroke that spills past the
        // construction line) fills the texture edge-to-edge: no offset, no crop.
        Renderer::Vec2 fmin{ bmn.x, bmn.y }, fmax{ bmx.x, bmx.y };
        // The frame bounds must enter the hash (the texture content depends on them).
        uint64_t fhash = 0; auto mixf = [&](float v){ uint32_t u; std::memcpy(&u,&v,4);
            fhash = fhash * 1099511628211ull ^ u; };
        mixf(bmn.x); mixf(bmn.y); mixf(bmx.x); mixf(bmx.y);
        uint64_t chash = Renderer::Tessellator::HashShape(sh, {0,0})
                       ^ ((uint64_t)wpx << 8) ^ ((uint64_t)hpx << 24) ^ fhash;
        ImTextureID tex = RenderGlyphTexture(slotKey, chash, { sh }, wpx, hpx,
                                             /*padFrac=*/0.0f, /*transparent=*/true,
                                             /*exactFit=*/true, &fmin, &fmax);
        if (!tex) return;
        ImVec2 p0 = d2s({ bmn.x, bmn.y }), p1 = d2s({ bmx.x, bmx.y });
        dl->AddImage(tex, ImVec2(std::min(p0.x,p1.x), std::min(p0.y,p1.y)),
                          ImVec2(std::max(p0.x,p1.x), std::max(p0.y,p1.y)),
                     ImVec2(0,0), ImVec2(1,1), ImGui::GetColorU32(ImVec4(1,1,1, a)));
    };
    // When continuing an existing curve, preview it with THAT curve's look. We keep
    // the preview shape in the SAME identity/world frame as the plain-curve preview
    // (which renders correctly) — NOT a clone of the host transform, which mismatched
    // the curve-flattening between WorldBounds(zoom=1) and the texture render and so
    // distorted the preview on bends. We copy the host part's PAINT and pre-scale its
    // stroke/decor sizes by the host's world scale so the thickness matches on screen.
    const Renderer::Shape* contHost = nullptr;
    int contPart = -1;
    if (!styled && toolState_.shapeContinue) {
        contHost = project_.document.FindShape(toolState_.shapeContinue);
        contPart = toolState_.partContinue;
    }
    float contScale = 1.0f;
    if (contHost) contScale = 0.5f * (std::fabs(contHost->transform.scale.x) +
                                      std::fabs(contHost->transform.scale.y));
    // Build a styled shape from a path (nodes already in world/display coords).
    auto styledShape = [&](const Renderer::Path& path, bool closed) {
        if (contHost && contPart >= 0 && contPart < (int)contHost->parts.size()) {
            Renderer::Shape s = MakeShape(Renderer::ShapeKind::Curve,
                                          Renderer::PartType::Curve,
                                          Renderer::SplineType::Bezier);
            Renderer::Part keep = contHost->parts[(size_t)contPart];
            keep.marks.clear();                      // preview carries no manual marks
            keep.path = path;                        // world coords (identity transform)
            keep.path.closed = closed;
            keep.kind = s.MainPart().kind; keep.type = s.MainPart().type;
            keep.spline = s.MainPart().spline;
            // Style dims are in the host's LOCAL units → bring them to world (screen)
            // units by the host's world scale so the preview thickness/decor match.
            keep.stroke.width        *= contScale;
            keep.stroke.decorSpacing *= contScale;
            keep.stroke.decorSize    *= contScale;
            keep.stroke.decorThickness *= contScale;
            for (Renderer::FillLayer& fl : keep.fillLayers) {
                fl.spacing *= contScale; fl.size *= contScale;
            }
            s.parts.clear();
            s.parts.push_back(std::move(keep));
            return s;
        }
        Renderer::Shape s = MakeShape(Renderer::ShapeKind::Curve,
                                      Renderer::PartType::Curve,
                                      Renderer::SplineType::Bezier);
        s.MainPart().path = path;
        if (closed) { s.MainPart().fill.enabled = true; s.MainPart().stroke.enabled = true; }
        else        { s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
        if (styled) ApplySymbolStyle(s, toolState_.styleTemplate, closed);
        else        ApplyDefaultColors(s);   // plain new curve → menu-bar default colours
        return s;
    };

    if (area) {
        // Surface: the provisional closed area with its FULL look (fill + motif +
        // outline + decorators), shown semi-transparent at the preview alpha.
        blitTex(styledShape(BuildCurvePath(true, &mouse), true), alpha, 0xA0E7u);
        return;
    }

    // Line / plain curve: the provisional run (incl. the mouse) at the preview alpha.
    Renderer::Path prov = BuildCurvePath(snapClose, snapClose ? nullptr : &mouse);
    blitTex(styledShape(prov, snapClose), alpha, 0xA0E8u);
}

// Apply a symbol's full look to a freshly drawn curve: the drawn PATH is kept and
// replicated once per template part, each carrying that part's paint/stroke/decor/
// fill-layers — so a multi-part symbol (e.g. a surface + its outline) follows the
// drawn contour exactly. Stroke flags are taken VERBATIM (forcing a stroke on a
// surface part painted a spurious black outline). Identity is copied too.
void Application::ApplySymbolStyle(Renderer::Shape& target,
                                   const Renderer::Shape& tpl, bool closed) {
    if (tpl.parts.empty() || target.parts.empty()) return;
    const Renderer::Part drawn = target.parts.front();   // the drawn geometry
    target.parts.clear();
    for (const Renderer::Part& src : tpl.parts) {
        Renderer::Part p = drawn;          // same path/nodes as drawn
        p.fill       = src.fill;
        p.stroke     = src.stroke;         // verbatim (may be disabled)
        p.fillLayers = src.fillLayers;
        p.openFillStraight = src.openFillStraight;   // open-fill close mode
        p.marks.clear();
        p.path.closed = closed;
        // Parametric template parts (Rectangle/Ellipse surfaces) become the drawn
        // path; ensure the part type matches the drawn curve so it tessellates.
        p.kind = drawn.kind; p.type = drawn.type; p.spline = drawn.spline;
        target.parts.push_back(std::move(p));
    }
    target.name        = tpl.name;
    target.isomCode    = tpl.isomCode;
    target.allowCapEdit= tpl.allowCapEdit;
    target.lockScale   = tpl.lockScale;
    target.lockRotation= tpl.lockRotation;
}

// A tiny WHITE-card thumbnail of the symbol just below-right of the cursor, so the
// user sees which symbol they are drawing. Rendered at a FIXED resolution with a
// stable cache key (no per-frame GPU realloc). No margin / rounding / border — just
// the preview image. `shape` should already be a compact preview (short line
// sample / small swatch); the preview fills the box (padFrac 0).
void Application::DrawPlacementMiniGhost(const Renderer::Shape& shape, ImVec2 mp,
                                         float effZoom) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float box = 24.0f * ds.GetGlobalScale();   // mini swatch size (px)
    const ImVec2 o(mp.x + 16.0f, mp.y + 16.0f);      // bottom-right of the crosshair
    const ImVec2 omax(o.x + box, o.y + box);

    Renderer::Vec2 bmn, bmx;
    if (!Renderer::Tessellator::WorldBounds(shape, 1.0f, bmn, bmx, {0,0})) return;
    constexpr int kMiniPx = 64;                       // fixed res → stable key
    uint64_t key = 0x6A06u;                           // single reused mini-ghost slot
    uint64_t chash = Renderer::Tessellator::HashShape(shape, {0,0});
    std::vector<Renderer::Shape> shapes = { shape };
    // Transparent (like the object / line-mark previews) so it overlays the canvas
    // as a ghost; fill the box (no margin), no border. Tinted to the preview alpha.
    const float alpha = ds.GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);
    ImTextureID tex = RenderGlyphTexture(key, chash, shapes, kMiniPx, kMiniPx,
                                         /*padFrac=*/0.0f, /*transparent=*/true);
    if (tex) fg->AddImage(tex, o, omax, ImVec2(0,0), ImVec2(1,1),
                          ImGui::GetColorU32(ImVec4(1,1,1, alpha)));
    (void)effZoom;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Line-mark tool helpers — which symbols accept which mark, and the spec mm
//  preset for each (size / thickness / OM / gap). Sizes are in millimetres and
//  scaled by the module's symbolScale at placement time.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
// ISOM code ×10 (isomCode): 1010 Contour, 1020 Index contour, 1030 Form line;
// 5131/5132/5140/5150 walls; 5160/5170/5180 fences; 5280/5290 prominent lines;
// 5100 power line, 5110 major power line.

// Is a crossing point (519) allowed on this symbol? Used by the SYMBOL placement
// path (crossing is no longer a tool kind) — walls, fences, prominent lines.
bool CrossingAllowedOn(int isomCode) {
    return (isomCode >= 5131 && isomCode <= 5150) ||   // walls
           (isomCode >= 5160 && isomCode <= 5180) ||   // fences
           isomCode == 5280 || isomCode == 5290;       // prominent lines
}

// A symbol that takes a DASH ANCHOR (a phase pin): the dashed / patterned line
// features — form line, ruined walls/fences, paths, ditches, vegetation borders,
// and the regular-pattern walls/fences/prominent lines.
bool DashAnchorAllowedOn(int isomCode) {
    switch (isomCode) {
        case 1030:                       // 103 form line (dashed)
        case 1051: case 1052: case 1060: // earth wall / retaining / ruined earth wall
            return true;
        default: break;
    }
    // Walls / fences / prominent lines (regular pattern, incl. ruined + impassable).
    if ((isomCode >= 5131 && isomCode <= 5180) || isomCode == 5280 || isomCode == 5290)
        return true;
    // Vehicle track + footpaths + rides (dashed).
    if (isomCode >= 5040 && isomCode <= 5080)
        return true;
    return false;
}

// The mark KIND the Line-Mark tool drops on a given curve symbol — auto-chosen
// from the symbol: slope tick on contours, pylon on power lines, dash anchor on
// dashed/patterned features. Returns false if the symbol takes no tool-placeable
// mark.
bool AutoMarkKindFor(int isomCode, Renderer::LineMarkKind& outKind) {
    // Solid contours take slope ticks; the dashed form line takes a dash anchor.
    if (isomCode == 1010 || isomCode == 1020) {
        outKind = Renderer::LineMarkKind::SlopeTick; return true;
    }
    if (isomCode == 5100 || isomCode == 5110) {
        outKind = Renderer::LineMarkKind::Pylon; return true;
    }
    if (DashAnchorAllowedOn(isomCode)) {
        outKind = Renderer::LineMarkKind::DashAnchor; return true;
    }
    return false;
}

// Fill a mark's spec dimensions (in DOC-units) for the given symbol + scale.
void ApplyMarkPreset(Renderer::LineMark& m, int isomCode, float scale) {
    const float s = (scale > 0.01f) ? scale : 1.0f;
    switch (m.kind) {
        case Renderer::LineMarkKind::SlopeTick:
            m.outsideMeasure = true;
            m.size = 0.4f * s;                                  // 0.4 OM
            m.thickness = ((isomCode == 1030) ? 0.10f : 0.14f) * s;  // 103 thinner
            m.gap = 0.0f;
            break;
        case Renderer::LineMarkKind::Crossing:
            m.outsideMeasure = false;
            m.size = 0.5f * s;     // tick half-length (≈1.0 total)
            m.gap  = 1.0f * s;     // the cut in the line
            m.thickness = 0.18f * s;
            break;
        case Renderer::LineMarkKind::Bridge:
            m.outsideMeasure = false;
            m.size = 0.5f * s; m.gap = 2.0f * s; m.thickness = 0.18f * s;
            break;
        case Renderer::LineMarkKind::Pylon:
            // Bar half-length follows the host power line: 511 (twin rails 0.4 CC)
            // overhangs 0.3 OM each side → 0.2 rail + 0.3 = 0.5; 510 (single 0.14
            // line) → 0.07 + 0.3 ≈ 0.37. Thickness 0.2 either way.
            m.outsideMeasure = false;
            m.size = ((isomCode == 5110) ? 0.5f : 0.37f) * s;
            m.gap = 0.0f; m.thickness = 0.2f * s;
            break;
        case Renderer::LineMarkKind::DashAnchor:
            // No geometry; side +1 = centre a dash/element, −1 = centre a gap.
            m.side = +1; m.size = 0.0f; m.gap = 0.0f; m.thickness = 0.0f;
            break;
    }
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Line-mark tool (tool.linemark) — manage marks on curves. The mark KIND is
//  AUTO-chosen from the curve symbol: slope tick on contours (101/102/103), pylon
//  on power lines (510/511). Hovering a compatible curve shows a translucent GHOST
//  of the result; clicking drops it. Clicking an EXISTING mark deletes it (on
//  release); click-drag moves it along the line. Crossing points (519) are placed
//  via the catalogue symbol, not this tool.
// ─────────────────────────────────────────────────────────────────────────────
void Application::HandleLineMarkTool(
    EditorState& st,
    const std::function<Vec2(ImVec2)>& s2d,
    const std::function<ImVec2(Vec2)>& d2s,
    float effZoom, bool hovered, ImDrawList* dl) {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const float zoom = std::max(0.0001f, effZoom);
    const Vec2 mdoc = s2d(io.MousePos);
    const float scale = activeCapabilities_.symbolScale > 0.01f
                            ? activeCapabilities_.symbolScale : 1.0f;
    const void* self = &st;

    // Shared geometry helpers (used by the modal ops, hit-test and placement).
    auto flatten0 = [&](const Renderer::Shape& s, const Renderer::Part& part,
                        int subi, bool& closed, float z) {
        return Renderer::Tessellator::OutlinePartSub(
            s, part, subi, z, closed, CurPageOriginOfShape(s.id));
    };
    auto pointAtT0 = [](const std::vector<Vec2>& poly, bool closed, float t,
                        Vec2& outP, Vec2& outTan) {
        size_t n = poly.size(), sc = closed ? n : n - 1;
        float total = 0.0f;
        for (size_t i = 0; i < sc; ++i)
            total += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].y - poly[i].y);
        float d = std::clamp(t, 0.0f, 1.0f) * total, acc = 0.0f;
        for (size_t i = 0; i < sc; ++i) {
            Vec2 a = poly[i], b = poly[(i + 1) % n];
            float L = std::hypot(b.x - a.x, b.y - a.y);
            if (L < 1e-6f) continue;
            if (d <= acc + L) { float u = (d - acc) / L;
                outP = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
                outTan = { (b.x - a.x) / L, (b.y - a.y) / L }; return; }
            acc += L;
        }
        outP = poly[n - 1]; outTan = { 1, 0 };
    };

    // ── Modal G (move along curve) / R (flip side) / S (scale crossing gap) ──────
    // These act like Blender's transform ops but specialised to marks. Started by
    // the shortcuts (which set markGrab_.op); R is instantaneous (flip + commit).
    // The first hovered leaf claims ownership of the modal op.
    if (markGrab_.Active() && markGrab_.owner == nullptr && hovered) markGrab_.owner = self;
    if (markGrab_.Active() && markGrab_.owner == self) {
        const bool commit = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                            ImGui::IsKeyPressed(ImGuiKey_Enter);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        // The REAL marks are NOT touched during the op — we compute a preview value
        // per ref and either GHOST it (live) or APPLY it once on commit. This keeps
        // the canvas stable (no continuous re-render) and produces ONE undo step.
        // Scale factor (crossing gap): EXACTLY like the core scale gizmo — the pivot
        // is the active mark's centre, and scale = |pivot→mouse| / |pivot→start|, so
        // moving toward the pivot scales DOWN and away scales UP.
        ImVec2 pivotS = markGrab_.startMouse;     // fallback
        if (!markGrab_.refs.empty()) {
            Vec2 pp, ptan;
            if (MarkWorldPoint(markGrab_.refs.front(), zoom, pp, ptan)) pivotS = d2s(pp);
        }
        float scaleF = 1.0f;
        if (markGrab_.op == MarkOp::Scale) {
            float d0 = std::hypot(markGrab_.startMouse.x - pivotS.x,
                                  markGrab_.startMouse.y - pivotS.y);
            float d1 = std::hypot(io.MousePos.x - pivotS.x, io.MousePos.y - pivotS.y);
            float raw = (d0 > 1e-3f) ? d1 / d0 : 1.0f;
            // Shift precision-drag: ease the factor toward 1 so motion is finer.
            scaleF = std::clamp(1.0f + (raw - 1.0f) * PrecisionDragFactor(), 0.05f, 20.0f);
        }
        // RELATIVE move (like the object Move op): the marks stay PUT when G is
        // pressed and shift along the curve in proportion to the mouse displacement
        // since the press — NO teleport to the nearest point. Δt is the mouse
        // displacement projected onto the ANCHOR mark's tangent, divided by the
        // anchor subpath's total arc-length, eased by Shift for precision. Every
        // selected mark shifts by this shared Δt (keeping its own offset).
        float deltaT = 0.0f;
        if (markGrab_.op == MarkOp::Grab && !markGrab_.refs.empty()) {
            const Renderer::MarkRef& a = markGrab_.refs.front();
            Renderer::Shape* s = doc.FindShape(a.shape);
            if (s && a.part < (int)s->parts.size() &&
                a.index < (int)s->parts[(size_t)a.part].marks.size()) {
                Renderer::Part& part = s->parts[(size_t)a.part];
                bool closed = false;
                std::vector<Vec2> poly = flatten0(
                    *s, part, part.marks[(size_t)a.index].sub, closed, zoom);
                if (poly.size() >= 2) {
                    // Anchor's tangent + the subpath's total arc-length.
                    Vec2 ap, atan; pointAtT0(poly, closed, markGrab_.t0.front(), ap, atan);
                    size_t n = poly.size(), sc = closed ? n : n - 1;
                    float total = 0.0f;
                    for (size_t i = 0; i < sc; ++i)
                        total += std::hypot(poly[(i+1)%n].x - poly[i].x,
                                            poly[(i+1)%n].y - poly[i].y);
                    // Mouse displacement since press (doc-units), eased by precision.
                    Vec2 startDoc = s2d(markGrab_.startMouse);
                    Vec2 dMouse{ (mdoc.x - startDoc.x) * PrecisionDragFactor(),
                                 (mdoc.y - startDoc.y) * PrecisionDragFactor() };
                    float along = dMouse.x * atan.x + dMouse.y * atan.y;   // arc-length px
                    if (total > 1e-4f) deltaT = along / total;
                }
            }
        }
        // Preview t for ref `k`: its start t plus the shared Δt, clamped to [0,1].
        auto previewT = [&](size_t k) -> float {
            return std::clamp(markGrab_.t0[k] + deltaT, 0.0f, 1.0f);
        };

        // Draw a GHOST of each affected mark at its preview value (real mark stays).
        for (size_t k = 0; k < markGrab_.refs.size(); ++k) {
            const Renderer::MarkRef& r = markGrab_.refs[k];
            Renderer::Shape* s = doc.FindShape(r.shape);
            if (!s || r.part >= (int)s->parts.size()) continue;
            Renderer::Part& part = s->parts[(size_t)r.part];
            if (r.index >= (int)part.marks.size()) continue;
            Renderer::LineMark ghost = part.marks[(size_t)r.index];
            if (markGrab_.op == MarkOp::Grab) ghost.t = previewT(k);
            else if (ghost.kind == Renderer::LineMarkKind::Crossing)
                ghost.gap = std::max(0.05f, markGrab_.gap0[k] * scaleF);
            Vec2 gp, gtan;
            // Position the ghost at the (possibly new) t.
            bool closed = false;
            std::vector<Vec2> poly = flatten0(*s, part, ghost.sub, closed, zoom);
            if (poly.size() >= 2) { pointAtT0(poly, closed, ghost.t, gp, gtan);
                DrawLineMarkGhost(*s, part, ghost, gp, gtan, d2s, zoom, &poly, closed); }
        }

        // Scale gizmo: same look as the core scale op — pivot ring + dashed line
        // from the PIVOT (mark centre) to the cursor, plus the factor label.
        if (markGrab_.op == MarkOp::Scale) {
            ImU32 gz = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddCircle(pivotS, 5.0f, gz, 0, 1.5f);
            // Dashed pivot→cursor line.
            ImVec2 a = pivotS, b = io.MousePos;
            float len = std::hypot(b.x - a.x, b.y - a.y);
            int segs = std::max(1, (int)(len / 8.0f));
            for (int i = 0; i < segs; i += 2) {
                float t0 = (float)i / segs, t1 = (float)std::min(i + 1, segs) / segs;
                fg->AddLine(ImVec2(a.x + (b.x-a.x)*t0, a.y + (b.y-a.y)*t0),
                            ImVec2(a.x + (b.x-a.x)*t1, a.y + (b.y-a.y)*t1), gz, 1.5f);
            }
            char lbl[24]; std::snprintf(lbl, sizeof lbl, "x%.2f", scaleF);
            fg->AddText(ImVec2(io.MousePos.x + 8, io.MousePos.y - 16), gz, lbl);
        }

        if (cancel) { markGrab_.Reset(); rmbConsumedByTransform_ = true; return; }
        if (commit) {
            // Apply the preview to the REAL marks now → exactly one undo step.
            for (size_t k = 0; k < markGrab_.refs.size(); ++k) {
                const Renderer::MarkRef& r = markGrab_.refs[k];
                Renderer::Shape* s = doc.FindShape(r.shape);
                if (!s || r.part >= (int)s->parts.size()) continue;
                Renderer::Part& part = s->parts[(size_t)r.part];
                if (r.index >= (int)part.marks.size()) continue;
                Renderer::LineMark& m = part.marks[(size_t)r.index];
                if (markGrab_.op == MarkOp::Grab) m.t = previewT(k);
                else if (m.kind == Renderer::LineMarkKind::Crossing)
                    m.gap = std::max(0.05f, markGrab_.gap0[k] * scaleF);
            }
            MarkUndoLabel(markGrab_.op == MarkOp::Grab ? "Move line marks" : "Scale crossing");
            project_.dirty = true;
            markGrab_.Reset();
        }
        // Move op → the multi-directional move cursor (Blender); Scale keeps the
        // placement crosshair (its gizmo already conveys the scale direction).
        if (hovered) {
            if (markGrab_.op == MarkOp::Grab) ShowMoveCursor();
            else                              ShowCrosshairCursor();
        }
        return;
    }

    // The "+" placement cursor (hides the OS cursor + draws into the foreground draw
    // list, which spans the whole app) must only show while the mouse is over THIS
    // canvas — otherwise it leaks across every other panel of the application.
    if (hovered) ShowCrosshairCursor();

    // Flatten one subpath of a part in world space (cached per call site).
    auto flatten = [&](const Renderer::Shape& s, const Renderer::Part& part,
                       int subi, bool& closed) {
        return Renderer::Tessellator::OutlinePartSub(
            s, part, subi, zoom, closed, CurPageOriginOfShape(s.id));
    };
    // Arc-length total + point at fraction t of a flattened polyline.
    auto polyTotal = [](const std::vector<Vec2>& poly, bool closed) {
        float total = 0.0f; size_t n = poly.size(), sc = closed ? n : n - 1;
        for (size_t i = 0; i < sc; ++i)
            total += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].y - poly[i].y);
        return total;
    };
    auto pointAtT = [](const std::vector<Vec2>& poly, bool closed, float t,
                       Vec2& outP, Vec2& outTan) {
        size_t n = poly.size(), sc = closed ? n : n - 1;
        float total = 0.0f;
        for (size_t i = 0; i < sc; ++i)
            total += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].y - poly[i].y);
        float d = std::clamp(t, 0.0f, 1.0f) * total, acc = 0.0f;
        for (size_t i = 0; i < sc; ++i) {
            Vec2 a = poly[i], b = poly[(i + 1) % n];
            float L = std::hypot(b.x - a.x, b.y - a.y);
            if (L < 1e-6f) continue;
            if (d <= acc + L) {
                float u = (d - acc) / L;
                outP = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
                outTan = { (b.x - a.x) / L, (b.y - a.y) / L };
                return;
            }
            acc += L;
        }
        outP = poly[n - 1]; outTan = { 1, 0 };
    };

    // ── In-progress click-drag of an existing mark ────────────────────────────
    // A press on a mark ARMS this; moving past the threshold turns it into a MOVE
    // (drag the grabbed item, Blender-style). A plain click (release while still
    // armed) selects and, on an already-sole-selected mark, applies the deferred
    // type change. While the press is armed/active no NEW mark is placed.
    if (markDrag_.active || markDrag_.armed) {
        Renderer::Shape* s = doc.FindShape(markDrag_.shape);
        if (!s || markDrag_.part >= (int)s->parts.size()) { markDrag_ = {}; return; }
        Renderer::Part& part = s->parts[(size_t)markDrag_.part];
        if (markDrag_.index >= (int)part.marks.size()) { markDrag_ = {}; return; }
        Renderer::LineMark& m = part.marks[(size_t)markDrag_.index];
        // Past threshold → it's a move (not a click / type-change).
        if (markDrag_.armed &&
            std::hypot(io.MousePos.x - markDrag_.pressPos.x,
                       io.MousePos.y - markDrag_.pressPos.y) > 4.0f) {
            markDrag_.armed = false; markDrag_.active = true;
        }
        // The GRABBED mark moves with the cursor; every OTHER selected mark on the
        // SAME subpath keeps its offset and shifts by the same Δt (group move),
        // scaled by Shift for precision. Marks on a different subpath aren't reachable
        // by this projection, so they hold position — matching the G-move grouping.
        bool closed = false;
        std::vector<Vec2> poly = flatten(*s, part, m.sub, closed);
        float deltaT = 0.0f;
        if (markDrag_.active && poly.size() >= 2) {
            // RELATIVE move (no teleport): Δt = mouse displacement since press,
            // projected on the grabbed mark's tangent, over the subpath arc-length,
            // eased by Shift precision. The mark's geometric side follows which side
            // of the line the cursor sits on (ignored for dash anchors on commit).
            Vec2 mp, mtan; pointAtT(poly, closed, m.t, mp, mtan);
            float total = polyTotal(poly, closed);
            Vec2 startDoc = s2d(markDrag_.pressPos);
            Vec2 dMouse{ (mdoc.x - startDoc.x) * PrecisionDragFactor(),
                         (mdoc.y - startDoc.y) * PrecisionDragFactor() };
            float along = dMouse.x * mtan.x + dMouse.y * mtan.y;
            if (total > 1e-4f) deltaT = along / total;
            markDrag_.dragT = std::clamp(m.t + deltaT, 0.0f, 1.0f);
            // Side from the cursor's offset across the tangent (perpendicular sign).
            float cross = mtan.x * (mdoc.y - mp.y) - mtan.y * (mdoc.x - mp.x);
            markDrag_.dragSide = cross >= 0 ? +1 : -1;
            // Ghost the grabbed mark at its drag target (real mark stays put). For a
            // dash anchor `side` means dash/gap (NOT a geometric side), so a move must
            // NOT rewrite it — only slope-tick-style marks follow the cursor's side.
            Renderer::LineMark ghost = m;
            ghost.t = markDrag_.dragT;
            if (m.kind != Renderer::LineMarkKind::DashAnchor) ghost.side = markDrag_.dragSide;
            Vec2 gp, gtan; pointAtT(poly, closed, ghost.t, gp, gtan);
            DrawLineMarkGhost(*s, part, ghost, gp, gtan, d2s, zoom, &poly, closed);
            // Ghost every OTHER selected mark shifted by the same Δt on its own subpath.
            for (const Renderer::MarkRef& r : doc.MarkSelection()) {
                if (r.shape == markDrag_.shape && r.part == markDrag_.part &&
                    r.index == markDrag_.index) continue;             // already ghosted
                Renderer::Shape* os = doc.FindShape(r.shape);
                if (!os || r.part >= (int)os->parts.size()) continue;
                Renderer::Part& opart = os->parts[(size_t)r.part];
                if (r.index >= (int)opart.marks.size()) continue;
                Renderer::LineMark og = opart.marks[(size_t)r.index];
                og.t = std::clamp(og.t + deltaT, 0.0f, 1.0f);
                bool oc = false; std::vector<Vec2> op = flatten(*os, opart, og.sub, oc);
                if (op.size() < 2) continue;
                Vec2 ogp, ogt; pointAtT(op, oc, og.t, ogp, ogt);
                DrawLineMarkGhost(*os, opart, og, ogp, ogt, d2s, zoom, &op, oc);
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (markDrag_.active) {
                // Commit the group move: shift every selected mark by Δt; the grabbed
                // mark also takes the projected side. One undo step.
                for (const Renderer::MarkRef& r : doc.MarkSelection()) {
                    Renderer::Shape* os = doc.FindShape(r.shape);
                    if (!os || r.part >= (int)os->parts.size()) continue;
                    Renderer::Part& opart = os->parts[(size_t)r.part];
                    if (r.index >= (int)opart.marks.size()) continue;
                    Renderer::LineMark& om = opart.marks[(size_t)r.index];
                    om.t = std::clamp(om.t + deltaT, 0.0f, 1.0f);
                }
                m.t = markDrag_.dragT;
                // Don't rewrite a dash anchor's side (it's dash/gap, not geometry).
                if (m.kind != Renderer::LineMarkKind::DashAnchor)
                    m.side = markDrag_.dragSide;
                MarkUndoLabel(doc.MarkSelection().size() > 1 ? "Move line marks"
                                                             : "Move line mark");
                project_.dirty = true;
            } else if (markDrag_.pendingMode != MarkClickMode::None) {
                // A plain click on an already-sole-selected mark → deferred type change.
                if (markDrag_.pendingMode == MarkClickMode::CycleFormLine) {
                    // 103 Form line cycle: SlopeTick → DashAnchor(dash) → (gap) → tick.
                    if (m.kind == Renderer::LineMarkKind::SlopeTick) {
                        m.kind = Renderer::LineMarkKind::DashAnchor; m.side = +1;
                    } else if (m.kind == Renderer::LineMarkKind::DashAnchor && m.side >= 0) {
                        m.side = -1;                                   // dash → gap
                    } else {
                        m.kind = Renderer::LineMarkKind::SlopeTick; m.side = +1;
                        ApplyMarkPreset(m, s->isomCode, scale);        // restore tick dims
                    }
                    MarkUndoLabel("Cycle line mark"); project_.dirty = true;
                } else if (markDrag_.pendingMode == MarkClickMode::ToggleDashAnchor) {
                    m.side = -m.side;                                  // dash ⇄ gap
                    MarkUndoLabel("Toggle dash anchor"); project_.dirty = true;
                }
            }
            markDrag_ = {};
        }
        (void)deltaT;
        return;
    }

    // ── Hit-test EXISTING marks first (so a press grabs them) ─────────────────
    const float kMarkPx = 9.0f;
    uint64_t hitShape = 0; int hitPart = -1, hitIdx = -1; float hitD = kMarkPx + 1.0f;
    ImVec2 hitSP{0, 0};
    auto considerMarks = [&](const Renderer::Shape& s) {
        if (!s.visible) return;
        for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
            const Renderer::Part& part = s.parts[(size_t)pi];
            if (part.marks.empty()) continue;
            for (int mi = 0; mi < (int)part.marks.size(); ++mi) {
                const Renderer::LineMark& m = part.marks[(size_t)mi];
                bool closed = false;
                std::vector<Vec2> poly = flatten(s, part, m.sub, closed);
                if (poly.size() < 2) continue;
                Vec2 p, tan; pointAtT(poly, closed, m.t, p, tan);
                ImVec2 sp = d2s(p);
                float dpx = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y);
                if (dpx < hitD) { hitD = dpx; hitShape = s.id; hitPart = pi; hitIdx = mi; hitSP = sp; }
            }
        }
    };
    for (const auto& ab : doc.artboards) for (const Renderer::Shape& s : ab.shapes) considerMarks(s);
    for (const Renderer::Shape& s : doc.looseShapes) considerMarks(s);

    // Draw a HANDLE for every line mark on every object (the geometry-point style),
    // so DashAnchors (invisible geometry) are findable and the selection reads. The
    // hovered one (computed below) and the selected ones get the overlay shape; the
    // hovered handle is drawn after the hit-test so it can show Hover state.
    {
        auto drawAll = [&](const Renderer::Shape& sh) {
            if (!sh.visible) return;
            for (int pi = 0; pi < (int)sh.parts.size(); ++pi)
                for (int mi = 0; mi < (int)sh.parts[(size_t)pi].marks.size(); ++mi) {
                    const Renderer::LineMark& m = sh.parts[(size_t)pi].marks[(size_t)mi];
                    // Non-anchor marks already render their own geometry; only show a
                    // handle dot for them when SELECTED (so the canvas isn't cluttered).
                    Renderer::MarkRef ref{ sh.id, pi, mi };
                    bool seld = doc.IsMarkSelected(ref);
                    bool anchor = (m.kind == Renderer::LineMarkKind::DashAnchor);
                    if (!anchor && !seld) continue;
                    Vec2 wp, wt;
                    if (!MarkWorldPoint(ref, zoom, wp, wt)) continue;
                    ImVec2 c = d2s(wp);
                    Vec2 ts{ d2s({wp.x+wt.x, wp.y+wt.y}).x - c.x,
                             d2s({wp.x+wt.x, wp.y+wt.y}).y - c.y };
                    DrawMarkHandle(c, ts, m,
                        seld ? MarkHandleState::Selected : MarkHandleState::Normal);
                }
        };
        for (const auto& ab : doc.artboards) for (const Renderer::Shape& s : ab.shapes) drawAll(s);
        for (const Renderer::Shape& s : doc.looseShapes) drawAll(s);
    }

    // ── Box-select in progress ────────────────────────────────────────────────
    if (markBox_.active && markBox_.owner == self) {
        ImVec2 a = d2s(markBox_.start), b = io.MousePos;
        ImU32 boxC = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
        dl->AddRect(ImVec2(std::min(a.x,b.x), std::min(a.y,b.y)),
                    ImVec2(std::max(a.x,b.x), std::max(a.y,b.y)), boxC, 0, 0, 1.5f);
        markBox_.now = mdoc;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            Vec2 mn{ std::min(markBox_.start.x, markBox_.now.x), std::min(markBox_.start.y, markBox_.now.y) };
            Vec2 mx{ std::max(markBox_.start.x, markBox_.now.x), std::max(markBox_.start.y, markBox_.now.y) };
            if (!markBox_.additive) doc.ClearMarkSelection();
            auto boxAdd = [&](const Renderer::Shape& s) {
                if (!s.visible) return;
                for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
                    const Renderer::Part& part = s.parts[(size_t)pi];
                    for (int mi = 0; mi < (int)part.marks.size(); ++mi) {
                        Vec2 wp, wt;
                        if (!MarkWorldPoint({ s.id, pi, mi }, zoom, wp, wt)) continue;
                        if (wp.x >= mn.x && wp.x <= mx.x && wp.y >= mn.y && wp.y <= mx.y)
                            doc.MarkSelectAdd({ s.id, pi, mi });
                    }
                }
            };
            for (const auto& ab : doc.artboards) for (const Renderer::Shape& s : ab.shapes) boxAdd(s);
            for (const Renderer::Shape& s : doc.looseShapes) boxAdd(s);
            markBox_ = {};
        }
        return;
    }

    if (hitIdx >= 0) {
        Renderer::MarkRef hitRef{ hitShape, hitPart, hitIdx };
        Renderer::Shape* sh = doc.FindShape(hitShape);
        const bool isAnchor = sh && hitPart < (int)sh->parts.size() &&
            hitIdx < (int)sh->parts[(size_t)hitPart].marks.size() &&
            sh->parts[(size_t)hitPart].marks[(size_t)hitIdx].kind
                == Renderer::LineMarkKind::DashAnchor;
        // Hover overlay in the same point/diamond style (selected wins over hover).
        if (sh && hitIdx < (int)sh->parts[(size_t)hitPart].marks.size()) {
            const Renderer::LineMark& hm = sh->parts[(size_t)hitPart].marks[(size_t)hitIdx];
            Vec2 wp, wt;
            if (MarkWorldPoint(hitRef, zoom, wp, wt)) {
                ImVec2 hc = d2s(wp);
                Vec2 ts{ d2s({wp.x+wt.x, wp.y+wt.y}).x - hc.x,
                         d2s({wp.x+wt.x, wp.y+wt.y}).y - hc.y };
                DrawMarkHandle(hc, ts, hm, doc.IsMarkSelected(hitRef)
                    ? MarkHandleState::Selected : MarkHandleState::Hover);
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // A type-change (form-line cycle / dash-anchor toggle) only applies when
            // the mark was ALREADY the sole selection before this press, and only on
            // RELEASE without a drag — so a press always lets a drag MOVE the mark.
            const bool wasSelected = doc.IsMarkSelected(hitRef);
            const bool soleSelected = wasSelected && doc.MarkSelection().size() == 1;
            if (io.KeyAlt) {
                // Alt+click DELETES the mark (the attached anchor, not the node).
                if (sh && hitIdx < (int)sh->parts[(size_t)hitPart].marks.size()) {
                    sh->parts[(size_t)hitPart].marks.erase(
                        sh->parts[(size_t)hitPart].marks.begin() + hitIdx);
                    doc.MarkDeselect(hitRef);
                    MarkUndoLabel("Remove line mark");
                    project_.dirty = true;
                }
            } else if (io.KeyShift) {
                doc.MarkSelectToggle(hitRef);          // add/remove from selection
                doc.SetActive(hitShape);
            } else {
                if (!doc.IsMarkSelected(hitRef)) doc.MarkSelectOnly(hitRef);
                else doc.MarkSelectAdd(hitRef);        // make active, keep selection
                doc.SetActive(hitShape);
                // Arm a click-drag of this one mark (Blender: drag the grabbed item).
                // If it was already sole-selected, defer a type change to release.
                MarkClickMode pending = MarkClickMode::None;
                if (soleSelected && sh && sh->isomCode == 1030)
                    pending = MarkClickMode::CycleFormLine;     // 103 Form line cycle
                else if (soleSelected && isAnchor)
                    pending = MarkClickMode::ToggleDashAnchor;  // dash/gap toggle
                markDrag_ = { false, true, hitShape, hitPart, hitIdx,
                              io.MousePos, 0.0f, +1, false, pending };
            }
        }
        return;   // a mark is under the cursor → don't also place a new one
    }

    // ── Find the closest COMPATIBLE stroked line to drop a NEW mark ───────────
    // The KIND is auto-chosen from the curve symbol (slope tick on contours,
    // pylon on power lines); symbols that take neither are skipped.
    struct Hit { uint64_t sid = 0; int part = -1; int sub = 0; float t = 0.5f;
                 int side = +1; float dpx = 1e9f; Vec2 p{0,0}; Vec2 tan{1,0};
                 Renderer::LineMarkKind kind = Renderer::LineMarkKind::SlopeTick; } best;
    auto consider = [&](const Renderer::Shape& s) {
        if (!s.visible) return;
        Renderer::LineMarkKind autoKind;
        if (!AutoMarkKindFor(s.isomCode, autoKind)) return;
        for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
            const Renderer::Part& part = s.parts[(size_t)pi];
            if (!part.stroke.enabled) continue;
            int subs = Renderer::Tessellator::SubpathCount(part);
            for (int subi = 0; subi < subs; ++subi) {
                bool closed = false;
                std::vector<Vec2> poly = flatten(s, part, subi, closed);
                if (poly.size() < 2) continue;
                float total = polyTotal(poly, closed), acc = 0.0f;
                size_t n = poly.size(), sc = closed ? n : n - 1;
                for (size_t i = 0; i < sc; ++i) {
                    Vec2 a = poly[i], b = poly[(i + 1) % n];
                    Vec2 ab{ b.x - a.x, b.y - a.y };
                    float segLen = std::hypot(ab.x, ab.y);
                    if (segLen < 1e-6f) continue;
                    float u = std::clamp(((mdoc.x - a.x) * ab.x + (mdoc.y - a.y) * ab.y)
                                             / (segLen * segLen), 0.0f, 1.0f);
                    Vec2 proj{ a.x + ab.x * u, a.y + ab.y * u };
                    float dpx = std::hypot(io.MousePos.x - d2s(proj).x, io.MousePos.y - d2s(proj).y);
                    if (dpx < best.dpx) {
                        float cross = ab.x * (mdoc.y - a.y) - ab.y * (mdoc.x - a.x);
                        best = { s.id, pi, subi, total > 1e-4f ? (acc + segLen * u) / total : 0.0f,
                                 cross >= 0 ? +1 : -1, dpx, proj,
                                 { ab.x / segLen, ab.y / segLen }, autoKind };
                    }
                    acc += segLen;
                }
            }
        }
    };
    for (const auto& ab : doc.artboards) for (const Renderer::Shape& s : ab.shapes) consider(s);
    for (const Renderer::Shape& s : doc.looseShapes) consider(s);

    const float kPickPx = 14.0f;
    const bool onLine = best.part >= 0 && best.dpx <= kPickPx;
    if (onLine) {
        // Build the would-be mark with its spec preset, then draw a translucent
        // GHOST of exactly what it will render (incl. the erased line for crossings).
        Renderer::Shape* sp = doc.FindShape(best.sid);
        if (sp && best.part < (int)sp->parts.size()) {
            const Renderer::Part& part = sp->parts[(size_t)best.part];
            Renderer::LineMark m; m.kind = best.kind; m.sub = best.sub;
            m.t = best.t; m.side = best.side;
            ApplyMarkPreset(m, sp->isomCode, scale);
            DrawLineMarkGhost(*sp, part, m, best.p, best.tan, d2s, zoom);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sp &&
            best.part < (int)sp->parts.size()) {
            Renderer::Part& part = sp->parts[(size_t)best.part];
            Renderer::LineMark m; m.kind = best.kind; m.sub = best.sub;
            m.t = best.t; m.side = best.side;
            ApplyMarkPreset(m, sp->isomCode, scale);
            // A DashAnchor placed near a CONTROL POINT pins to it (nodeAnchor), so it
            // follows that point as the curve is edited. Otherwise it's free (t).
            if (best.kind == Renderer::LineMarkKind::DashAnchor &&
                part.IsCurveLike()) {
                Vec2 po = CurPageOriginOfShape(best.sid);
                float bestNodeD = 12.0f; int bestNode = -1;   // screen-px snap radius
                for (int ni = 0; ni < (int)part.path.nodes.size(); ++ni) {
                    ImVec2 nsp = d2s(Renderer::Tessellator::WorldTransform(
                        *sp, part.path.nodes[(size_t)ni].pos, po));
                    float dnp = std::hypot(io.MousePos.x - nsp.x, io.MousePos.y - nsp.y);
                    if (dnp < bestNodeD) { bestNodeD = dnp; bestNode = ni; }
                }
                if (bestNode >= 0) m.nodeAnchor = bestNode;
            }
            int newIdx = (int)part.marks.size();
            part.marks.push_back(m);
            doc.SetActive(best.sid);
            doc.MarkSelectOnly({ best.sid, best.part, newIdx });   // select the new mark
            MarkUndoLabel("Add line mark");
            project_.dirty = true;
        }
        (void)st;
        return;
    }

    // ── Empty press (not on a mark, not on a compatible line) ─────────────────
    // Start a box-select (Shift = add to the selection); a plain click clears it.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        markBox_ = { true, self, mdoc, mdoc, io.KeyShift };
        if (!io.KeyShift) doc.ClearMarkSelection();
    }
    (void)st;
}

// Draw a translucent preview of a line mark at world point `p` (tangent `tan`),
// mirroring how the tessellator will render it. Tinted with the OBJECT's own
// colour (the mark is part of that symbol), at the placement preview alpha.
void Application::DrawLineMarkGhost(
    const Renderer::Shape& s, const Renderer::Part& part, const Renderer::LineMark& m,
    Vec2 p, Vec2 tan, const std::function<ImVec2(Vec2)>& d2s, float zoom,
    const std::vector<Vec2>* curve, bool curveClosed) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    // Foreground list so the ghost shows in every context (incl. mid-drag).
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float alpha = ds.GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);
    const Renderer::Color& oc = part.stroke.color;
    ImU32 col = ImGui::GetColorU32(ImVec4(oc.r, oc.g, oc.b, std::max(0.35f, alpha)));
    const float avgScale = 0.5f * (std::fabs(s.transform.scale.x) + std::fabs(s.transform.scale.y));
    float baseW = part.stroke.width * avgScale;
    Vec2 nrm{ -tan.y, tan.x };
    float thPx = std::max(1.5f, (m.thickness > 1e-5f ? m.thickness : part.stroke.width)
                                    * avgScale * zoom);
    auto seg = [&](Vec2 A, Vec2 B) { dl->AddLine(d2s(A), d2s(B), col, thPx); };
    auto add = [](Vec2 v, Vec2 d, float k) { return Vec2{ v.x + d.x * k, v.y + d.y * k }; };

    // Sample the curve at arc-length offset `off` from p (signed). Returns the
    // point + tangent THERE, so crossing/bridge ends FOLLOW the curve (not a
    // straight tangent line). Falls back to a straight offset if no curve given.
    auto sampleOff = [&](float off, Vec2& outP, Vec2& outTan) {
        if (!curve || curve->size() < 2) { outP = add(p, tan, off); outTan = tan; return; }
        const std::vector<Vec2>& poly = *curve;
        size_t n = poly.size(), sc = curveClosed ? n : n - 1;
        float total = 0.0f, baseD = 0.0f;
        for (size_t i = 0; i < sc; ++i)
            total += std::hypot(poly[(i+1)%n].x - poly[i].x, poly[(i+1)%n].y - poly[i].y);
        baseD = std::clamp(m.t, 0.0f, 1.0f) * total;
        float d = std::clamp(baseD + off, 0.0f, total), acc = 0.0f;
        for (size_t i = 0; i < sc; ++i) {
            Vec2 a = poly[i], b = poly[(i+1)%n];
            float L = std::hypot(b.x-a.x, b.y-a.y); if (L < 1e-6f) continue;
            if (d <= acc + L) { float u = (d-acc)/L;
                outP = { a.x+(b.x-a.x)*u, a.y+(b.y-a.y)*u };
                outTan = { (b.x-a.x)/L, (b.y-a.y)/L }; return; }
            acc += L;
        }
        outP = poly[n-1]; outTan = tan;
    };

    switch (m.kind) {
        case Renderer::LineMarkKind::SlopeTick: {
            float len = m.outsideMeasure ? (baseW * 0.5f + m.size * avgScale) : m.size * avgScale;
            float side = m.side >= 0 ? 1.0f : -1.0f;
            seg(p, add(p, nrm, len * side));
            break; }
        case Renderer::LineMarkKind::Crossing: {
            float half = m.gap * avgScale * 0.5f, sz = m.size * avgScale;
            // Two end ticks at ±half ALONG the curve, each perpendicular to the
            // local tangent there (so the gap follows the path's bend).
            for (float sgn : { -1.0f, +1.0f }) {
                Vec2 e, et; sampleOff(sgn * half, e, et);
                Vec2 en{ -et.y, et.x };
                seg(add(e, en, -sz), add(e, en, sz));
            }
            // Hint the erased segment by tracing the curve between the two ends.
            ImU32 cut = ImGui::GetColorU32(ImVec4(oc.r, oc.g, oc.b, 0.18f));
            const int N = 12; Vec2 prev, pt;
            sampleOff(-half, prev, pt);
            for (int i = 1; i <= N; ++i) {
                Vec2 cur, ct; sampleOff(-half + (2*half)*(float)i/N, cur, ct);
                dl->AddLine(d2s(prev), d2s(cur), cut, std::max(1.0f, baseW * zoom));
                prev = cur;
            }
            break; }
        case Renderer::LineMarkKind::Bridge: {
            float half = m.gap * avgScale * 0.5f, sz = m.size * avgScale;
            for (float sgn : { -1.0f, +1.0f }) {
                Vec2 e, et; sampleOff(sgn * half, e, et);
                Vec2 en{ -et.y, et.x };
                Vec2 inward = { et.x * (-sgn), et.y * (-sgn) };
                Vec2 top = add(e, en, sz), bot = add(e, en, -sz);
                seg(add(top, inward, sz * 0.6f), top); seg(top, bot);
                seg(bot, add(bot, inward, sz * 0.6f));
            }
            break; }
        case Renderer::LineMarkKind::Pylon: {
            float sz = m.size * avgScale;
            seg(add(p, nrm, -sz), add(p, nrm, sz));
            break; }
        case Renderer::LineMarkKind::DashAnchor: {
            // No geometry — show the same POINT + diamond/square handle as the tool
            // overlay (it's a phase pin, not a drawn glyph). Screen-space tangent.
            ImVec2 c2 = d2s(p);
            Vec2 ts{ d2s({p.x+tan.x, p.y+tan.y}).x - c2.x,
                     d2s({p.x+tan.x, p.y+tan.y}).y - c2.y };
            DrawMarkHandle(c2, ts, m, MarkHandleState::Hover);
            break; }
    }
}

// Draw a mark's clickable HANDLE in the geometry-point style. `tanScreen` is the
// curve tangent at the point (screen space) for orienting the dash-anchor diamond.
void Application::DrawMarkHandle(ImVec2 sp, Vec2 tanScreen,
                                const Renderer::LineMark& m, MarkHandleState state) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    using Tok = DesignSystem::Tok;
    const bool isAnchor = (m.kind == Renderer::LineMarkKind::DashAnchor);
    const bool dashMode = (m.side >= 0);
    // Centre-dot colour: orange when selected (geometry convention); else per-kind.
    ImU32 centre = state == MarkHandleState::Selected
        ? ImGui::GetColorU32(ds.GetColor(Tok::S_State_Active_OnPage))   // orange
        : isAnchor
            ? ImGui::GetColorU32(ds.GetColor(dashMode ? Tok::C_EditHandle_Vector       // violet
                                                      : Tok::C_EditHandle_Mirrored))   // green
            : ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Accent_Default));
    ImU32 ring = ImGui::GetColorU32(ds.GetColor(Tok::C_EditHandle_VertexRing));
    const float vr = 3.5f;
    // The geometry-style dot.
    dl->AddCircleFilled(sp, vr, centre);
    dl->AddCircle(sp, vr, ring, 0, 1.0f);

    // Select / hover overlay. Normal marks → a ring; dash anchors → a DIAMOND (dash
    // mode) or a SQUARE (gap mode). The overlay keeps the TYPE colour (violet / green
    // / accent) in every state — only the centre dot turns orange when selected.
    if (state == MarkHandleState::Normal) return;
    ImU32 ov = isAnchor
        ? ImGui::GetColorU32(ds.GetColor(dashMode ? Tok::C_EditHandle_Vector
                                                  : Tok::C_EditHandle_Mirrored))
        : ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Accent_Default));
    float r = (state == MarkHandleState::Selected) ? 8.0f : 7.0f;
    float th = (state == MarkHandleState::Selected) ? 2.0f : 1.5f;
    if (!isAnchor) { dl->AddCircle(sp, r, ov, 16, th); return; }
    // Diamond oriented ALONG the curve for "dash"; square (axis of the curve) for
    // "gap" — a rotated square is just the diamond turned 45°, so use the tangent.
    Vec2 t = tanScreen; float tl = std::hypot(t.x, t.y);
    if (tl < 1e-4f) t = { 1, 0 }; else { t.x /= tl; t.y /= tl; }
    Vec2 nrm{ -t.y, t.x };
    auto P = [&](float a, float b){ return ImVec2(sp.x + t.x*a + nrm.x*b,
                                                  sp.y + t.y*a + nrm.y*b); };
    if (dashMode) {
        // Diamond: vertices along ±tangent and ±normal.
        dl->AddQuad(P(r,0), P(0,r), P(-r,0), P(0,-r), ov, th);
    } else {
        // Square aligned to the curve (corners on the diagonals).
        float h = r * 0.72f;
        dl->AddQuad(P(h,h), P(-h,h), P(-h,-h), P(h,-h), ov, th);
    }
}

// World position + tangent of a mark at its `t` along its host part's subpath.
bool Application::MarkWorldPoint(const Renderer::MarkRef& ref, float zoom,
                                 Vec2& outPos, Vec2& outTan) {
    Renderer::Shape* s = project_.document.FindShape(ref.shape);
    if (!s || ref.part < 0 || ref.part >= (int)s->parts.size()) return false;
    Renderer::Part& part = s->parts[(size_t)ref.part];
    if (ref.index < 0 || ref.index >= (int)part.marks.size()) return false;
    const Renderer::LineMark& m = part.marks[(size_t)ref.index];
    bool closed = false;
    std::vector<Vec2> poly = Renderer::Tessellator::OutlinePartSub(
        *s, part, m.sub, std::max(0.0001f, zoom), closed, CurPageOriginOfShape(ref.shape));
    if (poly.size() < 2) return false;
    // Arc-length walk to t.
    size_t n = poly.size(), sc = closed ? n : n - 1;
    float total = 0.0f;
    for (size_t i = 0; i < sc; ++i)
        total += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].y - poly[i].y);
    float d = std::clamp(m.t, 0.0f, 1.0f) * total, acc = 0.0f;
    for (size_t i = 0; i < sc; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float L = std::hypot(b.x - a.x, b.y - a.y);
        if (L < 1e-6f) continue;
        if (d <= acc + L) {
            float u = (d - acc) / L;
            outPos = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
            outTan = { (b.x - a.x) / L, (b.y - a.y) / L };
            return true;
        }
        acc += L;
    }
    outPos = poly[n - 1]; outTan = { 1, 0 };
    return true;
}

// Whether a mark's SIDE can be flipped (R) — only one-sided marks (slope ticks).
static bool MarkFlippable(Renderer::LineMarkKind k) {
    return k == Renderer::LineMarkKind::SlopeTick;
}

void Application::BeginMarkTransform(TransformKind kind) {
    auto& doc = project_.document;
    if (!doc.HasMarkSelection()) return;
    if (kind == TransformKind::Rotate) {
        // Instantaneous: flip the side of every FLIPPABLE selected mark (centred
        // marks are unaffected). No modal.
        bool any = false;
        for (const Renderer::MarkRef& r : doc.MarkSelection()) {
            Renderer::Shape* s = doc.FindShape(r.shape);
            if (!s || r.part >= (int)s->parts.size()) continue;
            Renderer::Part& part = s->parts[(size_t)r.part];
            if (r.index >= (int)part.marks.size()) continue;
            Renderer::LineMark& m = part.marks[(size_t)r.index];
            if (MarkFlippable(m.kind)) { m.side = -m.side; any = true; }
        }
        if (any) { MarkUndoLabel("Flip mark side"); project_.dirty = true; }
        return;
    }
    // Move / Scale → arm the modal op, snapshotting each mark's t/side/gap.
    markGrab_.Reset();
    markGrab_.op = (kind == TransformKind::Scale) ? MarkOp::Scale : MarkOp::Grab;
    markGrab_.owner = nullptr;   // set by the first hovered leaf that drives it
    markGrab_.startMouse = ImGui::GetIO().MousePos;
    for (const Renderer::MarkRef& r : doc.MarkSelection()) {
        Renderer::Shape* s = doc.FindShape(r.shape);
        if (!s || r.part >= (int)s->parts.size()) continue;
        Renderer::Part& part = s->parts[(size_t)r.part];
        if (r.index >= (int)part.marks.size()) continue;
        // Scale only affects crossings; skip the rest so a mixed selection is safe.
        if (markGrab_.op == MarkOp::Scale &&
            part.marks[(size_t)r.index].kind != Renderer::LineMarkKind::Crossing) continue;
        markGrab_.refs.push_back(r);
        markGrab_.t0.push_back(part.marks[(size_t)r.index].t);
        markGrab_.side0.push_back(part.marks[(size_t)r.index].side);
        markGrab_.gap0.push_back(part.marks[(size_t)r.index].gap);
    }
    if (markGrab_.refs.empty()) markGrab_.Reset();   // nothing applicable
}

void Application::DeleteSelectedMarks() {
    auto& doc = project_.document;
    if (!doc.HasMarkSelection()) return;
    // Group by (shape,part) and erase in DESCENDING index order so earlier indices
    // stay valid.
    std::vector<Renderer::MarkRef> refs(doc.MarkSelection().begin(), doc.MarkSelection().end());
    std::sort(refs.begin(), refs.end(), [](const Renderer::MarkRef& a, const Renderer::MarkRef& b){
        if (a.shape != b.shape) return a.shape < b.shape;
        if (a.part  != b.part)  return a.part  < b.part;
        return a.index > b.index;   // descending within a part
    });
    for (const Renderer::MarkRef& r : refs) {
        Renderer::Shape* s = doc.FindShape(r.shape);
        if (!s || r.part >= (int)s->parts.size()) continue;
        Renderer::Part& part = s->parts[(size_t)r.part];
        if (r.index >= 0 && r.index < (int)part.marks.size())
            part.marks.erase(part.marks.begin() + r.index);
    }
    doc.ClearMarkSelection();
    MarkUndoLabel("Delete line marks");
    project_.dirty = true;
}

// Draw the multi-directional move cursor centred on the mouse, hiding the OS
// cursor (same technique as ZoneLayout::ApplyCursor).
void Application::ShowMoveCursor() {
    auto& im = VectorGraphics::IconManager::Instance();
    const char* iconId = "multi-directionnal-move-cur";
    if (!im.HasIcon(iconId)) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImVec2 p(mp.x - sz * 0.5f, mp.y - sz * 0.5f);
    ImVec4 col = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = col;
    im.RenderIcon(ImGui::GetForegroundDrawList(), iconId, p, sz, md);
}

// Draw a cursor icon centred on the mouse, rotated by angleRad. The icon is
// emitted to the foreground draw list, then its just-added vertices are rotated
// about the mouse (same technique as the vertical ruler text in Viewport.cpp).
void Application::ShowOrientedCursor(const char* iconId, float angleRad) {
    auto& im = VectorGraphics::IconManager::Instance();
    if (!im.HasIcon(iconId)) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    int vtx0 = fg->VtxBuffer.Size;
    ImVec4 col = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = col;
    im.RenderIcon(fg, iconId, ImVec2(mp.x - sz * 0.5f, mp.y - sz * 0.5f), sz, md);
    int vtx1 = fg->VtxBuffer.Size;
    float c = std::cos(angleRad), s = std::sin(angleRad);
    for (int i = vtx0; i < vtx1; ++i) {
        ImDrawVert& v = fg->VtxBuffer[i];
        float dx = v.pos.x - mp.x, dy = v.pos.y - mp.y;
        v.pos.x = mp.x + dx * c - dy * s;
        v.pos.y = mp.y + dx * s + dy * c;
    }
}

// Wrap the cursor to the opposite edge of the given SCREEN rect (the active
// zone's canvas) for unbounded dragging. Returns the warp vector in screen px
// ({0,0} if none) so an absolute-anchor transform can shift its anchor and stay
// continuous. Per-zone (the rect is passed in), so multiple viewports / other
// editors each wrap within their own bounds.
void Application::BeginGestureMouseTracking() {
    gestureMouseRef_ = ImGui::GetIO().MousePos;
}

ImVec2 Application::GestureMouseDelta() {
    ImVec2 cur = ImGui::GetIO().MousePos;
    ImVec2 d{ cur.x - gestureMouseRef_.x, cur.y - gestureMouseRef_.y };
    gestureMouseRef_ = cur;          // advance the reference to the current pos
    return d;
}

// Global Shift precision-drag factor. Held Shift slows the RELATIVE motion of the
// dragged thing (object, vertex, handle, line mark, slider value) without slowing
// the cursor — Blender's "finer adjustment". One factor for the whole app so the
// feel is uniform. (Behavioral interaction constant, like the drag thresholds
// elsewhere in this file — not a visual-style value, so not a design token.)
float Application::PrecisionDragFactor() const {
    constexpr float kPrecisionFactor = 0.1f;   // Shift → 10% of normal motion
    return ImGui::GetIO().KeyShift ? kPrecisionFactor : 1.0f;
}

// Adaptive "nice" grid step in doc-units for Ctrl snapping, matching the ruler
// subdivision at the current zoom (1·10ⁿ / 2·10ⁿ / 5·10ⁿ). effZoom = st.zoom ×
// unitScale (screen px per doc-unit). Mirrors the ruler's NiceStep so a snap lands
// exactly on a visible grid line.
float Application::SnapGridStep(float effZoom) const {
    const float kTargetPx = 48.0f;             // same target spacing as the rulers
    float raw = kTargetPx / std::max(1e-4f, effZoom);
    if (raw <= 0.0f) return 1.0f;
    float e = std::floor(std::log10(raw));
    float pow10 = std::pow(10.0f, e);
    float f = raw / pow10;                      // 1 .. 10
    float nice = (f <= 1.0f) ? 1.0f : (f <= 2.0f) ? 2.0f
               : (f <= 5.0f) ? 5.0f : 10.0f;
    return nice * pow10;
}

bool Application::WrapMouseInRect(ImVec2 mn, ImVec2 mx) {
    if (!window_) return false;
    const float pad = 2.0f;
    if (mx.x - mn.x < 8.0f || mx.y - mn.y < 8.0f) return false;
    ImVec2 mp = ImGui::GetIO().MousePos;
    float nx = mp.x, ny = mp.y;
    bool wrap = false;
    if (mp.x <= mn.x + pad)      { nx = mx.x - pad - 1.0f; wrap = true; }
    else if (mp.x >= mx.x - pad) { nx = mn.x + pad + 1.0f; wrap = true; }
    if (mp.y <= mn.y + pad)      { ny = mx.y - pad - 1.0f; wrap = true; }
    else if (mp.y >= mx.y - pad) { ny = mn.y + pad + 1.0f; wrap = true; }
    if (!wrap) return false;
    // Warp the OS cursor to the opposite edge, and move OUR reference to the
    // same target. Next frame GestureMouseDelta() = reportedPos − warpTarget =
    // the user's REAL motion since the warp, with the warp jump itself excluded
    // exactly (no rounding-based drift, and no lost motion at high speed — the
    // earlier "drop a whole frame" approach lost the fast motion → the drift).
    SDL_WarpMouseInWindow(window_, nx, ny);
    ImGui::GetIO().MousePos = ImVec2(nx, ny);   // reflect the warp this frame
    gestureMouseRef_ = ImVec2(nx, ny);          // reference follows the warp
    return true;
}

// Snap pie menu (Shift+S). 2D adaptation of Blender's snap pie: moves the
// selection to targets, or the 2D cursor to references. "Grid" snaps each
// object individually to the nearest grid crossing by its ORIGIN (not the pivot).
void Application::RenderViewportPieMenu() {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;

    // The doc-unit spacing of one grid cell (matches the viewport's minor grid
    // intent: 50 doc-units is a sensible default cell).
    const float kGrid = 50.0f;
    auto snapGrid = [&](Renderer::Vec2 p) {
        return Renderer::Vec2{ std::round(p.x / kGrid) * kGrid,
                               std::round(p.y / kGrid) * kGrid };
    };
    auto originWorld = [&](uint64_t id) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(id);
        // Use THIS viewport's display origin so snaps line up with the 2D cursor
        // and the on-screen objects under any auto page layout (not the stored
        // Manual position).
        return s ? Renderer::Tessellator::WorldTransform(*s, s->origin,
                                                         CurPageOriginOfShape(id))
                 : Renderer::Vec2{0,0};
    };
    // Move a whole shape so its origin lands on world point `to`.
    auto moveOriginTo = [&](uint64_t id, Renderer::Vec2 to) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) return;
        Renderer::Vec2 cur = originWorld(id);
        s->transform.translate.x += to.x - cur.x;
        s->transform.translate.y += to.y - cur.y;
    };

    ImGui::PushStyleColor(ImGuiCol_PopupBg,
        ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));

    if (ImGui::BeginPopup("##snapPie")) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Snap");
        ImGui::PopStyleColor();
        ImGui::Separator();

        const bool hasSel = doc.HasSelection();
        const auto sel = doc.Selection();

        if (ImGui::MenuItem("Selection to Cursor", nullptr, false, hasSel)) {
            for (uint64_t id : sel) moveOriginTo(id, doc.cursor);
            project_.dirty = true;
        }
        if (ImGui::MenuItem("Selection to Active", nullptr, false, sel.size() >= 2)) {
            Renderer::Vec2 a = originWorld(doc.ActiveId());
            for (uint64_t id : sel) if (id != doc.ActiveId()) moveOriginTo(id, a);
            project_.dirty = true;
        }
        if (ImGui::MenuItem("Selection to Grid", nullptr, false, hasSel)) {
            for (uint64_t id : sel) moveOriginTo(id, snapGrid(originWorld(id)));
            project_.dirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cursor to Active", nullptr, false, hasSel))
            doc.cursor = originWorld(doc.ActiveId());
        if (ImGui::MenuItem("Cursor to Selected", nullptr, false, hasSel)) {
            Renderer::Vec2 sum{0,0}; int n = 0;
            for (uint64_t id : sel) { Renderer::Vec2 o = originWorld(id); sum.x += o.x; sum.y += o.y; ++n; }
            if (n) doc.cursor = { sum.x / n, sum.y / n };
        }
        if (ImGui::MenuItem("Cursor to Origin"))
            doc.cursor = { 0, 0 };
        if (ImGui::MenuItem("Cursor to Page Origin")) {
            if (!doc.artboards.empty()) doc.cursor = doc.artboards.front().pos;
        }
        if (ImGui::MenuItem("Cursor to Grid"))
            doc.cursor = snapGrid(doc.cursor);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
}

// Local-space bbox centre of a shape's combined outline (all parts).
static Renderer::Vec2 LocalCenter(Renderer::Shape& s) {
    Renderer::Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
    bool any = false;
    for (const Renderer::Part& part : s.parts) {
        bool cl = false;
        std::vector<Renderer::Vec2> w =
            Renderer::Tessellator::OutlinePart(s, part, 1.0f, cl);
        for (auto& p : w) {
            Renderer::Vec2 l = Renderer::Tessellator::InverseTransform(s, p);
            mn.x = std::min(mn.x, l.x); mn.y = std::min(mn.y, l.y);
            mx.x = std::max(mx.x, l.x); mx.y = std::max(mx.y, l.y);
            any = true;
        }
    }
    return any ? Renderer::Vec2{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f } : s.origin;
}
// Move a shape's origin marker to a new WORLD point, keeping the geometry
// visually fixed. We solve for the local origin + translate so that:
//   (a) the geometry's world position is unchanged, and
//   (b) the origin marker lands exactly on `worldTarget`.
// Geometry world = origin + R·S·(local − origin) + translate. Requiring it
// constant for all `local` forces translate to absorb the change in
// (origin − R·S·origin); then translate is offset so World(origin) == target.
static void MoveOriginToWorld(Renderer::Shape& s, Renderer::Vec2 worldTarget,
                              Renderer::Vec2 pageOrigin = {0, 0}) {
    // Snapshot a reference geometry world point (origin's current world pos) so
    // we can keep the picture fixed across the change.
    auto rotS = [&](Renderer::Vec2 v) {
        v.x *= s.transform.scale.x; v.y *= s.transform.scale.y;
        float c = std::cos(s.transform.rotate), sn = std::sin(s.transform.rotate);
        return Renderer::Vec2{ v.x * c - v.y * sn, v.x * sn + v.y * c };
    };
    // World of an arbitrary local point BEFORE the change (to preserve geometry).
    Renderer::Vec2 newOriginLocal =
        Renderer::Tessellator::InverseTransform(s, worldTarget, pageOrigin);
    // Keep geometry fixed: world(local) must be unchanged. With origin o and
    // translate t: world = o + RS(local-o) + t = RS·local + (o - RS·o + t).
    // The constant term C = o - RS·o + t must stay the same. Solve t' for the
    // new origin o': t' = C - (o' - RS·o').
    Renderer::Vec2 oOld = s.origin, tOld = s.transform.translate;
    Renderer::Vec2 RSoOld = rotS(oOld);
    Renderer::Vec2 C{ oOld.x - RSoOld.x + tOld.x, oOld.y - RSoOld.y + tOld.y };
    Renderer::Vec2 RSoNew = rotS(newOriginLocal);
    s.origin = newOriginLocal;
    s.transform.translate = { C.x - (newOriginLocal.x - RSoNew.x),
                              C.y - (newOriginLocal.y - RSoNew.y) };
}

// ── Shift+A "Add" menu (Lot 5): spawn an object at the 2D cursor ─────────────
// Build a default geometry for `what` (object-local, centred near origin), then
// place it so its centre lands on the 2D cursor, on the page under the cursor.
void Application::Action_AddShape(const std::string& what) {
    // Preview placement: arm a cursor-following preview instead of dropping the
    // object at the 2D cursor right now (the click commits it). The guard avoids
    // re-arming when UpdatePlacement re-enters this path to actually create it.
    if (!placementCommitting_ && PreviewPlacementEnabled()) {
        RequestPlacementCore(what);
        return;
    }
    using K = Renderer::ShapeKind;
    auto& doc = project_.document;
    const float R = 100.0f;                 // default half-extent (doc-units)

    // A regular N-gon path (mesh) around the local origin, flat-top.
    auto ngon = [&](Renderer::Shape& s, int n) {
        for (int i = 0; i < n; ++i) {
            float a = (float)i / (float)n * 6.2831853f - 1.5707963f;
            Renderer::Node nd({ std::cos(a) * R, std::sin(a) * R });
            nd.mode = Renderer::HandleMode::Vector;
            s.MainPart().path.nodes.push_back(nd);
        }
        s.MainPart().path.closed = true;
    };
    // A closed cubic circle (4 quadrants) as an editable Bézier CURVE around
    // origin (anchors on the curve, aligned in/out handles).
    auto bezierCircle = [&](Renderer::Shape& s) {
        const float k = 0.5522847498f * R;
        Renderer::Vec2 p[4] = { {R,0}, {0,R}, {-R,0}, {0,-R} };
        Renderer::Vec2 t[4] = { {0,k}, {-k,0}, {0,-k}, {k,0} };  // tangents (CCW)
        for (int i = 0; i < 4; ++i) {
            Renderer::Node nd(p[i]);
            nd.hasIn = nd.hasOut = true;
            nd.hIn  = { p[i].x - t[i].x, p[i].y - t[i].y };
            nd.hOut = { p[i].x + t[i].x, p[i].y + t[i].y };
            nd.mode = Renderer::HandleMode::Aligned;
            s.MainPart().path.nodes.push_back(nd);
        }
        s.MainPart().path.closed = true;
    };
    Renderer::Shape s;
    if      (what == "rectangle") { s = MakeShape(K::Rectangle); s.name = "Rectangle";
                                    s.MainPart().pos = {-R,-R}; s.MainPart().size = {2*R,2*R}; }
    else if (what == "ellipse")   { s = MakeShape(K::Ellipse);   s.name = "Ellipse";
                                    s.MainPart().pos = {-R,-R}; s.MainPart().size = {2*R,2*R}; }
    else if (what == "circle")    { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
                                    s.name = "Circle"; bezierCircle(s); }
    else if (what == "triangle")  { s = MakeShape(K::Triangle);  s.name = "Triangle";  ngon(s, 3); CenterOrigin(s); }
    else if (what == "hexagon")   { s = MakeShape(K::Path);      s.name = "Hexagon";   ngon(s, 6); }
    else if (what == "bezier")    { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
                                    s.name = "Bezier";
                                    // a simple open 2-point bezier
                                    Renderer::Node a({-R,0}); a.hasOut=true; a.hOut={-R*0.3f,-R}; a.mode=Renderer::HandleMode::Aligned;
                                    Renderer::Node b({ R,0}); b.hasIn =true; b.hIn ={ R*0.3f, R}; b.mode=Renderer::HandleMode::Aligned;
                                    s.MainPart().path.nodes = { a, b }; s.MainPart().path.closed = false;
                                    s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
    else if (what == "nurbs_circle") { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Nurbs);
                                    s.name = "NURBS Circle";
                                    // EXACT rational circle: 8 control points on a SQUARE —
                                    // edge-midpoints (on the circle, radius R) weight 1, corners
                                    // (the square corners, at R·√2) weight √2/2. Closed periodic
                                    // quadratic. This is Blender's NURBS-circle control hull.
                                    {
                                        const float w = 0.70710678f;          // √2/2
                                        auto& nodes = s.MainPart().path.nodes;
                                        nodes.clear();
                                        // Order: right-mid, corner, top-mid, corner, left-mid,
                                        // corner, bottom-mid, corner (CCW, Y-down).
                                        struct CP { float x, y, w; };
                                        const CP cps[8] = {
                                            { R, 0, 1}, { R, R, w}, { 0, R, 1}, {-R, R, w},
                                            {-R, 0, 1}, {-R,-R, w}, { 0,-R, 1}, { R,-R, w} };
                                        for (const CP& c : cps) {
                                            Renderer::Node nd({ c.x, c.y });
                                            nd.hasIn = nd.hasOut = false; nd.weight = c.w;
                                            nodes.push_back(nd);
                                        }
                                    }
                                    // Bezier U → the periodic loop is 4 rational-Bézier
                                    // quarter-arcs = an EXACT circle (not a rounded square).
                                    s.MainPart().path.closed = true; s.MainPart().orderU = 3;
                                    s.MainPart().nurbsBezier = true; }
    else if (what == "nurbs")     { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Nurbs);
                                    s.name = "NURBS Curve";
                                    // open 4-control-point arc (control points OFF the curve),
                                    // endpoint-clamped so it meets the first/last control point.
                                    Renderer::Node a({-R,0}), b({-R*0.33f,-R}), c({R*0.33f,-R}), d({R,0});
                                    a.hasIn=a.hasOut=b.hasIn=b.hasOut=false;
                                    c.hasIn=c.hasOut=d.hasIn=d.hasOut=false;
                                    s.MainPart().path.nodes = { a, b, c, d }; s.MainPart().path.closed = false;
                                    s.MainPart().orderU = 3;
                                    s.MainPart().nurbsEndpoint = true; s.MainPart().nurbsBezier = false;
                                    s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
    else                          { return; }            // unknown id

    ApplyDefaultColors(s);   // new primitive uses the menu-bar default fill/stroke
    if (what != "triangle") CenterOrigin(s);             // triangle already done

    // The new object belongs to the ACTIVE PAGE (Shift+A target), not the page
    // under the cursor. If no page is active it's added page-less (loose, under
    // the root collection). It must appear exactly AT the 2D cursor — which is in
    // document/display space — so we place its translate relative to the active
    // page's DISPLAY origin (this viewport's layout), then convert to the stored
    // page-relative (ab.pos) frame. For a loose object the reference is {0,0}.
    int ab = -1;
    if (doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());

    if (editorMode_ == EditorMode::Edit) {
        // In Edit Mode, fold the new geometry into the active object (like Join).
        uint64_t hostId = doc.ActiveId();
        if (!hostId && !doc.Selection().empty()) hostId = doc.Selection().front();
        if (hostId) {
            // Place at the cursor relative to the host's page display origin.
            Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{0, 0};
            s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                                      doc.cursor.y - dispPo.y - s.origin.y };
            AddShapeWorldDisplay(doc, ab, std::move(s));
            FoldNewShapeIntoObject(hostId);
            MarkUndoLabel("Add " + std::string(what));
            project_.dirty = true;
            return;
        }
    }

    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{0, 0};
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
}

// The Shift+A Add menu. Contextual: Object Mode offers everything; Edit Mode
// offers only shapes compatible with the active object's type (curve→curves,
// mesh→meshes), Blender-style.
void Application::RenderAddMenu() {
    // A module may REPLACE the Add menu entirely (e.g. IOF Mapping → ISOM
    // catalogue). If it does, use those entries verbatim.
    if (activeModule_) {
        std::vector<UI::MenuEntry> modEntries;
        if (activeModule_->BuildAddMenu(modEntries)) {
            if (modEntries.empty()) {
                UI::MenuEntry e; e.label = "(no objects)"; e.enabled = false;
                modEntries.push_back(std::move(e));
            }
            UI::ContextMenu("##addMenu", addMenuPos_, modEntries, "Add");
            return;
        }
    }
    // Module disabled the core primitives → nothing to add from here.
    if (!activeCapabilities_.corePrimitivesAddMenu) {
        std::vector<UI::MenuEntry> none;
        UI::MenuEntry e; e.label = "(no objects in this module)"; e.enabled = false;
        none.push_back(std::move(e));
        UI::ContextMenu("##addMenu", addMenuPos_, none, "Add");
        return;
    }
    const bool edit = (editorMode_ == EditorMode::Edit);
    // In Edit Mode, what can be added depends on the active object's family:
    // a Mesh object accepts only meshes, a curve-like object only curve-likes
    // (you can't fold incompatible geometry into the edited object).
    bool activeIsCurve = false; bool haveActive = false;
    if (edit) {
        if (Renderer::Shape* s = project_.document.ActiveShape();
            s && !s->parts.empty()) {
            activeIsCurve = (s->Family() == Renderer::PartType::Curve);
            haveActive = true;
        }
    }
    auto leaf = [&](const char* label, const char* id, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        std::string what = id;
        e.onClick = [this, what]{ Action_AddShape(what); };
        return e;
    };

    std::vector<UI::MenuEntry> entries;
    const bool allowMesh  = !edit || (haveActive && !activeIsCurve);
    const bool allowCurve = !edit || (haveActive && activeIsCurve);

    if (allowMesh) {
        UI::MenuEntry shapes; shapes.label = "Shape";
        shapes.submenu.push_back(leaf("Rectangle", "rectangle", "Add a rectangle"));
        shapes.submenu.push_back(leaf("Ellipse",   "ellipse",   "Add an ellipse"));
        shapes.submenu.push_back(leaf("Triangle",  "triangle",  "Add a triangle"));
        shapes.submenu.push_back(leaf("Hexagon",   "hexagon",   "Add a hexagon"));
        entries.push_back(std::move(shapes));
    }
    if (allowCurve) {
        UI::MenuEntry curve; curve.label = "Curve";
        curve.submenu.push_back(leaf("Bezier",       "bezier",       "Add a Bézier curve"));
        curve.submenu.push_back(leaf("Circle",       "circle",       "Add a Bézier circle"));
        curve.submenu.push_back(leaf("Nurbs Curve",  "nurbs",        "Add a NURBS curve"));
        curve.submenu.push_back(leaf("Nurbs Circle", "nurbs_circle", "Add a NURBS circle"));
        entries.push_back(std::move(curve));
    }
    if (entries.empty()) {   // edit mode with no active object → nothing addable
        UI::MenuEntry e; e.label = "(select an object first)"; e.enabled = false;
        entries.push_back(std::move(e));
    }
    UI::ContextMenu("##addMenu", addMenuPos_, entries, "Add");
}

// Shift+G "Select Grouped" picker: a flat menu of the relationship modes. Each
// entry runs Action_SelectGrouped, which also publishes the operator panel so the
// mode can be changed afterwards.
void Application::RenderSelectGroupedMenu() {
    auto group = [&](const char* label, GroupedMode mode, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        e.enabled = project_.document.ActiveId() != 0;
        e.onClick = [this, mode]{ Action_SelectGrouped(mode); };
        return e;
    };
    std::vector<UI::MenuEntry> entries;
    entries.push_back(group("Children", GroupedMode::Children,
        "Select all hierarchical descendants of the active object"));
    entries.push_back(group("Immediate Children", GroupedMode::ImmediateChildren,
        "Select the direct children of the active object"));
    entries.push_back(group("Parent", GroupedMode::Parent,
        "Select the parent of the active object"));
    entries.push_back(group("Siblings", GroupedMode::Siblings,
        "Select objects sharing the active object's parent"));
    entries.push_back(group("Type", GroupedMode::Type,
        "Select objects of the same geometry type"));
    entries.push_back(group("Collection", GroupedMode::Collection,
        "Select objects in the active object's collection"));
    entries.push_back(group("Color", GroupedMode::Color,
        "Select objects with the same fill and stroke colour"));
    UI::ContextMenu("##selectGroupedMenu", selectGroupedMenuPos_, entries, "Select Grouped");
}

// Object context menu — uses the shared UI::ContextMenu (same look as the
// Dropdown menu), opened at the right-click position. Each row shows its bound
// shortcut. The menu captures input, so the tool handler is gated while it is
// open (see RenderViewport) to stop click-through.
void Application::RenderViewportContextMenu() {
    auto& sm  = Shortcuts::ShortcutManager::Instance();
    auto& doc = project_.document;
    const bool hasSel = doc.HasSelection();
    const bool multi  = doc.Selection().size() >= 2;

    // Pull label/shortcut/description for a registered action into a menu entry
    // (Lot 4: every menu row carries its action's description as a dwell tooltip
    // and its bound shortcut, kept in sync with the keymap automatically).
    auto fromAction = [&](const char* actionId, const char* fallbackLabel) {
        UI::MenuEntry e;
        const Shortcuts::Action* a = sm.GetAction(actionId);
        e.label    = (a && !a->name.empty()) ? a->name : fallbackLabel;
        e.shortcut = sm.GetShortcutString(actionId);
        if (a) e.tooltip = a->description;
        return e;
    };

    std::vector<UI::MenuEntry> entries;
    {
        UI::MenuEntry e = fromAction("edit.deleteSelection", "Delete");
        e.icon = "ink-eraser";
        e.enabled = hasSel; e.onClick = [this]{ Action_DeleteSelection(); };
        entries.push_back(std::move(e));
    }
    // Join is typed: two objects can merge only if they share a FAMILY
    // (Mesh↔Mesh, or any curve-like↔curve-like). A mixed selection greys Join
    // out with a tooltip explaining why, and offers "Convert all & Join"
    // shortcuts (Lot 6). Compute whether the selection is single-family.
    bool sameFamily = true; bool haveFamily = false; Renderer::PartType selFamily{};
    for (uint64_t id : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) continue;
        Renderer::PartType f = s->Family();
        if (!haveFamily) { selFamily = f; haveFamily = true; }
        else if (f != selFamily) { sameFamily = false; break; }
    }
    {
        UI::MenuEntry e = fromAction("edit.joinSelection", "Join");
        const bool joinable = multi && sameFamily;
        e.enabled = joinable; e.onClick = [this]{ Action_JoinSelection(); };
        if (!multi)            e.tooltip = "Select at least two objects to join them";
        else if (!sameFamily)  e.tooltip = "Cannot join: the selection mixes Mesh and "
                                           "Curve types. Convert them to one type first "
                                           "(see \"Convert & Join\" below).";
        entries.push_back(std::move(e));
    }
    // When the selection mixes families, offer one-click "convert everything to
    // X, then Join" so the user doesn't have to do it in two steps.
    if (multi && !sameFamily) {
        UI::MenuEntry cj; cj.label = "Convert & Join"; cj.enabled = true;
        { UI::MenuEntry e; e.label = "All to Mesh & Join";
          e.tooltip = "Convert every selected object to Mesh, then join them";
          e.onClick = [this]{ Action_ConvertAllAndJoin(Renderer::PartType::Mesh); };
          cj.submenu.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "All to Curve & Join";
          e.tooltip = "Convert every selected object to Curve, then join them";
          e.onClick = [this]{ Action_ConvertAllAndJoin(Renderer::PartType::Curve); };
          cj.submenu.push_back(std::move(e)); }
        entries.push_back(std::move(cj));
    }
    // Convert To ▸ (Mesh / Curve) — Object-mode operation on the selection. This
    // switches the FAMILY (Mesh ⇄ Curve). The spline kind (Bézier/NURBS/Poly) is
    // chosen in Edit Mode via "Set Spline Type". The parent is just a grouping
    // label (no action) → no tooltip; the leaves carry the descriptions.
    {
        UI::MenuEntry c; c.label = "Convert To"; c.enabled = hasSel;
        auto leaf = [&](const char* label, Renderer::PartType t, const char* tip) {
            UI::MenuEntry e; e.label = label; e.tooltip = tip;
            e.onClick = [this, t]{ Action_ConvertSelectionTo(t); };
            c.submenu.push_back(std::move(e));
        };
        leaf("Mesh",  Renderer::PartType::Mesh,  "Straight-edge mesh: vertex / edge / face editing");
        leaf("Curve", Renderer::PartType::Curve, "Vector curve: point editing (set Bézier/NURBS/Poly in Edit Mode)");
        entries.push_back(std::move(c));
    }
    {
        UI::MenuEntry setOrigin; setOrigin.label = "Set Origin"; setOrigin.enabled = hasSel;
        {
            UI::MenuEntry e; e.label = "Origin to Geometry";
            e.tooltip = "Move the object's origin to the centre of its geometry (geometry stays put)";
            e.onClick = [this]{
                // Move the ORIGIN marker to the geometry's world centre; the
                // geometry stays put.
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        Renderer::Vec2 po = CurPageOriginOfShape(id);
                        Renderer::Vec2 mn, mx;
                        if (Renderer::Tessellator::WorldBounds(*s, 1.0f, mn, mx, po))
                            MoveOriginToWorld(*s, { (mn.x + mx.x) * 0.5f,
                                                    (mn.y + mx.y) * 0.5f }, po);
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Geometry to Origin";
            e.tooltip = "Move the geometry so it is centred on the object's origin";
            e.onClick = [this]{
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        Renderer::Vec2 c = LocalCenter(*s);
                        float dx = s->origin.x - c.x, dy = s->origin.y - c.y;
                        for (Renderer::Part& part : s->parts) {
                            for (Renderer::Node& n : part.path.nodes) {
                                n.pos.x  += dx; n.pos.y  += dy;
                                n.hIn.x  += dx; n.hIn.y  += dy;
                                n.hOut.x += dx; n.hOut.y += dy;
                            }
                            part.pos.x += dx; part.pos.y += dy;
                        }
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Origin to 2D Cursor";
            e.tooltip = "Move the object's origin onto the 2D cursor (geometry stays put)";
            e.onClick = [this]{
                // Move the ORIGIN marker onto the 2D cursor; geometry stays put.
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        MoveOriginToWorld(*s, project_.document.cursor,
                                          CurPageOriginOfShape(id));
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(setOrigin));
    }

    UI::ContextMenu("##viewportCtx", viewportMenuPos_, entries, "Object");
}

// ── Page (artboard) context menu ──────────────────────────────────────────────
// Right-clicking a page's name label opens this. Lets the user pick how the .acu
// thumbnail is framed (whole page, or an interactive crop zone) and resize the
// page. `pageCtxArtboard_` was captured when the menu was requested.
void Application::RenderPageContextMenu() {
    const int ab = pageCtxArtboard_;
    const bool valid = ab >= 0 && ab < (int)project_.artboards().size();

    std::vector<UI::MenuEntry> entries;
    {
        UI::MenuEntry def; def.label = "Define Thumbnail"; def.icon = "image";
        def.enabled = valid;
        {
            UI::MenuEntry e; e.label = "Whole Page";
            e.tooltip = "Use the whole page as the .acu thumbnail";
            e.onClick = [this, ab]{
                Action_UpdateThumbnail(ab, {0, 0}, {0, 0});   // whole artboard
            };
            def.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Zone…";
            e.tooltip = "Draw a crop rectangle to frame the thumbnail";
            e.onClick = [this, ab]{ BeginThumbnailCrop(ab); };
            def.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(def));
    }
    {
        UI::MenuEntry e; e.label = "Rename Page…"; e.icon = "label";
        e.enabled = valid;
        e.tooltip = "Rename this page";
        e.onClick = [this, ab]{
            renamePageRequest_ = true; renamePageArtboard_ = ab;
            if (ab >= 0 && ab < (int)project_.artboards().size())
                std::snprintf(renamePageBuf_, sizeof(renamePageBuf_), "%s",
                              project_.artboards()[(size_t)ab].name.c_str());
        };
        entries.push_back(std::move(e));
    }
    {
        UI::MenuEntry e; e.label = "Resize Page…"; e.icon = "image-aspect-ratio";
        e.enabled = valid;
        e.tooltip = "Change this page's dimensions";
        e.onClick = [this, ab]{ resizePageRequest_ = true; resizePageArtboard_ = ab; };
        entries.push_back(std::move(e));
    }
    {
        // Toggle: clip this page's objects to its bounds (overflow into the void
        // is hidden — only the selection outline shows it). Per-page, off default.
        bool on = valid && project_.artboards()[(size_t)ab].clipContents;
        UI::MenuEntry e;
        e.label = std::string(on ? "[x] " : "[ ] ") + "Clip Contents to Page";
        e.tooltip = "Clip this page's objects to its bounds (hide overflow)";
        e.enabled = valid;
        e.onClick = [this, ab]{
            if (ab >= 0 && ab < (int)project_.artboards().size()) {
                auto& a = project_.artboards()[(size_t)ab];
                a.clipContents = !a.clipContents;
                MarkUndoLabel("Toggle page clip");
                project_.dirty = true;
            }
        };
        entries.push_back(std::move(e));
    }

    UI::ContextMenu("##pageCtx", pageCtxPos_, entries, "Page");
}

// Enter the interactive "Zone" thumbnail crop for `artboard`: seed a centred
// rectangle (60% of the page) the user can then move/resize, and arm a fresh
// drag so an immediate press-drag redefines it from scratch. Enter confirms.
void Application::BeginThumbnailCrop(int artboard) {
    if (artboard < 0 || artboard >= (int)project_.artboards().size()) return;
    const Renderer::Artboard& art = project_.artboards()[(size_t)artboard];
    cropArtboard_ = artboard;
    Renderer::Vec2 c{ art.pos.x + art.size.x * 0.5f,
                      art.pos.y + art.size.y * 0.5f };
    Renderer::Vec2 h{ art.size.x * 0.3f, art.size.y * 0.3f };
    cropMin_ = { c.x - h.x, c.y - h.y };
    cropMax_ = { c.x + h.x, c.y + h.y };
    cropDrag_ = -1;        // a press redefines/moves/resizes; nothing held yet
    cropOwner_ = nullptr;  // the first hovered Viewport leaf will claim it
}

// Drive the interactive crop overlay. Coordinates: the crop rect is stored in
// doc-units; `d2sDoc`/`s2dDoc` map doc-units ↔ screen (already unit-scaled by
// the caller, so we pass plain doc-units). Press-drag in empty space redefines
// the rect; dragging a corner/edge handle resizes; dragging inside moves it.
// Enter renders the thumbnail from the rect; Esc or RMB cancels.
void Application::HandleThumbnailCrop(const std::function<ImVec2(Renderer::Vec2)>& d2sDoc,
                                      const std::function<Renderer::Vec2(ImVec2)>& s2dDoc,
                                      float /*pxPer*/, bool hovered,
                                      ImDrawList* dl) {
    if (cropArtboard_ < 0) return;
    ImGuiIO& io = ImGui::GetIO();
    auto& ds = DesignSystem::DesignSystem::Instance();

    // Cancel (Esc / RMB) — leave the thumbnail untouched.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        cropArtboard_ = -1; cropDrag_ = -1; cropOwner_ = nullptr;
        rmbConsumedByTransform_ = true;   // swallow this RMB (no context menu)
        return;
    }
    // Confirm (Enter) — render the thumbnail from the normalised rect.
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        Renderer::Vec2 mn{ std::min(cropMin_.x, cropMax_.x),
                           std::min(cropMin_.y, cropMax_.y) };
        Renderer::Vec2 mx{ std::max(cropMin_.x, cropMax_.x),
                           std::max(cropMin_.y, cropMax_.y) };
        int ab = cropArtboard_;
        cropArtboard_ = -1; cropDrag_ = -1; cropOwner_ = nullptr;
        Action_UpdateThumbnail(ab, mn, { mx.x - mn.x, mx.y - mn.y });
        return;
    }

    // Normalised rect (doc-units) → screen, recomputed each frame.
    auto screenRect = [&](ImVec2& r0, ImVec2& r1) {
        Renderer::Vec2 mn{ std::min(cropMin_.x, cropMax_.x),
                           std::min(cropMin_.y, cropMax_.y) };
        Renderer::Vec2 mx{ std::max(cropMin_.x, cropMax_.x),
                           std::max(cropMin_.y, cropMax_.y) };
        ImVec2 a = d2sDoc(mn), b = d2sDoc(mx);
        r0 = ImVec2(std::min(a.x, b.x), std::min(a.y, b.y));
        r1 = ImVec2(std::max(a.x, b.x), std::max(a.y, b.y));
    };
    ImVec2 r0, r1; screenRect(r0, r1);

    // Handle layout (screen px). hx/hy: which side each axis drives (-1 = min,
    // +1 = max, 0 = none). 0..3 corners, 4..7 edge midpoints.
    const float kHalf = 4.0f * ds.GetGlobalScale();   // handle half-size (small)
    const float kHit  = 7.0f * ds.GetGlobalScale();   // hit half-size (a bit larger)
    struct H { ImVec2 p; int hx; int hy; };
    ImVec2 mid((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    H handles[8] = {
        {{r0.x, r0.y}, -1, -1}, {{r1.x, r0.y}, +1, -1},
        {{r0.x, r1.y}, -1, +1}, {{r1.x, r1.y}, +1, +1},
        {{mid.x, r0.y},  0, -1}, {{mid.x, r1.y},  0, +1},
        {{r0.x, mid.y}, -1,  0}, {{r1.x, mid.y}, +1,  0},
    };

    // ── Begin a drag: pick handle / body / empty, and anchor to the cursor ────
    if (hovered && cropDrag_ == -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int pick = -1;
        for (int i = 0; i < 8; ++i)
            if (std::fabs(io.MousePos.x - handles[i].p.x) <= kHit &&
                std::fabs(io.MousePos.y - handles[i].p.y) <= kHit) { pick = i; break; }
        if (pick < 0) {
            bool inside = io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                          io.MousePos.y >= r0.y && io.MousePos.y <= r1.y;
            pick = inside ? -2 : -3;          // -2 move, -3 define a new rect
        }
        cropDrag_      = pick;
        cropDragRef_   = s2dDoc(io.MousePos);   // grab anchor (doc-units)
        cropRect0Min_  = cropMin_;
        cropRect0Max_  = cropMax_;
        cropEdgeX_ = cropEdgeY_ = 0;
        if (pick == -3) {                       // define: start a zero rect here
            cropMin_ = cropMax_ = cropDragRef_;
            cropRect0Min_ = cropRect0Max_ = cropDragRef_;
        } else if (pick >= 0) {
            // Map the visual handle side to the REAL component it grabs. The
            // visual "left" edge (hx<0) is the smaller screen-X = the smaller
            // doc-X component; capture which of min/max that is NOW, so the held
            // edge keeps following the cursor even if the rect later inverts.
            const H& h = handles[pick];
            if (h.hx < 0) cropEdgeX_ = (cropMin_.x <= cropMax_.x) ? -1 : +1;
            else if (h.hx > 0) cropEdgeX_ = (cropMin_.x <= cropMax_.x) ? +1 : -1;
            if (h.hy < 0) cropEdgeY_ = (cropMin_.y <= cropMax_.y) ? -1 : +1;
            else if (h.hy > 0) cropEdgeY_ = (cropMin_.y <= cropMax_.y) ? +1 : -1;
        }
    }

    // ── Continue the drag: apply (cursor − anchor) to the rect at grab time ───
    if (cropDrag_ != -1) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Renderer::Vec2 cur = s2dDoc(io.MousePos);
            float dx = cur.x - cropDragRef_.x, dy = cur.y - cropDragRef_.y;
            if (cropDrag_ == -3) {              // define: opposite corner follows
                cropMin_ = cropRect0Min_;
                cropMax_ = cur;
            } else if (cropDrag_ == -2) {       // move the whole rect by the delta
                cropMin_ = { cropRect0Min_.x + dx, cropRect0Min_.y + dy };
                cropMax_ = { cropRect0Max_.x + dx, cropRect0Max_.y + dy };
            } else {                            // resize: move only the held REAL
                cropMin_ = cropRect0Min_; cropMax_ = cropRect0Max_;  // component
                if      (cropEdgeX_ < 0) cropMin_.x = cropRect0Min_.x + dx;
                else if (cropEdgeX_ > 0) cropMax_.x = cropRect0Max_.x + dx;
                if      (cropEdgeY_ < 0) cropMin_.y = cropRect0Min_.y + dy;
                else if (cropEdgeY_ > 0) cropMax_.y = cropRect0Max_.y + dy;
            }
        } else {
            cropDrag_ = -1;                     // released
        }
    }

    // ── Draw: dim outside the (normalised, clamped) rect, frame, handles, hint ─
    screenRect(r0, r1);   // refresh after edits this frame
    mid = ImVec2((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    handles[0].p = {r0.x, r0.y}; handles[1].p = {r1.x, r0.y};
    handles[2].p = {r0.x, r1.y}; handles[3].p = {r1.x, r1.y};
    handles[4].p = {mid.x, r0.y}; handles[5].p = {mid.x, r1.y};
    handles[6].p = {r0.x, mid.y}; handles[7].p = {r1.x, mid.y};

    ImU32 accent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    ImVec2 cMin = gestureCanvasMin_, cMax = gestureCanvasMax_;
    ImU32 dim = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_ZoneOverlay_TransformDim));
    ImVec2 q0(std::max(r0.x, cMin.x), std::max(r0.y, cMin.y));
    ImVec2 q1(std::min(r1.x, cMax.x), std::min(r1.y, cMax.y));
    dl->AddRectFilled(cMin, ImVec2(cMax.x, q0.y), dim);               // top band
    dl->AddRectFilled(ImVec2(cMin.x, q1.y), cMax, dim);              // bottom band
    dl->AddRectFilled(ImVec2(cMin.x, q0.y), ImVec2(q0.x, q1.y), dim); // left
    dl->AddRectFilled(ImVec2(q1.x, q0.y), ImVec2(cMax.x, q1.y), dim); // right
    dl->AddRect(q0, q1, accent, 0.0f, 0, 1.5f);
    for (int i = 0; i < 8; ++i)
        dl->AddRectFilled(ImVec2(handles[i].p.x - kHalf, handles[i].p.y - kHalf),
                          ImVec2(handles[i].p.x + kHalf, handles[i].p.y + kHalf),
                          accent, 1.0f);
    const char* hint = "Thumbnail crop — drag to adjust, Enter to confirm, Esc to cancel";
    ImU32 txt = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    dl->AddText(ImVec2(cMin.x + 8.0f, cMin.y + 8.0f), txt, hint);
}

// ── Modules::ModuleHost::CreateObjectSpec ─────────────────────────────────────
// Build a real Shape from a module's ObjectSpec (geometry + colour) and place it
// at the 2D cursor, exactly like Shift+A — so it is then selectable / movable /
// deletable. `loose` objects are page-less (the IOF overprint/course layer);
// others land on the active page. Returns the new shape id (0 on failure).
uint64_t Application::CreateObjectSpec(const Modules::ObjectSpec& spec) {
    // Preview placement: arm a cursor-following preview (the click commits). The
    // guard lets UpdatePlacement re-enter to actually create the object.
    if (!placementCommitting_ && PreviewPlacementEnabled()) {
        RequestPlacementSpec(spec);
        return 0;
    }
    using K = Renderer::ShapeKind;
    auto& doc = project_.document;
    const Renderer::Color col{ spec.r, spec.g, spec.b, spec.a };
    const float R = std::max(2.0f, spec.size) * 0.5f;

    Renderer::Shape s;
    switch (spec.geom) {
        case Modules::ObjectSpec::Geom::Area: {
            // Filled rectangle (e.g. lake, forest, open land).
            s = MakeShape(K::Rectangle);
            s.MainPart().pos = { -R, -R }; s.MainPart().size = { 2 * R, 2 * R };
            s.MainPart().fill.enabled = true;  s.MainPart().fill.color   = col;
            s.MainPart().stroke.enabled = false;
            break;
        }
        case Modules::ObjectSpec::Geom::Line: {
            // Short 2-point stroke (e.g. contour, path, fence).
            s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
            Renderer::Node a({ -R, 0 }); Renderer::Node b({ R, 0 });
            a.mode = b.mode = Renderer::HandleMode::Vector;
            s.MainPart().path.nodes = { a, b };
            s.MainPart().path.closed   = false;
            s.MainPart().fill.enabled  = false;
            s.MainPart().stroke.enabled = true;  s.MainPart().stroke.color = col;
            s.MainPart().stroke.width   = std::max(2.0f, R * 0.18f);
            break;
        }
        case Modules::ObjectSpec::Geom::Point:
        default: {
            // Small filled disc (most point symbols, and the course controls which
            // are an unfilled ring → stroke only).
            s = MakeShape(K::Ellipse);
            s.MainPart().pos = { -R, -R }; s.MainPart().size = { 2 * R, 2 * R };
            // A control/start/finish reads as a RING (stroke, no fill).
            const bool ring = spec.name.rfind("Control", 0) == 0 ||
                              spec.name == "Start" || spec.name == "Finish";
            s.MainPart().fill.enabled   = !ring; s.MainPart().fill.color   = col;
            s.MainPart().stroke.enabled = ring;  s.MainPart().stroke.color = col;
            s.MainPart().stroke.width   = std::max(2.0f, R * 0.22f);
            break;
        }
    }
    s.name = spec.name;
    s.lockScale    = spec.lockScale;     // fixed-size symbol
    s.lockRotation = spec.lockRotation;  // north-oriented symbol
    s.collectionId = spec.collectionId;  // IOF print-layer collection (0 = none)
    CenterOrigin(s);

    // Place at the 2D cursor (display space), like Shift+A. Loose → raw doc space;
    // otherwise relative to the active page's display origin in this viewport.
    int ab = -1;
    if (!spec.loose && doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());
    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{ 0, 0 };
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    uint64_t id = AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preview placement
// ─────────────────────────────────────────────────────────────────────────────

bool Application::PreviewPlacementEnabled() {
    if (activeCapabilities_.previewPlacement) return true;   // module forces it
    // Bool01 token (stored as Int), not a float — GetInt, else it throws.
    return DesignSystem::DesignSystem::Instance()
               .GetInt(DesignSystem::Tok::S_Config_PreviewPlacement) != 0;
}

void Application::RequestPlacementCore(const std::string& kind) {
    placement_          = {};             // reset previous arming
    placement_.armed    = true;
    placement_.source   = PlacementSource::Core;
    placement_.coreKind = kind;
}

void Application::RequestPlacementSpec(const Modules::ObjectSpec& spec) {
    placement_        = {};
    placement_.armed  = true;
    placement_.source = PlacementSource::ModuleSpec;
    placement_.spec   = spec;
}

void Application::RequestPlacementBaked(const Renderer::Shape& shape, bool loose,
                                       uint64_t coll, Modules::ModuleHost::PlaceMode mode) {
    const bool alreadyArmed = placement_.armed;
    // Switching symbols mid-placement: abandon any in-progress STYLED curve so the
    // draw branch re-seeds with the NEW symbol (otherwise the old template kept
    // drawing + the old mini-ghost stayed). Harmless when no gesture is active
    // (e.g. the re-arm after FinishCurveGesture already reset it).
    if (toolState_.styleActive) toolState_.Reset();
    placement_            = {};
    placement_.armed      = true;
    placement_.source     = PlacementSource::Baked;
    placement_.baked      = shape;
    placement_.bakedLoose = loose;
    placement_.bakedColl  = coll;
    placement_.mode       = mode;
    // Remember the tool to restore on cancel (only on the FIRST arm — re-arms of an
    // infinite placement keep the original previous tool). Then drop the active
    // tool: the placement preview owns the input until cancelled / committed.
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    if (!alreadyArmed) placementPrevTool_ = tm.GetActiveTool();
    tm.SetActiveTool("");
}

// Cancel an armed placement (and any in-progress styled-curve gesture) and restore
// the tool the user had before they picked the symbol.
void Application::EndPlacement() {
    placement_.armed = false;
    if (toolState_.styleActive) toolState_.Reset();
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool(placementPrevTool_);
    placementPrevTool_.clear();
}

void Application::SetPlacementPreview(const Renderer::Shape& preview) {
    placement_.bakedPreview = preview;
    placement_.hasPreview   = true;
}

int Application::ArmedSymbolCode() const {
    // Armed via the placement preview, OR mid-draw of a styled curve (the gesture
    // owns the symbol then). Either way report the symbol's ISOM code.
    if (placement_.armed && placement_.source == PlacementSource::Baked)
        return placement_.baked.isomCode;
    if (toolState_.styleActive) return toolState_.styleTemplate.isomCode;
    return 0;
}

// A thin crosshair centred on the mouse with a 1px hole = the exact drop point.
// Drawn with a dark halo + bright core so it reads on both the page and the
// canvas background. Hides the OS cursor (same technique as ShowMoveCursor).
void Application::ShowCrosshairCursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const ImU32 core = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_Crosshair));
    const ImU32 halo = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorTick));
    const float arm = 12.0f, hole = 1.5f;
    auto cross = [&](ImU32 c, float t) {
        fg->AddLine(ImVec2(mp.x - arm, mp.y), ImVec2(mp.x - hole, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x + hole, mp.y), ImVec2(mp.x + arm, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y - arm), ImVec2(mp.x, mp.y - hole), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y + hole), ImVec2(mp.x, mp.y + arm), c, t);
    };
    cross(halo, 3.0f);   // dark outline for contrast
    cross(core, 1.0f);   // thin bright core (1px hole at centre = drop point)
}

bool Application::UpdatePlacement(EditorState& st,
        const std::function<Renderer::Vec2(ImVec2)>& s2d,
        const std::function<ImVec2(Renderer::Vec2)>& d2s,
        float effZoom, bool hovered, ImDrawList* dl) {
    (void)st;
    ImGuiIO& io = ImGui::GetIO();

    // Cancel: Esc or right-click drops the arming and restores the previous tool.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        EndPlacement();
        rmbConsumedByTransform_ = true;
        return true;
    }
    if (!hovered) return true;   // owns the input but only draws over a canvas

    const ImVec2  mp   = io.MousePos;
    const Renderer::Vec2 mDoc = s2d(mp);

    // ── Line / area symbols: DRAW the geometry point-by-point ─────────────────
    // The baked shape is a STYLE TEMPLATE; we hand off to the core Curve tool but
    // tag the gesture so it carries the symbol's style and re-arms when finished.
    if (placement_.source == PlacementSource::Baked &&
        (placement_.mode == Modules::ModuleHost::PlaceMode::DrawLine ||
         placement_.mode == Modules::ModuleHost::PlaceMode::DrawArea)) {
        const bool closed = (placement_.mode == Modules::ModuleHost::PlaceMode::DrawArea);
        // Seed/continue the styled curve gesture on this leaf.
        if (!toolState_.Active()) {
            toolState_.Reset();
            toolState_.gesture       = ToolGesture::Bezier;
            toolState_.owner         = &st;
            toolState_.styleActive   = true;
            toolState_.styleClosed   = closed;
            toolState_.styleLoose    = placement_.bakedLoose;
            toolState_.styleColl     = placement_.bakedColl;
            toolState_.styleTemplate = placement_.baked;
            toolState_.stylePreview  = placement_.hasPreview ? placement_.bakedPreview
                                                             : placement_.baked;
            toolState_.styleHasPreview = true;
            toolState_.targetArtboard = project_.document.ActivePage()
                ? project_.document.ArtboardIndexById(project_.document.ActivePage()) : -1;
        }
        // The curve tool runs the whole authoring (it owns the gesture from here);
        // it draws its own preview. We keep `placement_.armed` so this branch is
        // re-entered each frame until the curve tool finishes (which re-arms us).
        HandleCurveTool(st, s2d, d2s, effZoom, hovered, dl);
        // A tiny ghost of the symbol bottom-right of the cursor (which symbol).
        DrawPlacementMiniGhost(toolState_.stylePreview, mp, effZoom);
        return true;
    }

    // ── Crossing point (519) onto a compatible curve ──────────────────────────
    // The crossing symbol is not a free object: dropping it on a wall/fence/
    // prominent-line inserts a Crossing mark that CUTS that line. Hold Ctrl to
    // place it freely anywhere instead (the normal baked-object path below).
    if (placement_.source == PlacementSource::Baked &&
        placement_.baked.isomCode == 5190 && !io.KeyCtrl) {
        struct CH { uint64_t sid = 0; int part = -1; int sub = 0; float t = 0.5f;
                    float dpx = 1e9f; Renderer::Vec2 p{0,0}, tan{1,0}; } best;
        const float zoom = std::max(0.0001f, effZoom);
        auto consider = [&](const Renderer::Shape& s) {
            if (!s.visible || !CrossingAllowedOn(s.isomCode)) return;
            Renderer::Vec2 po = CurPageOriginOfShape(s.id);
            for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
                const Renderer::Part& part = s.parts[(size_t)pi];
                if (!part.stroke.enabled) continue;
                int subs = Renderer::Tessellator::SubpathCount(part);
                for (int subi = 0; subi < subs; ++subi) {
                    bool cl = false;
                    auto poly = Renderer::Tessellator::OutlinePartSub(s, part, subi, zoom, cl, po);
                    if (poly.size() < 2) continue;
                    float total = 0.0f; size_t n = poly.size(), sc = cl ? n : n - 1;
                    for (size_t i = 0; i < sc; ++i)
                        total += std::hypot(poly[(i+1)%n].x - poly[i].x, poly[(i+1)%n].y - poly[i].y);
                    float acc = 0.0f;
                    for (size_t i = 0; i < sc; ++i) {
                        Renderer::Vec2 a = poly[i], b = poly[(i+1)%n];
                        Renderer::Vec2 ab{ b.x - a.x, b.y - a.y };
                        float segLen = std::hypot(ab.x, ab.y); if (segLen < 1e-6f) continue;
                        float u = std::clamp(((mDoc.x-a.x)*ab.x + (mDoc.y-a.y)*ab.y)/(segLen*segLen), 0.0f, 1.0f);
                        Renderer::Vec2 proj{ a.x + ab.x*u, a.y + ab.y*u };
                        float dpx = std::hypot(mp.x - d2s(proj).x, mp.y - d2s(proj).y);
                        if (dpx < best.dpx)
                            best = { s.id, pi, subi, total > 1e-4f ? (acc+segLen*u)/total : 0.0f,
                                     dpx, proj, { ab.x/segLen, ab.y/segLen } };
                        acc += segLen;
                    }
                }
            }
        };
        for (const auto& ab : project_.document.artboards)
            for (const Renderer::Shape& s : ab.shapes) consider(s);
        for (const Renderer::Shape& s : project_.document.looseShapes) consider(s);

        if (best.part >= 0 && best.dpx <= 14.0f) {
            Renderer::Shape* sp = project_.document.FindShape(best.sid);
            if (sp && best.part < (int)sp->parts.size()) {
                const float scl = activeCapabilities_.symbolScale > 0.01f
                                      ? activeCapabilities_.symbolScale : 1.0f;
                Renderer::Part& part = sp->parts[(size_t)best.part];
                Renderer::LineMark m; m.kind = Renderer::LineMarkKind::Crossing;
                m.sub = best.sub; m.t = best.t;
                ApplyMarkPreset(m, sp->isomCode, scl);
                DrawLineMarkGhost(*sp, part, m, best.p, best.tan, d2s, zoom);
                ShowCrosshairCursor();
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    part.marks.push_back(m);
                    project_.document.SelectOnly(best.sid);
                    project_.document.SetActive(best.sid);
                    MarkUndoLabel("Add crossing point");
                    project_.dirty = true;
                    // Stay armed → place more crossings; Esc / right-click ends it.
                }
                return true;   // snapped to a curve → don't draw/commit a free object
            }
        }
        // Not over a compatible curve and Ctrl is up → a crossing can ONLY go on a
        // line here. Show the crosshair but DON'T place a free object; the user
        // must hover a wall/fence/prominent line (or hold Ctrl to place freely).
        ShowCrosshairCursor();
        return true;
    }

    // ── Ghost preview at the mouse (follows it like G/move) ───────────────────
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float alpha = ds.GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);

    if (placement_.source == PlacementSource::Baked) {
        // The exact baked glyph as a SMOOTH (SSAA) transparent Vulkan texture, so
        // the ghost has no triangle seams and matches the placed object. Sized to
        // the glyph's on-screen extent; blitted at the mouse, tinted to the preview
        // alpha. Falls back to a CPU triangle blit if texture rendering is absent.
        Renderer::Vec2 bmn, bmx;
        bool gotB = Renderer::Tessellator::WorldBounds(placement_.baked, 1.0f, bmn, bmx, {0,0});
        ImTextureID tex = 0; ImVec2 imgMin, imgMax;
        if (gotB) {
            float wpxF = (bmx.x - bmn.x) * effZoom, hpxF = (bmx.y - bmn.y) * effZoom;
            int wpx = std::max(4, (int)std::lround(wpxF));
            int hpx = std::max(4, (int)std::lround(hpxF));
            // padFrac 0 → the content fills the texture (1:1 with the on-screen
            // extent), so the ghost is the right size.
            uint64_t key = 0x6A05u;                 // single reused ghost slot
            // RenderGlyphTexture is now PROCEDURAL (GPU fills + instanced decor), so
            // the full symbol — patterns included — renders fast and shows in the ghost.
            uint64_t chash = Renderer::Tessellator::HashShape(placement_.baked, {0,0})
                           ^ ((uint64_t)wpx << 8) ^ ((uint64_t)hpx << 24);
            std::vector<Renderer::Shape> shapes = { placement_.baked };
            tex = RenderGlyphTexture(key, chash, shapes, wpx, hpx, 0.0f, /*transparent=*/true);
            // The texture is centred on the content centre; place it so the glyph's
            // local origin {0,0} lands on the mouse.
            Renderer::Vec2 c{ (bmn.x+bmx.x)*0.5f, (bmn.y+bmx.y)*0.5f };
            ImVec2 texCtr(mp.x + c.x * effZoom, mp.y + c.y * effZoom);
            imgMin = ImVec2(texCtr.x - wpx*0.5f, texCtr.y - hpx*0.5f);
            imgMax = ImVec2(texCtr.x + wpx*0.5f, texCtr.y + hpx*0.5f);
        }
        if (tex) {
            dl->AddImage(tex, imgMin, imgMax, ImVec2(0,0), ImVec2(1,1),
                         ImGui::GetColorU32(ImVec4(1,1,1, alpha)));
        } else {
            // CPU fallback (AA fill off to avoid seams). Strip fill PATTERNS — the
            // per-frame legacy bake of a patterned area is O(steps²) and would freeze
            // the placement ghost; the motif appears (GPU-fast) once placed.
            Renderer::Shape bakedGhost = placement_.baked;
            for (Renderer::Part& p : bakedGhost.parts) p.fillLayers.clear();
            Renderer::Mesh mesh;
            Renderer::Tessellator::AppendShape(bakedGhost, mesh, effZoom, {0, 0});
            const auto& vtx = mesh.vertices;
            const ImDrawListFlags savedAA = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (size_t i = 0; i + 3 <= vtx.size(); i += 3) {
                ImVec2 p[3]; const Renderer::Vertex& v0 = vtx[i];
                for (int k = 0; k < 3; ++k) {
                    const Renderer::Vertex& v = vtx[i + (size_t)k];
                    p[k] = ImVec2(mp.x + v.x * effZoom, mp.y + v.y * effZoom);
                }
                dl->AddTriangleFilled(p[0], p[1], p[2],
                                      ImGui::GetColorU32(ImVec4(v0.r, v0.g, v0.b, v0.a * alpha)));
            }
            dl->Flags = savedAA;
        }
    } else if (placement_.source == PlacementSource::ModuleSpec) {
        const float R = std::max(2.0f, placement_.spec.size) * 0.5f;
        const float rpx = std::max(3.0f, R * effZoom);
        ImVec4 gc{ placement_.spec.r, placement_.spec.g, placement_.spec.b, alpha };
        const ImU32 col = ImGui::GetColorU32(gc);
        switch (placement_.spec.geom) {
            case Modules::ObjectSpec::Geom::Area:
                dl->AddRectFilled(ImVec2(mp.x - rpx, mp.y - rpx), ImVec2(mp.x + rpx, mp.y + rpx), col); break;
            case Modules::ObjectSpec::Geom::Line:
                dl->AddLine(ImVec2(mp.x - rpx, mp.y), ImVec2(mp.x + rpx, mp.y), col,
                            std::max(2.0f, placement_.spec.size * 0.18f * effZoom)); break;
            default:
                dl->AddCircleFilled(mp, rpx, col, 32); break;
        }
    } else {
        const float rpx = std::max(3.0f, 100.0f * effZoom);
        ImVec4 gc = ds.GetColor(DesignSystem::Tok::C_Viewport_Crosshair); gc.w = alpha;
        const ImU32 col = ImGui::GetColorU32(gc);
        if (placement_.coreKind == "bezier" || placement_.coreKind == "nurbs")
            dl->AddLine(ImVec2(mp.x - rpx, mp.y), ImVec2(mp.x + rpx, mp.y), col, 2.0f);
        else
            dl->AddRect(ImVec2(mp.x - rpx, mp.y - rpx), ImVec2(mp.x + rpx, mp.y + rpx), col, 0.0f, 0, 1.5f);
    }
    ShowCrosshairCursor();

    // ── Commit on left click ──────────────────────────────────────────────────
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Renderer::Vec2 saved = project_.document.cursor;
        project_.document.cursor = mDoc;
        placementCommitting_ = true;
        switch (placement_.source) {
            case PlacementSource::Baked:
                AddBakedShape(placement_.baked, placement_.bakedLoose, placement_.bakedColl); break;
            case PlacementSource::ModuleSpec:
                CreateObjectSpec(placement_.spec); break;
            case PlacementSource::Core:
                Action_AddShape(placement_.coreKind); break;
        }
        placementCommitting_ = false;
        project_.document.cursor = saved;
        // Baked symbols (the IOF catalogue) place INFINITELY — stay armed so the
        // user can drop another. Esc / right-click ends it. Core/ModuleSpec keep
        // the classic single-shot behaviour.
        if (placement_.source != PlacementSource::Baked) placement_.armed = false;
    }
    return true;
}

// ── Modules::ModuleHost::AddBakedShape ────────────────────────────────────────
// Place a fully-baked shape (parts authored in local doc units, centred at the
// local origin) at the 2D cursor — like CreateObjectSpec, but the geometry is
// supplied by the caller (the IOF exact glyph builder). Honours preview placement.
uint64_t Application::AddBakedShape(const Renderer::Shape& shape,
                                    bool loose, uint64_t collectionId,
                                    Modules::ModuleHost::PlaceMode mode) {
    // Line/area symbols are always DRAWN point-by-point (even without the preview
    // pref) — they have no fixed geometry to stamp. Points honour the pref.
    const bool drawCurve = (mode == Modules::ModuleHost::PlaceMode::DrawLine ||
                            mode == Modules::ModuleHost::PlaceMode::DrawArea);
    if (!placementCommitting_ && (drawCurve || PreviewPlacementEnabled())) {
        RequestPlacementBaked(shape, loose, collectionId, mode);
        return 0;
    }
    auto& doc = project_.document;
    Renderer::Shape s = shape;
    s.collectionId = collectionId;
    int ab = -1;
    if (!loose && doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());
    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{ 0, 0 };
    // Geometry is centred at origin {0,0}; land that origin at the 2D cursor.
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    uint64_t id = AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
    return id;
}

} // namespace App

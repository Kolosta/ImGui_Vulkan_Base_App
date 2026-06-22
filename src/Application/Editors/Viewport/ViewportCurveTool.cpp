#include "Application.h"
#include "ViewportToolsShared.h"
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
    target.lockPosX    = tpl.lockPosX;
    target.lockPosY    = tpl.lockPosY;
    target.lockScaleX  = tpl.lockScaleX;
    target.lockScaleY  = tpl.lockScaleY;
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


} // namespace App

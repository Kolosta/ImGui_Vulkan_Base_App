#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Ink/View/OverlayList.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Follow-curve (Shift while the pen is active) — a faithful port of the legacy
//  "follow path" (OpenOrienteering-Mapper style) onto the Ink model.
//
//  A FollowCurve is one subpath of a target node, baked into DOCUMENT-space
//  Bézier nodes (positions + ABSOLUTE in/out handles). It supports:
//    • projecting a cursor point → the nearest point on the curve (arc length),
//    • extracting the exact Bézier nodes between two arc lengths (De Casteljau
//      split at both ends) so the drawn segment copies the target geometry 1:1.
//
//  Two phases:
//    • NOT LOCKED → a blue diamond at the nearest curve ENTRY (within a pickup
//      radius); a click places that entry point and locks onto the curve.
//    • LOCKED → project the cursor onto the LOCKED curve (no distance limit);
//      preview traces it anchor→projection; a click freezes the traced piece
//      (exact target nodes) onto the pen and re-anchors there.
//
//  The pen node is built with an identity transform in DOCUMENT coordinates
//  while drawing (see BeginPenDraw), so document space == the node's local space:
//  the extracted document-space anchors append to the pen path directly.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

namespace {

using V2 = Ink::DVec2;

struct WNode { V2 pos, hIn, hOut; bool hasIn, hasOut; };

// Evaluate the cubic of node-segment [a,b] at u (straight when a handle absent).
V2 CubicAt(const WNode& a, const WNode& b, double u) {
    V2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
    V2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
    double v = 1.0 - u;
    double b0 = v*v*v, b1 = 3*v*v*u, b2 = 3*v*u*u, b3 = u*u*u;
    return { b0*p0.x + b1*p1.x + b2*p2.x + b3*p3.x,
             b0*p0.y + b1*p1.y + b2*p2.y + b3*p3.y };
}

struct FollowCurve {
    std::vector<WNode> n;
    bool               closed = false;
    bool valid() const { return n.size() >= 2; }
    int  segCount() const { return closed ? (int)n.size() : (int)n.size() - 1; }
    void seg(int k, WNode& a, WNode& b) const {
        a = n[(std::size_t)k]; b = n[(std::size_t)((k + 1) % n.size())];
    }

    // Build the document-space nodes of subpath `sub` of `node` (world matrix w).
    static FollowCurve Build(const Ink::Node& node, int sub, const Ink::DMat23& w) {
        FollowCurve fc;
        if (sub < 0 || sub >= (int)node.path.subpaths.size()) return fc;
        const Ink::Subpath& sp = node.path.subpaths[(std::size_t)sub];
        fc.closed = sp.closed;
        for (const Ink::Anchor& a : sp.anchors) {
            WNode wn;
            wn.pos   = w.Apply(a.pos);
            wn.hasIn = a.hasIn; wn.hasOut = a.hasOut;
            wn.hIn   = w.Apply({ a.pos.x + a.in.x,  a.pos.y + a.in.y });
            wn.hOut  = w.Apply({ a.pos.x + a.out.x, a.pos.y + a.out.y });
            fc.n.push_back(wn);
        }
        return fc;
    }

    static double SegLen(const WNode& a, const WNode& b, double uEnd) {
        const int FL = 24; double acc = 0.0; V2 prev = a.pos;
        for (int i = 1; i <= FL; ++i) {
            double u = uEnd * (double)i / (double)FL;
            V2 cur = CubicAt(a, b, u);
            acc += std::hypot(cur.x - prev.x, cur.y - prev.y); prev = cur;
        }
        return acc;
    }
    double TotalArc() const {
        double acc = 0.0; WNode a, b;
        for (int k = 0; k < segCount(); ++k) { seg(k, a, b); acc += SegLen(a, b, 1.0); }
        return acc;
    }
    double ArcOf(int s, double u) const {
        double acc = 0.0; WNode a, b;
        for (int k = 0; k < s; ++k) { seg(k, a, b); acc += SegLen(a, b, 1.0); }
        seg(s, a, b);
        return acc + SegLen(a, b, u);
    }
    // Project `p` onto the curve: squared distance + (segment, u, closest point).
    double Project(V2 p, int& segOut, double& uOut, V2& closest) const {
        const int FL = 16;
        double best = 1e300; segOut = 0; uOut = 0; closest = n[0].pos;
        WNode a, b;
        for (int k = 0; k < segCount(); ++k) {
            seg(k, a, b);
            V2 prev = a.pos; double pu = 0.0;
            for (int i = 1; i <= FL; ++i) {
                double u = (double)i / (double)FL;
                V2 cur = CubicAt(a, b, u);
                V2 ab{ cur.x - prev.x, cur.y - prev.y };
                double L2 = ab.x*ab.x + ab.y*ab.y;
                double t = L2 > 1e-12 ? std::clamp(
                    ((p.x-prev.x)*ab.x + (p.y-prev.y)*ab.y)/L2, 0.0, 1.0) : 0.0;
                V2 c{ prev.x + ab.x*t, prev.y + ab.y*t };
                double d = (p.x-c.x)*(p.x-c.x) + (p.y-c.y)*(p.y-c.y);
                if (d < best) { best = d; segOut = k; uOut = pu + (u - pu) * t; closest = c; }
                prev = cur; pu = u;
            }
        }
        return best;
    }
    double ProjectArc(V2 p, V2& closest) const {
        int s; double u; Project(p, s, u, closest);
        return ArcOf(s, u);
    }
    void LocOfArc(double arc, int& segOut, double& uOut) const {
        double acc = 0.0; WNode a, b;
        for (int k = 0; k < segCount(); ++k) {
            seg(k, a, b); double L = SegLen(a, b, 1.0);
            if (arc <= acc + L || k == segCount() - 1) {
                segOut = k;
                double target = std::clamp(arc - acc, 0.0, L);
                double lo = 0.0, hi = 1.0;
                for (int it = 0; it < 18; ++it) {
                    double mid = 0.5*(lo+hi);
                    (SegLen(a, b, mid) < target) ? lo = mid : hi = mid;
                }
                uOut = 0.5*(lo+hi); return;
            }
            acc += L;
        }
        segOut = segCount() - 1; uOut = 1.0;
    }
    static void SplitLeft(const WNode& a, const WNode& b, double u,
                          V2& P0, V2& P1, V2& P2, V2& P3) {
        V2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
        V2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
        auto L = [&](V2 A, V2 B){ return V2{ A.x+(B.x-A.x)*u, A.y+(B.y-A.y)*u }; };
        V2 q0 = L(p0,p1), q1 = L(p1,p2), q2 = L(p2,p3);
        V2 r0 = L(q0,q1), r1 = L(q1,q2);
        V2 s0 = L(r0,r1);
        P0 = p0; P1 = q0; P2 = r0; P3 = s0;
    }
    static void SplitRight(const WNode& a, const WNode& b, double u,
                           V2& P0, V2& P1, V2& P2, V2& P3) {
        V2 p0 = a.pos, p1 = a.hasOut ? a.hOut : a.pos;
        V2 p2 = b.hasIn ? b.hIn : b.pos, p3 = b.pos;
        auto Lr = [&](V2 A, V2 B){ return V2{ A.x+(B.x-A.x)*u, A.y+(B.y-A.y)*u }; };
        V2 q0 = Lr(p0,p1), q1 = Lr(p1,p2), q2 = Lr(p2,p3);
        V2 r0 = Lr(q0,q1), r1 = Lr(q1,q2);
        V2 s0 = Lr(r0,r1);
        P0 = s0; P1 = r1; P2 = q2; P3 = p3;
    }
};

// The exact Bézier nodes of `fc` between arc lengths [arcFrom, arcTo] (signed,
// possibly unwrapped; arcTo < arcFrom = a backward trace), De Casteljau-split at
// both ends. Travel is clamped to one full lap on a cyclic curve.
std::vector<WNode> FollowExtract(const FollowCurve& fc, double arcFrom, double arcTo) {
    std::vector<WNode> out;
    if (!fc.valid()) return out;
    const double total = fc.TotalArc();
    if (total < 1e-6) return out;
    const bool fwd = arcTo >= arcFrom;
    double a0 = std::min(arcFrom, arcTo), a1 = std::max(arcFrom, arcTo);
    double span = a1 - a0;
    if (!fc.closed) { a0 = std::clamp(a0, 0.0, total); a1 = std::clamp(a1, 0.0, total); }
    else            { span = std::min(span, total); a1 = a0 + span; }
    if (a1 - a0 < 1e-6) return out;

    auto pushCubic = [&](V2 P0, V2 P1, V2 P2, V2 P3) {
        if (out.empty()) {
            WNode s{}; s.pos = P0; s.hasOut = true; s.hOut = P1; s.hasIn = false;
            out.push_back(s);
        } else {
            out.back().hOut = P1; out.back().hasOut = true;
        }
        WNode e{}; e.pos = P3; e.hasIn = true; e.hIn = P2; e.hasOut = false;
        out.push_back(e);
    };
    const int sc = fc.segCount();
    auto wrapArc = [&](double a){ if (!fc.closed) return a;
        a = std::fmod(a, total); return a < 0 ? a + total : a; };
    double cur = a0;
    int guard = 0;
    while (cur < a1 - 1e-6 && guard++ < sc + 4) {
        int s; double u;
        fc.LocOfArc(wrapArc(cur), s, u);
        WNode A, B; fc.seg(s, A, B);
        double segLen = FollowCurve::SegLen(A, B, 1.0);
        if (segLen < 1e-6) { cur += 1e-5; continue; }
        double remain = a1 - cur;
        double segRemainArc = FollowCurve::SegLen(A, B, 1.0) - FollowCurve::SegLen(A, B, u);
        double hi;
        if (remain <= segRemainArc) {
            double targetArc = FollowCurve::SegLen(A, B, u) + remain;
            double lo2 = u, hi2 = 1.0;
            for (int it = 0; it < 18; ++it) { double mid = 0.5*(lo2+hi2);
                (FollowCurve::SegLen(A, B, mid) < targetArc) ? lo2 = mid : hi2 = mid; }
            hi = 0.5*(lo2+hi2);
        } else {
            hi = 1.0;
        }
        V2 P0, P1, P2, P3;
        if (u <= 1e-7 && hi >= 1.0 - 1e-7) {
            P0 = A.pos; P1 = A.hasOut ? A.hOut : A.pos;
            P2 = B.hasIn ? B.hIn : B.pos; P3 = B.pos;
        } else {
            V2 r0, r1, r2, r3; FollowCurve::SplitRight(A, B, u, r0, r1, r2, r3);
            WNode RA{ r0, {}, r1, false, true }, RB{ r3, r2, {}, true, false };
            double hi2 = (hi - u) / std::max(1e-6, 1.0 - u);
            FollowCurve::SplitLeft(RA, RB, std::clamp(hi2, 0.0, 1.0), P0, P1, P2, P3);
        }
        pushCubic(P0, P1, P2, P3);
        cur += (hi >= 1.0 - 1e-7) ? segRemainArc : remain;
    }
    if (!fwd) {
        std::reverse(out.begin(), out.end());
        for (WNode& w : out) { std::swap(w.hIn, w.hOut); std::swap(w.hasIn, w.hasOut); }
    }
    return out;
}

// Collect every visible path node (document order), like the mark tool.
void CollectPathNodes(const Ink::Document& doc, std::vector<Ink::NodeId>& out) {
    for (const Ink::Page& page : doc.Pages()) {
        std::vector<Ink::NodeId> stack(page.children.rbegin(), page.children.rend());
        while (!stack.empty()) {
            const Ink::NodeId id = stack.back();
            stack.pop_back();
            const Ink::Node* n = doc.Find(id);
            if (!n || !n->visible) continue;
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
                stack.push_back(*it);
            if (n->kind == Ink::NodeKind::Path && !n->path.Empty())
                out.push_back(id);
        }
    }
}

}  // namespace

// Append the traced world piece onto the pen node's open subpath. `piece` is in
// DOCUMENT space (== the pen node's local space, identity transform). The pen's
// last anchor adopts the piece's first OUT handle so the join leaves along the
// curve; every new anchor copies the target geometry exactly. Returns false if
// the pen has no usable path.
static bool AppendFollowPiece(Ink::Document& doc, Ink::NodeId penNode,
                              const std::vector<WNode>& piece) {
    if (piece.size() < 2 || penNode == Ink::kNullNode) return false;
    const Ink::Node* n = doc.Find(penNode);
    if (!n || n->path.subpaths.empty()) return false;
    Ink::PathData p = n->path;
    Ink::Subpath& sp = p.subpaths.front();
    auto toAnchor = [](const WNode& w) {
        Ink::Anchor a; a.pos = w.pos;
        a.hasIn = w.hasIn; a.hasOut = w.hasOut;
        a.in  = w.hasIn  ? Ink::DVec2{ w.hIn.x  - w.pos.x, w.hIn.y  - w.pos.y }  : Ink::DVec2{0,0};
        a.out = w.hasOut ? Ink::DVec2{ w.hOut.x - w.pos.x, w.hOut.y - w.pos.y } : Ink::DVec2{0,0};
        a.kind = (a.hasIn || a.hasOut) ? Ink::AnchorKind::Smooth : Ink::AnchorKind::Corner;
        return a;
    };
    if (sp.anchors.empty()) sp.anchors.push_back(toAnchor(piece.front()));
    else {
        // The existing last anchor leaves along the curve (adopt piece[0].out).
        Ink::Anchor& last = sp.anchors.back();
        last.out = piece.front().hasOut
            ? Ink::DVec2{ piece.front().hOut.x - last.pos.x,
                          piece.front().hOut.y - last.pos.y }
            : Ink::DVec2{0,0};
        last.hasOut = piece.front().hasOut;
        if (last.hasIn || last.hasOut) last.kind = Ink::AnchorKind::Smooth;
    }
    for (std::size_t i = 1; i < piece.size(); ++i)
        sp.anchors.push_back(toAnchor(piece[i]));
    doc.SetPath(penNode, p);
    return true;
}

bool Application::UpdatePenFollowCurve(const ViewCam& cam, bool hovered,
                                       Ink::OverlayList& ov, bool& committed) {
    committed = false;
    if (!project_.document) { followLocked_ = false; return false; }
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyShift) { followLocked_ = false; return false; }
    Ink::Document& doc = *project_.document;
    const double zoom = std::max(1e-4, cam.zoom);
    const bool lpressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const Ink::DVec2 mDoc = cam.ScreenToDoc(io.MousePos.x, io.MousePos.y);

    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    Ink::Color blue;
    try {
        const ImVec4 c = ds.GetColor(Tok::S_Color_Accent_Default);
        blue = Ink::SrgbToLinearPremultiplied(c.x, c.y, c.z, 1.0f);
    } catch (...) { blue = Ink::SrgbToLinearPremultiplied(0.25f, 0.5f, 0.95f, 1.0f); }
    auto d2v = [&](Ink::DVec2 p) { return cam.DocToView(p.x, p.y); };
    auto blueDiamond = [&](Ink::DVec2 wp) {
        const Ink::Vec2 c = d2v(wp);
        const float r = 8.0f * gs, th = 2.0f * gs;
        ov.AddLine({ c.x, c.y - r }, { c.x + r, c.y }, blue, th);
        ov.AddLine({ c.x + r, c.y }, { c.x, c.y + r }, blue, th);
        ov.AddLine({ c.x, c.y + r }, { c.x - r, c.y }, blue, th);
        ov.AddLine({ c.x - r, c.y }, { c.x, c.y - r }, blue, th);
    };

    // The pen's last placed anchor (document space) — the trace anchor / link.
    Ink::DVec2 lastPt{ 0, 0 }; bool haveLast = false;
    if (penNode_ != Ink::kNullNode) {
        const Ink::Node* pn = doc.Find(penNode_);
        if (pn && !pn->path.subpaths.empty() &&
            !pn->path.subpaths.front().anchors.empty()) {
            lastPt = pn->path.subpaths.front().anchors.back().pos;
            haveLast = true;
        }
    }

    const double kPickPx = 24.0, kOnCurvePx = 2.0;

    // ── NOT LOCKED: find an entry point (blue diamond); a click locks on ────────
    if (!followLocked_) {
        std::vector<Ink::NodeId> nodes;
        CollectPathNodes(doc, nodes);
        const double kPickDoc2 = (kPickPx / zoom) * (kPickPx / zoom);
        double bestD2 = kPickDoc2; bool found = false;
        Ink::NodeId bestNode = Ink::kNullNode; int bestSub = -1;
        int bestSeg = 0; double bestU = 0.0; Ink::DVec2 bestPt{};
        // Also test the last placed anchor: if it already sits ON a curve, lock
        // onto it immediately (Shift starts following straight away, no link).
        double lastBestD2 = (kOnCurvePx / zoom) * (kOnCurvePx / zoom);
        bool lastOn = false; Ink::NodeId lastNode = Ink::kNullNode; int lastSub = -1;
        double lastArc = 0.0;

        for (Ink::NodeId id : nodes) {
            if (id == penNode_) continue;   // never follow the path being drawn
            const Ink::Node* nn = doc.Find(id);
            if (!nn) continue;
            const Ink::DMat23 w = doc.WorldTransform(id);
            for (int sub = 0; sub < (int)nn->path.subpaths.size(); ++sub) {
                FollowCurve fc = FollowCurve::Build(*nn, sub, w);
                if (!fc.valid()) continue;
                int seg; double u; Ink::DVec2 c;
                double d2 = fc.Project(mDoc, seg, u, c);
                if (d2 < bestD2) {
                    bestD2 = d2; found = true; bestNode = id; bestSub = sub;
                    bestSeg = seg; bestU = u; bestPt = c;
                }
                if (haveLast) {
                    Ink::DVec2 lc; double la = fc.ProjectArc(lastPt, lc);
                    double ld2 = (lc.x-lastPt.x)*(lc.x-lastPt.x) +
                                 (lc.y-lastPt.y)*(lc.y-lastPt.y);
                    if (ld2 < lastBestD2) {
                        lastBestD2 = ld2; lastOn = true; lastNode = id;
                        lastSub = sub; lastArc = la;
                    }
                }
            }
        }

        if (lastOn) {
            followLocked_ = true; followNode_ = lastNode; followSub_ = lastSub;
            followAnchorArc_ = lastArc; followCursorArc_ = lastArc;
            // fall through to the LOCKED branch below (this same frame)
        } else if (found) {
            blueDiamond(bestPt);
            if (lpressed) {
                // Place the entry point ON the curve and lock.
                const Ink::Node* pn = doc.Find(penNode_);
                if (penNode_ == Ink::kNullNode || !pn) {
                    // Start a fresh pen path at the entry point.
                    if (doc.Pages().empty()) return true;
                    Ink::PathData p; Ink::Subpath sp; sp.spline = penSpline_;
                    Ink::Anchor a; a.pos = bestPt; sp.anchors.push_back(a);
                    p.subpaths.push_back(std::move(sp));
                    Ink::Style ps = DefaultStyle();
                    if (!penIsArea_) ps.fills.clear();   // a curve has no fill
                    penNode_ = doc.AddPath(doc.Pages().front().id, std::move(p),
                                           std::move(ps), penIsArea_ ? "Free" : "Bézier");
                } else {
                    Ink::PathData p = pn->path;
                    if (p.subpaths.empty()) { Ink::Subpath sp; sp.spline = penSpline_;
                                              p.subpaths.push_back(sp); }
                    Ink::Anchor a; a.pos = bestPt;
                    p.subpaths.front().anchors.push_back(a);
                    doc.SetPath(penNode_, p);
                }
                const FollowCurve fc =
                    FollowCurve::Build(*doc.Find(bestNode), bestSub,
                                       doc.WorldTransform(bestNode));
                followLocked_ = true; followNode_ = bestNode; followSub_ = bestSub;
                followAnchorArc_ = fc.ArcOf(bestSeg, bestU);
                followCursorArc_ = followAnchorArc_;
                committed = true;
            }
            return true;
        } else {
            return false;   // nothing to follow → normal pen drawing
        }
    }

    // ── LOCKED: trace the locked curve, no distance limit ──────────────────────
    const Ink::Node* tn = doc.Find(followNode_);
    if (!tn || followSub_ < 0 || followSub_ >= (int)tn->path.subpaths.size()) {
        followLocked_ = false; return false;
    }
    FollowCurve fc = FollowCurve::Build(*tn, followSub_, doc.WorldTransform(followNode_));
    if (!fc.valid()) { followLocked_ = false; return false; }
    const double total = fc.TotalArc();
    Ink::DVec2 c; double rawArc = fc.ProjectArc(mDoc, c);
    // Unwrap against the previous arc so a continuous drag tracks across a cyclic
    // seam (open curves have no seam → no-op).
    double cursorArc = rawArc;
    if (fc.closed && total > 1e-4) {
        double k = std::round((followCursorArc_ - rawArc) / total);
        cursorArc = rawArc + k * total;
    }
    followCursorArc_ = cursorArc;

    std::vector<WNode> piece = FollowExtract(fc, followAnchorArc_, cursorArc);
    blueDiamond(c);
    // Preview the traced piece as a translucent polyline (flatten the cubics).
    if (piece.size() >= 2) {
        // Premultiplied → scale ALL channels for a translucent preview.
        Ink::Color prev{ blue.r * 0.7f, blue.g * 0.7f, blue.b * 0.7f, blue.a * 0.7f };
        for (std::size_t i = 0; i + 1 < piece.size(); ++i) {
            const WNode& A = piece[i]; const WNode& B = piece[i + 1];
            Ink::Vec2 p0 = d2v(A.pos);
            const int FL = 18;
            for (int s = 1; s <= FL; ++s) {
                const Ink::Vec2 p1 = d2v(CubicAt(A, B, (double)s / FL));
                ov.AddLine(p0, p1, prev, 1.5f * gs);
                p0 = p1;
            }
        }
    }
    // Click: freeze the traced piece onto the pen and re-anchor here.
    if (lpressed && piece.size() >= 2) {
        AppendFollowPiece(doc, penNode_, piece);
        followAnchorArc_ = cursorArc;
        committed = true;
    }
    return true;
}

// ── Draw-on-Create cursor + shape preview glyph ──────────────────────────────
// The kind currently armed for a create tool (empty = none). The pen's spline
// while it is active, else the shape/curve drag-box armed by the Add menu.
std::string Application::CreateCursorKind() const {
    if (penActive_) {
        return penSpline_ == Ink::SplineType::Nurbs ? "nurbs"
             : penSpline_ == Ink::SplineType::Poly  ? "poly" : "curve";
    }
    if (!pendingDrawKind_.empty()) return pendingDrawKind_;
    if (canvasDrag_.kind == CanvasDrag::Kind::DrawShape)
        return canvasDrag_.shapeKind;
    return {};
}

// Hide the OS cursor and draw a crosshair (foreground list) with a small preview
// GLYPH of the shape being created at the bottom-right of the cursor. Faithful to
// the legacy ShowCrosshairCursor + the placement ghost, adapted to Ink.
void Application::DrawCreateCursor(const char* kind) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    auto colOf = [&](Tok t, float a) {
        try { ImVec4 c = ds.GetColor(t); c.w = a;
              return ImGui::ColorConvertFloat4ToU32(c); }
        catch (...) { return ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f,0.9f,0.9f,a)); }
    };
    const ImU32 core = colOf(Tok::C_Viewport_Crosshair, 1.0f);
    const ImU32 halo = colOf(Tok::C_Viewport_CursorTick, 1.0f);

    // The crosshair "+" (dark halo under a bright core, 1px hole at the drop point).
    const float arm = 12.0f * gs, hole = 1.5f * gs;
    auto cross = [&](ImU32 c, float t) {
        fg->AddLine(ImVec2(mp.x - arm, mp.y), ImVec2(mp.x - hole, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x + hole, mp.y), ImVec2(mp.x + arm, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y - arm), ImVec2(mp.x, mp.y - hole), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y + hole), ImVec2(mp.x, mp.y + arm), c, t);
    };
    cross(halo, 3.0f * gs);
    cross(core, 1.0f * gs);

    // Preview glyph of the shape to create, at the BOTTOM-RIGHT of the cursor.
    if (!kind || !*kind) return;
    const float sz = 14.0f * gs;                       // glyph box side
    const float ox = mp.x + arm + 4.0f * gs;           // offset right of the cross
    const float oy = mp.y + arm + 4.0f * gs;           // and below it
    const ImVec2 gmn(ox, oy), gmx(ox + sz, oy + sz);
    const ImVec2 gc((gmn.x + gmx.x) * 0.5f, (gmn.y + gmx.y) * 0.5f);
    const float hs = sz * 0.5f;
    const ImU32 gl = colOf(Tok::C_Viewport_Crosshair, 1.0f);
    const float th = 1.6f * gs;
    // A faint plate behind the glyph for contrast on any background.
    fg->AddRectFilled(ImVec2(gmn.x - 2*gs, gmn.y - 2*gs),
                      ImVec2(gmx.x + 2*gs, gmx.y + 2*gs),
                      colOf(Tok::C_Viewport_CursorTick, 0.5f), 2.0f * gs);
    auto is = [&](const char* k){ return std::strcmp(kind, k) == 0; };
    if (is("rect")) {
        fg->AddRect(gmn, gmx, gl, 0.0f, 0, th);
    } else if (is("ellipse") || is("beziercircle") || is("nurbscircle")) {
        fg->AddCircle(gc, hs, gl, 20, th);
    } else if (is("triangle")) {
        fg->AddTriangle(ImVec2(gc.x, gmn.y), gmx, ImVec2(gmn.x, gmx.y), gl, th);
    } else {
        // curve / nurbs / poly / free → a small S-curve glyph.
        const int N = 16;
        ImVec2 prev(gmn.x, gmx.y);
        for (int i = 1; i <= N; ++i) {
            const float t = (float)i / N;
            const float x = gmn.x + sz * t;
            const float y = gc.y - std::sin(t * 6.2831853f) * hs * 0.7f;
            const ImVec2 cur(x, y);
            fg->AddLine(prev, cur, gl, th);
            prev = cur;
        }
    }
}

}  // namespace App

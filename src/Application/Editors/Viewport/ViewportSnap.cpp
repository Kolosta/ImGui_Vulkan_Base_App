#include "Application.h"

#include "ViewportMath.h"
#include <Ink/Geometry/Geometry.h>
#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Interactive snapping during a modal G/R/S — the legacy "Snap To" system,
//  reimplemented on the Ink model (docs/Ink/ROADMAP.md Lot 8).
//
//  Modes:
//    • Increment — round the DISPLACEMENT to the move increment (no absolute
//      grid; the transform code applies it directly, no indicator).
//    • Grid      — a moving SOURCE lands on the nearest ABSOLUTE grid crossing
//      (spacing = SnapGridStep()); dots preview the crossings.
//    • Vertex / Edge / EdgeCenter / Face — snap onto document geometry within a
//      screen-pixel radius of the cursor, never onto the moving selection.
//
//  Snap Base picks WHICH point of the moving selection lands on the target:
//  Closest = the moving control point nearest the target; Pivot/Median = the
//  op pivot; Active = the active object's origin / active vertex. The pieces
//  here are the geometry query (ComputeSnap / CollectSnapPoints), the source
//  set (SnapBaseSources) and the self-snap rejection (MovingSelection*). The
//  transform integration lives in ViewportModal.cpp; the overlays (violet
//  candidates + the accent indicator glyph) in ViewportOverlays.cpp.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {

// Every visible path node (document order), like the mark / follow tools.
void CollectVisiblePathNodes(const Ink::Document& doc,
                             std::vector<Ink::NodeId>& out) {
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
            if (n->kind == Ink::NodeKind::Path && !n->path.Empty())
                out.push_back(id);
        }
    }
}

// Cubic Bézier point at parameter t.
inline Ink::DVec2 CubicAt(Ink::DVec2 p0, Ink::DVec2 p1, Ink::DVec2 p2,
                          Ink::DVec2 p3, double t) {
    const double u = 1.0 - t;
    const double b0 = u * u * u, b1 = 3 * u * u * t,
                 b2 = 3 * u * t * t, b3 = t * t * t;
    return { b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
             b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y };
}

// The 50%-arc-length point of the node-segment a→b (its control handles), in
// the path's LOCAL space — the "edge centre" of one control-node span (not of
// every flattened sub-segment, which would scatter candidates along a curve).
Ink::DVec2 SegmentMidpoint(const Ink::Anchor& a, const Ink::Anchor& b) {
    const Ink::DVec2 P0 = a.pos;
    const Ink::DVec2 P1 = a.hasOut ? Ink::DVec2{ a.pos.x + a.out.x,
                                                 a.pos.y + a.out.y } : a.pos;
    const Ink::DVec2 P2 = b.hasIn ? Ink::DVec2{ b.pos.x + b.in.x,
                                                b.pos.y + b.in.y } : b.pos;
    const Ink::DVec2 P3 = b.pos;
    constexpr int N = 24;
    std::vector<Ink::DVec2> pts;
    pts.reserve(N + 1);
    pts.push_back(P0);
    double total = 0.0;
    for (int i = 1; i <= N; ++i) {
        Ink::DVec2 p = CubicAt(P0, P1, P2, P3, (double)i / N);
        total += std::hypot(p.x - pts.back().x, p.y - pts.back().y);
        pts.push_back(p);
    }
    const double half = total * 0.5;
    double acc = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const double l = std::hypot(pts[i].x - pts[i - 1].x,
                                    pts[i].y - pts[i - 1].y);
        if (acc + l >= half) {
            const double u = l > 1e-9 ? (half - acc) / l : 0.0;
            return { pts[i - 1].x + (pts[i].x - pts[i - 1].x) * u,
                     pts[i - 1].y + (pts[i].y - pts[i - 1].y) * u };
        }
        acc += l;
    }
    return P3;
}

} // namespace

// Snapping engages when the magnet is on OR Ctrl is held, gated per transform
// by the Affect toggle (Ctrl is Blender's transient "snap just this drag").
bool Application::SnapActiveFor(TransformOp::Kind kind) const {
    // Ctrl INVERTS the magnet (Blender's transient toggle) — matches the modal
    // transform's own predicate so the overlay and the maths always agree.
    const bool on = edit_.snap.enabled != ImGui::GetIO().KeyCtrl;
    if (!on) return false;
    switch (kind) {
        case TransformOp::Kind::Move:   return edit_.snap.affectMove;
        case TransformOp::Kind::Rotate: return edit_.snap.affectRotate;
        case TransformOp::Kind::Scale:  return edit_.snap.affectScale;
        default:                        return false;
    }
}

// The Grid step at a given zoom: the raw move increment, coarsened by a nice
// factor so on-screen crossings stay ≥ a minimum spacing — bounding the count
// (a fixed doc-space step explodes when dezoomed). Both the dot display and the
// snap use this, so the dots always mark the crossings you snap to.
double Application::GridSnapStep(double zoom) const {
    const double base = SnapGridStep();            // the move increment (fixed)
    if (base < 1e-9 || zoom < 1e-12) return base;
    const double kMinPx = 24.0;                    // min on-screen crossing spacing
    if (base * zoom >= kMinPx) return base;        // already legible → raw increment
    const double need = kMinPx / zoom;             // required doc spacing
    const double m = need / base;                  // multiplier > 1 needed
    const double e = std::floor(std::log10(m));
    const double p = std::pow(10.0, e);
    const double f = m / p;                         // 1 .. 10
    const double nice = f <= 1.0 ? 1.0 : f <= 2.0 ? 2.0 : f <= 5.0 ? 5.0 : 10.0;
    return base * nice * p;                          // base × {1,2,5}·10ⁿ ≥ need
}

// Discrete candidates for the DISCRETE modes (Vertex anchors / EdgeCenter
// node-segment midpoints / Face centroids), in world space, excluding the
// moving selection (`exclude` ids + `rejectPts` current positions).
std::vector<Ink::DVec2> Application::CollectSnapPoints(
        const std::vector<Ink::NodeId>& exclude,
        const std::vector<Ink::DVec2>& rejectPts) const {
    std::vector<Ink::DVec2> out;
    if (!project_.document) return out;
    const Ink::Document& doc = *project_.document;
    const SnapSettings::Mode mode = edit_.snap.mode;
    const double zoom = std::max(1e-4, hoveredCam_.zoom);
    const double kRejDoc = 1.0 / zoom;              // ~1px coincidence tolerance
    auto rejected = [&](Ink::DVec2 p) {
        for (const Ink::DVec2& r : rejectPts)
            if (std::hypot(p.x - r.x, p.y - r.y) < kRejDoc) return true;
        return false;
    };
    auto excluded = [&](Ink::NodeId id) {
        return std::find(exclude.begin(), exclude.end(), id) != exclude.end();
    };

    std::vector<Ink::NodeId> nodes;
    CollectVisiblePathNodes(doc, nodes);
    for (Ink::NodeId id : nodes) {
        if (excluded(id)) continue;
        const Ink::Node* n = doc.Find(id);
        if (!n) continue;
        const Ink::DMat23 W = doc.WorldTransform(id);
        for (const Ink::Subpath& sp : n->path.subpaths) {
            const std::vector<Ink::Anchor>& A = sp.anchors;
            const int cnt = (int)A.size();
            if (cnt == 0) continue;

            if (mode == SnapSettings::Mode::Vertex) {
                for (const Ink::Anchor& a : A) {
                    const Ink::DVec2 w = W.Apply(a.pos);
                    if (!rejected(w)) out.push_back(w);
                }
            } else if (mode == SnapSettings::Mode::EdgeCenter) {
                const int segs = sp.closed ? cnt : cnt - 1;
                for (int k = 0; k < segs; ++k) {
                    const Ink::Anchor& a = A[(std::size_t)k];
                    const Ink::Anchor& b = A[(std::size_t)((k + 1) % cnt)];
                    // Skip a moving edge (both control endpoints rejected).
                    if (rejected(W.Apply(a.pos)) && rejected(W.Apply(b.pos)))
                        continue;
                    const Ink::DVec2 w = W.Apply(SegmentMidpoint(a, b));
                    if (!rejected(w)) out.push_back(w);
                }
            } else if (mode == SnapSettings::Mode::Face) {
                if (!sp.closed || cnt < 3) continue;
                Ink::PathData one;
                one.subpaths.push_back(sp);
                const double scl = std::hypot(W.m[0], W.m[3]);
                const double tol =
                    std::max(1e-4, (0.5 / zoom) / std::max(1e-6, scl));
                Ink::DVec2 c{ 0, 0 };
                std::size_t np = 0;
                bool allRej = true;
                for (const auto& pl : Ink::geom::Flatten(one, tol))
                    for (const Ink::DVec2& p : pl.points) {
                        const Ink::DVec2 w = W.Apply(p);
                        c.x += w.x; c.y += w.y; ++np;
                        if (!rejected(w)) allRej = false;
                    }
                if (np && !allRej)
                    out.push_back({ c.x / (double)np, c.y / (double)np });
            }
        }
    }
    return out;
}

// Find the geometry snap target nearest the cursor within a screen-pixel radius,
// skipping any candidate on the moving selection.
Application::SnapResult Application::ComputeSnap(
        Ink::DVec2 cursor, double zoom,
        const std::vector<Ink::NodeId>& exclude,
        const std::vector<Ink::DVec2>& rejectPts,
        const std::vector<Ink::DVec2>& rejectSegs) const {
    SnapResult out;
    out.pos = cursor;
    if (!project_.document) return out;
    const Ink::Document& doc = *project_.document;
    const double z = std::max(1e-4, zoom);
    const double kRadiusDoc = 16.0 / z;             // pickup radius (screen px)
    const double kRejDoc = 1.0 / z;
    const SnapSettings::Mode mode = edit_.snap.mode;

    auto isRejected = [&](Ink::DVec2 p) {
        for (const Ink::DVec2& r : rejectPts)
            if (std::hypot(p.x - r.x, p.y - r.y) < kRejDoc) return true;
        return false;
    };
    auto distToSeg = [](Ink::DVec2 p, Ink::DVec2 a, Ink::DVec2 b) {
        const Ink::DVec2 ab{ b.x - a.x, b.y - a.y };
        const double L2 = ab.x * ab.x + ab.y * ab.y;
        const double t = L2 > 1e-9
            ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / L2, 0.0, 1.0)
            : 0.0;
        return std::hypot(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t));
    };
    // On the moving selection = a rejected point, or on a moving edge span.
    auto onSelection = [&](Ink::DVec2 p) {
        if (isRejected(p)) return true;
        for (std::size_t i = 0; i + 1 < rejectSegs.size(); i += 2)
            if (distToSeg(p, rejectSegs[i], rejectSegs[i + 1]) < kRejDoc)
                return true;
        return false;
    };
    double bestD = kRadiusDoc;
    bool found = false;
    Ink::DVec2 best{};
    auto consider = [&](Ink::DVec2 p) {
        if (onSelection(p)) return;
        const double d = std::hypot(p.x - cursor.x, p.y - cursor.y);
        if (d < bestD) { bestD = d; best = p; found = true; }
    };

    if (mode == SnapSettings::Mode::Vertex ||
        mode == SnapSettings::Mode::EdgeCenter ||
        mode == SnapSettings::Mode::Face) {
        for (const Ink::DVec2& p : CollectSnapPoints(exclude, rejectPts))
            consider(p);
    } else if (mode == SnapSettings::Mode::Edge) {
        // Project the cursor onto every flattened outline segment (any point on
        // a line is a valid target), skipping segments on a moving edge.
        auto excluded = [&](Ink::NodeId id) {
            return std::find(exclude.begin(), exclude.end(), id) != exclude.end();
        };
        std::vector<Ink::NodeId> nodes;
        CollectVisiblePathNodes(doc, nodes);
        for (Ink::NodeId id : nodes) {
            if (excluded(id)) continue;
            const Ink::Node* n = doc.Find(id);
            if (!n) continue;
            const Ink::DMat23 W = doc.WorldTransform(id);
            const double scl = std::hypot(W.m[0], W.m[3]);
            const double tol = std::max(1e-4, (0.5 / z) / std::max(1e-6, scl));
            for (const Ink::Subpath& sp : n->path.subpaths) {
                if (sp.anchors.size() < 2) continue;
                Ink::PathData one;
                one.subpaths.push_back(sp);
                for (const auto& pl : Ink::geom::Flatten(one, tol)) {
                    const std::size_t np = pl.points.size();
                    if (np < 2) continue;
                    const std::size_t segc = pl.closed ? np : np - 1;
                    for (std::size_t i = 0; i < segc; ++i) {
                        const Ink::DVec2 a = W.Apply(pl.points[i]);
                        const Ink::DVec2 b = W.Apply(pl.points[(i + 1) % np]);
                        if (onSelection(a) && onSelection(b)) continue;
                        const Ink::DVec2 ab{ b.x - a.x, b.y - a.y };
                        const double L2 = ab.x * ab.x + ab.y * ab.y;
                        if (L2 < 1e-9) continue;
                        const double t = std::clamp(
                            ((cursor.x - a.x) * ab.x + (cursor.y - a.y) * ab.y)
                                / L2, 0.0, 1.0);
                        consider({ a.x + ab.x * t, a.y + ab.y * t });
                    }
                }
            }
        }
    }
    if (found) { out.pos = best; out.snapped = true; out.showMark = true; }
    return out;
}

// PRE-MOVE world snap-source point(s) of the moving selection under the current
// Snap Base. Object sources are re-derived through the (unchanged) parent chain
// from the op SNAPSHOT transform, so toggling snap ON mid-drag still reads the
// original positions.
std::vector<Ink::DVec2> Application::SnapBaseSources() const {
    std::vector<Ink::DVec2> out;
    if (!project_.document) return out;
    const Ink::Document& doc = *project_.document;
    const SnapSettings::Base base = edit_.snap.base;

    if (transformOp_.editVerts) {
        const Ink::Node* n = doc.Find(transformOp_.editNode);
        if (!n) { out.push_back(transformOp_.pivot); return out; }
        const Ink::DMat23 W = doc.WorldTransform(transformOp_.editNode);
        const Ink::PathData& op = transformOp_.origPath;
        auto anchorW = [&](int sp, int a) -> Ink::DVec2 {
            if (sp >= 0 && sp < (int)op.subpaths.size() &&
                a >= 0 && a < (int)op.subpaths[(std::size_t)sp].anchors.size())
                return W.Apply(op.subpaths[(std::size_t)sp].anchors[(std::size_t)a].pos);
            return transformOp_.pivot;
        };
        if (base == SnapSettings::Base::Active && !edit_.elemSel.empty()) {
            const EditContext::ElemRef& e = edit_.elemSel.back();
            out.push_back(anchorW(e.sp, e.a));
            return out;
        }
        if (base == SnapSettings::Base::Pivot ||
            base == SnapSettings::Base::Median) {
            out.push_back(transformOp_.pivot);
            return out;
        }
        // Closest: every selected POINT element (pre-move world).
        for (const EditContext::ElemRef& e : edit_.elemSel)
            if (e.part == EditContext::ElemPart::Point)
                out.push_back(anchorW(e.sp, e.a));
        if (out.empty()) out.push_back(transformOp_.pivot);
        return out;
    }

    // Object mode. Pre-move world of a node-local point through the parent chain:
    //   W_pre = (W_live · L_live⁻¹) · L_snapshot.
    auto preWorld = [&](const TransformOp::NodeOrig& o,
                        Ink::DVec2 localPt) -> Ink::DVec2 {
        const Ink::Node* n = doc.Find(o.id);
        if (!n) return transformOp_.pivot;
        const Ink::DMat23 parent =
            doc.WorldTransform(o.id).Compose(vpm::InvertAffine(n->transform.Matrix()));
        return parent.Compose(o.t.Matrix()).Apply(localPt);
    };
    if (base == SnapSettings::Base::Active) {
        for (const TransformOp::NodeOrig& o : transformOp_.nodes)
            if (o.id == edit_.active) { out.push_back(preWorld(o, { 0, 0 })); break; }
        if (!out.empty()) return out;
    }
    if (base == SnapSettings::Base::Pivot ||
        base == SnapSettings::Base::Median) {
        out.push_back(transformOp_.pivot);
        return out;
    }
    // Closest: every moving node's control points (pre-move world).
    for (const TransformOp::NodeOrig& o : transformOp_.nodes) {
        const Ink::Node* n = doc.Find(o.id);
        if (!n) continue;
        for (const Ink::Subpath& sp : n->path.subpaths)
            for (const Ink::Anchor& a : sp.anchors)
                out.push_back(preWorld(o, a.pos));
    }
    if (out.empty()) out.push_back(transformOp_.pivot);
    return out;
}

// Every moving vertex (edit mode) at its PRE-MOVE world position — rejected as a
// snap target so the selection never snaps onto itself. Empty in object mode.
std::vector<Ink::DVec2> Application::MovingSelectionPoints() const {
    std::vector<Ink::DVec2> out;
    if (!transformOp_.editVerts || !project_.document) return out;
    const Ink::Node* n = project_.document->Find(transformOp_.editNode);
    if (!n) return out;
    const Ink::DMat23 W = project_.document->WorldTransform(transformOp_.editNode);
    const Ink::PathData& op = transformOp_.origPath;
    for (const EditContext::ElemRef& e : edit_.elemSel) {
        if (e.part != EditContext::ElemPart::Point) continue;
        if (e.sp >= 0 && e.sp < (int)op.subpaths.size() &&
            e.a >= 0 && e.a < (int)op.subpaths[(std::size_t)e.sp].anchors.size())
            out.push_back(
                W.Apply(op.subpaths[(std::size_t)e.sp].anchors[(std::size_t)e.a].pos));
    }
    return out;
}

// The moving selection's EDGES as PRE-MOVE world segment pairs (edit mode): an
// edge is moving when BOTH its endpoint anchors are selected points.
std::vector<Ink::DVec2> Application::MovingSelectionEdges() const {
    std::vector<Ink::DVec2> segs;
    if (!transformOp_.editVerts || !project_.document) return segs;
    const Ink::Node* n = project_.document->Find(transformOp_.editNode);
    if (!n) return segs;
    const Ink::DMat23 W = project_.document->WorldTransform(transformOp_.editNode);
    const Ink::PathData& op = transformOp_.origPath;
    auto selPoint = [&](int sp, int a) {
        return edit_.ElemSelected(sp, a, EditContext::ElemPart::Point);
    };
    for (int spi = 0; spi < (int)op.subpaths.size(); ++spi) {
        const std::vector<Ink::Anchor>& A = op.subpaths[(std::size_t)spi].anchors;
        const int cnt = (int)A.size();
        const int segc = op.subpaths[(std::size_t)spi].closed ? cnt : cnt - 1;
        for (int k = 0; k < segc; ++k) {
            const int ia = k, ib = (k + 1) % cnt;
            if (!selPoint(spi, ia) || !selPoint(spi, ib)) continue;
            segs.push_back(W.Apply(A[(std::size_t)ia].pos));
            segs.push_back(W.Apply(A[(std::size_t)ib].pos));
        }
    }
    return segs;
}

// Apply the active Snap To mode to a Move op's displacement `moveD` (in place),
// publishing snapIndicator_ for Grid / geometry snaps. `cursorDoc` is the
// effective (drift-free) cursor in doc space. Increment rounds the displacement;
// Grid and the geometry modes OVERRIDE moveD so a chosen SOURCE lands on the
// target — an active geometry/grid snap wins over an axis constraint, as before.
void Application::ApplyMoveSnap(const ViewCam& cam, Ink::DVec2 cursorDoc,
                               Ink::DVec2& moveD, bool precise) {
    // Capture the snap SOURCES once, from PRE-MOVE geometry (Closest = every
    // moving control point; else the single base point). Lazy so toggling snap
    // ON mid-drag still reads pre-move positions.
    if (!transformOp_.snapSourceInit) {
        if (edit_.snap.base == SnapSettings::Base::Closest) {
            transformOp_.snapSources = SnapBaseSources();
        } else {
            std::vector<Ink::DVec2> s = SnapBaseSources();
            transformOp_.snapSources.assign(
                1, s.empty() ? transformOp_.pivot : s.front());
        }
        if (transformOp_.snapSources.empty())
            transformOp_.snapSources.push_back(transformOp_.pivot);
        transformOp_.snapSourceInit = true;
    }
    const SnapSettings::Mode mode = edit_.snap.mode;
    // The cursor travel so far (drift-free — driven by the cursor, NOT the
    // snapped result), used to bring pre-move reject points to their live spot.
    const Ink::DVec2 mv{ cursorDoc.x - transformOp_.startDoc.x,
                         cursorDoc.y - transformOp_.startDoc.y };

    if (mode == SnapSettings::Mode::Increment) {
        const double inc = precise ? edit_.snap.movePrecision
                                   : edit_.snap.moveIncrement;
        if (inc > 1e-9) {
            moveD.x = vpm::SnapTo(moveD.x, inc);
            moveD.y = vpm::SnapTo(moveD.y, inc);
        }
        return;
    }
    if (mode == SnapSettings::Mode::Grid) {
        const double g = GridSnapStep(cam.zoom);   // same step the dots show
        if (g <= 1e-9) return;
        // Closest: the moving source currently nearest the cursor lands on the
        // grid crossing nearest the cursor; other bases use the single source.
        Ink::DVec2 src0 = transformOp_.snapSources.front();
        if (edit_.snap.base == SnapSettings::Base::Closest) {
            double best = 1e300;
            for (const Ink::DVec2& s0 : transformOp_.snapSources) {
                const double d = std::hypot(s0.x + mv.x - cursorDoc.x,
                                            s0.y + mv.y - cursorDoc.y);
                if (d < best) { best = d; src0 = s0; }
            }
        }
        const Ink::DVec2 gp{ std::round(cursorDoc.x / g) * g,
                             std::round(cursorDoc.y / g) * g };
        moveD = { gp.x - src0.x, gp.y - src0.y };
        snapIndicator_.snapped = true;
        snapIndicator_.showMark = true;
        snapIndicator_.pos = gp;
        return;
    }

    // Geometry modes (Vertex / Edge / EdgeCenter / Face): the TARGET is the
    // geometry nearest the cursor within the radius (same for every base); the
    // base only picks the SOURCE that lands on it.
    std::vector<Ink::NodeId> exclude;
    if (!transformOp_.editVerts)
        for (const TransformOp::NodeOrig& o : transformOp_.nodes)
            exclude.push_back(o.id);
    // Reject the moving selection as a target. Edit mode: ALL its vertices (not
    // just the source — else a non-source moving vertex feeds back and flickers)
    // and its real edges. Object mode: the sources (whole shapes are excluded).
    std::vector<Ink::DVec2> reject = MovingSelectionPoints();
    if (reject.empty()) reject = transformOp_.snapSources;
    std::vector<Ink::DVec2> rejectSegs = MovingSelectionEdges();
    for (Ink::DVec2& p : reject)     { p.x += mv.x; p.y += mv.y; }
    for (Ink::DVec2& p : rejectSegs) { p.x += mv.x; p.y += mv.y; }

    const SnapResult sr =
        ComputeSnap(cursorDoc, cam.zoom, exclude, reject, rejectSegs);
    if (!sr.snapped) return;
    Ink::DVec2 src0 = transformOp_.snapSources.front();
    if (edit_.snap.base == SnapSettings::Base::Closest) {
        // The PRE-MOVE source nearest the (cursor-found) target lands on it, so
        // dragging the target over a different original vertex picks THAT one.
        double best = 1e300;
        for (const Ink::DVec2& s0 : transformOp_.snapSources) {
            const double d = std::hypot(s0.x - sr.pos.x, s0.y - sr.pos.y);
            if (d < best) { best = d; src0 = s0; }
        }
    }
    moveD = { sr.pos.x - src0.x, sr.pos.y - src0.y };
    if (sr.showMark) {
        snapIndicator_.snapped = true;
        snapIndicator_.showMark = true;
        snapIndicator_.pos = sr.pos;
    }
}

} // namespace App

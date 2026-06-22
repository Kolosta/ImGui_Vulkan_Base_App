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
        // (e.g. ISOM) or a per-axis padlock ignores the matching op (a mixed
        // selection still moves the unlocked ones). Rotation locks wholesale;
        // move/scale lock per-axis (restored from the snapshot below), so only the
        // fully-locked scale can early-out here.
        if (transformOp_.kind == TransformKind::Scale  && sp->LockScaleBoth()) continue;
        if (transformOp_.kind == TransformKind::Rotate && sp->lockRotation)    continue;
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
        // Restore any per-axis-locked component from the snapshot (no-op on that axis).
        if (sp->lockPosX)   t.translate.x = snap.translate.x;
        if (sp->lockPosY)   t.translate.y = snap.translate.y;
        if (sp->lockScaleX) t.scale.x     = snap.scale.x;
        if (sp->lockScaleY) t.scale.y     = snap.scale.y;
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
        if (kind == TransformKind::Scale  && sp->LockScaleBoth()) continue;
        if (kind == TransformKind::Rotate && sp->lockRotation)    continue;
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
        if (sp->lockPosX)   t.translate.x = snap[i].translate.x;
        if (sp->lockPosY)   t.translate.y = snap[i].translate.y;
        if (sp->lockScaleX) t.scale.x     = snap[i].scale.x;
        if (sp->lockScaleY) t.scale.y     = snap[i].scale.y;
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

} // namespace App

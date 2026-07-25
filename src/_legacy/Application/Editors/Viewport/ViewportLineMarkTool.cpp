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
    float effZoom, bool hovered, App::OverlayDL& dl) {
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
        dl.AddRect(ImVec2(std::min(a.x,b.x), std::min(a.y,b.y)),
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
    App::OverlayDL dl(ImGui::GetForegroundDrawList(), &overlay_,
                      renderer_ && renderer_->PresentsViaSwapchain());
    const float alpha = ds.GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);
    const Renderer::Color& oc = part.stroke.color;
    ImU32 col = ImGui::GetColorU32(ImVec4(oc.r, oc.g, oc.b, std::max(0.35f, alpha)));
    const float avgScale = 0.5f * (std::fabs(s.transform.scale.x) + std::fabs(s.transform.scale.y));
    float baseW = part.stroke.width * avgScale;
    Vec2 nrm{ -tan.y, tan.x };
    float thPx = std::max(1.5f, (m.thickness > 1e-5f ? m.thickness : part.stroke.width)
                                    * avgScale * zoom);
    auto seg = [&](Vec2 A, Vec2 B) { dl.AddLine(d2s(A), d2s(B), col, thPx); };
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
                dl.AddLine(d2s(prev), d2s(cur), cut, std::max(1.0f, baseW * zoom));
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
    App::OverlayDL dl(ImGui::GetForegroundDrawList(), &overlay_,
                      renderer_ && renderer_->PresentsViaSwapchain());
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
    dl.AddCircleFilled(sp, vr, centre);
    dl.AddCircle(sp, vr, ring, 0, 1.0f);

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
    if (!isAnchor) { dl.AddCircle(sp, r, ov, 16, th); return; }
    // Diamond oriented ALONG the curve for "dash"; square (axis of the curve) for
    // "gap" — a rotated square is just the diamond turned 45°, so use the tangent.
    Vec2 t = tanScreen; float tl = std::hypot(t.x, t.y);
    if (tl < 1e-4f) t = { 1, 0 }; else { t.x /= tl; t.y /= tl; }
    Vec2 nrm{ -t.y, t.x };
    auto P = [&](float a, float b){ return ImVec2(sp.x + t.x*a + nrm.x*b,
                                                  sp.y + t.y*a + nrm.y*b); };
    if (dashMode) {
        // Diamond: vertices along ±tangent and ±normal.
        dl.AddQuad(P(r,0), P(0,r), P(-r,0), P(0,-r), ov, th);
    } else {
        // Square aligned to the curve (corners on the diagonals).
        float h = r * 0.72f;
        dl.AddQuad(P(h,h), P(-h,h), P(-h,-h), P(h,-h), ov, th);
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

} // namespace App

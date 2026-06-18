#include "Application.h"
#include "EditModeShared.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Edit Mode — Blender-style 2D vertex/edge/face editing of the selected
//  objects. The fundamental unit is a VERTEX (Document::VertRef); edges/faces
//  derive from vertex selection. Handles (Bézier control points) are edited
//  per node with HandleMode constraints (Free/Aligned/Mirrored/Vector).
//
//  Coordinates: nodes/handles live in OBJECT-LOCAL space; the shape transform
//  places them in the world. The editor hit-tests + draws in world→screen via
//  the shape's WorldTransform and the leaf's d2s; edits are written back in
//  local space via InverseTransform.
//
//  Split across EditMode.cpp (interactive editor) and EditModeActions.cpp
//  (the Action_* operators); shared node helpers live in EditModeShared.h.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

using Renderer::Vec2;
using Renderer::VertRef;
using Renderer::Node;
using Renderer::HandleMode;
using Renderer::SelectElementMode;

// Apply the HandleMode coupling after one side of a node was edited. `movedOut`
// = the OUT handle was dragged (else IN). Couples the OPPOSITE handle:
//   • Aligned          → collinear (opposite direction), other keeps its length.
//   • Mirrored         → equal length, other keeps its own direction.
//   • AlignedMirrored  → collinear AND equal length (fully symmetric).
//   • Free / Vector    → no coupling.
void Application::ApplyHandleMode(Node& n, bool movedOut) {
    if (!n.hasIn || !n.hasOut) return;
    const bool aligned  = (n.mode == HandleMode::Aligned ||
                           n.mode == HandleMode::AlignedMirrored);
    const bool mirrored = (n.mode == HandleMode::Mirrored ||
                           n.mode == HandleMode::AlignedMirrored);
    if (!aligned && !mirrored) return;
    Vec2 from  = movedOut ? n.hOut : n.hIn;
    Vec2& other = movedOut ? n.hIn : n.hOut;
    Vec2 dir{ from.x - n.pos.x, from.y - n.pos.y };
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-5f) return;
    Vec2 oOld{ other.x - n.pos.x, other.y - n.pos.y };
    float oLen = std::sqrt(oOld.x * oOld.x + oOld.y * oOld.y);
    if (oLen < 1e-5f) oLen = len;
    // Direction of the opposite handle: collinear-opposite if aligned, else keep
    // its own direction. Length: equal to the moved one if mirrored, else its own.
    Vec2 ou;   // unit direction for `other`
    if (aligned) { ou = { -dir.x / len, -dir.y / len }; }
    else         { ou = { oOld.x / oLen, oOld.y / oLen }; }
    float oUse = mirrored ? len : oLen;
    other = { n.pos.x + ou.x * oUse, n.pos.y + ou.y * oUse };
}

// Begin a modal G/R/S on the SELECTED single handle. Snapshots the node so the op
// can preview live and cancel. Pivots about the handle's own anchor node.
void Application::BeginHandleTransform(TransformKind kind) {
    auto& doc = project_.document;
    const Renderer::HandleRef& h = doc.SelectedHandle();
    Renderer::Shape* s = doc.FindShape(h.shape);
    if (!s || h.part >= (int)s->parts.size()) return;
    auto& ns = s->parts[(size_t)h.part].path.nodes;
    if (h.node >= (int)ns.size()) return;
    handleOp_.Reset();
    handleOp_.kind = kind;
    handleOp_.ref = h;
    handleOp_.snapshot = ns[(size_t)h.node];
    handleOp_.startMouse = lastHoverDoc_;        // doc-units (raw); refined on first update
    MarkUndoLabel(kind == TransformKind::Move ? "Move Handle"
                : kind == TransformKind::Rotate ? "Rotate Handle" : "Scale Handle");
}

// Drive the handle op: move/rotate/scale the selected handle about its anchor; the
// opposite handle follows per the node's HandleMode (ApplyHandleMode). LMB/Enter
// confirm, Esc/RMB cancel (restore the snapshot).
void Application::UpdateHandleTransform(
        EditorState& st,
        const std::function<Vec2(ImVec2)>& s2d,
        const std::function<ImVec2(Vec2)>& d2s,
        float effZoom, bool hovered, ImDrawList* dl) {
    if (!handleOp_.Active()) return;
    const void* self = &st;
    if (handleOp_.owner == nullptr) { if (!hovered) return; handleOp_.owner = self;
                                      handleOp_.startMouse = s2d(ImGui::GetIO().MousePos); }
    if (handleOp_.owner != self) return;
    auto& doc = project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const Renderer::HandleRef& h = handleOp_.ref;
    Renderer::Shape* s = doc.FindShape(h.shape);
    if (!s || h.part >= (int)s->parts.size()) { handleOp_.Reset(); return; }
    auto& ns = s->parts[(size_t)h.part].path.nodes;
    if (h.node >= (int)ns.size()) { handleOp_.Reset(); return; }
    Node& n = ns[(size_t)h.node];
    const Vec2 po = CurPageOriginOfShape(h.shape);

    // s2d returns RAW doc-units (the converters fold the object-local frame in/out),
    // and a handle op edits a single shape, so the handle math is done directly in
    // that doc space about the anchor node.
    Vec2 mDoc = s2d(io.MousePos);
    Vec2 startMouse = handleOp_.startMouse;
    Vec2 anchorD = handleOp_.snapshot.pos;
    Vec2 startHandleD = h.outSide ? handleOp_.snapshot.hOut : handleOp_.snapshot.hIn;

    Vec2 newHandle = startHandleD;
    const float pf = PrecisionDragFactor();
    if (handleOp_.kind == TransformKind::Move) {
        // Move the handle by the (precision-eased) mouse delta.
        newHandle = { startHandleD.x + (mDoc.x - startMouse.x) * pf,
                      startHandleD.y + (mDoc.y - startMouse.y) * pf };
    } else if (handleOp_.kind == TransformKind::Rotate) {
        float a0 = std::atan2(startMouse.y - anchorD.y, startMouse.x - anchorD.x);
        float a1 = std::atan2(mDoc.y - anchorD.y, mDoc.x - anchorD.x);
        float ang = (a1 - a0) * pf;
        if (io.KeyCtrl) { const float inc = (io.KeyShift ? snap_.rotPrecisionIncrement
                                                         : snap_.rotIncrement) * 3.14159265358979f/180.0f;
                          if (inc > 1e-4f) ang = std::round(ang/inc)*inc; }
        Vec2 rel{ startHandleD.x - anchorD.x, startHandleD.y - anchorD.y };
        float c = std::cos(ang), sn = std::sin(ang);
        newHandle = { anchorD.x + rel.x*c - rel.y*sn, anchorD.y + rel.x*sn + rel.y*c };
    } else { // Scale
        float d0 = std::hypot(startMouse.x - anchorD.x, startMouse.y - anchorD.y);
        float d1 = std::hypot(mDoc.x - anchorD.x, mDoc.y - anchorD.y);
        float f = (d0 > 1e-4f) ? d1/d0 : 1.0f;
        f = 1.0f + (f - 1.0f) * pf;
        Vec2 rel{ startHandleD.x - anchorD.x, startHandleD.y - anchorD.y };
        newHandle = { anchorD.x + rel.x*f, anchorD.y + rel.y*f };
    }
    if (h.outSide) { n.hOut = newHandle; n.hasOut = true; }
    else           { n.hIn  = newHandle; n.hasIn  = true; }
    ApplyHandleMode(n, h.outSide);     // opposite follows per HandleMode

    // Guide line anchor→handle (orange).
    ImU32 acc = ImGui::GetColorU32(DesignSystem::DesignSystem::Instance()
        .GetColor(DesignSystem::Tok::S_State_Active_OnPage));
    dl->AddLine(d2s(anchorD), d2s(newHandle), acc, 1.4f);
    if (hovered) ShowMoveCursor();

    bool confirm = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                   ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    bool cancel  = ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (cancel) { n = handleOp_.snapshot; handleOp_.Reset(); rmbConsumedByTransform_ = true; }
    else if (confirm) {
        project_.dirty = true;
        char d[96]; std::snprintf(d, sizeof d, "handle=%s", h.outSide ? "out" : "in");
        LogInfoActionRich(handleOp_.kind == TransformKind::Move ? "Move Handle"
                        : handleOp_.kind == TransformKind::Rotate ? "Rotate Handle" : "Scale Handle", d);
        handleOp_.Reset();
    }
    (void)effZoom;
}

// ── Edit-mode overlay: faces (closed fills hint), edges, vertices + handles ───
void Application::DrawEditOverlay(const std::function<ImVec2(Vec2)>& d2s,
                                  float effZoom, ImDrawList* dl) {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;
    if (editorMode_ != EditorMode::Edit) return;

    using DesignSystem::Tok;
    auto col = [&](Tok t) { return ImGui::GetColorU32(ds.GetColor(t)); };
    ImU32 cEdge    = col(Tok::C_EditHandle_Edge);
    // Selection cue (same orange palette as objects, everywhere): the ACTIVE element
    // is bright orange (S_State_Active_OnPage), a SELECTED-not-active element is a
    // darker orange (S_State_Selected_OnPage). Was the blue accent before.
    ImU32 cActive    = col(Tok::S_State_Active_OnPage);     // bright orange
    ImU32 cSelOrange = col(Tok::S_State_Selected_OnPage);   // darker orange
    ImU32 cEdgeSel = cSelOrange;
    ImU32 cVert    = col(Tok::C_EditHandle_Vertex);
    ImU32 cVertSel = cSelOrange;
    // White used for the edges that bound a SELECTED FACE (so the orange surface
    // tint reads, with crisp white borders). Token: the vertex highlight ring (white).
    ImU32 cFaceEdge = col(Tok::C_EditHandle_VertexRing);
    // Face surface tint: a light TRANSPARENT orange over the selected face.
    ImU32 cFaceSel = (cActive & 0x00FFFFFF) | 0x33000000;
    // Per-handle-type colours (so the two handles of a point are recognisable):
    auto handleColor = [&](HandleMode m) -> ImU32 {
        switch (m) {
            case HandleMode::Free:     return col(Tok::C_EditHandle_Free);
            case HandleMode::Aligned:  return col(Tok::C_EditHandle_Aligned);
            case HandleMode::Mirrored: return col(Tok::C_EditHandle_Mirrored);
            case HandleMode::AlignedMirrored: return col(Tok::C_EditHandle_Mirrored);
            case HandleMode::Vector:   return col(Tok::C_EditHandle_Vector);
        }
        return col(Tok::C_EditHandle_Default);
    };
    // Lighten (selected) / darken (normal) a resolved palette colour — a STATE
    // variation of the type token, so selected handles read brighter than normal
    // ones while keeping their type hue (blue/amber/green/purple).
    auto shade = [](ImU32 c, float f) {       // f>1 lighten, f<1 darken
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        auto ch = [&](float x){ return std::clamp(f <= 1.0f ? x * f
                                                            : x + (1.0f - x) * (f - 1.0f),
                                                  0.0f, 1.0f); };
        return ImGui::ColorConvertFloat4ToU32(ImVec4(ch(v.x), ch(v.y), ch(v.z), v.w));
    };
    const float vr = 3.5f, hr = 3.0f;
    const SelectElementMode em = doc.elementMode;

    for (uint64_t sid : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(sid);
        if (!s) continue;
        // Geometry is page-relative; use THIS viewport's DISPLAY origin for the
        // shape's page (curPageViews_), not the stored Manual position — otherwise
        // the overlay is offset whenever an auto layout (Single Page, spreads, …)
        // moves the page away from its Artboard::pos, matching the object-mode
        // selection overlay which already uses CurPageOriginOfShape.
        const Vec2 po = CurPageOriginOfShape(sid);
        for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
            Renderer::Part& part = s->parts[(size_t)pi];
            const auto& nodes = part.path.nodes;
            // A curve-like part is always point-edited (sub-modes don't apply);
            // a MESH honours the vertex/edge/face sub-mode.
            const bool curve = part.IsCurveLike();
            // NURBS: the nodes are CONTROL POINTS — draw the straight control
            // polygon (the "hull"), never Bézier handles. Poly: straight edges,
            // no handles. Bézier: cubic edges + in/out handles.
            const bool nurbs = curve && part.spline == Renderer::SplineType::Nurbs;
            const bool bezier = curve && part.spline == Renderer::SplineType::Bezier;
            const bool showVerts = curve || em == SelectElementMode::Vertex;
            const bool showEdges = !curve && em == SelectElementMode::Edge;
            const bool showFaces = !curve && em == SelectElementMode::Face;


            // Face highlight (Face mode): if all the part's verts are selected, the
            // FACE is selected — tint its interior transparent-orange. The edges that
            // bound it are then drawn WHITE below (cFaceEdge) so the surface reads.
            bool faceSelected = false;
            if (showFaces && part.path.closed && nodes.size() >= 3) {
                bool allSel = true;
                for (int ni = 0; ni < (int)nodes.size(); ++ni)
                    if (!doc.IsVertSelected({ sid, pi, ni })) { allSel = false; break; }
                if (allSel) {
                    faceSelected = true;
                    // Simple convex-ish fan tint over the flattened outline.
                    bool cl=false; auto poly = Renderer::Tessellator::OutlinePart(*s, part, effZoom, cl, po);
                    if (poly.size() >= 3) {
                        std::vector<ImVec2> sp; sp.reserve(poly.size());
                        for (auto& p : poly) sp.push_back(d2s(p));
                        dl->AddConvexPolyFilled(sp.data(), (int)sp.size(), cFaceSel);
                    }
                }
            }

            // Edges. Build the per-subpath edge list (ia→ib) so we never bridge
            // ACROSS a subpath boundary (that was the stray "construction line" to
            // a far endpoint on a branched path), and only CLOSE within a subpath.
            std::vector<std::pair<int,int>> edges;
            {
                const int sc = std::max(1, part.path.subCount());
                for (int spi = 0; spi < sc; ++spi) {
                    int b0 = 0, e0 = (int)nodes.size();
                    part.path.subRange(spi, b0, e0);
                    for (int k = b0; k + 1 < e0; ++k) edges.push_back({ k, k + 1 });
                    if (part.path.closed && (e0 - b0) >= 2) edges.push_back({ e0 - 1, b0 });
                }
            }
            for (const auto& ed : edges) {
                int ia = ed.first, ib = ed.second;
                const Node& a = nodes[(size_t)ia];
                const Node& b = nodes[(size_t)ib];
                // An edge is "selected" when both its endpoints are selected.
                bool edgeSel = showEdges &&
                    doc.IsVertSelected({ sid, pi, ia }) && doc.IsVertSelected({ sid, pi, ib });
                // Face mode: the edges bounding a selected face are WHITE (the orange
                // is the surface tint); edge mode: selected edges are orange.
                ImU32 ec = faceSelected ? cFaceEdge : (edgeSel ? cEdgeSel : cEdge);
                float ew = (edgeSel || faceSelected) ? 2.5f : 1.5f;
                if (nurbs) {
                    // Control polygon (hull): straight, dimmer — it's a guide, not
                    // the curve (the smooth curve itself is Vulkan-rendered).
                    dl->AddLine(d2s(NodeWorld(*s, a, po)), d2s(NodeWorld(*s, b, po)),
                                edgeSel ? cEdgeSel : col(Tok::C_EditHandle_NurbsHull), ew);
                } else if (bezier && (a.hasOut || b.hasIn)) {
                    // The construction line between nodes must stay as smooth as the
                    // Vulkan-rendered curve, so derive the step count from the screen
                    // chord error (sagitta × zoom), not a fixed 24 (which faceted at
                    // high zoom). bend = max control-point deviation from the chord.
                    Vec2 c0 = a.hasOut ? a.hOut : a.pos;
                    Vec2 c1 = b.hasIn  ? b.hIn  : b.pos;
                    Vec2 chord{ b.pos.x - a.pos.x, b.pos.y - a.pos.y };
                    float cl = std::sqrt(chord.x*chord.x + chord.y*chord.y);
                    float bend = 0.0f;
                    if (cl > 1e-5f) {
                        Vec2 nrm{ -chord.y / cl, chord.x / cl };
                        bend = std::max(std::fabs((c0.x-a.pos.x)*nrm.x + (c0.y-a.pos.y)*nrm.y),
                                        std::fabs((c1.x-a.pos.x)*nrm.x + (c1.y-a.pos.y)*nrm.y));
                    }
                    float bendPx = bend * std::max(effZoom, 0.05f);
                    int steps = std::clamp((int)std::ceil(std::sqrt(bendPx / 0.3f) * 2.0f),
                                           8, 512);
                    std::vector<Vec2> pts;
                    Renderer::Tessellator::FlattenCubic(
                        a.pos, c0, b.hasIn ? b.hIn : b.pos, b.pos, steps, pts);
                    ImVec2 prev = d2s(NodeWorld(*s, a, po));
                    for (Vec2 p : pts) {
                        ImVec2 cur = d2s(Renderer::Tessellator::WorldTransform(*s, p, po));
                        dl->AddLine(prev, cur, ec, ew); prev = cur;
                    }
                } else {
                    dl->AddLine(d2s(NodeWorld(*s, a, po)), d2s(NodeWorld(*s, b, po)), ec, ew);
                }
            }

            // Handles + vertices (vertices hidden in edge/face mesh modes).
            for (int ni = 0; ni < (int)nodes.size(); ++ni) {
                const Node& n = nodes[(size_t)ni];
                ImVec2 ap = d2s(NodeWorld(*s, n, po));
                VertRef ref{ sid, pi, ni };
                bool sel    = doc.IsVertSelected(ref);
                bool active = (doc.ActiveVert() == ref);
                const bool inSel  = doc.IsHandleSelected({ sid, pi, ni, false });
                const bool outSel = doc.IsHandleSelected({ sid, pi, ni, true });
                const bool nodeHasSelHandle = inSel || outSel;
                // Handle drawing rules (Blender-like) — all in the handle's TYPE hue
                // (blue/amber/green/purple), NEVER orange:
                //  • LINE: lightened (selected) when the POINT is selected OR this
                //    handle is selected; else the type colour darkened.
                //  • DOT: lightened when THIS handle is selected, darkened otherwise.
                //    Selecting the POINT colours the LINES but leaves the handle DOTS
                //    in their normal (darkened) state.
                auto handleDot = [&](bool outSide, ImVec2 hp) {
                    bool thisSel = outSide ? outSel : inSel;
                    bool lineSel = sel || active || thisSel;   // point-sel colours both lines
                    ImU32 typeC  = handleColor(n.mode);
                    ImU32 lineC  = lineSel ? shade(typeC, 1.35f) : shade(typeC, 0.75f);
                    ImU32 dotC   = thisSel ? shade(typeC, 1.35f) : shade(typeC, 0.8f);
                    dl->AddLine(ap, hp, lineC, lineSel ? 1.5f : 1.0f);
                    dl->AddCircleFilled(hp, thisSel ? hr + 1.0f : hr, dotC);
                };
                // Bézier handles only — NURBS uses control points (no handles)
                // and Poly is straight, so neither shows handles.
                if (bezier && (sel || active || nodeHasSelHandle) && (showVerts || curve)) {
                    if (n.hasIn)  handleDot(false, d2s(HandleInWorld(*s, n, po)));
                    if (n.hasOut) handleDot(true,  d2s(HandleOutWorld(*s, n, po)));
                }
                if (showVerts) {
                    // Point: active = bright orange, selected = darker orange, else dim.
                    ImU32 c = active ? cActive : (sel ? cVertSel : cVert);
                    dl->AddCircleFilled(ap, vr, c);
                    dl->AddCircle(ap, vr, col(Tok::C_EditHandle_VertexRing), 0, 1.0f);
                }
            }
        }
    }
    (void)effZoom;
}

// ── Edit-mode interaction ─────────────────────────────────────────────────────
void Application::HandleEditMode(EditorState& st,
                                 const std::function<Vec2(ImVec2)>& s2d,
                                 const std::function<ImVec2(Vec2)>& d2s,
                                 float effZoom, bool hovered, ImDrawList* dl) {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const void* self = &st;
    const float zoom = std::max(0.0001f, effZoom);
    const float pickPx = 7.0f;          // screen-px pick radius
    const SelectElementMode em = doc.elementMode;

    // Foreign-gesture guard (another leaf owns the edit drag).
    if (editDrag_.Active() && editDrag_.owner != self) return;

    const bool lpressed  = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool lreleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool escape    = ImGui::IsKeyPressed(ImGuiKey_Escape);

    // ── Drive an active drag (verts or a handle) ─────────────────────────────
    if (editDrag_.Active() && editDrag_.owner == self) {
        if (escape || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            // Cancel: restore the snapshot.
            if (editDrag_.kind == EditDragState::Kind::Verts) {
                size_t k = 0;
                for (const VertRef& v : editDrag_.ids) {
                    Renderer::Shape* s = doc.FindShape(v.shape);
                    if (s && v.part < (int)s->parts.size()) {
                        auto& ns = s->parts[(size_t)v.part].path.nodes;
                        if (v.node < (int)ns.size() && k < editDrag_.snapshot.size())
                            ns[(size_t)v.node] = editDrag_.snapshot[k];
                    }
                    ++k;
                }
            } else if (editDrag_.kind != EditDragState::Kind::Box &&
                       !editDrag_.snapshot.empty()) {
                const VertRef& v = editDrag_.handleVert;
                Renderer::Shape* s = doc.FindShape(v.shape);
                if (s && v.part < (int)s->parts.size()) {
                    auto& ns = s->parts[(size_t)v.part].path.nodes;
                    if (v.node < (int)ns.size()) ns[(size_t)v.node] = editDrag_.snapshot[0];
                }
            }
            editDrag_.Reset();
            return;
        }

        // Box-select drive. Everything commits on RELEASE so it's cancellable:
        //   • right-click / Escape before release → cancel, keep the selection;
        //   • release WITHOUT a drag (a plain click) → clear (unless Shift);
        //   • release WITH a drag → box-select the enclosed VERTICES and HANDLES
        //     (additive when Shift, else replace the selection).
        if (editDrag_.kind == EditDragState::Kind::Box) {
            Vec2 m = s2d(io.MousePos);
            // Track whether the box was actually dragged (vs a click).
            if (std::hypot(io.MousePos.x - d2s(editDrag_.dragStart).x,
                           io.MousePos.y - d2s(editDrag_.dragStart).y) > 3.0f)
                editDrag_.movedPastThreshold = true;
            // Cancel: right-click or Escape keeps the current selection.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editDrag_.Reset(); rmbConsumedByTransform_ = true; return;
            }
            ImVec2 a = d2s(editDrag_.dragStart), b = io.MousePos;
            ImU32 cAccent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
            dl->AddRectFilled(a, b, (cAccent & 0x00FFFFFF) | 0x22000000);
            dl->AddRect(a, b, cAccent, 0.0f, 0, 1.0f);
            if (lreleased) {
                if (!editDrag_.movedPastThreshold) {
                    // Plain click on empty space → clear (Shift keeps the selection).
                    if (!io.KeyShift) { doc.ClearVertSelection(); doc.ClearHandleSelection(); }
                } else {
                    float x0 = std::min(editDrag_.dragStart.x, m.x), x1 = std::max(editDrag_.dragStart.x, m.x);
                    float y0 = std::min(editDrag_.dragStart.y, m.y), y1 = std::max(editDrag_.dragStart.y, m.y);
                    if (!io.KeyShift) { doc.ClearVertSelection(); doc.ClearHandleSelection(); }
                    auto inBox = [&](Vec2 w){ return w.x>=x0&&w.x<=x1&&w.y>=y0&&w.y<=y1; };
                    ForEachEditableNode(doc, [&](Renderer::Shape& s, uint64_t sid, int pi, int ni,
                                                 Renderer::Part&, Node& n) {
                        Vec2 po = CurPageOriginOfShape(sid);
                        VertRef vref{ sid, pi, ni };
                        // A vertex in the box → select the vertex AND its handles (the
                        // point's handles come with it, even if outside the box / not
                        // previously visible — Blender-style).
                        const bool vertNowSel = inBox(NodeWorld(s, n, po));
                        if (vertNowSel) {
                            doc.VertSelectAdd(vref);
                            if (n.hasIn)  doc.HandleSelectAdd({ sid, pi, ni, false });
                            if (n.hasOut) doc.HandleSelectAdd({ sid, pi, ni, true });
                        }
                        // A handle in the box is selectable only when it is VISIBLE,
                        // i.e. its point is selected (now or already) — invisible handles
                        // of unselected points can't be box-selected.
                        const bool pointVisible = vertNowSel || doc.IsVertSelected(vref) ||
                                                  doc.ActiveVert() == vref;
                        if (pointVisible) {
                            if (n.hasIn  && inBox(HandleInWorld(s, n, po)))
                                doc.HandleSelectAdd({ sid, pi, ni, false });
                            if (n.hasOut && inBox(HandleOutWorld(s, n, po)))
                                doc.HandleSelectAdd({ sid, pi, ni, true });
                        }
                    });
                    MarkUndoLabel("Box Select");
                }
                editDrag_.Reset();
            }
            return;
        }

        // Vertex / handle drag: integrate real motion (drift-free wrap), eased by
        // the global Shift precision-drag factor (finer move without slowing the
        // cursor).
        ImVec2 dPx = GestureMouseDelta();
        const float pf = PrecisionDragFactor();
        editDrag_.moveAccum.x += dPx.x * pf / zoom;
        editDrag_.moveAccum.y += dPx.y * pf / zoom;
        Vec2 disp{ editDrag_.moveAccum.x - editDrag_.dragStart.x,
                   editDrag_.moveAccum.y - editDrag_.dragStart.y };
        if (std::hypot(disp.x, disp.y) * zoom > 3.0f) editDrag_.movedPastThreshold = true;

        if (editDrag_.movedPastThreshold) {
            if (editDrag_.kind == EditDragState::Kind::Verts) {
                // Move every selected vertex by the local-space delta. World disp
                // → local disp uses the shape's inverse-rotation/scale; assume a
                // single shape selection for edit (Blender edits one mesh) but
                // support multi via per-shape inverse.
                size_t k = 0;
                for (const VertRef& v : editDrag_.ids) {
                    Renderer::Shape* s = doc.FindShape(v.shape);
                    if (!s || v.part >= (int)s->parts.size()) { ++k; continue; }
                    auto& ns = s->parts[(size_t)v.part].path.nodes;
                    if (v.node >= (int)ns.size() || k >= editDrag_.snapshot.size()) { ++k; continue; }
                    // Convert the world displacement into this shape's local frame
                    // by mapping two world points through the inverse transform.
                    Vec2 baseW = NodeWorld(*s, editDrag_.snapshot[k]);
                    Vec2 tgtW{ baseW.x + disp.x, baseW.y + disp.y };
                    Vec2 baseL = Renderer::Tessellator::InverseTransform(*s, baseW);
                    Vec2 tgtL  = Renderer::Tessellator::InverseTransform(*s, tgtW);
                    Vec2 dL{ tgtL.x - baseL.x, tgtL.y - baseL.y };
                    Node nn = editDrag_.snapshot[k];
                    nn.pos.x  += dL.x; nn.pos.y  += dL.y;
                    nn.hIn.x  += dL.x; nn.hIn.y  += dL.y;
                    nn.hOut.x += dL.x; nn.hOut.y += dL.y;
                    ns[(size_t)v.node] = nn;
                    ++k;
                }
            } else { // HandleIn / HandleOut
                const VertRef& v = editDrag_.handleVert;
                Renderer::Shape* s = doc.FindShape(v.shape);
                if (s && v.part < (int)s->parts.size()) {
                    auto& ns = s->parts[(size_t)v.part].path.nodes;
                    if (v.node < (int)ns.size()) {
                        Node& n = ns[(size_t)v.node];
                        bool outSide = (editDrag_.kind == EditDragState::Kind::HandleOut);
                        Vec2 base0 = outSide ? editDrag_.snapshot[0].hOut
                                             : editDrag_.snapshot[0].hIn;
                        Vec2 baseW = Renderer::Tessellator::WorldTransform(*s, base0);
                        Vec2 tgtW{ baseW.x + disp.x, baseW.y + disp.y };
                        Vec2 tgtL = Renderer::Tessellator::InverseTransform(*s, tgtW);
                        if (outSide) { n.hOut = tgtL; n.hasOut = true; }
                        else         { n.hIn  = tgtL; n.hasIn  = true; }
                        ApplyHandleMode(n, outSide);
                    }
                }
            }
            ShowMoveCursor();
            WrapMouseInRect(gestureCanvasMin_, gestureCanvasMax_);
        }
        if (lreleased) {
            if (editDrag_.movedPastThreshold) project_.dirty = true;
            // A plain CLICK (no drag) on an already-selected element among many
            // reduces the selection to just those targets (deferred from press, so a
            // drag could move the whole group instead).
            else if (!editDrag_.reduceTargets.empty()) {
                doc.ClearVertSelection();
                for (const VertRef& t : editDrag_.reduceTargets) doc.VertSelectAdd(t);
            }
            editDrag_.Reset();
        }
        return;
    }

    // ── Idle: hit-test on press → start the appropriate drag / selection ─────
    if (lpressed) {
        Vec2 m = s2d(io.MousePos);
        // 1) Handles of selected/active vertices take priority.
        VertRef bestVert; float bestVertD2 = pickPx * pickPx;
        int   bestHandle = 0;           // 0 none, 1 in, 2 out
        VertRef handleVert; float bestHandleD2 = pickPx * pickPx;

        ForEachEditableNode(doc, [&](Renderer::Shape& s, uint64_t sid, int pi, int ni,
                                     Renderer::Part&, Node& n) {
            VertRef ref{ sid, pi, ni };
            Vec2 po = CurPageOriginOfShape(sid);
            ImVec2 ap = d2s(NodeWorld(s, n, po));
            float d2 = Dist2(ap, io.MousePos);
            if (d2 < bestVertD2) { bestVertD2 = d2; bestVert = ref; }
            // Handles pickable when the node is selected/active, OR when one of its
            // handles is currently selected (so it stays grabbable).
            const bool nodeHasSelHandle =
                doc.IsHandleSelected({ sid, pi, ni, false }) ||
                doc.IsHandleSelected({ sid, pi, ni, true });
            if (doc.IsVertSelected(ref) || doc.ActiveVert() == ref || nodeHasSelHandle) {
                if (n.hasIn) {
                    float di = Dist2(d2s(HandleInWorld(s, n, po)), io.MousePos);
                    if (di < bestHandleD2) { bestHandleD2 = di; bestHandle = 1; handleVert = ref; }
                }
                if (n.hasOut) {
                    float dou = Dist2(d2s(HandleOutWorld(s, n, po)), io.MousePos);
                    if (dou < bestHandleD2) { bestHandleD2 = dou; bestHandle = 2; handleVert = ref; }
                }
            }
        });

        // A handle hit (closer than a vertex) selects that handle AND (if it ends up
        // selected) starts a drag. Shift+click TOGGLES it (adds, or removes if already
        // selected — keeping the rest); a plain click selects only it.
        if (bestHandle != 0 && bestHandleD2 <= bestVertD2) {
            Renderer::Shape* s = doc.FindShape(handleVert.shape);
            if (s && handleVert.part < (int)s->parts.size()) {
                Node& n = s->parts[(size_t)handleVert.part].path.nodes[(size_t)handleVert.node];
                Renderer::HandleRef hr{ handleVert.shape, handleVert.part,
                                        handleVert.node, bestHandle == 2 };
                if (io.KeyShift)        doc.HandleSelectToggle(hr);
                else if (!doc.IsHandleSelected(hr)) doc.HandleSelectOnly(hr);
                else                    doc.HandleSelectAdd(hr);   // re-affirm active
                // Only arm a drag if the handle is (still) selected — a Shift+click
                // that DESELECTED it must not start a drag.
                if (doc.IsHandleSelected(hr)) {
                    editDrag_.Reset();
                    editDrag_.kind  = (bestHandle == 2) ? EditDragState::Kind::HandleOut
                                                        : EditDragState::Kind::HandleIn;
                    editDrag_.owner = self;
                    editDrag_.handleVert = handleVert;
                    editDrag_.dragStart = editDrag_.moveAccum = m;
                    editDrag_.snapshot.assign(1, n);
                    BeginGestureMouseTracking();
                }
            }
            return;
        }

        // Determine what was clicked, honouring the sub-mode. A CURVE is always
        // vertex-picked; a MESH picks the nearest EDGE (Edge mode) or the FACE
        // under the cursor (Face mode). The selection is a set of VERTS:
        //   vertex → that vert; edge → its 2 ends; face → the whole part ring.
        std::vector<VertRef> targets;     // empty → nothing hit
        VertRef anchorForMove = bestVert; // vert used to arm a subsequent move

        auto partIsCurve = [&](uint64_t sid, int pi) {
            Renderer::Shape* s = doc.FindShape(sid);
            return s && pi < (int)s->parts.size() && s->parts[(size_t)pi].IsCurveLike();
        };
        const bool curveActive = (bestVert.shape != 0) && partIsCurve(bestVert.shape, bestVert.part);

        if (em == SelectElementMode::Vertex || curveActive) {
            if (bestVert.shape != 0) targets.push_back(bestVert);
        } else if (em == SelectElementMode::Edge) {
            // Nearest mesh edge by point-to-segment distance.
            float bestD2 = pickPx * pickPx; VertRef ea, eb;
            for (uint64_t sid : doc.Selection()) {
                Renderer::Shape* s = doc.FindShape(sid);
                if (!s) continue;
                Vec2 po = CurPageOriginOfShape(sid);
                for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
                    Renderer::Part& part = s->parts[(size_t)pi];
                    if (part.IsCurveLike()) continue;
                    auto& ns = part.path.nodes;
                    size_t segs = part.path.closed ? ns.size() : (ns.empty() ? 0 : ns.size() - 1);
                    for (size_t i = 0; i < segs; ++i) {
                        ImVec2 a = d2s(NodeWorld(*s, ns[i], po));
                        ImVec2 b = d2s(NodeWorld(*s, ns[(i + 1) % ns.size()], po));
                        ImVec2 ab{ b.x - a.x, b.y - a.y };
                        float len2 = ab.x * ab.x + ab.y * ab.y;
                        float t = len2 > 1e-3f ? std::clamp(((io.MousePos.x-a.x)*ab.x +
                                  (io.MousePos.y-a.y)*ab.y)/len2, 0.0f, 1.0f) : 0.0f;
                        ImVec2 c{ a.x + ab.x*t, a.y + ab.y*t };
                        float d2 = Dist2(c, io.MousePos);
                        if (d2 < bestD2) { bestD2 = d2; ea = { sid, pi, (int)i };
                                           eb = { sid, pi, (int)((i + 1) % ns.size()) }; }
                    }
                }
            }
            if (ea.shape != 0) { targets = { ea, eb }; anchorForMove = ea; }
        } else { // Face
            for (uint64_t sid : doc.Selection()) {
                Renderer::Shape* s = doc.FindShape(sid);
                if (!s) continue;
                Vec2 po = CurPageOriginOfShape(sid);
                for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
                    Renderer::Part& part = s->parts[(size_t)pi];
                    if (part.IsCurveLike() || !part.path.closed) continue;
                    bool cl=false; auto poly = Renderer::Tessellator::OutlinePart(*s, part, zoom, cl, po);
                    if (poly.size() >= 3 && PointInPoly(poly, m)) {
                        for (int i = 0; i < (int)part.path.nodes.size(); ++i)
                            targets.push_back({ sid, pi, i });
                        anchorForMove = { sid, pi, 0 };
                        break;
                    }
                }
                if (!targets.empty()) break;
            }
        }

        if (!targets.empty()) {
            // Whether this click is on an already-fully-selected element.
            bool allSelected = true;
            for (const VertRef& t : targets) if (!doc.IsVertSelected(t)) { allSelected = false; break; }
            // Whether the selection contains anything BESIDES these targets (so a
            // plain click on an already-selected element reduces to just it).
            bool hasOthers = false;
            for (const VertRef& v : doc.VertSelection()) {
                bool isTarget = false;
                for (const VertRef& t : targets) if (v == t) { isTarget = true; break; }
                if (!isTarget) { hasOthers = true; break; }
            }
            bool deferReduce = false;
            if (io.KeyShift) {
                // Shift keeps the rest: toggle these into/out of the selection.
                for (const VertRef& t : targets) doc.VertSelectToggle(t);
            } else if (allSelected && hasOthers) {
                // Click on an already-selected element among many: KEEP the group now
                // (so a drag can move it); a click WITHOUT a drag reduces to just the
                // targets on release. Re-affirm the clicked one as active.
                doc.VertSelectAdd(anchorForMove);
                deferReduce = true;
            } else if (!allSelected) {
                // Plain click on a new element selects ONLY it.
                doc.ClearVertSelection();
                for (const VertRef& t : targets) doc.VertSelectAdd(t);
            } else {
                doc.VertSelectAdd(anchorForMove);   // sole selection → re-affirm active
            }
            // Label + log the element selection (clearer than the generic "Edit").
            {
                const char* em = doc.elementMode == Renderer::SelectElementMode::Vertex ? "vertex"
                               : doc.elementMode == Renderer::SelectElementMode::Edge   ? "edge"
                                                                                        : "face";
                const Renderer::Shape* sh = doc.FindShape(targets.front().shape);
                const std::string oname = sh ? (sh->name.empty() ? "Object" : sh->name) : "";
                char d[160];
                std::snprintf(d, sizeof d, "element=%s  count=%d  object=%s",
                              em, (int)doc.VertSelection().size(), oname.c_str());
                MarkUndoLabel(io.KeyShift ? "Extend Select" : "Select");
                LogInfoActionRich(std::string("Select ") + em, d);
            }

            // Arm a move of all selected verts.
            if (doc.HasVertSelection()) {
                editDrag_.Reset();
                editDrag_.kind  = EditDragState::Kind::Verts;
                editDrag_.owner = self;
                editDrag_.dragStart = editDrag_.moveAccum = m;
                if (deferReduce) editDrag_.reduceTargets = targets;   // reduce on click
                editDrag_.ids.assign(doc.VertSelection().begin(), doc.VertSelection().end());
                editDrag_.snapshot.clear();
                for (const VertRef& v : editDrag_.ids) {
                    Renderer::Shape* s = doc.FindShape(v.shape);
                    if (s && v.part < (int)s->parts.size() &&
                        v.node < (int)s->parts[(size_t)v.part].path.nodes.size())
                        editDrag_.snapshot.push_back(
                            s->parts[(size_t)v.part].path.nodes[(size_t)v.node]);
                    else editDrag_.snapshot.push_back(Node{});
                }
                BeginGestureMouseTracking();
            }
            return;
        }

        // Empty space → start a box-select. DO NOT clear the selection now: a plain
        // click (no drag) clears it on RELEASE; a drag box-selects; a right-click /
        // Escape before release CANCELS (so an in-progress box-select keeps the
        // current selection). Shift keeps the existing selection (additive box).
        editDrag_.Reset();
        editDrag_.kind  = EditDragState::Kind::Box;
        editDrag_.owner = self;
        editDrag_.dragStart = m;
        editDrag_.reduceTargets.clear();   // (unused for Box; Box additivity = Shift)
        return;
    }
}


} // namespace App

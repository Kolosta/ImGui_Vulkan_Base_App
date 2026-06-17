#include "Application.h"
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
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

using Renderer::Vec2;
using Renderer::VertRef;
using Renderer::Node;
using Renderer::HandleMode;
using Renderer::SelectElementMode;

namespace {

// World position of a node's anchor / handles, through the shape transform AND
// the owning page origin (geometry is page-relative).
Vec2 NodeWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.pos, po);
}
Vec2 HandleInWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.hIn, po);
}
Vec2 HandleOutWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.hOut, po);
}

float Dist2(ImVec2 a, ImVec2 b) {
    float dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy;
}

// Even-odd point-in-polygon (world-space), for Face-mode picking.
bool PointInPoly(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                       (poly[j].y - poly[i].y) + poly[i].x))
            in = !in;
    return in;
}

// Iterate every editable node of the selection (the selected OBJECTS' parts).
template <class F>
void ForEachEditableNode(Renderer::Document& doc, F&& fn) {
    for (uint64_t sid : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(sid);
        if (!s) continue;
        s->EnsurePath();   // editing requires baked nodes (primitive → path)
        for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
            Renderer::Part& part = s->parts[(size_t)pi];
            for (int ni = 0; ni < (int)part.path.nodes.size(); ++ni)
                fn(*s, sid, pi, ni, part, part.path.nodes[(size_t)ni]);
        }
    }
}

} // namespace

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

// ── Actions ──────────────────────────────────────────────────────────────────
void Application::Action_DeleteElements() {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    // Delete selected vertices. Removing a node from a part; group by (shape,part)
    // and erase in descending node order so indices stay valid.
    struct Key { uint64_t shape; int part; };
    // Collect node indices per (shape,part).
    std::vector<VertRef> sel(doc.VertSelection().begin(), doc.VertSelection().end());
    std::sort(sel.begin(), sel.end(), [](const VertRef& a, const VertRef& b){
        if (a.shape != b.shape) return a.shape < b.shape;
        if (a.part  != b.part)  return a.part  < b.part;
        return a.node > b.node;   // descending node
    });
    for (const VertRef& v : sel) {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) continue;
        Renderer::Part& part = s->parts[(size_t)v.part];
        auto& ns = part.path.nodes;
        if (v.node < (int)ns.size()) {
            ns.erase(ns.begin() + v.node);
            part.path.OnNodeErased(v.node);   // keep subpath boundaries valid
        }
    }
    // Drop parts/shapes that became degenerate (<2 nodes).
    for (uint64_t sid : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(sid);
        if (!s) continue;
        for (int pi = (int)s->parts.size() - 1; pi >= 0; --pi)
            if (s->parts[(size_t)pi].path.nodes.size() < 2 &&
                !s->parts[(size_t)pi].IsParametric())
                s->parts.erase(s->parts.begin() + pi);
    }
    doc.ClearVertSelection();
    project_.dirty = true;
}

void Application::Action_SetHandleType(HandleMode mode) {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    for (const VertRef& v : doc.VertSelection()) {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) continue;
        Renderer::Part& part = s->parts[(size_t)v.part];
        // Handles are only honoured by a Bézier curve. A Poly part (e.g. an IOF
        // line symbol) ignores them → setting a handle type did nothing. Promote
        // the whole part to Bézier so the chosen handles actually shape the curve.
        if (part.type == Renderer::PartType::Curve && part.spline == Renderer::SplineType::Poly)
            part.spline = Renderer::SplineType::Bezier;
        else if (part.type == Renderer::PartType::Mesh)
            { part.type = Renderer::PartType::Curve; part.spline = Renderer::SplineType::Bezier; }
        auto& ns = part.path.nodes;
        if (v.node >= (int)ns.size()) continue;
        Node& n = ns[(size_t)v.node];
        n.mode = mode;
        // Seed default tangents (a quarter of the adjacent segment length, along
        // the neighbour direction) so handles appear at a sensible scale for ANY
        // unit (a fixed 40-unit default was huge for mm-based IOF symbols).
        Vec2 prevP = n.pos, nextP = n.pos;
        if (v.node > 0)                 prevP = ns[(size_t)(v.node - 1)].pos;
        if (v.node + 1 < (int)ns.size()) nextP = ns[(size_t)(v.node + 1)].pos;
        Vec2 tangent{ nextP.x - prevP.x, nextP.y - prevP.y };
        float tl = std::hypot(tangent.x, tangent.y);
        if (tl < 1e-4f) { tangent = { 1, 0 }; tl = 1.0f; }
        tangent.x /= tl; tangent.y /= tl;
        float len = std::max(0.25f * std::max(std::hypot(n.pos.x - prevP.x, n.pos.y - prevP.y),
                                              std::hypot(nextP.x - n.pos.x, nextP.y - n.pos.y)),
                             1e-3f);
        if (!n.hasIn && !n.hasOut) {
            n.hasIn = n.hasOut = true;
            n.hOut = { n.pos.x + tangent.x * len, n.pos.y + tangent.y * len };
            n.hIn  = { n.pos.x - tangent.x * len, n.pos.y - tangent.y * len };
        } else if (!n.hasIn) {
            n.hasIn = true; n.hIn = { 2*n.pos.x - n.hOut.x, 2*n.pos.y - n.hOut.y };
        } else if (!n.hasOut) {
            n.hasOut = true; n.hOut = { 2*n.pos.x - n.hIn.x, 2*n.pos.y - n.hIn.y };
        }
        ApplyHandleMode(n, true);   // Aligned/Mirrored snap the pair
    }
    MarkUndoLabel("Set handle type");
    project_.dirty = true;
}

// Strip both handles from the selected vertices → perfectly straight corner
// points (hasIn/hasOut = false, handles collapsed onto the anchor, Vector mode).
// This is the missing inverse of adding handles: once a straight point gains
// handles there was no way back. Unlike "Vector" (which seeds visible handles for
// editing), this removes them entirely so the segments are dead straight.
void Application::Action_RemoveHandles() {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    for (const VertRef& v : doc.VertSelection()) {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) continue;
        auto& ns = s->parts[(size_t)v.part].path.nodes;
        if (v.node >= (int)ns.size()) continue;
        Node& n = ns[(size_t)v.node];
        n.hasIn = n.hasOut = false;
        n.hIn = n.hOut = n.pos;
        n.mode = HandleMode::Vector;
    }
    MarkUndoLabel("Remove handles");
    project_.dirty = true;
}

void Application::Action_ToggleCloseCurve() {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    Renderer::Shape* s = doc.FindShape(doc.ActiveId());
    if (!s) { if (!doc.Selection().empty()) s = doc.FindShape(doc.Selection().front()); }
    if (!s || s->parts.empty()) return;
    Renderer::Part& part = s->parts.front();
    part.EnsurePath();
    part.path.closed = !part.path.closed;
    project_.dirty = true;
}

// Merge modes: 0 center, 1 2D-cursor, 2 by-distance, 3 first, 4 last.
// Merging truly FUSES the selected vertices into a single vertex (the others are
// removed and the path reconnected), not just stacked at the same spot.
void Application::Action_MergeVertices(int mode) {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    auto sel = doc.VertSelection();
    if (sel.size() < 2) return;

    auto worldOf = [&](const VertRef& v) -> Vec2 {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) return {0,0};
        auto& ns = s->parts[(size_t)v.part].path.nodes;
        if (v.node >= (int)ns.size()) return {0,0};
        return NodeWorld(*s, ns[(size_t)v.node], CurPageOriginOfShape(v.shape));
    };

    // Group selected verts by (shape,part); merging only fuses within a part.
    struct Group { uint64_t shape; int part; std::vector<int> nodes; };
    std::vector<Group> groups;
    for (const VertRef& v : sel) {
        Group* g = nullptr;
        for (Group& cand : groups)
            if (cand.shape == v.shape && cand.part == v.part) { g = &cand; break; }
        if (!g) { groups.push_back({ v.shape, v.part, {} }); g = &groups.back(); }
        g->nodes.push_back(v.node);
    }

    auto fuseGroup = [&](Group& g, Vec2 worldTarget) {
        Renderer::Shape* s = doc.FindShape(g.shape);
        if (!s || g.part >= (int)s->parts.size()) return;
        auto& ns = s->parts[(size_t)g.part].path.nodes;
        std::sort(g.nodes.begin(), g.nodes.end());
        if (g.nodes.empty()) return;
        // Keep the FIRST selected node as the survivor; place it at the target,
        // collapse its handles, then erase the rest (descending) → one vertex.
        int keep = g.nodes.front();
        if (keep < (int)ns.size()) {
            Node& k = ns[(size_t)keep];
            k.pos = Renderer::Tessellator::InverseTransform(*s, worldTarget,
                                                            CurPageOriginOfShape(g.shape));
            // A fused point loses its now-meaningless handles toward removed nbrs.
            k.hIn = k.hOut = k.pos; k.hasIn = k.hasOut = false; k.mode = HandleMode::Vector;
        }
        for (int i = (int)g.nodes.size() - 1; i >= 1; --i) {
            int idx = g.nodes[(size_t)i];
            if (idx < (int)ns.size()) ns.erase(ns.begin() + idx);
        }
    };

    if (mode == 2) {
        // By Distance: within each group, fuse only sub-clusters of nodes that
        // are very close to each other (centroid of each cluster).
        const float thr = 8.0f;
        for (Group& g : groups) {
            Renderer::Shape* s = doc.FindShape(g.shape);
            if (!s || g.part >= (int)s->parts.size()) continue;
            std::vector<bool> done(g.nodes.size(), false);
            // Build clusters; fuse each (≥2) at its centroid.
            for (size_t i = 0; i < g.nodes.size(); ++i) {
                if (done[i]) continue;
                Vec2 wi = worldOf({ g.shape, g.part, g.nodes[i] });
                Group sub{ g.shape, g.part, { g.nodes[i] } };
                Vec2 sum = wi; int cnt = 1; done[i] = true;
                for (size_t j = i + 1; j < g.nodes.size(); ++j) {
                    if (done[j]) continue;
                    Vec2 wj = worldOf({ g.shape, g.part, g.nodes[j] });
                    if (std::hypot(wi.x - wj.x, wi.y - wj.y) <= thr) {
                        sub.nodes.push_back(g.nodes[j]); sum.x+=wj.x; sum.y+=wj.y; ++cnt; done[j]=true;
                    }
                }
                if (cnt >= 2) fuseGroup(sub, { sum.x/cnt, sum.y/cnt });
            }
        }
        doc.ClearVertSelection();
        project_.dirty = true;
        return;
    }

    // Single target for center/cursor/first/last.
    Vec2 target{0, 0};
    if (mode == 1)      target = doc.cursor;
    else if (mode == 3) target = worldOf(sel.front());                 // At First
    else if (mode == 4) target = worldOf(sel.back());                  // At Last
    else { for (const VertRef& v : sel) { Vec2 w = worldOf(v); target.x+=w.x; target.y+=w.y; }
           target.x /= (float)sel.size(); target.y /= (float)sel.size(); }  // Center

    // Fuse each group (descending part order not needed; groups are independent).
    for (Group& g : groups) fuseGroup(g, target);
    doc.ClearVertSelection();
    project_.dirty = true;
}

// ── Edit-mode menus (shared UI::ContextMenu, same look as the object menu) ────
// Member so the lambdas can reach the private Action_MergeVertices.
std::vector<UI::MenuEntry> Application::BuildMergeSubmenu() {
    std::vector<UI::MenuEntry> sub;
    { UI::MenuEntry e; e.label = "At Center";    e.tooltip = "Merge selected vertices into one at their average position"; e.onClick = [this]{ Action_MergeVertices(0); }; sub.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "At 2D Cursor"; e.tooltip = "Merge selected vertices into one at the 2D cursor"; e.onClick = [this]{ Action_MergeVertices(1); }; sub.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "At First";     e.tooltip = "Merge into the first selected vertex"; e.onClick = [this]{ Action_MergeVertices(3); }; sub.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "At Last";      e.tooltip = "Merge into the last selected vertex"; e.onClick = [this]{ Action_MergeVertices(4); }; sub.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "By Distance";  e.tooltip = "Collapse vertices that are closer than a threshold"; e.onClick = [this]{ Action_MergeVertices(2); }; sub.push_back(std::move(e)); }
    return sub;
}

void Application::RenderMergeMenu() {
    // Standalone "Merge" menu (M) with the merge targets.
    std::vector<UI::MenuEntry> entries = BuildMergeSubmenu();
    UI::ContextMenu("##mergeMenu", editMenuPos_, entries, "Merge");
}

// Shared handle-type rows (used by the RMB submenu and the V menu).
std::vector<UI::MenuEntry> Application::BuildHandleTypeSubmenu() {
    std::vector<UI::MenuEntry> sub;
    auto add = [&](const char* lbl, const char* tip, Renderer::HandleMode mode){
        UI::MenuEntry e; e.label = lbl; e.tooltip = tip;
        e.onClick = [this, mode]{ Action_SetHandleType(mode); };
        sub.push_back(std::move(e)); };
    add("Free",     "Both handles move independently",                  Renderer::HandleMode::Free);
    add("Aligned",  "Handles stay collinear (lengths independent)",     Renderer::HandleMode::Aligned);
    add("Mirrored", "Handles keep equal length (directions independent)", Renderer::HandleMode::Mirrored);
    add("Aligned + Mirrored", "Handles stay collinear AND equal length", Renderer::HandleMode::AlignedMirrored);
    add("Vector",   "Handles point at the neighbouring points (straight)", Renderer::HandleMode::Vector);
    { UI::MenuEntry e; e.label = "Remove Handles";
      e.tooltip = "Delete both handles — a perfectly straight corner point (undo of adding handles)";
      e.onClick = [this]{ Action_RemoveHandles(); };
      sub.push_back(std::move(e)); }
    return sub;
}

void Application::RenderHandleTypeMenu() {
    // V: directly open the handle-type chooser as its own titled menu.
    std::vector<UI::MenuEntry> entries = BuildHandleTypeSubmenu();
    UI::ContextMenu("##handleMenu", editMenuPos_, entries, "Set Handle Type");
}

void Application::RenderEditContextMenu() {
    auto& sm  = Shortcuts::ShortcutManager::Instance();
    auto& doc = project_.document;
    const bool hasVerts = doc.HasVertSelection();
    std::vector<UI::MenuEntry> entries;
    // Merge ▸
    { UI::MenuEntry e; e.label = "Merge"; e.shortcut = sm.GetShortcutString("edit.merge");
      e.enabled = hasVerts; e.submenu = BuildMergeSubmenu(); entries.push_back(std::move(e)); }
    // Set Spline Type ▸ — changes the selected curve part(s) between Bézier
    // (on-curve anchors + handles), NURBS (off-curve control points), and Poly
    // (straight). Only meaningful for curve-like parts.
    {
        UI::MenuEntry st; st.label = "Set Spline Type";
        st.tooltip = "Change how the selected curve interprets its points";
        auto add = [&](const char* lbl, const char* tip, Renderer::SplineType t) {
            UI::MenuEntry e; e.label = lbl; e.tooltip = tip;
            e.onClick = [this, t]{ Action_SetSplineType(t); };
            st.submenu.push_back(std::move(e));
        };
        add("Bezier", "On-curve points with Bézier handles", Renderer::SplineType::Bezier);
        add("NURBS",  "Off-curve control points, smooth B-spline", Renderer::SplineType::Nurbs);
        add("Poly",   "Straight segments through the points",  Renderer::SplineType::Poly);
        entries.push_back(std::move(st));
    }
    // Set Handle Type ▸ (Bézier only)
    { UI::MenuEntry h; h.label = "Set Handle Type"; h.shortcut = sm.GetShortcutString("edit.handleMenu");
      h.enabled = hasVerts; h.submenu = BuildHandleTypeSubmenu(); entries.push_back(std::move(h)); }
    // Toggle close curve
    { UI::MenuEntry e; e.label = "Toggle Cyclic (close path)";
      e.tooltip = "Open or close the path (join its last point to its first)";
      e.onClick = [this]{ Action_ToggleCloseCurve(); }; entries.push_back(std::move(e)); }
    // Delete
    { UI::MenuEntry e; e.label = "Delete"; e.icon = "ink-eraser";
      e.shortcut = sm.GetShortcutString("edit.deleteSelection");
      e.tooltip = "Delete the selected vertices / edges / faces";
      e.enabled = hasVerts; e.onClick = [this]{ Action_DeleteElements(); };
      entries.push_back(std::move(e)); }

    UI::ContextMenu("##editCtx", editMenuPos_, entries, "Edit");
}

void Application::FoldNewShapeIntoObject(uint64_t hostId) {
    auto& doc = project_.document;
    if (!hostId) return;
    uint64_t newId = doc.ActiveId();
    if (newId == 0 || newId == hostId) return;     // nothing new was created
    Renderer::Shape* host = doc.FindShape(hostId);
    Renderer::Shape* nw   = doc.FindShape(newId);
    if (!host || !nw) return;
    // The just-created shape is only "added into" the host when it was made THIS
    // interaction (its id is the freshly-selected one and it is a distinct
    // object). Rebase its parts into the host's local space, then drop it.
    const Renderer::Vec2 hostPo = CurPageOriginOfShape(hostId);
    const Renderer::Vec2 nwPo   = CurPageOriginOfShape(newId);
    auto rebase = [&](Renderer::Part part) {
        part.EnsurePath();
        auto toHostLocal = [&](Renderer::Vec2 p) {
            return Renderer::Tessellator::InverseTransform(
                *host, Renderer::Tessellator::WorldTransform(*nw, p, nwPo), hostPo);
        };
        for (Renderer::Node& n : part.path.nodes) {
            n.pos = toHostLocal(n.pos);
            if (n.hasIn)  n.hIn  = toHostLocal(n.hIn);
            if (n.hasOut) n.hOut = toHostLocal(n.hOut);
        }
        return part;
    };
    for (const Renderer::Part& part : nw->parts)
        host->parts.push_back(rebase(part));
    doc.EraseShape(newId);
    doc.SelectOnly(hostId);     // keep editing the host (stay in Edit Mode)
    project_.dirty = true;
}

// Convert the selected objects' parts between the two FAMILIES. Mesh straightens
// the path (drops Bézier handles → straight edges, edge/face editing); Curve is
// point-edited (its spline kind — Bézier/NURBS/Poly — is chosen separately via
// Set Spline Type). Converting Mesh → Curve seeds it as a Bézier spline.
void Application::Action_ConvertSelectionTo(Renderer::PartType target) {
    if (editorMode_ != EditorMode::Edit && editorMode_ != EditorMode::Object) return;
    MarkUndoLabel(std::string("Convert to ") + Renderer::PartTypeName(target));
    for (uint64_t sid : project_.document.Selection()) {
        Renderer::Shape* s = project_.document.FindShape(sid);
        if (!s) continue;
        s->EnsurePath();
        for (Renderer::Part& part : s->parts) {
            part.type = target;
            if (target == Renderer::PartType::Mesh) {
                // → Mesh: straighten (drop Bézier handles).
                for (Renderer::Node& n : part.path.nodes) {
                    n.hasIn = n.hasOut = false; n.hIn = n.hOut = n.pos;
                    n.mode = Renderer::HandleMode::Vector;
                }
            } else {
                part.spline = Renderer::SplineType::Bezier;  // Curve defaults to Bézier
            }
        }
    }
    project_.dirty = true;
}

// Set the spline kind (Bézier / NURBS / Poly) of the curve part(s) holding the
// selected points — or every curve part of the selection if no point is picked.
// Converts the Node[] sensibly between representations so the picture stays
// recognisable: NURBS/Poly drop the Bézier handles (their points become control
// points / straight anchors); switching back to Bézier re-derives smooth handles.
void Application::Action_SetSplineType(Renderer::SplineType target) {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    MarkUndoLabel(std::string("Set spline ") + Renderer::SplineTypeName(target));

    // Which parts to retype: those with a selected point, else all curve parts.
    auto hasSelectedPoint = [&](uint64_t sid, int pi) {
        for (const Renderer::VertRef& v : doc.VertSelection())
            if (v.shape == sid && v.part == pi) return true;
        return false;
    };
    bool anyPointSel = doc.HasVertSelection();

    for (uint64_t sid : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(sid);
        if (!s) continue;
        s->EnsurePath();
        for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
            Renderer::Part& part = s->parts[(size_t)pi];
            if (!part.IsCurveLike()) continue;          // meshes are unaffected
            if (anyPointSel && !hasSelectedPoint(sid, pi)) continue;

            part.type = Renderer::PartType::Curve;
            part.spline = target;
            if (target == Renderer::SplineType::Bezier) {
                // Re-derive smooth aligned handles from neighbours so the former
                // control points/anchors become a sensible Bézier curve.
                const int n = (int)part.path.nodes.size();
                for (int i = 0; i < n; ++i) {
                    Renderer::Node& nd = part.path.nodes[(size_t)i];
                    int ip = (i - 1 + n) % n, in = (i + 1) % n;
                    bool openEnds = !part.path.closed && (i == 0 || i == n - 1);
                    Renderer::Vec2 prev = part.path.nodes[(size_t)ip].pos;
                    Renderer::Vec2 next = part.path.nodes[(size_t)in].pos;
                    Renderer::Vec2 dir{ (next.x - prev.x) * 0.25f,
                                        (next.y - prev.y) * 0.25f };
                    nd.mode = Renderer::HandleMode::Aligned;
                    nd.hasIn = nd.hasOut = !openEnds || (i != 0 && i != n - 1);
                    nd.hIn  = { nd.pos.x - dir.x, nd.pos.y - dir.y };
                    nd.hOut = { nd.pos.x + dir.x, nd.pos.y + dir.y };
                }
            } else {
                // NURBS / Poly: points are control points / straight anchors —
                // no Bézier handles.
                for (Renderer::Node& nd : part.path.nodes) {
                    nd.hasIn = nd.hasOut = false; nd.hIn = nd.hOut = nd.pos;
                    nd.mode = Renderer::HandleMode::Vector;
                }
                if (target == Renderer::SplineType::Nurbs && part.orderU < 2)
                    part.orderU = 3;
            }
        }
    }
    project_.dirty = true;
}

// Convert the whole selection to one type, then Join — the one-click resolution
// the context menu offers when a mixed-family selection can't be joined as-is.
// Object mode only (Join is). One undo step labelled "Convert & Join".
void Application::Action_ConvertAllAndJoin(Renderer::PartType target) {
    if (editorMode_ != EditorMode::Object) return;
    if (project_.document.Selection().size() < 2) return;
    const std::string label =
        std::string("Convert to ") + Renderer::PartTypeName(target) + " & Join";
    // Re-type without its own undo label (we set ours below); same logic.
    for (uint64_t sid : project_.document.Selection()) {
        Renderer::Shape* s = project_.document.FindShape(sid);
        if (!s) continue;
        s->EnsurePath();
        for (Renderer::Part& part : s->parts) {
            part.type = target;
            if (target == Renderer::PartType::Mesh)
                for (Renderer::Node& n : part.path.nodes) {
                    n.hasIn = n.hasOut = false; n.hIn = n.hOut = n.pos;
                    n.mode = Renderer::HandleMode::Vector;
                }
        }
    }
    Action_JoinSelection();   // selection is now single-family → succeeds
    // Join set its own "Join" label; override so this whole op reads as one
    // logical step in the undo history (commit happens end-of-frame).
    MarkUndoLabel(label);
    project_.dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Extrude — add a new connected point from the active vertex. The behaviour is
//  DIRECTION-AWARE (the mouse direction relative to the vertex picks which side)
//  and CYCLIC (pressing E again on the same vertex flips the mode):
//
//   • Endpoint of a strand:
//       cycle 0 → extrude OUTWARD: a new endpoint beyond the free end.
//       cycle 1 → insert INTO the existing (only) segment.
//     The mouse direction seeds cycle 0/1: pointing away from the neighbour =
//     outward (0); toward it = insert (1). E then toggles.
//   • Interior vertex:
//       cycle 0 → INSERT a point into the segment the mouse points toward
//                 (prev or next, by direction not proximity).
//       cycle 1 → BRANCH: start a NEW subpath (strand) from this vertex toward
//                 the mouse (true 3-way junction via multi-subpath).
//     E toggles insert ↔ branch.
//
//  The new point starts ON the active vertex (a following modal Move / drag
//  positions it). Shared by the E shortcut and the Extrude tool.
// ─────────────────────────────────────────────────────────────────────────────
bool Application::ExtrudeFromActiveVertex(Renderer::VertRef& outNew) {
    auto& doc = project_.document;
    const VertRef v = doc.ActiveVert();
    Renderer::Shape* s = doc.FindShape(v.shape);
    if (!s || v.part < 0 || v.part >= (int)s->parts.size()) return false;
    Renderer::Part& part = s->parts[(size_t)v.part];
    part.EnsurePath();
    auto& nodes = part.path.nodes;
    if (v.node < 0 || v.node >= (int)nodes.size()) return false;

    // Cycle bookkeeping: a new active vertex resets the cycle; same vertex +E
    // advances it. (Set up by Action_ExtrudeActiveVertex before calling us.)
    const int cycle = extrudeCycle_;

    // This vertex's subpath range and its role within it.
    int sb = 0, se = (int)nodes.size();
    int sub = part.path.subOf(v.node);
    part.path.subRange(sub, sb, se);
    const bool isStart = (v.node == sb);
    const bool isEnd   = (v.node == se - 1);
    const bool isEndpoint = (!part.path.closed) && (isStart || isEnd) && (se - sb >= 1);

    // Mouse direction relative to the vertex (doc-space). Fallbacks keep it sane
    // when the hovered position is stale.
    const Vec2 po = CurPageOriginOfShape(v.shape);
    const Vec2 vw = NodeWorld(*s, nodes[(size_t)v.node], po);
    Vec2 dirMouse{ 0, 0 };
    if (lastHoverValid_) dirMouse = { lastHoverDoc_.x - vw.x, lastHoverDoc_.y - vw.y };
    auto norm = [](Vec2 d){ float l = std::hypot(d.x, d.y); return l > 1e-5f ? Vec2{d.x/l, d.y/l} : Vec2{0,0}; };
    dirMouse = norm(dirMouse);
    auto dot = [](Vec2 a, Vec2 b){ return a.x*b.x + a.y*b.y; };

    // The new point starts ON the active vertex AND inherits its handle type,
    // size and orientation (mode + in/out handles). Since it shares the source's
    // position, copying the node keeps the absolute handle positions identical
    // (i.e. same relative offsets), so the extruded point continues the curve with
    // matching curvature — the modal Move then drags it (carrying its handles).
    Node nn = nodes[(size_t)v.node];

    int insertAt = -1;

    if (isEndpoint) {
        // The single existing neighbour and the direction toward it.
        int nb = isStart ? (v.node + 1) : (v.node - 1);
        Vec2 dirNb{ 0, 0 };
        if (nb >= sb && nb < se) {
            Vec2 nbw = NodeWorld(*s, nodes[(size_t)nb], po);
            dirNb = norm({ nbw.x - vw.x, nbw.y - vw.y });
        }
        // Seed: outward if the mouse points away from the neighbour; else insert.
        const bool mouseOutward = dot(dirMouse, dirNb) <= 0.0f;
        const bool outward = (cycle % 2 == 0) ? mouseOutward : !mouseOutward;
        if (outward || nb < sb || nb >= se) {
            insertAt = isStart ? sb : se;          // beyond the free end (new endpoint)
        } else {
            insertAt = isStart ? sb + 1 : se - 1;  // into the one existing segment
        }
    } else if (cycle % 2 == 1) {
        // BRANCH: append a new strand that SHARES this vertex (a real junction). The
        // branch's first node carries the SAME position and a common junctionId as the
        // source node, so edit mode shows ONE vertex with all the branches' handles
        // (≥3) and the coincident nodes move together. The new subpath =
        // [junctionStart, newPoint]; we return the new (2nd) point.
        uint32_t jid = nodes[(size_t)v.node].junctionId;
        if (jid == 0) {
            uint32_t mx = 0; for (const Node& nn2 : nodes) mx = std::max(mx, nn2.junctionId);
            jid = mx + 1; nodes[(size_t)v.node].junctionId = jid;
        }
        Node junction = nodes[(size_t)v.node];     // duplicate at the junction pos
        junction.hasIn = junction.hasOut = false; junction.mode = HandleMode::Vector;
        junction.junctionId = jid;                  // weld to the same junction group
        int branchStart = (int)nodes.size();
        nodes.push_back(junction);
        nodes.push_back(nn);                        // the movable new endpoint
        part.path.SplitAt(branchStart);             // mark the new subpath boundary
        outNew = VertRef{ v.shape, v.part, branchStart + 1 };
        doc.VertSelectOnly(outNew);
        return true;
    } else {
        // INSERT into the prev or next segment, chosen by the MOUSE DIRECTION.
        int prev = v.node - 1, next = v.node + 1;
        Vec2 dPrev{0,0}, dNext{0,0};
        bool hasPrev = (prev >= sb), hasNext = (next < se);
        if (hasPrev) { Vec2 w = NodeWorld(*s, nodes[(size_t)prev], po); dPrev = norm({ w.x-vw.x, w.y-vw.y }); }
        if (hasNext) { Vec2 w = NodeWorld(*s, nodes[(size_t)next], po); dNext = norm({ w.x-vw.x, w.y-vw.y }); }
        bool chooseNext;
        if (hasPrev && hasNext) chooseNext = dot(dirMouse, dNext) >= dot(dirMouse, dPrev);
        else                    chooseNext = hasNext;
        insertAt = chooseNext ? next : v.node;     // midpoint of [v,next] or [prev,v]
    }

    if (insertAt < 0) return false;
    // End-outward appends past this subpath's last node (insertAt == se): the new
    // node must join THIS subpath, so the NEXT subpath's boundary (== se) moves up.
    const bool endOutward = isEndpoint && isEnd && (insertAt == se);
    nodes.insert(nodes.begin() + insertAt, nn);
    if (endOutward) part.path.OnNodeInsertedInclusive(insertAt);
    else            part.path.OnNodeInserted(insertAt);

    outNew = VertRef{ v.shape, v.part, insertAt };
    doc.VertSelectOnly(outNew);                     // select+activate the new point
    return true;
}

// Remove the node(s) created by the LAST extrude (used by cyclic E to revert
// before re-applying the next mode). A branch added two nodes (junction + point).
void Application::RevertLastExtrude() {
    auto& doc = project_.document;
    const VertRef cur = extrudeCreatedVert_;
    Renderer::Shape* s = doc.FindShape(cur.shape);
    if (!s || cur.part < 0 || cur.part >= (int)s->parts.size()) return;
    Renderer::Part& part = s->parts[(size_t)cur.part];
    if (cur.node < 0 || cur.node >= (int)part.path.nodes.size()) return;
    if (extrudeWasBranch_ && cur.node >= 1) {
        part.path.nodes.erase(part.path.nodes.begin() + cur.node);
        part.path.OnNodeErased(cur.node);
        part.path.nodes.erase(part.path.nodes.begin() + (cur.node - 1));
        part.path.OnNodeErased(cur.node - 1);
    } else {
        part.path.nodes.erase(part.path.nodes.begin() + cur.node);
        part.path.OnNodeErased(cur.node);
    }
}

// Perform one extrude from the active vertex with the current cycle, recording
// the bookkeeping the cyclic path + revert need. Returns the new vertex (or an
// invalid one). `freshChain` resets the cycle (a brand-new E press).
bool Application::DoExtrudeStep(bool freshChain, Renderer::VertRef& outNew) {
    auto& doc = project_.document;
    if (freshChain) { extrudeCycle_ = 0; extrudeSourceVert_ = doc.ActiveVert(); }
    if (!ExtrudeFromActiveVertex(outNew)) return false;
    extrudeWasBranch_ = false;
    if (Renderer::Shape* s = doc.FindShape(outNew.shape); s && outNew.part < (int)s->parts.size())
        extrudeWasBranch_ = (s->parts[(size_t)outNew.part].path.subOf(outNew.node) !=
                             s->parts[(size_t)outNew.part].path.subOf(extrudeSourceVert_.node));
    extrudeCreatedVert_ = outNew;
    extrudeJustCreated_ = true;
    return true;
}

void Application::Action_ExtrudeActiveVertex() {
    if (editorMode_ != EditorMode::Edit) return;
    auto& doc = project_.document;
    // If a previous extrude's modal Move is STILL active, pressing E cycles the
    // mode IN PLACE: cancel that move (reverting the point to its source), drop the
    // created node, bump the cycle and re-extrude — then a fresh Move follows the
    // mouse again. This is what makes E cyclic while the point tracks the cursor.
    bool cycling = false;
    if (transformOp_.Active() && transformOp_.element && extrudeJustCreated_ &&
        transformOp_.kind == TransformKind::Move) {
        transformOp_.Reset();              // stop the move WITHOUT committing it
        RevertLastExtrude();
        ++extrudeCycle_;
        doc.VertSelectOnly(extrudeSourceVert_);
        cycling = true;
    }
    Renderer::VertRef nv;
    if (!DoExtrudeStep(/*freshChain=*/!cycling, nv)) return;
    MarkUndoLabel("Extrude");
    project_.dirty = true;
    // The new point FOLLOWS THE MOUSE; a click drops it, click-drag tunes handles
    // (the modal Move handles that). Pressing E again during this move cycles.
    Action_BeginTransform(TransformKind::Move);
}

void Application::HandleExtrudeTool(EditorState& st,
                                    const std::function<Vec2(ImVec2)>& s2d,
                                    const std::function<ImVec2(Vec2)>& d2s,
                                    float effZoom, bool hovered, ImDrawList* dl) {
    (void)effZoom;
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;
    ImGuiIO& io = ImGui::GetIO();
    const void* self = &st;

    // A drag armed by a previous grab is driven by HandleEditMode (routing), so
    // here we only draw the ring and start a new extrude on a grab.
    const VertRef av = doc.ActiveVert();
    Renderer::Shape* s = doc.FindShape(av.shape);
    if (!s || av.part < 0 || av.part >= (int)s->parts.size()) { ShowCrosshairCursor(); return; }
    Renderer::Part& part = s->parts[(size_t)av.part];
    if (av.node < 0 || av.node >= (int)part.path.nodes.size()) { ShowCrosshairCursor(); return; }

    const Vec2 w = NodeWorld(*s, part.path.nodes[(size_t)av.node], CurPageOriginOfShape(av.shape));
    const ImVec2 c = d2s(w);
    const float ring = 16.0f * ds.GetGlobalScale();
    ImU32 col = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    dl->AddCircle(c, ring, col, 28, 1.5f);
    dl->AddCircleFilled(c, 2.5f, col);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= ring) {
        VertRef nv;
        extrudeCycle_ = 0;                 // tool extrude = direction-based, drag to place
        extrudeJustCreated_ = false;
        if (ExtrudeFromActiveVertex(nv)) {
            MarkUndoLabel("Extrude");
            Renderer::Shape* ns = doc.FindShape(nv.shape);
            // Arm a vertex drag that follows the mouse and drops on RELEASE.
            editDrag_.Reset();
            editDrag_.kind  = EditDragState::Kind::Verts;
            editDrag_.owner = self;
            Vec2 m = s2d(io.MousePos);
            editDrag_.dragStart = m;
            editDrag_.moveAccum = m;
            editDrag_.ids = { nv };
            editDrag_.snapshot = { ns->parts[(size_t)nv.part].path.nodes[(size_t)nv.node] };
            BeginGestureMouseTracking();
            project_.dirty = true;
        }
    }
    ShowCrosshairCursor();
}

// Edit-mode Select More / Less (Shift+Numpad ±). More adds the path-adjacent nodes
// (index ±1 within the same part) of every selected vertex; Less drops vertices
// that have an un-selected path neighbour (boundary). Operates on the current vert
// selection across all edited objects.
void Application::Action_SelectMoreLessElements(bool grow) {
    auto& doc = project_.document;
    const auto sel = doc.VertSelection();      // copy (we mutate the selection)
    if (sel.empty()) return;
    auto selected = [&](const Renderer::VertRef& v){ return doc.IsVertSelected(v); };
    // Path-adjacent neighbours of node `v` within its subpath. For a CLOSED path the
    // ends wrap (last↔first), so the "next" of the final point is the first one — the
    // other side of the loop, geometrically connected. Returns up to 2 refs.
    auto neighbours = [&](const Renderer::VertRef& v, std::vector<Renderer::VertRef>& out) {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) return;
        const Renderer::Path& path = s->parts[(size_t)v.part].path;
        int b = 0, e = (int)path.nodes.size();
        path.subRange(path.subOf(v.node), b, e);
        if (e - b < 2) return;                  // a 1-node subpath has no neighbour
        const bool closed = path.closed;
        int prev = (v.node > b) ? v.node - 1 : (closed ? e - 1 : -1);
        int next = (v.node < e - 1) ? v.node + 1 : (closed ? b : -1);
        if (prev >= 0) out.push_back({ v.shape, v.part, prev });
        if (next >= 0) out.push_back({ v.shape, v.part, next });
    };
    if (grow) {
        std::vector<Renderer::VertRef> add;
        for (const Renderer::VertRef& v : sel) neighbours(v, add);
        for (const Renderer::VertRef& v : add) doc.VertSelectAdd(v);
    } else {
        // Drop boundary vertices (those with an un-selected neighbour), but NEVER
        // below one remaining vertex (there must always be ≥1 selected element).
        std::vector<Renderer::VertRef> remove;
        for (const Renderer::VertRef& v : sel) {
            std::vector<Renderer::VertRef> nb; neighbours(v, nb);
            bool boundary = false;
            for (const Renderer::VertRef& n : nb) if (!selected(n)) { boundary = true; break; }
            if (boundary) remove.push_back(v);
        }
        // Keep at least one: if removing all of them would empty the selection, keep
        // the active vertex (or the first) as the lone survivor.
        if ((int)remove.size() >= (int)sel.size()) {
            Renderer::VertRef keep = doc.ActiveVert();
            bool keepValid = false;
            for (const Renderer::VertRef& v : sel) if (v == keep) { keepValid = true; break; }
            if (!keepValid) keep = sel.front();
            remove.erase(std::remove(remove.begin(), remove.end(), keep), remove.end());
        }
        for (const Renderer::VertRef& v : remove) doc.VertDeselect(v);
    }
}

} // namespace App

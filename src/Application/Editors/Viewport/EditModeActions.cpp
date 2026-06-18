#include "Application.h"
#include "EditModeShared.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace App {

using Renderer::Vec2;
using Renderer::VertRef;
using Renderer::Node;
using Renderer::HandleMode;
using Renderer::SelectElementMode;

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

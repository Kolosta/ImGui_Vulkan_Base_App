#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip (shared styled tooltip)
#include <UI/Widgets/Dropdown.h>      // UI::Dropdown (operator panel params)
#include <UI/Widgets/ButtonGroup.h>   // UI::ButtonGroup (snap base/affect)
#include <UI/Widgets/Checkbox.h>      // UI::Checkbox (operator panel bool params)
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace App {

void Application::DrawMetricsOverlay(ImVec2 canvasMin, ImVec2 canvasMax) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    if (!renderer_) return;
    const Renderer::IViewRenderer::Metrics& m = renderer_->GetMetrics();
    ImGuiIO& io = ImGui::GetIO();
    const float fps    = io.Framerate;                       // ImGui's smoothed FPS
    const float frameMs = (fps > 0.0f) ? 1000.0f / fps : 0.0f;

    char l0[64], l1[80], l2[96], l3[96], l4[96];
    std::snprintf(l0, sizeof l0, "%.0f FPS   %.2f ms", fps, frameMs);
    std::snprintf(l1, sizeof l1, "tris %d   draws %d", m.triangles, m.drawCalls);
    // `dirty` (Lot 13-1a): shapes that actually changed since last build — 0 on a
    // static scene even when a full rebuild fires (exposes over-rebuild).
    std::snprintf(l2, sizeof l2, "obj %d  cache %d  built %d  cull %d  dirty %d",
                  m.shapesDrawn, m.shapesCached, m.shapesBuilt, m.shapesCulled, m.shapesDirty);
    // Lot 13-0 breakdown: where the frame time goes (CPU stages + GPU render).
    std::snprintf(l3, sizeof l3, "sig %.2f  tess %.2f  up %.2f  rec %.2f ms",
                  m.sigMs, m.tessMs, m.uploadMs, m.recordMs);
    std::snprintf(l4, sizeof l4, "gpu %.2f ms   gpu-wait %.2f ms", m.gpuMs, m.gpuWaitMs);

    const char* lines[5] = { l0, l1, l2, l3, l4 };
    // Metrics HUD is text-heavy → keep it on ImGui for now (migrated with text in 12-4).
    App::OverlayDL dl(ImGui::GetWindowDrawList(), &overlay_, /*gpu=*/false);
    const float pad = 6.0f, lh = ImGui::GetTextLineHeight();
    float wmax = 0.0f;
    for (const char* s : lines) wmax = std::max(wmax, ImGui::CalcTextSize(s).x);
    // Anchor BOTTOM-LEFT of the canvas.
    const int kLines = 5;
    const float boxH = lh * (float)kLines + pad * 2.0f;
    ImVec2 p0(canvasMin.x + 8.0f, canvasMax.y - 8.0f - boxH);
    ImVec2 p1(p0.x + wmax + pad * 2.0f, p0.y + boxH);
    dl.AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 150), 4.0f);
    // FPS line tinted by health (green ≥55, amber ≥30, red below); rest neutral.
    ImU32 fpsCol = fps >= 55.0f ? IM_COL32(120, 230, 120, 255)
                 : fps >= 30.0f ? IM_COL32(235, 200, 110, 255)
                                : IM_COL32(235, 120, 120, 255);
    ImU32 txtCol = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    for (int i = 0; i < kLines; ++i)
        dl.AddText(ImVec2(p0.x + pad, p0.y + pad + lh * (float)i),
                    i == 0 ? fpsCol : txtCol, lines[i]);
}

// ── Snap-candidate preview (violet, while Ctrl/snap is active) ────────────────
// Shows every POSSIBLE snap point for the current mode in violet so the user sees
// where they can snap before getting there. Hidden once a snap engages (only the
// orange glyph remains). Excludes the moving selection. Edge mode shows nothing.
void Application::DrawSnapCandidates(
        const std::function<ImVec2(Renderer::Vec2)>& d2sDoc,
        float effZoom, ImVec2 canvasMin, ImVec2 canvasMax, App::OverlayDL& dl) {
    using DesignSystem::Tok;
    // Only during a transform whose kind snapping affects, and only while snap is on.
    if (!transformOp_.Active() || !SnapActiveFor(transformOp_.kind)) return;
    // Edge mode: nothing to preview (you can snap anywhere along an edge).
    if (snap_.mode == SnapSettings::Mode::Edge ||
        snap_.mode == SnapSettings::Mode::Increment) return;

    auto& ds  = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    ImU32 vio = ImGui::GetColorU32(ds.GetColor(Tok::S_State_Selected_Loose));   // violet
    auto& doc = project_.document;
    const float zoom = std::max(1e-4f, effZoom);

    auto onScreen = [&](ImVec2 s){
        return s.x >= canvasMin.x && s.x <= canvasMax.x &&
               s.y >= canvasMin.y && s.y <= canvasMax.y;
    };

    // ── Grid: a small violet dot at every grid intersection in view ──────────
    // Grid ALWAYS snaps (the grid is everywhere), so it doesn't honour the
    // "hide all when a snap engages" rule — the intersections stay visible the
    // whole time Ctrl is held, so the user can see where things will land.
    if (snap_.mode == SnapSettings::Mode::Grid) {
        const float g = SnapGridStep(effZoom);
        if (g < 1e-6f) return;
        // Invert the affine d2sDoc (screen = o + (sx·docx, sy·docy)) from 3 probes to
        // get the visible doc range of the canvas, then step the grid over it.
        ImVec2 o  = d2sDoc({0,0});
        ImVec2 ex = d2sDoc({1,0}); ImVec2 ey = d2sDoc({0,1});
        float sx = ex.x - o.x, sy = ey.y - o.y;
        if (std::fabs(sx) < 1e-6f || std::fabs(sy) < 1e-6f) return;
        auto sx2dx = [&](float px){ return (px - o.x) / sx; };
        auto sy2dy = [&](float py){ return (py - o.y) / sy; };
        float dx0 = std::min(sx2dx(canvasMin.x), sx2dx(canvasMax.x));
        float dx1 = std::max(sx2dx(canvasMin.x), sx2dx(canvasMax.x));
        float dy0 = std::min(sy2dy(canvasMin.y), sy2dy(canvasMax.y));
        float dy1 = std::max(sy2dy(canvasMin.y), sy2dy(canvasMax.y));
        float x0 = std::floor(dx0 / g) * g, y0 = std::floor(dy0 / g) * g;
        const float dotR = 1.5f * gs;
        int guard = 0;
        for (float x = x0; x <= dx1 && guard < 20000; x += g)
            for (float y = y0; y <= dy1 && guard < 20000; y += g, ++guard) {
                ImVec2 s = d2sDoc({ x, y });
                if (onScreen(s)) dl.AddCircleFilled(s, dotR, vio);
            }
        return;
    }

    // ── Geometry modes (Vertex / EdgeCenter / Face) ──────────────────────────
    // A snap is currently engaged → hide ALL geometry previews (clear view, only
    // the orange glyph remains).
    if (snapIndicator_.snapped) return;

    // PRE-MOVE positions of the moving selection: used both to reject the moving
    // geometry from the candidates AND to compute the moving shape's own Face
    // centroid from its pre-move outline (so the candidate square does NOT drift
    // with the live selection). Keyed by (shape,part,node) → snapshot Node.
    std::vector<Renderer::Vec2> reject;     // pre-move world positions of moving nodes
    struct PreNode { uint64_t shape; int part, node; Renderer::Node n; };
    std::vector<PreNode> preNodes;
    for (size_t i = 0; i < transformOp_.vrefs.size() && i < transformOp_.vsnap.size(); ++i) {
        const Renderer::VertRef& v = transformOp_.vrefs[i];
        const Renderer::Node&    n = transformOp_.vsnap[i];
        preNodes.push_back({ v.shape, v.part, v.node, n });
        if (Renderer::Shape* s = doc.FindShape(v.shape))
            reject.push_back(Renderer::Tessellator::WorldTransform(
                *s, n.pos, CurPageOriginOfShape(v.shape)));
    }
    const float kRejDoc = 1.0f / zoom;
    auto rejected = [&](Renderer::Vec2 p){
        for (const Renderer::Vec2& rp : reject)
            if (std::hypot(p.x - rp.x, p.y - rp.y) < kRejDoc) return true;
        return false;
    };
    // Return a copy of `part` with any moving nodes restored to their PRE-MOVE
    // snapshot (so an edited shape's own outline/centroid uses pre-move geometry).
    auto preMovePart = [&](uint64_t shapeId, int partIdx,
                           const Renderer::Part& part) -> Renderer::Part {
        Renderer::Part baked = part; baked.EnsurePath();
        for (const PreNode& pn : preNodes) {
            if (pn.shape != shapeId || pn.part != partIdx) continue;
            if (pn.node >= 0 && pn.node < (int)baked.path.nodes.size())
                baked.path.nodes[(size_t)pn.node] = pn.n;
        }
        return baked;
    };

    // ── Geometry modes: scan visible shapes (skip the moving objects) ────────
    const float r = 6.0f * gs;     // candidate glyph radius (smaller than the snap glyph)
    auto scanShape = [&](const Renderer::Shape& s) {
        if (!s.visible) return;
        // Skip the moving OBJECTS (object mode) — they're the selection.
        if (!transformOp_.element &&
            std::find(transformOp_.ids.begin(), transformOp_.ids.end(), s.id)
                != transformOp_.ids.end()) return;
        Renderer::Vec2 po = CurPageOriginOfShape(s.id);
        for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
            const Renderer::Part& part = s.parts[(size_t)pi];
            if (snap_.mode == SnapSettings::Mode::Vertex) {
                Renderer::Part baked = preMovePart(s.id, pi, part);
                for (const Renderer::Node& nd : baked.path.nodes) {
                    Renderer::Vec2 w = Renderer::Tessellator::WorldTransform(s, nd.pos, po);
                    if (rejected(w)) continue;
                    ImVec2 sp = d2sDoc(w);
                    if (onScreen(sp)) { dl.AddCircleFilled(sp, r * 0.45f, vio);
                                        dl.AddCircle(sp, r * 0.7f, vio, 12, 1.2f); }
                }
            } else if (snap_.mode == SnapSettings::Mode::EdgeCenter) {
                Renderer::Part baked = preMovePart(s.id, pi, part);
                const int sc = std::max(1, baked.path.subCount());
                for (int spi = 0; spi < sc; ++spi) {
                    int b0 = 0, e0 = (int)baked.path.nodes.size();
                    baked.path.subRange(spi, b0, e0);
                    const bool cyc = baked.path.closed;
                    int segCount = cyc ? (e0 - b0) : (e0 - b0 - 1);
                    for (int k = 0; k < segCount; ++k) {
                        int ia = b0 + k, ib = b0 + ((k + 1) % (e0 - b0));
                        const Renderer::Node& na = baked.path.nodes[(size_t)ia];
                        const Renderer::Node& nb = baked.path.nodes[(size_t)ib];
                        std::vector<Renderer::Vec2> pts{ na.pos };
                        Renderer::Tessellator::FlattenCubic(
                            na.pos, na.hasOut ? na.hOut : na.pos,
                            nb.hasIn ? nb.hIn : nb.pos, nb.pos, 24, pts);
                        float total = 0.0f;
                        for (size_t j = 1; j < pts.size(); ++j)
                            total += std::hypot(pts[j].x-pts[j-1].x, pts[j].y-pts[j-1].y);
                        float half = total*0.5f, acc = 0.0f; Renderer::Vec2 mid = na.pos;
                        for (size_t j = 1; j < pts.size(); ++j) {
                            float l = std::hypot(pts[j].x-pts[j-1].x, pts[j].y-pts[j-1].y);
                            if (acc+l >= half) { float u = l>1e-6f?(half-acc)/l:0.0f;
                                mid = { pts[j-1].x+(pts[j].x-pts[j-1].x)*u,
                                        pts[j-1].y+(pts[j].y-pts[j-1].y)*u }; break; }
                            acc += l;
                        }
                        Renderer::Vec2 w = Renderer::Tessellator::WorldTransform(s, mid, po);
                        if (rejected(w)) continue;
                        ImVec2 cc = d2sDoc(w);
                        if (!onScreen(cc)) continue;
                        const float tr = r * 0.55f;     // smaller than the snap triangle
                        dl.AddTriangle(ImVec2(cc.x, cc.y - tr), ImVec2(cc.x - tr, cc.y + tr),
                                        ImVec2(cc.x + tr, cc.y + tr), vio, 1.4f);
                    }
                }
            } else if (snap_.mode == SnapSettings::Mode::Face) {
                bool closed = false;
                Renderer::Part baked = preMovePart(s.id, pi, part);
                std::vector<Renderer::Vec2> poly =
                    Renderer::Tessellator::OutlinePartSub(s, baked, 0, zoom, closed, po);
                if (closed && poly.size() >= 3) {
                    Renderer::Vec2 cn{0,0};
                    for (const Renderer::Vec2& v : poly) { cn.x+=v.x; cn.y+=v.y; }
                    cn.x /= (float)poly.size(); cn.y /= (float)poly.size();
                    if (rejected(cn)) return;
                    ImVec2 cc = d2sDoc(cn);
                    if (onScreen(cc)) {
                        const float sr = r * 0.5f;     // small violet square
                        dl.AddRect(ImVec2(cc.x-sr, cc.y-sr), ImVec2(cc.x+sr, cc.y+sr), vio, 0, 0, 1.4f);
                    }
                }
            }
        }
    };
    for (const Renderer::Artboard& ab : doc.artboards)
        for (const Renderer::Shape& s : ab.shapes) scanShape(s);
    for (const Renderer::Shape& s : doc.looseShapes) scanShape(s);
}

// Draw the published snap indicator glyph at snapIndicator_.pos. Its SHAPE encodes
// the snap mode (circle=vertex, triangle=edge-center, diamond=edge, square=grid/
// face, small dot=other). `accentColor` overrides the default orange cue — the
// curve tool passes blue while following a curve under Shift. No-op if no mark.
void Application::DrawSnapIndicatorGlyph(
        const std::function<ImVec2(Renderer::Vec2)>& d2s, App::OverlayDL& dl,
        float gs, ImU32 accentColor) {
    if (!snapIndicator_.snapped || !snapIndicator_.showMark) return;
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec2 c = d2s(snapIndicator_.pos);
    const float r = 8.0f * gs, th = 2.0f * gs;
    ImU32 sc = accentColor ? accentColor
             : ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_State_Active_OnPage));
    switch (snap_.mode) {
        case SnapSettings::Mode::Vertex:
            dl.AddCircle(c, r, sc, 16, th); break;
        case SnapSettings::Mode::EdgeCenter: {
            ImVec2 p0(c.x, c.y - r), p1(c.x - r, c.y + r), p2(c.x + r, c.y + r);
            dl.AddTriangle(p0, p1, p2, sc, th); break;
        }
        case SnapSettings::Mode::Edge: {   // diamond
            ImVec2 p0(c.x, c.y - r), p1(c.x + r, c.y),
                   p2(c.x, c.y + r), p3(c.x - r, c.y);
            dl.AddQuad(p0, p1, p2, p3, sc, th); break;
        }
        case SnapSettings::Mode::Grid:
        case SnapSettings::Mode::Face:   // square, like Grid
            dl.AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), sc, 0, 0, th);
            break;
        default:
            dl.AddCircleFilled(c, r * 0.6f, sc); break;
    }
}

// ── Operator redo panel (Blender's bottom-left F6 box) ───────────────────────
// A collapsible, fully token-styled panel in the canvas BOTTOM-LEFT showing the
// last operator's title + its adjustable parameters. Collapsed by default; once
// expanded it stays expanded for later operations (operatorPanelExpanded_). Editing
// a param re-runs the operator (guarded by operatorRerunning_ so the re-run's own
// commit doesn't dismiss the panel). Any other action dismisses it (MarkUndoLabel).
void Application::DrawOperatorPanel(ImVec2 canvasMin, ImVec2 canvasMax, EditorState& st) {
    if (!lastOperator_.active) return;
    using DesignSystem::Tok;
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    // Chrome from the SMALL-element tokens (popup/dropdown), so the panel matches
    // inputs/popups — not the larger window radius/border.
    const ImVec2 padV   = ds.GetVec2(Tok::C_Window_Padding);
    const float  radius = ds.GetFloat(Tok::C_Popup_CornerRadius) * gs;
    const float  border = ds.GetFloat(Tok::C_Dropdown_BorderWidth);
    const float  itemSp = ds.GetFloat(Tok::P_Spacing_100) * gs;
    const float  chev   = ds.GetFloat(Tok::C_Dropdown_ChevronSize) * gs;
    const ImVec4 bg     = ds.GetColor(Tok::S_Color_Background_Layer1);
    const ImVec4 borderC= ds.GetColor(Tok::C_Dropdown_Border);
    const float  rowH   = ImGui::GetFrameHeightWithSpacing();

    const bool expanded = operatorPanelExpanded_;
    // ── Auto-size to content ──────────────────────────────────────────────────
    // Width = max(title row, each param row). A Float param row = label + a fixed
    // input field; an Enum row = label + dropdown trigger; a Bool row = checkbox +
    // label. Measure so nothing overflows.
    const float kInputW = 70.0f * gs;            // numeric field width
    const float kEnumW  = 90.0f * gs;            // enum trigger width
    auto textW = [](const char* s){ return ImGui::CalcTextSize(s).x; };
    float contentW = chev + itemSp + textW(lastOperator_.title.c_str());
    if (expanded) {
        for (const OperatorParam& p : lastOperator_.params) {
            float rw = textW(p.label.c_str()) + itemSp;
            if      (p.kind == OperatorParam::Kind::Float) rw += kInputW;
            else if (p.kind == OperatorParam::Kind::Enum)  rw += kEnumW;
            else /* Bool */                                rw += ImGui::GetFrameHeight();
            contentW = std::max(contentW, rw);
        }
    }
    const float w = contentW + padV.x * 2.0f;
    int paramRows = expanded ? (int)lastOperator_.params.size() : 0;
    const float h = rowH * (float)(1 + paramRows) + padV.y * 2.0f
                  + (paramRows > 0 ? itemSp * (float)paramRows : 0.0f);

    // Anchor bottom-left; sit ABOVE the metrics overlay when it's visible.
    const float metricsH = showMetrics_ ? (ImGui::GetTextLineHeight() * 4.0f + 16.0f * gs)
                                        : 0.0f;
    ImVec2 pos(canvasMin.x + padV.x, canvasMax.y - padV.y - metricsH - h);

    // Publish the panel rect so the canvas hover-test excludes it (no click-through).
    st.overlayRects.push_back(ImVec4(pos.x, pos.y, pos.x + w, pos.y + h));

    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,     bg);
    ImGui::PushStyleColor(ImGuiCol_Border,      borderC);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   padV);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(itemSp, itemSp));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   radius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, border);
    ImGui::BeginChild("##operatorPanel", ImVec2(w, h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        DesignSystem::DesignSystem::ZoneStyle zone("viewport/operator", "Operator panel");
        // Header: a chevron glyph toggles expand/collapse; the title follows. The
        // chevron + title share one clickable button so the whole header toggles.
        auto& iconMgr = VectorGraphics::IconManager::Instance();
        ImVec2 hp = ImGui::GetCursorScreenPos();
        const char* glyph = operatorPanelExpanded_ ? "chevron-down" : "chevron-right";
        if (iconMgr.HasIcon(glyph)) {
            auto md = iconMgr.GetDefaultMetadata(glyph);
            if (!md.colorZones.empty())
                md.colorZones[0].customColor = ds.GetColor(Tok::S_Color_Text_Subtle);
            iconMgr.RenderIcon(ImGui::GetWindowDrawList(), glyph,
                ImVec2(hp.x, hp.y + (ImGui::GetTextLineHeight() - chev) * 0.5f), chev, md);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Default));
        ImGui::SetCursorScreenPos(ImVec2(hp.x + chev + itemSp, hp.y));
        ImGui::TextUnformatted(lastOperator_.title.c_str());
        ImGui::PopStyleColor();
        // Header hitbox over the whole row toggles the panel.
        ImGui::SetCursorScreenPos(hp);
        if (ImGui::InvisibleButton("##ophdr", ImVec2(contentW, ImGui::GetTextLineHeight())))
            operatorPanelExpanded_ = !operatorPanelExpanded_;

        if (operatorPanelExpanded_) {
            bool changed = false;
            for (size_t i = 0; i < lastOperator_.params.size(); ++i) {
                OperatorParam& p = lastOperator_.params[i];
                ImGui::PushID((int)i);
                ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
                if (p.kind == OperatorParam::Kind::Enum) {
                    // Label, then a fixed-width dropdown trigger on the same row.
                    ImGui::TextUnformatted(p.label.c_str());
                    ImGui::SameLine(0.0f, itemSp);
                    UI::DropdownConfig cfg; cfg.id = "##opparam";
                    cfg.triggerLabel = (p.value >= 0 && p.value < (int)p.options.size())
                                     ? p.options[(size_t)p.value] : "";
                    for (const std::string& o : p.options) {
                        UI::DropdownItem it; it.label = o; cfg.items.push_back(it);
                    }
                    cfg.selectedIndex = p.value;
                    UI::DropdownResult r = UI::Dropdown(cfg);
                    if (r.changed && r.selected >= 0 && r.selected != p.value) {
                        p.value = r.selected; changed = true;
                    }
                } else if (p.kind == OperatorParam::Kind::Float) {
                    // Label, then a fixed-width numeric field — neither overflows.
                    ImGui::TextUnformatted(p.label.c_str());
                    ImGui::SameLine(0.0f, itemSp);
                    ImGui::SetNextItemWidth(kInputW);
                    float v = p.fvalue;
                    if (ImGui::DragFloat("##v", &v, 0.1f, 0, 0, "%.4g")) {
                        p.fvalue = v; changed = true;
                    }
                } else { // Bool
                    bool b = p.value != 0;
                    if (UI::Checkbox("##boolParam", p.label.c_str(), &b)) { p.value = b ? 1 : 0; changed = true; }
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            if (changed && lastOperator_.rerun) {
                operatorRerunning_ = true;        // suppress dismiss during re-run
                lastOperator_.rerun();
                operatorRerunning_ = false;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
}

// ── The 2D canvas editor: artboard + unit rulers + camera + navigation ───────

} // namespace App

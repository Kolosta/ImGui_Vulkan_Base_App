#include "Application.h"
#include "PngWrite.h"
#include <Shell/ShellIntegration.h>
#include <SDL3/SDL.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip
#include <UI/Widgets/ScrollArea.h>    // UI::BeginScroll / EndScroll
#include <UI/Widgets/SidePanel.h>     // UI::EditorSidePanel
#include <imgui_internal.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

namespace App {
// ── Full-screen layout container ──────────────────────────────────────────────

void Application::RenderMainLayout() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    // The custom title bar occupies the top `titleBarHeightPx_` of the window.
    // It is a plain ImGui window pinned at vp->Pos, so the layout must use the
    // FULL window rect (Pos/Size), not the work-area, and start just below the
    // bar — otherwise it overlaps the editors underneath.
    const float topInset = titleBarHeightPx_;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topInset));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - topInset));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus            |
        ImGuiWindowFlags_NoScrollbar           |
        ImGuiWindowFlags_NoScrollWithMouse     |
        ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MainLayout", nullptr, kFlags);
    ImGui::PopStyleVar(3);

    // Reserve space for the bottom status bar. ImGui inserts ItemSpacing.y
    // between ##LayoutBody and the status-bar child, so subtract it too —
    // otherwise the status bar is pushed ItemSpacing.y past the window bottom
    // and its content clips below the screen edge.
    const float statusBarHeight = UI::StatusBar::Height();
    const float itemSpacingY    = ImGui::GetStyle().ItemSpacing.y;
    const float layoutHeight =
        ImGui::GetWindowHeight() - statusBarHeight - itemSpacingY;

    // NoBackground: this container is the app canvas BEHIND the editor zones —
    // the gap between zones must show the app base surface (the host
    // ##MainLayout window bg = component.window.background → app base), not the
    // generic child surface. Painting a ChildBg here would tint the inter-zone
    // gaps with the child colour.
    if (ImGui::BeginChild("##LayoutBody",
                          ImVec2(0.0f, layoutHeight),
                          false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoBackground)) {
        // Blender-style fixed 3-zone layout (no native docking UX). The tool
        // palette is no longer a global left strip — it floats inside the
        // Viewport editor. Each zone renders the editor it was assigned.
        zoneLayout_.Render();
    }
    ImGui::EndChild();

    RenderStatusBar();

    ImGui::End();
}

void Application::RenderStatusBar() {
    UI::StatusBar::Render(kVersion);
}

// ── Core editor registration ─────────────────────────────────────────────────
// Register the built-in editors into the EditorRegistry once at startup. Each
// descriptor's draw/topBar call the existing Render* methods. Modules add their
// own editors the same way (see ModuleAPI). This replaces the old per-kind
// switch in the layout: a zone now draws whatever id it holds.
void Application::RegisterCoreEditors() {
    auto& reg = EditorRegistry::Instance();

    // Viewport — the vector canvas. Draws its own chrome edge-to-edge: no scroll
    // wrap, no content inset. Carries the big editor top bar (mode/pivot/page).
    {
        EditorDescriptor d;
        d.id = CoreEditor::Viewport; d.name = "Viewport"; d.icon = "image";
        d.column = 0; d.themeScope = "editors/viewport";
        d.switchAction = "editor.viewport";
        d.wrapInScroll = false; d.contentInset = false;
        d.draw = [this](ImVec2 sz, EditorState& st) { RenderViewport(sz, st); };
        d.topBar = [this](EditorState& st, EditorBar& bar) {
            auto& ds = DesignSystem::DesignSystem::Instance();
            const float gs = ds.GetGlobalScale();
            EditorState* pst = &st;

            // LEFT: Object/Edit mode + ruler-space dropdowns + fill/stroke swatches.
            bar.left.width = (110.0f + 6.0f + 130.0f + 6.0f + 52.0f) * gs;
            bar.left.draw  = [this, gs](ImVec2 pos, float) {
                ImGui::SetCursorPos(pos);
                const bool canEdit = project_.document.HasSelection();
                { UI::DropdownConfig cfg; cfg.id = "##editormode";
                  cfg.triggerLabel = (editorMode_ == EditorMode::Edit) ? "Edit Mode" : "Object Mode";
                  { UI::DropdownItem it; it.label="Object Mode"; it.tooltip="Select and transform whole objects"; cfg.items.push_back(it); }
                  { UI::DropdownItem it; it.label="Edit Mode"; it.tooltip="Edit an object's points, edges and faces"; it.enabled=canEdit; cfg.items.push_back(it); }
                  cfg.selectedIndex = (editorMode_==EditorMode::Edit)?1:0;
                  UI::DropdownResult r = UI::Dropdown(cfg);
                  if (r.changed) { if (r.selected==0) editorMode_=EditorMode::Object;
                                   else if (r.selected==1 && canEdit) editorMode_=EditorMode::Edit; } }
            };
            {
                auto baseDraw = bar.left.draw;
                bar.left.draw = [this, gs, baseDraw, pst](ImVec2 pos, float bh) {
                    baseDraw(pos, bh);
                    ImGui::SameLine(0.0f, 6.0f * gs);
                    using RS = EditorState::RulerSpace;
                    UI::DropdownConfig cfg; cfg.id = "##rulerspace";
                    cfg.triggerLabel = (pst->rulerSpace==RS::Page)?"Page rulers":"Viewport rulers";
                    { UI::DropdownItem it; it.label="Viewport rulers"; it.tooltip="Rulers show absolute document coordinates"; cfg.items.push_back(it); }
                    { UI::DropdownItem it; it.label="Page rulers"; it.tooltip="Rulers show coordinates relative to the selected page"; cfg.items.push_back(it); }
                    cfg.selectedIndex = (pst->rulerSpace==RS::Page)?1:0;
                    UI::DropdownResult r = UI::Dropdown(cfg);
                    if (r.changed) pst->rulerSpace = (r.selected==1)?RS::Page:RS::Viewport;
                    // Default fill / stroke colour swatches (filled disc = fill, ring
                    // = stroke), right of the ruler dropdown. New shapes/curves use them.
                    ImGui::SameLine(0.0f, 8.0f * gs);
                    DrawDefaultColorSwatches(bh);
                };
            }

            // MIDDLE: Transform Orientation + Pivot Point + Snap group widget. The
            // three flow with the SAME small gap as the left/right groups (SameLine),
            // so the dropdowns auto-size and sit tight together (no wide fixed slots).
            const float kOrientW = 130.0f * gs;
            const float kPivotW  = 175.0f * gs;
            const float kSnapW   = 150.0f * gs;   // magnet button + mode trigger
            const float kMidGap  = 6.0f * gs;
            bar.middle.width = kOrientW + kMidGap + kPivotW + kMidGap + kSnapW;
            bar.middle.draw  = [this, kSnapW, kMidGap](ImVec2 pos, float) {
                // Transform Orientation: Global / Local / View / Cursor / Parent.
                ImGui::SetCursorPos(pos);
                static const char* kOrients[] = { "Global","Local","View","Cursor","Parent" };
                static const char* kOrientTips[] = {
                    "Transform along the document axes",
                    "Transform along the active object's own axes",
                    "Transform along the view axes",
                    "Transform along the 2D cursor's axes",
                    "Transform along the active object's parent axes" };
                { UI::DropdownConfig cfg; cfg.id="##orient"; cfg.triggerIcon="crop-free";
                  cfg.triggerLabel = kOrients[(int)transformOrientation_];
                  for (int i=0;i<5;++i){ UI::DropdownItem it; it.label=kOrients[i];
                      it.tooltip=kOrientTips[i]; cfg.items.push_back(it); }
                  cfg.selectedIndex=(int)transformOrientation_;
                  UI::DropdownResult r = UI::Dropdown(cfg);
                  if (r.changed && r.selected>=0 && r.selected<5)
                      transformOrientation_=(TransformOrientation)r.selected;
                }
                // Transform Pivot Point — flows right after, same gap as left group.
                ImGui::SameLine(0.0f, kMidGap);
                static const char* kPivots[] = { "Bounding Box Center","2D Cursor",
                    "Individual Origins","Median Point","Active Element" };
                static const char* kTips[] = {
                    "Pivot at the centre of the selection's bounding box",
                    "Pivot at the 2D cursor",
                    "Each object rotates/scales about its own origin",
                    "Pivot at the median of the selected origins",
                    "Pivot at the active (last-selected) object" };
                { UI::DropdownConfig cfg; cfg.id="##pivot"; cfg.triggerIcon="crop-free";
                  cfg.triggerLabel = kPivots[(int)pivotMode_];
                  for (int i=0;i<5;++i){ UI::DropdownItem it; it.label=kPivots[i]; it.tooltip=kTips[i]; cfg.items.push_back(it); }
                  cfg.selectedIndex=(int)pivotMode_;
                  UI::DropdownResult r = UI::Dropdown(cfg);
                  if (r.changed && r.selected>=0 && r.selected<5) pivotMode_=(PivotMode)r.selected;
                }
                // Snap group widget — flows right after with the same gap.
                ImGui::SameLine(0.0f, kMidGap);
                DrawSnapWidget(ImGui::GetCursorPos(), kSnapW);
            };

            // RIGHT: Page-Layout dropdown + show/hide-pages + "+" buttons.
            const float h = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gs;
            bar.right.width = 60.0f * gs + 6.0f * gs + h + 6.0f * gs + h + 6.0f * gs + h
                              + 6.0f * gs + h;   // + metrics-toggle button
            bar.right.draw  = [this, gs, h, pst](ImVec2 pos, float) {
                auto& ds2 = DesignSystem::DesignSystem::Instance();
                auto& iconMgr = VectorGraphics::IconManager::Instance();
                const float isz = ds2.GetFloat(DesignSystem::Tok::C_Dropdown_IconSize) * gs;
                ImGui::SetCursorPos(pos);
                { static const PageLayoutMode kModes[] = {
                    PageLayoutMode::Manual, PageLayoutMode::LeftToRight, PageLayoutMode::RightToLeft,
                    PageLayoutMode::TopToBottom, PageLayoutMode::BottomToTop, PageLayoutMode::Grid,
                    PageLayoutMode::BookLeft, PageLayoutMode::BookRight,
                    PageLayoutMode::SinglePage, PageLayoutMode::SingleBookLeft, PageLayoutMode::SingleBookRight };
                  static const char* kTips[] = {
                    "Pages stay where you placed them (free move)",
                    "Arrange pages in a row, left to right","Arrange pages in a row, right to left",
                    "Arrange pages in a column, top to bottom","Arrange pages in a column, bottom to top",
                    "Arrange pages in a wrapped grid","Two-page spreads, first page on the left",
                    "Two-page spreads, first page on the right",
                    "Show one page at a time (use the N panel ▸ Pages to switch)",
                    "One spread at a time, first page on the left",
                    "One spread at a time, first page on the right" };
                  constexpr int kModeCount = (int)(sizeof(kModes)/sizeof(kModes[0]));
                  UI::DropdownConfig cfg; cfg.id="##pagelayout"; cfg.triggerIcon="image-aspect-ratio";
                  cfg.triggerLabel = "";
                  int cur=0; for (int i=0;i<kModeCount;++i){ UI::DropdownItem it; it.label=PageLayoutModeName(kModes[i]); it.tooltip=kTips[i]; cfg.items.push_back(it); if(kModes[i]==pst->pageLayout.mode) cur=i; }
                  cfg.selectedIndex=cur;
                  UI::DropdownResult r = UI::Dropdown(cfg);
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                      UI::DrawTooltip((std::string("Page layout: ") +
                          PageLayoutModeName(pst->pageLayout.mode)).c_str(), ImGui::GetIO().MousePos);
                  if (r.changed && r.selected>=0 && r.selected<kModeCount) pst->pageLayout.mode=kModes[r.selected]; }

                ImGui::SameLine(0.0f, 6.0f * gs);
                ImGui::PushStyleColor(ImGuiCol_Button, ds2.GetColor(DesignSystem::Tok::C_IconButton_Background));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                bool showPagesClk = ImGui::Button("##showpages", ImVec2(h,h));
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                { ImVec2 bm = ImGui::GetItemRectMin();
                  ImVec2 ip = { bm.x + (h-isz)*0.5f, bm.y + (h-isz)*0.5f };
                  ImVec4 tn = ds2.GetColor(DesignSystem::Tok::S_Color_Text_Default);
                  auto m2 = iconMgr.GetDefaultMetadata("eye");
                  if (!m2.colorZones.empty()) { m2.colorZones[0].customColor = tn;
                      iconMgr.RenderIcon(ImGui::GetWindowDrawList(), "eye", ip, isz, m2); } }
                if (showPagesClk) ImGui::OpenPopup("##showPagesPopup");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    UI::DrawTooltip("Show / hide pages in this viewport", ImGui::GetIO().MousePos);
                ImGui::PushStyleColor(ImGuiCol_PopupBg, ds2.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
                ImGui::PushStyleColor(ImGuiCol_Text,    ds2.GetColor(DesignSystem::Tok::S_Color_Text_Default));
                if (ImGui::BeginPopup("##showPagesPopup")) {
                    ImGui::TextDisabled("Pages shown in this viewport");
                    auto& doc = project_.document;
                    auto& hidden = pst->pageLayout.hiddenPages;
                    for (const auto& ab : doc.artboards) {
                        bool shown = std::find(hidden.begin(), hidden.end(), ab.id) == hidden.end();
                        if (ImGui::Checkbox(ab.name.c_str(), &shown)) {
                            if (!shown) {
                                int shownCount = 0;
                                for (const auto& a2 : doc.artboards)
                                    if (std::find(hidden.begin(), hidden.end(), a2.id) == hidden.end()) ++shownCount;
                                if (shownCount > 1) hidden.push_back(ab.id);
                            } else {
                                hidden.erase(std::remove(hidden.begin(), hidden.end(), ab.id), hidden.end());
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor(2);

                // 2D cursor visibility toggle — hides only the drawing (the cursor
                // still has a position used by transforms / snaps).
                ImGui::SameLine(0.0f, 6.0f * gs);
                ImGui::PushStyleColor(ImGuiCol_Button, ds2.GetColor(DesignSystem::Tok::C_IconButton_Background));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                bool curClk = ImGui::Button("##cursortoggle", ImVec2(h,h));
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                { ImVec2 bm = ImGui::GetItemRectMin();
                  ImVec2 ip = { bm.x + (h-isz)*0.5f, bm.y + (h-isz)*0.5f };
                  ImVec4 tn = ds2.GetColor(show2DCursor_ ? DesignSystem::Tok::S_Color_Text_Default
                                                         : DesignSystem::Tok::S_Color_Text_Disabled);
                  const char* icon = show2DCursor_ ? "eye" : "eye-closed";
                  auto m3 = iconMgr.GetDefaultMetadata(icon);
                  if (!m3.colorZones.empty()) { m3.colorZones[0].customColor = tn;
                      iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, ip, isz, m3); } }
                if (curClk) show2DCursor_ = !show2DCursor_;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    UI::DrawTooltip("Show / hide the 2D cursor", ImGui::GetIO().MousePos);

                // Performance metrics overlay toggle (FPS / triangles / cache…).
                ImGui::SameLine(0.0f, 6.0f * gs);
                ImGui::PushStyleColor(ImGuiCol_Button, ds2.GetColor(DesignSystem::Tok::C_IconButton_Background));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                bool mtClk = ImGui::Button("##metricstoggle", ImVec2(h,h));
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                { ImVec2 bm = ImGui::GetItemRectMin();
                  ImVec2 ip = { bm.x + (h-isz)*0.5f, bm.y + (h-isz)*0.5f };
                  ImVec4 tn = ds2.GetColor(showMetrics_ ? DesignSystem::Tok::S_Color_Accent_Default
                                                        : DesignSystem::Tok::S_Color_Text_Default);
                  const char* icon = iconMgr.HasIcon("speed") ? "speed"
                                   : iconMgr.HasIcon("activity") ? "activity" : "info";
                  auto mm = iconMgr.GetDefaultMetadata(icon);
                  if (!mm.colorZones.empty()) { mm.colorZones[0].customColor = tn;
                      iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, ip, isz, mm); } }
                if (mtClk) showMetrics_ = !showMetrics_;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    UI::DrawTooltip("Show / hide performance metrics", ImGui::GetIO().MousePos);

                ImGui::SameLine(0.0f, 6.0f * gs);
                ImGui::PushStyleColor(ImGuiCol_Button, ds2.GetColor(DesignSystem::Tok::C_IconButton_Background));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                bool clicked = ImGui::Button("##newdoc", ImVec2(h,h));
                ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
                ImVec2 bmin = ImGui::GetItemRectMin();
                ImVec2 ipos = { bmin.x + (h-isz)*0.5f, bmin.y + (h-isz)*0.5f };
                ImVec4 tint = ds2.GetColor(DesignSystem::Tok::S_Color_Text_Default);
                auto md = iconMgr.GetDefaultMetadata("new");
                if (!md.colorZones.empty()) { md.colorZones[0].customColor = tint;
                    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), "new", ipos, isz, md); }
                if (clicked) pst->openNewDoc = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    UI::DrawTooltip("New document (Ctrl+Shift+N)", ImGui::GetIO().MousePos);
            };
        };
        reg.Register(std::move(d));
    }

    // Outliner — object/collection/page tree. Has its own top bar.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Outliner; d.name = "Outliner"; d.icon = "checklist";
        d.column = 2; d.themeScope = "editors/outliner";
        d.switchAction = "editor.outliner";
        d.draw   = [this](ImVec2, EditorState& st) { RenderOutliner(st); };
        d.topBar = [this](EditorState& st, EditorBar& bar) { BuildOutlinerTopBar(st, bar); };
        reg.Register(std::move(d));
    }

    // Properties — selected object's properties.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Properties; d.name = "Properties"; d.icon = "settings";
        d.column = 2; d.themeScope = "editors/properties";
        d.switchAction = "editor.properties";
        d.draw = [this](ImVec2, EditorState&) { RenderProperties(); };
        reg.Register(std::move(d));
    }

    // Timeline — placeholder strip with the reusable side panel demo.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Timeline; d.name = "Timeline"; d.icon = "find-replace";
        d.column = 1; d.themeScope = "editors/timeline";
        d.switchAction = "editor.timeline";
        d.wrapInScroll = false;
        d.draw = [this](ImVec2 sz, EditorState& st) {
            ImGui::TextDisabled("Timeline");
            ImGui::TextDisabled("(animation timeline — later)");
            ImVec2 cMn = ImGui::GetWindowPos();
            ImVec2 cMx(cMn.x + sz.x, cMn.y + sz.y);
            std::vector<UI::SidePanelTab> tabs;
            const char* names[] = { "Tool", "Curves", "Modifiers" };
            for (const char* nm : names) {
                UI::SidePanelTab t; t.name = nm;
                std::string label = nm;
                t.draw = [label](ImVec2 a, ImVec2) {
                    ImGui::SetCursorScreenPos(ImVec2(a.x + 10, a.y + 10));
                    ImGui::TextDisabled("%s panel (demo)", label.c_str());
                };
                tabs.push_back(std::move(t));
            }
            if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_N, false) &&
                !ImGui::GetIO().WantTextInput)
                st.sidePanel.stage = (st.sidePanel.stage == 2) ? 0 : 2;
            UI::EditorSidePanel("##timelineSide", cMn, cMx, st.sidePanel, tabs);
        };
        reg.Register(std::move(d));
    }

    // Dev Panel — live debug data (undo/redo lists, document/selection state).
    {
        EditorDescriptor d;
        d.id = CoreEditor::DevPanels; d.name = "Dev Panel"; d.icon = "draw";
        d.column = 2; d.themeScope = "editors/devPanels";
        d.switchAction = "editor.devPanels";
        d.draw = [this](ImVec2, EditorState&) { RenderDevDataEditor(); };
        reg.Register(std::move(d));
    }

    // Info — live action feed (Blender info-log style). Manages its own scroll.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Info; d.name = "Info"; d.icon = "format-align-left";
        d.column = 2; d.themeScope = "editors/info";
        d.switchAction = "editor.info";
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState&) { RenderInfoEditor(); };
        reg.Register(std::move(d));
    }
}

// Regenerate the .acu thumbnail PNG from the chosen page/region. Frames the
// region into a max-256px image (aspect-preserving), renders it offscreen via
// the Vulkan canvas renderer, and PNG-encodes the result into project_.

// No-arg form: use the project's stored framing (default whole Page 1).
void Application::Action_UpdateThumbnail() {
    Action_UpdateThumbnail(project_.thumbArtboard,
                           project_.thumbRegionMin, project_.thumbRegionSize);
}

void Application::Action_UpdateThumbnail(int ab,
                                         Renderer::Vec2 rmin,
                                         Renderer::Vec2 rsz) {
    auto& doc = project_.document;
    if (ab < 0 || ab >= (int)doc.artboards.size()) ab = 0;
    if (doc.artboards.empty()) return;
    const Renderer::Artboard& art = doc.artboards[(size_t)ab];

    // Region in doc-units: explicit sub-region, else the whole artboard.
    if (rsz.x <= 1.0f || rsz.y <= 1.0f) { rmin = art.pos; rsz = art.size; }
    if (rsz.x <= 1.0f || rsz.y <= 1.0f) return;

    // Fit the region into a 256-px box, preserving aspect.
    const int kMax = 256;
    float aspect = rsz.x / rsz.y;
    int W = kMax, H = kMax;
    if (aspect >= 1.0f) H = std::max(1, (int)std::lround(kMax / aspect));
    else                W = std::max(1, (int)std::lround(kMax * aspect));

    Renderer::Camera cam;
    cam.unitScale = 1.0f;
    cam.zoom = (float)W / rsz.x;     // region width → image width
    cam.panX = rmin.x;               // doc point rmin maps to pixel (0,0)
    cam.panY = rmin.y;

    std::vector<unsigned char> rgba;
    ImVec4 backdrop(1, 1, 1, 1);     // white page backdrop for the thumbnail
    if (!canvasRenderer_.RenderToRGBA(doc, cam, W, H, backdrop, rgba)) return;

    std::vector<uint8_t> pngBytes;
    if (App::png::EncodeRGBA(rgba.data(), W, H, pngBytes)) {
        project_.thumbnailPng  = std::move(pngBytes);
        project_.thumbArtboard = ab;
        // Persist the framing: a whole-page render clears the sub-region (so a
        // later page resize re-frames the whole page); a Zone render stores it.
        bool wholePage = (rmin.x == art.pos.x && rmin.y == art.pos.y &&
                          rsz.x == art.size.x && rsz.y == art.size.y);
        project_.thumbRegionMin  = wholePage ? Renderer::Vec2{0, 0} : rmin;
        project_.thumbRegionSize = wholePage ? Renderer::Vec2{0, 0} : rsz;
        project_.dirty = true;
    }
}

namespace {
// Pack several RGBA sizes into a single PNG-embedded .ico (ICONDIR + entries).
// Windows (Vista+) accepts PNG image data inside .ico for any size.
void WriteIco(const std::string& path,
              const std::vector<std::pair<int, std::vector<uint8_t>>>& pngs) {
    std::vector<uint8_t> out;
    auto u16 = [&](uint16_t v){ out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v){ for (int i=0;i<4;++i) out.push_back((uint8_t)(v >> (i*8))); };
    // ICONDIR.
    u16(0); u16(1); u16((uint16_t)pngs.size());
    // Entries (image data starts after the dir).
    uint32_t offset = 6 + 16 * (uint32_t)pngs.size();
    for (const auto& [sz, png] : pngs) {
        out.push_back(sz >= 256 ? 0 : (uint8_t)sz);   // width (0 = 256)
        out.push_back(sz >= 256 ? 0 : (uint8_t)sz);   // height
        out.push_back(0);  // palette
        out.push_back(0);  // reserved
        u16(1);            // colour planes
        u16(32);           // bpp
        u32((uint32_t)png.size());
        u32(offset);
        offset += (uint32_t)png.size();
    }
    for (const auto& [sz, png] : pngs) { (void)sz; out.insert(out.end(), png.begin(), png.end()); }
    std::ofstream f(path, std::ios::binary);
    if (f) f.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
}
} // namespace

void Application::RegisterShellIntegration() {
#ifdef _WIN32
    const char* base = SDL_GetBasePath();
    if (!base) return;
    std::string exeDir = base;
    std::string icoPath = exeDir + "carto_acu.ico";
    std::string dllPath = exeDir + "acu_thumbs.dll";

    // Build the .ico from the logo (original colours) at a few sizes — only if
    // it isn't already there (cheap idempotency; delete the file to refresh).
    {
        std::ifstream exists(icoPath, std::ios::binary);
        if (!exists.good()) {
            auto& im = VectorGraphics::IconManager::Instance();
            std::string svg = exeDir + "resources/icons/logo_carto.svg";
            const int sizes[] = { 16, 32, 48, 256 };
            std::vector<std::pair<int, std::vector<uint8_t>>> pngs;
            for (int s : sizes) {
                std::vector<uint8_t> rgba;
                if (!im.RasterizeSvgFile(svg, s, s, rgba)) continue;
                std::vector<uint8_t> png;
                if (App::png::EncodeRGBA(rgba.data(), s, s, png))
                    pngs.emplace_back(s, std::move(png));
            }
            if (!pngs.empty()) WriteIco(icoPath, pngs);
        }
    }

    ShellReg::EnsureRegistered(icoPath, dllPath);
#endif
}

// Build an SDL_Surface (RGBA32) from the logo SVG at `px`, owning a copy of the
// pixels (SDL_CreateSurfaceFrom does NOT copy, so we duplicate into the surface).
namespace {
SDL_Surface* LogoSurface(const std::string& svgPath, int px) {
    std::vector<uint8_t> rgba;
    if (!VectorGraphics::IconManager::Instance().RasterizeSvgFile(svgPath, px, px, rgba))
        return nullptr;
    if ((int)rgba.size() < px * px * 4) return nullptr;
    // Make a surface that owns its pixels (so the temporary `rgba` can die).
    SDL_Surface* s = SDL_CreateSurface(px, px, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);
    for (int y = 0; y < px; ++y)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + (size_t)y * s->pitch,
                    rgba.data() + (size_t)y * px * 4, (size_t)px * 4);
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    return s;
}
} // namespace

void Application::SetWindowIconFromLogo() {
    if (!window_) return;
    const char* base = SDL_GetBasePath();
    if (!base) return;
    const std::string svg = std::string(base) + "resources/icons/logo_carto.svg";

    // Primary icon at a large size; Windows scales it for the taskbar / Alt-Tab.
    SDL_Surface* icon = LogoSurface(svg, 256);
    if (!icon) return;
    // Crisper small sizes for the taskbar/title (SDL picks the best alternate).
    for (int px : { 16, 24, 32, 48, 64 })
        if (SDL_Surface* alt = LogoSurface(svg, px))
            SDL_AddSurfaceAlternateImage(icon, alt);   // icon keeps a reference

    SDL_SetWindowIcon(window_, icon);
    // SDL copies what it needs into the platform icon on SetWindowIcon, so the
    // surfaces (and their alternates) can be freed now.
    SDL_DestroySurface(icon);
}

// ── Left toolbar ──────────────────────────────────────────────────────────────

// A real, minimal tool palette (Photoshop/Affinity-style): a narrow single
// column of small icon buttons for the *actual* tools only, with a settings
// button pinned at the bottom. Compact, light surface (token-driven, not the
// accent colour), active tool highlighted.
void Application::RenderToolbar() {
    auto& ds      = DesignSystem::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    auto& sm      = Shortcuts::ShortcutManager::Instance();
    auto& tm      = Shortcuts::Tools::ToolManager::Instance();

    const float gs    = ds.GetGlobalScale();
    const float kBtn  = 28.0f * gs;             // small, like a real toolbar
    const float kPad  = 4.0f  * gs;
    const float width = kBtn + kPad * 2.0f;
    toolbarWidth_ = width;

    // Light surface from the design system (NOT accent/primary).
    ImVec4 barBg   = ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1);
    ImVec4 btnBg   = ds.GetColor(DesignSystem::Tok::C_IconButton_Background);
    ImVec4 btnHov  = ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover);
    ImVec4 btnActC = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);
    ImVec4 iconCol = ds.GetColor(DesignSystem::Tok::S_Color_Text_Default);

    ImGui::PushStyleColor(ImGuiCol_ChildBg,          barBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, kPad));

    if (ImGui::BeginChild("##Toolbar", ImVec2(width, 0.0f), false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse))
    {
        sm.RegisterRegionContext("##Toolbar", "", "toolbar");
        DesignSystem::DesignSystem::ZoneStyle zone("toolbar", "Toolbar");

        const std::string activeTool = tm.GetActiveTool();

        // Legacy standalone palette (unused — the live palette floats inside the
        // Viewport now). Kept data-driven from ToolManager so it never drifts.
        struct ToolDef {
            const char* key;       // ToolManager id
            const char* icon;      // icon id
            const char* tip;       // tooltip label
            const char* shortcut;  // shortcut action id
        };
        const ToolDef tools[] = {
            { "tool.select", "select",         "Select",    "tool.select.activate"   },
            { "tool.rect",   "crop-landscape", "Rectangle", "tool.rect.activate"     },
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                            ds.GetFloat(DesignSystem::Tok::C_IconButton_CornerRadius) * gs);

        auto iconButton = [&](const char* id, const char* icon,
                              const char* tip, const char* shortcut,
                              bool selected) -> bool {
            ImGui::PushID(id);
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  selected ? btnActC : btnBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  selected ? btnActC : btnHov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActC);
            bool clicked = ImGui::Button("##b", ImVec2(kBtn, kBtn));
            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(tip);
                std::string sc = sm.GetShortcutString(shortcut);
                if (!sc.empty()) ImGui::TextDisabled("Shortcut: %s", sc.c_str());
                ImGui::EndTooltip();
            }

            const float  isz = kBtn * 0.62f;
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 ipos = { bmin.x + (kBtn - isz) * 0.5f,
                                  bmin.y + (kBtn - isz) * 0.5f };
            auto md = iconMgr.GetDefaultMetadata(icon);
            // Single-zone (ds-primary) icons tint to the toolbar text colour.
            if (!md.colorZones.empty()) md.colorZones[0].customColor = iconCol;
            iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, ipos, isz, md);
            ImGui::PopID();
            return clicked;
        };

        for (const ToolDef& t : tools) {
            if (iconButton(t.key, t.icon, t.tip, t.shortcut,
                           activeTool == t.key))
                Action_ActivateNamedTool(t.key);
        }

        // Push the settings button to the bottom.
        float used = ImGui::GetCursorPosY();
        float avail = ImGui::GetWindowHeight();
        float gap = avail - used - kBtn - kPad;
        if (gap > 0.0f) ImGui::Dummy(ImVec2(0.0f, gap));

        if (iconButton("app.settings", "settings", "Settings",
                       "app.toggleSettings", showSettings_))
            Action_ToggleSettings();

        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild(); // ##Toolbar

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Two swatches in the Viewport top bar for the DEFAULT fill / stroke colours used
// by new shapes & curves: a filled disc (fill) and a ring (stroke). Clicking a
// swatch opens an ImGui colour picker popup; the choice updates defaultFill_ /
// defaultStroke_. Drawn with the design-system border token so they match the bar.
void Application::DrawDefaultColorSwatches(float barH) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float d  = 18.0f * gs;                       // swatch diameter
    const float gap = 6.0f * gs;
    ImU32 border = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Dropdown_Border));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto swatch = [&](const char* id, Renderer::Color& col, bool ring) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        // Vertically centre the swatch in the bar.
        float yoff = (barH > 0.0f) ? (barH - d) * 0.5f : 0.0f;
        ImVec2 c{ p.x + d * 0.5f, p.y + yoff + d * 0.5f };
        ImGui::InvisibleButton(id, ImVec2(d, d + (yoff > 0 ? yoff * 2 : 0)));
        bool hovered = ImGui::IsItemHovered();
        ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(col.r, col.g, col.b, 1.0f));
        if (ring) {
            // Stroke swatch: a thick ring (hollow centre) so it reads as "contour".
            dl->AddCircleFilled(c, d * 0.5f, fill, 24);
            dl->AddCircleFilled(c, d * 0.28f,
                ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1)), 24);
        } else {
            dl->AddCircleFilled(c, d * 0.5f, fill, 24);
        }
        dl->AddCircle(c, d * 0.5f, border, 24, hovered ? 2.0f : 1.0f);
        if (hovered)
            UI::DrawTooltip(ring ? "Default stroke colour (new shapes)"
                                 : "Default fill colour (new shapes)",
                            ImGui::GetIO().MousePos);
        if (ImGui::IsItemClicked()) ImGui::OpenPopup(id);
        if (ImGui::BeginPopup(id)) {
            DesignSystem::DesignSystem::ZoneStyle zone("editors/viewport", "Viewport");
            float rgba[4] = { col.r, col.g, col.b, col.a };
            if (ImGui::ColorPicker4("##pick", rgba,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview)) {
                col.r = rgba[0]; col.g = rgba[1]; col.b = rgba[2]; col.a = rgba[3];
            }
            ImGui::EndPopup();
        }
    };

    swatch("##fillCol", defaultFill_, /*ring=*/false);
    ImGui::SameLine(0.0f, gap);
    swatch("##strokeCol", defaultStroke_, /*ring=*/true);
}

// ── Floating tool palette, drawn as an overlay inside the canvas ──────────────
// Real tools (Brush / Eraser / Hand), pinned to the left edge inside the
// viewport like Affinity / Illustrator. A child window so it overlaps the
// canvas content.

void Application::RenderMainContent() {
    // The demo/test panels moved to the floating "Dev Test Window". This
    // central area is the viewport host; the full 3-zone layout + rulers +
    // floating tools are built in BLOC 2. Placeholder until then.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("##MainContent", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    ImGui::TextDisabled("Viewport");
    ImGui::TextDisabled("(3-zone layout, rulers and floating tools: next step)");
    ImGui::EndChild(); // ##MainContent
}

// All former main-area panels, gathered into one unique, non-dockable
// floating window — same organisation as before (chevron sections).

} // namespace App

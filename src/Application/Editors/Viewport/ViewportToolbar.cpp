#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/DragValue.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport top bar + floating tool palette + Snap widget + Add menu
//  (docs/Ink/ROADMAP.md Lot 8). This restores the legacy Viewport bar on the
//  Ink model: the Object/Edit-mode, transform-orientation, pivot-point and snap
//  controls plus the overlay dropdown. Controls whose backing feature has NOT
//  landed yet on Ink are shown but GREYED (disabled) — they light up in later
//  passes. Every colour/size comes from design-system tokens.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ── Floating tool palette (left column of the canvas) ─────────────────────────

void Application::RenderToolPalette(ImVec2 origin, EditorState& st) {
    auto& ds      = DS::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    auto& sm      = Shortcuts::ShortcutManager::Instance();
    auto& tm      = Shortcuts::Tools::ToolManager::Instance();

    const float gs   = ds.GetGlobalScale();
    const float kBtn = 26.0f * gs;
    const float kPad = 4.0f  * gs;

    std::vector<const Shortcuts::Tools::ToolDef*> toolDefs = tm.GetAllTools();
    const std::string activeTool = tm.GetActiveTool();
    const bool edit = (edit_.mode == EditorMode::Edit);
    // Edit-only tools are hidden in Object Mode (mirrors the legacy skip-logic).
    auto toolVisible = [&](const Shortcuts::Tools::ToolDef* t) {
        if (t->id == "tool.extrude") return edit;
        return true;
    };
    int rows = 0;
    for (const auto* t : toolDefs) if (toolVisible(t)) ++rows;
    if (rows == 0) return;
    const float w = kBtn + kPad * 2.0f;
    const float h = (float)rows * kBtn + (float)(rows + 1) * kPad;

    // Publish the palette rect so canvas hit-testing excludes it.
    st.overlayRects.push_back(ImVec4(origin.x + kPad, origin.y + kPad,
                                     origin.x + kPad + w, origin.y + kPad + h));

    ImGui::SetCursorScreenPos(ImVec2(origin.x + kPad, origin.y + kPad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ds.GetColor(Tok::S_Color_Background_Layer1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f * gs);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("##InkTools", ImVec2(w, h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        DS::DesignSystem::ZoneStyle zone("viewport/tools", "Viewport tools");
        ImVec4 bg   = ds.GetColor(Tok::C_IconButton_Background);
        ImVec4 hov  = ds.GetColor(Tok::C_IconButton_BackgroundHover);
        ImVec4 acc  = ds.GetColor(Tok::S_Color_Accent_Default);
        ImVec4 tint = ds.GetColor(Tok::S_Color_Text_Default);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        for (const auto* t : toolDefs) {
            if (!toolVisible(t)) continue;
            const bool seld = (activeTool == t->id);
            ImGui::PushID(t->id.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button,        seld ? acc : bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, seld ? acc : hov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  acc);
            const bool clk = ImGui::Button("##b", ImVec2(kBtn, kBtn));
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                std::string tip = t->name;
                if (!t->actionIds.empty()) {
                    std::string s = sm.GetShortcutString(t->actionIds.front());
                    if (!s.empty()) tip += "   (" + s + ")";
                }
                UI::DrawTooltip(tip.c_str(), ImGui::GetIO().MousePos);
            }
            const float isz = kBtn * 0.62f;
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 ipos = { bmin.x + (kBtn - isz) * 0.5f,
                                  bmin.y + (kBtn - isz) * 0.5f };
            auto md = iconMgr.GetDefaultMetadata(t->iconId);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            iconMgr.RenderIcon(ImGui::GetWindowDrawList(), t->iconId, ipos, isz, md);
            ImGui::PopID();
            if (clk) Action_ActivateNamedTool(t->id);
        }
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

// ── Default fill / stroke swatches (new-shape style) ──────────────────────────

void Application::DrawDefaultColorSwatches(float barHeight) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float sw = barHeight * 0.62f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Fill swatch (filled disc).
    ImGui::PushID("##fillSw");
    if (ImGui::ColorButton("##fill", edit_.defaultFill,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(sw, sw)))
        ImGui::OpenPopup("##fillPick");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        UI::DrawTooltip("Default fill for new shapes", ImGui::GetIO().MousePos);
    if (ImGui::BeginPopup("##fillPick")) {
        ImGui::ColorPicker4("##fillp", &edit_.defaultFill.x,
                            ImGuiColorEditFlags_NoSidePreview);
        ImGui::Checkbox("Fill enabled", &edit_.defaultFillEnabled);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    (void)dl;

    ImGui::SameLine(0.0f, 4.0f * gs);
    // Stroke swatch.
    ImGui::PushID("##strokeSw");
    if (ImGui::ColorButton("##stroke", edit_.defaultStroke,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(sw, sw)))
        ImGui::OpenPopup("##strokePick");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        UI::DrawTooltip("Default stroke for new shapes", ImGui::GetIO().MousePos);
    if (ImGui::BeginPopup("##strokePick")) {
        ImGui::ColorPicker4("##strokep", &edit_.defaultStroke.x,
                            ImGuiColorEditFlags_NoSidePreview);
        ImGui::Checkbox("Stroke enabled", &edit_.defaultStrokeEnabled);
        float w = (float)edit_.defaultStrokeWidth;
        if (ImGui::DragFloat("Width", &w, 0.1f, 0.0f, 100.0f, "%.1f"))
            edit_.defaultStrokeWidth = w;
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// ── Snap widget: magnet toggle fused to a Snap dropdown ───────────────────────

static const char* kSnapModeNames[] = { "Increment", "Grid", "Vertex", "Edge",
                                         "Face", "Edge Center" };
static const char* kSnapBaseNames[] = { "Closest", "Pivot", "Median", "Active" };

void Application::DrawSnapWidget(ImVec2 pos, float widthPx) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    ImGui::SetCursorPos(pos);
    UI::DropdownConfig cfg;
    cfg.id = "##snap";
    cfg.triggerIcon = "background-dot-small";
    cfg.triggerLabel = kSnapModeNames[(int)edit_.snap.mode];
    UI::DropdownButton mag;
    mag.id = "magnet"; mag.icon = "background-dot-small"; mag.active = edit_.snap.enabled;
    mag.side = UI::DropdownButton::Side::Left;
    mag.tooltip = edit_.snap.enabled
        ? "Snapping ON (click to disable; Ctrl snaps one drag)"
        : "Snapping OFF (click to enable; hold Ctrl to snap a drag)";
    cfg.buttons.push_back(mag);

    const float bodyW = 230.0f * gs;
    const float bodyH = 300.0f * gs;
    cfg.menuSize = ImVec2(bodyW, bodyH);
    cfg.bodyDraw = [this, &ds, gs, bodyW]() {
        auto subtle = [&](const char* s) {
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted(s); ImGui::PopStyleColor();
        };
        // Snap To — only Increment is implemented on Ink; the rest are greyed.
        subtle("Snap To");
        {
            static const char* kSnapIcons[6] = {
                "background-dot-small", "grid-on", "line-start-square",
                "diagonal-line", "crop-free", "line-end-diamond" };
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapto");
            g.SetGrid({ bodyW }, std::vector<float>(6, cellH));
            for (int i = 0; i < 6; ++i) {
                UI::ButtonGroup::Cell c{};
                c.label = kSnapModeNames[i];
                c.icon  = kSnapIcons[i];
                c.col = 0; c.row = i;
                c.selected = ((int)edit_.snap.mode == i);
                c.enabled  = (i == 0);   // Increment only (Lot 8); others later
                c.align = UI::ButtonGroup::Align::Left;
                g.AddCell(c);
            }
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex == 0) edit_.snap.mode = SnapSettings::Mode::Increment;
        }
        ImGui::Separator();
        // Snap Base — a 4-cell group (functional: affects incremental rounding
        // base later; kept enabled to preserve the control surface).
        subtle("Snap Base");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 3.0f) / 4.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapbase");
            g.SetGrid({ cellW, cellW, cellW, cellW }, { cellH });
            for (int i = 0; i < 4; ++i)
                g.AddCell(kSnapBaseNames[i], i, 0, 1, 1, (int)edit_.snap.base == i);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex >= 0) edit_.snap.base = (SnapSettings::Base)r.clickedIndex;
        }
        ImGui::Separator();
        subtle("Affect");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 2.0f) / 3.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapaffect");
            g.SetGrid({ cellW, cellW, cellW }, { cellH });
            g.AddCell("Move",   0, 0, 1, 1, edit_.snap.affectMove);
            g.AddCell("Rotate", 1, 0, 1, 1, edit_.snap.affectRotate);
            g.AddCell("Scale",  2, 0, 1, 1, edit_.snap.affectScale);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex == 0) edit_.snap.affectMove   = !edit_.snap.affectMove;
            if (r.clickedIndex == 1) edit_.snap.affectRotate = !edit_.snap.affectRotate;
            if (r.clickedIndex == 2) edit_.snap.affectScale  = !edit_.snap.affectScale;
        }
        ImGui::Separator();
        subtle("Increment");
        {
            float mv = (float)edit_.snap.moveIncrement;
            if (ImGui::DragFloat("Move##inc", &mv, 0.5f, 0.0f, 1000.0f, "%.1f"))
                edit_.snap.moveIncrement = mv;
            if (ImGui::DragFloat("Rotate##inc", &edit_.snap.rotIncrement, 0.5f,
                                 0.0f, 180.0f, "%.1f\xC2\xB0"))
                ;
        }
    };
    UI::DropdownResult r = UI::Dropdown(cfg);
    if (r.buttonClicked == 0) edit_.snap.enabled = !edit_.snap.enabled;
    (void)widthPx;
}

// ── The Viewport top bar ──────────────────────────────────────────────────────

void Application::BuildViewportTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    EditorState* pst = &st;

    // LEFT: Object/Edit mode + ruler space (greyed) + fill/stroke swatches.
    bar.left.width = (110.0f + 6.0f + 130.0f + 6.0f + 60.0f) * gs;
    bar.left.draw = [this, gs, pst](ImVec2 pos, float bh) {
        ImGui::SetCursorPos(pos);
        const bool canEdit = edit_.active != Ink::kNullNode && project_.document &&
            project_.document->Find(edit_.active) &&
            project_.document->Find(edit_.active)->kind == Ink::NodeKind::Path;
        { UI::DropdownConfig cfg; cfg.id = "##editormode";
          cfg.triggerLabel = (edit_.mode == EditorMode::Edit) ? "Edit Mode" : "Object Mode";
          { UI::DropdownItem it; it.label = "Object Mode";
            it.tooltip = "Select and transform whole objects"; cfg.items.push_back(it); }
          { UI::DropdownItem it; it.label = "Edit Mode";
            it.tooltip = "Edit an object's points"; it.enabled = canEdit;
            cfg.items.push_back(it); }
          cfg.selectedIndex = (edit_.mode == EditorMode::Edit) ? 1 : 0;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed) {
              if (r.selected == 0) Action_ExitEditMode();
              else if (r.selected == 1 && canEdit) Action_EnterEditMode();
          }
        }
        // Ruler-space dropdown — the rulers/ruler-space feature is not on Ink
        // yet, so the control is present but GREYED (returns in a later pass).
        ImGui::SameLine(0.0f, 6.0f * gs);
        { UI::DropdownConfig cfg; cfg.id = "##rulerspace";
          cfg.triggerLabel = "Viewport rulers";
          { UI::DropdownItem it; it.label = "Viewport rulers"; it.enabled = false;
            cfg.items.push_back(it); }
          { UI::DropdownItem it; it.label = "Page rulers"; it.enabled = false;
            cfg.items.push_back(it); }
          cfg.selectedIndex = 0;
          UI::Dropdown(cfg);
        }
        ImGui::SameLine(0.0f, 8.0f * gs);
        DrawDefaultColorSwatches(bh);
        (void)pst;
    };

    // MIDDLE: Transform Orientation + Pivot Point + Snap widget.
    const float kOrientW = 130.0f * gs;
    const float kPivotW  = 190.0f * gs;
    const float kSnapW   = 150.0f * gs;
    const float kMidGap  = 6.0f * gs;
    bar.middle.width = kOrientW + kMidGap + kPivotW + kMidGap + kSnapW;
    bar.middle.draw = [this, kMidGap](ImVec2 pos, float) {
        ImGui::SetCursorPos(pos);
        static const char* kOrients[] = { "Global", "Local", "View", "Cursor", "Parent" };
        static const char* kOrientTips[] = {
            "Transform along the document axes",
            "Transform along the active object's own axes",
            "Transform along the view axes",
            "Transform along the 2D cursor's axes",
            "Transform along the active object's parent axes" };
        { UI::DropdownConfig cfg; cfg.id = "##orient"; cfg.triggerIcon = "crop-free";
          cfg.triggerLabel = kOrients[(int)edit_.orientation];
          for (int i = 0; i < 5; ++i) {
              UI::DropdownItem it; it.label = kOrients[i]; it.tooltip = kOrientTips[i];
              cfg.items.push_back(it);
          }
          cfg.selectedIndex = (int)edit_.orientation;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed && r.selected >= 0 && r.selected < 5)
              edit_.orientation = (TransformOrientation)r.selected;
        }
        ImGui::SameLine(0.0f, kMidGap);
        static const char* kPivots[] = { "Bounding Box Center", "2D Cursor",
            "Individual Origins", "Median Point", "Active Element" };
        { UI::DropdownConfig cfg; cfg.id = "##pivot"; cfg.triggerIcon = "crop-free";
          cfg.triggerLabel = kPivots[(int)edit_.pivot];
          for (int i = 0; i < 5; ++i) {
              UI::DropdownItem it; it.label = kPivots[i];
              cfg.items.push_back(it);
          }
          cfg.selectedIndex = (int)edit_.pivot;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed && r.selected >= 0 && r.selected < 5)
              edit_.pivot = (PivotMode)r.selected;
        }
        ImGui::SameLine(0.0f, kMidGap);
        DrawSnapWidget(ImGui::GetCursorPos(), 150.0f);
    };

    // RIGHT: overlay dropdown (icon-only). Page-layout / per-page / 2D-cursor /
    // metrics are not on Ink yet → shown greyed.
    const float h = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    bar.right.width = h + 6.0f * gs;
    bar.right.draw = [this, gs](ImVec2 pos, float) {
        auto& ds2 = DS::DesignSystem::Instance();
        ImGui::SetCursorPos(pos);
        const float bodyW = 220.0f * gs, bodyH = 200.0f * gs;
        UI::DropdownConfig ov;
        ov.id = "##viewportOverlay";
        ov.triggerIcon = "image-aspect-ratio";
        ov.triggerLabel = "";
        ov.menuSize = ImVec2(bodyW, bodyH);
        ov.bodyDraw = [&ds2]() {
            ImGui::PushStyleColor(ImGuiCol_Text, ds2.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted("Overlays"); ImGui::PopStyleColor();
            ImGui::BeginDisabled(true);
            bool page = true, pages = true, cursor = false, metrics = false;
            ImGui::Checkbox("Page layout (later)", &page);
            ImGui::Checkbox("Show pages (later)", &pages);
            ImGui::Checkbox("2D cursor (later)", &cursor);
            ImGui::Checkbox("Performance metrics (later)", &metrics);
            ImGui::EndDisabled();
        };
        UI::Dropdown(ov);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip("Viewport overlays (page layout, cursor, metrics — later)",
                            ImGui::GetIO().MousePos);
    };
}

// ── Shift+A Add menu ──────────────────────────────────────────────────────────

// The Add menu (Shift+A). CRITICAL: this must be called every frame from a
// STABLE, unconditional place (RenderViewport calls it after the canvas blit,
// not gated by hover) — a popup whose BeginPopup stops being called freezes.
// Action_OpenAddMenu opens the ImGui popup exactly once; here we only render it.
void Application::RenderAddMenu() {
    if (!addMenuOpen_) return;

    std::vector<UI::MenuEntry> entries;
    auto leaf = [&](const char* label, const char* kind, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        e.onClick = [this, kind]() { SpawnShape(kind); addMenuOpen_ = false; };
        entries.push_back(std::move(e));
    };
    leaf("Rectangle", "rect",     "Add a rectangle at the 2D cursor");
    leaf("Ellipse",   "ellipse",  "Add an ellipse at the 2D cursor");
    leaf("Triangle",  "triangle", "Add a triangle at the 2D cursor");

    const bool open = UI::ContextMenu("##addMenu", addMenuPos_, entries, "Add");
    if (!open) addMenuOpen_ = false;
}

// The viewport right-click context menu. Same unconditional-render rule.
// viewportCtxNode_ is the object under the cursor (or null for empty space).
void Application::RenderViewportContextMenu() {
    if (!viewportCtxOpen_) return;
    Ink::Document* doc = project_.document.get();

    std::vector<UI::MenuEntry> entries;
    const bool onObject = viewportCtxNode_ != Ink::kNullNode && doc && doc->Find(viewportCtxNode_);
    const bool hasSel = !edit_.selection.empty();
    auto close = [this]{ viewportCtxOpen_ = false; };

    if (onObject) {
        const Ink::NodeId id = viewportCtxNode_;
        const bool isPath = doc->Find(id)->kind == Ink::NodeKind::Path;
        { UI::MenuEntry e; e.label = "Select"; e.enabled = !edit_.IsSelected(id);
          e.onClick = [this, id, close]{ edit_.SelectOnly(id); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect"; e.enabled = edit_.IsSelected(id);
          e.onClick = [this, id, close]{ edit_.Deselect(id); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Enter Edit Mode"; e.enabled = isPath;
          e.onClick = [this, id, close]{ edit_.SelectOnly(id); Action_EnterEditMode(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate"; e.shortcut = "Ctrl D";
          e.onClick = [this, close]{ Action_DuplicateSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Group"; e.shortcut = "Ctrl G"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_GroupSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Ungroup"; e.shortcut = "Ctrl Alt G";
          e.onClick = [this, close]{ Action_UngroupSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.shortcut = "X"; e.icon = "ink-eraser";
          e.onClick = [this, close]{ Action_DeleteSelection(); close(); };
          entries.push_back(std::move(e)); }
    } else {
        // Empty space: the Add submenu + selection helpers.
        UI::MenuEntry add; add.label = "Add";
        auto leaf = [&](const char* label, const char* kind) {
            UI::MenuEntry e; e.label = label;
            e.onClick = [this, kind, close]{ SpawnShape(kind); close(); };
            add.submenu.push_back(std::move(e));
        };
        leaf("Rectangle", "rect"); leaf("Ellipse", "ellipse"); leaf("Triangle", "triangle");
        entries.push_back(std::move(add));
        { UI::MenuEntry e; e.label = "Select All"; e.shortcut = "A";
          e.onClick = [this, close]{ Action_SelectAll(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect All"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_DeselectAll(); close(); };
          entries.push_back(std::move(e)); }
    }

    const bool open = UI::ContextMenu("##viewportCtx", viewportCtxPos_, entries, "Object");
    if (!open) viewportCtxOpen_ = false;
}

} // namespace App

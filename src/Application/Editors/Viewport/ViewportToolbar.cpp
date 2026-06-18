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
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace App {

void Application::RenderToolbarInto(ImVec2 origin, EditorState& st) {
    auto& ds      = DesignSystem::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    auto& sm      = Shortcuts::ShortcutManager::Instance();
    auto& tm      = Shortcuts::Tools::ToolManager::Instance();

    const float gs   = ds.GetGlobalScale();
    const float kBtn = 26.0f * gs;
    const float kPad = 4.0f  * gs;

    // Data-driven: list every tool registered in ToolManager (drawing tools +
    // hand), so the palette stays in sync with the shortcut system.
    std::vector<const Shortcuts::Tools::ToolDef*> toolDefs = tm.GetAllTools();
    const std::string activeTool = tm.GetActiveTool();
    // A tool is hidden in some contexts (Edit-Mode-only tools in Object Mode, the
    // Curve tool when a module forbids free drawing). Mirror the draw-loop's skip
    // logic so the container height matches what is actually shown.
    // Two distinct tool SETS, one per interaction mode (Blender-style). The active
    // tool is remembered per mode (objectModeTool_ / editToolByObject_).
    //   Object Mode: Select, 2D Cursor, Curve (authors a new object), Line Mark.
    //   Edit Mode:   Select, 2D Cursor, Curve (continue/branch), Extrude, Line Mark.
    const bool edit = (editorMode_ == EditorMode::Edit);
    auto toolVisible = [&](const Shortcuts::Tools::ToolDef* t) {
        if (t->id == "tool.extrude") return edit;          // Edit-only
        if (t->id == "tool.curve" && !activeCapabilities_.curveTool) return false;
        return true;                                        // shared in both sets
    };
    int rows = 0;
    for (const Shortcuts::Tools::ToolDef* t : toolDefs) if (toolVisible(t)) ++rows;
    const float w = kBtn + kPad * 2.0f;
    const float h = (float)rows * kBtn + (float)(rows + 1) * kPad;

    // Publish the palette's screen rect so the canvas hover-test can exclude it
    // (a click on a tool button must not also fall through to the canvas).
    st.overlayRects.push_back(ImVec4(origin.x + kPad, origin.y + kPad,
                                     origin.x + kPad + w, origin.y + kPad + h));

    ImGui::SetCursorScreenPos(ImVec2(origin.x + kPad, origin.y + kPad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f * gs);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("##FloatTools", ImVec2(w, h), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    {
        DesignSystem::DesignSystem::ZoneStyle zone("viewport/tools",
                                                   "Viewport tools");
        const std::string& active = activeTool;
        ImVec4 bg   = ds.GetColor(DesignSystem::Tok::C_IconButton_Background);
        ImVec4 hov  = ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover);
        ImVec4 acc  = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);
        ImVec4 tint = ds.GetColor(DesignSystem::Tok::S_Color_Text_Default);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        // The tool buttons must NOT be keyboard-nav targets: otherwise pressing
        // Tab (which the shortcut system uses to toggle Object/Edit mode) would
        // also move ImGui's keyboard focus onto a tool button.
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        for (const Shortcuts::Tools::ToolDef* t : toolDefs) {
            if (!toolVisible(t)) continue;   // hidden in this mode/module
            bool seld = (active == t->id);
            ImGui::PushID(t->id.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button,        seld ? acc : bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, seld ? acc : hov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  acc);
            bool clk = ImGui::Button("##b", ImVec2(kBtn, kBtn));
            ImGui::PopStyleColor(3);
            // Tool tooltip via the shared styled widget, after the normal hover
            // delay. Name + bound shortcut on one line.
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                std::string tip = t->name;
                if (!t->actionIds.empty()) {
                    std::string s = sm.GetShortcutString(t->actionIds.front());
                    if (!s.empty()) tip += "   (" + s + ")";
                }
                UI::DrawTooltip(tip.c_str(), ImGui::GetIO().MousePos);
            }
            float isz = kBtn * 0.62f;
            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 ipos = { bmin.x + (kBtn - isz) * 0.5f,
                            bmin.y + (kBtn - isz) * 0.5f };
            auto md = iconMgr.GetDefaultMetadata(t->iconId);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            iconMgr.RenderIcon(ImGui::GetWindowDrawList(), t->iconId, ipos, isz, md);
            ImGui::PopID();
            if (clk) Action_ActivateNamedTool(t->id);
        }
        ImGui::PopItemFlag();   // NoNav
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

// Snap option label tables (file scope so the dropdown body lambda can read them).
static const char* kSnapModeNames[] = { "Increment","Grid","Vertex","Edge","Face","Edge Center" };
// "Pivot" is what Blender calls "Center" here — it snaps the transform pivot point.
static const char* kSnapBaseNames[] = { "Closest","Pivot","Median","Active" };

// ── Snap group widget: a magnet TOGGLE button FUSED to a Snap dropdown ───────
// Built on UI::Dropdown: the magnet is a linked button (ButtonGroup look) on the
// trigger's LEFT; the dropdown's CUSTOM BODY holds Snap To (radio), Snap Base +
// Affect (ButtonGroups) and the rotation increments. All token-styled, same menu
// chrome as every other dropdown. Magnet on = snap always; else hold Ctrl.
void Application::DrawSnapWidget(ImVec2 pos, float widthPx) {
    using DesignSystem::Tok;
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    ImGui::SetCursorPos(pos);
    UI::DropdownConfig cfg;
    cfg.id = "##snap";
    cfg.triggerIcon = "background-dot-small";
    cfg.triggerLabel = kSnapModeNames[(int)snap_.mode];
    // Magnet linked button on the LEFT (accented when always-on snap is enabled).
    UI::DropdownButton mag;
    mag.id = "magnet"; mag.icon = "background-dot-small"; mag.active = snap_.enabled;
    mag.side = UI::DropdownButton::Side::Left;
    mag.tooltip = snap_.enabled ? "Snapping ON (click to disable; Ctrl snaps one drag)"
                                : "Snapping OFF (click to enable; hold Ctrl to snap a drag)";
    cfg.buttons.push_back(mag);
    // Custom body sized to fit the options.
    const float bodyW = 230.0f * gs;
    const float bodyH = 250.0f * gs;
    cfg.menuSize = ImVec2(bodyW, bodyH);
    cfg.bodyDraw = [this, &ds, gs, bodyW]() {
        using DesignSystem::Tok;
        auto subtle = [&](const char* s){
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted(s); ImGui::PopStyleColor();
        };
        // Snap To (radio list).
        subtle("Snap To");
        for (int i = 0; i < 6; ++i)
            if (ImGui::RadioButton(kSnapModeNames[i], (int)snap_.mode == i))
                snap_.mode = (SnapSettings::Mode)i;
        ImGui::Separator();
        // Snap Base — a 4-cell ButtonGroup (clicking doesn't close the menu: these
        // are real ImGui::Buttons inside the popup, so the popup stays open).
        subtle("Snap Base");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 3.0f) / 4.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapbase");
            g.SetGrid({ cellW, cellW, cellW, cellW }, { cellH });
            for (int i = 0; i < 4; ++i)
                g.AddCell(kSnapBaseNames[i], i, 0, 1, 1, (int)snap_.base == i);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex >= 0) snap_.base = (SnapSettings::Base)r.clickedIndex;
        }
        ImGui::Separator();
        // Affect — a 3-cell ButtonGroup, multi-select (each toggles independently).
        subtle("Affect");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 2.0f) / 3.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapaffect");
            g.SetGrid({ cellW, cellW, cellW }, { cellH });
            g.AddCell("Move",   0, 0, 1, 1, snap_.affectMove);
            g.AddCell("Rotate", 1, 0, 1, 1, snap_.affectRotate);
            g.AddCell("Scale",  2, 0, 1, 1, snap_.affectScale);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex == 0) snap_.affectMove   = !snap_.affectMove;
            if (r.clickedIndex == 1) snap_.affectRotate = !snap_.affectRotate;
            if (r.clickedIndex == 2) snap_.affectScale  = !snap_.affectScale;
        }
        ImGui::Separator();
        subtle("Rotation Increment");
        ImGui::SetNextItemWidth(90 * gs);
        ImGui::InputFloat("Increment##rot", &snap_.rotIncrement, 0, 0, "%.1f\xC2\xB0");
        ImGui::SetNextItemWidth(90 * gs);
        ImGui::InputFloat("Precision (Shift)##rotprec", &snap_.rotPrecisionIncrement, 0, 0, "%.1f\xC2\xB0");
    };
    UI::DropdownResult r = UI::Dropdown(cfg);
    if (r.buttonClicked == 0) snap_.enabled = !snap_.enabled;   // magnet toggled
    (void)widthPx;
}

} // namespace App

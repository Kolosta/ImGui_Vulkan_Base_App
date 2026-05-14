#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace App {

// ── Menu bar ─────────────────────────────────────────────────────────────────

void Application::RenderMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    auto& sm = Shortcuts::ShortcutManager::Instance();

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New",  sm.GetShortcutString("action.new").c_str()))
            TestAction_NewFile();
        if (ImGui::MenuItem("Open", sm.GetShortcutString("action.open").c_str()))
            TestAction_OpenFile();
        if (ImGui::MenuItem("Save", sm.GetShortcutString("action.save").c_str()))
            TestAction_SaveFile();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", sm.GetShortcutString("action.quit").c_str()))
            TestAction_Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Paramètres", nullptr, showSettings_))
            showSettings_ = !showSettings_;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ── Full-screen layout container ──────────────────────────────────────────────

void Application::RenderMainLayout() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

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

    RenderToolbar();

    {
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImVec2      wPos = ImGui::GetWindowPos();
        float       sepX = wPos.x + toolbarWidth_;
        float       top  = wPos.y;
        float       bot  = top + ImGui::GetWindowHeight();
        dl->AddLine(ImVec2(sepX, top), ImVec2(sepX, bot),
                    ImGui::GetColorU32(ImGuiCol_Separator));
    }

    ImGui::SameLine(0.0f, 1.0f);
    RenderMainContent();

    ImGui::End();
}

// ── Left toolbar ──────────────────────────────────────────────────────────────

void Application::RenderToolbar() {
    auto& ds      = DesignSystem::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();

    // Hardcoded UI dimensions are now scaled by the design system's global
    // scale so the toolbar tracks semantic.scale.default just like every
    // token-driven element.
    const float globalScale = ds.GetGlobalScale();
    const float kButtonSize = 48.0f * globalScale;
    const float kSpacing    = 4.0f  * globalScale;
    const int   kNumTools   = 50;

    const float kSettingsAreaEst = kButtonSize + kSpacing * 5.0f + 2.0f * globalScale;

    float availH   = ImGui::GetContentRegionAvail().y;
    float toolsAvH = std::max(kButtonSize + kSpacing, availH - kSettingsAreaEst);
    int   perCol   = std::max(1, (int)((toolsAvH - kSpacing) / (kButtonSize + kSpacing)));
    int   neededCols = (kNumTools + perCol - 1) / perCol;
    int   numCols    = std::min(3, neededCols);
    bool  needsScroll = (neededCols > 3);

    float width   = (float)numCols * kButtonSize + (float)(numCols + 1) * kSpacing;
    toolbarWidth_ = width;

    ImVec4 toolbarBg = ImVec4(0.13f, 0.13f, 0.15f, 1.0f);
    try { toolbarBg = ds.GetColor("semantic.color.surface"); } catch (...) {}

    ImGui::PushStyleColor(ImGuiCol_ChildBg,          toolbarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(kSpacing, kSpacing));

    if (ImGui::BeginChild("##Toolbar", ImVec2(width, 0.0f), false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse))
    {
        Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
            "##Toolbar", Shortcuts::ShortcutZone::Toolbar);

        ImVec4 accentColor = ImVec4(0.3f, 0.5f, 0.9f, 1.0f);
        try { accentColor = ds.GetColor("semantic.color.primary"); } catch (...) {}

        static int selectedTool = -1;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * globalScale);

        float settingsAreaH =
            kSpacing
            + std::max(1.0f, 1.0f * globalScale)   // separator (>=1px so it stays visible)
            + kSpacing * 2.0f
            + kSpacing
            + kButtonSize;
        float toolScrollH = std::max(0.0f,
            ImGui::GetContentRegionAvail().y - settingsAreaH);

        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::max(2.0f, 4.0f * globalScale));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
            ImVec4(0.75f, 0.75f, 0.75f, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
            ImVec4(0.85f, 0.85f, 0.85f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
            ImVec4(1.00f, 1.00f, 1.00f, 0.90f));

        ImGuiWindowFlags scrollFlags = needsScroll
            ? ImGuiWindowFlags_None
            : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (ImGui::BeginChild("##ToolScroll",
                              ImVec2(0.0f, toolScrollH), false, scrollFlags))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::SetCursorPosY(kSpacing);

            for (int i = 0; i < kNumTools; ++i) {
                int col = i % numCols;
                if (col == 0)
                    ImGui::SetCursorPosX(kSpacing);
                else
                    ImGui::SameLine(0.0f, kSpacing);

                const int  toolId     = i + 1;
                const bool isSelected = (selectedTool == toolId);

                ImGui::PushID(i);

                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        accentColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentColor);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accentColor);
                }

                if (ImGui::Button("##tool", ImVec2(kButtonSize, kButtonSize))) {
                    selectedTool = toolId;
                    if (toolId == 1)      TestAction_Tool1();
                    else if (toolId == 2) TestAction_Tool2();
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Tool %d", toolId);
                    ImGui::TextDisabled("Click to activate");
                    ImGui::TextDisabled("Right-click for options");
                    ImGui::EndTooltip();
                }

                if (ImGui::BeginPopupContextItem()) {
                    ImGui::Text("Tool %d Options", toolId);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Option A")) {}
                    if (ImGui::MenuItem("Option B")) {}
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset to default")) {}
                    ImGui::EndPopup();
                }

                const float  iconSz  = kButtonSize * 0.60f;
                const ImVec2 btnMin  = ImGui::GetItemRectMin();
                const ImVec2 iconPos = {
                    btnMin.x + (kButtonSize - iconSz) * 0.5f,
                    btnMin.y + (kButtonSize - iconSz) * 0.5f
                };
                const char* iconName = (toolId % 2 == 1) ? "tool1" : "tool2";
                iconMgr.RenderIcon(dl, iconName, iconPos, iconSz,
                                   iconMgr.GetDefaultMetadata(iconName));

                if (isSelected) ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); // ##ToolScroll

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(1);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetCursorPosX((width - kButtonSize) * 0.5f);

        ImDrawList* settingsDL = ImGui::GetWindowDrawList();

        ImGui::PushID("settings");
        if (ImGui::Button("##settings", ImVec2(kButtonSize, kButtonSize))) {
            showSettings_ = !showSettings_;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Paramètres");

        {
            const float  iconSz  = kButtonSize * 0.60f;
            const ImVec2 btnMin  = ImGui::GetItemRectMin();
            const ImVec2 iconPos = {
                btnMin.x + (kButtonSize - iconSz) * 0.5f,
                btnMin.y + (kButtonSize - iconSz) * 0.5f
            };
            iconMgr.RenderIcon(settingsDL, "settings", iconPos, iconSz,
                               iconMgr.GetDefaultMetadata("settings"));
        }
        ImGui::PopID();

        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild(); // ##Toolbar

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Right scrollable content area ─────────────────────────────────────────────

void Application::RenderMainContent() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("##MainContent", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Icon Test Lab")) {
        ImGui::PushID("sec_icons");
        ImGui::Indent(8.0f);
        RenderSectionIconTestLab();
        ImGui::Unindent(8.0f);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Design System Example")) {
        ImGui::PushID("sec_design");
        ImGui::Indent(8.0f);
        RenderSectionDesignExample();
        ImGui::Unindent(8.0f);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Theme Preview")) {
        ImGui::PushID("sec_theme");
        ImGui::Indent(8.0f);
        RenderSectionThemePreview();
        ImGui::Unindent(8.0f);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Test Zone 1")) {
        ImGui::PushID("sec_zone1");
        ImGui::Indent(8.0f);
        RenderSectionTestZone1();
        ImGui::Unindent(8.0f);
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Test Zone 2")) {
        ImGui::PushID("sec_zone2");
        ImGui::Indent(8.0f);
        RenderSectionTestZone2();
        ImGui::Unindent(8.0f);
        ImGui::PopID();
    }

    ImGui::EndChild(); // ##MainContent
}

} // namespace App
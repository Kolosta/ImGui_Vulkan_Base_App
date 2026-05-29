#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {
void Application::RenderMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    auto& sm = Shortcuts::ShortcutManager::Instance();
    auto sc = [&](const char* id) { return sm.GetShortcutString(id); };

    if (ImGui::BeginMenu("File")) {
        if (UI::IconMenuItem("new",  "New",  sc("file.new").c_str()))
            Action_NewFile();
        if (UI::IconMenuItem("open", "Open", sc("file.open").c_str()))
            Action_OpenFile();
        if (UI::IconMenuItem("save", "Save", sc("file.save").c_str()))
            Action_SaveFile();
        ImGui::Separator();
        // No matching icon for Quit → slot stays blank (never a checkmark).
        if (UI::IconMenuItem("close", "Quit", sc("app.quit").c_str()))
            Action_Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        // Settings uses the settings icon, and its open-state is shown as a
        // row highlight (selected) — not a checkmark.
        if (UI::IconMenuItem("settings", "Settings",
                             sc("app.toggleSettings").c_str(), showSettings_))
            Action_ToggleSettings();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows")) {
        if (UI::IconMenuItem("checklist", "Dev Test Window", nullptr,
                             showDevWindow_))
            showDevWindow_ = !showDevWindow_;
        if (UI::IconMenuItem("", "ImGui Demo",
                             sc("view.toggleDemo").c_str(), showImGuiDemo_))
            Action_ToggleImGuiDemo();
        ImGui::EndMenu();
    }

    // ── Project tabs, pinned to the far right ───────────────────────────
    // [ current project title ] [ + ]. The title shows "* (unsaved)" until
    // the project is saved, then "name" / "name *" by dirty state. "+"
    // starts a fresh empty project. (Multi-project tabs come with save/open.)
    {
        auto& ds = DesignSystem::DesignSystem::Instance();
        std::string title = project_.TabTitle();
        float plusW = ImGui::GetFrameHeight();
        float pad   = ImGui::GetStyle().ItemSpacing.x * 2.0f;
        float tabW  = ImGui::CalcTextSize(title.c_str()).x + pad * 2.0f;
        float right = ImGui::GetWindowWidth();
        ImGui::SameLine(right - (tabW + plusW + pad));

        ImGui::PushStyleColor(ImGuiCol_Button,
                              ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
        ImGui::Button(title.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Current project%s",
                              project_.dirty ? " — unsaved changes" : "");
        ImGui::SameLine(0.0f, pad);
        if (ImGui::Button("+", ImVec2(plusW, 0.0f)))
            Action_NewProject();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New project");
        ImGui::PopStyleColor(2);
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

    // Reserve space for the bottom status bar. ImGui inserts ItemSpacing.y
    // between ##LayoutBody and the status-bar child, so subtract it too —
    // otherwise the status bar is pushed ItemSpacing.y past the window bottom
    // and its content clips below the screen edge.
    const float statusBarHeight = UI::StatusBar::Height();
    const float itemSpacingY    = ImGui::GetStyle().ItemSpacing.y;
    const float layoutHeight =
        ImGui::GetWindowHeight() - statusBarHeight - itemSpacingY;

    if (ImGui::BeginChild("##LayoutBody",
                          ImVec2(0.0f, layoutHeight),
                          false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {
        // Blender-style fixed 3-zone layout (no native docking UX). The tool
        // palette is no longer a global left strip — it floats inside the
        // Viewport editor. Each zone renders the editor it was assigned.
        zoneLayout_.Render(
            // drawEditor: per-leaf content + its own EditorState.
            [this](EditorKind kind, ImVec2 sz, EditorState& st) {
                switch (kind) {
                    case EditorKind::Viewport:
                        RenderViewport(sz, st);
                        break;
                    case EditorKind::Outliner:
                        RenderOutliner();
                        break;
                    case EditorKind::Timeline:
                        ImGui::TextDisabled("Timeline");
                        ImGui::TextDisabled("(animation timeline — later)");
                        break;
                    case EditorKind::DevPanels:
                        // Same panels as the Dev Test Window, inline in a zone.
                        RenderSectionDesignExample();
                        break;
                    default:
                        break;
                }
            },
            // topBarExtras: the new-document icon button, Viewport only.
            [this](EditorKind kind, EditorState& st) {
                if (kind != EditorKind::Viewport) return;
                auto& ds      = DesignSystem::DesignSystem::Instance();
                auto& iconMgr = VectorGraphics::IconManager::Instance();
                const float gs  = ds.GetGlobalScale();
                // Control-height square button with a 16px icon (the icon size
                // = control-height − 2× the button's vertical inset).
                const float h   = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gs;
                const float isz = ds.GetFloat(DesignSystem::Tok::C_Dropdown_IconSize) * gs;
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ds.GetColor(DesignSystem::Tok::C_IconButton_Background));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                bool clicked = ImGui::Button("##newdoc", ImVec2(h, h));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                // Draw the "new" icon centred on the button.
                ImVec2 bmin = ImGui::GetItemRectMin();
                ImVec2 ipos = { bmin.x + (h - isz) * 0.5f,
                                bmin.y + (h - isz) * 0.5f };
                ImVec4 tint = ds.GetColor(DesignSystem::Tok::S_Color_Text_Default);
                auto md = iconMgr.GetDefaultMetadata("new");
                if (!md.colorZones.empty()) {
                    md.colorZones[0].customColor = tint;
                    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), "new",
                                       ipos, isz, md);
                } else {
                    // Fallback: draw a "+" text glyph if icon not loaded.
                    ImVec2 ts = ImGui::CalcTextSize("+");
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(bmin.x + (h - ts.x) * 0.5f,
                               bmin.y + (h - ts.y) * 0.5f),
                        ImGui::GetColorU32(tint), "+");
                }
                if (clicked) st.openNewDoc = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("New document (Ctrl+Shift+N)");
                (void)gs;
            });
    }
    ImGui::EndChild();

    RenderStatusBar();

    ImGui::End();
}

void Application::RenderStatusBar() {
    UI::StatusBar::Render(kVersion);
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

        // The real tools. Adding one = one row here (id, icon, shortcut,
        // tooltip, action index). No more 50 fake buttons.
        struct ToolDef {
            const char* key;       // ToolManager id
            const char* icon;      // icon id (file stem after reorg)
            const char* tip;       // tooltip label
            const char* shortcut;  // shortcut action id
            int         action;    // 1 = brush, 2 = eraser
        };
        const ToolDef tools[] = {
            { "tool.brush",  "pen",        "Brush",  "tool.brush.activate",  1 },
            { "tool.eraser", "ink-eraser", "Eraser", "tool.eraser.activate", 2 },
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
                           activeTool == t.key)) {
                if (t.action == 1)      Action_ActivateTool1();
                else if (t.action == 2) Action_ActivateTool2();
            }
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

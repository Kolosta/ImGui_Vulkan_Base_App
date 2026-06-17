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
void Application::RenderFloatingWindows() {
    // NOTE: the Preferences window is NOT drawn here — it lives in its own OS
    // window + ImGui context, rendered by settingsHost_.RenderFrame() from
    // Application::RenderFrame(). RenderSettings() is kept as a no-op shim.

    // Old Design System / Shortcuts / Icons editor, kept as a classic window.
    if (showDesignSystem_)
        RenderDesignSystemWindow();

    RenderDevTestWindow();   // self-gated by showDevWindow_

    if (showImGuiDemo_)
        ImGui::ShowDemoWindow(&showImGuiDemo_);
}

// Preferences is rendered by settingsHost_ in its own context (see RenderFrame).
void Application::RenderSettings() {}

// ─────────────────────────────────────────────────────────────────────────────
//  Design System window - the former Settings content (three editors in tabs),
//  moved verbatim into its own classic ImGui window.
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderDesignSystemWindow() {
    ImGui::SetNextWindowSize(ImVec2(920.0f, 660.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(120.0f, 80.0f),   ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("Design System", &showDesignSystem_, kFlags)) {
        ImGui::End();
        return;
    }

    // Scope the whole Settings window contents: every token read inside
    // resolves through "settings" first, then global. The Settings theme-def
    // recolours its Buttons in NOTICE to prove the scope is wired live.
    {
        DesignSystem::DesignSystem::ZoneStyle zone("settings",
                                                   "Settings window");

        if (ImGui::BeginTabBar("##SettingsTabs")) {

            if (ImGui::BeginTabItem("Design System")) {
                // Sub-scope per tab so a tab can be re-themed independently.
                DesignSystem::DesignSystem::ZoneStyle sub(
                    "settings/designSystem", "Design System tab");
                auto& ds = DesignSystem::DesignSystem::Instance();
                tokenEditor_.RenderContent(ds.GetCurrentContext(),
                                           ds.GetOverrideManager());
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Shortcuts")) {
                DesignSystem::DesignSystem::ZoneStyle sub(
                    "settings/shortcuts", "Shortcuts tab");
                shortcutEditor_.RenderContent();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Icons")) {
                DesignSystem::DesignSystem::ZoneStyle sub(
                    "settings/icons", "Icons tab");
                iconEditor_.RenderContent();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();

    shortcutEditor_.RenderCapturePopup();
}


} // namespace App

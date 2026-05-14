// #include "Application.h"
// #include <DesignSystem/DesignSystem.h>
// #include <Shortcuts/ShortcutManager.h>
// #include <VectorGraphics/IconManager.h>

// namespace App {

// // ─────────────────────────────────────────────────────────────────────────────
// //  Inline content sections  (no ImGui::Begin / End — rendered inside the
// //  scrollable child from RenderMainContent)
// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderSectionIconTestLab() {
//     auto& iconManager = VectorGraphics::IconManager::Instance();

//     ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
//     ImGui::TextUnformatted("These icons use semantic.icon.color.primary/secondary");
//     ImGui::TextUnformatted("CSS classes in SVG determine which elements use which token");

//     ImGui::Spacing();
//     ImGui::Text("Settings:");
//     iconManager.RenderIcon("settings", 32.0f);
//     ImGui::SameLine();
//     iconManager.RenderIcon("settings", 48.0f);

//     ImGui::Text("Tool1:");
//     iconManager.RenderIcon("tool1", 32.0f);
//     ImGui::SameLine();
//     iconManager.RenderIcon("tool1", 48.0f);

//     ImGui::Text("Tool2:");
//     iconManager.RenderIcon("tool2", 32.0f);
//     ImGui::SameLine();
//     iconManager.RenderIcon("tool2", 48.0f);

//     ImGui::Spacing();
//     ImGui::Separator();
//     ImGui::Spacing();

//     ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
//     ImGui::TextUnformatted("These icons keep their original SVG colors");

//     ImGui::Text("Three Balls:");
//     iconManager.RenderIcon("three_balls", 64.0f);

//     ImGui::Text("Logo Carto:");
//     iconManager.RenderIcon("logo_carto", 64.0f);

//     ImGui::Spacing();
//     ImGui::Separator();
//     ImGui::Spacing();

//     ImGui::SeparatorText("CUSTOM COLORS");

//     static ImVec4 customPrimary   = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
//     static ImVec4 customSecondary = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);

//     ImGui::ColorEdit4("Custom Primary",   (float*)&customPrimary);
//     ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);

//     static VectorGraphics::IconMetadata customMetadata;
//     static bool metadataInitialized = false;

//     if (!metadataInitialized) {
//         customMetadata = iconManager.GetDefaultMetadata("tool1");
//         customMetadata.scheme = VectorGraphics::IconColorScheme::Multicolor;
//         if (customMetadata.colorZones.size() >= 2) {
//             customMetadata.colorZones[0].customColor = customPrimary;
//             customMetadata.colorZones[1].customColor = customSecondary;
//         }
//         metadataInitialized = true;
//     }

//     if (customMetadata.colorZones.size() >= 2) {
//         customMetadata.colorZones[0].customColor = customPrimary;
//         customMetadata.colorZones[1].customColor = customSecondary;
//     }

//     ImGui::Text("Tool1 with custom colors:");
//     iconManager.RenderIcon("tool1", 64.0f, customMetadata);

//     ImGui::Spacing();
// }

// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderSectionDesignExample() {
//     // Zone registration: wrap in a bordered child so focus-based shortcuts work
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
//     ImGui::BeginChild("DesignExampleZone",
//                       ImVec2(0.0f, 90.0f), true,
//                       ImGuiWindowFlags_None);
//     ImGui::PopStyleVar();

//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
//         "DesignExampleZone", Shortcuts::ShortcutZone::DesignExample);

//     ImGui::TextUnformatted("This section uses the Design System!");

//     auto& ds = DesignSystem::DesignSystem::Instance();
//     ds.PushAllStyles();

//     static int int_value = 50;
//     ImGui::TextUnformatted("Drag to adjust:");
//     ImGui::SetNextItemWidth(150.0f);
//     ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
//     ImGui::SameLine();
//     if (ImGui::Button("Print Value", ImVec2(120.0f, 0.0f)))
//         printf("Value: %d\n", int_value);

//     ds.PopAllStyles();

//     ImGui::EndChild();
//     ImGui::Spacing();
// }

// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderSectionThemePreview() {
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
//     ImGui::BeginChild("ThemePreviewZone",
//                       ImVec2(0.0f, 160.0f), true,
//                       ImGuiWindowFlags_None);
//     ImGui::PopStyleVar();

//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
//         "ThemePreviewZone", Shortcuts::ShortcutZone::ThemePreview);

//     ImGui::TextUnformatted("Preview with current context");
//     ImGui::Separator();

//     auto& ds = DesignSystem::DesignSystem::Instance();
//     try {
//         ImVec4 bgColor      = ds.GetColor("semantic.color.background");
//         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

//         ImGui::ColorButton("##bg", bgColor,
//                            ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
//         ImGui::SameLine();
//         ImGui::TextUnformatted("Background Color");

//         ImGui::ColorButton("##primary", primaryColor,
//                            ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
//         ImGui::SameLine();
//         ImGui::TextUnformatted("Primary Color");

//         ImGui::Separator();
//         ImGui::TextUnformatted("Sample Components:");

//         if (ImGui::Button("Sample Button")) {}

//         static char textBuffer[128] = "Sample Input";
//         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

//     } catch (const std::exception& e) {
//         ImGui::Text("Error: %s", e.what());
//     }

//     ImGui::EndChild();
//     ImGui::Spacing();
// }

// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderSectionTestZone1() {
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
//     ImGui::BeginChild("TestZone1Child",
//                       ImVec2(0.0f, 90.0f), true,
//                       ImGuiWindowFlags_None);
//     ImGui::PopStyleVar();

//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
//         "TestZone1Child", Shortcuts::ShortcutZone::TestZone1);

//     ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
//     ImGui::TextUnformatted("Press Ctrl+A to trigger Zone 1 action.");

//     if (ImGui::Button("Trigger Zone 1 Action"))
//         TestAction_Zone1();

//     ImGui::EndChild();
//     ImGui::Spacing();
// }

// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderSectionTestZone2() {
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
//     ImGui::BeginChild("TestZone2Child",
//                       ImVec2(0.0f, 100.0f), true,
//                       ImGuiWindowFlags_None);
//     ImGui::PopStyleVar();

//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
//         "TestZone2Child", Shortcuts::ShortcutZone::TestZone2);

//     ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
//     ImGui::TextUnformatted("Press Ctrl+A for Zone 2 action.");
//     ImGui::TextUnformatted("(same shortcut as Zone 1, different action)");

//     if (ImGui::Button("Trigger Zone 2 Action"))
//         TestAction_Zone2();

//     ImGui::EndChild();
//     ImGui::Spacing();
// }

// // ─────────────────────────────────────────────────────────────────────────────
// //  Floating windows  (free-floating, can dock to each other if desired)
// // ─────────────────────────────────────────────────────────────────────────────

// void Application::RenderFloatingWindows() {
//     if (showTokenEditor_) {
//         auto& ds = DesignSystem::DesignSystem::Instance();
//         tokenEditor_.Render(ds.GetCurrentContext(), ds.GetOverrideManager());
//     }

//     if (showShortcutEditor_)
//         shortcutEditor_.Render(&showShortcutEditor_);

//     if (showIconEditor_)
//         iconEditor_.Render(&showIconEditor_);

//     if (showImGuiDemo_)
//         ImGui::ShowDemoWindow(&showImGuiDemo_);
// }

// } // namespace App



// // #include "Application.h"
// // #include <DesignSystem/DesignSystem.h>
// // #include <Shortcuts/ShortcutManager.h>
// // #include <VectorGraphics/IconManager.h>

// // namespace App {

// // // ─────────────────────────────────────────────────────────────────────────────
// // //  Sections inline (pas de Begin/End propre — rendues dans RenderMainContent)
// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSectionIconTestLab() {
// //     auto& iconManager = VectorGraphics::IconManager::Instance();

// //     ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
// //     ImGui::TextUnformatted("These icons use semantic.icon.color.primary/secondary");
// //     ImGui::TextUnformatted("CSS classes in SVG determine which elements use which token");

// //     ImGui::Spacing();
// //     ImGui::Text("Settings:");
// //     iconManager.RenderIcon("settings", 32.0f);
// //     ImGui::SameLine();
// //     iconManager.RenderIcon("settings", 48.0f);

// //     ImGui::Text("Tool1:");
// //     iconManager.RenderIcon("tool1", 32.0f);
// //     ImGui::SameLine();
// //     iconManager.RenderIcon("tool1", 48.0f);

// //     ImGui::Text("Tool2:");
// //     iconManager.RenderIcon("tool2", 32.0f);
// //     ImGui::SameLine();
// //     iconManager.RenderIcon("tool2", 48.0f);

// //     ImGui::Spacing();
// //     ImGui::Separator();
// //     ImGui::Spacing();

// //     ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
// //     ImGui::TextUnformatted("These icons keep their original SVG colors");

// //     ImGui::Text("Three Balls:");
// //     iconManager.RenderIcon("three_balls", 64.0f);

// //     ImGui::Text("Logo Carto:");
// //     iconManager.RenderIcon("logo_carto", 64.0f);

// //     ImGui::Spacing();
// //     ImGui::Separator();
// //     ImGui::Spacing();

// //     ImGui::SeparatorText("CUSTOM COLORS");

// //     static ImVec4 customPrimary   = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
// //     static ImVec4 customSecondary = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);

// //     ImGui::ColorEdit4("Custom Primary",   (float*)&customPrimary);
// //     ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);

// //     static VectorGraphics::IconMetadata customMetadata;
// //     static bool metadataInitialized = false;

// //     if (!metadataInitialized) {
// //         customMetadata = iconManager.GetDefaultMetadata("tool1");
// //         customMetadata.scheme = VectorGraphics::IconColorScheme::Multicolor;
// //         if (customMetadata.colorZones.size() >= 2) {
// //             customMetadata.colorZones[0].customColor = customPrimary;
// //             customMetadata.colorZones[1].customColor = customSecondary;
// //         }
// //         metadataInitialized = true;
// //     }

// //     if (customMetadata.colorZones.size() >= 2) {
// //         customMetadata.colorZones[0].customColor = customPrimary;
// //         customMetadata.colorZones[1].customColor = customSecondary;
// //     }

// //     ImGui::Text("Tool1 with custom colors:");
// //     iconManager.RenderIcon("tool1", 64.0f, customMetadata);

// //     ImGui::Spacing();
// // }

// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSectionDesignExample() {
// //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
// //     ImGui::BeginChild("DesignExampleZone", ImVec2(0.0f, 90.0f), true,
// //                       ImGuiWindowFlags_None);
// //     ImGui::PopStyleVar();

// //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
// //         "DesignExampleZone", Shortcuts::ShortcutZone::DesignExample);

// //     ImGui::TextUnformatted("This section uses the Design System!");

// //     auto& ds = DesignSystem::DesignSystem::Instance();
// //     ds.PushAllStyles();

// //     static int int_value = 50;
// //     ImGui::TextUnformatted("Drag to adjust:");
// //     ImGui::SetNextItemWidth(150.0f);
// //     ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
// //     ImGui::SameLine();
// //     if (ImGui::Button("Print Value", ImVec2(120.0f, 0.0f)))
// //         printf("Value: %d\n", int_value);

// //     ds.PopAllStyles();

// //     ImGui::EndChild();
// //     ImGui::Spacing();
// // }

// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSectionThemePreview() {
// //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
// //     ImGui::BeginChild("ThemePreviewZone", ImVec2(0.0f, 160.0f), true,
// //                       ImGuiWindowFlags_None);
// //     ImGui::PopStyleVar();

// //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
// //         "ThemePreviewZone", Shortcuts::ShortcutZone::ThemePreview);

// //     ImGui::TextUnformatted("Preview with current context");
// //     ImGui::Separator();

// //     auto& ds = DesignSystem::DesignSystem::Instance();
// //     try {
// //         ImVec4 bgColor      = ds.GetColor("semantic.color.background");
// //         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

// //         ImGui::ColorButton("##bg", bgColor,
// //                            ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
// //         ImGui::SameLine();
// //         ImGui::TextUnformatted("Background Color");

// //         ImGui::ColorButton("##primary", primaryColor,
// //                            ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
// //         ImGui::SameLine();
// //         ImGui::TextUnformatted("Primary Color");

// //         ImGui::Separator();
// //         ImGui::TextUnformatted("Sample Components:");

// //         if (ImGui::Button("Sample Button")) {}

// //         static char textBuffer[128] = "Sample Input";
// //         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

// //     } catch (const std::exception& e) {
// //         ImGui::Text("Error: %s", e.what());
// //     }

// //     ImGui::EndChild();
// //     ImGui::Spacing();
// // }

// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSectionTestZone1() {
// //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
// //     ImGui::BeginChild("TestZone1Child", ImVec2(0.0f, 90.0f), true,
// //                       ImGuiWindowFlags_None);
// //     ImGui::PopStyleVar();

// //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
// //         "TestZone1Child", Shortcuts::ShortcutZone::TestZone1);

// //     ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
// //     ImGui::TextUnformatted("Press Ctrl+A to trigger Zone 1 action.");

// //     if (ImGui::Button("Trigger Zone 1 Action"))
// //         TestAction_Zone1();

// //     ImGui::EndChild();
// //     ImGui::Spacing();
// // }

// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSectionTestZone2() {
// //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
// //     ImGui::BeginChild("TestZone2Child", ImVec2(0.0f, 100.0f), true,
// //                       ImGuiWindowFlags_None);
// //     ImGui::PopStyleVar();

// //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
// //         "TestZone2Child", Shortcuts::ShortcutZone::TestZone2);

// //     ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
// //     ImGui::TextUnformatted("Press Ctrl+A for Zone 2 action.");
// //     ImGui::TextUnformatted("(same shortcut as Zone 1, different action)");

// //     if (ImGui::Button("Trigger Zone 2 Action"))
// //         TestAction_Zone2();

// //     ImGui::EndChild();
// //     ImGui::Spacing();
// // }

// // // ─────────────────────────────────────────────────────────────────────────────
// // //  Fenêtres flottantes
// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderFloatingWindows() {
// //     if (showSettings_)
// //         RenderSettings();

// //     if (showImGuiDemo_)
// //         ImGui::ShowDemoWindow(&showImGuiDemo_);
// // }

// // // ─────────────────────────────────────────────────────────────────────────────
// // //  Fenêtre "Paramètres" — trois éditeurs en onglets
// // //
// // //  • NoDocking : ne peut pas être dockée dans une autre fenêtre, ni accepter
// // //    qu'une autre se docke en elle.
// // //  • Pas de NoMove : peut être déplacée hors de la fenêtre principale si
// // //    ImGuiConfigFlags_ViewportsEnable est activé dans ApplicationInit.cpp.
// // //  • La croix de fermeture est fournie par ImGui via &showSettings_.
// // // ─────────────────────────────────────────────────────────────────────────────

// // void Application::RenderSettings() {
// //     ImGui::SetNextWindowSize(ImVec2(920.0f, 660.0f), ImGuiCond_FirstUseEver);
// //     ImGui::SetNextWindowPos(ImVec2(120.0f, 80.0f),   ImGuiCond_FirstUseEver);

// //     constexpr ImGuiWindowFlags kFlags =
// //         ImGuiWindowFlags_NoDocking;

// //     if (!ImGui::Begin("Paramètres", &showSettings_, kFlags)) {
// //         ImGui::End();
// //         return;
// //     }

// //     if (ImGui::BeginTabBar("##SettingsTabs")) {

// //         // ── Onglet Design System ──────────────────────────────────────────
// //         if (ImGui::BeginTabItem("Design System")) {
// //             auto& ds = DesignSystem::DesignSystem::Instance();
// //             tokenEditor_.RenderContent(ds.GetCurrentContext(),
// //                                        ds.GetOverrideManager());
// //             ImGui::EndTabItem();
// //         }

// //         // ── Onglet Raccourcis ─────────────────────────────────────────────
// //         if (ImGui::BeginTabItem("Raccourcis")) {
// //             shortcutEditor_.RenderContent();
// //             ImGui::EndTabItem();
// //         }

// //         // ── Onglet Icônes ─────────────────────────────────────────────────
// //         if (ImGui::BeginTabItem("Icônes")) {
// //             iconEditor_.RenderContent();
// //             ImGui::EndTabItem();
// //         }

// //         ImGui::EndTabBar();
// //     }

// //     ImGui::End();

// //     // Le popup de capture de raccourci doit être rendu hors du Begin/End
// //     // de la fenêtre principale pour que ImGui le positionne correctement.
// //     shortcutEditor_.RenderCapturePopup();
// // }

// // } // namespace App




#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Sections inline (pas de Begin/End propre — rendues dans RenderMainContent)
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionIconTestLab() {
    auto& iconManager = VectorGraphics::IconManager::Instance();

    ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
    ImGui::TextUnformatted("These icons use semantic.icon.color.primary/secondary");
    ImGui::TextUnformatted("CSS classes in SVG determine which elements use which token");

    ImGui::Spacing();
    ImGui::Text("Settings:");
    iconManager.RenderIcon("settings", 32.0f);
    ImGui::SameLine();
    iconManager.RenderIcon("settings", 48.0f);

    ImGui::Text("Tool1:");
    iconManager.RenderIcon("tool1", 32.0f);
    ImGui::SameLine();
    iconManager.RenderIcon("tool1", 48.0f);

    ImGui::Text("Tool2:");
    iconManager.RenderIcon("tool2", 32.0f);
    ImGui::SameLine();
    iconManager.RenderIcon("tool2", 48.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
    ImGui::TextUnformatted("These icons keep their original SVG colors");

    ImGui::Text("Three Balls:");
    iconManager.RenderIcon("three_balls", 64.0f);

    ImGui::Text("Logo Carto:");
    iconManager.RenderIcon("logo_carto", 64.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SeparatorText("CUSTOM COLORS");

    static ImVec4 customPrimary   = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
    static ImVec4 customSecondary = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);

    ImGui::ColorEdit4("Custom Primary",   (float*)&customPrimary);
    ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);

    static VectorGraphics::IconMetadata customMetadata;
    static bool metadataInitialized = false;

    if (!metadataInitialized) {
        customMetadata = iconManager.GetDefaultMetadata("tool1");
        customMetadata.scheme = VectorGraphics::IconColorScheme::Multicolor;
        if (customMetadata.colorZones.size() >= 2) {
            customMetadata.colorZones[0].customColor = customPrimary;
            customMetadata.colorZones[1].customColor = customSecondary;
        }
        metadataInitialized = true;
    }

    if (customMetadata.colorZones.size() >= 2) {
        customMetadata.colorZones[0].customColor = customPrimary;
        customMetadata.colorZones[1].customColor = customSecondary;
    }

    ImGui::Text("Tool1 with custom colors:");
    iconManager.RenderIcon("tool1", 64.0f, customMetadata);

    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionDesignExample() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("DesignExampleZone", ImVec2(0.0f, 90.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
        "DesignExampleZone", Shortcuts::ShortcutZone::DesignExample);

    ImGui::TextUnformatted("This section uses the Design System!");

    auto& ds = DesignSystem::DesignSystem::Instance();
    ds.PushAllStyles();

    static int int_value = 50;
    ImGui::TextUnformatted("Drag to adjust:");
    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
    ImGui::SameLine();
    if (ImGui::Button("Print Value", ImVec2(120.0f, 0.0f)))
        printf("Value: %d\n", int_value);

    ds.PopAllStyles();

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionThemePreview() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("ThemePreviewZone", ImVec2(0.0f, 160.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
        "ThemePreviewZone", Shortcuts::ShortcutZone::ThemePreview);

    ImGui::TextUnformatted("Preview with current context");
    ImGui::Separator();

    auto& ds = DesignSystem::DesignSystem::Instance();
    try {
        ImVec4 bgColor      = ds.GetColor("semantic.color.background");
        ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

        ImGui::ColorButton("##bg", bgColor,
                           ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted("Background Color");

        ImGui::ColorButton("##primary", primaryColor,
                           ImGuiColorEditFlags_NoTooltip, ImVec2(50.0f, 25.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted("Primary Color");

        ImGui::Separator();
        ImGui::TextUnformatted("Sample Components:");

        if (ImGui::Button("Sample Button")) {}

        static char textBuffer[128] = "Sample Input";
        ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

    } catch (const std::exception& e) {
        ImGui::Text("Error: %s", e.what());
    }

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionTestZone1() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("TestZone1Child", ImVec2(0.0f, 90.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
        "TestZone1Child", Shortcuts::ShortcutZone::TestZone1);

    ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
    ImGui::TextUnformatted("Press Ctrl+A to trigger Zone 1 action.");

    if (ImGui::Button("Trigger Zone 1 Action"))
        TestAction_Zone1();

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionTestZone2() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("TestZone2Child", ImVec2(0.0f, 100.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterWindowZone(
        "TestZone2Child", Shortcuts::ShortcutZone::TestZone2);

    ImGui::TextUnformatted("Shortcuts active when this zone is focused.");
    ImGui::TextUnformatted("Press Ctrl+A for Zone 2 action.");
    ImGui::TextUnformatted("(same shortcut as Zone 1, different action)");

    if (ImGui::Button("Trigger Zone 2 Action"))
        TestAction_Zone2();

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fenêtres flottantes
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderFloatingWindows() {
    if (showSettings_)
        RenderSettings();

    if (showImGuiDemo_)
        ImGui::ShowDemoWindow(&showImGuiDemo_);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fenêtre "Paramètres" — trois éditeurs en onglets
//
//  • NoDocking : ne peut pas être dockée dans une autre fenêtre, ni accepter
//    qu'une autre se docke en elle.
//  • Pas de NoMove : peut être déplacée hors de la fenêtre principale si
//    ImGuiConfigFlags_ViewportsEnable est activé dans ApplicationInit.cpp.
//  • La croix de fermeture est fournie par ImGui via &showSettings_.
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSettings() {
    ImGui::SetNextWindowSize(ImVec2(920.0f, 660.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(120.0f, 80.0f),   ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("Paramètres", &showSettings_, kFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##SettingsTabs")) {

        // ── Onglet Design System ──────────────────────────────────────────
        if (ImGui::BeginTabItem("Design System")) {
            auto& ds = DesignSystem::DesignSystem::Instance();
            tokenEditor_.RenderContent(ds.GetCurrentContext(),
                                       ds.GetOverrideManager());
            ImGui::EndTabItem();
        }

        // ── Onglet Raccourcis ─────────────────────────────────────────────
        if (ImGui::BeginTabItem("Raccourcis")) {
            shortcutEditor_.RenderContent();
            ImGui::EndTabItem();
        }

        // ── Onglet Icônes ─────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Icônes")) {
            iconEditor_.RenderContent();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    // Le popup de capture de raccourci doit être rendu hors du Begin/End
    // de la fenêtre principale pour que ImGui le positionne correctement.
    shortcutEditor_.RenderCapturePopup();
}

} // namespace App
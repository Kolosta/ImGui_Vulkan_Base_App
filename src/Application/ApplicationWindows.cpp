// #include "Application.h"
// #include <DesignSystem/DesignSystem.h>
// #include <Shortcuts/ShortcutManager.h>
// #include <UI/IconManager.h>

// namespace App {

// void Application::RenderIconTestWindow() {
//     ImGui::Begin("Icon Test Lab");

//     ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
//     ImGui::Text("These icons use semantic.icon.color.primary & secondary");
    
//     ImGui::Text("Settings (bicolor):");
//     UI::IconManager::Instance().RenderIcon("settings", 32.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("settings", 48.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("settings", 64.0f);
    
//     ImGui::Text("Tool1 (bicolor):");
//     UI::IconManager::Instance().RenderIcon("tool1", 32.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("tool1", 48.0f);
    
//     ImGui::Text("Tool2 (bicolor):");
//     UI::IconManager::Instance().RenderIcon("tool2", 32.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("tool2", 48.0f);
    
//     ImGui::Separator();
    
//     ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
//     ImGui::Text("These icons keep their original SVG colors");
    
//     ImGui::Text("Three Balls:");
//     UI::IconManager::Instance().RenderIcon("three_balls", 64.0f);
    
//     ImGui::Text("Logo Carto:");
//     UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);
    
//     ImGui::Separator();
    
//     ImGui::SeparatorText("CUSTOM COLORS (Bicolor Override)");
    
//     ImGui::Text("Tool1 with custom bicolor:");
//     static ImVec4 customPrimary = ImVec4(0, 0.5f, 1, 1);
//     static ImVec4 customSecondary = ImVec4(1, 0.5f, 0, 1);
    
//     ImGui::ColorEdit4("Custom Primary", (float*)&customPrimary);
//     ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);
    
//     UI::IconManager::Instance().RenderIcon("tool1", 64.0f, customPrimary, customSecondary);
    
//     ImGui::Separator();
    
//     ImGui::SeparatorText("NEW API EXAMPLES");
//     ImGui::Text("Using the unified API with different modes");
    
//     ImGui::Text("Token-based (string tokens):");
//     UI::IconManager::Instance().RenderIcon(
//         "tool1", 48.0f,
//         "semantic.color.warning",
//         "semantic.color.background"
//     );
    
//     ImGui::Text("Direct colors (ImVec4):");
//     ImVec4 cyan(0, 1, 1, 1);
//     ImVec4 magenta(1, 0, 1, 1);
//     UI::IconManager::Instance().RenderIcon("tool1", 48.0f, cyan, magenta);
    
//     ImGui::Text("Auto-mode (uses metadata config):");
//     UI::IconManager::Instance().RenderIcon("settings", 48.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("three_balls", 48.0f);
    
//     ImGui::Separator();

//     ImGui::Text("Logo Sizes:");
//     UI::IconManager::Instance().RenderIcon("logo_carto", 16.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f);
//     ImGui::SameLine();
//     UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);

//     ImGui::End();
// }

// void Application::RenderDesignExample() {
//     ImGui::Begin("Design System Example", nullptr, ImGuiWindowFlags_NoCollapse);
//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Design System Example", 
//         Shortcuts::ShortcutZone::DesignExample);

//     ImGui::Text("This window uses the Design System!");

//     auto& ds = DesignSystem::DesignSystem::Instance();
//     ds.PushAllStyles();

//     static int int_value = 50;
//     ImGui::Text("Drag to adjust:");
//     ImGui::SetNextItemWidth(150.0f);
//     ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");

//     ImGui::SameLine();
//     if (ImGui::Button("Print Value", ImVec2(120, 0))) {
//         printf("Value: %d\n", int_value);
//     }

//     ds.PopAllStyles();

//     ImGui::End();
// }

// void Application::RenderThemePreview() {
//     ImGui::Begin("Theme Preview", nullptr, ImGuiWindowFlags_NoCollapse);
//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Theme Preview", 
//         Shortcuts::ShortcutZone::ThemePreview);

//     ImGui::Text("Preview with current context");
//     ImGui::Separator();

//     auto& ds = DesignSystem::DesignSystem::Instance();

//     try {
//         ImVec4 bgColor = ds.GetColor("semantic.color.background");
//         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

//         ImGui::ColorButton("##bg", bgColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
//         ImGui::SameLine();
//         ImGui::Text("Background Color");

//         ImGui::ColorButton("##primary", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
//         ImGui::SameLine();
//         ImGui::Text("Primary Color");

//         ImGui::Separator();
//         ImGui::Text("Sample Components:");

//         if (ImGui::Button("Sample Button")) {}

//         static char textBuffer[128] = "Sample Input";
//         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

//     } catch (const std::exception& e) {
//         ImGui::Text("Error resolving tokens: %s", e.what());
//     }

//     ImGui::End();
// }

// void Application::RenderTestZone1() {
//     ImGui::Begin("Test Zone 1", nullptr, ImGuiWindowFlags_NoCollapse);
//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 1", 
//         Shortcuts::ShortcutZone::TestZone1);

//     ImGui::Text("Test shortcuts in this zone");
//     ImGui::Text("Press Ctrl+A to trigger Zone 1 action");

//     if (ImGui::Button("Trigger Zone 1 Action")) {
//         TestAction_Zone1();
//     }

//     ImGui::End();
// }

// void Application::RenderTestZone2() {
//     ImGui::Begin("Test Zone 2", nullptr, ImGuiWindowFlags_NoCollapse);
//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 2", 
//         Shortcuts::ShortcutZone::TestZone2);

//     ImGui::Text("Test shortcuts in this zone");
//     ImGui::Text("Press Ctrl+A to trigger Zone 2 action");
//     ImGui::Text("(same shortcut as Zone 1, but different action)");

//     if (ImGui::Button("Trigger Zone 2 Action")) {
//         TestAction_Zone2();
//     }

//     ImGui::End();
// }

// void Application::RenderFloatingWindows() {
//     if (showTokenEditor_) {
//         auto& ds = DesignSystem::DesignSystem::Instance();
//         tokenEditor_.Render(ds.GetCurrentContext(), ds.GetOverrideManager());
//     }

//     if (showShortcutEditor_) {
//         shortcutEditor_.Render(&showShortcutEditor_);
//     }

//     if (showImGuiDemo_) {
//         ImGui::ShowDemoWindow(&showImGuiDemo_);
//     }
// }

// } // namespace App








#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/IconManager.h>

namespace App {

void Application::RenderIconTestWindow() {
    ImGui::Begin("Icon Test Lab");

    ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
    ImGui::Text("These icons use semantic.icon.color.primary & secondary");
    
    ImGui::Text("Settings:");
    UI::IconManager::Instance().RenderIcon("settings", 32.0f);
    ImGui::SameLine();
    UI::IconManager::Instance().RenderIcon("settings", 48.0f);
    
    ImGui::Text("Tool1:");
    UI::IconManager::Instance().RenderIcon("tool1", 32.0f);
    ImGui::SameLine();
    UI::IconManager::Instance().RenderIcon("tool1", 48.0f);
    
    ImGui::Separator();
    
    ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
    ImGui::Text("These icons keep their original SVG colors");
    
    ImGui::Text("Three Balls:");
    UI::IconManager::Instance().RenderIcon("three_balls", 64.0f);
    
    ImGui::Text("Logo Carto:");
    UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);
    
    ImGui::Separator();
    
    ImGui::SeparatorText("CUSTOM COLORS");
    
    static ImVec4 customPrimary = ImVec4(0, 0.5f, 1, 1);
    static ImVec4 customSecondary = ImVec4(1, 0.5f, 0, 1);
    
    ImGui::ColorEdit4("Custom Primary", (float*)&customPrimary);
    ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);
    
    UI::IconManager::Instance().RenderIcon("tool1", 64.0f, customPrimary, customSecondary);

    ImGui::End();
}

void Application::RenderDesignExample() {
    ImGui::Begin("Design System Example");
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Design System Example", 
        Shortcuts::ShortcutZone::DesignExample);

    ImGui::Text("This window uses the Design System!");

    auto& ds = DesignSystem::DesignSystem::Instance();
    ds.PushAllStyles();

    static int int_value = 50;
    ImGui::Text("Drag to adjust:");
    ImGui::SetNextItemWidth(150.0f);
    ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");

    ImGui::SameLine();
    if (ImGui::Button("Print Value", ImVec2(120, 0))) {
        printf("Value: %d\n", int_value);
    }

    ds.PopAllStyles();

    ImGui::End();
}

void Application::RenderThemePreview() {
    ImGui::Begin("Theme Preview");
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Theme Preview", 
        Shortcuts::ShortcutZone::ThemePreview);

    ImGui::Text("Preview with current context");
    ImGui::Separator();

    auto& ds = DesignSystem::DesignSystem::Instance();

    try {
        ImVec4 bgColor = ds.GetColor("semantic.color.background");
        ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

        ImGui::ColorButton("##bg", bgColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
        ImGui::SameLine();
        ImGui::Text("Background Color");

        ImGui::ColorButton("##primary", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
        ImGui::SameLine();
        ImGui::Text("Primary Color");

        ImGui::Separator();
        ImGui::Text("Sample Components:");

        if (ImGui::Button("Sample Button")) {}

        static char textBuffer[128] = "Sample Input";
        ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

    } catch (const std::exception& e) {
        ImGui::Text("Error: %s", e.what());
    }

    ImGui::End();
}

void Application::RenderTestZone1() {
    ImGui::Begin("Test Zone 1");
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 1", 
        Shortcuts::ShortcutZone::TestZone1);

    ImGui::Text("Test shortcuts in this zone");
    ImGui::Text("Press Ctrl+A to trigger Zone 1 action");

    if (ImGui::Button("Trigger Zone 1 Action")) {
        TestAction_Zone1();
    }

    ImGui::End();
}

void Application::RenderTestZone2() {
    ImGui::Begin("Test Zone 2");
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 2", 
        Shortcuts::ShortcutZone::TestZone2);

    ImGui::Text("Test shortcuts in this zone");
    ImGui::Text("Press Ctrl+A for Zone 2 action");
    ImGui::Text("(same shortcut as Zone 1, different action)");

    if (ImGui::Button("Trigger Zone 2 Action")) {
        TestAction_Zone2();
    }

    ImGui::End();
}

void Application::RenderFloatingWindows() {
    if (showTokenEditor_) {
        auto& ds = DesignSystem::DesignSystem::Instance();
        tokenEditor_.Render(ds.GetCurrentContext(), ds.GetOverrideManager());
    }

    if (showShortcutEditor_) {
        shortcutEditor_.Render(&showShortcutEditor_);
    }

    if (showImGuiDemo_) {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }
}

} // namespace App
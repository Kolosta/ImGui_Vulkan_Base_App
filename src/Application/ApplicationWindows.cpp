#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>

namespace App {

void Application::RenderIconTestWindow() {
    ImGui::Begin("Icon Test Lab");

    auto& iconManager = VectorGraphics::IconManager::Instance();

    ImGui::SeparatorText("BICOLOR ICONS (Design System Tokens)");
    ImGui::Text("These icons use semantic.icon.color.primary/secondary");
    ImGui::Text("CSS classes in SVG determine which elements use which token");
    
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
    
    ImGui::Separator();
    
    ImGui::SeparatorText("MULTICOLOR ICONS (Original Colors)");
    ImGui::Text("These icons keep their original SVG colors");
    
    ImGui::Text("Three Balls:");
    iconManager.RenderIcon("three_balls", 64.0f);
    
    ImGui::Text("Logo Carto:");
    iconManager.RenderIcon("logo_carto", 64.0f);
    
    ImGui::Separator();
    
    ImGui::SeparatorText("CUSTOM COLORS");
    
    static ImVec4 customPrimary = ImVec4(0, 0.5f, 1, 1);
    static ImVec4 customSecondary = ImVec4(1, 0.5f, 0, 1);
    
    ImGui::ColorEdit4("Custom Primary", (float*)&customPrimary);
    ImGui::ColorEdit4("Custom Secondary", (float*)&customSecondary);
    
    // Create custom metadata for testing
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
    
    // Update colors
    if (customMetadata.colorZones.size() >= 2) {
        customMetadata.colorZones[0].customColor = customPrimary;
        customMetadata.colorZones[1].customColor = customSecondary;
    }
    
    ImGui::Text("Tool1 with custom colors:");
    iconManager.RenderIcon("tool1", 64.0f, customMetadata);

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

    if (showIconEditor_) {
        iconEditor_.Render(&showIconEditor_);
    }

    if (showImGuiDemo_) {
        ImGui::ShowDemoWindow(&showImGuiDemo_);
    }
}

} // namespace App
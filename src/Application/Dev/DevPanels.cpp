#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Panel.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {
void Application::RenderSectionIconTestLab() {
    // No BeginChild here (rendered inside MainContent's child), so this RAII
    // safely pops at function return, before MainContent's EndChild.
    DesignSystem::DesignSystem::ZoneStyle zone("iconTestLab", "Icon Test Lab");

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

    Shortcuts::ShortcutManager::Instance().RegisterRegionContext(
        "DesignExampleZone", "designExample", "content");

    // The zone's pushed styles MUST be popped before EndChild() — ImGui
    // validates the style stack there. Scope every ZoneStyle inside an
    // explicit block that closes before EndChild().
    {
        // Whole zone themed through the "designExample" scope.
        DesignSystem::DesignSystem::ZoneStyle zone("designExample",
                                                   "Design System Example");

        ImGui::TextUnformatted("This section uses the Design System!");

        static int int_value = 50;
        ImGui::TextUnformatted("Drag to adjust:");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
        ImGui::SameLine();
        {
            // "Print Value" is a specific sub-component of this zone.
            DesignSystem::DesignSystem::ZoneStyle sub("designExample/print",
                                                      "Print button");
            if (ImGui::Button("Print Value", ImVec2(120.0f, 0.0f)))
                printf("Value: %d\n", int_value);
        }
    }

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionThemePreview() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("ThemePreviewZone", ImVec2(0.0f, 160.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterRegionContext(
        "ThemePreviewZone", "themePreview", "content");

    // Scope this zone so its scoped theme-defs / overrides take visual effect.
    // (Block closes before EndChild — ImGui validates the style stack there.)
    {
        DesignSystem::DesignSystem::ZoneStyle zone("themePreview",
                                                   "Theme Preview");

        auto& sm = Shortcuts::ShortcutManager::Instance();
        std::string sc = sm.GetShortcutString("edit.themePreview.cycle");

        ImGui::TextUnformatted("Preview with current context");
        if (!sc.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s to cycle)", sc.c_str());
        }
        ImGui::Separator();

        auto& ds = DesignSystem::DesignSystem::Instance();
        try {
            ImVec4 bgColor      = ds.GetColor(DesignSystem::Tok::S_Color_Background_Default);
            ImVec4 primaryColor = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);

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

    Shortcuts::ShortcutManager::Instance().RegisterRegionContext(
        "TestZone1Child", "testZone1", "content");

    // ZoneStyle must be popped before EndChild() (ImGui validates the style
    // stack there), so keep all scoped styles inside this block.
    {
        // Scoped cascade: everything in this zone resolves through "testZone1"
        // (then the global typed defaults). Demonstrates per-zone theming with
        // zero extra tokens — see InstallZoneThemeDemo().
        DesignSystem::DesignSystem::ZoneStyle zone("testZone1", "Test Zone 1");

        auto& smZ1 = Shortcuts::ShortcutManager::Instance();
        std::string scZ1 = smZ1.GetShortcutString("edit.testZone1.action");

        ImGui::TextUnformatted("Shortcuts active when this area is hovered.");
        if (!scZ1.empty())
            ImGui::Text("Press %s to trigger the Zone 1 action.", scZ1.c_str());

        {
            // The action button is a specific sub-component: resolves through
            // "testZone1/action" → "testZone1" → global, proving infinite depth.
            DesignSystem::DesignSystem::ZoneStyle sub("testZone1/action",
                                                      "Action button");
            if (ImGui::Button("Trigger Zone 1 Action"))
                Action_Zone1();
            if (ImGui::IsItemHovered() && !scZ1.empty())
                ImGui::SetTooltip("Shortcut: %s", scZ1.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderSectionTestZone2() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("TestZone2Child", ImVec2(0.0f, 100.0f), true,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    Shortcuts::ShortcutManager::Instance().RegisterRegionContext(
        "TestZone2Child", "testZone2", "content");

    {
        DesignSystem::DesignSystem::ZoneStyle zone("testZone2", "Test Zone 2");

        auto& smZ2 = Shortcuts::ShortcutManager::Instance();
        std::string scZ2 = smZ2.GetShortcutString("edit.testZone2.action");

        ImGui::TextUnformatted("Shortcuts active when this area is hovered.");
        if (!scZ2.empty())
            ImGui::Text("Press %s for the Zone 2 action.", scZ2.c_str());
        ImGui::TextUnformatted("(same key as Zone 1, different action - resolved by context)");

        {
            DesignSystem::DesignSystem::ZoneStyle sub("testZone2/action",
                                                      "Action button");
            if (ImGui::Button("Trigger Zone 2 Action"))
                Action_Zone2();
            if (ImGui::IsItemHovered() && !scZ2.empty())
                ImGui::SetTooltip("Shortcut: %s", scZ2.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────

// Render-engine selector, injected into Preferences ▸ Dev (SetDevPageExtra).
// Runs in the Preferences ImGui context. A click only STAGES the swap
// (pendingRendererKind_); Update applies it between frames so we never tear down
// the live engine's offscreen textures while this frame's draw data references
// them (that caused a TDR / VK_ERROR_DEVICE_LOST).
void Application::RenderDevRenderEnginePanel() {
    using Kind = Renderer::IViewRenderer::Kind;

    UI::PanelConfig pc; pc.id = "##devRenderEngine"; pc.label = "Render Engine";
    pc.defaultOpen = true;
    UI::PanelResult pr = UI::BeginPanel(pc);
    if (pr.open) {
        // Reflect a staged swap immediately (so the radio doesn't flicker back for
        // a frame), falling back to the live kind.
        const Kind shown = pendingRendererKind_.value_or(rendererKind_);

        ImGui::TextDisabled("Exactly one engine is alive; switching does a clean\n"
                            "swap, applied between frames.");
        ImGui::Spacing();

        if (ImGui::RadioButton("Legacy — CanvasRenderer (VkRenderPass)",
                               shown == Kind::Legacy) && shown != Kind::Legacy)
            pendingRendererKind_ = Kind::Legacy;

        // The Compositor needs a Vulkan 1.3 device (dynamic rendering, sync2,
        // timeline). When the device lacks them, disable the option.
        ImGui::BeginDisabled(!compositorSupported_);
        if (ImGui::RadioButton("Compositor — Comp::Engine (modern Vulkan)",
                               shown == Kind::Compositor) && shown != Kind::Compositor)
            pendingRendererKind_ = Kind::Compositor;
        ImGui::EndDisabled();

        ImGui::Spacing();
        if (!compositorSupported_)
            ImGui::TextDisabled("Compositor unavailable: this device/driver does not\n"
                                "expose the required Vulkan 1.3 features.");
        else if (shown == Kind::Compositor)
            ImGui::TextDisabled("Compositor renders an empty canvas for now\n"
                                "(GPU output lands in Lot 1b).");
    }
    UI::EndPanel();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Floating windows
// ─────────────────────────────────────────────────────────────────────────────


void Application::RenderDevTestWindow() {
    if (!showDevWindow_) return;

    ImGui::SetNextWindowSize(ImVec2(620.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dev Test Window", &showDevWindow_,
                       ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    // Scope the whole Dev Test floating window so every section inherits a
    // common base look ("devTest"), and each collapsible section pushes its
    // own deeper sub-scope ("devTest/<section>"). Theme-defs / overrides
    // can therefore target a single section. The per-section ZoneStyle
    // calls inside RenderSectionDesignExample / RenderSectionThemePreview
    // etc. still push their legacy scope names (designExample, themePreview,
    // …) for backwards compatibility with seeded theme-defs.
    {
        DesignSystem::DesignSystem::ZoneStyle devZone("devTest",
                                                      "Dev Test window");

        struct Sec { const char* id; const char* label; const char* icon;
                     const char* scope; const char* scopeLabel;
                     void (Application::*fn)(); };
        const Sec secs[] = {
            { "sec_icons",  "Icon Test Lab",         "image",
              "devTest/icons",        "DevTest icons",
              &Application::RenderSectionIconTestLab },
            { "sec_design", "Design System Example", "draw",
              "devTest/design",       "DevTest design",
              &Application::RenderSectionDesignExample },
            { "sec_theme",  "Theme Preview",         "contrast-square",
              "devTest/themePreview", "DevTest theme preview",
              &Application::RenderSectionThemePreview },
            { "sec_zone1",  "Test Zone 1",           "checklist",
              "devTest/zone1",        "DevTest zone 1",
              &Application::RenderSectionTestZone1 },
            { "sec_zone2",  "Test Zone 2",           "checklist",
              "devTest/zone2",        "DevTest zone 2",
              &Application::RenderSectionTestZone2 },
        };
        for (const Sec& s : secs) {
            if (UI::IconCollapsingHeader(s.id, s.label, s.icon, /*open=*/true)) {
                ImGui::PushID(s.id);
                ImGui::Indent(8.0f);
                {
                    DesignSystem::DesignSystem::ZoneStyle secZone(
                        s.scope, s.scopeLabel);
                    (this->*s.fn)();
                }
                ImGui::Unindent(8.0f);
                ImGui::PopID();
            }
            ImGui::Spacing();
        }
    }

    ImGui::End();
}

} // namespace App

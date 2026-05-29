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
void Application::RenderOutliner() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));

    // Root = the project itself. Title reflects saved / dirty state.
    std::string title = project_.TabTitle();
    if (UI::IconTreeNode("##prj", title.c_str(), /*defaultOpen=*/true)) {
        if (project_.artboards.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted("(no page — add one with + in a Viewport)");
            ImGui::PopStyleColor();
        }
        for (size_t i = 0; i < project_.artboards.size(); ++i) {
            const Artboard& ab = project_.artboards[i];
            char id[32];
            std::snprintf(id, sizeof(id), "##ab%zu", i);
            char label[96];
            std::snprintf(label, sizeof(label), "%s  (%.0f x %.0f)",
                          ab.name.c_str(), ab.size.x, ab.size.y);
            if (UI::IconTreeNode(id, label, /*defaultOpen=*/false)) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
                ImGui::TextUnformatted("(no objects yet)");
                ImGui::PopStyleColor();
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopStyleColor();
}

// ── Right scrollable content area ─────────────────────────────────────────────


} // namespace App

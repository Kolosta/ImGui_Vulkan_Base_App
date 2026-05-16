#include <UI/StatusBar.h>
#include <UI/KeyCap.h>
#include <Shortcuts/ShortcutManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>

namespace UI {

namespace {

ImVec4 SafeColor(const std::string& token, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(token); }
    catch (...) { return fallback; }
}

float SafeFloat(const std::string& token, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(token); }
    catch (...) { return fallback; }
}

ImVec2 SafeVec2(const std::string& token, ImVec2 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetVec2(token); }
    catch (...) { return fallback; }
}

} // namespace

float StatusBar::Height() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    float h  = SafeFloat("component.statusbar.height", 22.0f);
    return h * ds.GetGlobalScale();
}

void StatusBar::Render(const std::string& versionLabel) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    const float height = Height();

    ImVec2 padding = SafeVec2("component.statusbar.padding", ImVec2(8.0f, 3.0f));
    padding.x *= scale;
    padding.y *= scale;

    ImVec4 bg   = SafeColor("component.statusbar.background",
                            ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImVec4 text = SafeColor("component.statusbar.text",
                            ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, padding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8.0f * scale, padding.y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);

    if (ImGui::BeginChild("##StatusBar", ImVec2(0.0f, height), false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {

        auto& sm = Shortcuts::ShortcutManager::Instance();

        // Modal hints take precedence
        Shortcuts::ModalSession* modal = sm.TopModal();
        if (modal) {
            ImGui::TextColored(SafeColor("component.shortcut.recording",
                                         ImVec4(0.95f, 0.30f, 0.30f, 1.0f)),
                               "[%s]", modal->Name().c_str());
            for (const auto& hint : modal->Hints()) {
                ImGui::SameLine(0.0f, 8.0f * scale);
                KeyCap::DrawShortcut(hint.trigger, false);
                ImGui::SameLine(0.0f, 4.0f * scale);
                ImGui::TextColored(text, "%s", hint.label.c_str());
            }
        } else {
            auto actions = sm.GetStatusBarActions(5);
            bool first = true;
            for (const auto* a : actions) {
                if (!first) {
                    ImGui::SameLine(0.0f, 6.0f * scale);
                    ImGui::TextColored(text, "|");
                    ImGui::SameLine(0.0f, 6.0f * scale);
                }
                first = false;
                const Shortcuts::ShortcutBinding* b = sm.GetBinding(a->id);
                if (b && !b->current.empty()) {
                    KeyCap::DrawShortcut(b->current.front(), !first);
                    ImGui::SameLine(0.0f, 4.0f * scale);
                }
                ImGui::TextColored(text, "%s", a->name.c_str());
            }
            if (actions.empty()) {
                ImGui::TextColored(text, "Hover over the application to see available shortcuts");
            }
        }

        // Right side: version label
        ImVec2 versionSize = ImGui::CalcTextSize(versionLabel.c_str());
        float availableW = ImGui::GetWindowWidth();
        float xRight = availableW - versionSize.x - padding.x;
        if (xRight > ImGui::GetCursorPosX() + 12.0f * scale) {
            ImGui::SameLine(xRight);
            ImGui::TextColored(text, "%s", versionLabel.c_str());
        } else {
            // not enough room — pad and rightmost
            ImGui::SameLine();
            ImGui::TextColored(text, "  %s", versionLabel.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace UI

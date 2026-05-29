#include <UI/Chrome/StatusBar.h>
#include <UI/Shortcuts/KeyCap.h>
#include <Shortcuts/ShortcutManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>

namespace UI {

namespace {

ImVec4 SafeColor(DesignSystem::Tok t, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(t); }
    catch (...) { return fallback; }
}

float SafeFloat(DesignSystem::Tok t, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(t); }
    catch (...) { return fallback; }
}

ImVec2 SafeVec2(DesignSystem::Tok t, ImVec2 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetVec2(t); }
    catch (...) { return fallback; }
}

} // namespace

float StatusBar::Height() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    // Bar height = control-height + top/bottom inset (padding.y), so inner
    // widgets sized at control-height sit centred with the inset above & below.
    const float controlH = SafeFloat(DesignSystem::Tok::S_Size_ControlHeight, 20.0f);
    const float padY     = SafeVec2(DesignSystem::Tok::C_StatusBar_Padding,
                                    ImVec2(8.0f, 2.0f)).y;
    return (controlH + padY * 2.0f) * ds.GetGlobalScale();
}

// The whole bar is laid out by hand in screen coordinates: every element
// (a keycap group, a label, a "|" separator, the version) is drawn at an
// explicit X and vertically centred inside the bar's usable height. This
// avoids ImGui's per-item-type baseline differences (text vs chip vs button)
// that made each element sit at a different vertical position.
void StatusBar::Render(const std::string& versionLabel) {
    DesignSystem::DesignSystem::ComponentScope _cs("StatusBar");
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale  = ds.GetGlobalScale();
    const float height = Height();

    // padding.x = left/right margin, padding.y = top/bottom inset.
    ImVec2 pad = SafeVec2(DesignSystem::Tok::C_StatusBar_Padding, ImVec2(8.0f, 2.0f));
    pad.x *= scale;
    pad.y *= scale;
    // Gap between shortcut GROUPS (around each "|" separator).
    const float groupGap = SafeFloat(DesignSystem::Tok::C_StatusBar_Gap, 12.0f) * scale;
    // Small gap between a keycap group and its action label.
    const float labelGap = 4.0f * scale;

    const float innerH = std::max(0.0f, height - pad.y * 2.0f);

    ImVec4 bg   = SafeColor(DesignSystem::Tok::C_StatusBar_Background,
                            ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImVec4 text = SafeColor(DesignSystem::Tok::C_StatusBar_Label,
                            ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);

    if (!ImGui::BeginChild("##StatusBar", ImVec2(0.0f, height), false,
                           ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    auto& sm = Shortcuts::ShortcutManager::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();      // top-left, screen space
    const float  winW   = ImGui::GetWindowWidth();
    const float  topY   = origin.y + pad.y;           // content top (after inset)
    const float  textH  = ImGui::GetTextLineHeight();
    const ImU32  textCol = ImGui::ColorConvertFloat4ToU32(text);

    // Draw a label vertically centred in innerH; return its width.
    auto drawText = [&](float x, ImU32 col, const char* s) -> float {
        ImVec2 ts = ImGui::CalcTextSize(s);
        dl->AddText(ImVec2(x, topY + (innerH - textH) * 0.5f), col, s);
        return ts.x;
    };

    float x = origin.x + pad.x;     // running left cursor (after the left inset)

    Shortcuts::ModalSession* modal = sm.TopModal();
    if (modal) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[%s]", modal->Name().c_str());
        ImU32 recCol = ImGui::ColorConvertFloat4ToU32(
            SafeColor(DesignSystem::Tok::C_Shortcut_Recording,
                      ImVec4(0.95f, 0.30f, 0.30f, 1.0f)));
        x += drawText(x, recCol, buf) + groupGap;
        for (const auto& hint : modal->Hints()) {
            x += KeyCap::DrawShortcutAt(hint.trigger, ImVec2(x, topY), innerH);
            x += labelGap;
            x += drawText(x, textCol, hint.label.c_str()) + groupGap;
        }
    } else {
        auto actions = sm.GetStatusBarActions(5);
        bool first = true;
        for (const auto* a : actions) {
            if (!first) {
                x += groupGap * 0.5f;
                const float sepH  = innerH * 0.55f;
                const float sepY1 = topY + (innerH - sepH) * 0.5f;
                const ImU32 sepCol = (textCol & 0x00FFFFFFu) | 0x60000000u;
                dl->AddLine(ImVec2(x + 0.5f * scale, sepY1),
                            ImVec2(x + 0.5f * scale, sepY1 + sepH),
                            sepCol, 1.0f * scale);
                x += 1.0f * scale + groupGap * 0.5f;
            }
            first = false;
            const Shortcuts::ShortcutBinding* b = sm.GetBinding(a->id);
            if (b && !b->current.empty()) {
                x += KeyCap::DrawShortcutAt(b->current.front(), ImVec2(x, topY), innerH);
                x += labelGap;
            }
            x += drawText(x, textCol, a->name.c_str());
        }
        if (actions.empty()) {
            drawText(x, textCol,
                     "Hover over the application to see available shortcuts");
        }
    }

    // Right side: version label.
    ImVec2 versionSize = ImGui::CalcTextSize(versionLabel.c_str());
    float xRight = origin.x + winW - versionSize.x - pad.x;
    if (xRight > x + 12.0f * scale)
        drawText(xRight, textCol, versionLabel.c_str());

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace UI

#include <UI/Widgets/ToolPalette.h>

#include <UI/Widgets/PopupMenu.h>          // DrawTooltip
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

float SafeFloat(Tok t, float fb) {
    try { return DS::DesignSystem::Instance().GetFloat(t); } catch (...) { return fb; }
}
ImVec4 SafeColor(Tok t, ImVec4 fb) {
    try { return DS::DesignSystem::Instance().GetColor(t); } catch (...) { return fb; }
}
} // namespace

ToolPaletteResult ToolPalette(const char* id, ImVec2 origin,
                              const std::vector<ToolPaletteItem>& items) {
    ToolPaletteResult res;
    if (items.empty()) return res;

    auto& ds      = DS::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float gs   = ds.GetGlobalScale();
    const float btn  = SafeFloat(Tok::C_ToolPalette_ButtonSize, 32.0f) * gs;
    const float pad  = 4.0f * gs;                         // outer margin
    const float gap  = 4.0f * gs;                         // between buttons
    const float ggap = 12.0f * gs;                        // between GROUPS
    const float rnd  = SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * gs;
    const int   n    = (int)items.size();

    // Column geometry: no container — the buttons stack directly, a larger gap
    // whenever the group index changes.
    const ImVec2 mn(origin.x + pad, origin.y + pad);
    std::vector<float> tops((std::size_t)n, 0.0f);
    float y = mn.y;
    for (int i = 0; i < n; ++i) {
        if (i > 0)
            y += btn + (items[(std::size_t)i].group !=
                        items[(std::size_t)(i - 1)].group ? ggap : gap);
        tops[(std::size_t)i] = y;
    }
    res.rectMin = mn;
    res.rectMax = ImVec2(mn.x + btn, tops.back() + btn);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec4 bg    = SafeColor(Tok::C_IconButton_Background,      ImVec4(0.2f, 0.2f, 0.22f, 1));
    const ImVec4 hov   = SafeColor(Tok::C_IconButton_BackgroundHover, ImVec4(0.3f, 0.3f, 0.33f, 1));
    const ImVec4 acc   = SafeColor(Tok::S_Color_Accent_Default,       ImVec4(0.25f, 0.5f, 0.9f, 1));
    ImVec4 tint        = SafeColor(Tok::S_Color_Text_Default,         ImVec4(0.9f, 0.9f, 0.9f, 1));
    const float disMul = SafeFloat(Tok::S_Opacity_Disabled, 0.45f);

    ImGui::PushID(id);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);   // Tab must not focus tools
    for (int i = 0; i < n; ++i) {
        const ToolPaletteItem& it = items[(std::size_t)i];
        const ImVec2 bmin(mn.x, tops[(std::size_t)i]);
        const ImVec2 bmax(bmin.x + btn, bmin.y + btn);
        ImGui::SetCursorScreenPos(bmin);
        ImGui::PushID(i);
        ImGui::BeginDisabled(!it.enabled);
        const bool clicked = ImGui::InvisibleButton("##tp", ImVec2(btn, btn));
        ImGui::EndDisabled();
        const bool hovered = ImGui::IsItemHovered();

        // Button fill: accent when selected, hover tint otherwise.
        const ImVec4 fill = it.selected ? acc : (hovered ? hov : bg);
        dl->AddRectFilled(bmin, bmax, ImGui::ColorConvertFloat4ToU32(fill),
                          std::max(0.0f, rnd - 1.0f));

        // Icon centred in the button.
        const float isz = btn * SafeFloat(Tok::C_ToolPalette_IconScale, 0.68f);
        ImVec4 iconTint = tint;
        if (!it.enabled) iconTint.w *= disMul;
        auto md = iconMgr.GetDefaultMetadata(it.icon);
        if (!md.colorZones.empty()) md.colorZones[0].customColor = iconTint;
        iconMgr.RenderIcon(dl, it.icon.c_str(),
                           ImVec2(bmin.x + (btn - isz) * 0.5f, bmin.y + (btn - isz) * 0.5f),
                           isz, md);

        // Multi-tool corner: a small filled triangle at the bottom-right,
        // signalling the right-click variant menu.
        if (it.hasMenu) {
            const float t = 6.0f * gs, inset = 2.5f * gs;
            const ImVec2 c(bmax.x - inset, bmax.y - inset);
            dl->AddTriangleFilled(ImVec2(c.x - t, c.y), ImVec2(c.x, c.y - t), c,
                                  ImGui::ColorConvertFloat4ToU32(iconTint));
        }

        if (hovered && !it.tooltip.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            DrawTooltip(it.tooltip.c_str(), ImGui::GetIO().MousePos);
        if (clicked && it.enabled) res.clicked = i;
        if (hovered && it.enabled && it.hasMenu &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            res.rightClicked = i;
        ImGui::PopID();
    }
    ImGui::PopItemFlag();
    ImGui::PopID();
    return res;
}

} // namespace UI

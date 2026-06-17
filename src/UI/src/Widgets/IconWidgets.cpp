#include <UI/Widgets/IconWidgets.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>

namespace UI {

namespace {

// Tint an icon to the given colour (single-zone ds-primary icons) and blit it
// at pos with the given pixel size onto the current window draw list.
void DrawIcon(const char* icon, ImVec2 pos, float size, const ImVec4& tint) {
    if (!icon || !*icon) return;
    auto& im = VectorGraphics::IconManager::Instance();
    auto md = im.GetDefaultMetadata(icon);
    if (md.colorZones.empty()) return;          // unknown icon → draw nothing
    for (auto& z : md.colorZones) z.customColor = tint;
    im.RenderIcon(ImGui::GetWindowDrawList(), icon, pos, size, md);
}

// Shared header row: a clickable strip that toggles `*open`, drawing the
// chevron (right/down) + optional content icon + label. Returns the new open
// state. `frameBg` true → fills the row like a CollapsingHeader.
bool HeaderRow(const char* strId, const char* label, const char* icon,
               bool defaultOpen, bool frameBg) {
    ImGui::PushID(strId);
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("##open");
    bool open = st->GetBool(key, defaultOpen);

    const float h    = ImGui::GetFrameHeight();
    const float pad  = ImGui::GetStyle().FramePadding.x;
    const float chev = h * 0.55f;
    const float icoS = h * 0.66f;
    const ImVec4 fg  = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float  w  = ImGui::GetContentRegionAvail().x;
    if (w < h) w = h;

    if (ImGui::InvisibleButton("##hit", ImVec2(w, h)))
        open = !open;
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (frameBg && (hovered || open)) {
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h),
            ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered
                                       : ImGuiCol_Header),
            ImGui::GetStyle().FrameRounding);
    }

    float x = p0.x + pad;
    // Chevron: right when collapsed, down when expanded.
    DrawIcon(open ? "chevron-down" : "chevron-right",
             ImVec2(x, p0.y + (h - chev) * 0.5f), chev, fg);
    x += chev + pad;
    if (icon && *icon) {
        DrawIcon(icon, ImVec2(x, p0.y + (h - icoS) * 0.5f), icoS, fg);
        x += icoS + pad;
    }
    dl->AddText(ImVec2(x, p0.y + (h - ImGui::GetFontSize()) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), label);

    st->SetBool(key, open);
    ImGui::PopID();
    return open;
}

} // namespace

bool IconCollapsingHeader(const char* id, const char* label,
                          const char* icon, bool defaultOpen) {
    return HeaderRow(id, label, icon, defaultOpen, /*frameBg=*/true);
}

bool IconTreeNode(const char* id, const char* label, bool defaultOpen) {
    // Same contract as ImGui::TreeNodeEx: caller does ImGui::TreePop() when
    // this returns true. We only push the ID so TreePop() balances; the
    // visual indent is left to the caller (matches prior code paths).
    bool open = HeaderRow(id, label, "", defaultOpen, /*frameBg=*/false);
    if (open) ImGui::TreePush(id);
    return open;
}

void InlineIcon(const char* icon, float size, const ImVec4& tint) {
    // Always reserve the slot so labels stay column-aligned even when an
    // item has no matching icon (then nothing is drawn).
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  y = p.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
    DrawIcon(icon, ImVec2(p.x, y), size, tint);
    ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
}

bool IconMenuItem(const char* icon, const char* label, const char* shortcut,
                  bool selected, bool enabled) {
    // A normal MenuItem (no native check glyph), with the icon overlaid on a
    // reserved left slot. selected → row highlight only, never a checkmark.
    const float sz = ImGui::GetTextLineHeight();
    ImVec2 p0 = ImGui::GetCursorScreenPos();

    // Indent the label so it doesn't overlap the icon slot.
    std::string lbl = "    ";       // ~ one icon width of leading space
    lbl += label;
    bool clicked = ImGui::MenuItem(lbl.c_str(), shortcut, /*selected=*/false,
                                   enabled);

    // Draw the icon into the reserved slot (nothing if icon empty/unknown).
    const ImVec4 fg = ImGui::GetStyleColorVec4(
        enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    DrawIcon(icon, ImVec2(p0.x, p0.y), sz, fg);

    // Subtle highlight for the "active/open" state, instead of a checkmark.
    if (selected) {
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(
            mn, mx, ImGui::GetColorU32(ImGuiCol_HeaderActive),
            ImGui::GetStyle().FrameRounding);
    }
    return clicked;
}

} // namespace UI

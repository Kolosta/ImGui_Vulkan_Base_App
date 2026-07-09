#pragma once

#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/ListRow.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner row layout — the fixed column slots and chrome shared by every
//  Outliner row builder, rebuilt on the Ink model but restoring the legacy
//  design (docs/Ink/ROADMAP.md Lot 9 rework). Slot order, left → right:
//
//     [active-dot gutter] [chevron slot] [icon slot] [name ……] [eye]
//
//  All helpers are `inline` (this header is included by more than one .cpp).
//  Every size/colour is a design-system token so the look matches the rest of
//  the app and themes cleanly.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {
namespace ol {   // outliner-layout helpers

namespace DST = DesignSystem;
using Tok = DesignSystem::Tok;

inline float Gs() { return DST::DesignSystem::Instance().GetGlobalScale(); }
inline float RowH() { return UI::ListRowBandHeight(); }
inline float ItemH() { return UI::ListRowStripeHeight(); }

inline float SafeFloat(Tok t, float fallback) {
    try { return DST::DesignSystem::Instance().GetFloat(t); } catch (...) { return fallback; }
}
inline ImVec4 SafeColor(Tok t, ImVec4 fallback) {
    try { return DST::DesignSystem::Instance().GetColor(t); } catch (...) { return fallback; }
}

inline float IconSize()    { return SafeFloat(Tok::C_Dropdown_IconSize, 16.0f) * Gs(); }
inline float IconSlotW()   { return IconSize() + 4.0f * Gs(); }
inline float ChevronSize() {
    const float chev = SafeFloat(Tok::C_Dropdown_ChevronSize, 12.0f) * Gs();
    const float icon = IconSize();
    return std::clamp((chev + icon) * 0.5f, chev, icon);
}
inline float ChevronSlotW() { return IconSlotW(); }   // same as icon, so columns align
inline float DotGutterW()   { return ImGui::GetTextLineHeight() * 0.34f + 5.0f * Gs(); }

// Left/right of a full-width row band (the editor opts out of the content
// inset, so WorkRect.Min.x is flush; the right runs under the scrollbar gutter).
inline float RowLeft()  { return ImGui::GetCurrentWindow()->WorkRect.Min.x; }
inline float RowRight() {
    return ImGui::GetCurrentWindow()->WorkRect.Max.x + ImGui::GetStyle().ScrollbarSize;
}
inline float BandMargin() { return std::max(0.0f, SafeFloat(Tok::C_Editor_ContentInset, 6.0f) * Gs()); }

// A collapse chevron in a fixed uniform slot. Toggles `open`, returns slot width.
inline float Chevron(const char* id, bool& open) {
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float chev = ChevronSize(), slot = ChevronSlotW(), rowH = RowH();
    ImGui::PushID(id);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##chev", ImVec2(slot, ItemH()))) open = !open;
    const ImVec2 ipos(p0.x + (slot - chev) * 0.5f, p0.y + (rowH - chev) * 0.5f);
    const char* icon = open ? "chevron-down" : "chevron-right";
    auto md = iconMgr.GetDefaultMetadata(icon);
    if (!md.colorZones.empty())
        md.colorZones[0].customColor = SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f,0.6f,0.6f,1));
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, ipos, chev, md);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PopID();
    return slot;
}

// Reserve the chevron slot on a row with no chevron (keeps the icon column aligned).
inline void ChevronSpacer() {
    ImGui::Dummy(ImVec2(ChevronSlotW(), ItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// Reserve the active-dot gutter (the dot itself is drawn inside the band).
inline void DotGutter() {
    ImGui::Dummy(ImVec2(DotGutterW(), ItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// Draw the "active" dot inside the row's selection band, vertically centred.
inline void ActiveDotAt(float bandL, float rowTopY, ImU32 col) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float rowH = RowH();
    const float d = ImGui::GetTextLineHeight() * 0.34f;
    ImVec2 c(bandL + 4.0f * Gs() + d * 0.5f, rowTopY + rowH * 0.5f);
    w->DrawList->AddCircleFilled(c, d * 0.5f, col);
}

// Draw an icon centred in the icon slot, then advance one slot on the same line.
inline void SlotIcon(const char* icon, ImVec4 tint) {
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float slot = IconSlotW(), isz = IconSize(), rowH = RowH();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 ipos(p0.x + (slot - isz) * 0.5f, p0.y + (rowH - isz) * 0.5f);
    auto md = iconMgr.GetDefaultMetadata(icon);
    if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, ipos, isz, md);
    ImGui::Dummy(ImVec2(slot, ItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// Draw a rounded colour swatch in the icon slot (collections).
inline void SlotSwatch(ImVec4 color) {
    const float slot = IconSlotW(), isz = IconSize(), rowH = RowH();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(p0.x + (slot - isz) * 0.5f, p0.y + (rowH - isz) * 0.5f);
    ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + isz, a.y + isz),
        ImGui::ColorConvertFloat4ToU32(color), 2.0f * Gs());
    ImGui::Dummy(ImVec2(slot, ItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// The screen-X at which a parent row places its children's tree guide line.
inline float GuideX(float rowContentX) {
    return rowContentX + DotGutterW() + ChevronSlotW() * 0.5f;
}

// Vertical tree guide line (pixel-snapped crisp hairline). `dotted` = dashed.
inline void TreeLine(float x, float yStart, float yEnd, ImU32 color, bool dotted) {
    const float gs = Gs();
    float inset = SafeFloat(Tok::C_Outliner_TreeLineInset, 4.0f) * gs;
    yStart += inset; yEnd -= inset;
    if (yEnd <= yStart) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::max(1.0f, std::floor(gs));
    const float x0 = std::floor(x), x1 = x0 + th;
    const float y0 = std::floor(yStart), y1 = std::floor(yEnd);
    if (!dotted) {
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    } else {
        const float dash = std::floor(2.0f * gs), gap = std::floor(2.0f * gs);
        for (float y = y0; y < y1; y += dash + gap)
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, std::min(y + dash, y1)), color);
    }
}

// Row label colour: search hit → green; dimmed (hidden) → disabled; else default.
inline ImU32 LabelColor(bool searchHit, bool dim) {
    if (searchHit)
        return ImGui::ColorConvertFloat4ToU32(
            SafeColor(Tok::C_Outliner_Search_Text, ImVec4(0.4f,0.8f,0.4f,1)));
    return ImGui::ColorConvertFloat4ToU32(SafeColor(
        dim ? Tok::S_Color_Text_Disabled : Tok::C_Outliner_Text, ImVec4(0.85f,0.85f,0.85f,1)));
}

// Case-insensitive substring test.
inline bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    return it != hay.end();
}

} // namespace ol
} // namespace App

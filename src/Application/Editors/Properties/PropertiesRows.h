#pragma once

#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/DragValue.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ButtonGroup.h>
#include <Ink/Document/Document.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties row layout — the legacy Compositor property-row conventions,
//  shared by every Properties sub-section (Properties.cpp, PropertiesPaint.cpp,
//  PropertiesModifiers.cpp). The row model: a RIGHT-JUSTIFIED label in the
//  left 40 % column, the control filling the right column; vector fields
//  write their group title ONCE, inline with the first axis row ("Location ·
//  X · [input]" / "Y · [input]"); property GROUPS are separated by a larger
//  token-driven gap (C_PropertyGroup_Gap) so they read as distinct blocks.
//  All helpers are `inline` (included by more than one .cpp).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {
namespace pr {   // properties-row helpers

namespace DST = DesignSystem;
using Tok = DesignSystem::Tok;

inline float Gs() { return DST::DesignSystem::Instance().GetGlobalScale(); }
inline float SafeFloat(Tok t, float fallback) {
    try { return DST::DesignSystem::Instance().GetFloat(t); } catch (...) { return fallback; }
}
inline ImVec4 SafeColor(Tok t, ImVec4 fallback) {
    try { return DST::DesignSystem::Instance().GetColor(t); } catch (...) { return fallback; }
}

constexpr float kLabelFrac = 0.40f;   // left 2/5 of the row for the label

inline float RowH() { return SafeFloat(Tok::S_Size_ControlHeight, 22.0f) * Gs(); }

// Extra vertical space BEFORE a group, so a group reads as distinct from the
// plain item rows around it (token-driven, larger than ItemSpacing).
inline void GroupGap() {
    ImGui::Dummy(ImVec2(0.0f, SafeFloat(Tok::C_PropertyGroup_Gap, 6.0f) * Gs()));
}

// sRGB (UI pickers) ↔ linear (document colours).
inline ImVec4 ToSrgb(const Ink::Color& c) {
    auto s = [](float u) {
        return u <= 0.0031308f ? u * 12.92f
                               : 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
    };
    return { s(c.r), s(c.g), s(c.b), c.a };
}
inline Ink::Color ToLinear(const ImVec4& c) {
    auto l = [](float u) {
        return u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f);
    };
    return { l(c.x), l(c.y), l(c.z), c.w };
}

// A right-justified label in the left column, vertically centred on a ui-unit
// row. Leaves the cursor at the control column; returns the control width.
inline float Label(const char* text) {
    const float full = ImGui::GetContentRegionAvail().x;
    const float lblW = full * kLabelFrac;
    const float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float ty = origin.y + std::max(0.0f, (RowH() - ts.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(origin.x + std::max(0.0f, lblW - ts.x - pad), ty),
        ImGui::ColorConvertFloat4ToU32(
            SafeColor(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))),
        text);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + lblW + pad, origin.y));
    return std::max(40.0f, full - lblW - pad);
}

// Cursor to the start of the control column (label-less buttons/rows).
inline void ControlColumn() {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x * kLabelFrac +
                         ImGui::GetStyle().ItemInnerSpacing.x);
}

// A DragValue row: full-precision store, `decimals` shown. True while changing.
inline bool DragFloat(const char* label, float* v, float speed, float mn,
                      float mx, int decimals = 3, const char* unit = "") {
    const float w = Label(label);
    UI::DragValueConfig dc;
    dc.id = "##dv"; dc.speed = speed; dc.min = mn; dc.max = mx;
    dc.displayDecimals = decimals; dc.unit = unit; dc.width = w;
    ImGui::PushID(label);
    const bool ch = UI::DragValue(dc, v);
    ImGui::PopID();
    return ch;
}

// An integer drag row (whole-number display, int storage).
inline bool DragInt(const char* label, int* v, float speed, int mn, int mx) {
    float f = (float)*v;
    if (!DragFloat(label, &f, speed, (float)mn, (float)mx, 0)) return false;
    const int nv = (int)std::lround(std::clamp(f, (float)mn, (float)mx));
    if (nv == *v) return false;
    *v = nv;
    return true;
}

// Draw a group title RIGHT-JUSTIFIED on the same line as the first axis row,
// ending just left of the axis label — the legacy "Location · X · [input]"
// look. Does NOT advance the layout (the axis row lays out from the origin).
inline void GroupTitleInline(const char* title, const char* firstAxis) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float lblW = ImGui::GetContentRegionAvail().x * kLabelFrac;
    const float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    const float axisW = ImGui::CalcTextSize(firstAxis).x;
    const float axisX = start.x + std::max(0.0f, lblW - axisW - pad);
    const float titleW = ImGui::CalcTextSize(title).x;
    const float ty = start.y +
        std::max(0.0f, (RowH() - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(std::max(start.x, axisX - pad - titleW), ty),
        ImGui::ColorConvertFloat4ToU32(
            SafeColor(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))),
        title);
}

// Blender-style vector field: group gap, then "title · X · [input]" and
// "Y · [input]" (title written ONCE). Returns a per-axis change mask (bit 0 =
// X changed this frame, bit 1 = Y) so callers can commit per released field.
inline unsigned Vec2Group(const char* title, float v[2], float speed, float mn,
                          float mx, int decimals = 3, const char* unit = "",
                          bool* xDeactivated = nullptr,
                          bool* yDeactivated = nullptr) {
    unsigned changed = 0;
    ImGui::PushID(title);
    GroupGap();
    GroupTitleInline(title, "X");
    if (DragFloat("X", &v[0], speed, mn, mx, decimals, unit)) changed |= 1u;
    if (xDeactivated) *xDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    if (DragFloat("Y", &v[1], speed, mn, mx, decimals, unit)) changed |= 2u;
    if (yDeactivated) *yDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
    return changed;
}

// A checkbox row (box at the control column, description on the label side).
inline bool CheckRow(const char* label, bool* v) {
    Label(label);
    ImGui::PushID(label);
    const bool ch = UI::CheckboxBox("##cb", v);
    ImGui::PopID();
    return ch;
}

// An enum dropdown row. Returns true (and writes *value) on a pick.
inline bool DropdownRow(const char* label, const char* const* items, int count,
                        int* value) {
    Label(label);
    const int cur = std::clamp(*value, 0, count - 1);
    UI::DropdownConfig cfg; cfg.id = "##pdd";
    cfg.triggerLabel = count > 0 ? items[cur] : "";
    for (int i = 0; i < count; ++i) {
        UI::DropdownItem it; it.label = items[i]; cfg.items.push_back(it);
    }
    cfg.selectedIndex = cur;
    ImGui::PushID(label);
    UI::DropdownResult r = UI::Dropdown(cfg);
    ImGui::PopID();
    if (r.changed && r.selected != *value) { *value = r.selected; return true; }
    return false;
}

// A linked single-toggle button group row (small enums — the legacy look for
// clip / anchor / space switches). Returns true on a pick.
inline bool ButtonGroupRow(const char* label, const char* const* items,
                           int count, int* value) {
    const float ctrlW = Label(label);
    const float cellW = std::max(24.0f, ctrlW / (float)count);
    ImGui::PushID(label);
    UI::ButtonGroup bg("##bg");
    bg.SetGrid(std::vector<float>((std::size_t)count, cellW), { RowH() });
    for (int i = 0; i < count; ++i) {
        UI::ButtonGroup::Cell c{};
        c.label = items[i]; c.col = i; c.row = 0;
        c.selected = (i == *value);
        bg.AddCell(c);
    }
    UI::ButtonGroup::Result r = bg.Render();
    ImGui::PopID();
    if (r.clickedIndex >= 0 && r.clickedIndex != *value) {
        *value = r.clickedIndex;
        return true;
    }
    return false;
}

// A colour row: right-justified label + a FULL-WIDTH swatch button (one
// ui-unit tall, checkerboard alpha) opening a picker popup. Document colour
// (linear) in/out; the picker edits in sRGB. True while the picker changes;
// *released fires when a picker drag ends (undo-fold commit point).
inline bool ColorRow(const char* label, Ink::Color* col, bool withAlpha = true,
                     bool* released = nullptr) {
    const float ctrlW = Label(label);
    ImGui::PushID(label);
    ImVec4 c = ToSrgb(*col);
    bool ch = false;
    if (released) *released = false;
    if (ImGui::ColorButton("##sw", c,
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
            ImVec2(ctrlW, RowH())))
        ImGui::OpenPopup("##pick");
    if (ImGui::BeginPopup("##pick")) {
        ImGuiColorEditFlags f = withAlpha ? ImGuiColorEditFlags_AlphaBar
                                          : ImGuiColorEditFlags_NoAlpha;
        if (ImGui::ColorPicker4("##p", &c.x, f)) { *col = ToLinear(c); ch = true; }
        if (released) *released = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return ch;
}

// A node-reference picker row: a dropdown listing the document's PATH nodes
// by name (the referenced inputs of patterns/modifiers/instances). Writes the
// picked id into *ref; `allowNone` prepends a "None" entry. Excludes `self`.
inline bool NodePickerRow(const char* label, const Ink::Document& doc,
                          Ink::NodeId* ref, Ink::NodeId self,
                          bool allowNone = true, bool pathsOnly = true) {
    Label(label);
    std::vector<Ink::NodeId> ids;
    UI::DropdownConfig cfg; cfg.id = "##npick";
    cfg.searchable = true;   // document lists get long — filter + scroll
    int cur = -1;
    if (allowNone) {
        UI::DropdownItem it; it.label = "None"; cfg.items.push_back(it);
        ids.push_back(Ink::kNullNode);
        if (*ref == Ink::kNullNode) cur = 0;
    }
    for (const Ink::Page& page : doc.Pages()) {
        std::vector<Ink::NodeId> stack(page.children.begin(), page.children.end());
        while (!stack.empty()) {
            const Ink::NodeId id = stack.back(); stack.pop_back();
            const Ink::Node* n = doc.Find(id);
            if (!n) continue;
            for (Ink::NodeId ch : n->children) stack.push_back(ch);
            if (id == self) continue;
            if (pathsOnly && n->kind != Ink::NodeKind::Path) continue;
            if (id == *ref) cur = (int)ids.size();
            UI::DropdownItem it;
            it.label = n->name.empty() ? "(unnamed)" : n->name;
            cfg.items.push_back(it);
            ids.push_back(id);
        }
    }
    cfg.selectedIndex = cur;
    cfg.triggerLabel = cur >= 0 ? cfg.items[(std::size_t)cur].label
                                : std::string("Pick\xE2\x80\xA6");
    ImGui::PushID(label);
    UI::DropdownResult r = UI::Dropdown(cfg);
    ImGui::PopID();
    if (r.changed && r.selected >= 0 && r.selected < (int)ids.size() &&
        ids[(std::size_t)r.selected] != *ref) {
        *ref = ids[(std::size_t)r.selected];
        return true;
    }
    return false;
}

} // namespace pr
} // namespace App

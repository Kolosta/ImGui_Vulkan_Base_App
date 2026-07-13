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

// An enum dropdown row. Returns true (and writes *value) on a pick. The
// trigger fills the control column (label left, chevron pinned right) like
// every other field.
inline bool DropdownRow(const char* label, const char* const* items, int count,
                        int* value) {
    const float w = Label(label);
    const int cur = std::clamp(*value, 0, count - 1);
    UI::DropdownConfig cfg; cfg.id = "##pdd";
    cfg.triggerLabel = count > 0 ? items[cur] : "";
    cfg.triggerWidth = w;
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

// A node-reference picker row: an OBJECT-PICKER dropdown (object icon left,
// eyedropper / clear cross right) listing the document's nodes by name. Writes
// the picked id into *ref; `allowNone` prepends a "None" entry. Excludes
// `self`. `*pickReq` is set when the eyedropper button is clicked (the caller
// arms the viewport/outliner pick to write *ref); returns true on a list pick.
inline bool NodePickerRow(const char* label, const Ink::Document& doc,
                          Ink::NodeId* ref, Ink::NodeId self,
                          bool allowNone = true, bool pathsOnly = true,
                          bool* pickReq = nullptr) {
    const float w = Label(label);
    std::vector<Ink::NodeId> ids;
    UI::DropdownConfig cfg; cfg.id = "##npick";
    cfg.searchable = true;   // document lists get long — filter + scroll
    cfg.triggerWidth = w;
    cfg.objectPicker = true;
    cfg.objectPickerHasValue = (*ref != Ink::kNullNode);
    cfg.placeholder = "Object";   // grey placeholder when empty (no "None" row)
    (void)allowNone;              // clearing is the trailing cross, not a row
    int cur = -1;
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
                                : std::string();   // empty → grey placeholder
    ImGui::PushID(label);
    UI::DropdownResult r = UI::Dropdown(cfg);
    ImGui::PopID();
    if (pickReq) *pickReq = r.pickRequested;
    if (r.cleared) { *ref = Ink::kNullNode; return true; }
    if (r.changed && r.selected >= 0 && r.selected < (int)ids.size() &&
        ids[(std::size_t)r.selected] != *ref) {
        *ref = ids[(std::size_t)r.selected];
        return true;
    }
    return false;
}

// ── Paint-stack thumbnails (the fill / stroke vignette rails) ────────────────

// A selectable square tile: token chrome, accent border when selected,
// brighter border on hover. Returns true on click; outMin/outMax receive the
// CONTENT rect (inside the border) for the caller to draw the preview into.
inline bool ThumbTile(const char* idstr, float size, bool selected,
                      ImVec2* outMin, ImVec2* outMax) {
    const float gs = Gs();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + size, mn.y + size);
    ImGui::PushID(idstr);
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = SafeFloat(Tok::S_CornerRadius_Control, 3.0f) * gs;
    // White canvas behind every sample (the same paper the viewport pages
    // show), so colours and alpha read as printed — not against the panel.
    dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), r);
    const ImVec4 bord =
        selected ? SafeColor(Tok::S_Color_Accent_Default, ImVec4(0.3f, 0.5f, 0.9f, 1))
        : hovered ? SafeColor(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))
                  : SafeColor(Tok::S_Color_Border_Default, ImVec4(0.3f, 0.3f, 0.3f, 1));
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(bord), r, 0,
                (selected ? 2.0f : 1.0f) * gs);
    const float inset = 3.0f * gs;
    if (outMin) *outMin = ImVec2(mn.x + inset, mn.y + inset);
    if (outMax) *outMax = ImVec2(mx.x - inset, mx.y - inset);
    return clicked;
}

// Preview of ONE fill, alone, over the tile's white canvas: a solid swatch
// (alpha shows against the white) or a motif lattice for a pattern fill (the
// Paint page substitutes the REAL pipeline render when a view is available).
inline void DrawFillSample(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                           const Ink::Fill& f) {
    const float gs = Gs();
    if (f.kind == Ink::FillKind::Solid) {
        ImVec4 c = ToSrgb(f.paint.color);
        c.w *= f.opacity;
        dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(c));
    } else {
        // Pattern: a small dot lattice in the subtle text colour (the motif is
        // another object — the lattice reads as "pattern" at a glance).
        const ImU32 dot = ImGui::ColorConvertFloat4ToU32(
            SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
        const float w = mx.x - mn.x, h = mx.y - mn.y;
        for (int gy = 0; gy < 3; ++gy)
            for (int gx = 0; gx < 3; ++gx)
                dl->AddCircleFilled(
                    ImVec2(mn.x + w * (0.25f + 0.25f * gx),
                           mn.y + h * (0.25f + 0.25f * gy)),
                    1.8f * gs, dot);
    }
    if (!f.enabled) {
        // Disabled: wash the tile + a diagonal slash.
        dl->AddRectFilled(mn, mx, IM_COL32(30, 30, 30, 150));
        dl->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mn.y),
                    IM_COL32(220, 80, 80, 200), 1.5f * gs);
    }
}

// Preview of ONE stroke, alone, over the tile's white canvas: a horizontal
// line SAMPLE (not the host shape's contour) with the stroke's colour, a
// clamped display width and its dashes.
inline void DrawStrokeSample(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                             const Ink::Stroke& s) {
    const float gs = Gs();
    ImVec4 c = ToSrgb(s.paint.color);
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(c);
    const float y = (mn.y + mx.y) * 0.5f;
    const float pad = 3.0f * gs;
    const float x0 = mn.x + pad, x1 = mx.x - pad;
    const float wpx = std::clamp((float)s.width * gs, 1.0f,
                                 (mx.y - mn.y) * 0.45f);
    if (s.dashPattern.empty()) {
        dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), col, wpx);
    } else {
        // Scale the dash pattern so ~2.5 periods fit in the sample.
        double period = 0.0;
        for (double d : s.dashPattern) period += d;
        if (s.dashPattern.size() & 1) period *= 2.0;   // odd runs repeat doubled
        const double scale = period > 1e-6
            ? ((double)(x1 - x0) / 2.5) / period : 1.0;
        float x = x0;
        std::size_t k = 0;
        bool on = true;
        while (x < x1) {
            const float run =
                (float)(s.dashPattern[k % s.dashPattern.size()] * scale);
            if (on && run > 0.01f)
                dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + run, x1), y), col, wpx);
            x += std::max(run, 0.5f);
            on = !on;
            ++k;
        }
    }
    if (!s.enabled) {
        dl->AddRectFilled(mn, mx, IM_COL32(30, 30, 30, 150));
        dl->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mn.y),
                    IM_COL32(220, 80, 80, 200), 1.5f * gs);
    }
}

// The "+" tile appended under a thumbnail rail (adds an item).
inline bool ThumbAddTile(float size) {
    const float gs = Gs();
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + size, mn.y + size);
    const bool clicked = ImGui::InvisibleButton("##addTile", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = SafeFloat(Tok::S_CornerRadius_Control, 3.0f) * gs;
    const ImVec4 fg = hovered
        ? SafeColor(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))
        : SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1));
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(fg), r, 0, 1.0f * gs);
    const ImVec2 ctr((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    const float arm = size * 0.22f;
    const ImU32 fgc = ImGui::ColorConvertFloat4ToU32(fg);
    dl->AddLine(ImVec2(ctr.x - arm, ctr.y), ImVec2(ctr.x + arm, ctr.y), fgc, 1.5f * gs);
    dl->AddLine(ImVec2(ctr.x, ctr.y - arm), ImVec2(ctr.x, ctr.y + arm), fgc, 1.5f * gs);
    return clicked;
}

} // namespace pr
} // namespace App

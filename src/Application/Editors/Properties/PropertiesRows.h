#pragma once

#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/DragValue.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/PopupMenu.h>
#include <VectorGraphics/IconManager.h>
#include <Ink/Document/Document.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
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
namespace un  = UI::Units;
using Tok = DesignSystem::Tok;
using Quantity    = UI::Units::Quantity;
using LengthScale = UI::Units::LengthScale;

inline float Gs() { return DST::DesignSystem::Instance().GetGlobalScale(); }
inline float SafeFloat(Tok t, float fallback) {
    try { return DST::DesignSystem::Instance().GetFloat(t); } catch (...) { return fallback; }
}
inline ImVec4 SafeColor(Tok t, ImVec4 fallback) {
    try { return DST::DesignSystem::Instance().GetColor(t); } catch (...) { return fallback; }
}
inline ImVec2 SafeVec2(Tok t, ImVec2 fallback) {
    try { return DST::DesignSystem::Instance().GetVec2(t); } catch (...) { return fallback; }
}

constexpr float kLabelFrac = 0.40f;   // left 2/5 of the row for the label

inline float RowH() { return SafeFloat(Tok::S_Size_ControlHeight, 22.0f) * Gs(); }

// Extra vertical space BEFORE a group, so a group reads as distinct from the
// plain item rows around it (token-driven, larger than ItemSpacing).
inline void GroupGap() {
    ImGui::Dummy(ImVec2(0.0f, SafeFloat(Tok::C_PropertyGroup_Gap, 6.0f) * Gs()));
}

// A small padlock toggle for a per-property lock (Ink::PropLock bits): drawn
// as a floating icon in the row's LEFT margin, then the cursor is restored so
// the property group lays out exactly as before. Returns true on a click
// (the caller flips the bit). `managed` = the active module owns the lock
// (spec-fixed): rendered in the notice colour and inert. Call it immediately
// BEFORE the group's first row.
inline bool LockToggle(const char* id, bool locked, bool managed) {
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float h  = RowH();
    const float sz = h * 0.85f;
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##lock", ImVec2(sz, h));
    ImGui::PopID();
    const bool hov = ImGui::IsItemHovered();
    // `managed` only TINTS the padlock (a module spec-lock) — it stays
    // clickable so the property can be temporarily unlocked for testing.
    auto& im = VectorGraphics::IconManager::Instance();
    const char* icon = locked ? "lock" : "lock-open";
    if (im.HasIcon(icon)) {
        ImVec4 col = locked
            ? SafeColor(managed ? Tok::S_Color_Notice_Default
                                : Tok::S_Color_Accent_Default,
                        ImVec4(0.95f, 0.6f, 0.2f, 1))
            : SafeColor(hov ? Tok::S_Color_Text_Default : Tok::S_Color_Text_Subtle,
                        ImVec4(0.6f, 0.6f, 0.6f, 1));
        if (!hov && !locked) col.w *= 0.55f;   // unlocked at rest: discreet
        auto md = im.GetDefaultMetadata(icon);
        if (!md.colorZones.empty()) md.colorZones[0].customColor = col;
        const float isz = sz * 0.82f;
        im.RenderIcon(ImGui::GetWindowDrawList(), icon,
                      ImVec2(rowStart.x + (sz - isz) * 0.5f,
                             rowStart.y + (h - isz) * 0.5f), isz, md);
    }
    if (hov)
        UI::DrawTooltip(managed
            ? (locked ? "Module lock (spec) — click to unlock for testing"
                      : "Module property — click to re-lock")
            : (locked ? "Unlock this property" : "Lock this property"),
            ImGui::GetIO().MousePos);
    // Same-row restore: the group renders from the original row start.
    ImGui::SetCursorScreenPos(rowStart);
    return clicked;
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
// `q` tags the QUANTITY (Length/Angle/Percent/Scalar) so the field shows + parses
// in the document display unit; Scalar keeps the fixed `unit` suffix. `unit` is
// only honoured for Scalar (a bare "×" / "px" label).
inline bool DragFloat(const char* label, float* v, float speed, float mn,
                      float mx, int decimals = 3, const char* unit = "",
                      Quantity q = Quantity::Scalar,
                      LengthScale scale = LengthScale::Normal,
                      bool useDocSys = true,
                      un::UnitSystem sysOverride = un::UnitSystem::Pixel) {
    const float w = Label(label);
    UI::DragValueConfig dc;
    dc.id = "##dv"; dc.speed = speed; dc.min = mn; dc.max = mx;
    dc.displayDecimals = decimals; dc.unit = unit; dc.width = w;
    dc.quantity = q; dc.scale = scale;
    dc.useDocSystem = useDocSys; dc.system = sysOverride;
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
                          bool* yDeactivated = nullptr,
                          bool leadingGap = true,
                          Quantity q = Quantity::Scalar,
                          LengthScale scale = LengthScale::Normal,
                          bool useDocSys = true,
                          un::UnitSystem sysOverride = un::UnitSystem::Pixel) {
    unsigned changed = 0;
    ImGui::PushID(title);
    if (leadingGap) GroupGap();   // a FIRST group in a panel passes false
    GroupTitleInline(title, "X");
    if (DragFloat("X", &v[0], speed, mn, mx, decimals, unit, q, scale,
                  useDocSys, sysOverride)) changed |= 1u;
    if (xDeactivated) *xDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    if (DragFloat("Y", &v[1], speed, mn, mx, decimals, unit, q, scale,
                  useDocSys, sysOverride)) changed |= 2u;
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

// ImGui outlines a colour swatch EVEN WHEN frame borders are switched off:
// ColorButton falls back to a hairline in ImGuiCol_FrameBg because "color
// buttons are often in need of some sort of border". That fallback is why the
// "Current" sample wears a faint outline while the hand-drawn ones next to it
// looked naked — so the decision is replicated here rather than guessed at.
// `fallback` off means "draw only what the style asks for", for surfaces ImGui
// itself leaves unoutlined.
inline void SampleFrame(ImDrawList* dl, const ImVec2& a, const ImVec2& b,
                        float r, bool fallback = true) {
    const float bs = ImGui::GetStyle().FrameBorderSize;
    if (bs > 0.0f) {
        dl->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f),
                    ImGui::GetColorU32(ImGuiCol_BorderShadow), r, 0, bs);
        dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border), r, 0, bs);
    } else if (fallback) {
        dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), r, 0,
                    std::max(1.0f, std::trunc(Gs())));
    }
}

// The picker's SV square and its hue / alpha bars are drawn by ImGui with the
// corner radius hard-coded to zero, and no flag or style var can change that:
// they are painted with AddRectFilledMultiColor, a raw four-vertex quad that
// takes no rounding at all. So the radius is carved out afterwards — each
// square corner is repainted with the surface behind it, up to the arc, and the
// square outline ImGui left is replaced by a rounded one.
//
// `surface` must be the opaque colour BEHIND the picker: the carved corner is
// background, not picker, and has to read as background even when the picker is
// disabled and everything else is dimmed.
inline void RoundPickerFrames(const ImVec2& pickerPos, float width,
                              bool alphaBar, ImU32 surface) {
    const float r = SafeFloat(Tok::S_CornerRadius_Control, 3.0f) * Gs();
    if (r < 0.5f) return;
    const ImGuiStyle& st = ImGui::GetStyle();
    // Same arithmetic as ColorPicker4's own layout block.
    const float barsW = ImGui::GetFrameHeight();
    const float svSz = std::max(
        barsW, width - (alphaBar ? 2.0f : 1.0f) * (barsW + st.ItemInnerSpacing.x));
    const float bar0X = pickerPos.x + svSz + st.ItemInnerSpacing.x;
    const float bar1X = bar0X + barsW + st.ItemInnerSpacing.x;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto carve = [&](const ImVec2& a, const ImVec2& b) {
        constexpr float kPi = 3.14159265358979323846f;
        const struct { ImVec2 c, corner; float a0; } k[4] = {
            { { a.x + r, a.y + r }, { a.x, a.y }, kPi },
            { { b.x - r, a.y + r }, { b.x, a.y }, kPi * 1.5f },
            { { b.x - r, b.y - r }, { b.x, b.y }, 0.0f },
            { { a.x + r, b.y - r }, { a.x, b.y }, kPi * 0.5f },
        };
        for (const auto& q : k) {
            // Arc FIRST, corner last. AddConvexPolyFilled derives its
            // anti-aliasing fringe from the edge normals, so a wedge wound the
            // other way would push the fringe inwards and leave a hairline of
            // the square corner showing through.
            dl->PathClear();
            dl->PathArcTo(q.c, r, q.a0 + kPi * 0.5f, q.a0);
            dl->PathLineTo(q.corner);
            dl->PathFillConvex(surface);
        }
        SampleFrame(dl, a, b, r, /*fallback=*/false);
    };
    carve(pickerPos, ImVec2(pickerPos.x + svSz, pickerPos.y + svSz));
    carve(ImVec2(bar0X, pickerPos.y), ImVec2(bar0X + barsW, pickerPos.y + svSz));
    if (alphaBar)
        carve(ImVec2(bar1X, pickerPos.y), ImVec2(bar1X + barsW, pickerPos.y + svSz));
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
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        const float pw = ImGui::CalcItemWidth();
        if (ImGui::ColorPicker4("##p", &c.x, f)) { *col = ToLinear(c); ch = true; }
        if (released) *released = ImGui::IsItemDeactivatedAfterEdit();
        RoundPickerFrames(pp, pw, withAlpha, ImGui::ColorConvertFloat4ToU32(
                              ImGui::GetStyleColorVec4(ImGuiCol_PopupBg)));
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return ch;
}

// A 0-255 colour channel on the shared drag widget, letter first ("R 128") —
// the same control the rest of the app uses, so a colour is edited like every
// other number. `v` is the 0-1 component.
inline bool ChannelField(const char* letter, float* v01, float w,
                         float scale = 255.0f) {
    float d = *v01 * scale;
    UI::DragValueConfig dc;
    dc.id = "##ch";
    dc.speed = scale / 255.0f;
    dc.min = 0.0f; dc.max = scale;
    dc.displayDecimals = 0;
    dc.unit = letter;
    dc.unitBeforeValue = true;
    dc.width = w;
    dc.quantity = Quantity::Scalar;
    ImGui::PushID(letter);
    const bool ch = UI::DragValue(dc, &d);
    ImGui::PopID();
    if (ch) *v01 = std::clamp(d / scale, 0.0f, 1.0f);
    return ch;
}

// A row of channels sharing `width`.
inline bool ChannelRow(const char* id, const char* const* letters, float** vals,
                       int n, float width, float scale = 255.0f) {
    const float sp = 3.0f * Gs();
    const float fw = (width - sp * (float)(n - 1)) / (float)n;
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < n; ++i) {
        if (i) ImGui::SameLine(0.0f, sp);
        if (ChannelField(letters[i], vals[i], fw, scale)) changed = true;
    }
    ImGui::PopID();
    return changed;
}

// One CMYK channel field: the shared drag widget with the channel letter drawn
// BEFORE the number ("C 56"), which is how an ink coverage reads.
inline bool CmykField(const char* id, const char* letter, float* v, float w) {
    UI::DragValueConfig dc;
    dc.id = id;
    dc.speed = 0.5f;
    dc.min = 0.0f; dc.max = 100.0f;
    dc.displayDecimals = 0;
    dc.unit = letter;
    dc.unitBeforeValue = true;
    dc.width = w;
    dc.quantity = Quantity::Scalar;
    return UI::DragValue(dc, v);
}

// A whole CMYK row: four fields side by side at one ui-unit tall, sharing the
// given width. Reads and writes Ink::Cmyk (0-100 per channel).
inline bool CmykRow(const char* id, Ink::Cmyk* ink, float width) {
    static const char* kL[4] = { "C", "M", "Y", "K" };
    const float sp = 3.0f * Gs();
    const float fw = (width - sp * 3.0f) * 0.25f;
    double* ch[4] = { &ink->c, &ink->m, &ink->y, &ink->k };
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0.0f, sp);
        float f = (float)*ch[i];
        if (CmykField(kL[i], kL[i], &f, fw)) { *ch[i] = f; changed = true; }
    }
    ImGui::PopID();
    return changed;
}

// The picker's RIGHT COLUMN, built here rather than by ImGui so the three
// swatches stack in the order that actually helps: what the colour WAS on top
// (click to restore), what it is NOW in the middle, and what it will look like
// over white and over black at the bottom — the last being the only one that
// says anything about a partly transparent paint. ImGui's own side preview puts
// "Original" below "Current" and offers no transparency read at all, so the
// picker is asked for NoSidePreview and this takes its place.
//
// Returns true when the "before" swatch was clicked (the caller reverts).
inline bool DrawPickerSideColumn(const ImVec4& now, const ImVec4& before,
                                 float& colW) {
    const float gs = Gs();
    const float h = RowH() * 2.0f;                    // two ui-units tall
    // A golden rectangle reads as a colour sample rather than as a control.
    colW = h * 1.6180339887f;
    const bool bordersOn = DST::DesignSystem::Instance().BordersEnabled();
    const float borderW = bordersOn
        ? SafeFloat(Tok::S_BorderWidth_Thin, 1.0f) * gs : 0.0f;
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(
        SafeColor(Tok::S_Color_Border_Default, ImVec4(0.4f, 0.4f, 0.4f, 1)));
    // The three samples must be indistinguishable but for their contents, so
    // the two hand-drawn ones adopt ColorButton's geometry rather than an
    // approximation of it: the same clamped radius, and the same 0.75 px inset
    // between the fill and the outline (ImGui's own comment: the border "tends
    // to look off when color is near-opaque and rounding is enabled").
    const float rTok = SafeFloat(Tok::S_CornerRadius_Control, 3.0f) * gs;
    const float r = std::min(rTok, (std::min(colW, h) / 2.99f) * 0.5f);
    constexpr float kInset = 0.75f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool revert = false;

    const ImU32 lblCol = ImGui::ColorConvertFloat4ToU32(
        SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));
    auto caption = [&](const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(lblCol));
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
    };
    // Every outline below — the hand-drawn ones AND ColorButton's — resolves
    // through these two, so one style decision covers all three.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, borderW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rTok);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::BeginGroup();
    caption("Original");
    // BEFORE — opaque, clickable to restore.
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + colW, p0.y + h);
        ImVec4 b = before; b.w = 1.0f;
        if (ImGui::InvisibleButton("##before", ImVec2(colW, h))) revert = true;
        dl->AddRectFilled(ImVec2(p0.x + kInset, p0.y + kInset),
                          ImVec2(p1.x - kInset, p1.y - kInset),
                          ImGui::ColorConvertFloat4ToU32(b), r);
        SampleFrame(dl, p0, p1, r);
        if (ImGui::IsItemHovered())
            UI::DrawTooltip("Original — click to restore",
                            ImGui::GetIO().MousePos);
    }
    ImGui::Dummy(ImVec2(colW, 2.0f * gs));
    caption("Current");
    // NOW — opaque on the left, checkerboard-backed on the right.
    ImGui::ColorButton("##now", now,
                       ImGuiColorEditFlags_AlphaPreviewHalf |
                       ImGuiColorEditFlags_NoTooltip,
                       ImVec2(colW, h));
    ImGui::Dummy(ImVec2(colW, 2.0f * gs));
    // No caption: this is still the CURRENT colour — just shown against the two
    // extremes. Labelling it again would read as a third, different colour.
    // OVER WHITE | OVER BLACK.
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        auto over = [&](float bg) {
            return ImGui::ColorConvertFloat4ToU32(
                ImVec4(now.x * now.w + bg * (1.0f - now.w),
                       now.y * now.w + bg * (1.0f - now.w),
                       now.z * now.w + bg * (1.0f - now.w), 1.0f));
        };
        // The two halves are the SAME rectangle drawn twice — identical corners,
        // identical edges — the second one merely clipped to the right half.
        // Because the black pass shares the white pass's geometry it cannot
        // round differently at the corners nor stop short of the right edge,
        // which is what let the black square poke out of the rounded corners
        // and leave a white pixel down the right side. The clip cuts only at
        // the middle; it is left generous on the other three sides so the black
        // rect's own anti-aliased edges land on top of the white one's instead
        // of being shaved off. The middle is snapped to a whole pixel, exactly
        // as ImGui does for its own half-and-half swatch.
        const ImVec2 p1(p0.x + colW, p0.y + h);
        const ImVec2 i0(p0.x + kInset, p0.y + kInset);
        const ImVec2 i1(p1.x - kInset, p1.y - kInset);
        const float mid = std::floor((i0.x + i1.x) * 0.5f + 0.5f);
        dl->AddRectFilled(i0, i1, over(1.0f), r);
        dl->PushClipRect(ImVec2(mid, p0.y - 2.0f), ImVec2(p1.x + 2.0f, p1.y + 2.0f),
                         true);
        dl->AddRectFilled(i0, i1, over(0.0f), r);
        dl->PopClipRect();
        SampleFrame(dl, p0, p1, r);
        ImGui::Dummy(ImVec2(colW, h));
        if (ImGui::IsItemHovered())
            UI::DrawTooltip("Over white | over black",
                            ImGui::GetIO().MousePos);
    }
    ImGui::EndGroup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return revert;
}

// The same row, but the colour may instead FOLLOW a document swatch (a named
// colour in the document's table — Ink/Document/Swatch.h). The button shows the
// colour that will actually be drawn; the popup carries the picker, CMYK fields
// and the document palette. While bound, the picker is disabled: the swatch
// owns the colour, and the literal underneath is only the fallback if the
// swatch is ever deleted.
//
// `*swatch` is kNullSwatch for a free colour. Returns true on any change;
// *released fires when a picker drag ends OR a binding changes (commit point).
inline bool SwatchRow(const char* label, Ink::Color* col, Ink::SwatchId* swatch,
                      const Ink::Document& doc, bool withAlpha = true,
                      bool* released = nullptr) {
    const float ctrlW = Label(label);
    ImGui::PushID(label);
    const float gs = Gs();
    const Ink::Swatch* bound = doc.FindSwatch(*swatch);
    ImVec4 c = ToSrgb(bound ? bound->display : *col);
    bool ch = false;
    if (released) *released = false;
    // The colour the popup OPENED with — what cancelling would restore, and
    // what the "before" bar inside compares against.
    static std::unordered_map<ImGuiID, ImVec4> s_opened;
    const ImGuiID pickKey = ImGui::GetID("##pick");
    if (ImGui::ColorButton("##sw", c,
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
            ImVec2(ctrlW, RowH()))) {
        s_opened[pickKey] = c;
        ImGui::OpenPopup("##pick");
    }
    if (bound && ImGui::IsItemHovered())
        UI::DrawTooltip(bound->name.c_str(), ImGui::GetIO().MousePos);

    // The popup wears the menu surface and the window padding, like every other
    // dropdown in the app, instead of ImGui's bare frame.
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
        SafeColor(Tok::C_Menu_Background, ImVec4(0.13f, 0.13f, 0.15f, 1)));
    {   // Same air on every side: the padding token is not square, and the
        // picker is wide enough that the difference reads.
        const ImVec2 pad = SafeVec2(Tok::C_Window_Padding, ImVec2(8, 8));
        const float m = std::max(pad.x, pad.y);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m, m));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,
                        SafeFloat(Tok::C_Window_CornerRadius, 6.0f) * gs);
    const bool pickOpen = ImGui::BeginPopup("##pick");
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (pickOpen) {
        // Which document colour this paint follows, spelled out — the outlined
        // tile in the palette below says which one, this says its name.
        if (bound) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));
            ImGui::TextUnformatted(bound->name.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(bound != nullptr);
        // ImGui's own side column puts "Original" BELOW "Current" and says
        // nothing about transparency, so it is turned off and rebuilt here in
        // the order that helps. Its inputs go too — the fields below are the
        // app's own drag widgets, CMYK included.
        const ImGuiColorEditFlags f =
            (withAlpha ? ImGuiColorEditFlags_AlphaBar
                       : ImGuiColorEditFlags_NoAlpha) |
            ImGuiColorEditFlags_NoSidePreview |
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoOptions;
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        const float pw = ImGui::CalcItemWidth();
        if (ImGui::ColorPicker4("##p", &c.x, f)) { *col = ToLinear(c); ch = true; }
        if (released && ImGui::IsItemDeactivatedAfterEdit()) *released = true;
        const float pickW = ImGui::GetItemRectSize().x;
        // The popup's own surface — ImGuiCol_PopupBg was pushed for BeginPopup
        // and popped again, so the token is the only thing that still knows it.
        RoundPickerFrames(pp, pw, withAlpha, ImGui::ColorConvertFloat4ToU32(
            SafeColor(Tok::C_Menu_Background, ImVec4(0.13f, 0.13f, 0.15f, 1))));

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        // The column sizes itself (golden rectangles) and reports back, so the
        // rows below end exactly where it does — no dead strip on the right.
        float colW = 0.0f;
        if (DrawPickerSideColumn(c, s_opened[pickKey], colW)) {
            c = s_opened[pickKey];
            *col = ToLinear(c);
            ch = true;
            if (released) *released = true;
        }
        const float fullW = pickW + ImGui::GetStyle().ItemInnerSpacing.x + colW;

        // RGBA, HSVA, CMYK, hex — all on the app's own drag field, so a colour
        // channel behaves like every other number in the app.
        {
            static const char* kRGBA[4] = { "R", "G", "B", "A" };
            float* rgba[4] = { &c.x, &c.y, &c.z, &c.w };
            if (ChannelRow("##rgba", kRGBA, rgba, withAlpha ? 4 : 3, fullW)) {
                *col = ToLinear(c); ch = true;
                if (released) *released = true;
            }
        }
        {
            float hsv[3];
            ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, hsv[0], hsv[1], hsv[2]);
            float a4 = c.w;
            static const char* kHSVA[4] = { "H", "S", "V", "A" };
            float* vals[4] = { &hsv[0], &hsv[1], &hsv[2], &a4 };
            if (ChannelRow("##hsva", kHSVA, vals, withAlpha ? 4 : 3, fullW,
                           100.0f)) {
                ImGui::ColorConvertHSVtoRGB(hsv[0], hsv[1], hsv[2],
                                            c.x, c.y, c.z);
                c.w = a4;
                *col = ToLinear(c); ch = true;
                if (released) *released = true;
            }
        }
        {   // CMYK, round-tripped through the same ink model the print previews
            // use — so what the field says is what a separation would carry.
            Ink::Cmyk ink = Ink::NaiveCmyk(ToLinear(c));
            if (CmykRow("##cmyk", &ink, fullW)) {
                const float keepA = c.w;
                c = ToSrgb(Ink::InkOverPaper(ink, Ink::PrintChannelAll));
                c.w = keepA;
                *col = ToLinear(c);
                ch = true;
                if (released) *released = true;
            }
        }
        {
            ImGui::SetNextItemWidth(fullW);
            const ImGuiColorEditFlags hf =
                ImGuiColorEditFlags_DisplayHex |
                ImGuiColorEditFlags_NoSmallPreview |
                ImGuiColorEditFlags_NoPicker |
                ImGuiColorEditFlags_NoOptions |
                (withAlpha ? 0 : ImGuiColorEditFlags_NoAlpha);
            if (ImGui::ColorEdit4("##hex", &c.x, hf)) { *col = ToLinear(c); ch = true; }
            if (released && ImGui::IsItemDeactivatedAfterEdit()) *released = true;
        }
        ImGui::EndDisabled();

        // The document palette sits BELOW the picker and fills its width.
        const auto& table = doc.Swatches();
        if (!table.empty()) {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text,
                SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));
            ImGui::TextUnformatted("Document colours");
            ImGui::PopStyleColor();
            const float sp = 3.0f * gs;
            const int perRow = std::max(1, (int)std::floor((fullW + sp) / (RowH() + sp)));
            const float tile = (fullW - sp * (float)(perRow - 1)) / (float)perRow;
            int n = 0;
            for (const Ink::Swatch& sw : table) {
                if (n++ % perRow) ImGui::SameLine(0.0f, sp);
                ImGui::PushID((int)sw.id);
                if (ImGui::ColorButton("##s", ToSrgb(sw.display),
                        ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip,
                        ImVec2(tile, tile))) {
                    *swatch = sw.id;
                    // Binding means "use THIS colour": the literal underneath
                    // becomes fully opaque so the swatch alone decides, alpha
                    // included. Keeping the paint's old alpha would multiply it
                    // in and quietly give a colour the palette never contained.
                    col->a = 1.0f;
                    ch = true;
                    if (released) *released = true;
                    ImGui::CloseCurrentPopup();
                }
                if (bound && bound->id == sw.id) {
                    // Ring the tile this paint follows, so the palette shows
                    // WHICH one is picked and not only that one is.
                    const ImVec2 a0 = ImGui::GetItemRectMin();
                    const ImVec2 a1 = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(ImVec2(a0.x - 2.0f * gs, a0.y - 2.0f * gs),
                                ImVec2(a1.x + 2.0f * gs, a1.y + 2.0f * gs),
                                ImGui::ColorConvertFloat4ToU32(SafeColor(
                                    Tok::S_Color_Accent_Default,
                                    ImVec4(1, 0.6f, 0.2f, 1))),
                                2.0f * gs, 0, 2.0f * gs);
                }
                if (ImGui::IsItemHovered())
                    UI::DrawTooltip(sw.name.c_str(), ImGui::GetIO().MousePos);
                ImGui::PopID();
            }
            if (bound) {
                UI::ButtonGroup bg("##free");
                bg.SetGrid({ fullW }, { RowH() });
                UI::ButtonGroup::Cell fc{};
                fc.label = "Free colour"; fc.col = 0; fc.row = 0;
                bg.AddCell(fc);
                if (bg.Render().clickedIndex == 0) {
                    // Keep the resolved colour as the new literal, so unbinding
                    // changes nothing visible.
                    *col = bound->display;
                    *swatch = Ink::kNullSwatch;
                    ch = true;
                    if (released) *released = true;
                }
            }
        }
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

// ── Vertical reorder rail (dynamic drag & drop, like the modifier panels) ────
// A uniform vertical stack of fixed-height cells that can be reordered by drag:
// the grabbed cell floats with the cursor ABOVE its siblings, the others slide
// out of its way, and the move is committed on release. State lives in the
// window ImGuiStorage (survives frames), keyed off `id`.
//
// Usage per rail:
//   pr::VReorder rr("##strokeRail", (int)n, cellH);
//   for (i in [0,n)):
//       ImVec2 pos = rr.CellScreenPos(i, naturalPosForI);
//       ... draw the cell's InvisibleButton + visuals at `pos` ...
//       rr.HandleCell(i, /*itemActivated=*/ImGui::IsItemActivated(),
//                        /*itemActive=*/ImGui::IsItemActive(),
//                        cellMinY, cellMaxY);
//   if (rr.Grabbed()==i) draw that cell LAST (on top).
//   pr::VReorder::Move mv = rr.Commit();   // {from,to} or {-1,-1}
struct VReorder {
    ImGuiStorage* st = nullptr;
    ImGuiID kDrag, kPress, kGrabDY, kFloatY, kTarget;
    int   count;
    float cellH;
    float mouseY;
    bool  mouseDown;

    VReorder(const char* id, int count_, float cellH_) : count(count_), cellH(cellH_) {
        st = ImGui::GetStateStorage();
        ImGui::PushID(id);
        kDrag   = ImGui::GetID("##vrDrag");
        kPress  = ImGui::GetID("##vrPress");
        kGrabDY = ImGui::GetID("##vrGrabDY");
        kFloatY = ImGui::GetID("##vrFloatY");
        kTarget = ImGui::GetID("##vrTarget");
        ImGui::PopID();
        mouseY    = ImGui::GetIO().MousePos.y;
        mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        int d = st->GetInt(kDrag, -1);
        if (d >= count) { st->SetInt(kDrag, -1); st->SetInt(kTarget, -1); }
        if (!mouseDown && d < 0) st->SetInt(kPress, -1);   // stray press cleared
    }
    int Grabbed() const { return st->GetInt(kDrag, -1); }

    // The vertical offset to apply to cell `index` this frame (float follow for
    // the grabbed one; slide for the neighbours between origin and target).
    float CellOffset(int index) {
        const int drag = st->GetInt(kDrag, -1);
        if (drag < 0) return 0.0f;
        const int tgt = std::clamp(st->GetInt(kTarget, drag), 0, count - 1);
        if (index == drag) return 0.0f;   // handled via absolute float pos
        if (drag < tgt && index > drag && index <= tgt) return -cellH;
        if (tgt < drag && index >= tgt && index < drag) return +cellH;
        return 0.0f;
    }
    // Absolute screen Y for the grabbed cell (follows the cursor, clamped to the
    // rail). `railTopY` = natural screen Y of cell 0.
    float GrabbedScreenY(float railTopY) {
        const float grabDY = st->GetFloat(kGrabDY, cellH * 0.5f);
        float y = mouseY - grabDY;
        const float lo = railTopY, hi = railTopY + (count - 1) * cellH;
        return std::clamp(y, lo, hi);
    }
    // Called right after the cell's InvisibleButton. Promotes a press to a drag
    // past the threshold and tracks the insertion slot. `cellTopY` = the cell's
    // NATURAL screen top (without offset), used to derive the grab point.
    void HandleCell(int index, bool activated, bool active,
                    float cellTopY, float railTopY) {
        int drag = st->GetInt(kDrag, -1);
        if (activated) {
            st->SetInt(kPress, index);
            st->SetFloat(kGrabDY, mouseY - cellTopY);
        }
        const int press = st->GetInt(kPress, -1);
        if (drag < 0 && active && press == index) {
            if (std::fabs(mouseY - (cellTopY + st->GetFloat(kGrabDY, 0.0f)))
                    > ImGui::GetIO().MouseDragThreshold) {
                drag = index;
                st->SetInt(kDrag, drag);
                st->SetInt(kTarget, drag);
            }
        }
        if (drag >= 0) {
            const float fc = GrabbedScreenY(railTopY) + cellH * 0.5f;
            int slot = (int)std::floor((fc - railTopY) / cellH);
            slot = std::clamp(slot, 0, count - 1);
            st->SetInt(kTarget, slot);
        }
    }
    struct Move { int from = -1, to = -1; };
    // On mouse release while dragging: return the committed move and reset.
    Move Commit() {
        Move mv;
        const int drag = st->GetInt(kDrag, -1);
        if (drag >= 0 && !mouseDown) {
            const int tgt = std::clamp(st->GetInt(kTarget, drag), 0, count - 1);
            if (tgt != drag) { mv.from = drag; mv.to = tgt; }
            st->SetInt(kDrag, -1);
            st->SetInt(kPress, -1);
            st->SetInt(kTarget, -1);
        }
        return mv;
    }
};

// ── Paint-stack thumbnails (the fill / stroke vignette rails) ────────────────

// A selectable square tile: token chrome, accent border when selected,
// brighter border on hover. Returns true on click; outMin/outMax receive the
// CONTENT rect (inside the border) for the caller to draw the preview into.
inline bool ThumbTile(const char* idstr, float size, bool selected,
                      ImVec2* outMin, ImVec2* outMax, const ImVec2* posOverride = nullptr) {
    const float gs = Gs();
    // A drag rail places the tile explicitly (floating item); otherwise it flows
    // at the current cursor. The InvisibleButton is submitted at that position.
    if (posOverride) ImGui::SetCursorScreenPos(*posOverride);
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
    // No border in the normal / hover state (a clean vignette). ONLY the
    // selected tile carries an outline, in the selection ORANGE (not blue).
    (void)hovered;
    if (selected) {
        const ImVec4 sel = SafeColor(Tok::S_State_Active_OnPage,
                                     ImVec4(1.0f, 0.55f, 0.1f, 1));
        dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(sel), r, 0, 2.0f * gs);
    }
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

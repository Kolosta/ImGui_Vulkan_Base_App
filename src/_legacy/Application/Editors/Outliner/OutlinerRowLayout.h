#pragma once
// ── Outliner row layout primitives ───────────────────────────────────────────
// Shared, inline geometry/draw helpers for the Outliner tree rows so every row
// (object / page / collection / mark) uses the SAME column slots and aligns
// across indentation:
//
//   [active-dot gutter] [chevron slot] [icon slot] [name …………] [eye]
//
// • Chevron and icon slots have a FIXED width (icon slot = ui-unit-ish, sized
//   like a dropdown's icon; chevron slot a bit narrower) so rows at any indent
//   line up. The chevron glyph is mid-sized (between the old tiny chevron and a
//   full icon). The icon is drawn centred in its slot.
// • The active dot and the eye are drawn INSIDE the row's selection band (inset
//   from the band edges), vertically centred on the row.
// • Tree guide lines (vertical, in the collection/parent colour; dotted for a
//   parented object subtree) are drawn from a parent's chevron column down to
//   its last child row.
//
// These are inline free helpers (one definition shared by the Outliner TUs).
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <Renderer/Document/Shape.h>
#include <UI/Widgets/ListRow.h>
#include <imgui.h>
#include <algorithm>

namespace App {

namespace DST = DesignSystem;

// Icon ids for the tree (filename stems under resources/icons/**).
inline constexpr const char* kIconFolder = "folder";          // the document root
inline constexpr const char* kIconPage   = "frame-filled";    // a page / artboard
inline constexpr const char* kIconShape  = "shape-category";  // a mesh / filled shape
inline constexpr const char* kIconBezier = "bezier-curve";    // a Bézier curve
inline constexpr const char* kIconNurbs  = "nurbs-curve";     // a NURBS curve

// Pick the type icon for an object: NURBS curve, Bézier/poly curve, else shape.
inline const char* OutlinerShapeIcon(const Renderer::Shape& s) {
    if (s.parts.empty()) return kIconShape;
    if (s.Family() == Renderer::PartType::Curve) {
        // A curve part's spline kind selects the curve glyph.
        for (const Renderer::Part& p : s.parts)
            if (p.IsCurveLike())
                return p.spline == Renderer::SplineType::Nurbs ? kIconNurbs : kIconBezier;
        return kIconBezier;
    }
    return kIconShape;
}

// Icon side length: like a dropdown's icon (ui-unit minus the slot margin).
inline float OutlinerIconSize() {
    auto& ds = DST::DesignSystem::Instance();
    return ds.GetFloat(DST::Tok::C_Dropdown_IconSize) * ds.GetGlobalScale();
}
// Square icon slot, a hair wider than the icon so glyphs breathe.
inline float OutlinerIconSlotW() {
    return OutlinerIconSize() + 4.0f * DST::DesignSystem::Instance().GetGlobalScale();
}
// Chevron glyph: between the tiny dropdown chevron and the icon size.
inline float OutlinerChevronSize() {
    auto& ds = DST::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float chev = ds.GetFloat(DST::Tok::C_Dropdown_ChevronSize) * gs;
    const float icon = OutlinerIconSize();
    return std::clamp((chev + icon) * 0.5f, chev, icon);   // midway, mid-sized
}
// Chevron slot: same width as the icon slot so the icon column is identical
// whether or not a row has a chevron (aligned across siblings/indents).
inline float OutlinerChevronSlotW() { return OutlinerIconSlotW(); }

// The coloured selection BAND height = one ui-unit (the generic ListRow band).
// Glyphs/labels centre on this. The zebra stripe is this + 2px (UI::ListRow).
inline float OutlinerRowH() { return UI::ListRowBandHeight(); }
// Per-ITEM layout height for inline content drawn ON the band line: the band
// height (content centres within it). Items are chained with SameLine(0,0) so
// they never drive the vertical advance — that's owned by the ListRow scope.
inline float OutlinerItemH() { return UI::ListRowBandHeight(); }

// Draw an icon centred in a slot at the current cursor, tinted to `tint`, and
// advance the cursor by the slot width (same line). Used for the type icon.
inline void OutlinerSlotIcon(const char* icon, const ImVec4& tint) {
    auto& im = VectorGraphics::IconManager::Instance();
    const float slot = OutlinerIconSlotW();
    const float sz   = OutlinerIconSize();
    const float rowH = OutlinerRowH();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (icon && *icon) {
        auto md = im.GetDefaultMetadata(icon);
        if (!md.colorZones.empty()) {
            for (auto& z : md.colorZones) z.customColor = tint;
            im.RenderIcon(ImGui::GetWindowDrawList(), icon,
                          ImVec2(p.x + (slot - sz) * 0.5f, p.y + (rowH - sz) * 0.5f),
                          sz, md);
        }
    }
    // Slot width + a small trailing gap so the name isn't glued to the icon.
    const float gap = 4.0f * DST::DesignSystem::Instance().GetGlobalScale();
    ImGui::Dummy(ImVec2(slot + gap, OutlinerItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// Draw a coloured square (collection swatch) centred in an icon slot, same size
// as a type icon, vertically centred; advance past the slot (+ a small gap).
inline void OutlinerSlotSwatch(ImU32 color) {
    const float slot = OutlinerIconSlotW();
    const float sz   = OutlinerIconSize();
    const float rowH = OutlinerRowH();
    const float gs   = DST::DesignSystem::Instance().GetGlobalScale();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a(p.x + (slot - sz) * 0.5f, p.y + (rowH - sz) * 0.5f);
    ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + sz, a.y + sz),
                                              color, 2.0f * gs);
    const float gap = 4.0f * gs;
    ImGui::Dummy(ImVec2(slot + gap, OutlinerItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

} // namespace App

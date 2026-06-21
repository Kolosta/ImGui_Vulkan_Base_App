#include <UI/Widgets/Checkbox.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {

namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// Shared core: lays out a ui-unit-tall row holding a centred square box (+ an
// optional label) and toggles *v on click. `id` keys the InvisibleButton.
bool Draw(const char* id, const char* label, bool* v) {
    DS::DesignSystem::ComponentScope _cs("Checkbox");
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    const float rowH    = Flt(Tok::S_Size_ControlHeight) * gs;     // ui-unit row
    const float boxSz   = Flt(Tok::C_Checkbox_BoxSize)    * gs;    // smaller box
    const float radius  = Flt(Tok::C_Checkbox_CornerRadius) * gs;
    const bool  bordersOn = ds.BordersEnabled();
    const float borderW = bordersOn ? Flt(Tok::C_Checkbox_BorderWidth) * gs : 0.0f;
    const float gap     = ImGui::GetStyle().ItemInnerSpacing.x;

    const bool hasLabel = label && *label;
    const ImVec2 lblSz = hasLabel ? ImGui::CalcTextSize(label) : ImVec2(0, 0);

    float rowW = boxSz;
    if (hasLabel) rowW += gap + lblSz.x;

    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // The whole row (box + label) is the hit target so clicking the label toggles
    // too — the standard checkbox affordance.
    const bool clicked = ImGui::InvisibleButton("##cb", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    // "Pressed" look only while the button is held AND the cursor is still over it
    // (dragging off cancels the visual press, mirroring that releasing off-target
    // does not toggle). IsItemActive() alone stays true off-target → sticky press.
    const bool down    = ImGui::IsItemActive() && hovered;
    if (clicked) *v = !*v;
    const bool on = *v;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Box: centred vertically in the ui-unit row, left-aligned.
    const ImVec2 boxMin(origin.x, origin.y + (rowH - boxSz) * 0.5f);
    const ImVec2 boxMax(boxMin.x + boxSz, boxMin.y + boxSz);

    ImVec4 fillV;
    if (on) fillV = down ? Col(Tok::C_Checkbox_BackgroundSelectedDown)
                : hovered ? Col(Tok::C_Checkbox_BackgroundSelectedHover)
                          : Col(Tok::C_Checkbox_BackgroundSelected);
    else    fillV = down ? Col(Tok::C_Checkbox_BackgroundDown)
                : hovered ? Col(Tok::C_Checkbox_BackgroundHover)
                          : Col(Tok::C_Checkbox_Background);
    const ImVec4 bordV = on ? Col(Tok::C_Checkbox_BorderSelected)
                            : Col(Tok::C_Checkbox_Border);

    dl->AddRectFilled(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(fillV), radius);
    if (borderW > 0.01f)
        dl->AddRect(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(bordV),
                    radius, 0, borderW);

    // Tick: a two-segment polyline inside the box, drawn only when checked. The
    // points are box-relative fractions so it scales with the box size; ends are
    // snapped to half-pixels so the AA stays even (same trick as the title-bar X).
    if (on) {
        const ImU32 mark = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Checkbox_Mark));
        const float t = std::max(1.5f, std::floor(boxSz * 0.13f));   // stroke width
        auto px = [&](float fx, float fy) {
            return ImVec2(std::floor(boxMin.x + boxSz * fx) + 0.5f,
                          std::floor(boxMin.y + boxSz * fy) + 0.5f);
        };
        const ImVec2 p0 = px(0.24f, 0.52f);
        const ImVec2 p1 = px(0.42f, 0.70f);
        const ImVec2 p2 = px(0.76f, 0.30f);
        dl->AddLine(p0, p1, mark, t);
        dl->AddLine(p1, p2, mark, t);
    }

    // Label, vertically centred on the row.
    if (hasLabel) {
        const ImU32 textCol = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Default));
        dl->AddText(ImVec2(boxMax.x + gap, origin.y + (rowH - lblSz.y) * 0.5f),
                    textCol, label);
    }

    ImGui::PopID();
    return clicked;
}

} // namespace

bool Checkbox(const char* id, const char* label, bool* v) {
    return Draw(id, label, v);
}

bool CheckboxBox(const char* id, bool* v) {
    return Draw(id, "", v);
}

} // namespace UI

#pragma once

#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Token-driven checkbox (replaces ImGui::Checkbox).
//
//  The interactive ROW is one ui-unit tall (S_Size_ControlHeight) so a checkbox
//  lines up with the other controls (dropdowns, panels, frames); the drawn BOX
//  is a smaller square centred vertically in that row (Blender-style), sized by
//  component.checkbox.box-size. A trailing label, when given, sits to the right
//  of the box, vertically centred on the row.
//
//  Every colour/size is resolved from design tokens, per state:
//    • unchecked: background.color.{default,hover,down} + border.color.default
//    • checked:   background.color.{selected,selected-hover,selected-down}
//                 + border.color.selected, with the tick in mark.color.default
//
//  Returns true on toggle (and writes the new state into *v), matching the
//  ImGui::Checkbox contract.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Full checkbox: drawn box + optional trailing label. `id` must be unique within
// the current ImGui id stack (the label is NOT used for the id, so two
// same-label checkboxes still need distinct ids — pass "##something" when there
// is no visible label).
bool Checkbox(const char* id, const char* label, bool* v);

// Box-only variant (no label) for tight layouts (e.g. enum-as-toggles columns).
// The box still occupies a ui-unit-tall row so it aligns with labelled rows.
bool CheckboxBox(const char* id, bool* v);

} // namespace UI

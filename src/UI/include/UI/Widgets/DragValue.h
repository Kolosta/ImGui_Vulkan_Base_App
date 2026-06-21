#pragma once

#include <imgui.h>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Blender-style numeric drag field (replaces ImGui::DragFloat in our UI).
//
//  Behaviour (mirrors Blender's number button):
//   • One ui-unit tall (S_Size_ControlHeight); token-styled fill per state
//     (default / hover / drag) and a horizontal-resize cursor on hover.
//   • Hover shows a "−" and "+" step button inset at each end; clicking one
//     nudges the value by one step (no drag).
//   • Press-drag changes the value: the OS cursor is hidden and warped so the
//     drag is "infinite"; on release the cursor reappears EXACTLY where the
//     press began. Modifiers while dragging:
//       Shift = finer precision · Ctrl = snap to round values · Alt = stepped
//       increments from the value at press start.
//   • Press-release WITHOUT a drag enters manual text edit: an input field
//     opens showing the FULL-precision value (no display rounding), committed
//     on Enter / focus-loss. Esc or right-click cancels (restores the start
//     value), as does right-click / Esc during a drag.
//   • The displayed value is truncated to `displayDecimals` for readability
//     only; the stored value keeps full precision.
//   • An optional `unit` suffix is appended to the display ("mm", "°", "pt", …)
//     and, when the per-modifier steps are left at 0, drives sensible defaults.
//
//  Returns true on any frame the value changed (drag tick, step button, or a
//  committed manual edit). A cancelled edit/drag does NOT report a change.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct DragValueConfig {
    const char* id = "##drag";        // unique within the current ImGui id stack
    float speed = 1.0f;               // value change per pixel of drag
    float min = 0.0f;                 // clamp range; min==max (e.g. 0,0) = unbounded
    float max = 0.0f;
    int   displayDecimals = 3;        // decimals shown at rest (display only)
    const char* unit = "";            // suffix shown after the number ("mm","°",…)
    float width = 0.0f;               // 0 = use the available content width

    // Per-modifier increments. 0 = auto (derived from `speed`/`unit`/magnitude,
    // Blender-style). Override to pin exact behaviour for a given field.
    float shiftPrecision = 0.0f;      // Shift: finer speed multiplier (0 = auto ×0.1)
    float ctrlStep = 0.0f;            // Ctrl: snap-to-round increment (0 = auto)
    float altStep = 0.0f;             // Alt: stepped increment from the start value
    float buttonStep = 0.0f;          // +/- step-button increment (0 = auto = ctrlStep)
};

// Draw the field at the current cursor and edit *v in place. Returns true if the
// value changed this frame.
bool DragValue(const DragValueConfig& cfg, float* v);

} // namespace UI

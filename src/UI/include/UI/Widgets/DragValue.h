#pragma once

#include <UI/Units.h>
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
//   • UNIT-AWARE: when `quantity` is Length/Angle/Percent, `*v` is the BASE
//     value (px / degrees / 0..1 fraction). The field CONVERTS to the active
//     display unit — the maths, drag, steps and label all run in display units;
//     manual edit hides the unit, accepts a Blender-style expression with any
//     compatible unit tag, and stores back the base (see UI::Units). With
//     `quantity == Scalar` it stays a plain number with the fixed `unit` suffix.
//
//  Returns true on any frame the value changed (drag tick, step button, or a
//  committed manual edit). A cancelled edit/drag does NOT report a change.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct DragValueConfig {
    const char* id = "##drag";        // unique within the current ImGui id stack
    float speed = 1.0f;               // value change per pixel of drag (display units)
    float min = 0.0f;                 // clamp range (BASE units); min==max = unbounded
    float max = 0.0f;
    int   displayDecimals = 3;        // decimals shown at rest (display only)
    const char* unit = "";            // Scalar-only fixed suffix ("×", "px",…)
    // Draw the unit BEFORE the value instead of after it. What reads naturally
    // depends on the quantity: "12 mm" but "C 56" — a channel name prefixes the
    // amount it labels.
    bool  unitBeforeValue = false;
    // Below this width the +/- step buttons are dropped and the field is drag /
    // double-click only: at narrow sizes they eat the room the NUMBER needs,
    // and a value you cannot read is worse than one you cannot nudge. Measured
    // in ui-units; re-evaluated every frame, so they come back on their own
    // when the panel is widened.
    float minButtonsUiUnits = 4.0f;
    float width = 0.0f;               // 0 = use the available content width

    // Unit awareness. Length/Angle/Percent → `*v` is the BASE value, converted
    // to the display unit; Scalar → a plain number with the `unit` suffix.
    Units::Quantity    quantity = Units::Quantity::Scalar;
    Units::LengthScale scale    = Units::LengthScale::Normal;
    // Which unit SYSTEM to display in: useDocSystem (default) = the document
    // system; otherwise `system` (a viewport's rulers / N-panel pass their own).
    bool               useDocSystem = true;
    Units::UnitSystem  system       = Units::UnitSystem::Pixel;

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

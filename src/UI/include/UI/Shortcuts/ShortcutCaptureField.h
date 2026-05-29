#pragma once

#include <Shortcuts/Event.h>
#include <imgui.h>
#include <string>

namespace UI {

/**
 * Inline clickable field that captures a key combination, mouse button,
 * wheel direction or drag.  Click → record; press → commit (combo mode);
 * Esc cancels (combo mode only).
 *
 * The field embeds three things in a single horizontal frame:
 *
 *   [ Kbd | Mouse ]   the live recorded shortcut (key caps)        [ ▾ ]
 *      input kind                main capture area              picker
 *
 * The `▾` button on the right opens a popup whose contents depend on the
 * selected input kind, so the user does not have to physically press the
 * key/button.  In combo mode, the picker only assigns the non-modifier
 * portion of the binding.
 *
 * The field is fully driven by the design system (`component.captureField.*`
 * tokens) and updates `inout` immediately on each commit (returns true on
 * the frame the value changed).
 */
class ShortcutCaptureField {
public:
    enum class Mode {
        Combo,        // record modifiers + a non-modifier trigger
        SingleKey     // record one keyboard key (no modifiers, no mouse)
    };

    /** Selectable input kind. KeyboardOnly hides Mouse/Wheel from the
     *  picker; MouseOnly hides Keyboard.  Auto picks based on the current
     *  EventType in `inout`. */
    enum class InputKind {
        Auto = 0,
        Keyboard,
        Mouse
    };

    /** Visual override for the field border.  Use to surface error/warning
     *  states from the caller (the field itself does not know about
     *  conflicts or dangerous-binding rules).  Pass alpha=0 to skip the
     *  override and use the design-system colour. */
    enum class StatusOverride { None, Error, Warning };

    /** Render the field at current cursor position.
     *
     *  Returns true on every frame the user commits a new value (then
     *  `inout` holds the new signature).  The composite widget already
     *  handles its own ImGui ID scope. */
    static bool Render(const char* id,
                       Shortcuts::EventSignature& inout,
                       Mode mode = Mode::Combo,
                       bool withInputKindToggle = true,
                       bool withDropdown = true,
                       StatusOverride status = StatusOverride::None);

    /** Field height (logical px, scaled). */
    static float Height();
};

} // namespace UI

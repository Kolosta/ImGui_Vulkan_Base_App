#pragma once

#include <Shortcuts/Event.h>
#include <imgui.h>
#include <string>

namespace UI {

/**
 * Renders shortcut text as a sequence of small "key cap" chips.
 *
 * Style is fully driven by design tokens:
 *   component.keycap.background / border / text / radius / padding / fontScale
 *
 * Usage:
 *   UI::DrawKeyCap("Ctrl");
 *   UI::DrawShortcut(eventSignature);   // splits into multiple caps + "+"
 */
class KeyCap {
public:
    /** Render a single label as a chip and advance the cursor (uses SameLine). */
    static void DrawKeyCap(const char* label, bool useSameLine = true);

    /** Render a full shortcut signature ("Ctrl+Shift+A" → 3 chips with "+"). */
    static void DrawShortcut(const Shortcuts::EventSignature& sig, bool useSameLine = true);

    /** Like DrawShortcut but with explicit highlight (recording / conflict). */
    enum class State { Normal, Recording, ConflictSoft, ConflictHard };
    static void DrawShortcutStyled(const Shortcuts::EventSignature& sig, State state,
                                   bool useSameLine = true);

    /** Estimated bounding-box width (in current frame) of a shortcut chip group. */
    static float MeasureShortcutWidth(const Shortcuts::EventSignature& sig);
};

} // namespace UI

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
    /** Render a single label as a chip and advance the cursor (uses SameLine).
     *  `fixedHeight` > 0 forces the chip to exactly that pixel height (the
     *  label is centred inside it); 0 = size from the font + token padding and
     *  align to the current text line (the default, for inline use). */
    static void DrawKeyCap(const char* label, bool useSameLine = true,
                           float fixedHeight = 0.0f);

    /** Render a full shortcut signature ("Ctrl+Shift+A" → 3 chips with "+"). */
    static void DrawShortcut(const Shortcuts::EventSignature& sig, bool useSameLine = true,
                             float fixedHeight = 0.0f);

    /** Like DrawShortcut but with explicit highlight (recording / conflict). */
    enum class State { Normal, Recording, ConflictSoft, ConflictHard };
    static void DrawShortcutStyled(const Shortcuts::EventSignature& sig, State state,
                                   bool useSameLine = true, float fixedHeight = 0.0f);

    /** Estimated bounding-box width (in current frame) of a shortcut chip group. */
    static float MeasureShortcutWidth(const Shortcuts::EventSignature& sig);

    /** Height (px) a chip would take with the given fixed-height request (or 0
     *  for the natural font+padding height). Lets a caller (status bar) size /
     *  centre the row before drawing. */
    static float ChipHeight(float fixedHeight = 0.0f);

    // ── Absolute-position rendering (no ImGui flow) ──────────────────────────
    // For chrome like the status bar that places everything by hand. The chip
    // group is drawn with its top-left at `topLeft`, each chip exactly
    // `rowHeight` tall and the labels centred in it. Returns the total width.
    static float DrawShortcutAt(const Shortcuts::EventSignature& sig,
                                ImVec2 topLeft, float rowHeight);
    /** Width DrawShortcutAt would consume for `sig` (chips + "+" separators). */
    static float MeasureShortcut(const Shortcuts::EventSignature& sig);
};

} // namespace UI

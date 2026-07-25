#pragma once

#include <imgui.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  ToolPalette — the floating vertical tool strip pinned inside an editor
//  canvas (the Viewport's left tool column). A dedicated widget so the chrome
//  is consistent and token-driven:
//   • NO container chrome — the buttons float directly over the canvas as a
//     plain column; related tools are grouped, with a larger gap separating
//     one group from the next;
//   • every BUTTON draws its own square background, its icon centred;
//   • a MULTI-TOOL button (one button hosting several related tools) carries a
//     small corner triangle at its bottom-right; a RIGHT-CLICK on it is
//     reported so the host can open its variant menu. Single-tool buttons have
//     no triangle;
//   • the widget publishes the strip's screen rect so the host canvas can
//     exclude it from its own hit-testing (a palette click must never fall
//     through).
//
//  Buttons are NOT keyboard-nav targets (canvas shortcuts like Tab must not
//  move ImGui focus onto a tool button).
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct ToolPaletteItem {
    std::string icon;        // icon id (resources/icons)
    std::string tooltip;     // shown on hover-dwell (may be empty)
    bool        selected = false;
    bool        enabled  = true;   // false → greyed, not clickable
    // Multi-tool button: draws the corner triangle and reports right-clicks
    // (the host opens the variant menu).
    bool        hasMenu  = false;
    // Group index: consecutive items with a DIFFERENT group are separated by a
    // larger gap (related tools read as one cluster).
    int         group    = 0;
};

struct ToolPaletteResult {
    int    clicked      = -1;  // index left-clicked this frame (−1 = none)
    int    rightClicked = -1;  // index right-clicked this frame (menu request)
    ImVec2 rectMin{}, rectMax{};   // the strip's screen rect (published)
};

// Draw the palette anchored at `origin` (screen px, its top-left corner plus
// the outer margin). `id` must be unique in the current ImGui id stack.
ToolPaletteResult ToolPalette(const char* id, ImVec2 origin,
                              const std::vector<ToolPaletteItem>& items);

} // namespace UI

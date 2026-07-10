#pragma once

#include <imgui.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  ToolPalette — the floating vertical tool strip pinned inside an editor
//  canvas (the Viewport's left tool column). A dedicated widget so the chrome
//  is consistent and token-driven:
//   • the CONTAINER sizes itself from the item count (button size + margins,
//     all design tokens), with the background / border / corner radius of the
//     rest of the system;
//   • every BUTTON is centred in the strip and its icon centred in the button
//     (explicit geometry — no reliance on window padding);
//   • the widget publishes its screen rect so the host canvas can exclude it
//     from its own hit-testing (a palette click must never fall through).
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
};

struct ToolPaletteResult {
    int    clicked = -1;     // index of the item clicked this frame (−1 = none)
    ImVec2 rectMin{}, rectMax{};   // the container's screen rect (published)
};

// Draw the palette anchored at `origin` (screen px, its top-left corner plus
// the outer margin). `id` must be unique in the current ImGui id stack.
ToolPaletteResult ToolPalette(const char* id, ImVec2 origin,
                              const std::vector<ToolPaletteItem>& items);

} // namespace UI

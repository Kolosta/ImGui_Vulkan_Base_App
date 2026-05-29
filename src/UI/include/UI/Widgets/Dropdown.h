#pragma once

#include <imgui.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Reusable Blender-style dropdown.
//
//  The trigger shows: icon (left) + label + small chevron (right). Clicking it
//  opens a menu that, by default, is left-aligned to the trigger's left edge and
//  opens downward. The trigger's bottom corners and the menu's top corner on the
//  join side lose their rounding so the two visually merge. When the menu would
//  leave the screen, it flips (open leftward / upward) and the merged corners
//  follow the chosen direction.
//
//  The menu is data-driven: items carry an optional column group, so a single
//  config can render a multi-column menu (e.g. the editor selector's
//  General / Animation / Data columns). Every dimension and colour is resolved
//  from design tokens (component.dropdown.* / component.menu.*).
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct DropdownItem {
    const char* icon = "";       // icon id (may be empty)
    std::string label;
    std::string shortcut;        // right-aligned hint text (may be empty)
    int  columnGroup = 0;        // column index when columnHeaders is set
    bool enabled     = true;
};

struct DropdownConfig {
    const char* id = "##dropdown";              // unique ImGui id
    const char* triggerIcon = "";               // icon shown in the trigger
    std::string triggerLabel;                   // text shown in the trigger
    std::vector<std::string> columnHeaders;     // empty => single-column list
    std::vector<DropdownItem> items;
    int selectedIndex = -1;                     // highlighted item, -1 = none
};

struct DropdownResult {
    bool changed  = false;       // an item was clicked this frame
    int  selected = -1;          // index of the clicked item (valid if changed)
};

// Draw the trigger at the current cursor position and, if open, its menu.
// Returns whether an item was picked this frame.
DropdownResult Dropdown(const DropdownConfig& cfg);

} // namespace UI

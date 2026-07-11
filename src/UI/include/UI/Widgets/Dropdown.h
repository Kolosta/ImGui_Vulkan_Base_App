#pragma once

#include <imgui.h>
#include <functional>
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

// Visual style of the trigger. Default = a filled, bordered chip with a
// chevron. Minimal = a borderless, transparent trigger with no chevron and only
// a subtle hover fill — used for menu-bar-style dropdowns (e.g. the title bar's
// File/Edit/Windows menus) that should read as flat menu items, not chips.
enum class DropdownStyle {
    Default,
    Minimal,
};

struct DropdownItem {
    const char* icon = "";       // icon id (may be empty)
    std::string label;
    std::string shortcut;        // right-aligned hint text (may be empty)
    int  columnGroup = 0;        // column index when columnHeaders is set
    bool enabled     = true;
    // NB: keep `tooltip` LAST so existing brace-init aggregates
    // `{icon,label,shortcut,columnGroup,enabled}` keep mapping their trailing
    // int/bool to columnGroup/enabled (not to a std::string → null crash).
    std::string tooltip;         // description shown on hover-dwell (Blender-like)
};

// A button FUSED to the dropdown trigger (ButtonGroup-style: shared border, only
// the group's outer corners rounded). Placed left or right of the trigger. The
// button is independent of the menu (e.g. a magnet toggle next to a Snap dropdown).
struct DropdownButton {
    std::string id;              // unique within the dropdown
    const char* icon = "";       // icon id (optional)
    std::string label;           // text (optional; icon-only if empty)
    std::string tooltip;
    bool        active = false;  // toggled/accented state
    enum class Side { Left, Right } side = Side::Left;
};

struct DropdownConfig {
    const char* id = "##dropdown";              // unique ImGui id
    const char* triggerIcon = "";               // icon shown in the trigger
    std::string triggerLabel;                   // text shown in the trigger
    std::vector<std::string> columnHeaders;     // empty => single-column list
    std::vector<DropdownItem> items;
    int selectedIndex = -1;                     // highlighted item, -1 = none
    DropdownStyle style = DropdownStyle::Default;
    // Single-column lists: a search field at the top of the menu (keyboard
    // focused on open) filters the items live. Long lists scroll inside the
    // menu regardless (see component.menu.max-height.default) — searchable
    // just forces the scrolling layout even when the list would fit.
    bool searchable = false;
    // Buttons fused to the trigger (ButtonGroup look). Rendered left/right per side.
    std::vector<DropdownButton> buttons;
    // CUSTOM BODY: when set, the popup renders THIS instead of the item list (the
    // body is responsible for its own widgets). The menu chrome (bg/border/merged
    // corner) is still drawn by the dropdown; the callback runs inside the popup
    // with the cursor at the menu's content origin. `menuSize` sizes the popup.
    std::function<void()> bodyDraw;
    ImVec2 menuSize{0, 0};                      // required when bodyDraw is set
};

struct DropdownResult {
    bool changed  = false;       // an item was clicked this frame
    int  selected = -1;          // index of the clicked item (valid if changed)
    // Index into cfg.buttons of a fused button clicked this frame, or -1.
    int  buttonClicked = -1;
};

// Draw the trigger at the current cursor position and, if open, its menu.
// Returns whether an item was picked this frame.
DropdownResult Dropdown(const DropdownConfig& cfg);

} // namespace UI

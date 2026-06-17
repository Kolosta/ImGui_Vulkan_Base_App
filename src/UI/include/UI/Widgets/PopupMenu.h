#pragma once

#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  PopupMenu — the single-column menu rendering shared by the Dropdown widget
//  and by free-floating context menus. Every dimension and colour comes from the
//  design-system menu tokens (component.menu.*), so all menus look identical.
//
//  Two entry points:
//   • MenuBody(...)  — low-level: draw the rows of an already-open popup window
//     onto its draw list, with a caller-chosen corner-rounding flag (the
//     Dropdown squares the join-side corner; a floating menu rounds all four).
//     Returns the clicked row index, or -1.
//   • ContextMenu(...) — high-level: open + render a floating menu at a screen
//     position from a list of MenuEntry (supports one level of sub-menus and a
//     shortcut hint per row). Handles BeginPopup/positioning/flip. The caller
//     calls OpenContextMenu() once to request it, then ContextMenu() every frame.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Measure + draw the menu rows for an OPEN popup. `items` is parallel arrays via
// the small struct below. Drawn on the current window's draw list inside the
// rect [pos, pos+size]. Returns the index clicked this frame (-1 = none).
struct MenuRow {
    const char* icon = "";
    std::string label;
    std::string shortcut;     // right-aligned hint (may be empty)
    bool        enabled = true;
    bool        hasSubmenu = false;
};

// Compute the menu size for a set of rows (single column). Mirrors the Dropdown
// measurement so a floating menu and a dropdown menu size identically.
ImVec2 MeasureMenu(const std::vector<MenuRow>& rows);

// Draw the rows of an open popup onto `dl` within [pos, pos+size]; `menuRound`
// selects which corners are rounded. Returns the clicked enabled row, else -1.
// `hoveredOut` (optional) receives the hovered row index (-1 if none).
// `drawBorder` draws the menu bg + outline; pass false when the caller already
// frames the whole card (e.g. a titled context menu) so the body adds no extra
// top edge (which would read as a second separator under the title).
int MenuBody(ImDrawList* dl, ImVec2 pos, ImVec2 size,
             const std::vector<MenuRow>& rows, ImDrawFlags menuRound,
             int selectedIndex = -1, int* hoveredOut = nullptr,
             bool drawBorder = true,
             // true  → activate on mouse RELEASE (main popup body, covered window);
             // false → activate on mouse PRESS (submenu on foreground draw list,
             //         whose press would otherwise close the parent popup first).
             bool onRelease = true);

// ── High-level floating context menu with one level of sub-menus ─────────────
struct MenuEntry {
    std::string            label;
    std::string            shortcut;
    std::string            tooltip;        // description shown on hover-dwell
    const char*            icon = "";
    bool                   enabled = true;
    std::function<void()>  onClick;        // leaf action (empty if submenu)
    std::vector<MenuEntry> submenu;        // non-empty → this row opens a submenu
};

// Render a floating context menu (call every frame). `popupId` must be opened
// via ImGui::OpenPopup(popupId) the frame the menu should appear. `screenPos`
// anchors it. `title`, if non-empty, draws a header row + separator at the top
// (floating menus carry a title; the Dropdown menu does not). Returns true while
// the popup is open (so callers can suppress click-through). Styled identically
// to the Dropdown menu.
bool ContextMenu(const char* popupId, ImVec2 screenPos,
                 const std::vector<MenuEntry>& entries,
                 const char* title = nullptr);

// The ONE tooltip used everywhere (Blender-style description popups). Drawn on
// the FOREGROUND draw list so it sits above sub-menus / everything, with the
// component.tooltip.* style (bg = editor menu bar, control corner radius, thin
// border honouring the global toggle, real padding). `text` may be multi-line.
// `anchor` is a screen point near which to place it (the row being hovered);
// the box is offset slightly and clamped to the viewport. Call when the dwell
// delay has elapsed.
void DrawTooltip(const char* text, ImVec2 anchor);

// Same as DrawTooltip but the background (and border) alpha are scaled by
// `bgAlpha` (0..1), for a translucent variant — used by the Outliner's
// "synchronise with a viewport" prompt so the viewports stay readable beneath.
void DrawTooltipTranslucent(const char* text, ImVec2 anchor, float bgAlpha);

} // namespace UI

#pragma once

#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Blender-style nested collapsible "panel".
//
//  Layout/behaviour (per the design spec):
//   • The header is FLAT — no accent, no colour change on hover/select.
//   • Body background darkens with nesting DEPTH so deeper sub-panels recede.
//   • LEVEL-1 panels are visually separate: a vertical gap before each, a
//     filled background and a 1px border.
//   • Children of a level-1 panel are flush (NO gap, NO border) — they live
//     directly inside the parent, exactly like Blender's panels.
//   • Optional override badge on the right; clicking it requests a recursive
//     reset of every override under this panel.
//
//  Usage (depth is tracked automatically by Begin/End nesting):
//      UI::PanelResult r = UI::BeginPanel(cfg);
//      ... optional header inline content (drawn whether open or not) ...
//      if (r.open) { ... child panels / property rows ... }
//      UI::EndPanel();                       // ALWAYS call EndPanel()
//
//  EndPanel() is ALWAYS paired with BeginPanel() (open or collapsed): the panel
//  body is a child window that exists in both states (header-only when
//  collapsed), so any header inline editor stays inside it and is never covered
//  by a following panel.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct PanelConfig {
    const char* id = "##panel";    // unique within the current ImGui id stack
    const char* label = "";        // header text
    const char* icon = "";         // optional leading icon id (after the chevron)
    bool defaultOpen = false;
    // Override badge: shown on the right when hasOverride is true. The returned
    // PanelResult.resetClicked tells the caller to reset overrides recursively.
    bool hasOverride = false;
    // Optional right-aligned inline content width reserved in the header (e.g.
    // an inline editor for a property panel). 0 = none.
    float headerInlineWidth = 0.0f;
    // FLAT body: keep the PARENT's body colour instead of darkening with depth,
    // so a sub-panel reads as flush with the surface it sits on (the Properties
    // editor wants every section to share the level-1 surface). Level-1 panels
    // ignore this (they already own the top surface).
    bool flatBody = false;
    // CLOSE button: a small cross at the header's right edge. When clicked,
    // PanelResult.closeClicked is set (the caller removes the item). The slot is
    // reserved so the label/inline area never shifts.
    bool closable = false;
};

struct PanelResult {
    bool open = false;          // body is expanded this frame
    bool resetClicked = false;  // the override badge was clicked
    bool closeClicked = false;  // the close cross was clicked (closable panels)
    // Screen-space rect of the header's right-hand inline area (valid when
    // headerInlineWidth > 0): the caller can draw an inline editor there.
    ImVec2 inlineMin{};
    ImVec2 inlineMax{};
};

// Open a panel at the current nesting depth. Returns the result; when
// result.open is true the caller MUST emit body content and call EndPanel().
PanelResult BeginPanel(const PanelConfig& cfg);
void EndPanel();

// Current panel nesting depth (0 = top level / before any BeginPanel). Useful
// for callers that need depth-aware spacing of their own content.
int PanelDepth();

// Horizontal offset (in pixels, DPI-scaled) of a panel header's TEXT from the
// left edge of that panel's body, for a panel opened at `depth`. Callers that
// emit their own body content can use this to align flush with a sibling
// sub-panel's header label (e.g. a token-id line aligned with the nested
// "Resolution chain" panel header). `depth` is the would-be panel depth — i.e.
// PanelDepth() + 1 for a sub-panel begun inside the current body.
float PanelHeaderTextIndent(int depth);

// ─────────────────────────────────────────────────────────────────────────────
//  Reorderable panel list — Blender's modifier-stack interaction, reusable.
//
//  A run of SIBLING panels the user can reorder by dragging their headers
//  (neighbours shift live under the drag) and remove via the header cross:
//
//      UI::PanelListEdit edit;
//      for (int i = 0; i < n; ++i) {
//          UI::PanelConfig cfg; cfg.id = "##item"; cfg.label = ...;
//          if (UI::BeginPanelListItem(cfg, i, n, edit).open) { ...rows... }
//          UI::EndPanelListItem();
//      }
//      if (edit.removeAt >= 0)      items.erase(items.begin() + edit.removeAt);
//      else if (edit.moveFrom >= 0) std::swap(items[edit.moveFrom],
//                                             items[edit.moveTo]);
//
//  The list owns the per-item ImGui ids (do NOT PushID around items) and tracks
//  the drag at LIST level, so a multi-step drag keeps working while items swap
//  underneath, and each panel's open/closed state FOLLOWS its item. The header
//  toggles open/closed on a press RELEASED without dragging — release, not
//  click, is what lets drag and expand share the header. The dragged panel gets
//  an accent outline. Every item of one list must use the same cfg.id; one list
//  per id scope.
// ─────────────────────────────────────────────────────────────────────────────
struct PanelListEdit {
    int removeAt = -1;               // index whose close cross was clicked
    int moveFrom = -1, moveTo = -1;  // swap these two sibling indices
};

// Begin item `index` of `count`. cfg.closable/flatBody are honoured (set them
// for the Blender look); the drag handle is always on. Pair with
// EndPanelListItem() (NOT EndPanel).
PanelResult BeginPanelListItem(const PanelConfig& cfg, int index, int count,
                               PanelListEdit& edit);
void EndPanelListItem();

} // namespace UI

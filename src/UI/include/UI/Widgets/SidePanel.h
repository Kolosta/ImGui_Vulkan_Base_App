#pragma once

#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  EditorSidePanel — a Blender-style, reusable right-side panel for ANY editor.
//
//  It overlays the editor canvas (semi-transparent), with a vertical tab bar on
//  its RIGHT edge and the active tab's content to its left. Two reveal stages
//  dragged from a small translucent handle near the top-right:
//    • stage 1 → only the tab bar (one ui-unit wide),
//    • stage 2 → the full content panel (resizable from the left edge).
//  Dragging right collapses; a tab click opens stage 2 (or, if already open on
//  that tab, retracts back to the tab bar). The host key (e.g. a toggle) can flip
//  stage 2 ⇄ 0 directly.
//
//  Tabs look like a zone tab-bar rotated vertical: concave fillets on the LEFT
//  side (toward the panel), hover feedback, vertical text reading bottom→top.
//  Colours: panel + selected/hovered tab = editor menu-bar colour; the tab bar
//  strip = editor base background; both at the same opacity so the canvas shows
//  through.
//
//  STATE is owned by the caller (so it persists per editor/leaf). CONTENT is a
//  callback drawing into the given screen rect. The widget is purely UI; nothing
//  editor-specific lives here.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

struct SidePanelState {
    int   stage = 0;        // 0 closed, 1 tab-bar only, 2 full panel
    float width = 320.0f;   // full-panel width (px), resizable
    int   tab   = 0;        // active tab index
    // Identity of the active tab BY NAME: the host reconciles `tab` against it
    // when the tab list composition changes (tabs appear/disappear with modes),
    // so the selection never silently jumps to a different tab.
    std::string tabName;
    // OUTPUT (filled by EditorSidePanel each frame): the screen rects the panel
    // ACTUALLY occupies — the tab-bar column (full height; the closed handle at
    // stage 0) and the height-FITTED content panel incl. its resize grip (zero
    // when absent). The host excludes exactly these from its canvas
    // hit-testing, not a full-height band (the content panel auto-fits its
    // content, so the canvas below it stays live).
    ImVec4 outBarRect{ 0, 0, 0, 0 };
    ImVec4 outPanelRect{ 0, 0, 0, 0 };
};

struct SidePanelTab {
    std::string name;
    std::function<void(ImVec2 contentMin, ImVec2 contentMax)> draw;  // tab body
};

// Render the side panel over the canvas rect [cMin, cMax] (screen px). `id` must
// be unique per panel instance. `tabs` may be empty → the panel stays hidden and
// stage is forced to 0. Call once per frame, after drawing the canvas.
void EditorSidePanel(const char* id, ImVec2 cMin, ImVec2 cMax,
                     SidePanelState& st, const std::vector<SidePanelTab>& tabs);

// The horizontal extent (px) the panel currently occupies on the RIGHT of the
// canvas [cMin,cMax]: 0 when closed, the tab-bar width at stage 1, the (clamped)
// full width at stage 2. Lets a host editor exclude that band from its own canvas
// hit-testing so clicks on the panel don't also drive the canvas underneath.
// Must match EditorSidePanel's own width logic (same tokens / clamps).
float SidePanelOccupiedWidth(const SidePanelState& st, ImVec2 cMin, ImVec2 cMax);

} // namespace UI

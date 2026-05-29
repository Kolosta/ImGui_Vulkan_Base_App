#pragma once

#include <string>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Chevron-based collapsible widgets.
//
//  ImGui draws its own triangle for TreeNode/CollapsingHeader internally. To
//  use our SVG chevrons (navigation/chevron-*) without patching ImGui, these
//  helpers reimplement the open/close affordance with a chevron icon:
//    • chevron-right  when collapsed,
//    • chevron-down   when expanded.
//  They keep ImGui's per-id open/close state (so they persist like a normal
//  tree node) and are drop-in replacements in our own UI code. An optional
//  leading content icon can be drawn after the chevron.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Collapsible section header (replaces ImGui::CollapsingHeader). Returns true
// when expanded. `id` makes the open state unique; `icon` (icon id, may be
// empty) is drawn between the chevron and the label.
bool IconCollapsingHeader(const char* id, const char* label,
                          const char* icon = "",
                          bool defaultOpen = false);

// Tree node (replaces ImGui::TreeNodeEx for leaf-less branches). Caller must
// ImGui::TreePop() when it returns true, exactly like TreeNodeEx.
bool IconTreeNode(const char* id, const char* label,
                  bool defaultOpen = false);

// Draw an icon (or nothing if `icon` is empty / unknown) sized to the current
// line, then advance the cursor by `size`+spacing on the same line. Used by
// the menu bar to put an icon left of each item's text.
void InlineIcon(const char* icon, float size, const ImVec4& tint);

// Menu item with an icon left of the label (replaces ImGui::MenuItem). The
// icon slot is always reserved so labels stay aligned even when `icon` is
// empty / unknown (then nothing is drawn there — never a checkmark).
// `selected` only affects the row highlight, NOT a checkmark glyph.
bool IconMenuItem(const char* icon, const char* label,
                  const char* shortcut = nullptr, bool selected = false,
                  bool enabled = true);

} // namespace UI

#pragma once

#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ScrollArea — a scrollable child with a Blender-style OVERLAY scrollbar.
//
//  Why not the native ImGui scrollbar: ImGui always reserves `ScrollbarSize` of
//  the work rect when its scrollbar is visible (imgui.cpp, Begin: ScrollbarSizes
//  → DecoOuterSizeX2), so the content shifts when it appears, and ImGui only
//  reacts to a hover over the grab — never to cursor *proximity*. This wrapper
//  instead hides the native bar (NoScrollbar) and draws its own grab in the
//  child's right margin:
//    • ZERO reserved space — the grab floats in the margin, the content never
//      shifts whether it scrolls or not;
//    • appears only when the content overflows (GetScrollMaxY() > 0);
//    • grows from a thin rest width to a thicker hover width and brightens as
//      the cursor nears it (proximity), and while dragging;
//    • the mouse wheel keeps working (NoScrollWithMouse is NOT set).
//
//  All metrics/colours come from design tokens (component.scrollbar.overlay.*,
//  reusing the scrollbar grab colour tokens). Use exactly like BeginChild:
//
//      if (UI::BeginScroll("##id", ImVec2(0,0))) {
//          ... content ...
//      }
//      UI::EndScroll();   // always call, even when BeginScroll returned false
//
//  BeginScroll returns the BeginChild visibility (true if not clipped), matching
//  ImGui::BeginChild semantics; EndScroll must always be paired with it.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Begin a scrollable child with an overlay scrollbar. `extraFlags` are OR'd onto
// the window flags (NoScrollbar is always forced). Mirrors ImGui::BeginChild.
bool BeginScroll(const char* id, const ImVec2& size = ImVec2(0, 0),
                 ImGuiChildFlags childFlags = 0,
                 ImGuiWindowFlags extraFlags = 0);

// End a scrollable child opened with BeginScroll. Draws + handles the overlay
// scrollbar, then calls EndChild. Always pair with BeginScroll.
void EndScroll();

} // namespace UI

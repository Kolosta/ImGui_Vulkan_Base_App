#pragma once

#include <imgui.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Live row reorder — the PRESENTATION half.
//
//  For any list whose rows are positioned by index at a fixed pitch (the
//  Outliner, the Palette). While a row is dragged it should read as physically
//  picked up: its band follows the cursor and the other rows step aside to open
//  the slot it will land in, animated — rather than the row sitting still while
//  a detached label trails the mouse.
//
//  This deliberately does NOT own the gesture. ImGui's drag & drop still starts
//  the drag and still decides what a drop MEANS, which is what keeps the
//  Outliner's richer rules intact — dropping INTO a collection, clipping or
//  masking onto a layer, copying a modifier onto another object are not
//  reorders and never were. All this answers is "where is the grabbed row
//  drawn" and "how far does each other row move", and it animates the answer.
//
//  ── The one-frame lag ──
//  The landing slot is published by whichever drop target is under the cursor,
//  and that happens PART WAY THROUGH the row loop — too late for the rows
//  already drawn this frame. So the layout uses the slot published on the
//  PREVIOUS frame. The animation is interpolating towards it anyway, so the lag
//  is not observable.
//
//  ── Usage ──
//      UI::RowDrag rd("##outlinerRows", (int)rows.size(), stripeH);
//      for each row i:
//          cfg.bandOffsetY = rd.Offset(i);        // band + content, not zebra
//          … inside the drag SOURCE block:  rd.SetSource(i);
//          … inside the drop TARGET block:  rd.SetLandingAtBoundary(b);
//                                      or:  rd.SetNoGap();   // not a reorder
//      draw row rd.Source() LAST, so the floating band passes OVER its
//      neighbours instead of under them.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

class RowDrag {
public:
    // `pitch` is the row stripe height; `count` the number of rows in the flat
    // list this frame. State is kept in the window's ImGuiStorage under `id`.
    RowDrag(const char* id, int count, float pitch);

    bool Active() const { return src_ >= 0; }
    // Flat index of the grabbed row, or -1. Draw this one last.
    int  Source() const { return src_; }

    // Rows of UNEQUAL height (a list where an entry can expand in place): pass
    // every row's height, in index order, before reading any offset. Without
    // it every row is `pitch` tall. Heights must describe the list as it is
    // being drawn this frame — during a drag nothing can expand, so a caller
    // whose rows are measured rather than known may safely feed last frame's.
    void SetRowHeights(const float* heights, int count);

    // Called from the drag source while the payload is alive.
    void SetSource(int index);

    // Where the dragged row will END UP, as a final slot index.
    void SetLanding(int index);
    // The same thing expressed as an INSERTION BOUNDARY — the gap between rows
    // b-1 and b — which is how a reorder drop is naturally described ("above
    // this row" = boundary b, "below it" = b+1). Removing the row before
    // re-inserting it shifts everything below, and this does that conversion so
    // callers never have to.
    void SetLandingAtBoundary(int boundary);
    // The hovered drop is not a reorder (drop INTO a collection with no chosen
    // position, clip / mask, a refused target): the list stays put.
    void SetNoGap();

    // This frame's vertical offset for row `index` — the animated step-aside
    // for the others, the raw cursor delta for the grabbed one.
    float Offset(int index) const;
    // The same, but for the grabbed row it gives the SLOT it is heading for
    // rather than where the cursor is holding it. Anything that has to reason
    // about the arrangement itself — tree guides spanning a parent's children —
    // wants this, not the floating band.
    float GapOffset(int index) const;
    // The slot `index` ends up in once the drop lands. Idle, it is `index`.
    int   Slot(int index) const;

    // Call once after the row loop. Forgets the drag when the payload is gone
    // and commits the landing published during the frame.
    void End();

    // The opened slot, drawn as the very shape it is holding open: the row
    // band, same corners, in the drop colour at the carried-row opacity. An
    // empty gap says something is coming; this says WHAT.
    static void DrawSlot(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius);

private:
    ImGuiStorage* st_ = nullptr;
    ImGuiID kSrc_ = 0, kLand_ = 0, kAnim_ = 0;
    int   count_ = 0;
    int   src_ = -1;      // grabbed row, -1 when idle
    int   land_ = -1;     // landing slot the layout is animating towards
    int   newLand_ = -1;  // published this frame, applied by End()
    float pitch_ = 0.0f;
    float anim_ = 0.0f;   // eased landing slot (fractional)
    std::vector<float> h_;      // per-row heights (all `pitch` by default)
    mutable std::vector<float> off_;   // this frame's offsets, built on demand
    mutable bool built_ = false;

    void Build() const;
    std::vector<float> Arrange(int land) const;
};

} // namespace UI

#pragma once

#include <imgui.h>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  Generic zebra list-row primitive (reusable across editors — Outliner, future
//  list panels, …). It owns the per-row GEOMETRY and CHROME; callers draw their
//  own content (icons, labels, …) inside the band.
//
//  Vertical model (the important part):
//    • The coloured SELECTION-PREVIEW band is exactly one ui-unit tall.
//    • The zebra STRIPE (and the full-row HIT zone) is the band + 1px top + 1px
//      bottom, so the whole stripe — including those 1px margins — reacts to
//      hover/click, but only the inner ui-unit band changes colour.
//    • Consecutive rows tile with NO gap: the row pitch == the stripe height.
//
//  Horizontal model:
//    • The HIT zone spans the whole editor width (edge to edge, under the overlay
//      scrollbar gutter too) — no dead zone.
//    • The coloured band is inset on the LEFT by `bandMarginLeft` and reaches
//      `bandRight` on the right (the caller passes the scrollbar-gutter edge),
//      with rounded corners. Content is laid out from `contentX` (band left).
//
//  Usage (RAII):
//      UI::ListRowConfig cfg; cfg.id = rowId; cfg.zebraOdd = (i & 1);
//      cfg.selected = …; cfg.active = …; cfg.hovered/… filled by Begin;
//      cfg.colors = {…};                       // per-state band colours
//      UI::ListRow row(cfg);
//      // row.Hovered() / row.Clicked() / … for input
//      ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
//      … draw content (use row.BandLeft/BandRight/RowTop/RowH for placement) …
//      // destructor advances the layout cursor by exactly one stripe.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Per-state band colours (ImU32, premultiplied alpha as ImGui expects). A 0 (=
// fully transparent) means "no band drawn for this state".
struct ListRowColors {
    ImU32 hover         = 0;
    ImU32 selected      = 0;
    ImU32 selectedHover = 0;
    ImU32 active        = 0;
    ImU32 activeHover   = 0;
    ImU32 idle          = 0;   // an always-on tint even when not hovered/selected
};

struct ListRowConfig {
    ImGuiID id        = 0;     // unique row id (for the hit InvisibleButton)
    bool    zebraOdd  = false; // true → draw the (lighter) zebra stripe
    ImU32   zebraColor= 0;     // stripe fill for odd rows (0 = none)
    bool    selected  = false;
    bool    active    = false;
    float   bandMarginLeft = 0.0f;  // left inset of the coloured band + content
    float   cornerRadius   = 0.0f;
    // Slides the coloured band AND the row's content, leaving the zebra stripe
    // and the hit zone where they are. This is what a live reorder moves: the
    // stripe is a ruling of the list, not part of the item, so the row a user
    // has picked up leaves an empty stripe behind and the rows stepping aside
    // slide within a pattern that stays still. See UI/Widgets/RowDrag.h.
    float   bandOffsetY    = 0.0f;
    // When set, the zebra STRIPE is drawn into channel 0 of this splitter and
    // everything else into channel 1. A displaced row would otherwise slide
    // UNDER the stripe of a row drawn after it and disappear: the stripes are
    // one continuous ruling of the list, so they all have to be laid down
    // before any row's band. The caller owns the splitter (one Split / Merge
    // around the whole list) - see UI/Widgets/RowDrag.h.
    ImDrawListSplitter* bgSplitter = nullptr;
    // This row is the one being carried. It keeps its selected colour if it had
    // one and takes a neutral fill if it had none - a row with no background at
    // all reads as nothing being picked up - and both go SEE-THROUGH, so an
    // insertion line or the rows underneath stay readable while it passes over
    // them. Colours and opacity are token-driven (component.list-row.drag.*).
    bool    dragging = false;
    ListRowColors colors;
    // Mouse buttons the hit zone reacts to.
    bool    wantRightClick = true;
};

// Result of the row's full-stripe hit test (geometric, so the 1px margins count).
struct ListRowInput {
    bool hovered       = false;
    bool pressed       = false;  // left mouse-down on the row this frame
    bool clicked       = false;  // left release without a drag (selection click)
    bool rightClicked  = false;
    bool doubleClicked = false;
};

class ListRow {
public:
    explicit ListRow(const ListRowConfig& cfg);
    ~ListRow();
    ListRow(const ListRow&) = delete;
    ListRow& operator=(const ListRow&) = delete;

    const ListRowInput& Input() const { return in_; }

    // Geometry (screen space).
    float RowTop()    const { return top_; }     // band top Y (content centres here)
    float RowH()      const { return bandH_; }   // band height = one ui-unit
    float StripeTop() const { return stripeTop_; }            // zebra stripe top Y
    float StripeBottom() const { return stripeTop_ + pitch_; }// zebra stripe bottom Y
    float BandLeft()  const { return bandL_; }   // coloured band left X (full width)
    float BandRight() const { return bandR_; }   // coloured band right X
    float ContentX()  const { return contentX_; } // indented content start X

    // Vertically centre an item of height `h` on the band; returns its top Y.
    float CenterY(float h) const { return top_ + (bandH_ - h) * 0.5f; }

    // Suppress the row's own click/press (e.g. when an inline button under the
    // cursor should win). Call after construction, before reading Input().
    void SuppressInputIn(float x0, float x1);

private:
    ListRowInput in_;
    float stripeTop_ = 0;  // stripe (zebra/hit) top Y
    float top_ = 0;        // band top Y (= RowTop; content centres on the band)
    float bandH_ = 0, bandL_ = 0, bandR_ = 0, contentX_ = 0;
    float pitch_ = 0;       // full stripe height (= row advance)
    ImVec2 restoreCursor_{};
};

// Reset/seed the per-frame zebra parity counter (call once before a list).
void ListRowResetZebra();
// The current zebra parity (advances per ListRow). Callers that draw their own
// trailing fill (e.g. zebra to the bottom of the panel) read/advance it.
int  ListRowZebraIndex();
void ListRowAdvanceZebra();

// Geometry helpers (so callers can size trailing fills / columns consistently).
float ListRowBandHeight();   // one ui-unit (S_Size_ControlHeight) × band scale
float ListRowStripeHeight(); // band + 2px (the zebra/hit/pitch height)

// Per-frame band-height multiplier (default 1). The Outliner's Layers view sets it
// to ~2.5 so each row is tall enough to show an isolated render preview, then resets
// it. Affects every ListRow drawn while active — set/reset around a section.
void  ListRowSetBandScale(float s);
float ListRowBandScale();

} // namespace UI

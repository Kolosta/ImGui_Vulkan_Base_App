#include <UI/Widgets/ListRow.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

int s_zebra = 0;   // per-frame stripe parity (reset by ListRowResetZebra)
float s_bandScale = 1.0f;   // extra multiplier on the band height (Layers previews ×2.5)

// One ui-unit (the coloured band height); the stripe is this + 2px (1px margins).
float BandH() {
    auto& ds = DS::DesignSystem::Instance();
    return ds.GetFloat(Tok::S_Size_ControlHeight) * ds.GetGlobalScale() * s_bandScale;
}
float StripeMargin() { return 1.0f; }   // px above + below the band (per side)
} // namespace

float ListRowBandHeight()   { return BandH(); }
float ListRowStripeHeight() { return BandH() + 2.0f * StripeMargin(); }

void ListRowResetZebra()     { s_zebra = 0; }
int  ListRowZebraIndex()     { return s_zebra; }
void ListRowAdvanceZebra()   { ++s_zebra; }
void  ListRowSetBandScale(float s) { s_bandScale = (s > 0.0f) ? s : 1.0f; }
float ListRowBandScale()           { return s_bandScale; }

ListRow::ListRow(const ListRowConfig& cfg) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGuiIO& io = ImGui::GetIO();

    bandH_ = BandH();
    pitch_ = ListRowStripeHeight();
    const float margin = StripeMargin();

    // The current cursor Y is the STRIPE top. The visible BAND (and all content)
    // sits `margin` px below it; `top_` (RowTop) is that band top so callers centre
    // content on the visible row, not the slightly-taller stripe.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    stripeTop_ = p0.y;
    top_ = stripeTop_ + margin;
    const float editorL = w->WorkRect.Min.x;
    const float editorR = w->WorkRect.Max.x + ImGui::GetStyle().ScrollbarSize; // under the gutter
    bandL_ = w->WorkRect.Min.x + cfg.bandMarginLeft;
    bandR_ = w->WorkRect.Max.x;                       // up to the scrollbar gutter
    // The CONTENT is indented per the ImGui indent already applied (p0.x carries
    // WorkRect.Min.x + DC.Indent.x). The coloured band stays full-width; only the
    // content (icons/label) shifts right by the indent.
    const float indent = std::max(0.0f, p0.x - editorL);
    contentX_ = bandL_ + indent;

    // ── Zebra stripe (full width, full stripe height) ────────────────────────
    if (cfg.zebraOdd && cfg.zebraColor)
        w->DrawList->AddRectFilled(ImVec2(editorL, stripeTop_),
                                   ImVec2(editorR, stripeTop_ + pitch_), cfg.zebraColor);

    // ── Full-width hit zone ──────────────────────────────────────────────────
    // Its LAYOUT height is the stripe height minus one ItemSpacing.y, so the row
    // pitch (item + post-spacing) equals exactly the stripe height → uniform
    // tiling. Detection is widened geometrically over the WHOLE stripe below so
    // the 1px margins still register (no dead gap between bands).
    ImGui::SetCursorScreenPos(ImVec2(editorL, stripeTop_));
    char bid[24]; std::snprintf(bid, sizeof bid, "##lr%llu", (unsigned long long)cfg.id);
    const float itemH = std::max(1.0f, pitch_ - ImGui::GetStyle().ItemSpacing.y);
    ImGui::SetNextItemAllowOverlap();
    ImGuiButtonFlags bf = ImGuiButtonFlags_MouseButtonLeft |
                          (cfg.wantRightClick ? ImGuiButtonFlags_MouseButtonRight : 0);
    ImGui::InvisibleButton(bid, ImVec2(std::max(1.0f, editorR - editorL), itemH), bf);

    const bool itemHov = ImGui::IsItemHovered();
    // Geometric full-stripe hover (fills the gap the shorter item leaves). It must
    // NOT fire while ANY popup/menu is open — geometric tests bypass ImGui's popup
    // modality, so without this guard a click on a context menu drawn OVER the list
    // would also hit the row behind it. (The InvisibleButton above is already
    // modality-aware; only this geometric path needs the guard.)
    const bool anyPopup = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool inStripe = !anyPopup &&
        io.MousePos.x >= editorL && io.MousePos.x < editorR &&
        io.MousePos.y >= stripeTop_ && io.MousePos.y < stripeTop_ + pitch_ &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        !ImGui::IsAnyItemHovered();
    in_.hovered = itemHov || inStripe;
    in_.pressed = ImGui::IsItemActivated() ||
                  (inStripe && ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    in_.clicked = (ImGui::IsItemDeactivated() && itemHov &&
                   !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) ||
                  (inStripe && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                   !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left));
    in_.rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                       (inStripe && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    in_.doubleClicked = (itemHov || inStripe) &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    // ── Coloured selection-preview band (ui-unit tall, inset 1px in the stripe) ─
    ImU32 band = 0;
    const ListRowColors& c = cfg.colors;
    if (cfg.selected && cfg.active) band = in_.hovered ? c.activeHover : c.active;
    else if (cfg.selected)          band = in_.hovered ? c.selectedHover : c.selected;
    else if (in_.hovered)           band = c.hover;
    else                            band = c.idle;
    if (band) {
        ImVec2 a(bandL_, top_);                 // top_ is already the band top
        ImVec2 b(bandR_, top_ + bandH_);
        w->DrawList->AddRectFilled(a, b, band, cfg.cornerRadius);
    }

    // Rewind to the (indented) content origin at the band top so the caller's
    // content centres on the visible band and respects the tree indent.
    ImGui::SetCursorScreenPos(ImVec2(contentX_, top_));
    restoreCursor_ = ImVec2(editorL, stripeTop_);   // (unused sentinel; see destructor)
}

void ListRow::SuppressInputIn(float x0, float x1) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MousePos.x >= x0 && io.MousePos.x <= x1 &&
        io.MousePos.y >= stripeTop_ && io.MousePos.y < stripeTop_ + pitch_)
        in_ = ListRowInput{};   // clear all — an inline control under the cursor wins
}

ListRow::~ListRow() {
    // Advance the layout cursor to the next stripe top, KEEPING the caller's
    // current indent X (the destructor may fire mid-tree right after a child's
    // ImGui::Indent — resetting X to the window left would wipe the indentation).
    // Then validate with a zero-size Dummy so the trailing SetCursorScreenPos never
    // trips ImGui's boundary-extension assert.
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float indentX = w->WorkRect.Min.x + w->DC.Indent.x;
    ImGui::SetCursorScreenPos(ImVec2(indentX, stripeTop_ + pitch_ -
                                     ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ListRowAdvanceZebra();
}

} // namespace UI

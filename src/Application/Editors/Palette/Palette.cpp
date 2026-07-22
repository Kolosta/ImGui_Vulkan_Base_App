#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/TreeRow.h>
#include <UI/Widgets/ListRow.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui_internal.h>   // ImRect, BeginDragDropTargetCustom

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Palette editor (core.palette) — the document's COLOUR TABLE.
//
//  A swatch is a colour used as a VARIABLE: any paint in the document may
//  follow one (the swatch rows in the Properties editors), so editing it here
//  restyles every user at once. On top of the colour it optionally carries what
//  a print workflow needs and a screen colour cannot express:
//
//    • its CMYK — the separation definition. `display` stays independent
//      because a spot ink's on-screen appearance is its calibrated equivalent,
//      which no CMYK→sRGB formula reproduces.
//    • its place in the PLATE STACK. Printing is sequential: the lowest order
//      is laid down first and ends up underneath.
//    • whether it OVERPRINTS — knockout (the default) writes all four channels
//      including the zeros, erasing the ink below; overprint writes only the
//      channels this ink uses, so the two mix on paper.
//
//  Rows are the shared UI::Tree chrome (zebra bands, hover/selected tints,
//  chevrons) so the editor reads like the Outliner and the Colour Usage list.
//  An EXPANDED body deliberately keeps its parent row's zebra shade instead of
//  alternating: the detail belongs to that row, and re-striping it would read
//  as a list of unrelated lines.
//
//  Swatches seeded from a specification (the IOF module installs the 29 ISOM
//  separations) are LOCKED: their name, ink and print order are re-asserted on
//  every module open, so only overprint is editable on them here.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
namespace tr = UI::Tree;

// The spot inks a map is actually printed with. A PMS reference is a measured
// ink, not a name you can invent — typing one that does not exist would send a
// plate to a press that cannot mix it. So the field offers the inks the IOF
// specification names, plus a free entry for a house ink the operator knows.
// (The full Pantone library is licensed and not redistributable; these are the
// ones the orienteering specification itself quotes.)
struct SpotInk { const char* name; float r, g, b; };
constexpr SpotInk kSpotInks[] = {
    { "PMS 471",       0.722f, 0.380f, 0.145f },   // brown, CMYK+B
    { "PMS Purple",    0.733f, 0.161f, 0.733f },   // course overprint
    { "PMS Process Blue", 0.000f, 0.522f, 0.792f },
    { "PMS 361",       0.263f, 0.690f, 0.165f },   // green
    { "PMS 136",       1.000f, 0.749f, 0.247f },   // yellow
    { "PMS Black",     0.0f,   0.0f,   0.0f   },
};
constexpr int kSpotInkCount = (int)(sizeof kSpotInks / sizeof kSpotInks[0]);

// The list runs top plate first, so the rank counts DOWN — the bottom row is
// 1/x, the plate actually laid down first.
std::string OrderLabel(const Ink::Swatch& sw, int rank, int total) {
    char buf[64];
    if (!sw.hasPrintOrder) { std::snprintf(buf, sizeof buf, "—"); return buf; }
    std::snprintf(buf, sizeof buf, "%d / %d", total - rank, total);
    return buf;
}
}  // namespace

void Application::RenderPalette(EditorState& st) {
    (void)st;
    if (!project_.document) {
        ImGui::TextUnformatted("No document.");
        return;
    }
    Ink::Document& doc = *project_.document;
    const float gs = tr::Gs();

    // Listed as the stack LOOKS, not as it prints: the topmost plate at the top
    // of the list, the first one laid down at the bottom. Reading a plate stack
    // upside down is a good way to mis-order a map. Swatches with no print
    // order have no place in the stack and follow at the end.
    // BY VALUE, deliberately: adding or removing a swatch reallocates the
    // document's table, and a list of pointers into it would dangle the moment
    // the button below fires.
    std::vector<Ink::Swatch> ordered(doc.Swatches().begin(), doc.Swatches().end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Ink::Swatch& a, const Ink::Swatch& b) {
                         if (a.hasPrintOrder != b.hasPrintOrder)
                             return a.hasPrintOrder;
                         if (!a.hasPrintOrder) return false;
                         return a.printOrder > b.printOrder;
                     });
    int stackTotal = 0;
    for (const Ink::Swatch& sw : ordered) if (sw.hasPrintOrder) ++stackTotal;

    if (!UI::BeginScroll("##paletteScroll", ImVec2(0, 0))) { UI::EndScroll(); return; }
    UI::ListRowResetZebra();
    UI::ListRowSetBandScale(1.0f);

    const ImU32 zebra = ImGui::ColorConvertFloat4ToU32(
        tr::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
    const ImVec4 textCol = tr::SafeColor(Tok::C_Outliner_Text,
                                         ImVec4(0.85f, 0.85f, 0.85f, 1));
    const ImU32 subtle = ImGui::ColorConvertFloat4ToU32(
        tr::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));

    // ── Add a free colour (applied AFTER the loop — see above) ───────────────
    // Filled by a drop; applied after the loop so the table is stable while
    // it is being walked.
    Ink::SwatchId dropSrc = Ink::kNullSwatch, dropDst = Ink::kNullSwatch;
    bool dropAbove = false;
    bool addColour = false;
    {
        // The rows run flush to the editor edges (no content inset), so this
        // button has to bring its own margin.
        const float m = tr::BandMargin();
        ImGui::Dummy(ImVec2(1.0f, m));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m);
        UI::ButtonGroup bg("##palAdd");
        bg.SetGrid({ ImGui::GetContentRegionAvail().x - m * 2.0f }, { pr::RowH() });
        UI::ButtonGroup::Cell c{};
        c.label = "New colour"; c.col = 0; c.row = 0;
        bg.AddCell(c);
        if (bg.Render().clickedIndex == 0) addColour = true;
    }
    ImGui::Dummy(ImVec2(1.0f, 4.0f * gs));

    int rank = 0, rowIndex = 0;
    Ink::SwatchId remove = Ink::kNullSwatch;
    for (const Ink::Swatch& cur : ordered) {
        Ink::Swatch sw = cur;                  // edit a copy, commit on change
        const Ink::SwatchId id = sw.id;
        bool changed = false;
        const bool open = paletteOpen_.count(id) != 0;

        UI::ListRowConfig cfg;
        cfg.id = (ImGuiID)(id * 2654435761u);
        // The HEADER stripe is ListRow's own, byte for byte what the Outliner
        // draws — hand-painting it is what let ItemSpacing creep in and gave
        // the dark rows their phantom margins. Only the expanded BODY needs an
        // extension painted below, in the same shade.
        cfg.zebraOdd = (rowIndex & 1) != 0;
        cfg.zebraColor = zebra;
        cfg.bandMarginLeft = tr::BandMargin();
        cfg.cornerRadius = tr::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * gs;
        // The last colour picked stays lit, so a row that moves when its print
        // order changes is still findable.
        cfg.selected = paletteSel_ == id;
        cfg.active   = paletteSel_ == id;
        {
            ImVec4 hov = tr::SafeColor(Tok::C_Outliner_Row_Hover,
                                       ImVec4(0.3f, 0.5f, 0.9f, 1));
            hov.w = 0.35f;
            cfg.colors.hover = ImGui::ColorConvertFloat4ToU32(hov);
            cfg.colors.selected = ImGui::ColorConvertFloat4ToU32(
                tr::SafeColor(Tok::C_Outliner_Row_Selected,
                              ImVec4(0.2f, 0.4f, 0.7f, 1)));
            cfg.colors.selectedHover = ImGui::ColorConvertFloat4ToU32(
                tr::SafeColor(Tok::C_Outliner_Row_SelectedHover,
                              ImVec4(0.3f, 0.5f, 0.8f, 1)));
            cfg.colors.active = ImGui::ColorConvertFloat4ToU32(
                tr::SafeColor(Tok::C_Outliner_Row_Active,
                              ImVec4(0.25f, 0.45f, 0.75f, 1)));
            cfg.colors.activeHover = ImGui::ColorConvertFloat4ToU32(
                tr::SafeColor(Tok::C_Outliner_Row_ActiveHover,
                              ImVec4(0.35f, 0.55f, 0.85f, 1)));
        }
        const bool striped = (rowIndex & 1) != 0;
        ++rowIndex;
        ImDrawList* edl = ImGui::GetWindowDrawList();
        edl->ChannelsSplit(2);
        edl->ChannelsSetCurrent(1);

        // The row is SCOPED: its destructor rewinds the cursor to just past the
        // band, which would swallow the expanded body's height and let the next
        // row draw straight over it. Closing it here means the body advances
        // the cursor for real and the rows below are pushed down.
        bool clicked = false;
        float stripeBot = 0.0f;
        {
        UI::ListRow row(cfg);
        // ── Reorder by drag ──────────────────────────────────────────────────
        // The two groups are separate ORDERS and never mix: a colour in the
        // plate stack is ranked by its print order, one outside it only by its
        // place in the table. Dropping across the boundary would have to
        // silently add or remove a print order, so it is refused outright and
        // the drop preview never appears there.
        {
            constexpr const char* kPal = "PALETTE_SWATCH";
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload(kPal, &id, sizeof id);
                ImGui::TextUnformatted(sw.name.c_str());
                ImGui::EndDragDropSource();
            }
            const ImRect stripe(ImVec2(tr::RowLeft(), row.StripeTop()),
                                ImVec2(tr::RowRight(), row.StripeBottom()));
            const ImGuiID tid = ImGui::GetID((void*)(std::uintptr_t)(id ^ 0xDD00ull));
            if (ImGui::BeginDragDropTargetCustom(stripe, tid)) {
                constexpr ImGuiDragDropFlags kPeek =
                    ImGuiDragDropFlags_AcceptBeforeDelivery |
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                if (const ImGuiPayload* pl =
                        ImGui::AcceptDragDropPayload(kPal, kPeek)) {
                    const Ink::SwatchId src = *(const Ink::SwatchId*)pl->Data;
                    const Ink::Swatch* ss = doc.FindSwatch(src);
                    const bool sameGroup =
                        ss && ss->hasPrintOrder == sw.hasPrintOrder;
                    if (sameGroup && src != id) {
                        // Insertion line on the half the cursor is nearer.
                        const float mid = (row.StripeTop() + row.StripeBottom()) * 0.5f;
                        const bool above = ImGui::GetIO().MousePos.y < mid;
                        const float y = above ? row.StripeTop() : row.StripeBottom();
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(stripe.Min.x, y), ImVec2(stripe.Max.x, y),
                            ImGui::ColorConvertFloat4ToU32(tr::SafeColor(
                                Tok::S_Color_Accent_Default,
                                ImVec4(1, 0.6f, 0.2f, 1))),
                            std::max(2.0f, 2.0f * gs));
                        if (pl->IsDelivery()) {
                            dropSrc = src; dropDst = id; dropAbove = above;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        clicked = false;
        stripeBot = row.StripeBottom();
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)cfg.id);
        tr::DotGutter();
        bool o = open;
        tr::Chevron("##ex", o);
        if (o != open) {
            if (o) paletteOpen_.insert(id); else paletteOpen_.erase(id);
        }
        // Show the ink this technique would actually lay: under CMYK+B / PMS a
        // colour with a spot definition prints as that ink, so previewing its
        // process build would be a lie about what comes off the press.
        tr::SlotSwatch(pr::ToSrgb(Ink::SwatchPrintsSpot(sw, doc.PrintTech())
                                      ? sw.spotDisplay : sw.display));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetCursorScreenPos().x + 4.0f * gs,
                   row.RowTop() + (tr::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(textCol), sw.name.c_str());
        {
            const std::string ord = OrderLabel(sw, rank, stackTotal);
            const std::string tag = sw.overprint ? (ord + "  ·  OP") : ord;
            const ImVec2 ts = ImGui::CalcTextSize(tag.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(row.BandRight() - ts.x - 8.0f * gs,
                       row.RowTop() + (tr::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
                subtle, tag.c_str());
        }
        ImGui::PopID();
        clicked = row.Input().clicked;

        }   // ← row destroyed
        // Pin the cursor to the stripe's own bottom: whatever the destructor
        // left, THIS is where the next row (or this one's body) begins.
        ImGui::SetCursorScreenPos(
            ImVec2(ImGui::GetCurrentWindow()->WorkRect.Min.x, stripeBot));
        if (clicked) paletteSel_ = id;
        if (sw.hasPrintOrder) ++rank;

        // One band for the WHOLE entry, edge to edge and square-cornered: the
        // detail belongs to its row, so it takes the same shade with no inset
        // and no rounding to break the block up.
        auto paintBand = [&]() {
            // ListRow already drew the header stripe; this only continues that
            // shade DOWN over an expanded body, edge to edge and square, so the
            // detail reads as part of its row.
            if (striped && paletteOpen_.count(id)) {
                ImGuiWindow* w = ImGui::GetCurrentWindow();
                edl->ChannelsSetCurrent(0);
                edl->AddRectFilled(ImVec2(w->WorkRect.Min.x, stripeBot),
                                   ImVec2(w->WorkRect.Max.x +
                                              ImGui::GetStyle().ScrollbarSize,
                                          ImGui::GetCursorScreenPos().y),
                                   zebra);
            }
            edl->ChannelsMerge();
        };
        if (!paletteOpen_.count(id)) { paintBand(); continue; }

        // ── Expanded body, on the row's own zebra shade ──────────────────────
        ImGui::PushID((int)cfg.id + 1);
        ImGui::Indent(tr::DotGutterW() + tr::ChevronSlotW());

        {   // Name (a specification swatch keeps its name).
            char buf[128];
            std::snprintf(buf, sizeof buf, "%s", sw.name.c_str());
            ImGui::BeginDisabled(sw.locked);
            const float cw = pr::Label("Name");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::InputText("##nm", buf, sizeof buf)) { sw.name = buf; changed = true; }
            ImGui::EndDisabled();
        }
        {   // The on-screen colour. Independent of the ink on purpose.
            Ink::SwatchId none = Ink::kNullSwatch;   // a swatch has no swatch
            bool rel = false;
            if (pr::SwatchRow("Screen", &sw.display, &none, doc, true, &rel))
                changed = true;
        }
        {   // The separation — the very same CMYK row the colour picker uses,
            // so an ink is entered the same way wherever you meet it.
            ImGui::BeginDisabled(sw.locked);
            const float cw = pr::Label("Separation");
            if (pr::CmykRow("##sep", &sw.ink, cw)) changed = true;
            ImGui::EndDisabled();
        }
        {   // The SPOT definition, when the colour has one. CMYK+B and PMS lay
            // this ink instead of building the colour from the process set —
            // which is the whole point of CMYK+B for brown line work.
            bool hs = sw.hasSpot;
            if (pr::CheckRow("Spot ink", &hs)) { sw.hasSpot = hs; changed = true; }
            if (ImGui::IsItemHovered())
                UI::DrawTooltipTranslucent(
                    "Print this colour as its own named ink under CMYK+B and "
                    "PMS, instead of mixing it from the four process inks",
                    ImGui::GetIO().MousePos, 1.0f);
            if (sw.hasSpot) {
                // Picked from the known inks — choosing one sets its measured
                // colour too, which is the whole point of naming an ink.
                ImGui::BeginDisabled(sw.locked);
                std::vector<const char*> names;
                names.reserve(kSpotInkCount + 1);
                for (const SpotInk& si : kSpotInks) names.push_back(si.name);
                names.push_back("Custom…");
                int cur = kSpotInkCount;               // Custom unless matched
                for (int k = 0; k < kSpotInkCount; ++k)
                    if (sw.spotName == kSpotInks[k].name) { cur = k; break; }
                if (pr::DropdownRow("Ink", names.data(),
                                    (int)names.size(), &cur)) {
                    if (cur < kSpotInkCount) {
                        sw.spotName = kSpotInks[cur].name;
                        sw.spotDisplay = Ink::SrgbToLinearPremultiplied(
                            kSpotInks[cur].r, kSpotInks[cur].g,
                            kSpotInks[cur].b, 1.0f);
                        sw.spotDisplay.a = 1.0f;
                    } else if (sw.spotName.empty()) {
                        sw.spotName = "Custom ink";
                    }
                    changed = true;
                }
                if (cur == kSpotInkCount) {
                    char nb[64];
                    std::snprintf(nb, sizeof nb, "%s", sw.spotName.c_str());
                    const float cw = pr::Label("Ink name");
                    ImGui::SetNextItemWidth(cw);
                    if (ImGui::InputText("##spotnm", nb, sizeof nb)) {
                        sw.spotName = nb; changed = true;
                    }
                }
                ImGui::EndDisabled();
                Ink::SwatchId none = Ink::kNullSwatch;
                bool rel = false;
                if (pr::SwatchRow("Ink colour", &sw.spotDisplay, &none, doc,
                                  false, &rel))
                    changed = true;
            }
        }
        {   // Plate stack membership + position.
            ImGui::BeginDisabled(sw.locked);
            bool has = sw.hasPrintOrder;
            if (pr::CheckRow("In print stack", &has)) { sw.hasPrintOrder = has; changed = true; }
            if (sw.hasPrintOrder) {
                int ord = sw.printOrder;
                if (pr::DragInt("Print order", &ord, 0.2f, 0, 999)) {
                    sw.printOrder = ord; changed = true;
                }
            }
            ImGui::EndDisabled();
        }
        {   // Overprint stays editable even on a specification swatch: it is a
            // press decision, not part of the colour definition.
            bool op = sw.overprint;
            if (pr::CheckRow("Overprint", &op)) { sw.overprint = op; changed = true; }
            if (ImGui::IsItemHovered())
                UI::DrawTooltipTranslucent(
                    "Knockout (off) erases the inks underneath; overprint writes "
                    "only this ink's own channels, so the colours mix on paper",
                    ImGui::GetIO().MousePos, 1.0f);
        }
        if (!sw.locked) {
            UI::ButtonGroup bg("##del");
            bg.SetGrid({ ImGui::GetContentRegionAvail().x }, { pr::RowH() });
            UI::ButtonGroup::Cell c{};
            c.label = "Delete colour"; c.col = 0; c.row = 0;
            bg.AddCell(c);
            if (bg.Render().clickedIndex == 0) remove = id;
        }
        ImGui::Unindent(tr::DotGutterW() + tr::ChevronSlotW());
        ImGui::PopID();

        paintBand();
        if (changed) { doc.SetSwatch(id, sw); MarkDirty(); }
    }
    if (dropSrc && dropDst) {
        // Rebuild the dragged colour's GROUP in its new visual order, then
        // write that order back the way that group is actually ranked.
        const bool stacked = [&] {
            const Ink::Swatch* s2 = doc.FindSwatch(dropSrc);
            return s2 && s2->hasPrintOrder;
        }();
        std::vector<Ink::SwatchId> group;
        for (const Ink::Swatch& sw2 : ordered)
            if (sw2.hasPrintOrder == stacked && sw2.id != dropSrc)
                group.push_back(sw2.id);
        const auto at = std::find(group.begin(), group.end(), dropDst);
        group.insert(dropAbove ? at : (at == group.end() ? at : at + 1), dropSrc);
        if (stacked) {
            // The list runs top plate first, so the first entry gets the
            // HIGHEST order — it is the one laid down last.
            int n = (int)group.size();
            for (int k = 0; k < n; ++k)
                if (const Ink::Swatch* s2 = doc.FindSwatch(group[(std::size_t)k])) {
                    Ink::Swatch up = *s2;
                    up.printOrder = n - 1 - k;
                    doc.SetSwatch(up.id, up);
                }
        } else {
            // Unstacked colours are ranked by the table itself; the stacked
            // ones keep their relative place ahead of them.
            std::vector<Ink::SwatchId> order;
            for (const Ink::Swatch& sw2 : ordered)
                if (sw2.hasPrintOrder) order.push_back(sw2.id);
            order.insert(order.end(), group.begin(), group.end());
            doc.ReorderSwatches(order);
        }
        MarkDirty();
    }
    if (remove != Ink::kNullSwatch) { doc.RemoveSwatch(remove); MarkDirty(); }
    if (addColour) {
        Ink::Swatch ns;
        ns.name = "Colour " + std::to_string(doc.Swatches().size() + 1);
        ns.display = { 0.5f, 0.5f, 0.5f, 1.0f };
        doc.AddSwatch(ns);
        MarkDirty();
    }

    UI::EndScroll();
}

}  // namespace App

#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/TreeRow.h>
#include <UI/Widgets/ListRow.h>
#include <UI/Widgets/ScrollArea.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Colour Usage editor (core.colorusage) — every colour the document actually
//  paints with, in plate order, expandable down to the exact piece that uses
//  it.
//
//  The tree is COLOUR → OBJECT → PIECE, because "which object is brown" is
//  rarely the useful question: an object routinely carries several paints on
//  different plates, and what matters before a print run is which PIECE of it
//  lands on which separation. So an object appears once under every colour it
//  contributes to, and only the pieces using THAT colour sit under it.
//
//  It reads the compiled Scene rather than the document: that is where every
//  paint source has already been resolved into one drawable carrying its
//  swatch and its piece indices, so pattern cells, instances, modifier copies
//  and boolean results are all counted exactly as they render.
//
//  Layout follows the Outliner exactly — same UI::Tree row chrome, same flat
//  row list built first and then drawn windowed, same guide lines. Building the
//  list up front is what keeps the zebra parity, the row ids and the scroll
//  height correct; drawing a hierarchy inline cannot do any of the three.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
namespace tr = UI::Tree;

// ── Gathered model ───────────────────────────────────────────────────────────
struct PieceUse {
    std::uint64_t key = 0;        // piece identity within its object
    std::string   label;
};
struct ObjectUse {
    Ink::NodeId           node = Ink::kNullNode;
    std::vector<PieceUse> pieces;
};
struct ColorUse {
    std::uint64_t          key = 0;        // row identity (swatch id or literal)
    Ink::SwatchId          swatch = Ink::kNullSwatch;
    Ink::Color             color{};
    int                    plate = Ink::kNoPlate;
    bool                   overprint = false;
    std::string            name;
    std::vector<ObjectUse> objects;
};

// One drawn line of the tree.
struct UsageRow {
    enum class Kind : std::uint8_t { Colour, Object, Piece };
    Kind          kind = Kind::Colour;
    int           depth = 0;
    std::uint64_t rowId = 0;       // stable, unique — drives ImGui ids + expand
    const ColorUse* colour = nullptr;
    const ObjectUse* object = nullptr;
    const PieceUse*  piece  = nullptr;
    std::uint64_t parentId = 0;    // 0 at the top level
    bool          expandable = false;
    bool          open = false;
};

// The piece a drawable belongs to, in the owner's own terms. `ownerPiece`
// indexes the OWNER's fill/stroke list; everything a pattern or instanced fill
// expands into carries the HOST fill's index, so a motif's own stroke reads as
// a part of that fill rather than as a stroke of the object.
std::string PieceLabel(const Ink::Drawable& d) {
    char buf[96];
    const int idx = (int)d.ownerPiece + 1;
    if (d.ownerPieceStroke) {
        if (d.isStroke && d.pieceIndex == d.ownerPiece)
            std::snprintf(buf, sizeof buf, "Stroke %d", idx);
        else
            std::snprintf(buf, sizeof buf, "Stroke %d  ›  mark object", idx);
    } else if (!d.isStroke && d.pieceIndex == d.ownerPiece) {
        std::snprintf(buf, sizeof buf, "Fill %d", idx);
    } else if (d.isStroke) {
        std::snprintf(buf, sizeof buf, "Fill %d  ›  element stroke %d",
                      idx, (int)d.pieceIndex + 1);
    } else {
        std::snprintf(buf, sizeof buf, "Fill %d  ›  element %d",
                      idx, (int)d.pieceIndex + 1);
    }
    return buf;
}

std::uint64_t LiteralKey(const Ink::Color& c) {
    auto q = [](float v) {
        return (std::uint64_t)(std::uint32_t)std::lround(
            std::clamp(v, 0.0f, 1.0f) * 4095.0f);
    };
    return (q(c.r) << 36) | (q(c.g) << 24) | (q(c.b) << 12) | q(c.a);
}

// A colour with no swatch has no name of its own — show its value, so the rows
// stay tellable apart. These are exactly the artwork that has no plate yet,
// which is what you are looking for before an export.
std::string LiteralName(const Ink::Color& c) {
    const ImVec4 s = pr::ToSrgb(c);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Unassigned  #%02X%02X%02X",
                  (int)std::lround(std::clamp(s.x, 0.0f, 1.0f) * 255.0f),
                  (int)std::lround(std::clamp(s.y, 0.0f, 1.0f) * 255.0f),
                  (int)std::lround(std::clamp(s.z, 0.0f, 1.0f) * 255.0f));
    return buf;
}

std::uint64_t Mix(std::uint64_t a, std::uint64_t b) {
    a ^= b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2);
    return a;
}

// Is `sel` somewhere BELOW rows[i]? Walking the parent chain up from the
// selected row is cheaper than searching down, and the chain is at most three
// links long.
bool IsUsageAncestor(const std::vector<UsageRow>& rows, std::size_t i,
                     std::uint64_t sel) {
    for (const UsageRow& r : rows) {
        if (r.rowId != sel) continue;
        for (std::uint64_t p = r.parentId; p; ) {
            if (p == rows[i].rowId) return true;
            std::uint64_t next = 0;
            for (const UsageRow& q : rows)
                if (q.rowId == p) { next = q.parentId; break; }
            p = next;
        }
        return false;
    }
    return false;
}
}  // namespace

void Application::RenderColorUsage(EditorState& st) {
    (void)st;
    if (!project_.document || !ink_) {
        ImGui::TextUnformatted("No document.");
        return;
    }
    Ink::Document& doc = *project_.document;

    // ── Pass 1a: gather drawables into colour → object → piece ───────────────
    std::map<std::uint64_t, ColorUse> byColor;
    for (const Ink::Drawable& d : ink_->SceneDrawables()) {
        if (d.isClipSource || d.previewOnly) continue;
        const Ink::Swatch* sw = doc.FindSwatch(d.swatch);
        const std::uint64_t key =
            sw ? sw->id : (0x8000000000000000ull | LiteralKey(d.color));
        auto& cu = byColor[key];
        if (cu.key == 0) {
            cu.key       = key;
            cu.swatch    = sw ? sw->id : Ink::kNullSwatch;
            cu.color     = sw ? sw->display : d.color;
            cu.plate     = (sw && sw->hasPrintOrder) ? sw->printOrder
                                                     : Ink::kNoPlate;
            cu.overprint = sw && sw->overprint;
            cu.name      = sw ? sw->name : LiteralName(d.color);
        }
        auto oit = std::find_if(cu.objects.begin(), cu.objects.end(),
                                [&](const ObjectUse& o) { return o.node == d.owner; });
        if (oit == cu.objects.end()) {
            cu.objects.push_back({ d.owner, {} });
            oit = cu.objects.end() - 1;
        }
        // One entry per PIECE, not per drawable: a pattern expands into
        // hundreds of cells that are all the same "Fill 2".
        const std::uint64_t pk = ((std::uint64_t)d.ownerPiece << 24) |
                                 ((std::uint64_t)d.ownerPieceStroke << 16) |
                                 ((std::uint64_t)d.pieceIndex << 8) |
                                 (std::uint64_t)d.isStroke;
        if (std::none_of(oit->pieces.begin(), oit->pieces.end(),
                         [&](const PieceUse& p) { return p.key == pk; }))
            oit->pieces.push_back({ pk, PieceLabel(d) });
    }

    // Plate order — topmost plate first, matching the Palette editor; colours
    // with no plate fall to the end where they stand out.
    std::vector<const ColorUse*> colours;
    colours.reserve(byColor.size());
    for (const auto& kv : byColor) colours.push_back(&kv.second);
    std::stable_sort(colours.begin(), colours.end(),
                     [](const ColorUse* a, const ColorUse* b) {
                         if (a->plate != b->plate) {
                             if (a->plate == Ink::kNoPlate) return false;
                             if (b->plate == Ink::kNoPlate) return true;
                             return a->plate > b->plate;
                         }
                         return a->name < b->name;
                     });

    if (colours.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            tr::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f,0.65f,0.65f,1)));
        ImGui::TextUnformatted("Nothing painted yet.");
        ImGui::PopStyleColor();
        return;
    }

    // ── Pass 1b: flatten to drawn rows, honouring what is expanded ───────────
    std::vector<UsageRow> rows;
    for (const ColorUse* cu : colours) {
        UsageRow cr;
        cr.kind = UsageRow::Kind::Colour;
        cr.depth = 0;
        cr.rowId = Mix(0xC0102ull, cu->key);
        cr.colour = cu;
        cr.parentId = 0;
        cr.expandable = !cu->objects.empty();
        cr.open = colorUsageOpen_.count(cr.rowId) != 0;
        rows.push_back(cr);
        if (!cr.open) continue;
        for (const ObjectUse& ou : cu->objects) {
            UsageRow orow;
            orow.kind = UsageRow::Kind::Object;
            orow.depth = 1;
            orow.rowId = Mix(cr.rowId, ou.node);
            orow.colour = cu;
            orow.object = &ou;
            orow.parentId = cr.rowId;
            orow.expandable = !ou.pieces.empty();
            orow.open = colorUsageOpen_.count(orow.rowId) != 0;
            rows.push_back(orow);
            if (!orow.open) continue;
            for (const PieceUse& pu : ou.pieces) {
                UsageRow pr2;
                pr2.kind = UsageRow::Kind::Piece;
                pr2.depth = 2;
                pr2.rowId = Mix(orow.rowId, pu.key);
                pr2.colour = cu;
                pr2.object = &ou;
                pr2.piece = &pu;
                pr2.parentId = orow.rowId;
                rows.push_back(pr2);
            }
        }
    }

    // ── Pass 2: windowed draw, exactly the Outliner's loop ───────────────────
    if (!UI::BeginScroll("##colorUsageScroll", ImVec2(0, 0))) return;

    UI::ListRowResetZebra();
    UI::ListRowSetBandScale(1.0f);   // the Outliner's Layers view raises it
    const float stripeH = UI::ListRowStripeHeight();
    const float scrollY = ImGui::GetScrollY();
    const float viewTop = scrollY;
    const float viewBot = scrollY + ImGui::GetWindowHeight();
    const float startY  = ImGui::GetCursorPosY();

    const ImU32 zebra = ImGui::ColorConvertFloat4ToU32(
        tr::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
    const ImVec4 textCol = tr::SafeColor(Tok::C_Outliner_Text,
                                         ImVec4(0.85f, 0.85f, 0.85f, 1));
    const ImU32 subtle = ImGui::ColorConvertFloat4ToU32(
        tr::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));

    std::uint64_t toggle = 0;   // applied after the loop (the list is live)

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float rowTop = startY + (float)i * stripeH;
        if (rowTop + stripeH < viewTop - stripeH || rowTop > viewBot + stripeH) {
            ImGui::SetCursorPosY(rowTop);
            ImGui::Dummy(ImVec2(1.0f, stripeH));
            UI::ListRowAdvanceZebra();      // keep parity in step with the index
            continue;
        }
        ImGui::SetCursorPosY(rowTop);
        const UsageRow& r = rows[i];

        UI::ListRowConfig cfg;
        cfg.id = (ImGuiID)(r.rowId ^ (r.rowId >> 32));
        cfg.zebraOdd = (UI::ListRowZebraIndex() & 1);
        cfg.zebraColor = zebra;
        cfg.bandMarginLeft = tr::BandMargin();
        cfg.cornerRadius = tr::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * tr::Gs();
        // A colour row highlights when the viewport selection uses it, so the
        // two editors read together.
        // Highlight the row last picked here, and its ancestors with it, so a
        // deep piece stays easy to trace back to its colour. It means nothing
        // beyond "this is where you are".
        cfg.active   = r.rowId == colorUsageSel_;
        cfg.selected = !cfg.active && colorUsageSel_ != 0 &&
                       IsUsageAncestor(rows, i, colorUsageSel_);
        // The Outliner's own row tints — without them the config carries zeros
        // and the bands are simply never painted.
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
        UI::ListRow row(cfg);

        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)(cfg.id & 0x7FFFFFFF));
        tr::DotGutter();
        for (int d = 0; d < r.depth; ++d) tr::ChevronSpacer();
        if (r.expandable) {
            bool open = r.open;
            tr::Chevron("##ex", open);
            if (open != r.open) toggle = r.rowId;
        } else {
            tr::ChevronSpacer();
        }

        switch (r.kind) {
        case UsageRow::Kind::Colour: tr::SlotSwatch(pr::ToSrgb(r.colour->color)); break;
        case UsageRow::Kind::Object: tr::SlotIcon("bezier-curve", textCol);       break;
        case UsageRow::Kind::Piece:  tr::SlotIcon("shape-category",
                                                  tr::SafeColor(Tok::S_Color_Text_Subtle,
                                                      ImVec4(0.65f,0.65f,0.65f,1))); break;
        }

        const char* label = "";
        std::string owned;
        if (r.kind == UsageRow::Kind::Colour) label = r.colour->name.c_str();
        else if (r.kind == UsageRow::Kind::Piece) label = r.piece->label.c_str();
        else {
            const Ink::Node* n = doc.Find(r.object->node);
            owned = n ? n->name : std::string("(removed)");
            label = owned.c_str();
        }
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetCursorScreenPos().x + 4.0f * tr::Gs(),
                   row.RowTop() + (tr::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
            r.kind == UsageRow::Kind::Piece ? subtle
                                            : ImGui::ColorConvertFloat4ToU32(textCol),
            label);

        // Trailing column: the plate rank and the overprint marker, on the
        // colour rows only — the children inherit it.
        if (r.kind == UsageRow::Kind::Colour) {
            char tag[48];
            if (r.colour->plate == Ink::kNoPlate)
                std::snprintf(tag, sizeof tag, "no plate");
            else if (r.colour->overprint)
                std::snprintf(tag, sizeof tag, "plate %d  ·  OP", r.colour->plate);
            else
                std::snprintf(tag, sizeof tag, "plate %d", r.colour->plate);
            const ImVec2 ts = ImGui::CalcTextSize(tag);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(row.BandRight() - ts.x - 8.0f * tr::Gs(),
                       row.RowTop() + (tr::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
                subtle, tag);
        }
        ImGui::PopID();

        // A click marks where you are. A DOUBLE click reaches through to the
        // document: whatever level you are on, it selects the object that owns
        // the piece — which is the one thing a leaf row can usefully do.
        if (row.Input().clicked) colorUsageSel_ = r.rowId;
        if (row.Input().doubleClicked && r.object)
            edit_.SelectOnly(r.object->node);
        // NB: no ListRowAdvanceZebra here — ListRow advances the parity itself,
        // and stepping it twice is what flattens the stripes to one shade.
    }

    // Reserve the full content height so the scroll range is right.
    ImGui::SetCursorPosY(startY + (float)rows.size() * stripeH);
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    // ── Guide lines: parent → its last descendant, like the Outliner ─────────
    {
        const float winTop = ImGui::GetWindowPos().y;
        const float sY = ImGui::GetScrollY();
        const float vTop = winTop, vBot = winTop + ImGui::GetWindowHeight();
        auto rowTopY = [&](std::size_t i) {
            return winTop - sY + startY + (float)i * stripeH;
        };
        const float x0 = tr::RowLeft() + tr::BandMargin() + tr::DotGutterW();
        const ImU32 solid = ImGui::ColorConvertFloat4ToU32(
            tr::SafeColor(Tok::S_Color_Border_Default, ImVec4(0.4f, 0.4f, 0.4f, 1)));
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].open) continue;
            std::size_t last = i;
            for (std::size_t j = i + 1;
                 j < rows.size() && rows[j].depth > rows[i].depth; ++j)
                last = j;
            if (last == i) continue;
            const float ys = rowTopY(i + 1);
            const float ye = rowTopY(last) + stripeH;
            if (ye < vTop || ys > vBot) continue;
            const float x = x0 + ((float)rows[i].depth + 0.5f) * tr::ChevronSlotW();
            tr::TreeLine(x, ys, ye, solid, /*dotted=*/false);
        }
    }

    UI::EndScroll();
    if (toggle) {
        if (colorUsageOpen_.count(toggle)) colorUsageOpen_.erase(toggle);
        else colorUsageOpen_.insert(toggle);
    }
}

}  // namespace App

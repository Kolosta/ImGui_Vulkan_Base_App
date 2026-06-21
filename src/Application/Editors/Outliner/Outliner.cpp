#include "Application.h"
#include "PageLayout.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/PopupMenu.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/Checkbox.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "OutlinerShared.h"
#include "OutlinerRowLayout.h"

namespace App {

namespace DST = DesignSystem;

namespace {
const char* kShapePayload = "OUTLINER_SHAPE";   // drag payload: shape id (u64)
const char* kNodePayload  = "OUTLINER_NODE";    // drag payload: tree node id (u64)
                                                // (a collection OR a page)

// Left/right edges of a full-width row band, in screen X. The Outliner editor
// opts OUT of the host content inset (d.contentInset=false), so the content child
// is flush: WorkRect.Min.x IS the editor's left edge — the zebra reaches it with
// no left margin. The right runs under the overlay scrollbar gutter so the stripe
// spans edge to edge. (Row geometry/striping is owned by UI::ListRow now.)
float OutlinerRowLeft()  { return ImGui::GetCurrentWindow()->WorkRect.Min.x; }
float OutlinerRowRight() {
    return ImGui::GetCurrentWindow()->WorkRect.Max.x + ImGui::GetStyle().ScrollbarSize;
}
// Left inset of the coloured band + content — the standard editor content inset
// token (same margin used by every other editor), so the Outliner matches them.
float OutlinerBandMargin() {
    auto& ds = DST::DesignSystem::Instance();
    float v = 0.0f;
    try { v = ds.GetFloat(DST::Tok::C_Editor_ContentInset) * ds.GetGlobalScale(); }
    catch (...) {}
    return std::max(0.0f, v);
}

// A collapse chevron in a FIXED uniform slot (same width as the icon slot, so the
// icon column lines up across siblings and indents). Mid-sized glyph, vertically
// centred on the row, clickable on the slot. Toggles `open`. Returns slot width.
float OutlinerChevron(const char* id, bool& open) {
    auto& ds      = DST::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float chev = OutlinerChevronSize();
    const float slot = OutlinerChevronSlotW();
    const float rowH = OutlinerRowH();
    ImGui::PushID(id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##chev", ImVec2(slot, OutlinerItemH()))) open = !open;
    ImVec2 ipos(p0.x + (slot - chev) * 0.5f, p0.y + (rowH - chev) * 0.5f);
    auto md = iconMgr.GetDefaultMetadata(open ? "chevron-down" : "chevron-right");
    if (!md.colorZones.empty())
        md.colorZones[0].customColor = ds.GetColor(DST::Tok::S_Color_Text_Subtle);
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(),
                       open ? "chevron-down" : "chevron-right", ipos, chev, md);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PopID();
    return slot;
}

// Reserve the chevron SLOT at the start of a row that has NO chevron, so its icon
// column lines up with collapsible siblings. Pure layout (no drawing).
void OutlinerChevronSpacer() {
    ImGui::Dummy(ImVec2(OutlinerChevronSlotW(), OutlinerItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}

// Reserve the left gutter for the active dot (so content never overlaps it). The
// dot itself is drawn inside the selection band by OutlinerActiveDotAt.
void OutlinerDotGutter() {
    const float d = ImGui::GetTextLineHeight() * 0.34f;
    ImGui::Dummy(ImVec2(d + 5.0f * DST::DesignSystem::Instance().GetGlobalScale(),
                        OutlinerItemH()));
    ImGui::SameLine(0.0f, 0.0f);
}
// Draw the "active" dot INSIDE the row's selection band (inset from the band's
// left edge `bandL`), vertically centred on the row.
void OutlinerActiveDotAt(float bandL, float rowTopY, ImU32 col) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float rowH = OutlinerRowH();
    const float gs = DST::DesignSystem::Instance().GetGlobalScale();
    const float d = ImGui::GetTextLineHeight() * 0.34f;     // dot diameter
    ImVec2 c(bandL + 4.0f * gs + d * 0.5f, rowTopY + rowH * 0.5f);
    w->DrawList->AddCircleFilled(c, d * 0.5f, col);
}

// Draw a vertical tree guide line at screen-X `x`, from `yStart` to `yEnd`, in
// `color`. `dotted` → a dashed line (parented-object subtree); solid otherwise
// (collection / page). Drawn on the window draw list (absolute coords).
// Drawn as a pixel-snapped FILLED RECT (not AddLine) so the hairline is crisp —
// no anti-alias bleed. The thickness is a hairline: 1px, stepping to 2px… only
// when the global scale crosses an integer (floor(gs)). `x` is snapped to a pixel
// boundary so the column doesn't straddle two pixels (which is what made it look
// like 1px + a faint extra pixel).
// `yStart`/`yEnd` are the FIRST child's stripe top and the LAST child's stripe
// bottom; this helper applies a uniform top/bottom inset (token) so every line —
// page, collection, parented object — starts/ends the same small distance inside
// the child block.
void OutlinerTreeLine(float x, float yStart, float yEnd, ImU32 color, bool dotted) {
    auto& ds = DST::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    float inset = 4.0f;
    try { inset = ds.GetFloat(DST::Tok::C_Outliner_TreeLineInset); } catch (...) {}
    inset *= gs;
    yStart += inset; yEnd -= inset;
    if (yEnd <= yStart) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::max(1.0f, std::floor(gs));       // hairline, integer px
    const float x0 = std::floor(x);                        // snap to pixel grid
    const float x1 = x0 + th;
    const float y0 = std::floor(yStart), y1 = std::floor(yEnd);
    if (!dotted) {
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    } else {
        const float dash = std::floor(2.0f * gs), gap = std::floor(2.0f * gs);
        for (float y = y0; y < y1; y += dash + gap)
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, std::min(y + dash, y1)), color);
    }
}

// The screen-X at which a parent row drawn at indent origin `rowX` places its
// chevron CENTRE — i.e. where the guide line for that parent's children sits.
// `rowX` is the parent row's content origin (GetCursorScreenPos().x BEFORE the
// dot gutter), so add the dot gutter + half a chevron slot.
float OutlinerGuideX(float rowContentX) {
    const float gs = DST::DesignSystem::Instance().GetGlobalScale();
    const float dotGutter = ImGui::GetTextLineHeight() * 0.34f + 5.0f * gs;
    return rowContentX + dotGutter + OutlinerChevronSlotW() * 0.5f;
}

// Row label colour: a matched search row is GREEN (the search text token); a
// hidden object is dimmed; otherwise the default row-text token.
ImU32 OutlinerLabelColor(bool searchHit, bool dim) {
    auto& ds = DST::DesignSystem::Instance();
    if (searchHit)
        return ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::C_Outliner_Search_Text));
    return ImGui::ColorConvertFloat4ToU32(ds.GetColor(
        dim ? DST::Tok::S_Color_Text_Disabled : DST::Tok::C_Outliner_Text));
}

// Case-insensitive substring test.
bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char a, char b){ return std::tolower((unsigned char)a) ==
                                   std::tolower((unsigned char)b); });
    return it != hay.end();
}
} // namespace

// ── Outliner selection (objects sync with the document; colls/pages outliner-only) ──
// An OBJECT is "selected" if the document has it selected (kept in sync both ways);
// a collection/page is selected only via the outliner set. This lets a viewport
// selection light up the Outliner without an explicit mirror step.
bool Application::OutlinerIsSelected(uint64_t id) const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    if (doc.FindShape(id)) return doc.IsSelected(id);
    return std::find(outlinerCur_->sel.begin(), outlinerCur_->sel.end(), id) != outlinerCur_->sel.end();
}

// Handle a row click with modifiers. `isObject` true → also drive the document
// selection so the viewport stays in sync (collections/pages are outliner-only).
//   plain  : select only this row (+ for an object, SelectOnly in the document)
//   Shift  : range-select from the active row to this one (in draw order)
//   Ctrl   : toggle/add this row + make it active
//   Alt    : add this row WITHOUT making it active
void Application::OutlinerSelectClick(uint64_t id, bool isObject) {
    (void)isObject;   // object-ness is re-derived from the document (FindShape)
    ImGuiIO& io = ImGui::GetIO();
    auto& doc = project_.document;
    auto syncObjToDoc = [&]{
        // Mirror the outliner selection's OBJECTS into the document selection
        // (so the viewport shows them). The active object = the outliner active
        // if it's an object, else keep the document's.
        doc.ClearSelection();
        for (uint64_t s : outlinerCur_->sel)
            if (doc.FindShape(s)) doc.SelectAdd(s);
        if (doc.FindShape(outlinerCur_->active)) doc.SetActive(outlinerCur_->active);
        doc.SyncActivePageToSelection();
    };

    if (io.KeyShift && outlinerCur_->active) {
        // Range from active to clicked, in the current draw order.
        const auto& order = outlinerCur_->rowOrder;
        int ia = -1, ib = -1;
        for (int i = 0; i < (int)order.size(); ++i) {
            if (order[(size_t)i] == outlinerCur_->active) ia = i;
            if (order[(size_t)i] == id) ib = i;
        }
        if (ia >= 0 && ib >= 0) {
            if (ia > ib) std::swap(ia, ib);
            outlinerCur_->sel.clear();
            for (int i = ia; i <= ib; ++i) outlinerCur_->sel.push_back(order[(size_t)i]);
            // active stays the prior active (Blender keeps it on shift-range)
        } else {
            outlinerCur_->sel = { id }; outlinerCur_->active = id;
        }
    } else if (io.KeyCtrl) {
        if (OutlinerIsSelected(id))
            outlinerCur_->sel.erase(std::remove(outlinerCur_->sel.begin(), outlinerCur_->sel.end(), id),
                                outlinerCur_->sel.end());
        else outlinerCur_->sel.push_back(id);
        outlinerCur_->active = id;                       // Ctrl makes it active
    } else if (io.KeyAlt) {
        if (!OutlinerIsSelected(id)) outlinerCur_->sel.push_back(id);
        // Alt: add WITHOUT changing the active row.
        if (!outlinerCur_->active) outlinerCur_->active = id;
    } else {
        outlinerCur_->sel = { id };
        outlinerCur_->active = id;
    }
    syncObjToDoc();
    project_.dirty = true;
}

// True if a row of `kind` (0 object, 1 page, 2 collection) passes the kind +
// object-state filters. `invertFilter` flips the final result.
bool Application::OutlinerSyncShowsShape(uint64_t id) const {
    if (!outlinerCur_->syncTarget) return true;          // no sync → show everything
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    int ab = doc.ArtboardOfShape(id);
    if (ab < 0) return outlinerCur_->syncOrphans;         // page-less (orphan) object
    if (ab >= (int)outlinerCur_->syncPageVisible.size()) return true;
    return outlinerCur_->syncPageVisible[(size_t)ab];
}

bool Application::OutlinerSyncShowsPage(int abIndex) const {
    if (!outlinerCur_->syncTarget) return true;          // no sync → show every page
    if (abIndex < 0 || abIndex >= (int)outlinerCur_->syncPageVisible.size()) return true;
    return outlinerCur_->syncPageVisible[(size_t)abIndex];   // hide non-displayed pages
}

bool Application::OutlinerPassesFilter(uint64_t id, int kind) const {
    bool pass = true;
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    if (kind == 0) {
        // Viewport-sync: hide objects not visible in the synced viewport. This
        // gate is OUTSIDE the invert toggle (it constrains the candidate set, it
        // is not part of the user's invertable kind/state filter).
        if (!OutlinerSyncShowsShape(id)) return false;
        pass = outlinerCur_->showObjects;
        if (pass && (outlinerCur_->showMeshes || outlinerCur_->showCurves)) {
            if (Renderer::Shape* s = doc.FindShape(id); s && !s->parts.empty()) {
                bool isCurve = (s->Family() == Renderer::PartType::Curve);
                if (isCurve && !outlinerCur_->showCurves) pass = false;
                if (!isCurve && !outlinerCur_->showMeshes) pass = false;
            }
        }
        if (pass && outlinerCur_->objState != ObjStateFilter::All) {
            Renderer::Shape* s = doc.FindShape(id);
            switch (outlinerCur_->objState) {
                case ObjStateFilter::Visible:    pass = s && s->visible; break;
                case ObjStateFilter::Selected:   pass = doc.IsSelected(id); break;
                case ObjStateFilter::Active:     pass = (doc.ActiveId() == id); break;
                case ObjStateFilter::Selectable: pass = s && s->visible; break; // hidden = unselectable
                default: break;
            }
        }
    } else if (kind == 1) {
        // Viewport-sync also hides pages NOT displayed in the synced viewport
        // (outside the invert toggle, like the per-object gate above).
        if (!OutlinerSyncShowsPage(doc.ArtboardIndexById(id))) return false;
        pass = outlinerCur_->showPages;
    }
    else                  pass = outlinerCur_->showCollections;
    return outlinerCur_->invertFilter ? !pass : pass;
}

// Recompute the search-match set for the current query (called each frame at the
// top of RenderOutliner). A node matches if its own NAME contains the query
// (case-insensitive). Ancestors of a match stay shown (but un-highlighted) so the
// path to a match is visible.
void Application::OutlinerRebuildSearch() {
    auto& doc = project_.document;
    outlinerCur_->searchMatches.clear();
    std::string q = outlinerCur_->search;
    outlinerCur_->searchActive = !q.empty();
    if (!outlinerCur_->searchActive) return;
    for (Renderer::Collection& c : doc.collections)
        if (!c.IsRoot() && ContainsCI(c.name, q)) outlinerCur_->searchMatches.push_back(c.id);
    for (Renderer::Artboard& ab : doc.artboards)
        if (ContainsCI(ab.name, q)) outlinerCur_->searchMatches.push_back(ab.id);
    for (Renderer::Artboard& ab : doc.artboards)
        for (Renderer::Shape& s : ab.shapes)
            if (ContainsCI(s.name, q)) outlinerCur_->searchMatches.push_back(s.id);
    for (Renderer::Shape& s : doc.looseShapes)
        if (ContainsCI(s.name, q)) outlinerCur_->searchMatches.push_back(s.id);
}

// During a search, a node is shown if it matches OR any descendant matches (so
// the ancestor path stays visible). Outside a search → always visible.
bool Application::OutlinerSearchVisible(uint64_t id) const {
    if (!outlinerCur_->searchActive) return true;
    auto& m = outlinerCur_->searchMatches;
    if (std::find(m.begin(), m.end(), id) != m.end()) return true;
    // Descendant match? Collect this node's subtree (nodes) + its objects.
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    std::vector<uint64_t> nodes; doc.CollectSubtreeNodes(id, nodes);
    for (uint64_t n : nodes) {
        if (std::find(m.begin(), m.end(), n) != m.end()) return true;
        // objects under collection `n`
        if (doc.IsCollectionId(n) || doc.IsPageId(n)) {
            for (uint64_t mm : m)
                if (Renderer::Shape* s = doc.FindShape(mm)) {
                    // a matching object is "under" n if its collection is n, or it
                    // sits on page n (collectionId 0).
                    if (s->collectionId == n) return true;
                    if (doc.IsPageId(n) && s->collectionId == 0 &&
                        doc.ArtboardOfShape(mm) == doc.ArtboardIndexById(n)) return true;
                }
        }
    }
    return false;
}

// Close the row opened by the previous OutlinerRowBegin (its ListRow destructor
// advances the cursor exactly one stripe). Called at the start of the next row and
// once at the end of the tree (OutlinerRowFinish).
void Application::OutlinerRowFinish() { outlinerRow_.reset(); }

// Open one tree row on top of the generic UI::ListRow primitive (zebra stripe,
// full-width hit zone, ui-unit coloured selection band, uniform tiling). This
// function just maps the Outliner's per-state COLOUR tokens (normal blue family /
// search green family) into the generic component and exposes the input + band
// geometry. The caller then draws the row content inside the band.
Application::RowResult Application::OutlinerRowBegin(uint64_t id, int kind, bool searchHit,
                                                    int forceSel, int forceActive) {
    (void)kind;
    auto& ds = DST::DesignSystem::Instance();
    auto& docA = const_cast<Renderer::Document&>(project_.document);

    const bool selected = forceSel >= 0 ? (forceSel != 0) : OutlinerIsSelected(id);
    const bool active   = forceActive >= 0 ? (forceActive != 0)
                        : ((outlinerCur_->active == id) ||
                           (docA.FindShape(id) && docA.ActiveId() == id));
    const bool useSearch = outlinerCur_->searchActive && searchHit;

    // Per-state band colours (search swaps the blue family for the green one).
    using T = DST::Tok;
    auto col = [&](T normal, T search){ return ds.GetColor(useSearch ? search : normal); };
    auto opaque = [](ImVec4 c){ return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x,c.y,c.z,1.0f)); };
    auto withA  = [](ImVec4 c, float a){ return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x,c.y,c.z,a)); };

    UI::ListRowConfig cfg;
    cfg.id = (ImGuiID)(id ? id : 1);
    cfg.zebraOdd   = (UI::ListRowZebraIndex() & 1) != 0;
    cfg.zebraColor = ImGui::ColorConvertFloat4ToU32(ds.GetColor(T::S_Color_Background_Layer2));
    cfg.selected   = selected;
    cfg.active     = active;
    cfg.bandMarginLeft = OutlinerBandMargin();
    cfg.cornerRadius   = ds.GetFloat(T::S_CornerRadius_Control);
    cfg.colors.hover         = withA(col(T::C_Outliner_Row_Hover,         T::C_Outliner_Search_Hover), 0.55f);
    cfg.colors.selected      = opaque(col(T::C_Outliner_Row_Selected,      T::C_Outliner_Search_Selected));
    cfg.colors.selectedHover = opaque(col(T::C_Outliner_Row_SelectedHover, T::C_Outliner_Search_SelectedHover));
    cfg.colors.active        = opaque(col(T::C_Outliner_Row_Active,        T::C_Outliner_Search_Active));
    cfg.colors.activeHover   = opaque(col(T::C_Outliner_Row_ActiveHover,   T::C_Outliner_Search_ActiveHover));
    // A matched-but-idle search row keeps a faint green tint.
    if (useSearch) cfg.colors.idle = withA(ds.GetColor(T::C_Outliner_Search_Visual), 0.45f);

    // Close the previous row, then open this one (RAII; destructor advances the
    // cursor). Stored in a member so it lives until the next row / tree end.
    outlinerRow_.reset();
    outlinerRow_.emplace(cfg);
    UI::ListRow& row = *outlinerRow_;

    // Expose band geometry for the content helpers (dot / eye / tree lines).
    outlinerRowTopY_   = row.RowTop();
    outlinerBandLeft_  = row.BandLeft();
    outlinerBandRight_ = row.BandRight();
    outlinerLastStripeTop_    = row.StripeTop();
    outlinerLastStripeBottom_ = row.StripeBottom();

    // The eye (drawn later) owns the band's right slot — suppress the row's own
    // click there so toggling visibility doesn't also select/rename the row.
    {
        const float gs = ds.GetGlobalScale();
        const float slot = OutlinerRowH(), pad = 6.0f * gs;
        row.SuppressInputIn(outlinerBandRight_ - pad - slot, outlinerBandRight_ - pad);
    }

    RowResult r;
    const UI::ListRowInput& in = row.Input();
    r.hovered = in.hovered; r.pressed = in.pressed; r.clicked = in.clicked;
    r.rightClicked = in.rightClicked; r.doubleClicked = in.doubleClicked;

    // Cursor sits at the band content origin (ListRow placed it there).
    return r;
}

// A small eye toggle button drawn at the right edge of a row. Flips `visible`.
// Call right after the row's main item (same line). Positioned absolutely at the
// content region's right edge so it works under any tree indent.
void Application::OutlinerEyeButton(bool& visible, const char* id) {
    auto& ds      = DST::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float icon = OutlinerIconSize();
    const float gs   = ds.GetGlobalScale();
    // Place on THIS row using the cached row top (the layout cursor has already
    // advanced past the row by now). Sit a small margin inside the band's right
    // edge. A real InvisibleButton handles the click reliably; we SAVE/RESTORE the
    // cursor around it so it never extends the row height or shifts the layout.
    const float pad  = 6.0f * gs;
    const float slot = OutlinerRowH();               // square slot = band height
    const float x0 = outlinerBandRight_ - pad - slot;
    // outlinerRowTopY_ is the BAND top (ListRow::RowTop), so the eye slot spans the
    // band exactly → vertically centred on the visible row.
    const float y0 = outlinerRowTopY_;
    const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(ImVec2(x0, y0));
    ImGui::SetNextItemAllowOverlap();
    bool clk = ImGui::InvisibleButton("##eyebtn", ImVec2(slot, slot));
    ImGui::PopID();
    // Restore the layout cursor to where it was, then submit a zero-size Dummy so
    // ImGui validates the cursor move (a SetCursorScreenPos NOT followed by an item
    // trips the "uses SetCursorPos to extend boundaries" assert — the eye is the
    // last call on the row, so nothing else would validate it).
    ImGui::SetCursorScreenPos(savedCursor);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    // No background — just the glyph (subtle for visible, disabled-tint for hidden).
    const char* ic = visible ? "eye" : "eye-closed";
    ImVec4 tint = ds.GetColor(visible ? DST::Tok::S_Color_Text_Subtle
                                      : DST::Tok::S_Color_Text_Disabled);
    auto md = iconMgr.GetDefaultMetadata(ic);
    if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), ic,
                       ImVec2(x0 + (slot - icon) * 0.5f, y0 + (slot - icon) * 0.5f),
                       icon, md);
    if (clk) { visible = !visible; project_.dirty = true; }
}

// Draw the inline rename InputText for the row about to be replaced, styled like
// the DragValue manual-edit field: exactly one ui-unit tall, NO border and NO
// keyboard-focus ring, and spanning ONLY from the name column (after the chevron
// + icon slots, where the label would start) to just before the eye slot — so it
// never overlaps the dot / chevron / icon / eye. `hasIcon` reserves the icon slot
// (false for the root, which has no type icon). Returns true on Enter/commit.
bool Application::OutlinerRenameField(char* buf, size_t bufSize, bool hasIcon) {
    auto& ds = DST::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float rowH = OutlinerRowH();
    // Row origin = current cursor (rename runs before any content is laid out).
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float dotGutter = ImGui::GetTextLineHeight() * 0.34f + 5.0f * gs;
    float nameX = p0.x + OutlinerBandMargin() + dotGutter + OutlinerChevronSlotW();
    if (hasIcon) nameX += OutlinerIconSlotW() + 4.0f * gs;   // icon slot + its gap
    const float eyePad  = 6.0f * gs;
    const float eyeSlot = OutlinerItemH();
    const float rightX  = outlinerBandRight_ - eyePad - eyeSlot - 4.0f * gs;
    const float width   = std::max(40.0f, rightX - nameX);
    const float padY    = std::max(0.0f, (rowH - ImGui::GetTextLineHeight()) * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(nameX, p0.y));
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ds.GetColor(DST::Tok::S_Background_App_Frame));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));   // no focus ring
    ImGui::PushStyleColor(ImGuiCol_Border,    ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  ds.GetFloat(DST::Tok::C_Frame_CornerRadius) * gs);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, padY));
    ImGui::SetKeyboardFocusHere();
    bool commit = ImGui::InputText("##rename", buf, bufSize,
                                   ImGuiInputTextFlags_EnterReturnsTrue |
                                   ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    // Advance the layout cursor by exactly one stripe (like a real row), so the
    // following rows tile correctly while this one is being renamed.
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCurrentWindow()->WorkRect.Min.x,
        p0.y + UI::ListRowStripeHeight() - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return commit;
}

// Draw a compact summary of a collapsed node's DIRECT contents: one type icon per
// present category, with a small count badge at its lower-right when >1. Drawn
// inline on the current line (after the name). Categories: collections, pages,
// shapes, bezier curves, nurbs curves (and line marks for an object).
void Application::OutlinerCollapsedSummary(uint64_t nodeId) {
    auto& doc = project_.document;
    auto& ds  = DST::DesignSystem::Instance();
    // Tally direct contents.
    int nColl = 0, nPage = 0, nShape = 0, nBez = 0, nNurbs = 0, nMark = 0;
    auto tallyShape = [&](const Renderer::Shape& s) {
        const char* ic = OutlinerShapeIcon(s);
        if (ic == kIconNurbs) ++nNurbs; else if (ic == kIconBezier) ++nBez; else ++nShape;
    };
    if (nodeId & kCollBit) {
        uint64_t cid = nodeId & ~kCollBit;
        if (Renderer::Collection* c = doc.FindCollection(cid)) {
            for (uint64_t ch : c->children) {
                if (doc.IsCollectionId(ch)) ++nColl;
                else if (doc.ArtboardIndexById(ch) >= 0) ++nPage;
            }
            for (Renderer::Artboard& ab : doc.artboards)
                for (Renderer::Shape& s : ab.shapes) if (s.collectionId == cid) tallyShape(s);
            for (Renderer::Shape& s : doc.looseShapes) if (s.collectionId == cid) tallyShape(s);
        }
    } else if (nodeId & kPageBit) {
        uint64_t pid = nodeId & ~kPageBit;
        int abi = doc.ArtboardIndexById(pid);
        if (abi >= 0) {
            Renderer::Artboard& ab = doc.artboards[(size_t)abi];
            for (uint64_t ch : ab.children) if (doc.IsCollectionId(ch)) ++nColl;
            for (Renderer::Shape& s : ab.shapes) if (s.collectionId == 0) tallyShape(s);
        }
    } else if (Renderer::Shape* s = doc.FindShape(nodeId)) {
        for (uint64_t cid : doc.ChildrenOf(s->id))
            if (Renderer::Shape* c = doc.FindShape(cid)) tallyShape(*c);
        for (const Renderer::Part& p : s->parts) nMark += (int)p.marks.size();
    }

    struct Cat { const char* icon; int count; };
    Cat cats[] = {
        { kIconPage,  nPage },
        { kIconShape, nShape },
        { kIconBezier, nBez },
        { kIconNurbs, nNurbs },
    };
    const float icon = OutlinerIconSize();
    const float gs   = ds.GetGlobalScale();
    const float rowH = OutlinerRowH();
    const ImVec4 tint = ds.GetColor(DST::Tok::S_Color_Text_Subtle);
    auto& im = VectorGraphics::IconManager::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto drawCat = [&](const char* ic, int count, ImU32 swatch, bool useSwatch) {
        if (count <= 0) return;
        ImGui::SameLine(0.0f, 6.0f * gs);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float y = p.y + (rowH - icon) * 0.5f;
        if (useSwatch) {
            dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + icon, y + icon), swatch, 2.0f * gs);
        } else if (ic && *ic) {
            auto md = im.GetDefaultMetadata(ic);
            if (!md.colorZones.empty()) {
                for (auto& z : md.colorZones) z.customColor = tint;
                im.RenderIcon(dl, ic, ImVec2(p.x, y), icon, md);
            }
        }
        // Count badge (lower-right), only when more than one.
        if (count > 1) {
            char b[8]; std::snprintf(b, sizeof b, "%d", count);
            ImVec2 ts = ImGui::CalcTextSize(b);
            float bx = p.x + icon - ts.x * 0.5f;
            float by = y + icon - ts.y * 0.7f;
            dl->AddText(ImVec2(bx, by),
                        ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::S_Color_Text_Default)), b);
        }
        ImGui::Dummy(ImVec2(icon + (count > 1 ? 8.0f * gs : 0.0f), OutlinerItemH()));
    };

    // Collections first (swatch — generic subtle), then pages/shapes/curves.
    if (nColl > 0)
        drawCat(nullptr, nColl, ImGui::ColorConvertFloat4ToU32(tint), /*useSwatch=*/true);
    for (const Cat& c : cats) drawCat(c.icon, c.count, 0, false);
    if (nMark > 0) drawCat(kIconShape, nMark, 0, false);   // marks reuse a neutral glyph
}

// One object row: menu-style state background + full-row hitbox, with selection
// sync, rename, drag source, RMB menu and an eye visibility toggle. Honours the
// kind/state filters and the search (hidden when neither it nor a descendant
// matches; the search itself can't have descendants for an object).
void Application::OutlinerObjectRow(Renderer::Shape& s) {
    auto& doc = project_.document;
    if (!OutlinerPassesFilter(s.id, /*kind object*/0)) return;
    if (!OutlinerSearchVisible(s.id)) return;
    outlinerCur_->rowOrder.push_back(s.id);     // for Shift range select

    if (s_renameId == s.id) {
        OutlinerRowFinish();                 // close any previous row
        UI::ListRowAdvanceZebra();           // keep the stripe parity in step
        if (OutlinerRenameField(s_renameBuf, sizeof(s_renameBuf), /*hasIcon=*/true)) {
            s.name = s_renameBuf; s_renameId = 0; project_.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) { s.name = s_renameBuf; s_renameId = 0; project_.dirty = true; }
        return;
    }

    const bool searchHit = outlinerCur_->searchActive &&
        std::find(outlinerCur_->searchMatches.begin(), outlinerCur_->searchMatches.end(), s.id)
            != outlinerCur_->searchMatches.end();
    RowResult rr = OutlinerRowBegin(s.id, /*object*/0, searchHit);
    // Drag source attaches to the row hitbox (the last submitted item). Carries
    // the whole selection when this object is part of it (multi-drag). Disabled
    // when the active module locks the tree (IOF: fixed print-layer hierarchy).
    if (!OutlinerTreeLocked() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(kShapePayload, &s.id, sizeof(uint64_t));
        size_t n = OutlinerDraggedIds(s.id).size();
        const char* nm = s.name.empty() ? "Object" : s.name.c_str();
        if (n > 1) ImGui::Text("%s  (+%d)", nm, (int)n - 1);
        else       ImGui::TextUnformatted(nm);
        ImGui::EndDragDropSource();
    }
    // LAYERS mode (Core only): dropping an object onto this row REORDERS the page's
    // z-stack so the dragged object sits just ABOVE this one (higher z). It never
    // re-collections here — this is the z-index editing affordance of the view.
    if (outlinerCur_->display == OutlinerDisplayMode::Layers && !OutlinerTreeLocked())
        OutlinerReorderDropOnObject(s.id);
    OutlinerHandleRowInput(s.id, rr, /*isObject=*/true);
    if (rr.doubleClicked) { s_renameId = s.id;
                            std::snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", s.name.c_str()); }
    if (rr.rightClicked) {
        outlinerCtxKind_ = OutlinerCtxKind::Object; outlinerCtxId_ = s.id;
        outlinerCtxPos_  = ImGui::GetMousePos(); outlinerCtxOpen_ = true;
    }

    // ── Content (drawn on top of the state background) ──
    // Active dot at the FAR LEFT (ignores indent), state colour (orange / violet
    // when loose) — only an object can be the viewport-active one. Resolved from
    // the shared S_State_* tokens (same mapping the Viewport uses).
    if (s.id == doc.ActiveId()) {
        const bool loose = doc.IsLooseShape(s.id);
        ImU32 dotColor = ImGui::GetColorU32(DST::DesignSystem::Instance().GetColor(
            loose ? DesignSystem::Tok::S_State_Active_Loose
                  : DesignSystem::Tok::S_State_Active_OnPage));
        OutlinerActiveDotAt(outlinerBandLeft_, outlinerRowTopY_, dotColor);
        // Numpad . — recentre this Outliner on the active object's row (Blender's
        // "Frame Selected"). Consumed here so it fires once.
        if (outlinerCur_->reqScrollToActive) {
            ImGui::SetScrollHereY(0.5f);
            outlinerCur_->reqScrollToActive = false;
        }
    }
    OutlinerDotGutter();
    // A chevron when the object has CHILDREN — line marks OR parented child objects.
    // The same open state gates both the mark sub-rows (below) and the child subtree
    // (OutlinerObjectSubtree reads the same key), so it collapses like a collection.
    int markCount = 0;
    for (const Renderer::Part& p : s.parts) markCount += (int)p.marks.size();
    const bool hasChildObjs = !doc.ChildrenOf(s.id).empty();
    const bool collapsible = markCount > 0 || hasChildObjs;
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID openKey = ImGui::GetID((void*)(intptr_t)(s.id ^ 0x9E3779B97F4A7C15ull));
    bool open = store->GetBool(openKey, true);
    if (collapsible) {
        char ocid[32]; std::snprintf(ocid, sizeof(ocid), "##ochev%llu", (unsigned long long)s.id);
        OutlinerChevron(ocid, open);
        store->SetBool(openKey, open);
    } else {
        OutlinerChevronSpacer();           // keep the icon column aligned
    }
    const bool dim = !s.visible;
    // Type icon (shape / bezier / nurbs), dimmed when hidden.
    OutlinerSlotIcon(OutlinerShapeIcon(s),
                     DST::DesignSystem::Instance().GetColor(
                         dim ? DST::Tok::S_Color_Text_Disabled : DST::Tok::C_Outliner_Text));
    ImU32 txt = OutlinerLabelColor(searchHit, dim);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight()) * 0.5f),
        txt, s.name.empty() ? "Object" : s.name.c_str());
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(s.name.empty() ? "Object" : s.name.c_str()).x,
                        OutlinerItemH()));

    // Collapsed → summarise the hidden contents inline (child objects / marks).
    if (collapsible && !open) OutlinerCollapsedSummary(s.id);

    // Eye toggle at the right edge.
    char eid[32]; std::snprintf(eid, sizeof(eid), "##eye%llu", (unsigned long long)s.id);
    OutlinerEyeButton(s.visible, eid);

    // ── Line marks as collapsible SUB-OBJECTS under the curve ──
    // A line mark is half object / half property: indented under its host object with
    // its own name, independently SELECTABLE (drives the per-mark Properties), but it
    // can't be dragged out and no collection can nest under an object — so these rows
    // are NOT drag sources / drop targets.
    if (markCount > 0 && open) {
        const float lh = ImGui::GetTextLineHeight();
        ImGui::Indent(OutlinerChevronSlotW());
        OutlinerMarkRows(s);
        ImGui::Unindent(OutlinerChevronSlotW());
    }
}

// Render object `id`'s row followed by its child objects (parenting) indented
// beneath it — but only children that share the SAME scope (`scopeColl`: a
// collection id, or 0 for "bare on the same page"), so a child re-collectioned
// elsewhere shows under its own collection instead. Recursive (cycle-safe via the
// document's parent graph). The mark sub-rows are still handled inside the row.
void Application::OutlinerObjectSubtree(uint64_t id, uint64_t scopeColl) {
    auto& doc = project_.document;
    Renderer::Shape* s = doc.FindShape(id);
    if (!s) return;
    // Guide-line X = this object's chevron column. The cursor here is the UN-shifted
    // indented row origin; the row content is shifted right by the band margin, so
    // add it to match the chevron's real X.
    const float guideX = OutlinerGuideX(ImGui::GetCursorScreenPos().x + OutlinerBandMargin());
    OutlinerObjectRow(*s);
    // Honour the row's collapse state (same key OutlinerObjectRow uses) — collapsed
    // → don't draw the child subtree, exactly like a collapsed collection.
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID openKey = ImGui::GetID((void*)(intptr_t)(id ^ 0x9E3779B97F4A7C15ull));
    if (!store->GetBool(openKey, true)) return;
    // Gather child objects whose scope matches this subtree level.
    std::vector<Renderer::Shape*> kids;
    for (uint64_t cid : doc.ChildrenOf(id)) {
        Renderer::Shape* c = doc.FindShape(cid);
        if (!c) continue;
        if (scopeColl != 0) { if (c->collectionId != scopeColl) continue; }
        else { if (c->collectionId != 0 ||
                   doc.ArtboardOfShape(c->id) != doc.ArtboardOfShape(id)) continue; }
        kids.push_back(c);
    }
    if (kids.empty()) return;
    std::sort(kids.begin(), kids.end(), [](Renderer::Shape* a, Renderer::Shape* b){
        return a->name < b->name;
    });
    const float lh = ImGui::GetTextLineHeight();
    // First child's stripe top == the bottom of the row just drawn (the object row,
    // or its last mark sub-row). Children tile continuously, so this is exact.
    const float firstChildTop = outlinerLastStripeBottom_;
    ImGui::Indent(OutlinerChevronSlotW());
    for (Renderer::Shape* c : kids) OutlinerObjectSubtree(c->id, scopeColl);
    ImGui::Unindent(OutlinerChevronSlotW());
    // A parented-object subtree gets a DOTTED guide line (vs solid for coll/page),
    // from the first child's stripe top to the last child's stripe bottom.
    OutlinerTreeLine(guideX, firstChildTop, outlinerLastStripeBottom_,
                     ImGui::ColorConvertFloat4ToU32(
                         DST::DesignSystem::Instance().GetColor(DST::Tok::S_Color_Text_Subtle)),
                     /*dotted=*/true);
}

// Render the indented mark rows under object `s`. Selectable (single / Shift),
// no drag, no rename. Fill layers stay on the object (not shown here).
void Application::OutlinerMarkRows(Renderer::Shape& s) {
    auto& doc = project_.document;
    auto& ds  = DST::DesignSystem::Instance();
    for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
        Renderer::Part& part = s.parts[(size_t)pi];
        for (int mi = 0; mi < (int)part.marks.size(); ++mi) {
            const Renderer::LineMark& m = part.marks[(size_t)mi];
            Renderer::MarkRef ref{ s.id, pi, mi };
            const char* kindName =
                m.kind == Renderer::LineMarkKind::SlopeTick ? "Slope tick"
              : m.kind == Renderer::LineMarkKind::Crossing  ? "Crossing point"
              : m.kind == Renderer::LineMarkKind::Bridge    ? "Bridge"
              : m.kind == Renderer::LineMarkKind::DashAnchor
                    ? (m.side >= 0 ? "Dash anchor (dash)" : "Dash anchor (gap)")
              : "Pylon";
            // Synthetic row id (marks have no document id). Reuse the SHARED row
            // chrome (zebra/hover/select) via OutlinerRowBegin with explicit state.
            uint64_t rid = (s.id * 131u + (uint64_t)pi * 17u + (uint64_t)mi + 1u)
                           ^ 0xD15A11C0ull;
            const bool sel = doc.IsMarkSelected(ref);
            const bool act = sel && (doc.ActiveMark() == ref);
            RowResult rr = OutlinerRowBegin(rid, /*kind*/0, /*searchHit*/false,
                                            sel ? 1 : 0, act ? 1 : 0);
            if (rr.clicked) {
                if (ImGui::GetIO().KeyShift) doc.MarkSelectToggle(ref);
                else                         doc.MarkSelectOnly(ref);
                doc.SetActive(s.id);
            }
            // Content over the state bg: dot gutter, chevron + icon spacers (so the
            // mark label lines up with object labels), then the label.
            OutlinerDotGutter();
            OutlinerChevronSpacer();
            OutlinerChevronSpacer();   // icon-slot spacer (same width) — no mark icon
            ImU32 txt = ImGui::GetColorU32(ds.GetColor(sel
                ? DesignSystem::Tok::S_Color_Text_Default
                : DesignSystem::Tok::S_Color_Text_Subtle));
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing()
                       - ImGui::GetTextLineHeight()) * 0.5f), txt, kindName);
            ImGui::Dummy(ImVec2(ImGui::CalcTextSize(kindName).x, OutlinerItemH()));
        }
    }
}

// Shared row input → selection. Press on an UNSELECTED row (no modifier) selects
// it at once (so a fresh item can be dragged immediately); a press on an already
// selected row defers to the RELEASE so a multi-item drag can start instead.
// `clicked` (release without a drag) applies the full modifier semantics.
void Application::OutlinerHandleRowInput(uint64_t id, const RowResult& rr, bool isObject) {
    ImGuiIO& io = ImGui::GetIO();
    const bool mod = io.KeyShift || io.KeyCtrl || io.KeyAlt;
    if (rr.pressed && !mod && !OutlinerIsSelected(id))
        OutlinerSelectClick(id, isObject);          // select-on-press for new item
    else if (rr.clicked)
        OutlinerSelectClick(id, isObject);          // select-on-release otherwise
    if (rr.rightClicked && !OutlinerIsSelected(id))
        OutlinerSelectClick(id, isObject);          // RMB on unselected → select it
}

// A drop target that re-parents a dropped object into collection `collId`
// (0 = document root / no collection).
void Application::OutlinerDropIntoCollection(uint64_t collId) {
    if (OutlinerTreeLocked()) return;          // module owns the tree → no drops
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kShapePayload)) {
            uint64_t sid = *(const uint64_t*)p->Data;
            if (Renderer::Shape* s = project_.document.FindShape(sid);
                s && ModuleAllowsReparent(sid, collId)) {
                s->collectionId = collId;
                project_.document.ReflowLooseShapes();  // collection may be page-less
                MarkUndoLabel("Move to collection");
                project_.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
}

// LAYERS mode z-index reorder: dropping a dragged object (or the whole dragged
// selection) onto `targetId`'s row moves it just ABOVE `targetId` in the page's
// draw stack (higher z). The rows are drawn top-of-stack first, so "above this
// row" = the slot right after `targetId` in draw order; we insert there by moving
// each dragged shape before the target's next draw-order sibling. Only objects on
// the SAME page as the target reorder (cross-page belongs to Collections mode).
void Application::OutlinerReorderDropOnObject(uint64_t targetId) {
    auto& doc = project_.document;
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kShapePayload)) {
        uint64_t sid = *(const uint64_t*)p->Data;
        int abi = doc.ArtboardOfShape(targetId);
        if (abi >= 0) {
            const Renderer::Artboard& ab = doc.artboards[(size_t)abi];
            // The shape drawn just after the target (0 = target is top → append).
            auto nextOf = [&](uint64_t id) -> uint64_t {
                for (size_t i = 0; i + 1 < ab.shapes.size(); ++i)
                    if (ab.shapes[i].id == id) return ab.shapes[i + 1].id;
                return 0;   // target is last (top of stack) → move to the very top
            };
            bool any = false;
            // Move the dragged ids (multi-drag aware), skipping the target itself and
            // any not on this page. Re-evaluate the anchor after each move so a
            // multi-select keeps its relative order above the target.
            for (uint64_t id : OutlinerDraggedIds(sid)) {
                if (id == targetId || doc.ArtboardOfShape(id) != abi) continue;
                if (doc.MoveShapeBeforeInPage(id, nextOf(targetId))) any = true;
            }
            if (any) { MarkUndoLabel("Reorder layer"); project_.dirty = true; }
        }
    }
    ImGui::EndDragDropTarget();
}

// The set of ids a drag should carry: the WHOLE outliner selection if the
// dragged trigger is part of it (multi-drag), else just the trigger.
std::vector<uint64_t> Application::OutlinerDraggedIds(uint64_t triggerId) {
    if (OutlinerIsSelected(triggerId) && outlinerCur_->sel.size() > 1) {
        std::vector<uint64_t> ids = outlinerCur_->sel;
        // Mirror in objects selected in the doc but not yet in outlinerCur_->sel.
        for (uint64_t s : project_.document.Selection())
            if (std::find(ids.begin(), ids.end(), s) == ids.end()) ids.push_back(s);
        return ids;
    }
    return { triggerId };
}

// Drag source for a TREE NODE (a collection or a page) — lets the user reorder /
// reparent it in the unified tree. Carries the node id as kNodePayload; the drop
// handlers expand it to the whole selection when the node is part of it.
void Application::OutlinerNodeDragSource(uint64_t nodeId, const char* label) {
    if (nodeId == Renderer::kProjectRootId) return;   // the root never moves
    if (OutlinerTreeLocked()) return;                 // module owns the tree
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(kNodePayload, &nodeId, sizeof(uint64_t));
        size_t n = OutlinerDraggedIds(nodeId).size();
        if (n > 1) ImGui::Text("%s  (+%d)", label, (int)n - 1);
        else       ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
}

// Drop target on a collection that accepts a dragged tree node (collection or
// page) and re-parents it into this collection (MoveNode guards cycles/root).
// Also accepts an object payload (re-collection), so a collection row is a single
// target for both kinds.
void Application::OutlinerNodeDropInto(uint64_t collId) {
    if (OutlinerTreeLocked()) return;          // module owns the tree → no drops
    auto& doc = project_.document;
    if (ImGui::BeginDragDropTarget()) {
        // A node drag → move every dragged node into this collection.
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNodePayload)) {
            uint64_t nodeId = *(const uint64_t*)p->Data;
            for (uint64_t n : OutlinerDraggedIds(nodeId))
                if (doc.IsCollectionId(n) || doc.IsPageId(n)) doc.MoveNode(n, collId);
            MarkUndoLabel("Move in tree");
            project_.dirty = true;
        }
        // An object drag → re-collection every dragged object.
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kShapePayload)) {
            uint64_t sid = *(const uint64_t*)p->Data;
            for (uint64_t o : OutlinerDraggedIds(sid))
                if (Renderer::Shape* s = doc.FindShape(o);
                    s && ModuleAllowsReparent(o, collId))
                    s->collectionId = collId;
            doc.ReflowLooseShapes();
            MarkUndoLabel("Move to collection");
            project_.dirty = true;
        }
        ImGui::EndDragDropTarget();
    }
}

// Render one collection node + its child collections + its objects (alpha
// sorted). Only the CHEVRON collapses; the label is free for rename/menu.
void Application::OutlinerCollectionNode(uint64_t collId) {
    auto& doc = project_.document;
    Renderer::Collection* coll = doc.FindCollection(collId);
    if (!coll) return;
    if (!OutlinerPassesFilter(collId, /*coll*/2)) return;
    if (!OutlinerSearchVisible(collId)) return;
    outlinerCur_->rowOrder.push_back(collId);
    const float lh = ImGui::GetTextLineHeight();

    // Rename in place (replaces the whole row while active).
    if (s_renameId == (collId | kCollBit)) {
        OutlinerRowFinish(); UI::ListRowAdvanceZebra();
        if (OutlinerRenameField(s_renameBuf, sizeof(s_renameBuf), /*hasIcon=*/true)) {
            coll->name = s_renameBuf; s_renameId = 0; project_.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) { coll->name = s_renameBuf; s_renameId = 0; project_.dirty = true; }
        return;
    }

    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID openKey = ImGui::GetID((void*)(intptr_t)(collId | kCollBit));
    bool open = store->GetBool(openKey, true);

    const bool searchHit = outlinerCur_->searchActive &&
        std::find(outlinerCur_->searchMatches.begin(), outlinerCur_->searchMatches.end(), collId)
            != outlinerCur_->searchMatches.end();
    RowResult rr = OutlinerRowBegin(collId, /*coll*/2, searchHit);
    OutlinerNodeDragSource(collId, coll->name.c_str());  // drag attaches to the hitbox
    OutlinerNodeDropInto(collId);                         // accept node/object drops
    OutlinerHandleRowInput(collId, rr, /*isObject=*/false);
    if (rr.doubleClicked) { s_renameId = (collId | kCollBit);
                            std::snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", coll->name.c_str()); }
    if (rr.rightClicked) {
        outlinerCtxKind_ = OutlinerCtxKind::Collection; outlinerCtxId_ = collId;
        outlinerCtxPos_ = ImGui::GetMousePos(); outlinerCtxOpen_ = true;
    }

    // ── Content over the state bg ──
    const float guideX = OutlinerGuideX(ImGui::GetCursorScreenPos().x);
    const ImU32 collColor = CollectionIconColor(*coll);
    OutlinerDotGutter();
    char cid[32]; std::snprintf(cid, sizeof(cid), "##cchev%llu", (unsigned long long)collId);
    OutlinerChevron(cid, open);
    store->SetBool(openKey, open);
    // Collection swatch: a coloured square aligned/sized like a type icon.
    OutlinerSlotSwatch(collColor);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f),
        txt, coll->name.c_str());
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(coll->name.c_str()).x, OutlinerItemH()));

    if (!open) OutlinerCollapsedSummary(collId | kCollBit);

    // Collection eye: hides/reveals the whole subtree.
    {
        bool hidden = doc.CollectionHidden(collId);
        bool vis = !hidden;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##ceye%llu", (unsigned long long)collId);
        OutlinerEyeButton(vis, eid);
        if (vis == hidden) doc.SetCollectionVisible(collId, vis);   // button flipped it
    }
    if (open) {
        // First child's stripe top == this collection row's stripe bottom.
        const float firstChildTop = outlinerLastStripeBottom_;
        ImGui::Indent(OutlinerChevronSlotW());   // one slot → children align, shifted by 1
        // Children in their stored order: a child id is either a nested
        // collection or a page (unified tree).
        for (uint64_t childId : coll->children) {
            if (doc.IsCollectionId(childId)) OutlinerCollectionNode(childId);
            else if (int ab = doc.ArtboardIndexById(childId); ab >= 0)
                OutlinerPageNode(ab);
        }
        // Then this collection's own objects (alpha sorted): objects tagged with
        // this collectionId, whether they live on a page (ab.shapes) or are loose
        // (page-less). Parented objects nest UNDER their parent (Blender), so only
        // objects whose parent is NOT in this same collection appear at top level;
        // each is drawn with its child subtree indented beneath it.
        std::vector<Renderer::Shape*> objs;
        for (Renderer::Artboard& ab : doc.artboards)
            for (Renderer::Shape& s : ab.shapes)
                if (s.collectionId == collId) objs.push_back(&s);
        for (Renderer::Shape& s : doc.looseShapes)
            if (s.collectionId == collId) objs.push_back(&s);
        std::sort(objs.begin(), objs.end(), [](Renderer::Shape* a, Renderer::Shape* b){
            return a->name < b->name;
        });
        auto parentInThisColl = [&](Renderer::Shape* s){
            if (!s->parentId) return false;
            Renderer::Shape* p = doc.FindShape(s->parentId);
            return p && p->collectionId == collId;
        };
        for (Renderer::Shape* s : objs)
            if (!parentInThisColl(s)) OutlinerObjectSubtree(s->id, collId);
        ImGui::Unindent(OutlinerChevronSlotW());   // must match the Indent above
        // Vertical guide line in the collection's colour, from the first child's
        // stripe top to the last child's stripe bottom.
        OutlinerTreeLine(guideX, firstChildTop, outlinerLastStripeBottom_,
                         collColor, /*dotted=*/false);
    }
}

// Render one page (artboard) node: chevron + name (rename on double-click) + an
// eye toggling the WHOLE page's visibility (objects + layout). Symmetric to a
// collection. `abIndex` is the artboard index.
void Application::OutlinerPageNode(int abIndex) {
    auto& doc = project_.document;
    if (abIndex < 0 || abIndex >= (int)doc.artboards.size()) return;
    Renderer::Artboard& ab = doc.artboards[(size_t)abIndex];
    if (!OutlinerPassesFilter(ab.id, /*page*/1)) return;
    if (!OutlinerSearchVisible(ab.id)) return;
    outlinerCur_->rowOrder.push_back(ab.id);
    const float lh = ImGui::GetTextLineHeight();

    if (s_renameId == (ab.id | kPageBit)) {
        OutlinerRowFinish(); UI::ListRowAdvanceZebra();
        if (OutlinerRenameField(s_renameBuf, sizeof(s_renameBuf), /*hasIcon=*/true)) {
            ab.name = s_renameBuf; s_renameId = 0; project_.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) { ab.name = s_renameBuf; s_renameId = 0; project_.dirty = true; }
        return;
    }

    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID openKey = ImGui::GetID((void*)(intptr_t)(ab.id | kPageBit));
    bool open = store->GetBool(openKey, true);

    const bool searchHit = outlinerCur_->searchActive &&
        std::find(outlinerCur_->searchMatches.begin(), outlinerCur_->searchMatches.end(), ab.id)
            != outlinerCur_->searchMatches.end();
    RowResult rr = OutlinerRowBegin(ab.id, /*page*/1, searchHit);
    OutlinerNodeDragSource(ab.id, ab.name.c_str());      // drag attaches to the hitbox
    // Drop targets on the page header (object → attach/move + uncollection; node →
    // nest a collection under this page). Skipped when the module locks the tree.
    if (!OutlinerTreeLocked() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kShapePayload)) {
            uint64_t sid = *(const uint64_t*)p->Data;
            for (uint64_t o : OutlinerDraggedIds(sid)) {
                Renderer::Shape* s = doc.FindShape(o);
                if (!s) continue;
                // Dropping on a page header un-collections to the page root; a
                // module may forbid leaving its layer collection (collectionId 0).
                if (!ModuleAllowsReparent(o, 0)) continue;
                if (doc.IsLooseShape(o))                 doc.AttachShapeToPage(o, abIndex);
                else if (doc.ArtboardOfShape(o) != abIndex) doc.MoveShapeToArtboard(o, abIndex, true);
                // Whether it moved pages or was already here, dropping it on the
                // page header puts it at the page's ROOT (out of any collection).
                if (Renderer::Shape* s2 = doc.FindShape(o)) s2->collectionId = 0;
            }
            MarkUndoLabel("Move to page"); project_.dirty = true;
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kNodePayload)) {
            uint64_t nodeId = *(const uint64_t*)p->Data;
            for (uint64_t n : OutlinerDraggedIds(nodeId))
                if (doc.IsCollectionId(n) || doc.IsPageId(n)) doc.MoveNode(n, ab.id);
            MarkUndoLabel("Move in tree"); project_.dirty = true;
        }
        ImGui::EndDragDropTarget();
    }
    OutlinerHandleRowInput(ab.id, rr, /*isObject=*/false);
    if (rr.doubleClicked) { s_renameId = (ab.id | kPageBit);
                            std::snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", ab.name.c_str()); }
    if (rr.rightClicked) { pageCtxRequest_ = true; pageCtxArtboard_ = abIndex;
                           pageCtxPos_ = ImGui::GetMousePos(); }

    // ── Content over the state bg ──
    const float guideX = OutlinerGuideX(ImGui::GetCursorScreenPos().x);
    const ImU32 pageLineColor = ImGui::ColorConvertFloat4ToU32(
        DST::DesignSystem::Instance().GetColor(DST::Tok::S_Color_Border_Default));
    OutlinerDotGutter();
    char cid[32]; std::snprintf(cid, sizeof(cid), "##pchev%llu", (unsigned long long)ab.id);
    OutlinerChevron(cid, open);
    store->SetBool(openKey, open);
    OutlinerSlotIcon(kIconPage, DST::DesignSystem::Instance().GetColor(DST::Tok::C_Outliner_Text));
    char label[96];
    std::snprintf(label, sizeof(label), "%s  (%.0f x %.0f)",
                  ab.name.c_str(), ab.size.x, ab.size.y);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f), txt, label);
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(label).x, OutlinerItemH()));

    if (!open) OutlinerCollapsedSummary(ab.id | kPageBit);

    // Page eye: hide/show the whole page.
    {
        bool vis = ab.pageVisible;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##peye%llu", (unsigned long long)ab.id);
        OutlinerEyeButton(vis, eid);
        if (vis != ab.pageVisible) { ab.pageVisible = vis; project_.dirty = true; }
    }
    if (open) {
        // First child's stripe top == this page row's stripe bottom.
        const float firstChildTop = outlinerLastStripeBottom_;
        ImGui::Indent(OutlinerChevronSlotW());   // one slot → children align, shifted by 1
        OutlinerDropIntoCollection(0);     // drop an object here → un-collection
        // Collections nested under this page (a page is a full tree node, 8c).
        for (uint64_t childId : ab.children)
            if (doc.IsCollectionId(childId)) OutlinerCollectionNode(childId);
        // Objects directly on this page (no collection). Parented objects nest
        // under their parent; only objects whose parent isn't another bare object
        // on THIS page appear at top level.
        std::vector<Renderer::Shape*> objs;
        for (Renderer::Shape& s : ab.shapes)
            if (s.collectionId == 0) objs.push_back(&s);
        std::sort(objs.begin(), objs.end(), [](Renderer::Shape* a, Renderer::Shape* b){
            return a->name < b->name;
        });
        uint64_t pageColl = 0; (void)pageColl;
        auto parentBareOnPage = [&](Renderer::Shape* s){
            if (!s->parentId) return false;
            Renderer::Shape* p = doc.FindShape(s->parentId);
            return p && p->collectionId == 0 &&
                   doc.ArtboardOfShape(p->id) == doc.ArtboardOfShape(s->id);
        };
        for (Renderer::Shape* s : objs)
            if (!parentBareOnPage(s)) OutlinerObjectSubtree(s->id, 0);
        ImGui::Unindent(OutlinerChevronSlotW());
        OutlinerTreeLine(guideX, firstChildTop, outlinerLastStripeBottom_,
                         pageLineColor, /*dotted=*/false);
    }
}

// LAYERS-mode page node: the page header (rename / hide) followed by EVERY object
// on the page listed in DRAW order — top of the stack first (last drawn → top of
// the list), so the rows read like a layer stack / the print order. Collections
// are ignored here (this is the flat z-order view); no reparent drop targets.
void Application::OutlinerPageLayersNode(int abIndex) {
    auto& doc = project_.document;
    if (abIndex < 0 || abIndex >= (int)doc.artboards.size()) return;
    Renderer::Artboard& ab = doc.artboards[(size_t)abIndex];
    if (!OutlinerPassesFilter(ab.id, /*page*/1)) return;
    if (!OutlinerSearchVisible(ab.id)) return;
    outlinerCur_->rowOrder.push_back(ab.id);
    const float lh = ImGui::GetTextLineHeight();

    if (s_renameId == (ab.id | kPageBit)) {
        OutlinerRowFinish(); UI::ListRowAdvanceZebra();
        if (OutlinerRenameField(s_renameBuf, sizeof(s_renameBuf), /*hasIcon=*/true)) {
            ab.name = s_renameBuf; s_renameId = 0; project_.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) { ab.name = s_renameBuf; s_renameId = 0; project_.dirty = true; }
        return;
    }

    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID openKey = ImGui::GetID((void*)(intptr_t)((ab.id | kPageBit) ^ 0x1A4E5ull));
    bool open = store->GetBool(openKey, true);

    const bool searchHit = outlinerCur_->searchActive &&
        std::find(outlinerCur_->searchMatches.begin(), outlinerCur_->searchMatches.end(), ab.id)
            != outlinerCur_->searchMatches.end();
    RowResult rr = OutlinerRowBegin(ab.id, /*page*/1, searchHit);
    OutlinerNodeDragSource(ab.id, ab.name.c_str());      // drag the page (locked → no-op)
    OutlinerHandleRowInput(ab.id, rr, /*isObject=*/false);
    if (rr.doubleClicked) { s_renameId = (ab.id | kPageBit);
                            std::snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", ab.name.c_str()); }
    if (rr.rightClicked) { pageCtxRequest_ = true; pageCtxArtboard_ = abIndex;
                           pageCtxPos_ = ImGui::GetMousePos(); }

    // ── Content over the state bg ──
    OutlinerDotGutter();
    char cid[32]; std::snprintf(cid, sizeof(cid), "##plchev%llu", (unsigned long long)ab.id);
    OutlinerChevron(cid, open);
    store->SetBool(openKey, open);
    OutlinerSlotIcon(kIconPage, DST::DesignSystem::Instance().GetColor(DST::Tok::C_Outliner_Text));
    char label[96];
    std::snprintf(label, sizeof(label), "%s  (%.0f x %.0f)",
                  ab.name.c_str(), ab.size.x, ab.size.y);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f), txt, label);
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(label).x, OutlinerItemH()));

    {
        bool vis = ab.pageVisible;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##pleye%llu", (unsigned long long)ab.id);
        OutlinerEyeButton(vis, eid);
        if (vis != ab.pageVisible) { ab.pageVisible = vis; project_.dirty = true; }
    }
    if (open) {
        ImGui::Indent(OutlinerChevronSlotW());
        // Every object on the page, in REVERSE draw order (top of the z-stack at
        // the top of the list). No collection grouping, no parenting nesting — a
        // flat layer list. Parented children still show via the object row's own
        // mark/child handling, but here we list each shape once at top level.
        for (size_t i = ab.shapes.size(); i-- > 0; )
            OutlinerObjectRow(ab.shapes[i]);
        ImGui::Unindent(OutlinerChevronSlotW());
    }
}

void Application::RenderOutliner(EditorState& st) {
    auto& ds  = DST::DesignSystem::Instance();
    auto& doc = project_.document;
    outlinerCur_ = &st.outliner;   // this Outliner leaf owns the state this frame
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Default));

    // ── Viewport-sync upkeep (the top-bar "synchronise" button). ─────────────
    // Drop a stale target (its zone was closed/changed), and while picking, draw
    // the follow-the-mouse prompt + honour the RMB/Escape cancel. The orange
    // hover preview + click-to-confirm live in each Viewport (RenderViewport).
    if (outlinerCur_->syncTarget &&
        !zoneLayout_.IsLiveEditorState(outlinerCur_->syncTarget, CoreEditor::Viewport))
        outlinerCur_->syncTarget = nullptr;
    // Cache the synced viewport's per-page visibility for this frame's filter.
    outlinerCur_->syncPageVisible.clear();
    if (outlinerCur_->syncTarget) {
        std::vector<PageView> pv = ComputePageViews(outlinerCur_->syncTarget->pageLayout, doc);
        outlinerCur_->syncPageVisible.resize(pv.size());
        for (size_t i = 0; i < pv.size(); ++i) outlinerCur_->syncPageVisible[i] = pv[i].visible;
        outlinerCur_->syncOrphans = outlinerCur_->syncTarget->nPanelShowOrphans;
    }
    if (outlinerCur_->syncPicking) {
        bool stop = false;
        if (zoneLayout_.CountEditors(CoreEditor::Viewport) == 0) stop = true;  // nothing to pick
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Right)) stop = true;    // cancel
        if (stop) {
            outlinerCur_->syncPicking = false;
            if (outlinerPickingState_ == outlinerCur_) outlinerPickingState_ = nullptr;
        } else {
            UI::DrawTooltipTranslucent("Select a viewport to synchronise with",
                ImGui::GetMousePos(),
                ds.GetFloat(DST::Tok::S_Opacity_Moderate));     // translucent bg
        }
    }

    UI::ListRowResetZebra();   // reset stripe parity at the top of the tree
    outlinerCur_->rowOrder.clear();          // rebuilt per frame for Shift-range select
    OutlinerRebuildSearch();             // recompute search matches for this frame

    // Keep the outliner selection's OBJECTS in sync with the document (viewport →
    // outliner): drop object ids no longer selected in the doc, add newly selected
    // ones, and keep non-object (collection/page) ids untouched.
    {
        std::vector<uint64_t> next;
        for (uint64_t id : outlinerCur_->sel)
            if (!doc.FindShape(id) || doc.IsSelected(id)) next.push_back(id);  // keep nodes + still-selected objs
        for (uint64_t id : doc.Selection())
            if (std::find(next.begin(), next.end(), id) == next.end()) next.push_back(id);
        outlinerCur_->sel.swap(next);
        if (doc.ActiveId()) outlinerCur_->active = doc.ActiveId();  // active obj drives it
    }

    // The whole tree hangs off the single "Project" root collection. Its row is
    // the project title (not renamable/removable); its children (collections AND
    // pages, in order) + its loose objects follow.
    doc.EnsureProjectRoot();
    Renderer::Collection* root = doc.FindCollection(Renderer::kProjectRootId);
    std::string title = project_.TabTitle();
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID rootKey = ImGui::GetID("##prjroot");
    bool rootOpen = store->GetBool(rootKey, true);
    // Root row via the generic ListRow (zebra + hit, no selection band). The
    // chevron + title draw inside the band, aligned like every other row.
    {
        UI::ListRowConfig rc;
        rc.id = (ImGuiID)0x12345678u;   // stable id for the project-root row
        rc.zebraOdd = (UI::ListRowZebraIndex() & 1) != 0;
        rc.zebraColor = ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::S_Color_Background_Layer2));
        rc.bandMarginLeft = OutlinerBandMargin();
        rc.cornerRadius   = ds.GetFloat(DST::Tok::S_CornerRadius_Control);
        outlinerRow_.reset();
        outlinerRow_.emplace(rc);
        OutlinerDotGutter();
        char rcid[16]; std::snprintf(rcid, sizeof(rcid), "##prjchev");
        OutlinerChevron(rcid, rootOpen);
        store->SetBool(rootKey, rootOpen);
        OutlinerSlotIcon(kIconFolder, ds.GetColor(DST::Tok::C_Outliner_Text));
        ImVec2 tp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tp.x, tp.y + (OutlinerRowH() - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::C_Outliner_Text)),
            title.c_str());
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(title.c_str()).x, OutlinerItemH()));
        OutlinerNodeDropInto(Renderer::kProjectRootId);  // drop a node/object onto root
    }
    if (rootOpen && root) {
        ImGui::Indent(OutlinerChevronSlotW());
        if (outlinerCur_->display == OutlinerDisplayMode::Layers) {
            // LAYERS mode: objects per PAGE in DRAW order (z-order), ignoring the
            // collection tree. Each page is a separate render, so they stay split.
            for (int ab = 0; ab < (int)doc.artboards.size(); ++ab)
                OutlinerPageLayersNode(ab);
            // Page-less (loose) objects have no page render; list them after the
            // pages so nothing is hidden in this view (none in IOF — all on-page).
            for (size_t i = doc.looseShapes.size(); i-- > 0; )
                OutlinerObjectRow(doc.looseShapes[i]);
        } else {
            // COLLECTIONS mode: the unified collection/page tree. Children in order:
            // nested collections and pages. Objects appear under their page
            // (collectionId 0) or under their collection (collectionId).
            for (uint64_t childId : root->children) {
                if (doc.IsCollectionId(childId)) OutlinerCollectionNode(childId);
                else if (int ab = doc.ArtboardIndexById(childId); ab >= 0)
                    OutlinerPageNode(ab);
            }
            // Loose (page-less) objects parented directly to the root — i.e. their
            // collection is the root / 0 / a now-deleted collection. (Loose objects
            // inside a real collection are listed by that collection's node.)
            std::vector<Renderer::Shape*> rootLoose;
            for (Renderer::Shape& s : doc.looseShapes) {
                uint64_t c = s.collectionId;
                if (c == 0 || c == Renderer::kProjectRootId || !doc.FindCollection(c))
                    rootLoose.push_back(&s);
            }
            std::sort(rootLoose.begin(), rootLoose.end(),
                      [](Renderer::Shape* a, Renderer::Shape* b){ return a->name < b->name; });
            for (Renderer::Shape* s : rootLoose) OutlinerObjectRow(*s);
        }
        ImGui::Unindent(OutlinerChevronSlotW());
    }

    // Close the last open row (its destructor advances the cursor one stripe).
    OutlinerRowFinish();

    // Continue the zebra stripes to the BOTTOM of the editor even past the last
    // row, so the alternation never stops mid-panel.
    {
        const float h = UI::ListRowStripeHeight();
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        float y = ImGui::GetCursorScreenPos().y;
        const float bottom = w->WorkRect.Max.y;
        const float L = OutlinerRowLeft(), R = OutlinerRowRight();   // edge to edge
        ImU32 stripe = ImGui::ColorConvertFloat4ToU32(
            ds.GetColor(DST::Tok::S_Color_Background_Layer2));
        while (y < bottom) {
            if (UI::ListRowZebraIndex() & 1)
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(L, y), ImVec2(R, std::min(y + h, bottom)), stripe);
            y += h;
            UI::ListRowAdvanceZebra();
        }
    }

    // Clicks on the EMPTY background: LMB clears the outliner selection (keeps the
    // document active object), RMB opens the Add Collection menu. Suppressed while
    // any popup/menu is open (a geometric click test would otherwise fire THROUGH
    // an open context menu drawn over the panel).
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!anyPopupOpen &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            outlinerCur_->sel.clear(); outlinerCur_->active = 0;
            project_.document.DeselectAll();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            outlinerCtxKind_ = OutlinerCtxKind::Background;
            outlinerCtxId_   = 0;
            outlinerCtxPos_  = ImGui::GetMousePos();
            outlinerCtxOpen_ = true;
        }
    }

    RenderOutlinerContextMenus();
    // Clear a Frame-Selected request even if the active row wasn't drawn this
    // frame (filtered/collapsed/hidden) — so it doesn't fire spuriously later.
    outlinerCur_->reqScrollToActive = false;
    ImGui::PopStyleColor();
}

// ── Outliner top-bar groups: Sync+Display (left chips) | Search | Filter ──
// Each group's draw lambda captures THIS Outliner leaf's state pointer (`os`)
// and re-points outlinerCur_ at it before touching state, because the top bars
// of all zones are built first and drawn later — outlinerCur_ would otherwise
// reflect whichever Outliner rendered last. `st` (EditorState) is persistent in
// the zone tree, so capturing &st.outliner by value is safe.
void Application::BuildOutlinerTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = DST::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float h  = ds.GetFloat(DST::Tok::S_Size_ControlHeight) * gs;
    OutlinerState* os = &st.outliner;
    outlinerCur_ = os;

    // LEFT: Display-mode dropdown.
    bar.left.width = 110.0f * gs;
    bar.left.draw  = [this, os](ImVec2 pos, float) {
        outlinerCur_ = os;
        ImGui::SetCursorPos(pos);
        static const char* kModes[] = { "Collections", "Layers" };
        UI::DropdownConfig cfg; cfg.id = "##outDisplay";
        cfg.triggerLabel = kModes[(int)outlinerCur_->display];
        for (const char* m : kModes) { UI::DropdownItem it; it.label = m; cfg.items.push_back(it); }
        cfg.selectedIndex = (int)outlinerCur_->display;
        UI::DropdownResult res = UI::Dropdown(cfg);
        if (res.changed) outlinerCur_->display = (OutlinerDisplayMode)res.selected;
    };

    // MIDDLE: search field, forced to exactly one ui-unit tall (so it doesn't
    // overshoot the bar). FramePadding.y is set so fontSize + 2·pad == uiU.
    bar.middle.width = 200.0f * gs;
    bar.middle.draw  = [this, os, gs, h](ImVec2 pos, float) {
        outlinerCur_ = os;
        ImGui::SetCursorPos(pos);
        float padY = std::max(0.0f, (h - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, padY));
        ImGui::SetNextItemWidth(200.0f * gs);
        ImGui::InputTextWithHint("##outSearch", "Search…", outlinerCur_->search,
                                 sizeof(outlinerCur_->search));
        ImGui::PopStyleVar();
    };

    // RIGHT: a viewport-sync toggle button, then a Filter dropdown (control-
    // height, dropdown-style trigger) whose popup holds the kind toggles + an
    // Object-state dropdown widget + an invert icon button beside it.
    const float syncW = h;               // square icon button, control-height
    const float grpGap = 6.0f * gs;      // gap between the sync button and Filter
    bar.right.width = syncW + grpGap + 100.0f * gs;
    bar.right.draw  = [this, os, gs, h, syncW, grpGap](ImVec2 pos, float) {
        outlinerCur_ = os;
        auto& ds2 = DST::DesignSystem::Instance();
        auto& iconMgr = VectorGraphics::IconManager::Instance();

        // ── Sync-with-viewport button (left of Filter) ──────────────────────
        // Disabled when no Viewport zone exists. Click: arm picking (or, when
        // already synced/picking, toggle the whole thing off).
        const bool hasViewport = zoneLayout_.CountEditors(CoreEditor::Viewport) > 0;
        const bool syncOn = outlinerCur_->syncTarget != nullptr || outlinerCur_->syncPicking;
        ImGui::SetCursorPos(pos);
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!hasViewport);
        bool sclk = ImGui::InvisibleButton("##outSyncBtn", ImVec2(syncW, h));
        bool shov = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec4 sbg = ds2.GetColor(syncOn ? DST::Tok::S_Color_Accent_Default
                                    : shov   ? DST::Tok::C_IconButton_BackgroundHover
                                             : DST::Tok::C_IconButton_Background);
            if (!hasViewport) sbg.w *= ds2.GetFloat(DST::Tok::S_Opacity_Disabled);
            float rad = ds2.GetFloat(DST::Tok::C_Dropdown_CornerRadius) * gs;
            dl->AddRectFilled(sp, ImVec2(sp.x + syncW, sp.y + h),
                              ImGui::ColorConvertFloat4ToU32(sbg), rad);
            float isz = ds2.GetFloat(DST::Tok::C_Dropdown_IconSize) * gs;
            ImVec4 tn = ds2.GetColor(DST::Tok::S_Color_Text_Default);
            if (!hasViewport) tn.w *= ds2.GetFloat(DST::Tok::S_Opacity_Disabled);
            auto md = iconMgr.GetDefaultMetadata("arrow-warm-up");
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tn;
            iconMgr.RenderIcon(dl, "arrow-warm-up",
                               ImVec2(sp.x + (syncW-isz)*0.5f, sp.y + (h-isz)*0.5f), isz, md);
        }
        if (sclk && hasViewport) {
            if (syncOn) {                       // toggle the whole thing off
                outlinerCur_->syncTarget  = nullptr;
                outlinerCur_->syncPicking = false;
                if (outlinerPickingState_ == outlinerCur_) outlinerPickingState_ = nullptr;
            } else {                            // arm "pick a viewport" (modal)
                outlinerCur_->syncPicking = true;
                outlinerPickingState_     = outlinerCur_;
            }
        }
        if (shov && hasViewport && !syncOn &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip("Synchronise the Outliner with a viewport's visible objects",
                            ImGui::GetMousePos());

        ImGui::SetCursorPos(ImVec2(pos.x + syncW + grpGap, pos.y));
        // The Filter dropdown now reuses the shared UI::Dropdown component with a
        // CUSTOM BODY (same menu chrome/style as every other dropdown), instead of a
        // hand-rolled trigger + popup.
        const bool anyFilter = !outlinerCur_->showObjects || !outlinerCur_->showPages ||
                         !outlinerCur_->showCollections || !outlinerCur_->showMeshes ||
                         !outlinerCur_->showCurves || outlinerCur_->objState != ObjStateFilter::All ||
                         outlinerCur_->invertFilter;
        UI::DropdownConfig fc;
        fc.id = "##outFilter";
        fc.triggerIcon = anyFilter ? "filter" : "filter_off";
        fc.triggerLabel = "Filter";
        fc.menuSize = ImVec2(210.0f * gs, 230.0f * gs);
        fc.bodyDraw = [this, &ds2, gs, &iconMgr, h]() {
            ImGui::TextDisabled("Show");
            UI::Checkbox("##fObjects",     "Objects",     &outlinerCur_->showObjects);
            UI::Checkbox("##fPages",       "Pages",       &outlinerCur_->showPages);
            UI::Checkbox("##fCollections", "Collections", &outlinerCur_->showCollections);
            ImGui::Separator();
            UI::Checkbox("##fMeshes", "Meshes", &outlinerCur_->showMeshes);
            UI::Checkbox("##fCurves", "Curves", &outlinerCur_->showCurves);
            ImGui::Separator();
            // Object state: a nested dropdown + an invert icon button to its right.
            static const char* kStates[] = { "All", "Visible", "Selected", "Active", "Selectable" };
            UI::DropdownConfig sc; sc.id = "##outState";
            sc.triggerLabel = std::string("State: ") + kStates[(int)outlinerCur_->objState];
            for (int i = 0; i < 5; ++i) { UI::DropdownItem it; it.label = kStates[i]; sc.items.push_back(it); }
            sc.selectedIndex = (int)outlinerCur_->objState;
            UI::DropdownResult sr = UI::Dropdown(sc);
            if (sr.changed && sr.selected >= 0 && sr.selected < 5)
                outlinerCur_->objState = (ObjStateFilter)sr.selected;
            ImGui::SameLine(0.0f, 4.0f * gs);
            ImGui::PushStyleColor(ImGuiCol_Button, outlinerCur_->invertFilter
                ? ds2.GetColor(DST::Tok::S_Color_Accent_Default)
                : ds2.GetColor(DST::Tok::C_IconButton_Background));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(DST::Tok::C_IconButton_BackgroundHover));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
            if (ImGui::Button("##invFilter", ImVec2(h, h))) outlinerCur_->invertFilter = !outlinerCur_->invertFilter;
            ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
            { ImVec2 bm = ImGui::GetItemRectMin();
              float isz = ds2.GetFloat(DST::Tok::C_Dropdown_IconSize) * gs;
              ImVec2 ip(bm.x + (h-isz)*0.5f, bm.y + (h-isz)*0.5f);
              ImVec4 tn = ds2.GetColor(DST::Tok::S_Color_Text_Default);
              auto md = iconMgr.GetDefaultMetadata("swap_horiz");
              if (!md.colorZones.empty()) { md.colorZones[0].customColor = tn;
                  iconMgr.RenderIcon(ImGui::GetWindowDrawList(), "swap_horiz", ip, isz, md); } }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                UI::DrawTooltip("Invert the filter (show the complement)", ImGui::GetMousePos());
        };
        UI::Dropdown(fc);
    };
}


} // namespace App

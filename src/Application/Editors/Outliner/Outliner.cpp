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
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {

namespace DST = DesignSystem;

namespace {
// Per-frame scratch for inline renaming (one rename at a time across the tree).
// The id is the target: a shape id (no tag bit), a collection id | kCollBit, or
// an artboard id | kPageBit.
static uint64_t s_renameId   = 0;
static char     s_renameBuf[128] = {0};
constexpr uint64_t kCollBit = 1ull << 63;   // tag a rename target as a collection
constexpr uint64_t kPageBit = 1ull << 62;   // tag a rename target as a page

// Zebra row striping: a per-frame row counter (reset at the top of RenderOutliner)
// so alternate rows get a slightly lighter background — Blender-style, easier to
// track across the tree. The fill is drawn full-width behind each row.
static int  s_zebraRow = 0;

const char* kShapePayload = "OUTLINER_SHAPE";   // drag payload: shape id (u64)
const char* kNodePayload  = "OUTLINER_NODE";    // drag payload: tree node id (u64)
                                                // (a collection OR a page)

// Collection icon colour palette. Index 0 = default (theme text); 1..N map to a
// design-system primitive hue (resolved at draw time so it follows the theme).
struct Hue { const char* name; DST::Tok tok; };
const Hue kCollHues[] = {
    { "Cyan",       DST::Tok::P_Color_Cyan_500       },
    { "Indigo",     DST::Tok::P_Color_Indigo_500     },
    { "Cinnamon",   DST::Tok::P_Color_Cinnamon_500   },
    { "Green",      DST::Tok::P_Color_Green_500       },
    { "Yellow",     DST::Tok::P_Color_Yellow_500     },
    { "Orange",     DST::Tok::P_Color_Orange_500     },
    { "Red",        DST::Tok::P_Color_Red_500         },
    { "Magenta",    DST::Tok::P_Color_Magenta_500    },
    { "Purple",     DST::Tok::P_Color_Purple_500     },
    { "Turquoise",  DST::Tok::P_Color_Turquoise_500  },
};
constexpr int kNumCollHues = (int)(sizeof(kCollHues) / sizeof(kCollHues[0]));

// Resolve a collection's icon colour to an ImU32. colorIndex 0 = theme text,
// 1..N = palette hue, −1 = its stored customColor.
ImU32 CollectionIconColor(const Renderer::Collection& c) {
    auto& ds = DST::DesignSystem::Instance();
    if (c.colorIndex < 0)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(c.customColor.r, c.customColor.g, c.customColor.b, c.customColor.a));
    if (c.colorIndex == 0)
        return ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::S_Color_Text_Subtle));
    int i = (c.colorIndex - 1) % kNumCollHues;
    return ImGui::ColorConvertFloat4ToU32(ds.GetColor(kCollHues[(size_t)i].tok));
}

// Draw the zebra background for the row that is ABOUT to be laid out at the
// current cursor Y, spanning the FULL window width. Call before submitting the
// row's widgets. Advances the stripe counter. Odd rows get a slightly lighter
// fill; even rows stay on the editor base (transparent here).
// Row height used for the zebra stripes (matches a tree/selectable row).
float ZebraRowHeight() { return ImGui::GetTextLineHeightWithSpacing(); }

void ZebraRowBg() {
    auto& ds = DST::DesignSystem::Instance();
    const float h = ZebraRowHeight();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (s_zebraRow & 1) {
        ImVec2 mn(w->WorkRect.Min.x, p.y);
        ImVec2 mx(w->WorkRect.Max.x + ImGui::GetStyle().ScrollbarSize, p.y + h);
        ImGui::GetWindowDrawList()->AddRectFilled(
            mn, mx, ImGui::ColorConvertFloat4ToU32(
                        ds.GetColor(DST::Tok::S_Color_Background_Layer2)));
    }
    ++s_zebraRow;
}

// A small collapse chevron (Dropdown chevron size), clickable on the GLYPH only.
// Toggles `open`. Returns the width consumed (so the caller advances past it).
// Unlike a tree node header, clicking the row's label does NOT collapse — only
// this chevron does (so the label is free for select / rename / menu).
float OutlinerChevron(const char* id, bool& open) {
    auto& ds      = DST::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float gs   = ds.GetGlobalScale();
    const float chev = ds.GetFloat(DST::Tok::C_Dropdown_ChevronSize) * gs;
    const float h    = ImGui::GetTextLineHeight();
    const float slot = h;                       // square hit slot, line-tall
    ImGui::PushID(id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##chev", ImVec2(slot, h))) open = !open;
    ImVec2 ipos(p0.x + (slot - chev) * 0.5f, p0.y + (h - chev) * 0.5f);
    auto md = iconMgr.GetDefaultMetadata(open ? "chevron-down" : "chevron-right");
    if (!md.colorZones.empty())
        md.colorZones[0].customColor = ds.GetColor(DST::Tok::S_Color_Text_Subtle);
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(),
                       open ? "chevron-down" : "chevron-right", ipos, chev, md);
    ImGui::PopID();
    return slot;
}

// Reserve a small fixed gutter at the start of EVERY row (so the active dot,
// drawn at the far left in absolute coords, never overlaps the content). Pure
// layout: no drawing. Call at the very start of a row's content.
void OutlinerDotGutter() {
    const float d = ImGui::GetTextLineHeight() * 0.30f;
    ImGui::Dummy(ImVec2(d + 4.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 2.0f);
}
// Draw the "active" dot at the FAR LEFT of the row (window left edge, ignoring
// the tree indent), vertically centred on the row. Absolute coords → no layout.
void OutlinerActiveDotAt(float rowTopY, ImU32 col) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const float d = ImGui::GetTextLineHeight() * 0.30f;     // dot diameter
    ImVec2 c(w->WorkRect.Min.x + 2.0f + d * 0.5f, rowTopY + rowH * 0.5f);
    w->DrawList->AddCircleFilled(c, d * 0.5f, col);
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

// Menu-style row: a full-width hitbox over the zebra + a state background with a
// small corner radius and a 1px(v)/2px(h) inset (so the zebra colour shows around
// it, like a menu item). The hitbox covers the WHOLE row (no dead zone); the
// content draws on top afterwards. Search remaps the accent → green (positive).
Application::RowResult Application::OutlinerRowBegin(uint64_t id, int kind, bool searchHit,
                                                    int forceSel, int forceActive) {
    auto& ds = DST::DesignSystem::Instance();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();      // row top-left (indented)
    const float left  = w->WorkRect.Min.x;
    const float right = w->WorkRect.Max.x;

    // Full-row hitbox: an InvisibleButton spanning the entire content width at the
    // row's Y. We place it at the window's left edge (full width) so there's no
    // dead zone, then rewind so the content overdraws it.
    ImGui::SetCursorScreenPos(ImVec2(left, p0.y));
    char bid[24]; std::snprintf(bid, sizeof(bid), "##row%llu", (unsigned long long)id);
    ImGui::SetNextItemAllowOverlap();   // chevron / eye sit on top and win their area
    ImGui::InvisibleButton(bid, ImVec2(std::max(1.0f, right - left), rowH),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    RowResult r;
    r.hovered      = ImGui::IsItemHovered();
    r.pressed      = ImGui::IsItemActivated();   // mouse-DOWN on the row
    // A "click" that should change the selection fires on RELEASE without a drag,
    // so a press on an already-selected row can start a multi-item drag instead.
    r.clicked      = ImGui::IsItemDeactivated() &&
                     ImGui::IsItemHovered() &&
                     !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
    r.rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    r.doubleClicked= ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    auto& docA = const_cast<Renderer::Document&>(project_.document);
    // Selection / active state: explicit override (line-mark rows) or auto from id.
    const bool selected = forceSel >= 0 ? (forceSel != 0) : OutlinerIsSelected(id);
    const bool active   = forceActive >= 0 ? (forceActive != 0)
                        : ((outlinerCur_->active == id) ||
                           (docA.FindShape(id) && docA.ActiveId() == id));

    // The six row states map to design-system COMPONENT tokens. There are two
    // families: NORMAL (default status → grey hover / blue selected/active) and
    // SEARCH (positive status → green), swapped when a search is active and this
    // row matched. Selection/active bgs are opaque; hover is drawn semi-
    // transparent so it stays light over the zebra.
    const bool useSearch = outlinerCur_->searchActive && searchHit;
    auto tok = [&](DST::Tok normal, DST::Tok search){
        return ds.GetColor(useSearch ? search : normal);
    };
    auto opaque = [](ImVec4 c){ return ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.x, c.y, c.z, 1.0f)); };
    auto withA  = [](ImVec4 c, float a){ return ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.x, c.y, c.z, a)); };

    using T = DST::Tok;
    ImU32 bg = 0;
    if (selected && active)
        bg = opaque(tok(r.hovered ? T::C_Outliner_Row_ActiveHover : T::C_Outliner_Row_Active,
                        r.hovered ? T::C_Outliner_Search_ActiveHover : T::C_Outliner_Search_Active));
    else if (selected)
        bg = opaque(tok(r.hovered ? T::C_Outliner_Row_SelectedHover : T::C_Outliner_Row_Selected,
                        r.hovered ? T::C_Outliner_Search_SelectedHover : T::C_Outliner_Search_Selected));
    else if (r.hovered)
        bg = withA(tok(T::C_Outliner_Row_Hover, T::C_Outliner_Search_Hover), 0.55f);
    // A matched (but idle) search row shows a faint green tint so it reads "found".
    if (!selected && !r.hovered && useSearch)
        bg = withA(ds.GetColor(T::C_Outliner_Search_Visual), 0.45f);

    if (bg) {
        const float radius = ds.GetFloat(DST::Tok::S_CornerRadius_Control);
        ImVec2 a(left + 2.0f, p0.y + 1.0f);
        ImVec2 b(right - 2.0f, p0.y + rowH - 1.0f);
        w->DrawList->AddRectFilled(a, b, bg, radius);
    }

    // Rewind to the row's indented content origin so chevron/icon/label overdraw.
    ImGui::SetCursorScreenPos(p0);
    (void)kind;
    return r;
}

// A small eye toggle button drawn at the right edge of a row. Flips `visible`.
// Call right after the row's main item (same line). Positioned absolutely at the
// content region's right edge so it works under any tree indent.
void Application::OutlinerEyeButton(bool& visible, const char* id) {
    auto& ds      = DST::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float h = ImGui::GetTextLineHeight();
    // Place at the right edge of the window's content region (window-local X).
    float rightX = ImGui::GetWindowContentRegionMax().x - h;
    ImGui::SameLine(rightX);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ds.GetColor(DST::Tok::C_IconButton_BackgroundHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    bool clk = ImGui::Button("##eyebtn", ImVec2(h, h));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const char* icon = visible ? "eye" : "eye-closed";
    ImVec4 tint = ds.GetColor(visible ? DST::Tok::S_Color_Text_Subtle
                                      : DST::Tok::S_Color_Text_Disabled);
    ImVec2 bmin = ImGui::GetItemRectMin();
    auto md = iconMgr.GetDefaultMetadata(icon);
    if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
    iconMgr.RenderIcon(ImGui::GetWindowDrawList(), icon, bmin, h, md);
    ImGui::PopID();
    if (clk) { visible = !visible; project_.dirty = true; }
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

    ZebraRowBg();

    if (s_renameId == s.id) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", s_renameBuf, sizeof(s_renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
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
        OutlinerActiveDotAt(ImGui::GetCursorScreenPos().y, dotColor);
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
        ImGui::SameLine(0.0f, 2.0f);
    }
    const bool dim = !s.visible;
    ImU32 txt = OutlinerLabelColor(searchHit, dim);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight()) * 0.5f),
        txt, s.name.empty() ? "Object" : s.name.c_str());
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(s.name.empty() ? "Object" : s.name.c_str()).x,
                        ImGui::GetTextLineHeightWithSpacing()));

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
        ImGui::Indent(lh * 0.9f);
        OutlinerMarkRows(s);
        ImGui::Unindent(lh * 0.9f);
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
    ImGui::Indent(lh * 0.9f);
    for (Renderer::Shape* c : kids) OutlinerObjectSubtree(c->id, scopeColl);
    ImGui::Unindent(lh * 0.9f);
}

// Render the indented mark rows under object `s`. Selectable (single / Shift),
// no drag, no rename. Fill layers stay on the object (not shown here).
void Application::OutlinerMarkRows(Renderer::Shape& s) {
    auto& doc = project_.document;
    auto& ds  = DST::DesignSystem::Instance();
    const float lh = ImGui::GetTextLineHeight();
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
            ZebraRowBg();
            const bool sel = doc.IsMarkSelected(ref);
            const bool act = sel && (doc.ActiveMark() == ref);
            RowResult rr = OutlinerRowBegin(rid, /*kind*/0, /*searchHit*/false,
                                            sel ? 1 : 0, act ? 1 : 0);
            if (rr.clicked) {
                if (ImGui::GetIO().KeyShift) doc.MarkSelectToggle(ref);
                else                         doc.MarkSelectOnly(ref);
                doc.SetActive(s.id);
            }
            // Content over the state bg: indent past the chevron gutter, then label.
            OutlinerDotGutter();
            ImGui::Dummy(ImVec2(lh * 0.6f, ImGui::GetTextLineHeightWithSpacing()));  // mark indent
            ImGui::SameLine(0.0f, 4.0f);
            ImU32 txt = ImGui::GetColorU32(ds.GetColor(sel
                ? DesignSystem::Tok::S_Color_Text_Default
                : DesignSystem::Tok::S_Color_Text_Subtle));
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing()
                       - ImGui::GetTextLineHeight()) * 0.5f), txt, kindName);
            ImGui::Dummy(ImVec2(ImGui::CalcTextSize(kindName).x,
                                ImGui::GetTextLineHeightWithSpacing()));
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

    ZebraRowBg();

    // Rename in place (replaces the whole row while active).
    if (s_renameId == (collId | kCollBit)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##crename", s_renameBuf, sizeof(s_renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
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
    OutlinerDotGutter();
    char cid[32]; std::snprintf(cid, sizeof(cid), "##cchev%llu", (unsigned long long)collId);
    OutlinerChevron(cid, open);
    store->SetBool(openKey, open);
    ImGui::SameLine(0.0f, 2.0f);
    ImVec2 sq = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(sq.x, sq.y + lh * 0.2f), ImVec2(sq.x + lh * 0.6f, sq.y + lh * 0.8f),
        CollectionIconColor(*coll), 2.0f);
    ImGui::Dummy(ImVec2(lh * 0.8f, lh));
    ImGui::SameLine(0.0f, 4.0f);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f),
        txt, coll->name.c_str());
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(coll->name.c_str()).x,
                        ImGui::GetTextLineHeightWithSpacing()));

    // Collection eye: hides/reveals the whole subtree.
    {
        bool hidden = doc.CollectionHidden(collId);
        bool vis = !hidden;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##ceye%llu", (unsigned long long)collId);
        OutlinerEyeButton(vis, eid);
        if (vis == hidden) doc.SetCollectionVisible(collId, vis);   // button flipped it
    }
    if (open) {
        ImGui::Indent(lh);   // deeper child indent → clearer hierarchy
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
        ImGui::Unindent(lh);   // must match the Indent(lh) above (was 0.5f → leak)
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

    ZebraRowBg();

    if (s_renameId == (ab.id | kPageBit)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##prename", s_renameBuf, sizeof(s_renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
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
    OutlinerDotGutter();
    char cid[32]; std::snprintf(cid, sizeof(cid), "##pchev%llu", (unsigned long long)ab.id);
    OutlinerChevron(cid, open);
    store->SetBool(openKey, open);
    ImGui::SameLine(0.0f, 2.0f);
    char label[96];
    std::snprintf(label, sizeof(label), "%s  (%.0f x %.0f)",
                  ab.name.c_str(), ab.size.x, ab.size.y);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f), txt, label);
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(label).x, ImGui::GetTextLineHeightWithSpacing()));

    // Page eye: hide/show the whole page.
    {
        bool vis = ab.pageVisible;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##peye%llu", (unsigned long long)ab.id);
        OutlinerEyeButton(vis, eid);
        if (vis != ab.pageVisible) { ab.pageVisible = vis; project_.dirty = true; }
    }
    if (open) {
        ImGui::Indent(lh);                 // deeper child indent → clearer hierarchy
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
        ImGui::Unindent(lh);
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

    ZebraRowBg();

    if (s_renameId == (ab.id | kPageBit)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##prenameL", s_renameBuf, sizeof(s_renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
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
    ImGui::SameLine(0.0f, 2.0f);
    char label[96];
    std::snprintf(label, sizeof(label), "%s  (%.0f x %.0f)",
                  ab.name.c_str(), ab.size.x, ab.size.y);
    ImU32 txt = OutlinerLabelColor(searchHit, /*dim=*/false);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(tp.x, tp.y + (ImGui::GetTextLineHeightWithSpacing() - lh) * 0.5f), txt, label);
    ImGui::Dummy(ImVec2(ImGui::CalcTextSize(label).x, ImGui::GetTextLineHeightWithSpacing()));

    {
        bool vis = ab.pageVisible;
        char eid[32]; std::snprintf(eid, sizeof(eid), "##pleye%llu", (unsigned long long)ab.id);
        OutlinerEyeButton(vis, eid);
        if (vis != ab.pageVisible) { ab.pageVisible = vis; project_.dirty = true; }
    }
    if (open) {
        ImGui::Indent(lh);
        // Every object on the page, in REVERSE draw order (top of the z-stack at
        // the top of the list). No collection grouping, no parenting nesting — a
        // flat layer list. Parented children still show via the object row's own
        // mark/child handling, but here we list each shape once at top level.
        for (size_t i = ab.shapes.size(); i-- > 0; )
            OutlinerObjectRow(ab.shapes[i]);
        ImGui::Unindent(lh);
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

    s_zebraRow = 0;   // reset stripe parity at the top of the tree
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
    ZebraRowBg();
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID rootKey = ImGui::GetID("##prjroot");
    bool rootOpen = store->GetBool(rootKey, true);
    OutlinerDotGutter();
    char rcid[16]; std::snprintf(rcid, sizeof(rcid), "##prjchev");
    OutlinerChevron(rcid, rootOpen);
    store->SetBool(rootKey, rootOpen);
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextUnformatted(title.c_str());
    OutlinerNodeDropInto(Renderer::kProjectRootId);  // drop a node/object onto root
    if (rootOpen && root) {
        ImGui::Indent(ImGui::GetTextLineHeight() * 0.5f);
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
        ImGui::Unindent(ImGui::GetTextLineHeight() * 0.5f);
    }

    // Continue the zebra stripes to the BOTTOM of the editor even past the last
    // row, so the alternation never stops mid-panel.
    {
        const float h = ImGui::GetTextLineHeightWithSpacing();
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        float y = ImGui::GetCursorScreenPos().y;
        const float bottom = w->WorkRect.Max.y;
        ImU32 stripe = ImGui::ColorConvertFloat4ToU32(
            ds.GetColor(DST::Tok::S_Color_Background_Layer2));
        while (y < bottom) {
            if (s_zebraRow & 1)
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(w->WorkRect.Min.x, y),
                    ImVec2(w->WorkRect.Max.x + ImGui::GetStyle().ScrollbarSize,
                           std::min(y + h, bottom)),
                    stripe);
            y += h;
            ++s_zebraRow;
        }
    }

    // Clicks on the EMPTY background: LMB clears the outliner selection (keeps the
    // document active object), RMB opens the Add Collection menu.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered()) {
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
            ImGui::Checkbox("Objects",     &outlinerCur_->showObjects);
            ImGui::Checkbox("Pages",       &outlinerCur_->showPages);
            ImGui::Checkbox("Collections", &outlinerCur_->showCollections);
            ImGui::Separator();
            ImGui::Checkbox("Meshes", &outlinerCur_->showMeshes);
            ImGui::Checkbox("Curves", &outlinerCur_->showCurves);
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

// ── Outliner context menus (object / collection / background) ─────────────────
void Application::RenderOutlinerContextMenus() {
    auto& doc = project_.document;

    if (outlinerCtxOpen_) {
        const char* pid =
            outlinerCtxKind_ == OutlinerCtxKind::Object     ? "##outObjCtx"
          : outlinerCtxKind_ == OutlinerCtxKind::Collection ? "##outCollCtx"
                                                            : "##outBgCtx";
        ImGui::OpenPopup(pid);
        outlinerCtxOpen_ = false;
    }

    // ── Object menu ──────────────────────────────────────────────────────────
    {
        std::vector<UI::MenuEntry> entries;
        const uint64_t id = outlinerCtxId_;
        const bool sel = doc.IsSelected(id);
        const bool canPaste = !outlinerClipboard_.empty();
        { UI::MenuEntry e; e.label = "Select";   e.enabled = !sel;
          e.tooltip = "Make this the active selection";
          e.onClick = [this, id]{ project_.document.SelectOnly(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect"; e.enabled = sel;
          e.tooltip = "Remove this object from the selection";
          e.onClick = [this, id]{ project_.document.Deselect(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Copy"; e.tooltip = "Copy the object to the clipboard";
          e.onClick = [this, id]{ Action_OutlinerCopy(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Paste"; e.enabled = canPaste;
          e.tooltip = "Paste the clipboard object(s)";
          e.onClick = [this]{ Action_OutlinerPaste(); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate";
          e.tooltip = "Make an independent copy, nudged slightly";
          e.onClick = [this, id]{ Action_OutlinerDuplicate(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.icon = "ink-eraser";
          e.tooltip = "Delete this object";
          e.onClick = [this, id]{ MarkUndoLabel("Delete"); project_.document.EraseShape(id);
                                  project_.dirty = true; }; entries.push_back(std::move(e)); }
        UI::ContextMenu("##outObjCtx", outlinerCtxPos_, entries, "Object");
    }

    // ── Collection menu ──────────────────────────────────────────────────────
    {
        std::vector<UI::MenuEntry> entries;
        const uint64_t cid = outlinerCtxId_;
        { UI::MenuEntry e; e.label = "Add Collection";
          e.tooltip = "Add a nested collection inside this one";
          e.onClick = [this, cid]{ project_.document.AddCollection("Collection", cid);
                                   project_.dirty = true; }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Rename";
          e.tooltip = "Rename this collection";
          e.onClick = [this, cid]{ s_renameId = (cid | kCollBit);
              if (Renderer::Collection* c = project_.document.FindCollection(cid))
                  std::snprintf(s_renameBuf, sizeof(s_renameBuf), "%s", c->name.c_str()); };
          entries.push_back(std::move(e)); }
        // Icon Colour ▸ palette swatches + default + custom.
        { UI::MenuEntry colour; colour.label = "Icon Colour";
          colour.tooltip = "Set this collection's icon tint";
          { UI::MenuEntry e; e.label = "Default";
            e.onClick = [this, cid]{ if (auto* c = project_.document.FindCollection(cid)) {
                c->colorIndex = 0; project_.dirty = true; } };
            colour.submenu.push_back(std::move(e)); }
          for (int h = 0; h < kNumCollHues; ++h) {
              UI::MenuEntry e; e.label = kCollHues[(size_t)h].name;
              int idx = h + 1;
              e.onClick = [this, cid, idx]{ if (auto* c = project_.document.FindCollection(cid)) {
                  c->colorIndex = idx; project_.dirty = true; } };
              colour.submenu.push_back(std::move(e));
          }
          { UI::MenuEntry e; e.label = "Custom...";
            e.tooltip = "Pick a custom colour";
            e.onClick = [this, cid]{ if (auto* c = project_.document.FindCollection(cid)) {
                c->colorIndex = -1; outlinerColorPickColl_ = cid;
                outlinerColorPickOpen_ = true;   // one-shot open request
                project_.dirty = true; } };
            colour.submenu.push_back(std::move(e)); }
          entries.push_back(std::move(colour)); }
        { UI::MenuEntry e; e.label = "Delete";
          e.tooltip = "Delete the collection; its objects move to the parent";
          e.onClick = [this, cid]{ MarkUndoLabel("Delete collection");
              project_.document.EraseCollection(cid, /*deleteContents=*/false);
              project_.dirty = true; }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete Hierarchy"; e.icon = "ink-eraser";
          e.tooltip = "Delete the collection AND every nested collection and object";
          e.onClick = [this, cid]{ MarkUndoLabel("Delete hierarchy");
              project_.document.EraseCollection(cid, /*deleteContents=*/true);
              project_.dirty = true; }; entries.push_back(std::move(e)); }
        UI::ContextMenu("##outCollCtx", outlinerCtxPos_, entries, "Collection");
    }

    // ── Background menu ──────────────────────────────────────────────────────
    {
        std::vector<UI::MenuEntry> entries;
        { UI::MenuEntry e; e.label = "Add Collection";
          e.tooltip = "Add a new top-level collection";
          e.onClick = [this]{ project_.document.AddCollection("Collection", Renderer::kProjectRootId);
                              project_.dirty = true; }; entries.push_back(std::move(e)); }
        UI::ContextMenu("##outBgCtx", outlinerCtxPos_, entries, "Outliner");
    }

    // Custom-colour picker popup for a collection (opened by "Custom...").
    // Open EXACTLY ONCE on the request edge (so it doesn't re-open / follow the
    // mouse every frame); ImGui then owns it and closes it on Esc / click-away.
    if (outlinerColorPickOpen_) {
        ImGui::OpenPopup("##collColorPick");
        outlinerColorPickOpen_ = false;
    }
    if (ImGui::BeginPopup("##collColorPick")) {
        Renderer::Collection* c = doc.FindCollection(outlinerColorPickColl_);
        if (c) {
            float col[4] = { c->customColor.r, c->customColor.g,
                             c->customColor.b, c->customColor.a };
            ImGui::TextUnformatted("Collection colour");
            if (ImGui::ColorPicker4("##cc", col,
                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
                c->customColor = { col[0], col[1], col[2], col[3] };
                c->colorIndex  = -1;
                project_.dirty = true;
            }
        }
        ImGui::EndPopup();
    } else {
        outlinerColorPickColl_ = 0;   // popup closed → forget the target
    }
}

// ── Clipboard actions ─────────────────────────────────────────────────────────
void Application::Action_OutlinerCopy(uint64_t shapeId) {
    Renderer::Shape* s = project_.document.FindShape(shapeId);
    if (!s) return;
    outlinerClipboard_.clear();
    outlinerClipboard_.push_back(*s);          // deep copy
}

void Application::Action_OutlinerPaste() {
    if (outlinerClipboard_.empty()) return;
    auto& doc = project_.document;
    int ab = doc.artboards.empty() ? -1 : 0;
    if (doc.ActiveShape()) {                    // paste onto the active object's page
        int a = doc.ArtboardOfShape(doc.ActiveId());
        if (a >= 0) ab = a;
    }
    if (ab < 0) return;
    MarkUndoLabel("Paste");
    uint64_t last = 0;
    for (const Renderer::Shape& src : outlinerClipboard_) {
        Renderer::Shape copy = src;
        copy.name = src.name.empty() ? "Object copy" : src.name + " copy";
        copy.transform.translate.x += 12.0f;
        copy.transform.translate.y += 12.0f;
        last = doc.AddShape(ab, std::move(copy));   // AddShape assigns id + selects
    }
    if (last) doc.SelectOnly(last);
    project_.dirty = true;
}

void Application::Action_OutlinerDuplicate(uint64_t shapeId) {
    MarkUndoLabel("Duplicate");
    uint64_t nid = project_.document.DuplicateShape(shapeId);
    if (nid) project_.document.SelectOnly(nid);
    project_.dirty = true;
}

} // namespace App

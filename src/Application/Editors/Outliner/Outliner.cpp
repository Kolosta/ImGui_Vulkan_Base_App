#include "Application.h"

#include "OutlinerRowLayout.h"
#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner editor — the Ink document's organisation tree, rebuilt to restore
//  the legacy design and feature set (docs/Ink/ROADMAP.md Lot 9 rework):
//  ListRow-based rows with the chevron / active-dot / icon / name / eye slots,
//  zebra striping, tree guide lines, the two views (Collections and Layers),
//  the click / Ctrl / Shift-range selection synced to the shared EditContext,
//  inline rename, search (matched rows go green), the kind/state/invert filter
//  and the viewport-sync button. Every edit is an undoable typed op.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

const char* NodeIcon(const Ink::Node& n) {
    switch (n.kind) {
        case Ink::NodeKind::Group:    return "folder";
        case Ink::NodeKind::Instance: return "swap_horiz";
        default:                      return "shape-category";
    }
}
} // namespace

// ── Filters ───────────────────────────────────────────────────────────────────

bool Application::OutlinerSearchHit(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_ || outlinerCur_->search[0] == '\0') return false;
    const Ink::Node* n = project_.document->Find(id);
    return n && ol::ContainsCI(n->name, outlinerCur_->search);
}

bool Application::OutlinerPassesFilter(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_) return true;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return false;
    OutlinerState& o = *outlinerCur_;

    // Kind toggles.
    bool ok = true;
    if (n->kind == Ink::NodeKind::Group) { if (!o.showGroups) ok = false; }
    else                                  { if (!o.showObjects) ok = false; }

    // Object-state filter.
    if (ok) switch (o.objState) {
        case ObjStateFilter::Visible:    ok = n->visible; break;
        case ObjStateFilter::Selected:   ok = edit_.IsSelected(id); break;
        case ObjStateFilter::Active:     ok = (edit_.active == id); break;
        case ObjStateFilter::Selectable: ok = !n->locked; break;
        case ObjStateFilter::All:        default: break;
    }
    return o.invertFilter ? !ok : ok;
}

bool Application::OutlinerRowSelected(Ink::NodeId id) const {
    if (edit_.IsSelected(id)) return true;
    return outlinerCur_ && outlinerCur_->RowSelected(id);
}

// ── Selection click (legacy semantics: plain / Shift-range / Ctrl / Alt) ──────

void Application::OutlinerSelectClick(Ink::NodeId id, bool isObject) {
    (void)isObject;
    ImGuiIO& io = ImGui::GetIO();
    OutlinerState& o = *outlinerCur_;

    if (io.KeyShift && o.active != 0) {
        // Range from active to clicked in the current draw order.
        const auto& order = o.rowOrder;
        int ia = -1, ib = -1;
        for (int i = 0; i < (int)order.size(); ++i) {
            if (order[i] == o.active) ia = i;
            if (order[i] == id)       ib = i;
        }
        if (ia >= 0 && ib >= 0) {
            if (ia > ib) std::swap(ia, ib);
            edit_.Clear();
            for (int i = ia; i <= ib; ++i)
                if (project_.document->Find(order[i])) edit_.SelectAdd(order[i]);
            edit_.active = o.active;   // active stays put (Blender-style)
            return;
        }
        // fall through to single-select if an endpoint vanished
    }
    if (io.KeyCtrl) {
        if (edit_.IsSelected(id)) edit_.Deselect(id); else edit_.SelectAdd(id);
        o.active = id;
        return;
    }
    if (io.KeyAlt) {
        edit_.SelectAdd(id);
        edit_.active = edit_.active;   // add WITHOUT changing active
        o.active = id;
        return;
    }
    edit_.SelectOnly(id);
    o.active = id;
}

// ── One object/group row + its parented-child subtree ─────────────────────────

void Application::OutlinerObjectRow(Ink::NodeId id, int depth) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    OutlinerState& o = *outlinerCur_;

    // Search: show a node when it or a descendant matches.
    const bool searching = o.search[0] != '\0';
    std::function<bool(const Ink::Node&)> subtreeHit = [&](const Ink::Node& node) {
        if (ol::ContainsCI(node.name, o.search)) return true;
        for (Ink::NodeId c : node.children)
            if (const Ink::Node* ch = doc.Find(c)) if (subtreeHit(*ch)) return true;
        return false;
    };
    const bool passes = OutlinerPassesFilter(id);
    const bool searchOk = !searching || subtreeHit(*n);
    // A group that fails the kind filter still recurses so objects stay reachable.
    const bool drawSelf = passes && searchOk;

    auto& ds = DS::DesignSystem::Instance();
    const bool selfHit = OutlinerSearchHit(id);
    const bool hasKids = !n->children.empty();
    const bool collapsed = o.IsCollapsed(id);

    float rowContentX = 0.0f, rowStripeTop = 0.0f, rowStripeBot = 0.0f;

    if (drawSelf) {
        o.rowOrder.push_back(id);

        // ── ListRow chrome ──
        const bool selected = OutlinerRowSelected(id);
        const bool active   = (edit_.active == id);
        UI::ListRowConfig cfg;
        cfg.id = ImGui::GetID((void*)(uintptr_t)id);
        cfg.zebraOdd = (UI::ListRowZebraIndex() & 1);
        cfg.zebraColor = ImGui::ColorConvertFloat4ToU32(
            ol::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
        cfg.selected = selected; cfg.active = active;
        cfg.bandMarginLeft = ol::BandMargin();
        cfg.cornerRadius = ol::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * ol::Gs();
        auto col = [&](Tok normal, Tok search, float a) {
            ImVec4 c = ol::SafeColor(selfHit && searching ? search : normal, ImVec4(0.3f,0.5f,0.9f,1));
            c.w = a; return ImGui::ColorConvertFloat4ToU32(c);
        };
        cfg.colors.hover         = col(Tok::C_Outliner_Row_Hover, Tok::C_Outliner_Search_Hover, 0.55f);
        cfg.colors.selected      = col(Tok::C_Outliner_Row_Selected, Tok::C_Outliner_Search_Selected, 1.0f);
        cfg.colors.selectedHover = col(Tok::C_Outliner_Row_SelectedHover, Tok::C_Outliner_Search_SelectedHover, 1.0f);
        cfg.colors.active        = col(Tok::C_Outliner_Row_Active, Tok::C_Outliner_Search_Active, 1.0f);
        cfg.colors.activeHover   = col(Tok::C_Outliner_Row_ActiveHover, Tok::C_Outliner_Search_ActiveHover, 1.0f);
        if (selfHit && searching && !selected)
            cfg.colors.idle = col(Tok::C_Outliner_Search_Visual, Tok::C_Outliner_Search_Visual, 0.45f);

        UI::ListRow row(cfg);
        rowStripeTop = row.StripeTop();
        rowStripeBot = row.StripeBottom();
        UI::ListRowAdvanceZebra();

        // Content, laid out from the band's content origin.
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        rowContentX = row.ContentX();
        ImGui::PushID((int)id);

        ol::DotGutter();
        // Indentation: one chevron slot per depth.
        for (int d = 0; d < depth; ++d) ol::ChevronSpacer();
        if (hasKids) {
            bool open = !collapsed;
            ol::Chevron("##ch", open);
            if (open == collapsed) o.ToggleCollapsed(id);   // toggled this frame
        } else {
            ol::ChevronSpacer();
        }
        // Icon (or a folder for a group).
        ol::SlotIcon(NodeIcon(*n), ds.GetColor(Tok::S_Color_Text_Default));

        // Name or the inline rename field.
        const float nameX = ImGui::GetCursorScreenPos().x;
        const float eyeSlot = ol::RowH();
        const float eyeX = row.BandRight() - 6.0f * ol::Gs() - eyeSlot;
        if (o.renaming == id) {
            ImGui::SetCursorScreenPos(ImVec2(nameX, row.RowTop() +
                (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::SetNextItemWidth(std::max(40.0f, eyeX - nameX - 4.0f));
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", o.renameBuf, sizeof o.renameBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
                Action_RenameNode(id, o.renameBuf);
                o.renaming = 0;
            }
            if (ImGui::IsItemDeactivated()) o.renaming = 0;
        } else {
            const bool dim = !n->visible;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(nameX, row.RowTop() + (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
                ol::LabelColor(selfHit && searching, dim),
                n->name.empty() ? "(unnamed)" : n->name.c_str());
        }

        // Active dot (drawn inside the band) for the active object.
        if (active) {
            ImVec4 dc = ol::SafeColor(Tok::S_State_Active_OnPage, ImVec4(0.95f,0.6f,0.2f,1));
            ol::ActiveDotAt(row.BandLeft(), row.RowTop(), ImGui::ColorConvertFloat4ToU32(dc));
        }

        // Eye toggle on the right (suppress the row's own click there).
        row.SuppressInputIn(eyeX, eyeX + eyeSlot);
        {
            auto& im = VectorGraphics::IconManager::Instance();
            const char* icon = n->visible ? "eye" : "eye-closed";
            const float isz = ol::IconSize();
            const ImVec2 ep(eyeX + (eyeSlot - isz) * 0.5f, row.RowTop() + (ol::RowH() - isz) * 0.5f);
            const ImRect er(ImVec2(eyeX, row.RowTop()), ImVec2(eyeX + eyeSlot, row.RowTop() + ol::RowH()));
            const bool ehov = er.Contains(ImGui::GetIO().MousePos);
            ImVec4 tint = ol::SafeColor(n->visible ? Tok::S_Color_Text_Subtle
                                                   : Tok::S_Color_Text_Disabled, ImVec4(0.6f,0.6f,0.6f,1));
            if (ehov) tint = ds.GetColor(Tok::S_Color_Accent_Default);
            auto md = im.GetDefaultMetadata(icon);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            im.RenderIcon(ImGui::GetWindowDrawList(), icon, ep, isz, md);
            if (ehov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                Action_ToggleNodeVisible(id);
        }

        ImGui::PopID();

        // Row input (selection / rename / context).
        const UI::ListRowInput& in = row.Input();
        if (in.doubleClicked) {
            o.renaming = id;
            std::snprintf(o.renameBuf, sizeof o.renameBuf, "%s", n->name.c_str());
        } else if (in.clicked) {
            OutlinerSelectClick(id, n->kind != Ink::NodeKind::Group);
        }
        if (in.rightClicked) {
            if (!OutlinerRowSelected(id)) OutlinerSelectClick(id, true);
            outlinerCtxOpen_ = true;
            outlinerCtxPos_  = ImGui::GetIO().MousePos;
            outlinerCtxNode_ = id;
        }
        // Scroll-to-active.
        if (active && o.reqScrollToActive) ImGui::SetScrollHereY(0.5f);
    }

    // Children (parented subtree): top-of-stack first in Layers, order in
    // Collections — we use painter order reversed for a stack-like read.
    if (hasKids && !collapsed) {
        const int childDepth = drawSelf ? depth + 1 : depth;
        float firstTop = 0.0f, lastBot = 0.0f; bool any = false;
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
            const std::size_t before = o.rowOrder.size();
            OutlinerObjectRow(*it, childDepth);
            if (o.rowOrder.size() > before) any = true;
        }
        // Dotted guide line for the parented subtree (best-effort geometry:
        // spans from just under this row to the current cursor).
        if (drawSelf && any) {
            const float gx = ol::GuideX(rowContentX);
            firstTop = rowStripeBot;
            lastBot  = ImGui::GetCursorScreenPos().y;
            ol::TreeLine(gx, firstTop, lastBot,
                ImGui::ColorConvertFloat4ToU32(
                    ol::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f,0.6f,0.6f,1))), true);
        }
    }
}

// ── Layers view: each page → its layer tree, top-of-stack first ───────────────

void Application::OutlinerPageLayersNode(const Ink::Page& page) {
    auto& ds = DS::DesignSystem::Instance();
    OutlinerState& o = *outlinerCur_;
    const uint64_t pageKey = page.id;
    const bool collapsed = o.IsCollapsed(pageKey);

    // Page header row (no ListRow selection band — a plain header).
    UI::ListRowConfig cfg;
    cfg.id = ImGui::GetID((void*)(uintptr_t)pageKey);
    cfg.zebraOdd = (UI::ListRowZebraIndex() & 1);
    cfg.zebraColor = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
    cfg.bandMarginLeft = ol::BandMargin();
    UI::ListRow row(cfg);
    UI::ListRowAdvanceZebra();
    ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
    ImGui::PushID((int)pageKey);
    ol::DotGutter();
    bool open = !collapsed;
    ol::Chevron("##pch", open);
    if (open == collapsed) o.ToggleCollapsed(pageKey);
    ol::SlotIcon("folder", ds.GetColor(Tok::S_Color_Text_Subtle));
    char label[160];
    std::snprintf(label, sizeof label, "%s  (%dx%d)",
                  page.name.empty() ? "Page" : page.name.c_str(),
                  (int)page.size.x, (int)page.size.y);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(ImGui::GetCursorScreenPos().x,
               row.RowTop() + (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
        ol::LabelColor(false, false), label);
    ImGui::PopID();

    if (!collapsed)
        for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
            OutlinerObjectRow(*it, 1);
}

void Application::OutlinerLayersView(EditorState& st) {
    (void)st;
    for (const Ink::Page& page : project_.document->Pages())
        if (outlinerCur_->showPages) OutlinerPageLayersNode(page);
        else for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
            OutlinerObjectRow(*it, 0);
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderOutliner(EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();
    outlinerCur_ = &st.outliner;
    if (!project_.document) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No document");
        ImGui::PopStyleColor();
        return;
    }
    edit_.Prune(*project_.document);
    st.outliner.rowOrder.clear();
    // Mirror the shared active object so a Shift-range click anchors on the
    // viewport/Outliner-agreed active row.
    if (edit_.active != Ink::kNullNode) st.outliner.active = edit_.active;

    // Viewport-sync upkeep (Lot 9). Drop a target that is no longer a live
    // Viewport leaf; drive the "pick a viewport" phase (cancel on Esc / RMB;
    // a follow-mouse prompt is shown while armed — the accent wash + click
    // confirmation live in RenderViewport).
    if (st.outliner.syncTarget &&
        !zoneLayout_.IsLiveEditorState(st.outliner.syncTarget, CoreEditor::Viewport))
        st.outliner.syncTarget = nullptr;
    if (st.outliner.syncPicking) {
        const bool noViewport = zoneLayout_.CountEditors(CoreEditor::Viewport) == 0;
        if (noViewport || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            st.outliner.syncPicking = false;
            if (outlinerPickingState_ == &st.outliner) outlinerPickingState_ = nullptr;
        } else {
            UI::DrawTooltipTranslucent("Select a viewport to synchronise with",
                                       ImGui::GetIO().MousePos,
                                       ds.GetFloat(Tok::S_Opacity_Moderate));
        }
    }

    if (UI::BeginScroll("##outlinerScroll", ImVec2(0, 0))) {
        UI::ListRowResetZebra();
        if (st.outliner.display == OutlinerDisplayMode::Collections)
            OutlinerCollectionsView(st);
        else
            OutlinerLayersView(st);

        // Empty-space clicks.
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) edit_.Clear();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                outlinerCtxOpen_ = true;
                outlinerCtxPos_  = ImGui::GetIO().MousePos;
                outlinerCtxNode_ = Ink::kNullNode;
            }
        }
    }
    UI::EndScroll();
    st.outliner.reqScrollToActive = false;   // consumed (even if the row wasn't drawn)

    RenderOutlinerContextMenu(st);
}

} // namespace App

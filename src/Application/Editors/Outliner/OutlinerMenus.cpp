#include "Application.h"

#include "OutlinerRowLayout.h"
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/PopupMenu.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/ListRow.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner top bar (display / search / viewport-sync / Filter), the
//  Collections view and the context menu + organisation commands — the legacy
//  design restored on the Ink model (docs/Ink/ROADMAP.md Lot 9 rework). Every
//  command is undoable through the shared DocUndoStack.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ── Top bar ───────────────────────────────────────────────────────────────────

void Application::BuildOutlinerTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float h  = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    OutlinerState* os = &st.outliner;
    outlinerCur_ = os;

    // LEFT — display dropdown.
    bar.left.width = 110.0f * gs;
    bar.left.draw = [this, os](ImVec2 pos, float) {
        outlinerCur_ = os;
        ImGui::SetCursorPos(pos);
        static const char* kModes[] = { "Collections", "Layers" };
        UI::DropdownConfig cfg; cfg.id = "##outDisplay";
        cfg.triggerLabel = kModes[(int)os->display];
        for (const char* m : kModes) { UI::DropdownItem it; it.label = m; cfg.items.push_back(it); }
        cfg.selectedIndex = (int)os->display;
        UI::DropdownResult r = UI::Dropdown(cfg);
        if (r.changed) os->display = (OutlinerDisplayMode)r.selected;
    };

    // MIDDLE — search.
    bar.middle.width = 200.0f * gs;
    bar.middle.draw = [this, os, gs, h](ImVec2 pos, float) {
        outlinerCur_ = os;
        ImGui::SetCursorPos(pos);
        const float padY = std::max(0.0f, (h - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, padY));
        ImGui::SetNextItemWidth(200.0f * gs);
        ImGui::InputTextWithHint("##outSearch", "Search…", os->search, sizeof os->search);
        ImGui::PopStyleVar();
    };

    // RIGHT — viewport-sync button + Filter dropdown.
    const float syncW = h;
    const float grpGap = 6.0f * gs;
    bar.right.width = syncW + grpGap + 100.0f * gs;
    bar.right.draw = [this, os, gs, h, syncW, grpGap](ImVec2 pos, float) {
        outlinerCur_ = os;
        auto& ds2 = DS::DesignSystem::Instance();
        auto& iconMgr = VectorGraphics::IconManager::Instance();

        // Sync-with-viewport button.
        const bool hasViewport = zoneLayout_.CountEditors(CoreEditor::Viewport) > 0;
        const bool syncOn = os->syncTarget != nullptr || os->syncPicking;
        ImGui::SetCursorPos(pos);
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(!hasViewport);
        const bool sclk = ImGui::InvisibleButton("##outSyncBtn", ImVec2(syncW, h));
        const bool shov = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec4 sbg = ds2.GetColor(syncOn ? Tok::S_Color_Accent_Default
                                    : shov   ? Tok::C_IconButton_BackgroundHover
                                             : Tok::C_IconButton_Background);
            if (!hasViewport) sbg.w *= ds2.GetFloat(Tok::S_Opacity_Disabled);
            const float rad = ds2.GetFloat(Tok::C_Dropdown_CornerRadius) * gs;
            dl->AddRectFilled(sp, ImVec2(sp.x + syncW, sp.y + h),
                              ImGui::ColorConvertFloat4ToU32(sbg), rad);
            const float isz = ds2.GetFloat(Tok::C_Dropdown_IconSize) * gs;
            ImVec4 tn = ds2.GetColor(Tok::S_Color_Text_Default);
            if (!hasViewport) tn.w *= ds2.GetFloat(Tok::S_Opacity_Disabled);
            auto md = iconMgr.GetDefaultMetadata("arrow-warm-up");
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tn;
            iconMgr.RenderIcon(dl, "arrow-warm-up",
                               ImVec2(sp.x + (syncW-isz)*0.5f, sp.y + (h-isz)*0.5f), isz, md);
        }
        if (sclk && hasViewport) {
            if (syncOn) {
                os->syncTarget = nullptr; os->syncPicking = false;
                if (outlinerPickingState_ == os) outlinerPickingState_ = nullptr;
            } else {
                os->syncPicking = true; outlinerPickingState_ = os;
            }
        }
        if (shov && hasViewport && !syncOn &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip("Synchronise the Outliner with a viewport", ImGui::GetIO().MousePos);

        // Filter dropdown.
        ImGui::SetCursorPos(ImVec2(pos.x + syncW + grpGap, pos.y));
        const bool anyFilter = !os->showObjects || !os->showPages || !os->showCollections ||
                               !os->showGroups || os->objState != ObjStateFilter::All ||
                               os->invertFilter;
        UI::DropdownConfig fc;
        fc.id = "##outFilter";
        fc.triggerIcon = anyFilter ? "filter" : "filter_off";
        fc.triggerLabel = "Filter";
        fc.menuSize = ImVec2(210.0f * gs, 210.0f * gs);
        fc.bodyDraw = [this, os, &ds2, gs, &iconMgr, h]() {
            ImGui::TextDisabled("Show");
            UI::Checkbox("##fObjects",     "Objects",     &os->showObjects);
            UI::Checkbox("##fGroups",      "Groups",      &os->showGroups);
            UI::Checkbox("##fPages",       "Pages",       &os->showPages);
            UI::Checkbox("##fCollections", "Collections", &os->showCollections);
            ImGui::Separator();
            static const char* kStates[] = { "All", "Visible", "Selected", "Active", "Selectable" };
            UI::DropdownConfig sc; sc.id = "##outState";
            sc.triggerLabel = std::string("State: ") + kStates[(int)os->objState];
            for (int i = 0; i < 5; ++i) { UI::DropdownItem it; it.label = kStates[i]; sc.items.push_back(it); }
            sc.selectedIndex = (int)os->objState;
            UI::DropdownResult sr = UI::Dropdown(sc);
            if (sr.changed && sr.selected >= 0 && sr.selected < 5)
                os->objState = (ObjStateFilter)sr.selected;
            ImGui::SameLine(0.0f, 4.0f * gs);
            ImGui::PushStyleColor(ImGuiCol_Button, os->invertFilter
                ? ds2.GetColor(Tok::S_Color_Accent_Default)
                : ds2.GetColor(Tok::C_IconButton_Background));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ds2.GetColor(Tok::C_IconButton_BackgroundHover));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button("##invFilter", ImVec2(h, h))) os->invertFilter = !os->invertFilter;
            ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
            { const ImVec2 bm = ImGui::GetItemRectMin();
              const float isz = ds2.GetFloat(Tok::C_Dropdown_IconSize) * gs;
              auto md = iconMgr.GetDefaultMetadata("swap_horiz");
              if (!md.colorZones.empty()) md.colorZones[0].customColor = ds2.GetColor(Tok::S_Color_Text_Default);
              iconMgr.RenderIcon(ImGui::GetWindowDrawList(), "swap_horiz",
                                 ImVec2(bm.x + (h-isz)*0.5f, bm.y + (h-isz)*0.5f), isz, md); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                UI::DrawTooltip("Invert the filter (show the complement)", ImGui::GetIO().MousePos);
        };
        UI::Dropdown(fc);
    };
}

// (The Collections/Layers views and the per-row builders live in Outliner.cpp,
//  which flattens the tree and draws only the visible window — the two-pass
//  culled renderer. OutlinerMenus.cpp keeps the top bar, context menu and the
//  organisation commands.)

// ── Context menu ──────────────────────────────────────────────────────────────

// The legacy collection hue palette (primitive tokens), in the legacy order.
namespace {
struct CollHue { const char* name; Tok token; };
constexpr CollHue kCollHues[] = {
    { "Cyan",      Tok::P_Color_Cyan_500 },      { "Indigo",    Tok::P_Color_Indigo_500 },
    { "Cinnamon",  Tok::P_Color_Cinnamon_500 },  { "Green",     Tok::P_Color_Green_500 },
    { "Yellow",    Tok::P_Color_Yellow_500 },    { "Orange",    Tok::P_Color_Orange_500 },
    { "Red",       Tok::P_Color_Red_500 },       { "Magenta",   Tok::P_Color_Magenta_500 },
    { "Purple",    Tok::P_Color_Purple_500 },    { "Turquoise", Tok::P_Color_Turquoise_500 },
};
Ink::Color TokenToInk(Tok t, const ImVec4& fallback) {
    ImVec4 c = fallback;
    try { c = DS::DesignSystem::Instance().GetColor(t); } catch (...) {}
    return { c.x, c.y, c.z, c.w };
}
} // namespace

void Application::RenderOutlinerContextMenu(EditorState& st) {
    if (!outlinerCtxOpen_) return;   // OpenPopup happened ONCE at the click site
    Ink::Document& doc = *project_.document;

    std::vector<UI::MenuEntry> entries;
    const bool hasSel = !edit_.selection.empty();
    const bool collectionsMode =
        st.outliner.display == OutlinerDisplayMode::Collections;
    const Ink::Collection* ctxColl = doc.FindCollection(outlinerCtxNode_);
    const bool onObject = !ctxColl && outlinerCtxNode_ != Ink::kNullNode &&
                          doc.Find(outlinerCtxNode_);
    // Menu actions act on the SELECTION when the clicked row is part of it,
    // else on the clicked row alone (legacy rule).
    auto targetIds = [&]() -> std::vector<Ink::NodeId> {
        if (onObject && !edit_.IsSelected(outlinerCtxNode_)) return { outlinerCtxNode_ };
        std::vector<Ink::NodeId> ids;
        for (Ink::NodeId id : edit_.selection) if (doc.Find(id)) ids.push_back(id);
        return ids;
    };
    const char* title = ctxColl ? "Collection" : (onObject ? "Object" : "Outliner");

    if (ctxColl) {
        // ── Collection menu (Collections view) ──
        const Ink::NodeId col = outlinerCtxNode_;
        { UI::MenuEntry e; e.label = "Add Collection";
          e.tooltip = "Create a new collection nested inside this one";
          e.onClick = [this, col]() {
              project_.document->AddCollection("Collection", col);
              LogInfoAction("Add Collection");
          };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Rename";
          e.onClick = [this, col, &st]() {
              if (const Ink::Collection* c = project_.document->FindCollection(col)) {
                  st.outliner.renaming = col; st.outliner.renameTakeFocus = true;
                  std::snprintf(st.outliner.renameBuf, sizeof st.outliner.renameBuf,
                                "%s", c->name.c_str());
              }
          };
          entries.push_back(std::move(e)); }
        // Icon Colour ▸ — Default, the 10 legacy hues, then Custom…
        { UI::MenuEntry cc; cc.label = "Icon Colour";
          { UI::MenuEntry e; e.label = "Default";
            e.onClick = [this, col]() {
                project_.document->SetCollectionColor(col,
                    TokenToInk(Tok::S_Color_Text_Subtle, ImVec4(0.55f,0.60f,0.70f,1)));
            };
            cc.submenu.push_back(std::move(e)); }
          for (const CollHue& h : kCollHues) {
              UI::MenuEntry e; e.label = h.name;
              const Tok tok = h.token;
              e.onClick = [this, col, tok]() {
                  project_.document->SetCollectionColor(col,
                      TokenToInk(tok, ImVec4(0.5f,0.5f,0.5f,1)));
              };
              cc.submenu.push_back(std::move(e));
          }
          { UI::MenuEntry e; e.label = "Custom...";
            e.onClick = [this, col]() {
                outlinerColorPickColl_ = col;
                outlinerColorPickRequested_ = true;
            };
            cc.submenu.push_back(std::move(e)); }
          entries.push_back(std::move(cc)); }
        { UI::MenuEntry e; e.label = "Delete";
          e.tooltip = "Remove the collection; its objects are kept";
          e.onClick = [this, col]() {
              project_.document->RemoveCollection(col, false);
              LogInfoAction("Delete Collection");
          };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete Hierarchy"; e.icon = "ink-eraser";
          e.tooltip = "Remove the collection AND delete its objects and child collections";
          e.onClick = [this, col]() {
              project_.document->RemoveCollection(col, true);
              edit_.Prune(*project_.document);
              LogInfoAction("Delete Hierarchy");
          };
          entries.push_back(std::move(e)); }
    } else if (onObject && collectionsMode) {
        // ── Object menu, Collections view (organisation: collections/parents;
        //    NO layer-group entries here — groups are a Layers concept) ──
        { UI::MenuEntry e; e.label = "Select"; e.enabled = !OutlinerRowSelected(outlinerCtxNode_);
          e.onClick = [this]() { edit_.SelectOnly(outlinerCtxNode_); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect"; e.enabled = OutlinerRowSelected(outlinerCtxNode_);
          e.onClick = [this]() { edit_.Deselect(outlinerCtxNode_); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Copy"; e.shortcut = "Ctrl C"; e.enabled = false;
          e.tooltip = "Returns with the clipboard rework";
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Cut"; e.shortcut = "Ctrl X"; e.enabled = false;
          e.tooltip = "Returns with the clipboard rework";
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Paste"; e.shortcut = "Ctrl V"; e.enabled = false;
          e.tooltip = "Returns with the clipboard rework";
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate"; e.shortcut = "Ctrl D";
          e.onClick = [this]() { Action_DuplicateSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.shortcut = "X"; e.icon = "ink-eraser";
          e.onClick = [this]() { Action_DeleteSelection(); };
          entries.push_back(std::move(e)); }
        if (!doc.Collections().empty()) {
            UI::MenuEntry mv; mv.label = "Move to Collection";
            for (const Ink::Collection& c : doc.Collections()) {
                UI::MenuEntry e; e.label = c.name.empty() ? "(collection)" : c.name;
                const Ink::NodeId col = c.id;
                e.onClick = [this, col, targetIds]() {
                    OutlinerDropToCollection(targetIds(), col);
                };
                mv.submenu.push_back(std::move(e));
            }
            entries.push_back(std::move(mv));
        }
        { UI::MenuEntry e; e.label = "Remove from Collections";
          e.onClick = [this, targetIds]() { OutlinerRemoveFromCollections(targetIds()); };
          entries.push_back(std::move(e)); }
        { const Ink::Node* n = doc.Find(outlinerCtxNode_);
          UI::MenuEntry e; e.label = "Unparent";
          e.tooltip = "Clear the object parenting (keeps the world position)";
          e.enabled = n && n->parentId != Ink::kNullNode;
          e.onClick = [this, targetIds]() { OutlinerUnparent(targetIds()); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "New Collection from Selection"; e.enabled = hasSel;
          e.onClick = [this]() { Action_NewCollectionFromSelection(); };
          entries.push_back(std::move(e)); }
    } else if (onObject) {
        // ── Object menu, Layers view (stacking: groups/z-order; NO collection
        //    entries here — collections are the other organisation) ──
        { UI::MenuEntry e; e.label = "Select"; e.enabled = !OutlinerRowSelected(outlinerCtxNode_);
          e.onClick = [this]() { edit_.SelectOnly(outlinerCtxNode_); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect"; e.enabled = OutlinerRowSelected(outlinerCtxNode_);
          e.onClick = [this]() { edit_.Deselect(outlinerCtxNode_); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Group"; e.shortcut = "Ctrl G"; e.enabled = hasSel;
          e.onClick = [this]() { Action_GroupSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Ungroup"; e.shortcut = "Ctrl Alt G"; e.enabled = hasSel;
          e.onClick = [this]() { Action_UngroupSelection(); };
          entries.push_back(std::move(e)); }
        // Blend Mode ▸ — objects AND groups composite (the scene opens a
        // scope for any node with a non-Normal blend). Acts on the target set.
        { const Ink::Node* ctxN = doc.Find(outlinerCtxNode_);
          UI::MenuEntry bm; bm.label = "Blend Mode";
          bm.tooltip = "How this object composites over what is below it";
          static const char* kBlend[] = {
              "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
              "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
              "Difference", "Exclusion", "Erase" };
          for (int bi = 0; bi < (int)(sizeof kBlend / sizeof kBlend[0]); ++bi) {
              UI::MenuEntry e;
              const bool cur = ctxN && (int)ctxN->blend == bi;
              e.label = std::string(cur ? "\xE2\x80\xA2 " : "") + kBlend[bi];
              e.onClick = [this, targetIds, bi]() {
                  Action_SetBlendMode(targetIds(), (Ink::BlendMode)bi);
              };
              bm.submenu.push_back(std::move(e));
          }
          entries.push_back(std::move(bm)); }
        { UI::MenuEntry e; e.label = "Rename"; e.enabled = edit_.active != Ink::kNullNode;
          e.onClick = [this, &st]() {
              if (const Ink::Node* n = project_.document->Find(edit_.active)) {
                  st.outliner.renaming = edit_.active; st.outliner.renameTakeFocus = true;
                  std::snprintf(st.outliner.renameBuf, sizeof st.outliner.renameBuf,
                                "%s", n->name.c_str());
              }
          };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate"; e.shortcut = "Ctrl D"; e.enabled = hasSel;
          e.onClick = [this]() { Action_DuplicateSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.shortcut = "X"; e.icon = "ink-eraser";
          e.enabled = hasSel; e.onClick = [this]() { Action_DeleteSelection(); };
          entries.push_back(std::move(e)); }
    } else {
        // ── Background ── Collections view: Add Collection. Layers: nothing.
        if (!collectionsMode) { outlinerCtxOpen_ = false; return; }
        { UI::MenuEntry e; e.label = "Add Collection";
          e.onClick = [this]() {
              project_.document->AddCollection("Collection");
              LogInfoAction("Add Collection");
          };
          entries.push_back(std::move(e)); }
    }

    // ContextMenu returns false once ImGui closed the popup (item click, click
    // outside, or Esc — ImGui closes popups on Esc itself), which clears the flag.
    // The flag is only ever SET at the click site, together with a single
    // OpenPopup — never re-opened here, or the menu could never close.
    const bool open = UI::ContextMenu("##outlinerCtx", outlinerCtxPos_, entries, title);
    if (!open) outlinerCtxOpen_ = false;
}

// The Custom… collection colour picker. Rendered every frame in the same
// window scope (the Outliner scroll child); opened exactly once on request.
void Application::RenderOutlinerColorPicker() {
    if (outlinerColorPickRequested_) {
        outlinerColorPickRequested_ = false;
        ImGui::OpenPopup("##collColorPick");
    }
    if (outlinerColorPickColl_ == Ink::kNullNode) return;
    if (ImGui::BeginPopup("##collColorPick")) {
        const Ink::Collection* c = project_.document
            ? project_.document->FindCollection(outlinerColorPickColl_) : nullptr;
        if (!c) { ImGui::CloseCurrentPopup(); }
        else {
            ImVec4 col(c->colorTag.r, c->colorTag.g, c->colorTag.b, c->colorTag.a);
            if (ImGui::ColorPicker4("##collCol", &col.x,
                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview))
                project_.document->SetCollectionColor(outlinerColorPickColl_,
                    { col.x, col.y, col.z, col.w });
        }
        ImGui::EndPopup();
    } else {
        outlinerColorPickColl_ = Ink::kNullNode;   // closed
    }
}

// ── Organisation commands (undoable) ──────────────────────────────────────────

void Application::Action_SetBlendMode(const std::vector<Ink::NodeId>& ids,
                                      Ink::BlendMode mode) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    std::vector<std::pair<Ink::NodeId, Ink::BlendMode>> before;
    for (Ink::NodeId id : ids)
        if (const Ink::Node* n = doc.Find(id)) before.push_back({ id, n->blend });
    if (before.empty()) return;
    for (const auto& [id, unused] : before) doc.SetBlend(id, mode);
    PushDocCommand("Blend Mode",
        [before](Ink::Document& d) {
            for (const auto& [id, b] : before) d.SetBlend(id, b);
        },
        [before, mode](Ink::Document& d) {
            for (const auto& [id, b] : before) d.SetBlend(id, mode);
        });
    LogInfoAction("Blend Mode");
}

void Application::Action_ToggleNodeVisible(Ink::NodeId id) {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return;
    const bool now = !n->visible, was = n->visible;
    project_.document->SetVisible(id, now);
    PushDocCommand(now ? "Show" : "Hide",
        [id, was](Ink::Document& d) { d.SetVisible(id, was); },
        [id, now](Ink::Document& d) { d.SetVisible(id, now); });
}

void Application::Action_RenameNode(Ink::NodeId id, const std::string& name) {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(id);
    if (!n || n->name == name) return;
    const std::string before = n->name;
    project_.document->SetName(id, name);
    PushDocCommand("Rename",
        [id, before](Ink::Document& d) { d.SetName(id, before); },
        [id, name](Ink::Document& d) { d.SetName(id, name); });
}

void Application::Action_GroupSelection() {
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* act = doc.Find(edit_.active);
    if (!act) return;
    std::vector<Ink::NodeId> members;
    for (Ink::NodeId id : edit_.selection)
        if (const Ink::Node* n = doc.Find(id); n && n->parent == act->parent && n->page == act->page)
            members.push_back(id);
    if (members.empty()) return;
    const Ink::NodeId g = doc.GroupNodes(members, "Group");
    if (g == Ink::kNullNode) return;
    edit_.SelectOnly(g);
    auto after = doc.CopySubtree(g);
    PushDocCommand("Group",
        [g](Ink::Document& d) { d.UngroupNode(g); },
        [after](Ink::Document& d) { d.RestoreSubtree(after); });
    LogInfoAction("Group");
}

void Application::Action_UngroupSelection() {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    std::vector<Ink::NodeId> groups;
    for (Ink::NodeId id : edit_.selection)
        if (const Ink::Node* n = doc.Find(id); n && n->kind == Ink::NodeKind::Group)
            groups.push_back(id);
    if (groups.empty()) return;
    for (Ink::NodeId g : groups) {
        auto snap = doc.CopySubtree(g);
        auto freed = doc.UngroupNode(g);
        edit_.Clear();
        for (Ink::NodeId c : freed) edit_.SelectAdd(c);
        PushDocCommand("Ungroup",
            [snap](Ink::Document& d) { d.RestoreSubtree(snap); },
            [g](Ink::Document& d) { d.UngroupNode(g); });
    }
    LogInfoAction("Ungroup");
}

void Application::Action_NewCollectionFromSelection() {
    if (!project_.document || edit_.selection.empty()) return;
    Ink::Document& doc = *project_.document;
    const Ink::NodeId col = doc.AddCollection("Collection");
    for (Ink::NodeId id : edit_.selection) doc.AddToCollection(col, id);
    LogInfoAction("New Collection");
}

} // namespace App

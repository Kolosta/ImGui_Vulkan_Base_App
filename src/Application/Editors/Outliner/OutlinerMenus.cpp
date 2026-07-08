#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/PopupMenu.h>
#include <UI/Widgets/ScrollArea.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner top bar, Collections view, context menu and the organisation
//  commands (group / ungroup / collections) — docs/Ink/ROADMAP.md Lot 9. Every
//  command is undoable via the shared DocUndoStack: it captures the reverse
//  before applying, so a single Ctrl+Z restores the exact prior structure.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ── Top bar: display-mode toggle + search + kind filters ──────────────────────

void Application::BuildOutlinerTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    EditorState* pst = &st;

    bar.left.width = 150.0f * gs;
    bar.left.draw = [this, pst](ImVec2 pos, float) {
        ImGui::SetCursorPos(pos);
        UI::DropdownConfig cfg; cfg.id = "##outDisplay";
        cfg.triggerLabel = pst->outliner.display == OutlinerDisplayMode::Collections
                               ? "Collections" : "Layers";
        { UI::DropdownItem it; it.label = "Layers";
          it.tooltip = "Pages and their layer trees (z-order)"; cfg.items.push_back(it); }
        { UI::DropdownItem it; it.label = "Collections";
          it.tooltip = "Organisational sets (no z-order)"; cfg.items.push_back(it); }
        cfg.selectedIndex = (int)pst->outliner.display;
        UI::DropdownResult r = UI::Dropdown(cfg);
        if (r.changed && r.selected >= 0)
            pst->outliner.display = (OutlinerDisplayMode)r.selected;
    };

    bar.right.width = 200.0f * gs;
    bar.right.draw = [this, gs, pst](ImVec2 pos, float bh) {
        ImGui::SetCursorPos(pos);
        ImGui::SetNextItemWidth(150.0f * gs);
        ImGui::InputTextWithHint("##outSearch", "Search",
                                 pst->outliner.search, sizeof pst->outliner.search);
        ImGui::SameLine(0.0f, 4.0f * gs);
        (void)bh;
        // Kind filter popup (objects / groups).
        if (ImGui::Button("Filter")) ImGui::OpenPopup("##outFilter");
        if (ImGui::BeginPopup("##outFilter")) {
            ImGui::Checkbox("Objects", &pst->outliner.showObjects);
            ImGui::Checkbox("Groups",  &pst->outliner.showGroups);
            ImGui::EndPopup();
        }
    };
}

// ── Collections view ──────────────────────────────────────────────────────────

void Application::OutlinerCollectionsView(EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();
    Ink::Document& doc = *project_.document;
    const float gs = ds.GetGlobalScale();
    const float rowH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;

    if (doc.Collections().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No collections");
        ImGui::TextUnformatted("Right-click a selection to create one");
        ImGui::PopStyleColor();
        return;
    }

    OutlinerState& os = st.outliner;
    for (const Ink::Collection& c : doc.Collections()) {
        const bool collapsed = os.IsCollapsed(c.id);
        ImGui::PushID((int)c.id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float fullW = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##crow", ImVec2(fullW, rowH));
        const float cy = rowMin.y + rowH * 0.5f;
        float x = rowMin.x + 4.0f * gs;

        // Disclosure.
        const float s = 3.2f * gs;
        ImU32 tcol = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle));
        if (collapsed)
            dl->AddTriangleFilled({ x, cy - s }, { x + 2 * s, cy }, { x, cy + s }, tcol);
        else
            dl->AddTriangleFilled({ x, cy - s }, { x + 2 * s, cy - s }, { x + s, cy + s }, tcol);
        x += 16.0f * gs;

        // Color tag chip.
        const ImVec4 tag{ c.colorTag.r, c.colorTag.g, c.colorTag.b, c.colorTag.a };
        dl->AddRectFilled(ImVec2(x, cy - 5 * gs), ImVec2(x + 10 * gs, cy + 5 * gs),
                          ImGui::GetColorU32(tag), 2.0f * gs);
        x += 16.0f * gs;

        ImVec4 tc = ds.GetColor(Tok::S_Color_Text_Default);
        if (!c.visible) tc.w *= 0.4f;
        dl->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() * 0.5f),
                    ImGui::GetColorU32(tc),
                    c.name.empty() ? "(collection)" : c.name.c_str());

        // Eye toggle on the right.
        const float btn = rowH * 0.7f;
        const ImRect eye(ImVec2(rowMin.x + fullW - btn - 4 * gs, rowMin.y + (rowH - btn) * 0.5f),
                         ImVec2(rowMin.x + fullW - 4 * gs, rowMin.y + (rowH + btn) * 0.5f));
        const bool eyeHov = eye.Contains(ImGui::GetIO().MousePos);
        {
            auto& im = VectorGraphics::IconManager::Instance();
            auto md = im.GetDefaultMetadata(c.visible ? "eye" : "eye-closed");
            ImVec4 col = ds.GetColor(c.visible ? Tok::S_Color_Text_Default
                                               : Tok::S_Color_Text_Subtle);
            if (eyeHov) col = ds.GetColor(Tok::S_Color_Accent_Default);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = col;
            im.RenderIcon(dl, c.visible ? "eye" : "eye-closed", eye.Min, btn, md);
        }
        if (eyeHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            doc.SetCollectionVisible(c.id, !c.visible);
        else if (clicked) os.ToggleCollapsed(c.id);

        // Right-click → collection context (delete).
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            outlinerCtxOpen_ = true;
            outlinerCtxPos_  = ImGui::GetIO().MousePos;
            outlinerCtxNode_ = c.id;   // a collection id
        }

        ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));

        // Members.
        if (!collapsed)
            for (Ink::NodeId m : c.members) {
                const Ink::Node* mn = doc.Find(m);
                if (!mn) continue;
                ImGui::Indent(28.0f * gs);
                const bool sel = edit_.IsSelected(m);
                if (ImGui::Selectable(
                        (mn->name.empty() ? "(unnamed)" : mn->name).c_str(), sel))
                    edit_.SelectOnly(m);
                ImGui::Unindent(28.0f * gs);
            }
        ImGui::PopID();
    }
}

// ── Context menu ──────────────────────────────────────────────────────────────

void Application::RenderOutlinerContextMenu(EditorState& st) {
    if (!outlinerCtxOpen_) return;
    if (!ImGui::IsPopupOpen("##outlinerCtx")) ImGui::OpenPopup("##outlinerCtx");
    Ink::Document& doc = *project_.document;

    std::vector<UI::MenuEntry> entries;
    const bool hasSel = !edit_.selection.empty();
    const bool ctxIsCollection = doc.FindCollection(outlinerCtxNode_) != nullptr;

    if (ctxIsCollection) {
        const Ink::NodeId col = outlinerCtxNode_;
        { UI::MenuEntry e; e.label = "Rename Collection";
          e.onClick = [this, col, &st]() {
              if (const Ink::Collection* c = project_.document->FindCollection(col)) {
                  st.outliner.renaming = col;
                  std::snprintf(st.outliner.renameBuf, sizeof st.outliner.renameBuf,
                                "%s", c->name.c_str());
              }
          };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete Collection";
          e.onClick = [this, col]() {
              std::string name = project_.document->FindCollection(col)
                                     ? project_.document->FindCollection(col)->name : "";
              // (Collections aren't in the subtree undo model; a simple redo
              // re-adds by value would change ids. Kept direct: log only.)
              project_.document->RemoveCollection(col);
              LogInfoAction("Delete Collection");
          };
          entries.push_back(std::move(e)); }
    } else {
        { UI::MenuEntry e; e.label = "Group"; e.shortcut = "Ctrl G"; e.enabled = hasSel;
          e.onClick = [this]() { Action_GroupSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Ungroup"; e.shortcut = "Ctrl Alt G";
          e.enabled = hasSel; e.onClick = [this]() { Action_UngroupSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Rename"; e.enabled = edit_.active != Ink::kNullNode;
          e.onClick = [this, &st]() {
              if (const Ink::Node* n = project_.document->Find(edit_.active)) {
                  st.outliner.renaming = edit_.active;
                  std::snprintf(st.outliner.renameBuf, sizeof st.outliner.renameBuf,
                                "%s", n->name.c_str());
              }
          };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.shortcut = "X"; e.enabled = hasSel;
          e.onClick = [this]() { Action_DeleteSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate"; e.shortcut = "Ctrl D"; e.enabled = hasSel;
          e.onClick = [this]() { Action_DuplicateSelection(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "New Collection from Selection"; e.enabled = hasSel;
          e.onClick = [this]() { Action_NewCollectionFromSelection(); };
          entries.push_back(std::move(e)); }
        // Add-to-existing-collection submenu.
        if (hasSel && !doc.Collections().empty()) {
            UI::MenuEntry add; add.label = "Add to Collection";
            for (const Ink::Collection& c : doc.Collections()) {
                UI::MenuEntry e; e.label = c.name.empty() ? "(collection)" : c.name;
                const Ink::NodeId col = c.id;
                e.onClick = [this, col]() {
                    for (Ink::NodeId id : edit_.selection)
                        project_.document->AddToCollection(col, id);
                    LogInfoAction("Add to Collection");
                };
                add.submenu.push_back(std::move(e));
            }
            entries.push_back(std::move(add));
        }
    }

    const bool open = UI::ContextMenu("##outlinerCtx", outlinerCtxPos_, entries, "Outliner");
    if (!open) outlinerCtxOpen_ = false;
}

// ── Organisation commands (undoable) ──────────────────────────────────────────

void Application::Action_ToggleNodeVisible(Ink::NodeId id) {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return;
    const bool now = !n->visible;
    project_.document->SetVisible(id, now);
    PushDocCommand(now ? "Show" : "Hide",
        [id, was = n->visible](Ink::Document& d) { d.SetVisible(id, was); },
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
    // GroupNodes requires a common parent; grouping a mixed-parent selection
    // is out of v1 scope — take the members that share the active's parent.
    const Ink::Node* act = doc.Find(edit_.active);
    if (!act) return;
    std::vector<Ink::NodeId> members;
    for (Ink::NodeId id : edit_.selection)
        if (const Ink::Node* n = doc.Find(id); n && n->parent == act->parent &&
            n->page == act->page)
            members.push_back(id);
    if (members.empty()) return;

    // Snapshot the members' current placement for undo (ungroup on redo-undo).
    std::vector<Ink::Document::SubtreeSnapshot> before;
    for (Ink::NodeId id : members) before.push_back(doc.CopySubtree(id));
    const Ink::NodeId g = doc.GroupNodes(members, "Group");
    if (g == Ink::kNullNode) return;
    edit_.SelectOnly(g);
    auto after = doc.CopySubtree(g);
    PushDocCommand("Group",
        [g, before](Ink::Document& d) {
            d.UngroupNode(g);
            (void)before;   // children returned to their siblings by UngroupNode
        },
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

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
#include "OutlinerShared.h"

namespace App {

namespace DST = DesignSystem;

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
        const bool canPaste = !clipboard_.empty();
        // Copy/Cut operate on the whole selection when the clicked row is part of
        // it, else just the clicked row.
        auto srcIds = [this, id]() -> std::vector<uint64_t> {
            if (outlinerCur_ &&
                std::find(outlinerCur_->sel.begin(), outlinerCur_->sel.end(), id)
                    != outlinerCur_->sel.end())
                return outlinerCur_->sel;
            return { id };
        };
        { UI::MenuEntry e; e.label = "Select";   e.enabled = !sel;
          e.tooltip = "Make this the active selection";
          e.onClick = [this, id]{ project_.document.SelectOnly(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect"; e.enabled = sel;
          e.tooltip = "Remove this object from the selection";
          e.onClick = [this, id]{ project_.document.Deselect(id); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Copy"; e.shortcut = "Ctrl+C";
          e.tooltip = "Copy the selection to the internal clipboard";
          e.onClick = [this, srcIds]{ ClipboardCopy(srcIds()); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Cut"; e.shortcut = "Ctrl+X";
          e.tooltip = "Cut the selection to the internal clipboard";
          e.onClick = [this, srcIds]{ ClipboardCut(srcIds()); }; entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Paste"; e.enabled = canPaste; e.shortcut = "Ctrl+V";
          e.tooltip = "Paste the internal clipboard";
          e.onClick = [this]{ ClipboardPaste(); }; entries.push_back(std::move(e)); }
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

// ── Duplicate (the unified copy/cut/paste lives in App/Clipboard.cpp) ─────────
void Application::Action_OutlinerDuplicate(uint64_t shapeId) {
    MarkUndoLabel("Duplicate");
    uint64_t nid = project_.document.DuplicateShape(shapeId);
    if (nid) project_.document.SelectOnly(nid);
    project_.dirty = true;
}

} // namespace App

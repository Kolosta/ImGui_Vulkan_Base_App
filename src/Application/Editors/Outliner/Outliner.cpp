#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner editor — the Ink document's organisation tree (docs/Ink/ROADMAP.md
//  Lot 9). Two views: LAYERS (pages → layer trees, top-of-stack first, with
//  z-order, visibility, lock and rename) and COLLECTIONS (organisational sets).
//  The object selection is the shared App::EditContext — a click here selects
//  in every Viewport and the Properties editor, and vice-versa. Every edit
//  goes through the document's typed ops so it is undoable and recompiles the
//  scene exactly.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

const char* NodeIcon(const Ink::Node& n) {
    switch (n.kind) {
        case Ink::NodeKind::Group:    return "checklist";
        case Ink::NodeKind::Instance: return "swap_horiz";
        default:                      return "shape-category";
    }
}
} // namespace

// One Layers row (recursive). Draws the disclosure toggle, icon, name (or the
// inline rename field), and the eye/lock buttons; handles selection clicks.
void Application::OutlinerLayersRow(EditorState& st, Ink::NodeId id, int depth) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    if (n->kind == Ink::NodeKind::Group && !st.outliner.showGroups) {
        // Still recurse so children (objects) remain reachable.
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            OutlinerLayersRow(st, *it, depth);
        return;
    }
    if (n->kind != Ink::NodeKind::Group && !st.outliner.showObjects) return;

    // Search filter: show a node if it (or a descendant) matches.
    OutlinerState& os = st.outliner;
    const bool searching = os.search[0] != '\0';
    auto matches = [&](const Ink::Node& node) {
        std::string hay = node.name, needle = os.search;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        return hay.find(needle) != std::string::npos;
    };
    std::function<bool(const Ink::Node&)> subtreeMatch = [&](const Ink::Node& node) {
        if (matches(node)) return true;
        for (Ink::NodeId c : node.children)
            if (const Ink::Node* ch = doc.Find(c)) if (subtreeMatch(*ch)) return true;
        return false;
    };
    if (searching && !subtreeMatch(*n)) return;

    auto& ds      = DS::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float gs   = ds.GetGlobalScale();
    const float rowH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    const float indent = 14.0f * gs;
    const bool hasChildren = n->kind == Ink::NodeKind::Group && !n->children.empty();
    const bool collapsed   = os.IsCollapsed(id);
    const bool selected    = edit_.IsSelected(id);
    const bool isActive     = (edit_.active == id);

    ImGui::PushID((int)id);
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Selection band (full-width, Blender-style).
    if (selected) {
        ImVec4 band = ds.GetColor(Tok::S_Color_Accent_Default);
        band.w = isActive ? 0.42f : 0.24f;
        dl->AddRectFilled(rowMin, ImVec2(rowMin.x + fullW, rowMin.y + rowH),
                          ImGui::GetColorU32(band));
    }

    // Row hit target (whole width) for selection; drawn manually below.
    ImGui::SetCursorScreenPos(rowMin);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(fullW, rowH));
    const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                     ImGui::IsItemHovered();
    if (ImGui::IsItemHovered() && !selected) {
        ImVec4 hov = ds.GetColor(Tok::S_Color_Background_Layer2); hov.w = 0.5f;
        dl->AddRectFilled(rowMin, ImVec2(rowMin.x + fullW, rowMin.y + rowH),
                          ImGui::GetColorU32(hov));
    }

    float x = rowMin.x + depth * indent + 4.0f * gs;
    const float cy = rowMin.y + rowH * 0.5f;

    // Disclosure triangle.
    if (hasChildren) {
        const ImVec2 tc{ x + 5.0f * gs, cy };
        const float s = 3.2f * gs;
        ImU32 col = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle));
        if (collapsed)
            dl->AddTriangleFilled({ tc.x - s, tc.y - s }, { tc.x + s, tc.y },
                                  { tc.x - s, tc.y + s }, col);
        else
            dl->AddTriangleFilled({ tc.x - s, tc.y - s }, { tc.x + s, tc.y - s },
                                  { tc.x, tc.y + s }, col);
        // A click on the triangle toggles collapse instead of selecting.
        const ImRect tri(ImVec2(x, rowMin.y), ImVec2(x + 14.0f * gs, rowMin.y + rowH));
        if (clicked && tri.Contains(ImGui::GetIO().MousePos)) {
            os.ToggleCollapsed(id);
            ImGui::PopID();
            return;
        }
    }
    x += 14.0f * gs;

    // Icon.
    {
        const float isz = rowH * 0.6f;
        auto md = iconMgr.GetDefaultMetadata(NodeIcon(*n));
        if (!md.colorZones.empty())
            md.colorZones[0].customColor = ds.GetColor(Tok::S_Color_Text_Default);
        iconMgr.RenderIcon(dl, NodeIcon(*n), ImVec2(x, cy - isz * 0.5f), isz, md);
        x += isz + 6.0f * gs;
    }

    // Name or inline rename field.
    if (os.renaming == id) {
        ImGui::SetCursorScreenPos(ImVec2(x, rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::SetNextItemWidth(fullW - (x - rowMin.x) - 60.0f * gs);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", os.renameBuf, sizeof os.renameBuf,
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
            Action_RenameNode(id, os.renameBuf);
            os.renaming = 0;
        }
        if (ImGui::IsItemDeactivated()) os.renaming = 0;
    } else {
        ImVec4 tc = ds.GetColor(isActive ? Tok::S_Color_Text_Default
                                          : Tok::S_Color_Text_Default);
        if (!n->visible) tc.w *= 0.4f;
        dl->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() * 0.5f),
                    ImGui::GetColorU32(tc),
                    n->name.empty() ? "(unnamed)" : n->name.c_str());
    }

    // Eye (visibility) button on the right. Lock lives in the context menu
    // (no dedicated lock glyph in the icon set yet).
    const float btn = rowH * 0.7f;
    float rx = rowMin.x + fullW - btn - 4.0f * gs;
    auto iconButton = [&](const char* icon, bool on, float bx) {
        const ImRect r(ImVec2(bx, rowMin.y + (rowH - btn) * 0.5f),
                       ImVec2(bx + btn, rowMin.y + (rowH + btn) * 0.5f));
        const bool hov = r.Contains(ImGui::GetIO().MousePos);
        auto md = iconMgr.GetDefaultMetadata(icon);
        ImVec4 col = ds.GetColor(on ? Tok::S_Color_Text_Default : Tok::S_Color_Text_Subtle);
        if (!on) col.w *= 0.6f;
        if (hov) col = ds.GetColor(Tok::S_Color_Accent_Default);
        if (!md.colorZones.empty()) md.colorZones[0].customColor = col;
        iconMgr.RenderIcon(dl, icon, ImVec2(bx, r.Min.y), btn, md);
        return hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    };
    const bool eyeHit = iconButton(n->visible ? "eye" : "eye-closed", n->visible, rx);

    if (eyeHit) {
        Action_ToggleNodeVisible(id);
    } else if (dbl) {
        os.renaming = id;
        std::snprintf(os.renameBuf, sizeof os.renameBuf, "%s", n->name.c_str());
    } else if (clicked) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift && outlinerRangeAnchor_ != Ink::kNullNode) {
            // Range select is best-effort on the flattened row order; here we
            // just add the clicked node (full range needs the row list — kept
            // simple: Shift extends the selection to include this node).
            edit_.SelectAdd(id);
        } else if (io.KeyCtrl) {
            if (selected) edit_.Deselect(id); else edit_.SelectAdd(id);
            outlinerRangeAnchor_ = id;
        } else {
            edit_.SelectOnly(id);
            outlinerRangeAnchor_ = id;
        }
    }
    if (rightClicked) {
        if (!selected) edit_.SelectOnly(id);
        outlinerCtxOpen_ = true;
        outlinerCtxPos_  = ImGui::GetIO().MousePos;
        outlinerCtxNode_ = id;
    }

    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
    ImGui::PopID();

    // Children (top-of-stack first → reverse painter order).
    if (hasChildren && !collapsed)
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            OutlinerLayersRow(st, *it, depth + 1);
}

void Application::RenderOutliner(EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();
    if (!project_.document) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No document");
        ImGui::PopStyleColor();
        return;
    }
    Ink::Document& doc = *project_.document;

    // Keep the shared selection valid (a node may have been deleted/undone).
    edit_.Prune(doc);

    if (UI::BeginScroll("##outlinerScroll", ImVec2(0, 0))) {
        if (st.outliner.display == OutlinerDisplayMode::Collections) {
            OutlinerCollectionsView(st);
        } else {
            // Layers: each page's tree, top-of-stack first.
            for (const Ink::Page& page : doc.Pages()) {
                auto& ds2 = DS::DesignSystem::Instance();
                ImGui::PushStyleColor(ImGuiCol_Text, ds2.GetColor(Tok::S_Color_Text_Subtle));
                ImGui::TextUnformatted(page.name.empty() ? "Page" : page.name.c_str());
                ImGui::PopStyleColor();
                for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
                    OutlinerLayersRow(st, *it, 0);
            }
        }
        // Click on empty space clears the selection / opens the empty context.
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
            edit_.Clear();
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            !ImGui::IsAnyItemHovered()) {
            outlinerCtxOpen_ = true;
            outlinerCtxPos_  = ImGui::GetIO().MousePos;
            outlinerCtxNode_ = Ink::kNullNode;
        }
    }
    UI::EndScroll();

    RenderOutlinerContextMenu(st);
}

} // namespace App

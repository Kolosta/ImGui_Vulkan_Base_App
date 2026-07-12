#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/ButtonGroup.h>
#include <imgui.h>
#include <cstdio>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties editor — the active object's FULL property set, in the legacy
//  Compositor layout (docs/Ink/ROADMAP.md Lot 9 rework): 40 %-label rows,
//  vector groups titled once ("Location · X/Y"), token-driven gaps between
//  property groups, collapsible UI::Panel sections. Sections:
//    • Transform (any node)         — Properties.cpp (this file)
//    • Compositing (any node)       — opacity / blend / isolate / clip
//    • Paint (path nodes)           — PropertiesPaint.cpp (fills incl.
//      patterns, strokes incl. hairlines)
//    • Modifiers (path nodes)       — PropertiesModifiers.cpp (Array /
//      Along Path / Boolean, fully parametric)
//    • Instance (instance nodes)    — PropertiesModifiers.cpp (target)
//  Continuous drags fold into ONE undo command (captured on the first edited
//  frame, committed when that field deactivates).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

void Application::CommitStyleEdit(Ink::NodeId id, const Ink::Style& before,
                                  const std::string& label) {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return;
    const Ink::Style after = n->style;
    PushDocCommand(label,
        [id, before](Ink::Document& d) { d.SetStyle(id, before); },
        [id, after](Ink::Document& d)  { d.SetStyle(id, after); });
}

void Application::CommitModifiersEdit(Ink::NodeId id,
                                      const std::vector<Ink::Modifier>& before,
                                      const std::string& label) {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return;
    const std::vector<Ink::Modifier> after = n->modifiers;
    PushDocCommand(label,
        [id, before](Ink::Document& d) { d.SetModifiers(id, before); },
        [id, after](Ink::Document& d)  { d.SetModifiers(id, after); });
}

// ── Transform panel (legacy layout: Location X/Y group, Rotation, Scale X/Y
//    group — the group title written once, groups separated by the gap) ──────

void Application::PropTransformSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    UI::PanelConfig pc; pc.id = "##transform"; pc.label = "Transform";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::Transform2D t = n->transform;
        float loc[2] = { (float)t.tx, (float)t.ty };
        float scl[2] = { (float)t.sx, (float)t.sy };
        float rotDeg = (float)(t.rotation * 180.0 / 3.14159265358979);

        // Live-apply on change; ONE undo command when the edited field is
        // released (each drag folds into a single command).
        auto applyLive = [&](auto mutate) {
            if (!propEditActive_) {
                propEditActive_ = true; propEditNode_ = id;
                transformBeforeScratch_ = n->transform;
            }
            Ink::Transform2D nt = doc.Find(id)->transform;
            mutate(nt);
            doc.SetTransform(id, nt);
        };
        auto commitOnRelease = [&](bool deactivated) {
            if (deactivated && propEditActive_ && propEditNode_ == id) {
                const Ink::Transform2D before = transformBeforeScratch_;
                const Ink::Transform2D after  = doc.Find(id)->transform;
                PushDocCommand("Transform",
                    [id, before](Ink::Document& d) { d.SetTransform(id, before); },
                    [id, after](Ink::Document& d)  { d.SetTransform(id, after); });
                propEditActive_ = false; propEditNode_ = Ink::kNullNode;
            }
        };

        bool dx = false, dy = false;
        unsigned ch = pr::Vec2Group("Location", loc, 0.5f, 0.0f, 0.0f, 3, "",
                                    &dx, &dy);
        if (ch & 1u) applyLive([&](Ink::Transform2D& x) { x.tx = loc[0]; });
        if (ch & 2u) applyLive([&](Ink::Transform2D& x) { x.ty = loc[1]; });
        commitOnRelease(dx || dy);

        pr::GroupGap();
        if (pr::DragFloat("Rotation", &rotDeg, 0.5f, -3600, 3600, 1, "\xC2\xB0"))
            applyLive([&](Ink::Transform2D& x) {
                x.rotation = rotDeg * 3.14159265358979 / 180.0;
            });
        commitOnRelease(ImGui::IsItemDeactivatedAfterEdit());

        ch = pr::Vec2Group("Scale", scl, 0.01f, 0.0f, 0.0f, 3, "", &dx, &dy);
        if (ch & 1u) applyLive([&](Ink::Transform2D& x) { x.sx = scl[0]; });
        if (ch & 2u) applyLive([&](Ink::Transform2D& x) { x.sy = scl[1]; });
        commitOnRelease(dx || dy);

        if (n->kind == Ink::NodeKind::Path &&
            (n->transform.sx != 1.0 || n->transform.sy != 1.0)) {
            pr::GroupGap();
            pr::ControlColumn();
            if (ImGui::SmallButton("Apply Scale")) Action_ApplyScale();
        }
    }
    UI::EndPanel();
}

// ── Compositing panel — ANY node composites (the scene opens a scope for a
//    path/instance with opacity/blend too, not only groups) ──────────────────

void Application::PropCompositingSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    static const char* kBlend[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
        "Difference", "Exclusion", "Erase" };
    constexpr int kBlendCount = (int)(sizeof kBlend / sizeof kBlend[0]);

    UI::PanelConfig pc; pc.id = "##compositing"; pc.label = "Compositing";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        float op = n->opacity;
        if (pr::DragFloat("Opacity", &op, 0.005f, 0.0f, 1.0f, 3))
            doc.SetOpacity(id, op);
        if (ImGui::IsItemDeactivatedAfterEdit()) LogInfoAction("Opacity");

        int blend = std::min((int)n->blend, kBlendCount - 1);
        if (pr::DropdownRow("Blend", kBlend, kBlendCount, &blend))
            Action_SetBlendMode({ id }, (Ink::BlendMode)blend);

        bool iso = n->isolate;
        if (pr::CheckRow("Isolate", &iso)) {
            doc.SetIsolate(id, iso);
            LogInfoAction(iso ? "Isolate" : "Un-isolate");
        }
        if (n->kind == Ink::NodeKind::Group) {
            bool clip = n->clip;
            if (pr::CheckRow("Clip", &clip)) {
                doc.SetClip(id, clip);
                LogInfoAction(clip ? "Clip" : "Un-clip");
            }
        }
        // A NESTED node (Affinity layer child) can act as a mask of its parent
        // rather than a clipped child — the toggle here mirrors the Outliner
        // drag-onto-preview gesture.
        if (n->parent != Ink::kNullNode) {
            bool mask = n->isMask;
            if (pr::CheckRow("Mask", &mask)) {
                doc.SetMask(id, mask);
                LogInfoAction(mask ? "Mask Layer" : "Clip Layer");
            }
        }
    }
    UI::EndPanel();
}

// ── Top bar — the centred page tabs (Blender's property tabs) ─────────────────
// One linked ButtonGroup, exactly one page active: Object / Paint / Modifiers.
// Paint only applies to path nodes — its cell is DISABLED otherwise and the
// page falls back to Object (PropsEffectiveTab, shared with RenderProperties so
// the highlighted cell always matches the page actually shown).

EditorState::PropTab Application::PropsEffectiveTab(const EditorState& st) const {
    if (st.propTab == EditorState::PropTab::Paint) {
        const Ink::Node* n = project_.document && edit_.active != Ink::kNullNode
                                 ? project_.document->Find(edit_.active)
                                 : nullptr;
        if (!n || n->kind != Ink::NodeKind::Path)
            return EditorState::PropTab::Object;
    }
    return st.propTab;
}

void Application::BuildPropertiesTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = pr::DST::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float h  = ds.GetFloat(pr::Tok::S_Size_ControlHeight) * gs;
    const float cellW = 84.0f * gs;
    EditorState* stp = &st;

    bar.middle.width = cellW * 3.0f;
    bar.middle.draw = [this, stp, cellW, h](ImVec2 pos, float) {
        ImGui::SetCursorPos(pos);
        const Ink::Node* n = project_.document && edit_.active != Ink::kNullNode
                                 ? project_.document->Find(edit_.active)
                                 : nullptr;
        const bool isPath = n && n->kind == Ink::NodeKind::Path;
        const EditorState::PropTab cur = PropsEffectiveTab(*stp);

        struct TabDef { const char* label; EditorState::PropTab tab; bool enabled; };
        const TabDef tabs[] = {
            { "Object",    EditorState::PropTab::Object,    true },
            { "Paint",     EditorState::PropTab::Paint,     isPath },
            { "Modifiers", EditorState::PropTab::Modifiers, true },
        };
        UI::ButtonGroup g("##propTabs");
        g.SetGrid({ cellW, cellW, cellW }, { h });
        for (int i = 0; i < 3; ++i) {
            UI::ButtonGroup::Cell c{};
            c.label = tabs[i].label; c.col = i; c.row = 0;
            c.selected = (cur == tabs[i].tab);
            c.enabled  = tabs[i].enabled;
            g.AddCell(c);
        }
        UI::ButtonGroup::Result r = g.Render();
        if (r.clickedIndex >= 0 && r.clickedIndex < 3 &&
            tabs[r.clickedIndex].enabled)
            stp->propTab = tabs[r.clickedIndex].tab;
    };
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderProperties(EditorState& st) {
    auto& ds = pr::DST::DesignSystem::Instance();
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    edit_.Prune(doc);

    const Ink::NodeId id = edit_.active;
    if (id == Ink::kNullNode || !doc.Find(id)) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ds.GetColor(pr::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No active object.");
        ImGui::TextUnformatted("(select an object in the Viewport or Outliner)");
        ImGui::PopStyleColor();
        return;
    }
    const Ink::Node* n = doc.Find(id);

    if (UI::BeginScroll("##propsScroll", ImVec2(0, 0))) {
        // Name + type header (shown on both pages).
        {
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof nameBuf, "%s", n->name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##objname", nameBuf, sizeof nameBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                Action_RenameNode(id, nameBuf);

            const char* kind = n->kind == Ink::NodeKind::Group ? "Group"
                             : n->kind == Ink::NodeKind::Instance ? "Instance"
                             : "Path";
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ds.GetColor(pr::Tok::S_Color_Text_Subtle));
            ImGui::Text("Type: %s", kind);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        // Blender-style pages (the top-bar tabs): Object = transform /
        // compositing / instance target; Paint = the fill & stroke stacks;
        // Modifiers = the modifier stack. Paint falls back to Object when the
        // active node is not a path (its tab is disabled).
        switch (PropsEffectiveTab(st)) {
            case EditorState::PropTab::Paint:
                PropFillsSection(id);
                PropStrokesSection(id);
                break;
            case EditorState::PropTab::Modifiers:
                PropModifiersSection(id);
                break;
            case EditorState::PropTab::Object:
            default:
                PropTransformSection(id);
                PropCompositingSection(id);
                if (n->kind == Ink::NodeKind::Instance) PropInstanceSection(id);
                break;
        }
    }
    UI::EndScroll();
}

} // namespace App

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

// Flip one per-property lock bit as an undoable command (the padlock toggles).
void Application::TogglePropLock(Ink::NodeId id, std::uint32_t bit) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    const std::uint32_t before = n->propLocks;
    const std::uint32_t after  = before ^ bit;
    doc.SetPropLocks(id, after);
    PushDocCommand("Property Lock",
        [id, before](Ink::Document& d) { d.SetPropLocks(id, before); },
        [id, after](Ink::Document& d)  { d.SetPropLocks(id, after);  });
    LogInfoAction("Property Lock");
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

        // Per-property padlocks (Node::propLocks): a locked group renders
        // read-only; a module-managed lock (IOF spec-fixed channels) is inert.
        const std::uint32_t locks = n->propLocks;
        const bool managed = (locks & Ink::PropLockManaged) != 0;
        auto lockRow = [&](const char* lid, std::uint32_t bit) {
            if (pr::LockToggle(lid, (locks & bit) != 0, managed))
                TogglePropLock(id, bit);
            return (locks & bit) != 0;
        };

        bool dx = false, dy = false;
        pr::GroupGap();
        bool lk = lockRow("##lkPos", Ink::PropLockPosition);
        ImGui::BeginDisabled(lk);
        unsigned ch = pr::Vec2Group("Location", loc, 0.5f, 0.0f, 0.0f, 3, "",
                                    &dx, &dy, false, pr::Quantity::Length);
        ImGui::EndDisabled();
        if (ch & 1u) applyLive([&](Ink::Transform2D& x) { x.tx = loc[0]; });
        if (ch & 2u) applyLive([&](Ink::Transform2D& x) { x.ty = loc[1]; });
        commitOnRelease(dx || dy);

        pr::GroupGap();
        lk = lockRow("##lkRot", Ink::PropLockRotation);
        ImGui::BeginDisabled(lk);
        const bool rotCh = pr::DragFloat("Rotation", &rotDeg, 0.5f, -3600, 3600,
                                         1, "", pr::Quantity::Angle);
        const bool rotRel = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndDisabled();
        if (rotCh)
            applyLive([&](Ink::Transform2D& x) {
                x.rotation = rotDeg * 3.14159265358979 / 180.0;
            });
        commitOnRelease(rotRel);

        pr::GroupGap();
        lk = lockRow("##lkScl", Ink::PropLockScale);
        ImGui::BeginDisabled(lk);
        ch = pr::Vec2Group("Scale", scl, 0.01f, 0.0f, 0.0f, 3, "", &dx, &dy,
                           false);
        ImGui::EndDisabled();
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

// ── Curve panel (path nodes) — the subpath spline model (legacy Curve
//    properties): Bézier / NURBS / Poly type, cyclic, NURBS order + knot
//    modes, and in Edit mode the rational weight of the selected control
//    points. Type/cyclic/knob edits apply to EVERY subpath of the path. ──────

void Application::PropCurveSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path || n->path.subpaths.empty()) return;

    UI::PanelConfig pc; pc.id = "##curve"; pc.label = "Curve";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        const Ink::Subpath& s0 = n->path.subpaths.front();

        // One-shot structural change: mutate a copy, SetPath, one undo command.
        auto commit = [&](const char* label, auto mutate) {
            const Ink::PathData before = n->path;
            Ink::PathData after = before;
            mutate(after);
            doc.SetPath(id, after);
            PushDocCommand(label,
                [id, before](Ink::Document& d) { d.SetPath(id, before); },
                [id, after](Ink::Document& d)  { d.SetPath(id, after); });
        };
        // Drag fields (order / weight) live-apply and fold into ONE undo
        // command, committed when the field deactivates.
        auto applyLive = [&](auto mutate) {
            if (!propEditActive_) {
                propEditActive_ = true; propEditNode_ = id;
                pathBeforeScratch_ = n->path;
            }
            Ink::PathData np = doc.Find(id)->path;
            mutate(np);
            doc.SetPath(id, np);
        };
        auto commitOnRelease = [&](const char* label, bool deactivated) {
            if (deactivated && propEditActive_ && propEditNode_ == id) {
                const Ink::PathData before = pathBeforeScratch_;
                const Ink::PathData after  = doc.Find(id)->path;
                PushDocCommand(label,
                    [id, before](Ink::Document& d) { d.SetPath(id, before); },
                    [id, after](Ink::Document& d)  { d.SetPath(id, after); });
                propEditActive_ = false; propEditNode_ = Ink::kNullNode;
            }
        };

        static const char* kSpline[] = { "B\xC3\xA9zier", "NURBS", "Poly" };
        int stype = std::clamp((int)s0.spline, 0, 2);
        if (pr::ButtonGroupRow("Spline", kSpline, 3, &stype))
            commit("Spline Type", [&](Ink::PathData& p) {
                for (Ink::Subpath& sp : p.subpaths)
                    sp.spline = (Ink::SplineType)stype;
            });

        bool cyc = s0.closed;
        if (pr::CheckRow("Cyclic", &cyc))
            commit(cyc ? "Cyclic On" : "Cyclic Off", [&](Ink::PathData& p) {
                for (Ink::Subpath& sp : p.subpaths) sp.closed = cyc;
            });

        if ((Ink::SplineType)stype == Ink::SplineType::Nurbs) {
            pr::GroupGap();
            // Order is capped per subpath at min(6, control-point count); the
            // row's range uses the largest subpath so the value is reachable.
            int maxOrder = 2;
            for (const Ink::Subpath& sp : n->path.subpaths)
                maxOrder = std::max(maxOrder,
                    (int)std::min<std::size_t>(6, sp.anchors.size()));
            int order = std::clamp((int)s0.orderU, 2, maxOrder);
            if (pr::DragInt("Order U", &order, 0.05f, 2, maxOrder))
                applyLive([&](Ink::PathData& p) {
                    for (Ink::Subpath& sp : p.subpaths)
                        sp.orderU = (std::uint8_t)std::clamp(order, 2,
                            (int)std::min<std::size_t>(6, sp.anchors.size()));
                });
            commitOnRelease("Order U", ImGui::IsItemDeactivatedAfterEdit());

            bool ep = s0.nurbsEndpoint;
            if (pr::CheckRow("Endpoint U", &ep))
                commit("Endpoint U", [&](Ink::PathData& p) {
                    for (Ink::Subpath& sp : p.subpaths) sp.nurbsEndpoint = ep;
                });
            bool bz = s0.nurbsBezier;
            if (pr::CheckRow("Bezier U", &bz))
                commit("Bezier U", [&](Ink::PathData& p) {
                    for (Ink::Subpath& sp : p.subpaths) sp.nurbsBezier = bz;
                });

            // Edit mode: the rational weight of the SELECTED control points
            // (shows the mean; a drag writes the value to all of them).
            if (edit_.mode == EditorMode::Edit && edit_.active == id) {
                double wsum = 0.0; int wcount = 0;
                for (const EditContext::ElemRef& e : edit_.elemSel) {
                    if (e.part != EditContext::ElemPart::Point) continue;
                    if (e.sp < 0 || e.sp >= (int)n->path.subpaths.size()) continue;
                    const auto& an = n->path.subpaths[(std::size_t)e.sp].anchors;
                    if (e.a < 0 || e.a >= (int)an.size()) continue;
                    wsum += an[(std::size_t)e.a].weight; ++wcount;
                }
                if (wcount > 0) {
                    pr::GroupGap();
                    float w = (float)(wsum / wcount);
                    if (pr::DragFloat("Weight", &w, 0.01f, 0.01f, 100.0f, 3))
                        applyLive([&](Ink::PathData& p) {
                            for (const EditContext::ElemRef& e : edit_.elemSel) {
                                if (e.part != EditContext::ElemPart::Point)
                                    continue;
                                if (e.sp < 0 ||
                                    e.sp >= (int)p.subpaths.size()) continue;
                                auto& an = p.subpaths[(std::size_t)e.sp].anchors;
                                if (e.a < 0 || e.a >= (int)an.size()) continue;
                                an[(std::size_t)e.a].weight = (double)w;
                            }
                        });
                    commitOnRelease("Weight",
                                    ImGui::IsItemDeactivatedAfterEdit());
                }
            }
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
        if (pr::DragFloat("Opacity", &op, 0.5f, 0.0f, 1.0f, 0, "",
                          pr::Quantity::Percent))
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
    // Icon-only fused cells in the DROPDOWN chrome — exactly the family of the
    // Viewport bar's snap widget (dark dropdown fill, no group outline,
    // dead-centred icons, accent fill on the selected cell).
    const float cellW = ds.GetFloat(pr::Tok::C_Dropdown_Height) * gs * 1.45f;
    EditorState* stp = &st;

    constexpr int kNTabs = 4;
    bar.middle.width = cellW * (float)kNTabs;
    bar.middle.draw = [this, stp, cellW](ImVec2 pos, float) {
        ImGui::SetCursorPos(pos);
        const Ink::Node* n = project_.document && edit_.active != Ink::kNullNode
                                 ? project_.document->Find(edit_.active)
                                 : nullptr;
        const bool isPath = n && n->kind == Ink::NodeKind::Path;
        const EditorState::PropTab cur = PropsEffectiveTab(*stp);

        struct TabDef {
            const char* icon; const char* tip;
            EditorState::PropTab tab; bool enabled;
        };
        const TabDef tabs[] = {
            { "shape-category", "Object properties",
              EditorState::PropTab::Object,    true },
            { "colorize",       "Paint (fills & strokes)",
              EditorState::PropTab::Paint,     isPath },
            { "settings",       "Modifiers",
              EditorState::PropTab::Modifiers, true },
            { "crop-free",      "Document settings",
              EditorState::PropTab::Document,  true },
        };
        std::vector<UI::DropdownButton> cells(kNTabs);
        for (int i = 0; i < kNTabs; ++i) {
            cells[(std::size_t)i].id      = tabs[i].icon;
            cells[(std::size_t)i].icon    = tabs[i].icon;
            cells[(std::size_t)i].tooltip = tabs[i].tip;
            cells[(std::size_t)i].active  = (cur == tabs[i].tab);
            cells[(std::size_t)i].enabled = tabs[i].enabled;
        }
        const int clicked = UI::DropdownButtonRow("##propTabs", cells, cellW);
        if (clicked >= 0 && clicked < kNTabs && tabs[clicked].enabled)
            stp->propTab = tabs[clicked].tab;
    };
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderProperties(EditorState& st) {
    auto& ds = pr::DST::DesignSystem::Instance();
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    edit_.Prune(doc);

    // Document settings are independent of the selection — render them even with
    // no active object (before the "no active object" guard below).
    if (st.propTab == EditorState::PropTab::Document) {
        if (UI::BeginScroll("##propsScroll", ImVec2(0, 0)))
            DrawPropertiesDocument();
        UI::EndScroll();
        return;
    }

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
                PropPaintOrderSection(id);
                break;
            case EditorState::PropTab::Modifiers:
                PropModifiersSection(id);
                break;
            case EditorState::PropTab::Object:
            default:
                PropTransformSection(id);
                if (n->kind == Ink::NodeKind::Path) PropCurveSection(id);
                PropCompositingSection(id);
                if (n->kind == Ink::NodeKind::Instance) PropInstanceSection(id);
                break;
        }
    }
    UI::EndScroll();
}

// ── Document settings tab ─────────────────────────────────────────────────────
// Document-wide options (independent of the selection). The DISPLAY UNIT SYSTEM
// is the first: it drives every unit-aware input in the app (a viewport's rulers
// + N-panel Item tab can override it locally). Geometry is stored once in the
// base unit (px) — this only changes how values are shown/parsed.
void Application::DrawPropertiesDocument() {
    auto& ds = pr::DST::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(pr::Tok::S_Color_Text_Subtle));
    ImGui::TextUnformatted("Units");
    ImGui::PopStyleColor();
    ImGui::Separator();

    static const char* kSystems[UI::Units::kUnitSystemCount] = {
        "Metric", "Imperial", "Typographic", "Pixel" };
    int sys = (int)project_.docUnitSystem;
    if (pr::DropdownRow("Unit system", kSystems, UI::Units::kUnitSystemCount, &sys)) {
        project_.docUnitSystem = (UI::Units::UnitSystem)sys;
        project_.dirty = true;
    }

    pr::GroupGap();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(pr::Tok::S_Color_Text_Subtle));
    ImGui::TextUnformatted("Colour");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Colour MODE (RGB screen / CMYK print separations). A module may PIN it
    // (IOF: the ISOM inks are CMYK-defined) — the switch then renders inert.
    static const char* kModes[] = { "RGB", "CMYK" };
    const bool pinned = activeCapabilities_.colorMode >= 0;
    int cm = (int)project_.colorMode;
    ImGui::BeginDisabled(pinned);
    if (pr::ButtonGroupRow("Colour mode", kModes, 2, &cm) && !pinned) {
        project_.colorMode = cm ? Project::ColorModeKind::Cmyk
                                : Project::ColorModeKind::Rgb;
        project_.dirty = true;
    }
    ImGui::EndDisabled();
    if (pinned && ImGui::IsItemHovered())
        UI::DrawTooltipTranslucent(
            "Fixed by the active module (its colour definitions are CMYK)",
            ImGui::GetIO().MousePos, 1.0f);

    // How the document will be PRINTED. This is an output choice, not a way of
    // looking at the canvas, so it belongs here and not in a viewport's proofing
    // menu: it decides which ink a colour is actually made of, for every view
    // and for every export.
    if (project_.document) {
        Ink::Document& pdoc = *project_.document;
        static const char* kTech[3] = { "CMYK", "CMYK+B", "PMS" };
        int tech = (int)pdoc.PrintTech();
        if (pr::ButtonGroupRow("Print technique", kTech, 3, &tech)) {
            pdoc.SetPrintTech((Ink::PrintTechnique)tech);
            // A module keys its palette off the technique (which colours are
            // laid as their own ink) — let it reseed before the next frame.
            if (activeModule_) activeModule_->OnActivate();
            project_.dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip(
                "Which inks the press lays. It changes what a colour is MADE "
                "of, never where it sits in the stack — the plate order is the "
                "same for all three.\n"
                "CMYK: every colour mixed from the four process inks — "
                "cheapest, but thin brown lines soften.\n"
                "CMYK+B: the colours that name a spot ink are pulled out of "
                "that mix and laid as their own (PMS 471 brown, PMS purple), "
                "which restores the line sharpness.\n"
                "PMS: everything as spot inks — sharpest, costlier, and it "
                "cannot carry process artwork.",
                ImGui::GetIO().MousePos);
    }
}

} // namespace App

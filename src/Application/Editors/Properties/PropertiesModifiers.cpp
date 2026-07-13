#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/Panel.h>
#include <imgui.h>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties — the MODIFIER stack (docs/Ink/DOCUMENT_MODEL.md §6) and the
//  Instance target, fully parametric: everything the demo scene builds in
//  code (array chains, along-path distributions, booleans, instances) is
//  editable here. Structural edits (add/remove/reorder/pickers/switches)
//  commit immediately; numeric drags fold into one undo command committed on
//  release (modifiersBeforeScratch_).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

void Application::PropModifiersSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;

    // Blender's modifier stack: NO wrapper section — each modifier is its own
    // top-level expandable, directly on the Modifiers page. Drag a panel to
    // reorder it (the grabbed panel floats, the others slide out of its way,
    // animated); the header cross removes it. Each edit lands as ONE undoable
    // command after the loop.
    {
        std::vector<Ink::Modifier> mods = n->modifiers;
        bool structural = false;
        const char* structLabel = "Edit Modifiers";
        UI::PanelListEdit listEdit;

        // "+ Add Modifier" ON TOP of the stack (Blender): a full-width menu
        // button listing the modifier kinds; the new modifier appends BELOW
        // the existing ones. Along Path samples this node's own spine — path
        // nodes only.
        {
            struct AddDef { const char* label; Ink::ModifierKind kind; };
            std::vector<AddDef> defs = {
                { "Array",   Ink::ModifierKind::Array },
            };
            if (n->kind == Ink::NodeKind::Path)
                defs.push_back({ "Along Path", Ink::ModifierKind::AlongPath });
            defs.push_back({ "Boolean", Ink::ModifierKind::Boolean });

            UI::DropdownConfig cfg;
            cfg.id = "##addModifier";
            cfg.triggerIcon  = "new";
            cfg.triggerLabel = "Add Modifier";
            cfg.triggerWidth = ImGui::GetContentRegionAvail().x;
            for (const AddDef& d : defs) {
                UI::DropdownItem it;
                it.label = d.label;
                cfg.items.push_back(it);
            }
            UI::DropdownResult r = UI::Dropdown(cfg);
            if (r.changed && r.selected >= 0 && r.selected < (int)defs.size()) {
                Ink::Modifier m;
                m.kind = defs[(std::size_t)r.selected].kind;
                if (m.kind == Ink::ModifierKind::Array) {
                    m.step.tx = 20.0;
                    m.stepSpace = Ink::ArrayStepSpace::Parent;
                }
                mods.push_back(m);
                structural = true; structLabel = "Add Modifier";
            }
        }

        auto liveApply = [&](const char* releaseLabel, bool released) {
            if (!propEditActive_) {
                propEditActive_ = true; propEditNode_ = id;
                modifiersBeforeScratch_ = n->modifiers;
            }
            doc.SetModifiers(id, mods);
            if (released && propEditActive_ && propEditNode_ == id) {
                CommitModifiersEdit(id, modifiersBeforeScratch_, releaseLabel);
                propEditActive_ = false;
            }
        };
        auto dragCommit = [&](const char* label) {
            if (ImGui::IsItemDeactivatedAfterEdit()) liveApply(label, true);
        };

        for (std::size_t i = 0; i < mods.size(); ++i) {
            Ink::Modifier& m = mods[i];
            const char* kindName =
                m.kind == Ink::ModifierKind::Array ? "Array"
                : m.kind == Ink::ModifierKind::AlongPath ? "Along Path"
                : "Boolean";
            char lab[40];
            std::snprintf(lab, sizeof lab, "%d \xC2\xB7 %s", (int)i + 1, kindName);
            UI::PanelConfig mp; mp.id = "##mod"; mp.label = lab;
            mp.defaultOpen = true;
            mp.closable = true;       // a close cross removes it
            UI::PanelResult mr =
                UI::BeginPanelListItem(mp, (int)i, (int)mods.size(), listEdit);
            if (mr.open) {
                bool enabled = m.enabled;
                if (pr::CheckRow("Enabled", &enabled)) {
                    m.enabled = enabled;
                    structural = true; structLabel = "Toggle Modifier";
                }

                if (m.kind == Ink::ModifierKind::Array) {
                    int count = m.count;
                    if (pr::DragInt("Count", &count, 0.1f, 1, 10000)) {
                        m.count = count;
                        liveApply("Array Count", false);
                    }
                    dragCommit("Array Count");

                    float st[2] = { (float)m.step.tx, (float)m.step.ty };
                    bool dx = false, dy = false;
                    if (pr::Vec2Group("Step", st, 0.5f, 0.0f, 0.0f, 2, "",
                                      &dx, &dy)) {
                        m.step.tx = st[0]; m.step.ty = st[1];
                        liveApply("Array Step", false);
                    }
                    if (dx || dy) liveApply("Array Step", true);

                    pr::GroupGap();
                    float rotDeg =
                        (float)(m.step.rotation * 180.0 / 3.14159265358979);
                    if (pr::DragFloat("Step rotation", &rotDeg, 0.5f, -360.0f,
                                      360.0f, 1, "\xC2\xB0")) {
                        m.step.rotation = rotDeg * 3.14159265358979 / 180.0;
                        liveApply("Array Rotation", false);
                    }
                    dragCommit("Array Rotation");

                    float sc[2] = { (float)m.step.sx, (float)m.step.sy };
                    if (pr::Vec2Group("Step scale", sc, 0.005f, 0.0f, 0.0f, 3,
                                      "", &dx, &dy)) {
                        m.step.sx = sc[0]; m.step.sy = sc[1];
                        liveApply("Array Scale", false);
                    }
                    if (dx || dy) liveApply("Array Scale", true);

                    pr::GroupGap();
                    static const char* kSpace[] = { "Local", "Parent" };
                    int space = (int)m.stepSpace;
                    if (pr::ButtonGroupRow("Step space", kSpace, 2, &space)) {
                        m.stepSpace = (Ink::ArrayStepSpace)space;
                        structural = true; structLabel = "Array Step Space";
                    }
                } else if (m.kind == Ink::ModifierKind::AlongPath) {
                    // The modifier lives on THIS path; it instances the picked
                    // OBJECT along the path's own spine (Blender's rule).
                    bool pickReq = false;
                    if (pr::NodePickerRow("Object", doc, &m.motifRef, id,
                                          /*allowNone=*/true,
                                          /*pathsOnly=*/false, &pickReq)) {
                        structural = true; structLabel = "Along Path Object";
                    }
                    if (pickReq) {
                        const std::size_t mIdx = i;
                        BeginObjectPick(nullptr, [this, id, mIdx](Ink::NodeId picked) {
                            if (!project_.document) return;
                            const Ink::Node* nn = project_.document->Find(id);
                            if (!nn || mIdx >= nn->modifiers.size()) return;
                            std::vector<Ink::Modifier> before = nn->modifiers, after = before;
                            after[mIdx].motifRef = picked;
                            project_.document->SetModifiers(id, after);
                            CommitModifiersEdit(id, before, "Along Path Object");
                        });
                    }
                    static const char* kDist[] = { "Count", "Spacing",
                                                   "Anchors" };
                    int dist = (int)m.distribute;
                    if (pr::ButtonGroupRow("Distribute", kDist, 3, &dist)) {
                        m.distribute = (Ink::AlongDistribute)dist;
                        structural = true; structLabel = "Along Distribution";
                    }
                    if (m.distribute == Ink::AlongDistribute::ByCount) {
                        int count = m.alongCount;
                        if (pr::DragInt("Count", &count, 0.1f, 1, 100000)) {
                            m.alongCount = count;
                            liveApply("Along Count", false);
                        }
                        dragCommit("Along Count");
                    } else if (m.distribute == Ink::AlongDistribute::BySpacing) {
                        float sp = (float)m.spacing;
                        if (pr::DragFloat("Spacing", &sp, 0.2f, 0.01f,
                                          100000.0f, 2)) {
                            m.spacing = sp;
                            liveApply("Along Spacing", false);
                        }
                        dragCommit("Along Spacing");
                    }
                    static const char* kAlign[] = { "None", "Tangent" };
                    int align = (int)m.align;
                    if (pr::ButtonGroupRow("Align", kAlign, 2, &align)) {
                        m.align = (Ink::AlongAlign)align;
                        structural = true; structLabel = "Along Align";
                    }
                    if (m.distribute != Ink::AlongDistribute::AtAnchors) {
                        pr::GroupGap();
                        float t0 = (float)m.startTrim, t1 = (float)m.endTrim;
                        if (pr::DragFloat("Trim start", &t0, 0.5f, 0.0f,
                                          1000000.0f, 1)) {
                            m.startTrim = t0;
                            liveApply("Along Trim", false);
                        }
                        dragCommit("Along Trim");
                        if (pr::DragFloat("Trim end", &t1, 0.5f, 0.0f,
                                          1000000.0f, 1)) {
                            m.endTrim = t1;
                            liveApply("Along Trim", false);
                        }
                        dragCommit("Along Trim");
                    }
                } else {   // Boolean
                    static const char* kOp[] = { "Union", "Subtract",
                                                 "Intersect", "Xor" };
                    int op = (int)m.op;
                    if (pr::DropdownRow("Operation", kOp, 4, &op)) {
                        m.op = (Ink::BooleanOp)op;
                        structural = true; structLabel = "Boolean Operation";
                    }
                    bool pickOp = false;
                    if (pr::NodePickerRow("Operand", doc, &m.operandRef, id,
                                          /*allowNone=*/true, /*pathsOnly=*/true,
                                          &pickOp)) {
                        structural = true; structLabel = "Boolean Operand";
                    }
                    if (pickOp) {
                        const std::size_t mIdx = i;
                        BeginObjectPick(nullptr, [this, id, mIdx](Ink::NodeId picked) {
                            if (!project_.document) return;
                            const Ink::Node* nn = project_.document->Find(id);
                            if (!nn || mIdx >= nn->modifiers.size()) return;
                            std::vector<Ink::Modifier> before = nn->modifiers, after = before;
                            after[mIdx].operandRef = picked;
                            project_.document->SetModifiers(id, after);
                            CommitModifiersEdit(id, before, "Boolean Operand");
                        });
                    }
                }
            }
            UI::EndPanelListItem();
        }

        // Apply the list edit (a close, or the drag's move committed at
        // release), as one undoable command. Only one edit fires per frame.
        if (listEdit.removeAt >= 0 && listEdit.removeAt < (int)mods.size()) {
            mods.erase(mods.begin() + listEdit.removeAt);
            structural = true; structLabel = "Remove Modifier";
        } else if (listEdit.moveFrom >= 0) {
            UI::PanelListApplyMove(mods, listEdit);
            structural = true; structLabel = "Reorder Modifiers";
        }

        if (structural) {
            const std::vector<Ink::Modifier> before = n->modifiers;
            doc.SetModifiers(id, mods);
            CommitModifiersEdit(id, before, structLabel);
        }
    }
}

// ── Instance node: the rendered target ────────────────────────────────────────

void Application::PropInstanceSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Instance) return;

    UI::PanelConfig pc; pc.id = "##instance"; pc.label = "Instance";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::NodeId target = n->targetRef;
        // Any node can be instanced (paths, groups, even other instances) —
        // and the target can be CLEARED (the trailing cross writes kNullNode).
        auto commitTarget = [this, id](Ink::NodeId picked) {
            if (!project_.document) return;
            const Ink::Node* nn = project_.document->Find(id);
            if (!nn) return;
            const Ink::NodeId before = nn->targetRef;
            if (picked == before) return;
            project_.document->SetInstanceTarget(id, picked);
            PushDocCommand("Instance Target",
                [id, before](Ink::Document& d) { d.SetInstanceTarget(id, before); },
                [id, picked](Ink::Document& d) { d.SetInstanceTarget(id, picked); });
            LogInfoAction("Instance Target");
        };
        bool pickTgt = false;
        // The picker writes the chosen id (or kNullNode on clear) into `target`
        // and returns true; the trigger already shows the resolved target name,
        // so there is NO separate "Renders <name>" row (it doubled the label and
        // overlapped the input).
        if (pr::NodePickerRow("Target", doc, &target, id,
                              /*allowNone=*/true, /*pathsOnly=*/false, &pickTgt))
            commitTarget(target);
        if (pickTgt) BeginObjectPick(nullptr, commitTarget);
    }
    UI::EndPanel();
}

} // namespace App

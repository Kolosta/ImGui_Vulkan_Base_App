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

    UI::PanelConfig pc; pc.id = "##modifiers"; pc.label = "Modifiers";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        std::vector<Ink::Modifier> mods = n->modifiers;
        bool structural = false;
        const char* structLabel = "Edit Modifiers";

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
            ImGui::PushID((int)(3000 + i));
            Ink::Modifier& m = mods[i];
            const char* kindName =
                m.kind == Ink::ModifierKind::Array ? "Array"
                : m.kind == Ink::ModifierKind::AlongPath ? "Along Path"
                : "Boolean";
            char lab[40];
            std::snprintf(lab, sizeof lab, "%d \xC2\xB7 %s", (int)i + 1, kindName);
            UI::PanelConfig mp; mp.id = "##mod"; mp.label = lab;
            mp.defaultOpen = true;
            if (UI::BeginPanel(mp).open) {
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

                pr::ControlColumn();
                if (ImGui::SmallButton("Up") && i > 0) {
                    std::swap(mods[i], mods[i - 1]);
                    structural = true; structLabel = "Reorder Modifiers";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && i + 1 < mods.size()) {
                    std::swap(mods[i], mods[i + 1]);
                    structural = true; structLabel = "Reorder Modifiers";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    mods.erase(mods.begin() + (long)i);
                    structural = true; structLabel = "Remove Modifier";
                    UI::EndPanel(); ImGui::PopID();
                    break;
                }
            }
            UI::EndPanel();
            ImGui::PopID();
        }

        pr::ControlColumn();
        if (ImGui::SmallButton("+ Array")) {
            Ink::Modifier m; m.kind = Ink::ModifierKind::Array;
            m.step.tx = 20.0; m.stepSpace = Ink::ArrayStepSpace::Parent;
            mods.push_back(m);
            structural = true; structLabel = "Add Modifier";
        }
        ImGui::SameLine();
        // Along Path samples THIS node's own spine — path nodes only.
        if (n->kind == Ink::NodeKind::Path && ImGui::SmallButton("+ Along Path")) {
            Ink::Modifier m; m.kind = Ink::ModifierKind::AlongPath;
            mods.push_back(m);
            structural = true; structLabel = "Add Modifier";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Boolean")) {
            Ink::Modifier m; m.kind = Ink::ModifierKind::Boolean;
            mods.push_back(m);
            structural = true; structLabel = "Add Modifier";
        }

        if (structural) {
            const std::vector<Ink::Modifier> before = n->modifiers;
            doc.SetModifiers(id, mods);
            CommitModifiersEdit(id, before, structLabel);
        }
    }
    UI::EndPanel();
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
        // Any node can be instanced (paths, groups, even other instances).
        auto commitTarget = [this, id](Ink::NodeId picked) {
            if (!project_.document) return;
            const Ink::Node* nn = project_.document->Find(id);
            if (!nn || picked == Ink::kNullNode) return;
            const Ink::NodeId before = nn->targetRef;
            project_.document->SetInstanceTarget(id, picked);
            PushDocCommand("Instance Target",
                [id, before](Ink::Document& d) { d.SetInstanceTarget(id, before); },
                [id, picked](Ink::Document& d) { d.SetInstanceTarget(id, picked); });
            LogInfoAction("Instance Target");
        };
        bool pickTgt = false;
        if (pr::NodePickerRow("Target", doc, &target, id,
                              /*allowNone=*/false, /*pathsOnly=*/false, &pickTgt))
            commitTarget(target);
        if (pickTgt) BeginObjectPick(nullptr, commitTarget);
        if (const Ink::Node* t = doc.Find(n->targetRef)) {
            pr::Label("Renders");
            ImGui::TextUnformatted(t->name.empty() ? "(unnamed)"
                                                   : t->name.c_str());
        }
    }
    UI::EndPanel();
}

} // namespace App

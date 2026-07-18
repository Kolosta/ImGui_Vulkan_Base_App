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
                if (m.kind == Ink::ModifierKind::Array) m.step.tx = 20.0;
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
                    constexpr double kDeg = 3.14159265358979 / 180.0;
                    // Placement mode (Blender's array, 2D): Transform =
                    // cumulative per-copy transform; Line = straight line with
                    // in-place instance spin; Circle = copies on a circle.
                    static const char* kAMode[] = { "Transform", "Line",
                                                    "Circle" };
                    int am = (int)m.arrayMode;
                    if (pr::ButtonGroupRow("Mode", kAMode, 3, &am)) {
                        m.arrayMode = (Ink::ArrayMode)am;
                        structural = true; structLabel = "Array Mode";
                    }
                    pr::GroupGap();

                    const bool circle =
                        m.arrayMode == Ink::ArrayMode::Circle;
                    const bool byAngle = circle &&
                        m.circleMethod == Ink::ArrayCircleMethod::ByAngle;
                    if (!circle || !byAngle) {
                        int count = m.count;
                        if (pr::DragInt("Count", &count, 0.1f, 1, 10000)) {
                            m.count = count;
                            liveApply("Array Count", false);
                        }
                        dragCommit("Array Count");
                    }

                    if (m.arrayMode == Ink::ArrayMode::Transform ||
                        m.arrayMode == Ink::ArrayMode::Line) {
                        if (m.arrayMode == Ink::ArrayMode::Line) {
                            static const char* kLine[] = { "Relative",
                                                           "Offset",
                                                           "Endpoint" };
                            int lm = (int)m.lineMode;
                            if (pr::ButtonGroupRow("Line mode", kLine, 3, &lm)) {
                                m.lineMode = (Ink::ArrayLineMode)lm;
                                structural = true; structLabel = "Array Line Mode";
                            }
                        }
                        const bool endpoint =
                            m.arrayMode == Ink::ArrayMode::Line &&
                            m.lineMode == Ink::ArrayLineMode::Endpoint;
                        const bool relative =
                            m.arrayMode == Ink::ArrayMode::Line &&
                            m.lineMode == Ink::ArrayLineMode::Relative;
                        float st[2] = { (float)m.step.tx, (float)m.step.ty };
                        bool dx = false, dy = false;
                        if (pr::Vec2Group(endpoint ? "End point"
                                          : relative ? "Factor" : "Step",
                                          st, relative ? 0.01f : 0.5f,
                                          0.0f, 0.0f, relative ? 3 : 2, "",
                                          &dx, &dy, true,
                                          relative ? pr::Quantity::Scalar
                                                   : pr::Quantity::Length)) {
                            m.step.tx = st[0]; m.step.ty = st[1];
                            liveApply("Array Step", false);
                        }
                        if (dx || dy) liveApply("Array Step", true);

                        pr::GroupGap();
                        float rotDeg = (float)(m.step.rotation / kDeg);
                        if (pr::DragFloat("Rotation", &rotDeg, 0.5f, -3600.0f,
                                          3600.0f, 1, "", pr::Quantity::Angle)) {
                            m.step.rotation = rotDeg * kDeg;
                            liveApply("Array Rotation", false);
                        }
                        dragCommit("Array Rotation");

                        float sc[2] = { (float)m.step.sx, (float)m.step.sy };
                        if (pr::Vec2Group("Scale", sc, 0.005f, 0.0f, 0.0f, 3,
                                          "", &dx, &dy)) {
                            m.step.sx = sc[0]; m.step.sy = sc[1];
                            liveApply("Array Scale", false);
                        }
                        if (dx || dy) liveApply("Array Scale", true);

                    } else {   // Circle
                        static const char* kMeth[] = { "Count", "Angle" };
                        int meth = (int)m.circleMethod;
                        if (pr::ButtonGroupRow("Method", kMeth, 2, &meth)) {
                            m.circleMethod = (Ink::ArrayCircleMethod)meth;
                            structural = true; structLabel = "Array Method";
                        }
                        if (byAngle) {
                            float stepDeg =
                                (float)(m.circleAngleStep / kDeg);
                            if (pr::DragFloat("Angle step", &stepDeg, 0.5f,
                                              0.1f, 360.0f, 1, "", pr::Quantity::Angle)) {
                                m.circleAngleStep = stepDeg * kDeg;
                                liveApply("Array Angle Step", false);
                            }
                            dragCommit("Array Angle Step");
                        }
                        float radius = (float)m.circleRadius;
                        if (pr::DragFloat("Radius", &radius, 0.5f, 0.0f,
                                          1000000.0f, 1, "", pr::Quantity::Length)) {
                            m.circleRadius = radius;
                            liveApply("Array Radius", false);
                        }
                        dragCommit("Array Radius");

                        pr::GroupGap();
                        bool arc = m.circleArc;
                        if (pr::CheckRow("Arc", &arc)) {
                            m.circleArc = arc;
                            structural = true; structLabel = "Array Arc";
                        }
                        if (m.circleArc) {
                            float sweepDeg =
                                (float)(m.circleSweep / kDeg);
                            if (pr::DragFloat("Sweep", &sweepDeg, 0.5f, 1.0f,
                                              360.0f, 1, "", pr::Quantity::Angle)) {
                                m.circleSweep = sweepDeg * kDeg;
                                liveApply("Array Sweep", false);
                            }
                            dragCommit("Array Sweep");
                        }
                        bool alignR = m.circleAlign;
                        if (pr::CheckRow("Align rotation", &alignR)) {
                            m.circleAlign = alignR;
                            structural = true; structLabel = "Array Align";
                        }
                    }
                } else if (m.kind == Ink::ModifierKind::AlongPath) {
                    // The modifier lives on THIS path; it distributes either a
                    // PRIMITIVE shape or instances of the picked OBJECT along
                    // the path's own spine (Blender's rule) — groups, sides,
                    // inclination, add/cut like the stroke repeats (but NEVER
                    // mark-aware).
                    static const char* kContent[] = {
                        "Object", "Circle", "Rectangle", "Diamond",
                        "Triangle", "Half Circle" };
                    const Ink::MarkShape kContentShape[] = {
                        Ink::MarkShape::Instance, Ink::MarkShape::Circle,
                        Ink::MarkShape::Rectangle, Ink::MarkShape::Diamond,
                        Ink::MarkShape::Triangle, Ink::MarkShape::HalfCircle };
                    int content = 0;
                    for (int ci = 1; ci < 6; ++ci)
                        if (m.alongShape == kContentShape[ci]) content = ci;
                    if (pr::DropdownRow("Content", kContent, 6, &content)) {
                        m.alongShape = kContentShape[content];
                        structural = true; structLabel = "Along Content";
                    }
                    if (m.alongShape == Ink::MarkShape::Instance) {
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
                        float ascl = (float)m.alongScale;
                        if (pr::DragFloat("Scale", &ascl, 0.01f, 0.001f,
                                          1000.0f, 3, "\xC3\x97")) {
                            m.alongScale = ascl; liveApply("Along Scale", false);
                        }
                        dragCommit("Along Scale");
                    } else {
                        float asz = (float)m.alongSize;
                        if (pr::DragFloat("Length", &asz, 0.2f, 0.01f,
                                          100000.0f, 2, "", pr::Quantity::Length)) {
                            m.alongSize = asz;
                            liveApply("Along Size", false);
                        }
                        dragCommit("Along Size");
                        if (m.alongShape == Ink::MarkShape::Rectangle ||
                            m.alongShape == Ink::MarkShape::Triangle) {
                            float awd = (float)m.alongWidth;
                            if (pr::DragFloat("Width", &awd, 0.2f, 0.01f,
                                              100000.0f, 2, "", pr::Quantity::Length)) {
                                m.alongWidth = awd;
                                liveApply("Along Width", false);
                            }
                            dragCommit("Along Width");
                        }
                        static const char* kAMode[] = { "Add", "Blend", "Cut" };
                        int amode = (int)m.alongMode;
                        if (pr::ButtonGroupRow("Mode", kAMode, 3, &amode)) {
                            m.alongMode = (Ink::MarkObjectMode)amode;
                            structural = true; structLabel = "Along Mode";
                        }
                        if (m.alongMode != Ink::MarkObjectMode::Subtract) {
                            bool crel = false;
                            if (pr::ColorRow("Colour", &m.alongColor, true,
                                             &crel))
                                liveApply("Along Colour", false);
                            if (crel) liveApply("Along Colour", true);
                        }
                        float aop = m.alongOpacity;
                        if (pr::DragFloat("Opacity", &aop, 0.5f, 0.0f, 1.0f,
                                          0, "", pr::Quantity::Percent)) {
                            m.alongOpacity = aop;
                            liveApply("Along Opacity", false);
                        }
                        dragCommit("Along Opacity");
                    }
                    static const char* kDist[] = { "Count", "Spacing",
                                                   "Anchors", "Gap",
                                                   "Density" };
                    int dist = (int)m.distribute;
                    if (pr::DropdownRow("Distribute along path", kDist, 5,
                                        &dist)) {
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
                        if (pr::DragFloat("Spacing c-c", &sp, 0.2f, 0.01f,
                                          100000.0f, 2, "", pr::Quantity::Length)) {
                            m.spacing = sp;
                            liveApply("Along Spacing", false);
                        }
                        dragCommit("Along Spacing");
                    } else if (m.distribute == Ink::AlongDistribute::ByGap) {
                        float gp = (float)m.alongGap;
                        if (pr::DragFloat("Gap edge-edge", &gp, 0.2f, 0.0f,
                                          100000.0f, 2, "", pr::Quantity::Length)) {
                            m.alongGap = gp;
                            liveApply("Along Gap", false);
                        }
                        dragCommit("Along Gap");
                    } else if (m.distribute == Ink::AlongDistribute::ByDensity) {
                        float dv = (float)m.alongDensity;
                        if (pr::DragFloat("Per 100 units", &dv, 0.2f, 0.01f,
                                          1000.0f, 2)) {
                            m.alongDensity = dv;
                            liveApply("Along Density", false);
                        }
                        dragCommit("Along Density");
                    }
                    if (m.distribute != Ink::AlongDistribute::AtAnchors) {
                        float aph = (float)m.alongPhase;
                        if (pr::DragFloat("Phase", &aph, 0.2f, -100000.0f,
                                          100000.0f, 2, "", pr::Quantity::Length)) {
                            m.alongPhase = aph;
                            liveApply("Along Phase", false);
                        }
                        dragCommit("Along Phase");
                        int gc = m.alongGroupCount;
                        if (pr::DragInt("Group size", &gc, 0.1f, 1, 64)) {
                            m.alongGroupCount = gc;
                            liveApply("Along Group", false);
                        }
                        dragCommit("Along Group");
                        if (m.alongGroupCount > 1) {
                            float gp2 = (float)m.alongGroupPitch;
                            if (pr::DragFloat("Group c-c", &gp2, 0.1f, 0.01f,
                                              100000.0f, 2, "", pr::Quantity::Length)) {
                                m.alongGroupPitch = gp2;
                                liveApply("Along Group Pitch", false);
                            }
                            dragCommit("Along Group Pitch");
                        }
                    }
                    static const char* kSide[] = { "Center", "Left", "Right",
                                                   "Inside", "Outside" };
                    int aside = (int)m.alongSide;
                    if (pr::DropdownRow("Side", kSide, 5, &aside)) {
                        const bool wasCenter =
                            m.alongSide == Ink::RepeatSide::Center;
                        m.alongSide = (Ink::RepeatSide)aside;
                        // First time OFF Center → 50 % default.
                        if (wasCenter && m.alongSide != Ink::RepeatSide::Center
                            && std::abs(m.alongSideOffset) < 1e-9) {
                            m.alongOffsetPercent = true;
                            m.alongSideOffset = 50.0;
                        }
                        structural = true; structLabel = "Along Side";
                    }
                    if (m.alongSide != Ink::RepeatSide::Center) {
                        float aoff = (float)m.alongSideOffset;
                        // Percent mode stores a percentage NUMBER (a plain "%"
                        // scalar); absolute mode is a document length.
                        if (pr::DragFloat(m.alongOffsetPercent ? "Offset %"
                                                               : "Offset",
                                          &aoff, 0.2f, -100000.0f,
                                          100000.0f, 2,
                                          m.alongOffsetPercent ? "%" : "",
                                          m.alongOffsetPercent
                                              ? pr::Quantity::Scalar
                                              : pr::Quantity::Length)) {
                            m.alongSideOffset = aoff;
                            liveApply("Along Offset", false);
                        }
                        dragCommit("Along Offset");
                        bool aopct = m.alongOffsetPercent;
                        if (pr::CheckRow("Offset in %", &aopct)) {
                            m.alongOffsetPercent = aopct;
                            structural = true; structLabel = "Along Offset Unit";
                        }
                    }
                    float arot =
                        (float)(m.alongRotation * 180.0 / 3.14159265358979);
                    if (pr::DragFloat("Incline", &arot, 0.5f, -360.0f, 360.0f,
                                      1, "", pr::Quantity::Angle)) {
                        m.alongRotation = arot * 3.14159265358979 / 180.0;
                        liveApply("Along Incline", false);
                    }
                    dragCommit("Along Incline");
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
                                          1000000.0f, 1, "", pr::Quantity::Length)) {
                            m.startTrim = t0;
                            liveApply("Along Trim", false);
                        }
                        dragCommit("Along Trim");
                        if (pr::DragFloat("Trim end", &t1, 0.5f, 0.0f,
                                          1000000.0f, 1, "", pr::Quantity::Length)) {
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

        // Which of the ORIGINAL's transform components this instance keeps
        // copying LIVE. Default: none — the link is the edit-mode data; the
        // object transform was merely copied once at Alt+D.
        pr::GroupGap();
        bool cl = n->instCopyLoc, cr = n->instCopyRot, cs = n->instCopyScale;
        auto commitCopy = [&](const char* label) {
            const bool bl = n->instCopyLoc, br = n->instCopyRot,
                       bs = n->instCopyScale;
            doc.SetInstanceTransformCopy(id, cl, cr, cs);
            PushDocCommand(label,
                [id, bl, br, bs](Ink::Document& d) {
                    d.SetInstanceTransformCopy(id, bl, br, bs);
                },
                [id, cl, cr, cs](Ink::Document& d) {
                    d.SetInstanceTransformCopy(id, cl, cr, cs);
                });
            LogInfoAction(label);
        };
        if (pr::CheckRow("Copy location", &cl))
            commitCopy("Instance Copy Location");
        if (pr::CheckRow("Copy rotation", &cr))
            commitCopy("Instance Copy Rotation");
        if (pr::CheckRow("Copy scale", &cs))
            commitCopy("Instance Copy Scale");
    }
    UI::EndPanel();
}

} // namespace App

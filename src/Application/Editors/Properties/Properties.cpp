#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/ScrollArea.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties editor — the active object's transform + unified style
//  (multi-fill / multi-stroke) on the Ink model (docs/Ink/ROADMAP.md Lot 9).
//  Every field drives the document's typed ops so edits are undoable and the
//  scene recompiles exactly. Continuous edits (a colour or width drag) fold
//  into ONE undo command: the style is captured on the first active frame
//  (propEditBefore_) and committed when the widget is released.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

// linear-straight (document) ↔ sRGB (UI colour pickers).
ImVec4 ToSrgb(const Ink::Color& c) {
    auto s = [](float u) {
        return u <= 0.0031308f ? u * 12.92f
                               : 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
    };
    return { s(c.r), s(c.g), s(c.b), c.a };
}
Ink::Color ToLinear(const ImVec4& c) {
    auto l = [](float u) {
        return u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f);
    };
    return { l(c.x), l(c.y), l(c.z), c.w };
}
} // namespace

// Begin a live style edit (capture the before-state once), returning a mutable
// copy the caller mutates in place; call CommitStyleEdit on release.
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

void Application::PropTransformSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Ink::Transform2D t = n->transform;
    bool changed = false;
    auto row2 = [&](const char* label, double* a, double* b, float speed) {
        float v[2] = { (float)*a, (float)*b };
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat2(label, v, speed)) {
            *a = v[0]; *b = v[1]; changed = true;
        }
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    const bool posDone = row2("Position##pos", &t.tx, &t.ty, 0.5f);
    const bool sclDone = row2("Scale##scl", &t.sx, &t.sy, 0.01f);
    float rotDeg = (float)(t.rotation * 180.0 / 3.14159265358979);
    ImGui::SetNextItemWidth(-1);
    const bool rotChanged = ImGui::DragFloat("Rotation##rot", &rotDeg, 0.5f);
    const bool rotDone = ImGui::IsItemDeactivatedAfterEdit();
    if (rotChanged) { t.rotation = rotDeg * 3.14159265358979 / 180.0; changed = true; }

    if (changed) {
        if (!propEditActive_) {   // capture the before-transform once
            propEditActive_ = true; propEditNode_ = id;
            propEditBefore_.fills.clear(); propEditBefore_.strokes.clear();
            // Stash the transform in a scratch (reuse a NodeOrig-like closure).
            transformBeforeScratch_ = n->transform;
        }
        doc.SetTransform(id, t);
    }
    if ((posDone || sclDone || rotDone) && propEditActive_ && propEditNode_ == id) {
        const Ink::Transform2D before = transformBeforeScratch_;
        const Ink::Transform2D after  = doc.Find(id)->transform;
        PushDocCommand("Transform",
            [id, before](Ink::Document& d) { d.SetTransform(id, before); },
            [id, after](Ink::Document& d)  { d.SetTransform(id, after); });
        propEditActive_ = false; propEditNode_ = Ink::kNullNode;
    }

    // Apply Scale button (Blender's bake — Lot 8 op).
    if (t.sx != 1.0 || t.sy != 1.0) {
        if (ImGui::Button("Apply Scale")) Action_ApplyScale();
    }
}

void Application::PropFillsSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    if (!ImGui::CollapsingHeader("Fills", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Ink::Style style = n->style;   // working copy
    bool structural = false;       // add/remove/toggle → single-shot command
    for (std::size_t i = 0; i < style.fills.size(); ++i) {
        ImGui::PushID((int)(1000 + i));
        Ink::Fill& f = style.fills[i];
        bool enabled = f.enabled;
        if (ImGui::Checkbox("##fen", &enabled)) { f.enabled = enabled; structural = true; }
        ImGui::SameLine();
        ImVec4 col = ToSrgb(f.paint.color);
        if (ImGui::ColorEdit4("##fcol", &col.x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            f.paint.color = ToLinear(col);
            if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id;
                                    propEditBefore_ = n->style; }
            doc.SetStyle(id, style);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
            CommitStyleEdit(id, propEditBefore_, "Fill Colour");
            propEditActive_ = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            style.fills.erase(style.fills.begin() + i); structural = true;
            ImGui::PopID(); break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Fill")) {
        Ink::Fill f; f.paint.color = ToLinear(edit_.defaultFill);
        style.fills.push_back(f); structural = true;
    }
    if (structural) {
        const Ink::Style before = n->style;
        doc.SetStyle(id, style);
        CommitStyleEdit(id, before, "Edit Fills");
    }
}

void Application::PropStrokesSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    if (!ImGui::CollapsingHeader("Strokes", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Ink::Style style = n->style;
    bool structural = false;
    static const char* kAlign[] = { "Center", "Inside", "Outside" };
    static const char* kCap[]   = { "Butt", "Round", "Square" };
    static const char* kJoin[]  = { "Miter", "Round", "Bevel" };

    for (std::size_t i = 0; i < style.strokes.size(); ++i) {
        ImGui::PushID((int)(2000 + i));
        Ink::Stroke& s = style.strokes[i];
        bool enabled = s.enabled;
        if (ImGui::Checkbox("##sen", &enabled)) { s.enabled = enabled; structural = true; }
        ImGui::SameLine();
        ImVec4 col = ToSrgb(s.paint.color);
        if (ImGui::ColorEdit4("##scol", &col.x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            s.paint.color = ToLinear(col);
            if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id;
                                    propEditBefore_ = n->style; }
            doc.SetStyle(id, style);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
            CommitStyleEdit(id, propEditBefore_, "Stroke Colour");
            propEditActive_ = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            style.strokes.erase(style.strokes.begin() + i); structural = true;
            ImGui::PopID(); break;
        }
        // Width (geometry-affecting → one command on release).
        float w = (float)s.width;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("Width##w", &w, 0.1f, 0.0f, 1000.0f, "%.2f")) {
            s.width = w;
            if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id;
                                    propEditBefore_ = n->style; }
            doc.SetStyle(id, style);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
            CommitStyleEdit(id, propEditBefore_, "Stroke Width");
            propEditActive_ = false;
        }
        // Align / cap / join.
        int align = (int)s.align, cap = (int)s.cap, join = (int)s.join;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("Align##al", &align, kAlign, 3)) {
            s.align = (Ink::StrokeAlign)align; structural = true;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("Cap##cap", &cap, kCap, 3)) {
            s.cap = (Ink::CapStyle)cap; structural = true;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("Join##jn", &join, kJoin, 3)) {
            s.join = (Ink::JoinStyle)join; structural = true;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (ImGui::Button("Add Stroke")) {
        Ink::Stroke s; s.paint.color = ToLinear(edit_.defaultStroke);
        s.width = edit_.defaultStrokeWidth;
        style.strokes.push_back(s); structural = true;
    }
    if (structural) {
        const Ink::Style before = n->style;
        doc.SetStyle(id, style);
        CommitStyleEdit(id, before, "Edit Strokes");
    }
}

void Application::RenderProperties() {
    auto& ds = DS::DesignSystem::Instance();
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    edit_.Prune(doc);

    const Ink::NodeId id = edit_.active;
    if (id == Ink::kNullNode || !doc.Find(id)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No active object");
        ImGui::TextUnformatted("Select an object to edit its properties");
        ImGui::PopStyleColor();
        return;
    }

    const Ink::Node* n = doc.Find(id);
    // Header: name + kind.
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Default));
    ImGui::TextUnformatted(n->name.empty() ? "(unnamed)" : n->name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (UI::BeginScroll("##propsScroll", ImVec2(0, 0))) {
        PropTransformSection(id);
        if (n->kind == Ink::NodeKind::Path) {
            PropFillsSection(id);
            PropStrokesSection(id);
        } else if (n->kind == Ink::NodeKind::Group) {
            if (ImGui::CollapsingHeader("Group", ImGuiTreeNodeFlags_DefaultOpen)) {
                float op = n->opacity;
                if (ImGui::SliderFloat("Opacity", &op, 0.0f, 1.0f))
                    doc.SetOpacity(id, op);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    LogInfoAction("Group Opacity");
                bool clip = n->clip;
                if (ImGui::Checkbox("Clip to first child", &clip))
                    doc.SetClip(id, clip);
            }
        }
    }
    UI::EndScroll();
}

} // namespace App

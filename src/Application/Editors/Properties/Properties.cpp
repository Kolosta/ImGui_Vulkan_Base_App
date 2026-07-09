#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/DragValue.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ButtonGroup.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties editor — the active object's transform + unified style, rebuilt
//  to restore the legacy Compositor layout and component set on the Ink model
//  (docs/Ink/ROADMAP.md Lot 9 rework): the 40%-label property-row convention,
//  Blender-style collapsible UI::Panel sections, UI::DragValue numeric fields,
//  UI::Checkbox / UI::ButtonGroup / UI::Dropdown controls, and raw ColorEdit for
//  document colours. Continuous drags fold into one undo command (captured on
//  the first edited frame, committed on release).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

constexpr float kLabelFrac = 0.40f;

float PropRowH() {
    auto& ds = DS::DesignSystem::Instance();
    try { return ds.GetFloat(Tok::S_Size_ControlHeight) * ds.GetGlobalScale(); }
    catch (...) { return 22.0f; }
}

// A right-justified label in the left 40%, vertically centred on a ui-unit row.
// Returns the control width to the right. Leaves the cursor at the control.
float PropLabel(const char* text) {
    auto& ds = DS::DesignSystem::Instance();
    const float full = ImGui::GetContentRegionAvail().x;
    const float lblW = full * kLabelFrac;
    const float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    const float rowH = PropRowH();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float ty = origin.y + std::max(0.0f, (rowH - ts.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + lblW - ts.x, ty),
        ImGui::ColorConvertFloat4ToU32(ds.GetColor(Tok::S_Color_Text_Default)), text);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + lblW + pad, origin.y));
    return std::max(40.0f, full - lblW - pad);
}

ImVec4 ToSrgb(const Ink::Color& c) {
    auto s = [](float u) {
        return u <= 0.0031308f ? u * 12.92f : 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
    };
    return { s(c.r), s(c.g), s(c.b), c.a };
}
Ink::Color ToLinear(const ImVec4& c) {
    auto l = [](float u) {
        return u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f);
    };
    return { l(c.x), l(c.y), l(c.z), c.w };
}

// One labelled DragValue row. Returns true while changing.
bool PropDragFloat(const char* label, float* v, float speed, float mn, float mx,
                   int decimals = 3, const char* unit = "") {
    const float w = PropLabel(label);
    UI::DragValueConfig dc;
    dc.id = "##dv"; dc.speed = speed; dc.min = mn; dc.max = mx;
    dc.displayDecimals = decimals; dc.unit = unit; dc.width = w;
    ImGui::PushID(label);
    const bool ch = UI::DragValue(dc, v);
    ImGui::PopID();
    return ch;
}

// A labelled checkbox row (box on the control side).
bool PropCheckRow(const char* label, bool* v) {
    PropLabel(label);
    ImGui::PushID(label);
    const bool ch = UI::CheckboxBox("##cb", v);
    ImGui::PopID();
    return ch;
}

// A labelled dropdown row. Returns the picked index (or -1).
int PropDropdown(const char* label, const char* const* items, int count, int cur) {
    const float w = PropLabel(label);
    (void)w;
    UI::DropdownConfig cfg; cfg.id = "##pdd";
    cfg.triggerLabel = (cur >= 0 && cur < count) ? items[cur] : "";
    for (int i = 0; i < count; ++i) { UI::DropdownItem it; it.label = items[i]; cfg.items.push_back(it); }
    cfg.selectedIndex = cur;
    ImGui::PushID(label);
    UI::DropdownResult r = UI::Dropdown(cfg);
    ImGui::PopID();
    return r.changed ? r.selected : -1;
}
} // namespace

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

// ── Transform panel ───────────────────────────────────────────────────────────

void Application::PropTransformSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    UI::PanelConfig pc; pc.id = "##transform"; pc.label = "Transform"; pc.defaultOpen = true;
    UI::PanelResult pr = UI::BeginPanel(pc);
    if (pr.open) {
        Ink::Transform2D t = n->transform;
        float loc[2] = { (float)t.tx, (float)t.ty };
        float scl[2] = { (float)t.sx, (float)t.sy };
        float rotDeg = (float)(t.rotation * 180.0 / 3.14159265358979);

        // Each field applies live and commits ONE undo command when THAT field
        // is released (IsItemDeactivatedAfterEdit right after its own widget).
        auto field = [&](bool changed, auto apply) {
            if (changed) {
                if (!propEditActive_) {
                    propEditActive_ = true; propEditNode_ = id;
                    transformBeforeScratch_ = n->transform;
                }
                Ink::Transform2D nt = doc.Find(id)->transform;
                apply(nt);
                doc.SetTransform(id, nt);
            }
            if (propEditActive_ && propEditNode_ == id &&
                ImGui::IsItemDeactivatedAfterEdit()) {
                const Ink::Transform2D before = transformBeforeScratch_;
                const Ink::Transform2D after  = doc.Find(id)->transform;
                PushDocCommand("Transform",
                    [id, before](Ink::Document& d) { d.SetTransform(id, before); },
                    [id, after](Ink::Document& d)  { d.SetTransform(id, after); });
                propEditActive_ = false; propEditNode_ = Ink::kNullNode;
            }
        };
        field(PropDragFloat("Location X", &loc[0], 0.5f, 0, 0), [&](Ink::Transform2D& x){ x.tx = loc[0]; });
        field(PropDragFloat("Location Y", &loc[1], 0.5f, 0, 0), [&](Ink::Transform2D& x){ x.ty = loc[1]; });
        field(PropDragFloat("Rotation", &rotDeg, 0.5f, -3600, 3600, 1, "\xC2\xB0"),
              [&](Ink::Transform2D& x){ x.rotation = rotDeg * 3.14159265358979 / 180.0; });
        field(PropDragFloat("Scale X", &scl[0], 0.01f, 0, 0), [&](Ink::Transform2D& x){ x.sx = scl[0]; });
        field(PropDragFloat("Scale Y", &scl[1], 0.01f, 0, 0), [&](Ink::Transform2D& x){ x.sy = scl[1]; });

        if (n->kind == Ink::NodeKind::Path && (t.sx != 1.0 || t.sy != 1.0)) {
            PropLabel("");
            if (ImGui::SmallButton("Apply Scale")) Action_ApplyScale();
        }
    }
    UI::EndPanel();
}

// ── Paint (fills + strokes) ───────────────────────────────────────────────────

void Application::PropFillsSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    UI::PanelConfig pc; pc.id = "##fills"; pc.label = "Fills"; pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::Style style = n->style;
        bool structural = false;
        for (std::size_t i = 0; i < style.fills.size(); ++i) {
            ImGui::PushID((int)(1000 + i));
            Ink::Fill& f = style.fills[i];
            bool enabled = f.enabled;
            char lab[24]; std::snprintf(lab, sizeof lab, "Fill %d", (int)i + 1);
            if (PropCheckRow(lab, &enabled)) { f.enabled = enabled; structural = true; }

            ImVec4 col = ToSrgb(f.paint.color);
            PropLabel("Color");
            if (ImGui::ColorEdit4("##fcol", &col.x,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                f.paint.color = ToLinear(col);
                if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id; propEditBefore_ = n->style; }
                doc.SetStyle(id, style);
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
                CommitStyleEdit(id, propEditBefore_, "Fill Colour"); propEditActive_ = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) { style.fills.erase(style.fills.begin() + i);
                structural = true; ImGui::PopID(); break; }
            ImGui::PopID();
        }
        PropLabel("");
        if (ImGui::SmallButton("+ Fill")) {
            Ink::Fill f; f.paint.color = ToLinear(edit_.defaultFill);
            style.fills.push_back(f); structural = true;
        }
        if (structural) {
            const Ink::Style before = n->style;
            doc.SetStyle(id, style);
            CommitStyleEdit(id, before, "Edit Fills");
        }
    }
    UI::EndPanel();
}

void Application::PropStrokesSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    static const char* kAlign[] = { "Center", "Inner", "Outer" };
    static const char* kCap[]   = { "Butt", "Round", "Square" };
    static const char* kJoin[]  = { "Miter", "Round", "Bevel" };

    UI::PanelConfig pc; pc.id = "##strokes"; pc.label = "Strokes"; pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::Style style = n->style;
        bool structural = false;
        for (std::size_t i = 0; i < style.strokes.size(); ++i) {
            ImGui::PushID((int)(2000 + i));
            Ink::Stroke& s = style.strokes[i];
            bool enabled = s.enabled;
            char lab[24]; std::snprintf(lab, sizeof lab, "Stroke %d", (int)i + 1);
            if (PropCheckRow(lab, &enabled)) { s.enabled = enabled; structural = true; }

            ImVec4 col = ToSrgb(s.paint.color);
            PropLabel("Color");
            if (ImGui::ColorEdit4("##scol", &col.x,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                s.paint.color = ToLinear(col);
                if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id; propEditBefore_ = n->style; }
                doc.SetStyle(id, style);
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
                CommitStyleEdit(id, propEditBefore_, "Stroke Colour"); propEditActive_ = false;
            }

            float w = (float)s.width;
            if (PropDragFloat("Width", &w, 0.1f, 0.0f, 1000.0f, 2)) {
                s.width = w;
                if (!propEditActive_) { propEditActive_ = true; propEditNode_ = id; propEditBefore_ = n->style; }
                doc.SetStyle(id, style);
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
                CommitStyleEdit(id, propEditBefore_, "Stroke Width"); propEditActive_ = false;
            }

            int align = (int)s.align, cap = (int)s.cap, join = (int)s.join;
            if (int r = PropDropdown("Align", kAlign, 3, align); r >= 0) { s.align = (Ink::StrokeAlign)r; structural = true; }
            if (int r = PropDropdown("Cap",   kCap,   3, cap);   r >= 0) { s.cap   = (Ink::CapStyle)r;   structural = true; }
            if (int r = PropDropdown("Join",  kJoin,  3, join);  r >= 0) { s.join  = (Ink::JoinStyle)r;  structural = true; }
            if (s.join == Ink::JoinStyle::Miter) {
                float ml = (float)s.miterLimit;
                if (PropDragFloat("Miter limit", &ml, 0.1f, 1.0f, 100.0f, 1)) { s.miterLimit = ml; structural = true; }
            }
            ImGui::PushID("rm");
            PropLabel("");
            if (ImGui::SmallButton("Remove")) { style.strokes.erase(style.strokes.begin() + i);
                structural = true; ImGui::PopID(); ImGui::PopID(); break; }
            ImGui::PopID();
            ImGui::Separator();
            ImGui::PopID();
        }
        PropLabel("");
        if (ImGui::SmallButton("+ Stroke")) {
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
    UI::EndPanel();
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderProperties() {
    auto& ds = DS::DesignSystem::Instance();
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    edit_.Prune(doc);

    const Ink::NodeId id = edit_.active;
    if (id == Ink::kNullNode || !doc.Find(id)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No active object.");
        ImGui::TextUnformatted("(select an object in the Viewport or Outliner)");
        ImGui::PopStyleColor();
        return;
    }
    const Ink::Node* n = doc.Find(id);

    static const char* kBlend[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
        "Difference", "Exclusion", "Erase" };
    const int kBlendCount = (int)(sizeof(kBlend) / sizeof(kBlend[0]));

    if (UI::BeginScroll("##propsScroll", ImVec2(0, 0))) {
        // Name.
        {
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof nameBuf, "%s", n->name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##objname", nameBuf, sizeof nameBuf,
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                Action_RenameNode(id, nameBuf);
        }

        // Type (read-only).
        {
            const char* kind = n->kind == Ink::NodeKind::Group ? "Group"
                             : n->kind == Ink::NodeKind::Instance ? "Instance" : "Path";
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::Text("Type: %s", kind);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        // Transform.
        PropTransformSection(id);

        if (n->kind == Ink::NodeKind::Group) {
            UI::PanelConfig gc; gc.id = "##group"; gc.label = "Group"; gc.defaultOpen = true;
            if (UI::BeginPanel(gc).open) {
                float op = n->opacity;
                if (PropDragFloat("Opacity", &op, 0.005f, 0.0f, 1.0f, 3)) doc.SetOpacity(id, op);
                if (ImGui::IsItemDeactivatedAfterEdit()) LogInfoAction("Group Opacity");
                int blend = std::min((int)n->blend, kBlendCount - 1);
                if (int r = PropDropdown("Blend", kBlend, kBlendCount, blend); r >= 0)
                    doc.SetBlend(id, (Ink::BlendMode)r);
                bool clip = n->clip;
                if (PropCheckRow("Clip", &clip)) doc.SetClip(id, clip);
            }
            UI::EndPanel();
        } else if (n->kind == Ink::NodeKind::Path) {
            // Paint panel (fills + strokes).
            UI::PanelConfig pc; pc.id = "##paint"; pc.label = "Paint"; pc.defaultOpen = true;
            if (UI::BeginPanel(pc).open) {
                PropFillsSection(id);
                PropStrokesSection(id);
            }
            UI::EndPanel();
        }
    }
    UI::EndScroll();
}

} // namespace App

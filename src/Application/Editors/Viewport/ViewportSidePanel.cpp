#include "Application.h"
#include "PropertiesRows.h"
#include <UI/Widgets/SidePanel.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport side panel (the reusable UI::EditorSidePanel — the "N" panel).
//
//    • Item  — the active object's Location / Rotation / Scale / Dimensions
//              (Blender's N-panel Item tab). Always present.
//    • Marks — the ACTIVE selected line mark's info. Only in Line-Mark mode
//              (the mark editors moved OUT of the stroke Properties menu here).
//
//  The panel chrome (stages, tab bar, drag, transparency) lives in the widget;
//  this file only supplies the editor-specific tab bodies. Each body opens a
//  child window over the given content rect so it can use the shared pr:: rows.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// The tab body draws at the FLOW of the scrollable child the side-panel widget
// already opened (with padding + a scrollbar when the content is tall), so the
// pr:: rows lay out with the usual label/column split. `id`/`cMin`/`cMax` are
// kept for signature symmetry; the rect is degenerate-checked by the widget.
static bool BeginTabChild(const char*, ImVec2, ImVec2) { return true; }
static void EndTabChild() {}

// ── Item tab ────────────────────────────────────────────────────────────────
// Location / Rotation / Scale (edited live, one undo command per drag) plus the
// active object's world Dimensions (read-only, from the rendered bounds).
void Application::DrawSidePanelItemTab(ImVec2 cMin, ImVec2 cMax) {
    if (!BeginTabChild("##itemTab", cMin, cMax)) return;
    if (!project_.document || edit_.active == Ink::kNullNode) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            pr::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
        ImGui::TextUnformatted("No active object.");
        ImGui::PopStyleColor();
        EndTabChild();
        return;
    }
    Ink::Document& doc = *project_.document;
    const Ink::NodeId id = edit_.active;
    const Ink::Node* n = doc.Find(id);
    if (!n) { EndTabChild(); return; }

    Ink::Transform2D t = n->transform;
    float loc[2] = { (float)t.tx, (float)t.ty };
    float scl[2] = { (float)t.sx, (float)t.sy };
    float rotDeg = (float)(t.rotation * 180.0 / 3.14159265358979);

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
    // First group in the panel → no leading gap (the panel's own top margin
    // provides the inset), so the top padding matches the other tabs exactly.
    unsigned ch = pr::Vec2Group("Location", loc, 0.5f, 0.0f, 0.0f, 3, "", &dx, &dy,
                                /*leadingGap=*/false);
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

    // Dimensions — the active object's world bounding-box size (read-only).
    Ink::DRect b;
    if (ink_ && ink_->NodeBounds(id, b) && b.valid) {
        float dim[2] = { (float)(b.max.x - b.min.x), (float)(b.max.y - b.min.y) };
        ImGui::BeginDisabled(true);
        bool ddx = false, ddy = false;
        pr::Vec2Group("Dimensions", dim, 0.0f, 0.0f, 0.0f, 2, "", &ddx, &ddy);
        ImGui::EndDisabled();
    }
    EndTabChild();
}

// ── Marks tab (Line-Mark mode only) ─────────────────────────────────────────
// The ACTIVE selected mark's key fields. When several are selected only the
// active (last-selected) one is shown, per the design. Full per-object editing
// still lives in the mark tool; this is the quick inspector.
void Application::DrawSidePanelMarksTab(ImVec2 cMin, ImVec2 cMax) {
    if (!BeginTabChild("##marksTab", cMin, cMax)) return;
    auto subtle = [&](const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            pr::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    };
    if (!project_.document || edit_.markSel.empty()) {
        subtle("No mark selected.");
        EndTabChild();
        return;
    }
    Ink::Document& doc = *project_.document;
    // The ACTIVE mark = the last one selected.
    const EditContext::MarkRef ref = edit_.markSel.back();
    const Ink::Node* n = doc.Find(ref.node);
    if (!n || ref.stroke < 0 || ref.stroke >= (int)n->style.strokes.size()) {
        subtle("Mark unavailable.");
        EndTabChild();
        return;
    }
    const auto& marks = n->style.strokes[(std::size_t)ref.stroke].marks;
    if (ref.index < 0 || ref.index >= (int)marks.size()) {
        subtle("Mark unavailable.");
        EndTabChild();
        return;
    }

    if (edit_.markSel.size() > 1) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%d marks selected (showing active)",
                      (int)edit_.markSel.size());
        subtle(buf);
        pr::GroupGap();
    }

    // The full editor for the active mark (position/side/phase + objects).
    DrawMarkEditor(ref.node, ref.stroke, ref.index);
    EndTabChild();
}

// ── Panel host ──────────────────────────────────────────────────────────────
void Application::RenderViewportSidePanel(EditorState& st, ImVec2 cMin, ImVec2 cMax) {
    std::vector<UI::SidePanelTab> tabs;
    // Item — always available.
    {
        UI::SidePanelTab item; item.name = "Item";
        item.draw = [this](ImVec2 conMin, ImVec2 conMax) {
            DrawSidePanelItemTab(conMin, conMax);
        };
        tabs.push_back(std::move(item));
    }
    // Marks — only in Line-Mark mode.
    if (MarkModeActive()) {
        UI::SidePanelTab marks; marks.name = "Marks";
        marks.draw = [this](ImVec2 conMin, ImVec2 conMax) {
            DrawSidePanelMarksTab(conMin, conMax);
        };
        tabs.push_back(std::move(marks));
    }
    // Keep the active tab in range as tabs appear/disappear with the mode.
    if (st.sidePanel.tab >= (int)tabs.size())
        st.sidePanel.tab = 0;
    // "N" toggles the full panel open/closed (Blender's N panel), when this
    // viewport is hovered and no text field is being typed into.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_N, false) &&
        !ImGui::GetIO().WantTextInput)
        st.sidePanel.stage = (st.sidePanel.stage == 2) ? 0 : 2;
    UI::EditorSidePanel("##viewportSide", cMin, cMax, st.sidePanel, tabs);
    // Exclude the panel's interactive rects from the canvas hit-testing (input +
    // the mark tool + the custom cursor read st.overlayRects). The widget
    // publishes what it ACTUALLY occupies — the full-height tab-bar column (or
    // the closed handle) and the height-FITTED content panel + resize grip —
    // so the canvas below the auto-fitted panel stays fully interactive.
    auto pushRect = [&](const ImVec4& r) {
        if (r.z > r.x && r.w > r.y) st.overlayRects.push_back(r);
    };
    pushRect(st.sidePanel.outBarRect);
    pushRect(st.sidePanel.outPanelRect);
}

} // namespace App

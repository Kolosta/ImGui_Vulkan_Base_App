#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/Panel.h>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Stroke editor & Fill editor — two dedicated editors showing the SAME stack
//  UI as the Properties Paint page (DrawStrokesStackBody / DrawFillsStackBody),
//  with different apply semantics:
//    • edits apply to the WHOLE SELECTION (every selected path node), not just
//      the active object — one undo command per gesture;
//    • with NOTHING selected they edit the DEFAULT style: the stacks used for
//      NEW objects. The default mirrors the active object while one is
//      selected, and PERSISTS after deselection (the last style stays shown
//      and editable).
//  The top-bar swatches (DrawDefaultColorSwatches) are a mini-view of the same
//  state: the FIRST fill / stroke of these stacks.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

// Mirror the active path node's stacks into the defaults, so deselecting keeps
// the last style displayed (and used for new objects). Idempotent, called by
// the editors and the top-bar swatches every frame they draw.
void Application::SyncDefaultStyleFromActive() {
    if (!project_.document) return;
    const Ink::Node* n = project_.document->Find(edit_.active);
    if (n && n->kind == Ink::NodeKind::Path) {
        edit_.defaultFills   = n->style.fills;
        edit_.defaultStrokes = n->style.strokes;
        // Marks are PER-OBJECT annotations — the default style never carries
        // them (a new object must not inherit the active object's marks, and
        // a selection-wide apply must not overwrite each node's own marks).
        for (Ink::Stroke& s : edit_.defaultStrokes) s.marks.clear();
    }
}

// Every selected PATH node (the targets of a selection-wide style edit).
std::vector<Ink::NodeId> Application::SelectedPathNodes() const {
    std::vector<Ink::NodeId> out;
    if (!project_.document) return out;
    for (Ink::NodeId id : edit_.selection)
        if (const Ink::Node* n = project_.document->Find(id);
            n && n->kind == Ink::NodeKind::Path)
            out.push_back(id);
    return out;
}

namespace {
// Shared apply: push the default fills OR strokes onto every selected path
// node, folding a whole drag into ONE undo command (befores captured at
// gesture start, command pushed on release). Nothing selected → default-only
// edit, no document change, no undo.
void ApplyPaintEdit(Application& app, Ink::Document& doc,
                    bool strokes, const char* label, bool released,
                    bool& active,
                    std::vector<std::pair<Ink::NodeId, Ink::Style>>& before,
                    const std::vector<Ink::Fill>& fills,
                    const std::vector<Ink::Stroke>& strokesList,
                    const std::vector<Ink::NodeId>& sel,
                    const std::function<void(const std::string&,
                                             std::function<void(Ink::Document&)>,
                                             std::function<void(Ink::Document&)>)>& push) {
    if (sel.empty()) { active = false; before.clear(); return; }
    if (!active) {
        active = true;
        before.clear();
        for (Ink::NodeId id : sel)
            if (const Ink::Node* n = doc.Find(id))
                before.push_back({ id, n->style });
    }
    for (Ink::NodeId id : sel)
        if (const Ink::Node* n = doc.Find(id)) {
            Ink::Style s = n->style;
            if (strokes) {
                // Replace the stroke STYLE but keep each node's own MARKS
                // (per-object annotations, matched by stroke index) — a
                // selection-wide style edit must never wipe them.
                std::vector<Ink::Stroke> ns = strokesList;
                for (std::size_t i = 0;
                     i < ns.size() && i < s.strokes.size(); ++i)
                    ns[i].marks = s.strokes[i].marks;
                s.strokes = std::move(ns);
            } else {
                s.fills = fills;
            }
            doc.SetStyle(id, s);
        }
    if (released && active) {
        std::vector<std::pair<Ink::NodeId, Ink::Style>> b = before, a;
        for (const auto& p : b)
            if (const Ink::Node* n = doc.Find(p.first))
                a.push_back({ p.first, n->style });
        push(label,
             [b](Ink::Document& d) { for (const auto& p : b) d.SetStyle(p.first, p.second); },
             [a](Ink::Document& d) { for (const auto& p : a) d.SetStyle(p.first, p.second); });
        active = false;
        before.clear();
    }
    (void)app;
}
} // namespace

void Application::ApplyDefaultFillsEdit(const char* label, bool released) {
    if (!project_.document) return;
    ApplyPaintEdit(*this, *project_.document, /*strokes=*/false, label, released,
                   paintEdActive_, paintEdBefore_,
                   edit_.defaultFills, edit_.defaultStrokes, SelectedPathNodes(),
                   [this](const std::string& l,
                          std::function<void(Ink::Document&)> u,
                          std::function<void(Ink::Document&)> r) {
                       PushDocCommand(l, std::move(u), std::move(r));
                       LogInfoAction(l);
                   });
}

void Application::ApplyDefaultStrokesEdit(const char* label, bool released) {
    if (!project_.document) return;
    ApplyPaintEdit(*this, *project_.document, /*strokes=*/true, label, released,
                   paintEdActive_, paintEdBefore_,
                   edit_.defaultFills, edit_.defaultStrokes, SelectedPathNodes(),
                   [this](const std::string& l,
                          std::function<void(Ink::Document&)> u,
                          std::function<void(Ink::Document&)> r) {
                       PushDocCommand(l, std::move(u), std::move(r));
                       LogInfoAction(l);
                   });
}

namespace {
// The subtle "what am I editing" caption at the top of both editors.
void DrawTargetCaption(std::size_t selCount) {
    ImGui::PushStyleColor(ImGuiCol_Text,
        pr::SafeColor(pr::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
    if (selCount == 0)
        ImGui::TextUnformatted("No selection \xE2\x80\x94 editing the style "
                               "for NEW objects");
    else if (selCount == 1)
        ImGui::TextUnformatted("Editing the selected object");
    else
        ImGui::Text("Editing %d selected objects", (int)selCount);
    ImGui::PopStyleColor();
}
} // namespace

// ── Stroke editor ────────────────────────────────────────────────────────────

void Application::RenderStrokeEditor(EditorState& st) {
    (void)st;
    if (!project_.document) return;
    SyncDefaultStyleFromActive();
    const std::vector<Ink::NodeId> sel = SelectedPathNodes();

    // Working style: the default stacks (already mirroring the active object
    // when there is one).
    Ink::Style style;
    style.strokes = edit_.defaultStrokes;
    bool structural = false;
    const char* structLabel = "Edit Strokes";
    auto apply = [&](const char* label, bool released) {
        edit_.defaultStrokes = style.strokes;
        ApplyDefaultStrokesEdit(label, released);
    };

    DrawTargetCaption(sel.size());
    UI::PanelConfig pc; pc.id = "##strokesEd"; pc.label = "Strokes";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open)
        DrawStrokesStackBody(style, strokeEdSel_, apply, structural, structLabel);
    UI::EndPanel();
    if (structural) apply(structLabel, true);
}

// ── Fill editor ──────────────────────────────────────────────────────────────

void Application::RenderFillEditor(EditorState& st) {
    (void)st;
    if (!project_.document) return;
    SyncDefaultStyleFromActive();
    const std::vector<Ink::NodeId> sel = SelectedPathNodes();

    Ink::Style style;
    style.fills = edit_.defaultFills;
    bool structural = false;
    const char* structLabel = "Edit Fills";
    auto apply = [&](const char* label, bool released) {
        edit_.defaultFills = style.fills;
        ApplyDefaultFillsEdit(label, released);
    };

    // The pattern preview / eyedropper target: the active path node when the
    // edit applies to a selection; none when editing the default style.
    const Ink::Node* an = project_.document->Find(edit_.active);
    const Ink::NodeId previewNode =
        (an && an->kind == Ink::NodeKind::Path) ? edit_.active : Ink::kNullNode;

    DrawTargetCaption(sel.size());
    UI::PanelConfig pc; pc.id = "##fillsEd"; pc.label = "Fills";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open)
        DrawFillsStackBody(style, previewNode, fillEdSel_, apply,
                           structural, structLabel);
    UI::EndPanel();
    if (structural) apply(structLabel, true);
}

} // namespace App

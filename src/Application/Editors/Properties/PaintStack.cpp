#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/ScrollArea.h>
#include <imgui.h>

#include <cstdio>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  The PAINT STACK — fills and strokes as the ONE list they actually are.
//
//  A shape paints its pieces in a single sequence. The Fill and Stroke rails
//  each order their own kind, but nothing let a stroke sit UNDER a fill, and a
//  blend or erase piece only means something relative to what is beneath it —
//  so the two had to become one ordered stack (Ink::Style::PaintOrder).
//
//  This is the shared rail behind both surfaces: the "Paint order" panel in
//  Properties ▸ Paint, and the "Strokes & Fills" editor. Top of the list is the
//  top of the paint, like every other stack in the app; the model runs the other
//  way (index 0 paints first, underneath), so only the DISPLAY is flipped.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
constexpr float kThumb   = 40.0f;   // vignette side, × global scale
constexpr float kRailGap = 3.0f;    // vertical gap between vignettes, × gs
}  // namespace

void Application::DrawPaintStackRail(Ink::NodeId id) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    auto& ds = pr::DST::DesignSystem::Instance();
    auto subtle = [&](const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ds.GetColor(pr::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
    };
    if (!n || n->kind != Ink::NodeKind::Path) {
        subtle("No active shape.");
        return;
    }

    Ink::Style style = n->style;
    std::vector<Ink::Style::PaintRef> stack = style.PaintOrder();
    const int nP = (int)stack.size();
    if (nP == 0) {
        subtle("This shape has no fills or strokes.");
        return;
    }

    const float gs = pr::Gs();
    const float thumb = kThumb * gs;
    const float cellH = thumb + kRailGap * gs;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float textY = (thumb - ImGui::GetTextLineHeight()) * 0.5f;
    const ImU32 label = ImGui::ColorConvertFloat4ToU32(
        ds.GetColor(pr::Tok::S_Color_Text_Default));
    const ImU32 dim = ImGui::ColorConvertFloat4ToU32(
        ds.GetColor(pr::Tok::S_Color_Text_Subtle));

    pr::VReorder rr("##paintStack", nP, cellH);
    const int grabbed = rr.Grabbed();
    // Row 0 is the TOP of the paint; the model's last entry paints last.
    auto dataOf = [nP](int row) { return nP - 1 - row; };

    auto drawSample = [&](const Ink::Style::PaintRef& ref, ImDrawList* dl,
                          ImVec2 cmn, ImVec2 cmx) {
        if (ref.isStroke) {
            pr::DrawStrokeSample(dl, cmn, cmx, style.strokes[ref.index]);
        } else {
            const Ink::Fill& f = style.fills[ref.index];
            if (f.kind != Ink::FillKind::Pattern ||
                !PaintPatternSwatch(dl, cmn, cmx, f))
                pr::DrawFillSample(dl, cmn, cmx, f);
        }
    };

    for (int i = 0; i < nP; ++i) {
        const int di = dataOf(i);
        const Ink::Style::PaintRef& ref = stack[(std::size_t)di];
        ImGui::PushID(i);
        const bool isGrab = (i == grabbed);
        float posY = origin.y + (float)i * cellH + rr.CellOffset(i);
        if (isGrab) posY = rr.GrabbedScreenY(origin.y);
        const ImVec2 pos(origin.x, posY);
        ImVec2 cmn, cmx;
        char tid[16];
        std::snprintf(tid, sizeof tid, "p%d", di);
        pr::ThumbTile(tid, thumb, false, &cmn, &cmx, &pos);
        rr.HandleCell(i, ImGui::IsItemActivated(), ImGui::IsItemActive(),
                      origin.y + (float)i * cellH, origin.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!isGrab) {
            drawSample(ref, dl, cmn, cmx);
            // What the piece IS, and where it sits — the rank reads top-down so
            // it matches the list, not the model's bottom-up index.
            char txt[48];
            std::snprintf(txt, sizeof txt, "%s %d",
                          ref.isStroke ? "Stroke" : "Fill", ref.index + 1);
            dl->AddText(ImVec2(pos.x + thumb + 8.0f * gs, pos.y + textY),
                        label, txt);
            const float tw = ImGui::CalcTextSize(txt).x;
            char rank[32];
            std::snprintf(rank, sizeof rank, "%d / %d", i + 1, nP);
            dl->AddText(ImVec2(pos.x + thumb + 16.0f * gs + tw, pos.y + textY),
                        dim, rank);
        }
        ImGui::PopID();
    }
    // The grabbed tile's sample last, on the foreground list, so it floats over
    // the rows it is passing.
    if (grabbed >= 0 && grabbed < nP) {
        const float posY = rr.GrabbedScreenY(origin.y);
        const float ins = 3.0f * gs;
        drawSample(stack[(std::size_t)dataOf(grabbed)],
                   ImGui::GetForegroundDrawList(),
                   ImVec2(origin.x + ins, posY + ins),
                   ImVec2(origin.x + thumb - ins, posY + thumb - ins));
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (float)nP * cellH));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    pr::VReorder::Move mv = rr.Commit();
    if (mv.from < 0 || mv.to < 0 || mv.from == mv.to) return;
    const int from = dataOf(mv.from), to = dataOf(mv.to);
    if (from < 0 || from >= nP || to < 0 || to >= nP) return;
    const Ink::Style::PaintRef moved = stack[(std::size_t)from];
    stack.erase(stack.begin() + from);
    stack.insert(stack.begin() + to, moved);
    // Stamp the new arrangement as explicit ranks, so it survives any later
    // insertion (a fresh piece lands at 0 and would otherwise jump to the
    // bottom of the stack).
    int rank = 0;
    for (const Ink::Style::PaintRef& r : stack) {
        if (r.isStroke) style.strokes[r.index].order = rank++;
        else            style.fills[r.index].order   = rank++;
    }
    const Ink::Style before = n->style;
    doc.SetStyle(id, style);
    CommitStyleEdit(id, before, "Reorder Paint");
}

// The third panel of Properties ▸ Paint, under Fills and Strokes.
void Application::PropPaintOrderSection(Ink::NodeId id) {
    UI::PanelConfig pc;
    pc.id = "##paintOrder";
    pc.label = "Paint order";
    pc.defaultOpen = false;
    if (UI::BeginPanel(pc).open) {
        auto& ds = pr::DST::DesignSystem::Instance();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ds.GetColor(pr::Tok::S_Color_Text_Subtle));
        ImGui::TextWrapped("Fills and strokes in one stack — drag a vignette to "
                           "reorder. A blend or erase piece only affects what "
                           "sits below it here.");
        ImGui::PopStyleColor();
        pr::GroupGap();
        DrawPaintStackRail(id);
    }
    UI::EndPanel();
}

// The "Strokes & Fills" editor: the same stack, given a whole zone.
void Application::RenderPaintStackEditor(EditorState& st) {
    (void)st;
    if (!project_.document) {
        ImGui::TextUnformatted("No document.");
        return;
    }
    if (UI::BeginScroll("##paintStackScroll", ImVec2(0, 0)))
        DrawPaintStackRail(edit_.active);
    UI::EndScroll();
}

}  // namespace App

#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner editor — PLACEHOLDER (Ink rework, Lot 0).
//
//  The old Outliner (collections/pages/objects tree, Layers view, drag&drop,
//  rename, previews — all built on the old document) lives in
//  src/_legacy/Application/Editors/Outliner/ and is disconnected. Its
//  per-leaf state type (OutlinerState.h) is engine-agnostic and stays live.
//
//  The Outliner returns rebuilt on the Ink document model with its two views —
//  Layers (hierarchy, z-order, blend) and Collections (organisation sets) —
//  in docs/Ink/ROADMAP.md Lot 9.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderOutliner(EditorState& st) {
    (void)st;
    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
    ImGui::TextUnformatted("Outliner");
    ImGui::TextUnformatted("Awaiting the Ink document model");
    ImGui::TextUnformatted("(docs/Ink/ROADMAP.md - Lot 9)");
    ImGui::PopStyleColor();
}

} // namespace App

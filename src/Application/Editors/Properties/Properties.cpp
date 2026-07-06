#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ─────────────────────────────────────────────────────────────────────────────
//  Properties editor — PLACEHOLDER (Ink rework, Lot 0).
//
//  The old Properties (per-part fill/stroke, transforms, blend, line marks —
//  all built on the old document) lives in
//  src/_legacy/Application/Editors/Properties/ and is disconnected.
//
//  Properties returns rebuilt on the Ink style model (multi-fill /
//  multi-stroke, paints, modifiers) in docs/Ink/ROADMAP.md Lot 9.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderProperties() {
    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
    ImGui::TextUnformatted("Properties");
    ImGui::TextUnformatted("Awaiting the Ink document model");
    ImGui::TextUnformatted("(docs/Ink/ROADMAP.md - Lot 9)");
    ImGui::PopStyleColor();
}

} // namespace App

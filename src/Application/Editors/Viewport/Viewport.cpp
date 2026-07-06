#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <imgui.h>

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editor — PLACEHOLDER (Ink rework, Lot 0).
//
//  The old Viewport (canvas render, drawing tools, edit mode, transforms,
//  overlays — all built on the legacy engines and the old document) lives in
//  src/_legacy/Application/Editors/Viewport/ and is disconnected.
//
//  The real Viewport returns with the Ink engine (docs/Ink/ROADMAP.md Lot 1):
//  this method will own an Ink::View per zone leaf, drive its camera from the
//  leaf's EditorState (pan/zoom), submit editor overlays, and blit the view
//  texture with a single ImGui::Image — every canvas pixel 100 % Vulkan.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderViewport(ImVec2 size, EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();

    // Keep the shortcut context + hovered-leaf tracking correct while the
    // mouse is over this zone (RegisterRegionContext self-gates on hover).
    Shortcuts::ShortcutManager::Instance()
        .RegisterRegionContext("##zone", "viewport", "content");
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        zoneLayout_.SetHoveredEditorState(&st);

    // Consume the per-leaf view requests so they don't linger; the Ink
    // Viewport will act on them (fit / reset camera).
    st.reqFitDoc = st.reqFitSelection = st.reqResetOrigin = false;
    st.openNewDoc = false;

    // Empty canvas placeholder: the editor background + a centred note.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(
        ds.GetColor(Tok::S_Color_Background_Layer2)));

    const char* line1 = "Viewport";
    const char* line2 = "Awaiting the Ink engine (docs/Ink/ROADMAP.md - Lot 1)";
    const ImVec2 t1 = ImGui::CalcTextSize(line1);
    const ImVec2 t2 = ImGui::CalcTextSize(line2);
    const float  cy = p0.y + size.y * 0.5f;
    dl->AddText(ImVec2(p0.x + (size.x - t1.x) * 0.5f, cy - t1.y - 2.0f),
                ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Default)), line1);
    dl->AddText(ImVec2(p0.x + (size.x - t2.x) * 0.5f, cy + 2.0f),
                ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle)), line2);

    // Reserve the area so the zone's layout accounts for the canvas.
    ImGui::Dummy(size);
}

} // namespace App

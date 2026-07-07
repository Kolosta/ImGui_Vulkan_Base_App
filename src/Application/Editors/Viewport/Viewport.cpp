#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editor — Ink bootstrap (docs/Ink/ROADMAP.md Lot 1).
//
//  Each zone leaf owns an Ink::View keyed on its EditorState: the camera maps
//  exactly as the zone layout documents it (screen = canvasMin + (doc − pan)
//  · zoom), the canvas is blitted with a single ImGui::Image, and every pixel
//  inside — content AND editor overlays (page frame, origin cross, cursor
//  crosshair) — is rendered by Ink, 100 % Vulkan.
//
//  The drawing/editing tools return re-designed on the Ink document with the
//  editing-loop lot (ROADMAP Lot 8); until then the camera (wheel zoom at
//  cursor, middle-drag pan, fit/reset requests) is the whole interaction.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// A design-token color (sRGB straight) as the engine consumes it.
Ink::Color TokenColor(DS::DesignSystem& ds, Tok token, float alpha) {
    try {
        const ImVec4 c = ds.GetColor(token);
        return Ink::SrgbToLinearPremultiplied(c.x, c.y, c.z, alpha);
    } catch (...) {
        return Ink::SrgbToLinearPremultiplied(0.5f, 0.5f, 0.5f, alpha);
    }
}

} // namespace

void Application::RenderViewport(ImVec2 size, EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();

    // Shortcut context + hovered-leaf tracking while the mouse is over this
    // zone (RegisterRegionContext self-gates on hover).
    Shortcuts::ShortcutManager::Instance()
        .RegisterRegionContext("##zone", "viewport", "content");
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (hovered) zoneLayout_.SetHoveredEditorState(&st);

    st.openNewDoc = false;   // legacy request — consumed until Lot 2

    const ImVec2 cMin = ImGui::GetCursorScreenPos();

    // Engine unavailable (no Vulkan 1.3 / init failure): keep a plain,
    // token-styled placeholder so the shell stays usable.
    if (!ink_) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(cMin, ImVec2(cMin.x + size.x, cMin.y + size.y),
                          ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Background_Layer2)));
        const char* msg = "Ink engine unavailable (Vulkan 1.3 required)";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(cMin.x + (size.x - ts.x) * 0.5f,
                           cMin.y + (size.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle)), msg);
        ImGui::Dummy(size);
        return;
    }
    if (size.x < 8.0f || size.y < 8.0f) { ImGui::Dummy(size); return; }

    ImGuiIO& io = ImGui::GetIO();

    // Camera mapping (EditorState contract): screen = cMin + (doc − pan)·zoom.
    auto screenToDoc = [&](ImVec2 p) {
        return ImVec2((p.x - cMin.x) / st.zoom + st.pan.x,
                      (p.y - cMin.y) / st.zoom + st.pan.y);
    };
    auto docToView = [&](float dx, float dy) {   // view px (canvas-relative)
        return Ink::Vec2{ (dx - st.pan.x) * st.zoom,
                          (dy - st.pan.y) * st.zoom };
    };

    // ── Camera interactions ──────────────────────────────────────────────────
    if (hovered && io.MouseWheel != 0.0f) {
        // Zoom at the cursor: the document point under the mouse stays put.
        const float factor  = std::pow(1.15f, io.MouseWheel);
        const float newZoom = std::clamp(st.zoom * factor, 0.002f, 512.0f);
        const ImVec2 docAt  = screenToDoc(io.MousePos);
        st.pan.x = docAt.x - (io.MousePos.x - cMin.x) / newZoom;
        st.pan.y = docAt.y - (io.MousePos.y - cMin.y) / newZoom;
        st.zoom  = newZoom;
    }
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        st.pan.x -= io.MouseDelta.x / st.zoom;
        st.pan.y -= io.MouseDelta.y / st.zoom;
    }

    // Pending view requests (fit / reset), consumed by this leaf only.
    if (st.reqFitDoc || st.reqFitSelection) {
        st.reqFitDoc = st.reqFitSelection = false;
        const Ink::Rect b = ink_->SceneBounds();
        if (b.Width() > 0.0f && b.Height() > 0.0f) {
            const float z = std::clamp(std::min(size.x / b.Width(),
                                                size.y / b.Height()) * 0.94f,
                                       0.002f, 512.0f);
            st.zoom  = z;
            st.pan.x = b.min.x + b.Width()  * 0.5f - size.x * 0.5f / z;
            st.pan.y = b.min.y + b.Height() * 0.5f - size.y * 0.5f / z;
        }
    }
    if (st.reqResetOrigin) {
        st.reqResetOrigin = false;
        st.zoom = 1.0f;
        st.pan  = ImVec2(-40.0f, -40.0f);   // origin near the top-left corner
    }

    // ── Drive the Ink view ───────────────────────────────────────────────────
    Ink::View* view = ink_->AcquireView(&st);
    view->SetViewport((std::uint32_t)size.x, (std::uint32_t)size.y);
    view->SetCamera((double)st.pan.x, (double)st.pan.y, (double)st.zoom);
    view->SetBackground(TokenColor(ds, Tok::S_Color_Background_Layer2, 1.0f));

    // Editor overlays — Vulkan, through the engine's OverlayPass. Colors come
    // from design tokens (resolved app-side; the engine is token-free).
    Ink::OverlayList& ov = view->Overlay();
    const Ink::Color frameCol  = TokenColor(ds, Tok::S_Color_Text_Subtle, 0.85f);
    const Ink::Color accentCol = TokenColor(ds, Tok::S_Color_Accent_Default, 0.95f);

    // Demo page frame (the 1920×1080 page of the demo scene).
    ov.AddRect(docToView(0.0f, 0.0f), docToView(1920.0f, 1080.0f), frameCol, 1.5f);
    // Document origin cross.
    {
        const Ink::Vec2 o = docToView(0.0f, 0.0f);
        const float s = 7.0f;
        ov.AddLine({ o.x - s, o.y }, { o.x + s, o.y }, accentCol, 1.5f);
        ov.AddLine({ o.x, o.y - s }, { o.x, o.y + s }, accentCol, 1.5f);
    }
    // Cursor crosshair while this canvas is hovered.
    if (hovered) {
        const Ink::Vec2 m{ io.MousePos.x - cMin.x, io.MousePos.y - cMin.y };
        const float s = 9.0f;
        ov.AddLine({ m.x - s, m.y }, { m.x + s, m.y }, accentCol, 1.0f);
        ov.AddLine({ m.x, m.y - s }, { m.x, m.y + s }, accentCol, 1.0f);
    }

    // The single UI call for the whole canvas.
    if (auto tex = view->Texture())
        ImGui::Image((ImTextureID)tex, size);
    else
        ImGui::Dummy(size);
}

} // namespace App

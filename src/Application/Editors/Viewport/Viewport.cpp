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
//  The editing loop (ROADMAP Lot 8) runs on top: HandleViewportInput() routes
//  the mouse/keyboard on the hovered canvas to the active tool and the modal
//  G/R/S operation; DrawEditOverlays() paints selection/handles/feedback into
//  the same Vulkan OverlayPass. Camera (wheel zoom at cursor, middle-drag pan,
//  fit/reset) is unchanged from Lot 1.
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
    if (hovered) { zoneLayout_.SetHoveredEditorState(&st); hoveredViewport_ = &st; }

    st.openNewDoc = false;   // legacy request — consumed until Lot 2
    // NB: st.overlayRects intentionally NOT cleared here — the input router
    // (below) reads LAST frame's rects so a click on the tool palette never
    // falls through to the canvas; they are cleared right before the palette
    // repopulates them at the end of this function.

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

    // NO zoom limits (docs/Ink README req. 9): the camera is double
    // end-to-end and the engine rebases GPU transforms against a per-view
    // anchor (GEOMETRY.md §6), so precision holds at any zoom. Only guard
    // against a degenerate non-positive value.

    // Camera mapping (EditorState contract): screen = cMin + (doc − pan)·zoom.
    // Composed in double; narrowed only at the ImGui/overlay boundary.
    auto screenToDocX = [&](double px) { return (px - (double)cMin.x) / st.zoom + st.panX; };
    auto screenToDocY = [&](double py) { return (py - (double)cMin.y) / st.zoom + st.panY; };
    auto docToView = [&](double dx, double dy) {   // view px (canvas-relative)
        return Ink::Vec2{ (float)((dx - st.panX) * st.zoom),
                          (float)((dy - st.panY) * st.zoom) };
    };
    ViewCam cam;
    cam.canvasMin = cMin;  cam.panX = st.panX;  cam.panY = st.panY;  cam.zoom = st.zoom;
    if (hovered) {
        hoveredCam_ = cam;
        // Publish the canvas screen rect for the modal-op edge-wrap.
        canvasRectMin_ = cMin;
        canvasRectMax_ = ImVec2(cMin.x + size.x, cMin.y + size.y);
    }

    // ── Camera interactions ──────────────────────────────────────────────────
    // A floating overlay (tool palette, the N side panel) owns the pointer over
    // its rect: don't zoom/pan the canvas underneath, so the wheel scrolls the
    // panel only and a middle-drag there doesn't move the camera. (These are last
    // frame's rects, populated at the end of RenderViewport — stable.)
    const ImVec2 wp = io.MousePos;
    bool onOverlay = false;
    for (const ImVec4& r : st.overlayRects)
        if (wp.x >= r.x && wp.x <= r.z && wp.y >= r.y && wp.y <= r.w) {
            onOverlay = true; break;
        }
    if (hovered && !onOverlay && io.MouseWheel != 0.0f) {
        // Zoom at the cursor: the document point under the mouse stays put.
        const double factor  = std::pow(1.15, (double)io.MouseWheel);
        const double newZoom = st.zoom * factor;
        if (newZoom > 0.0 && std::isfinite(newZoom)) {
            const double docX = screenToDocX(io.MousePos.x);
            const double docY = screenToDocY(io.MousePos.y);
            st.panX = docX - (io.MousePos.x - (double)cMin.x) / newZoom;
            st.panY = docY - (io.MousePos.y - (double)cMin.y) / newZoom;
            st.zoom = newZoom;
        }
    }
    if (hovered && !onOverlay && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        st.panX -= (double)io.MouseDelta.x / st.zoom;
        st.panY -= (double)io.MouseDelta.y / st.zoom;
    }

    // Pending view requests (fit / reset), consumed by this leaf only.
    if (st.reqFitDoc || st.reqFitSelection) {
        const bool fitSel = st.reqFitSelection;
        st.reqFitDoc = st.reqFitSelection = false;
        // Numpad . frames the SELECTION (if any); Home/fit-doc frames everything.
        double minx, miny, w, h; bool have = false;
        Ink::DRect sb;
        if (fitSel && SelectionBounds(sb)) {
            minx = sb.min.x; miny = sb.min.y;
            w = std::max(1.0, sb.max.x - sb.min.x); h = std::max(1.0, sb.max.y - sb.min.y);
            have = true;
        } else {
            const Ink::Rect b = ink_->SceneBounds();
            if (b.Width() > 0.0f && b.Height() > 0.0f) {
                minx = b.min.x; miny = b.min.y; w = b.Width(); h = b.Height(); have = true;
            }
        }
        if (have) {
            const double z = std::min((double)size.x / w, (double)size.y / h) * 0.9;
            st.zoom = z;
            st.panX = minx + w * 0.5 - (double)size.x * 0.5 / z;
            st.panY = miny + h * 0.5 - (double)size.y * 0.5 / z;
        }
    }
    if (st.reqResetOrigin) {
        st.reqResetOrigin = false;
        st.zoom = 1.0;
        st.panX = st.panY = -40.0;   // origin near the top-left corner
    }

    // Refresh the camera snapshot after any pan/zoom this frame, then route the
    // active-tool / modal input on the hovered canvas (Lot 8).
    cam.panX = st.panX;  cam.panY = st.panY;  cam.zoom = st.zoom;
    if (hovered) hoveredCam_ = cam;
    HandleViewportInput(st, cam, hovered, cMin, size);

    // ── Drive the Ink view ───────────────────────────────────────────────────
    Ink::View* view = ink_->AcquireView(&st);
    view->SetViewport((std::uint32_t)size.x, (std::uint32_t)size.y);
    view->SetCamera(st.panX, st.panY, st.zoom);
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
    // Selection / handles / modal feedback / gesture preview (Lot 8).
    DrawEditOverlays(st, cam, ov, hovered);

    // Cursor crosshair while this canvas is hovered (only with a tool that has
    // no drag gesture in flight, to keep the modal feedback readable). NOT over
    // a floating overlay (tool palette, N panel) and not while a popup is open:
    // the pointer is on UI there, so it keeps the plain OS cursor. NB: popup
    // hierarchy makes IsWindowHovered(ChildWindows) report TRUE over a popup
    // opened from this zone — the explicit popup check is what excludes it.
    const bool anyPopup = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (hovered && !onOverlay && !anyPopup && !transformOp_.Active()) {
        const Ink::Vec2 m{ io.MousePos.x - cMin.x, io.MousePos.y - cMin.y };
        const float s = 9.0f;
        ov.AddLine({ m.x - s, m.y }, { m.x + s, m.y }, accentCol, 1.0f);
        ov.AddLine({ m.x, m.y - s }, { m.x, m.y + s }, accentCol, 1.0f);
    }

    // The single UI call for the whole canvas. The image blits with ROUNDED
    // BOTTOM corners only (the zone's radius): the top edge sits flush under
    // the menu bar and must meet it square, the bottom follows the zone frame.
    if (auto tex = view->Texture()) {
        const float rnd = ds.GetFloat(Tok::C_Window_CornerRadius) *
                          ds.GetGlobalScale();
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)tex, cMin, ImVec2(cMin.x + size.x, cMin.y + size.y),
            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rnd,
            ImDrawFlags_RoundCornersBottom);
        ImGui::Dummy(size);
    } else {
        ImGui::Dummy(size);
    }

    // Outliner viewport-sync picking (Lot 9): while an Outliner is armed, the
    // hovered viewport paints the LEGACY preview — the full zone inset like the
    // editor-tab drop preview, rounded like the window, filled with the notice
    // (orange) colour at the faint opacity — and consumes a left-click to
    // become the sync target.
    if (outlinerPickingState_ && hovered) {
        const float gs = ds.GetGlobalScale();
        const float inset = ds.GetFloat(Tok::C_ZoneTab_DropCenterInset) * gs;
        const float rnd   = ds.GetFloat(Tok::C_Window_CornerRadius) * gs;
        ImVec4 orange = ds.GetColor(Tok::S_Color_Notice_Default);
        orange.w = ds.GetFloat(Tok::S_Opacity_Faint);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(cMin.x + inset, cMin.y + inset),
                          ImVec2(cMin.x + size.x - inset, cMin.y + size.y - inset),
                          ImGui::GetColorU32(orange), rnd);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            outlinerPickingState_->syncTarget  = &st;
            outlinerPickingState_->syncPicking = false;
            outlinerPickingState_ = nullptr;
        }
    }

    // The transform-cursor icon (legacy glyphs), for the leaf owning the op.
    if (transformOp_.Active() && transformOp_.leaf == &st)
        DrawTransformCursor(cam);

    // Floating tool palette (drawn OVER the canvas image, as ImGui chrome).
    // Clear + repopulate the overlay rects HERE (input read last frame's).
    st.overlayRects.clear();
    // Rulers first (chrome over the canvas edges); they push their bands into
    // st.overlayRects so canvas input never falls through them.
    DrawRulers(st, cMin, size);
    // The tool palette + side panel are inset by the ENABLED rulers so they sit
    // INSIDE them, never on top (RulerInsets = left, top, right, bottom widths).
    const ImVec4 ins = RulerInsets(st);
    RenderToolPalette(ImVec2(cMin.x + ins.x, cMin.y + ins.y), st);
    // The right-side "N" panel (Item / Marks tabs), over the canvas. It records
    // its occupied band as an overlay rect so clicks on the panel don't also
    // drive the canvas underneath.
    RenderViewportSidePanel(st,
        ImVec2(cMin.x + ins.x, cMin.y + ins.y),
        ImVec2(cMin.x + size.x - ins.z, cMin.y + size.y - ins.w));
    // NB: the Add / context popups are rendered ONCE per frame from Update()
    // (after the whole layout), not here — a popup gated by per-zone hover
    // freezes when the cursor leaves the canvas onto the popup.
}

} // namespace App

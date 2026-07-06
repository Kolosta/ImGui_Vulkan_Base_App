#include "Application.h"
#include "ViewportToolsShared.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {

using Renderer::Vec2;

// ── Ultra-contextual status-bar hints (Lot 4) ────────────────────────────────
// Publish, every frame, the keys that are RELEVANT RIGHT NOW so the status bar
// shows nothing impossible in the current context (Blender-style).
void Application::PublishStatusHints() {
    using namespace Shortcuts;
    auto key = [](ImGuiKey k) {
        EventSignature s; s.type = EventType::KeyPress; s.key = k; return s;
    };
    auto modKey = [](ImGuiKey k, bool ctrl, bool shift) {
        EventSignature s; s.type = EventType::KeyPress; s.key = k;
        s.modifiers.ctrl = ctrl; s.modifiers.shift = shift; return s;
    };
    std::vector<ModalKeymapHint> hints;

    if (transformOp_.Active()) {
        // A modal G/R/S is running: advertise axis constraints, snap/precision and
        // confirm/cancel. Snap label matches the op (grid / 5° / 0.1×).
        const char* verb = transformOp_.kind == TransformKind::Move   ? "Move"
                         : transformOp_.kind == TransformKind::Rotate ? "Rotate"
                         : transformOp_.kind == TransformKind::Scale  ? "Scale"
                                                                      : "Transform";
        const char* snapLbl = transformOp_.kind == TransformKind::Rotate ? "Snap 5\xC2\xB0"
                            : transformOp_.kind == TransformKind::Scale  ? "Snap 0.1"
                                                                         : "Snap grid";
        // Axis keys apply to Move/Scale only (rotation is about Z in 2D). The label
        // shows the active constraint + orientation so the user knows the frame.
        if (transformOp_.kind != TransformKind::Rotate) {
            const char* orient = TransformOrientationName(transformOrientation_);
            std::string xl = std::string(verb) + " X";
            std::string yl = std::string(verb) + " Y";
            if (transformOp_.axis == TransformAxis::X) xl += " (" + std::string(orient) + ")";
            if (transformOp_.axis == TransformAxis::Y) yl += " (" + std::string(orient) + ")";
            hints.push_back({ key(ImGuiKey_X), xl });
            hints.push_back({ key(ImGuiKey_Y), yl });
        }
        hints.push_back({ modKey(ImGuiKey_LeftCtrl,  true,  false), snapLbl });
        hints.push_back({ modKey(ImGuiKey_LeftShift, false, true),  "Precision" });
        hints.push_back({ key(ImGuiKey_Enter),  "Confirm" });
        hints.push_back({ key(ImGuiKey_Escape), "Cancel" });
    }
    // else: leave empty → the status bar falls back to the generic context
    // actions (GetStatusBarActions), which already filter by the active
    // editor/mode/tool. (Edit-mode submodes, tool hints, between-editor RMB:
    // these are added here as the corresponding features land.)

    Shortcuts::ShortcutManager::Instance().SetTransientHints(std::move(hints));
}

// Revert + reset the in-progress gesture (Esc / right-click). Returns true if a
// gesture was actually cancelled.
bool Application::CancelViewportGesture() {
    if (!toolState_.Active()) return false;
    if (toolState_.gesture == ToolGesture::MoveObjects) {
        // Restore each moved shape's original translate.
        for (size_t i = 0; i < toolState_.moveIds.size(); ++i)
            if (Renderer::Shape* s = project_.document.FindShape(toolState_.moveIds[i]))
                if (i < toolState_.moveOrigTranslate.size())
                    s->transform.translate = toolState_.moveOrigTranslate[i];
    }
    // Drag-create / polyline / bezier: dropping the gesture discards the
    // in-progress geometry (nothing was committed to the document yet).
    toolState_.Reset();
    return true;
}


// Draw the multi-directional move cursor centred on the mouse, hiding the OS
// cursor (same technique as ZoneLayout::ApplyCursor).
void Application::ShowMoveCursor() {
    auto& im = VectorGraphics::IconManager::Instance();
    const char* iconId = "multi-directionnal-move-cur";
    if (!im.HasIcon(iconId)) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImVec2 p(mp.x - sz * 0.5f, mp.y - sz * 0.5f);
    ImVec4 col = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = col;
    im.RenderIcon(ImGui::GetForegroundDrawList(), iconId, p, sz, md);
}

// Draw a cursor icon centred on the mouse, rotated by angleRad. The icon is
// emitted to the foreground draw list, then its just-added vertices are rotated
// about the mouse (same technique as the vertical ruler text in Viewport.cpp).
void Application::ShowOrientedCursor(const char* iconId, float angleRad) {
    auto& im = VectorGraphics::IconManager::Instance();
    if (!im.HasIcon(iconId)) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    int vtx0 = fg->VtxBuffer.Size;
    ImVec4 col = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = col;
    im.RenderIcon(fg, iconId, ImVec2(mp.x - sz * 0.5f, mp.y - sz * 0.5f), sz, md);
    int vtx1 = fg->VtxBuffer.Size;
    float c = std::cos(angleRad), s = std::sin(angleRad);
    for (int i = vtx0; i < vtx1; ++i) {
        ImDrawVert& v = fg->VtxBuffer[i];
        float dx = v.pos.x - mp.x, dy = v.pos.y - mp.y;
        v.pos.x = mp.x + dx * c - dy * s;
        v.pos.y = mp.y + dx * s + dy * c;
    }
}

// Wrap the cursor to the opposite edge of the given SCREEN rect (the active
// zone's canvas) for unbounded dragging. Returns the warp vector in screen px
// ({0,0} if none) so an absolute-anchor transform can shift its anchor and stay
// continuous. Per-zone (the rect is passed in), so multiple viewports / other
// editors each wrap within their own bounds.
void Application::BeginGestureMouseTracking() {
    gestureMouseRef_ = ImGui::GetIO().MousePos;
}

ImVec2 Application::GestureMouseDelta() {
    ImVec2 cur = ImGui::GetIO().MousePos;
    ImVec2 d{ cur.x - gestureMouseRef_.x, cur.y - gestureMouseRef_.y };
    gestureMouseRef_ = cur;          // advance the reference to the current pos
    return d;
}

// Global Shift precision-drag factor. Held Shift slows the RELATIVE motion of the
// dragged thing (object, vertex, handle, line mark, slider value) without slowing
// the cursor — Blender's "finer adjustment". One factor for the whole app so the
// feel is uniform. (Behavioral interaction constant, like the drag thresholds
// elsewhere in this file — not a visual-style value, so not a design token.)
float Application::PrecisionDragFactor() const {
    constexpr float kPrecisionFactor = 0.1f;   // Shift → 10% of normal motion
    return ImGui::GetIO().KeyShift ? kPrecisionFactor : 1.0f;
}

// Adaptive "nice" grid step in doc-units for Ctrl snapping, matching the ruler
// subdivision at the current zoom (1·10ⁿ / 2·10ⁿ / 5·10ⁿ). effZoom = st.zoom ×
// unitScale (screen px per doc-unit). Mirrors the ruler's NiceStep so a snap lands
// exactly on a visible grid line.
float Application::SnapGridStep(float effZoom) const {
    const float kTargetPx = 48.0f;             // same target spacing as the rulers
    float raw = kTargetPx / std::max(1e-4f, effZoom);
    if (raw <= 0.0f) return 1.0f;
    float e = std::floor(std::log10(raw));
    float pow10 = std::pow(10.0f, e);
    float f = raw / pow10;                      // 1 .. 10
    float nice = (f <= 1.0f) ? 1.0f : (f <= 2.0f) ? 2.0f
               : (f <= 5.0f) ? 5.0f : 10.0f;
    return nice * pow10;
}

bool Application::WrapMouseInRect(ImVec2 mn, ImVec2 mx) {
    if (!window_) return false;
    const float pad = 2.0f;
    if (mx.x - mn.x < 8.0f || mx.y - mn.y < 8.0f) return false;
    ImVec2 mp = ImGui::GetIO().MousePos;
    float nx = mp.x, ny = mp.y;
    bool wrap = false;
    if (mp.x <= mn.x + pad)      { nx = mx.x - pad - 1.0f; wrap = true; }
    else if (mp.x >= mx.x - pad) { nx = mn.x + pad + 1.0f; wrap = true; }
    if (mp.y <= mn.y + pad)      { ny = mx.y - pad - 1.0f; wrap = true; }
    else if (mp.y >= mx.y - pad) { ny = mn.y + pad + 1.0f; wrap = true; }
    if (!wrap) return false;
    // Warp the OS cursor to the opposite edge, and move OUR reference to the
    // same target. Next frame GestureMouseDelta() = reportedPos − warpTarget =
    // the user's REAL motion since the warp, with the warp jump itself excluded
    // exactly (no rounding-based drift, and no lost motion at high speed — the
    // earlier "drop a whole frame" approach lost the fast motion → the drift).
    SDL_WarpMouseInWindow(window_, nx, ny);
    ImGui::GetIO().MousePos = ImVec2(nx, ny);   // reflect the warp this frame
    gestureMouseRef_ = ImVec2(nx, ny);          // reference follows the warp
    return true;
}

// Snap pie menu (Shift+S). 2D adaptation of Blender's snap pie: moves the
// selection to targets, or the 2D cursor to references. "Grid" snaps each
// object individually to the nearest grid crossing by its ORIGIN (not the pivot).
void Application::RenderViewportPieMenu() {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;

    // The doc-unit spacing of one grid cell (matches the viewport's minor grid
    // intent: 50 doc-units is a sensible default cell).
    const float kGrid = 50.0f;
    auto snapGrid = [&](Renderer::Vec2 p) {
        return Renderer::Vec2{ std::round(p.x / kGrid) * kGrid,
                               std::round(p.y / kGrid) * kGrid };
    };
    auto originWorld = [&](uint64_t id) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(id);
        // Use THIS viewport's display origin so snaps line up with the 2D cursor
        // and the on-screen objects under any auto page layout (not the stored
        // Manual position).
        return s ? Renderer::Tessellator::WorldTransform(*s, s->origin,
                                                         CurPageOriginOfShape(id))
                 : Renderer::Vec2{0,0};
    };
    // Move a whole shape so its origin lands on world point `to`.
    auto moveOriginTo = [&](uint64_t id, Renderer::Vec2 to) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) return;
        Renderer::Vec2 cur = originWorld(id);
        s->transform.translate.x += to.x - cur.x;
        s->transform.translate.y += to.y - cur.y;
    };

    ImGui::PushStyleColor(ImGuiCol_PopupBg,
        ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));

    if (ImGui::BeginPopup("##snapPie")) {
        ImGui::PushStyleColor(ImGuiCol_Text,
            ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Snap");
        ImGui::PopStyleColor();
        ImGui::Separator();

        const bool hasSel = doc.HasSelection();
        const auto sel = doc.Selection();

        if (ImGui::MenuItem("Selection to Cursor", nullptr, false, hasSel)) {
            for (uint64_t id : sel) moveOriginTo(id, doc.cursor);
            project_.dirty = true;
        }
        if (ImGui::MenuItem("Selection to Active", nullptr, false, sel.size() >= 2)) {
            Renderer::Vec2 a = originWorld(doc.ActiveId());
            for (uint64_t id : sel) if (id != doc.ActiveId()) moveOriginTo(id, a);
            project_.dirty = true;
        }
        if (ImGui::MenuItem("Selection to Grid", nullptr, false, hasSel)) {
            for (uint64_t id : sel) moveOriginTo(id, snapGrid(originWorld(id)));
            project_.dirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cursor to Active", nullptr, false, hasSel))
            doc.cursor = originWorld(doc.ActiveId());
        if (ImGui::MenuItem("Cursor to Selected", nullptr, false, hasSel)) {
            Renderer::Vec2 sum{0,0}; int n = 0;
            for (uint64_t id : sel) { Renderer::Vec2 o = originWorld(id); sum.x += o.x; sum.y += o.y; ++n; }
            if (n) doc.cursor = { sum.x / n, sum.y / n };
        }
        if (ImGui::MenuItem("Cursor to Origin"))
            doc.cursor = { 0, 0 };
        if (ImGui::MenuItem("Cursor to Page Origin")) {
            if (!doc.artboards.empty()) doc.cursor = doc.artboards.front().pos;
        }
        if (ImGui::MenuItem("Cursor to Grid"))
            doc.cursor = snapGrid(doc.cursor);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
}


// A thin crosshair centred on the mouse with a 1px hole = the exact drop point.
// Drawn with a dark halo + bright core so it reads on both the page and the
// canvas background. Hides the OS cursor (same technique as ShowMoveCursor).
void Application::ShowCrosshairCursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const ImU32 core = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_Crosshair));
    const ImU32 halo = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorTick));
    const float arm = 12.0f, hole = 1.5f;
    auto cross = [&](ImU32 c, float t) {
        fg->AddLine(ImVec2(mp.x - arm, mp.y), ImVec2(mp.x - hole, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x + hole, mp.y), ImVec2(mp.x + arm, mp.y), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y - arm), ImVec2(mp.x, mp.y - hole), c, t);
        fg->AddLine(ImVec2(mp.x, mp.y + hole), ImVec2(mp.x, mp.y + arm), c, t);
    };
    cross(halo, 3.0f);   // dark outline for contrast
    cross(core, 1.0f);   // thin bright core (1px hole at centre = drop point)
}


} // namespace App

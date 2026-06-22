#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip (shared styled tooltip)
#include <UI/Widgets/Dropdown.h>      // UI::Dropdown (operator panel params)
#include <UI/Widgets/ButtonGroup.h>   // UI::ButtonGroup (snap base/affect)
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace App {

// Page display origin / visibility for the viewport currently being drawn
// (curPageViews_ is filled at the top of RenderViewport from st.pageLayout).
// Falls back to the shared ab.pos / visible when no layout view is recorded.
Renderer::Vec2 Application::CurPageOrigin(int abIndex) const {
    if (abIndex >= 0 && abIndex < (int)curPageViews_.size())
        return curPageViews_[(size_t)abIndex].origin;
    if (abIndex >= 0 && abIndex < (int)project_.document.artboards.size())
        return project_.document.artboards[(size_t)abIndex].pos;
    return {0, 0};
}
Renderer::Vec2 Application::CurPageOriginOfShape(uint64_t shapeId) const {
    return CurPageOrigin(project_.document.ArtboardOfShape(shapeId));
}
bool Application::CurPageVisible(int abIndex) const {
    if (abIndex >= 0 && abIndex < (int)curPageViews_.size())
        return curPageViews_[(size_t)abIndex].visible;
    return true;
}


// New-document popup: a few presets + custom size, then a white artboard
// whose top-left corner is the document origin (0,0). Non-modal (BeginPopup)
// so it does NOT dim the whole screen white; token-styled throughout.
// Shared page-size popup, used both to CREATE a page ("New Document") and to
// RESIZE the current one ("Resize Page"). `popupId` selects which; for a resize
// the caller seeds `outSize` with the page's current dimensions and we pre-fill
// the custom fields with them. Returns true once the user confirms a size.
static bool PageSizePopup(const char* popupId, const char* confirmLabel,
                          ImVec2& outSize) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    bool created = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ds.GetVec2(DesignSystem::Tok::C_Window_Padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
                        ds.GetFloat(DesignSystem::Tok::C_Window_CornerRadius));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
                          ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));

    bool open = ImGui::BeginPopup(popupId, ImGuiWindowFlags_AlwaysAutoResize);
    if (open) {
        struct Preset { const char* name; float w, h; };
        static const Preset kPresets[] = {
            { "A4 portrait (px@96dpi)", 794,  1123 },
            { "A4 landscape",           1123, 794  },
            { "1920 x 1080",            1920, 1080 },
            { "1280 x 720",             1280, 720  },
            { "Square 1080",            1080, 1080 },
            { "Portrait 1080 x 1350",   1080, 1350 },
        };
        static int  sel = 2;
        static float cw = 1920, ch = 1080;
        // On open, seed the custom fields from the caller's size (the current
        // page size for a resize; harmless for create) and match a preset.
        if (ImGui::IsWindowAppearing() && outSize.x > 0.0f && outSize.y > 0.0f) {
            cw = outSize.x; ch = outSize.y; sel = -1;
            for (int i = 0; i < (int)(sizeof(kPresets)/sizeof(kPresets[0])); ++i)
                if (kPresets[i].w == cw && kPresets[i].h == ch) { sel = i; break; }
        }

        ImGui::PushStyleColor(ImGuiCol_Text,
                              ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Presets");
        ImGui::PopStyleColor();
        for (int i = 0; i < (int)(sizeof(kPresets)/sizeof(kPresets[0])); ++i) {
            if (ImGui::RadioButton(kPresets[i].name, sel == i)) {
                sel = i; cw = kPresets[i].w; ch = kPresets[i].h;
            }
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Custom size (px)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(90); ImGui::InputFloat("W", &cw, 0, 0, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputFloat("H", &ch, 0, 0, "%.0f");
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_Button,
                              ds.GetColor(DesignSystem::Tok::C_IconButton_Background));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover));
        if (ImGui::Button(confirmLabel, ImVec2(120, 0))) {
            outSize = ImVec2(std::max(1.0f, cw), std::max(1.0f, ch));
            created = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor(2);
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return created;
}

// Round a raw step up to the nearest "nice" value (1·10ⁿ, 2·10ⁿ, 5·10ⁿ).
static float NiceStep(float raw) {
    if (raw <= 0.0f) return 1.0f;
    float exp  = std::floor(std::log10(raw));
    float pow10 = std::pow(10.0f, exp);
    float f    = raw / pow10;            // 1 .. 10
    float nice = (f <= 1.0f) ? 1.0f : (f <= 2.0f) ? 2.0f
               : (f <= 5.0f) ? 5.0f : 10.0f;
    return nice * pow10;
}

// ── Real-time metrics overlay ────────────────────────────────────────────────
// A compact, game/Blender-style HUD in the canvas BOTTOM-LEFT: smoothed FPS +
// frame time, plus the Vulkan renderer's per-frame counters (triangles, draw
// calls, shapes drawn/cached/culled, CPU tessellation time and GPU-wait time).
// Reads CanvasRenderer::GetMetrics(), published at EndFrame.

// Camera + document live in `st` (this leaf's EditorState), so every Viewport
// zone has its own independent view.
void Application::RenderViewport(ImVec2 size, EditorState& st) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    // Per-viewport page layout (Lot 3): compute each page's DISPLAY origin +
    // visibility for THIS leaf, used by the renderer, chrome, picking and the
    // selection overlay below (so this Viewport can show a different page
    // arrangement than another without touching the shared Artboard::pos).
    curPageViews_ = ComputePageViews(st.pageLayout, project_.document);

    // Unit table: name + document-pixels per unit + minor subdivisions.
    struct Unit { const char* name; float pxPer; int minors; };
    static const Unit kUnits[] = {
        { "px", 1.0f,   5 },
        { "pt", 1.333f, 5 },
        { "mm", 3.78f,  5 },
        { "cm", 37.8f,  10 },
        { "in", 96.0f,  8 },
    };
    if (st.docUnit < 0 || st.docUnit >= (int)(sizeof(kUnits)/sizeof(kUnits[0])))
        st.docUnit = 0;
    const Unit& u = kUnits[st.docUnit];

    // Ruler thickness = the app's base UI unit (control-height), so the rulers
    // line up with every other 20px-tall chrome element.
    const float rulerW = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gs;
    // Tick lengths (measured from the canvas-facing edge of the ruler inward):
    // minor ticks are short, major ticks longer; the value label sits above the
    // major tick, centred in the gap between the major tick top and the ruler
    // top edge.
    const float minorTickLen = 5.0f * gs;
    const float majorTickLen = 8.0f * gs;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // C5: translucent ruler bands so the canvas shows through faintly.
    ImVec4 cBgV = ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1); cBgV.w = 0.6f;
    ImU32 cBg     = ImGui::GetColorU32(cBgV);
    ImU32 cBgFull = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImU32 cCanvas = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CanvasArea));
    ImU32 cTick   = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
    ImU32 cText   = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    ImU32 cGuide  = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_Guide));

    // Zone corner radius (resolved through the current editor scope, same token
    // the zone frame uses) so the canvas + the left ruler round their BOTTOM
    // corners to match the zone — the content then never pokes past the rounded
    // border at the bottom corners (no clip hack needed).
    const float zoneRnd =
        ds.GetFloat(DesignSystem::Tok::C_Window_CornerRadius) * gs;

    ImVec2 cMin(p0.x + rulerW, p0.y + rulerW);   // canvas top-left
    ImVec2 cMax(p0.x + size.x, p0.y + size.y);
    ImVec2 cSize(cMax.x - cMin.x, cMax.y - cMin.y);

    // ── Vulkan canvas (render-to-texture) ────────────────────────────────────
    // The vector document (artboards + shapes) is rendered EXCLUSIVELY by the
    // Vulkan CanvasRenderer into an offscreen texture sized to the canvas area,
    // then blitted here with ImGui::Image. ImGui draws only the chrome (rulers,
    // guides, labels, toolbar) on top. The camera maps doc-units to the target
    // so that doc point `pan` sits at the target's top-left and `zoom` is the
    // pixels-per-doc-unit factor — identical to the D2S mapping below.
    {
        const int twPx = (int)std::lround(cSize.x);
        const int thPx = (int)std::lround(cSize.y);
        Renderer::Camera cam;
        cam.panX      = st.pan.x;
        cam.panY      = st.pan.y;
        cam.zoom      = st.zoom;
        cam.unitScale = u.pxPer;   // same doc-unit→pixel factor as D2S
        // Canvas backdrop (behind the artboards). Keyed per-leaf by &st so each
        // Viewport zone owns an independent offscreen target.
        ImVec4 backdrop(0.30f, 0.30f, 0.32f, 1.0f);
        std::vector<Renderer::Tessellator::PagePlacement> placements;
        placements.reserve(curPageViews_.size());
        for (const PageView& pv : curPageViews_)
            placements.push_back({ pv.origin, pv.visible });
        // Focus = the leaf the user is interacting with (hovered last frame). The
        // focused view rebuilds immediately; others throttle their rebuild cadence
        // (same detail, just refreshed less often) so N open viewports don't all
        // re-tessellate on the same frame. HoveredEditorState() is the previous
        // frame's hover (set below), which is fine for a cadence hint.
        const bool viewFocused = (zoneLayout_.HoveredEditorState() == &st) ||
                                 (zoneLayout_.HoveredEditorState() == nullptr);
        ImTextureID canvasTex = canvasRenderer_.RenderView(
            &st, project_.document, cam, twPx, thPx, backdrop, &placements,
            /*includeLoose=*/st.nPanelShowOrphans, /*focused=*/viewFocused);
        if (canvasTex) {
            // Round the bottom-right corner to match the editor zone (AddImage is
            // a hard rectangle and would overhang the rounded corner there).
            dl->AddImageRounded(canvasTex, cMin, cMax, ImVec2(0, 0), ImVec2(1, 1),
                                IM_COL32_WHITE, zoneRnd,
                                zoneRnd > 0.5f ? ImDrawFlags_RoundCornersBottomRight
                                               : ImDrawFlags_RoundCornersNone);
        } else {
            // Fallback while the renderer warms up / shaders missing.
            dl->AddRectFilled(cMin, cMax, cCanvas, zoneRnd,
                              zoneRnd > 0.5f ? ImDrawFlags_RoundCornersBottomRight
                                             : ImDrawFlags_RoundCornersNone);
        }
    }

    // ── Real-time metrics overlay (togglable) ────────────────────────────────
    if (showMetrics_) DrawMetricsOverlay(cMin, cMax);

    // doc → screen mapping: screen = cMin + (doc - pan) * zoom.
    auto D2S = [&](ImVec2 d) {
        return ImVec2(cMin.x + (d.x - st.pan.x) * st.zoom,
                      cMin.y + (d.y - st.pan.y) * st.zoom);
    };
    auto S2D = [&](ImVec2 s) {
        return ImVec2((s.x - cMin.x) / st.zoom + st.pan.x,
                      (s.y - cMin.y) / st.zoom + st.pan.y);
    };

    // ── Hover / focus (C1) ──────────────────────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 m = io.MousePos;
    // The right-side panel overlays the canvas; clicks on it (its tab bar, resize
    // grip, or content widgets) must NOT also reach the canvas tools / 2D cursor.
    // Exclude the band it occupies from the canvas hover so a press there never
    // arms a box-select or moves the cursor behind the panel. The small stage-0
    // handle is a thin sliver at the right edge — also excluded via its width.
    float sidePanelW = UI::SidePanelOccupiedWidth(st.sidePanel, cMin, cMax);
    if (st.sidePanel.stage == 0) sidePanelW = 14.0f * gs;   // reserve the closed handle sliver
    const float canvasRight = cMax.x - sidePanelW;
    bool rectHover = (m.x >= cMin.x && m.x <= canvasRight &&
                      m.y >= cMin.y && m.y <= cMax.y);
    // Floating UI overlays (tool palette, operator panel, …) sit over the canvas; a
    // click on one must NOT reach the canvas underneath (e.g. select an object below
    // the operator panel). Exclude every published overlay rect (last frame's), then
    // clear the list so this frame's overlays repopulate it.
    for (const ImVec4& r : st.overlayRects)
        if (m.x >= r.x && m.x <= r.z && m.y >= r.y && m.y <= r.w) { rectHover = false; break; }
    st.overlayRects.clear();
    // Strict: false if a floating window occludes this point. Drives scope,
    // status bar and which leaf the camera actions target.
    bool scopeHovered = rectHover &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    // Permissive: keep middle-drag pan alive while a widget is active.
    bool camHovered = rectHover &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                               ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (scopeHovered) {
        Shortcuts::ShortcutManager::Instance()
            .RegisterRegionContext("##zone", "viewport", "content");
        zoneLayout_.SetHoveredEditorState(&st);
    }

    bool middleDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
    bool handTool = (Shortcuts::Tools::ToolManager::Instance().GetActiveTool()
                     == "tool.hand");

    // ── Camera pan (captured, wraps edge-to-edge) ─────────────────────────────
    // Whether the pan BUTTON is held (middle, or Left while the Hand tool is on).
    const bool panBtnDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
        (handTool && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    // Start a pan when a drag begins over THIS leaf's canvas (not while picking a
    // sync target). Once started, panOwner_ holds it until the button releases —
    // even if the cursor leaves the rect — so the wrap can fire at the border.
    if (!panOwner_ && camHovered && !outlinerPickingState_ &&
        (middleDrag || (handTool && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))))
        panOwner_ = &st;
    if (panOwner_ == &st) {
        if (!panBtnDown) {
            panOwner_ = nullptr;                      // button up → end the pan
        } else {
            st.pan.x -= io.MouseDelta.x / st.zoom;
            st.pan.y -= io.MouseDelta.y / st.zoom;
            // Infinite pan: warp the cursor to the opposite canvas edge when it
            // reaches one. WrapMouseInRect rewrites io.MousePos to the warp target
            // THIS frame, so next frame's io.MouseDelta already excludes the jump
            // (no skipped frame, no lost motion). Same behaviour as grab/transform.
            WrapMouseInRect(cMin, cMax);
        }
    }

    if (camHovered && !outlinerPickingState_) {
        // C2: suppress ALL wheel input during a middle-drag pan so it isn't
        // disturbed by zoom/scroll.
        if (!middleDrag) {
            if (io.MouseWheel != 0.0f) {
                if (io.KeyCtrl) {
                    st.pan.y -= io.MouseWheel * 60.0f / st.zoom;
                } else if (io.KeyShift) {
                    st.pan.x -= io.MouseWheel * 60.0f / st.zoom;
                } else {
                    ImVec2 before = S2D(m);
                    float f = io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f;
                    // Effectively unlimited zoom — bounds are just numeric guards
                    // (avoid 0 / overflow), not a usability limit.
                    st.zoom = std::clamp(st.zoom * f, 1e-5f, 1e7f);
                    ImVec2 after = S2D(m);
                    st.pan.x += before.x - after.x;
                    st.pan.y += before.y - after.y;
                }
            }
            if (io.MouseWheelH != 0.0f)
                st.pan.x -= io.MouseWheelH * 60.0f / st.zoom;
        }
    }

    // Bounding box of every VISIBLE artboard at its DISPLAY origin (per-viewport
    // layout), doc-units → px — so "fit" frames this viewport's arrangement.
    bool   hasArt = !project_.artboards().empty();
    ImVec2 bbMin( 1e9f,  1e9f), bbMax(-1e9f, -1e9f);
    for (int i = 0; i < (int)project_.artboards().size(); ++i) {
        if (!CurPageVisible(i)) continue;
        const Artboard& ab = project_.artboards()[(size_t)i];
        Renderer::Vec2 po = CurPageOrigin(i);
        bbMin.x = std::min(bbMin.x, po.x * u.pxPer);
        bbMin.y = std::min(bbMin.y, po.y * u.pxPer);
        bbMax.x = std::max(bbMax.x, (po.x + ab.size.x) * u.pxPer);
        bbMax.y = std::max(bbMax.y, (po.y + ab.size.y) * u.pxPer);
    }
    if (bbMin.x > bbMax.x) { bbMin = {0,0}; bbMax = {1,1}; }  // no visible page

    // C6: recenter requests, consumed by THIS leaf only.
    if (st.reqFitDoc && hasArt) {
        float bw = std::max(1.0f, bbMax.x - bbMin.x);
        float bh = std::max(1.0f, bbMax.y - bbMin.y);
        float zx = cSize.x / bw, zy = cSize.y / bh;
        st.zoom = std::clamp(std::min(zx, zy) * 0.9f, 1e-5f, 1e7f);
        st.pan  = ImVec2(
            (bbMin.x + bw * 0.5f) - cSize.x * 0.5f / st.zoom,
            (bbMin.y + bh * 0.5f) - cSize.y * 0.5f / st.zoom);
    }
    // Frame the selected/active object(s) — Blender's Numpad . "Frame Selected".
    // World bounds (doc-units) of every selected shape, plus the active one even if
    // it isn't in the selection. Each shape's bounds use THIS viewport's page
    // display origin so the framing matches what's shown.
    if (st.reqFitSelection) {
        auto& doc = project_.document;
        Renderer::Vec2 wmn{1e30f,1e30f}, wmx{-1e30f,-1e30f}; bool any = false;
        auto add = [&](uint64_t id) {
            Renderer::Shape* s = doc.FindShape(id);
            if (!s || !s->visible) return;
            Renderer::Vec2 a, b;   // detail is zoom-independent now → pass 1.0
            if (Renderer::Tessellator::WorldBounds(*s, 1.0f, a, b, CurPageOriginOfShape(id))) {
                wmn.x=std::min(wmn.x,a.x); wmn.y=std::min(wmn.y,a.y);
                wmx.x=std::max(wmx.x,b.x); wmx.y=std::max(wmx.y,b.y); any=true;
            }
        };
        for (uint64_t id : doc.Selection()) add(id);
        if (!doc.HasSelection() && doc.ActiveId()) add(doc.ActiveId());
        if (any) {
            // World doc-units → the camera's pre-scaled space (× u.pxPer).
            float x0 = wmn.x * u.pxPer, y0 = wmn.y * u.pxPer;
            float x1 = wmx.x * u.pxPer, y1 = wmx.y * u.pxPer;
            float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
            // Add a 30% margin around tiny/point objects so they don't fill the view.
            float zx = cSize.x / bw, zy = cSize.y / bh;
            st.zoom = std::clamp(std::min(zx, zy) * 0.7f, 1e-5f, 1e7f);
            st.pan  = ImVec2((x0 + bw * 0.5f) - cSize.x * 0.5f / st.zoom,
                             (y0 + bh * 0.5f) - cSize.y * 0.5f / st.zoom);
        }
    }
    if (st.reqResetOrigin) {
        st.zoom = 1.0f;
        st.pan  = ImVec2(-40.0f, -40.0f);
    }
    st.reqFitDoc = st.reqFitSelection = st.reqResetOrigin = false;

    // ── Artboard chrome (shared across every Viewport zone) ─────────────
    // The white PAGE itself is rendered by Vulkan (in the offscreen texture
    // above); ImGui only overlays the thin page border and the name label so
    // they stay crisp regardless of zoom. (Page shadow → Step 2, drawn inside
    // the Vulkan pass so it sits behind the page.)
    // Free page move only makes sense in Manual layout (auto layouts position
    // the pages themselves).
    const bool manualLayout = (st.pageLayout.mode == PageLayoutMode::Manual);
    if (hasArt) {
        const auto& abs = project_.artboards();
        for (int i = 0; i < (int)abs.size(); ++i) {
            if (!CurPageVisible(i)) continue;          // hidden in this viewport
            const Artboard& ab = abs[(size_t)i];
            const bool activePage = (project_.document.ActivePage() == ab.id);
            Renderer::Vec2 po = CurPageOrigin(i);      // display position
            ImVec2 a = D2S(ImVec2(po.x * u.pxPer, po.y * u.pxPer));
            ImVec2 b = D2S(ImVec2((po.x + ab.size.x) * u.pxPer,
                                  (po.y + ab.size.y) * u.pxPer));
            dl->AddRect(a, b, ImGui::GetColorU32(ds.GetColor(
                DesignSystem::Tok::C_Viewport_PageBorder)));
            if (activePage) {
                // Active page: a faint accent border (less prominent than an
                // object's active outline) over the plain page edge.
                ImVec4 acc = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);
                ImU32 accBorder = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(acc.x, acc.y, acc.z, 0.55f));
                dl->AddRect(a, b, accBorder, 0.0f, 0, 1.5f);
            }
            if (!ab.name.empty()) {
                ImVec2 ts = ImGui::CalcTextSize(ab.name.c_str());
                ImVec2 lp(a.x, a.y - ts.y - 2.0f);             // label top-left
                if (activePage) {
                    // Very light, transparent accent fill behind the title to mark
                    // the active page.
                    ImVec4 acc = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);
                    ImU32 fill = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(acc.x, acc.y, acc.z, 0.18f));
                    dl->AddRectFilled(ImVec2(lp.x - 3, lp.y - 1),
                                      ImVec2(lp.x + ts.x + 3, lp.y + ts.y + 1),
                                      fill, 2.0f);
                }
                // Right-click the page name → page context menu (thumbnail /
                // resize). Highlight the label when it can be clicked.
                bool overName = scopeHovered &&
                    io.MousePos.x >= lp.x && io.MousePos.x <= lp.x + ts.x &&
                    io.MousePos.y >= lp.y && io.MousePos.y <= lp.y + ts.y;
                if (overName && cropArtboard_ < 0 && pageDrag_ < 0) {
                    dl->AddRectFilled(ImVec2(lp.x - 2, lp.y - 1),
                                      ImVec2(lp.x + ts.x + 2, lp.y + ts.y + 1),
                                      ImGui::GetColorU32(ds.GetColor(
                                          DesignSystem::Tok::C_Viewport_PageNameHover)), 2.0f);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        pageCtxRequest_  = true;
                        pageCtxArtboard_ = i;
                        pageCtxPos_      = io.MousePos;
                    }
                    // Clicking the title also makes this the ACTIVE page.
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        project_.document.SetActivePage(ab.id);
                    // LMB-press on the name starts a FREE PAGE MOVE (Manual only).
                    if (manualLayout && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        ImVec2 mDoc = S2D(io.MousePos);
                        pageDrag_      = i;
                        pageDragOwner_ = &st;
                        pageDragPos0_  = ab.pos;
                        pageDragRef_   = { mDoc.x / u.pxPer, mDoc.y / u.pxPer };
                        MarkUndoLabel("Move page");
                    }
                }
                // Hint the move affordance while hovering the label.
                if (overName && manualLayout && cropArtboard_ < 0 && pageDrag_ < 0)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                dl->AddText(lp, cText, ab.name.c_str());
            }
        }
    } else {
        const char* hint = "Empty project — add a page with + or Ctrl+Shift+N";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cMin.x + (cSize.x - ts.x) * 0.5f,
                           cMin.y + (cSize.y - ts.y) * 0.5f),
                    cText, hint);
    }

    // Ruler reference: in Page space, ruler 0 sits at the active/selected page's
    // DISPLAY top-left, so labels read page-relative coords. The reference page
    // is the ACTIVE object's page (handles a multi-page selection unambiguously),
    // else the first selected object's page. No selection → Viewport space.
    // refDocPx is the offset in ruler-PIXELS (doc-units × pxPer), to subtract
    // from the tick value before dividing back into units.
    ImVec2 rulerRefPx(0.0f, 0.0f);
    if (st.rulerSpace == EditorState::RulerSpace::Page) {
        uint64_t refId = project_.document.ActiveId();
        if (!refId && !project_.document.Selection().empty())
            refId = project_.document.Selection().front();
        int refAb = (refId != 0) ? project_.document.ArtboardOfShape(refId) : -1;
        if (refAb >= 0) {
            Renderer::Vec2 po = CurPageOrigin(refAb);   // display origin (doc-units)
            rulerRefPx = ImVec2(po.x * u.pxPer, po.y * u.pxPer);
        }
    }

    // ── Rulers ──────────────────────────────────────────────────────────
    dl->AddRectFilled(ImVec2(p0.x, p0.y),
                      ImVec2(p0.x + size.x, p0.y + rulerW), cBg);   // top
    // Left ruler reaches the zone's BOTTOM-LEFT corner, so round it to match.
    dl->AddRectFilled(ImVec2(p0.x, p0.y),
                      ImVec2(p0.x + rulerW, p0.y + size.y), cBg,    // left
                      zoneRnd,
                      zoneRnd > 0.5f ? ImDrawFlags_RoundCornersBottomLeft
                                     : ImDrawFlags_RoundCornersNone);

    // C4: smaller ruler font (75% of the base size) via the ImDrawList
    // overload — no global style change.
    ImFont* font     = ImGui::GetFont();
    float   baseFs   = ImGui::GetFontSize();
    float   rulerFs  = baseFs * 0.75f;
    auto    smallText = [&](ImVec2 at, ImU32 col, const char* s) {
        dl->AddText(font, rulerFs, at, col, s);
    };
    auto    smallSize = [&](const char* s) {
        return font->CalcTextSizeA(rulerFs, FLT_MAX, 0.0f, s);
    };

    // Draw `s` rotated 90° CCW (reading bottom→top), with its baseline-box
    // centred on (cx, cy). Implemented by appending normal text to the draw
    // list, then rotating the vertices it produced about the pivot — the only
    // way to get rotated glyphs without a separate atlas.
    auto verticalText = [&](float cx, float cy, ImU32 col, const char* s) {
        ImVec2 sz = font->CalcTextSizeA(rulerFs, FLT_MAX, 0.0f, s);
        // Lay the text out at the pivot first (so it is inside the current clip
        // rect and actually generates vertices — text added off-screen would be
        // culled and produce nothing to rotate), then rotate those vertices in
        // place about the pivot.
        ImVec2 origin(cx - sz.x * 0.5f, cy - sz.y * 0.5f);
        int vtxStart = dl->VtxBuffer.Size;
        dl->AddText(font, rulerFs, origin, col, s);
        int vtxEnd = dl->VtxBuffer.Size;
        // Rotate −90° (CCW) about the pivot: (dx,dy) → (dy, -dx).
        for (int vi = vtxStart; vi < vtxEnd; ++vi) {
            ImDrawVert& v = dl->VtxBuffer[vi];
            float dx = v.pos.x - cx;
            float dy = v.pos.y - cy;
            v.pos.x = cx + dy;
            v.pos.y = cy - dx;
        }
    };

    // C3: adaptive "nice" step. The target on-screen spacing between major
    // ticks (px) sets how dense the rulers are: a smaller target keeps the
    // current subdivision longer (you must zoom further before it switches to
    // the next nicer step), which is what the user asked for.
    const float kMajorTargetPx = 48.0f * gs;
    float majorDoc = NiceStep(kMajorTargetPx / std::max(0.0001f, u.pxPer * st.zoom))
                     * u.pxPer;
    float majorPx  = majorDoc * st.zoom;

    // Tick spans: from the canvas-facing inner edge (p0 + rulerW) inward.
    const float topMajorY0 = p0.y + rulerW - majorTickLen;
    const float topMinorY0 = p0.y + rulerW - minorTickLen;
    const float lftMajorX0 = p0.x + rulerW - majorTickLen;
    const float lftMinorX0 = p0.x + rulerW - minorTickLen;
    // Value label centred vertically in the gap above the major tick (between
    // the ruler top and the major-tick top).
    const float topLabelCY = (p0.y + topMajorY0) * 0.5f;
    const float lftLabelCX = (p0.x + lftMajorX0) * 0.5f;

    if (majorPx > 4.0f) {
        // Top ruler: number ABOVE the major tick, centred on it; minor sub-ticks.
        float dxStart = std::floor(S2D(cMin).x / majorDoc) * majorDoc;
        for (float dx = dxStart; ; dx += majorDoc) {
            float sx = D2S(ImVec2(dx, 0)).x;
            if (sx > cMax.x) break;
            if (sx >= cMin.x) {
                dl->AddLine(ImVec2(sx, topMajorY0),
                            ImVec2(sx, p0.y + rulerW), cTick);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d",
                              (int)std::lround((dx - rulerRefPx.x) / u.pxPer));
                ImVec2 ts = smallSize(buf);
                smallText(ImVec2(sx - ts.x * 0.5f, topLabelCY - ts.y * 0.5f),
                          cText, buf);
            }
            for (int k = 1; k < u.minors; ++k) {
                float mx = D2S(ImVec2(dx + k * majorDoc / u.minors, 0)).x;
                if (mx >= cMin.x && mx <= cMax.x)
                    dl->AddLine(ImVec2(mx, topMinorY0),
                                ImVec2(mx, p0.y + rulerW), cTick);
            }
        }
        // Left ruler: number LEFT of the major tick, rotated vertically.
        float dyStart = std::floor(S2D(cMin).y / majorDoc) * majorDoc;
        for (float dy = dyStart; ; dy += majorDoc) {
            float sy = D2S(ImVec2(0, dy)).y;
            if (sy > cMax.y) break;
            if (sy >= cMin.y) {
                dl->AddLine(ImVec2(lftMajorX0, sy),
                            ImVec2(p0.x + rulerW, sy), cTick);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d",
                              (int)std::lround((dy - rulerRefPx.y) / u.pxPer));
                verticalText(lftLabelCX, sy, cText, buf);
            }
            for (int k = 1; k < u.minors; ++k) {
                float my = D2S(ImVec2(0, dy + k * majorDoc / u.minors)).y;
                if (my >= cMin.y && my <= cMax.y)
                    dl->AddLine(ImVec2(lftMinorX0, my),
                                ImVec2(p0.x + rulerW, my), cTick);
            }
        }
    }

    // Corner square: opaque (unit name must stay readable). Click to cycle.
    // dl->AddRectFilled(ImVec2(p0.x, p0.y),
    //                   ImVec2(p0.x + rulerW, p0.y + rulerW), cBgFull); //Background fully opaque under the text, but the rest of the rulers are translucent so the canvas shows through faintly.
    dl->AddRectFilled(ImVec2(p0.x, p0.y),
                      ImVec2(p0.x + rulerW, p0.y + rulerW), cBg); //Background translucent, but with the opacity of both of the rulers behind the text is still readable.
    {
        ImVec2 ts = ImGui::CalcTextSize(u.name);
        dl->AddText(ImVec2(p0.x + (rulerW - ts.x) * 0.5f,
                           p0.y + (rulerW - ts.y) * 0.5f), cText, u.name);
        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton("##unitsq", ImVec2(rulerW, rulerW)))
            st.docUnit = (st.docUnit + 1) %
                         (int)(sizeof(kUnits)/sizeof(kUnits[0]));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            char tip[64];
            std::snprintf(tip, sizeof(tip), "Unit: %s  (click to cycle)", u.name);
            UI::DrawTooltip(tip, ImGui::GetIO().MousePos);
        }
    }

    // Blue cursor guides — only while the mouse is genuinely in the canvas.
    if (scopeHovered) {
        dl->AddLine(ImVec2(m.x, p0.y), ImVec2(m.x, p0.y + rulerW), cGuide, 1.5f);
        dl->AddLine(ImVec2(p0.x, m.y), ImVec2(p0.x + rulerW, m.y), cGuide, 1.5f);
        dl->AddLine(ImVec2(m.x, cMin.y), ImVec2(m.x, cMax.y),
                    (cGuide & 0x00FFFFFF) | 0x33000000);
        dl->AddLine(ImVec2(cMin.x, m.y), ImVec2(cMax.x, m.y),
                    (cGuide & 0x00FFFFFF) | 0x33000000);
    }

    // ── Drawing tools + selection overlay ───────────────────────────────────
    // Converters in RAW document units (the camera's D2S/S2D work in pxPer-
    // scaled space; the document stores raw units, so fold u.pxPer in/out here).
    {
        auto d2sDoc = [&](Renderer::Vec2 d) {
            return D2S(ImVec2(d.x * u.pxPer, d.y * u.pxPer));
        };
        auto s2dDoc = [&](ImVec2 s) {
            ImVec2 sc = S2D(s);
            return Renderer::Vec2{ sc.x / u.pxPer, sc.y / u.pxPer };
        };
        const float effZoom = st.zoom * u.pxPer;

        // Cache the hovered cursor in RAW doc-units so globally-dispatched edit
        // actions (E extrude) can read its direction relative to a vertex.
        if (scopeHovered) { lastHoverDoc_ = s2dDoc(ImGui::GetIO().MousePos);
                            lastHoverValid_ = true; }

        // Selection highlight: outline every selected shape (the active one in a
        // brighter tint), plus the object-origin point (orange). Chrome → tokens.
        {
            using DesignSystem::Tok;
            ImU32 accent = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Accent_Default));
            // Shared active/selected state cues, now resolved from design tokens:
            // active = orange (on-page) / violet (loose); selected = the theme
            // accent (on-page) / a softer violet (loose). Coherent with the
            // Outliner and Edit mode, which read the same S_State_* tokens.
            auto stateActive = [&](bool loose) {
                return ImGui::GetColorU32(ds.GetColor(
                    loose ? Tok::S_State_Active_Loose : Tok::S_State_Active_OnPage));
            };
            auto stateSelected = [&](bool loose) {
                // On-page selected (not active) = darker orange, matching Edit mode
                // and the Outliner (was the blue accent before).
                return ImGui::GetColorU32(ds.GetColor(
                    loose ? Tok::S_State_Selected_Loose : Tok::S_State_Selected_OnPage));
            };
            const uint64_t activeId = project_.document.ActiveId();

            // Draw one object's outline (optional) + its origin dot.
            auto drawObject = [&](uint64_t id, bool outline, bool active) {
                Renderer::Shape* sel = project_.document.FindShape(id);
                if (!sel) return;
                const bool loose = project_.document.IsLooseShape(id);
                ImU32 c = active ? stateActive(loose) : stateSelected(loose);
                Renderer::Vec2 po = CurPageOriginOfShape(id);   // display origin ({0,0} if loose)
                if (outline)
                    for (const Renderer::Part& part : sel->parts) {
                        // Outline EVERY subpath (strand) of a branched path, so the
                        // whole object highlights — not just the first strand.
                        const int subs = Renderer::Tessellator::SubpathCount(part);
                        for (int sp = 0; sp < subs; ++sp) {
                            bool closed = false;
                            std::vector<Renderer::Vec2> poly =
                                Renderer::Tessellator::OutlinePartSub(*sel, part, sp, effZoom, closed, po);
                            if (poly.size() < 2) continue;
                            size_t segs = closed ? poly.size() : poly.size() - 1;
                            for (size_t i = 0; i < segs; ++i)
                                dl->AddLine(d2sDoc(poly[i]), d2sDoc(poly[(i + 1) % poly.size()]),
                                            c, 1.5f);
                        }
                    }
                // Object origin dot in world space (orange / violet via S_State_*).
                Renderer::Vec2 ow = Renderer::Tessellator::WorldTransform(*sel, sel->origin, po);
                ImVec2 op = d2sDoc(ow);
                // PARENT relationship line (Blender): dashed origin→parent-origin, so
                // the hierarchy reads in the canvas. Drawn under the origin dot.
                if (sel->parentId) {
                    if (Renderer::Shape* par = project_.document.FindShape(sel->parentId)) {
                        Renderer::Vec2 ppo = CurPageOriginOfShape(par->id);
                        Renderer::Vec2 pw = Renderer::Tessellator::WorldTransform(*par, par->origin, ppo);
                        ImVec2 pp = d2sDoc(pw);
                        ImU32 relCol = ImGui::GetColorU32(ds.GetColor(Tok::C_Viewport_Guide));
                        // Dashed segment a→b.
                        ImVec2 a = op, b = pp;
                        float len = std::hypot(b.x - a.x, b.y - a.y);
                        if (len > 1.0f) {
                            ImVec2 dir{ (b.x - a.x) / len, (b.y - a.y) / len };
                            const float dash = 5.0f, gap = 4.0f;
                            for (float t = 0.0f; t < len; t += dash + gap) {
                                float t2 = std::min(t + dash, len);
                                dl->AddLine(ImVec2(a.x + dir.x * t,  a.y + dir.y * t),
                                            ImVec2(a.x + dir.x * t2, a.y + dir.y * t2), relCol, 1.0f);
                            }
                        }
                    }
                }
                dl->AddCircleFilled(op, 4.0f, stateActive(loose));
                dl->AddCircle(op, 4.0f, ImGui::GetColorU32(ds.GetColor(
                    Tok::C_Viewport_OriginOutline)));
            };

            bool activeInSel = false;
            for (uint64_t selId : project_.document.Selection()) {
                if (selId == activeId) activeInSel = true;
                drawObject(selId, /*outline=*/true, /*active=*/(selId == activeId));
            }
            // Blender-style: the active object stays active even after deselecting
            // (it's no longer in the selection). Still show its origin dot — no
            // outline, since it isn't selected — so the pivot reference is visible.
            if (activeId && !activeInSel)
                drawObject(activeId, /*outline=*/false, /*active=*/true);

            // ── Drag-to-page PREVIEW (shared destination, per-viewport draw) ──
            // The OWNER viewport (the one driving the move) decides the target
            // page (in ITS layout) and publishes it to dropPreview_; EVERY
            // viewport then draws the feedback on THAT page at its OWN display
            // position. So all viewports agree on the destination even with
            // different layouts (a foreign viewport never picks its own target).
            const bool movingObjects =
                (toolState_.gesture == ToolGesture::MoveObjects) ||
                (transformOp_.Active() && !transformOp_.element &&
                 transformOp_.kind == TransformKind::Move);
            const bool isOwner =
                (toolState_.gesture == ToolGesture::MoveObjects && toolState_.owner == &st) ||
                (transformOp_.Active() && transformOp_.owner == &st);

            // 1) OWNER computes the destination page (in its display space).
            if (movingObjects && isOwner && activeId) {
                if (Renderer::Shape* act = project_.document.FindShape(activeId)) {
                    const int srcAb = project_.document.ArtboardOfShape(activeId);
                    const Renderer::Vec2 srcPo = CurPageOrigin(srcAb);
                    const Renderer::Vec2 ow =
                        Renderer::Tessellator::WorldTransform(*act, act->origin, srcPo);
                    int dstAb = -1;
                    const auto& abs = project_.document.artboards;
                    for (int k = (int)abs.size() - 1; k >= 0; --k) {
                        if (!CurPageVisible(k)) continue;
                        Renderer::Vec2 po = CurPageOrigin(k);
                        const auto& ab = abs[(size_t)k];
                        if (ow.x >= po.x && ow.x <= po.x + ab.size.x &&
                            ow.y >= po.y && ow.y <= po.y + ab.size.y) { dstAb = k; break; }
                    }
                    dropPreview_.active   = true;
                    dropPreview_.activeId = activeId;
                    dropPreview_.srcAb    = srcAb;
                    dropPreview_.dstAb    = dstAb;
                    dropPreview_.keepPage = io.KeyAlt;   // Alt = keep on its page
                    // On drop (keepWorldPos) the owner shifts translate by
                    // displayA − displayD to keep the object visually put. Publish
                    // that so every viewport can place the preview where it will
                    // actually land in its own layout.
                    dropPreview_.rebase = (dstAb >= 0)
                        ? Renderer::Vec2{ srcPo.x - CurPageOrigin(dstAb).x,
                                          srcPo.y - CurPageOrigin(dstAb).y }
                        : Renderer::Vec2{0, 0};
                }
            }

            // 2) ANY viewport draws the feedback using the SHARED decision, but
            //    at THIS viewport's display positions.
            if (dropPreview_.active && movingObjects) {
                Renderer::Shape* act = project_.document.FindShape(dropPreview_.activeId);
                const int srcAb = dropPreview_.srcAb, dstAb = dropPreview_.dstAb;
                const bool keepPage = dropPreview_.keepPage;
                const bool willMoveTo = !keepPage && dstAb >= 0 && dstAb != srcAb;
                // Page to FRAME (and where the object lands):
                //  • normal drop onto another page → that page D (accent, filled),
                //  • Alt over a foreign page → the SOURCE page (amber, outline
                //    only — it would sit behind the page on top).
                int frameAb = -1; bool outlineOnly = false; ImU32 hl = accent;
                if (willMoveTo) { frameAb = dstAb; hl = accent; }
                else if (keepPage && dstAb >= 0 && dstAb != srcAb) {
                    frameAb = srcAb; outlineOnly = true;
                    hl = ImGui::GetColorU32(ds.GetColor(Tok::S_State_Active_OnPage));
                }
                if (act && frameAb >= 0 && CurPageVisible(frameAb)) {
                    const auto& abs = project_.document.artboards;
                    Renderer::Vec2 fpo = CurPageOrigin(frameAb);     // page rect
                    const auto& fab = abs[(size_t)frameAb];
                    ImVec2 a = d2sDoc({ fpo.x, fpo.y });
                    ImVec2 b = d2sDoc({ fpo.x + fab.size.x, fpo.y + fab.size.y });
                    dl->AddRect(a, b, hl, 0.0f, 0, 2.0f);
                    dl->AddRectFilled(a, b, (hl & 0x00FFFFFF) | 0x1A000000);

                    // The object lands at the framed page's display origin PLUS
                    // the owner's rebase (for a move; rebase is 0 for Ctrl which
                    // keeps it on the source page). So it appears exactly where
                    // it will be after the drop, in THIS viewport's layout.
                    Renderer::Vec2 objPo = willMoveTo
                        ? Renderer::Vec2{ fpo.x + dropPreview_.rebase.x,
                                          fpo.y + dropPreview_.rebase.y }
                        : fpo;
                    ImVec2 clipA(std::min(a.x, b.x), std::min(a.y, b.y));
                    ImVec2 clipB(std::max(a.x, b.x), std::max(a.y, b.y));
                    dl->PushClipRect(clipA, clipB, true);
                    for (const Renderer::Part& part : act->parts) {
                        bool cl = false;
                        std::vector<Renderer::Vec2> poly =
                            Renderer::Tessellator::OutlinePart(*act, part, effZoom, cl, objPo);
                        if (poly.size() < 3) continue;
                        std::vector<ImVec2> sp; sp.reserve(poly.size());
                        for (auto& p : poly) sp.push_back(d2sDoc(p));
                        if (part.fill.enabled && cl && !outlineOnly) {
                            const auto& fc = part.fill.color;
                            ImU32 fill = ImGui::GetColorU32(
                                ImVec4(fc.r, fc.g, fc.b, fc.a * 0.7f));
                            dl->AddConvexPolyFilled(sp.data(), (int)sp.size(), fill);
                        }
                        size_t segs = cl ? sp.size() : sp.size() - 1;
                        for (size_t i = 0; i < segs; ++i)
                            dl->AddLine(sp[i], sp[(i + 1) % sp.size()], hl, 1.5f);
                    }
                    dl->PopClipRect();
                }
            }
        }

        // ── 2D cursor (Blender-style): a red/white ringed crosshair. Moved
        // with Shift+RMB (press or drag) anywhere on the canvas. ──────────────
        // The cursor always HAS a position (transforms/snaps keep using it); the
        // top-bar toggle (show2DCursor_) only hides its drawing.
        if (scopeHovered && io.KeyShift &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            project_.document.cursor = s2dDoc(io.MousePos);
        }
        if (show2DCursor_) {
            ImVec2 cp = d2sDoc(project_.document.cursor);
            // All four crosshair ticks share the SAME geometry: a segment from
            // the inner radius `ri` to the outer length `ro`, centred on `cp`, so
            // the ring stays concentric and no tick looks longer than another.
            // −X/−Y are black (readable on the white page); +X red, +Y green
            // (axes, for future rotation reference).
            const float rr = 9.0f, ri = 3.0f, ro = 14.0f;
            ImU32 cw     = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorRing));
            ImU32 cr     = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorRingAccent));
            ImU32 cBlack = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorTick));
            ImU32 axisX  = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorAxisX)); // +X red
            ImU32 axisY  = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorAxisY)); // +Y green
            dl->AddCircle(cp, rr, cw, 24, 3.0f);
            dl->AddCircle(cp, rr, cr, 24, 1.5f);
            // The crosshair ticks follow the cursor's ORIENTATION so the +X/+Y axes
            // show the "Cursor" transform-orientation frame. Rotate each tick dir by
            // the cursor's screen angle (doc Y is down, so a positive doc rotation is
            // CW on screen — the same convention object rotation uses here).
            float crot = project_.document.cursorRotation;
            float cc = std::cos(crot), cs = std::sin(crot);
            auto tick = [&](float dirx, float diry, float r0, float r1, ImU32 col) {
                ImVec2 d{ dirx * cc - diry * cs, dirx * cs + diry * cc };   // rotated unit dir
                dl->AddLine(ImVec2(cp.x + d.x * r0, cp.y + d.y * r0),
                            ImVec2(cp.x + d.x * r1, cp.y + d.y * r1), col, 1.8f);
            };
            tick(-1, 0, ri, ro, cBlack);   // −X
            tick(0, -1, ri, ro, cBlack);   // −Y
            tick(+1, 0, ri, ro, axisX);    // +X
            tick(0, +1, ri, ro, axisY);    // +Y
        }

        // Publish THIS leaf's canvas rect (screen px) so the modal transform /
        // grab-move can wrap the cursor within these bounds (per-zone infinite
        // drag), regardless of which zone the gesture started in.
        gestureCanvasMin_ = cMin;
        gestureCanvasMax_ = cMax;

        // A modal G/R/S transform has TOP priority and always runs while active
        // (even if a popup happens to be open) so its guide/cursor/wrap never
        // freeze. It owns the input until confirmed/cancelled.
        const bool popupOpen = ImGui::IsPopupOpen("##viewportCtx") ||
                               ImGui::IsPopupOpen("##editCtx") ||
                               ImGui::IsPopupOpen("##snapPie") ||
                               ImGui::IsPopupOpen("##mergeMenu") ||
                               ImGui::IsPopupOpen("##handleMenu") ||
                               ImGui::IsPopupOpen("##pageCtx") ||
                               ImGui::IsPopupOpen("##addMenu");
        const bool editMode = (editorMode_ == EditorMode::Edit);

        // In Edit Mode, draw the vertex/edge/face overlay every frame.
        if (editMode) DrawEditOverlay(d2sDoc, effZoom, dl);

        // Interactive thumbnail "Zone" crop has TOP priority (like a transform):
        // it draws/edits the crop rectangle and owns the input (Enter confirms,
        // Esc/RMB cancels) until done. ONE zone owns it (multi-viewport safe):
        // the first hovered leaf claims ownership; only that leaf runs/draws the
        // crop. Every other Viewport is frozen (no tools) while a crop is live.
        const bool cropping = (cropArtboard_ >= 0);
        if (cropping && cropOwner_ == nullptr && scopeHovered)
            cropOwner_ = &st;                          // claim ownership

        // ── Free page move (drag started on a page name label) ────────────────
        // Highest priority, owned by one leaf. The page pos follows the cursor;
        // its page-relative objects move with it automatically. Released on LMB
        // up; cancelled (restored) on Esc / RMB.
        const bool pageMoving = (pageDrag_ >= 0);
        if (pageMoving && pageDragOwner_ == &st) {
            if (pageDrag_ < (int)project_.artboards().size()) {
                Artboard& pab = project_.artboards()[(size_t)pageDrag_];
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    pab.pos = pageDragPos0_;            // cancel → restore
                    pageDrag_ = -1; pageDragOwner_ = nullptr;
                    rmbConsumedByTransform_ = true;
                } else {
                    Renderer::Vec2 cur = s2dDoc(io.MousePos);
                    pab.pos = { pageDragPos0_.x + (cur.x - pageDragRef_.x),
                                pageDragPos0_.y + (cur.y - pageDragRef_.y) };
                    project_.dirty = true;
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                        pageDrag_ = -1; pageDragOwner_ = nullptr;
                    }
                }
            } else { pageDrag_ = -1; pageDragOwner_ = nullptr; }
        }

        // A modal G/R/S transform has TOP priority and always runs while active
        // (even if a popup happens to be open) so its guide/cursor/wrap never
        // freeze. It owns the input until confirmed/cancelled.
        // ── Preview placement ─────────────────────────────────────────────────
        // When armed (Inputs pref / IOF), the chosen object follows the mouse and
        // is dropped on click. It owns the canvas (no tools) while live.
        const bool placing = placement_.armed;
        if (placing) {
            UpdatePlacement(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if (pageMoving) {
            // page move owns the input; nothing else this frame.
        } else if (cropping) {
            if (cropOwner_ == &st)
                HandleThumbnailCrop(d2sDoc, s2dDoc, u.pxPer, scopeHovered, dl);
        } else if (handleOp_.Active()) {
            UpdateHandleTransform(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if (transformOp_.Active()) {
            UpdateTransformOp(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            // Violet preview of all possible snap points (hidden once a snap engages).
            DrawSnapCandidates(d2sDoc, effZoom, cMin, cMax, dl);
            // Snap indicator: an orange-selection glyph whose SHAPE tells the snap
            // type — circle (vertex), triangle (edge center), square (grid), diamond
            // (edge), small circle (face/other). Drawn for geometry/grid snaps only
            // (Increment never sets showMark).
            DrawSnapIndicatorGlyph(d2sDoc, dl, gs);
        // A canvas gesture (box-select / move) already owned by THIS leaf must keep
        // running each frame until the button is released — even though dragging
        // inside the zone's child window makes ImGui report IsAnyItemActive() (the
        // child becomes the active item for scroll-drag), which ToolsBlockedByOther-
        // Action() would otherwise treat as "blocked". Without this, the gesture is
        // only advanced on the press and release frames, so the box-select rectangle
        // never updates mid-drag (it flashes once on release). Route to the handler
        // that owns the gesture: Edit Mode uses editDrag_, Object Mode toolState_.
        } else if (toolState_.Active() && toolState_.owner == &st &&
                   (toolState_.gesture == ToolGesture::Polyline ||
                    toolState_.gesture == ToolGesture::Bezier)) {
            // A curve in progress keeps running in either mode until finished.
            HandleCurveTool(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if (editMode && editDrag_.Active() && editDrag_.owner == &st) {
            HandleEditMode(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if (!editMode && toolState_.Active() && toolState_.owner == &st) {
            HandleViewportTools(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if ((markDrag_.active || markDrag_.armed || markBox_.active ||
                    markGrab_.Active()) &&
                   Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.linemark") {
            // A line-mark drag / box-select / modal G-S is in progress: keep driving
            // the tool every frame while it runs (ToolsBlockedByOtherAction() is true
            // mid-drag because the child window becomes the active item), so the ghost
            // follows the cursor and the op commits on release — like box-select/move.
            HandleLineMarkTool(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        } else if (!popupOpen && !ToolsBlockedByOtherAction()) {
            // Tools are now just Select + Cursor + Curve (object creation moved to
            // Shift+A). Cursor moves the 2D cursor in BOTH modes; Curve draws a new
            // Bézier (Edit Mode); otherwise Edit Mode routes to the element editor
            // and Object Mode to the select tool.
            const std::string tool =
                Shortcuts::Tools::ToolManager::Instance().GetActiveTool();
            if (tool == "tool.cursor")
                HandleViewportTools(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            else if (tool == "tool.linemark")
                HandleLineMarkTool(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            else if (tool == "tool.curve" && activeCapabilities_.curveTool)
                HandleCurveTool(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            else if (tool == "tool.extrude")
                HandleExtrudeTool(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            else if (editMode)
                HandleEditMode(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
            else
                HandleViewportTools(st, s2dDoc, d2sDoc, effZoom, scopeHovered, dl);
        }

        // Shift+S pie menu (opened by the shortcut). Lives in this zone's window.
        if (pieMenuRequest_ && scopeHovered) {
            ImGui::OpenPopup("##snapPie");
            pieMenuRequest_ = false;
        }
        RenderViewportPieMenu();

        // Shift+A "Add" menu (opened by the shortcut), in the hovered zone.
        // IMPORTANT: only the HOVERED leaf consumes the request. Clearing it
        // unconditionally let the FIRST leaf in render order reset the flag before
        // a different hovered leaf could open the menu — so menus only worked in the
        // top-left viewport with several open.
        if (addMenuRequest_ && scopeHovered) {
            addMenuPos_ = io.MousePos;
            addMenuState_ = &st;          // remember the leaf (for "New Document")
            ImGui::OpenPopup("##addMenu");
            addMenuRequest_ = false;
        }
        RenderAddMenu();

        // Shift+G "Select Grouped" picker (Object mode), opened by the shortcut.
        if (selectGroupedMenuRequest_ && scopeHovered) {
            selectGroupedMenuPos_ = io.MousePos;
            ImGui::OpenPopup("##selectGroupedMenu");
            selectGroupedMenuRequest_ = false;
        }
        RenderSelectGroupedMenu();

        // M merge menu / V handle-type menu (Edit Mode), opened by shortcuts.
        if (mergeMenuRequest_ && scopeHovered && editMode) {
            editMenuPos_ = io.MousePos;
            ImGui::OpenPopup("##mergeMenu");
            mergeMenuRequest_ = false;
        }
        RenderMergeMenu();
        if (handleMenuRequest_ && scopeHovered && editMode) {
            editMenuPos_ = io.MousePos;
            ImGui::OpenPopup("##handleMenu");
            handleMenuRequest_ = false;
        }
        RenderHandleTypeMenu();

        // Page context menu (right-click on a page's name label, captured in the
        // artboard-chrome pass above). Opened here so OpenPopup runs in the
        // hovered zone's window. Consumes the RMB so the object/edit menu below
        // does not also fire for the same click.
        if (pageCtxRequest_) { ImGui::OpenPopup("##pageCtx"); pageCtxRequest_ = false; }
        RenderPageContextMenu();
        const bool pageMenuOpen = ImGui::IsPopupOpen("##pageCtx");

        // Right-click: if a gesture is in progress, CANCEL it (Blender-style);
        // otherwise open the context menu at the cursor (object or edit). Never
        // both. Shift+RMB is the 2D-cursor move (handled above), so ignore here.
        // Skip while cropping (RMB cancels the crop) or when the page menu just
        // opened on this click (right-click was on a page name).
        if (scopeHovered && !io.KeyShift && !rmbConsumedByTransform_ && !popupOpen &&
            !cropping && !pageMoving && !pageMenuOpen && !outlinerPickingState_ &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            bool cancelled = CancelViewportGesture();
            if (!cancelled && editDrag_.Active()) { editDrag_.Reset(); cancelled = true; }
            if (!cancelled) {
                viewportMenuPos_ = editMenuPos_ = io.MousePos;
                ImGui::OpenPopup(editMode ? "##editCtx" : "##viewportCtx");
            }
        }
        rmbConsumedByTransform_ = false;  // one-frame flag
        if (editMode) RenderEditContextMenu();
        else          RenderViewportContextMenu();
    }

    // Floating tool palette pinned to the left, inside the canvas.
    RenderToolbarInto(ImVec2(cMin.x, cMin.y), st);

    // Operator redo panel (bottom-left), only in the hovered/focused viewport so a
    // single panel shows even with several Viewport zones open.
    if (zoneLayout_.HoveredEditorState() == &st ||
        zoneLayout_.HoveredEditorState() == nullptr)
        DrawOperatorPanel(cMin, cMax, st);

    // ── Outliner sync target picking (the "synchronise" button in the Outliner)
    // While picking, every hovered Viewport paints an orange full-zone preview
    // and consumes a left-click to become the sync target. The cancel (RMB /
    // Escape), the follow-mouse tooltip, the button's disabled state and the
    // target's liveness check are handled centrally in RenderOutliner (it asks
    // the layout directly, so it doesn't depend on render order).
    if (outlinerPickingState_ && outlinerPickingState_->syncPicking && scopeHovered) {
        auto& ds2 = DesignSystem::DesignSystem::Instance();
        // Same geometry as the editor-tab drop preview: full zone inset by a small
        // margin, rounded like the window. Colour = notice (orange) + an opacity
        // token, instead of the white drop fill.
        const float inset = ds2.GetFloat(DesignSystem::Tok::C_ZoneTab_DropCenterInset) * gs;
        const float rnd   = ds2.GetFloat(DesignSystem::Tok::C_Window_CornerRadius) * gs;
        ImVec4 orange = ds2.GetColor(DesignSystem::Tok::S_Color_Notice_Default);
        orange.w      = ds2.GetFloat(DesignSystem::Tok::S_Opacity_Faint);
        dl->AddRectFilled(ImVec2(cMin.x + inset, p0.y + inset),
                          ImVec2(cMax.x - inset, cMax.y - inset),
                          ImGui::ColorConvertFloat4ToU32(orange), rnd);
        // Click (left, since the button press) confirms the sync to THIS viewport
        // for the Outliner that armed the pick.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            outlinerPickingState_->syncTarget  = &st;
            outlinerPickingState_->syncPicking = false;
            outlinerPickingState_ = nullptr;
        }
    }

    // Right-side reusable side panel (vertical tabs; Pages tab). N toggles the
    // full panel (stage 2 ⇄ 0) when this leaf is hovered.
    if (scopeHovered && ImGui::IsKeyPressed(ImGuiKey_N, false) &&
        !ImGui::GetIO().WantTextInput)
        st.sidePanel.stage = (st.sidePanel.stage == 2) ? 0 : 2;
    RenderViewportSidePanel(st, cMin, cMax);

    // The active module may paint a canvas overlay (e.g. IOF course line). Give
    // it this viewport's doc→screen mapping (raw doc units → screen px) so it can
    // place markers at real document positions under the current pan/zoom.
    if (activeModule_) {
        auto docToScreen = [&](ImVec2 d) {
            return D2S(ImVec2(d.x * u.pxPer, d.y * u.pxPer));
        };
        activeModule_->DrawViewportOverlay(cMin, cMax, docToScreen);
    }

    // New-artboard popup (opened from the top-bar + button or the shortcut).
    // The page is added to the SHARED project, so it appears in every
    // Viewport zone and in the Outliner — not just this leaf.
    if (st.openNewDoc) { ImGui::OpenPopup("New Document");
                         st.openNewDoc = false; }
    ImVec2 newSz{0, 0};
    if (PageSizePopup("New Document", "Create", newSz)) {
        // Lay pages side by side: place the new one to the right of the
        // existing bounding box (in doc-units), 40 px gutter.
        float x0 = 0.0f;
        if (!project_.artboards().empty()) {
            float maxR = -1e9f;
            for (const Artboard& ab : project_.artboards())
                maxR = std::max(maxR, ab.pos.x + ab.size.x);
            x0 = maxR + 40.0f;
        }
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Page %d",
                      (int)project_.artboards().size() + 1);
        project_.AddArtboard(nm, ImVec2(x0, 0.0f), newSz);
        st.reqFitDoc = true;   // frame the updated project in THIS view
    }

    // "Resize Page" popup (opened from the page context menu). Reuses the page-
    // size popup, pre-filled with the target page's current dimensions, and
    // resizes that artboard in place (keeping its top-left position).
    if (resizePageRequest_) {
        ImGui::OpenPopup("Resize Page");
        resizePageRequest_ = false;
    }
    ImVec2 resizeSz{0, 0};
    if (resizePageArtboard_ >= 0 &&
        resizePageArtboard_ < (int)project_.artboards().size())
        resizeSz = ImVec2(project_.artboards()[resizePageArtboard_].size.x,
                          project_.artboards()[resizePageArtboard_].size.y);
    if (PageSizePopup("Resize Page", "Resize", resizeSz)) {
        if (resizePageArtboard_ >= 0 &&
            resizePageArtboard_ < (int)project_.artboards().size()) {
            Artboard& ab = project_.artboards()[(size_t)resizePageArtboard_];
            ab.size = { resizeSz.x, resizeSz.y };
            project_.dirty = true;
        }
    }

    // "Rename Page" popup (from the page context menu): a single InputText seeded
    // with the page's current name; Enter or the Rename button commits.
    if (renamePageRequest_) { ImGui::OpenPopup("Rename Page"); renamePageRequest_ = false; }
    {
        auto& ds = DesignSystem::DesignSystem::Instance();
        ImGui::PushStyleColor(ImGuiCol_PopupBg,
            ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
        if (ImGui::BeginPopup("Rename Page")) {
            ImGui::TextUnformatted("Rename page");
            ImGui::SetNextItemWidth(220.0f);
            bool commit = ImGui::InputText("##pgname", renamePageBuf_,
                                           sizeof(renamePageBuf_),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Rename")) commit = true;
            if (commit) {
                if (renamePageArtboard_ >= 0 &&
                    renamePageArtboard_ < (int)project_.artboards().size()) {
                    project_.artboards()[(size_t)renamePageArtboard_].name = renamePageBuf_;
                    MarkUndoLabel("Rename page");
                    project_.dirty = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(size);
}

// ── Viewport side panel (the reusable UI::EditorSidePanel) ───────────────────
// The Viewport only supplies the editor-SPECIFIC content: a "Pages" tab (shown
// in Single* layout modes) that switches the displayed page/spread and toggles
// the cover / orphan-objects options. All the panel chrome (stages, tab bar,
// drag, transparency) lives in the reusable widget.

void Application::RenderViewportSidePanel(EditorState& st, ImVec2 cMin, ImVec2 cMax) {
    auto& doc = project_.document;
    const bool single = st.pageLayout.mode == PageLayoutMode::SinglePage ||
                        st.pageLayout.mode == PageLayoutMode::SingleBookLeft ||
                        st.pageLayout.mode == PageLayoutMode::SingleBookRight;
    std::vector<UI::SidePanelTab> tabs;
    if (single) {
        UI::SidePanelTab pages; pages.name = "Pages";
        pages.draw = [this, &st, &doc](ImVec2 conMin, ImVec2 conMax) {
            RenderViewportPagesTab(st, conMin, conMax);
        };
        tabs.push_back(std::move(pages));
    }
    // The active module may append its own viewport tabs (e.g. IOF "Map elements").
    if (activeModule_) activeModule_->ViewportSidePanelTabs(tabs);
    UI::EditorSidePanel("##viewportSide", cMin, cMax, st.sidePanel, tabs);
}

// The "Pages" tab body: cover/orphan toggles + a list of pages/spreads, each
// with its name and a white page-preview rect; click switches the shown spread.
void Application::RenderViewportPagesTab(EditorState& st, ImVec2 conMin, ImVec2 conMax) {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    auto& doc = project_.document;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gs = ds.GetGlobalScale();
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    auto col = [&](DesignSystem::Tok t, float a){ ImVec4 c = ds.GetColor(t);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)); };
    ImU32 txt    = ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    ImU32 txtSub = ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));

    const float pad = 8.0f * gs;
    float y = conMin.y + pad;
    // Top controls: spread "cover" toggle (book modes) + orphans-visibility.
    const bool book = st.pageLayout.mode == PageLayoutMode::SingleBookLeft ||
                      st.pageLayout.mode == PageLayoutMode::SingleBookRight;
    auto topToggle = [&](const char* label, bool& flag) {
        ImVec2 rmin(conMin.x + pad, y), rmax(conMax.x - pad, y + rowH);
        ImGui::SetCursorScreenPos(rmin);
        ImGui::InvisibleButton(label, ImVec2(rmax.x - rmin.x, rowH));
        if (ImGui::IsItemClicked()) flag = !flag;
        if (ImGui::IsItemHovered()) dl->AddRectFilled(rmin, rmax, col(DesignSystem::Tok::C_Outliner_Row_Hover, 0.4f), 3.0f*gs);
        // a small check box
        ImVec2 bx(rmin.x + 2.0f*gs, rmin.y + (rowH - rowH*0.55f)*0.5f);
        ImVec2 bxe(bx.x + rowH*0.55f, bx.y + rowH*0.55f);
        dl->AddRect(bx, bxe, txtSub, 2.0f*gs);
        if (flag) dl->AddRectFilled(ImVec2(bx.x+2,bx.y+2), ImVec2(bxe.x-2,bxe.y-2),
                                    ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default)), 1.0f);
        dl->AddText(ImVec2(bxe.x + 6.0f*gs, rmin.y + (rowH-ImGui::GetTextLineHeight())*0.5f), txt, label);
        y += rowH + 2.0f * gs;
    };
    if (book) topToggle("First page as cover", st.pageLayout.spreadCover);
    topToggle("Show orphan objects", st.nPanelShowOrphans);
    dl->AddLine(ImVec2(conMin.x + pad, y), ImVec2(conMax.x - pad, y), txtSub, 1.0f);
    y += 4.0f * gs;

    // The eligible pages (document-visible + per-view), grouped into spreads.
    std::vector<int> elig;
    for (int i = 0; i < (int)doc.artboards.size(); ++i) {
        const auto& ab = doc.artboards[(size_t)i];
        bool hidden = std::find(st.pageLayout.hiddenPages.begin(),
            st.pageLayout.hiddenPages.end(), ab.id) != st.pageLayout.hiddenPages.end();
        if (ab.pageVisible && !hidden) elig.push_back(i);
    }
    const int cnt = App::SpreadCount((int)elig.size(), book, book && st.pageLayout.spreadCover);
    st.pageLayout.pageIndex = std::clamp(st.pageLayout.pageIndex, 0, std::max(0, cnt - 1));
    ImU32 hov  = col(DesignSystem::Tok::C_Outliner_Row_Hover, 0.5f);
    ImU32 selC = col(DesignSystem::Tok::C_Outliner_Row_Active, 0.9f);
    const float prevH = 64.0f * gs;     // page-preview height per entry

    for (int s = 0; s < cnt; ++s) {
        int start = 0, count = 0;
        App::SpreadRange((int)elig.size(), book, book && st.pageLayout.spreadCover, s, start, count);
        float entryH = rowH + prevH + 6.0f * gs;
        ImVec2 rmin(conMin.x + pad, y), rmax(conMax.x - pad, y + entryH);
        ImGui::SetCursorScreenPos(rmin);
        ImGui::InvisibleButton((std::string("##np") + std::to_string(s)).c_str(),
                               ImVec2(rmax.x - rmin.x, entryH));
        bool rh = ImGui::IsItemHovered();
        bool isCur = (st.pageLayout.pageIndex == s);
        if (isCur)    dl->AddRectFilled(rmin, rmax, selC, 4.0f * gs);
        else if (rh)  dl->AddRectFilled(rmin, rmax, hov, 4.0f * gs);
        if (ImGui::IsItemClicked()) st.pageLayout.pageIndex = s;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && start < (int)elig.size()) {
            pageCtxRequest_ = true; pageCtxArtboard_ = elig[(size_t)start]; pageCtxPos_ = io.MousePos;
        }
        // Name(s).
        std::string label;
        for (int k = 0; k < count && start + k < (int)elig.size(); ++k) {
            if (k) label += "  |  ";
            label += doc.artboards[(size_t)elig[(size_t)(start + k)]].name;
        }
        dl->AddText(ImVec2(rmin.x + 6.0f * gs, rmin.y + 3.0f * gs), txt, label.c_str());
        // Preview(s): the page's white rect at aspect ratio, side by side.
        float py = rmin.y + rowH + 2.0f * gs;
        float availW = (rmax.x - rmin.x) - 12.0f * gs;
        float slotW = (count > 1) ? (availW - 4.0f * gs) / (float)count : availW;
        float px = rmin.x + 6.0f * gs;
        for (int k = 0; k < count && start + k < (int)elig.size(); ++k) {
            const auto& ab = doc.artboards[(size_t)elig[(size_t)(start + k)]];
            float ar = (ab.size.y > 1.0f) ? ab.size.x / ab.size.y : 1.0f;
            float pw2 = std::min(slotW, prevH * ar), ph2 = std::min(prevH, slotW / std::max(0.01f, ar));
            ImVec2 a(px, py), b(px + pw2, py + ph2);
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ds.GetColor(
                DesignSystem::Tok::C_Viewport_ThumbnailBackground)), 2.0f * gs);
            dl->AddRect(a, b, ImGui::GetColorU32(ds.GetColor(
                DesignSystem::Tok::C_Viewport_ThumbnailBorder)), 2.0f * gs);
            px += slotW + 4.0f * gs;
        }
        y += entryH + 4.0f * gs;
    }
}

// ── Outliner: shared project → artboards (→ objects later) ────────────────────

} // namespace App

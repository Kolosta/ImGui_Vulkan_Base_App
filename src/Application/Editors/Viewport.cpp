#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {
void Application::RenderToolbarInto(ImVec2 origin) {
    auto& ds      = DesignSystem::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    auto& sm      = Shortcuts::ShortcutManager::Instance();
    auto& tm      = Shortcuts::Tools::ToolManager::Instance();

    const float gs   = ds.GetGlobalScale();
    const float kBtn = 26.0f * gs;
    const float kPad = 4.0f  * gs;

    struct T { const char* key; const char* icon; const char* tip;
               const char* sc; const char* act; };
    const T tools[] = {
        { "tool.brush",  "pen",        "Brush",  "tool.brush.activate",  "b" },
        { "tool.eraser", "ink-eraser", "Eraser", "tool.eraser.activate", "e" },
        { "tool.hand",   "draw",       "Hand (pan)", "tool.hand.activate","h" },
    };
    const int n = (int)(sizeof(tools) / sizeof(tools[0]));
    const float w = kBtn + kPad * 2.0f;
    const float h = (float)n * kBtn + (float)(n + 1) * kPad;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + kPad, origin.y + kPad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
        ds.GetColor(DesignSystem::Tok::S_Color_Background_Layer1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, kPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f * gs);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("##FloatTools", ImVec2(w, h), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    {
        DesignSystem::DesignSystem::ZoneStyle zone("viewport/tools",
                                                   "Viewport tools");
        const std::string active = tm.GetActiveTool();
        ImVec4 bg   = ds.GetColor(DesignSystem::Tok::C_IconButton_Background);
        ImVec4 hov  = ds.GetColor(DesignSystem::Tok::C_IconButton_BackgroundHover);
        ImVec4 acc  = ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default);
        ImVec4 tint = ds.GetColor(DesignSystem::Tok::S_Color_Text_Default);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        for (const T& t : tools) {
            bool seld = (active == t.key);
            ImGui::PushID(t.key);
            ImGui::PushStyleColor(ImGuiCol_Button,        seld ? acc : bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, seld ? acc : hov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  acc);
            bool clk = ImGui::Button("##b", ImVec2(kBtn, kBtn));
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(t.tip);
                std::string s = sm.GetShortcutString(t.sc);
                if (!s.empty()) ImGui::TextDisabled("Shortcut: %s", s.c_str());
                ImGui::EndTooltip();
            }
            float isz = kBtn * 0.62f;
            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 ipos = { bmin.x + (kBtn - isz) * 0.5f,
                            bmin.y + (kBtn - isz) * 0.5f };
            auto md = iconMgr.GetDefaultMetadata(t.icon);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            iconMgr.RenderIcon(ImGui::GetWindowDrawList(), t.icon, ipos, isz, md);
            ImGui::PopID();
            if (clk) {
                if      (t.act[0] == 'b') Action_ActivateTool1();
                else if (t.act[0] == 'e') Action_ActivateTool2();
                else                      Action_ActivateHand();
            }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

// New-document popup: a few presets + custom size, then a white artboard
// whose top-left corner is the document origin (0,0). Non-modal (BeginPopup)
// so it does NOT dim the whole screen white; token-styled throughout.
static bool NewDocumentPopup(ImVec2& outSize) {
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

    bool open = ImGui::BeginPopup("New Document",
                                  ImGuiWindowFlags_AlwaysAutoResize);
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
        if (ImGui::Button("Create", ImVec2(120, 0))) {
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

// ── The 2D canvas editor: artboard + unit rulers + camera + navigation ───────
// Camera + document live in `st` (this leaf's EditorState), so every Viewport
// zone has its own independent view.
void Application::RenderViewport(ImVec2 size, EditorState& st) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

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
    ImU32 cCanvas = ImGui::GetColorU32(ImVec4(0.30f, 0.30f, 0.32f, 1.0f));
    ImU32 cTick   = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
    ImU32 cText   = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    ImU32 cGuide  = ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.90f));

    ImVec2 cMin(p0.x + rulerW, p0.y + rulerW);   // canvas top-left
    ImVec2 cMax(p0.x + size.x, p0.y + size.y);
    ImVec2 cSize(cMax.x - cMin.x, cMax.y - cMin.y);
    dl->AddRectFilled(cMin, cMax, cCanvas);

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
    bool rectHover = (m.x >= cMin.x && m.x <= cMax.x &&
                      m.y >= cMin.y && m.y <= cMax.y);
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

    if (camHovered) {
        // Pan: middle-drag, or Hand tool + left-drag.
        if (middleDrag ||
            (handTool && ImGui::IsMouseDragging(ImGuiMouseButton_Left,0.0f))) {
            st.pan.x -= io.MouseDelta.x / st.zoom;
            st.pan.y -= io.MouseDelta.y / st.zoom;
        }
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
                    st.zoom = std::clamp(st.zoom * f, 0.05f, 32.0f);
                    ImVec2 after = S2D(m);
                    st.pan.x += before.x - after.x;
                    st.pan.y += before.y - after.y;
                }
            }
            if (io.MouseWheelH != 0.0f)
                st.pan.x -= io.MouseWheelH * 60.0f / st.zoom;
        }
    }

    // Bounding box of every artboard in the SHARED project (doc-units → px).
    bool   hasArt = !project_.artboards.empty();
    ImVec2 bbMin( 1e9f,  1e9f), bbMax(-1e9f, -1e9f);
    for (const Artboard& ab : project_.artboards) {
        bbMin.x = std::min(bbMin.x, ab.pos.x * u.pxPer);
        bbMin.y = std::min(bbMin.y, ab.pos.y * u.pxPer);
        bbMax.x = std::max(bbMax.x, (ab.pos.x + ab.size.x) * u.pxPer);
        bbMax.y = std::max(bbMax.y, (ab.pos.y + ab.size.y) * u.pxPer);
    }

    // C6: recenter requests, consumed by THIS leaf only.
    if (st.reqFitDoc && hasArt) {
        float bw = std::max(1.0f, bbMax.x - bbMin.x);
        float bh = std::max(1.0f, bbMax.y - bbMin.y);
        float zx = cSize.x / bw, zy = cSize.y / bh;
        st.zoom = std::clamp(std::min(zx, zy) * 0.9f, 0.05f, 32.0f);
        st.pan  = ImVec2(
            (bbMin.x + bw * 0.5f) - cSize.x * 0.5f / st.zoom,
            (bbMin.y + bh * 0.5f) - cSize.y * 0.5f / st.zoom);
    }
    if (st.reqResetOrigin) {
        st.zoom = 1.0f;
        st.pan  = ImVec2(-40.0f, -40.0f);
    }
    st.reqFitDoc = st.reqResetOrigin = false;

    // ── The project's artboards (shared across every Viewport zone) ─────
    if (hasArt) {
        for (const Artboard& ab : project_.artboards) {
            ImVec2 a = D2S(ImVec2(ab.pos.x * u.pxPer, ab.pos.y * u.pxPer));
            ImVec2 b = D2S(ImVec2((ab.pos.x + ab.size.x) * u.pxPer,
                                  (ab.pos.y + ab.size.y) * u.pxPer));
            ImVec4 sh(0, 0, 0, 0.35f);
            dl->AddRectFilled(ImVec2(a.x + 6, a.y + 6),
                              ImVec2(b.x + 6, b.y + 6),
                              ImGui::GetColorU32(sh));
            dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
            dl->AddRect(a, b, IM_COL32(0, 0, 0, 90));
            if (!ab.name.empty()) {
                ImVec2 ts = ImGui::CalcTextSize(ab.name.c_str());
                dl->AddText(ImVec2(a.x, a.y - ts.y - 2.0f), cText,
                            ab.name.c_str());
            }
        }
    } else {
        const char* hint = "Empty project — add a page with + or Ctrl+Shift+N";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cMin.x + (cSize.x - ts.x) * 0.5f,
                           cMin.y + (cSize.y - ts.y) * 0.5f),
                    cText, hint);
    }

    // ── Rulers ──────────────────────────────────────────────────────────
    dl->AddRectFilled(ImVec2(p0.x, p0.y),
                      ImVec2(p0.x + size.x, p0.y + rulerW), cBg);   // top
    dl->AddRectFilled(ImVec2(p0.x, p0.y),
                      ImVec2(p0.x + rulerW, p0.y + size.y), cBg);   // left

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
                              (int)std::lround(dx / u.pxPer));
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
                              (int)std::lround(dy / u.pxPer));
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Unit: %s (click to cycle)", u.name);
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

    // Floating tool palette pinned to the left, inside the canvas.
    RenderToolbarInto(ImVec2(cMin.x, cMin.y));

    // New-artboard popup (opened from the top-bar + button or the shortcut).
    // The page is added to the SHARED project, so it appears in every
    // Viewport zone and in the Outliner — not just this leaf.
    if (st.openNewDoc) { ImGui::OpenPopup("New Document");
                         st.openNewDoc = false; }
    ImVec2 newSz;
    if (NewDocumentPopup(newSz)) {
        // Lay pages side by side: place the new one to the right of the
        // existing bounding box (in doc-units), 40 px gutter.
        float x0 = 0.0f;
        if (!project_.artboards.empty()) {
            float maxR = -1e9f;
            for (const Artboard& ab : project_.artboards)
                maxR = std::max(maxR, ab.pos.x + ab.size.x);
            x0 = maxR + 40.0f;
        }
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Page %d",
                      (int)project_.artboards.size() + 1);
        project_.AddArtboard(nm, ImVec2(x0, 0.0f), newSz);
        st.reqFitDoc = true;   // frame the updated project in THIS view
    }

    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(size);
}

// ── Outliner: shared project → artboards (→ objects later) ────────────────────

} // namespace App

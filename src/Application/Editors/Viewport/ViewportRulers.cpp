#include "Application.h"

#include <UI/Units.h>
#include <UI/Widgets/PopupMenu.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport rulers + the per-viewport display unit (the legacy rulers, rebuilt
//  on the Ink canvas). Rulers are CHROME: translucent bands over the top + left
//  edges of the canvas (ImGui draw list — the canvas stays 100 % Vulkan). Ticks
//  adapt to the zoom with a "nice" 1/2/5·10ⁿ step and are labelled in the
//  viewport's display unit; blue guides track the cursor; the bottom-left corner
//  square is a button that cycles the viewport unit. The bands are excluded from
//  canvas hit-testing (st.overlayRects), so a click on a ruler never draws.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

ImU32 Col(DS::DesignSystem& ds, Tok t, float a = 1.0f) {
    try { ImVec4 c = ds.GetColor(t); c.w *= a; return ImGui::ColorConvertFloat4ToU32(c); }
    catch (...) { return IM_COL32(200, 200, 200, (int)(a * 255)); }
}

// Round a raw step UP to the nearest 1·10ⁿ / 2·10ⁿ / 5·10ⁿ.
double NiceStep(double raw) {
    if (raw <= 0.0) return 1.0;
    const double e = std::floor(std::log10(raw));
    const double p = std::pow(10.0, e);
    const double f = raw / p;                 // 1 .. 10
    const double nice = f <= 1.0 ? 1.0 : f <= 2.0 ? 2.0 : f <= 5.0 ? 5.0 : 10.0;
    return nice * p;
}

} // namespace

UI::Units::UnitSystem Application::ViewportUnitSystem(const EditorState& st) const {
    if (st.docUnit <= 0) return project_.docUnitSystem;     // 0 = follow document
    const int i = std::clamp(st.docUnit - 1, 0,
                             UI::Units::kUnitSystemCount - 1);
    return (UI::Units::UnitSystem)i;
}

float Application::RulerWidth() const {
    auto& ds = DS::DesignSystem::Instance();
    return std::max(14.0f, ds.GetFloat(Tok::S_Size_ControlHeight) * ds.GetGlobalScale());
}

ImVec4 Application::RulerInsets(const EditorState& st) const {
    const float w = RulerWidth();
    return ImVec4(st.rulerLeft ? w : 0.0f, st.rulerTop ? w : 0.0f,
                  st.rulerRight ? w : 0.0f, st.rulerBottom ? w : 0.0f);
}

void Application::DrawRulers(EditorState& st, ImVec2 cMin, ImVec2 size) {
    namespace un = UI::Units;
    if (!st.rulerTop && !st.rulerLeft && !st.rulerRight && !st.rulerBottom) return;
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float rw = RulerWidth();
    if (size.x < rw * 3.0f || size.y < rw * 3.0f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const ImVec2 cMax(cMin.x + size.x, cMin.y + size.y);
    const ImU32 bg    = Col(ds, Tok::S_Color_Background_Layer1, 0.88f);
    const ImU32 tick  = Col(ds, Tok::S_Color_Text_Subtle, 0.9f);
    const ImU32 text  = Col(ds, Tok::S_Color_Text_Default, 1.0f);
    const ImU32 guide = Col(ds, Tok::S_Color_Accent_Default, 0.9f);

    // The viewport display unit: base px per one display unit, and its name.
    const un::UnitSystem vsys = ViewportUnitSystem(st);
    const un::LengthUnit ru   = un::Resolve(vsys, un::LengthScale::Normal);
    const double pxPerUnit    = un::PxPer(ru);       // base px per display unit
    const char*  uname        = un::Name(ru);

    // The canvas area INSIDE the enabled rulers (ticks/labels/guides stay here).
    const float inL = st.rulerLeft   ? rw : 0.0f, inT = st.rulerTop    ? rw : 0.0f;
    const float inR = st.rulerRight  ? rw : 0.0f, inB = st.rulerBottom ? rw : 0.0f;
    const float innerL = cMin.x + inL, innerT = cMin.y + inT;
    const float innerR = cMax.x - inR, innerB = cMax.y - inB;

    // Bands (each enabled ruler; they overlap at the corners, same colour).
    if (st.rulerTop)    dl->AddRectFilled(cMin, ImVec2(cMax.x, cMin.y + rw), bg);
    if (st.rulerBottom) dl->AddRectFilled(ImVec2(cMin.x, cMax.y - rw), cMax, bg);
    if (st.rulerLeft)   dl->AddRectFilled(cMin, ImVec2(cMin.x + rw, cMax.y), bg);
    if (st.rulerRight)  dl->AddRectFilled(ImVec2(cMax.x - rw, cMin.y), cMax, bg);

    // doc (base px) ↔ screen: sx = cMin.x + (docx - pan) * zoom.
    const double zoom = std::max(1e-9, st.zoom);
    auto docToSx = [&](double dx) { return (float)(cMin.x + (dx - st.panX) * zoom); };
    auto docToSy = [&](double dy) { return (float)(cMin.y + (dy - st.panY) * zoom); };
    auto sxToDoc = [&](double sx) { return (sx - cMin.x) / zoom + st.panX; };
    auto syToDoc = [&](double sy) { return (sy - cMin.y) / zoom + st.panY; };

    // Nice step chosen in DISPLAY units so labels are round (…, 0, 10, 20 mm).
    const double pxPerDisp = zoom * pxPerUnit;       // screen px per display unit
    const double majorDisp = NiceStep(48.0 * gs / std::max(1e-9, pxPerDisp));
    const double majorDoc  = majorDisp * pxPerUnit;  // base px between majors
    const float  majorPx   = (float)(majorDoc * zoom);
    const int    minors    = 5;

    const float rulerFs = ImGui::GetFontSize() * 0.75f;
    ImFont* font = ImGui::GetFont();
    const float majTick = rw * 0.5f, minTick = rw * 0.28f;

    // A HORIZONTAL ruler along `edgeY` (its inner, canvas-facing edge); `td` = the
    // tick direction into the band (−1 top, +1 bottom). Ticks + centred labels
    // for every X-major inside the canvas strip.
    auto hRuler = [&](float edgeY, int td) {
        if (majorPx <= 4.0f) return;
        const float labY = edgeY + td * (majTick + rw) * 0.5f;
        const double d0 = std::floor(sxToDoc(innerL) / majorDoc) * majorDoc;
        for (double dx = d0; ; dx += majorDoc) {
            const float sx = docToSx(dx);
            if (sx > innerR) break;
            if (sx >= innerL) {
                dl->AddLine(ImVec2(sx, edgeY), ImVec2(sx, edgeY + td * majTick), tick);
                char buf[24];
                std::snprintf(buf, sizeof buf, "%g", dx / pxPerUnit);
                const ImVec2 ts = font->CalcTextSizeA(rulerFs, FLT_MAX, 0.0f, buf);
                dl->AddText(font, rulerFs,
                            ImVec2(sx - ts.x * 0.5f, labY - ts.y * 0.5f), text, buf);
            }
            for (int k = 1; k < minors; ++k) {
                const float mx = docToSx(dx + k * majorDoc / minors);
                if (mx >= innerL && mx <= innerR)
                    dl->AddLine(ImVec2(mx, edgeY), ImVec2(mx, edgeY + td * minTick), tick);
            }
        }
    };
    // A VERTICAL ruler along `edgeX`; `td` = −1 left / +1 right. Labels rotated.
    auto vRuler = [&](float edgeX, int td) {
        if (majorPx <= 4.0f) return;
        const float labX = edgeX + td * (majTick + rw) * 0.5f;
        const double d0 = std::floor(syToDoc(innerT) / majorDoc) * majorDoc;
        for (double dy = d0; ; dy += majorDoc) {
            const float sy = docToSy(dy);
            if (sy > innerB) break;
            if (sy >= innerT) {
                dl->AddLine(ImVec2(edgeX, sy), ImVec2(edgeX + td * majTick, sy), tick);
                char buf[24];
                std::snprintf(buf, sizeof buf, "%g", dy / pxPerUnit);
                const ImVec2 ts = font->CalcTextSizeA(rulerFs, FLT_MAX, 0.0f, buf);
                const int v0 = dl->VtxBuffer.Size;
                dl->AddText(font, rulerFs,
                            ImVec2(labX - ts.x * 0.5f, sy - ts.y * 0.5f), text, buf);
                const int v1 = dl->VtxBuffer.Size;   // rotate −90° about (labX,sy)
                for (int vi = v0; vi < v1; ++vi) {
                    ImDrawVert& v = dl->VtxBuffer[vi];
                    const float ddx = v.pos.x - labX, ddy = v.pos.y - sy;
                    v.pos.x = labX + ddy; v.pos.y = sy - ddx;
                }
            }
            for (int k = 1; k < minors; ++k) {
                const float my = docToSy(dy + k * majorDoc / minors);
                if (my >= innerT && my <= innerB)
                    dl->AddLine(ImVec2(edgeX, my), ImVec2(edgeX + td * minTick, my), tick);
            }
        }
    };

    if (st.rulerTop)    hRuler(cMin.y + rw, -1);
    if (st.rulerBottom) hRuler(cMax.y - rw, +1);
    if (st.rulerLeft)   vRuler(cMin.x + rw, -1);
    if (st.rulerRight)  vRuler(cMax.x - rw, +1);

    // ── Cursor guides (only while genuinely over the canvas) ─────────────────
    const ImVec2 mp = io.MousePos;
    const bool inCanvas = mp.x >= innerL && mp.x <= innerR &&
                          mp.y >= innerT && mp.y <= innerB;
    if (inCanvas) {
        if (st.rulerTop)    dl->AddLine(ImVec2(mp.x, cMin.y), ImVec2(mp.x, cMin.y + rw), guide, 1.5f);
        if (st.rulerBottom) dl->AddLine(ImVec2(mp.x, cMax.y - rw), ImVec2(mp.x, cMax.y), guide, 1.5f);
        if (st.rulerLeft)   dl->AddLine(ImVec2(cMin.x, mp.y), ImVec2(cMin.x + rw, mp.y), guide, 1.5f);
        if (st.rulerRight)  dl->AddLine(ImVec2(cMax.x - rw, mp.y), ImVec2(cMax.x, mp.y), guide, 1.5f);
    }

    // ── Corner unit button ────────────────────────────────────────────────────
    // Only where a horizontal ruler MEETS a vertical one. Priority: top-left,
    // top-right, bottom-left, bottom-right. None → no button (the Viewport
    // Overlay dropdown carries the same unit control as a fallback).
    ImVec2 corner;
    bool haveCorner = true;
    if      (st.rulerTop    && st.rulerLeft)  corner = cMin;
    else if (st.rulerTop    && st.rulerRight) corner = ImVec2(cMax.x - rw, cMin.y);
    else if (st.rulerBottom && st.rulerLeft)  corner = ImVec2(cMin.x, cMax.y - rw);
    else if (st.rulerBottom && st.rulerRight) corner = ImVec2(cMax.x - rw, cMax.y - rw);
    else haveCorner = false;
    if (haveCorner) {
        dl->AddRectFilled(corner, ImVec2(corner.x + rw, corner.y + rw), bg);
        const ImVec2 ts = ImGui::CalcTextSize(uname);
        dl->AddText(ImVec2(corner.x + (rw - ts.x) * 0.5f,
                           corner.y + (rw - ts.y) * 0.5f), text, uname);
        if (st.docUnit <= 0)   // a small tick marks "follows document"
            dl->AddRectFilled(ImVec2(corner.x + 2.0f, corner.y + 2.0f),
                              ImVec2(corner.x + 5.0f, corner.y + 5.0f), guide);
        ImGui::SetCursorScreenPos(corner);
        if (ImGui::InvisibleButton("##unitsq", ImVec2(rw, rw)))
            st.docUnit = (st.docUnit + 1) % (UI::Units::kUnitSystemCount + 1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            char tip[96];
            std::snprintf(tip, sizeof tip,
                          st.docUnit <= 0
                              ? "Viewport unit: %s (follows the document) — "
                                "click to override"
                              : "Viewport unit: %s — click to cycle",
                          un::SystemName(vsys));
            UI::DrawTooltip(tip, io.MousePos);
        }
    }

    // Exclude the enabled bands from canvas hit-testing (input reads overlayRects).
    if (st.rulerTop)    st.overlayRects.push_back(ImVec4(cMin.x, cMin.y, cMax.x, cMin.y + rw));
    if (st.rulerBottom) st.overlayRects.push_back(ImVec4(cMin.x, cMax.y - rw, cMax.x, cMax.y));
    if (st.rulerLeft)   st.overlayRects.push_back(ImVec4(cMin.x, cMin.y, cMin.x + rw, cMax.y));
    if (st.rulerRight)  st.overlayRects.push_back(ImVec4(cMax.x - rw, cMin.y, cMax.x, cMax.y));
}

} // namespace App

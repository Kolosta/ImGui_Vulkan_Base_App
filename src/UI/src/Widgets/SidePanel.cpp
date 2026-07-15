#include <UI/Widgets/SidePanel.h>
#include <UI/Widgets/ScrollArea.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
ImU32  ColA(Tok t, float a) { ImVec4 c = Col(t); return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a)); }

// Draw `text` rotated 90° CW — reads top→bottom, letter bases on the LEFT.
// Emits each glyph's atlas quad directly into the rotated frame (crisp, no
// re-rasterisation). The run descends along x = cx, centred vertically on cy.
// Returns the total advance length in px (= the height the text occupies).
//
// Rotation 90° CW: upright offset (gx, gy) → screen delta (−gy, +gx).
//   screen = (cx − gy,  penY + gx)
//
// In ImGui the pen is the TOP-LEFT of the text line, so Y0 ≈ 0 (top of glyph
// bitmap) and Y1 ≈ fontSize (bottom). After CW rotation, −gy maps the glyph
// ENTIRELY to the LEFT of cx: top edge at cx−Y0 ≈ cx, bottom at cx−Y1 ≈ cx−fontSize.
// To horizontally centre the glyph body on the bar column, pass:
//   cx = barColumnCentreX + fontSize * 0.5f
// (derived from: centre of glyph = cx − (Y0+Y1)/2 ≈ cx − fontSize/2 must equal
// barColumnCentreX, so cx = barColumnCentreX + fontSize/2).
float AddTextVertical(ImDrawList* dl, ImFont* font, float fontSize,
                      float cx, float cy, ImU32 col, const char* text) {
    ImFontBaked* baked = font->GetFontBaked(fontSize);
    if (!baked) return 0.0f;
    const float scale = fontSize / baked->Size;

    float total = 0.0f;
    for (const char* p = text; *p; ++p)
        if (const ImFontGlyph* g = baked->FindGlyph((ImWchar)(unsigned char)*p))
            total += g->AdvanceX * scale;
    if ((col >> 24) == 0) return total;   // measure-only, no draw

    // Pen descends: start at top of run, advance downward.
    float penY = cy - total * 0.5f;
    dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
    for (const char* p = text; *p; ++p) {
        const ImFontGlyph* g = baked->FindGlyph((ImWchar)(unsigned char)*p);
        if (!g) continue;
        const float adv = g->AdvanceX * scale;
        if (g->Visible) {
            const float gx0 = g->X0 * scale, gy0 = g->Y0 * scale;
            const float gx1 = g->X1 * scale, gy1 = g->Y1 * scale;
            // CW rotation keeps the same corner→UV pairing (no flip):
            //   Upright TL (X0,Y0) → (cx−gy0, penY+gx0)  UV(U0,V0)
            //   Upright TR (X1,Y0) → (cx−gy0, penY+gx1)  UV(U1,V0)
            //   Upright BR (X1,Y1) → (cx−gy1, penY+gx1)  UV(U1,V1)
            //   Upright BL (X0,Y1) → (cx−gy1, penY+gx0)  UV(U0,V1)
            dl->PrimReserve(6, 4);
            dl->PrimQuadUV(
                ImVec2(cx - gy0, penY + gx0), ImVec2(cx - gy0, penY + gx1),
                ImVec2(cx - gy1, penY + gx1), ImVec2(cx - gy1, penY + gx0),
                ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0),
                ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1), col);
        }
        penY += adv;
    }
    dl->PopTexture();
    return total;
}
} // namespace

void EditorSidePanel(const char* id, ImVec2 cMin, ImVec2 cMax,
                     SidePanelState& st, const std::vector<SidePanelTab>& tabs) {
    auto& ds = DS::DesignSystem::Instance();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gs = ds.GetGlobalScale();

    const int nTabs = (int)tabs.size();
    if (nTabs == 0) { st.stage = 0; return; }
    st.tab = std::clamp(st.tab, 0, nTabs - 1);

    const float uiU     = Flt(Tok::S_Size_ControlHeight) * gs;
    const float zoneRnd = Flt(Tok::C_Window_CornerRadius) * gs;
    const float barW    = std::max(22.0f * gs, uiU);
    const float minPanel = barW + 150.0f * gs;
    const float maxPanel = (cMax.x - cMin.x) * 0.7f;
    const float fillet  = std::min(zoneRnd, 8.0f * gs);
    const float A = 0.86f;
    // The uniform inset (token-driven) of the open panel from the canvas edge
    // (top/bottom) AND of its content from the panel edges (all four sides). The
    // tab BAR strip itself spans full height.
    const float margin = Flt(Tok::C_SidePanel_Margin) * gs;

    // ── Colours ──────────────────────────────────────────────────────────────
    // The content panel uses the DARKEST recessed surface (Layer1) at the panel
    // alpha, so it reads clearly darker than the canvas behind it; the tab
    // silhouettes share it so the selected tab merges seamlessly into the panel.
    ImU32 panelBg = ColA(Tok::S_Color_Background_Layer1, A);
    ImU32 barBg   = ColA(Tok::C_ZoneTab_BarBackground,   A);
    ImU32 txt     = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Default));
    ImU32 txtSub  = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Subtle));

    st.width = std::clamp(st.width, minPanel, std::max(minPanel, maxPanel));

    ImGui::PushID(id);

    // ── Drag handle / edge grip ───────────────────────────────────────────────
    ImGuiStorage* store  = ImGui::GetStateStorage();
    const ImGuiID dragKey = ImGui::GetID("##sp_drag");
    bool dragging = store->GetBool(dragKey, false);

    float width = (st.stage == 0) ? 0.0f : (st.stage == 1) ? barW : st.width;

    if (st.stage == 0) {
        // A COMPACT handle: just big enough to hold a LARGE chevron, with a thin
        // padding around it (no oversized grip). Sized off the chevron so the
        // handle shrinks/grows with it.
        const float cz  = Flt(Tok::C_Dropdown_ChevronSize) * gs * 1.6f;  // big icon
        const float padX = 3.0f * gs, padY = 4.0f * gs;
        const float hw  = cz + padX * 2.0f;
        const float hh  = cz + padY * 2.0f;
        ImVec2 hmn(cMax.x - hw, cMin.y + ImGui::GetTextLineHeightWithSpacing() * 0.6f);
        ImVec2 hmx(cMax.x, hmn.y + hh);
        dl->AddRectFilled(hmn, hmx, ColA(Tok::C_Editor_TopBarBackground, 0.45f),
                          4.0f * gs, ImDrawFlags_RoundCornersLeft);
        auto& iconMgr = VectorGraphics::IconManager::Instance();
        auto md = iconMgr.GetDefaultMetadata("chevron-left");
        if (!md.colorZones.empty()) md.colorZones[0].customColor = Col(Tok::S_Color_Text_Subtle);
        iconMgr.RenderIcon(dl, "chevron-left",
                            ImVec2(hmn.x + (hw - cz) * 0.5f,
                                  (hmn.y + hmx.y) * 0.5f - cz * 0.5f), cz, md);
        ImGui::SetCursorScreenPos(hmn);
        ImGui::InvisibleButton("##spHandle", ImVec2(hw, hh));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) { dragging = true; store->SetBool(dragKey, true); }
        if (ImGui::IsItemDeactivated() &&
            !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
            st.stage = 2; dragging = false; store->SetBool(dragKey, false);
        }
    } else {
        float edge = cMax.x - width;
        ImGui::SetCursorScreenPos(ImVec2(edge - 4.0f * gs, cMin.y));
        ImGui::InvisibleButton("##spEdge", ImVec2(8.0f * gs, cMax.y - cMin.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) { dragging = true; store->SetBool(dragKey, true); }
    }
    if (dragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragging = false; store->SetBool(dragKey, false);
        } else {
            float w = std::clamp(cMax.x - io.MousePos.x, 0.0f, maxPanel);
            if      (w < barW * 0.5f)   st.stage = 0;
            else if (w < minPanel)       st.stage = 1;
            else { st.stage = 2; st.width = std::max(minPanel, w); }
        }
    }
    if (st.stage == 0) { ImGui::PopID(); return; }

    width = (st.stage == 1) ? barW : st.width;
    // Pixel-snap the right edge and the bar's left edge. cMax.x (the editor's
    // right border) is not always integral, and it differs per editor (the
    // Viewport insets by the ruler width, the Timeline does not) — without
    // snapping, the bar's left edge lands on a fractional pixel and looks shifted
    // by 1px (Viewport) / 2px (Timeline). Round both edges to whole pixels so the
    // bar column is crisp and identically placed in every editor.
    const float rightX = std::round(cMax.x);
    const float barWpx = std::round(barW);
    const float panelLeft = std::round(cMax.x - width);
    ImVec2 pmin(panelLeft, cMin.y), pmax(rightX, cMax.y);

    ImVec2 barMin(rightX - barWpx, pmin.y), barMax(rightX, pmax.y);
    ImVec2 conMin(pmin.x, pmin.y),          conMax(barMin.x, pmax.y);

    const float r  = fillet;
    const float PI = 3.14159265358979f;

    // ── Tab layout + hit-testing ─────────────────────────────────────────────
    const ImVec2 tabPad = ds.GetVec2(Tok::C_ZoneTab_Padding);
    const float  tabPadX = tabPad.x * gs;
    const float  tabPadY = tabPad.y * gs;
    const float  tabGap  = Flt(Tok::C_ZoneTab_Gap) * gs;
    ImFont* font    = ImGui::GetFont();
    const float  fontSz  = ImGui::GetFontSize();

    const float barCentreX  = barMin.x + barW * 0.5f;
    const float textCX      = barCentreX + fontSz * 0.5f;

    struct TabBox { float top, bot; bool sel, hov; };
    std::vector<TabBox> boxes((size_t)nTabs);
    {
        float y = barMin.y + margin + tabPadY;   // inset the tabs from the top
        for (int i = 0; i < nTabs; ++i) {
            float textLen = AddTextVertical(dl, font, fontSz, 0.f, 0.f, 0,
                                            tabs[(size_t)i].name.c_str());
            float tabH = tabPadX * 2.0f + textLen;
            boxes[(size_t)i] = { y, y + tabH, false, false };
            y += tabH + tabGap;
        }
    }

    for (int i = 0; i < nTabs; ++i) {
        TabBox& b = boxes[(size_t)i];
        ImGui::SetCursorScreenPos(ImVec2(barMin.x, b.top));
        ImGui::InvisibleButton(tabs[(size_t)i].name.c_str(),
                               ImVec2(barW, b.bot - b.top));
        b.hov = ImGui::IsItemHovered();
        b.sel = (i == st.tab) && (st.stage == 2);
        if (ImGui::IsItemClicked()) {
            if (st.stage == 2 && st.tab == i) st.stage = 1;
            else { st.tab = i; st.stage = 2; }
        }
    }

    // ── Drawing ──────────────────────────────────────────────────────────────
    // Compositing (clean with alpha, no seam): the content panel and the bar are
    // each painted ONCE over the canvas. Under a selected tab the bar background
    // is NOT painted (it is cut out at the four corners), so the tab silhouette —
    // same panelBg, same alpha — composites over the bare canvas exactly like the
    // content panel and merges into it with no darker seam. This mirrors the zone
    // tab bar, whose concave fillets simply expose the editor colour beneath.
    //
    // Tab corners face the panel: the RIGHT side (the editor's outer border) gets
    // CONVEX rounded corners; only the SELECTED tab also gets CONCAVE fillets on
    // its LEFT side that flare into the content panel. A hovered (non-selected)
    // tab has the convex right corners but NO concave fillets — exactly like an
    // inactive zone tab. `er` (= zoneRnd) is the radius the editor frame itself
    // uses, so the bar's outer corners follow the SAME rounding as the editor.
    const float er = std::min(zoneRnd, barWpx * 0.5f);   // right (outer) corner radius
    const float cr = r;                                  // left concave fillet radius

    // The open content panel is inset by `margin` top AND bottom, and its height
    // AUTO-FITS the content: use last frame's measured content height (stable,
    // the content doesn't jump) clamped to the available band; a scrollbar
    // appears when it overflows. The tab BAR (barMin..barMax) stays full-height.
    const ImGuiID contentHKey = ImGui::GetID("##sp_contentH");
    // The content is inset by `margin` on ALL FOUR sides (uniform). It is applied
    // by the child's POSITION (top + left) and SIZE (bottom), not by ImGui window
    // padding — a borderless child ignores WindowPadding unless it is forced, so
    // positioning is the reliable way to get a consistent frame on every tab.
    const float innerPad = margin;
    const float availTop = cMin.y + margin;
    const float availBot = cMax.y - margin;
    const float availH   = std::max(uiU, availBot - availTop);
    float measuredH = store->GetFloat(contentHKey, 0.0f);
    // First frame (nothing measured yet) → take the full band, then shrink.
    float panelH = (measuredH <= 0.5f)
        ? availH
        : std::min(availH, measuredH + innerPad * 2.0f);
    conMin.y = availTop;
    conMax.y = availTop + panelH;

    if (st.stage == 2)
        dl->AddRectFilled(conMin, conMax, panelBg, zoneRnd,
                          ImDrawFlags_RoundCornersLeft);

    // Clip everything that follows. Selected-tab concave fillets reach LEFT into
    // the content panel (where they merge with it), so the clip starts at the
    // content's left edge when the panel is open; otherwise at the bar's left.
    const float clipLeft = (st.stage == 2) ? conMin.x : barMin.x;
    dl->PushClipRect(ImVec2(clipLeft, cMin.y), ImVec2(rightX, cMax.y), true);

    // Step 1 — bar background, ONE flat rectangle over the whole bar column. We do
    // NOT cut holes for the tabs: the tab silhouettes are simply painted on top in
    // panelBg (exactly like the zone tab bar, where the active tab body is drawn
    // over the already-painted bar). This keeps the rounded/concave tab edges
    // perfectly clean — there is no separate "complement" geometry that could
    // leave triangular gaps or expose the wrong colour behind the fillets.
    dl->AddRectFilled(barMin, barMax, barBg);

    // Step 2 — tab silhouettes (panelBg), drawn ON TOP of the bar. The SELECTED
    // tab is the proven shape: concave fillets on the LEFT that flare into the
    // content panel + convex corners on the RIGHT, traced as ONE closed outline
    // (no separate fill pieces → no diagonal seam). A HOVERED (non-selected) tab
    // keeps the convex right corners but has SQUARE left corners — no concave
    // fillets — exactly like an inactive zone tab.
    //
    // Outline order (single continuous loop, no stray closing edge):
    //   left side ▸ top-left ▸ top edge ▸ top-right convex ▸ right edge ▸
    //   bottom-right convex ▸ bottom edge ▸ bottom-left ▸ back up the left side.
    auto drawTabShape = [&](float top, float bot, ImU32 col, bool selected) {
        const float left  = barMin.x;
        const float right = barMax.x;
        if (er <= 0.5f) { dl->AddRectFilled(ImVec2(left, top), ImVec2(right, bot), col); return; }

        const bool concave = selected && cr > 0.5f;
        dl->PathClear();
        // 1. Top-left. Concave fillet centred ABOVE the corner at (left+cr, top−cr)
        //    so it bows OUTWARD (left) into the content panel; runs from the left
        //    edge (left, top−cr) round to the top edge (left+cr, top). This is the
        //    user-validated convention.
        if (concave) dl->PathArcTo(ImVec2(left + cr, top - cr), cr, PI, PI * 0.5f, 12);
        else         dl->PathLineTo(ImVec2(left, top));        // square top-left
        // 2. Top-right convex corner (top edge → right edge).
        dl->PathArcTo(ImVec2(right - er, top + er), er, -PI * 0.5f, 0.0f, 12);
        // 3. Bottom-right convex corner (right edge → bottom edge).
        dl->PathArcTo(ImVec2(right - er, bot - er), er, 0.0f, PI * 0.5f, 12);
        // 4. Bottom-left. Concave fillet centred BELOW the corner at (left+cr,bot+cr),
        //    from the bottom edge (left+cr, bot) round to the left edge (left, bot+cr).
        if (concave) dl->PathArcTo(ImVec2(left + cr, bot + cr), cr, -PI * 0.5f, -PI, 12);
        else         dl->PathLineTo(ImVec2(left, bot));        // square bottom-left
        // The closing edge (back up the left side) is implicit. Concave variant is
        // non-convex → concave-aware fill; otherwise the fast convex fill.
        if (concave) dl->PathFillConcave(col);
        else         dl->PathFillConvex(col);
    };

    for (int i = 0; i < nTabs; ++i) {           // selected first (lowest)
        const TabBox& b = boxes[(size_t)i];
        if (b.sel) drawTabShape(b.top, b.bot, panelBg, true);
    }
    for (int i = 0; i < nTabs; ++i) {           // hovered (non-selected) above
        const TabBox& b = boxes[(size_t)i];
        if (!b.sel && b.hov) drawTabShape(b.top, b.bot, panelBg, false);
    }

    // Step 3 — vertical labels (selected = full text colour, others = subtle).
    for (int i = 0; i < nTabs; ++i) {
        const TabBox& b = boxes[(size_t)i];
        const float cy = (b.top + b.bot) * 0.5f;
        AddTextVertical(dl, font, fontSz, textCX, cy,
                        b.sel ? txt : txtSub,
                        tabs[(size_t)i].name.c_str());
    }

    dl->PopClipRect();

    // ── Active tab content — a SCROLLABLE child (the app's overlay scrollbar,
    //    which floats in the right gutter and reserves no width) so a long body
    //    scrolls inside the auto-fitted, margin-inset panel. Left AND right
    //    horizontal margins inset the content from the panel edges. ────────────
    if (st.stage == 2) {
        // Uniform `margin` inset on all sides: LEFT & TOP via the child position,
        // BOTTOM via the reduced height. The RIGHT inset is the overlay
        // scrollbar's own gutter (BeginScroll reserves it inside the child and
        // floats the grab there — no reserved width), so the scrollbar sits in
        // the right margin. WindowPadding is zeroed (positioning owns the inset).
        const ImVec2 childPos(conMin.x + margin, conMin.y + margin);
        const ImVec2 childSz(std::max(1.0f, (conMax.x - conMin.x) - margin),
                             std::max(1.0f, (conMax.y - conMin.y) - margin * 2.0f));
        ImGui::SetCursorScreenPos(childPos);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        UI::BeginScroll("##spContent", childSz, ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoBackground);
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        const float  y0 = ImGui::GetCursorPosY();   // LOCAL — scroll-independent
        if (tabs[(size_t)st.tab].draw)
            tabs[(size_t)st.tab].draw(c0, ImVec2(childPos.x + childSz.x, conMax.y));
        // Measure the content's natural height (local cursor) for next frame's
        // auto-fit — independent of the current scroll offset.
        const float used = ImGui::GetCursorPosY() - y0;
        UI::EndScroll();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        store->SetFloat(contentHKey, std::max(0.0f, used));
    }

    ImGui::PopID();
}

float SidePanelOccupiedWidth(const SidePanelState& st, ImVec2 cMin, ImVec2 cMax) {
    if (st.stage == 0) return 0.0f;
    auto& ds = DS::DesignSystem::Instance();
    const float gs   = ds.GetGlobalScale();
    const float uiU  = Flt(Tok::S_Size_ControlHeight) * gs;
    const float barW = std::max(22.0f * gs, uiU);             // same as EditorSidePanel
    if (st.stage == 1) return barW;
    const float minPanel = barW + 150.0f * gs;
    const float maxPanel = (cMax.x - cMin.x) * 0.7f;
    return std::clamp(st.width, minPanel, std::max(minPanel, maxPanel));
}

} // namespace UI
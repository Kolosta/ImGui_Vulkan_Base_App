#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip (shared, topmost, styled)
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>

namespace UI {

namespace {

namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
ImVec2 V2(Tok t)  { return DS::DesignSystem::Instance().GetVec2(t); }

// Blit an icon tinted to `tint` onto `dl` at `pos` with the given pixel size.
void DrawIcon(ImDrawList* dl, const char* icon, ImVec2 pos, float size,
              const ImVec4& tint) {
    if (!icon || !*icon) return;
    auto& im = VectorGraphics::IconManager::Instance();
    auto md = im.GetDefaultMetadata(icon);
    if (md.colorZones.empty()) return;
    for (auto& z : md.colorZones) z.customColor = tint;
    im.RenderIcon(dl, icon, pos, size, md);
}

} // namespace

DropdownResult Dropdown(const DropdownConfig& cfg) {
    DS::DesignSystem::ComponentScope _cs("Dropdown");
    auto& ds   = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    DropdownResult result;

    const float controlH = Flt(Tok::C_Dropdown_Height)     * gs;
    ImVec2      pad       = V2(Tok::C_Dropdown_Padding);  pad.x *= gs; pad.y *= gs;
    const float chevSz    = Flt(Tok::C_Dropdown_ChevronSize) * gs;
    const float iconSz    = Flt(Tok::C_Dropdown_IconSize)    * gs;
    const float radius    = Flt(Tok::C_Dropdown_CornerRadius) * gs;

    // Honour the global border toggle (S_Border_Enabled): 0 width = no border.
    const bool  bordersOn = DS::DesignSystem::Instance().BordersEnabled();
    const float borderW   = bordersOn ? Flt(Tok::C_Dropdown_BorderWidth) * gs : 0.0f;

    const bool  minimal = cfg.style == DropdownStyle::Minimal;
    const bool  showChevron = !minimal;

    const ImVec4 dTextV = Col(Tok::C_Dropdown_Text);
    const ImVec4 dIconV = Col(Tok::C_Dropdown_Icon);
    // Minimal: transparent at rest, no border, no chevron; only a subtle hover
    // fill (a hair lighter than the host bar) and the same fill while open.
    const ImVec4 transparent(0, 0, 0, 0);
    const ImVec4 dBgV   = minimal ? transparent : Col(Tok::C_Dropdown_Background);
    const ImVec4 dHovV  = minimal ? Col(Tok::C_Dropdown_BackgroundHoverMinimal)
                                  : Col(Tok::C_Dropdown_BackgroundHover);
    const ImVec4 dDownV = minimal ? Col(Tok::C_Dropdown_BackgroundHoverMinimal)
                                  : Col(Tok::C_Dropdown_BackgroundDown);
    const ImVec4 dBordV     = minimal ? transparent : Col(Tok::C_Dropdown_Border);
    const ImVec4 dBordHovV  = minimal ? transparent : Col(Tok::C_Dropdown_BorderHover);
    const ImU32  dText  = ImGui::ColorConvertFloat4ToU32(dTextV);

    const float gap = 4.0f * gs;     // gap icon→label, label→chevron

    // ── Trigger geometry ────────────────────────────────────────────────────
    const char* label = cfg.triggerLabel.c_str();
    const bool  hasIcon = cfg.triggerIcon && *cfg.triggerIcon;
    ImVec2 ts = ImGui::CalcTextSize(label);

    float btnW = pad.x;
    if (hasIcon) btnW += iconSz + gap;
    btnW += ts.x + pad.x;
    if (showChevron) btnW += gap + chevSz;

    ImGui::PushID(cfg.id);

    // ── Fused linked buttons (ButtonGroup look) ──────────────────────────────
    // Width of one button cell: icon and/or label + symmetric padding.
    auto buttonCellW = [&](const DropdownButton& b) {
        float w = pad.x * 2.0f;
        const bool bIcon = b.icon && *b.icon;
        const bool bText = !b.label.empty();
        if (bIcon) w += iconSz;
        if (bIcon && bText) w += gap;
        if (bText) w += ImGui::CalcTextSize(b.label.c_str()).x;
        if (!bIcon && !bText) w = controlH;     // square icon-less fallback
        return std::max(w, controlH);
    };
    int nLeft = 0, nRight = 0;
    for (const DropdownButton& b : cfg.buttons)
        (b.side == DropdownButton::Side::Left ? nLeft : nRight)++;
    const bool fused = !cfg.buttons.empty();

    // Group origin (left edge of the whole fused widget).
    ImVec2 groupMin = ImGui::GetCursorScreenPos();
    float leftW = 0.0f;
    for (const DropdownButton& b : cfg.buttons)
        if (b.side == DropdownButton::Side::Left) leftW += buttonCellW(b);

    // Reserve the trigger's screen rect: it sits AFTER the left buttons.
    ImGui::SetCursorScreenPos(ImVec2(groupMin.x + leftW, groupMin.y));
    ImVec2 btnMin = ImGui::GetCursorScreenPos();
    const char* popupId = "##menu";
    const bool wasOpen = ImGui::IsPopupOpen(popupId);

    // Open on PRESS (not release): PressedOnClick fires the instant the button
    // goes down, so the menu opens immediately under the cursor.
    ImGui::InvisibleButton("##trigger", ImVec2(btnW, controlH),
                           ImGuiButtonFlags_PressedOnClick);
    const bool clicked = ImGui::IsItemActivated();
    const bool hovered = ImGui::IsItemHovered();
    if (clicked && !wasOpen) ImGui::OpenPopup(popupId);
    const bool isOpen = ImGui::IsPopupOpen(popupId);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 btnMax(btnMin.x + btnW, btnMin.y + controlH);

    // ── Pre-measure the menu so we can place it before BeginPopup ────────────
    const bool multiCol = !cfg.columnHeaders.empty();
    const int  nCols     = multiCol ? (int)cfg.columnHeaders.size() : 1;
    ImVec2 mPad   = V2(Tok::C_Menu_Padding);  mPad.x *= gs; mPad.y *= gs;
    const float colGap   = Flt(Tok::C_Menu_ColumnGap) * gs;
    const float itemGap  = Flt(Tok::C_Menu_ItemGap)   * gs;
    const float rowH     = controlH;
    const float lineH    = ImGui::GetTextLineHeight();
    const float headerH  = multiCol ? lineH + itemGap * 2.0f : 0.0f;
    const float menuIcon = iconSz;
    const float shortcutGap = 16.0f * gs;
    // Horizontal padding inside each item row, so the hover/selected fill
    // extends a little past the icon/label instead of hugging them.
    const float itemPadX = Flt(Tok::C_Menu_ItemPaddingX) * gs;

    std::vector<float> colW((size_t)nCols, 0.0f);
    std::vector<int>   colCount((size_t)nCols, 0);
    for (size_t i = 0; i < cfg.items.size(); ++i) {
        const DropdownItem& it = cfg.items[i];
        int c = multiCol ? std::clamp(it.columnGroup, 0, nCols - 1) : 0;
        float w = menuIcon + gap + ImGui::CalcTextSize(it.label.c_str()).x;
        if (!it.shortcut.empty())
            w += shortcutGap + ImGui::CalcTextSize(it.shortcut.c_str()).x;
        w += itemPadX * 2.0f;     // left + right row padding
        colW[(size_t)c] = std::max(colW[(size_t)c], w);
        colCount[(size_t)c]++;
    }
    if (multiCol) {
        for (int c = 0; c < nCols; ++c)
            colW[(size_t)c] = std::max(colW[(size_t)c],
                ImGui::CalcTextSize(cfg.columnHeaders[(size_t)c].c_str()).x
                + itemPadX * 2.0f);
    }

    int maxRows = 0;
    for (int c = 0; c < nCols; ++c) maxRows = std::max(maxRows, colCount[(size_t)c]);

    float menuW = mPad.x * 2.0f;
    for (int c = 0; c < nCols; ++c) {
        menuW += colW[(size_t)c];
        if (c + 1 < nCols) menuW += colGap;
    }
    float menuH = mPad.y * 2.0f + headerH +
                  (float)maxRows * rowH +
                  (float)std::max(0, maxRows - 1) * itemGap;
    // Custom body overrides the computed item-list size.
    if (cfg.bodyDraw) { menuW = cfg.menuSize.x; menuH = cfg.menuSize.y; }

    // ── Adaptive flip against the main viewport work-rect ─────────────────────
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float wL = vp->WorkPos.x;
    const float wT = vp->WorkPos.y;
    const float wR = vp->WorkPos.x + vp->WorkSize.x;
    const float wB = vp->WorkPos.y + vp->WorkSize.y;

    bool openUp   = (btnMax.y + menuH > wB) && (btnMin.y - menuH >= wT);
    bool openLeft = (btnMin.x + menuW > wR) && (btnMax.x - menuW >= wL);

    ImVec2 menuPos;
    menuPos.x = openLeft ? (btnMax.x - menuW) : btnMin.x;
    menuPos.y = openUp   ? (btnMin.y - menuH) : btnMax.y;
    // Keep on-screen even if neither flip fully fits.
    menuPos.x = std::clamp(menuPos.x, wL, std::max(wL, wR - menuW));
    menuPos.y = std::clamp(menuPos.y, wT, std::max(wT, wB - menuH));

    // The trigger and menu merge along their shared horizontal edge:
    //   • opening down → trigger drops its BOTTOM corners, menu drops its TOP
    //     corner on the trigger-aligned side.
    //   • opening up   → mirror (trigger drops TOP, menu drops BOTTOM).
    // The aligned side is LEFT unless we opened leftward.
    ImDrawFlags triggerRound;
    ImDrawFlags menuRound;
    if (!openUp) {
        triggerRound = ImDrawFlags_RoundCornersTop;
        menuRound    = ImDrawFlags_RoundCornersBottom
                     | (openLeft ? ImDrawFlags_RoundCornersTopLeft
                                 : ImDrawFlags_RoundCornersTopRight);
    } else {
        triggerRound = ImDrawFlags_RoundCornersBottom;
        menuRound    = ImDrawFlags_RoundCornersTop
                     | (openLeft ? ImDrawFlags_RoundCornersBottomLeft
                                 : ImDrawFlags_RoundCornersBottomRight);
    }

    // ── Trigger background + border ───────────────────────────────────────────
    // Always a filled, bordered chip so it reads as clickable on the bar:
    //   • rest  → base bg + subtle border (all corners rounded);
    //   • hover → slightly lighter bg + slightly brighter border;
    //   • open  → "down" bg, join-side corners squared so it merges with the
    //             menu; the brighter border is kept (the menu border continues
    //             it). Border width 0 (token) removes the stroke everywhere.
    {
        ImVec4 fillV   = isOpen ? dDownV : (hovered ? dHovV : dBgV);
        ImVec4 bordV   = (hovered || isOpen) ? dBordHovV : dBordV;
        ImDrawFlags rounding = isOpen ? triggerRound : ImDrawFlags_RoundCornersAll;
        // Fused: square the corners on the side(s) that touch a linked button so the
        // trigger merges with them (ButtonGroup look) — only the group's outer
        // corners stay rounded.
        if (nLeft > 0)  rounding &= ~ImDrawFlags_RoundCornersLeft;
        if (nRight > 0) rounding &= ~ImDrawFlags_RoundCornersRight;
        dl->AddRectFilled(btnMin, btnMax,
                          ImGui::ColorConvertFloat4ToU32(fillV), radius, rounding);
        if (borderW > 0.01f)
            dl->AddRect(btnMin, btnMax,
                        ImGui::ColorConvertFloat4ToU32(bordV), radius, rounding,
                        borderW);
    }

    // ── Trigger content: icon + label + chevron, vertically centred ──────────
    float cx = btnMin.x + pad.x;
    if (hasIcon) {
        DrawIcon(dl, cfg.triggerIcon,
                 ImVec2(cx, btnMin.y + (controlH - iconSz) * 0.5f), iconSz,
                 dIconV);
        cx += iconSz + gap;
    }
    dl->AddText(ImVec2(cx, btnMin.y + (controlH - ts.y) * 0.5f), dText, label);
    if (showChevron) {
        const char* chev = openUp ? "chevron-up" : "chevron-down";
        DrawIcon(dl, chev,
                 ImVec2(btnMax.x - pad.x - chevSz,
                        btnMin.y + (controlH - chevSz) * 0.5f),
                 chevSz, dIconV);
    }

    // ── Render the fused linked buttons (left, then right of the trigger) ─────
    if (fused) {
        const ImVec4 accentV = Col(Tok::S_Color_Accent_Default);
        float lx = groupMin.x;                 // left buttons fill [groupMin, btnMin)
        float rx = btnMax.x;                   // right buttons start at the trigger end
        int leftSeen = 0, rightSeen = 0;
        for (size_t bi = 0; bi < cfg.buttons.size(); ++bi) {
            const DropdownButton& b = cfg.buttons[bi];
            const float cw = buttonCellW(b);
            ImVec2 cmin, cmax;
            const bool isLeft = (b.side == DropdownButton::Side::Left);
            if (isLeft) { cmin = ImVec2(lx, btnMin.y); cmax = ImVec2(lx + cw, btnMax.y); lx += cw; }
            else        { cmin = ImVec2(rx, btnMin.y); cmax = ImVec2(rx + cw, btnMax.y); rx += cw; }
            // Only the GROUP's outer corner of the first-left / last-right cell rounds.
            ImDrawFlags br = ImDrawFlags_RoundCornersNone;
            if (isLeft  && leftSeen  == 0)            br |= ImDrawFlags_RoundCornersLeft;
            if (!isLeft && rightSeen == nRight - 1)   br |= ImDrawFlags_RoundCornersRight;
            ImGui::SetCursorScreenPos(cmin);
            ImGui::PushID((int)(bi + 1000));
            bool bHov = false;
            if (ImGui::InvisibleButton("##lb", ImVec2(cw, controlH))) result.buttonClicked = (int)bi;
            bHov = ImGui::IsItemHovered();
            ImGui::PopID();
            ImVec4 bf = b.active ? accentV : (bHov ? dHovV : dBgV);
            dl->AddRectFilled(cmin, cmax, ImGui::ColorConvertFloat4ToU32(bf), radius, br);
            if (borderW > 0.01f)
                dl->AddRect(cmin, cmax,
                            ImGui::ColorConvertFloat4ToU32(b.active ? accentV : dBordV),
                            radius, br, borderW);
            // Content: icon centred (or icon+label).
            ImVec4 bIconTint = b.active ? Col(Tok::S_Color_Text_Default) : dIconV;
            float bcx = cmin.x + pad.x;
            const bool bIcon = b.icon && *b.icon, bText = !b.label.empty();
            if (!bIcon && !bText) { /* nothing */ }
            if (bIcon && !bText) {  // centre the icon
                DrawIcon(dl, b.icon, ImVec2(cmin.x + (cw - iconSz) * 0.5f,
                         cmin.y + (controlH - iconSz) * 0.5f), iconSz, bIconTint);
            } else {
                if (bIcon) { DrawIcon(dl, b.icon, ImVec2(bcx, cmin.y + (controlH - iconSz) * 0.5f),
                                      iconSz, bIconTint); bcx += iconSz + gap; }
                if (bText) {
                    ImVec2 bts = ImGui::CalcTextSize(b.label.c_str());
                    dl->AddText(ImVec2(bcx, cmin.y + (controlH - bts.y) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(b.active ? Col(Tok::S_Color_Text_Default) : dTextV),
                        b.label.c_str());
                }
            }
            if (bHov && !b.tooltip.empty() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                DrawTooltip(b.tooltip.c_str(), ImGui::GetIO().MousePos);
            (isLeft ? leftSeen : rightSeen)++;
        }
    }

    // ── The menu popup ────────────────────────────────────────────────────────
    if (isOpen) {
        ImGui::SetNextWindowPos(menuPos);
        ImGui::SetNextWindowSize(ImVec2(menuW, menuH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));

        if (ImGui::BeginPopup(popupId,
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse)) {
            // The whole menu is drawn on the window draw list; reserve the
            // content extent so ImGui sizes the popup correctly and the click
            // region covers it.
            ImGui::Dummy(ImVec2(menuW, menuH));
            ImDrawList* mdl = ImGui::GetWindowDrawList();
            const float menuRadius = Flt(Tok::C_Menu_CornerRadius) * gs;
            const float menuBorderW = bordersOn ? Flt(Tok::C_Menu_BorderWidth) * gs : 0.0f;
            const ImVec4 menuBgV   = Col(Tok::C_Menu_Background);
            const ImVec4 borderV   = Col(Tok::C_Menu_Border);
            const ImU32  hdrCol    = ImGui::ColorConvertFloat4ToU32(
                                         Col(Tok::C_Menu_ColumnHeaderText));
            const ImU32  selBg     = ImGui::ColorConvertFloat4ToU32(
                                         Col(Tok::C_Menu_ItemSelectedBg));
            const ImU32  hovBg     = ImGui::ColorConvertFloat4ToU32(
                                         Col(Tok::C_Menu_ItemHoverBg));

            ImVec2 m0 = menuPos;
            ImVec2 m1(menuPos.x + menuW, menuPos.y + menuH);
            mdl->AddRectFilled(m0, m1,
                               ImGui::ColorConvertFloat4ToU32(menuBgV),
                               menuRadius, menuRound);
            if (menuBorderW > 0.01f)
                mdl->AddRect(m0, m1, ImGui::ColorConvertFloat4ToU32(borderV),
                             menuRadius, menuRound, menuBorderW);

            // ── Custom body: the caller draws its own widgets inside the chrome ──
            if (cfg.bodyDraw) {
                ImGui::SetCursorScreenPos(ImVec2(m0.x + mPad.x, m0.y + mPad.y));
                ImGui::PushStyleColor(ImGuiCol_Text, dTextV);
                ImGui::BeginGroup();
                cfg.bodyDraw();
                ImGui::EndGroup();
                ImGui::PopStyleColor();
                ImGui::EndPopup();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(3);
                ImGui::PopID();
                return result;
            }

            ImGuiIO& io = ImGui::GetIO();

            // Column left edges.
            std::vector<float> colX((size_t)nCols, 0.0f);
            float runX = m0.x + mPad.x;
            for (int c = 0; c < nCols; ++c) {
                colX[(size_t)c] = runX;
                runX += colW[(size_t)c] + colGap;
            }

            // Column headers (inset by the row padding so they align with the
            // item icons/labels below).
            if (multiCol) {
                for (int c = 0; c < nCols; ++c)
                    mdl->AddText(ImVec2(colX[(size_t)c] + itemPadX, m0.y + mPad.y),
                                 hdrCol, cfg.columnHeaders[(size_t)c].c_str());
            }

            // Per-column running Y for rows.
            std::vector<float> rowY((size_t)nCols,
                                    m0.y + mPad.y + headerH);

            for (size_t i = 0; i < cfg.items.size(); ++i) {
                const DropdownItem& it = cfg.items[i];
                int c = multiCol ? std::clamp(it.columnGroup, 0, nCols - 1) : 0;
                float x = colX[(size_t)c];
                float y = rowY[(size_t)c];
                rowY[(size_t)c] += rowH + itemGap;

                ImVec2 r0(x, y);
                ImVec2 r1(x + colW[(size_t)c], y + rowH);
                bool rowHov = it.enabled &&
                    io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                    io.MousePos.y >= r0.y && io.MousePos.y <= r1.y;

                if ((int)i == cfg.selectedIndex)
                    mdl->AddRectFilled(r0, r1, selBg, 2.0f * gs);
                else if (rowHov)
                    mdl->AddRectFilled(r0, r1, hovBg, 2.0f * gs);

                const ImVec4 fgV = it.enabled ? dTextV
                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const ImU32 fg = ImGui::ColorConvertFloat4ToU32(fgV);

                float ix = x + itemPadX;     // inset content by the row padding
                DrawIcon(mdl, it.icon,
                         ImVec2(ix, y + (rowH - menuIcon) * 0.5f), menuIcon, fgV);
                ix += menuIcon + gap;
                ImVec2 lts = ImGui::CalcTextSize(it.label.c_str());
                mdl->AddText(ImVec2(ix, y + (rowH - lts.y) * 0.5f), fg,
                             it.label.c_str());

                if (!it.shortcut.empty()) {
                    ImVec2 sts = ImGui::CalcTextSize(it.shortcut.c_str());
                    ImU32 scCol = ImGui::ColorConvertFloat4ToU32(
                        Col(Tok::C_Menu_ColumnHeaderText));
                    mdl->AddText(ImVec2(r1.x - itemPadX - sts.x,
                                        y + (rowH - sts.y) * 0.5f),
                                 scCol, it.shortcut.c_str());
                }

                // Description tooltip on hover-dwell (Blender-style), drawn via
                // the shared topmost tooltip widget. Rows are draw-list (not
                // ImGui items), so we time the dwell ourselves.
                if (rowHov && !it.tooltip.empty()) {
                    static int   s_dwellRow = -1;
                    static float s_dwellT   = 0.0f;
                    if (s_dwellRow != (int)i) { s_dwellRow = (int)i; s_dwellT = 0.0f; }
                    s_dwellT += io.DeltaTime;
                    float delay = Flt(Tok::S_Config_HoverDelayNormal);
                    if (delay <= 0.0f) delay = 0.4f;
                    if (s_dwellT >= delay)
                        DrawTooltip(it.tooltip.c_str(), io.MousePos);
                }

                // Activate on RELEASE over the row (menu convention): a press
                // anywhere followed by a release on the item commits it. This
                // covers both click-then-click and press-trigger-drag-release —
                // the trigger itself still OPENS the menu on press (see above).
                if (rowHov && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    result.changed  = true;
                    result.selected = (int)i;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    ImGui::PopID();
    return result;
}

} // namespace UI

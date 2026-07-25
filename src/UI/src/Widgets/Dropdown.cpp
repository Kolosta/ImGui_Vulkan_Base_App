#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip (shared, topmost, styled)
#include <UI/Widgets/ScrollArea.h>    // custom-scrollbar list region
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>

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

// Live search text per dropdown id (searchable single-column menus). Cleared
// when the menu opens.
std::unordered_map<ImU32, std::string> g_menuSearch;

// True once the mouse button has been RELEASED at least once since the menu
// opened. A menu opened on PRESS that flips UPWARD would otherwise place a row
// directly under the still-held cursor, so the release of the SAME click
// commits an item the user never chose. We only accept a release-to-commit
// after the opening click has ended (standard menu behaviour).
std::unordered_map<ImU32, bool> g_menuArmed;

// Keyboard-navigation selection index into the FILTERED list (arrow up/down
// while typing in the search field). -1 = none.
std::unordered_map<ImU32, int> g_menuKbSel;

// Last frame's list scroll offset per menu — decides the top/bottom scroll
// gutters (so items never scroll under the indicators).
std::unordered_map<ImU32, float> g_menuScroll;

bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    return it != hay.end();
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
    // Open state: a lighter fill than the rest bg so an open trigger reads active.
    // Minimal/title-bar dropdowns keep their subtle (transparent) hover treatment.
    const ImVec4 dOpenV = minimal ? Col(Tok::C_Dropdown_BackgroundHoverMinimal)
                                  : Col(Tok::C_Dropdown_BackgroundOpen);
    const ImVec4 dBordV     = minimal ? transparent : Col(Tok::C_Dropdown_Border);
    const ImVec4 dBordHovV  = minimal ? transparent : Col(Tok::C_Dropdown_BorderHover);
    const ImU32  dText  = ImGui::ColorConvertFloat4ToU32(dTextV);

    const float gap = 4.0f * gs;     // gap icon→label, label→chevron

    // ── Trigger geometry ────────────────────────────────────────────────────
    const char* label = cfg.triggerLabel.c_str();
    // The object picker forces a leading object icon and a trailing action
    // slot (eyedropper / clear cross) inside the trigger.
    const bool  objPick = cfg.objectPicker;
    // The trailing ACTION slot exists when there is something to draw there: a
    // CLEAR cross (a value is set) or the EYEDROPPER (allowed). A no-eyedropper
    // picker with an empty value has NO trailing slot (a plain placeholder).
    const bool  actionSlot = objPick &&
        (cfg.objectPickerHasValue || !cfg.objectPickerNoEyedropper);
    const bool  hasIcon = objPick || (cfg.triggerIcon && *cfg.triggerIcon);
    const char* leadIcon = objPick ? "shape-category" : cfg.triggerIcon;
    // The PLACEHOLDER (shown when the label is empty) sizes the trigger like
    // a real label — an empty-value picker must not collapse under its text.
    ImVec2 ts = ImGui::CalcTextSize(
        (cfg.triggerLabel.empty() && !cfg.placeholder.empty())
            ? cfg.placeholder.c_str() : label);

    float btnW = pad.x;
    if (hasIcon) btnW += iconSz + gap;
    btnW += ts.x + pad.x;
    if (showChevron) btnW += gap + chevSz;
    if (actionSlot)  btnW += gap + iconSz;   // trailing action slot
    if (cfg.triggerWidth > 0.0f) btnW = std::max(btnW, cfg.triggerWidth);

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
    ImVec2 btnMax(btnMin.x + btnW, btnMin.y + controlH);
    const char* popupId = "##menu";
    const bool wasOpen = ImGui::IsPopupOpen(popupId);
    bool objPickActHovered = false;

    // The object picker's trailing ACTION slot (eyedropper / clear cross) sits
    // just left of the chevron. Left edge of that slot:
    const float actionSlotX = actionSlot
        ? (btnMax.x - pad.x - (showChevron ? (chevSz + gap) : 0.0f) - iconSz)
        : btnMax.x;

    // Open on PRESS over the WHOLE trigger (label AND chevron both open the
    // menu). It ALLOWS OVERLAP so the action button (submitted right after, on
    // top) wins its own sub-rect: clicking the eyedropper / cross runs the
    // action and never opens the menu, while hovering anywhere on the trigger
    // — including the action / chevron zone — keeps the chrome highlighted.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##trigger", ImVec2(btnW, controlH),
                           ImGuiButtonFlags_PressedOnClick);
    bool clicked = ImGui::IsItemActivated();
    // Full width of the whole widget (left buttons + trigger + right buttons).
    // Every SetCursorScreenPos below (action slot, fused buttons, popup) is a
    // manual DRAW placement; if the layout cursor is left parked over the
    // trigger, the next panel row (e.g. the modifier Up/Down/Remove buttons)
    // draws ON the same line as this dropdown. We fix the layout by submitting
    // one final Dummy of this size at groupMin before returning (see FinishRow).
    float groupW = leftW + btnW;
    for (const DropdownButton& b : cfg.buttons)
        if (b.side == DropdownButton::Side::Right) groupW += buttonCellW(b);
    // Restore a clean single-row layout: place the cursor at the group origin
    // and submit a real (interaction-free) item so ImGui advances to the next
    // line AND grows the parent bounds — SetCursorScreenPos alone would assert
    // ("uses SetCursorPos to extend boundaries ... submit an item afterwards").
    auto FinishRow = [&]() {
        ImGui::SetCursorScreenPos(groupMin);
        ImGui::Dummy(ImVec2(groupW, controlH));
    };
    // Hover uses the geometric rect (the overlapping action button would
    // otherwise steal IsItemHovered from the trigger over its sub-rect).
    bool hovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);

    // The action button, on TOP of the trigger over its slot (AllowOverlap
    // above lets it take priority for the click there).
    if (actionSlot) {
        const ImRect actRect(ImVec2(actionSlotX, btnMin.y),
                             ImVec2(actionSlotX + iconSz, btnMax.y));
        ImGui::SetCursorScreenPos(actRect.Min);
        ImGui::PushID("##ddact");
        if (ImGui::InvisibleButton("##a", ImVec2(iconSz, controlH),
                                   ImGuiButtonFlags_PressedOnClick)) {
            if (cfg.objectPickerHasValue) result.cleared = true;
            else                          result.pickRequested = true;
        }
        objPickActHovered = ImGui::IsItemHovered();
        // A press that landed on the action must NOT also open the menu.
        if (objPickActHovered && ImGui::IsItemActivated()) clicked = false;
        ImGui::PopID();
    }
    const ImU32 menuKey = ImGui::GetID("##menukey");
    if (clicked && !wasOpen) {
        g_menuSearch[menuKey].clear();   // a fresh menu starts unfiltered
        g_menuArmed[menuKey] = false;    // wait for the opening click to end
        ImGui::OpenPopup(popupId);
    }
    const bool isOpen = ImGui::IsPopupOpen(popupId);
    // Arm the menu once the mouse button is up (so the opening click cannot
    // commit a row when the menu flipped upward under the cursor).
    if (isOpen && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        g_menuArmed[menuKey] = true;
    const bool menuArmed = g_menuArmed[menuKey];

    ImDrawList* dl = ImGui::GetWindowDrawList();

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
    // A single-column list never reads narrower than the trigger, and a
    // searchable one gets a comfortable minimum so short object names don't
    // collapse it.
    if (!multiCol) {
        float minW = btnW;
        if (cfg.searchable) minW = std::max(minW, 180.0f * gs);
        colW[0] = std::max(colW[0], minW - mPad.x * 2.0f);
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
    // Custom body: the popup AUTO-RESIZES to its content (width + height) with the
    // menu padding. We can't know that size before drawing, so for the position/
    // flip math we use the size MEASURED last frame (cached per dropdown id),
    // falling back to cfg.menuSize the first frame.
    static std::unordered_map<ImU32, ImVec2> s_bodySize;
    const ImU32 bodyKey = ImGui::GetID(cfg.id);
    if (cfg.bodyDraw) {
        auto it = s_bodySize.find(bodyKey);
        ImVec2 sz = (it != s_bodySize.end()) ? it->second : cfg.menuSize;
        menuW = sz.x; menuH = sz.y;
    }

    // ── Adaptive flip against the main viewport work-rect ─────────────────────
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float wL = vp->WorkPos.x;
    const float wT = vp->WorkPos.y;
    const float wR = vp->WorkPos.x + vp->WorkSize.x;
    const float wB = vp->WorkPos.y + vp->WorkSize.y;

    // ── Height cap (single-column lists): the menu never exceeds the token
    //    cap nor the work area — beyond it the ITEM LIST scrolls inside the
    //    menu (custom scrollbar), optionally under a search field. The flip
    //    math below uses the CLAMPED height, so up/down placement always fits.
    const bool  single  = !multiCol && !cfg.bodyDraw;
    const float searchH = (single && cfg.searchable) ? rowH + itemGap : 0.0f;
    float listH   = menuH - mPad.y * 2.0f;   // items-only extent
    bool  scrolled = false;
    if (single) {
        const float cap = std::min(Flt(Tok::C_Menu_MaxHeight) * gs,
                                   (wB - wT) - 24.0f * gs);
        if (cfg.searchable || menuH + searchH > cap) {
            scrolled = true;
            listH = std::max(rowH * 2.0f,
                             std::min(listH, cap - mPad.y * 2.0f - searchH));
            menuH = mPad.y * 2.0f + searchH + listH;
        }
    }

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
        ImVec4 fillV   = isOpen ? dOpenV : (hovered ? dHovV : dBgV);
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

    // ── Trigger content: icon + label + (action) + chevron, centred ──────────
    float cx = btnMin.x + pad.x;
    if (hasIcon) {
        DrawIcon(dl, leadIcon,
                 ImVec2(cx, btnMin.y + (controlH - iconSz) * 0.5f), iconSz,
                 dIconV);
        cx += iconSz + gap;
    }
    // Empty + a placeholder given → draw the placeholder in the subtle text
    // colour; otherwise the real label in the normal colour.
    if (cfg.triggerLabel.empty() && !cfg.placeholder.empty()) {
        const ImU32 phCol = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Disabled));
        dl->AddText(ImVec2(cx, btnMin.y + (controlH - ts.y) * 0.5f), phCol,
                    cfg.placeholder.c_str());
    } else {
        dl->AddText(ImVec2(cx, btnMin.y + (controlH - ts.y) * 0.5f), dText, label);
    }
    float rightX = btnMax.x - pad.x;
    if (showChevron) {
        const char* chev = openUp ? "chevron-up" : "chevron-down";
        DrawIcon(dl, chev,
                 ImVec2(rightX - chevSz,
                        btnMin.y + (controlH - chevSz) * 0.5f),
                 chevSz, dIconV);
        rightX -= chevSz + gap;
    }
    // Object-picker trailing action GLYPH: an eyedropper (start a pick) when
    // empty, a clear cross when a value is set. The clickable button was drawn
    // earlier (before the trigger) so it wins the click; here we only draw the
    // icon, greyed at rest and WHITE on hover so the user sees it is live.
    if (actionSlot) {
        const char* icon = cfg.objectPickerHasValue ? "close" : "colorize";
        const ImVec4 rest  = Col(Tok::S_Color_Text_Subtle);
        const ImVec4 hovC  = Col(Tok::S_Color_Text_Default);
        DrawIcon(dl, icon,
                 ImVec2(actionSlotX, btnMin.y + (controlH - iconSz) * 0.5f),
                 iconSz, objPickActHovered ? hovC : rest);
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

    // ── Custom-body popup: AUTO-RESIZES to its content (W + H) with real window
    //    padding, so the menu fits exactly and separators respect the margin. ──
    if (isOpen && cfg.bodyDraw) {
        const float menuRadius  = Flt(Tok::C_Menu_CornerRadius) * gs;
        const float menuBorderW = bordersOn ? Flt(Tok::C_Menu_BorderWidth) * gs : 0.0f;
        ImGui::SetNextWindowPos(menuPos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, mPad);  // real padding → content-fit
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  menuRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, menuBorderW);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Col(Tok::C_Menu_Background));
        ImGui::PushStyleColor(ImGuiCol_Border,  Col(Tok::C_Menu_Border));
        if (ImGui::BeginPopup(popupId,
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::PushStyleColor(ImGuiCol_Text, dTextV);
            ImGui::BeginGroup();
            cfg.bodyDraw();
            ImGui::EndGroup();
            ImGui::PopStyleColor();
            // Cache the content size (+ padding) for next frame's position/flip.
            ImVec2 content = ImGui::GetItemRectSize();
            s_bodySize[bodyKey] = ImVec2(content.x + mPad.x * 2.0f,
                                         content.y + mPad.y * 2.0f);
            // Esc closes the dropdown (like click-outside / right-click), unless
            // an inner field is being edited (then Esc cancels that edit first).
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        FinishRow();
        ImGui::PopID();
        return result;
    }

    // ── The (item-list) menu popup ─────────────────────────────────────────────
    if (isOpen) {
        ImGui::SetNextWindowPos(menuPos);
        ImGui::SetNextWindowSize(ImVec2(menuW, menuH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        // The rounded/merged menu background is drawn manually on the popup's
        // draw list below; the WINDOW background must still be OPAQUE (not
        // transparent) or widgets BEHIND the popup — the panel's Up/Down/Remove
        // buttons — show THROUGH it and read as being on top.
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Col(Tok::C_Menu_Background));

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

            ImGuiIO& io = ImGui::GetIO();
            // Keyboard-navigation highlight (arrow up/down over the FILTERED
            // list). -1 = none; set by the scrolled path each frame.
            int& kbSel = g_menuKbSel[menuKey];

            // Draw one item row at (x, y) on `rdl`; commits on release. `clip`
            // (if set) bounds the mouse hit-test to the visible list area, so a
            // row scrolled OUT of view (menu opened upward) can never receive a
            // hover/release — the cause of the upward-open self-pick. `kbHi`
            // draws the keyboard-navigation highlight.
            bool closeMenu = false;
            const ImVec4* clip = nullptr;
            auto drawRow = [&](ImDrawList* rdl, size_t i, float x, float y,
                               float width, bool kbHi) {
                const DropdownItem& it = cfg.items[i];
                ImVec2 r0(x, y);
                ImVec2 r1(x + width, y + rowH);
                bool rowHov = it.enabled &&
                    io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                    io.MousePos.y >= r0.y && io.MousePos.y <= r1.y &&
                    (!clip || (io.MousePos.x >= clip->x && io.MousePos.y >= clip->y &&
                               io.MousePos.x <= clip->z && io.MousePos.y <= clip->w));

                if ((int)i == cfg.selectedIndex)
                    rdl->AddRectFilled(r0, r1, selBg, 2.0f * gs);
                else if (rowHov || kbHi)
                    rdl->AddRectFilled(r0, r1, hovBg, 2.0f * gs);

                const ImVec4 fgV = it.enabled ? dTextV
                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                const ImU32 fg = ImGui::ColorConvertFloat4ToU32(fgV);

                float ix = x + itemPadX;     // inset content by the row padding
                DrawIcon(rdl, it.icon,
                         ImVec2(ix, y + (rowH - menuIcon) * 0.5f), menuIcon, fgV);
                ix += menuIcon + gap;
                ImVec2 lts = ImGui::CalcTextSize(it.label.c_str());
                rdl->AddText(ImVec2(ix, y + (rowH - lts.y) * 0.5f), fg,
                             it.label.c_str());

                if (!it.shortcut.empty()) {
                    ImVec2 sts = ImGui::CalcTextSize(it.shortcut.c_str());
                    ImU32 scCol = ImGui::ColorConvertFloat4ToU32(
                        Col(Tok::C_Menu_ColumnHeaderText));
                    rdl->AddText(ImVec2(r1.x - itemPadX - sts.x,
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
                // anywhere followed by a release on the item commits it —
                // BUT only once the opening click has ended (menuArmed), so a
                // menu flipped upward under the held cursor doesn't self-pick.
                if (rowHov && menuArmed &&
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    result.changed  = true;
                    result.selected = (int)i;
                    closeMenu = true;
                }
            };

            if (scrolled) {
                // ── Scrolling single-column list with a live search field ──
                std::string& search = g_menuSearch[menuKey];
                float y0 = m0.y + mPad.y;

                // Build the FILTERED index list first (drives search commit +
                // keyboard navigation).
                std::vector<size_t> vis;
                vis.reserve(cfg.items.size());
                for (size_t i = 0; i < cfg.items.size(); ++i)
                    if (ContainsCI(cfg.items[i].label, search)) vis.push_back(i);

                int kbChanged = 0;   // -1 up, +1 down; drives a ONE-shot scroll
                if (cfg.searchable) {
                    // Search field styled like the editors' search bars (theme
                    // frame background, field vertically centred), no separator.
                    const float padY = std::max(0.0f, (rowH - lineH) * 0.5f);
                    ImGui::SetCursorScreenPos(ImVec2(m0.x + mPad.x, y0));
                    ImGui::SetNextItemWidth(menuW - mPad.x * 2.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                        ImVec2(ImGui::GetStyle().FramePadding.x, padY));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                                        Flt(Tok::S_CornerRadius_Control) * gs * 0.5f);
                    char buf[128];
                    std::snprintf(buf, sizeof buf, "%s", search.c_str());
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere();   // type-to-filter at once
                        kbSel = -1;
                    }
                    // Arrow keys navigate the FILTERED list from inside the
                    // field (nav is disabled app-wide, so they are ours).
                    const int nVis = (int)vis.size();
                    if (nVis > 0) {
                        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
                            kbSel = (kbSel < 0) ? 0 : std::min(kbSel + 1, nVis - 1);
                            kbChanged = 1;
                        }
                        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
                            kbSel = (kbSel <= 0) ? 0 : kbSel - 1;
                            kbChanged = -1;
                        }
                    }
                    const bool commit = ImGui::InputTextWithHint(
                        "##ddsearch", "Search\xE2\x80\xA6", buf, sizeof buf,
                        ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::PopStyleVar(2);
                    if (std::string(buf) != search) { search = buf; kbSel = -1; }
                    if (commit && !vis.empty()) {
                        // Enter picks the keyboard-highlighted row, else first.
                        const int pick = (kbSel >= 0 && kbSel < nVis) ? kbSel : 0;
                        result.changed  = true;
                        result.selected = (int)vis[(size_t)pick];
                        closeMenu = true;
                    }
                    y0 += rowH + itemGap;
                }

                // Reserve a small GUTTER above / below the list for the scroll
                // indicators, so items never scroll UNDER them. The gutters
                // appear only when there is something off-view in that
                // direction — measured from last frame's scroll (a 1-frame
                // gutter latency on the very first open is imperceptible).
                float& lastScroll = g_menuScroll[menuKey];
                const float gutter = 9.0f * gs;
                const float pitch  = rowH + itemGap;
                const float total  = (float)vis.size() * pitch -
                                     (vis.empty() ? 0.0f : itemGap);
                const float availH = m0.y + menuH - mPad.y - y0;
                const bool  gutTop = lastScroll > 1.0f;
                const float listTop = y0 + (gutTop ? gutter : 0.0f);
                float listH = availH - (gutTop ? gutter : 0.0f);
                const bool overflowBelow =
                    total - lastScroll > (availH - (gutTop ? gutter : 0.0f)) + 1.0f;
                if (overflowBelow) listH -= gutter;
                listH = std::max(pitch, listH);

                ImGui::SetCursorScreenPos(ImVec2(m0.x + mPad.x, listTop));
                const ImVec4 listClip(m0.x, listTop, m0.x + menuW, listTop + listH);
                clip = &listClip;   // bounds the hit-test to the visible list
                bool moreAbove = false, moreBelow = false;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                if (UI::BeginScroll("##ddList",
                                    ImVec2(menuW - mPad.x * 2.0f, listH))) {
                    const ImVec2 base = ImGui::GetCursorScreenPos();
                    const float rowW = ImGui::GetContentRegionAvail().x;
                    ImGui::Dummy(ImVec2(rowW, std::max(total, 1.0f)));
                    // Blender-style: the view stays PUT while the highlight
                    // moves within it, and only scrolls by ONE row when the
                    // highlight would leave the top or bottom edge. Done ONLY
                    // on the frame an arrow moved it, so the wheel is free.
                    if (kbChanged != 0 && kbSel >= 0 && kbSel < (int)vis.size()) {
                        const float rowTop = (float)kbSel * pitch;
                        const float rowBot = rowTop + rowH;
                        const float sy = ImGui::GetScrollY();
                        if (rowTop < sy)               ImGui::SetScrollY(rowTop);
                        else if (rowBot > sy + listH)  ImGui::SetScrollY(rowBot - listH);
                    }
                    const float sy = ImGui::GetScrollY();
                    lastScroll = sy;
                    moreAbove = sy > 1.0f;
                    moreBelow = sy + listH < total - 1.0f;
                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                    for (size_t k = 0; k < vis.size(); ++k)
                        drawRow(cdl, vis[k], base.x,
                                base.y + (float)k * pitch, rowW, (int)k == kbSel);
                }
                UI::EndScroll();
                ImGui::PopStyleColor();
                // Up / down chevron indicators, drawn INSIDE the reserved
                // gutters (above / below the list) so items never overlap them.
                {
                    const ImU32 icol = ImGui::ColorConvertFloat4ToU32(
                        Col(Tok::C_Menu_ColumnHeaderText));
                    const float cxm = m0.x + menuW * 0.5f;
                    if (moreAbove) {
                        const float ty = y0 + (gutter - 4.0f * gs) * 0.5f;
                        mdl->AddTriangleFilled(ImVec2(cxm - 4*gs, ty + 4*gs),
                                               ImVec2(cxm + 4*gs, ty + 4*gs),
                                               ImVec2(cxm, ty), icol);
                    }
                    if (moreBelow) {
                        const float ty = listTop + listH +
                                         (gutter - 4.0f * gs) * 0.5f;
                        mdl->AddTriangleFilled(ImVec2(cxm - 4*gs, ty),
                                               ImVec2(cxm + 4*gs, ty),
                                               ImVec2(cxm, ty + 4*gs), icol);
                    }
                }
            } else {
                // ── Fixed layout (multi-column, or short single lists) ──
                std::vector<float> colX((size_t)nCols, 0.0f);
                float runX = m0.x + mPad.x;
                for (int c = 0; c < nCols; ++c) {
                    colX[(size_t)c] = runX;
                    runX += colW[(size_t)c] + colGap;
                }
                // Column headers (inset by the row padding so they align with
                // the item icons/labels below).
                if (multiCol) {
                    for (int c = 0; c < nCols; ++c)
                        mdl->AddText(ImVec2(colX[(size_t)c] + itemPadX,
                                            m0.y + mPad.y),
                                     hdrCol, cfg.columnHeaders[(size_t)c].c_str());
                }
                std::vector<float> rowY((size_t)nCols, m0.y + mPad.y + headerH);
                for (size_t i = 0; i < cfg.items.size(); ++i) {
                    int c = multiCol
                        ? std::clamp(cfg.items[i].columnGroup, 0, nCols - 1) : 0;
                    const float y = rowY[(size_t)c];
                    rowY[(size_t)c] += rowH + itemGap;
                    drawRow(mdl, i, colX[(size_t)c], y, colW[(size_t)c], false);
                }
            }
            if (closeMenu) ImGui::CloseCurrentPopup();
            // Esc closes the menu — unless a field (e.g. the search box) is being
            // edited, where Esc cancels/clears that edit first.
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    FinishRow();
    ImGui::PopID();
    return result;
}

// ── DropdownButtonRow — fused toggle cells in the dropdown chrome ────────────

int DropdownButtonRow(const char* id, const std::vector<DropdownButton>& cells,
                      float cellW) {
    if (cells.empty()) return -1;
    DS::DesignSystem::ComponentScope _cs("Dropdown");
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    const float controlH = Flt(Tok::C_Dropdown_Height) * gs;
    ImVec2 pad = V2(Tok::C_Dropdown_Padding); pad.x *= gs; pad.y *= gs;
    const float iconSz = Flt(Tok::C_Dropdown_IconSize) * gs;
    const float radius = Flt(Tok::C_Dropdown_CornerRadius) * gs;
    const bool  bordersOn = ds.BordersEnabled();
    const float borderW = bordersOn ? Flt(Tok::C_Dropdown_BorderWidth) * gs : 0.0f;
    const float gap = 4.0f * gs;

    const ImVec4 bgV     = Col(Tok::C_Dropdown_Background);
    const ImVec4 hovV    = Col(Tok::C_Dropdown_BackgroundHover);
    const ImVec4 bordV   = Col(Tok::C_Dropdown_Border);
    const ImVec4 accentV = Col(Tok::S_Color_Accent_Default);
    const ImVec4 iconV   = Col(Tok::C_Dropdown_Icon);
    const ImVec4 textV   = Col(Tok::C_Dropdown_Text);

    auto cellWidth = [&](const DropdownButton& b) {
        if (cellW > 0.0f) return cellW;
        float w = pad.x * 2.0f;
        const bool bIcon = b.icon && *b.icon;
        const bool bText = !b.label.empty();
        if (bIcon) w += iconSz;
        if (bIcon && bText) w += gap;
        if (bText) w += ImGui::CalcTextSize(b.label.c_str()).x;
        if (!bIcon && !bText) w = controlH;
        return std::max(w, controlH);
    };

    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int clicked = -1;
    float x = origin.x;
    float totalW = 0.0f;
    for (const DropdownButton& b : cells) totalW += cellWidth(b);

    for (std::size_t i = 0; i < cells.size(); ++i) {
        const DropdownButton& b = cells[i];
        const float cw = cellWidth(b);
        const ImVec2 cmin(x, origin.y), cmax(x + cw, origin.y + controlH);
        ImGui::SetCursorScreenPos(cmin);
        ImGui::PushID((int)i);
        if (ImGui::InvisibleButton("##cell", ImVec2(cw, controlH)) && b.enabled)
            clicked = (int)i;
        const bool hov = b.enabled && ImGui::IsItemHovered();
        ImGui::PopID();

        // Only the ROW's outer corners round (the ButtonGroup / snap look).
        ImDrawFlags rf = ImDrawFlags_RoundCornersNone;
        if (i == 0)                rf |= ImDrawFlags_RoundCornersLeft;
        if (i + 1 == cells.size()) rf |= ImDrawFlags_RoundCornersRight;
        const ImVec4 fill = b.active ? accentV : (hov ? hovV : bgV);
        dl->AddRectFilled(cmin, cmax, ImGui::ColorConvertFloat4ToU32(fill),
                          radius, rf);
        if (borderW > 0.01f)
            dl->AddRect(cmin, cmax, ImGui::ColorConvertFloat4ToU32(bordV),
                        radius, rf, borderW);

        ImVec4 tint = b.active ? textV : iconV;
        if (!b.enabled) tint.w *= 0.4f;   // dimmed, like a disabled control
        const bool bIcon = b.icon && *b.icon;
        const bool bText = !b.label.empty();
        if (bIcon && !bText) {
            // Icon-only cell: dead-centred (the snap magnet look).
            DrawIcon(dl, b.icon,
                     ImVec2(cmin.x + (cw - iconSz) * 0.5f,
                            cmin.y + (controlH - iconSz) * 0.5f),
                     iconSz, tint);
        } else {
            float cx = cmin.x + pad.x;
            if (bIcon) {
                DrawIcon(dl, b.icon,
                         ImVec2(cx, cmin.y + (controlH - iconSz) * 0.5f),
                         iconSz, tint);
                cx += iconSz + gap;
            }
            if (bText) {
                const ImVec2 ts = ImGui::CalcTextSize(b.label.c_str());
                ImVec4 tv = textV;
                if (!b.enabled) tv.w *= 0.4f;
                dl->AddText(ImVec2(cx, cmin.y + (controlH - ts.y) * 0.5f),
                            ImGui::ColorConvertFloat4ToU32(tv),
                            b.label.c_str());
            }
        }
        if (hov && !b.tooltip.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            DrawTooltip(b.tooltip.c_str(), ImGui::GetIO().MousePos);
        x += cw;
    }

    // One layout item for the whole row (the cells were manual placements).
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(totalW, controlH));
    ImGui::PopID();
    return clicked;
}

} // namespace UI

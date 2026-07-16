#include <UI/Widgets/PopupMenu.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>
#include <string>
#include <unordered_map>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
ImVec2 V2(Tok t)  { return DS::DesignSystem::Instance().GetVec2(t); }

void DrawIcon(ImDrawList* dl, const char* icon, ImVec2 pos, float size,
              const ImVec4& tint) {
    if (!icon || !*icon) return;
    auto& im = VectorGraphics::IconManager::Instance();
    auto md = im.GetDefaultMetadata(icon);
    if (md.colorZones.empty()) return;
    for (auto& z : md.colorZones) z.customColor = tint;
    im.RenderIcon(dl, icon, pos, size, md);
}

// Shared row metrics (mirror the Dropdown values).
struct Metrics {
    float gs, rowH, gap, menuIcon, chevSz, shortcutGap, itemPadX, itemGap;
    ImVec2 mPad;
};
Metrics Measure() {
    Metrics m;
    m.gs = DS::DesignSystem::Instance().GetGlobalScale();
    m.rowH        = Flt(Tok::C_Dropdown_Height) * m.gs;
    m.gap         = 4.0f * m.gs;
    m.menuIcon    = Flt(Tok::C_Dropdown_IconSize) * m.gs;
    m.chevSz      = Flt(Tok::C_Dropdown_ChevronSize) * m.gs;  // small submenu ▸
    m.shortcutGap = 16.0f * m.gs;
    m.itemPadX    = Flt(Tok::C_Menu_ItemPaddingX) * m.gs;
    m.itemGap     = Flt(Tok::C_Menu_ItemGap) * m.gs;
    m.mPad = V2(Tok::C_Menu_Padding); m.mPad.x *= m.gs; m.mPad.y *= m.gs;
    return m;
}
} // namespace

ImVec2 MeasureMenu(const std::vector<MenuRow>& rows) {
    Metrics m = Measure();
    float colW = 0.0f;
    for (const MenuRow& it : rows) {
        float w = m.menuIcon + m.gap + ImGui::CalcTextSize(it.label.c_str()).x;
        if (!it.shortcut.empty())
            w += m.shortcutGap + ImGui::CalcTextSize(it.shortcut.c_str()).x;
        if (it.hasSubmenu) w += m.shortcutGap;   // room for the ▸ chevron
        w += m.itemPadX * 2.0f;
        colW = std::max(colW, w);
    }
    float menuW = m.mPad.x * 2.0f + colW;
    float menuH = m.mPad.y * 2.0f + (float)rows.size() * m.rowH +
                  (float)std::max<int>(0, (int)rows.size() - 1) * m.itemGap;
    return ImVec2(menuW, menuH);
}

int MenuBody(ImDrawList* dl, ImVec2 pos, ImVec2 size,
             const std::vector<MenuRow>& rows, ImDrawFlags menuRound,
             int selectedIndex, int* hoveredOut, bool drawBorder,
             bool onRelease) {
    Metrics m = Measure();
    const float menuRadius  = Flt(Tok::C_Menu_CornerRadius) * m.gs;
    // Honour the global border toggle (S_Border_Enabled): 0 width = no border.
    const float menuBorderW = DS::DesignSystem::Instance().BordersEnabled()
                            ? Flt(Tok::C_Menu_BorderWidth) * m.gs : 0.0f;
    const ImVec4 menuBgV = Col(Tok::C_Menu_Background);
    const ImVec4 borderV = Col(Tok::C_Menu_Border);
    const ImU32  selBg   = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_ItemSelectedBg));
    const ImU32  hovBg   = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_ItemHoverBg));
    const ImVec4 dTextV  = Col(Tok::C_Dropdown_Text);
    const ImU32  scCol   = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_ColumnHeaderText));

    ImVec2 m0 = pos, m1(pos.x + size.x, pos.y + size.y);
    if (drawBorder) {
        dl->AddRectFilled(m0, m1, ImGui::ColorConvertFloat4ToU32(menuBgV), menuRadius, menuRound);
        if (menuBorderW > 0.01f)
            dl->AddRect(m0, m1, ImGui::ColorConvertFloat4ToU32(borderV), menuRadius, menuRound, menuBorderW);
    }

    ImGuiIO& io = ImGui::GetIO();
    int clicked = -1, hovered = -1;
    float x = m0.x + m.mPad.x;
    float colW = size.x - m.mPad.x * 2.0f;
    float y = m0.y + m.mPad.y;
    const float halfGap = m.itemGap * 0.5f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const MenuRow& it = rows[i];
        // Visual highlight rect — UNCHANGED (inset by the side padding, its own
        // corner radius), so the coloured hover pill looks exactly as before.
        ImVec2 r0(x, y), r1(x + colW, y + m.rowH);
        // Hit/hover rect — EXTENDED to the menu's full width (side margins
        // included) and across the inter-row gap (half above/below; the first
        // and last rows reach the menu's inner top/bottom edge). So the entire
        // band, margins and all, selects/clicks the item even though the
        // coloured highlight stays small. Reported for any row (incl. disabled)
        // so callers can close a submenu when the cursor moves onto a sibling.
        float hitTop = (i == 0)               ? m0.y : y - halfGap;
        float hitBot = (i + 1 == rows.size()) ? m1.y : y + m.rowH + halfGap;
        bool overRow = io.MousePos.x >= m0.x && io.MousePos.x <= m1.x &&
                       io.MousePos.y >= hitTop && io.MousePos.y <= hitBot;
        if (overRow) hovered = (int)i;
        bool rowHov = overRow && it.enabled;

        if ((int)i == selectedIndex) dl->AddRectFilled(r0, r1, selBg, 2.0f * m.gs);
        else if (rowHov)             dl->AddRectFilled(r0, r1, hovBg, 2.0f * m.gs);

        const ImVec4 fgV = it.enabled ? dTextV
                                      : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImU32 fg = ImGui::ColorConvertFloat4ToU32(fgV);
        float ix = x + m.itemPadX;
        DrawIcon(dl, it.icon, ImVec2(ix, y + (m.rowH - m.menuIcon) * 0.5f), m.menuIcon, fgV);
        ix += m.menuIcon + m.gap;
        ImVec2 lts = ImGui::CalcTextSize(it.label.c_str());
        dl->AddText(ImVec2(ix, y + (m.rowH - lts.y) * 0.5f), fg, it.label.c_str());

        if (it.hasSubmenu) {
            // Right-aligned ▸ chevron at the reduced chevron size (like the
            // dropdown trigger chevron), vertically centred.
            DrawIcon(dl, "chevron-right",
                     ImVec2(r1.x - m.itemPadX - m.chevSz,
                            y + (m.rowH - m.chevSz) * 0.5f), m.chevSz, fgV);
        } else if (!it.shortcut.empty()) {
            ImVec2 sts = ImGui::CalcTextSize(it.shortcut.c_str());
            dl->AddText(ImVec2(r1.x - m.itemPadX - sts.x, y + (m.rowH - sts.y) * 0.5f),
                        scCol, it.shortcut.c_str());
        }

        // Activate on RELEASE over the row (menu convention) for the MAIN popup
        // body (its window covers the rows, so a press there doesn't close it).
        // A SUBMENU is drawn on the foreground draw list, OUTSIDE the popup
        // window, so a press there closes the popup before any release — those
        // bodies pass onRelease=false and commit on press instead.
        const bool fire = onRelease ? ImGui::IsMouseReleased(ImGuiMouseButton_Left)
                                    : ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (rowHov && !it.hasSubmenu && fire)
            clicked = (int)i;
        y += m.rowH + m.itemGap;
    }
    if (hoveredOut) *hoveredOut = hovered;
    return clicked;
}

// ── Floating context menu (one level of sub-menus) ───────────────────────────
namespace {
// Convert MenuEntry list → MenuRow list for drawing.
std::vector<MenuRow> EntriesToRows(const std::vector<MenuEntry>& es) {
    std::vector<MenuRow> rows;
    rows.reserve(es.size());
    for (const MenuEntry& e : es) {
        MenuRow r;
        r.icon = e.icon; r.label = e.label; r.shortcut = e.shortcut;
        r.enabled = e.enabled; r.hasSubmenu = !e.submenu.empty();
        rows.push_back(r);
    }
    return rows;
}
ImVec2 ClampToViewport(ImVec2 pos, ImVec2 size) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float wL = vp->WorkPos.x, wT = vp->WorkPos.y;
    float wR = wL + vp->WorkSize.x, wB = wT + vp->WorkSize.y;
    pos.x = std::clamp(pos.x, wL, std::max(wL, wR - size.x));
    pos.y = std::clamp(pos.y, wT, std::max(wT, wB - size.y));
    return pos;
}
} // namespace

bool ContextMenu(const char* popupId, ImVec2 screenPos,
                 const std::vector<MenuEntry>& entries, const char* title) {
    DS::DesignSystem::ComponentScope _cs("Dropdown");
    if (!ImGui::IsPopupOpen(popupId)) return false;

    std::vector<MenuRow> rows = EntriesToRows(entries);
    ImVec2 rowsSize = MeasureMenu(rows);
    Metrics mm = Measure();
    // Optional title band (header text + separator) above the rows.
    const bool hasTitle = title && *title;
    const float titleH = hasTitle ? (ImGui::GetTextLineHeight() + mm.itemGap * 2.0f + 1.0f) : 0.0f;
    ImVec2 size{ rowsSize.x, rowsSize.y + titleH };
    // Title may be wider than the rows — grow the menu to fit it.
    if (hasTitle) {
        float tw = ImGui::CalcTextSize(title).x + mm.mPad.x * 2.0f + mm.itemPadX * 2.0f;
        size.x = std::max(size.x, tw);
    }
    ImVec2 pos  = ClampToViewport(screenPos, size);

    bool open = true;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));

    // Per-menu persistent state (Blender-accurate submenu behaviour): the active
    // submenu is "sticky" — it opens after a short dwell on its parent row and
    // stays open until the cursor dwells on a DIFFERENT row (or over the submenu
    // it stays). Keyed by popupId so distinct menus don't share state.
    struct SubState { int active = -1; int pendingRow = -1; float dwell = 0.0f;
                      int tipKey = -1; float tipDwell = 0.0f; };
    static std::unordered_map<std::string, SubState> s_state;
    SubState& ss = s_state[popupId];
    const float kSubmenuDelay = 0.18f;   // seconds before a submenu opens

    if (ImGui::BeginPopup(popupId,
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // Esc cancels the menu (the popup doesn't take keyboard focus, so
        // ImGui's own nav-cancel never fires for it — handle it explicitly).
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
            ss = SubState{};
        }
        ImGui::Dummy(size);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        Metrics m = Measure();

        // Titled menu: draw ONE rounded card (bg + border) spanning title + body
        // so there is a single outline. The title text + a partial-width
        // separator (inset by the side margins) sit on top; MenuBody then draws
        // its rows WITHOUT its own bg/border (drawBorder=false), so no full-width
        // top edge appears under the title (that was the spurious 2nd separator).
        ImVec2 rowsPos{ pos.x, pos.y + titleH };
        ImVec2 rowsBodySize{ size.x, size.y - titleH };
        if (hasTitle) {
            float menuRadius  = Flt(Tok::C_Menu_CornerRadius) * m.gs;
            float menuBorderW = DS::DesignSystem::Instance().BordersEnabled()
                              ? Flt(Tok::C_Menu_BorderWidth) * m.gs : 0.0f;
            ImU32 bg  = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_Background));
            ImU32 bd  = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_Border));
            ImU32 sep = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_Border));
            ImU32 txt = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Menu_TitleText));
            // Whole-card background + single outline.
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg,
                              menuRadius, ImDrawFlags_RoundCornersAll);
            if (menuBorderW > 0.01f)
                dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), bd,
                            menuRadius, ImDrawFlags_RoundCornersAll, menuBorderW);
            dl->AddText(ImVec2(pos.x + m.mPad.x + m.itemPadX, pos.y + m.itemGap), txt, title);
            // The kept separator: partial width (inset by the side margins), 1px.
            dl->AddLine(ImVec2(pos.x + m.mPad.x, pos.y + titleH - 1.0f),
                        ImVec2(pos.x + size.x - m.mPad.x, pos.y + titleH - 1.0f), sep, 1.0f);
        }
        // With a title the card frame is already drawn above → body adds no
        // border (avoids the duplicate top line); untitled menus frame as usual.
        int hov = -1;
        int clicked = MenuBody(dl, rowsPos, rowsBodySize, rows,
                               ImDrawFlags_RoundCornersAll, -1, &hov, !hasTitle);

        // A leaf (non-submenu) row clicked → run its action and close.
        if (clicked >= 0 && clicked < (int)entries.size() &&
            entries[(size_t)clicked].submenu.empty()) {
            if (entries[(size_t)clicked].onClick) entries[(size_t)clicked].onClick();
            ImGui::CloseCurrentPopup();
            ss = SubState{};
        }

        // Geometry of the currently-active submenu (if any), so we can tell when
        // the cursor is inside it.
        auto submenuRect = [&](int parentRow, ImVec2& outPos, ImVec2& outSize) {
            const auto& subEntries = entries[(size_t)parentRow].submenu;
            std::vector<MenuRow> subRows = EntriesToRows(subEntries);
            outSize = MeasureMenu(subRows);
            float rowY = rowsPos.y + m.mPad.y + (float)parentRow * (m.rowH + m.itemGap);
            outPos = ClampToViewport(ImVec2(pos.x + size.x - 2.0f, rowY), outSize);
        };
        bool overActiveSub = false;
        if (ss.active >= 0 && ss.active < (int)entries.size() &&
            !entries[(size_t)ss.active].submenu.empty()) {
            ImVec2 sp, sz; submenuRect(ss.active, sp, sz);
            overActiveSub = io.MousePos.x >= sp.x && io.MousePos.x <= sp.x + sz.x &&
                            io.MousePos.y >= sp.y && io.MousePos.y <= sp.y + sz.y;
        }

        // Decide the active submenu (Blender-accurate):
        //  • Inside the open submenu, or still on its parent row → keep it.
        //  • On ANY OTHER row (submenu parent, leaf, or even disabled) → the
        //    current submenu closes INSTANTLY (no delay); a new submenu parent
        //    only OPENS after a short dwell (anti-flicker on fast passes).
        //  • In a gap (hov == -1) and not over the submenu → close instantly.
        bool hovIsSubmenuParent = (hov >= 0 && !entries[(size_t)hov].submenu.empty());
        bool onActiveParent     = (hov >= 0 && hov == ss.active);
        if (overActiveSub || onActiveParent) {
            ss.pendingRow = -1; ss.dwell = 0.0f;          // keep current
        } else {
            // Anything else immediately drops the open submenu.
            ss.active = -1;
            if (hovIsSubmenuParent) {
                // Dwell before opening the newly-hovered submenu.
                if (ss.pendingRow != hov) { ss.pendingRow = hov; ss.dwell = 0.0f; }
                ss.dwell += io.DeltaTime;
                if (ss.dwell >= kSubmenuDelay) { ss.active = hov; ss.pendingRow = -1; }
            } else {
                ss.pendingRow = -1; ss.dwell = 0.0f;
            }
        }

        // Draw the active submenu (if any) on the foreground; track which of its
        // rows is hovered so its tooltip can be shown too. The hovered row is
        // identified by a STABLE key (row indices), NOT by the std::string's
        // address — `entries` is rebuilt every frame so addresses change each
        // frame, which reset the dwell timer and made the tooltip flicker.
        std::string tipText;            // COPY of the hovered row's tooltip
        int tipKey = -1;                // stable id: mainRow, or 1000+subRow
        if (hov >= 0 && hov < (int)entries.size() &&
            !entries[(size_t)hov].tooltip.empty()) {
            tipText = entries[(size_t)hov].tooltip;
            tipKey  = hov;
        }
        if (ss.active >= 0 && ss.active < (int)entries.size() &&
            !entries[(size_t)ss.active].submenu.empty()) {
            const auto& subEntries = entries[(size_t)ss.active].submenu;
            std::vector<MenuRow> subRows = EntriesToRows(subEntries);
            ImVec2 subPos, subSize; submenuRect(ss.active, subPos, subSize);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            int subHov = -1;
            // Submenu is on the foreground draw list (outside the popup window):
            // a press there closes the parent popup, so commit on PRESS here.
            int subClicked = MenuBody(fg, subPos, subSize, subRows,
                                      ImDrawFlags_RoundCornersAll, -1, &subHov,
                                      /*drawBorder=*/true, /*onRelease=*/false);
            if (subHov >= 0 && subHov < (int)subEntries.size() &&
                !subEntries[(size_t)subHov].tooltip.empty()) {
                tipText = subEntries[(size_t)subHov].tooltip;  // submenu wins
                tipKey  = 1000 + subHov;
            }
            if (subClicked >= 0 && subClicked < (int)subEntries.size()) {
                if (subEntries[(size_t)subClicked].onClick)
                    subEntries[(size_t)subClicked].onClick();
                ImGui::CloseCurrentPopup();
                ss = SubState{};
            }
        }

        // Description tooltip on hover-dwell, drawn LAST (after the submenu) so
        // the shared topmost tooltip sits above everything. Dwell keyed on the
        // STABLE row id (per popup), so it doesn't reset every frame.
        ss.tipDwell = (tipKey == ss.tipKey) ? ss.tipDwell + io.DeltaTime : 0.0f;
        ss.tipKey   = tipKey;
        if (tipKey >= 0) {
            float delay = Flt(Tok::S_Config_HoverDelayNormal);
            if (delay <= 0.0f) delay = 0.4f;
            if (ss.tipDwell >= delay) DrawTooltip(tipText.c_str(), io.MousePos);
        }
        ImGui::EndPopup();
    } else {
        open = false;
        ss = SubState{};   // reset when the menu closes
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return open;
}

// Shared tooltip body. `bgAlpha` scales the background + border alpha (1.0 =
// the opaque default; <1 = the translucent variant).
static void DrawTooltipImpl(const char* text, ImVec2 anchor, float bgAlpha) {
    if (!text || !*text) return;
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    ImVec2 pad    = V2(Tok::C_Tooltip_Padding); pad.x *= gs; pad.y *= gs;
    float  rnd    = Flt(Tok::C_Tooltip_CornerRadius) * gs;
    float  bw     = ds.BordersEnabled() ? Flt(Tok::C_Tooltip_BorderWidth) * gs : 0.0f;
    ImVec4 bgC    = Col(Tok::C_Tooltip_Background); bgC.w *= bgAlpha;
    ImVec4 bdC    = Col(Tok::C_Tooltip_Border);     bdC.w *= bgAlpha;
    ImU32  bg     = ImGui::ColorConvertFloat4ToU32(bgC);
    ImU32  bd     = ImGui::ColorConvertFloat4ToU32(bdC);
    ImU32  txt    = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Tooltip_Text));

    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 box(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    // Offset below-right of the anchor, then clamp to the viewport so it never
    // clips off-screen.
    ImVec2 p(anchor.x + 14.0f * gs, anchor.y + 18.0f * gs);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float wR = vp->WorkPos.x + vp->WorkSize.x, wB = vp->WorkPos.y + vp->WorkSize.y;
    if (p.x + box.x > wR) p.x = std::max(vp->WorkPos.x, wR - box.x);
    if (p.y + box.y > wB) p.y = std::max(vp->WorkPos.y, anchor.y - box.y - 4.0f * gs);

    // FOREGROUND draw list → above sub-menus and everything else.
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p, ImVec2(p.x + box.x, p.y + box.y), bg, rnd);
    if (bw > 0.01f)
        fg->AddRect(p, ImVec2(p.x + box.x, p.y + box.y), bd, rnd, 0, bw);
    fg->AddText(ImVec2(p.x + pad.x, p.y + pad.y), txt, text);
}

void DrawTooltip(const char* text, ImVec2 anchor) {
    DrawTooltipImpl(text, anchor, 1.0f);
}
void DrawTooltipTranslucent(const char* text, ImVec2 anchor, float bgAlpha) {
    DrawTooltipImpl(text, anchor, bgAlpha);
}

} // namespace UI

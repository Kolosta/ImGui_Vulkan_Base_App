#include <UI/Settings/SettingsWindow.h>
#include <UI/Settings/TokenPropertyRow.h>
#include <UI/Chrome/BorderlessWindow.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Shortcuts/ShortcutCaptureField.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/Action.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// One entry in the left navigation column.
struct NavItem {
    SettingsWindow::Page page;
    const char*          label;
    const char*          icon;   // icon id (may be "")
    int                  group;  // visual group index (gaps between groups)
};

// Groups: [General, Language] · [Theme, Customisation, Accessibility]
//         · [Inputs, Keymap, Navigation] · [Icons] · [Dev]
const std::array<NavItem, 10> kNav = {{
    { SettingsWindow::Page::General,       "General",       "settings", 0 },
    { SettingsWindow::Page::Language,      "Language",      "",         0 },
    { SettingsWindow::Page::Theme,         "Theme",         "",         1 },
    { SettingsWindow::Page::Customisation, "Customisation", "",         1 },
    { SettingsWindow::Page::Accessibility, "Accessibility", "",         1 },
    { SettingsWindow::Page::Inputs,        "Inputs",        "",         2 },
    { SettingsWindow::Page::Keymap,        "Keymap",        "",         2 },
    { SettingsWindow::Page::Navigation,    "Navigation",    "",         2 },
    { SettingsWindow::Page::Icons,         "Icons",         "",         3 },
    { SettingsWindow::Page::Dev,           "Dev",           "",         4 },
}};

const char* PageTitle(SettingsWindow::Page p) {
    for (const NavItem& n : kNav) if (n.page == p) return n.label;
    return "";
}

// ── Theme page ───────────────────────────────────────────────────────────────
struct ThemeCardInfo { DS::ThemeType theme; const char* name; };
const std::array<ThemeCardInfo, 4> kThemes = {{
    { DS::ThemeType::Dark,         "Dark" },
    { DS::ThemeType::Light,        "Light" },
    { DS::ThemeType::MutedGreen,   "Muted Green" },
    { DS::ThemeType::HighContrast, "High Contrast" },
}};

// Resolve a colour token AS IF the given theme were active (forced context),
// keeping the current accessibility mode so the preview matches what the user
// would actually see after switching.
ImVec4 ColIn(Tok t, DS::ThemeType theme) {
    auto& ds = DS::DesignSystem::Instance();
    DS::Context ctx(theme, ds.GetCurrentContext().GetAccessibility());
    return ds.GetColorValue(DS::TokIdStr(t), ctx);
}
float FltIn(Tok t, DS::ThemeType theme) {
    auto& ds = DS::DesignSystem::Instance();
    DS::Context ctx(theme, ds.GetCurrentContext().GetAccessibility());
    return ds.GetFloatValue(DS::TokIdStr(t), ctx);
}

// Declarative customisation map types. One property = one TokenPropertyRow;
// adding/reordering categories is a pure data edit (see CustoMap below). Shared
// by the Customisation page AND the Inputs page (which reuses CustoProp).
struct CustoProp    { Tok token; const char* label; };
struct CustoSection { const char* name; std::vector<CustoProp> props; };
struct CustoArea    { const char* name; const char* icon; std::vector<CustoSection> sections; };

} // namespace

void SettingsWindow::Render(bool* open) {
    if (!open || !*open) return;

    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    // Fill the host viewport: in the dedicated Preferences context the main
    // viewport IS the OS Preferences window, so the UI fills it edge-to-edge.
    // Native move/resize are handled by the SDL hit-test on that OS window, so
    // this ImGui window is NoMove/NoResize.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    // The whole window background is the editor surface colour (one flat colour
    // across every page, as requested).
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Col(Tok::S_Surface_Canvas));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Square corners: this window fills the whole OS viewport, so any rounding
    // (inherited from the main style's WindowRounding) would expose the Vulkan
    // clear colour behind the rounded corners — visible at the bottom edges.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    // No native title bar — we draw our own (logo + "Preferences").
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

    bool stayOpen = true;
    sysClose_ = false;
    if (ImGui::Begin("Preferences", &stayOpen, kFlags)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float controlH = Flt(Tok::S_Size_ControlHeight) * gs;
        const float barH = controlH + 8.0f * gs;

        RenderTitleBar(avail.x);

        // ONE uniform 7px gap everywhere: left of the nav column, between the
        // two columns, and right of the page. The page's RIGHT gap is provided
        // by the overlay scrollbar's reserved gutter (same 7px token), so the
        // page child runs to the right edge here (only g×2 of gaps reserved) and
        // the scrollbar sits centred in that single right-hand gap — no doubling.
        // Both regions are transparent (NoBackground) so the flat window
        // background (= editor surface) shows through.
        const float g     = Flt(Tok::C_Scrollbar_OverlayMargin) * gs;  // 7px, shared with the scrollbar gutter
        const float bodyH = avail.y - barH - g;   // bottom inset matches the side gaps
        const float colW  = 172.0f * gs;
        const float pageW = avail.x - colW - g * 2.0f;

        // Left column: x = g.
        ImGui::SetCursorPos(ImVec2(g, barH));
        ImGui::BeginChild("##settingsNav", ImVec2(colW, bodyH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        RenderLeftColumn(colW, bodyH);
        ImGui::EndChild();

        // Page content: x = g + colW + g (one gap between the columns). This is
        // a fixed CONTAINER — the actual scroll happens in the per-page child
        // (##pageScroll / ##custoScroll / ##keymapScroll), which carries the
        // overlay scrollbar — so suppress any native bar here.
        ImGui::SetCursorPos(ImVec2(g + colW + g, barH));
        ImGui::BeginChild("##settingsPage", ImVec2(pageW, bodyH), false,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        RenderPage(pageW, bodyH);
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    if (!stayOpen || sysClose_) *open = false;
}

void SettingsWindow::RenderTitleBar(float width) {
    auto& ds = DS::DesignSystem::Instance();
    auto& im = VectorGraphics::IconManager::Instance();
    const float gs = ds.GetGlobalScale();
    const float controlH = Flt(Tok::S_Size_ControlHeight) * gs;
    const float pad = 8.0f * gs;
    const float barH = controlH + pad;

    // Per-window title-bar colours (independent of the main title bar).
    const ImVec4 barBgV  = Col(Tok::C_PrefBar_Background);
    const ImVec4 textV   = Col(Tok::C_PrefBar_Text);
    const ImVec4 iconV   = Col(Tok::C_PrefBar_Icon);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImVec2 mx(mn.x + width, mn.y + barH);
    dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(barBgV));

    // Logo + "Preferences" on the left. The logo is tinted to the bar's icon
    // colour (white by default, like the main bar's icons).
    const float iconSz = Flt(Tok::C_Dropdown_IconSize) * gs;
    float x = mn.x + pad;
    {
        ImVec2 ip(x, mn.y + (barH - iconSz) * 0.5f);
        auto md = im.GetDefaultMetadata("logo_carto");
        for (auto& z : md.colorZones) z.customColor = iconV;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, "logo_carto", ip, iconSz, md);
        x += iconSz + pad;
    }
    {
        ImVec2 ts = ImGui::CalcTextSize("Preferences");
        dl->AddText(ImVec2(x, mn.y + (barH - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(textV), "Preferences");
    }

    // Reserve the bar height in layout WITHOUT capturing input: a Dummy grows
    // the parent bounds but stays transparent to clicks, so dragging the empty
    // bar area is handled by ImGui's own window-move (which moves the OS window
    // in multi-viewport mode).
    ImGui::SetCursorScreenPos(mn);
    ImGui::Dummy(ImVec2(width, barH));

    // ── System buttons (minimize / maximize-restore / close) on the right ────
    // The Preferences UI runs in its OWN ImGui context whose main viewport IS
    // the OS Preferences window. The SDL3 backend stores the SDL_WindowID (an
    // integer), NOT an SDL_Window*, in PlatformHandle — so resolve it back to
    // the window with SDL_GetWindowFromID (casting the id straight to a pointer
    // gave a bogus handle, which is why min/max did nothing).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    SDL_Window* sdlWin = vp
        ? SDL_GetWindowFromID((SDL_WindowID)(intptr_t)vp->PlatformHandle)
        : nullptr;
    // The borderless behaviour (maximize/restore/fullscreen) is owned by the
    // window's controller; the bar only chooses glyphs + forwards button hits.
    BorderlessWindowController* chrome =
        BorderlessWindowController::FromWindow(sdlWin);

    const float btnW = barH;     // square slots
    const ImU32 glyph = ImGui::ColorConvertFloat4ToU32(iconV);
    const ImU32 hovBg = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_PrefBar_ButtonHover));
    const ImU32 closeBg = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_PrefBar_CloseHover));

    // Show the restore glyph (two stacked squares) while maximized, like the
    // main title bar.
    const bool maximized = chrome && chrome->IsMaximized();

    struct SysBtn { const char* id; int kind; };  // 0 min, 1 max, 2 close
    const SysBtn order[3] = { {"##prefMin",0}, {"##prefMax",1}, {"##prefClose",2} };
    for (int i = 0; i < 3; ++i) {
        float bx = mx.x - btnW * (3 - i);
        ImGui::SetCursorScreenPos(ImVec2(bx, mn.y));
        ImGui::InvisibleButton(order[i].id, ImVec2(btnW, barH));
        bool hov = ImGui::IsItemHovered();
        // System buttons fire on RELEASE over the button, not on press — native
        // window-button behaviour and matching the main title bar. A press that
        // drags off before release is cancelled.
        bool clk = ImGui::IsItemDeactivated() && hov;
        ImVec2 bmn = ImGui::GetItemRectMin(), bmx = ImGui::GetItemRectMax();
        if (hov)
            dl->AddRectFilled(bmn, bmx, order[i].kind == 2 ? closeBg : hovBg);
        ImVec2 c((bmn.x + bmx.x) * 0.5f, (bmn.y + bmx.y) * 0.5f);
        const float s = 5.0f * gs;
        const float t = std::max(1.0f, std::floor(gs));
        if (order[i].kind == 0) {                 // minimize
            dl->AddRectFilled(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y + t), glyph);
        } else if (order[i].kind == 1) {          // maximize / restore
            if (maximized) {
                const float o = 2.0f * gs;        // back-square offset
                // Front square (lower-left) + back square top/right edges.
                dl->AddRect(ImVec2(c.x - s, c.y - s + o),
                            ImVec2(c.x + s - o, c.y + s), glyph, 0, 0, t);
                dl->AddLine(ImVec2(c.x - s + o, c.y - s),
                            ImVec2(c.x + s, c.y - s), glyph, t);
                dl->AddLine(ImVec2(c.x + s, c.y - s),
                            ImVec2(c.x + s, c.y + s - o), glyph, t);
            } else {
                dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
                            glyph, 0, 0, t);
            }
        } else {                                  // close (X)
            // Snap the four ends to pixel centres so both diagonals are the
            // same length (without it the un-snapped ends get eaten unevenly by
            // AA and the right branches look shorter) — same as the main bar.
            auto snap = [](float v){ return std::floor(v) + 0.5f; };
            float fl = snap(c.x - s), fr = snap(c.x + s);
            float ft = snap(c.y - s), fb = snap(c.y + s);
            dl->AddLine(ImVec2(fl, ft), ImVec2(fr, fb), glyph, t);
            dl->AddLine(ImVec2(fl, fb), ImVec2(fr, ft), glyph, t);
        }
        if (clk) {
            if (order[i].kind == 0) {             // minimize
                if (chrome) chrome->Minimize();
            } else if (order[i].kind == 1) {      // maximize / restore
                if (chrome) chrome->ToggleMaximizeOrFullscreen();
            } else {                              // close
                sysClose_ = true;
            }
        }
    }
}

void SettingsWindow::RenderLeftColumn(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float rowH = Flt(Tok::S_Size_ControlHeight) * gs + 8.0f * gs;
    const float pad  = 8.0f * gs;
    (void)height;

    // No horizontal indent here: the uniform outer gap is applied by the parent
    // (the nav child is already positioned at `g`). Buttons span the full column
    // width so both columns line up with the same single gap on each side.
    ImGui::Dummy(ImVec2(0.0f, pad));   // top padding (a real item)

    // Render one ButtonGroup per visual group, stacked vertically with a gap
    // between groups. Each group is a single column; one button per row.
    for (int g = 0; g <= 4; ++g) {
        std::vector<const NavItem*> items;
        for (const NavItem& n : kNav) if (n.group == g) items.push_back(&n);
        if (items.empty()) continue;

        char gid[24];
        std::snprintf(gid, sizeof(gid), "##navGroup%d", g);
        ButtonGroup bg(gid);
        std::vector<float> rows((size_t)items.size(), rowH);
        bg.SetGrid({ width }, rows);
        for (int i = 0; i < (int)items.size(); ++i) {
            ButtonGroup::Cell c{};
            c.label    = items[(size_t)i]->label;
            c.icon     = items[(size_t)i]->icon ? items[(size_t)i]->icon : "";
            c.col = 0;  c.row = i;
            c.selected = (items[(size_t)i]->page == page_);
            c.align    = ButtonGroup::Align::Left;
            bg.AddCell(c);
        }
        ButtonGroup::Result r = bg.Render();
        if (r.clickedIndex >= 0 && r.clickedIndex < (int)items.size())
            page_ = items[(size_t)r.clickedIndex]->page;

        ImGui::Dummy(ImVec2(0.0f, pad));   // gap between groups (a real item)
    }
}

void SettingsWindow::RenderPage(float width, float height) {
    switch (page_) {
        case Page::Theme:         RenderThemePage(width, height); return;
        case Page::Customisation: RenderCustomisationPage(width, height); return;
        case Page::Accessibility: RenderAccessibilityPage(width, height); return;
        case Page::Inputs:        RenderInputsPage(width, height); return;
        case Page::Keymap:        RenderKeymapPage(width, height); return;
        case Page::General:       RenderGeneralPage(width, height); return;
        case Page::Language:      RenderLanguagePage(width, height); return;
        case Page::Navigation:    RenderNavigationPage(width, height); return;
        case Page::Icons:         RenderIconsPage(width, height); return;
        case Page::Dev:           RenderDevPage(width, height); return;
    }
}

// ── Shared page scaffold ─────────────────────────────────────────────────────
// Every page opens with a left-inset title (+ optional caption) and then a
// transparent, full-width scroll region. Factoring this guarantees no page
// diverges in title position or spacing (a constraint the user called out).
void SettingsWindow::BeginPageBody(const char* title, const char* caption) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float pad = 16.0f * gs;

    // The page child is already inset by the uniform outer gap, so content sits
    // at x = 0 here (only a top inset for the title).
    ImGui::SetCursorPos(ImVec2(0.0f, pad * 0.5f));
    ImGui::TextUnformatted(title);
    if (caption && *caption) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", caption);
    }
    ImGui::Spacing();

    // Transparent, full-width scroll region (no rounded ChildBg at the corners),
    // with the Blender-style overlay scrollbar (zero reserved space).
    UI::BeginScroll("##pageScroll", ImVec2(0.0f, 0.0f), 0,
                    ImGuiWindowFlags_NoBackground);
}

void SettingsWindow::EndPageBody() { UI::EndScroll(); }

// ── Accessibility page: one card per colour-vision-deficiency mode ───────────
// Each card previews a small swatch row resolved IN that accessibility mode
// (forced context, current theme), so the user sees what the simulation does
// before applying it. Clicking a card switches the live accessibility mode.
void SettingsWindow::RenderAccessibilityPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    (void)height;

    const DS::ThemeType   theme  = ds.GetCurrentContext().GetTheme();
    const DS::AccessibilityType active = ds.GetCurrentContext().GetAccessibility();

    BeginPageBody("Accessibility",
                  "Simulate colour-vision deficiencies. The whole UI is "
                  "recoloured live; each card previews its mode.");

    struct CvdInfo { DS::AccessibilityType mode; const char* name; const char* desc; };
    const std::array<CvdInfo, 4> kModes = {{
        { DS::AccessibilityType::None,        "None",         "No transformation (default)." },
        { DS::AccessibilityType::Protanopia,  "Protanopia",   "Red-blind simulation." },
        { DS::AccessibilityType::Deuteranopia,"Deuteranopia", "Green-blind simulation." },
        { DS::AccessibilityType::Tritanopia,  "Tritanopia",   "Blue-blind simulation." },
    }};

    // A representative set of tokens to preview the palette under each mode.
    const Tok kSwatches[] = {
        Tok::S_Color_Accent_Default, Tok::S_Color_Positive_Default,
        Tok::S_Color_Negative_Default, Tok::S_Color_Notice_Default,
        Tok::S_Color_Text_Link,
    };

    const float cardW = 200.0f * gs;
    const float cardH = 92.0f * gs;
    const float gap   = 14.0f * gs;
    const float nameH = ImGui::GetTextLineHeightWithSpacing();
    const float rad   = Flt(Tok::S_CornerRadius_Default) * gs;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float startX = 0.0f;
    const float maxX   = ImGui::GetContentRegionAvail().x;
    float x = startX, y = ImGui::GetCursorPosY();

    for (const CvdInfo& m : kModes) {
        if (x > startX && x + cardW > maxX) { x = startX; y += cardH + nameH + gap; }
        ImGui::SetCursorPos(ImVec2(x, y));
        ImGui::PushID(m.name);
        bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, cardH + nameH));
        bool hovered = ImGui::IsItemHovered();
        bool down    = ImGui::IsItemActive();
        const bool selected = (m.mode == active);

        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 cardMax(mn.x + cardW, mn.y + cardH);

        ImVec4 fill = Col(Tok::S_Surface_Raised);
        if (down)         fill = Col(Tok::S_Background_App_Frame);
        else if (hovered) fill = Col(Tok::S_Background_App_Child);
        dl->AddRectFilled(mn, cardMax, ImGui::ColorConvertFloat4ToU32(fill), rad);

        // Swatch row resolved in this CVD mode (forced context).
        DS::Context cardCtx(theme, m.mode);
        const float sw = 22.0f * gs;
        const float pad = 10.0f * gs;
        float sx = mn.x + pad;
        const float sy = mn.y + pad;
        for (Tok t : kSwatches) {
            ImVec4 c = ds.GetColorValue(DS::TokIdStr(t), cardCtx);
            dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sw),
                              ImGui::ColorConvertFloat4ToU32(c), 3.0f * gs);
            sx += sw + 6.0f * gs;
        }
        // Description under the swatches.
        dl->AddText(ImVec2(mn.x + pad, sy + sw + 8.0f * gs),
                    ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Subtle)),
                    m.desc);

        // Selection / hover outline.
        if (selected)
            dl->AddRect(mn, cardMax, ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Accent_Default)),
                        rad, 0, 2.0f * gs);
        else if (hovered || down)
            dl->AddRect(mn, cardMax, ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Border_Strong)),
                        rad, 0, 1.5f * gs);

        // Mode name centred below.
        ImVec2 ts = ImGui::CalcTextSize(m.name);
        dl->AddText(ImVec2(mn.x + (cardW - ts.x) * 0.5f, cardMax.y + 4.0f * gs),
                    ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Default)), m.name);
        ImGui::PopID();

        if (clicked) {
            DS::Context next(theme, m.mode);
            ds.SetContext(next);
        }
        x += cardW + gap;
    }

    EndPageBody();
}

// ── Inputs page: interaction thresholds, all token-backed ────────────────────
void SettingsWindow::RenderInputsPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    (void)width; (void)height;
    DS::Context ctx = ds.GetCurrentContext();

    BeginPageBody("Inputs",
                  "Pointer and timing thresholds used across the application.");

    // Reuse the same panel + property-row treatment as Customisation so the
    // page looks identical. Edits go global (these are app-wide, not theming).
    static const std::vector<std::pair<const char*, std::vector<CustoProp>>> kGroups = {
        { "Pointer", {
            { Tok::S_Config_DragThreshold,       "Drag start distance" },
            { Tok::S_Config_TouchExtraPadding,   "Touch extra padding" },
            { Tok::S_Config_MouseCursorScale,    "Cursor scale" },
        }},
        { "Hover timing", {
            { Tok::S_Config_HoverDelayShort,     "Tooltip delay (short)" },
            { Tok::S_Config_HoverDelayNormal,    "Tooltip delay (normal)" },
            { Tok::S_Config_HoverDelayStationary,"Tooltip delay (stationary)" },
        }},
        { "Sliders & grab", {
            { Tok::S_Config_GrabMinSize,         "Minimum grab size" },
            { Tok::S_Config_LogSliderDeadzone,   "Log-slider dead zone" },
        }},
        { "Object placement", {
            // When on, new objects (Shift+A) follow the cursor as a preview and
            // are placed on click, instead of appearing at the 2D cursor. The IOF
            // module forces this mode regardless of the preference.
            { Tok::S_Config_PreviewPlacement,    "Preview placement (click to place)" },
        }},
    };

    int g = 0;
    for (const auto& grp : kGroups) {
        char gid[32]; std::snprintf(gid, sizeof(gid), "##inputs%d", g++);
        UI::PanelConfig pc; pc.id = gid; pc.label = grp.first; pc.defaultOpen = true;
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open)
            for (const CustoProp& p : grp.second)
                UI::TokenPropertyRow(gid, p.label, p.token, ctx, /*editGlobal=*/true);
        UI::EndPanel();
    }

    EndPageBody();
}

// ── General / Language / Navigation: layout-only stubs (per the brief) ───────
void SettingsWindow::RenderGeneralPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    (void)width; (void)height;
    DS::Context ctx = ds.GetCurrentContext();

    BeginPageBody("General");

    // Layout scaffold only — no behaviour wired yet (font family lands here
    // later, per the brief). Panels are laid out so the structure is visible.
    { UI::PanelConfig pc; pc.id = "##genAppearance"; pc.label = "Appearance";
      pc.defaultOpen = true;
      UI::PanelResult pr = UI::BeginPanel(pc);
      if (pr.open) {
          // Global font scale is genuinely useful and token-backed, so wire it.
          UI::TokenPropertyRow("##genAppearance", "Interface font size",
                               Tok::S_FontSize_Default, ctx, true);
          UI::TokenPropertyRow("##genAppearance", "Interface scale",
                               Tok::S_Scale_Default, ctx, true);
      }
      UI::EndPanel(); }
    { UI::PanelConfig pc; pc.id = "##genFonts"; pc.label = "Fonts";
      pc.defaultOpen = true;
      UI::PanelResult pr = UI::BeginPanel(pc);
      if (pr.open) {
          // Pick the font family per role (body / heading / mono) from the
          // families discovered in the project. The body family drives the
          // default UI font; ApplyFontTokens() re-reads these every frame so a
          // change takes effect live (same behaviour as the old DS editor).
          UI::TokenPropertyRow("##genFonts", "Interface font (body)",
                               Tok::S_FontFamily_Body, ctx, true);
          UI::TokenPropertyRow("##genFonts", "Heading font",
                               Tok::S_FontFamily_Heading, ctx, true);
          UI::TokenPropertyRow("##genFonts", "Monospace font",
                               Tok::S_FontFamily_Mono, ctx, true);
      }
      UI::EndPanel(); }
    { UI::PanelConfig pc; pc.id = "##genEditing"; pc.label = "Editing";
      pc.defaultOpen = true;
      UI::PanelResult pr = UI::BeginPanel(pc);
      if (pr.open) {
          // Undo history depth (steps kept per window). Read live by the app to
          // resize its undo stacks.
          UI::TokenPropertyRow("##genEditing", "Undo steps",
                               Tok::S_Config_UndoSteps, ctx, true);
      }
      UI::EndPanel(); }

    EndPageBody();
}

void SettingsWindow::RenderLanguagePage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    (void)width; (void)height;

    BeginPageBody("Language");

    UI::PanelConfig pc; pc.id = "##lang"; pc.label = "Display language";
    pc.defaultOpen = true;
    UI::PanelResult pr = UI::BeginPanel(pc);
    if (pr.open) {
        const float lpad = 8.0f * gs;
        ImGui::Indent(lpad);
        ImGui::TextDisabled("Language selection — not implemented yet.");
        ImGui::Unindent(lpad);
    }
    UI::EndPanel();

    EndPageBody();
}

void SettingsWindow::RenderNavigationPage(float width, float height) {
    (void)width; (void)height;
    BeginPageBody("Navigation", "(this page is not implemented yet)");
    EndPageBody();
}

// ── Icons page: pointer to the standalone icon editor ────────────────────────
// The full icon editor lives in the classic Design System window (it uploads
// Vulkan textures into the MAIN context's backend, which the Preferences
// context can't sample — a known deferred limitation). Here we just point the
// user there rather than render a broken duplicate.
void SettingsWindow::RenderIconsPage(float width, float height) {
    const float gs = DS::DesignSystem::Instance().GetGlobalScale();
    (void)width; (void)height;
    BeginPageBody("Icons");
    const float ipad = 8.0f * gs;
    ImGui::Indent(ipad);
    ImGui::TextWrapped("The icon set editor is available in the Design System "
                       "window (Windows menu > Design System > Icons tab).");
    ImGui::Unindent(ipad);
    EndPageBody();
}

// ── Dev page: developer/debug toggles (token-backed, persistent app-wide) ────
// Currently a single toggle for the colour-coded editor-corner hit-zone previews.
// The drawing code is kept; this only gates whether it shows.
void SettingsWindow::RenderDevPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    (void)width; (void)height;
    DS::Context ctx = ds.GetCurrentContext();

    BeginPageBody("Dev",
                  "Developer / debug toggles. Persisted app-wide.");

    UI::PanelConfig pc; pc.id = "##devOverlays"; pc.label = "Overlays";
    pc.defaultOpen = true;
    UI::PanelResult pr = UI::BeginPanel(pc);
    if (pr.open) {
        // A 0/1 token → TokenPropertyRow renders it as a checkbox. Edits go global
        // (a dev flag, not theme-scoped).
        UI::TokenPropertyRow("##devOverlays", "Show editor corner zones",
                             Tok::S_Config_ShowCornerZones, ctx, /*editGlobal=*/true);
    }
    UI::EndPanel();

    // App-injected dev tools (e.g. the render-engine selector) — needs Application
    // state, so the host provides it via SetDevPageExtra.
    if (devPageExtra_) devPageExtra_();

    EndPageBody();
}

// ── Theme page: one preview card per theme + a trailing "add" card ───────────
void SettingsWindow::RenderThemePage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float pad = 16.0f * gs;
    (void)height;

    const DS::ThemeType active = ds.GetCurrentContext().GetTheme();

    // Card geometry.
    const float cardW = 168.0f * gs;
    const float cardH = 132.0f * gs;
    const float gap   = 14.0f * gs;
    const float nameH = ImGui::GetTextLineHeightWithSpacing();

    ImGui::SetCursorPos(ImVec2(pad, pad));
    ImGui::TextUnformatted("Theme");
    ImGui::Spacing();

    // Flow the cards left→right, wrapping to the available width.
    const float startX = pad;
    float x = startX, y = ImGui::GetCursorPosY();
    const float maxX = width - pad;

    auto nextSlot = [&](float w) {
        if (x > startX && x + w > maxX) { x = startX; y += cardH + nameH + gap; }
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();

    // Draw a single theme preview card. Returns true if clicked.
    auto drawCard = [&](DS::ThemeType th, const char* name, bool selected) -> bool {
        nextSlot(cardW);
        ImGui::SetCursorPos(ImVec2(x, y));
        ImGui::PushID(name);
        bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, cardH + nameH));
        bool hovered = ImGui::IsItemHovered();
        bool activeBtn = ImGui::IsItemActive();

        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 cardMax(mn.x + cardW, mn.y + cardH);

        // Colours resolved in the card's OWN theme (forced context).
        ImVec4 cBase   = ColIn(Tok::S_Color_Background_Default, th);
        ImVec4 cSurf   = ColIn(Tok::S_Surface_Canvas, th);
        ImVec4 cText   = ColIn(Tok::S_Color_Text_Default, th);
        ImVec4 cBorder = ColIn(Tok::S_Color_Border_Default, th);
        ImVec4 cAccent = ColIn(Tok::S_Color_Accent_Default, th);
        float  rad     = FltIn(Tok::S_CornerRadius_Default, th) * gs;

        // Card body (base) + an inset "window" surface.
        dl->AddRectFilled(mn, cardMax, ImGui::ColorConvertFloat4ToU32(cBase), rad);
        float ins = 10.0f * gs;
        ImVec2 sMn(mn.x + ins, mn.y + ins);
        ImVec2 sMx(cardMax.x - ins, cardMax.y - ins);
        dl->AddRectFilled(sMn, sMx, ImGui::ColorConvertFloat4ToU32(cSurf), rad);
        dl->AddRect(sMn, sMx, ImGui::ColorConvertFloat4ToU32(cBorder), rad, 0, 1.0f * gs);

        // A couple of text bars + an accent "button".
        float tx = sMn.x + 8.0f * gs, ty = sMn.y + 8.0f * gs;
        ImU32 txtCol = ImGui::ColorConvertFloat4ToU32(cText);
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(sMx.x - 30.0f * gs, ty + 6.0f * gs), txtCol, 2.0f * gs);
        dl->AddRectFilled(ImVec2(tx, ty + 14.0f * gs), ImVec2(sMx.x - 60.0f * gs, ty + 20.0f * gs),
                          ImGui::ColorConvertFloat4ToU32(cText), 2.0f * gs);
        // Accent button.
        ImVec2 bMn(tx, sMx.y - 26.0f * gs);
        ImVec2 bMx(tx + 56.0f * gs, sMx.y - 8.0f * gs);
        dl->AddRectFilled(bMn, bMx, ImGui::ColorConvertFloat4ToU32(cAccent), rad);

        // Selection / hover outline (uses border colour, never accent on hover).
        if (selected) {
            dl->AddRect(mn, cardMax, ImGui::ColorConvertFloat4ToU32(cAccent),
                        rad, 0, 2.0f * gs);
        } else if (hovered || activeBtn) {
            ImVec4 hl = ColIn(Tok::S_Color_Border_Strong, th);
            dl->AddRect(mn, cardMax, ImGui::ColorConvertFloat4ToU32(hl),
                        rad, 0, 1.5f * gs);
        }

        // Theme name centred below the card.
        ImVec2 ts = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(mn.x + (cardW - ts.x) * 0.5f, cardMax.y + 4.0f * gs),
                    ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Default)),
                    name);
        ImGui::PopID();
        (void)winPos;
        x += cardW + gap;
        return clicked;
    };

    for (const ThemeCardInfo& tc : kThemes) {
        if (drawCard(tc.theme, tc.name, tc.theme == active)) {
            DS::Context ctx(tc.theme, ds.GetCurrentContext().GetAccessibility());
            ds.SetContext(ctx);
        }
    }

    // Trailing "+" card (add custom theme — not implemented yet).
    {
        nextSlot(cardW);
        ImGui::SetCursorPos(ImVec2(x, y));
        ImGui::PushID("##addTheme");
        bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, cardH + nameH));
        bool hovered = ImGui::IsItemHovered();
        bool activeBtn = ImGui::IsItemActive();
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 cardMax(mn.x + cardW, mn.y + cardH);
        float rad = Flt(Tok::S_CornerRadius_Default) * gs;

        ImVec4 fill = Col(Tok::S_Surface_Canvas);
        if (activeBtn)      fill = Col(Tok::S_Background_App_Frame);
        else if (hovered)   fill = Col(Tok::S_Background_App_Child);
        dl->AddRectFilled(mn, cardMax, ImGui::ColorConvertFloat4ToU32(fill), rad);
        ImU32 bd = ImGui::ColorConvertFloat4ToU32(
            hovered ? Col(Tok::S_Color_Border_Strong) : Col(Tok::S_Color_Border_Default));
        dl->AddRect(mn, cardMax, bd, rad, 0, 1.0f * gs);

        // "+" glyph centred.
        ImVec2 c((mn.x + cardMax.x) * 0.5f, (mn.y + cardMax.y) * 0.5f);
        float s = 16.0f * gs;
        ImU32 plus = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Subtle));
        dl->AddRectFilled(ImVec2(c.x - s, c.y - 1.5f * gs),
                          ImVec2(c.x + s, c.y + 1.5f * gs), plus);
        dl->AddRectFilled(ImVec2(c.x - 1.5f * gs, c.y - s),
                          ImVec2(c.x + 1.5f * gs, c.y + s), plus);

        ImVec2 ts = ImGui::CalcTextSize("Add theme");
        dl->AddText(ImVec2(mn.x + (cardW - ts.x) * 0.5f, cardMax.y + 4.0f * gs),
                    ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Text_Subtle)),
                    "Add theme");
        ImGui::PopID();
        if (clicked)
            std::printf("[settings] Add custom theme (not implemented yet)\n");
    }
}

// ── Customisation page (classic view) — Lot 4 demo of the nested Panel widget ─
// A scrollable list of Blender-style panels: level-1 categories (separated with
// a gap + border), nested child panels (flush, darker bodies), and leaf
// property rows showing the token's resolved value. Full token coverage +
// proper Global|Theme editors land in Lot 5.
// Declarative customisation map: Area (optional icon) → Section → properties.
// Each property is one TokenPropertyRow (a nested Panel). Adding/reordering is a
// pure data edit here.
namespace {

const std::vector<CustoArea>& CustoMap() {
    static const std::vector<CustoArea> kMap = {
        // ── User Interface: one section per widget, its editable properties ──
        { "User Interface", "", {
            { "Borders", {
                // Global on/off for EVERY border at once (window/child/frame/
                // tab/zone/panel…). Off → all borders collapse to 0.
                { Tok::S_Border_Enabled,           "Show borders" },
            }},
            { "Button", {
                { Tok::C_Button_Background,        "Background" },
                { Tok::C_Button_BackgroundHover,   "Background (hover)" },
                { Tok::C_Button_BackgroundDown,    "Background (pressed)" },
                { Tok::C_Button_Label,             "Label" },
            }},
            { "Frame & text inputs", {
                { Tok::C_Frame_Background,         "Background" },
                { Tok::C_Frame_BackgroundHover,    "Background (hover)" },
                { Tok::C_Frame_BackgroundDown,     "Background (active)" },
                { Tok::C_Frame_InputTextCursor,    "Text caret" },
                { Tok::C_Frame_CornerRadius,       "Corner radius" },
                { Tok::C_Frame_BorderWidth,        "Border width" },
            }},
            { "Dropdown", {
                { Tok::C_Dropdown_Background,       "Background" },
                { Tok::C_Dropdown_BackgroundHover,  "Background (hover)" },
                { Tok::C_Dropdown_BackgroundDown,   "Background (pressed)" },
                { Tok::C_Dropdown_Text,             "Label" },
                { Tok::C_Dropdown_Icon,             "Icon" },
                { Tok::C_Dropdown_Border,           "Border" },
                { Tok::C_Dropdown_CornerRadius,     "Corner radius" },
            }},
            { "Combo box", {
                { Tok::C_Combo_Background,           "Background" },
                { Tok::C_Combo_BackgroundHover,      "Background (hover)" },
                { Tok::C_Combo_PreviewText,          "Preview text" },
                { Tok::C_Combo_PopupBackground,      "Popup background" },
                { Tok::C_Combo_ItemBackgroundHover,  "Item (hover)" },
                { Tok::C_Combo_ItemBackgroundSelected, "Item (selected)" },
                { Tok::C_Combo_CornerRadius,         "Corner radius" },
            }},
            { "Menu", {
                { Tok::C_Menu_Background,            "Background" },
                { Tok::C_Menu_ItemHoverBg,          "Item (hover)" },
                { Tok::C_Menu_ItemSelectedBg,       "Item (selected)" },
                { Tok::C_Menu_Border,               "Border" },
                { Tok::C_Menu_CornerRadius,         "Corner radius" },
            }},
            { "Tabs", {
                { Tok::C_Tab_Background,            "Tab" },
                { Tok::C_Tab_BackgroundHover,       "Tab (hover)" },
                { Tok::C_Tab_BackgroundSelected,    "Tab (selected)" },
                { Tok::C_Tab_OverlineSelected,      "Selected overline" },
                { Tok::C_Tab_CornerRadius,          "Rounding" },
            }},
            { "Collapsing header", {
                { Tok::C_Header_Background,          "Background" },
                { Tok::C_Header_BackgroundHover,     "Background (hover)" },
                { Tok::C_Header_BackgroundDown,      "Background (active)" },
            }},
            { "Scroll bar", {
                { Tok::C_Scrollbar_Background,      "Track" },
                { Tok::C_Scrollbar_Grab,            "Grab" },
                { Tok::C_Scrollbar_GrabHover,       "Grab (hover)" },
                { Tok::C_Scrollbar_Size,            "Thickness" },
                { Tok::C_Scrollbar_CornerRadius,    "Rounding" },
            }},
            { "Slider", {
                { Tok::C_Slider_Grab,               "Grab" },
                { Tok::C_Slider_GrabDown,           "Grab (active)" },
                { Tok::C_Slider_CornerRadius,       "Rounding" },
            }},
            { "Checkbox", {
                { Tok::C_Checkbox_Mark,             "Check mark" },
                { Tok::C_Checkbox_BackgroundSelected, "Background (checked)" },
            }},
            { "Toggle", {
                { Tok::C_Toggle_Background,         "Background" },
                { Tok::C_Toggle_BackgroundSelected, "Background (on)" },
                { Tok::C_Toggle_Label,             "Label" },
                { Tok::C_Toggle_LabelSelected,      "Label (on)" },
                { Tok::C_Toggle_CornerRadius,       "Corner radius" },
            }},
            { "Icon button", {
                { Tok::C_IconButton_Background,     "Background" },
                { Tok::C_IconButton_BackgroundHover, "Background (hover)" },
                { Tok::C_IconButton_Icon,           "Icon" },
                { Tok::C_IconButton_CornerRadius,   "Corner radius" },
            }},
            { "Separator", {
                { Tok::C_Separator_Color,           "Line" },
                { Tok::C_Separator_Hover,           "Line (hover)" },
                { Tok::C_Separator_Size,            "Thickness" },
            }},
            { "Window & popup", {
                { Tok::C_Window_Background,          "Window background" },
                { Tok::C_Window_BorderColor,         "Window border" },
                { Tok::C_Window_CornerRadius,        "Window rounding" },
                { Tok::C_Popup_Background,            "Popup background" },
                { Tok::C_Popup_CornerRadius,          "Popup rounding" },
                { Tok::C_Child_Background,            "Child background" },
            }},
            { "Tooltip / key cap", {
                { Tok::C_KeyCap_Background,          "Key cap background" },
                { Tok::C_KeyCap_Border,             "Key cap border" },
                { Tok::C_KeyCap_Label,              "Key cap label" },
                { Tok::C_KeyCap_CornerRadius,       "Key cap rounding" },
            }},
            { "Icon color", {
                { Tok::S_Color_Icon_Primary,        "Primary" },
                { Tok::S_Color_Icon_Secondary,      "Secondary" },
                { Tok::S_Color_Icon_Tertiary,       "Tertiary" },
                { Tok::S_Icon_Disabled,             "Disabled" },
            }},
        }},

        // ── Text Style: editable font sizes & weights (NOT font families,
        //    which belong to General). S_Text_* are text-STYLE bundles and
        //    not numeric, so they are intentionally excluded. ──
        { "Text Style", "", {
            { "Global", {
                { Tok::S_FontSize_Default,         "Base interface size" },
                { Tok::S_FontScale_Default,        "Font scale" },
            }},
            { "Display", {
                { Tok::S_FontSize_DisplayL,        "Display L size" },
                { Tok::S_FontSize_DisplayM,        "Display M size" },
                { Tok::S_FontSize_DisplayS,        "Display S size" },
            }},
            { "Headings", {
                { Tok::S_FontSize_HeadingXl,       "Heading XL size" },
                { Tok::S_FontSize_HeadingL,        "Heading L size" },
                { Tok::S_FontSize_HeadingM,        "Heading M size" },
                { Tok::S_FontSize_HeadingS,        "Heading S size" },
                { Tok::S_FontSize_HeadingXs,       "Heading XS size" },
                { Tok::S_FontWeight_HeadingM,      "Heading weight" },
            }},
            { "Body", {
                { Tok::S_FontSize_BodyL,           "Body L size" },
                { Tok::S_FontSize_BodyM,           "Body M size" },
                { Tok::S_FontSize_BodyS,           "Body S size" },
                { Tok::S_FontWeight_BodyM,         "Body weight" },
            }},
            { "Labels & detail", {
                { Tok::S_FontSize_LabelL,          "Label L size" },
                { Tok::S_FontSize_LabelM,          "Label M size" },
                { Tok::S_FontSize_LabelS,          "Label S size" },
                { Tok::S_FontSize_DetailM,         "Detail size" },
            }},
            { "Monospace", {
                { Tok::S_FontSize_MonoL,           "Mono L size" },
                { Tok::S_FontSize_MonoM,           "Mono M size" },
                { Tok::S_FontSize_MonoS,           "Mono S size" },
                { Tok::S_FontSize_CodeM,           "Code size" },
            }},
        }},

        // ── Per-editor surfaces (tokens unique to particular editors). ──
        { "Viewport", "image", {
            { "Canvas", {
                { Tok::S_Surface_Canvas,           "Canvas background" },
                { Tok::C_Cursor_Color,             "Cursor guides" },
                { Tok::C_Viewport_CanvasArea,      "Ruler/canvas backdrop" },
                { Tok::C_Viewport_Guide,           "Alignment guide" },
            }},
            { "Object state", {
                { Tok::S_State_Active_OnPage,      "Active object (on page)" },
                { Tok::S_State_Active_Loose,       "Active object (page-less)" },
                { Tok::S_State_Selected_Loose,     "Selected object (page-less)" },
            }},
            { "Page & object overlays", {
                { Tok::C_Viewport_PageBorder,      "Page border" },
                { Tok::C_Viewport_PageNameHover,   "Page-name hover" },
                { Tok::C_Viewport_OriginOutline,   "Origin dot outline" },
                { Tok::C_Viewport_ThumbnailBackground, "Thumbnail background" },
                { Tok::C_Viewport_ThumbnailBorder, "Thumbnail border" },
            }},
            { "2D cursor", {
                { Tok::C_Viewport_CursorRing,      "Outer ring" },
                { Tok::C_Viewport_CursorRingAccent, "Inner ring" },
                { Tok::C_Viewport_CursorTick,      "Crosshair ticks" },
                { Tok::C_Viewport_CursorAxisX,     "+X axis" },
                { Tok::C_Viewport_CursorAxisY,     "+Y axis" },
            }},
        }},
        { "Editors", "", {
            { "Editor frame", {
                { Tok::C_Editor_Background,         "Editor background" },
                { Tok::C_Editor_TopBarBackground,   "Editor top bar" },
                { Tok::C_Editor_ContentInset,       "Content inset" },
                { Tok::C_Zone_SeparatorColor,       "Zone separator" },
                { Tok::C_Zone_SeparatorColorContinuation, "Zone separator (between)" },
                { Tok::C_Zone_SeparatorContinuationOpacity, "Zone separator opacity (between)" },
                { Tok::C_Zone_SeparatorSize,        "Zone separator size" },
            }},
            { "Zone tabs", {
                { Tok::C_ZoneTab_Background,         "Tab" },
                { Tok::C_ZoneTab_BackgroundActive,   "Tab (active)" },
                { Tok::C_ZoneTab_BackgroundHover,    "Tab (hover)" },
                { Tok::C_ZoneTab_Text,               "Tab label" },
                { Tok::C_ZoneTab_BarBackground,      "Tab bar background" },
            }},
            { "Zone overlays", {
                { Tok::C_ZoneOverlay_CornerTopLeft,     "Split corner (top-left)" },
                { Tok::C_ZoneOverlay_CornerTopRight,    "Split corner (top-right)" },
                { Tok::C_ZoneOverlay_CornerBottomLeft,  "Split corner (bottom-left)" },
                { Tok::C_ZoneOverlay_CornerBottomRight, "Split corner (bottom-right)" },
                { Tok::C_ZoneOverlay_SplitLine,         "Split preview line" },
                { Tok::C_ZoneOverlay_JoinKeep,          "Join: kept zone" },
                { Tok::C_ZoneOverlay_JoinRemove,        "Join: removed zone" },
                { Tok::C_ZoneOverlay_JoinResidual,      "Join: residual zone" },
                { Tok::C_ZoneOverlay_JoinFrame,         "Join: final frame" },
                { Tok::C_ZoneOverlay_TransformDim,      "Transform/crop scrim" },
            }},
        }},

        // ── Edit mode: vertex/edge/handle overlay cues. ──
        { "Edit Mode", "", {
            { "Edges & vertices", {
                { Tok::C_EditHandle_Edge,           "Edge line" },
                { Tok::C_EditHandle_Vertex,         "Vertex dot" },
                { Tok::C_EditHandle_VertexRing,     "Vertex ring" },
                { Tok::C_EditHandle_NurbsHull,      "NURBS control polygon" },
            }},
            { "Bézier handles", {
                { Tok::C_EditHandle_Free,           "Free" },
                { Tok::C_EditHandle_Aligned,        "Aligned" },
                { Tok::C_EditHandle_Mirrored,       "Mirrored" },
                { Tok::C_EditHandle_Vector,         "Vector" },
                { Tok::C_EditHandle_Default,        "Default" },
            }},
        }},

        // ── Colors grouped by usage (subtle / hover / pressed / focus). Only
        //    the semantic role colours actually used by the UI, not the full
        //    palette. ──
        { "Colors", "", {
            { "Accent", {
                { Tok::S_Color_Accent_Default,     "Default" },
                { Tok::S_Color_Accent_Hover,       "Hover" },
                { Tok::S_Color_Accent_Down,        "Pressed" },
                { Tok::S_Background_Accent_Subtle, "Subtle background" },
                { Tok::S_Background_Accent_KbdFocus, "Keyboard focus" },
            }},
            { "Positive (success)", {
                { Tok::S_Color_Positive_Default,   "Default" },
                { Tok::S_Background_Positive_Hover, "Hover" },
                { Tok::S_Background_Positive_Pressed, "Pressed" },
                { Tok::S_Background_Positive_Subtle, "Subtle background" },
            }},
            { "Negative (danger)", {
                { Tok::S_Color_Negative_Default,   "Default" },
                { Tok::S_Background_Negative_Hover, "Hover" },
                { Tok::S_Background_Negative_Pressed, "Pressed" },
                { Tok::S_Background_Negative_Subtle, "Subtle background" },
            }},
            { "Notice (warning)", {
                { Tok::S_Color_Notice_Default,     "Default" },
                { Tok::S_Background_Notice_Subtle, "Subtle background" },
            }},
            { "Info", {
                { Tok::S_Background_Info_Default,  "Default" },
                { Tok::S_Background_Info_Hover,    "Hover" },
                { Tok::S_Background_Info_Pressed,  "Pressed" },
                { Tok::S_Background_Info_Subtle,   "Subtle background" },
            }},
            { "Neutral", {
                { Tok::S_Background_Neutral_Default, "Default" },
                { Tok::S_Background_Neutral_Hover,   "Hover" },
                { Tok::S_Background_Neutral_Pressed, "Pressed" },
                { Tok::S_Background_Neutral_Subtle,  "Subtle background" },
            }},
            { "Application surfaces", {
                { Tok::S_Color_Background_Default,  "Base (app + title bar)" },
                { Tok::S_Background_App_Control,    "Clickable controls" },
                { Tok::S_Surface_Canvas,            "Editor canvas" },
                { Tok::S_Surface_Raised,            "Raised bar (menu bar)" },
                { Tok::S_Background_App_Child,      "Inner child" },
                { Tok::S_Background_App_Frame,      "Input / frame" },
                { Tok::S_Color_Background_Popup,    "Popup surface" },
            }},
            { "Application text", {
                { Tok::S_Color_Text_Default,        "Primary text" },
                { Tok::S_Color_Text_Subtle,         "Muted text" },
                { Tok::S_Text_Tertiary,             "Tertiary text" },
                { Tok::S_Color_Text_Disabled,       "Disabled text" },
                { Tok::S_Color_Text_Link,           "Link text" },
            }},
            { "Application borders", {
                { Tok::S_Color_Border_Default,      "Border" },
                { Tok::S_Border_Subtle,             "Subtle border" },
                { Tok::S_Color_Border_Strong,       "Strong border" },
                { Tok::S_Border_Focus,              "Focus ring" },
            }},
        }},

        // ── Top bar (main title bar) & status bar. ──
        { "Top bar", "", {
            { "Title bar", {
                { Tok::C_TitleBar_Background,       "Background" },
                { Tok::C_TitleBar_Text,             "Text" },
                { Tok::C_TitleBar_Icon,             "Icon" },
                { Tok::C_TitleBar_ButtonHover,      "Button (hover)" },
                { Tok::C_TitleBar_CloseHover,       "Close (hover)" },
            }},
            { "Preferences title bar", {
                { Tok::C_PrefBar_Background,        "Background" },
                { Tok::C_PrefBar_Text,              "Text" },
                { Tok::C_PrefBar_Icon,              "Icon" },
                { Tok::C_PrefBar_ButtonHover,       "Button (hover)" },
                { Tok::C_PrefBar_CloseHover,        "Close (hover)" },
            }},
        }},
        { "Status bar", "", {
            { "Status bar", {
                { Tok::C_StatusBar_Background,      "Background" },
                { Tok::C_StatusBar_Label,           "Label" },
                { Tok::C_StatusBar_Height,          "Height" },
            }},
        }},

        // ── Data visualisation: discrete role colours + chart furniture.
        //    The big sequential/diverging ramps are auto-generated below
        //    (RenderGeneratedColorFamilies), not listed here. ──
        { "Data Visualisation", "", {
            { "Categorical", {
                { Tok::S_DataViz_Cat_1,  "Category 1" }, { Tok::S_DataViz_Cat_2,  "Category 2" },
                { Tok::S_DataViz_Cat_3,  "Category 3" }, { Tok::S_DataViz_Cat_4,  "Category 4" },
                { Tok::S_DataViz_Cat_5,  "Category 5" }, { Tok::S_DataViz_Cat_6,  "Category 6" },
                { Tok::S_DataViz_Cat_7,  "Category 7" }, { Tok::S_DataViz_Cat_8,  "Category 8" },
                { Tok::S_DataViz_Cat_9,  "Category 9" }, { Tok::S_DataViz_Cat_10, "Category 10" },
                { Tok::S_DataViz_Cat_11, "Category 11" },{ Tok::S_DataViz_Cat_12, "Category 12" },
            }},
            { "Chart furniture", {
                { Tok::S_DataViz_Axis,      "Axis" },
                { Tok::S_DataViz_Grid,      "Grid lines" },
                { Tok::S_DataViz_Label,     "Labels" },
                { Tok::S_DataViz_Highlight, "Highlight" },
                { Tok::S_Color_DataViz_Line,      "Line series" },
                { Tok::S_Color_DataViz_Histogram, "Histogram" },
            }},
        }},
    };
    return kMap;
}
} // namespace

namespace {
// True if `tok` has any override (global or in `theme`).
bool TokOverridden(Tok tok, DS::ThemeType theme) {
    auto& mgr = DS::DesignSystem::Instance().GetOverrideManager();
    const std::string id = DS::TokIdStr(tok);
    return mgr.HasGlobalOverride(id) || mgr.HasThemeOverride(id, theme);
}
// Reset both layers of a token.
void TokReset(Tok tok, DS::ThemeType theme) {
    auto& ds = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    const std::string id = DS::TokIdStr(tok);
    mgr.RemoveGlobalOverride(id);
    mgr.RemoveThemeOverride(id, theme);
    ds.NotifyOverrideChange();
    ds.ApplyGlobalStyle();
}
bool SectionOverridden(const CustoSection& s, DS::ThemeType th) {
    for (const CustoProp& p : s.props) if (TokOverridden(p.token, th)) return true;
    return false;
}
bool AreaOverridden(const CustoArea& a, DS::ThemeType th) {
    for (const CustoSection& s : a.sections) if (SectionOverridden(s, th)) return true;
    return false;
}

// ── Auto-generated colour families (Palette + Data-Viz ramps) ────────────────
// These families are large, regular, and named by a numeric shade index, so we
// generate their editors by scanning the enum instead of hand-listing hundreds
// of tokens. A family is keyed by its id PREFIX; members sort by their trailing
// numeric shade (light → dark), which is the design-system convention
// (shade 100 = lightest, 1600 = darkest).

// A primitive palette hue, by token-id prefix and user-facing name.
struct ColorFamily { const char* prefix; const char* name; };

const std::vector<ColorFamily>& PaletteFamilies() {
    static const std::vector<ColorFamily> kHues = {
        { "primitive.color.gray.",       "Gray" },
        { "primitive.color.blue.",       "Blue" },
        { "primitive.color.indigo.",     "Indigo" },
        { "primitive.color.purple.",     "Purple" },
        { "primitive.color.fuchsia.",    "Fuchsia" },
        { "primitive.color.magenta.",    "Magenta" },
        { "primitive.color.pink.",       "Pink" },
        { "primitive.color.red.",        "Red" },
        { "primitive.color.cinnamon.",   "Cinnamon" },
        { "primitive.color.orange.",     "Orange" },
        { "primitive.color.brown.",      "Brown" },
        { "primitive.color.yellow.",     "Yellow" },
        { "primitive.color.chartreuse.", "Chartreuse" },
        { "primitive.color.celery.",     "Celery" },
        { "primitive.color.green.",      "Green" },
        { "primitive.color.seafoam.",    "Seafoam" },
        { "primitive.color.turquoise.",  "Turquoise" },
        { "primitive.color.cyan.",       "Cyan" },
        { "primitive.color.silver.",     "Silver" },
    };
    return kHues;
}

const std::vector<ColorFamily>& DataVizRampFamilies() {
    static const std::vector<ColorFamily> kRamps = {
        { "semantic.data-viz.sequential.viridis.", "Sequential - Viridis" },
        { "semantic.data-viz.sequential.magma.",   "Sequential - Magma" },
        { "semantic.data-viz.sequential.plasma.",  "Sequential - Plasma" },
        { "semantic.data-viz.sequential.inferno.", "Sequential - Inferno" },
        { "semantic.data-viz.sequential.cividis.", "Sequential - Cividis" },
        { "semantic.data-viz.diverging.rd-bu.",    "Diverging - Red/Blue" },
        { "semantic.data-viz.diverging.pu-gn.",    "Diverging - Purple/Green" },
        { "semantic.data-viz.diverging.br-teal.",  "Diverging - Brown/Teal" },
    };
    return kRamps;
}

// Trailing integer of an id (the shade index), or -1 if none. Used to order a
// family light→dark instead of by alphabetical token name.
int TrailingShade(const std::string& id) {
    size_t i = id.size();
    while (i > 0 && std::isdigit((unsigned char)id[i - 1])) --i;
    if (i == id.size()) return -1;
    return std::atoi(id.c_str() + i);
}

// True if `name` belongs to ANY generated family (so "Other" can skip it).
bool IsGeneratedFamilyToken(const std::string& name) {
    auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
    for (const ColorFamily& f : PaletteFamilies())     if (starts(f.prefix)) return true;
    for (const ColorFamily& f : DataVizRampFamilies()) if (starts(f.prefix)) return true;
    return false;
}

// Render one generated family as a sub-panel: every token whose id starts with
// `prefix`, sorted by shade (light → dark). Each row is a normal
// TokenPropertyRow, so editing/override/chain all work as elsewhere.
void RenderColorFamilyPanel(const char* panelId, const ColorFamily& fam,
                            DS::Context& ctx, bool editGlobal) {
    auto& ds = DS::DesignSystem::Instance();
    const DS::ThemeType th = ctx.GetTheme();

    std::vector<std::pair<int, Tok>> members;
    for (int k = 0; k < (int)Tok::_Count; ++k) {
        const std::string nm = DS::TokIdStr((Tok)k);
        if (nm.rfind(fam.prefix, 0) != 0) continue;
        members.emplace_back(TrailingShade(nm), (Tok)k);
    }
    if (members.empty()) return;
    std::sort(members.begin(), members.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    char fid[96];
    std::snprintf(fid, sizeof(fid), "%s_%s", panelId, fam.prefix);
    UI::PanelConfig fc; fc.id = fid; fc.label = fam.name;
    // Bubble an override badge up if any shade is overridden.
    for (const auto& m : members)
        if (TokOverridden(m.second, th)) { fc.hasOverride = true; break; }
    UI::PanelResult fr = UI::BeginPanel(fc);
    if (fr.resetClicked)
        for (const auto& m : members) TokReset(m.second, th);
    if (fr.open) {
        for (const auto& m : members) {
            const std::string nm = DS::TokIdStr(m.second);
            // Friendly per-shade label: the shade index (e.g. "300").
            char lbl[16];
            if (m.first >= 0) std::snprintf(lbl, sizeof(lbl), "%d", m.first);
            else              std::snprintf(lbl, sizeof(lbl), "%s", "value");
            UI::TokenPropertyRow(fid, lbl, m.second, ctx, editGlobal);
        }
    }
    UI::EndPanel();
}
} // namespace

void SettingsWindow::RenderCustomisationPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float pad = 16.0f * gs;
    (void)width; (void)height;
    DS::Context ctx = ds.GetCurrentContext();
    const DS::ThemeType th = ctx.GetTheme();

    // The page child is already inset by the uniform outer gap, so content sits
    // at x = 0 here (only a top inset for the title).
    ImGui::SetCursorPos(ImVec2(0.0f, pad * 0.5f));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Customisation");
    ImGui::SameLine(0.0f, 16.0f * gs);
    UI::Checkbox("##editGlobally", "Edit globally", &editGlobal_);
    ImGui::EndGroup();
    ImGui::Spacing();

    // Transparent, full-width scroll region (no rounded ChildBg at the corners),
    // with the Blender-style overlay scrollbar (zero reserved space).
    UI::BeginScroll("##custoScroll", ImVec2(0.0f, 0.0f),
                    0, ImGuiWindowFlags_NoBackground);

    int a = 0;
    for (const CustoArea& area : CustoMap()) {
        char aid[48]; std::snprintf(aid, sizeof(aid), "##area%d", a++);
        UI::PanelConfig ac; ac.id = aid; ac.label = area.name;
        ac.icon = area.icon; ac.defaultOpen = (a == 1);
        ac.hasOverride = AreaOverridden(area, th);   // badge bubbles up the tree
        UI::PanelResult ar = UI::BeginPanel(ac);
        if (ar.resetClicked)
            for (const CustoSection& s : area.sections)
                for (const CustoProp& p : s.props) TokReset(p.token, th);
        if (ar.open) {
            int s = 0;
            for (const CustoSection& sec : area.sections) {
                char sid[48]; std::snprintf(sid, sizeof(sid), "##sec%d_%d", a, s++);
                UI::PanelConfig sc; sc.id = sid; sc.label = sec.name;
                sc.hasOverride = SectionOverridden(sec, th);
                UI::PanelResult sr = UI::BeginPanel(sc);
                if (sr.resetClicked)
                    for (const CustoProp& p : sec.props) TokReset(p.token, th);
                if (sr.open)
                    for (const CustoProp& p : sec.props)
                        UI::TokenPropertyRow(sid, p.label, p.token, ctx, editGlobal_);
                UI::EndPanel();
            }
        }
        UI::EndPanel();
    }

    // ── Palette: one sub-panel per primitive hue, shades light → dark ─────────
    {
        UI::PanelConfig pc; pc.id = "##palette"; pc.label = "Palette";
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open)
            for (const ColorFamily& fam : PaletteFamilies())
                RenderColorFamilyPanel("##palette", fam, ctx, editGlobal_);
        UI::EndPanel();
    }

    // ── Gradients & ramps: sequential / diverging data-viz colour ramps ───────
    {
        UI::PanelConfig pc; pc.id = "##gradients"; pc.label = "Gradients & ramps";
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open)
            for (const ColorFamily& fam : DataVizRampFamilies())
                RenderColorFamilyPanel("##gradients", fam, ctx, editGlobal_);
        UI::EndPanel();
    }

    // ── "Other": auto-generated editors for EVERY token not handled above ─────
    // Walks the whole Tok enum, skips the ones already placed in CustoMap or in
    // a generated family (Palette / ramps), and groups the rest by tier
    // (Primitive / Semantic / Component). New tokens therefore show up here
    // automatically until curated into CustoMap.
    {
        // Set of tokens already curated above (in CustoMap or generated).
        static std::vector<bool> curated;
        if (curated.empty()) {
            curated.assign((size_t)Tok::_Count, false);
            for (const CustoArea& ar : CustoMap())
                for (const CustoSection& s : ar.sections)
                    for (const CustoProp& p : s.props)
                        curated[(size_t)p.token] = true;
            for (int k = 0; k < (int)Tok::_Count; ++k)
                if (IsGeneratedFamilyToken(DS::TokIdStr((Tok)k)))
                    curated[(size_t)k] = true;
        }
        UI::PanelConfig oc; oc.id = "##other"; oc.label = "Other";
        UI::PanelResult orr = UI::BeginPanel(oc);
        if (orr.open) {
            struct Tier { const char* name; char prefix; };
            const Tier tiers[] = { {"Primitives",'P'}, {"Semantic",'S'}, {"Component",'C'} };
            int ti = 0;
            for (const Tier& tier : tiers) {
                char tid[32]; std::snprintf(tid, sizeof(tid), "##other%d", ti++);
                UI::PanelConfig tc; tc.id = tid; tc.label = tier.name;
                UI::PanelResult tr = UI::BeginPanel(tc);
                if (tr.open) {
                    for (int k = 0; k < (int)Tok::_Count; ++k) {
                        if (curated[(size_t)k]) continue;
                        const std::string nm = DS::TokIdStr((Tok)k);
                        if (nm.empty() || nm[0] != tier.prefix) continue;
                        // Only show editable value tokens (skip text styles etc.).
                        DS::ValueType vt = ds.ResolveTokenValue(
                            nm, th).GetType();
                        if (vt != DS::ValueType::Color && vt != DS::ValueType::Float &&
                            vt != DS::ValueType::Int && vt != DS::ValueType::Vec2 &&
                            vt != DS::ValueType::Bezier)
                            continue;
                        UI::TokenPropertyRow(tid, nm.c_str(), (Tok)k, ctx, editGlobal_);
                    }
                }
                UI::EndPanel();
            }
        }
        UI::EndPanel();
    }

    UI::EndScroll();
}

// ── Keymap page: shortcuts as nested panels (category → action) ──────────────
// Mirrors the Customisation layout: a level-1 panel per action category, each
// holding one panel per action whose header shows the primary binding in an
// inline capture field (quick edit without expanding). Expanding reveals the
// full per-binding editor (all entries: capture field + enable + restore +
// delete) plus a "+" to add one — the same editing the classic Shortcuts tab
// offers, reusing the shared ShortcutCaptureField widget.
namespace {

// Commit an edited signature at `index`, respecting the safety gate (a
// dangerous binding is silently not committed; the field keeps the attempt).
void KeymapCommit(Shortcuts::ShortcutManager& sm, const std::string& actionId,
                  int index, const Shortcuts::EventSignature& sig) {
    const Shortcuts::ShortcutBinding* b = sm.GetBinding(actionId);
    if (!b || index < 0 || index >= (int)b->current.size()) return;
    if (!sm.IsDangerousBinding(actionId, sig).empty()) return;
    auto sigs = b->current;
    sigs[(size_t)index] = sig;
    sm.SetBindings(actionId, sigs);
}

// True if any binding of `actionId` differs from its default (→ override badge).
bool ActionOverridden(const Shortcuts::ShortcutBinding* b) {
    return b && b->current != b->defaults;
}

} // namespace

void SettingsWindow::RenderKeymapPage(float width, float height) {
    auto& ds = DS::DesignSystem::Instance();
    auto& sm = Shortcuts::ShortcutManager::Instance();
    const float gs = ds.GetGlobalScale();
    (void)width; (void)height;

    // Title + search on one line (page-specific, like Customisation's checkbox).
    const float pad = 16.0f * gs;
    ImGui::SetCursorPos(ImVec2(0.0f, pad * 0.5f));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Keymap");
    ImGui::SameLine(0.0f, 16.0f * gs);
    ImGui::SetNextItemWidth(220.0f * gs);
    ImGui::InputTextWithHint("##keymapSearch", "Search shortcuts...",
                             keymapSearch_, sizeof(keymapSearch_));
    ImGui::SameLine(0.0f, 12.0f * gs);
    if (ImGui::SmallButton("Restore all")) sm.RestoreAllDefaults();
    ImGui::EndGroup();
    ImGui::Spacing();

    // Lower-case search needle (matches name / id / shortcut text).
    std::string needle = keymapSearch_;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    auto matches = [&](const Shortcuts::Action& a) -> bool {
        if (needle.empty()) return true;
        auto has = [&](std::string h) {
            std::transform(h.begin(), h.end(), h.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return h.find(needle) != std::string::npos;
        };
        if (has(a.name) || has(a.id) || has(a.description)) return true;
        for (const std::string& s : sm.GetShortcutStrings(a.id)) if (has(s)) return true;
        return false;
    };

    UI::BeginScroll("##keymapScroll", ImVec2(0.0f, 0.0f), 0,
                    ImGuiWindowFlags_NoBackground);

    static const Shortcuts::ActionCategory kCats[] = {
        Shortcuts::ActionCategory::Application, Shortcuts::ActionCategory::File,
        Shortcuts::ActionCategory::Edit,        Shortcuts::ActionCategory::View,
        Shortcuts::ActionCategory::Window,      Shortcuts::ActionCategory::Tool,
        Shortcuts::ActionCategory::Selection,   Shortcuts::ActionCategory::Transform,
        Shortcuts::ActionCategory::Navigation,  Shortcuts::ActionCategory::Custom,
    };

    int ci = 0;
    for (Shortcuts::ActionCategory cat : kCats) {
        std::vector<const Shortcuts::Action*> actions;
        for (const Shortcuts::Action* a : sm.GetActionsByCategory(cat))
            if (matches(*a)) actions.push_back(a);
        if (actions.empty()) continue;

        char cid[48]; std::snprintf(cid, sizeof(cid), "##kmcat%d", ci++);
        UI::PanelConfig cc; cc.id = cid; cc.label = Shortcuts::ActionCategoryName(cat);
        cc.defaultOpen = (!needle.empty());   // expand all when searching
        // Bubble override badge up the category if any action is modified.
        for (const Shortcuts::Action* a : actions)
            if (ActionOverridden(sm.GetBinding(a->id))) { cc.hasOverride = true; break; }
        UI::PanelResult cr = UI::BeginPanel(cc);
        if (cr.resetClicked)
            for (const Shortcuts::Action* a : actions) sm.RestoreDefaults(a->id);

        if (cr.open) {
            for (const Shortcuts::Action* a : actions) {
                const Shortcuts::ShortcutBinding* b = sm.GetBinding(a->id);

                char aid[160]; std::snprintf(aid, sizeof(aid), "%s_%s", cid, a->id.c_str());
                // Header label = action name + its primary binding as text. We
                // do NOT put an interactive capture field in the header band:
                // that needs SetCursorScreenPos inside the panel child, which
                // fights ImGui's auto-resize bookkeeping and asserts. All editing
                // lives in the expanded body instead (kept alive for BeginPanel).
                std::string label = a->name;
                if (b && !b->current.empty()) {
                    label += "      ";
                    label += b->current.front().ToString();
                    if (b->current.size() > 1)
                        label += " +" + std::to_string(b->current.size() - 1);
                }
                UI::PanelConfig ac; ac.id = aid; ac.label = label.c_str();
                ac.hasOverride = ActionOverridden(b);
                UI::PanelResult ar = UI::BeginPanel(ac);
                if (ar.resetClicked) sm.RestoreDefaults(a->id);

                if (ar.open) {
                    const float ipad = 8.0f * gs;
                    ImGui::Indent(ipad);
                    if (!a->description.empty())
                        ImGui::TextWrapped("%s", a->description.c_str());
                    ImGui::Dummy(ImVec2(0.0f, 4.0f * gs));

                    b = sm.GetBinding(a->id);
                    if (b) {
                        for (size_t i = 0; i < b->current.size(); ++i) {
                            ImGui::PushID((int)i);
                            bool en = b->IsEntryEnabled(i);
                            if (UI::CheckboxBox("##en", &en)) {
                                sm.SetEntryEnabled(a->id, (int)i, en);
                                b = sm.GetBinding(a->id);
                            }
                            ImGui::SameLine(0.0f, 6.0f * gs);

                            Shortcuts::EventSignature sig = b->current[i];
                            std::string danger = sm.IsDangerousBinding(a->id, sig);
                            ShortcutCaptureField::StatusOverride st =
                                danger.empty() ? ShortcutCaptureField::StatusOverride::None
                                               : ShortcutCaptureField::StatusOverride::Error;
                            ImGui::PushItemWidth(260.0f * gs);
                            if (ShortcutCaptureField::Render("##field", sig,
                                    ShortcutCaptureField::Mode::Combo,
                                    /*withInputKindToggle=*/true, /*withDropdown=*/true, st)) {
                                KeymapCommit(sm, a->id, (int)i, sig);
                                b = sm.GetBinding(a->id);
                            }
                            ImGui::PopItemWidth();

                            // Restore this entry (only when it differs from default).
                            bool canRestore = (i < b->defaults.size()) &&
                                              !(b->current[i] == b->defaults[i]);
                            if (canRestore) {
                                ImGui::SameLine(0.0f, 6.0f * gs);
                                if (ImGui::SmallButton("Reset")) {
                                    sm.RestoreBindingAt(a->id, (int)i);
                                    b = sm.GetBinding(a->id);
                                }
                            }
                            ImGui::SameLine(0.0f, 6.0f * gs);
                            if (ImGui::SmallButton("Remove")) {
                                sm.RemoveBinding(a->id, b->current[i]);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 2.0f * gs));
                    if (ImGui::SmallButton("+ Add shortcut")) {
                        Shortcuts::EventSignature empty;
                        empty.type = Shortcuts::EventType::KeyPress;
                        empty.key  = ImGuiKey_None;
                        sm.AddBinding(a->id, empty);
                    }
                    ImGui::Unindent(ipad);
                }
                UI::EndPanel();
            }
        }
        UI::EndPanel();
    }

    UI::EndScroll();
}

} // namespace UI

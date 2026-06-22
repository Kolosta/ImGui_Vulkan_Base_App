#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/IconWidgets.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

// macOS puts the traffic-light window buttons on the LEFT; Windows/Linux put
// the min/max/close group on the RIGHT. Detected at compile time.
#if defined(__APPLE__)
constexpr bool kButtonsLeft = true;
#else
constexpr bool kButtonsLeft = false;
#endif

// Record a just-submitted item's screen rect as a hit-test "blocker" (so the
// SDL hit-test treats it as a clickable widget, not a window-drag handle).
// The SDL hit-test receives points in WINDOW coordinates, but GetItemRectMin/
// Max return SCREEN coordinates. With multi-viewport enabled the main
// viewport's Pos is the window's on-screen position (no longer (0,0) when the
// window isn't at the top-left), so we must subtract it to convert back to
// window space — otherwise every blocker is offset and the whole bar reads as
// draggable (the regression where buttons/menus stopped responding).
void PushBlocker(std::vector<SDL_Rect>& out) {
    const ImVec2 off = ImGui::GetMainViewport()->Pos;
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    out.push_back(SDL_Rect{ (int)(mn.x - off.x), (int)(mn.y - off.y),
                            (int)(mx.x - mn.x), (int)(mx.y - mn.y) });
}

} // namespace

// ── The unified, borderless application title bar ─────────────────────────────
void Application::RenderTitleBar() {
    DS::DesignSystem::ComponentScope _cs("TitleBar");
    auto& ds  = DS::DesignSystem::Instance();
    auto& sm  = Shortcuts::ShortcutManager::Instance();
    auto& im  = VectorGraphics::IconManager::Instance();
    const float gs = ds.GetGlobalScale();

    const float controlH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    ImVec2 pad = ds.GetVec2(Tok::C_Dropdown_Padding); pad.x *= gs; pad.y *= gs;
    const float barH = controlH + pad.y * 2.0f;
    titleBarHeightPx_ = barH;                 // published for the SDL hit-test
    titleBarBlockers_.clear();

    ImVec4 bg     = ds.GetColor(Tok::C_TitleBar_Background);
    ImVec4 textC  = ds.GetColor(Tok::C_TitleBar_Text);
    ImVec4 iconC  = ds.GetColor(Tok::C_TitleBar_Icon);
    ImVec4 btnHov = ds.GetColor(Tok::C_TitleBar_ButtonHover);
    ImVec4 closeHov = ds.GetColor(Tok::C_TitleBar_CloseHover);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, barH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
    // Let the bar be exactly barH tall: ImGui clamps windows to WindowMinSize
    // (~32px by default), which was making the 24px bar 32px tall and overlap
    // the layout below. Zero it for this window.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::Begin("##TitleBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoSavedSettings);

    // Publish the REAL height (after ImGui clamping) so the layout below docks
    // flush against it, whatever ImGui decided.
    titleBarHeightPx_ = ImGui::GetWindowSize().y;

    const float winW   = ImGui::GetWindowWidth();

    // No ImGui double-click catcher here: on a DRAGGABLE bar the native
    // HTCAPTION drag captures the mouse at button-down, so ImGui never sees the
    // double-click. We detect it from the raw SDL event in ProcessEvents, and
    // drag-to-restore is handled by the SDL_EVENT_WINDOW_MOVED watch.
    const float sysBtnW = controlH + pad.x * 2.0f;     // one system-button slot
    const float sysGroupW = sysBtnW * 3.0f;            // min + max + close

    // ── System window buttons (min / max / close), redrawn ───────────────────
    // Behaviour is native; only the pixels are custom. macOS → left pills,
    // Windows/Linux → right square group. Drawn first to reserve their slots.
    auto drawSystemButtons = [&](float startX) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Show the restore glyph whenever the window is "filled": our maximize
        // OR fullscreen. Clicking the button then leaves that state.
        const bool maximized = maximized_ || fullscreen_;
        struct Btn { const char* id; int kind; };  // 0 min, 1 max, 2 close
        const Btn order[3] = { {"##min",0}, {"##max",1}, {"##close",2} };
        float x = startX;
        for (const Btn& b : order) {
            ImGui::SetCursorPos(ImVec2(x, 0.0f));
            ImGui::InvisibleButton(b.id, ImVec2(sysBtnW, barH));
            bool hov = ImGui::IsItemHovered();
            // OS window buttons fire on RELEASE, not press: the action runs only
            // when the mouse is released while still over the button. A press
            // that drags off the button before release is cancelled (native
            // window-button behaviour). IsItemDeactivated() is the mouse-up frame
            // after this button was active; combined with IsItemHovered() it means
            // "released on top of me".
            bool clk = ImGui::IsItemDeactivated() && hov;
            // Held state (pressed but not yet released) for an optional darker cue.
            bool held = ImGui::IsItemActive();
            PushBlocker(titleBarBlockers_);
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            // Hover / held background (close → red-ish). A held button darkens
            // slightly so the press reads even though the action waits for release.
            if (hov || held) {
                ImVec4 base = (b.kind == 2) ? closeHov : btnHov;
                if (held) { base.x *= 0.8f; base.y *= 0.8f; base.z *= 0.8f; }
                dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(base));
            }
            // Windows 11 caption glyphs. Strokes are drawn as 1px FILLED rects
            // on integer pixels (no AddLine/AddRect anti-aliasing → perfectly
            // crisp). 10px box, centred on integer pixels. The close X keeps
            // AddLine (a 45° line wants AA to look smooth) but pixel-snapped.
            ImU32 g  = ImGui::ColorConvertFloat4ToU32(iconC);
            const int   box = (int)(10.0f * gs);
            const float t1  = std::max(1.0f, std::floor(gs));   // 1px @100%
            const int   icx = (int)(c.x), icy = (int)(c.y);
            const int   l = icx - box / 2, r = l + box;
            const int   tp = icy - box / 2, bt = tp + box;
            auto hline = [&](int x0, int x1, int y) {
                dl->AddRectFilled(ImVec2((float)x0, (float)y),
                                  ImVec2((float)x1, (float)y + t1), g);
            };
            auto vline = [&](int x, int y0, int y1) {
                dl->AddRectFilled(ImVec2((float)x, (float)y0),
                                  ImVec2((float)x + t1, (float)y1), g);
            };
            auto box_outline = [&](int x0, int y0, int x1, int y1) {
                hline(x0, x1, y0); hline(x0, x1, y1 - (int)t1);
                vline(x0, y0, y1); vline(x1 - (int)t1, y0, y1);
            };
            if (b.kind == 0) {                   // minimize: centred bar
                hline(l, r, icy);
            } else if (b.kind == 1) {            // maximize / restore
                if (maximized) {
                    const int o = (int)(3.0f * gs);   // back-square offset
                    // Front square.
                    box_outline(l, tp + o, r - o, bt);
                    // Back square top + right edges (lower-left hidden).
                    hline(l + o, r, tp);
                    vline(r - (int)t1, tp, bt - o);
                } else {
                    box_outline(l, tp, r, bt);
                }
            } else {                             // close: equal X (AA line)
                auto snap = [](float v){ return std::floor(v) + 0.5f; };
                float fl = snap((float)l), fr = snap((float)r);
                float ft = snap((float)tp), fb = snap((float)bt);
                dl->AddLine(ImVec2(fl, ft), ImVec2(fr, fb), g, t1);
                dl->AddLine(ImVec2(fl, fb), ImVec2(fr, ft), g, t1);
            }
            // Defer the actual window op (see Application::ProcessEvents): doing
            // it inline re-enters RenderFrame() via the resize event watch.
            if (clk) {
                if (b.kind == 0) pendingWindowOp_ = WindowOp::Minimize;
                else if (b.kind == 1)
                    // In fullscreen the button leaves fullscreen (same as F11);
                    // otherwise it toggles our maximize.
                    pendingWindowOp_ = fullscreen_ ? WindowOp::ToggleFullscreen
                                                   : WindowOp::ToggleMaximize;
                else pendingWindowOp_ = WindowOp::Close;
            }
            x += sysBtnW;
        }
    };

    // Left edge where content starts (after the macOS button group, if any).
    float leftX  = kButtonsLeft ? sysGroupW + pad.x : pad.x;
    float rightX = kButtonsLeft ? winW - pad.x : winW - sysGroupW;  // content right limit

    if (kButtonsLeft) drawSystemButtons(0.0f);

    // ── Logo dropdown (left): app icon → splash / about menu ─────────────────
    {
        ImGui::SetCursorPos(ImVec2(leftX, pad.y));
        UI::DropdownConfig cfg;
        cfg.id          = "##logoMenu";
        cfg.style       = UI::DropdownStyle::Minimal;
        cfg.triggerIcon = "logo_carto";
        cfg.triggerLabel = "";
        cfg.items = {
            { "image", "Show splash screen", "", 0, true },
            { "",      "About Carto",        "", 0, true },
        };
        UI::DropdownResult r = UI::Dropdown(cfg);
        PushBlocker(titleBarBlockers_);
        if (r.changed) {
            if (r.selected == 0) { showSplash_ = true; splashJustOpened_ = true; }
            else if (r.selected == 1) showAbout_ = true;
        }
        leftX = ImGui::GetItemRectMax().x - vp->Pos.x + pad.x;
    }

    // ── File / Edit / Windows menus (reused from the old main menu bar) ───────
    auto sc = [&](const char* id) { return sm.GetShortcutString(id); };
    auto menu = [&](const char* id, const char* label,
                    const std::vector<UI::DropdownItem>& items) -> int {
        ImGui::SetCursorPos(ImVec2(leftX, pad.y));
        UI::DropdownConfig cfg;
        cfg.id = id; cfg.triggerLabel = label; cfg.items = items;
        cfg.style = UI::DropdownStyle::Minimal;
        UI::DropdownResult r = UI::Dropdown(cfg);
        PushBlocker(titleBarBlockers_);
        leftX = ImGui::GetItemRectMax().x - vp->Pos.x + pad.x;
        return r.changed ? r.selected : -1;
    };
    // Build a menu row for a registered action: label + bound shortcut + the
    // action's description as a dwell tooltip (Lot 4 — menus reflect actions).
    auto item = [&](const char* icon, const char* actionId,
                    const char* fallbackLabel) {
        const Shortcuts::Action* a = sm.GetAction(actionId);
        UI::DropdownItem it;
        it.icon     = icon;
        it.label    = (a && !a->name.empty()) ? a->name : fallbackLabel;
        it.shortcut = sc(actionId);
        if (a) it.tooltip = a->description;
        return it;
    };

    int fileSel = menu("##fileMenu", "File", {
        item("new",  "file.new",    "New"),
        item("open", "file.open",   "Open"),
        item("save", "file.save",   "Save"),
        item("save", "file.saveAs", "Save As"),
        [&]{ UI::DropdownItem it; it.icon = "image"; it.label = "Update Thumbnail";
             it.tooltip = "Regenerate the .acu thumbnail from the current page"; return it; }(),
        item("close", "app.quit",   "Quit"),
    });
    if (fileSel == 0) Action_NewFile();
    else if (fileSel == 1) Action_OpenFile();
    else if (fileSel == 2) Action_SaveFile();
    else if (fileSel == 3) Action_SaveFileAs();
    else if (fileSel == 4) Action_UpdateThumbnail();
    else if (fileSel == 5) Action_Quit();

    int editSel = menu("##editMenu", "Edit", {
        { "settings", "Settings", sc("app.toggleSettings").c_str() },
    });
    if (editSel == 0) Action_ToggleSettings();

    int winSel = menu("##windowsMenu", "Windows", {
        { "checklist", "Dev Test Window", "" },
        { "",          "Design System",   "" },
        { "",          "ImGui Demo", sc("view.toggleDemo").c_str() },
    });
    if (winSel == 0) showDevWindow_ = !showDevWindow_;
    else if (winSel == 1) showDesignSystem_ = !showDesignSystem_;
    else if (winSel == 2) Action_ToggleImGuiDemo();

    // ── System buttons pinned to the right (nothing else on the right now) ────
    // The former Test dropdown + project-tab [title][+] were removed; the project
    // name is shown centred below. The right content limit is just left of the
    // system-button group so the centred title clamps against it.
    if (!kButtonsLeft) drawSystemButtons(winW - sysGroupW);

    // ── Centred project name (only when a project is actually open) ───────────
    // A plain label drawn on the bar's draw list — NOT a widget, so the area
    // stays a native window-drag handle (no hit-test blocker). Hidden if it
    // would overlap the left menus or the right project tab.
    if (!project_.name.empty()) {
        std::string centerTitle = project_.name;
        if (project_.dirty) centerTitle += " *";
        ImVec2 ts = ImGui::CalcTextSize(centerTitle.c_str());
        float cx = vp->Pos.x + (winW - ts.x) * 0.5f;
        float cy = vp->Pos.y + (barH - ts.y) * 0.5f;
        // Available gap between the left content (leftX) and the right content
        // (rightX), both in window space → convert to screen by adding vp->Pos.x.
        float gapL = vp->Pos.x + leftX + pad.x;
        float gapR = vp->Pos.x + rightX - pad.x;
        if (cx >= gapL && cx + ts.x <= gapR) {
            ImU32 tc = ImGui::ColorConvertFloat4ToU32(textC);
            ImGui::GetWindowDrawList()->AddText(ImVec2(cx, cy), tc,
                                                centerTitle.c_str());
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();    // WindowBg
    ImGui::PopStyleVar(4);     // Rounding, BorderSize, WindowPadding, WindowMinSize
}

// ── Cede the title bar to floating ImGui windows drawn on top of it ───────────
// The SDL hit-test only knows the title bar's own widget blockers. A floating
// window (Dev Test Window, ImGui Demo, a moved popup) covering part of the band
// is otherwise invisible to it, so a click there grabs the native title bar
// instead of the floating window. Run AFTER every window is submitted: walk the
// frame's window list and, for each top-level, visible, draggable window that
// overlaps the band, push its intersection as an extra hit-test blocker — the
// existing HitTestCallback then returns NORMAL there, letting ImGui handle it.
void Application::PublishOverlayTitleBarBlockers() {
    const float barH = titleBarHeightPx_;
    if (barH <= 0.0f) return;

    ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 off = ImGui::GetMainViewport()->Pos;   // screen → window space

    for (ImGuiWindow* w : g.Windows) {
        if (!w->WasActive || w->Hidden || w->Collapsed) continue;
        // Only real, movable top-level windows — skip children, tooltips, the
        // title bar itself, and the full-screen host/layout chrome (which start
        // below the band anyway and must stay draggable where they're empty).
        if (w->ParentWindow != nullptr) continue;
        if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip))
            continue;
        const char* name = w->Name ? w->Name : "";
        if (std::strncmp(name, "##TitleBar", 10) == 0)   continue;
        if (std::strncmp(name, "##MainLayout", 12) == 0) continue;

        // Intersect the window rect with the title-bar band, in window space.
        float x0 = w->Pos.x - off.x;
        float y0 = w->Pos.y - off.y;
        float x1 = x0 + w->Size.x;
        float y1 = y0 + w->Size.y;
        float bx0 = std::max(x0, 0.0f);
        float by0 = std::max(y0, 0.0f);
        float bx1 = x1;
        float by1 = std::min(y1, barH);
        if (bx1 <= bx0 || by1 <= by0) continue;   // no overlap with the band

        titleBarBlockers_.push_back(SDL_Rect{
            (int)bx0, (int)by0, (int)(bx1 - bx0), (int)(by1 - by0) });
    }
}

} // namespace App

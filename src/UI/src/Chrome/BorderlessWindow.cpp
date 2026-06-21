#include <UI/Chrome/BorderlessWindow.h>

namespace UI {

namespace {
// SDL window property carrying the controller pointer, so the bar content can
// resolve it from just the SDL_Window*.
constexpr const char* kPropController = "carto.borderless.controller";
} // namespace

// ── Bind: publish ourselves on the window + install the hit-test ──────────────
void BorderlessWindowController::Bind(SDL_Window* win,
                                      const float* titleBarHeightPx) {
    window_           = win;
    titleBarHeightPx_ = titleBarHeightPx;
    // Fresh window → fresh state (a recreated OS window starts non-maximized).
    maximized_  = false;
    fullscreen_ = false;
    programmaticMove_ = false;
    restoreRect_ = SDL_Rect{0, 0, 0, 0};
    if (!window_) return;
    SDL_SetPointerProperty(SDL_GetWindowProperties(window_), kPropController,
                           this);
    SDL_SetWindowHitTest(window_, &BorderlessWindowController::HitTest, this);
}

BorderlessWindowController* BorderlessWindowController::FromWindow(
    SDL_Window* win) {
    if (!win) return nullptr;
    return static_cast<BorderlessWindowController*>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(win), kPropController,
                               nullptr));
}

// ── User actions ──────────────────────────────────────────────────────────────
void BorderlessWindowController::Minimize() {
    if (window_) SDL_MinimizeWindow(window_);
}

void BorderlessWindowController::ToggleMaximizeOrFullscreen() {
    // In fullscreen the maximize button leaves fullscreen (same as F11);
    // otherwise it toggles our usable-bounds maximize.
    if (fullscreen_) ToggleFullscreen();
    else             SetMaximized(!maximized_);
}

// F11: borderless fullscreen-desktop (current resolution, no mode switch). The
// title bar stays visible — it is drawn by us, not the OS.
void BorderlessWindowController::ToggleFullscreen() {
    if (!window_) return;
    fullscreen_ = !fullscreen_;
    // The mode must be NULL *before* we enter, or SDL keeps the windowed size
    // and offsets it to the top-left corner.
    SDL_SetWindowFullscreenMode(window_, nullptr);
    SDL_SetWindowFullscreen(window_, fullscreen_);
}

// ── SDL event forwards ────────────────────────────────────────────────────────
// A caption double-click or drag-to-top makes Windows maximize the window
// (HTCAPTION is hard-wired to that, and our hit-test reports the bar as a
// caption). The OS maximize covers the taskbar, so we always cancel it and
// substitute our own state. This is also the ONLY reliable signal for a caption
// double-click: on a DRAGGABLE area the native move grabs the mouse at
// button-down, so neither ImGui nor SDL_PollEvent ever sees the click. So we
// treat this as a TOGGLE: if already filled (our maximize or fullscreen), the
// double-click means "restore"; otherwise it means "maximize".
void BorderlessWindowController::OnOsMaximized() {
    if (!window_) return;
    if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED)) return;
    SDL_RestoreWindow(window_);            // cancel the OS maximize (taskbar band)
    if (fullscreen_) ToggleFullscreen();   // double-click while fullscreen → exit
    else             SetMaximized(!maximized_);
}

// Dragging a maximized window restores it (Windows behaviour). The native
// HTCAPTION drag captures the mouse before we see it, so we react to the move
// here. Our own maximize/restore moves set programmaticMove_, which we skip. In
// fullscreen we do NOT restore on drag (a fullscreen-desktop window can't be
// moved) — F11 / double-click / the restore button leave fullscreen instead.
void BorderlessWindowController::OnWindowMoved() {
    if (programmaticMove_ || !maximized_ || fullscreen_) return;
    RestoreFromDragAtCursor();
}

// ── Taskbar-aware maximize (usable bounds, never SDL_MaximizeWindow) ──────────
void BorderlessWindowController::SetMaximized(bool on) {
    if (!window_ || on == maximized_) return;
    // Our own SDL_SetWindowPosition/Size below emit SDL_EVENT_WINDOW_MOVED; the
    // move handler must ignore those, or it would immediately "restore" the
    // window we are in the middle of maximizing.
    programmaticMove_ = true;
    if (on) {
        SDL_GetWindowPosition(window_, &restoreRect_.x, &restoreRect_.y);
        SDL_GetWindowSize(window_, &restoreRect_.w, &restoreRect_.h);
        SDL_Rect ub{};
        SDL_DisplayID disp = SDL_GetDisplayForWindow(window_);
        if (disp == 0) disp = SDL_GetPrimaryDisplay();
        if (!SDL_GetDisplayUsableBounds(disp, &ub)) { programmaticMove_ = false; return; }
        SDL_SetWindowPosition(window_, ub.x, ub.y);
        SDL_SetWindowSize(window_, ub.w, ub.h);
        maximized_ = true;
    } else {
        if (restoreRect_.w > 0 && restoreRect_.h > 0) {
            SDL_SetWindowPosition(window_, restoreRect_.x, restoreRect_.y);
            SDL_SetWindowSize(window_, restoreRect_.w, restoreRect_.h);
        }
        maximized_ = false;
    }
    programmaticMove_ = false;
}

// Restore a maximized window mid-drag while keeping the grabbed bar point under
// the cursor. A plain SetMaximized(false) snaps the window back to its old
// restore position, yanking it away from the still-running native drag. Instead
// we measure the cursor's fractional X across the maximized window, drop to the
// restored size, then place the window so that same fraction lands under it.
void BorderlessWindowController::RestoreFromDragAtCursor() {
    if (!maximized_) return;
    if (restoreRect_.w <= 0 || restoreRect_.h <= 0) { SetMaximized(false); return; }

    // Global cursor + the maximized window's current rect (before restoring).
    float gx = 0.0f, gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    SDL_Rect cur{};
    SDL_GetWindowPosition(window_, &cur.x, &cur.y);
    SDL_GetWindowSize(window_, &cur.w, &cur.h);
    float fx = (cur.w > 0) ? (gx - (float)cur.x) / (float)cur.w : 0.5f;

    programmaticMove_ = true;
    SDL_SetWindowSize(window_, restoreRect_.w, restoreRect_.h);
    int newX = (int)(gx - fx * (float)restoreRect_.w);
    int newY = cur.y;   // top stays put (the bar sits at the window top)
    SDL_SetWindowPosition(window_, newX, newY);
    maximized_ = false;
    programmaticMove_ = false;
}

// ── SDL hit-test: drag on the empty title-bar band, resize on the borders ─────
SDL_HitTestResult SDLCALL BorderlessWindowController::HitTest(
    SDL_Window* win, const SDL_Point* area, void* data) {
    auto* self = static_cast<BorderlessWindowController*>(data);
    int w = 0, h = 0; SDL_GetWindowSize(win, &w, &h);

    // Resize borders, disabled while maximized or fullscreen (no phantom edges).
    if (!self->maximized_ && !self->fullscreen_) {
        const int b = 6;
        const bool L = area->x < b, R = area->x >= w - b;
        const bool T = area->y < b, B = area->y >= h - b;
        if (T && L) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (T && R) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (B && L) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (B && R) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (T) return SDL_HITTEST_RESIZE_TOP;
        if (B) return SDL_HITTEST_RESIZE_BOTTOM;
        if (L) return SDL_HITTEST_RESIZE_LEFT;
        if (R) return SDL_HITTEST_RESIZE_RIGHT;
    }

    // In fullscreen the bar is NOT draggable: a fullscreen-desktop window can't
    // be meaningfully moved, and reporting it as a caption lets Windows start a
    // native move that slides the window off without changing state. Leave
    // fullscreen via F11 or the restore button instead.
    if (self->fullscreen_) return SDL_HITTEST_NORMAL;

    // Title-bar band: draggable, EXCEPT the right-hand system-button area. The
    // bar's own buttons are submitted as ImGui items and ImGui keeps the mouse,
    // so a press there is handled before the OS drag starts on release.
    const float barH = self->titleBarHeightPx_ ? *self->titleBarHeightPx_ : 0.0f;
    if ((float)area->y < barH) {
        // Right ~3 control-height slots are the min/max/close buttons.
        if (area->x < w - (int)(barH * 3.0f))
            return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

} // namespace UI

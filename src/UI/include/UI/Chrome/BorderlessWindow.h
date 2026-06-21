#pragma once

#include <SDL3/SDL.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Behaviour of a custom (borderless) title bar, factored out of its content.
//
//  Every window in the app is borderless: the OS draws no caption, so the
//  min / maximize-restore / close buttons and the whole window behaviour are
//  ours. That BEHAVIOUR is identical for the main window and every detached
//  SecondaryWindow — only the bar CONTENT differs (menus, title, icons). This
//  controller owns the behaviour so it is defined ONCE:
//
//    • Taskbar-aware maximize. We never call SDL_MaximizeWindow on a borderless
//      window — SDL's Windows backend reports the FULL monitor for that, which
//      covers the taskbar and paints a coloured band over it. Instead we size
//      the window to the display's USABLE bounds and remember the prior rect.
//    • Intercept the OS maximize. A caption double-click / drag-to-top is
//      hard-wired by Windows to a native maximize (our hit-test reports the bar
//      as HTCAPTION). We cancel it (SDL_RestoreWindow) and substitute our own
//      usable-bounds state — this is also the ONLY reliable signal for a caption
//      double-click, since the native move grabs the mouse at button-down.
//    • Drag-to-restore. Dragging a maximized window restores it under the cursor
//      (Windows behaviour), keeping the grabbed point of the bar under the mouse.
//    • F11 borderless-desktop fullscreen (the bar stays visible — we draw it).
//    • The SDL hit-test: title-bar background = draggable, borders = resize
//      (disabled while maximized / fullscreen), interactive widgets = normal.
//
//  The bar content (which runs with only the SDL_Window*) drives the controller
//  via FromWindow(win): the instance is published as an SDL window property, so
//  no owning pointer needs to be threaded through the content callback.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

class BorderlessWindowController {
public:
    // Bind the controller to its (already-created) borderless window and install
    // the SDL hit-test. `titleBarHeightPx` is read live each hit-test via the
    // supplied pointer so the bar can republish its height every frame.
    void Bind(SDL_Window* win, const float* titleBarHeightPx);

    // ── State queries (used by the bar to pick the maximize vs restore glyph) ──
    bool IsMaximized()  const { return maximized_; }
    bool IsFullscreen() const { return fullscreen_; }

    // ── User actions (called from the bar's system buttons) ───────────────────
    void Minimize();
    // Maximize button: leaves fullscreen if in it, else toggles our maximize.
    void ToggleMaximizeOrFullscreen();
    void ToggleFullscreen();

    // ── SDL events to forward from the window's event handler ─────────────────
    // A caption double-click / drag-to-top fired SDL_EVENT_WINDOW_MAXIMIZED.
    void OnOsMaximized();
    // The window moved; if it was a USER drag of a maximized window, restore it.
    void OnWindowMoved();

    // Resolve the controller bound to `win` (or nullptr), for the bar content.
    static BorderlessWindowController* FromWindow(SDL_Window* win);

private:
    void SetMaximized(bool on);
    void RestoreFromDragAtCursor();
    static SDL_HitTestResult SDLCALL HitTest(SDL_Window* win,
                                             const SDL_Point* area, void* data);

    SDL_Window*  window_ = nullptr;
    const float* titleBarHeightPx_ = nullptr;   // live bar height (window px)
    bool         maximized_  = false;
    bool         fullscreen_ = false;
    // Set while WE move/resize the window, so the WINDOW_MOVED handler ignores
    // our own moves and only restores on a genuine user drag.
    bool         programmaticMove_ = false;
    SDL_Rect     restoreRect_{0, 0, 0, 0};      // pre-maximize rect to restore to
};

} // namespace UI

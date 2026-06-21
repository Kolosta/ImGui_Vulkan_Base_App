#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <UI/Chrome/BorderlessWindow.h>
#include <functional>
#include <string>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  A REAL, separate OS window driven by a content callback (à la Blender).
//
//  Generic host for any detached window — Preferences today, and tomorrow a
//  second-screen editor zone, a dev console, a render window, etc. Each instance
//  owns its own borderless SDL_Window + Vulkan swapchain + a SECOND ImGui
//  context (the only way to draw several independent OS windows in one frame
//  without ImGui multi-viewport). The Vulkan instance / device / queue / pools
//  are SHARED with the main window (passed in at Init) — only the surface,
//  swapchain and per-window frame data are its own.
//
//  Settings stay perfectly in sync with the main window with no per-window code:
//  every frame RenderFrame copies the MAIN context's live ImGuiStyle + default
//  font (anti-aliasing, font size, theme colours…). DesignSystem::ApplyGlobal
//  Style writes to the main context (see DesignSystem::SetMainImGuiContext), so
//  one apply reaches all windows dynamically.
//
//  The window is borderless with a custom title bar + an SDL hit-test, exactly
//  like the main window: native drag / resize / snap, custom min/max/close.
//
//  Lifecycle: created lazily on first Show(); the OS window + swapchain are
//  created/destroyed by Show(true/false). Init() only records the shared Vulkan
//  handles + config and creates the 2nd ImGui context (kept for the session).
//
//  Adding a new detached window: instantiate a SecondaryWindow, call Init with a
//  Config{title,w,h} and a ContentFn lambda, then route events to it
//  (HandleEvent), reconcile Show() with your intent, and call RenderFrame()
//  after the main window's frame. (Application keeps a list of these.)
// ─────────────────────────────────────────────────────────────────────────────
class SecondaryWindow {
public:
    // Shared, app-owned Vulkan handles (NOT owned here).
    struct VulkanShared {
        VkInstance       instance       = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice         device         = VK_NULL_HANDLE;
        VkQueue          queue          = VK_NULL_HANDLE;
        uint32_t         queueFamily    = 0;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        uint32_t         minImageCount  = 2;
    };

    // Per-window configuration: OS window title + initial LOGICAL size (scaled
    // by dpiScale at creation). Title also names the SDL window in the taskbar.
    struct Config {
        std::string title  = "Window";
        int         width  = 940;   // logical px (×dpiScale at creation)
        int         height = 660;
    };

    // Draw the window content (called inside this window's ImGui frame). The
    // bool* is the "open" flag: clearing it requests the window to close.
    using ContentFn = std::function<void(bool* open)>;

    // Optional shortcut hooks, run in THIS window's ImGui context so the shared
    // ShortcutManager/EventNormalizer see this window's IO and hover. `pre` runs
    // right after NewFrame (drain IO + BeginFrame); `post` runs after the
    // content (dispatch, with the per-window focus gate). `focused` tells the
    // post-step whether this window holds keyboard focus.
    using ShortcutPreFn  = std::function<void()>;
    using ShortcutPostFn = std::function<void(bool focused)>;

    SecondaryWindow() = default;
    ~SecondaryWindow();

    // Record shared handles + config and create the secondary ImGui context.
    bool Init(const VulkanShared& shared, float dpiScale, const Config& cfg,
              ContentFn content);
    void Shutdown();

    // Install the per-frame shortcut hooks (optional). See ShortcutPreFn/PostFn.
    void SetShortcutHooks(ShortcutPreFn pre, ShortcutPostFn post) {
        shortcutPre_ = std::move(pre); shortcutPost_ = std::move(post);
    }

    // Show / hide the OS window. Creating it lazily on first Show keeps startup
    // cheap and avoids a second swapchain when the window is never opened.
    void Show(bool on);
    bool IsOpen() const { return open_; }

    // SDL focus state of the OS window (false when not yet created).
    bool HasInputFocus() const;   // keyboard focus (active window)
    bool HasMouseFocus() const;   // pointer is over this window

    // Ask the window to come to the front + take focus. Deferred to the next
    // Show()/reconcile pass by the owner via ConsumeFocusRequest (the SDL calls
    // must run outside the ImGui frame, like Show()).
    void RequestFocus() { focusRequested_ = true; }
    bool ConsumeFocusRequest() {
        bool f = focusRequested_; focusRequested_ = false; return f;
    }
    // Raise + focus the OS window now (call OUTSIDE the ImGui frame).
    void FocusNow();

    // True once if the window asked to close itself (title-bar close button or
    // the OS close box) since the last call. The owner reads this to clear its
    // own "show" intent. Resets the flag.
    bool ConsumeCloseRequest() {
        bool c = closeRequested_; closeRequested_ = false; return c;
    }

    // Route one SDL event to THIS window if it targets it. Returns true when
    // the event belonged to (and was consumed for) this window.
    bool HandleEvent(const SDL_Event& ev);

    // Drive the borderless behaviour (intercept OS-maximize / restore-on-drag)
    // for a window event targeting THIS window. MUST be called from the SDL
    // event WATCH, not the poll loop: the OS modal drag loop blocks
    // SDL_PollEvent, so a maximized window's WINDOW_MOVED only reaches us live
    // (while the cursor is still aligned) through the watch — the same reason
    // the main window handles it there. No-op for events not ours.
    void HandleWindowChromeEvent(const SDL_Event& ev);

    // Render one full frame of this window (no-op when hidden). Switches to the
    // secondary ImGui context, builds the UI, submits + presents its swapchain,
    // and restores the previous context.
    void RenderFrame();

private:
    void CreateOsWindow();
    void DestroyOsWindow();
    void CreateOrResizeSwapchain(int w, int h);
    void RenderTitleBarAndContent();   // custom bar + content, in this context

    VulkanShared shared_{};
    float        dpiScale_ = 1.0f;
    Config       config_{};
    ContentFn    content_;
    ShortcutPreFn  shortcutPre_;
    ShortcutPostFn shortcutPost_;

    ImGuiContext* ctx_      = nullptr;   // secondary context (owned)
    ImGuiContext* prevCtx_  = nullptr;   // saved while we are current
    SDL_Window*   window_   = nullptr;   // OS window (owned)
    bool          open_     = false;     // logical visibility
    bool          osCreated_= false;     // swapchain + backends live
    bool          swapRebuild_ = false;
    bool          rendering_ = false;     // re-entrancy guard for RenderFrame()
    bool          closeRequested_ = false;// window asked to close itself
    bool          focusRequested_ = false;// raise+focus asked (deferred)

    ImGui_ImplVulkanH_Window wd_{};      // this window's swapchain/frames

    // Borderless window behaviour (maximize/restore/fullscreen/hit-test), shared
    // with the main window — bar CONTENT is per-window, behaviour is not.
    UI::BorderlessWindowController chrome_;

    // Title bar / hit-test state (window-space px), like the main window.
    float titleBarHeightPx_ = 0.0f;
    // close requested by our title-bar button this frame.
    bool  pendingClose_ = false;
};

} // namespace App

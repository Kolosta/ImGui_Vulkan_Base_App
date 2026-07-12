#include "Application.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <SDL3/SDL_filesystem.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Text/FontManager.h>

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif
#include <imgui_impl_sdl3.h>

namespace App {

static VkAllocationCallbacks* g_Allocator      = nullptr;
static uint32_t               g_MinImageCount  = 2;
static bool                   g_SwapChainRebuild = false;

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

Application* Application::s_instance_ = nullptr;

Application::Application()  { s_instance_ = this; }
Application::~Application() { if (s_instance_ == this) s_instance_ = nullptr; }

void Application::RenderFrame() {
    // ProcessEvents (the SDL event pump) runs UNGUARDED: during the OS modal
    // resize loop, Windows pumps events from inside here, and the SDL watch
    // calls RenderFrame() to keep the window live. If the guard covered the
    // pump, that nested call would bail and the window would freeze (black /
    // crop until release) — the resize regression. So only the ImGui section
    // (Update/Render/Present + secondaries) is guarded against true nesting
    // (a watch firing WHILE we are mid-NewFrame would corrupt ImGui state,
    // worse with the Preferences window's second context).
    ProcessEvents();

    if (inRenderFrame_) return;   // a nested frame is already mid-render → skip
    inRenderFrame_ = true;
    Update();
    Render();
    Present();
    // Every detached window renders in its OWN ImGui context + swapchain, after
    // the main window's frame is submitted/presented. (Open/close intent is
    // reconciled in ProcessEvents via ConsumeCloseRequest()/Show().)
    for (SecondaryWindow* w : secondaryWindows_)
        w->RenderFrame();
    inRenderFrame_ = false;
}

void Application::Run() {
    while (running_) {
        RenderFrame();
    }
}

void Application::ProcessEvents() {
    // ── Pump events ALWAYS (even re-entrantly during the OS modal resize loop,
    //    where the SDL watch calls RenderFrame→ProcessEvents). Pumping is safe
    //    re-entrantly; the SWAPCHAIN/window-op work below is NOT — recreating
    //    the swapchain or toggling the window while the parent frame is mid
    //    Update/Render corrupts Vulkan/ImGui (the 0xc000041d crash). So those
    //    are guarded by inRenderFrame_ (true only inside the parent's render
    //    section) and skipped on a re-entrant pump.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Events targeting a detached window are routed to ITS ImGui context;
        // everything else goes to the main window's context.
        bool consumed = false;
        for (SecondaryWindow* w : secondaryWindows_)
            if (w->HandleEvent(event)) { consumed = true; break; }
        if (consumed) continue;
        ImGui_ImplSDL3_ProcessEvent(&event);
        // Modal-transform mouse capture (SDL relative mode): accumulate the RAW
        // relative motion. This is the transform's only motion source — no
        // absolute positions, no warps → no drift (ViewportModal.cpp).
        if (modalRelMode_ && event.type == SDL_EVENT_MOUSE_MOTION) {
            modalRelAccum_.x += event.motion.xrel;
            modalRelAccum_.y += event.motion.yrel;
        }
        if (event.type == SDL_EVENT_QUIT)
            running_ = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window_))
            running_ = false;
    }

    // Re-entrant pump (called from the watch while the parent frame renders):
    // do NOT touch the swapchain / window state — just having pumped is enough
    // to keep input flowing; the parent frame will resize/render.
    if (inRenderFrame_) return;

    // Apply any window op requested by a title-bar system button LAST frame.
    // Done here — outside the ImGui NewFrame/Render span — because these SDL
    // calls fire a synchronous PIXEL_SIZE_CHANGED that re-enters RenderFrame().
    if (pendingWindowOp_ != WindowOp::None) {
        WindowOp op = pendingWindowOp_;
        pendingWindowOp_ = WindowOp::None;
        switch (op) {
            case WindowOp::Minimize: SDL_MinimizeWindow(window_); break;
            case WindowOp::ToggleMaximize: SetMaximized(!maximized_); break;
            case WindowOp::ToggleFullscreen: ToggleFullscreen(); break;
            case WindowOp::Close: running_ = false; break;
            default: break;
        }
    }

    // Apply any file open/save chosen via the async dialog (its callback only
    // stashed the path; the load/save runs here, on the main thread, outside
    // the ImGui frame — loading replaces the document AND the zone layout).
    ProcessPendingFileOp();

    // Preferences open/close, applied OUTSIDE the ImGui frame: creating/showing
    // the OS window fires SDL window events that re-enter RenderFrame via the
    // event watch, which must not happen mid-NewFrame.
    //   1. If the window closed itself (close button / OS close), drop intent.
    //   2. Then reconcile the OS window with the desired state.
    if (settingsHost_.ConsumeCloseRequest())
        showSettings_ = false;
    if (settingsHost_.IsOpen() != showSettings_)
        settingsHost_.Show(showSettings_);
    // Raise/focus requests (e.g. the toggle shortcut hit while Settings was
    // behind) — done here, outside the ImGui frame, like Show().
    if (settingsHost_.ConsumeFocusRequest())
        settingsHost_.FocusNow();

    // Token Graph window: same open/close/focus reconciliation as Preferences.
    if (tokenGraphHost_.ConsumeCloseRequest())
        showTokenGraph_ = false;
    if (tokenGraphHost_.IsOpen() != showTokenGraph_)
        tokenGraphHost_.Show(showTokenGraph_);
    if (tokenGraphHost_.ConsumeFocusRequest())
        tokenGraphHost_.FocusNow();

    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        return;
    }

    int fb_width, fb_height;
    SDL_GetWindowSize(window_, &fb_width, &fb_height);
    if (fb_width > 0 && fb_height > 0 &&
        (g_SwapChainRebuild ||
         mainWindowData_.Width  != fb_width ||
         mainWindowData_.Height != fb_height))
    {
        ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            instance_, physicalDevice_, device_, &mainWindowData_,
            queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
        mainWindowData_.FrameIndex = 0;
        g_SwapChainRebuild = false;
    }
}

void Application::Update() {
    VectorGraphics::IconManager::Instance().CleanupCacheIfNeeded();

    // Let the active module keep the document invariants it owns (IOF: each page's
    // shapes in print-layer z-order) before any UI / viewport reads the document.
    if (activeModule_) activeModule_->OnFrameSync();

    // Reset component-usage tracking at the start of every frame, so the
    // Tokens viewer reads the previous frame's counts cleanly without
    // unbounded growth. ComponentScope RAII populates it as widgets render.
    DesignSystem::DesignSystem::Instance().ResetUsage();

    // Re-resolve the default font from design-system tokens. Guarded inside
    // (only rebuilds when family/weight changed), so font-family / font-weight
    // token edits in the Token editor take effect live.
    ApplyFontTokens();

    // No font atlas rebuild needed in imgui 1.92+: glyphs are rasterised
    // lazily at the exact size driven by style.FontSizeBase/FontScaleMain/FontScaleDpi
    // (set by DesignSystem::ApplyGlobalStyle).

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // While a modal transform grabs the mouse (SDL relative mode), the OS
    // still reports an absolute position that walks off the canvas — ImGui
    // would then hover the Outliner / other editors under a cursor that has
    // VISUALLY wrapped back inside the viewport. Pin ImGui's mouse to the
    // displayed (wrapped) position so hover/hit-testing matches what the user
    // sees. Done right after NewFrame, before any widget reads io.MousePos.
    if (transformOp_.Active() && modalRelMode_) {
        const ImVec2 wrapped = WrapPointInCanvas(
            ImVec2((float)transformOp_.virtPx.x, (float)transformOp_.virtPx.y));
        ImGui::GetIO().MousePos = wrapped;
    }

    // Shortcut pipeline:
    //   1. drain ImGui IO into normalised events
    //   2. reset per-frame context (BeginFrame); panels then call
    //      RegisterRegionContext from inside their hovered window
    //   3. UI renders (panels register their context this frame)
    //   4. ProcessInput dispatches events with the now-up-to-date context
    {
        // Pull the drag threshold from the design system every frame so DS
        // overrides take effect immediately without restarting the app.
        try {
            float t = DesignSystem::DesignSystem::Instance()
                        .GetFloat(DesignSystem::Tok::S_Config_DragThreshold);
            Shortcuts::EventNormalizer::Instance().SetDragThreshold(t);
        } catch (...) { /* token missing — keep current value */ }
        // Undo history depth (Preferences ▸ General). Applied live so a change
        // takes effect without restarting. (Only the Preferences history exists
        // during the Ink rework — see PrefsUndo.cpp.)
        try {
            int n = DesignSystem::DesignSystem::Instance()
                        .GetInt(DesignSystem::Tok::S_Config_UndoSteps);
            if (n != undoBufferSteps_) { undoBufferSteps_ = n; prefsUndo_.SetCapacity(n); }
        } catch (...) { /* token missing — keep current value */ }
    }
    Shortcuts::EventNormalizer::Instance().Frame();
    Shortcuts::ShortcutManager::Instance().BeginFrame();

    // Open the Ink frame BEFORE the UI is built: each Viewport zone acquires
    // and configures its Ink::View during the build; EndFrame below records
    // and submits the canvas work (docs/Ink/RENDER_GRAPH.md).
    if (ink_) ink_->BeginFrame();

    // Reset the per-frame hovered-viewport pointer; a Viewport leaf sets it
    // again while building if the mouse is over it (Lot 8 keyboard actions
    // target the hovered leaf).
    hoveredViewport_ = nullptr;
    if (!transformOp_.Active()) osCursorHidden_ = false;

    RenderTitleBar();      // publishes titleBarHeightPx_ + blockers first
    RenderMainLayout();    // viewports render their offscreen canvas here
    // Viewport popups (Add / right-click context) — rendered ONCE here, after
    // the whole layout, so their BeginPopup is called every frame from a stable
    // place regardless of which zone the cursor is over (fixes the freeze).
    RenderAddMenu();
    RenderViewportContextMenu();
    RenderHandleTypeMenu();
    RenderFloatingWindows();
    RenderSplash();        // start screen overlay (and the logo-menu re-open)
    RenderAbout();         // "About Carto" popup
    RenderUnsavedDialog(); // "Unsaved changes" guard for splash New File presets

    // Hide the OS cursor while a modal transform runs (a custom cursor is drawn
    // in Vulkan). Forcing ImGui's cursor to None LAST in the frame — after all
    // widgets have had their say — is the legacy-proven, reliable way (the SDL
    // backend then hides it; a bare SDL_HideCursor is undone by ImGui's own
    // per-frame cursor update).
    if (osCursorHidden_ && transformOp_.Active())
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    // Object eyedropper: hide the OS cursor and draw the "colorize" glyph on the
    // ImGui FOREGROUND list — the exact same technique as the transform cursor
    // (SetMouseCursor(None) + IconManager on the foreground list, tinted by
    // C_Cursor_Color, Multicolor scheme). The eyedropper points at its tip, so
    // the glyph is anchored by its bottom-left corner on the mouse position.
    // Esc cancels.
    if (ObjectPickActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        auto& im = VectorGraphics::IconManager::Instance();
        if (im.HasIcon("colorize")) {
            auto& ds = DesignSystem::DesignSystem::Instance();
            const float sz = 28.0f * ds.GetGlobalScale();
            const ImVec2 m = ImGui::GetIO().MousePos;
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec4 col = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
            auto md = im.GetDefaultMetadata("colorize");
            md.scheme = VectorGraphics::IconColorScheme::Multicolor;
            for (auto& z : md.colorZones) z.customColor = col;
            im.RenderIcon(fg, "colorize", ImVec2(m.x, m.y - sz), sz, md);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) CancelObjectPick();
    }

    // With every floating window now submitted, register the ones overlapping the
    // title bar as hit-test blockers so clicking them grabs the floating window,
    // not the native title bar.
    PublishOverlayTitleBarBlockers();

    // An armed .acu save renders its page thumbnail through THIS frame: the
    // off-screen view is set up here, recorded by EndFrame, read back and
    // written to disk right after (ProjectIO.cpp).
    PrepareSavePass();

    // Close the Ink frame: record every dirty view through the render graph
    // and submit. Same queue as the main pass — the graph's final barriers
    // order the canvas writes before ImGui's sampling, no semaphore needed.
    if (ink_) ink_->EndFrame();

    FinishSavePass();

    // Dispatch happens after panels have set the context for this frame so
    // editor/region/tool match the user's current hover. Gate global actions on
    // the MAIN window's keyboard focus (Blender-style): when another window/app
    // is focused, only context-scoped actions fire over the hovered zone.
    {
        auto& sm = Shortcuts::ShortcutManager::Instance();
        const bool mainFocused =
            window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS);
        activeUndoTarget_ = UndoTarget::Viewport;   // Ctrl+Z here = document undo
        sm.SetWindowFocused(mainFocused);
        sm.ProcessInput();
    }
}

void Application::Render() {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (is_minimized) return;

    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec4 clear_color = ds.GetColor(DesignSystem::Tok::S_Color_Background_Default);
    mainWindowData_.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
    mainWindowData_.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
    mainWindowData_.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
    mainWindowData_.ClearValue.color.float32[3] = clear_color.w;

    VkSemaphore image_acquired_semaphore =
        mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore =
        mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;

    VkResult err = vkAcquireNextImageKHR(
        device_, mainWindowData_.Swapchain, UINT64_MAX,
        image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

    ImGui_ImplVulkanH_Frame* fd = &mainWindowData_.Frames[mainWindowData_.FrameIndex];
    {
        err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        check_vk_result(err);
        err = vkResetFences(device_, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(device_, fd->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType  = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass               = mainWindowData_.RenderPass;
        info.framebuffer              = fd->Framebuffer;
        info.renderArea.extent.width  = mainWindowData_.Width;
        info.renderArea.extent.height = mainWindowData_.Height;
        info.clearValueCount          = 1;
        info.pClearValues             = &mainWindowData_.ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        // Wait on the swapchain image acquire (colour output) only. The Ink
        // canvas work submitted earlier this frame needs no semaphore: its
        // final barriers order the writes before ImGui's fragment sampling on
        // this same queue (docs/Ink/RENDER_GRAPH.md §5).
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount   = 1;
        info.pWaitSemaphores      = &image_acquired_semaphore;
        info.pWaitDstStageMask    = &wait_stage;
        info.commandBufferCount   = 1;
        info.pCommandBuffers      = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores    = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(queue_, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

void Application::Present() {
    if (g_SwapChainRebuild) return;

    VkSemaphore render_complete_semaphore =
        mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores    = &render_complete_semaphore;
    info.swapchainCount     = 1;
    info.pSwapchains        = &mainWindowData_.Swapchain;
    info.pImageIndices      = &mainWindowData_.FrameIndex;

    VkResult err = vkQueuePresentKHR(queue_, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

    mainWindowData_.SemaphoreIndex =
        (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;

    // Rendu des fenêtres additionnelles (multi-écrans / fenêtre Preferences
    // détachée). Activé via ImGuiConfigFlags_ViewportsEnable.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        // Safety net: UpdatePlatformWindows() asserts PlatformIO.Monitors is
        // non-empty. The SDL3 backend skips any display whose DPI scale is 0
        // (some Windows configs / virtual displays report 0), which can leave
        // the list empty and crash the instant a window is dragged out into its
        // own viewport. Rebuild a minimal monitor list from SDL if that happens.
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        if (pio.Monitors.Size == 0) {
            int n = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&n);
            for (int i = 0; i < n; ++i) {
                SDL_Rect r{};
                if (!SDL_GetDisplayBounds(displays[i], &r)) continue;
                ImGuiPlatformMonitor m;
                m.MainPos  = m.WorkPos  = ImVec2((float)r.x, (float)r.y);
                m.MainSize = m.WorkSize = ImVec2((float)r.w, (float)r.h);
                SDL_Rect ur{};
                if (SDL_GetDisplayUsableBounds(displays[i], &ur) && ur.w > 0 && ur.h > 0) {
                    m.WorkPos  = ImVec2((float)ur.x, (float)ur.y);
                    m.WorkSize = ImVec2((float)ur.w, (float)ur.h);
                }
                float dpi = SDL_GetDisplayContentScale(displays[i]);
                m.DpiScale = dpi > 0.0f ? dpi : 1.0f;   // never 0 (would be skipped)
                m.PlatformHandle = (void*)(intptr_t)i;
                pio.Monitors.push_back(m);
            }
            if (displays) SDL_free(displays);
        }
        if (pio.Monitors.Size > 0) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

// ── File actions: the .acu project lifecycle ──────────────────────────────────
// Open/save/save-as and the two-phase save pass live in ProjectIO.cpp
// (.acu v2 — docs/acu-format.md, docs/Ink/ROADMAP.md Lot 10). Here: the
// project/document creation flows they chain into.

void Application::Action_NewFile() {
    // Menu "New" = a fresh Classic project with no module. If a module is active,
    // drop to Classic (applyLayout=true deactivates it + restores the workspace);
    // if already Classic, keep the current zone arrangement (applyLayout=false).
    DoNewFile(LayoutPreset::General, /*applyLayout=*/activeModule_ != nullptr);
}

// Fresh document into the project + hand it to the Ink engine. Transitional:
// seeds the demo content so the Viewport shows something until the drawing
// tools land (docs/Ink/ROADMAP.md Lot 8).
void Application::ResetDocument() {
    project_.Reset();
    // Fresh editing state for the new document (Lot 8).
    edit_.Clear();
    edit_.mode = EditorMode::Object;
    // Seed the 2D cursor at the first page centre.
    if (project_.document && !project_.document->Pages().empty()) {
        const Ink::Page& pg = project_.document->Pages().front();
        edit_.cursor2D = { pg.pos.x + pg.size.x * 0.5, pg.pos.y + pg.size.y * 0.5 };
        edit_.cursor2DValid = true;
    }
    docUndo_.Clear();
    transformOp_ = TransformOp{};
    canvasDrag_  = CanvasDrag{};
    addMenuOpen_ = false;
    viewportCtxOpen_ = false;
    osCursorHidden_ = false;   // (cursor visibility is driven from Update)
    if (ink_) {
        Ink::SeedDemoDocument(*project_.document);
        ink_->SetDocument(project_.document.get());
    }
}

// Create a brand-new project and, optionally, switch the zone layout to
// `preset`. Shared by the menu New, the splash presets, and (with
// applyLayout=false) the module-open flow which supplies its own layout next.
void Application::DoNewFile(LayoutPreset preset, bool applyLayout) {
    ResetDocument();
    if (applyLayout) {
        if (activeModule_) { activeModule_->OnDeactivate(); activeModule_ = nullptr; }
        activeCapabilities_ = Modules::Capabilities{};   // Classic = full defaults
        zoneLayout_.SetEditorFilter(CoreEditor::Ids());  // Classic picker = core only
        zoneLayout_.ApplyPreset(preset);
    }
    LogInfoAction("New Project");
}

// Splash "New File <preset>": guard unsaved changes first. If the project is
// dirty, open the Save / Don't Save / Cancel dialog and remember the preset;
// otherwise create the new project immediately. Always applies the layout.
void Application::RequestNewFile(LayoutPreset preset) {
    pendingNewPreset_ = preset;
    pendingModuleId_.clear();               // preset path (not a module open)
    pendingOpenPath_.clear();               // and not a held file open
    if (project_.dirty) {
        unsavedDialogOpen_ = true;          // RenderUnsavedDialog() opens the modal
    } else {
        CommitPendingNew();
    }
}

// ── Recent files (splash start screen) ────────────────────────────────────────
// Persisted in the OS user-prefs folder (SDL_GetPrefPath), one path per line,
// most-recent first — distinct from the project's working-dir state files.
std::string Application::RecentFilesPath() const {
    char* pref = SDL_GetPrefPath("Carto", "Carto");   // creates the dir if needed
    std::string p = pref ? std::string(pref) + "recent.txt" : std::string();
    if (pref) SDL_free(pref);
    return p;
}

void Application::LoadRecentFiles() {
    recentFiles_.clear();
    const std::string file = RecentFilesPath();
    if (file.empty()) return;
    std::ifstream in(file);
    std::string line;
    std::error_code ec;
    while (std::getline(in, line) && recentFiles_.size() < kMaxRecentFiles) {
        if (line.empty()) continue;
        // Drop entries whose file no longer exists, so the splash never lists
        // dead paths.
        if (std::filesystem::exists(line, ec)) recentFiles_.push_back(line);
    }
}

void Application::SaveRecentFiles() const {
    const std::string file = RecentFilesPath();
    if (file.empty()) return;
    std::ofstream out(file, std::ios::trunc);
    for (const std::string& p : recentFiles_) out << p << '\n';
}

void Application::AddRecentFile(const std::string& path) {
    if (path.empty()) return;
    // Move-to-front with de-duplication, then cap the list.
    recentFiles_.erase(std::remove(recentFiles_.begin(), recentFiles_.end(), path),
                       recentFiles_.end());
    recentFiles_.insert(recentFiles_.begin(), path);
    if (recentFiles_.size() > kMaxRecentFiles) recentFiles_.resize(kMaxRecentFiles);
    SaveRecentFiles();
}


} // namespace App

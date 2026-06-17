#include "Application.h"
#include "ProjectFile.h"
#include "ModuleRegistry.h"   // restore a file's module on Load
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <Renderer/Tessellation/Tessellator.h>
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

static VkAllocationCallbacks* g_Allocator      = nullptr;
static uint32_t               g_MinImageCount  = 2;
static bool                   g_SwapChainRebuild = false;

namespace App {

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
        // takes effect without restarting.
        try {
            int n = DesignSystem::DesignSystem::Instance()
                        .GetInt(DesignSystem::Tok::S_Config_UndoSteps);
            if (n != undoBufferSteps_) { undoBufferSteps_ = n; undo_.SetCapacity(n); }
        } catch (...) { /* token missing — keep current value */ }
    }
    Shortcuts::EventNormalizer::Instance().Frame();
    Shortcuts::ShortcutManager::Instance().BeginFrame();
    // Publish this frame's contextual status-bar hints right after BeginFrame
    // cleared them and before the status bar is built in RenderMainLayout. Uses
    // the persistent transform/mode state (a frame's lag at most).
    PublishStatusHints();

    // Begin the vector-renderer frame BEFORE the UI is built: each Viewport's
    // RenderViewport() renders its canvas into an offscreen texture (its own
    // Vulkan render pass) during the UI-build phase — all of which completes
    // before ImGui::Render() and the main swapchain pass in Render().
    canvasRenderer_.BeginFrame();

    // Clear the shared drop-preview when no move gesture is in flight, so it
    // doesn't linger after a drop. While a move IS active it persists (the owner
    // viewport refreshes it during RenderMainLayout); all viewports read the
    // same value this frame.
    {
        const bool moveActive =
            (toolState_.gesture == ToolGesture::MoveObjects) ||
            (transformOp_.Active() && !transformOp_.element &&
             transformOp_.kind == TransformKind::Move);
        if (!moveActive) dropPreview_.active = false;
    }

    RenderTitleBar();      // publishes titleBarHeightPx_ + blockers first
    RenderMainLayout();    // viewports render their offscreen canvas here
    RenderFloatingWindows();
    RenderSplash();        // start screen overlay (and the logo-menu re-open)
    RenderAbout();         // "About Carto" popup
    RenderUnsavedDialog(); // "Unsaved changes" guard for splash New File presets

    // With every floating window now submitted, register the ones overlapping the
    // title bar as hit-test blockers so clicking them grabs the floating window,
    // not the native title bar.
    PublishOverlayTitleBarBlockers();

    // Evict offscreen targets whose Viewport zone no longer exists this frame.
    canvasRenderer_.EndFrame();

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

    // Push an undo step for any document change that finished this frame (once
    // no gesture is mid-flight, so each completed operation is one step).
    CommitUndoIfPending();
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
        // Wait on the swapchain image acquire (colour output) AND on every offscreen
        // canvas view rendered this frame (fragment shader, where ImGui samples those
        // textures). The latter replaces the per-view CPU fence stall in RenderView
        // with a pure GPU dependency, so the CPU never blocks on the offscreen work.
        std::vector<VkSemaphore> waits;
        std::vector<VkPipelineStageFlags> waitStages;
        waits.push_back(image_acquired_semaphore);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        for (VkSemaphore s : canvasRenderer_.FrameWaitSemaphores()) {
            waits.push_back(s);
            waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        VkSubmitInfo info = {};
        info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount   = (uint32_t)waits.size();
        info.pWaitSemaphores      = waits.data();
        info.pWaitDstStageMask    = waitStages.data();
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
// The SDL file dialogs are ASYNCHRONOUS and their callback may run on another
// thread, so the callback only stashes the chosen path in pendingFile_ (guarded
// by a mutex). ProcessPendingFileOp(), called from ProcessEvents outside the
// ImGui frame, performs the actual load/save on the main thread.

static const SDL_DialogFileFilter kAcuFilter[] = {
    { "Carto project (*.acu)", "acu" },
    { "All files",             "*"   },
};

void Application::Action_NewFile() {
    // Menu "New" = a fresh Classic project with no module. If a module is active,
    // drop to Classic (applyLayout=true deactivates it + restores the workspace);
    // if already Classic, keep the current zone arrangement (applyLayout=false).
    DoNewFile(LayoutPreset::General, /*applyLayout=*/activeModule_ != nullptr);
}

// Create a brand-new project (one default page) and, optionally, switch the zone
// layout to `preset`. Shared by the menu New, the splash presets, and (with
// applyLayout=false) the module-open flow which supplies its own layout next.
// A splash preset (applyLayout=true) returns to the Classic workspace, leaving
// any active module; the menu New (applyLayout=false) keeps the current module.
void Application::DoNewFile(LayoutPreset preset, bool applyLayout) {
    project_.Reset();
    objectModeTool_ = "tool.select"; editToolByObject_.clear();  // fresh tool memory
    project_.AddArtboard("Page 1", ImVec2(0, 0), ImVec2(1920, 1080));
    project_.dirty = false;
    ResetUndoHistory();   // baseline = the new empty project
    if (applyLayout) {
        if (activeModule_) { activeModule_->OnDeactivate(); activeModule_ = nullptr; }
        activeCapabilities_ = Modules::Capabilities{};   // Classic = full defaults
        zoneLayout_.SetEditorFilter(CoreEditor::Ids());  // Classic picker = core only
        zoneLayout_.ApplyPreset(preset);
    }
}

// Splash "New File <preset>": guard unsaved changes first. If the project is
// dirty, open the Save / Don't Save / Cancel dialog and remember the preset;
// otherwise create the new project immediately. Always applies the layout.
void Application::RequestNewFile(LayoutPreset preset) {
    pendingNewPreset_ = preset;
    pendingModuleId_.clear();               // preset path (not a module open)
    if (project_.dirty) {
        unsavedDialogOpen_ = true;          // RenderUnsavedDialog() opens the modal
    } else {
        CommitPendingNew();
    }
}

void Application::Action_OpenFile() {
    SDL_ShowOpenFileDialog(
        [](void* ud, const char* const* files, int /*filter*/) {
            auto* self = static_cast<Application*>(ud);
            if (files && files[0]) {
                std::lock_guard<std::mutex> lk(self->pendingFile_.mtx);
                self->pendingFile_.kind = 1;          // open
                self->pendingFile_.path = files[0];
            }
        },
        this, window_, kAcuFilter, 2, nullptr, /*allow_many=*/false);
}

void Application::Action_SaveFile() {
    if (project_.path.empty()) { Action_SaveFileAs(); return; }
    if (project_.thumbnailPng.empty()) Action_UpdateThumbnail();  // default Page-1 aperçu
    SyncSettingsToProject();                 // persist the live menu-bar settings
    if (App::ProjectFile::Save(project_.path, project_, zoneLayout_))
        project_.dirty = false;
}

void Application::Action_SaveFileAs() {
    SDL_ShowSaveFileDialog(
        [](void* ud, const char* const* files, int /*filter*/) {
            auto* self = static_cast<Application*>(ud);
            if (files && files[0]) {
                std::lock_guard<std::mutex> lk(self->pendingFile_.mtx);
                self->pendingFile_.kind = 2;          // save-to-path
                self->pendingFile_.path = files[0];
            }
        },
        this, window_, kAcuFilter, 2, nullptr);
}

void Application::SyncSettingsToProject() {
    EditorSettings& e = project_.editorSettings;
    e.pivotMode        = (int)pivotMode_;
    e.transformOrient  = (int)transformOrientation_;
    e.show2DCursor     = show2DCursor_;
    e.showMetrics      = showMetrics_;
    e.snapEnabled      = snap_.enabled;
    e.snapMode         = (int)snap_.mode;
    e.snapBase         = (int)snap_.base;
    e.snapAffectMove   = snap_.affectMove;
    e.snapAffectRotate = snap_.affectRotate;
    e.snapAffectScale  = snap_.affectScale;
    e.snapRotIncrement = snap_.rotIncrement;
    e.snapRotPrecision = snap_.rotPrecisionIncrement;
    // Per-mode tool memory. Persist the CURRENT mode's live tool too so a save
    // mid-session keeps the right tool for the active mode/object.
    const std::string curTool = Shortcuts::Tools::ToolManager::Instance().GetActiveTool();
    if (editorMode_ == EditorMode::Object) objectModeTool_ = curTool;
    else if (uint64_t id = EditToolObject())  editToolByObject_[id] = curTool;
    e.objectModeTool   = objectModeTool_;
    e.editToolByObject = editToolByObject_;
    e.defaultFill[0]=defaultFill_.r; e.defaultFill[1]=defaultFill_.g;
    e.defaultFill[2]=defaultFill_.b; e.defaultFill[3]=defaultFill_.a;
    e.defaultStroke[0]=defaultStroke_.r; e.defaultStroke[1]=defaultStroke_.g;
    e.defaultStroke[2]=defaultStroke_.b; e.defaultStroke[3]=defaultStroke_.a;
}

void Application::ApplySettingsFromProject() {
    const EditorSettings& e = project_.editorSettings;
    auto clampEnum = [](int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); };
    pivotMode_           = (PivotMode)clampEnum(e.pivotMode, 0, 4);
    transformOrientation_= (TransformOrientation)clampEnum(e.transformOrient, 0, 4);
    show2DCursor_        = e.show2DCursor;
    showMetrics_         = e.showMetrics;
    snap_.enabled        = e.snapEnabled;
    snap_.mode           = (SnapSettings::Mode)clampEnum(e.snapMode, 0, 5);
    snap_.base           = (SnapSettings::Base)clampEnum(e.snapBase, 0, 3);
    snap_.affectMove     = e.snapAffectMove;
    snap_.affectRotate   = e.snapAffectRotate;
    snap_.affectScale    = e.snapAffectScale;
    snap_.rotIncrement   = e.snapRotIncrement;
    snap_.rotPrecisionIncrement = e.snapRotPrecision;
    // Per-mode tool memory. Apply the tool the loaded mode/object should show.
    objectModeTool_   = e.objectModeTool.empty() ? "tool.select" : e.objectModeTool;
    editToolByObject_ = e.editToolByObject;
    std::string want = objectModeTool_;
    if (editorMode_ == EditorMode::Edit) {
        uint64_t id = EditToolObject();
        auto it = id ? editToolByObject_.find(id) : editToolByObject_.end();
        want = (it != editToolByObject_.end()) ? it->second : "tool.select";
    }
    if (!want.empty()) Shortcuts::Tools::ToolManager::Instance().SetActiveTool(want);
    defaultFill_   = { e.defaultFill[0],   e.defaultFill[1],   e.defaultFill[2],   e.defaultFill[3]   };
    defaultStroke_ = { e.defaultStroke[0], e.defaultStroke[1], e.defaultStroke[2], e.defaultStroke[3] };
}

void Application::ProcessPendingFileOp() {
    int kind = 0; std::string path;
    {
        std::lock_guard<std::mutex> lk(pendingFile_.mtx);
        kind = pendingFile_.kind; path = pendingFile_.path;
        pendingFile_.kind = 0; pendingFile_.path.clear();
    }
    if (kind == 0) return;

    if (kind == 1) {                                   // open
        if (App::ProjectFile::Load(path, project_, zoneLayout_)) {
            // Derive a display name from the file stem if META had none.
            if (project_.name.empty()) {
                size_t slash = path.find_last_of("/\\");
                size_t dot   = path.find_last_of('.');
                std::string stem = path.substr(
                    slash == std::string::npos ? 0 : slash + 1,
                    (dot == std::string::npos ? path.size() : dot) -
                    (slash == std::string::npos ? 0 : slash + 1));
                project_.name = stem;
            }
            ApplySettingsFromProject();  // restore the file's menu-bar settings
            ResetUndoHistory();    // baseline = the just-loaded document
            AddRecentFile(path);   // remember in the splash recent list
            // Restore the file's module (Classic if it has none). A file can't
            // switch modules afterwards — its workspace follows the document.
            // Keep the loaded layout (rebuildLayout=false): it's authoritative.
            ActivateModule(Modules::ModuleRegistry::Instance().Get(project_.moduleId),
                           /*rebuildLayout=*/false);
        }
    } else if (kind == 2) {                            // save-to-path
        // Ensure the .acu extension if the user didn't type one.
        if (path.find_last_of('.') == std::string::npos ||
            path.size() < 4 || path.substr(path.size() - 4) != ".acu")
            path += ".acu";
        // Give every saved .acu a default thumbnail (Page 1) if none exists yet.
        if (project_.thumbnailPng.empty()) Action_UpdateThumbnail();
        SyncSettingsToProject();             // persist the live menu-bar settings
        if (App::ProjectFile::Save(path, project_, zoneLayout_)) {
            project_.path  = path;
            project_.dirty = false;
            size_t slash = path.find_last_of("/\\");
            size_t dot   = path.find_last_of('.');
            project_.name = path.substr(
                slash == std::string::npos ? 0 : slash + 1,
                (dot == std::string::npos ? path.size() : dot) -
                (slash == std::string::npos ? 0 : slash + 1));
            AddRecentFile(path);   // remember in the splash recent list
            // If this Save-As was the "Save" branch of the unsaved-changes dialog
            // for a new-file request, create the new project now that the save
            // committed (the dialog was async, so it could only resolve here).
            if (newFileAfterSave_) {
                newFileAfterSave_ = false;
                CommitPendingNew();   // preset OR module, per the pending intent
            }
        }
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

void Application::Action_Zone1()    { std::cout << "[ACTION] Zone 1"    << std::endl; }
void Application::Action_Zone2()    { std::cout << "[ACTION] Zone 2"    << std::endl; }
void Application::Action_ThemePreviewCycle() {
    std::cout << "[ACTION] Theme Preview Cycle" << std::endl;
}

void Application::Action_Quit() {
    std::cout << "[ACTION] Quit" << std::endl;
    running_ = false;
}
void Application::Action_ToggleSettings() {
    // If Settings is already open but sits BEHIND (not keyboard-focused), the
    // shortcut should bring it to the front + focus rather than close it — only
    // toggle when it is closed or already the active window. The SDL raise is
    // deferred to ProcessEvents (outside the ImGui frame) via the focus request.
    if (showSettings_ && settingsHost_.IsOpen() && !settingsHost_.HasInputFocus()) {
        settingsHost_.RequestFocus();
        return;
    }
    // Only flip the desired state here (this runs inside the main ImGui frame).
    // The actual Show()/Hide() — which creates the OS window and fires SDL
    // window events that re-enter RenderFrame via the event watch — is deferred
    // to ProcessEvents(), OUTSIDE the NewFrame/Render span.
    showSettings_ = !showSettings_;
}
void Application::Action_ToggleImGuiDemo() {
    showImGuiDemo_ = !showImGuiDemo_;
}
void Application::Action_ActivateNamedTool(const std::string& toolId) {
    // Switching tools cancels any half-finished gesture so the new tool starts
    // clean (e.g. abandoning a polyline mid-placement).
    toolState_.Reset();
    // Extrude is an Edit-Mode tool: it authors geometry into the active object, so
    // it enters Edit Mode even with no selection (unlike Tab, which needs a
    // selected obj). Curve works in BOTH modes now, so it stays in the current one.
    const EditorMode prevMode = editorMode_;
    if (toolId == "tool.extrude" && editorMode_ != EditorMode::Edit) {
        // Save the Object-mode tool before forcing Edit (so leaving Edit restores it).
        objectModeTool_ = Shortcuts::Tools::ToolManager::Instance().GetActiveTool();
        editorMode_ = EditorMode::Edit;
        project_.document.ClearVertSelection();
        editDrag_.Reset();
    }
    // Mark selection only makes sense under the Line-Mark tool; leaving it clears
    // the selection (and any in-progress mark op) so other tools' Properties show.
    if (toolId != "tool.linemark") {
        project_.document.ClearMarkSelection();
        markGrab_.Reset(); markBox_ = {}; markDrag_ = {};
    }
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool(toolId);
    // Record this as the chosen tool for the CURRENT mode so a later mode switch
    // restores it (Object Mode → objectModeTool_; Edit Mode → per active object).
    if (editorMode_ == EditorMode::Object) {
        objectModeTool_ = toolId;
    } else {
        if (uint64_t id = EditToolObject()) editToolByObject_[id] = toolId;
    }
    (void)prevMode;
}
void Application::Action_NewDocument() {
    // Open the New Artboard popup in the Viewport leaf the mouse is over
    // (no-op if none). The popup adds an artboard to the shared project.
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->openNewDoc = true;
}
void Application::Action_NewProject() {
    // Reset to a brand-new empty project (no artboard). Shared by every
    // Viewport and the Outliner. File save/open comes later.
    project_.Reset();
    objectModeTool_ = "tool.select"; editToolByObject_.clear();  // fresh tool memory
    ResetUndoHistory();
    std::cout << "[ACTION] New Project" << std::endl;
}
void Application::Action_ViewFitDocument() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqFitDoc = true;
}
void Application::Action_ViewFitSelection() {
    // Numpad . — frame the selected/active object(s) in the hovered viewport, OR,
    // if the Outliner is hovered, scroll/recentre it on the active object.
    if (EditorState* st = zoneLayout_.HoveredEditorState()) {
        if (project_.document.HasSelection() || project_.document.ActiveId())
            st->reqFitSelection = true;
        st->outliner.reqScrollToActive = true;   // Outliner leaves act on this
    }
}
void Application::Action_ViewResetOrigin() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqResetOrigin = true;
}
uint64_t Application::EditToolObject() const {
    uint64_t id = project_.document.ActiveId();
    if (!id && !project_.document.Selection().empty())
        id = project_.document.Selection().front();
    return id;
}

// After a mode switch, save the tool of the mode we LEFT (`prevMode`) into its
// per-mode memory, then restore the tool the NEW mode should show. Object Mode has
// one remembered tool; Edit Mode remembers per edited object (defaulting to Select).
void Application::SyncToolForMode(EditorMode prevMode) {
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    const std::string cur = tm.GetActiveTool();
    if (prevMode == editorMode_) return;     // nothing changed

    if (prevMode == EditorMode::Object) {
        objectModeTool_ = cur;               // remember what Object Mode was using
    } else {
        if (uint64_t id = EditToolObject()) editToolByObject_[id] = cur;
    }

    std::string want;
    if (editorMode_ == EditorMode::Object) {
        want = objectModeTool_.empty() ? "tool.select" : objectModeTool_;
    } else {
        uint64_t id = EditToolObject();
        auto it = id ? editToolByObject_.find(id) : editToolByObject_.end();
        want = (it != editToolByObject_.end()) ? it->second : "tool.select";
    }
    // Apply directly (NOT Action_ActivateNamedTool — that would re-enter the mode
    // logic and re-save/clobber; here we only set the ToolManager + clear gestures).
    if (!want.empty() && want != cur) {
        toolState_.Reset();
        if (want != "tool.linemark") {
            project_.document.ClearMarkSelection();
            markGrab_.Reset(); markBox_ = {}; markDrag_ = {};
        }
        tm.SetActiveTool(want);
    }
}

void Application::Action_ToggleEditMode() {
    // Tab toggles Object ⇄ Edit. Entering Edit needs at least one selected
    // object; bake its parametric parts so vertices show immediately.
    const EditorMode prevMode = editorMode_;
    if (editorMode_ == EditorMode::Object) {
        if (project_.document.HasSelection()) {
            editorMode_ = EditorMode::Edit;
            for (uint64_t sid : project_.document.Selection())
                if (Renderer::Shape* s = project_.document.FindShape(sid)) s->EnsurePath();
            project_.document.ClearVertSelection();
        }
    } else {
        editorMode_ = EditorMode::Object;
    }
    SyncToolForMode(prevMode);   // save/restore the per-mode tool
    toolState_.Reset();
    editDrag_.Reset();
    // The editor mode is in the undo snapshot → make the toggle its own step.
    MarkUndoLabel(editorMode_ == EditorMode::Edit ? "Enter Edit Mode"
                                                  : "Enter Object Mode");
    LogInfoActionRich(editorMode_ == EditorMode::Edit ? "Enter Edit Mode"
                                                      : "Enter Object Mode",
                  std::string("mode=") +
                  (editorMode_ == EditorMode::Edit ? "EDIT" : "OBJECT"));
}

void Application::Action_DeleteSelection() {
    // During a modal G/R/S, X/Y are the AXIS CONSTRAINT (handled in
    // UpdateTransformOp), not Delete — so X must not also delete the selection.
    if (transformOp_.Active()) return;
    // Line-mark tool with marks selected → delete those marks (quasi-objects).
    if (Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.linemark" &&
        project_.document.HasMarkSelection()) {
        DeleteSelectedMarks();
        return;
    }
    // X deletes the right thing for the current mode: whole objects in Object
    // mode, selected vertices/edges/faces in Edit mode.
    if (editorMode_ == EditorMode::Edit) { Action_DeleteElements(); return; }
    auto ids = project_.document.Selection();   // copy: EraseShape mutates it
    if (ids.empty()) return;
    MarkUndoLabel("Delete");
    for (uint64_t id : ids) project_.document.EraseShape(id);
    project_.document.ClearSelection();
    project_.dirty = true;
}

void Application::Action_HideSelection() {
    // H (Object mode): hide the selected objects. Hidden objects don't render and
    // aren't pickable IN THE VIEWPORT, but stay SELECTABLE/active from the
    // Outliner (Blender-style) — so the selection (and the active element) is
    // kept. Transforms skip hidden objects; the Outliner shows a closed eye.
    if (editorMode_ != EditorMode::Object) return;
    auto ids = project_.document.Selection();
    if (ids.empty()) return;
    MarkUndoLabel("Hide");
    for (uint64_t id : ids)
        if (Renderer::Shape* s = project_.document.FindShape(id)) s->visible = false;
    project_.dirty = true;
}

void Application::Action_RevealAll() {
    // Alt+H: reveal every hidden object in the document (Blender-style).
    MarkUndoLabel("Reveal Hidden");
    project_.document.RevealAllShapes();
    project_.dirty = true;
}

void Application::Action_ParentSelection() {
    // Ctrl+P: parent every OTHER selected object to the ACTIVE one. The active
    // object is the parent; the others become its children (cycles refused by
    // Document::SetParent). Objects stay visually put — parenting only affects
    // FUTURE motion. Needs ≥2 selected and an active object.
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    uint64_t parent = doc.ActiveId();
    if (!parent || doc.Selection().size() < 2) return;
    bool any = false;
    for (uint64_t id : doc.Selection()) {
        if (id == parent) continue;
        if (doc.SetParent(id, parent)) any = true;
    }
    if (any) { MarkUndoLabel("Parent"); project_.dirty = true; }
}

void Application::Action_ClearParent() {
    // Alt+P: clear the parent of every selected object (keeps them visually put;
    // they simply stop following their former parent).
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    bool any = false;
    for (uint64_t id : doc.Selection())
        if (doc.ClearParent(id)) any = true;
    if (any) { MarkUndoLabel("Clear Parent"); project_.dirty = true; }
}

// ── Selection families ────────────────────────────────────────────────────────
namespace {
// Approximate colour equality (per channel) for "Select Color".
bool ColorNear(const Renderer::Color& a, const Renderer::Color& b) {
    const float e = 0.02f;
    return std::fabs(a.r - b.r) < e && std::fabs(a.g - b.g) < e &&
           std::fabs(a.b - b.b) < e && std::fabs(a.a - b.a) < e;
}
}

void Application::Action_SelectGrouped(GroupedMode mode) {
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    Renderer::Shape* act = doc.FindShape(doc.ActiveId());
    if (!act) return;
    const uint64_t activeId = act->id;

    // Snapshot the selection BEFORE applying, so the operator panel can re-run with
    // a different mode without compounding (restore base → re-apply). Captured once
    // per fresh invocation (a panel re-run reuses the same base via the closure).
    std::vector<uint64_t> base(doc.Selection().begin(), doc.Selection().end());

    // Visit every object in the document.
    auto forEach = [&](const std::function<void(Renderer::Shape&)>& fn) {
        for (Renderer::Artboard& ab : doc.artboards)
            for (Renderer::Shape& s : ab.shapes) fn(s);
        for (Renderer::Shape& s : doc.looseShapes) fn(s);
    };
    std::vector<uint64_t> add;
    auto pick = [&](uint64_t id){ if (id) add.push_back(id); };

    switch (mode) {
        case GroupedMode::Children:
            for (uint64_t d : doc.DescendantsOf(activeId)) pick(d);
            break;
        case GroupedMode::ImmediateChildren:
            for (uint64_t c : doc.ChildrenOf(activeId)) pick(c);
            break;
        case GroupedMode::Parent:
            pick(act->parentId);
            break;
        case GroupedMode::Siblings: {
            uint64_t par = act->parentId;     // 0 → root-level objects (no parent)
            forEach([&](Renderer::Shape& s){ if (s.parentId == par) pick(s.id); });
            break;
        }
        case GroupedMode::Type: {
            Renderer::PartType fam = act->Family();
            forEach([&](Renderer::Shape& s){ if (s.Family() == fam) pick(s.id); });
            break;
        }
        case GroupedMode::Collection: {
            uint64_t coll = act->collectionId;
            forEach([&](Renderer::Shape& s){ if (s.collectionId == coll) pick(s.id); });
            break;
        }
        case GroupedMode::Color: {
            if (act->Empty()) break;
            Renderer::Color fc = act->MainPart().fill.color;
            Renderer::Color sc = act->MainPart().stroke.color;
            forEach([&](Renderer::Shape& s){
                if (s.Empty()) return;
                if (ColorNear(s.MainPart().fill.color, fc) &&
                    ColorNear(s.MainPart().stroke.color, sc)) pick(s.id);
            });
            break;
        }
    }
    for (uint64_t id : add) if (Renderer::Shape* s = doc.FindShape(id); s && s->visible)
        doc.SelectAdd(id);
    doc.SetActive(activeId);                 // keep the original active object
    MarkUndoLabel("Select Grouped");

    // Publish the operator to the redo panel with the mode as an adjustable enum,
    // so the user can switch Children/Parent/Type/Color… after the fact (Blender).
    OperatorRecord op;
    op.active = true;
    op.title  = "Select Grouped";
    OperatorParam p;
    p.label = "Type"; p.kind = OperatorParam::Kind::Enum; p.value = (int)mode;
    p.options = { "Children","Immediate Children","Parent","Siblings",
                  "Type","Collection","Color" };
    op.params.push_back(std::move(p));
    uint64_t keepActive = activeId;
    op.rerun = [this, base, keepActive]() {
        auto& d = project_.document;
        // Restore the base selection, then re-apply with the panel's current mode.
        d.ClearSelection();
        for (uint64_t id : base) d.SelectAdd(id);
        d.SetActive(keepActive);
        int mi = lastOperator_.params.empty() ? 0 : lastOperator_.params[0].value;
        Action_SelectGrouped((GroupedMode)mi);
    };
    SetLastOperator(std::move(op));
}

void Application::Action_SelectLinked() {
    // No shared data-blocks in this model → "linked" = same geometry family + kind
    // as the active object (closest analogue to Blender's Object Data link).
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    Renderer::Shape* act = doc.FindShape(doc.ActiveId());
    if (!act || act->Empty()) return;
    Renderer::PartType fam = act->Family();
    Renderer::ShapeKind kind = act->MainPart().kind;
    auto consider = [&](Renderer::Shape& s){
        if (s.Empty() || !s.visible) return;
        if (s.Family() == fam && s.MainPart().kind == kind) doc.SelectAdd(s.id);
    };
    for (Renderer::Artboard& ab : doc.artboards) for (Renderer::Shape& s : ab.shapes) consider(s);
    for (Renderer::Shape& s : doc.looseShapes) consider(s);
    doc.SetActive(act->id);
    MarkUndoLabel("Select Linked");
}

void Application::Action_SelectMoreLess(bool grow) {
    auto& doc = project_.document;
    if (editorMode_ == EditorMode::Edit) {
        // Edit mode is handled by the element-selection grow/shrink in EditMode.cpp.
        Action_SelectMoreLessElements(grow);
        return;
    }
    // Object mode: More = add immediate parents + children of the selection; Less =
    // deselect objects that sit at a parent/child boundary of the selection.
    std::vector<uint64_t> sel(doc.Selection().begin(), doc.Selection().end());
    if (sel.empty()) return;
    auto inSel = [&](uint64_t id){ return std::find(sel.begin(), sel.end(), id) != sel.end(); };
    if (grow) {
        std::vector<uint64_t> add;
        for (uint64_t id : sel) {
            Renderer::Shape* s = doc.FindShape(id);
            if (s && s->parentId) add.push_back(s->parentId);   // parent
            for (uint64_t c : doc.ChildrenOf(id)) add.push_back(c);  // children
        }
        for (uint64_t id : add)
            if (Renderer::Shape* s = doc.FindShape(id); s && s->visible) doc.SelectAdd(id);
    } else {
        // Deselect any selected object that has a neighbour (parent or child) NOT in
        // the selection — i.e. it sits on the boundary.
        std::vector<uint64_t> remove;
        for (uint64_t id : sel) {
            Renderer::Shape* s = doc.FindShape(id);
            bool boundary = false;
            if (s && s->parentId && !inSel(s->parentId)) boundary = true;
            for (uint64_t c : doc.ChildrenOf(id)) if (!inSel(c)) { boundary = true; break; }
            if (boundary) remove.push_back(id);
        }
        // Never shrink to nothing: keep at least the active object.
        if ((int)remove.size() >= (int)sel.size()) {
            uint64_t keep = doc.ActiveId() ? doc.ActiveId() : sel.front();
            remove.erase(std::remove(remove.begin(), remove.end(), keep), remove.end());
        }
        for (uint64_t id : remove) doc.Deselect(id);
    }
    MarkUndoLabel(grow ? "Select More" : "Select Less");
}

void Application::Action_JoinSelection() {
    // Merge all selected objects into the ACTIVE one (Blender's Ctrl+J): the
    // result is ONE object holding every source object's PARTS, each keeping its
    // own geometry, fill and stroke. So a rectangle, a line and an ellipse can
    // coexist (different colours) inside a single selectable item with one
    // origin. Absorbed parts are rebased into the host's local space so the
    // picture is unchanged.
    if (editorMode_ != EditorMode::Object) return;
    auto ids = project_.document.Selection();
    if (ids.size() < 2) return;

    // Typed Join (Lot 6): you may only merge objects of the SAME family
    // (Mesh↔Mesh, or any curve-like↔curve-like). Reject a mixed selection — the
    // context menu greys the entry and offers "Convert & Join"; this guards the
    // Ctrl+J shortcut path too. (Action_ConvertAllAndJoin pre-converts, so by
    // the time it calls us the selection is single-family.)
    {
        bool haveFamily = false; Renderer::PartType fam{};
        for (uint64_t id : ids) {
            Renderer::Shape* s = project_.document.FindShape(id);
            if (!s) continue;
            Renderer::PartType f = s->Family();
            if (!haveFamily) { fam = f; haveFamily = true; }
            else if (f != fam) {
                LogInfoAction("Join cancelled: selection mixes Mesh and Curve types");
                return;
            }
        }
    }

    MarkUndoLabel("Join");
    uint64_t hostId = project_.document.ActiveId();
    Renderer::Shape* host = project_.document.FindShape(hostId);
    if (!host) return;

    // Re-express a source part's nodes from its object's WORLD space into the
    // host's local space, baking parametric parts first so we can move them.
    auto rebasePart = [&](Renderer::Shape& src, Renderer::Part part,
                          Renderer::Shape& dstHost) {
        part.EnsurePath();
        auto toHostLocal = [&](Renderer::Vec2 p) {
            return Renderer::Tessellator::InverseTransform(
                dstHost, Renderer::Tessellator::WorldTransform(src, p));
        };
        for (Renderer::Node& n : part.path.nodes) {
            n.pos = toHostLocal(n.pos);
            if (n.hasIn)  n.hIn  = toHostLocal(n.hIn);
            if (n.hasOut) n.hOut = toHostLocal(n.hOut);
        }
        return part;
    };

    // Append every other selected object's parts to the host (each keeps its
    // own geometry + fill + stroke). One object, one origin, multiple parts.
    for (uint64_t id : ids) {
        if (id == hostId) continue;
        Renderer::Shape* o = project_.document.FindShape(id);
        if (!o) continue;
        for (const Renderer::Part& part : o->parts)
            host->parts.push_back(rebasePart(*o, part, *host));
    }
    for (uint64_t id : ids) if (id != hostId) project_.document.EraseShape(id);
    project_.document.SelectOnly(hostId);
    project_.dirty = true;
}

Renderer::Vec2 Application::ComputePivot() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    const auto& sel = doc.Selection();
    if (sel.empty()) return doc.cursor;

    // Pivots are in WORLD/display space. Each object's geometry is page-relative,
    // so we must offset it by the page's DISPLAY origin for THIS viewport
    // (CurPageOriginOfShape) — the same origin UpdateTransformOp uses — otherwise
    // the pivot lands as if every object were on page 1 at (0,0): wrong as soon
    // as a page is moved, not page 1, or under an auto layout. Objects on
    // different pages each contribute at their own display position, so a
    // multi-page selection's pivot is the true visual centre/median.
    auto originWorld = [&](uint64_t id) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(id);
        return s ? Renderer::Tessellator::WorldTransform(*s, s->origin,
                                                         CurPageOriginOfShape(id))
                 : Renderer::Vec2{0, 0};
    };
    switch (pivotMode_) {
        case PivotMode::Cursor2D:
            return doc.cursor;
        case PivotMode::ActiveElement:
            return originWorld(doc.ActiveId());
        case PivotMode::IndividualOrigins:
            // Per-object pivot is applied in UpdateTransformOp; the median is a
            // sensible scalar fallback here.
        case PivotMode::MedianPoint: {
            Renderer::Vec2 sum{0, 0}; int n = 0;
            for (uint64_t id : sel) { Renderer::Vec2 o = originWorld(id); sum.x += o.x; sum.y += o.y; ++n; }
            return n ? Renderer::Vec2{ sum.x / n, sum.y / n } : doc.cursor;
        }
        case PivotMode::BoundingBoxCenter: {
            Renderer::Vec2 mn{ 1e30f, 1e30f }, mx{ -1e30f, -1e30f };
            for (uint64_t id : sel)
                if (Renderer::Shape* s = doc.FindShape(id)) {
                    bool cl = false;
                    std::vector<Renderer::Vec2> poly =
                        Renderer::Tessellator::Outline(*s, 1.0f, cl, CurPageOriginOfShape(id));
                    for (auto& p : poly) {
                        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y);
                        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y);
                    }
                }
            return { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
        }
    }
    return doc.cursor;
}

// World-space pivot for the edit-mode element selection (VERTICES + selected
// HANDLES) under pivotMode_. With a handle-only selection the pivot follows the
// selected handle endpoints (not the 2D cursor).
Renderer::Vec2 Application::ComputeVertPivot() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    auto vw = [&](const Renderer::VertRef& v) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) return {0,0};
        auto& ns = s->parts[(size_t)v.part].path.nodes;
        if (v.node >= (int)ns.size()) return {0,0};
        return Renderer::Tessellator::WorldTransform(*s, ns[(size_t)v.node].pos,
                                                     CurPageOriginOfShape(v.shape));
    };
    auto hw = [&](const Renderer::HandleRef& h) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(h.shape);
        if (!s || h.part >= (int)s->parts.size()) return {0,0};
        auto& ns = s->parts[(size_t)h.part].path.nodes;
        if (h.node >= (int)ns.size()) return {0,0};
        const Renderer::Node& n = ns[(size_t)h.node];
        return Renderer::Tessellator::WorldTransform(*s, h.outSide ? n.hOut : n.hIn,
                                                     CurPageOriginOfShape(h.shape));
    };
    // The full element point set: vertex positions + selected handle endpoints whose
    // node isn't itself a selected vertex (those move with the node).
    std::vector<Renderer::Vec2> pts;
    for (const Renderer::VertRef& v : doc.VertSelection()) pts.push_back(vw(v));
    for (const Renderer::HandleRef& h : doc.HandleSelection())
        if (!doc.IsVertSelected({ h.shape, h.part, h.node })) pts.push_back(hw(h));
    if (pts.empty()) return doc.cursor;

    if (pivotMode_ == PivotMode::Cursor2D) return doc.cursor;
    if (pivotMode_ == PivotMode::ActiveElement) {
        // Prefer the active vertex; else the active handle; else the first point.
        if (doc.ActiveVert().shape) return vw(doc.ActiveVert());
        if (doc.ActiveHandle().valid()) return hw(doc.ActiveHandle());
        return pts.front();
    }
    if (pivotMode_ == PivotMode::BoundingBoxCenter) {
        Renderer::Vec2 mn{1e30f,1e30f}, mx{-1e30f,-1e30f};
        for (const Renderer::Vec2& w : pts) {
            mn.x=std::min(mn.x,w.x); mn.y=std::min(mn.y,w.y);
            mx.x=std::max(mx.x,w.x); mx.y=std::max(mx.y,w.y); }
        return { (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
    }
    // Median (default) / IndividualOrigins → centroid of the element points.
    Renderer::Vec2 sum{0,0};
    for (const Renderer::Vec2& w : pts) { sum.x+=w.x; sum.y+=w.y; }
    return { sum.x/(float)pts.size(), sum.y/(float)pts.size() };
}

// Snapping applies when the magnet is on OR Ctrl is held, gated by the per-kind
// Affect toggle. Ctrl is the transient "snap just for this drag" Blender modifier.
bool Application::SnapActiveFor(TransformKind kind) const {
    const bool on = snap_.enabled || ImGui::GetIO().KeyCtrl;
    if (!on) return false;
    switch (kind) {
        case TransformKind::Move:   return snap_.affectMove;
        case TransformKind::Rotate: return snap_.affectRotate;
        case TransformKind::Scale:  return snap_.affectScale;
        default: return false;
    }
}

// Find the snap target for `world` under the current snap mode. Geometry modes
// (Vertex/Edge/Face/EdgeCenter) search every visible shape NOT in `exclude` and
// snap to the nearest candidate within a screen-pixel radius. Increment/Grid snap
// to the view grid (always). Returns the snapped world point + whether to draw the
// indicator (Increment never shows a mark).
Application::SnapResult Application::ComputeSnap(
        Renderer::Vec2 cursorWorld, float effZoom,
        const std::vector<uint64_t>& exclude,
        const std::vector<Renderer::Vec2>& rejectPts,
        const std::vector<Renderer::Vec2>& rejectSegs) const {
    // Snap is anchored to the CURSOR (cursorWorld), NOT the relative move amount —
    // so the target is stable per frame (no flicker) and only engages when the
    // cursor is within a screen-pixel radius of a candidate (Blender: too far → no
    // snap → normal move). Grid/Increment also gate on that radius.
    SnapResult out; out.pos = cursorWorld;
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    const float zoom = std::max(1e-4f, effZoom);
    const float kRadiusPx = 16.0f;               // snap pickup radius (screen px)
    const float kRadiusDoc = kRadiusPx / zoom;
    auto excluded = [&](uint64_t id){
        return std::find(exclude.begin(), exclude.end(), id) != exclude.end();
    };

    // Grid / Increment: round the CURSOR to the nearest grid crossing — ALWAYS snaps
    // (no radius; the grid is everywhere). Increment shows no mark.
    if (snap_.mode == SnapSettings::Mode::Grid ||
        snap_.mode == SnapSettings::Mode::Increment) {
        const float g = SnapGridStep(effZoom);
        if (g > 1e-6f) {
            out.pos = { std::round(cursorWorld.x / g) * g,
                        std::round(cursorWorld.y / g) * g };
            out.snapped = true;
            out.showMark = (snap_.mode == SnapSettings::Mode::Grid);
        }
        return out;
    }

    // Geometry modes: keep the candidate closest to the CURSOR within the radius,
    // skipping any candidate that coincides with a rejected point (the moving
    // selection's current positions) so the selection never snaps onto itself.
    const float kRejectDoc = 1.0f / zoom;        // ~1px coincidence tolerance
    auto isRejected = [&](Renderer::Vec2 p) {
        for (const Renderer::Vec2& r : rejectPts)
            if (std::hypot(p.x - r.x, p.y - r.y) < kRejectDoc) return true;
        return false;
    };
    // A flattened segment belongs to the MOVING selection when both its endpoints are
    // rejected points (a moving edge) → its projections/midpoint must be skipped, so
    // Edge/EdgeCenter never snap onto the selection itself (the edit-mode self-snap bug).
    auto segRejected = [&](Renderer::Vec2 a, Renderer::Vec2 b) {
        return isRejected(a) && isRejected(b);
    };
    // Distance from p to the segment [a,b].
    auto distToSeg = [](Renderer::Vec2 p, Renderer::Vec2 a, Renderer::Vec2 b) {
        Renderer::Vec2 ab{ b.x-a.x, b.y-a.y };
        float L2 = ab.x*ab.x + ab.y*ab.y;
        float t = L2 > 1e-9f ? std::clamp(((p.x-a.x)*ab.x + (p.y-a.y)*ab.y)/L2, 0.0f, 1.0f) : 0.0f;
        return std::hypot(p.x - (a.x+ab.x*t), p.y - (a.y+ab.y*t));
    };
    // A candidate is "on the moving selection" when it's a reject POINT, or lies on a
    // moving EDGE (rejectSegs is an explicit a,b,a,b,… list of the selection's actual
    // edges in world space, current positions). Using REAL edges — not consecutive
    // rejectPts (which form phantom segments between non-adjacent points).
    auto onSelection = [&](Renderer::Vec2 p) {
        if (isRejected(p)) return true;
        for (size_t i = 0; i + 1 < rejectSegs.size(); i += 2)
            if (distToSeg(p, rejectSegs[i], rejectSegs[i+1]) < kRejectDoc) return true;
        return false;
    };
    float bestD = kRadiusDoc; bool found = false; Renderer::Vec2 best{};
    auto consider = [&](Renderer::Vec2 p) {
        if (onSelection(p)) return;
        float d = std::hypot(p.x - cursorWorld.x, p.y - cursorWorld.y);
        if (d < bestD) { bestD = d; best = p; found = true; }
    };
    auto scanShape = [&](const Renderer::Shape& s) {
        if (!s.visible || excluded(s.id)) return;
        Renderer::Vec2 po = CurPageOriginOfShape(s.id);
        for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
            const Renderer::Part& part = s.parts[(size_t)pi];
            // VERTEX mode: only the CONTROL POINTS (path nodes for curves/paths; the
            // rectangle/ellipse corners via the baked nodes), NOT every flattened
            // outline point. We bake a copy so a parametric part still yields nodes.
            if (snap_.mode == SnapSettings::Mode::Vertex) {
                Renderer::Part baked = part; baked.EnsurePath();
                for (const Renderer::Node& nd : baked.path.nodes)
                    consider(Renderer::Tessellator::WorldTransform(s, nd.pos, po));
                continue;
            }
            // EDGE CENTER: the arc-length midpoint of each CONTROL-NODE segment (the
            // span between two consecutive nodes), NOT the midpoint of every tiny
            // flattened sub-segment (which scattered candidates along a curve). We
            // flatten per node-segment and take its 50%-arc-length point.
            if (snap_.mode == SnapSettings::Mode::EdgeCenter) {
                Renderer::Part baked = part; baked.EnsurePath();
                const int sc = std::max(1, baked.path.subCount());
                for (int spi = 0; spi < sc; ++spi) {
                    int b0 = 0, e0 = (int)baked.path.nodes.size();
                    baked.path.subRange(spi, b0, e0);
                    const bool cyc = baked.path.closed;
                    int segCount = cyc ? (e0 - b0) : (e0 - b0 - 1);
                    for (int k = 0; k < segCount; ++k) {
                        int ia = b0 + k, ib = b0 + ((k + 1) % (e0 - b0));
                        // World endpoints (node positions) — skip a moving edge.
                        Renderer::Vec2 wa = Renderer::Tessellator::WorldTransform(
                            s, baked.path.nodes[(size_t)ia].pos, po);
                        Renderer::Vec2 wb = Renderer::Tessellator::WorldTransform(
                            s, baked.path.nodes[(size_t)ib].pos, po);
                        if (segRejected(wa, wb)) continue;
                        // Mid arc-length point of this node-segment via a fine flatten.
                        const Renderer::Node& na = baked.path.nodes[(size_t)ia];
                        const Renderer::Node& nb = baked.path.nodes[(size_t)ib];
                        std::vector<Renderer::Vec2> pts{ na.pos };
                        Renderer::Tessellator::FlattenCubic(
                            na.pos, na.hasOut ? na.hOut : na.pos,
                            nb.hasIn ? nb.hIn : nb.pos, nb.pos, 24, pts);
                        float total = 0.0f;
                        for (size_t j = 1; j < pts.size(); ++j)
                            total += std::hypot(pts[j].x - pts[j-1].x, pts[j].y - pts[j-1].y);
                        float half = total * 0.5f, acc = 0.0f;
                        Renderer::Vec2 mid = na.pos;
                        for (size_t j = 1; j < pts.size(); ++j) {
                            float l = std::hypot(pts[j].x - pts[j-1].x, pts[j].y - pts[j-1].y);
                            if (acc + l >= half) { float u = l > 1e-6f ? (half - acc) / l : 0.0f;
                                mid = { pts[j-1].x + (pts[j].x - pts[j-1].x) * u,
                                        pts[j-1].y + (pts[j].y - pts[j-1].y) * u }; break; }
                            acc += l;
                        }
                        consider(Renderer::Tessellator::WorldTransform(s, mid, po));
                    }
                }
                continue;
            }
            // Edge / Face use the flattened outline (any point on the line for Edge;
            // centroid for Face).
            int subs = std::max(1, Renderer::Tessellator::SubpathCount(part));
            for (int sub = 0; sub < subs; ++sub) {
                bool closed = false;
                std::vector<Renderer::Vec2> poly =
                    Renderer::Tessellator::OutlinePartSub(s, part, sub, zoom, closed, po);
                if (poly.empty()) continue;
                const size_t n = poly.size();
                if (snap_.mode == SnapSettings::Mode::Edge) {
                    size_t sc = closed ? n : n - 1;
                    for (size_t i = 0; i < sc; ++i) {
                        Renderer::Vec2 a = poly[i], b = poly[(i+1)%n];
                        if (segRejected(a, b)) continue;   // moving edge → skip
                        Renderer::Vec2 ab{ b.x-a.x, b.y-a.y };
                        float L2 = ab.x*ab.x + ab.y*ab.y; if (L2 < 1e-9f) continue;
                        float t = std::clamp(((cursorWorld.x-a.x)*ab.x +
                                              (cursorWorld.y-a.y)*ab.y)/L2, 0.0f, 1.0f);
                        consider({ a.x + ab.x*t, a.y + ab.y*t });
                    }
                } else if (snap_.mode == SnapSettings::Mode::Face) {
                    if (closed && n >= 3) {     // face centroid
                        // Skip a MOVING face (all its outline points are on the moving
                        // selection) — else its centroid moves with the snap → flicker.
                        bool allMoving = true;
                        for (const Renderer::Vec2& v : poly)
                            if (!onSelection(v)) { allMoving = false; break; }
                        if (!allMoving) {
                            Renderer::Vec2 c{0,0};
                            for (const Renderer::Vec2& v : poly) { c.x += v.x; c.y += v.y; }
                            consider({ c.x / (float)n, c.y / (float)n });
                        }
                    }
                }
            }
        }
    };
    for (const Renderer::Artboard& ab : doc.artboards)
        for (const Renderer::Shape& s : ab.shapes) scanShape(s);
    for (const Renderer::Shape& s : doc.looseShapes) scanShape(s);
    if (found) { out.pos = best; out.snapped = true; out.showMark = true; }
    return out;
}

void Application::Action_BeginTransform(TransformKind kind) {
    // 2D Cursor tool: R rotates the 2D CURSOR (so the "Cursor" transform
    // orientation's axes can be aimed). G/S fall through to the normal ops.
    if (kind == TransformKind::Rotate &&
        Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.cursor") {
        cursorRotate_.Reset();
        cursorRotate_.active   = true;
        cursorRotate_.startRot = project_.document.cursorRotation;
        // startAngle is seeded by the first UpdateCursorRotate from the owning leaf.
        return;
    }
    // Line-mark tool: G/R/S act on the SELECTED MARKS (move along the curve / flip
    // side / scale a crossing's interval), not on objects. Intercept first.
    if (Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.linemark" &&
        project_.document.HasMarkSelection()) {
        BeginMarkTransform(kind);
        return;
    }
    // Label the resulting undo step (and the Info feed) by the transform kind.
    MarkUndoLabel(kind == TransformKind::Move   ? "Move"
                : kind == TransformKind::Rotate ? "Rotate"
                : kind == TransformKind::Scale  ? "Scale" : "Transform");
    transformOp_.Reset();
    if (editorMode_ == EditorMode::Edit) {
        // Element transform: the WHOLE edit-mode selection — VERTICES and HANDLES —
        // undergoes the SAME G/R/S about the common pivot. Vertices move as whole
        // nodes; individually-selected handles move only their endpoint (per the
        // node's HandleMode). A handle whose node is also a selected vertex is skipped
        // (it moves with the node).
        auto& doc = project_.document;
        if (!doc.HasVertSelection() && !doc.HasHandleSelection()) return;
        transformOp_.kind    = kind;
        transformOp_.element = true;
        transformOp_.pivot   = ComputeVertPivot();
        transformOp_.vrefs.assign(doc.VertSelection().begin(), doc.VertSelection().end());
        // JUNCTION welding: if a selected vertex belongs to a junction group, pull in
        // every coincident node sharing its junctionId (same part) so the whole vertex
        // — all its branches' anchors — moves as one (a single multi-path vertex).
        {
            auto contains = [&](const Renderer::VertRef& r){
                for (const Renderer::VertRef& e : transformOp_.vrefs)
                    if (e.shape==r.shape && e.part==r.part && e.node==r.node) return true;
                return false;
            };
            std::vector<Renderer::VertRef> extra;
            for (const Renderer::VertRef& v : transformOp_.vrefs) {
                Renderer::Shape* s = doc.FindShape(v.shape);
                if (!s || v.part < 0 || v.part >= (int)s->parts.size()) continue;
                auto& nds = s->parts[(size_t)v.part].path.nodes;
                if (v.node < 0 || v.node >= (int)nds.size()) continue;
                uint32_t jid = nds[(size_t)v.node].junctionId;
                if (!jid) continue;
                for (int ni = 0; ni < (int)nds.size(); ++ni) {
                    if (nds[(size_t)ni].junctionId != jid) continue;
                    Renderer::VertRef r{ v.shape, v.part, ni };
                    if (!contains(r)) extra.push_back(r);
                }
            }
            for (const Renderer::VertRef& r : extra) transformOp_.vrefs.push_back(r);
        }
        transformOp_.vsnap.clear();
        auto nodeOf = [&](const Renderer::VertRef& v) -> Renderer::Node {
            Renderer::Shape* s = doc.FindShape(v.shape);
            if (s && v.part < (int)s->parts.size() &&
                v.node < (int)s->parts[(size_t)v.part].path.nodes.size())
                return s->parts[(size_t)v.part].path.nodes[(size_t)v.node];
            return Renderer::Node{};
        };
        for (const Renderer::VertRef& v : transformOp_.vrefs)
            transformOp_.vsnap.push_back(nodeOf(v));
        // Selected handles whose NODE isn't already a selected vertex.
        transformOp_.hrefs.clear(); transformOp_.hsnap.clear();
        for (const Renderer::HandleRef& h : doc.HandleSelection()) {
            Renderer::VertRef hv{ h.shape, h.part, h.node };
            if (doc.IsVertSelected(hv)) continue;     // node moves whole → skip handle
            transformOp_.hrefs.push_back(h);
            transformOp_.hsnap.push_back(nodeOf(hv));
        }
        return;
    }
    if (!project_.document.HasSelection()) return;
    transformOp_.kind  = kind;
    // The pivot is computed from the FULL selection (so "Active Element" can pivot
    // about a hidden active object), but only VISIBLE objects are actually
    // transformed — a hidden object stays put even when selected/active.
    transformOp_.pivot = ComputePivot();
    transformOp_.ids.clear();
    transformOp_.snapshot.clear();
    // PARENTING: the op targets the selection's transitive closure over children, so
    // a parent drags its descendants rigidly — they're transformed by the SAME op
    // about the SAME pivot, no separate propagation pass (the clean way).
    for (uint64_t id : SelectionWithDescendants())
        if (Renderer::Shape* s = project_.document.FindShape(id); s && s->visible) {
            transformOp_.ids.push_back(id);
            transformOp_.snapshot.push_back(s->transform);
        }
    if (transformOp_.ids.empty()) { transformOp_.Reset(); return; }  // all hidden
    // Capture the orientation basis ONCE at op start (axes stay fixed during the op
    // even if a Local/Parent reference rotates). axis starts free; X/Y toggle it.
    transformOp_.axis = TransformAxis::None;
    ComputeOrientationBasis(transformOp_.axisX, transformOp_.axisY);
    // startMouse + owner are set by the first UpdateTransformOp from the leaf.
}

// Selection ∪ all object descendants (parenting closure). Stable, de-duplicated,
// selection order first then descendants.
std::vector<uint64_t> Application::SelectionWithDescendants() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    std::vector<uint64_t> out;
    auto pushUnique = [&](uint64_t id) {
        if (id && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };
    for (uint64_t id : doc.Selection()) {
        pushUnique(id);
        for (uint64_t d : doc.DescendantsOf(id)) pushUnique(d);
    }
    return out;
}

// Orthonormal basis of the current Transform Orientation for the active selection.
void Application::ComputeOrientationBasis(Renderer::Vec2& outX,
                                          Renderer::Vec2& outY,
                                          TransformOrientation orient) const {
    outX = {1, 0}; outY = {0, 1};                 // Global / View / Cursor (today)
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    auto axesFromRotation = [&](float rot) {
        float c = std::cos(rot), s = std::sin(rot);
        outX = { c, s };                          // rotated X
        outY = { -s, c };                         // rotated Y (90° CCW from X)
    };
    if (orient == TransformOrientation::Local) {
        if (Renderer::Shape* a = doc.FindShape(doc.ActiveId()))
            axesFromRotation(a->transform.rotate);
    } else if (orient == TransformOrientation::Parent) {
        Renderer::Shape* a = doc.FindShape(doc.ActiveId());
        Renderer::Shape* p = a ? doc.FindShape(a->parentId) : nullptr;
        if (p) axesFromRotation(p->transform.rotate);
        else if (a) axesFromRotation(a->transform.rotate);   // no parent → Local
    } else if (orient == TransformOrientation::Cursor) {
        axesFromRotation(doc.cursorRotation);                // 2D cursor's own angle
    }
    // Global / View keep the document axes until the canvas can rotate (View).
}

void Application::Action_CycleTool() {
    Shortcuts::Tools::ToolManager::Instance().CycleNext();
    std::cout << "[ACTION] Cycle Tool → "
              << Shortcuts::Tools::ToolManager::Instance().GetActiveTool()
              << std::endl;
}

} // namespace App
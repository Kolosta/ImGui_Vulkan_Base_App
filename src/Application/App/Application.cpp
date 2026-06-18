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


} // namespace App

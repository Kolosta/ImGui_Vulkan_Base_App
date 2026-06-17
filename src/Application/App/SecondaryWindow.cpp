#include "SecondaryWindow.h"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <algorithm>

namespace App {

namespace {
void check_vk(VkResult e) {
    if (e == VK_SUCCESS) return;
    std::fprintf(stderr, "[secondary-window][vulkan] VkResult = %d\n", e);
    if (e < 0) abort();
}
} // namespace

SecondaryWindow::~SecondaryWindow() { Shutdown(); }

// ── Init: record shared handles + config, create the secondary ImGui context ──
bool SecondaryWindow::Init(const VulkanShared& shared, float dpiScale,
                           const Config& cfg, ContentFn content) {
    shared_   = shared;
    dpiScale_ = dpiScale;
    config_   = cfg;
    content_  = std::move(content);

    // A dedicated ImGui context for this window. CRUCIAL: it SHARES the main
    // context's font atlas, so this window uses the exact same fonts (NotoSans
    // etc., at the same DPI-correct size) instead of ImGui's default bitmap
    // font (which looked blurry and wrongly sized). The shared atlas is
    // rasterised once; each backend uploads its own GPU texture from it.
    prevCtx_ = ImGui::GetCurrentContext();
    ImFontAtlas* sharedAtlas = prevCtx_ ? ImGui::GetIO().Fonts : nullptr;
    ctx_ = ImGui::CreateContext(sharedAtlas);
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;                    // don't fight the main ctx ini
    // Mirror the main context's style (colours, sizes, font scaling) so this
    // window is visually identical and stays linked to it. We copy the whole
    // style + the font-size drivers ImGui 1.92 uses for crisp text.
    if (prevCtx_) {
        ImGui::SetCurrentContext(prevCtx_);
        ImGuiStyle mainStyle = ImGui::GetStyle();
        ImFont* mainDefault = ImGui::GetIO().FontDefault;
        ImGui::SetCurrentContext(ctx_);
        ImGui::GetStyle() = mainStyle;
        ImGui::GetIO().FontDefault = mainDefault;  // same default face
    } else {
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(dpiScale_);
    }
    ImGui::SetCurrentContext(prevCtx_);
    prevCtx_ = nullptr;
    return true;
}

void SecondaryWindow::Shutdown() {
    if (osCreated_) DestroyOsWindow();
    if (ctx_) {
        ImGuiContext* save = ImGui::GetCurrentContext();
        ImGui::DestroyContext(ctx_);
        // DestroyContext clears the current context; restore the previous one
        // unless it was the one we just destroyed.
        ImGui::SetCurrentContext(save == ctx_ ? nullptr : save);
        ctx_ = nullptr;
    }
}

bool SecondaryWindow::HasInputFocus() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS);
}

bool SecondaryWindow::HasMouseFocus() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MOUSE_FOCUS);
}

void SecondaryWindow::FocusNow() {
    if (!window_) return;
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
}

// ── Show / hide: lazily create or tear down the OS window + swapchain ─────────
void SecondaryWindow::Show(bool on) {
    if (on == open_) return;
    open_ = on;
    if (on) {
        if (!osCreated_) CreateOsWindow();
        if (window_) { SDL_ShowWindow(window_); SDL_RaiseWindow(window_); }
    } else {
        if (window_) SDL_HideWindow(window_);
    }
}

// ── Create the OS window, surface, swapchain and per-context backends ─────────
void SecondaryWindow::CreateOsWindow() {
    // Borderless, resizable, hidden until Show() raises it. Its own taskbar
    // icon makes it a distinct window from the main one.
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN |
                            SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(config_.title.c_str(),
                               (int)(config_.width  * dpiScale_),
                               (int)(config_.height * dpiScale_), flags);
    if (!window_) {
        std::fprintf(stderr, "[secondary-window] SDL_CreateWindow failed: %s\n",
                     SDL_GetError());
        return;
    }
    SDL_SetWindowHitTest(window_, &SecondaryWindow::HitTest, this);

    // Surface + swapchain on the SHARED device.
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, shared_.instance, nullptr, &surface)) {
        std::fprintf(stderr, "[secondary-window] surface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_); window_ = nullptr; return;
    }
    wd_.Surface = surface;

    VkBool32 ok = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(shared_.physicalDevice,
                                         shared_.queueFamily, surface, &ok);
    if (ok != VK_TRUE) {
        std::fprintf(stderr, "[secondary-window] no WSI support on queue family\n");
    }

    const VkFormat fmts[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    wd_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        shared_.physicalDevice, surface, fmts, (size_t)IM_ARRAYSIZE(fmts),
        VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    VkPresentModeKHR pm[] = { VK_PRESENT_MODE_FIFO_KHR };
    wd_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        shared_.physicalDevice, surface, pm, IM_ARRAYSIZE(pm));

    int w = 0, h = 0; SDL_GetWindowSize(window_, &w, &h);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        shared_.instance, shared_.physicalDevice, shared_.device, &wd_,
        shared_.queueFamily, nullptr, w, h, shared_.minImageCount, 0);

    // Init the backends ON OUR CONTEXT.
    prevCtx_ = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplSDL3_InitForVulkan(window_);
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.Instance        = shared_.instance;
    ii.PhysicalDevice  = shared_.physicalDevice;
    ii.Device          = shared_.device;
    ii.QueueFamily     = shared_.queueFamily;
    ii.Queue           = shared_.queue;
    ii.DescriptorPool  = shared_.descriptorPool;
    ii.MinImageCount   = shared_.minImageCount;
    ii.ImageCount      = wd_.ImageCount;
    ii.PipelineInfoMain.RenderPass = wd_.RenderPass;
    ii.PipelineInfoMain.Subpass    = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&ii);
    ImGui::SetCurrentContext(prevCtx_);
    prevCtx_ = nullptr;

    osCreated_ = true;
}

void SecondaryWindow::DestroyOsWindow() {
    if (!osCreated_) return;
    vkDeviceWaitIdle(shared_.device);
    ImGuiContext* save = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    // Restore the caller's context (don't leave it dangling at our ctx_).
    ImGui::SetCurrentContext(save == ctx_ ? nullptr : save);

    ImGui_ImplVulkanH_DestroyWindow(shared_.instance, shared_.device, &wd_, nullptr);
    wd_ = ImGui_ImplVulkanH_Window{};
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    osCreated_ = false;
}

void SecondaryWindow::CreateOrResizeSwapchain(int w, int h) {
    if (w <= 0 || h <= 0) return;
    ImGui_ImplVulkan_SetMinImageCount(shared_.minImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        shared_.instance, shared_.physicalDevice, shared_.device, &wd_,
        shared_.queueFamily, nullptr, w, h, shared_.minImageCount, 0);
    wd_.FrameIndex = 0;
    swapRebuild_ = false;
}

// ── Event routing ────────────────────────────────────────────────────────────
bool SecondaryWindow::HandleEvent(const SDL_Event& ev) {
    if (!osCreated_ || !window_) return false;
    const SDL_WindowID myId = SDL_GetWindowID(window_);

    // Window-targeted events carry a windowID; mouse/key events carry the
    // focused window in their own fields. ImGui_ImplSDL3_ProcessEvent looks at
    // the focused window internally, so we switch to our context and forward
    // events whose window is ours.
    bool forUs = false;
    switch (ev.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            forUs = (ev.window.windowID == myId);
            break;
        case SDL_EVENT_MOUSE_MOTION:       forUs = (ev.motion.windowID == myId); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:    forUs = (ev.button.windowID == myId); break;
        case SDL_EVENT_MOUSE_WHEEL:        forUs = (ev.wheel.windowID == myId); break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:             forUs = (ev.key.windowID == myId); break;
        case SDL_EVENT_TEXT_INPUT:         forUs = (ev.text.windowID == myId); break;
        default: break;
    }
    if (!forUs) return false;

    if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        closeRequested_ = true; Show(false); return true;
    }

    ImGuiContext* save = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx_);
    ImGui_ImplSDL3_ProcessEvent(&ev);
    ImGui::SetCurrentContext(save);
    return true;
}

// ── Per-frame render in the secondary context ────────────────────────────────
void SecondaryWindow::RenderFrame() {
    if (!open_ || !osCreated_ || !window_) return;
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) return;
    // Re-entrancy guard: the SDL event watch can call Application::RenderFrame()
    // (→ here) again mid-render during a live resize. A second NewFrame on the
    // same context before Render() trips ImGui's "Forgot to call Render()?"
    // assert, so we simply skip the nested call.
    if (rendering_) return;
    rendering_ = true;

    ImGuiContext* save = ImGui::GetCurrentContext();
    // Keep this window LINKED to the main window's look: copy the live style +
    // default font from the main context every frame, so theme/font changes
    // (applied to the main context by DesignSystem/FontManager) take effect
    // here too. Done before switching, reading from the main (current) context.
    ImGuiStyle linkedStyle;
    ImFont*    linkedFont = nullptr;
    if (save && save != ctx_) {
        linkedStyle = ImGui::GetStyle();
        linkedFont  = ImGui::GetIO().FontDefault;
    }
    ImGui::SetCurrentContext(ctx_);
    if (save && save != ctx_) {
        ImGui::GetStyle() = linkedStyle;
        ImGui::GetIO().FontDefault = linkedFont;
    }

    // Resize the swapchain to the current window size if needed.
    int w = 0, h = 0; SDL_GetWindowSize(window_, &w, &h);
    if (w > 0 && h > 0 && (swapRebuild_ || wd_.Width != w || wd_.Height != h))
        CreateOrResizeSwapchain(w, h);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Shortcut pipeline in THIS window's context: drain IO + reset per-frame
    // context BEFORE the content (so panels can RegisterRegionContext), then
    // dispatch AFTER, gated by this window's keyboard focus.
    if (shortcutPre_) shortcutPre_();
    RenderTitleBarAndContent();
    if (shortcutPost_) shortcutPost_(HasInputFocus());

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    const bool minimized = (dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f);
    if (!minimized) {
        wd_.ClearValue.color.float32[0] = 0.07f;
        wd_.ClearValue.color.float32[1] = 0.07f;
        wd_.ClearValue.color.float32[2] = 0.08f;
        wd_.ClearValue.color.float32[3] = 1.0f;

        VkSemaphore acq = wd_.FrameSemaphores[wd_.SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore comp = wd_.FrameSemaphores[wd_.SemaphoreIndex].RenderCompleteSemaphore;
        VkResult err = vkAcquireNextImageKHR(shared_.device, wd_.Swapchain, UINT64_MAX,
                                             acq, VK_NULL_HANDLE, &wd_.FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
            swapRebuild_ = true;
        if (err != VK_ERROR_OUT_OF_DATE_KHR) {
            if (err != VK_SUBOPTIMAL_KHR) check_vk(err);
            ImGui_ImplVulkanH_Frame* fd = &wd_.Frames[wd_.FrameIndex];
            check_vk(vkWaitForFences(shared_.device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
            check_vk(vkResetFences(shared_.device, 1, &fd->Fence));
            check_vk(vkResetCommandPool(shared_.device, fd->CommandPool, 0));
            VkCommandBufferBeginInfo bi = {};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check_vk(vkBeginCommandBuffer(fd->CommandBuffer, &bi));
            VkRenderPassBeginInfo rp = {};
            rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.renderPass = wd_.RenderPass;
            rp.framebuffer = fd->Framebuffer;
            rp.renderArea.extent.width = wd_.Width;
            rp.renderArea.extent.height = wd_.Height;
            rp.clearValueCount = 1;
            rp.pClearValues = &wd_.ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
            ImGui_ImplVulkan_RenderDrawData(dd, fd->CommandBuffer);
            vkCmdEndRenderPass(fd->CommandBuffer);
            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo si = {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.waitSemaphoreCount = 1; si.pWaitSemaphores = &acq;
            si.pWaitDstStageMask = &ws;
            si.commandBufferCount = 1; si.pCommandBuffers = &fd->CommandBuffer;
            si.signalSemaphoreCount = 1; si.pSignalSemaphores = &comp;
            check_vk(vkEndCommandBuffer(fd->CommandBuffer));
            check_vk(vkQueueSubmit(shared_.queue, 1, &si, fd->Fence));

            if (!swapRebuild_) {
                VkPresentInfoKHR pi = {};
                pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &comp;
                pi.swapchainCount = 1; pi.pSwapchains = &wd_.Swapchain;
                pi.pImageIndices = &wd_.FrameIndex;
                VkResult pe = vkQueuePresentKHR(shared_.queue, &pi);
                if (pe == VK_ERROR_OUT_OF_DATE_KHR || pe == VK_SUBOPTIMAL_KHR)
                    swapRebuild_ = true;
                else check_vk(pe);
                wd_.SemaphoreIndex = (wd_.SemaphoreIndex + 1) % wd_.SemaphoreCount;
            }
        }
    }

    ImGui::SetCurrentContext(save);
    rendering_ = false;

    // A title-bar close button press (recorded during content) closes us after
    // the frame, outside the ImGui span.
    if (pendingClose_) { pendingClose_ = false; closeRequested_ = true; Show(false); }
}

// ── Content: the UI fills this window's viewport directly ────────────────────
void SecondaryWindow::RenderTitleBarAndContent() {
    // The content callback opens its OWN window sized to fill this context's
    // main viewport (= our OS window) and draws its custom title bar + body. On
    // close it clears `open`, which we translate to a window hide after the
    // frame (outside the ImGui span).
    bool stillOpen = true;
    if (content_) content_(&stillOpen);
    if (!stillOpen) pendingClose_ = true;

    // Title-bar band height for the SDL hit-test (control-height + insets),
    // matching the value the content UI uses for its bar.
    titleBarHeightPx_ = ImGui::GetFrameHeight() + 8.0f;

    // NOTE: drop shadow deferred. A true EXTERNAL shadow needs a transparent
    // swapchain (the shared ImGui Vulkan helper forces OPAQUE composite-alpha)
    // and, on Windows, often DwmEnableBlurBehindWindow — to be tackled in a
    // dedicated pass. The component.window.shadow.* tokens are kept for then.
}

// ── SDL hit-test: drag on the empty title-bar band, resize on the borders ────
SDL_HitTestResult SDLCALL SecondaryWindow::HitTest(
    SDL_Window* win, const SDL_Point* area, void* data) {
    auto* self = static_cast<SecondaryWindow*>(data);
    int w = 0, h = 0; SDL_GetWindowSize(win, &w, &h);
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
    // Title-bar band: draggable, EXCEPT where an interactive widget sits. The
    // UI publishes interactive blockers via ImGui; for the shell we treat the
    // whole band minus the right-hand system-button area as draggable. The UI's
    // own buttons are submitted as ImGui items and ImGui keeps the mouse, so a
    // press there is handled before the OS drag starts on release.
    if ((float)area->y < self->titleBarHeightPx_) {
        // Right ~3 control-height slots are the min/max/close buttons.
        if (area->x < w - (int)(self->titleBarHeightPx_ * 3.0f))
            return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

} // namespace App

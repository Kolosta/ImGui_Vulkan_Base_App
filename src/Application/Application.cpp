#include "Application.h"
#include <iostream>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>
#include <UI/FontManager.h>

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

void Application::Run() {
    while (running_) {
        ProcessEvents();
        Update();
        Render();
        Present();
    }
}

void Application::ProcessEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
            running_ = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(window_))
            running_ = false;
    }

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
                        .GetFloat("semantic.shortcut.dragThreshold");
            Shortcuts::EventNormalizer::Instance().SetDragThreshold(t);
        } catch (...) { /* token missing — keep current value */ }
    }
    Shortcuts::EventNormalizer::Instance().Frame();
    Shortcuts::ShortcutManager::Instance().BeginFrame();

    RenderMainMenuBar();
    RenderMainLayout();
    RenderFloatingWindows();

    // Dispatch happens after panels have set the context for this frame so
    // editor/region/tool match the user's current hover.
    Shortcuts::ShortcutManager::Instance().ProcessInput();
}

void Application::Render() {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (is_minimized) return;

    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec4 clear_color = ds.GetColor("semantic.color.background");
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

    // Rendu des fenêtres additionnelles (multi-écrans).
    // Nécessite ImGuiConfigFlags_ViewportsEnable dans ApplicationInit.cpp :
    //   io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

// ── Default Actions ──────────────────────────────────────────────────────────
void Application::Action_NewFile()  { std::cout << "[ACTION] New File"  << std::endl; }
void Application::Action_OpenFile() { std::cout << "[ACTION] Open File" << std::endl; }
void Application::Action_SaveFile() { std::cout << "[ACTION] Save File" << std::endl; }
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
    showSettings_ = !showSettings_;
}
void Application::Action_ToggleImGuiDemo() {
    showImGuiDemo_ = !showImGuiDemo_;
}
void Application::Action_ActivateTool1() {
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool("tool.brush");
    std::cout << "[ACTION] Activate Tool: brush" << std::endl;
}
void Application::Action_ActivateTool2() {
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool("tool.eraser");
    std::cout << "[ACTION] Activate Tool: eraser" << std::endl;
}
void Application::Action_CycleTool() {
    Shortcuts::Tools::ToolManager::Instance().CycleNext();
    std::cout << "[ACTION] Cycle Tool → "
              << Shortcuts::Tools::ToolManager::Instance().GetActiveTool()
              << std::endl;
}

} // namespace App
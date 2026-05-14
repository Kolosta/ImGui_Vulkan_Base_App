#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>

#include <UI/TokenEditor.h>
#include <UI/ShortcutEditor.h>
#include <VectorGraphics/editors/IconEditorWindow.h>

namespace App {

class Application {
public:
    Application();
    ~Application();

    bool Initialize();
    void Run();
    void Shutdown();

private:
    void ProcessEvents();
    void Update();
    void Render();
    void Present();

    // Init (ApplicationInit.cpp)
    void SetupVulkan();
    void SetupVulkanWindow();
    void InitializeSubsystems();
    void LoadResources();
    void RegisterTestShortcuts();

    // Layout (ApplicationUI.cpp)
    void RenderMainMenuBar();
    void RenderMainLayout();
    void RenderToolbar();
    void RenderMainContent();

    // Content sections — inline, no Begin/End (ApplicationWindows.cpp)
    void RenderSectionIconTestLab();
    void RenderSectionDesignExample();
    void RenderSectionThemePreview();
    void RenderSectionTestZone1();
    void RenderSectionTestZone2();

    // Floating windows (ApplicationWindows.cpp)
    void RenderFloatingWindows();
    void RenderSettings();

    // Test actions
    static void TestAction_NewFile();
    static void TestAction_OpenFile();
    static void TestAction_SaveFile();
    static void TestAction_Quit();
    static void TestAction_Tool1();
    static void TestAction_Tool2();
    static void TestAction_Zone1();
    static void TestAction_Zone2();

    // Core
    SDL_Window* window_    = nullptr;
    bool        running_   = true;
    float       mainScale_ = 1.0f;

    // Vulkan
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    ImGui_ImplVulkanH_Window mainWindowData_;

    // Layout
    float toolbarWidth_ = 64.0f;

    // UI state — les trois éditeurs sont regroupés dans une seule fenêtre "Paramètres"
    bool showSettings_  = false;
    bool showImGuiDemo_ = false;

    // UI components
    DesignSystem::TokenEditor        tokenEditor_;
    UI::ShortcutEditor               shortcutEditor_;
    VectorGraphics::IconEditorWindow iconEditor_;
};

} // namespace App
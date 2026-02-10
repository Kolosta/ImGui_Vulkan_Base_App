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
    // Lifecycle
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
    
    // UI (ApplicationUI.cpp)
    void RenderDockSpace();
    void RenderMainMenuBar();
    void RenderToolbar();
    
    // Windows (ApplicationWindows.cpp)
    void RenderIconTestWindow();
    void RenderDesignExample();
    void RenderThemePreview();
    void RenderTestZone1();
    void RenderTestZone2();
    void RenderFloatingWindows();
    
    // Test Actions
    static void TestAction_NewFile();
    static void TestAction_OpenFile();
    static void TestAction_SaveFile();
    static void TestAction_Quit();
    static void TestAction_Tool1();
    static void TestAction_Tool2();
    static void TestAction_Zone1();
    static void TestAction_Zone2();
    
    // Members
    SDL_Window* window_ = nullptr;
    bool running_ = true;
    float mainScale_ = 1.0f;
    
    // Vulkan
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    ImGui_ImplVulkanH_Window mainWindowData_;
    
    // UI State
    bool showTokenEditor_ = false;
    bool showShortcutEditor_ = false;
    bool showIconEditor_ = false;
    bool showImGuiDemo_ = false;
    
    // UI Components
    DesignSystem::TokenEditor tokenEditor_;
    UI::ShortcutEditor shortcutEditor_;
    VectorGraphics::IconEditorWindow iconEditor_;
};

} // namespace App
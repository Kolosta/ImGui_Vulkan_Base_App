// // // #pragma once

// // // #include <imgui.h>
// // // #include <SDL3/SDL.h>
// // // #include <vulkan/vulkan.h>
// // // #include <imgui_impl_vulkan.h>
// // // #include <DesignSystem/DesignSystem.h>
// // // #include <UI/TokenEditor.h>
// // // #include <UI/ShortcutEditor.h>
// // // #include <UI/IconManager.h>
// // // #include <UI/FontManager.h>
// // // #include <Shortcuts/ShortcutManager.h>

// // // namespace App {

// // // /**
// // //  * Classe principale de l'application
// // //  * Gère le cycle de vie, le rendu et les fenêtres
// // //  */
// // // // class Application {
// // // // public:
// // // //     Application();
// // // //     ~Application();
    
// // // //     bool Initialize();
// // // //     void Run();
// // // //     void Shutdown();
    
// // // // private:
// // // //     // Setup
// // // //     void SetupVulkan();
// // // //     void SetupVulkanWindow();
// // // //     void InitializeSubsystems();
// // // //     void LoadResources();
// // // //     void RegisterTestShortcuts();
    
// // // //     // Main loop
// // // //     void ProcessEvents();
// // // //     void Update();
// // // //     void Render();
// // // //     void Present();
    
// // // //     // UI Rendering
// // // //     void RenderMainMenuBar();
// // // //     void RenderDockSpace();
// // // //     void RenderToolbar();
// // // //     void RenderIconTestWindow();
// // // //     void RenderDesignExample();
// // // //     void RenderThemePreview();
// // // //     void RenderTestZone1();
// // // //     void RenderTestZone2();
// // // //     void RenderFloatingWindows();
    
// // // //     // Actions de test
// // // //     static void TestAction_NewFile();
// // // //     static void TestAction_OpenFile();
// // // //     static void TestAction_SaveFile();
// // // //     static void TestAction_Quit();
// // // //     static void TestAction_Tool1();
// // // //     static void TestAction_Tool2();
// // // //     static void TestAction_Zone1();
// // // //     static void TestAction_Zone2();
// // // class Application {
// // // public:
// // //     Application();
// // //     ~Application();
    
// // //     bool Initialize();
// // //     void Run();
// // //     void Shutdown();

// // // private:
// // //     // === Lifecycle ===
// // //     void ProcessEvents();
// // //     void Update();
// // //     void Render();
// // //     void Present();
    
// // //     // === Initialization (ApplicationInit.cpp) ===
// // //     void SetupVulkan();
// // //     void SetupVulkanWindow();
// // //     void InitializeSubsystems();
// // //     void LoadResources();
// // //     void LoadIconMetadata();  // NOUVEAU
// // //     void RegisterTestShortcuts();
    
// // //     // === UI Rendering (ApplicationUI.cpp) ===
// // //     void RenderDockSpace();
// // //     void RenderMainMenuBar();
// // //     void RenderToolbar();
    
// // //     // === Windows (ApplicationWindows.cpp) ===
// // //     void RenderIconTestWindow();
// // //     void RenderDesignExample();
// // //     void RenderThemePreview();
// // //     void RenderTestZone1();
// // //     void RenderTestZone2();
// // //     void RenderFloatingWindows();
    
// // //     // === Icon Editor (IconEditorWindow.cpp) ===
// // //     void RenderIconEditorWindow();  // NOUVEAU
    
// // //     // === Test Actions ===
// // //     static void TestAction_NewFile();
// // //     static void TestAction_OpenFile();
// // //     static void TestAction_SaveFile();
// // //     static void TestAction_Quit();
// // //     static void TestAction_Tool1();
// // //     static void TestAction_Tool2();
// // //     static void TestAction_Zone1();
// // //     static void TestAction_Zone2();
    
// // //     // Vulkan
// // //     VkInstance instance_ = VK_NULL_HANDLE;
// // //     VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
// // //     VkDevice device_ = VK_NULL_HANDLE;
// // //     uint32_t queueFamily_ = 0;
// // //     VkQueue queue_ = VK_NULL_HANDLE;
// // //     VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
// // //     VkCommandPool commandPool_ = VK_NULL_HANDLE;
// // //     ImGui_ImplVulkanH_Window mainWindowData_;
    
// // //     // SDL
// // //     SDL_Window* window_ = nullptr;
    
// // //     // Subsystems
// // //     DesignSystem::TokenEditor tokenEditor_;
// // //     UI::ShortcutEditor shortcutEditor_;
    
// // //     // State
// // //     bool running_ = true;
// // //     bool showTokenEditor_ = false;
// // //     bool showShortcutEditor_ = false;
// // //     bool showImGuiDemo_ = false;
    
// // //     float mainScale_ = 1.0f;
// // // };

// // // } // namespace App


// // #pragma once

// // // #include <imgui.h>
// // // #include <SDL3/SDL.h>
// // // #include <vulkan/vulkan.h>
// // // #include <imgui_impl_vulkan.h>

// // // #include <UI/TokenEditor.h>
// // // #include <UI/ShortcutEditor.h>

// // #include <imgui.h>
// // #include <SDL3/SDL.h>
// // #include <vulkan/vulkan.h>
// // #include <imgui_impl_vulkan.h>
// // #include <DesignSystem/DesignSystem.h>
// // #include <UI/TokenEditor.h>
// // #include <UI/ShortcutEditor.h>
// // #include <UI/IconManager.h>
// // #include <UI/FontManager.h>
// // #include <Shortcuts/ShortcutManager.h>

// // namespace App {

// // class Application {
// // public:
// //     Application();
// //     ~Application();
    
// //     bool Initialize();
// //     void Run();
// //     void Shutdown();

// // private:
// //     // ========== Lifecycle Methods ==========
// //     void ProcessEvents();
// //     void Update();
// //     void Render();
// //     void Present();
    
// //     // ========== Initialization Methods (ApplicationInit.cpp) ==========
// //     void SetupVulkan();
// //     void SetupVulkanWindow();
// //     void InitializeSubsystems();
// //     void LoadResources();
// //     void LoadIconMetadata();
// //     void RegisterTestShortcuts();
    
// //     // ========== UI Methods (ApplicationUI.cpp) ==========
// //     void RenderDockSpace();
// //     void RenderMainMenuBar();
// //     void RenderToolbar();
    
// //     // ========== Window Methods (ApplicationWindows.cpp) ==========
// //     void RenderIconTestWindow();
// //     void RenderDesignExample();
// //     void RenderThemePreview();
// //     void RenderTestZone1();
// //     void RenderTestZone2();
// //     void RenderFloatingWindows();
    
// //     // ========== Icon Editor (IconEditorWindow.cpp) ==========
// //     void RenderIconEditorWindow();
    
// //     // ========== Test Actions ==========
// //     static void TestAction_NewFile();
// //     static void TestAction_OpenFile();
// //     static void TestAction_SaveFile();
// //     static void TestAction_Quit();
// //     static void TestAction_Tool1();
// //     static void TestAction_Tool2();
// //     static void TestAction_Zone1();
// //     static void TestAction_Zone2();
    
// //     // ========== Members ==========
// //     SDL_Window* window_ = nullptr;
// //     bool running_ = true;
// //     float mainScale_ = 1.0f;
    
// //     // Vulkan
// //     VkInstance instance_ = VK_NULL_HANDLE;
// //     VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
// //     VkDevice device_ = VK_NULL_HANDLE;
// //     VkQueue queue_ = VK_NULL_HANDLE;
// //     VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
// //     VkCommandPool commandPool_ = VK_NULL_HANDLE;
// //     uint32_t queueFamily_ = 0;
// //     ImGui_ImplVulkanH_Window mainWindowData_;
    
// //     // UI State
// //     bool showTokenEditor_ = false;
// //     bool showShortcutEditor_ = false;
// //     bool showIconEditor_ = false;
// //     bool showImGuiDemo_ = false;
    
// //     // UI Components
// //     DesignSystem::TokenEditor tokenEditor_;
// //     UI::ShortcutEditor shortcutEditor_;
// // };

// // } // namespace App










// #pragma once

// #include <imgui.h>
// #include <SDL3/SDL.h>
// #include <vulkan/vulkan.h>
// #include <imgui_impl_vulkan.h>

// #include <UI/TokenEditor.h>
// #include <UI/ShortcutEditor.h>

// namespace App {

// class Application {
// public:
//     Application();
//     ~Application();
    
//     bool Initialize();
//     void Run();
//     void Shutdown();

// private:
//     // ========== Lifecycle Methods ==========
//     void ProcessEvents();
//     void Update();
//     void Render();
//     void Present();
    
//     // ========== Initialization Methods (ApplicationInit.cpp) ==========
//     void SetupVulkan();
//     void SetupVulkanWindow();
//     void InitializeSubsystems();
//     void LoadResources();
//     void RegisterTestShortcuts();
    
//     // ========== UI Methods (ApplicationUI.cpp) ==========
//     void RenderDockSpace();
//     void RenderMainMenuBar();
//     void RenderToolbar();
    
//     // ========== Window Methods (ApplicationWindows.cpp) ==========
//     void RenderIconTestWindow();
//     void RenderDesignExample();
//     void RenderThemePreview();
//     void RenderTestZone1();
//     void RenderTestZone2();
//     void RenderFloatingWindows();
    
//     // ========== Icon Editor (IconEditorWindow.cpp) ==========
//     void RenderIconEditorWindow();
    
//     // ========== Test Actions ==========
//     static void TestAction_NewFile();
//     static void TestAction_OpenFile();
//     static void TestAction_SaveFile();
//     static void TestAction_Quit();
//     static void TestAction_Tool1();
//     static void TestAction_Tool2();
//     static void TestAction_Zone1();
//     static void TestAction_Zone2();
    
//     // ========== Members ==========
//     SDL_Window* window_ = nullptr;
//     bool running_ = true;
//     float mainScale_ = 1.0f;
    
//     // Vulkan
//     VkInstance instance_ = VK_NULL_HANDLE;
//     VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
//     VkDevice device_ = VK_NULL_HANDLE;
//     VkQueue queue_ = VK_NULL_HANDLE;
//     VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
//     VkCommandPool commandPool_ = VK_NULL_HANDLE;
//     uint32_t queueFamily_ = 0;
//     ImGui_ImplVulkanH_Window mainWindowData_;
    
//     // UI State
//     bool showTokenEditor_ = false;
//     bool showShortcutEditor_ = false;
//     bool showIconEditor_ = false;
//     bool showImGuiDemo_ = false;
    
//     // UI Components
//     DesignSystem::TokenEditor tokenEditor_;
//     UI::ShortcutEditor shortcutEditor_;
// };

// } // namespace App













#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>

#include <UI/TokenEditor.h>
#include <UI/ShortcutEditor.h>

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
    
    // Icon Editor (IconEditorWindow.cpp)
    void RenderIconEditorWindow();
    
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
};

} // namespace App
#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>

#include <UI/Tokens/TokenEditor.h>
#include <UI/Shortcuts/ShortcutEditor.h>
#include <UI/Chrome/StatusBar.h>
#include <VectorGraphics/editors/IconEditorWindow.h>
#include "ZoneLayout.h"
#include "Project.h"

namespace App {

class Application {
public:
    static constexpr const char* kVersion = "v0.1.0";

    Application();
    ~Application();

    bool Initialize();
    void Run();
    void Shutdown();

    // One full frame: ProcessEvents + Update + Render + Present.
    // Called from the main loop and from the SDL event watch during the OS
    // modal resize loop (Windows blocks SDL_PollEvent during live resize).
    void RenderFrame();

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
    void RegisterDefaultShortcuts();
    // Resolve font design-system tokens (family role + weight) into the active
    // default font. Called at init and re-callable when a font token changes.
    void ApplyFontTokens();

    // Layout (ApplicationUI.cpp)
    void RenderMainMenuBar();
    void RenderMainLayout();
    void RenderToolbar();           // standalone palette (legacy / unused now)
    void RenderToolbarInto(ImVec2 origin);  // floating tools inside a canvas
    void RenderMainContent();
    void RenderStatusBar();

    // The 2D canvas editor: grey artboard, unit-aware rulers (top + left),
    // unit label at their crossing, blue cursor guides, floating tool
    // palette pinned left inside the canvas. Drawn into the given zone size.
    // Camera/document live in the leaf's EditorState (per-zone, not global).
    void RenderViewport(ImVec2 size, EditorState& st);

    // Outliner editor: a Blender-style tree of the shared project →
    // artboards (→ objects later). Reads the one shared Project.
    void RenderOutliner();

    // Content sections — inline, no Begin/End (ApplicationWindows.cpp)
    void RenderSectionIconTestLab();
    void RenderSectionDesignExample();
    void RenderSectionThemePreview();
    void RenderSectionTestZone1();
    void RenderSectionTestZone2();

    // Floating windows (ApplicationWindows.cpp)
    void RenderFloatingWindows();
    void RenderSettings();
    // "Dev Test Window": all the former main-area demo panels gathered into
    // one floating, non-dockable window (same organisation as before).
    void RenderDevTestWindow();

    // Default actions
    static void Action_NewFile();
    static void Action_OpenFile();
    static void Action_SaveFile();
    void Action_Quit();
    void Action_ToggleSettings();
    void Action_ToggleImGuiDemo();
    static void Action_Zone1();
    static void Action_Zone2();
    static void Action_ThemePreviewCycle();
    void Action_ActivateTool1();
    void Action_ActivateTool2();
    void Action_ActivateHand();
    void Action_CycleTool();
    void Action_NewDocument();   // add an artboard to the current project
    void Action_NewProject();    // reset to a fresh empty project
    void Action_ViewFitDocument();
    void Action_ViewResetOrigin();

    // Core
    SDL_Window* window_      = nullptr;
    bool        running_     = true;
    // True once ImGui + Vulkan backends are fully initialized. The SDL event
    // watch (live-resize) must NOT render before this, or it dereferences an
    // uninitialized ImGui backend (assert: "Did you call ImGui_ImplSDL3_Init").
    bool        initialized_ = false;
    float       mainScale_   = 1.0f;

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

    // Last font (family, weight) applied from design-system tokens. Lets
    // ApplyFontTokens() be called every frame yet only rebuild the default
    // font when a token actually changed.
    std::string lastFontFamily_;
    int         lastFontWeight_ = -1;

    // UI state. Floating windows are unique and non-dockable.
    bool showSettings_   = false;
    bool showImGuiDemo_  = false;
    bool showDevWindow_  = true;   // dev test panels (former main area)

    // Camera + view requests live per-leaf in ZoneLayout::EditorState (each
    // Viewport zone has its own). Camera actions target
    // zoneLayout_.HoveredEditorState().

    // The shared project: ONE model read/written by every Viewport zone and
    // the Outliner. Created empty on launch. (File save/open: later.)
    Project project_;

    // Blender-style dynamic zone tree (no native docking UX).
    ZoneLayout zoneLayout_;

    // UI components
    DesignSystem::TokenEditor        tokenEditor_;
    UI::ShortcutEditor               shortcutEditor_;
    VectorGraphics::IconEditorWindow iconEditor_;

    // Singleton-self for non-static actions (callbacks captured by lambda).
    static Application* s_instance_;
};

} // namespace App
#include "Application.h"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Text/FontManager.h>
#include <VectorGraphics/IconManager.h>
#include <iostream>

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

static VkAllocationCallbacks* g_Allocator = nullptr;
static uint32_t g_MinImageCount = 2;

namespace App {

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
    (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
    fprintf(stderr, "[vulkan] Debug report: %s\n\n", pMessage);
    return VK_FALSE;
}
#endif

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

bool Application::Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return false;
    }

    mainScale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    // BORDERLESS: the OS chrome (title bar + window buttons) is removed so we
    // can draw our own unified, themed application bar. Drag / snap / resize /
    // maximize stay NATIVE through the SDL hit-test registered below — only the
    // pixels are custom, the behavior is the OS's.
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                   SDL_WINDOW_BORDERLESS |
                                   SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(
        "Carto - Vector Graphics Demo",
        static_cast<int>(1280 * mainScale_),
        static_cast<int>(800 * mainScale_),
        window_flags
    );

    if (!window_) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return false;
    }

    // App icon (taskbar / Alt-Tab) from the logo. Harmless if the SVG is missing.
    SetWindowIconFromLogo();

    // Native window dragging / edge-resizing for the borderless window: the
    // callback classifies each point as draggable (title-bar background), a
    // resize border, or normal (an interactive widget — see titleBarBlockers_).
    SDL_SetWindowHitTest(window_, &Application::HitTestCallback, this);

    // Open "maximized" on first launch using OUR maximize (sizes the window to
    // the display's usable bounds — never the OS maximize, which would cover the
    // taskbar). Done while HIDDEN so the swapchain is sized right for the first
    // frame.
    SetMaximized(true);

    SetupVulkan();
    SetupVulkanWindow();

    // On Windows the OS modal resize loop blocks SDL_PollEvent, so the normal
    // ProcessEvents/Update/Render/Present loop never runs during live resize
    // → black / cropped frame until the user releases. This event watch fires
    // synchronously from inside the OS loop and renders a full frame each time
    // the window is resized or exposed, keeping the content live.
    SDL_AddEventWatch([](void* userdata, SDL_Event* ev) -> bool {
        auto* app = static_cast<Application*>(userdata);
        // The home-grown maximize/restore logic below is for the MAIN window
        // ONLY. The Preferences window is a separate OS window with its own
        // native maximize/restore — filter window-targeted events by id so
        // dragging Preferences never restores the main window (they were
        // behaving as if linked).
        const bool isMainWin =
            app->window_ && ev->type >= SDL_EVENT_WINDOW_FIRST &&
            ev->type <= SDL_EVENT_WINDOW_LAST &&
            ev->window.windowID == SDL_GetWindowID(app->window_);
        // A caption double-click / drag-to-top makes the OS maximize (HTCAPTION
        // is hard-wired to it). Undo it and apply our taskbar-aware maximize
        // before any frame renders a full-monitor size.
        if (ev->type == SDL_EVENT_WINDOW_MAXIMIZED && isMainWin)
            app->InterceptOsMaximize();
        // Dragging a maximized window restores it (Windows behaviour). The
        // native HTCAPTION drag captures the mouse before ImGui sees it, so we
        // detect the user's move here. Our own maximize/restore moves set
        // programmaticMove_, which we skip. Restore under the cursor so the
        // window doesn't jump away from the mouse mid-drag. In fullscreen we do
        // NOT restore on drag (a fullscreen-desktop window can't be moved, and
        // SDL's async re-placement on exit fights any manual move) — F11, the
        // double-click, or the restore button leave fullscreen instead.
        if (ev->type == SDL_EVENT_WINDOW_MOVED && isMainWin &&
            !app->programmaticMove_ &&
            app->maximized_ && !app->fullscreen_)
            app->RestoreFromDragAtCursor();
        // Each detached window gets the SAME treatment for ITS own id (it filters
        // internally). MUST be here in the watch, not the poll loop: the OS modal
        // drag loop blocks SDL_PollEvent, so a maximized secondary window's
        // WINDOW_MOVED only reaches it live (cursor still aligned) through here.
        if (ev->type == SDL_EVENT_WINDOW_MAXIMIZED ||
            ev->type == SDL_EVENT_WINDOW_MOVED)
            for (SecondaryWindow* w : app->secondaryWindows_)
                w->HandleWindowChromeEvent(*ev);
        // Ignore render events fired during init (maximize/show happen before
        // the ImGui backends exist — rendering then would assert). During the OS
        // modal resize loop SDL_PollEvent is blocked, so we render synchronously
        // here on every size/expose event to keep the content live (no black
        // bands / crop until release). Cover RESIZED + PIXEL_SIZE_CHANGED +
        // EXPOSED, for the main window AND any secondary (Preferences) window.
        if (app->initialized_ &&
            (ev->type == SDL_EVENT_WINDOW_RESIZED ||
             ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             ev->type == SDL_EVENT_WINDOW_EXPOSED)) {
            app->RenderFrame();
        }
        return false;
    }, this);

    // (No re-centering: the window is already placed at the display's usable
    // bounds above. Centering would move the now-maximized rect off-screen.)
    SDL_ShowWindow(window_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // The docking *engine* stays compiled (docking branch), but its native
    // drag&drop / drop-target UX is intentionally NOT enabled: the app uses
    // its own Blender-style fixed 3-zone layout + custom resize instead.
    io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    // NOTE: ImGui multi-viewport was tried for the Preferences window but proved
    // unreliable on this setup (SDL3 reports display DPI 0 → it empties the
    // monitor list every NewFrame → assert; the window never became a truly
    // independent OS window). The Preferences window is therefore a REAL,
    // SEPARATE SDL+Vulkan window owned by the app (see SecondaryWindow), not
    // an ImGui platform viewport. Multi-viewport stays OFF.
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale_);
    style.FontScaleDpi = 1.0f;  // fonts are loaded at physical pixel size; no post-raster DPI zoom

    // Designate this as the MAIN ImGui context: DesignSystem::ApplyGlobalStyle
    // always writes the resolved ImGuiStyle here, even when an override is
    // committed while a secondary window's context is current (Preferences).
    // Secondary windows copy this style every frame, so one apply reaches all
    // windows. Must precede InitializeSubsystems (which triggers ApplyGlobalStyle).
    DesignSystem::DesignSystem::Instance().SetMainImGuiContext(ImGui::GetCurrentContext());

    InitializeSubsystems();
    LoadResources();
    RegisterDefaultShortcuts();
    RegisterCoreEditors();   // populate the EditorRegistry before the first render
    RegisterModules();       // build the module catalogue (adds module editors)
    zoneLayout_.SetEditorFilter(CoreEditor::Ids());  // boot in Classic: core editors only

    ImGui_ImplSDL3_InitForVulkan(window_);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance_;
    init_info.PhysicalDevice = physicalDevice_;
    init_info.Device = device_;
    init_info.QueueFamily = queueFamily_;
    init_info.Queue = queue_;
    init_info.DescriptorPool = descriptorPool_;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = mainWindowData_.ImageCount;
    init_info.Allocator = g_Allocator;
    init_info.PipelineInfoMain.RenderPass = mainWindowData_.RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    // The Preferences UI gets its OWN OS window (separate SDL+Vulkan window +
    // 2nd ImGui context), sharing our Vulkan instance/device/queue/pool. Its
    // content is drawn by settingsWindow_. Created hidden; Show() on demand.
    // Registered in secondaryWindows_ so event routing + per-frame render are
    // generic (future detached windows just add another instance here).
    {
        SecondaryWindow::VulkanShared sh;
        sh.instance       = instance_;
        sh.physicalDevice = physicalDevice_;
        sh.device         = device_;
        sh.queue          = queue_;
        sh.queueFamily    = queueFamily_;
        sh.descriptorPool = descriptorPool_;
        sh.minImageCount  = g_MinImageCount;
        SecondaryWindow::Config cfg;
        cfg.title = "Preferences"; cfg.width = 940; cfg.height = 660;
        settingsHost_.Init(sh, mainScale_, cfg,
                           [this](bool* open){ settingsWindow_.Render(open); });
        // Run the shared shortcut pipeline inside the Preferences context too,
        // so shortcuts work there exactly like in the main window (the
        // ShortcutManager/EventNormalizer are singletons reading the current
        // context's IO). `pre` drains IO + resets the per-frame context; `post`
        // dispatches, gated by whether this window holds keyboard focus.
        settingsHost_.SetShortcutHooks(
            /*pre=*/[]{
                auto& sm = Shortcuts::ShortcutManager::Instance();
                auto& norm = Shortcuts::EventNormalizer::Instance();
                try {
                    float t = DesignSystem::DesignSystem::Instance()
                                .GetFloat(DesignSystem::Tok::S_Config_DragThreshold);
                    norm.SetDragThreshold(t);
                } catch (...) {}
                norm.Frame();
                sm.BeginFrame();
            },
            /*post=*/[this](bool focused){
                auto& sm = Shortcuts::ShortcutManager::Instance();
                // Route undo/redo to the Preferences history while dispatching
                // this window's shortcuts.
                activeUndoTarget_ = UndoTarget::Preferences;
                sm.SetWindowFocused(focused);
                sm.ProcessInput();
                // Capture an undo step for any override edit made this frame.
                CommitPrefsUndoIfChanged();
                activeUndoTarget_ = UndoTarget::Viewport;   // back to default
            });
        secondaryWindows_.push_back(&settingsHost_);
    }

    // The Token Graph editor gets its own detached OS window too, on the exact
    // same pattern as Preferences (shared Vulkan handles, 2nd ImGui context,
    // shared shortcut pipeline). Created hidden; Show() on demand.
    {
        SecondaryWindow::VulkanShared sh;
        sh.instance       = instance_;
        sh.physicalDevice = physicalDevice_;
        sh.device         = device_;
        sh.queue          = queue_;
        sh.queueFamily    = queueFamily_;
        sh.descriptorPool = descriptorPool_;
        sh.minImageCount  = g_MinImageCount;
        SecondaryWindow::Config cfg;
        cfg.title = "Token Graph"; cfg.width = 1280; cfg.height = 820;
        tokenGraphHost_.Init(sh, mainScale_, cfg,
                             [this](bool* open){ tokenGraphWindow_.Render(open); });
        // Run the shared shortcut pipeline inside this context too, so pan/zoom
        // and Tab work here like in the main window. (Undo stays on the main
        // history for now — the graph edits go straight through OverrideManager.)
        tokenGraphHost_.SetShortcutHooks(
            /*pre=*/[]{
                auto& sm = Shortcuts::ShortcutManager::Instance();
                auto& norm = Shortcuts::EventNormalizer::Instance();
                try {
                    float t = DesignSystem::DesignSystem::Instance()
                                .GetFloat(DesignSystem::Tok::S_Config_DragThreshold);
                    norm.SetDragThreshold(t);
                } catch (...) {}
                norm.Frame();
                sm.BeginFrame();
            },
            /*post=*/[](bool focused){
                auto& sm = Shortcuts::ShortcutManager::Instance();
                sm.SetWindowFocused(focused);
                sm.ProcessInput();
            });
        secondaryWindows_.push_back(&tokenGraphHost_);
    }

    // A fresh launch opens an empty default project. (The document model and
    // its default page return with the Ink engine — docs/Ink/ROADMAP.md Lot 2.)
    project_.Reset();

    // Load the recent-files list (shown on the splash start screen).
    LoadRecentFiles();

    // Register the per-user Windows shell integration for .acu (extension icon +
    // thumbnail provider). Idempotent; needs the IconManager (resvg) ready.
    RegisterShellIntegration();

    // Backends are now live — the SDL event watch may render during resize.
    initialized_ = true;
    return true;
}

void Application::SetupVulkan() {
    ImVector<const char*> extensions;
    uint32_t sdl_extensions_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
    for (uint32_t n = 0; n < sdl_extensions_count; n++)
        extensions.push_back(sdl_extensions[n]);

    VkResult err;

    // Request the highest Vulkan version the loader supports, capped at 1.3 (the
    // Ink engine's baseline: dynamic rendering + synchronization2 are core
    // there — see docs/Ink/ARCHITECTURE.md). The loader gates core 1.3 entry
    // points on the instance's apiVersion. Falls back gracefully: a lower
    // version just leaves modernVulkanSupported_ = false.
    uint32_t instanceVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&instanceVersion) != VK_SUCCESS)
        instanceVersion = VK_API_VERSION_1_0;
    const uint32_t requestedApi =
        (instanceVersion >= VK_API_VERSION_1_3) ? VK_API_VERSION_1_3 : instanceVersion;
    VkApplicationInfo app_info = {};
    app_info.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Carto";
    app_info.apiVersion = requestedApi;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    uint32_t properties_count;
    ImVector<VkExtensionProperties> properties;
    vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
    properties.resize(properties_count);
    err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
    check_vk_result(err);

    if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
    extensions.push_back("VK_EXT_debug_report");
#endif

    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.Size);
    create_info.ppEnabledExtensionNames = extensions.Data;
    err = vkCreateInstance(&create_info, g_Allocator, &instance_);
    check_vk_result(err);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugReportCallbackEXT");
    IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
    VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
    debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | 
                           VK_DEBUG_REPORT_WARNING_BIT_EXT | 
                           VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    debug_report_ci.pfnCallback = debug_report;
    err = f_vkCreateDebugReportCallbackEXT(instance_, &debug_report_ci, g_Allocator, &g_DebugReport);
    check_vk_result(err);
#endif

    physicalDevice_ = ImGui_ImplVulkanH_SelectPhysicalDevice(instance_);
    queueFamily_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice_);

    {
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain");

        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, properties.Data);

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = { 1.0f };
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = queueFamily_;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;

        // ── Modern features for the Ink engine (Vulkan 1.3) ───────────────────
        // Query what the device supports, then enable only the intersection with
        // what we want — so vkCreateDevice never fails on an unsupported request.
        // modernVulkanSupported_ gates Ink's initialisation (Lot 1); enabling
        // supported features is harmless for ImGui's own rendering.
        VkPhysicalDeviceProperties devProps = {};
        vkGetPhysicalDeviceProperties(physicalDevice_, &devProps);

        VkPhysicalDeviceVulkan13Features feats13 = {};
        feats13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features feats12 = {};
        feats12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        feats12.pNext = &feats13;
        VkPhysicalDeviceFeatures2 feats2 = {};
        feats2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feats2.pNext = &feats12;

        const bool api13 = (requestedApi >= VK_API_VERSION_1_3) &&
                           (devProps.apiVersion >= VK_API_VERSION_1_3);
        if (api13) {
            vkGetPhysicalDeviceFeatures2(physicalDevice_, &feats2);
            modernVulkanSupported_ = feats13.dynamicRendering &&
                                     feats13.synchronization2 &&
                                     feats12.timelineSemaphore;
        }

        // Enable chain (only the features we actually use), kept alive until
        // vkCreateDevice below.
        VkPhysicalDeviceVulkan13Features en13 = {};
        en13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features en12 = {};
        en12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        en12.pNext = &en13;
        if (modernVulkanSupported_) {
            en13.dynamicRendering   = VK_TRUE;
            en13.synchronization2   = VK_TRUE;
            en12.timelineSemaphore  = VK_TRUE;
            // Descriptor indexing / bindless: enable when present (Ink texture table).
            en12.descriptorIndexing                           = feats12.descriptorIndexing;
            en12.runtimeDescriptorArray                       = feats12.runtimeDescriptorArray;
            en12.shaderSampledImageArrayNonUniformIndexing    = feats12.shaderSampledImageArrayNonUniformIndexing;
            en12.descriptorBindingPartiallyBound              = feats12.descriptorBindingPartiallyBound;
            en12.descriptorBindingSampledImageUpdateAfterBind = feats12.descriptorBindingSampledImageUpdateAfterBind;
        }

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pNext = modernVulkanSupported_ ? (void*)&en12 : nullptr;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.Size);
        create_info.ppEnabledExtensionNames = device_extensions.Data;

        err = vkCreateDevice(physicalDevice_, &create_info, g_Allocator, &device_);
        check_vk_result(err);
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    }

    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(device_, &pool_info, g_Allocator, &descriptorPool_);
        check_vk_result(err);
    }

    {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = queueFamily_;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        err = vkCreateCommandPool(device_, &pool_info, g_Allocator, &commandPool_);
        check_vk_result(err);
    }
}

void Application::SetupVulkanWindow() {
    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(window_, instance_, g_Allocator, &surface) == 0) {
        printf("Failed to create Vulkan surface.\n");
        abort();
    }

    int w, h;
    SDL_GetWindowSize(window_, &w, &h);

    ImGui_ImplVulkanH_Window* wd = &mainWindowData_;
    
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamily_, surface, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error no WSI support\n");
        abort();
    }

    const VkFormat requestSurfaceImageFormat[] = {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
    };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice_, wd->Surface,
        requestSurfaceImageFormat,
        static_cast<size_t>(IM_ARRAYSIZE(requestSurfaceImageFormat)),
        requestSurfaceColorSpace
    );

    VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        physicalDevice_, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes)
    );

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance_, physicalDevice_, device_, wd, queueFamily_,
        g_Allocator, w, h, g_MinImageCount, 0
    );
}

// NOTE: the per-zone demo look is now part of the THEME (authored as theme
// definitions in DesignSystem/Tokens/ThemeDefinition.cpp and installed by
// DesignSystem::Initialize) — it is no longer a set of user overrides.
//
// Register every editable zone/sub-zone scope up front so the theme editors
// list them all immediately (a scope otherwise only appears once its zone
// has been rendered at least once). Registering is cheap and side-effect
// free; it does not change rendering — a zone with no theme-def/override
// still cascades to the global look.
//
// SCOPE TREE (matches ThemeDefinition.cpp seeds + the ZoneStyle pushes in
// ApplicationUI / ApplicationWindows):
//
//   global                             (the implicit parent — never registered)
//   ├─ toolbar / menuBar / statusBar / mainContent       (structural chrome)
//   ├─ editors                          (all editors share this base look)
//   │  ├─ editors/viewport
//   │  ├─ editors/outliner
//   │  ├─ editors/timeline
//   │  └─ editors/devPanels
//   ├─ settings                         (the Settings floating window)
//   │  ├─ settings/designSystem
//   │  ├─ settings/tokenTree
//   │  ├─ settings/userTheme
//   │  ├─ settings/shortcuts
//   │  └─ settings/icons
//   ├─ devTest                          (the Dev Test floating window)
//   │  ├─ devTest/icons
//   │  ├─ devTest/design
//   │  │  └─ devTest/design/print
//   │  ├─ devTest/themePreview
//   │  ├─ devTest/zone1
//   │  │  └─ devTest/zone1/action
//   │  └─ devTest/zone2
//   │      └─ devTest/zone2/action
//   └─ <legacy demo scopes kept for backwards compatibility — see below>
static void RegisterAppScopes() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    auto R = [&](const char* path, const char* label) {
        ds.RegisterScope(path, label);
    };
    // Structural chrome
    R("toolbar",                 "Toolbar");
    R("menuBar",                 "Menu bar");
    R("statusBar",               "Status bar");
    R("mainContent",             "Main content");
    // Editor zones (Blender-style layout). The root "editors" scope groups
    // every editor; each editor kind is a sub-scope. Window padding is
    // forced to 0 on "editors" (see ThemeDefinition.cpp) so editor content
    // is flush; sub-scopes let a single editor be themed apart.
    R("editors",                 "All editors");
    R("editors/viewport",        "Viewport editor");
    R("editors/outliner",        "Outliner editor");
    R("editors/properties",      "Properties editor");
    R("editors/timeline",        "Timeline editor");
    R("editors/devPanels",       "Dev Panels editor");
    // Settings floating window + its tabs (each tab is a sub-scope so
    // restyling one tab does not leak to the others).
    R("settings",                "Settings window");
    R("settings/designSystem",   "Settings · Design System tab");
    R("settings/tokenTree",      "Settings · Token Tree tab");
    R("settings/userTheme",      "Settings · User Theme tab");
    R("settings/shortcuts",      "Settings · Shortcuts tab");
    R("settings/icons",          "Settings · Icons tab");
    // Dev Test floating window + each collapsible section as a sub-scope.
    R("devTest",                 "Dev Test window");
    R("devTest/icons",           "DevTest · Icon Test Lab");
    R("devTest/design",          "DevTest · Design System Example");
    R("devTest/design/print",    "DevTest · Print button");
    R("devTest/themePreview",    "DevTest · Theme Preview");
    R("devTest/zone1",           "DevTest · Test Zone 1");
    R("devTest/zone1/action",    "DevTest · Zone 1 action");
    R("devTest/zone2",           "DevTest · Test Zone 2");
    R("devTest/zone2/action",    "DevTest · Zone 2 action");
    // Legacy demo scope paths kept registered for backwards compatibility
    // with code paths that still push them; their theme-defs in
    // ThemeDefinition.cpp mirror the devTest/* variants so the visual is
    // identical at both spots.
    R("iconTestLab",             "(legacy) Icon Test Lab");
    R("designExample",           "(legacy) Design System Example");
    R("designExample/print",     "(legacy) Print button");
    R("themePreview",            "(legacy) Theme Preview");
    R("testZone1",               "(legacy) Test Zone 1");
    R("testZone1/action",        "(legacy) Action button");
    R("testZone2",               "(legacy) Test Zone 2");
    R("testZone2/action",        "(legacy) Action button");
}

void Application::InitializeSubsystems() {
    DesignSystem::DesignSystem::Instance().Initialize(mainScale_);
    RegisterAppScopes();
    Shortcuts::Tools::ToolManager::Instance().Initialize();
    Shortcuts::EventNormalizer::Instance().Initialize();
    Shortcuts::ShortcutManager::Instance().Initialize();

    // Fonts are now design-system driven: discover everything under
    // resources/fonts, and let the DS tokens pick the default
    // family/weight/size (see ApplyFontTokens()). No symbol-font fallback merge:
    // a complete primary face already covers the glyphs the app uses.
    auto& fonts = UI::FontManager::Instance();
    fonts.Initialize(mainScale_);
    fonts.DiscoverFonts("resources/fonts");
    ApplyFontTokens();

    VectorGraphics::IconManager::Instance().Initialize(
        device_, physicalDevice_, queue_, commandPool_, descriptorPool_
    );

    // The Ink render engine (docs/Ink/), adopting the shared device. Needs
    // the Vulkan 1.3 features detected in SetupVulkan; without them the app
    // shell still runs and the Viewport shows its placeholder.
    if (modernVulkanSupported_) {
        Ink::Renderer::InitInfo ii;
        ii.instance       = instance_;
        ii.physicalDevice = physicalDevice_;
        ii.device         = device_;
        ii.queue          = queue_;
        ii.queueFamily    = queueFamily_;
        // Absolute shader dir from the exe location (the IDE's CWD may be the
        // project root, where a relative "shaders/ink" would not resolve).
        ii.shaderDir = "shaders/ink";
        if (const char* base = SDL_GetBasePath())
            ii.shaderDir = std::string(base) + "shaders/ink";
        // Canvas textures register through ImGui's Vulkan backend; Ink itself
        // never touches ImGui (the hooks keep it headless-capable).
        ii.textures.user = nullptr;
        ii.textures.create = [](void*, VkSampler sampler, VkImageView view,
                                VkImageLayout layout) -> std::uint64_t {
            return (std::uint64_t)(intptr_t)
                ImGui_ImplVulkan_AddTexture(sampler, view, layout);
        };
        ii.textures.destroy = [](void*, std::uint64_t texture) {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)(intptr_t)texture);
        };
        ink_ = std::make_unique<Ink::Renderer>();
        if (!ink_->Initialize(ii)) {
            fprintf(stderr, "[ink] engine initialisation failed — "
                            "the Viewport stays on its placeholder\n");
            ink_.reset();
        }
    } else {
        fprintf(stderr, "[ink] Vulkan 1.3 features unavailable on this device "
                        "— the Viewport stays on its placeholder\n");
    }
}

void Application::ApplyFontTokens() {
    auto& fonts = UI::FontManager::Instance();
    auto& ds    = DesignSystem::DesignSystem::Instance();

    // Discovered families get default role assignments (sans/serif/mono/cjk)
    // from name heuristics. Tokens then select a role; the user can re-map.
    fonts.AutoAssignRoles();

    // The default UI font follows the BODY role: the family NAME and the
    // weight come from design-system tokens (font.family.body resolves to a
    // FontFamily value; font.weight.body.m to an Int). Falls back gracefully.
    std::string family = "NotoSans";
    int weight = 400;
    try {
        auto v = ds.ResolveTokenValue(DesignSystem::TokIdStr(DesignSystem::Tok::S_FontFamily_Body),
                                      ds.GetCurrentContext().GetTheme());
        if (v.GetType() == DesignSystem::ValueType::FontFamily)
            family = v.AsFontFamily();
    } catch (...) {}
    try { weight = ds.GetInt(DesignSystem::Tok::S_FontWeight_BodyM); } catch (...) {}

    // If the named family wasn't discovered, fall back to the sans role, then
    // to any discovered family.
    if (!fonts.Family(family)) {
        std::string sans = fonts.RoleFamily(0);
        if (!sans.empty()) family = sans;
        else if (!fonts.FamilyNames().empty()) family = fonts.FamilyNames().front();
    }
    // Only rebuild the default font when the resolved (family, weight) changed.
    // ApplyFontTokens() is called every frame, so this guard keeps it cheap
    // while making token edits take effect live.
    if (!family.empty() &&
        (family != lastFontFamily_ || weight != lastFontWeight_)) {
        fonts.SetDefaultFont(family, weight);
        lastFontFamily_ = family;
        lastFontWeight_ = weight;
    }
}

void Application::LoadResources() {
    // Load icons from compile-time generated data
    VectorGraphics::IconManager::Instance().LoadCompiledIcons();
}


void Application::Shutdown() {
    VkResult err = vkDeviceWaitIdle(device_);
    check_vk_result(err);

    // Tear down the detached OS windows (their swapchain/backends/2nd context)
    // while the shared Vulkan device is still alive.
    settingsHost_.Shutdown();
    tokenGraphHost_.Shutdown();

    // Ink teardown: before ImGui's Vulkan backend dies (the texture-destroy
    // hooks call ImGui_ImplVulkan_RemoveTexture) and before the shared device
    // is destroyed.
    if (ink_) { ink_->Shutdown(); ink_.reset(); }

    DesignSystem::DesignSystem::Instance().Shutdown();
    Shortcuts::ShortcutManager::Instance().Shutdown();
    Shortcuts::Tools::ToolManager::Instance().Shutdown();
    VectorGraphics::IconManager::Instance().Shutdown();

    vkDestroyCommandPool(device_, commandPool_, g_Allocator);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &mainWindowData_, g_Allocator);
    vkDestroySurfaceKHR(instance_, mainWindowData_.Surface, g_Allocator);

    vkDestroyDescriptorPool(device_, descriptorPool_, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(instance_, g_DebugReport, g_Allocator);
#endif

    vkDestroyDevice(device_, g_Allocator);
    vkDestroyInstance(instance_, g_Allocator);

    SDL_DestroyWindow(window_);
    SDL_Quit();
}

// ── Home-grown maximize (taskbar-aware, no OS maximize) ───────────────────────
// We never use SDL_MaximizeWindow on the borderless window: SDL 3.4's Windows
// backend reports the FULL monitor for a borderless maximize (covers the
// taskbar) and resizing afterwards desyncs the swapchain. Instead we size the
// window to the display's usable bounds ourselves and remember the prior rect.
void Application::SetMaximized(bool on) {
    if (on == maximized_) return;
    // Our own SDL_SetWindowPosition/Size below emit SDL_EVENT_WINDOW_MOVED; the
    // event watch must ignore those, or it would immediately "restore" the
    // window we are in the middle of maximizing.
    programmaticMove_ = true;
    if (on) {
        SDL_GetWindowPosition(window_, &restoreRect_.x, &restoreRect_.y);
        SDL_GetWindowSize(window_, &restoreRect_.w, &restoreRect_.h);
        SDL_Rect ub{};
        SDL_DisplayID disp = SDL_GetDisplayForWindow(window_);
        if (disp == 0) disp = SDL_GetPrimaryDisplay();
        if (!SDL_GetDisplayUsableBounds(disp, &ub)) { programmaticMove_ = false; return; }
        SDL_SetWindowPosition(window_, ub.x, ub.y);
        SDL_SetWindowSize(window_, ub.w, ub.h);
        maximized_ = true;
    } else {
        if (restoreRect_.w > 0 && restoreRect_.h > 0) {
            SDL_SetWindowPosition(window_, restoreRect_.x, restoreRect_.y);
            SDL_SetWindowSize(window_, restoreRect_.w, restoreRect_.h);
        }
        maximized_ = false;
    }
    programmaticMove_ = false;
}

// Restore a maximized window mid-drag while keeping the grabbed bar point under
// the cursor. A plain SetMaximized(false) snaps the window back to its old
// restore position, yanking it away from the still-running native drag.
// Instead we measure the cursor's fractional X across the maximized window,
// drop to the restored size, then place the window so that same fraction lands
// under the cursor. (Fullscreen is intentionally not handled here — see the
// SDL_EVENT_WINDOW_MOVED watch.)
void Application::RestoreFromDragAtCursor() {
    if (!maximized_) return;
    if (restoreRect_.w <= 0 || restoreRect_.h <= 0) { SetMaximized(false); return; }

    // Global cursor + the maximized window's current rect (before restoring).
    float gx = 0.0f, gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    SDL_Rect cur{};
    SDL_GetWindowPosition(window_, &cur.x, &cur.y);
    SDL_GetWindowSize(window_, &cur.w, &cur.h);
    float fx = (cur.w > 0) ? (gx - (float)cur.x) / (float)cur.w : 0.5f;

    programmaticMove_ = true;
    SDL_SetWindowSize(window_, restoreRect_.w, restoreRect_.h);
    int newX = (int)(gx - fx * (float)restoreRect_.w);
    int newY = cur.y;   // top stays put (the bar sits at the window top)
    SDL_SetWindowPosition(window_, newX, newY);
    maximized_ = false;
    programmaticMove_ = false;
}

// A caption double-click or drag-to-top makes Windows maximize the window
// (HTCAPTION is hard-wired to that, and our hit-test reports the bar as a
// caption). The OS maximize covers the taskbar, so we always cancel it and
// substitute our own state. This is also the ONLY reliable signal for a
// caption double-click: on a DRAGGABLE area the native move grabs the mouse at
// button-down, so neither ImGui nor SDL_PollEvent ever sees the click. So we
// treat this as a TOGGLE: if already filled (our maximize or fullscreen), the
// double-click means "restore"; otherwise it means "maximize".
void Application::InterceptOsMaximize() {
    if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED)) return;
    SDL_RestoreWindow(window_);            // cancel the OS maximize (taskbar band)
    if (fullscreen_) {                     // double-click while fullscreen → exit
        ToggleFullscreen();
    } else {
        SetMaximized(!maximized_);         // toggle our usable-bounds maximize
    }
}

// F11: borderless fullscreen-desktop (current resolution, no mode switch). The
// title bar stays visible — it is drawn by us, not the OS.
void Application::ToggleFullscreen() {
    fullscreen_ = !fullscreen_;
    // Desktop (borderless) fullscreen: the mode must be NULL *before* we enter,
    // or SDL keeps the windowed size and offsets it to the top-left corner.
    SDL_SetWindowFullscreenMode(window_, nullptr);
    SDL_SetWindowFullscreen(window_, fullscreen_);
}

// ── Borderless window hit-test ────────────────────────────────────────────────
// SDL calls this for every mouse interaction on a borderless window to decide
// whether the OS should start a native move/resize. `area` is in window
// coordinates (the same logical space ImGui uses), so titleBarBlockers_ and
// titleBarHeightPx_ are stored in that space too — no DPI conversion needed.
SDL_HitTestResult SDLCALL Application::HitTestCallback(
    SDL_Window* win, const SDL_Point* area, void* data) {
    auto* app = static_cast<Application*>(data);

    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    // Disable resize borders while maximized or fullscreen (our own state).
    const bool maximized = app->maximized_ || app->fullscreen_;

    // Resize borders (skip when maximized — no phantom edges).
    if (!maximized) {
        const int b = 6;   // grab thickness in logical px
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
    }

    // In fullscreen the bar is NOT draggable: a fullscreen-desktop window
    // can't be meaningfully moved, and reporting it as a caption lets Windows
    // start a native move that slides the window off without changing state.
    // Leave fullscreen via F11 or the restore button instead.
    if (app->fullscreen_)
        return SDL_HITTEST_NORMAL;

    // Inside the title bar: draggable, except over an interactive widget.
    if ((float)area->y < app->titleBarHeightPx_) {
        for (const SDL_Rect& r : app->titleBarBlockers_) {
            if (area->x >= r.x && area->x < r.x + r.w &&
                area->y >= r.y && area->y < r.y + r.h)
                return SDL_HITTEST_NORMAL;   // let the widget handle the click
        }
        return SDL_HITTEST_DRAGGABLE;        // empty bar background drags window
    }

    return SDL_HITTEST_NORMAL;
}

} // namespace App
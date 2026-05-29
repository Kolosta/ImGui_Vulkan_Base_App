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

    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | 
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

    // Open maximized on first launch. The window is still HIDDEN; maximizing
    // now updates its size so SetupVulkanWindow()'s SDL_GetWindowSize() returns
    // the maximized dimensions and the swapchain is sized correctly for the
    // very first frame (no black flash / cropped frame on show).
    SDL_MaximizeWindow(window_);

    SetupVulkan();
    SetupVulkanWindow();

    // On Windows the OS modal resize loop blocks SDL_PollEvent, so the normal
    // ProcessEvents/Update/Render/Present loop never runs during live resize
    // → black / cropped frame until the user releases. This event watch fires
    // synchronously from inside the OS loop and renders a full frame each time
    // the window is resized or exposed, keeping the content live.
    SDL_AddEventWatch([](void* userdata, SDL_Event* ev) -> bool {
        auto* app = static_cast<Application*>(userdata);
        // Ignore events fired during init (maximize/show happen before the
        // ImGui backends exist — rendering then would assert).
        if (app->initialized_ &&
            (ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             ev->type == SDL_EVENT_WINDOW_EXPOSED)) {
            app->RenderFrame();
        }
        return false;
    }, this);

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
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

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale_);
    style.FontScaleDpi = 1.0f;  // fonts are loaded at physical pixel size; no post-raster DPI zoom

    InitializeSubsystems();
    LoadResources();
    RegisterDefaultShortcuts();

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
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

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

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

void Application::RegisterDefaultShortcuts() {
    using namespace Shortcuts;
    auto& sm = ShortcutManager::Instance();
    auto& tm = Tools::ToolManager::Instance();

    auto sigKey = [](ImGuiKey key, bool ctrl=false, bool shift=false, bool alt=false) {
        EventSignature s;
        s.type = EventType::KeyPress;
        s.key  = key;
        s.modifiers.ctrl = ctrl;
        s.modifiers.shift = shift;
        s.modifiers.alt  = alt;
        return s;
    };

    // ── Application ──────────────────────────────────────────────────────────
    {
        Action a;
        a.id = "app.quit";
        a.name = "Quit";
        a.description = "Close the application";
        a.category = ActionCategory::Application;
        a.callback = [this]{ Action_Quit(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Q, true) });
    }
    {
        Action a;
        a.id = "app.toggleSettings";
        a.name = "Toggle Settings";
        a.description = "Show or hide the Settings window";
        a.category = ActionCategory::Application;
        a.callback = [this]{ Action_ToggleSettings(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F1) });
    }

    // ── File ─────────────────────────────────────────────────────────────────
    {
        Action a; a.id = "file.new"; a.name = "New File"; a.description = "Create a new file";
        a.category = ActionCategory::File; a.callback = &Application::Action_NewFile;
        sm.RegisterAction(a, { sigKey(ImGuiKey_N, true) });
    }
    {
        Action a; a.id = "file.open"; a.name = "Open File"; a.description = "Open an existing file";
        a.category = ActionCategory::File; a.callback = &Application::Action_OpenFile;
        sm.RegisterAction(a, { sigKey(ImGuiKey_O, true) });
    }
    {
        Action a; a.id = "file.save"; a.name = "Save File"; a.description = "Save the current file";
        a.category = ActionCategory::File; a.callback = &Application::Action_SaveFile;
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, true) });
    }

    // ── View ─────────────────────────────────────────────────────────────────
    {
        Action a; a.id = "view.toggleDemo"; a.name = "ImGui Demo";
        a.description = "Show the ImGui demo window";
        a.category = ActionCategory::View;
        a.callback = [this]{ Action_ToggleImGuiDemo(); };
        // F12 conflicte parfois avec des hotkeys système (devtools, etc.) :
        // par défaut on prend Ctrl+Shift+D, plus sûr.
        sm.RegisterAction(a, { sigKey(ImGuiKey_D, /*ctrl=*/true, /*shift=*/true) });
    }

    // ── Tools ────────────────────────────────────────────────────────────────
    tm.RegisterTool({"tool.brush",  "Brush Tool",  "pen",        {"tool.brush.activate"}});
    tm.RegisterTool({"tool.eraser", "Eraser Tool", "ink-eraser", {"tool.eraser.activate"}});
    tm.RegisterTool({"tool.hand",   "Hand Tool",   "draw",       {"tool.hand.activate"}});

    {
        Action a; a.id = "tool.brush.activate"; a.name = "Activate Brush";
        a.description = "Activate the brush tool";
        a.category = ActionCategory::Tool;
        // Scoped to the Viewport: only fires when the mouse is over the
        // Viewport zone that registered "viewport" this frame, not any zone.
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateTool1(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_1) });
    }
    {
        Action a; a.id = "tool.eraser.activate"; a.name = "Activate Eraser";
        a.description = "Activate the eraser tool";
        a.category = ActionCategory::Tool;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateTool2(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_2) });
    }
    {
        Action a; a.id = "tool.hand.activate"; a.name = "Activate Hand";
        a.description = "Pan the canvas by dragging with the mouse";
        a.category = ActionCategory::Tool;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateHand(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_H) });
    }
    {
        Action a; a.id = "view.fitDocument"; a.name = "Fit Document in View";
        a.description = "Zoom/pan so the whole document is visible";
        a.category = ActionCategory::View;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ViewFitDocument(); };
        // Shift+C and Ctrl+Numpad0.
        sm.RegisterAction(a, { sigKey(ImGuiKey_C, false, true) });
    }
    {
        Action a; a.id = "view.resetOrigin"; a.name = "Reset View to Origin";
        a.description = "Recenter near the document origin at 100% zoom";
        a.category = ActionCategory::View;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ViewResetOrigin(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Keypad0, true) });
    }
    {
        Action a; a.id = "file.newDocument"; a.name = "New Document";
        a.description = "Create a new blank document / artboard";
        a.category = ActionCategory::File;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_NewDocument(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_N, true, true) });
    }
    {
        Action a; a.id = "tool.cycleNext"; a.name = "Next Tool";
        a.description = "Switch to the next tool in the toolbar";
        a.category = ActionCategory::Tool;
        a.requiredContext.region = "toolbar";
        a.callback = [this]{ Action_CycleTool(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab) });
    }

    // ── Editor switch shortcuts (Blender-style) ──────────────────────────────
    // Switch the editor kind of the zone under the mouse. No requiredContext:
    // they fire over any zone, targeting the hovered leaf (ZoneLayout resolves
    // it each frame). The editor-selector dropdown shows these bindings.
    {
        Action a; a.id = "editor.viewport"; a.name = "Viewport Editor";
        a.description = "Show the Viewport editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(EditorKind::Viewport); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F5, false, true) });
    }
    {
        Action a; a.id = "editor.outliner"; a.name = "Outliner Editor";
        a.description = "Show the Outliner editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(EditorKind::Outliner); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F9, false, true) });
    }
    {
        Action a; a.id = "editor.timeline"; a.name = "Timeline Editor";
        a.description = "Show the Timeline editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(EditorKind::Timeline); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F12, false, true) });
    }
    {
        Action a; a.id = "editor.devPanels"; a.name = "Dev Panels Editor";
        a.description = "Show the Dev Panels editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(EditorKind::DevPanels); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F11, false, true) });
    }

    // ── Tab navigation (multi-tab zones) ─────────────────────────────────────
    // Next/Previous cycle the tabs of the HOVERED zone; First/Last jump within
    // the ACTIVE zone (the last one clicked).
    {
        Action a; a.id = "editor.tabNext"; a.name = "Next Tab";
        a.description = "Activate the next tab in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.HoveredTabCycle(+1); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab, true) });
    }
    {
        Action a; a.id = "editor.tabPrev"; a.name = "Previous Tab";
        a.description = "Activate the previous tab in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.HoveredTabCycle(-1); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab, true, true) });
    }
    {
        Action a; a.id = "editor.tabFirst"; a.name = "First Tab";
        a.description = "Activate the first tab in the active zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.ActiveTabSelectEdge(false); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Home, true) });
    }
    {
        Action a; a.id = "editor.tabLast"; a.name = "Last Tab";
        a.description = "Activate the last tab in the active zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.ActiveTabSelectEdge(true); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_End, true) });
    }

    // ── Zone-specific actions (same key, different editors) ──────────────────
    {
        Action a; a.id = "edit.themePreview.cycle"; a.name = "Cycle Theme";
        a.description = "Cycle through themes (active in the Theme Preview area)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "themePreview";
        a.callback = &Application::Action_ThemePreviewCycle;
        sm.RegisterAction(a, { sigKey(ImGuiKey_T) });
    }
    {
        Action a; a.id = "edit.testZone1.action"; a.name = "Zone 1 Action";
        a.description = "Action scoped to test zone 1";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "testZone1";
        a.callback = &Application::Action_Zone1;
        sm.RegisterAction(a, { sigKey(ImGuiKey_A) });
    }
    {
        Action a; a.id = "edit.testZone2.action"; a.name = "Zone 2 Action";
        a.description = "Action scoped to test zone 2";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "testZone2";
        a.callback = &Application::Action_Zone2;
        sm.RegisterAction(a, { sigKey(ImGuiKey_A) });
    }

    // re-save once after registering everything so freshly added defaults
    // are persisted (Load happened before Register, so defaults are missing
    // from disk on first run).
    sm.Save();
}

void Application::Shutdown() {
    VkResult err = vkDeviceWaitIdle(device_);
    check_vk_result(err);

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

} // namespace App
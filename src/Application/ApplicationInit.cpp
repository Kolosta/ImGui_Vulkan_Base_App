#include "Application.h"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/FontManager.h>
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

    SetupVulkan();
    SetupVulkanWindow();

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale_);
    style.FontScaleDpi = mainScale_;

    InitializeSubsystems();
    LoadResources();
    RegisterTestShortcuts();

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

void Application::InitializeSubsystems() {
    DesignSystem::DesignSystem::Instance().Initialize();
    Shortcuts::ShortcutManager::Instance().Initialize();
    
    UI::FontManager::Instance().Initialize();
    if (UI::FontManager::Instance().LoadFont("qanelas", "resources/fonts/Qanelas-Regular.otf", 14.0f)) {
        UI::FontManager::Instance().SetDefaultFont("qanelas");
    }
    
    VectorGraphics::IconManager::Instance().Initialize(
        device_, physicalDevice_, queue_, commandPool_, descriptorPool_
    );
}

void Application::LoadResources() {
    // Load icons from compile-time generated data
    VectorGraphics::IconManager::Instance().LoadCompiledIcons();
}

void Application::RegisterTestShortcuts() {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    
    sm.RegisterAction(Shortcuts::Action("action.new", "New File", 
        Shortcuts::ShortcutZone::Global, TestAction_NewFile));
    sm.SetBinding("action.new", {{Shortcuts::KeyCombination(ImGuiKey_N, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("action.open", "Open File",
        Shortcuts::ShortcutZone::Global, TestAction_OpenFile));
    sm.SetBinding("action.open", {{Shortcuts::KeyCombination(ImGuiKey_O, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("action.save", "Save File",
        Shortcuts::ShortcutZone::Global, TestAction_SaveFile));
    sm.SetBinding("action.save", {{Shortcuts::KeyCombination(ImGuiKey_S, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("action.quit", "Quit",
        Shortcuts::ShortcutZone::Global, TestAction_Quit));
    sm.SetBinding("action.quit", {{Shortcuts::KeyCombination(ImGuiKey_Q, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("tool.1", "Tool 1",
        Shortcuts::ShortcutZone::Toolbar, TestAction_Tool1));
    sm.SetBinding("tool.1", {{Shortcuts::KeyCombination(ImGuiKey_1, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("tool.2", "Tool 2",
        Shortcuts::ShortcutZone::Toolbar, TestAction_Tool2));
    sm.SetBinding("tool.2", {{Shortcuts::KeyCombination(ImGuiKey_2, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("zone1.action", "Zone 1 Action",
        Shortcuts::ShortcutZone::TestZone1, TestAction_Zone1, true));
    sm.SetBinding("zone1.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});
    
    sm.RegisterAction(Shortcuts::Action("zone2.action", "Zone 2 Action",
        Shortcuts::ShortcutZone::TestZone2, TestAction_Zone2, true));
    sm.SetBinding("zone2.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});
}

void Application::Shutdown() {
    VkResult err = vkDeviceWaitIdle(device_);
    check_vk_result(err);

    DesignSystem::DesignSystem::Instance().Shutdown();
    Shortcuts::ShortcutManager::Instance().Shutdown();
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
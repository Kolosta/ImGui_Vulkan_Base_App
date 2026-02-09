// // // // #include "Application.h"
// // // // #include <imgui_impl_sdl3.h>
// // // // #include <imgui_impl_vulkan.h>
// // // // #include <iostream>
// // // // #include <algorithm>
// // // // #include <SDL3/SDL_vulkan.h>

// // // // #ifdef _DEBUG
// // // // #define APP_USE_VULKAN_DEBUG_REPORT
// // // // static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
// // // // #endif

// // // // static VkAllocationCallbacks* g_Allocator = nullptr;
// // // // static uint32_t g_MinImageCount = 2;
// // // // static bool g_SwapChainRebuild = false;

// // // // namespace App {

// // // // // ========== Vulkan Helpers ==========

// // // // static void check_vk_result(VkResult err) {
// // // //     if (err == VK_SUCCESS) return;
// // // //     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
// // // //     if (err < 0) abort();
// // // // }

// // // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // // static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
// // // //     VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
// // // //     uint64_t object, size_t location, int32_t messageCode,
// // // //     const char* pLayerPrefix, const char* pMessage, void* pUserData)
// // // // {
// // // //     (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
// // // //     fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
// // // //     return VK_FALSE;
// // // // }
// // // // #endif

// // // // static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
// // // //     for (const VkExtensionProperties& p : properties)
// // // //         if (strcmp(p.extensionName, extension) == 0)
// // // //             return true;
// // // //     return false;
// // // // }

// // // // // ========== Application Implementation ==========

// // // // Application::Application() {}

// // // // Application::~Application() {}

// // // // bool Application::Initialize() {
// // // //     // Initialize SDL
// // // //     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
// // // //         printf("Error: SDL_Init(): %s\n", SDL_GetError());
// // // //         return false;
// // // //     }

// // // //     mainScale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

// // // //     // Create window
// // // //     SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
// // // //                                    SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
// // // //     window_ = SDL_CreateWindow(
// // // //         "Carto - Design System Demo",
// // // //         static_cast<int>(1280 * mainScale_),
// // // //         static_cast<int>(800 * mainScale_),
// // // //         window_flags
// // // //     );

// // // //     if (!window_) {
// // // //         printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
// // // //         return false;
// // // //     }

// // // //     // Setup Vulkan
// // // //     SetupVulkan();
// // // //     SetupVulkanWindow();

// // // //     SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
// // // //     SDL_ShowWindow(window_);

// // // //     // Setup Dear ImGui
// // // //     IMGUI_CHECKVERSION();
// // // //     ImGui::CreateContext();
// // // //     ImGuiIO& io = ImGui::GetIO();
// // // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
// // // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
// // // //     io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

// // // //     ImGui::StyleColorsDark();

// // // //     ImGuiStyle& style = ImGui::GetStyle();
// // // //     style.ScaleAllSizes(mainScale_);
// // // //     style.FontScaleDpi = mainScale_;

// // // //     // Initialize subsystems
// // // //     InitializeSubsystems();
// // // //     LoadResources();
// // // //     RegisterTestShortcuts();

// // // //     // Setup Platform/Renderer backends
// // // //     ImGui_ImplSDL3_InitForVulkan(window_);
// // // //     ImGui_ImplVulkan_InitInfo init_info = {};
// // // //     init_info.Instance = instance_;
// // // //     init_info.PhysicalDevice = physicalDevice_;
// // // //     init_info.Device = device_;
// // // //     init_info.QueueFamily = queueFamily_;
// // // //     init_info.Queue = queue_;
// // // //     init_info.DescriptorPool = descriptorPool_;
// // // //     init_info.MinImageCount = g_MinImageCount;
// // // //     init_info.ImageCount = mainWindowData_.ImageCount;
// // // //     init_info.Allocator = g_Allocator;
// // // //     init_info.PipelineInfoMain.RenderPass = mainWindowData_.RenderPass;
// // // //     init_info.PipelineInfoMain.Subpass = 0;
// // // //     init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
// // // //     init_info.CheckVkResultFn = check_vk_result;
// // // //     ImGui_ImplVulkan_Init(&init_info);

// // // //     return true;
// // // // }

// // // // void Application::SetupVulkan() {
// // // //     ImVector<const char*> extensions;
// // // //     uint32_t sdl_extensions_count = 0;
// // // //     const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
// // // //     for (uint32_t n = 0; n < sdl_extensions_count; n++)
// // // //         extensions.push_back(sdl_extensions[n]);

// // // //     VkResult err;
// // // //     VkInstanceCreateInfo create_info = {};
// // // //     create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

// // // //     uint32_t properties_count;
// // // //     ImVector<VkExtensionProperties> properties;
// // // //     vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
// // // //     properties.resize(properties_count);
// // // //     err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
// // // //     check_vk_result(err);

// // // //     if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
// // // //         extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

// // // // #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
// // // //     if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
// // // //         extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
// // // //         create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
// // // //     }
// // // // #endif

// // // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // //     const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
// // // //     create_info.enabledLayerCount = 1;
// // // //     create_info.ppEnabledLayerNames = layers;
// // // //     extensions.push_back("VK_EXT_debug_report");
// // // // #endif

// // // //     create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.Size);
// // // //     create_info.ppEnabledExtensionNames = extensions.Data;
// // // //     err = vkCreateInstance(&create_info, g_Allocator, &instance_);
// // // //     check_vk_result(err);

// // // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // //     auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
// // // //         vkGetInstanceProcAddr(instance_, "vkCreateDebugReportCallbackEXT");
// // // //     IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
// // // //     VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
// // // //     debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
// // // //     debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
// // // //                            VK_DEBUG_REPORT_WARNING_BIT_EXT |
// // // //                            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
// // // //     debug_report_ci.pfnCallback = debug_report;
// // // //     debug_report_ci.pUserData = nullptr;
// // // //     err = f_vkCreateDebugReportCallbackEXT(instance_, &debug_report_ci, g_Allocator, &g_DebugReport);
// // // //     check_vk_result(err);
// // // // #endif

// // // //     physicalDevice_ = ImGui_ImplVulkanH_SelectPhysicalDevice(instance_);
// // // //     queueFamily_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice_);

// // // //     // Create logical device
// // // //     {
// // // //         ImVector<const char*> device_extensions;
// // // //         device_extensions.push_back("VK_KHR_swapchain");

// // // //         uint32_t properties_count;
// // // //         ImVector<VkExtensionProperties> properties;
// // // //         vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, nullptr);
// // // //         properties.resize(properties_count);
// // // //         vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, properties.Data);

// // // // #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
// // // //         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
// // // //             device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
// // // // #endif

// // // //         const float queue_priority[] = { 1.0f };
// // // //         VkDeviceQueueCreateInfo queue_info[1] = {};
// // // //         queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
// // // //         queue_info[0].queueFamilyIndex = queueFamily_;
// // // //         queue_info[0].queueCount = 1;
// // // //         queue_info[0].pQueuePriorities = queue_priority;

// // // //         VkDeviceCreateInfo create_info = {};
// // // //         create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
// // // //         create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
// // // //         create_info.pQueueCreateInfos = queue_info;
// // // //         create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.Size);
// // // //         create_info.ppEnabledExtensionNames = device_extensions.Data;

// // // //         err = vkCreateDevice(physicalDevice_, &create_info, g_Allocator, &device_);
// // // //         check_vk_result(err);
// // // //         vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
// // // //     }

// // // //     // Create descriptor pool
// // // //     {
// // // //         VkDescriptorPoolSize pool_sizes[] = {
// // // //             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
// // // //         };
// // // //         VkDescriptorPoolCreateInfo pool_info = {};
// // // //         pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
// // // //         pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
// // // //         pool_info.maxSets = 1000;
// // // //         pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
// // // //         pool_info.pPoolSizes = pool_sizes;
// // // //         err = vkCreateDescriptorPool(device_, &pool_info, g_Allocator, &descriptorPool_);
// // // //         check_vk_result(err);
// // // //     }

// // // //     // Create command pool
// // // //     {
// // // //         VkCommandPoolCreateInfo pool_info{};
// // // //         pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
// // // //         pool_info.queueFamilyIndex = queueFamily_;
// // // //         pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
// // // //         err = vkCreateCommandPool(device_, &pool_info, g_Allocator, &commandPool_);
// // // //         check_vk_result(err);
// // // //     }
// // // // }

// // // // void Application::SetupVulkanWindow() {
// // // //     VkSurfaceKHR surface;
// // // //     if (SDL_Vulkan_CreateSurface(window_, instance_, g_Allocator, &surface) == 0) {
// // // //         printf("Failed to create Vulkan surface.\n");
// // // //         abort();
// // // //     }

// // // //     int w, h;
// // // //     SDL_GetWindowSize(window_, &w, &h);

// // // //     ImGui_ImplVulkanH_Window* wd = &mainWindowData_;

// // // //     VkBool32 res;
// // // //     vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamily_, surface, &res);
// // // //     if (res != VK_TRUE) {
// // // //         fprintf(stderr, "Error no WSI support on physical device 0\n");
// // // //         abort();
// // // //     }

// // // //     const VkFormat requestSurfaceImageFormat[] = {
// // // //         VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
// // // //         VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
// // // //     };
// // // //     const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
// // // //     wd->Surface = surface;
// // // //     wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
// // // //         physicalDevice_, wd->Surface,
// // // //         requestSurfaceImageFormat,
// // // //         static_cast<size_t>(IM_ARRAYSIZE(requestSurfaceImageFormat)),
// // // //         requestSurfaceColorSpace
// // // //     );

// // // //     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
// // // //     wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
// // // //         physicalDevice_, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes)
// // // //     );

// // // //     ImGui_ImplVulkanH_CreateOrResizeWindow(
// // // //         instance_, physicalDevice_, device_, wd, queueFamily_,
// // // //         g_Allocator, w, h, g_MinImageCount, 0
// // // //     );
// // // // }

// // // // void Application::InitializeSubsystems() {
// // // //     // Design System
// // // //     auto& designSystem = DesignSystem::DesignSystem::Instance();
// // // //     designSystem.Initialize();

// // // //     // Shortcut Manager
// // // //     Shortcuts::ShortcutManager::Instance().Initialize();

// // // //     // Font Manager
// // // //     ImGuiIO& io = ImGui::GetIO();
// // // //     UI::FontManager::Instance().Initialize();

// // // //     // Charger Qanelas
// // // //     if (UI::FontManager::Instance().LoadFont("qanelas", "resources/fonts/Qanelas-Regular.otf", 14.0f)) {
// // // //         UI::FontManager::Instance().SetDefaultFont("qanelas");
// // // //     }

// // // //     // Icon Manager
// // // //     UI::IconManager::Instance().Initialize(
// // // //         device_, physicalDevice_, queue_, commandPool_, descriptorPool_
// // // //     );
// // // // }

// // // // void Application::LoadResources() {
// // // //     // Charger toutes les icônes depuis le dossier resources/icons
// // // //     UI::IconManager::Instance().LoadIconsFromDirectory("resources/icons");
// // // // }

// // // // void Application::RegisterTestShortcuts() {
// // // //     auto& sm = Shortcuts::ShortcutManager::Instance();

// // // //     // Actions globales
// // // //     sm.RegisterAction(Shortcuts::Action("action.new", "New File",
// // // //         Shortcuts::ShortcutZone::Global, TestAction_NewFile));
// // // //     sm.SetBinding("action.new", {{Shortcuts::KeyCombination(ImGuiKey_N, true, false, false)}});

// // // //     sm.RegisterAction(Shortcuts::Action("action.open", "Open File",
// // // //         Shortcuts::ShortcutZone::Global, TestAction_OpenFile));
// // // //     sm.SetBinding("action.open", {{Shortcuts::KeyCombination(ImGuiKey_O, true, false, false)}});

// // // //     sm.RegisterAction(Shortcuts::Action("action.save", "Save File",
// // // //         Shortcuts::ShortcutZone::Global, TestAction_SaveFile));
// // // //     sm.SetBinding("action.save", {{Shortcuts::KeyCombination(ImGuiKey_S, true, false, false)}});

// // // //     sm.RegisterAction(Shortcuts::Action("action.quit", "Quit",
// // // //         Shortcuts::ShortcutZone::Global, TestAction_Quit));
// // // //     sm.SetBinding("action.quit", {{Shortcuts::KeyCombination(ImGuiKey_Q, true, false, false)}});

// // // //     // Actions toolbar
// // // //     sm.RegisterAction(Shortcuts::Action("tool.1", "Tool 1",
// // // //         Shortcuts::ShortcutZone::Toolbar, TestAction_Tool1));
// // // //     sm.SetBinding("tool.1", {{Shortcuts::KeyCombination(ImGuiKey_1, true, false, false)}});

// // // //     sm.RegisterAction(Shortcuts::Action("tool.2", "Tool 2",
// // // //         Shortcuts::ShortcutZone::Toolbar, TestAction_Tool2));
// // // //     sm.SetBinding("tool.2", {{Shortcuts::KeyCombination(ImGuiKey_2, true, false, false)}});

// // // //     // Actions zones de test avec priorité
// // // //     sm.RegisterAction(Shortcuts::Action("zone1.action", "Zone 1 Action",
// // // //         Shortcuts::ShortcutZone::TestZone1, TestAction_Zone1, true));
// // // //     sm.SetBinding("zone1.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});

// // // //     sm.RegisterAction(Shortcuts::Action("zone2.action", "Zone 2 Action",
// // // //         Shortcuts::ShortcutZone::TestZone2, TestAction_Zone2, true));
// // // //     sm.SetBinding("zone2.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});
// // // // }

// // // // void Application::Run() {
// // // //     while (running_) {
// // // //         ProcessEvents();
// // // //         Update();
// // // //         Render();
// // // //         Present();
// // // //     }
// // // // }

// // // // void Application::ProcessEvents() {
// // // //     SDL_Event event;
// // // //     while (SDL_PollEvent(&event)) {
// // // //         ImGui_ImplSDL3_ProcessEvent(&event);
// // // //         if (event.type == SDL_EVENT_QUIT)
// // // //             running_ = false;
// // // //         if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
// // // //             event.window.windowID == SDL_GetWindowID(window_))
// // // //             running_ = false;
// // // //     }

// // // //     if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
// // // //         SDL_Delay(10);
// // // //         return;
// // // //     }

// // // //     // Handle window resize
// // // //     int fb_width, fb_height;
// // // //     SDL_GetWindowSize(window_, &fb_width, &fb_height);
// // // //     if (fb_width > 0 && fb_height > 0 &&
// // // //         (g_SwapChainRebuild || mainWindowData_.Width != fb_width || mainWindowData_.Height != fb_height)) {
// // // //         ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
// // // //         ImGui_ImplVulkanH_CreateOrResizeWindow(
// // // //             instance_, physicalDevice_, device_, &mainWindowData_,
// // // //             queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0
// // // //         );
// // // //         mainWindowData_.FrameIndex = 0;
// // // //         g_SwapChainRebuild = false;
// // // //     }
// // // // }

// // // // void Application::Update() {
// // // //     ImGui_ImplVulkan_NewFrame();
// // // //     ImGui_ImplSDL3_NewFrame();
// // // //     ImGui::NewFrame();

// // // //     // Process shortcuts
// // // //     Shortcuts::ShortcutManager::Instance().ProcessInput();

// // // //     // Render UI
// // // //     RenderDockSpace();
// // // //     RenderMainMenuBar();
// // // //     RenderToolbar();
// // // //     RenderIconTestWindow();
// // // //     RenderDesignExample();
// // // //     RenderThemePreview();
// // // //     RenderTestZone1();
// // // //     RenderTestZone2();
// // // //     RenderFloatingWindows();
// // // // }

// // // // void Application::Render() {
// // // //     ImGui::Render();
// // // //     ImDrawData* draw_data = ImGui::GetDrawData();
// // // //     const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
// // // //     if (is_minimized) return;

// // // //     auto& ds = DesignSystem::DesignSystem::Instance();
// // // //     ImVec4 clear_color = ds.GetColor("semantic.color.background");
// // // //     mainWindowData_.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
// // // //     mainWindowData_.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
// // // //     mainWindowData_.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
// // // //     mainWindowData_.ClearValue.color.float32[3] = clear_color.w;

// // // //     VkSemaphore image_acquired_semaphore =
// // // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].ImageAcquiredSemaphore;
// // // //     VkSemaphore render_complete_semaphore =
// // // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;

// // // //     VkResult err = vkAcquireNextImageKHR(
// // // //         device_, mainWindowData_.Swapchain, UINT64_MAX,
// // // //         image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex
// // // //     );
// // // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// // // //         g_SwapChainRebuild = true;
// // // //     }
// // // //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// // // //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// // // //     ImGui_ImplVulkanH_Frame* fd = &mainWindowData_.Frames[mainWindowData_.FrameIndex];
// // // //     {
// // // //         err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
// // // //         check_vk_result(err);
// // // //         err = vkResetFences(device_, 1, &fd->Fence);
// // // //         check_vk_result(err);
// // // //     }

// // // //     {
// // // //         err = vkResetCommandPool(device_, fd->CommandPool, 0);
// // // //         check_vk_result(err);
// // // //         VkCommandBufferBeginInfo info = {};
// // // //         info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
// // // //         info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
// // // //         err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
// // // //         check_vk_result(err);
// // // //     }

// // // //     {
// // // //         VkRenderPassBeginInfo info = {};
// // // //         info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
// // // //         info.renderPass = mainWindowData_.RenderPass;
// // // //         info.framebuffer = fd->Framebuffer;
// // // //         info.renderArea.extent.width = mainWindowData_.Width;
// // // //         info.renderArea.extent.height = mainWindowData_.Height;
// // // //         info.clearValueCount = 1;
// // // //         info.pClearValues = &mainWindowData_.ClearValue;
// // // //         vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
// // // //     }

// // // //     ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

// // // //     vkCmdEndRenderPass(fd->CommandBuffer);
// // // //     {
// // // //         VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
// // // //         VkSubmitInfo info = {};
// // // //         info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
// // // //         info.waitSemaphoreCount = 1;
// // // //         info.pWaitSemaphores = &image_acquired_semaphore;
// // // //         info.pWaitDstStageMask = &wait_stage;
// // // //         info.commandBufferCount = 1;
// // // //         info.pCommandBuffers = &fd->CommandBuffer;
// // // //         info.signalSemaphoreCount = 1;
// // // //         info.pSignalSemaphores = &render_complete_semaphore;

// // // //         err = vkEndCommandBuffer(fd->CommandBuffer);
// // // //         check_vk_result(err);
// // // //         err = vkQueueSubmit(queue_, 1, &info, fd->Fence);
// // // //         check_vk_result(err);
// // // //     }
// // // // }

// // // // void Application::Present() {
// // // //     if (g_SwapChainRebuild) return;

// // // //     VkSemaphore render_complete_semaphore =
// // // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;
// // // //     VkPresentInfoKHR info = {};
// // // //     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
// // // //     info.waitSemaphoreCount = 1;
// // // //     info.pWaitSemaphores = &render_complete_semaphore;
// // // //     info.swapchainCount = 1;
// // // //     info.pSwapchains = &mainWindowData_.Swapchain;
// // // //     info.pImageIndices = &mainWindowData_.FrameIndex;

// // // //     VkResult err = vkQueuePresentKHR(queue_, &info);
// // // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// // // //         g_SwapChainRebuild = true;
// // // //     }
// // // //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// // // //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// // // //     mainWindowData_.SemaphoreIndex =
// // // //         (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;
// // // // }

// // // // void Application::RenderDockSpace() {
// // // //     ImGuiViewport* viewport = ImGui::GetMainViewport();
// // // //     ImGui::SetNextWindowPos(viewport->WorkPos);
// // // //     ImGui::SetNextWindowSize(viewport->WorkSize);
// // // //     ImGui::SetNextWindowViewport(viewport->ID);

// // // //     ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
// // // //     window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
// // // //     window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
// // // //     window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
// // // //     window_flags |= ImGuiWindowFlags_MenuBar;

// // // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
// // // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
// // // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
// // // //     ImGui::Begin("DockSpace", nullptr, window_flags);
// // // //     ImGui::PopStyleVar(3);

// // // //     ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
// // // //     ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderMainMenuBar() {
// // // //     if (ImGui::BeginMainMenuBar()) {
// // // //         auto& sm = Shortcuts::ShortcutManager::Instance();

// // // //         if (ImGui::BeginMenu("File")) {
// // // //             std::string newShortcut = sm.GetShortcutString("action.new");
// // // //             if (ImGui::MenuItem("New", newShortcut.c_str())) TestAction_NewFile();

// // // //             std::string openShortcut = sm.GetShortcutString("action.open");
// // // //             if (ImGui::MenuItem("Open", openShortcut.c_str())) TestAction_OpenFile();

// // // //             std::string saveShortcut = sm.GetShortcutString("action.save");
// // // //             if (ImGui::MenuItem("Save", saveShortcut.c_str())) TestAction_SaveFile();

// // // //             ImGui::Separator();

// // // //             std::string quitShortcut = sm.GetShortcutString("action.quit");
// // // //             if (ImGui::MenuItem("Quit", quitShortcut.c_str())) TestAction_Quit();

// // // //             ImGui::EndMenu();
// // // //         }

// // // //         if (ImGui::BeginMenu("Edit")) {
// // // //             if (ImGui::MenuItem("Token Editor", nullptr, showTokenEditor_)) {
// // // //                 showTokenEditor_ = !showTokenEditor_;
// // // //             }
// // // //             if (ImGui::MenuItem("Shortcut Editor", nullptr, showShortcutEditor_)) {
// // // //                 showShortcutEditor_ = !showShortcutEditor_;
// // // //             }
// // // //             ImGui::EndMenu();
// // // //         }

// // // //         if (ImGui::BeginMenu("Windows")) {
// // // //             ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
// // // //             ImGui::EndMenu();
// // // //         }

// // // //         ImGui::EndMainMenuBar();
// // // //     }
// // // // }

// // // // void Application::RenderToolbar() {
// // // //     ImGui::Begin("Toolbar", nullptr,
// // // //         ImGuiWindowFlags_NoCollapse |
// // // //         ImGuiWindowFlags_NoTitleBar |
// // // //         ImGuiWindowFlags_NoResize);

// // // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Toolbar", Shortcuts::ShortcutZone::Toolbar);
// // // //     // ShortcutManager::Instance().RegisterWindowZone("Toolbar", ShortcutZone::Toolbar);

// // // //     ImGui::Text("Tools");
// // // //     ImGui::Separator();

// // // //     const float iconSize = 20.0f;
// // // //     const float buttonHeight = 36.0f;

// // // //     // Tool 1
// // // //     if (ImGui::BeginChild("##tool1", ImVec2(0, buttonHeight), false)) {
// // // //         UI::IconManager::Instance().RenderIcon("tool1", iconSize);
// // // //         ImGui::SameLine();
// // // //         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
// // // //         if (ImGui::Selectable("Tool 1", false))
// // // //             TestAction_Tool1();
// // // //     }
// // // //     ImGui::EndChild();

// // // //     // Tool 2
// // // //     if (ImGui::BeginChild("##tool2", ImVec2(0, buttonHeight), false)) {
// // // //         UI::IconManager::Instance().RenderIcon("tool2", iconSize);
// // // //         ImGui::SameLine();
// // // //         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
// // // //         if (ImGui::Selectable("Tool 2", false))
// // // //             TestAction_Tool2();
// // // //     }
// // // //     ImGui::EndChild();

// // // //     ImGui::Separator();

// // // //     // Bottom icons
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f);

// // // //     ImVec4 white = ImVec4(1, 1, 1, 1);
// // // //     UI::IconManager::Instance().RenderIcon("settings", 24.0f, &white);

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderIconTestWindow() {
// // // //     ImGui::Begin("Icon Test Lab");

// // // //     // 1. Tailles multiples
// // // //     ImGui::Text("Logo Sizes:");
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 16.0f); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);

// // // //     ImGui::Separator();

// // // //     // 2. Couleurs fixes
// // // //     ImGui::Text("Settings Tinted:");
// // // //     ImVec4 cyan(0, 1, 1, 1);
// // // //     ImVec4 red(1, 0, 0, 1);
// // // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &cyan); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &red);

// // // //     ImGui::Separator();

// // // //     // 3. Color Picker dynamique pour open.svg
// // // //     ImGui::Text("Dynamic Color (Open Icon):");
// // // //     static ImVec4 dynamicColor = ImVec4(1, 1, 0, 1);

// // // //     // Si la couleur change, on pourrait accumuler des textures dans le cache.
// // // //     // Pour la performance, on utilise le cache par défaut (la clé contient la couleur).
// // // //     // Note: Dans un vrai projet, prévoyez un ClearCache périodique si le picker est bcp utilisé.
// // // //     if (ImGui::ColorEdit4("Icon Color", (float*)&dynamicColor)) {
// // // //         // Optionnel : UI::IconManager::Instance().ClearCache();
// // // //         // Mais comme CreateCacheKey inclut la couleur, ça crée juste une nouvelle texture.
// // // //     }

// // // //     UI::IconManager::Instance().RenderIcon("open", 48.0f, &dynamicColor);

// // // //     ImGui::Separator();
// // // //     ImGui::Text("Logo Carto – Multi Shape Control");

// // // //     // Couleurs initiales = SVG
// // // //     static ImVec4 holeColor   = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // transparent
// // // //     static ImVec4 ringColor   = ImVec4(0.69f, 0.25f, 0.57f, 1.0f); // #b04191
// // // //     static ImVec4 cursorColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // noir

// // // //     ImGui::ColorEdit4("Hole (transparent)", (float*)&holeColor);
// // // //     ImGui::ColorEdit4("Ring",              (float*)&ringColor);
// // // //     ImGui::ColorEdit4("Cursor",            (float*)&cursorColor);

// // // //     // Mapping EXACT des shapes NanoSVG
// // // //     // Shape 0 : trou (fill-opacity 0)
// // // //     // Shape 1 : anneau
// // // //     // Shape 2 : curseur central
// // // //     std::map<int, ImVec4> logoColors;
// // // //     logoColors[0] = holeColor;
// // // //     logoColors[1] = ringColor;
// // // //     logoColors[2] = cursorColor;

// // // //     UI::IconManager::Instance().RenderIcon(
// // // //         "logo_carto",
// // // //         64.0f,
// // // //         nullptr,
// // // //         &logoColors
// // // //     );

// // // //     ImGui::Separator();
// // // //     ImGui::Text("Manga Icon (original colors)");

// // // //     UI::IconManager::Instance().RenderIcon(
// // // //         "manga_icon",
// // // //         64.0f
// // // //     );

// // // //     ImGui::Separator();
// // // //     ImGui::Text("Three Balls – Independent Colors");

// // // //     static ImVec4 ball0 = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // rouge
// // // //     static ImVec4 ball1 = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // bleu
// // // //     static ImVec4 ball2 = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // jaune

// // // //     ImGui::ColorEdit4("Ball 1", (float*)&ball0);
// // // //     ImGui::ColorEdit4("Ball 2", (float*)&ball1);
// // // //     ImGui::ColorEdit4("Ball 3", (float*)&ball2);

// // // //     std::map<int, ImVec4> ballsColors;
// // // //     ballsColors[0] = ball0;
// // // //     ballsColors[1] = ball1;
// // // //     ballsColors[2] = ball2;

// // // //     UI::IconManager::Instance().RenderIcon(
// // // //         "three_balls",
// // // //         64.0f,
// // // //         nullptr,
// // // //         &ballsColors
// // // //     );

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderDesignExample() {
// // // //     ImGui::Begin("Design System Example", nullptr, ImGuiWindowFlags_NoCollapse);
// // // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Design System Example",
// // // //         Shortcuts::ShortcutZone::DesignExample);

// // // //     ImGui::Text("This window uses the Design System!");

// // // //     auto& ds = DesignSystem::DesignSystem::Instance();
// // // //     ds.PushAllStyles();

// // // //     static int int_value = 50;
// // // //     ImGui::Text("Drag to adjust:");
// // // //     ImGui::SetNextItemWidth(150.0f);
// // // //     ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");

// // // //     ImGui::SameLine();
// // // //     if (ImGui::Button("Print Value", ImVec2(120, 0))) {
// // // //         printf("Value: %d\n", int_value);
// // // //     }

// // // //     ds.PopAllStyles();

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderThemePreview() {
// // // //     ImGui::Begin("Theme Preview", nullptr, ImGuiWindowFlags_NoCollapse);
// // // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Theme Preview",
// // // //         Shortcuts::ShortcutZone::ThemePreview);

// // // //     ImGui::Text("Preview with current context");
// // // //     ImGui::Separator();

// // // //     auto& ds = DesignSystem::DesignSystem::Instance();

// // // //     try {
// // // //         ImVec4 bgColor = ds.GetColor("semantic.color.background");
// // // //         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

// // // //         ImGui::ColorButton("##bg", bgColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
// // // //         ImGui::SameLine();
// // // //         ImGui::Text("Background Color");

// // // //         ImGui::ColorButton("##primary", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
// // // //         ImGui::SameLine();
// // // //         ImGui::Text("Primary Color");

// // // //         ImGui::Separator();
// // // //         ImGui::Text("Sample Components:");

// // // //         if (ImGui::Button("Sample Button")) {}

// // // //         static char textBuffer[128] = "Sample Input";
// // // //         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

// // // //     } catch (const std::exception& e) {
// // // //         ImGui::Text("Error resolving tokens: %s", e.what());
// // // //     }

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderTestZone1() {
// // // //     ImGui::Begin("Test Zone 1", nullptr, ImGuiWindowFlags_NoCollapse);
// // // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 1",
// // // //         Shortcuts::ShortcutZone::TestZone1);

// // // //     ImGui::Text("Test shortcuts in this zone");
// // // //     ImGui::Text("Press Ctrl+A to trigger Zone 1 action");

// // // //     if (ImGui::Button("Trigger Zone 1 Action")) {
// // // //         TestAction_Zone1();
// // // //     }

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderTestZone2() {
// // // //     ImGui::Begin("Test Zone 2", nullptr, ImGuiWindowFlags_NoCollapse);
// // // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 2",
// // // //         Shortcuts::ShortcutZone::TestZone2);

// // // //     ImGui::Text("Test shortcuts in this zone");
// // // //     ImGui::Text("Press Ctrl+A to trigger Zone 2 action");
// // // //     ImGui::Text("(same shortcut as Zone 1, but different action)");

// // // //     if (ImGui::Button("Trigger Zone 2 Action")) {
// // // //         TestAction_Zone2();
// // // //     }

// // // //     ImGui::End();
// // // // }

// // // // void Application::RenderFloatingWindows() {
// // // //     if (showTokenEditor_) {
// // // //         auto& ds = DesignSystem::DesignSystem::Instance();
// // // //         tokenEditor_.Render(ds.GetCurrentContext(), ds.GetOverrideManager());
// // // //     }

// // // //     if (showShortcutEditor_) {
// // // //         shortcutEditor_.Render(&showShortcutEditor_);
// // // //     }

// // // //     if (showImGuiDemo_) {
// // // //         ImGui::ShowDemoWindow(&showImGuiDemo_);
// // // //     }
// // // // }

// // // // void Application::Shutdown() {
// // // //     VkResult err = vkDeviceWaitIdle(device_);
// // // //     check_vk_result(err);

// // // //     DesignSystem::DesignSystem::Instance().Shutdown();
// // // //     Shortcuts::ShortcutManager::Instance().Shutdown();
// // // //     UI::IconManager::Instance().Shutdown();

// // // //     vkDestroyCommandPool(device_, commandPool_, g_Allocator);

// // // //     ImGui_ImplVulkan_Shutdown();
// // // //     ImGui_ImplSDL3_Shutdown();
// // // //     ImGui::DestroyContext();

// // // //     ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &mainWindowData_, g_Allocator);
// // // //     vkDestroySurfaceKHR(instance_, mainWindowData_.Surface, g_Allocator);

// // // //     vkDestroyDescriptorPool(device_, descriptorPool_, g_Allocator);

// // // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // //     auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
// // // //         vkGetInstanceProcAddr(instance_, "vkDestroyDebugReportCallbackEXT");
// // // //     f_vkDestroyDebugReportCallbackEXT(instance_, g_DebugReport, g_Allocator);
// // // // #endif

// // // //     vkDestroyDevice(device_, g_Allocator);
// // // //     vkDestroyInstance(instance_, g_Allocator);

// // // //     SDL_DestroyWindow(window_);
// // // //     SDL_Quit();
// // // // }

// // // // // ========== Test Actions ==========

// // // // void Application::TestAction_NewFile() {
// // // //     std::cout << "[ACTION] New File" << std::endl;
// // // // }

// // // // void Application::TestAction_OpenFile() {
// // // //     std::cout << "[ACTION] Open File" << std::endl;
// // // // }

// // // // void Application::TestAction_SaveFile() {
// // // //     std::cout << "[ACTION] Save File" << std::endl;
// // // // }

// // // // void Application::TestAction_Quit() {
// // // //     std::cout << "[ACTION] Quit" << std::endl;
// // // // }

// // // // void Application::TestAction_Tool1() {
// // // //     std::cout << "[ACTION] Tool 1 (Toolbar)" << std::endl;
// // // // }

// // // // void Application::TestAction_Tool2() {
// // // //     std::cout << "[ACTION] Tool 2 (Toolbar)" << std::endl;
// // // // }

// // // // void Application::TestAction_Zone1() {
// // // //     std::cout << "[ACTION] Test Zone 1 Action" << std::endl;
// // // // }

// // // // void Application::TestAction_Zone2() {
// // // //     std::cout << "[ACTION] Test Zone 2 Action" << std::endl;
// // // // }

// // // // } // namespace App

// // // #include "Application.h"
// // // #include <imgui_impl_sdl3.h>
// // // #include <imgui_impl_vulkan.h>
// // // #include <iostream>
// // // #include <algorithm>
// // // #include <SDL3/SDL_vulkan.h>

// // // #ifdef _DEBUG
// // // #define APP_USE_VULKAN_DEBUG_REPORT
// // // static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
// // // #endif

// // // static VkAllocationCallbacks* g_Allocator = nullptr;
// // // static uint32_t g_MinImageCount = 2;
// // // static bool g_SwapChainRebuild = false;

// // // namespace App {

// // // // ========== Vulkan Helpers ==========

// // // static void check_vk_result(VkResult err) {
// // //     if (err == VK_SUCCESS) return;
// // //     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
// // //     if (err < 0) abort();
// // // }

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
// // //     VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
// // //     uint64_t object, size_t location, int32_t messageCode,
// // //     const char* pLayerPrefix, const char* pMessage, void* pUserData)
// // // {
// // //     (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
// // //     fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
// // //     return VK_FALSE;
// // // }
// // // #endif

// // // static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
// // //     for (const VkExtensionProperties& p : properties)
// // //         if (strcmp(p.extensionName, extension) == 0)
// // //             return true;
// // //     return false;
// // // }

// // // // ========== Application Implementation ==========

// // // Application::Application() {}

// // // Application::~Application() {}

// // // bool Application::Initialize() {
// // //     // Initialize SDL
// // //     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
// // //         printf("Error: SDL_Init(): %s\n", SDL_GetError());
// // //         return false;
// // //     }

// // //     mainScale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

// // //     // Create window
// // //     SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
// // //                                    SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
// // //     window_ = SDL_CreateWindow(
// // //         "Carto - Design System Demo",
// // //         static_cast<int>(1280 * mainScale_),
// // //         static_cast<int>(800 * mainScale_),
// // //         window_flags
// // //     );

// // //     if (!window_) {
// // //         printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
// // //         return false;
// // //     }

// // //     // Setup Vulkan
// // //     SetupVulkan();
// // //     SetupVulkanWindow();

// // //     SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
// // //     SDL_ShowWindow(window_);

// // //     // Setup Dear ImGui
// // //     IMGUI_CHECKVERSION();
// // //     ImGui::CreateContext();
// // //     ImGuiIO& io = ImGui::GetIO();
// // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
// // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
// // //     io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

// // //     ImGui::StyleColorsDark();

// // //     ImGuiStyle& style = ImGui::GetStyle();
// // //     style.ScaleAllSizes(mainScale_);
// // //     style.FontScaleDpi = mainScale_;

// // //     // Initialize subsystems
// // //     InitializeSubsystems();
// // //     LoadResources();
// // //     RegisterTestShortcuts();

// // //     // Setup Platform/Renderer backends
// // //     ImGui_ImplSDL3_InitForVulkan(window_);
// // //     ImGui_ImplVulkan_InitInfo init_info = {};
// // //     init_info.Instance = instance_;
// // //     init_info.PhysicalDevice = physicalDevice_;
// // //     init_info.Device = device_;
// // //     init_info.QueueFamily = queueFamily_;
// // //     init_info.Queue = queue_;
// // //     init_info.DescriptorPool = descriptorPool_;
// // //     init_info.MinImageCount = g_MinImageCount;
// // //     init_info.ImageCount = mainWindowData_.ImageCount;
// // //     init_info.Allocator = g_Allocator;
// // //     init_info.PipelineInfoMain.RenderPass = mainWindowData_.RenderPass;
// // //     init_info.PipelineInfoMain.Subpass = 0;
// // //     init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
// // //     init_info.CheckVkResultFn = check_vk_result;
// // //     ImGui_ImplVulkan_Init(&init_info);

// // //     return true;
// // // }

// // // void Application::SetupVulkan() {
// // //     ImVector<const char*> extensions;
// // //     uint32_t sdl_extensions_count = 0;
// // //     const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
// // //     for (uint32_t n = 0; n < sdl_extensions_count; n++)
// // //         extensions.push_back(sdl_extensions[n]);

// // //     VkResult err;
// // //     VkInstanceCreateInfo create_info = {};
// // //     create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

// // //     uint32_t properties_count;
// // //     ImVector<VkExtensionProperties> properties;
// // //     vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
// // //     properties.resize(properties_count);
// // //     err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
// // //     check_vk_result(err);

// // //     if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
// // //         extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

// // // #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
// // //     if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
// // //         extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
// // //         create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
// // //     }
// // // #endif

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //     const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
// // //     create_info.enabledLayerCount = 1;
// // //     create_info.ppEnabledLayerNames = layers;
// // //     extensions.push_back("VK_EXT_debug_report");
// // // #endif

// // //     create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.Size);
// // //     create_info.ppEnabledExtensionNames = extensions.Data;
// // //     err = vkCreateInstance(&create_info, g_Allocator, &instance_);
// // //     check_vk_result(err);

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //     auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
// // //         vkGetInstanceProcAddr(instance_, "vkCreateDebugReportCallbackEXT");
// // //     IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
// // //     VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
// // //     debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
// // //     debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
// // //                            VK_DEBUG_REPORT_WARNING_BIT_EXT |
// // //                            VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
// // //     debug_report_ci.pfnCallback = debug_report;
// // //     debug_report_ci.pUserData = nullptr;
// // //     err = f_vkCreateDebugReportCallbackEXT(instance_, &debug_report_ci, g_Allocator, &g_DebugReport);
// // //     check_vk_result(err);
// // // #endif

// // //     physicalDevice_ = ImGui_ImplVulkanH_SelectPhysicalDevice(instance_);
// // //     queueFamily_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice_);

// // //     // Create logical device
// // //     {
// // //         ImVector<const char*> device_extensions;
// // //         device_extensions.push_back("VK_KHR_swapchain");

// // //         uint32_t properties_count;
// // //         ImVector<VkExtensionProperties> properties;
// // //         vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, nullptr);
// // //         properties.resize(properties_count);
// // //         vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &properties_count, properties.Data);

// // // #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
// // //         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
// // //             device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
// // // #endif

// // //         const float queue_priority[] = { 1.0f };
// // //         VkDeviceQueueCreateInfo queue_info[1] = {};
// // //         queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
// // //         queue_info[0].queueFamilyIndex = queueFamily_;
// // //         queue_info[0].queueCount = 1;
// // //         queue_info[0].pQueuePriorities = queue_priority;

// // //         VkDeviceCreateInfo create_info = {};
// // //         create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
// // //         create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
// // //         create_info.pQueueCreateInfos = queue_info;
// // //         create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.Size);
// // //         create_info.ppEnabledExtensionNames = device_extensions.Data;

// // //         err = vkCreateDevice(physicalDevice_, &create_info, g_Allocator, &device_);
// // //         check_vk_result(err);
// // //         vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
// // //     }

// // //     // Create descriptor pool
// // //     {
// // //         VkDescriptorPoolSize pool_sizes[] = {
// // //             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
// // //         };
// // //         VkDescriptorPoolCreateInfo pool_info = {};
// // //         pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
// // //         pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
// // //         pool_info.maxSets = 1000;
// // //         pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
// // //         pool_info.pPoolSizes = pool_sizes;
// // //         err = vkCreateDescriptorPool(device_, &pool_info, g_Allocator, &descriptorPool_);
// // //         check_vk_result(err);
// // //     }

// // //     // Create command pool
// // //     {
// // //         VkCommandPoolCreateInfo pool_info{};
// // //         pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
// // //         pool_info.queueFamilyIndex = queueFamily_;
// // //         pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
// // //         err = vkCreateCommandPool(device_, &pool_info, g_Allocator, &commandPool_);
// // //         check_vk_result(err);
// // //     }
// // // }

// // // void Application::SetupVulkanWindow() {
// // //     VkSurfaceKHR surface;
// // //     if (SDL_Vulkan_CreateSurface(window_, instance_, g_Allocator, &surface) == 0) {
// // //         printf("Failed to create Vulkan surface.\n");
// // //         abort();
// // //     }

// // //     int w, h;
// // //     SDL_GetWindowSize(window_, &w, &h);

// // //     ImGui_ImplVulkanH_Window* wd = &mainWindowData_;

// // //     VkBool32 res;
// // //     vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamily_, surface, &res);
// // //     if (res != VK_TRUE) {
// // //         fprintf(stderr, "Error no WSI support on physical device 0\n");
// // //         abort();
// // //     }

// // //     const VkFormat requestSurfaceImageFormat[] = {
// // //         VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
// // //         VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM
// // //     };
// // //     const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
// // //     wd->Surface = surface;
// // //     wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
// // //         physicalDevice_, wd->Surface,
// // //         requestSurfaceImageFormat,
// // //         static_cast<size_t>(IM_ARRAYSIZE(requestSurfaceImageFormat)),
// // //         requestSurfaceColorSpace
// // //     );

// // //     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
// // //     wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
// // //         physicalDevice_, wd->Surface, &present_modes[0], IM_ARRAYSIZE(present_modes)
// // //     );

// // //     ImGui_ImplVulkanH_CreateOrResizeWindow(
// // //         instance_, physicalDevice_, device_, wd, queueFamily_,
// // //         g_Allocator, w, h, g_MinImageCount, 0
// // //     );
// // // }

// // // void Application::InitializeSubsystems() {
// // //     // Design System
// // //     auto& designSystem = DesignSystem::DesignSystem::Instance();
// // //     designSystem.Initialize();

// // //     // Shortcut Manager
// // //     Shortcuts::ShortcutManager::Instance().Initialize();

// // //     // Font Manager
// // //     ImGuiIO& io = ImGui::GetIO();
// // //     UI::FontManager::Instance().Initialize();

// // //     // Charger Qanelas
// // //     if (UI::FontManager::Instance().LoadFont("qanelas", "resources/fonts/Qanelas-Regular.otf", 14.0f)) {
// // //         UI::FontManager::Instance().SetDefaultFont("qanelas");
// // //     }

// // //     // Icon Manager
// // //     UI::IconManager::Instance().Initialize(
// // //         device_, physicalDevice_, queue_, commandPool_, descriptorPool_
// // //     );
// // // }

// // // void Application::LoadResources() {
// // //     // Charger toutes les icônes depuis le dossier resources/icons
// // //     UI::IconManager::Instance().LoadIconsFromDirectory("resources/icons");

// // //     // ===== CONFIGURATION DES MÉTADONNÉES D'ICÔNES =====
// // //     auto& iconMgr = UI::IconManager::Instance();

// // //     // Logo Carto (multicolore - 3 shapes)
// // //     {
// // //         UI::IconMetadata meta;
// // //         meta.scheme = UI::IconColorScheme::Multicolor;
// // //         meta.multicolorDefaults[0] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // transparent hole
// // //         meta.multicolorDefaults[1] = ImVec4(0.69f, 0.25f, 0.57f, 1.0f); // #b04191 ring
// // //         meta.multicolorDefaults[2] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // black cursor
// // //         iconMgr.SetIconMetadata("logo_carto", meta);
// // //     }

// // //     // Three Balls (multicolore - 3 shapes)
// // //     {
// // //         UI::IconMetadata meta;
// // //         meta.scheme = UI::IconColorScheme::Multicolor;
// // //         meta.multicolorDefaults[0] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // rouge
// // //         meta.multicolorDefaults[1] = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // bleu
// // //         meta.multicolorDefaults[2] = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // jaune
// // //         iconMgr.SetIconMetadata("three_balls", meta);
// // //     }

// // //     // Manga Icon (multicolore)
// // //     {
// // //         UI::IconMetadata meta;
// // //         meta.scheme = UI::IconColorScheme::Multicolor;
// // //         // Pas de defaults = utilise couleurs originales du SVG
// // //         iconMgr.SetIconMetadata("manga_icon", meta);
// // //     }

// // //     // Settings (bicolore - défaut auto : shape 0 = primaire, reste = secondaire)
// // //     // Pas besoin de config, le défaut convient

// // //     // Tool1, Tool2, New, Open, Save (bicolores)
// // //     // Idem, défauts auto
// // // }

// // // void Application::RegisterTestShortcuts() {
// // //     auto& sm = Shortcuts::ShortcutManager::Instance();

// // //     // Actions globales
// // //     sm.RegisterAction(Shortcuts::Action("action.new", "New File",
// // //         Shortcuts::ShortcutZone::Global, TestAction_NewFile));
// // //     sm.SetBinding("action.new", {{Shortcuts::KeyCombination(ImGuiKey_N, true, false, false)}});

// // //     sm.RegisterAction(Shortcuts::Action("action.open", "Open File",
// // //         Shortcuts::ShortcutZone::Global, TestAction_OpenFile));
// // //     sm.SetBinding("action.open", {{Shortcuts::KeyCombination(ImGuiKey_O, true, false, false)}});

// // //     sm.RegisterAction(Shortcuts::Action("action.save", "Save File",
// // //         Shortcuts::ShortcutZone::Global, TestAction_SaveFile));
// // //     sm.SetBinding("action.save", {{Shortcuts::KeyCombination(ImGuiKey_S, true, false, false)}});

// // //     sm.RegisterAction(Shortcuts::Action("action.quit", "Quit",
// // //         Shortcuts::ShortcutZone::Global, TestAction_Quit));
// // //     sm.SetBinding("action.quit", {{Shortcuts::KeyCombination(ImGuiKey_Q, true, false, false)}});

// // //     // Actions toolbar
// // //     sm.RegisterAction(Shortcuts::Action("tool.1", "Tool 1",
// // //         Shortcuts::ShortcutZone::Toolbar, TestAction_Tool1));
// // //     sm.SetBinding("tool.1", {{Shortcuts::KeyCombination(ImGuiKey_1, true, false, false)}});

// // //     sm.RegisterAction(Shortcuts::Action("tool.2", "Tool 2",
// // //         Shortcuts::ShortcutZone::Toolbar, TestAction_Tool2));
// // //     sm.SetBinding("tool.2", {{Shortcuts::KeyCombination(ImGuiKey_2, true, false, false)}});

// // //     // Actions zones de test avec priorité
// // //     sm.RegisterAction(Shortcuts::Action("zone1.action", "Zone 1 Action",
// // //         Shortcuts::ShortcutZone::TestZone1, TestAction_Zone1, true));
// // //     sm.SetBinding("zone1.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});

// // //     sm.RegisterAction(Shortcuts::Action("zone2.action", "Zone 2 Action",
// // //         Shortcuts::ShortcutZone::TestZone2, TestAction_Zone2, true));
// // //     sm.SetBinding("zone2.action", {{Shortcuts::KeyCombination(ImGuiKey_A, true, false, false)}});
// // // }

// // // void Application::Run() {
// // //     while (running_) {
// // //         ProcessEvents();
// // //         Update();
// // //         Render();
// // //         Present();
// // //     }
// // // }

// // // void Application::ProcessEvents() {
// // //     SDL_Event event;
// // //     while (SDL_PollEvent(&event)) {
// // //         ImGui_ImplSDL3_ProcessEvent(&event);
// // //         if (event.type == SDL_EVENT_QUIT)
// // //             running_ = false;
// // //         if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
// // //             event.window.windowID == SDL_GetWindowID(window_))
// // //             running_ = false;
// // //     }

// // //     if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
// // //         SDL_Delay(10);
// // //         return;
// // //     }

// // //     // Handle window resize
// // //     int fb_width, fb_height;
// // //     SDL_GetWindowSize(window_, &fb_width, &fb_height);
// // //     if (fb_width > 0 && fb_height > 0 &&
// // //         (g_SwapChainRebuild || mainWindowData_.Width != fb_width || mainWindowData_.Height != fb_height)) {
// // //         ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
// // //         ImGui_ImplVulkanH_CreateOrResizeWindow(
// // //             instance_, physicalDevice_, device_, &mainWindowData_,
// // //             queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0
// // //         );
// // //         mainWindowData_.FrameIndex = 0;
// // //         g_SwapChainRebuild = false;
// // //     }
// // // }

// // // void Application::Update() {
// // //     ImGui_ImplVulkan_NewFrame();
// // //     ImGui_ImplSDL3_NewFrame();
// // //     ImGui::NewFrame();

// // //     // Process shortcuts
// // //     Shortcuts::ShortcutManager::Instance().ProcessInput();

// // //     // Render UI
// // //     RenderDockSpace();
// // //     RenderMainMenuBar();
// // //     RenderToolbar();
// // //     RenderIconTestWindow();
// // //     RenderDesignExample();
// // //     RenderThemePreview();
// // //     RenderTestZone1();
// // //     RenderTestZone2();
// // //     RenderIconEditorWindow();
// // //     RenderFloatingWindows();
// // // }

// // // void Application::Render() {
// // //     ImGui::Render();
// // //     ImDrawData* draw_data = ImGui::GetDrawData();
// // //     const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
// // //     if (is_minimized) return;

// // //     auto& ds = DesignSystem::DesignSystem::Instance();
// // //     ImVec4 clear_color = ds.GetColor("semantic.color.background");
// // //     mainWindowData_.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
// // //     mainWindowData_.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
// // //     mainWindowData_.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
// // //     mainWindowData_.ClearValue.color.float32[3] = clear_color.w;

// // //     VkSemaphore image_acquired_semaphore =
// // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].ImageAcquiredSemaphore;
// // //     VkSemaphore render_complete_semaphore =
// // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;

// // //     VkResult err = vkAcquireNextImageKHR(
// // //         device_, mainWindowData_.Swapchain, UINT64_MAX,
// // //         image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex
// // //     );
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// // //         g_SwapChainRebuild = true;
// // //     }
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// // //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// // //     ImGui_ImplVulkanH_Frame* fd = &mainWindowData_.Frames[mainWindowData_.FrameIndex];
// // //     {
// // //         err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
// // //         check_vk_result(err);
// // //         err = vkResetFences(device_, 1, &fd->Fence);
// // //         check_vk_result(err);
// // //     }

// // //     {
// // //         err = vkResetCommandPool(device_, fd->CommandPool, 0);
// // //         check_vk_result(err);
// // //         VkCommandBufferBeginInfo info = {};
// // //         info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
// // //         info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
// // //         err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
// // //         check_vk_result(err);
// // //     }

// // //     {
// // //         VkRenderPassBeginInfo info = {};
// // //         info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
// // //         info.renderPass = mainWindowData_.RenderPass;
// // //         info.framebuffer = fd->Framebuffer;
// // //         info.renderArea.extent.width = mainWindowData_.Width;
// // //         info.renderArea.extent.height = mainWindowData_.Height;
// // //         info.clearValueCount = 1;
// // //         info.pClearValues = &mainWindowData_.ClearValue;
// // //         vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
// // //     }

// // //     ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

// // //     vkCmdEndRenderPass(fd->CommandBuffer);
// // //     {
// // //         VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
// // //         VkSubmitInfo info = {};
// // //         info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
// // //         info.waitSemaphoreCount = 1;
// // //         info.pWaitSemaphores = &image_acquired_semaphore;
// // //         info.pWaitDstStageMask = &wait_stage;
// // //         info.commandBufferCount = 1;
// // //         info.pCommandBuffers = &fd->CommandBuffer;
// // //         info.signalSemaphoreCount = 1;
// // //         info.pSignalSemaphores = &render_complete_semaphore;

// // //         err = vkEndCommandBuffer(fd->CommandBuffer);
// // //         check_vk_result(err);
// // //         err = vkQueueSubmit(queue_, 1, &info, fd->Fence);
// // //         check_vk_result(err);
// // //     }
// // // }

// // // void Application::Present() {
// // //     if (g_SwapChainRebuild) return;

// // //     VkSemaphore render_complete_semaphore =
// // //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;
// // //     VkPresentInfoKHR info = {};
// // //     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
// // //     info.waitSemaphoreCount = 1;
// // //     info.pWaitSemaphores = &render_complete_semaphore;
// // //     info.swapchainCount = 1;
// // //     info.pSwapchains = &mainWindowData_.Swapchain;
// // //     info.pImageIndices = &mainWindowData_.FrameIndex;

// // //     VkResult err = vkQueuePresentKHR(queue_, &info);
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// // //         g_SwapChainRebuild = true;
// // //     }
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// // //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// // //     mainWindowData_.SemaphoreIndex =
// // //         (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;
// // // }

// // // void Application::RenderDockSpace() {
// // //     ImGuiViewport* viewport = ImGui::GetMainViewport();
// // //     ImGui::SetNextWindowPos(viewport->WorkPos);
// // //     ImGui::SetNextWindowSize(viewport->WorkSize);
// // //     ImGui::SetNextWindowViewport(viewport->ID);

// // //     ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
// // //     window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
// // //     window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
// // //     window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
// // //     window_flags |= ImGuiWindowFlags_MenuBar;

// // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
// // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
// // //     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
// // //     ImGui::Begin("DockSpace", nullptr, window_flags);
// // //     ImGui::PopStyleVar(3);

// // //     ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
// // //     ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

// // //     ImGui::End();
// // // }

// // // void Application::RenderMainMenuBar() {
// // //     if (ImGui::BeginMainMenuBar()) {
// // //         auto& sm = Shortcuts::ShortcutManager::Instance();

// // //         if (ImGui::BeginMenu("File")) {
// // //             std::string newShortcut = sm.GetShortcutString("action.new");
// // //             if (ImGui::MenuItem("New", newShortcut.c_str())) TestAction_NewFile();

// // //             std::string openShortcut = sm.GetShortcutString("action.open");
// // //             if (ImGui::MenuItem("Open", openShortcut.c_str())) TestAction_OpenFile();

// // //             std::string saveShortcut = sm.GetShortcutString("action.save");
// // //             if (ImGui::MenuItem("Save", saveShortcut.c_str())) TestAction_SaveFile();

// // //             ImGui::Separator();

// // //             std::string quitShortcut = sm.GetShortcutString("action.quit");
// // //             if (ImGui::MenuItem("Quit", quitShortcut.c_str())) TestAction_Quit();

// // //             ImGui::EndMenu();
// // //         }

// // //         if (ImGui::BeginMenu("Edit")) {
// // //             if (ImGui::MenuItem("Token Editor", nullptr, showTokenEditor_)) {
// // //                 showTokenEditor_ = !showTokenEditor_;
// // //             }
// // //             if (ImGui::MenuItem("Shortcut Editor", nullptr, showShortcutEditor_)) {
// // //                 showShortcutEditor_ = !showShortcutEditor_;
// // //             }
// // //             ImGui::EndMenu();
// // //         }

// // //         if (ImGui::BeginMenu("Windows")) {
// // //             ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
// // //             ImGui::EndMenu();
// // //         }

// // //         ImGui::EndMainMenuBar();
// // //     }
// // // }

// // // void Application::RenderToolbar() {
// // //     ImGui::Begin("Toolbar", nullptr,
// // //         ImGuiWindowFlags_NoCollapse |
// // //         ImGuiWindowFlags_NoTitleBar |
// // //         ImGuiWindowFlags_NoResize);

// // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Toolbar", Shortcuts::ShortcutZone::Toolbar);

// // //     ImGui::Text("Tools");
// // //     ImGui::Separator();

// // //     const float iconSize = 20.0f;
// // //     const float buttonHeight = 36.0f;

// // //     // Tool 1
// // //     if (ImGui::BeginChild("##tool1", ImVec2(0, buttonHeight), false)) {
// // //         UI::IconManager::Instance().RenderBicolorIcon("tool1", iconSize);
// // //         ImGui::SameLine();
// // //         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
// // //         if (ImGui::Selectable("Tool 1", false))
// // //             TestAction_Tool1();
// // //     }
// // //     ImGui::EndChild();

// // //     // Tool 2
// // //     if (ImGui::BeginChild("##tool2", ImVec2(0, buttonHeight), false)) {
// // //         UI::IconManager::Instance().RenderBicolorIcon("tool2", iconSize);
// // //         ImGui::SameLine();
// // //         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
// // //         if (ImGui::Selectable("Tool 2", false))
// // //             TestAction_Tool2();
// // //     }
// // //     ImGui::EndChild();

// // //     ImGui::Separator();

// // //     // Bottom icons
// // //     UI::IconManager::Instance().RenderMulticolorIcon("logo_carto", 32.0f);

// // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 24.0f);

// // //     ImGui::End();
// // // }

// // // // void Application::RenderIconTestWindow() {
// // // //     ImGui::Begin("Icon Test Lab");

// // // //     // ===== NOUVELLES SECTIONS BICOLORES & MULTICOLORES =====

// // // //     ImGui::SeparatorText("BICOLOR ICONS (Token-based)");
// // // //     ImGui::Text("These icons use semantic.icon.color.primary & secondary");

// // // //     ImGui::Text("Settings (bicolor):");
// // // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 32.0f);
// // // //     ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 48.0f);
// // // //     ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 64.0f);

// // // //     ImGui::Text("Tool1 (bicolor):");
// // // //     UI::IconManager::Instance().RenderBicolorIcon("tool1", 32.0f);
// // // //     ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderBicolorIcon("tool1", 48.0f);

// // // //     ImGui::Text("Tool2 (bicolor):");
// // // //     UI::IconManager::Instance().RenderBicolorIcon("tool2", 32.0f);
// // // //     ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderBicolorIcon("tool2", 48.0f);

// // // //     ImGui::Separator();

// // // //     ImGui::SeparatorText("MULTICOLOR ICONS (Original colors)");
// // // //     ImGui::Text("These icons use their default palette");

// // // //     ImGui::Text("Manga Icon:");
// // // //     UI::IconManager::Instance().RenderMulticolorIcon("manga_icon", 64.0f);

// // // //     ImGui::Text("Updated Tool 1 Icon:");
// // // //     UI::IconManager::Instance().RenderMulticolorIcon("tool1", 64.0f);

// // // //     ImGui::Text("Three Balls:");
// // // //     UI::IconManager::Instance().RenderMulticolorIcon("three_balls", 64.0f);

// // // //     ImGui::Text("Logo Carto:");
// // // //     UI::IconManager::Instance().RenderMulticolorIcon("logo_carto", 64.0f);

// // // //     ImGui::Separator();

// // // //     // ===== SECTIONS LEGACY (conservées) =====

// // // //     ImGui::SeparatorText("LEGACY METHODS");

// // // //     // 1. Tailles multiples
// // // //     ImGui::Text("Logo Sizes:");
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 16.0f); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);

// // // //     ImGui::Separator();

// // // //     // 2. Couleurs fixes
// // // //     ImGui::Text("Settings Tinted:");
// // // //     ImVec4 cyan(0, 1, 1, 1);
// // // //     ImVec4 red(1, 0, 0, 1);
// // // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &cyan); ImGui::SameLine();
// // // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &red);

// // // //     ImGui::Separator();

// // // //     // 3. Color Picker dynamique pour open.svg
// // // //     ImGui::Text("Dynamic Color (Open Icon):");
// // // //     static ImVec4 dynamicColor = ImVec4(1, 1, 0, 1);

// // // //     if (ImGui::ColorEdit4("Icon Color", (float*)&dynamicColor)) {
// // // //         // Cache auto avec clé
// // // //     }

// // // //     UI::IconManager::Instance().RenderIcon("open", 48.0f, &dynamicColor);

// // // //     ImGui::Separator();
// // // //     ImGui::Text("Logo Carto – Multi Shape Control");

// // // //     // Couleurs initiales = SVG
// // // //     static ImVec4 holeColor   = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // transparent
// // // //     static ImVec4 ringColor   = ImVec4(0.69f, 0.25f, 0.57f, 1.0f); // #b04191
// // // //     static ImVec4 cursorColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // noir

// // // //     ImGui::ColorEdit4("Hole (transparent)", (float*)&holeColor);
// // // //     ImGui::ColorEdit4("Ring",              (float*)&ringColor);
// // // //     ImGui::ColorEdit4("Cursor (fill+stroke)",            (float*)&cursorColor);

// // // //     std::map<int, ImVec4> logoColors;
// // // //     logoColors[0] = holeColor;
// // // //     logoColors[1] = ringColor;
// // // //     logoColors[2] = cursorColor; // Appliqué au fill ET stroke maintenant

// // // //     UI::IconManager::Instance().RenderIcon(
// // // //         "logo_carto",
// // // //         64.0f,
// // // //         nullptr,
// // // //         &logoColors
// // // //     );

// // // //     ImGui::Separator();
// // // //     ImGui::Text("Three Balls – Independent Colors");

// // // //     static ImVec4 ball0 = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // rouge
// // // //     static ImVec4 ball1 = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // bleu
// // // //     static ImVec4 ball2 = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // jaune

// // // //     ImGui::ColorEdit4("Ball 1", (float*)&ball0);
// // // //     ImGui::ColorEdit4("Ball 2", (float*)&ball1);
// // // //     ImGui::ColorEdit4("Ball 3", (float*)&ball2);

// // // //     std::map<int, ImVec4> ballsColors;
// // // //     ballsColors[0] = ball0;
// // // //     ballsColors[1] = ball1;
// // // //     ballsColors[2] = ball2;

// // // //     UI::IconManager::Instance().RenderIcon(
// // // //         "three_balls",
// // // //         64.0f,
// // // //         nullptr,
// // // //         &ballsColors
// // // //     );

// // // //     ImGui::End();
// // // // }
// // // void Application::RenderIconTestWindow() {
// // //     ImGui::Begin("Icon Test Lab");

// // //     // ===== CONFIGURATION DES MÉTADONNÉES (à faire une seule fois au démarrage) =====
// // //     static bool metadataConfigured = false;
// // //     if (!metadataConfigured) {
// // //         // Configuration pour tool1: 3 shapes avec 2 zones de couleur
// // //         // Shape 0 = tournevis (primaire), Shapes 1,2 = marteau + ligne (secondaire)
// // //         UI::IconMetadata tool1Meta;
// // //         tool1Meta.scheme = UI::IconColorScheme::Bicolor;
// // //         tool1Meta.primaryShapes = {0};      // Tournevis
// // //         tool1Meta.secondaryShapes = {1, 2}; // Marteau + ligne
// // //         UI::IconManager::Instance().SetIconMetadata("tool1", tool1Meta);

// // //         // Configuration pour tool2 (si applicable)
// // //         UI::IconMetadata tool2Meta;
// // //         tool2Meta.scheme = UI::IconColorScheme::Bicolor;
// // //         tool2Meta.primaryShapes = {0};
// // //         tool2Meta.secondaryShapes = {1};
// // //         UI::IconManager::Instance().SetIconMetadata("tool2", tool2Meta);

// // //         // Configuration pour settings
// // //         UI::IconMetadata settingsMeta;
// // //         settingsMeta.scheme = UI::IconColorScheme::Bicolor;
// // //         settingsMeta.primaryShapes = {0};
// // //         settingsMeta.secondaryShapes = {1};
// // //         UI::IconManager::Instance().SetIconMetadata("settings", settingsMeta);

// // //         // Configuration pour logo_carto (multicolore)
// // //         // Shape 0 = trou transparent, Shape 1 = anneau violet, Shape 2 = curseur noir
// // //         UI::IconMetadata logoMeta;
// // //         logoMeta.scheme = UI::IconColorScheme::Multicolor;
// // //         logoMeta.multicolorDefaults[0] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // transparent
// // //         logoMeta.multicolorDefaults[1] = ImVec4(0.69f, 0.25f, 0.57f, 1.0f); // #b04191
// // //         logoMeta.multicolorDefaults[2] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // noir
// // //         UI::IconManager::Instance().SetIconMetadata("logo_carto", logoMeta);

// // //         // Configuration pour manga_icon (multicolore)
// // //         UI::IconMetadata mangaMeta;
// // //         mangaMeta.scheme = UI::IconColorScheme::Multicolor;
// // //         // Définir les couleurs par défaut selon votre manga_icon.svg
// // //         // Par exemple:
// // //         mangaMeta.multicolorDefaults[0] = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); // peau
// // //         mangaMeta.multicolorDefaults[1] = ImVec4(0.2f, 0.1f, 0.05f, 1.0f); // cheveux
// // //         mangaMeta.multicolorDefaults[2] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // vêtements
// // //         UI::IconManager::Instance().SetIconMetadata("manga_icon", mangaMeta);

// // //         // Configuration pour three_balls (multicolore)
// // //         UI::IconMetadata ballsMeta;
// // //         ballsMeta.scheme = UI::IconColorScheme::Multicolor;
// // //         ballsMeta.multicolorDefaults[0] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // rouge
// // //         ballsMeta.multicolorDefaults[1] = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // bleu
// // //         ballsMeta.multicolorDefaults[2] = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // jaune
// // //         UI::IconManager::Instance().SetIconMetadata("three_balls", ballsMeta);

// // //         metadataConfigured = true;
// // //     }

// // //     // ===== NOUVELLES SECTIONS BICOLORES & MULTICOLORES =====

// // //     ImGui::SeparatorText("BICOLOR ICONS (Token-based)");
// // //     ImGui::Text("These icons use semantic.icon.color.primary & secondary");

// // //     ImGui::Text("Settings (bicolor):");
// // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 32.0f);
// // //     ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 48.0f);
// // //     ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderBicolorIcon("settings", 64.0f);

// // //     ImGui::Text("Tool1 (bicolor):");
// // //     UI::IconManager::Instance().RenderBicolorIcon("tool1", 32.0f);
// // //     ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderBicolorIcon("tool1", 48.0f);

// // //     ImGui::Text("Tool2 (bicolor):");
// // //     UI::IconManager::Instance().RenderBicolorIcon("tool2", 32.0f);
// // //     ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderBicolorIcon("tool2", 48.0f);

// // //     ImGui::Separator();

// // //     ImGui::SeparatorText("MULTICOLOR ICONS (Original colors)");
// // //     ImGui::Text("These icons use their default palette");

// // //     ImGui::Text("Manga Icon:");
// // //     UI::IconManager::Instance().RenderMulticolorIcon("manga_icon", 64.0f);

// // //     ImGui::Text("Updated Tool 1 Icon:");
// // //     UI::IconManager::Instance().RenderMulticolorIcon("tool1", 64.0f);

// // //     ImGui::Text("Three Balls:");
// // //     UI::IconManager::Instance().RenderMulticolorIcon("three_balls", 64.0f);

// // //     ImGui::Text("Logo Carto:");
// // //     UI::IconManager::Instance().RenderMulticolorIcon("logo_carto", 64.0f);

// // //     ImGui::Separator();

// // //     // ===== SECTIONS LEGACY (conservées) =====

// // //     ImGui::SeparatorText("LEGACY METHODS");

// // //     // 1. Tailles multiples
// // //     ImGui::Text("Logo Sizes:");
// // //     UI::IconManager::Instance().RenderIcon("logo_carto", 16.0f); ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f); ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderIcon("logo_carto", 64.0f);

// // //     ImGui::Separator();

// // //     // 2. Couleurs fixes
// // //     ImGui::Text("Settings Tinted:");
// // //     ImVec4 cyan(0, 1, 1, 1);
// // //     ImVec4 red(1, 0, 0, 1);
// // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &cyan); ImGui::SameLine();
// // //     UI::IconManager::Instance().RenderIcon("settings", 32.0f, &red);

// // //     ImGui::Separator();

// // //     // 3. Color Picker dynamique pour open.svg
// // //     ImGui::Text("Dynamic Color (Open Icon):");
// // //     static ImVec4 dynamicColor = ImVec4(1, 1, 0, 1);

// // //     if (ImGui::ColorEdit4("Icon Color", (float*)&dynamicColor)) {
// // //         // Cache auto avec clé
// // //     }

// // //     UI::IconManager::Instance().RenderIcon("open", 48.0f, &dynamicColor);

// // //     ImGui::Separator();
// // //     ImGui::Text("Logo Carto – Multi Shape Control");

// // //     // Couleurs initiales = SVG
// // //     static ImVec4 holeColor   = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // transparent
// // //     static ImVec4 ringColor   = ImVec4(0.69f, 0.25f, 0.57f, 1.0f); // #b04191
// // //     static ImVec4 cursorColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f); // noir

// // //     ImGui::ColorEdit4("Hole (transparent)", (float*)&holeColor);
// // //     ImGui::ColorEdit4("Ring",              (float*)&ringColor);
// // //     ImGui::ColorEdit4("Cursor (fill+stroke)",            (float*)&cursorColor);

// // //     std::map<int, ImVec4> logoColors;
// // //     logoColors[0] = holeColor;
// // //     logoColors[1] = ringColor;
// // //     logoColors[2] = cursorColor; // Appliqué au fill ET stroke maintenant

// // //     UI::IconManager::Instance().RenderIcon(
// // //         "logo_carto",
// // //         64.0f,
// // //         nullptr,
// // //         &logoColors
// // //     );

// // //     ImGui::Separator();
// // //     ImGui::Text("Three Balls – Independent Colors");

// // //     static ImVec4 ball0 = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // rouge
// // //     static ImVec4 ball1 = ImVec4(0.0f, 0.0f, 1.0f, 1.0f); // bleu
// // //     static ImVec4 ball2 = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // jaune

// // //     ImGui::ColorEdit4("Ball 1", (float*)&ball0);
// // //     ImGui::ColorEdit4("Ball 2", (float*)&ball1);
// // //     ImGui::ColorEdit4("Ball 3", (float*)&ball2);

// // //     std::map<int, ImVec4> ballsColors;
// // //     ballsColors[0] = ball0;
// // //     ballsColors[1] = ball1;
// // //     ballsColors[2] = ball2;

// // //     UI::IconManager::Instance().RenderIcon(
// // //         "three_balls",
// // //         64.0f,
// // //         nullptr,
// // //         &ballsColors
// // //     );

// // //     ImGui::End();
// // // }

// // // void Application::RenderDesignExample() {
// // //     ImGui::Begin("Design System Example", nullptr, ImGuiWindowFlags_NoCollapse);
// // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Design System Example",
// // //         Shortcuts::ShortcutZone::DesignExample);

// // //     ImGui::Text("This window uses the Design System!");

// // //     auto& ds = DesignSystem::DesignSystem::Instance();
// // //     ds.PushAllStyles();

// // //     static int int_value = 50;
// // //     ImGui::Text("Drag to adjust:");
// // //     ImGui::SetNextItemWidth(150.0f);
// // //     ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");

// // //     ImGui::SameLine();
// // //     if (ImGui::Button("Print Value", ImVec2(120, 0))) {
// // //         printf("Value: %d\n", int_value);
// // //     }

// // //     ds.PopAllStyles();

// // //     ImGui::End();
// // // }

// // // void Application::RenderThemePreview() {
// // //     ImGui::Begin("Theme Preview", nullptr, ImGuiWindowFlags_NoCollapse);
// // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Theme Preview",
// // //         Shortcuts::ShortcutZone::ThemePreview);

// // //     ImGui::Text("Preview with current context");
// // //     ImGui::Separator();

// // //     auto& ds = DesignSystem::DesignSystem::Instance();

// // //     try {
// // //         ImVec4 bgColor = ds.GetColor("semantic.color.background");
// // //         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");

// // //         ImGui::ColorButton("##bg", bgColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
// // //         ImGui::SameLine();
// // //         ImGui::Text("Background Color");

// // //         ImGui::ColorButton("##primary", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
// // //         ImGui::SameLine();
// // //         ImGui::Text("Primary Color");

// // //         ImGui::Separator();
// // //         ImGui::Text("Sample Components:");

// // //         if (ImGui::Button("Sample Button")) {}

// // //         static char textBuffer[128] = "Sample Input";
// // //         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));

// // //     } catch (const std::exception& e) {
// // //         ImGui::Text("Error resolving tokens: %s", e.what());
// // //     }

// // //     ImGui::End();
// // // }

// // // void Application::RenderTestZone1() {
// // //     ImGui::Begin("Test Zone 1", nullptr, ImGuiWindowFlags_NoCollapse);
// // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 1",
// // //         Shortcuts::ShortcutZone::TestZone1);

// // //     ImGui::Text("Test shortcuts in this zone");
// // //     ImGui::Text("Press Ctrl+A to trigger Zone 1 action");

// // //     if (ImGui::Button("Trigger Zone 1 Action")) {
// // //         TestAction_Zone1();
// // //     }

// // //     ImGui::End();
// // // }

// // // void Application::RenderTestZone2() {
// // //     ImGui::Begin("Test Zone 2", nullptr, ImGuiWindowFlags_NoCollapse);
// // //     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Test Zone 2",
// // //         Shortcuts::ShortcutZone::TestZone2);

// // //     ImGui::Text("Test shortcuts in this zone");
// // //     ImGui::Text("Press Ctrl+A to trigger Zone 2 action");
// // //     ImGui::Text("(same shortcut as Zone 1, but different action)");

// // //     if (ImGui::Button("Trigger Zone 2 Action")) {
// // //         TestAction_Zone2();
// // //     }

// // //     ImGui::End();
// // // }

// // // void Application::RenderFloatingWindows() {
// // //     if (showTokenEditor_) {
// // //         auto& ds = DesignSystem::DesignSystem::Instance();
// // //         tokenEditor_.Render(ds.GetCurrentContext(), ds.GetOverrideManager());
// // //     }

// // //     if (showShortcutEditor_) {
// // //         shortcutEditor_.Render(&showShortcutEditor_);
// // //     }

// // //     if (showImGuiDemo_) {
// // //         ImGui::ShowDemoWindow(&showImGuiDemo_);
// // //     }
// // // }

// // // void Application::Shutdown() {
// // //     VkResult err = vkDeviceWaitIdle(device_);
// // //     check_vk_result(err);

// // //     DesignSystem::DesignSystem::Instance().Shutdown();
// // //     Shortcuts::ShortcutManager::Instance().Shutdown();
// // //     UI::IconManager::Instance().Shutdown();

// // //     vkDestroyCommandPool(device_, commandPool_, g_Allocator);

// // //     ImGui_ImplVulkan_Shutdown();
// // //     ImGui_ImplSDL3_Shutdown();
// // //     ImGui::DestroyContext();

// // //     ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &mainWindowData_, g_Allocator);
// // //     vkDestroySurfaceKHR(instance_, mainWindowData_.Surface, g_Allocator);

// // //     vkDestroyDescriptorPool(device_, descriptorPool_, g_Allocator);

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //     auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)
// // //         vkGetInstanceProcAddr(instance_, "vkDestroyDebugReportCallbackEXT");
// // //     f_vkDestroyDebugReportCallbackEXT(instance_, g_DebugReport, g_Allocator);
// // // #endif

// // //     vkDestroyDevice(device_, g_Allocator);
// // //     vkDestroyInstance(instance_, g_Allocator);

// // //     SDL_DestroyWindow(window_);
// // //     SDL_Quit();
// // // }

// // // // ========== Test Actions ==========

// // // void Application::TestAction_NewFile() {
// // //     std::cout << "[ACTION] New File" << std::endl;
// // // }

// // // void Application::TestAction_OpenFile() {
// // //     std::cout << "[ACTION] Open File" << std::endl;
// // // }

// // // void Application::TestAction_SaveFile() {
// // //     std::cout << "[ACTION] Save File" << std::endl;
// // // }

// // // void Application::TestAction_Quit() {
// // //     std::cout << "[ACTION] Quit" << std::endl;
// // // }

// // // void Application::TestAction_Tool1() {
// // //     std::cout << "[ACTION] Tool 1 (Toolbar)" << std::endl;
// // // }

// // // void Application::TestAction_Tool2() {
// // //     std::cout << "[ACTION] Tool 2 (Toolbar)" << std::endl;
// // // }

// // // void Application::TestAction_Zone1() {
// // //     std::cout << "[ACTION] Test Zone 1 Action" << std::endl;
// // // }

// // // void Application::TestAction_Zone2() {
// // //     std::cout << "[ACTION] Test Zone 2 Action" << std::endl;
// // // }

// // // } // namespace App

// // #include "Application.h"
// // #include <iostream>

// // #ifdef _DEBUG
// // #define APP_USE_VULKAN_DEBUG_REPORT
// // #endif
// // #include <imgui_impl_sdl3.h>

// // static VkAllocationCallbacks* g_Allocator = nullptr;
// // static uint32_t g_MinImageCount = 2;
// // static bool g_SwapChainRebuild = false;

// // namespace App {

// // static void check_vk_result(VkResult err) {
// //     if (err == VK_SUCCESS) return;
// //     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
// //     if (err < 0) abort();
// // }

// // Application::Application() {}

// // Application::~Application() {}

// // void Application::Run() {
// //     while (running_) {
// //         ProcessEvents();
// //         Update();
// //         Render();
// //         Present();
// //     }
// // }

// // void Application::ProcessEvents() {
// //     SDL_Event event;
// //     while (SDL_PollEvent(&event)) {
// //         ImGui_ImplSDL3_ProcessEvent(&event);
// //         if (event.type == SDL_EVENT_QUIT)
// //             running_ = false;
// //         if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
// //             event.window.windowID == SDL_GetWindowID(window_))
// //             running_ = false;
// //     }

// //     if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
// //         SDL_Delay(10);
// //         return;
// //     }

// //     // Handle window resize
// //     int fb_width, fb_height;
// //     SDL_GetWindowSize(window_, &fb_width, &fb_height);
// //     if (fb_width > 0 && fb_height > 0 &&
// //         (g_SwapChainRebuild || mainWindowData_.Width != fb_width || mainWindowData_.Height != fb_height)) {
// //         ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
// //         ImGui_ImplVulkanH_CreateOrResizeWindow(
// //             instance_, physicalDevice_, device_, &mainWindowData_,
// //             queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0
// //         );
// //         mainWindowData_.FrameIndex = 0;
// //         g_SwapChainRebuild = false;
// //     }
// // }

// // void Application::Update() {
// //     ImGui_ImplVulkan_NewFrame();
// //     ImGui_ImplSDL3_NewFrame();
// //     ImGui::NewFrame();

// //     // Process shortcuts
// //     Shortcuts::ShortcutManager::Instance().ProcessInput();

// //     // Render UI
// //     RenderDockSpace();
// //     RenderMainMenuBar();
// //     RenderToolbar();
// //     RenderIconTestWindow();
// //     RenderDesignExample();
// //     RenderThemePreview();
// //     RenderTestZone1();
// //     RenderTestZone2();
// //     RenderIconEditorWindow();
// //     RenderFloatingWindows();
// // }

// // void Application::Render() {
// //     ImGui::Render();
// //     ImDrawData* draw_data = ImGui::GetDrawData();
// //     const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
// //     if (is_minimized) return;

// //     auto& ds = DesignSystem::DesignSystem::Instance();
// //     ImVec4 clear_color = ds.GetColor("semantic.color.background");
// //     mainWindowData_.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
// //     mainWindowData_.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
// //     mainWindowData_.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
// //     mainWindowData_.ClearValue.color.float32[3] = clear_color.w;

// //     VkSemaphore image_acquired_semaphore =
// //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].ImageAcquiredSemaphore;
// //     VkSemaphore render_complete_semaphore =
// //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;

// //     VkResult err = vkAcquireNextImageKHR(
// //         device_, mainWindowData_.Swapchain, UINT64_MAX,
// //         image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex
// //     );
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// //         g_SwapChainRebuild = true;
// //     }
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// //     ImGui_ImplVulkanH_Frame* fd = &mainWindowData_.Frames[mainWindowData_.FrameIndex];
// //     {
// //         err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
// //         check_vk_result(err);
// //         err = vkResetFences(device_, 1, &fd->Fence);
// //         check_vk_result(err);
// //     }

// //     {
// //         err = vkResetCommandPool(device_, fd->CommandPool, 0);
// //         check_vk_result(err);
// //         VkCommandBufferBeginInfo info = {};
// //         info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
// //         info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
// //         err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
// //         check_vk_result(err);
// //     }

// //     {
// //         VkRenderPassBeginInfo info = {};
// //         info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
// //         info.renderPass = mainWindowData_.RenderPass;
// //         info.framebuffer = fd->Framebuffer;
// //         info.renderArea.extent.width = mainWindowData_.Width;
// //         info.renderArea.extent.height = mainWindowData_.Height;
// //         info.clearValueCount = 1;
// //         info.pClearValues = &mainWindowData_.ClearValue;
// //         vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
// //     }

// //     ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

// //     vkCmdEndRenderPass(fd->CommandBuffer);
// //     {
// //         VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
// //         VkSubmitInfo info = {};
// //         info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
// //         info.waitSemaphoreCount = 1;
// //         info.pWaitSemaphores = &image_acquired_semaphore;
// //         info.pWaitDstStageMask = &wait_stage;
// //         info.commandBufferCount = 1;
// //         info.pCommandBuffers = &fd->CommandBuffer;
// //         info.signalSemaphoreCount = 1;
// //         info.pSignalSemaphores = &render_complete_semaphore;

// //         err = vkEndCommandBuffer(fd->CommandBuffer);
// //         check_vk_result(err);
// //         err = vkQueueSubmit(queue_, 1, &info, fd->Fence);
// //         check_vk_result(err);
// //     }
// // }

// // void Application::Present() {
// //     if (g_SwapChainRebuild) return;

// //     VkSemaphore render_complete_semaphore =
// //         mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;
// //     VkPresentInfoKHR info = {};
// //     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
// //     info.waitSemaphoreCount = 1;
// //     info.pWaitSemaphores = &render_complete_semaphore;
// //     info.swapchainCount = 1;
// //     info.pSwapchains = &mainWindowData_.Swapchain;
// //     info.pImageIndices = &mainWindowData_.FrameIndex;

// //     VkResult err = vkQueuePresentKHR(queue_, &info);
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
// //         g_SwapChainRebuild = true;
// //     }
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
// //     if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

// //     mainWindowData_.SemaphoreIndex =
// //         (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;
// // }

// // // ========== Test Actions ==========

// // void Application::TestAction_NewFile() {
// //     std::cout << "[ACTION] New File" << std::endl;
// // }

// // void Application::TestAction_OpenFile() {
// //     std::cout << "[ACTION] Open File" << std::endl;
// // }

// // void Application::TestAction_SaveFile() {
// //     std::cout << "[ACTION] Save File" << std::endl;
// // }

// // void Application::TestAction_Quit() {
// //     std::cout << "[ACTION] Quit" << std::endl;
// // }

// // void Application::TestAction_Tool1() {
// //     std::cout << "[ACTION] Tool 1 (Toolbar)" << std::endl;
// // }

// // void Application::TestAction_Tool2() {
// //     std::cout << "[ACTION] Tool 2 (Toolbar)" << std::endl;
// // }

// // void Application::TestAction_Zone1() {
// //     std::cout << "[ACTION] Test Zone 1 Action" << std::endl;
// // }

// // void Application::TestAction_Zone2() {
// //     std::cout << "[ACTION] Test Zone 2 Action" << std::endl;
// // }

// // } // namespace App

// #include "Application.h"
// #include <iostream>
// #include <UI/IconManager.h>
// #include <DesignSystem/DesignSystem.h>
// #include <Shortcuts/ShortcutManager.h>

// #ifdef _DEBUG
// #define APP_USE_VULKAN_DEBUG_REPORT
// #endif
// #include <imgui_impl_sdl3.h>

// static VkAllocationCallbacks *g_Allocator = nullptr;
// static uint32_t g_MinImageCount = 2;
// static bool g_SwapChainRebuild = false;

// namespace App
// {

//     static void check_vk_result(VkResult err)
//     {
//         if (err == VK_SUCCESS)
//             return;
//         fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
//         if (err < 0)
//             abort();
//     }

//     Application::Application() {}

//     Application::~Application() {}

//     void Application::Run()
//     {
//         while (running_)
//         {
//             ProcessEvents();
//             Update();
//             Render();
//             Present();
//         }
//     }

//     void Application::ProcessEvents()
//     {
//         SDL_Event event;
//         while (SDL_PollEvent(&event))
//         {
//             ImGui_ImplSDL3_ProcessEvent(&event);
//             if (event.type == SDL_EVENT_QUIT)
//                 running_ = false;
//             if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
//                 event.window.windowID == SDL_GetWindowID(window_))
//                 running_ = false;
//         }

//         if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)
//         {
//             SDL_Delay(10);
//             return;
//         }

//         // Handle window resize
//         int fb_width, fb_height;
//         SDL_GetWindowSize(window_, &fb_width, &fb_height);
//         if (fb_width > 0 && fb_height > 0 &&
//             (g_SwapChainRebuild || mainWindowData_.Width != fb_width || mainWindowData_.Height != fb_height))
//         {
//             ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
//             ImGui_ImplVulkanH_CreateOrResizeWindow(
//                 instance_, physicalDevice_, device_, &mainWindowData_,
//                 queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
//             mainWindowData_.FrameIndex = 0;
//             g_SwapChainRebuild = false;
//         }
//     }

//     void Application::Update()
//     {
//         // IMPORTANT: Nettoyer le cache au début du frame (évite les crashs Vulkan)
//         UI::IconManager::Instance().CleanupCacheIfNeeded();

//         ImGui_ImplVulkan_NewFrame();
//         ImGui_ImplSDL3_NewFrame();
//         ImGui::NewFrame();

//         // Process shortcuts
//         Shortcuts::ShortcutManager::Instance().ProcessInput();

//         // Render UI
//         RenderDockSpace();
//         RenderMainMenuBar();
//         RenderToolbar();
//         RenderIconTestWindow();
//         RenderDesignExample();
//         RenderThemePreview();
//         RenderTestZone1();
//         RenderTestZone2();
//         RenderIconEditorWindow();
//         RenderFloatingWindows();
//     }

//     void Application::Render()
//     {
//         ImGui::Render();
//         ImDrawData *draw_data = ImGui::GetDrawData();
//         const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
//         if (is_minimized)
//             return;

//         auto &ds = DesignSystem::DesignSystem::Instance();
//         ImVec4 clear_color = ds.GetColor("semantic.color.background");
//         mainWindowData_.ClearValue.color.float32[0] = clear_color.x * clear_color.w;
//         mainWindowData_.ClearValue.color.float32[1] = clear_color.y * clear_color.w;
//         mainWindowData_.ClearValue.color.float32[2] = clear_color.z * clear_color.w;
//         mainWindowData_.ClearValue.color.float32[3] = clear_color.w;

//         VkSemaphore image_acquired_semaphore =
//             mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].ImageAcquiredSemaphore;
//         VkSemaphore render_complete_semaphore =
//             mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;

//         VkResult err = vkAcquireNextImageKHR(
//             device_, mainWindowData_.Swapchain, UINT64_MAX,
//             image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex);
//         if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
//         {
//             g_SwapChainRebuild = true;
//         }
//         if (err == VK_ERROR_OUT_OF_DATE_KHR)
//             return;
//         if (err != VK_SUBOPTIMAL_KHR)
//             check_vk_result(err);

//         ImGui_ImplVulkanH_Frame *fd = &mainWindowData_.Frames[mainWindowData_.FrameIndex];
//         {
//             err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
//             check_vk_result(err);
//             err = vkResetFences(device_, 1, &fd->Fence);
//             check_vk_result(err);
//         }

//         {
//             err = vkResetCommandPool(device_, fd->CommandPool, 0);
//             check_vk_result(err);
//             VkCommandBufferBeginInfo info = {};
//             info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//             info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//             err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
//             check_vk_result(err);
//         }

//         {
//             VkRenderPassBeginInfo info = {};
//             info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
//             info.renderPass = mainWindowData_.RenderPass;
//             info.framebuffer = fd->Framebuffer;
//             info.renderArea.extent.width = mainWindowData_.Width;
//             info.renderArea.extent.height = mainWindowData_.Height;
//             info.clearValueCount = 1;
//             info.pClearValues = &mainWindowData_.ClearValue;
//             vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
//         }

//         ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

//         vkCmdEndRenderPass(fd->CommandBuffer);
//         {
//             VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
//             VkSubmitInfo info = {};
//             info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//             info.waitSemaphoreCount = 1;
//             info.pWaitSemaphores = &image_acquired_semaphore;
//             info.pWaitDstStageMask = &wait_stage;
//             info.commandBufferCount = 1;
//             info.pCommandBuffers = &fd->CommandBuffer;
//             info.signalSemaphoreCount = 1;
//             info.pSignalSemaphores = &render_complete_semaphore;

//             err = vkEndCommandBuffer(fd->CommandBuffer);
//             check_vk_result(err);
//             err = vkQueueSubmit(queue_, 1, &info, fd->Fence);
//             check_vk_result(err);
//         }
//     }

//     void Application::Present()
//     {
//         if (g_SwapChainRebuild)
//             return;

//         VkSemaphore render_complete_semaphore =
//             mainWindowData_.FrameSemaphores[mainWindowData_.SemaphoreIndex].RenderCompleteSemaphore;
//         VkPresentInfoKHR info = {};
//         info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
//         info.waitSemaphoreCount = 1;
//         info.pWaitSemaphores = &render_complete_semaphore;
//         info.swapchainCount = 1;
//         info.pSwapchains = &mainWindowData_.Swapchain;
//         info.pImageIndices = &mainWindowData_.FrameIndex;

//         VkResult err = vkQueuePresentKHR(queue_, &info);
//         if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
//         {
//             g_SwapChainRebuild = true;
//         }
//         if (err == VK_ERROR_OUT_OF_DATE_KHR)
//             return;
//         if (err != VK_SUBOPTIMAL_KHR)
//             check_vk_result(err);

//         mainWindowData_.SemaphoreIndex =
//             (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;
//     }

//     // ========== Test Actions ==========

//     void Application::TestAction_NewFile()
//     {
//         std::cout << "[ACTION] New File" << std::endl;
//     }

//     void Application::TestAction_OpenFile()
//     {
//         std::cout << "[ACTION] Open File" << std::endl;
//     }

//     void Application::TestAction_SaveFile()
//     {
//         std::cout << "[ACTION] Save File" << std::endl;
//     }

//     void Application::TestAction_Quit()
//     {
//         std::cout << "[ACTION] Quit" << std::endl;
//     }

//     void Application::TestAction_Tool1()
//     {
//         std::cout << "[ACTION] Tool 1 (Toolbar)" << std::endl;
//     }

//     void Application::TestAction_Tool2()
//     {
//         std::cout << "[ACTION] Tool 2 (Toolbar)" << std::endl;
//     }

//     void Application::TestAction_Zone1()
//     {
//         std::cout << "[ACTION] Test Zone 1 Action" << std::endl;
//     }

//     void Application::TestAction_Zone2()
//     {
//         std::cout << "[ACTION] Test Zone 2 Action" << std::endl;
//     }

// } // namespace App














#include "Application.h"
#include <iostream>
#include <UI/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif
#include <imgui_impl_sdl3.h>

static VkAllocationCallbacks* g_Allocator = nullptr;
static uint32_t g_MinImageCount = 2;
static bool g_SwapChainRebuild = false;

namespace App {

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

Application::Application() {}
Application::~Application() {}

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
        if (event.type == SDL_EVENT_QUIT) running_ = false;
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
        (g_SwapChainRebuild || mainWindowData_.Width != fb_width || mainWindowData_.Height != fb_height)) {
        ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
        ImGui_ImplVulkanH_CreateOrResizeWindow(
            instance_, physicalDevice_, device_, &mainWindowData_,
            queueFamily_, g_Allocator, fb_width, fb_height, g_MinImageCount, 0
        );
        mainWindowData_.FrameIndex = 0;
        g_SwapChainRebuild = false;
    }
}

void Application::Update() {
    // CRITICAL: Clean cache at frame start (prevents Vulkan crashes)
    UI::IconManager::Instance().CleanupCacheIfNeeded();
    
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    Shortcuts::ShortcutManager::Instance().ProcessInput();

    RenderDockSpace();
    RenderMainMenuBar();
    RenderToolbar();
    RenderIconTestWindow();
    RenderDesignExample();
    RenderThemePreview();
    RenderTestZone1();
    RenderTestZone2();
    RenderIconEditorWindow();
    RenderFloatingWindows();
}

void Application::Render() {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
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
        image_acquired_semaphore, VK_NULL_HANDLE, &mainWindowData_.FrameIndex
    );
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
    }
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
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
        check_vk_result(err);
    }

    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = mainWindowData_.RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = mainWindowData_.Width;
        info.renderArea.extent.height = mainWindowData_.Height;
        info.clearValueCount = 1;
        info.pClearValues = &mainWindowData_.ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

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
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &mainWindowData_.Swapchain;
    info.pImageIndices = &mainWindowData_.FrameIndex;

    VkResult err = vkQueuePresentKHR(queue_, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
    }
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) check_vk_result(err);

    mainWindowData_.SemaphoreIndex = 
        (mainWindowData_.SemaphoreIndex + 1) % mainWindowData_.SemaphoreCount;
}

// Test Actions
void Application::TestAction_NewFile() { std::cout << "[ACTION] New File" << std::endl; }
void Application::TestAction_OpenFile() { std::cout << "[ACTION] Open File" << std::endl; }
void Application::TestAction_SaveFile() { std::cout << "[ACTION] Save File" << std::endl; }
void Application::TestAction_Quit() { std::cout << "[ACTION] Quit" << std::endl; }
void Application::TestAction_Tool1() { std::cout << "[ACTION] Tool 1" << std::endl; }
void Application::TestAction_Tool2() { std::cout << "[ACTION] Tool 2" << std::endl; }
void Application::TestAction_Zone1() { std::cout << "[ACTION] Zone 1" << std::endl; }
void Application::TestAction_Zone2() { std::cout << "[ACTION] Zone 2" << std::endl; }

} // namespace App
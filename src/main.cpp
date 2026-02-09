// // // // Dear ImGui: standalone example application for SDL3 + Vulkan

// // // // Learn about Dear ImGui:
// // // // - FAQ                  https://dearimgui.com/faq
// // // // - Getting Started      https://dearimgui.com/getting-started
// // // // - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// // // // - Introduction, links and more at the top of imgui.cpp

// // // // Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// // // // - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
// // // //   You will use those if you want to use this rendering backend in your engine/app.
// // // // - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
// // // //   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// // // // Read comments in imgui_impl_vulkan.h.

// // // #include "imgui.h"
// // // #include "imgui_impl_sdl3.h"
// // // #include "imgui_impl_vulkan.h"
// // // #include <stdio.h>          // printf, fprintf
// // // #include <stdlib.h>         // abort
// // // #include <SDL3/SDL.h>
// // // #include <SDL3/SDL_vulkan.h>

// // // // This example doesn't compile with Emscripten yet! Awaiting SDL3 support.
// // // #ifdef __EMSCRIPTEN__
// // // #include "../libs/emscripten/emscripten_mainloop_stub.h"
// // // #endif

// // // // Volk headers
// // // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// // // #define VOLK_IMPLEMENTATION
// // // #include <volk.h>
// // // #endif

// // // //#define APP_USE_UNLIMITED_FRAME_RATE
// // // #ifdef _DEBUG
// // // #define APP_USE_VULKAN_DEBUG_REPORT
// // // static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
// // // #endif

// // // // Data
// // // static VkAllocationCallbacks*   g_Allocator = nullptr;
// // // static VkInstance               g_Instance = VK_NULL_HANDLE;
// // // static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
// // // static VkDevice                 g_Device = VK_NULL_HANDLE;
// // // static uint32_t                 g_QueueFamily = (uint32_t)-1;
// // // static VkQueue                  g_Queue = VK_NULL_HANDLE;
// // // static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
// // // static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

// // // static ImGui_ImplVulkanH_Window g_MainWindowData;
// // // static uint32_t                 g_MinImageCount = 2;
// // // static bool                     g_SwapChainRebuild = false;

// // // static void check_vk_result(VkResult err)
// // // {
// // //     if (err == VK_SUCCESS)
// // //         return;
// // //     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
// // //     if (err < 0)
// // //         abort();
// // // }

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // // static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
// // // {
// // //     (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
// // //     fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
// // //     return VK_FALSE;
// // // }
// // // #endif // APP_USE_VULKAN_DEBUG_REPORT

// // // static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
// // // {
// // //     for (const VkExtensionProperties& p : properties)
// // //         if (strcmp(p.extensionName, extension) == 0)
// // //             return true;
// // //     return false;
// // // }

// // // static void SetupVulkan(ImVector<const char*> instance_extensions)
// // // {
// // //     VkResult err;
// // // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// // //     volkInitialize();
// // // #endif

// // //     // Create Vulkan Instance
// // //     {
// // //         VkInstanceCreateInfo create_info = {};
// // //         create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

// // //         // Enumerate available extensions
// // //         uint32_t properties_count;
// // //         ImVector<VkExtensionProperties> properties;
// // //         vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
// // //         properties.resize(properties_count);
// // //         err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
// // //         check_vk_result(err);

// // //         // Enable required extensions
// // //         if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
// // //             instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
// // // #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
// // //         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
// // //         {
// // //             instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
// // //             create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
// // //         }
// // // #endif

// // //         // Enabling validation layers
// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //         const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
// // //         create_info.enabledLayerCount = 1;
// // //         create_info.ppEnabledLayerNames = layers;
// // //         instance_extensions.push_back("VK_EXT_debug_report");
// // // #endif

// // //         // Create Vulkan Instance
// // //         create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
// // //         create_info.ppEnabledExtensionNames = instance_extensions.Data;
// // //         err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
// // //         check_vk_result(err);
// // // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// // //         volkLoadInstance(g_Instance);
// // // #endif

// // //         // Setup the debug report callback
// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //         auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
// // //         IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
// // //         VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
// // //         debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
// // //         debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
// // //         debug_report_ci.pfnCallback = debug_report;
// // //         debug_report_ci.pUserData = nullptr;
// // //         err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
// // //         check_vk_result(err);
// // // #endif
// // //     }

// // //     // Select Physical Device (GPU)
// // //     g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
// // //     IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

// // //     // Select graphics queue family
// // //     g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
// // //     IM_ASSERT(g_QueueFamily != (uint32_t)-1);

// // //     // Create Logical Device (with 1 queue)
// // //     {
// // //         ImVector<const char*> device_extensions;
// // //         device_extensions.push_back("VK_KHR_swapchain");

// // //         // Enumerate physical device extension
// // //         uint32_t properties_count;
// // //         ImVector<VkExtensionProperties> properties;
// // //         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
// // //         properties.resize(properties_count);
// // //         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
// // // #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
// // //         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
// // //             device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
// // // #endif

// // //         const float queue_priority[] = { 1.0f };
// // //         VkDeviceQueueCreateInfo queue_info[1] = {};
// // //         queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
// // //         queue_info[0].queueFamilyIndex = g_QueueFamily;
// // //         queue_info[0].queueCount = 1;
// // //         queue_info[0].pQueuePriorities = queue_priority;
// // //         VkDeviceCreateInfo create_info = {};
// // //         create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
// // //         create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
// // //         create_info.pQueueCreateInfos = queue_info;
// // //         create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
// // //         create_info.ppEnabledExtensionNames = device_extensions.Data;
// // //         err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
// // //         check_vk_result(err);
// // //         vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
// // //     }

// // //     // Create Descriptor Pool
// // //     // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
// // //     {
// // //         VkDescriptorPoolSize pool_sizes[] =
// // //         {
// // //             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
// // //         };
// // //         VkDescriptorPoolCreateInfo pool_info = {};
// // //         pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
// // //         pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
// // //         pool_info.maxSets = 0;
// // //         for (VkDescriptorPoolSize& pool_size : pool_sizes)
// // //             pool_info.maxSets += pool_size.descriptorCount;
// // //         pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
// // //         pool_info.pPoolSizes = pool_sizes;
// // //         err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
// // //         check_vk_result(err);
// // //     }
// // // }

// // // // All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// // // // Your real engine/app may not use them.
// // // static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
// // // {
// // //     // Check for WSI support
// // //     VkBool32 res;
// // //     vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
// // //     if (res != VK_TRUE)
// // //     {
// // //         fprintf(stderr, "Error no WSI support on physical device 0\n");
// // //         exit(-1);
// // //     }

// // //     // Select Surface Format
// // //     const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
// // //     const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
// // //     wd->Surface = surface;
// // //     wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

// // //     // Select Present Mode
// // // #ifdef APP_USE_UNLIMITED_FRAME_RATE
// // //     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
// // // #else
// // //     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
// // // #endif
// // //     wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));
// // //     //printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

// // //     // Create SwapChain, RenderPass, Framebuffer, etc.
// // //     IM_ASSERT(g_MinImageCount >= 2);
// // //     ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);
// // // }

// // // static void CleanupVulkan()
// // // {
// // //     vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

// // // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // //     // Remove the debug report callback
// // //     auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
// // //     f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
// // // #endif // APP_USE_VULKAN_DEBUG_REPORT

// // //     vkDestroyDevice(g_Device, g_Allocator);
// // //     vkDestroyInstance(g_Instance, g_Allocator);
// // // }

// // // static void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd)
// // // {
// // //     ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
// // //     vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
// // // }

// // // static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
// // // {
// // //     VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
// // //     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
// // //     VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
// // //         g_SwapChainRebuild = true;
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR)
// // //         return;
// // //     if (err != VK_SUBOPTIMAL_KHR)
// // //         check_vk_result(err);

// // //     ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
// // //     {
// // //         err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
// // //         check_vk_result(err);

// // //         err = vkResetFences(g_Device, 1, &fd->Fence);
// // //         check_vk_result(err);
// // //     }
// // //     {
// // //         err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
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
// // //         info.renderPass = wd->RenderPass;
// // //         info.framebuffer = fd->Framebuffer;
// // //         info.renderArea.extent.width = wd->Width;
// // //         info.renderArea.extent.height = wd->Height;
// // //         info.clearValueCount = 1;
// // //         info.pClearValues = &wd->ClearValue;
// // //         vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
// // //     }

// // //     // Record dear imgui primitives into command buffer
// // //     ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

// // //     // Submit command buffer
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
// // //         err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
// // //         check_vk_result(err);
// // //     }
// // // }

// // // static void FramePresent(ImGui_ImplVulkanH_Window* wd)
// // // {
// // //     if (g_SwapChainRebuild)
// // //         return;
// // //     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
// // //     VkPresentInfoKHR info = {};
// // //     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
// // //     info.waitSemaphoreCount = 1;
// // //     info.pWaitSemaphores = &render_complete_semaphore;
// // //     info.swapchainCount = 1;
// // //     info.pSwapchains = &wd->Swapchain;
// // //     info.pImageIndices = &wd->FrameIndex;
// // //     VkResult err = vkQueuePresentKHR(g_Queue, &info);
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
// // //         g_SwapChainRebuild = true;
// // //     if (err == VK_ERROR_OUT_OF_DATE_KHR)
// // //         return;
// // //     if (err != VK_SUBOPTIMAL_KHR)
// // //         check_vk_result(err);
// // //     wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
// // // }

// // // // Main code
// // // int main(int, char**)
// // // {
// // //     // Setup SDL
// // //     // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
// // //     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
// // //     {
// // //         printf("Error: SDL_Init(): %s\n", SDL_GetError());
// // //         return 1;
// // //     }

// // //     // Create window with Vulkan graphics context
// // //     float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
// // //     SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
// // //     SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL3+Vulkan example", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
// // //     if (window == nullptr)
// // //     {
// // //         printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
// // //         return 1;
// // //     }

// // //     ImVector<const char*> extensions;
// // //     {
// // //         uint32_t sdl_extensions_count = 0;
// // //         const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
// // //         for (uint32_t n = 0; n < sdl_extensions_count; n++)
// // //             extensions.push_back(sdl_extensions[n]);
// // //     }
// // //     SetupVulkan(extensions);

// // //     // Create Window Surface
// // //     VkSurfaceKHR surface;
// // //     VkResult err;
// // //     if (SDL_Vulkan_CreateSurface(window, g_Instance, g_Allocator, &surface) == 0)
// // //     {
// // //         printf("Failed to create Vulkan surface.\n");
// // //         return 1;
// // //     }

// // //     // Create Framebuffers
// // //     int w, h;
// // //     SDL_GetWindowSize(window, &w, &h);
// // //     ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
// // //     SetupVulkanWindow(wd, surface, w, h);
// // //     SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
// // //     SDL_ShowWindow(window);

// // //     // Setup Dear ImGui context
// // //     IMGUI_CHECKVERSION();
// // //     ImGui::CreateContext();
// // //     ImGuiIO& io = ImGui::GetIO(); (void)io;
// // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
// // //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

// // //     // Setup Dear ImGui style
// // //     ImGui::StyleColorsDark();
// // //     //ImGui::StyleColorsLight();

// // //     // Setup scaling
// // //     ImGuiStyle& style = ImGui::GetStyle();
// // //     style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
// // //     style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

// // //     // Setup Platform/Renderer backends
// // //     ImGui_ImplSDL3_InitForVulkan(window);
// // //     ImGui_ImplVulkan_InitInfo init_info = {};
// // //     //init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
// // //     init_info.Instance = g_Instance;
// // //     init_info.PhysicalDevice = g_PhysicalDevice;
// // //     init_info.Device = g_Device;
// // //     init_info.QueueFamily = g_QueueFamily;
// // //     init_info.Queue = g_Queue;
// // //     init_info.PipelineCache = g_PipelineCache;
// // //     init_info.DescriptorPool = g_DescriptorPool;
// // //     init_info.MinImageCount = g_MinImageCount;
// // //     init_info.ImageCount = wd->ImageCount;
// // //     init_info.Allocator = g_Allocator;
// // //     init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
// // //     init_info.PipelineInfoMain.Subpass = 0;
// // //     init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
// // //     init_info.CheckVkResultFn = check_vk_result;
// // //     ImGui_ImplVulkan_Init(&init_info);

// // //     // Load Fonts
// // //     // - If fonts are not explicitly loaded, Dear ImGui will call AddFontDefault() to select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
// // //     //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
// // //     // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
// // //     // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
// // //     // - Read 'docs/FONTS.md' for more instructions and details.
// // //     // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
// // //     // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
// // //     //style.FontSizeBase = 20.0f;
// // //     //io.Fonts->AddFontDefaultVector();
// // //     //io.Fonts->AddFontDefaultBitmap();
// // //     //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
// // //     //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
// // //     //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
// // //     //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
// // //     //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
// // //     //IM_ASSERT(font != nullptr);

// // //     // Our state
// // //     bool show_demo_window = true;
// // //     bool show_another_window = false;
// // //     ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// // //     // Main loop
// // //     bool done = false;
// // //     while (!done)
// // //     {
// // //         // Poll and handle events (inputs, window resize, etc.)
// // //         // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// // //         // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// // //         // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// // //         // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
// // //         // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
// // //         SDL_Event event;
// // //         while (SDL_PollEvent(&event))
// // //         {
// // //             ImGui_ImplSDL3_ProcessEvent(&event);
// // //             if (event.type == SDL_EVENT_QUIT)
// // //                 done = true;
// // //             if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
// // //                 done = true;
// // //         }

// // //         // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
// // //         if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
// // //         {
// // //             SDL_Delay(10);
// // //             continue;
// // //         }

// // //         // Resize swap chain?
// // //         int fb_width, fb_height;
// // //         SDL_GetWindowSize(window, &fb_width, &fb_height);
// // //         if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
// // //         {
// // //             ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
// // //             ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
// // //             g_MainWindowData.FrameIndex = 0;
// // //             g_SwapChainRebuild = false;
// // //         }

// // //         // Start the Dear ImGui frame
// // //         ImGui_ImplVulkan_NewFrame();
// // //         ImGui_ImplSDL3_NewFrame();
// // //         ImGui::NewFrame();

// // //         // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
// // //         if (show_demo_window)
// // //             ImGui::ShowDemoWindow(&show_demo_window);

// // //         // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
// // //         {
// // //             static float f = 0.0f;
// // //             static int counter = 0;

// // //             ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

// // //             ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
// // //             ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
// // //             ImGui::Checkbox("Another Window", &show_another_window);

// // //             ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
// // //             ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

// // //             if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
// // //                 counter++;
// // //             ImGui::SameLine();
// // //             ImGui::Text("counter = %d", counter);

// // //             ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
// // //             ImGui::End();
// // //         }

// // //         // 3. Show another simple window.
// // //         if (show_another_window)
// // //         {
// // //             ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
// // //             ImGui::Text("Hello from another window!");
// // //             if (ImGui::Button("Close Me"))
// // //                 show_another_window = false;
// // //             ImGui::End();
// // //         }

// // //         // 4. Show a simple window with a label, a button and a integer input box.
// // //         // {
// // //         //     static int int_value = 0;
// // //         //     ImGui::Begin("Input Integer Example");
// // //         //     ImGui::Text("Input an integer value:");
// // //         //     ImGui::InputInt("##inputint", &int_value);
// // //         //     ImGui::SameLine();
// // //         //     if (ImGui::Button("Print Value"))
// // //         //         printf("Input Integer Value: %d\n", int_value);
// // //         //     ImGui::End();
// // //         // }
// // //         {
// // //             static int int_value = 0;

// // //             // --- Début du Style Personnalisé ---
            
// // //             // 1. Arrondir les coins (FrameRounding pour les champs, FramePadding pour l'espace interne)
// // //             ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f); 
// // //             ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 5)); // Plus d'espace interne

// // //             // 2. Couleurs du Slider/Input (Fond du champ)
// // //             ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.2f, 0.2f, 0.3f, 1.0f));
// // //             ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.3f, 0.3f, 0.4f, 1.0f));
// // //             ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.4f, 0.4f, 0.5f, 1.0f));

// // //             // 3. Couleurs du Bouton (Normal, Survolé, Cliqué)
// // //             ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.5f, 0.0f, 1.0f)); // Vert
// // //             ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.7f, 0.0f, 1.0f)); // Vert clair
// // //             ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.0f, 0.3f, 0.0f, 1.0f)); // Vert foncé

// // //             // --- Début de la Fenêtre ---
// // //             ImGui::Begin("Input Integer Example");

// // //             ImGui::Text("Ajustez la valeur (glissez la souris) :");
            
// // //             // CHANGEMENT ICI : DragInt au lieu de InputInt
// // //             // Syntaxe : Label, &valeur, vitesse, min, max, format
// // //             ImGui::SetNextItemWidth(150.0f); // Définir la largeur spécifique de l'élément suivant
// // //             ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%"); 

// // //             ImGui::SameLine();

// // //             // Bouton avec taille personnalisée (120x0 -> 0 signifie hauteur auto basée sur le padding)
// // //             if (ImGui::Button("Print Value", ImVec2(120, 0)))
// // //             {
// // //                 printf("Input Integer Value: %d\n", int_value);
// // //             }

// // //             ImGui::End();

// // //             // --- Nettoyage du Style ---
// // //             // Il est CRUCIAL de "pop" (retirer) le nombre exact de styles/couleurs ajoutés
// // //             ImGui::PopStyleColor(6); // Nous avons poussé 6 couleurs
// // //             ImGui::PopStyleVar(2);   // Nous avons poussé 2 variables
// // //         }

// // //         // Rendering
// // //         ImGui::Render();
// // //         ImDrawData* draw_data = ImGui::GetDrawData();
// // //         const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
// // //         if (!is_minimized)
// // //         {
// // //             wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
// // //             wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
// // //             wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
// // //             wd->ClearValue.color.float32[3] = clear_color.w;
// // //             FrameRender(wd, draw_data);
// // //             FramePresent(wd);
// // //         }
// // //     }

// // //     // Cleanup
// // //     // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
// // //     err = vkDeviceWaitIdle(g_Device);
// // //     check_vk_result(err);
// // //     ImGui_ImplVulkan_Shutdown();
// // //     ImGui_ImplSDL3_Shutdown();
// // //     ImGui::DestroyContext();

// // //     CleanupVulkanWindow(&g_MainWindowData);
// // //     CleanupVulkan();

// // //     SDL_DestroyWindow(window);
// // //     SDL_Quit();

// // //     return 0;
// // // }




// // // Dear ImGui: standalone example application for SDL3 + Vulkan with Design System

// // #include <imgui.h>
// // #include <imgui_impl_sdl3.h>
// // #include <imgui_impl_vulkan.h>
// // // #include "DesignSystem/DesignSystem.h"
// // // #include "DesignSystem/UI/TokenEditor.h"
// // #include <DesignSystem/DesignSystem.h>
// // #include <DesignSystem/UI/TokenEditor.h>
// // // #include "DesignSystem.h"
// // // #include "UI/TokenEditor.h"
// // #include <stdio.h>
// // #include <stdlib.h>
// // #include <SDL3/SDL.h>
// // #include <SDL3/SDL_vulkan.h>

// // #ifdef __EMSCRIPTEN__
// // #include "../libs/emscripten/emscripten_mainloop_stub.h"
// // #endif

// // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// // #define VOLK_IMPLEMENTATION
// // #include <volk.h>
// // #endif

// // #ifdef _DEBUG
// // #define APP_USE_VULKAN_DEBUG_REPORT
// // static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
// // #endif

// // // Vulkan Data
// // static VkAllocationCallbacks*   g_Allocator = nullptr;
// // static VkInstance               g_Instance = VK_NULL_HANDLE;
// // static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
// // static VkDevice                 g_Device = VK_NULL_HANDLE;
// // static uint32_t                 g_QueueFamily = (uint32_t)-1;
// // static VkQueue                  g_Queue = VK_NULL_HANDLE;
// // static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
// // static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

// // static ImGui_ImplVulkanH_Window g_MainWindowData;
// // static uint32_t                 g_MinImageCount = 2;
// // static bool                     g_SwapChainRebuild = false;

// // static void check_vk_result(VkResult err)
// // {
// //     if (err == VK_SUCCESS)
// //         return;
// //     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
// //     if (err < 0)
// //         abort();
// // }

// // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// // static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, 
// //                                                      uint64_t object, size_t location, int32_t messageCode, 
// //                                                      const char* pLayerPrefix, const char* pMessage, void* pUserData)
// // {
// //     (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
// //     fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
// //     return VK_FALSE;
// // }
// // #endif

// // static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
// // {
// //     for (const VkExtensionProperties& p : properties)
// //         if (strcmp(p.extensionName, extension) == 0)
// //             return true;
// //     return false;
// // }

// // static void SetupVulkan(ImVector<const char*> instance_extensions)
// // {
// //     VkResult err;
// // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// //     volkInitialize();
// // #endif

// //     VkInstanceCreateInfo create_info = {};
// //     create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

// //     uint32_t properties_count;
// //     ImVector<VkExtensionProperties> properties;
// //     vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
// //     properties.resize(properties_count);
// //     err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
// //     check_vk_result(err);

// //     if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
// //         instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
// // #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
// //     if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
// //     {
// //         instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
// //         create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
// //     }
// // #endif

// // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// //     const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
// //     create_info.enabledLayerCount = 1;
// //     create_info.ppEnabledLayerNames = layers;
// //     instance_extensions.push_back("VK_EXT_debug_report");
// // #endif

// //     create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
// //     create_info.ppEnabledExtensionNames = instance_extensions.Data;
// //     err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
// //     check_vk_result(err);
// // #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// //     volkLoadInstance(g_Instance);
// // #endif

// // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// //     auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
// //     IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
// //     VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
// //     debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
// //     debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
// //     debug_report_ci.pfnCallback = debug_report;
// //     debug_report_ci.pUserData = nullptr;
// //     err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
// //     check_vk_result(err);
// // #endif

// //     g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
// //     IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);
// //     g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
// //     IM_ASSERT(g_QueueFamily != (uint32_t)-1);

// //     {
// //         ImVector<const char*> device_extensions;
// //         device_extensions.push_back("VK_KHR_swapchain");

// //         uint32_t properties_count;
// //         ImVector<VkExtensionProperties> properties;
// //         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
// //         properties.resize(properties_count);
// //         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
// // #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
// //         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
// //             device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
// // #endif

// //         const float queue_priority[] = { 1.0f };
// //         VkDeviceQueueCreateInfo queue_info[1] = {};
// //         queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
// //         queue_info[0].queueFamilyIndex = g_QueueFamily;
// //         queue_info[0].queueCount = 1;
// //         queue_info[0].pQueuePriorities = queue_priority;
// //         VkDeviceCreateInfo create_info = {};
// //         create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
// //         create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
// //         create_info.pQueueCreateInfos = queue_info;
// //         create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
// //         create_info.ppEnabledExtensionNames = device_extensions.Data;
// //         err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
// //         check_vk_result(err);
// //         vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
// //     }

// //     {
// //         VkDescriptorPoolSize pool_sizes[] =
// //         {
// //             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
// //         };
// //         VkDescriptorPoolCreateInfo pool_info = {};
// //         pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
// //         pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
// //         pool_info.maxSets = 0;
// //         for (VkDescriptorPoolSize& pool_size : pool_sizes)
// //             pool_info.maxSets += pool_size.descriptorCount;
// //         pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
// //         pool_info.pPoolSizes = pool_sizes;
// //         err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
// //         check_vk_result(err);
// //     }
// // }

// // static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
// // {
// //     VkBool32 res;
// //     vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
// //     if (res != VK_TRUE)
// //     {
// //         fprintf(stderr, "Error no WSI support on physical device 0\n");
// //         exit(-1);
// //     }

// //     const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
// //     const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
// //     wd->Surface = surface;
// //     wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

// //     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
// //     wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));

// //     IM_ASSERT(g_MinImageCount >= 2);
// //     ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);
// // }

// // static void CleanupVulkan()
// // {
// //     vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

// // #ifdef APP_USE_VULKAN_DEBUG_REPORT
// //     auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
// //     f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
// // #endif

// //     vkDestroyDevice(g_Device, g_Allocator);
// //     vkDestroyInstance(g_Instance, g_Allocator);
// // }

// // static void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd)
// // {
// //     ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
// //     vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
// // }

// // static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
// // {
// //     VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
// //     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
// //     VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
// //         g_SwapChainRebuild = true;
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR)
// //         return;
// //     if (err != VK_SUBOPTIMAL_KHR)
// //         check_vk_result(err);

// //     ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
// //     {
// //         err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
// //         check_vk_result(err);

// //         err = vkResetFences(g_Device, 1, &fd->Fence);
// //         check_vk_result(err);
// //     }
// //     {
// //         err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
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
// //         info.renderPass = wd->RenderPass;
// //         info.framebuffer = fd->Framebuffer;
// //         info.renderArea.extent.width = wd->Width;
// //         info.renderArea.extent.height = wd->Height;
// //         info.clearValueCount = 1;
// //         info.pClearValues = &wd->ClearValue;
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
// //         err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
// //         check_vk_result(err);
// //     }
// // }

// // static void FramePresent(ImGui_ImplVulkanH_Window* wd)
// // {
// //     if (g_SwapChainRebuild)
// //         return;
// //     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
// //     VkPresentInfoKHR info = {};
// //     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
// //     info.waitSemaphoreCount = 1;
// //     info.pWaitSemaphores = &render_complete_semaphore;
// //     info.swapchainCount = 1;
// //     info.pSwapchains = &wd->Swapchain;
// //     info.pImageIndices = &wd->FrameIndex;
// //     VkResult err = vkQueuePresentKHR(g_Queue, &info);
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
// //         g_SwapChainRebuild = true;
// //     if (err == VK_ERROR_OUT_OF_DATE_KHR)
// //         return;
// //     if (err != VK_SUBOPTIMAL_KHR)
// //         check_vk_result(err);
// //     wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
// // }

// // int main(int, char**)
// // {
// //     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
// //     {
// //         printf("Error: SDL_Init(): %s\n", SDL_GetError());
// //         return 1;
// //     }

// //     float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
// //     SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
// //     SDL_Window* window = SDL_CreateWindow("Design System Demo - ImGui + SDL3 + Vulkan", 
// //                                          (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
// //     if (window == nullptr)
// //     {
// //         printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
// //         return 1;
// //     }

// //     ImVector<const char*> extensions;
// //     {
// //         uint32_t sdl_extensions_count = 0;
// //         const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
// //         for (uint32_t n = 0; n < sdl_extensions_count; n++)
// //             extensions.push_back(sdl_extensions[n]);
// //     }
// //     SetupVulkan(extensions);

// //     VkSurfaceKHR surface;
// //     VkResult err;
// //     if (SDL_Vulkan_CreateSurface(window, g_Instance, g_Allocator, &surface) == 0)
// //     {
// //         printf("Failed to create Vulkan surface.\n");
// //         return 1;
// //     }

// //     int w, h;
// //     SDL_GetWindowSize(window, &w, &h);
// //     ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
// //     SetupVulkanWindow(wd, surface, w, h);
// //     SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
// //     SDL_ShowWindow(window);

// //     // Setup Dear ImGui context
// //     IMGUI_CHECKVERSION();
// //     ImGui::CreateContext();
// //     ImGuiIO& io = ImGui::GetIO(); (void)io;
// //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
// //     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

// //     ImGui::StyleColorsDark();

// //     ImGuiStyle& style = ImGui::GetStyle();
// //     style.ScaleAllSizes(main_scale);
// //     style.FontScaleDpi = main_scale;

// //     // Initialize Design System
// //     auto& designSystem = DesignSystem::DesignSystem::Instance();
// //     designSystem.Initialize();

// //     // Setup Platform/Renderer backends
// //     ImGui_ImplSDL3_InitForVulkan(window);
// //     ImGui_ImplVulkan_InitInfo init_info = {};
// //     init_info.Instance = g_Instance;
// //     init_info.PhysicalDevice = g_PhysicalDevice;
// //     init_info.Device = g_Device;
// //     init_info.QueueFamily = g_QueueFamily;
// //     init_info.Queue = g_Queue;
// //     init_info.PipelineCache = g_PipelineCache;
// //     init_info.DescriptorPool = g_DescriptorPool;
// //     init_info.MinImageCount = g_MinImageCount;
// //     init_info.ImageCount = wd->ImageCount;
// //     init_info.Allocator = g_Allocator;
// //     init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
// //     init_info.PipelineInfoMain.Subpass = 0;
// //     init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
// //     init_info.CheckVkResultFn = check_vk_result;
// //     ImGui_ImplVulkan_Init(&init_info);

// //     // Our state
// //     bool show_token_editor = true;
// //     bool show_demo_window = false;
// //     ImVec4 clear_color = designSystem.GetColor("semantic.color.background");
    
// //     // Token editor
// //     DesignSystem::TokenEditor tokenEditor;

// //     // Main loop
// //     bool done = false;
// //     while (!done)
// //     {
// //         SDL_Event event;
// //         while (SDL_PollEvent(&event))
// //         {
// //             ImGui_ImplSDL3_ProcessEvent(&event);
// //             if (event.type == SDL_EVENT_QUIT)
// //                 done = true;
// //             if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
// //                 done = true;
// //         }

// //         if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
// //         {
// //             SDL_Delay(10);
// //             continue;
// //         }

// //         int fb_width, fb_height;
// //         SDL_GetWindowSize(window, &fb_width, &fb_height);
// //         if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
// //         {
// //             ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
// //             ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
// //             g_MainWindowData.FrameIndex = 0;
// //             g_SwapChainRebuild = false;
// //         }

// //         ImGui_ImplVulkan_NewFrame();
// //         ImGui_ImplSDL3_NewFrame();
// //         ImGui::NewFrame();

// //         // Main menu bar
// //         if (ImGui::BeginMainMenuBar())
// //         {
// //             if (ImGui::BeginMenu("Windows"))
// //             {
// //                 ImGui::MenuItem("Token Editor", nullptr, &show_token_editor);
// //                 ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
// //                 ImGui::EndMenu();
// //             }
// //             ImGui::EndMainMenuBar();
// //         }

// //         // Show token editor
// //         if (show_token_editor)
// //         {
// //             tokenEditor.Render(designSystem.GetCurrentContext(), designSystem.GetOverrideManager());
// //         }

// //         // Show demo window
// //         if (show_demo_window)
// //             ImGui::ShowDemoWindow(&show_demo_window);

// //         // Example custom window with design system
// //         {
// //             ImGui::Begin("Design System Example");
            
// //             ImGui::Text("This window uses the Design System!");
            
// //             // Use design system tokens
// //             designSystem.PushAllStyles();
            
// //             static int int_value = 50;
// //             ImGui::Text("Drag to adjust:");
// //             ImGui::SetNextItemWidth(150.0f);
// //             ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
            
// //             ImGui::SameLine();
// //             if (ImGui::Button("Print Value", ImVec2(120, 0)))
// //             {
// //                 printf("Value: %d\n", int_value);
// //             }
            
// //             designSystem.PopAllStyles();
            
// //             ImGui::End();
// //         }

// //         // Rendering
// //         ImGui::Render();
// //         ImDrawData* draw_data = ImGui::GetDrawData();
// //         const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
// //         if (!is_minimized)
// //         {
// //             // Use design system background color
// //             clear_color = designSystem.GetColor("semantic.color.background");
// //             wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
// //             wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
// //             wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
// //             wd->ClearValue.color.float32[3] = clear_color.w;
// //             FrameRender(wd, draw_data);
// //             FramePresent(wd);
// //         }
// //     }

// //     // Cleanup
// //     err = vkDeviceWaitIdle(g_Device);
// //     check_vk_result(err);
    
// //     designSystem.Shutdown();
    
// //     ImGui_ImplVulkan_Shutdown();
// //     ImGui_ImplSDL3_Shutdown();
// //     ImGui::DestroyContext();

// //     CleanupVulkanWindow(&g_MainWindowData);
// //     CleanupVulkan();

// //     SDL_DestroyWindow(window);
// //     SDL_Quit();

// //     return 0;
// // }
















// #include <imgui.h>
// #include <imgui_impl_sdl3.h>
// #include <imgui_impl_vulkan.h>
// #include <DesignSystem/DesignSystem.h>
// #include <DesignSystem/UI/TokenEditor.h>
// #include "ShortcutManager.h"
// #include "ShortcutEditor.h"
// #include <DesignSystem/UI/SvgManager.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <SDL3/SDL.h>
// #include <SDL3/SDL_vulkan.h>
// #include <iostream>

// #ifdef __EMSCRIPTEN__
// #include "../libs/emscripten/emscripten_mainloop_stub.h"
// #endif

// #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
// #define VOLK_IMPLEMENTATION
// #include <volk.h>
// #endif

// #ifdef _DEBUG
// #define APP_USE_VULKAN_DEBUG_REPORT
// static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
// #endif

// // Vulkan Data
// static VkAllocationCallbacks*   g_Allocator = nullptr;
// static VkInstance               g_Instance = VK_NULL_HANDLE;
// static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
// static VkDevice                 g_Device = VK_NULL_HANDLE;
// static uint32_t                 g_QueueFamily = (uint32_t)-1;
// static VkQueue                  g_Queue = VK_NULL_HANDLE;
// static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
// static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;
// static VkCommandPool g_UploadCommandPool = VK_NULL_HANDLE;

// static ImGui_ImplVulkanH_Window g_MainWindowData;
// static uint32_t                 g_MinImageCount = 2;
// static bool                     g_SwapChainRebuild = false;

// static void check_vk_result(VkResult err)
// {
//     if (err == VK_SUCCESS)
//         return;
//     fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
//     if (err < 0)
//         abort();
// }

// #ifdef APP_USE_VULKAN_DEBUG_REPORT
// static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, 
//                                                      uint64_t object, size_t location, int32_t messageCode, 
//                                                      const char* pLayerPrefix, const char* pMessage, void* pUserData)
// {
//     (void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix;
//     fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
//     return VK_FALSE;
// }
// #endif

// static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension)
// {
//     for (const VkExtensionProperties& p : properties)
//         if (strcmp(p.extensionName, extension) == 0)
//             return true;
//     return false;
// }

// static void SetupVulkan(ImVector<const char*> instance_extensions)
// {
//     VkResult err;
// #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
//     volkInitialize();
// #endif

//     VkInstanceCreateInfo create_info = {};
//     create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

//     uint32_t properties_count;
//     ImVector<VkExtensionProperties> properties;
//     vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
//     properties.resize(properties_count);
//     err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
//     check_vk_result(err);

//     if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
//         instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
// #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
//     if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
//     {
//         instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
//         create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
//     }
// #endif

// #ifdef APP_USE_VULKAN_DEBUG_REPORT
//     const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
//     create_info.enabledLayerCount = 1;
//     create_info.ppEnabledLayerNames = layers;
//     instance_extensions.push_back("VK_EXT_debug_report");
// #endif

//     create_info.enabledExtensionCount = (uint32_t)instance_extensions.Size;
//     create_info.ppEnabledExtensionNames = instance_extensions.Data;
//     err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
//     check_vk_result(err);
// #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
//     volkLoadInstance(g_Instance);
// #endif

// #ifdef APP_USE_VULKAN_DEBUG_REPORT
//     auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
//     IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
//     VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
//     debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
//     debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
//     debug_report_ci.pfnCallback = debug_report;
//     debug_report_ci.pUserData = nullptr;
//     err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
//     check_vk_result(err);
// #endif

//     g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
//     IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);
//     g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
//     IM_ASSERT(g_QueueFamily != (uint32_t)-1);

//     {
//         ImVector<const char*> device_extensions;
//         device_extensions.push_back("VK_KHR_swapchain");

//         uint32_t properties_count;
//         ImVector<VkExtensionProperties> properties;
//         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
//         properties.resize(properties_count);
//         vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);
// #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
//         if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
//             device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
// #endif

//         const float queue_priority[] = { 1.0f };
//         VkDeviceQueueCreateInfo queue_info[1] = {};
//         queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
//         queue_info[0].queueFamilyIndex = g_QueueFamily;
//         queue_info[0].queueCount = 1;
//         queue_info[0].pQueuePriorities = queue_priority;
//         VkDeviceCreateInfo create_info = {};
//         create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
//         create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
//         create_info.pQueueCreateInfos = queue_info;
//         create_info.enabledExtensionCount = (uint32_t)device_extensions.Size;
//         create_info.ppEnabledExtensionNames = device_extensions.Data;
//         err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
//         check_vk_result(err);
//         vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
//         VkCommandPool g_UploadCommandPool = VK_NULL_HANDLE;
//     }

//     {
//         VkDescriptorPoolSize pool_sizes[] =
//         {
//             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
//         };
//         VkDescriptorPoolCreateInfo pool_info = {};
//         pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
//         pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
//         pool_info.maxSets = 0;
//         for (VkDescriptorPoolSize& pool_size : pool_sizes)
//             pool_info.maxSets += pool_size.descriptorCount;
//         pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
//         pool_info.pPoolSizes = pool_sizes;
//         err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
//         check_vk_result(err);
//     }

//     {
//         VkCommandPoolCreateInfo pool_info{};
//         pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
//         pool_info.queueFamilyIndex = g_QueueFamily;
//         pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

//         VkResult err = vkCreateCommandPool(
//             g_Device,
//             &pool_info,
//             g_Allocator,
//             &g_UploadCommandPool
//         );
//         check_vk_result(err);
//     }
// }

// static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
// {
//     VkBool32 res;
//     vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &res);
//     if (res != VK_TRUE)
//     {
//         fprintf(stderr, "Error no WSI support on physical device 0\n");
//         exit(-1);
//     }

//     const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
//     const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
//     wd->Surface = surface;
//     wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat), requestSurfaceColorSpace);

//     VkPresentModeKHR present_modes[] = { VK_PRESENT_MODE_FIFO_KHR };
//     wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &present_modes[0], IM_COUNTOF(present_modes));

//     IM_ASSERT(g_MinImageCount >= 2);
//     ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount, 0);
// }

// static void CleanupVulkan()
// {
//     vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

// #ifdef APP_USE_VULKAN_DEBUG_REPORT
//     auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
//     f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
// #endif

//     vkDestroyDevice(g_Device, g_Allocator);
//     vkDestroyInstance(g_Instance, g_Allocator);
// }

// static void CleanupVulkanWindow(ImGui_ImplVulkanH_Window* wd)
// {
//     ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
//     vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
// }

// static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
// {
//     VkSemaphore image_acquired_semaphore  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
//     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
//     VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
//     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
//         g_SwapChainRebuild = true;
//     if (err == VK_ERROR_OUT_OF_DATE_KHR)
//         return;
//     if (err != VK_SUBOPTIMAL_KHR)
//         check_vk_result(err);

//     ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
//     {
//         err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
//         check_vk_result(err);

//         err = vkResetFences(g_Device, 1, &fd->Fence);
//         check_vk_result(err);
//     }
//     {
//         err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
//         check_vk_result(err);
//         VkCommandBufferBeginInfo info = {};
//         info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//         info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//         err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
//         check_vk_result(err);
//     }
//     {
//         VkRenderPassBeginInfo info = {};
//         info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
//         info.renderPass = wd->RenderPass;
//         info.framebuffer = fd->Framebuffer;
//         info.renderArea.extent.width = wd->Width;
//         info.renderArea.extent.height = wd->Height;
//         info.clearValueCount = 1;
//         info.pClearValues = &wd->ClearValue;
//         vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
//     }

//     ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

//     vkCmdEndRenderPass(fd->CommandBuffer);
//     {
//         VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
//         VkSubmitInfo info = {};
//         info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//         info.waitSemaphoreCount = 1;
//         info.pWaitSemaphores = &image_acquired_semaphore;
//         info.pWaitDstStageMask = &wait_stage;
//         info.commandBufferCount = 1;
//         info.pCommandBuffers = &fd->CommandBuffer;
//         info.signalSemaphoreCount = 1;
//         info.pSignalSemaphores = &render_complete_semaphore;

//         err = vkEndCommandBuffer(fd->CommandBuffer);
//         check_vk_result(err);
//         err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
//         check_vk_result(err);
//     }
// }

// static void FramePresent(ImGui_ImplVulkanH_Window* wd)
// {
//     if (g_SwapChainRebuild)
//         return;
//     VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
//     VkPresentInfoKHR info = {};
//     info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
//     info.waitSemaphoreCount = 1;
//     info.pWaitSemaphores = &render_complete_semaphore;
//     info.swapchainCount = 1;
//     info.pSwapchains = &wd->Swapchain;
//     info.pImageIndices = &wd->FrameIndex;
//     VkResult err = vkQueuePresentKHR(g_Queue, &info);
//     if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
//         g_SwapChainRebuild = true;
//     if (err == VK_ERROR_OUT_OF_DATE_KHR)
//         return;
//     if (err != VK_SUBOPTIMAL_KHR)
//         check_vk_result(err);
//     wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
// }

// // Fonctions de test pour les shortcuts
// void TestAction_NewFile() {
//     std::cout << "[ACTION] New File" << std::endl;
// }

// void TestAction_OpenFile() {
//     std::cout << "[ACTION] Open File" << std::endl;
// }

// void TestAction_SaveFile() {
//     std::cout << "[ACTION] Save File" << std::endl;
// }

// void TestAction_Quit() {
//     std::cout << "[ACTION] Quit" << std::endl;
// }

// void TestAction_Tool1() {
//     std::cout << "[ACTION] Tool 1 (Toolbar)" << std::endl;
// }

// void TestAction_Tool2() {
//     std::cout << "[ACTION] Tool 2 (Toolbar)" << std::endl;
// }

// void TestAction_Zone1() {
//     std::cout << "[ACTION] Test Zone 1 Action" << std::endl;
// }

// void TestAction_Zone2() {
//     std::cout << "[ACTION] Test Zone 2 Action" << std::endl;
// }

// void RegisterTestShortcuts() {
//     auto& sm = DesignSystem::ShortcutManager::Instance();
    
//     // Actions globales
//     sm.RegisterAction(DesignSystem::Action("action.new", "New File", DesignSystem::ShortcutZone::Global, TestAction_NewFile));
//     sm.SetBinding("action.new", {{DesignSystem::KeyCombination(ImGuiKey_N, true, false, false)}});
    
//     sm.RegisterAction(DesignSystem::Action("action.open", "Open File", DesignSystem::ShortcutZone::Global, TestAction_OpenFile));
//     sm.SetBinding("action.open", {{DesignSystem::KeyCombination(ImGuiKey_O, true, false, false)}});
    
//     sm.RegisterAction(DesignSystem::Action("action.save", "Save File", DesignSystem::ShortcutZone::Global, TestAction_SaveFile));
//     sm.SetBinding("action.save", {{DesignSystem::KeyCombination(ImGuiKey_S, true, false, false)}});
    
//     sm.RegisterAction(DesignSystem::Action("action.quit", "Quit", DesignSystem::ShortcutZone::Global, TestAction_Quit));
//     sm.SetBinding("action.quit", {{DesignSystem::KeyCombination(ImGuiKey_Q, true, false, false)}});
    
//     // Actions toolbar
//     sm.RegisterAction(DesignSystem::Action("tool.1", "Tool 1", DesignSystem::ShortcutZone::Toolbar, TestAction_Tool1));
//     sm.SetBinding("tool.1", {{DesignSystem::KeyCombination(ImGuiKey_1, true, false, false)}});
    
//     sm.RegisterAction(DesignSystem::Action("tool.2", "Tool 2", DesignSystem::ShortcutZone::Toolbar, TestAction_Tool2));
//     sm.SetBinding("tool.2", {{DesignSystem::KeyCombination(ImGuiKey_2, true, false, false)}});
    
//     // Actions zones de test
//     sm.RegisterAction(DesignSystem::Action("zone1.action", "Zone 1 Action", DesignSystem::ShortcutZone::TestZone1, TestAction_Zone1, true));
//     sm.SetBinding("zone1.action", {{DesignSystem::KeyCombination(ImGuiKey_A, true, false, false)}});
    
//     sm.RegisterAction(DesignSystem::Action("zone2.action", "Zone 2 Action", DesignSystem::ShortcutZone::TestZone2, TestAction_Zone2, true));
//     sm.SetBinding("zone2.action", {{DesignSystem::KeyCombination(ImGuiKey_A, true, false, false)}}); // Même shortcut, zone différente
// }

// static SDL_Surface* CreateSurfaceFromSvg(const char* filepath, int size)
// {
//     NSVGimage* svg = nsvgParseFromFile(filepath, "px", 96.0f);
//     if (!svg) return nullptr;

//     NSVGrasterizer* rast = nsvgCreateRasterizer();

//     float scale = size / std::max(svg->width, svg->height);
//     int w = (int)(svg->width * scale);
//     int h = (int)(svg->height * scale);

//     unsigned char* pixels = (unsigned char*)malloc(w * h * 4);
//     memset(pixels, 0, w * h * 4);

//     nsvgRasterize(rast, svg, 0, 0, scale, pixels, w, h, w * 4);

//     SDL_Surface* surface = SDL_CreateSurfaceFrom(
//         w, h,
//         SDL_PIXELFORMAT_RGBA32,
//         pixels,
//         w * 4
//     );

//     nsvgDeleteRasterizer(rast);
//     nsvgDelete(svg);

//     return surface; // SDL free + pixels free lors de DestroySurface
// }


// int main(int, char**)
// {
//     if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
//     {
//         printf("Error: SDL_Init(): %s\n", SDL_GetError());
//         return 1;
//     }

//     float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
//     SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
//     SDL_Window* window = SDL_CreateWindow(
//         "Carto - Design System Demo", 
//         (int)(1280 * main_scale), (int)(800 * main_scale), window_flags
//     );
//     if (window == nullptr)
//     {
//         printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
//         return 1;
//     }

//     SDL_Surface* iconSurface = CreateSurfaceFromSvg(
//         "resources/icons/logo_carto.svg",
//         64
//     );
//     if (iconSurface)
//     {
//         SDL_SetWindowIcon(window, iconSurface);
//         SDL_DestroySurface(iconSurface);
//     }


//     ImVector<const char*> extensions;
//     {
//         uint32_t sdl_extensions_count = 0;
//         const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
//         for (uint32_t n = 0; n < sdl_extensions_count; n++)
//             extensions.push_back(sdl_extensions[n]);
//     }
//     SetupVulkan(extensions);

//     VkSurfaceKHR surface;
//     VkResult err;
//     if (SDL_Vulkan_CreateSurface(window, g_Instance, g_Allocator, &surface) == 0)
//     {
//         printf("Failed to create Vulkan surface.\n");
//         return 1;
//     }

//     int w, h;
//     SDL_GetWindowSize(window, &w, &h);
//     ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
//     SetupVulkanWindow(wd, surface, w, h);
//     SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
//     SDL_ShowWindow(window);

//     // Setup Dear ImGui context
//     IMGUI_CHECKVERSION();
//     ImGui::CreateContext();
//     ImGuiIO& io = ImGui::GetIO(); (void)io;
//     io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
//     io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
//     io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

//     ImGui::StyleColorsDark();

//     ImGuiStyle& style = ImGui::GetStyle();
//     style.ScaleAllSizes(main_scale);
//     style.FontScaleDpi = main_scale;

//     // Initialize Design System
//     auto& designSystem = DesignSystem::DesignSystem::Instance();
//     designSystem.Initialize();

//     // Register shortcuts
//     RegisterTestShortcuts();

//     // Setup Platform/Renderer backends
//     ImGui_ImplSDL3_InitForVulkan(window);
//     ImGui_ImplVulkan_InitInfo init_info = {};
//     init_info.Instance = g_Instance;
//     init_info.PhysicalDevice = g_PhysicalDevice;
//     init_info.Device = g_Device;
//     init_info.QueueFamily = g_QueueFamily;
//     init_info.Queue = g_Queue;
//     init_info.PipelineCache = g_PipelineCache;
//     init_info.DescriptorPool = g_DescriptorPool;
//     init_info.MinImageCount = g_MinImageCount;
//     init_info.ImageCount = wd->ImageCount;
//     init_info.Allocator = g_Allocator;
//     init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
//     init_info.PipelineInfoMain.Subpass = 0;
//     init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
//     init_info.CheckVkResultFn = check_vk_result;
//     ImGui_ImplVulkan_Init(&init_info);

//     auto& svgMgr = UI::SvgManager::Instance();
//     svgMgr.Initialize(
//         g_Device,
//         g_PhysicalDevice,
//         g_Queue,
//         g_UploadCommandPool,
//         g_DescriptorPool
//     );

//     // --- Load SVG icons ---
//     svgMgr.LoadSvg("logo",       "resources/icons/logo_carto.svg");
//     svgMgr.LoadSvg("settings",   "resources/icons/settings.svg");

//     svgMgr.LoadSvg("tool.new",   "resources/icons/actions/new.svg");
//     svgMgr.LoadSvg("tool.open",  "resources/icons/actions/open.svg");
//     svgMgr.LoadSvg("tool.save",  "resources/icons/actions/save.svg");

//     svgMgr.LoadSvg("tool1",      "resources/icons/tools/tool1.svg");
//     svgMgr.LoadSvg("tool2",      "resources/icons/tools/tool2.svg");



//     // State
//     bool show_token_editor = false;
//     bool show_shortcut_editor = false;
//     bool show_demo_window = false;
//     ImVec4 clear_color = designSystem.GetColor("semantic.color.background");
    
//     DesignSystem::TokenEditor tokenEditor;
//     DesignSystem::ShortcutEditor shortcutEditor;
    
//     DesignSystem::ShortcutZone currentZone = DesignSystem::ShortcutZone::Global;

//     // Main loop
//     bool done = false;
//     while (!done)
//     {
//         SDL_Event event;
//         while (SDL_PollEvent(&event))
//         {
//             ImGui_ImplSDL3_ProcessEvent(&event);
//             if (event.type == SDL_EVENT_QUIT)
//                 done = true;
//             if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
//                 done = true;
//         }

//         if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
//         {
//             SDL_Delay(10);
//             continue;
//         }

//         int fb_width, fb_height;
//         SDL_GetWindowSize(window, &fb_width, &fb_height);
//         if (fb_width > 0 && fb_height > 0 && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height))
//         {
//             ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
//             ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, fb_width, fb_height, g_MinImageCount, 0);
//             g_MainWindowData.FrameIndex = 0;
//             g_SwapChainRebuild = false;
//         }

//         ImGui_ImplVulkan_NewFrame();
//         ImGui_ImplSDL3_NewFrame();
//         ImGui::NewFrame();

//         // Process shortcuts
//         DesignSystem::ShortcutManager::Instance().ProcessInput(currentZone);

//         // DockSpace
//         ImGuiViewport* viewport = ImGui::GetMainViewport();
//         ImGui::SetNextWindowPos(viewport->WorkPos);
//         ImGui::SetNextWindowSize(viewport->WorkSize);
//         ImGui::SetNextWindowViewport(viewport->ID);
        
//         ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
//         window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
//         window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
//         window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

//         ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
//         ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
//         ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
//         ImGui::Begin("DockSpace", nullptr, window_flags);
//         ImGui::PopStyleVar(3);

//         ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
//         ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

//         // Menu bar
//         if (ImGui::BeginMenuBar())
//         {
//             if (ImGui::BeginMenu("File"))
//             {
//                 auto& sm = DesignSystem::ShortcutManager::Instance();
                
//                 std::string newShortcut = sm.GetShortcutString("action.new");
//                 if (ImGui::MenuItem("New", newShortcut.c_str())) TestAction_NewFile();
                
//                 std::string openShortcut = sm.GetShortcutString("action.open");
//                 if (ImGui::MenuItem("Open", openShortcut.c_str())) TestAction_OpenFile();
                
//                 std::string saveShortcut = sm.GetShortcutString("action.save");
//                 if (ImGui::MenuItem("Save", saveShortcut.c_str())) TestAction_SaveFile();
                
//                 ImGui::Separator();
                
//                 std::string quitShortcut = sm.GetShortcutString("action.quit");
//                 if (ImGui::MenuItem("Quit", quitShortcut.c_str())) TestAction_Quit();
                
//                 ImGui::EndMenu();
//             }
            
//             if (ImGui::BeginMenu("Edit"))
//             {
//                 if (ImGui::MenuItem("Token Editor", nullptr, show_token_editor)) {
//                     show_token_editor = !show_token_editor;
//                 }
//                 if (ImGui::MenuItem("Shortcut Editor", nullptr, show_shortcut_editor)) {
//                     show_shortcut_editor = !show_shortcut_editor;
//                 }
//                 ImGui::EndMenu();
//             }
            
//             if (ImGui::BeginMenu("Windows"))
//             {
//                 ImGui::MenuItem("ImGui Demo", nullptr, &show_demo_window);
//                 ImGui::EndMenu();
//             }
            
//             ImGui::EndMenuBar();
//         }

//         ImGui::End();

//         // Toolbar (gauche)
//         // ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
//         // currentZone = DesignSystem::ShortcutZone::Toolbar;
        
//         // ImGui::Text("Tools");
//         // ImGui::Separator();
        
//         // if (ImGui::Button("Tool 1", ImVec2(-1, 40))) TestAction_Tool1();
//         // if (ImGui::Button("Tool 2", ImVec2(-1, 40))) TestAction_Tool2();
//         // if (ImGui::Button("Tool 3", ImVec2(-1, 40))) {}
//         // // Cas 1: Icône avec couleurs originales
//         // svgMgr.RenderSvg("logo", 32.0f);

//         // // Cas 2: Icône monochrome avec couleur paramétrable
//         // ImVec4 iconColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
//         // svgMgr.RenderSvg("settings", 24.0f, &iconColor);
        
//         // ImGui::End();
//         ImGui::Begin("Toolbar", nullptr,
//         ImGuiWindowFlags_NoCollapse |
//         ImGuiWindowFlags_NoTitleBar |
//         ImGuiWindowFlags_NoResize);

//         currentZone = DesignSystem::ShortcutZone::Toolbar;

//         ImGui::Text("Tools");
//         ImGui::Separator();

//         const float iconSize = 20.0f;
//         const float buttonHeight = 36.0f;

//         // ---------- Tool 1 ----------
//         if (ImGui::BeginChild("##tool1", ImVec2(0, buttonHeight), false))
//         {
//             svgMgr.RenderSvg("tool1", iconSize);
//             ImGui::SameLine();
//             ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
//             if (ImGui::Selectable("Tool 1", false))
//                 TestAction_Tool1();
//         }
//         ImGui::EndChild();

//         // ---------- Tool 2 ----------
//         if (ImGui::BeginChild("##tool2", ImVec2(0, buttonHeight), false))
//         {
//             svgMgr.RenderSvg("tool2", iconSize);
//             ImGui::SameLine();
//             ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
//             if (ImGui::Selectable("Tool 2", false))
//                 TestAction_Tool2();
//         }
//         ImGui::EndChild();

//         // ---------- Tool 3 ----------
//         if (ImGui::BeginChild("##tool3", ImVec2(0, buttonHeight), false))
//         {
//             ImGui::Dummy(ImVec2(iconSize, iconSize)); // placeholder
//             ImGui::SameLine();
//             ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
//             ImGui::Selectable("Tool 3", false);
//         }
//         ImGui::EndChild();

//         ImGui::Separator();

//         // ---------- Bottom icons ----------
//         svgMgr.RenderSvg("logo", 32.0f);

//         ImVec4 white = ImVec4(1,1,1,1);
//         svgMgr.RenderSvg("settings", 24.0f, &white);

//         ImGui::End();



        

//         // Design System Example
//         ImGui::Begin("Design System Example", nullptr, ImGuiWindowFlags_NoCollapse);
//         currentZone = DesignSystem::ShortcutZone::DesignExample;
        
//         ImGui::Text("This window uses the Design System!");
        
//         designSystem.PushAllStyles();
        
//         static int int_value = 50;
//         ImGui::Text("Drag to adjust:");
//         ImGui::SetNextItemWidth(150.0f);
//         ImGui::DragInt("##dragint", &int_value, 1.0f, 0, 100, "%d %%");
        
//         ImGui::SameLine();
//         if (ImGui::Button("Print Value", ImVec2(120, 0)))
//         {
//             printf("Value: %d\n", int_value);
//         }
        
//         designSystem.PopAllStyles();
        
//         ImGui::End();

//         // Theme Preview
//         ImGui::Begin("Theme Preview", nullptr, ImGuiWindowFlags_NoCollapse);
//         currentZone = DesignSystem::ShortcutZone::ThemePreview;
        
//         ImGui::Text("Preview with current context");
//         ImGui::Separator();
        
//         try {
//             ImVec4 bgColor = designSystem.GetColor("semantic.color.background");
//             ImVec4 primaryColor = designSystem.GetColor("semantic.color.primary");
            
//             ImGui::ColorButton("##bg", bgColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
//             ImGui::SameLine();
//             ImGui::Text("Background Color");
            
//             ImGui::ColorButton("##primary", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 25));
//             ImGui::SameLine();
//             ImGui::Text("Primary Color");
            
//             ImGui::Separator();
//             ImGui::Text("Sample Components:");
            
//             if (ImGui::Button("Sample Button")) {}
            
//             static char textBuffer[128] = "Sample Input";
//             ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));
            
//         } catch (const std::exception& e) {
//             ImGui::Text("Error resolving tokens: %s", e.what());
//         }
        
//         ImGui::End();

//         // Test Zone 1
//         ImGui::Begin("Test Zone 1", nullptr, ImGuiWindowFlags_NoCollapse);
//         currentZone = DesignSystem::ShortcutZone::TestZone1;
        
//         ImGui::Text("Test shortcuts in this zone");
//         ImGui::Text("Press Ctrl+A to trigger Zone 1 action");
        
//         if (ImGui::Button("Trigger Zone 1 Action")) {
//             TestAction_Zone1();
//         }
        
//         ImGui::End();

//         // Test Zone 2
//         ImGui::Begin("Test Zone 2", nullptr, ImGuiWindowFlags_NoCollapse);
//         currentZone = DesignSystem::ShortcutZone::TestZone2;
        
//         ImGui::Text("Test shortcuts in this zone");
//         ImGui::Text("Press Ctrl+A to trigger Zone 2 action");
//         ImGui::Text("(same shortcut as Zone 1, but different action)");
        
//         if (ImGui::Button("Trigger Zone 2 Action")) {
//             TestAction_Zone2();
//         }
        
//         ImGui::End();

//         // Floating windows
//         if (show_token_editor) {
//             tokenEditor.Render(designSystem.GetCurrentContext(), designSystem.GetOverrideManager());
//         }

//         if (show_shortcut_editor) {
//             shortcutEditor.Render(&show_shortcut_editor);
//         }

//         if (show_demo_window)
//             ImGui::ShowDemoWindow(&show_demo_window);

//         // Rendering
//         ImGui::Render();
//         ImDrawData* draw_data = ImGui::GetDrawData();
//         const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
//         if (!is_minimized)
//         {
//             clear_color = designSystem.GetColor("semantic.color.background");
//             wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
//             wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
//             wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
//             wd->ClearValue.color.float32[3] = clear_color.w;
//             FrameRender(wd, draw_data);
//             FramePresent(wd);
//         }
//     }

//     // Cleanup
//     err = vkDeviceWaitIdle(g_Device);
//     check_vk_result(err);
    
//     designSystem.Shutdown();

//     svgMgr.Shutdown();

//     vkDestroyCommandPool(g_Device, g_UploadCommandPool, g_Allocator);
    
//     ImGui_ImplVulkan_Shutdown();
//     ImGui_ImplSDL3_Shutdown();
//     ImGui::DestroyContext();

//     CleanupVulkanWindow(&g_MainWindowData);
//     CleanupVulkan();

//     SDL_DestroyWindow(window);
//     SDL_Quit();

//     return 0;
// }




#include "Application/Application.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        App::Application app;
        
        if (!app.Initialize()) {
            std::cerr << "Failed to initialize application" << std::endl;
            return 1;
        }
        
        app.Run();
        app.Shutdown();
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
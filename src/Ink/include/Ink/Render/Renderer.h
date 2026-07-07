#pragma once

#include "Ink/Core/Math.h"
#include "Ink/Render/Stats.h"
#include "Ink/View/View.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — the engine root (docs/Ink/ARCHITECTURE.md §7). Adopts the
//  application's shared Vulkan 1.3 device, owns the RHI device wrapper, the
//  GpuScene, the pipelines and the per-frame loop; hands out one View per
//  Viewport zone.
//
//  Frame protocol (single graphics queue):
//    BeginFrame()                  — before the UI build (fence/slot rotation)
//    AcquireView(key) + View setup — during the UI build, per Viewport zone
//    EndFrame()                    — after the UI build: records every dirty
//                                    view through the render graph and submits
//  The main swapchain pass needs NO semaphore: the graph's final barriers
//  order the canvas writes before any later fragment sampling on the same
//  queue (submission-order scopes) — the app submits ImGui afterwards as
//  usual. Idle views (unchanged signature) are skipped entirely: a static
//  canvas re-presents its cached texture at zero record cost.
// ─────────────────────────────────────────────────────────────────────────────

// How the engine registers a sampled canvas texture with the UI layer without
// depending on it (the app wraps ImGui_ImplVulkan_Add/RemoveTexture; a
// headless run — ink_bench — passes none).
struct TextureHooks {
    void* user = nullptr;
    std::uint64_t (*create)(void* user, VkSampler sampler, VkImageView view,
                            VkImageLayout layout) = nullptr;
    void (*destroy)(void* user, std::uint64_t texture) = nullptr;
};

class Renderer {
public:
    struct InitInfo {
        VkInstance       instance       = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice         device         = VK_NULL_HANDLE;
        VkQueue          queue          = VK_NULL_HANDLE;
        std::uint32_t    queueFamily    = 0;
        std::string      shaderDir;     // "<exe>/shaders/ink"
        TextureHooks     textures;      // optional (headless: leave null)
    };

    Renderer();
    ~Renderer();

    // Requires a device created with the Vulkan 1.3 features (dynamic
    // rendering + synchronization2). Loads shaders, builds the pipelines and
    // uploads the Lot 1 demo scene. False = engine unavailable (the app keeps
    // running; the Viewport shows its placeholder).
    bool Initialize(const InitInfo& info);
    void Shutdown();   // call with the device idle, before it is destroyed

    void BeginFrame();
    void EndFrame();

    // The view for a zone key (the leaf's EditorState address). Created on
    // first use; evicted (targets freed) after going unused for a few frames.
    View* AcquireView(const void* key);

    const Stats& GetStats() const;
    // Demo-scene bounds (doc units) — drives the Viewport's fit-view until the
    // real document lands (Lot 2).
    Rect SceneBounds() const;

private:
    std::unique_ptr<detail::RendererImpl> impl_;
};

} // namespace Ink

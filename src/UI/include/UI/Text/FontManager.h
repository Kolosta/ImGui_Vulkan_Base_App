#pragma once

#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

namespace UI {

// In imgui 1.92+, fonts are rasterized lazily at any size on demand via the
// dynamic atlas (ImGuiBackendFlags_RendererHasTextures). To change size at
// runtime, just modify style.FontSizeBase / style.FontScaleMain / style.FontScaleDpi.
// No atlas rebuild is needed — FontManager only holds the font handles.
class FontManager {
public:
    static FontManager& Instance();

    // dpiScale: SDL_GetDisplayContentScale() — used as the initial atlas size hint.
    void Initialize(float dpiScale = 1.0f);

    // Load a named font. logicalSize is the "hint" size for the initial baked glyphs;
    // the font remains usable at any other size via lazy rasterization.
    bool LoadFont(const std::string& id, const std::string& filepath, float logicalSize = 14.0f);

    // Marks the given font as imgui's io.FontDefault (used at every NewFrame).
    void SetDefaultFont(const std::string& id);

    ImFont* GetFont(const std::string& id) const;
    ImFont* GetDefaultFont() const;

    // Push the named font for the current size (style.FontSizeBase).
    void PushFont(const std::string& id);
    void PopFont();

    // Compatibility no-ops (kept so existing callers still compile).
    void RequestRebuild(float /*logicalBaseSize*/) {}
    void ExecuteRebuildIfNeeded(VkDevice /*device*/) {}

private:
    FontManager() = default;

    std::unordered_map<std::string, ImFont*> fonts_;
    std::string defaultFontId_;
    float       dpiScale_ = 1.0f;
};

} // namespace UI

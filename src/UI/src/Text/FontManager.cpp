#include <UI/FontManager.h>
#include <iostream>

namespace UI {

FontManager& FontManager::Instance() {
    static FontManager instance;
    return instance;
}

void FontManager::Initialize(float dpiScale) {
    dpiScale_ = dpiScale;
    // No fonts added here; caller drives LoadFont() + SetDefaultFont().
}

bool FontManager::LoadFont(const std::string& id, const std::string& filepath, float logicalSize) {
    ImGuiIO& io = ImGui::GetIO();

    // CRITICAL settings for fluid + crisp dynamic scaling in imgui 1.92+:
    //   - PixelSnapH = false: pixel-snapping breaks fractional font sizes (jumpy scaling).
    //     With it off, glyph advance is sub-pixel and scale changes feel continuous.
    //   - OversampleH = 2: anti-aliased horizontal positioning, sharp at any size.
    //   - OversampleV = 1: vertical sub-pixel positioning isn't used in imgui rendering.
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;

    // Hint the initial baked size at physical (DPI-scaled) pixels.
    // Lazy rasterization will create additional baked sizes as needed.
    float hintSize = logicalSize * dpiScale_;
    ImFont* font = io.Fonts->AddFontFromFileTTF(filepath.c_str(), hintSize, &cfg);
    if (!font) {
        std::cerr << "[FontManager] Failed to load font: " << filepath << std::endl;
        return false;
    }

    fonts_[id] = font;
    return true;
}

void FontManager::SetDefaultFont(const std::string& id) {
    auto it = fonts_.find(id);
    if (it == fonts_.end()) return;
    defaultFontId_         = id;
    ImGui::GetIO().FontDefault = it->second;
}

ImFont* FontManager::GetFont(const std::string& id) const {
    auto it = fonts_.find(id);
    return it != fonts_.end() ? it->second : nullptr;
}

ImFont* FontManager::GetDefaultFont() const {
    return GetFont(defaultFontId_);
}

void FontManager::PushFont(const std::string& id) {
    if (ImFont* f = GetFont(id))
        ImGui::PushFont(f, 0.0f);  // 0.0f = keep current FontSizeBase
}

void FontManager::PopFont() {
    ImGui::PopFont();
}

} // namespace UI

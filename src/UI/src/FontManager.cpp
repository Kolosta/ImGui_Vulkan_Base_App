// #include <DesignSystem/UI/FontManager.h>

// namespace UI {

// FontManager& FontManager::Instance() {
//     static FontManager* instance = new FontManager();
//     return *instance;
// }

// void FontManager::Initialize() {
//     ImGuiIO& io = ImGui::GetIO();
//     ImFontConfig config;
//     config.OversampleH = 2;
//     config.OversampleV = 2;
    
//     // Tailles standard: 12, 14, 16, 18, 20
//     float sizes[] = {12.0f, 14.0f, 16.0f, 18.0f, 20.0f};
    
//     struct FontDef {
//         FontStyle style;
//         const char* path;
//     };
    
//     FontDef fontDefs[] = {
//         {FontStyle::Thin, "resources/fonts/Qanelas-Thin.otf"},
//         {FontStyle::Light, "resources/fonts/Qanelas-Light.otf"},
//         {FontStyle::Regular, "resources/fonts/Qanelas-Regular.otf"},
//         {FontStyle::Medium, "resources/fonts/Qanelas-Medium.otf"},
//         {FontStyle::Bold, "resources/fonts/Qanelas-Bold.otf"},
//         {FontStyle::Black, "resources/fonts/Qanelas-Black.otf"},
//     };
    
//     for (const auto& fontDef : fontDefs) {
//         for (float size : sizes) {
//             ImFont* font = io.Fonts->AddFontFromFileTTF(fontDef.path, size, &config);
//             if (font) {
//                 fonts_[{fontDef.style, size}] = font;
//             }
//         }
//     }
    
//     io.Fonts->Build();
// }

// ImFont* FontManager::GetFont(FontStyle style, float size) {
//     FontKey key{style, size};
//     auto it = fonts_.find(key);
//     return (it != fonts_.end()) ? it->second : nullptr;
// }

// void FontManager::PushFont(FontStyle style, float size) {
//     ImFont* font = GetFont(style, size);
//     if (font) {
//         ImGui::PushFont(font);
//     }
// }

// void FontManager::PopFont() {
//     ImGui::PopFont();
// }

// } // namespace UI




#include <UI/FontManager.h>
#include <iostream>

namespace UI {

FontManager& FontManager::Instance() {
    static FontManager instance;
    return instance;
}

void FontManager::Initialize() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Police par défaut ImGui
    fonts_["imgui_default"] = io.Fonts->AddFontDefault();
    defaultFontId_ = "imgui_default";
}

bool FontManager::LoadFont(const std::string& id, const std::string& filepath, float size) {
    ImGuiIO& io = ImGui::GetIO();
    
    ImFont* font = io.Fonts->AddFontFromFileTTF(filepath.c_str(), size);
    if (!font) {
        std::cerr << "Failed to load font: " << filepath << std::endl;
        return false;
    }
    
    fonts_[id] = font;
    return true;
}

void FontManager::SetDefaultFont(const std::string& id) {
    if (fonts_.find(id) != fonts_.end()) {
        defaultFontId_ = id;
    }
}

ImFont* FontManager::GetFont(const std::string& id) const {
    auto it = fonts_.find(id);
    if (it != fonts_.end()) {
        return it->second;
    }
    return nullptr;
}

ImFont* FontManager::GetDefaultFont() const {
    return GetFont(defaultFontId_);
}

void FontManager::PushFont(const std::string& id) {
    ImFont* font = GetFont(id);
    if (font) {
        ImGui::PushFont(font);
    }
}

void FontManager::PopFont() {
    ImGui::PopFont();
}

} // namespace UI
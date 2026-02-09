// // #include <DesignSystem/DesignSystem.h>
// // #include <DesignSystem/Persistence/Serialization.h>
// // #include <DesignSystem/Accessibility/ColorBlindness.h>
// // #include <stdexcept>
// // #include <iostream>

// // namespace DesignSystem {

// // /**
// //  * Use pointer allocation to avoid static deinitialization issues.
// //  */
// // DesignSystem& DesignSystem::Instance() {
// //     static DesignSystem* instance = new DesignSystem();
// //     return *instance;
// // }

// // DesignSystem::DesignSystem() : stylesPushedCount_(0) {}

// // DesignSystem::~DesignSystem() {
// //     Shutdown();
// // }

// // void DesignSystem::Initialize() {
// //     Serialization::Initialize();
// //     TokenRegistry::Instance().InitializeDefaultTokens();
    
// //     if (!LoadState()) {
// //         currentContext_ = Context(ThemeType::Dark, AccessibilityType::None);
// //     }
    
// //     ApplyGlobalStyle();
// // }

// // void DesignSystem::Shutdown() {
// //     SaveState();
// //     Serialization::Shutdown();
// // }

// // /**
// //  * CRITICAL: SetContext saves state and reapplies global style immediately.
// //  */
// // void DesignSystem::SetContext(const Context& context) {
// //     currentContext_ = context;
// //     SaveState();  // Save immediately
// //     ApplyGlobalStyle();
// // }

// // /**
// //  * CRITICAL: Trigger save after override operations.
// //  */
// // void DesignSystem::NotifyOverrideChange() {
// //     SaveState();
// // }

// // ImVec4 DesignSystem::GetColor(const std::string& tokenId, bool applyAccessibility) {
// //     return GetColorValue(tokenId, currentContext_, applyAccessibility);
// // }

// // float DesignSystem::GetFloat(const std::string& tokenId) {
// //     return GetFloatValue(tokenId, currentContext_);
// // }

// // int DesignSystem::GetInt(const std::string& tokenId) {
// //     return GetIntValue(tokenId, currentContext_);
// // }

// // ImVec2 DesignSystem::GetVec2(const std::string& tokenId) {
// //     return GetVec2Value(tokenId, currentContext_);
// // }

// // /**
// //  * Resolve color with proper theme handling.
// //  * Accessibility is applied on top of theme-resolved value.
// //  */
// // ImVec4 DesignSystem::GetColorValue(const std::string& tokenId, const Context& context, 
// //                                   bool applyAccessibility) {
// //     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    
// //     if (value.GetType() != ValueType::Color) {
// //         throw std::runtime_error("Token is not a color: " + tokenId);
// //     }
    
// //     ImVec4 color = value.AsColor();
    
// //     if (applyAccessibility && context.GetAccessibility() != AccessibilityType::None) {
// //         color = ApplyAccessibility(color, context.GetAccessibility());
// //     }
    
// //     return color;
// // }

// // float DesignSystem::GetFloatValue(const std::string& tokenId, const Context& context) {
// //     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
// //     if (value.GetType() != ValueType::Float) {
// //         throw std::runtime_error("Token is not a float: " + tokenId);
// //     }
// //     return value.AsFloat();
// // }

// // int DesignSystem::GetIntValue(const std::string& tokenId, const Context& context) {
// //     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
// //     if (value.GetType() != ValueType::Int) {
// //         throw std::runtime_error("Token is not an int: " + tokenId);
// //     }
// //     return value.AsInt();
// // }

// // ImVec2 DesignSystem::GetVec2Value(const std::string& tokenId, const Context& context) {
// //     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
// //     if (value.GetType() != ValueType::Vec2) {
// //         throw std::runtime_error("Token is not a Vec2: " + tokenId);
// //     }
// //     return value.AsVec2();
// // }

// // /**
// //  * ApplyGlobalStyle is public and called by TokenEditor for immediate UI refresh.
// //  */
// // void DesignSystem::ApplyGlobalStyle() {
// //     ImGuiStyle& style = ImGui::GetStyle();
    
// //     try {
// //         style.FrameRounding = GetFloat("component.frame.radius");
// //         style.WindowRounding = GetFloat("component.frame.radius");
// //         style.FramePadding = GetVec2("component.frame.padding");
        
// //         ImVec4* colors = style.Colors;
// //         colors[ImGuiCol_WindowBg] = GetColor("semantic.color.background");
// //         colors[ImGuiCol_FrameBg] = GetColor("component.frame.background");
// //         colors[ImGuiCol_FrameBgHovered] = GetColor("component.frame.background");
// //         colors[ImGuiCol_FrameBgActive] = GetColor("component.frame.background");
// //         colors[ImGuiCol_Button] = GetColor("component.button.background");
// //         colors[ImGuiCol_ButtonHovered] = GetColor("component.button.background");
// //         colors[ImGuiCol_ButtonActive] = GetColor("component.button.background");
// //         colors[ImGuiCol_Text] = GetColor("semantic.color.text");
// //     } catch (...) {
// //         // Tokens might not exist yet during initialization
// //     }
// // }

// // void DesignSystem::PushAllStyles() {
// //     stylesPushedCount_ = 0;
    
// //     try {
// //         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, GetFloat("component.frame.radius"));
// //         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GetVec2("component.frame.padding"));
// //         stylesPushedCount_ += 2;
        
// //         ImGui::PushStyleColor(ImGuiCol_FrameBg, GetColor("component.frame.background"));
// //         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, GetColor("component.frame.background"));
// //         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, GetColor("component.frame.background"));
// //         ImGui::PushStyleColor(ImGuiCol_Button, GetColor("component.button.background"));
// //         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetColor("component.button.background"));
// //         ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetColor("component.button.background"));
// //         stylesPushedCount_ += 6;
// //     } catch (...) {}
// // }

// // void DesignSystem::PopAllStyles() {
// //     if (stylesPushedCount_ > 0) {
// //         ImGui::PopStyleColor(6);
// //         ImGui::PopStyleVar(2);
// //         stylesPushedCount_ = 0;
// //     }
// // }

// // /**
// //  * CRITICAL: Save state immediately to disk.
// //  */
// // void DesignSystem::SaveState() {
// //     Serialization::SaveState(currentContext_, overrideManager_);
// // }

// // bool DesignSystem::LoadState() {
// //     return Serialization::LoadState(currentContext_, overrideManager_);
// // }

// // /**
// //  * Recursively resolve token value through references.
// //  */
// // TokenValue DesignSystem::ResolveTokenValue(const std::string& tokenId, ThemeType theme) {
// //     auto& registry = TokenRegistry::Instance();
    
// //     // Check for override first
// //     const Override* override = overrideManager_.GetBestOverride(tokenId, theme);
// //     if (override) {
// //         TokenValue value = override->GetValue();
// //         if (value.IsReference()) {
// //             return ResolveTokenValue(value.AsReference(), theme);
// //         }
// //         return value;
// //     }
    
// //     // Get token from registry
// //     auto token = registry.GetToken(tokenId);
// //     if (!token) {
// //         throw std::runtime_error("Token not found: " + tokenId);
// //     }
    
// //     // Check for theme-specific value
// //     Context themeContext(theme, AccessibilityType::None);
// //     const TokenValue* themeValue = token->GetContextValue(themeContext);
// //     if (themeValue) {
// //         if (themeValue->IsReference()) {
// //             return ResolveTokenValue(themeValue->AsReference(), theme);
// //         }
// //         return *themeValue;
// //     }
    
// //     // Use default value
// //     TokenValue defaultValue = token->GetDefaultValue();
// //     if (defaultValue.IsReference()) {
// //         return ResolveTokenValue(defaultValue.AsReference(), theme);
// //     }
    
// //     return defaultValue;
// // }

// // ImVec4 DesignSystem::ApplyAccessibility(const ImVec4& color, AccessibilityType type) {
// //     return ColorBlindness::ApplyColorBlindness(color, type);
// // }

// // } // namespace DesignSystem




// #include <DesignSystem/DesignSystem.h>
// #include <DesignSystem/Persistence/Serialization.h>
// #include <DesignSystem/Accessibility/ColorBlindness.h>
// #include <stdexcept>
// #include <iostream>

// namespace DesignSystem {

// DesignSystem& DesignSystem::Instance() {
//     static DesignSystem* instance = new DesignSystem();
//     return *instance;
// }

// DesignSystem::DesignSystem() : stylesPushedCount_(0) {}

// DesignSystem::~DesignSystem() {
//     Shutdown();
// }

// void DesignSystem::Initialize() {
//     Serialization::Initialize();
//     TokenRegistry::Instance().InitializeDefaultTokens();
    
//     if (!LoadState()) {
//         currentContext_ = Context(ThemeType::Dark, AccessibilityType::None);
//     }
    
//     ApplyGlobalStyle();
// }

// void DesignSystem::Shutdown() {
//     SaveState();
//     Serialization::Shutdown();
// }

// void DesignSystem::SetContext(const Context& context) {
//     currentContext_ = context;
//     SaveState();
//     ApplyGlobalStyle();
// }

// void DesignSystem::NotifyOverrideChange() {
//     SaveState();
// }

// ImVec4 DesignSystem::GetColor(const std::string& tokenId, bool applyAccessibility) {
//     return GetColorValue(tokenId, currentContext_, applyAccessibility);
// }

// float DesignSystem::GetFloat(const std::string& tokenId) {
//     return GetFloatValue(tokenId, currentContext_);
// }

// int DesignSystem::GetInt(const std::string& tokenId) {
//     return GetIntValue(tokenId, currentContext_);
// }

// ImVec2 DesignSystem::GetVec2(const std::string& tokenId) {
//     return GetVec2Value(tokenId, currentContext_);
// }

// ImVec4 DesignSystem::GetColorValue(const std::string& tokenId, const Context& context, 
//                                   bool applyAccessibility) {
//     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    
//     if (value.GetType() != ValueType::Color) {
//         throw std::runtime_error("Token is not a color: " + tokenId);
//     }
    
//     ImVec4 color = value.AsColor();
    
//     if (applyAccessibility && context.GetAccessibility() != AccessibilityType::None) {
//         color = ApplyAccessibility(color, context.GetAccessibility());
//     }
    
//     return color;
// }

// float DesignSystem::GetFloatValue(const std::string& tokenId, const Context& context) {
//     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
//     if (value.GetType() != ValueType::Float) {
//         throw std::runtime_error("Token is not a float: " + tokenId);
//     }
//     return value.AsFloat();
// }

// int DesignSystem::GetIntValue(const std::string& tokenId, const Context& context) {
//     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
//     if (value.GetType() != ValueType::Int) {
//         throw std::runtime_error("Token is not an int: " + tokenId);
//     }
//     return value.AsInt();
// }

// ImVec2 DesignSystem::GetVec2Value(const std::string& tokenId, const Context& context) {
//     TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
//     if (value.GetType() != ValueType::Vec2) {
//         throw std::runtime_error("Token is not a Vec2: " + tokenId);
//     }
//     return value.AsVec2();
// }

// void DesignSystem::ApplyGlobalStyle() {
//     ImGuiStyle& style = ImGui::GetStyle();
//     ImGuiIO& io = ImGui::GetIO();
    
//     try {
//         // Radius (clamped à 12 max via les tokens)
//         style.WindowRounding = GetFloat("component.window.radius");
//         style.ChildRounding = GetFloat("component.child.radius");
//         style.FrameRounding = GetFloat("component.frame.radius");
//         style.PopupRounding = GetFloat("component.popup.radius");
//         style.GrabRounding = GetFloat("component.grab.radius");
        
//         // Padding
//         style.FramePadding = GetVec2("component.frame.padding");
        
//         // Global Alpha
//         float globalAlpha = GetFloat("semantic.alpha.default");
//         style.Alpha = globalAlpha;
        
//         // Font Scale (appliqué via ImGuiIO)
//         // Note: Le changement de font size nécessite de recréer les fonts
//         // Pour l'instant, on utilise juste le scale
//         float fontSize = GetFloat("semantic.fontSize.default");
//         io.FontGlobalScale = fontSize / 14.0f; // 14 = taille de base
        
//         // UI Scale global (appliqué via ScaleAllSizes)
//         float uiScale = GetFloat("semantic.scale.default");
//         static float lastScale = 1.0f;
//         if (uiScale != lastScale) {
//             float scaleFactor = uiScale / lastScale;
//             style.ScaleAllSizes(scaleFactor);
//             lastScale = uiScale;
//         }
        
//         // Colors
//         ImVec4* colors = style.Colors;
//         colors[ImGuiCol_WindowBg] = GetColor("semantic.color.background");
//         colors[ImGuiCol_ChildBg] = GetColor("semantic.color.surface");
//         colors[ImGuiCol_FrameBg] = GetColor("component.frame.background");
//         colors[ImGuiCol_FrameBgHovered] = GetColor("component.frame.background");
//         colors[ImGuiCol_FrameBgActive] = GetColor("component.frame.background");
//         colors[ImGuiCol_Button] = GetColor("component.button.background");
//         colors[ImGuiCol_ButtonHovered] = GetColor("component.button.background");
//         colors[ImGuiCol_ButtonActive] = GetColor("component.button.background");
//         colors[ImGuiCol_Text] = GetColor("semantic.color.text");
        
//     } catch (...) {
//         // Tokens might not exist yet during initialization
//     }
// }

// void DesignSystem::PushAllStyles() {
//     stylesPushedCount_ = 0;
    
//     try {
//         ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, GetFloat("component.frame.radius"));
//         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GetVec2("component.frame.padding"));
//         ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, GetFloat("component.window.radius"));
//         ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, GetFloat("component.child.radius"));
//         ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, GetFloat("component.popup.radius"));
//         ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, GetFloat("component.grab.radius"));
//         stylesPushedCount_ += 6;
        
//         ImGui::PushStyleColor(ImGuiCol_FrameBg, GetColor("component.frame.background"));
//         ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, GetColor("component.frame.background"));
//         ImGui::PushStyleColor(ImGuiCol_FrameBgActive, GetColor("component.frame.background"));
//         ImGui::PushStyleColor(ImGuiCol_Button, GetColor("component.button.background"));
//         ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetColor("component.button.background"));
//         ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetColor("component.button.background"));
//         stylesPushedCount_ += 6;
//     } catch (...) {}
// }

// void DesignSystem::PopAllStyles() {
//     if (stylesPushedCount_ > 0) {
//         ImGui::PopStyleColor(6);
//         ImGui::PopStyleVar(6);
//         stylesPushedCount_ = 0;
//     }
// }

// void DesignSystem::SaveState() {
//     Serialization::SaveState(currentContext_, overrideManager_);
// }

// bool DesignSystem::LoadState() {
//     return Serialization::LoadState(currentContext_, overrideManager_);
// }

// TokenValue DesignSystem::ResolveTokenValue(const std::string& tokenId, ThemeType theme) {
//     auto& registry = TokenRegistry::Instance();
    
//     const Override* override = overrideManager_.GetBestOverride(tokenId, theme);
//     if (override) {
//         TokenValue value = override->GetValue();
//         if (value.IsReference()) {
//             return ResolveTokenValue(value.AsReference(), theme);
//         }
//         return value;
//     }
    
//     auto token = registry.GetToken(tokenId);
//     if (!token) {
//         throw std::runtime_error("Token not found: " + tokenId);
//     }
    
//     Context themeContext(theme, AccessibilityType::None);
//     const TokenValue* themeValue = token->GetContextValue(themeContext);
//     if (themeValue) {
//         if (themeValue->IsReference()) {
//             return ResolveTokenValue(themeValue->AsReference(), theme);
//         }
//         return *themeValue;
//     }
    
//     TokenValue defaultValue = token->GetDefaultValue();
//     if (defaultValue.IsReference()) {
//         return ResolveTokenValue(defaultValue.AsReference(), theme);
//     }
    
//     return defaultValue;
// }

// ImVec4 DesignSystem::ApplyAccessibility(const ImVec4& color, AccessibilityType type) {
//     return ColorBlindness::ApplyColorBlindness(color, type);
// }

// } // namespace DesignSystem




#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Persistence/Serialization.h>
#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <stdexcept>
#include <iostream>

namespace DesignSystem {

DesignSystem& DesignSystem::Instance() {
    static DesignSystem* instance = new DesignSystem();
    return *instance;
}

DesignSystem::DesignSystem() : stylesPushedCount_(0) {}

DesignSystem::~DesignSystem() {
    Shutdown();
}

void DesignSystem::Initialize() {
    Serialization::Initialize();
    TokenRegistry::Instance().InitializeDefaultTokens();
    
    if (!LoadState()) {
        currentContext_ = Context(ThemeType::Dark, AccessibilityType::None);
    }
    
    ApplyGlobalStyle();
}

void DesignSystem::Shutdown() {
    SaveState();
    Serialization::Shutdown();
}

void DesignSystem::SetContext(const Context& context) {
    currentContext_ = context;
    SaveState();
    ApplyGlobalStyle();
}

void DesignSystem::NotifyOverrideChange() {
    SaveState();
}

ImVec4 DesignSystem::GetColor(const std::string& tokenId, bool applyAccessibility) {
    return GetColorValue(tokenId, currentContext_, applyAccessibility);
}

float DesignSystem::GetFloat(const std::string& tokenId) {
    return GetFloatValue(tokenId, currentContext_);
}

int DesignSystem::GetInt(const std::string& tokenId) {
    return GetIntValue(tokenId, currentContext_);
}

ImVec2 DesignSystem::GetVec2(const std::string& tokenId) {
    return GetVec2Value(tokenId, currentContext_);
}

ImVec4 DesignSystem::GetColorValue(const std::string& tokenId, const Context& context, 
                                  bool applyAccessibility) {
    TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    
    if (value.GetType() != ValueType::Color) {
        throw std::runtime_error("Token is not a color: " + tokenId);
    }
    
    ImVec4 color = value.AsColor();
    
    if (applyAccessibility && context.GetAccessibility() != AccessibilityType::None) {
        color = ApplyAccessibility(color, context.GetAccessibility());
    }
    
    return color;
}

float DesignSystem::GetFloatValue(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    if (value.GetType() != ValueType::Float) {
        throw std::runtime_error("Token is not a float: " + tokenId);
    }
    return value.AsFloat();
}

int DesignSystem::GetIntValue(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    if (value.GetType() != ValueType::Int) {
        throw std::runtime_error("Token is not an int: " + tokenId);
    }
    return value.AsInt();
}

ImVec2 DesignSystem::GetVec2Value(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveTokenValue(tokenId, context.GetTheme());
    if (value.GetType() != ValueType::Vec2) {
        throw std::runtime_error("Token is not a Vec2: " + tokenId);
    }
    return value.AsVec2();
}

void DesignSystem::ApplyGlobalStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    // Keep a copy of the original style to allow for proper scaling and resetting if needed.
    static ImGuiStyle baseStyle;
    static bool isBaseStyleCaptured = false;
    if (!isBaseStyleCaptured) {
        baseStyle = style;
        isBaseStyleCaptured = true;
    }
    
    try {
        // UI Scaling (Reset and apply new scale, absolute scaling based on baseStyle)
        // Allways reset to baseStyle before applying new scale to prevent compounding scales
        float uiScale = GetFloat("semantic.scale.default");
        if (uiScale < 0.1f) uiScale = 0.1f; 

        // Copy of base to reinitialize all variables before scaling, ensuring consistent scaling behavior
        style = baseStyle;

        // Apply absolute new scale to all sizes one time
        style.ScaleAllSizes(uiScale);

        // Global Alpha
        style.Alpha = GetFloat("semantic.alpha.default");
        
        // Rounding & Geometry
        style.WindowRounding = GetFloat("component.window.radius");
        style.ChildRounding = GetFloat("component.child.radius");
        style.FrameRounding = GetFloat("component.frame.radius");
        style.PopupRounding = GetFloat("component.popup.radius");
        style.GrabRounding = GetFloat("component.grab.radius");
        
        style.FramePadding = GetVec2("component.frame.padding");
    
        
        // Font Scaling
        // Based on a baseline of 14.0f
        float fontSize = GetFloat("semantic.fontSize.default");
        io.FontGlobalScale = fontSize / 14.0f;
        
        // Colors
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = GetColor("semantic.color.background");
        colors[ImGuiCol_ChildBg] = GetColor("semantic.color.surface");
        colors[ImGuiCol_FrameBg] = GetColor("component.frame.background");
        colors[ImGuiCol_FrameBgHovered] = GetColor("component.frame.background");
        colors[ImGuiCol_FrameBgActive] = GetColor("component.frame.background");
        colors[ImGuiCol_Button] = GetColor("component.button.background");
        colors[ImGuiCol_ButtonHovered] = GetColor("component.button.background");
        colors[ImGuiCol_ButtonActive] = GetColor("component.button.background");
        colors[ImGuiCol_Text] = GetColor("semantic.color.text");
        
    } catch (...) {}
}

void DesignSystem::PushAllStyles() {
    stylesPushedCount_ = 0;
    
    try {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, GetFloat("component.frame.radius"));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GetVec2("component.frame.padding"));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, GetFloat("component.window.radius"));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, GetFloat("component.child.radius"));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, GetFloat("component.popup.radius"));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, GetFloat("component.grab.radius"));
        stylesPushedCount_ += 6;
        
        ImGui::PushStyleColor(ImGuiCol_FrameBg, GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_Button, GetColor("component.button.background"));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GetColor("component.button.background"));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, GetColor("component.button.background"));
        stylesPushedCount_ += 6;
    } catch (...) {}
}

void DesignSystem::PopAllStyles() {
    if (stylesPushedCount_ > 0) {
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(6);
        stylesPushedCount_ = 0;
    }
}

void DesignSystem::SaveState() {
    Serialization::SaveState(currentContext_, overrideManager_);
}

bool DesignSystem::LoadState() {
    return Serialization::LoadState(currentContext_, overrideManager_);
}

TokenValue DesignSystem::ResolveTokenValue(const std::string& tokenId, ThemeType theme) {
    auto& registry = TokenRegistry::Instance();
    
    const Override* override = overrideManager_.GetBestOverride(tokenId, theme);
    if (override) {
        TokenValue value = override->GetValue();
        if (value.IsReference()) {
            return ResolveTokenValue(value.AsReference(), theme);
        }
        return value;
    }
    
    auto token = registry.GetToken(tokenId);
    if (!token) {
        throw std::runtime_error("Token not found: " + tokenId);
    }
    
    Context themeContext(theme, AccessibilityType::None);
    const TokenValue* themeValue = token->GetContextValue(themeContext);
    if (themeValue) {
        if (themeValue->IsReference()) {
            return ResolveTokenValue(themeValue->AsReference(), theme);
        }
        return *themeValue;
    }
    
    TokenValue defaultValue = token->GetDefaultValue();
    if (defaultValue.IsReference()) {
        return ResolveTokenValue(defaultValue.AsReference(), theme);
    }
    
    return defaultValue;
}

ImVec4 DesignSystem::ApplyAccessibility(const ImVec4& color, AccessibilityType type) {
    return ColorBlindness::ApplyColorBlindness(color, type);
}

} // namespace DesignSystem
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

void DesignSystem::Initialize(float dpiScale) {
    dpiScale_ = dpiScale;
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
    ApplyGlobalStyle();
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

// Helper: scale a "thin line" value so it never collapses below 1 physical pixel
// when the user picks a small uiScale. 0 stays 0 (intentional no-border).
static inline float ScaleThinLine(float base, float scale) {
    if (base <= 0.0f) return 0.0f;
    float v = base * scale;
    return v < 1.0f ? 1.0f : v;
}

void DesignSystem::ApplyGlobalStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Capture the DPI-scaled dark theme as the immutable baseline.
    // Captured once after StyleColorsDark() + ScaleAllSizes(dpiScale).
    static ImGuiStyle baseStyle;
    static bool       baseStyleCaptured = false;
    if (!baseStyleCaptured) {
        baseStyle         = style;
        baseStyleCaptured = true;
    }

    try {
        float uiScale = GetFloat("semantic.scale.default");
        if (uiScale < 0.1f) uiScale = 0.1f;

        // Reset to baseline (DPI-baked) and apply UI scale absolutely.
        // Idempotent — calling this every frame produces the same result.
        style = baseStyle;
        style.ScaleAllSizes(uiScale);

        // Clamp thin lines so 1px borders don't vanish at uiScale < 1.0.
        style.WindowBorderSize   = ScaleThinLine(baseStyle.WindowBorderSize,   uiScale);
        style.ChildBorderSize    = ScaleThinLine(baseStyle.ChildBorderSize,    uiScale);
        style.PopupBorderSize    = ScaleThinLine(baseStyle.PopupBorderSize,    uiScale);
        style.FrameBorderSize    = ScaleThinLine(baseStyle.FrameBorderSize,    uiScale);
        style.TabBorderSize      = ScaleThinLine(baseStyle.TabBorderSize,      uiScale);
        style.SeparatorTextBorderSize = ScaleThinLine(baseStyle.SeparatorTextBorderSize, uiScale);

        // ── FONT SIZING (imgui 1.92+ native, lazy rasterisation) ───────────────
        // Final rendered size = FontSizeBase * FontScaleMain * FontScaleDpi.
        // The dynamic atlas creates a fresh baked glyph set at that exact size:
        // no rebuild, no post-raster zoom, always crisp.
        //
        //   FontSizeBase   <- semantic.fontSize.default  (logical px base size)
        //   FontScaleMain  <- semantic.scale.default × semantic.fontScale.default
        //   FontScaleDpi   <- monitor DPI scale
        //
        // semantic.fontScale.default is a font-only multiplier so the user can
        // grow / shrink text WITHOUT changing the rest of the UI density.
        float baseFontSize = 14.0f;
        try { baseFontSize = GetFloat("semantic.fontSize.default"); } catch (...) {}
        if (baseFontSize < 1.0f) baseFontSize = 14.0f;

        float fontScale = 1.0f;
        try { fontScale = GetFloat("semantic.fontScale.default"); } catch (...) {}
        if (fontScale < 0.1f) fontScale = 0.1f;

        // style.FontSizeBase is read into g.FontSizeBase at NewFrame() (imgui.cpp:9637)
        // and used by all default text rendering. style._NextFrameFontSizeBase is
        // imgui's internal "queue for next frame" channel — setting it too forces
        // PushFont() stacks to pick up the change even if we're mid-frame.
        style.FontSizeBase           = baseFontSize;
        style._NextFrameFontSizeBase = baseFontSize;
        style.FontScaleMain          = uiScale * fontScale;
        style.FontScaleDpi           = dpiScale_;

        // Token-driven metrics are in logical pixels; convert to physical with
        // the same effective scale used everywhere else.
        float effectiveScale = uiScale * dpiScale_;

        style.Alpha          = GetFloat("semantic.alpha.default");
        style.WindowRounding = GetFloat("component.window.radius") * effectiveScale;
        style.ChildRounding  = GetFloat("component.child.radius")  * effectiveScale;
        style.FrameRounding  = GetFloat("component.frame.radius")  * effectiveScale;
        style.PopupRounding  = GetFloat("component.popup.radius")  * effectiveScale;
        style.GrabRounding   = GetFloat("component.grab.radius")   * effectiveScale;

        ImVec2 fp          = GetVec2("component.frame.padding");
        style.FramePadding = ImVec2(fp.x * effectiveScale, fp.y * effectiveScale);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg]       = GetColor("semantic.color.background");
        colors[ImGuiCol_ChildBg]        = GetColor("semantic.color.surface");
        colors[ImGuiCol_FrameBg]        = GetColor("component.frame.background");
        colors[ImGuiCol_FrameBgHovered] = GetColor("component.frame.background");
        colors[ImGuiCol_FrameBgActive]  = GetColor("component.frame.background");
        colors[ImGuiCol_Button]         = GetColor("component.button.background");
        colors[ImGuiCol_ButtonHovered]  = GetColor("component.button.background");
        colors[ImGuiCol_ButtonActive]   = GetColor("component.button.background");
        colors[ImGuiCol_Text]           = GetColor("semantic.color.text");

    } catch (...) {
        // Tokens may not exist yet during first initialization pass.
    }
}

float DesignSystem::GetUiScale() const {
    try {
        // Resolve through the public path so overrides are honoured.
        auto& self = const_cast<DesignSystem&>(*this);
        float s = self.GetFloat("semantic.scale.default");
        return s < 0.1f ? 0.1f : s;
    } catch (...) {
        return 1.0f;
    }
}

float DesignSystem::GetGlobalScale() const {
    return GetUiScale() * dpiScale_;
}

void DesignSystem::PushAllStyles() {
    stylesPushedCount_ = 0;

    try {
        float uiScale = 1.0f;
        try { uiScale = GetFloat("semantic.scale.default"); } catch (...) {}
        if (uiScale < 0.1f) uiScale = 0.1f;
        float es = uiScale * dpiScale_;

        ImVec2 fp = GetVec2("component.frame.padding");
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  GetFloat("component.frame.radius")  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(fp.x * es, fp.y * es));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, GetFloat("component.window.radius") * es);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,  GetFloat("component.child.radius")  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  GetFloat("component.popup.radius")  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,   GetFloat("component.grab.radius")   * es);
        stylesPushedCount_ += 6;

        ImGui::PushStyleColor(ImGuiCol_FrameBg,          GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    GetColor("component.frame.background"));
        ImGui::PushStyleColor(ImGuiCol_Button,           GetColor("component.button.background"));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,    GetColor("component.button.background"));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,     GetColor("component.button.background"));
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

std::vector<DesignSystem::ReferenceChainEntry>
DesignSystem::GetReferenceChain(const std::string& tokenId, ThemeType theme) {
    std::vector<ReferenceChainEntry> chain;
    auto& registry = TokenRegistry::Instance();

    // Bound the walk defensively: a reference cycle would otherwise loop here
    // until the stack/CPU give up. 64 hops is well past any sane chain depth
    // (primitive → semantic → component is 3).  Cycle detection proper is
    // tracked in ROADMAP 1.2.7.
    std::string current = tokenId;
    for (int hop = 0; hop < 64; ++hop) {
        ReferenceChainEntry entry;
        entry.tokenId = current;
        entry.found = false;
        entry.overridden = false;

        const Override* override = overrideManager_.GetBestOverride(current, theme);
        if (override) {
            entry.overridden = true;
            entry.value = override->GetValue();
            entry.found = true;
            chain.push_back(entry);
            if (!entry.value.IsReference()) return chain;
            current = entry.value.AsReference();
            continue;
        }

        auto token = registry.GetToken(current);
        if (!token) {
            chain.push_back(entry); // not found
            return chain;
        }
        entry.found = true;

        Context themeContext(theme, AccessibilityType::None);
        const TokenValue* themeValue = token->GetContextValue(themeContext);
        const TokenValue& src = themeValue ? *themeValue : token->GetDefaultValue();
        entry.value = src;
        chain.push_back(entry);

        if (!src.IsReference()) return chain;
        current = src.AsReference();
    }
    return chain;
}

} // namespace DesignSystem












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

// // void DesignSystem::RebuildFonts() {
// //     ImGuiIO& io = ImGui::GetIO();

// //     io.Fonts->Clear();

// //     ImFontConfig cfg;
// //     cfg.OversampleH = 2;
// //     cfg.OversampleV = 2;
// //     cfg.PixelSnapH = true;

// //     io.Fonts->AddFontFromFileTTF(
// //         "ressources/fonts/NotoSans-Regular.ttf",
// //         scaleMetrics_.finalFontSize,
// //         &cfg
// //     );

// //     io.Fonts->Build();

// //     io.FontGlobalScale = 1.0f;
// // }

// // void DesignSystem::ApplyGlobalStyle() {
// //     ImGuiStyle& style = ImGui::GetStyle();
// //     ImGuiIO& io = ImGui::GetIO();

// //     // Keep a copy of the original style to allow for proper scaling and resetting if needed.
// //     static ImGuiStyle baseStyle;
// //     static bool isBaseStyleCaptured = false;
// //     if (!isBaseStyleCaptured) {
// //         baseStyle = style;
// //         isBaseStyleCaptured = true;
// //     }
    
// //     try {
// //         // UI Scaling (Reset and apply new scale, absolute scaling based on baseStyle)
// //         // Allways reset to baseStyle before applying new scale to prevent compounding scales
// //         float uiScale = GetFloat("semantic.scale.default");
// //         if (uiScale < 0.1f) uiScale = 0.1f; 

// //         // Copy of base to reinitialize all variables before scaling, ensuring consistent scaling behavior
// //         style = baseStyle;

// //         // Apply absolute new scale to all sizes one time
// //         style.ScaleAllSizes(uiScale);

// //         // Global Alpha
// //         style.Alpha = GetFloat("semantic.alpha.default");
        
// //         // Rounding & Geometry
// //         style.WindowRounding = GetFloat("component.window.radius");
// //         style.ChildRounding = GetFloat("component.child.radius");
// //         style.FrameRounding = GetFloat("component.frame.radius");
// //         style.PopupRounding = GetFloat("component.popup.radius");
// //         style.GrabRounding = GetFloat("component.grab.radius");
        
// //         style.FramePadding = GetVec2("component.frame.padding");
    
        
// //         // Font Scaling
// //         // Based on a baseline of 14.0f
// //         float fontSize = GetFloat("semantic.fontSize.default");
// //         io.FontGlobalScale = fontSize / 14.0f; //Mauvaise façon, scale après rasterisation
// //         // io.FontGlobalScale = 1.0f; //TODO : Faire une implémentation DPI-aware du ui scale, dpi scale, fontScale
        
// //         // Colors
// //         ImVec4* colors = style.Colors;
// //         colors[ImGuiCol_WindowBg] = GetColor("semantic.color.background");
// //         colors[ImGuiCol_ChildBg] = GetColor("semantic.color.surface");
// //         colors[ImGuiCol_FrameBg] = GetColor("component.frame.background");
// //         colors[ImGuiCol_FrameBgHovered] = GetColor("component.frame.background");
// //         colors[ImGuiCol_FrameBgActive] = GetColor("component.frame.background");
// //         colors[ImGuiCol_Button] = GetColor("component.button.background");
// //         colors[ImGuiCol_ButtonHovered] = GetColor("component.button.background");
// //         colors[ImGuiCol_ButtonActive] = GetColor("component.button.background");
// //         colors[ImGuiCol_Text] = GetColor("semantic.color.text");
        
// //     } catch (...) {}
// // }

// // void DesignSystem::ApplyGlobalStyle() {
// //     ImGuiStyle& style = ImGui::GetStyle();

// //     try {
// //         //
// //         // SCALE COMPUTATION
// //         //

// //         float uiScale = GetFloat("semantic.scale.default");
// //         if (uiScale < 0.1f)
// //             uiScale = 0.1f;

// //         float baseFontSize =
// //             GetFloat("semantic.fontSize.default");

// //         float dpiScale = 1.0f;

// // #ifdef IMGUI_HAS_VIEWPORT
// //         ImGuiViewport* viewport = ImGui::GetMainViewport();
// //         if (viewport)
// //             dpiScale = viewport->DpiScale;
// // #endif

// //         scaleMetrics_.dpiScale = dpiScale;
// //         scaleMetrics_.uiScale = uiScale;
// //         scaleMetrics_.baseFontSize = baseFontSize;

// //         scaleMetrics_.finalFontSize =
// //             baseFontSize *
// //             uiScale *
// //             dpiScale;

// //         scaleMetrics_.em =
// //             scaleMetrics_.finalFontSize;

// //         //
// //         // FONT REBUILD
// //         //

// //         // RebuildFonts();

// //         //
// //         // STYLE RESET
// //         //

// //         style = ImGuiStyle();

// //         //
// //         // GLOBAL
// //         //

// //         style.Alpha =
// //             GetFloat("semantic.alpha.default");

// //         //
// //         // METRICS
// //         //

// //         const float em = scaleMetrics_.em;

// //         //
// //         // ROUNDING
// //         //

// //         style.WindowRounding =
// //             GetFloat("component.window.radius") * em;

// //         style.ChildRounding =
// //             GetFloat("component.child.radius") * em;

// //         style.FrameRounding =
// //             GetFloat("component.frame.radius") * em;

// //         style.PopupRounding =
// //             GetFloat("component.popup.radius") * em;

// //         style.GrabRounding =
// //             GetFloat("component.grab.radius") * em;

// //         //
// //         // PADDING
// //         //

// //         {
// //             ImVec2 pad =
// //                 GetVec2("component.frame.padding");

// //             style.FramePadding =
// //                 ImVec2(
// //                     pad.x * em,
// //                     pad.y * em
// //                 );
// //         }

// //         //
// //         // SPACING
// //         //

// //         style.ItemSpacing =
// //             ImVec2(
// //                 0.5f * em,
// //                 0.35f * em
// //             );

// //         style.ItemInnerSpacing =
// //             ImVec2(
// //                 0.35f * em,
// //                 0.35f * em
// //             );

// //         style.WindowPadding =
// //             ImVec2(
// //                 0.75f * em,
// //                 0.75f * em
// //             );

// //         style.CellPadding =
// //             ImVec2(
// //                 0.4f * em,
// //                 0.3f * em
// //             );

// //         //
// //         // BORDERS
// //         //

// //         style.WindowBorderSize = 1.0f;
// //         style.ChildBorderSize = 1.0f;
// //         style.FrameBorderSize = 1.0f;
// //         style.PopupBorderSize = 1.0f;

// //         //
// //         // SCROLLBAR
// //         //

// //         style.ScrollbarSize =
// //             0.85f * em;

// //         style.ScrollbarRounding =
// //             0.4f * em;

// //         //
// //         // GRAB
// //         //

// //         style.GrabMinSize =
// //             0.75f * em;

// //         //
// //         // TABS
// //         //

// //         style.TabRounding =
// //             0.4f * em;

// //         //
// //         // COLORS
// //         //

// //         ImVec4* colors = style.Colors;

// //         colors[ImGuiCol_WindowBg] =
// //             GetColor("semantic.color.background");

// //         colors[ImGuiCol_ChildBg] =
// //             GetColor("semantic.color.surface");

// //         colors[ImGuiCol_FrameBg] =
// //             GetColor("component.frame.background");

// //         colors[ImGuiCol_FrameBgHovered] =
// //             GetColor("component.frame.background");

// //         colors[ImGuiCol_FrameBgActive] =
// //             GetColor("component.frame.background");

// //         colors[ImGuiCol_Button] =
// //             GetColor("component.button.background");

// //         colors[ImGuiCol_ButtonHovered] =
// //             GetColor("component.button.background");

// //         colors[ImGuiCol_ButtonActive] =
// //             GetColor("component.button.background");

// //         colors[ImGuiCol_Text] =
// //             GetColor("semantic.color.text");

// //     } catch (...) {
// //     }
// // }


// void DesignSystem::ApplyGlobalStyle() {
//     ImGuiStyle& style = ImGui::GetStyle();

//     try {
//         float uiScale = GetFloat("semantic.scale.default");
//         if (uiScale < 0.1f)
//             uiScale = 0.1f;

//         // float baseFontSize = GetFloat("semantic.fontSize.default");
//         float baseFontSize = 1.0f;
        
//         float dpiScale = 1.0f;

// #ifdef IMGUI_HAS_VIEWPORT
//         ImGuiViewport* viewport = ImGui::GetMainViewport();
//         if (viewport)
//             dpiScale = viewport->DpiScale;
// #endif

//         scaleMetrics_.dpiScale = dpiScale;
//         scaleMetrics_.uiScale = uiScale;
//         scaleMetrics_.baseFontSize = baseFontSize;

//         scaleMetrics_.finalFontSize =
//             baseFontSize * uiScale * dpiScale;

//         scaleMetrics_.em = scaleMetrics_.finalFontSize;

//         // ❌ SUPPRIMÉ: RebuildFonts();

//         style = ImGuiStyle();

//         style.Alpha = GetFloat("semantic.alpha.default");

//         const float em = scaleMetrics_.em;

//         style.WindowRounding = GetFloat("component.window.radius") * em;
//         style.ChildRounding  = GetFloat("component.child.radius") * em;
//         style.FrameRounding  = GetFloat("component.frame.radius") * em;
//         style.PopupRounding  = GetFloat("component.popup.radius") * em;
//         style.GrabRounding   = GetFloat("component.grab.radius") * em;

//         ImVec2 pad = GetVec2("component.frame.padding");
//         style.FramePadding = ImVec2(pad.x * em, pad.y * em);

//         style.ItemSpacing = ImVec2(0.5f * em, 0.35f * em);
//         style.WindowPadding = ImVec2(0.75f * em, 0.75f * em);

//         ImVec4* colors = style.Colors;

//         colors[ImGuiCol_WindowBg] = GetColor("semantic.color.background");
//         colors[ImGuiCol_Text]     = GetColor("semantic.color.text");

//     } catch (...) {}
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
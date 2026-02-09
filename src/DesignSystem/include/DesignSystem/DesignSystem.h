#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <imgui.h>
#include <string>

namespace DesignSystem {

/**
 * Main design system facade - singleton.
 * Provides high-level API for token resolution and style management.
 */
class DesignSystem {
public:
    static DesignSystem& Instance();
    
    /**
     * Initialization.
     */
    void Initialize();
    void Shutdown();
    
    /**
     * Context management.
     */
    Context& GetCurrentContext() { return currentContext_; }
    const Context& GetCurrentContext() const { return currentContext_; }
    void SetContext(const Context& context);
    
    /**
     * Notify that an override was changed (triggers save).
     */
    void NotifyOverrideChange();
    
    /**
     * Value resolution (with current context).
     */
    ImVec4 GetColor(const std::string& tokenId, bool applyAccessibility = true);
    float GetFloat(const std::string& tokenId);
    int GetInt(const std::string& tokenId);
    ImVec2 GetVec2(const std::string& tokenId);
    
    /**
     * Value resolution (with specific context).
     */
    ImVec4 GetColorValue(const std::string& tokenId, const Context& context, 
                        bool applyAccessibility = true);
    float GetFloatValue(const std::string& tokenId, const Context& context);
    int GetIntValue(const std::string& tokenId, const Context& context);
    ImVec2 GetVec2Value(const std::string& tokenId, const Context& context);
    
    /**
     * Apply global ImGui style.
     * Called on context change and by TokenEditor to ensure UI updates.
     */
    void ApplyGlobalStyle();
    
    /**
     * Scoped style management.
     */
    void PushAllStyles();
    void PopAllStyles();
    
    /**
     * Access to subsystems.
     */
    TokenRegistry& GetRegistry() { return TokenRegistry::Instance(); }
    OverrideManager& GetOverrideManager() { return overrideManager_; }
    
    /**
     * Persistence.
     */
    void SaveState();
    bool LoadState();
    
    /**
     * Recursively resolve token value (public for TokenEditor).
     */
    TokenValue ResolveTokenValue(const std::string& tokenId, ThemeType theme);
    
private:
    DesignSystem();
    ~DesignSystem();
    DesignSystem(const DesignSystem&) = delete;
    DesignSystem& operator=(const DesignSystem&) = delete;
    
    /**
     * Apply accessibility transformation.
     */
    ImVec4 ApplyAccessibility(const ImVec4& color, AccessibilityType type);
    
    Context currentContext_;
    OverrideManager overrideManager_;
    int stylesPushedCount_;
};

} // namespace DesignSystem
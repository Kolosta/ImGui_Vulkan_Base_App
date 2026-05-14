#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <imgui.h>
#include <string>
#include <vector>

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
     * dpiScale: SDL_GetDisplayContentScale() — used to convert logical to physical pixels.
     */
    void Initialize(float dpiScale = 1.0f);
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

    /**
     * Trace the chain of references for `tokenId` in `theme`, from the token
     * itself down to the first non-reference value (or the first token not
     * found). Each entry is the id of one token visited; the last entry's
     * resolved value is what you'd get from `ResolveTokenValue`.
     *
     * Used by the TokenEditor to display "A → B → C → #1A73E8" trails. Also
     * a future cycle-detection point: today the function bounds itself to 64
     * hops and silently stops to avoid hangs.
     */
    struct ReferenceChainEntry {
        std::string tokenId;       // token visited
        bool found;                // false if this id has no registered token
        bool overridden;           // true if an override (global or theme) supplied the value
        TokenValue value;          // the value held at this step (reference or terminal)
    };
    std::vector<ReferenceChainEntry> GetReferenceChain(const std::string& tokenId,
                                                       ThemeType theme);

    /**
     * Scale accessors — for code that needs to scale raw pixel values
     * (e.g. hardcoded toolbar widths) so they track the UI scale + DPI.
     *   GetUiScale()       = current semantic.scale.default token value
     *   GetGlobalScale()   = uiScale * dpiScale  (full physical-pixel multiplier)
     *   GetDpiScale()      = monitor DPI scale captured at init
     */
    float GetUiScale() const;
    float GetGlobalScale() const;
    float GetDpiScale() const { return dpiScale_; }

private:
    DesignSystem();
    ~DesignSystem();
    DesignSystem(const DesignSystem&) = delete;
    DesignSystem& operator=(const DesignSystem&) = delete;

    ImVec4 ApplyAccessibility(const ImVec4& color, AccessibilityType type);

    Context         currentContext_;
    OverrideManager overrideManager_;
    int             stylesPushedCount_;
    float           dpiScale_ = 1.0f;
};

} // namespace DesignSystem
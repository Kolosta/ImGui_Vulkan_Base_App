#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <string>
#include <vector>
#include <memory>
#include <imgui.h>

namespace DesignSystem {

class Token;
class OverrideManager;

/**
 * Complete token editing UI with all fixes:
 * - Refreshes on theme change
 * - Override editing/removal works correctly
 * - Split color preview (original | accessibility-transformed)
 * - Active override indicator
 * - Default value editing blocked
 * - Type validation for overrides
 * - Visual previews for all value types
 */
class TokenEditor {
public:
    TokenEditor();
    
    /**
     * Main render function.
     */
    void Render(Context& currentContext, OverrideManager& overrideManager);
    
private:
    /**
     * UI panels.
     */
    void RenderContextSelector(Context& currentContext);
    void RenderTokenList();
    void RenderTokenDetails(Context& currentContext, OverrideManager& overrideManager);
    void RenderOverridePanel(Context& currentContext, OverrideManager& overrideManager);
    void RenderPreview(const Context& currentContext);
    
    /**
     * Initialize new override value with correct type.
     */
    void InitializeNewOverrideValue(std::shared_ptr<Token> token);
    
    /**
     * Render actual value for a token (with overrides applied).
     */
    void RenderActualValue(std::shared_ptr<Token> token, const Context& currentContext);
    
    /**
     * Render preview for a resolved reference.
     */
    void RenderResolvedPreview(const std::string& refTokenId, const Context& currentContext);
    
    /**
     * Render value preview with visual representation.
     */
    void RenderValuePreview(const char* label, const TokenValue& value, 
                           const Context& currentContext, bool showLabel);
    
    /**
     * Render unified color preview with accessibility split.
     */
    void RenderColorPreview(const char* label, const ImVec4& color, const Context& currentContext);
    
    /**
     * Render visual preview for float values (radius, spacing, font size, etc.).
     */
    void RenderFloatPreview(const char* label, float value);
    
    /**
     * Render value editor with type enforcement.
     * Returns true if value was modified.
     */
    bool RenderValueEditor(const char* label, TokenValue& value, 
                          std::shared_ptr<Token> token, const Context& currentContext);
    
    /**
     * Validate that override value type matches token's expected type.
     */
    bool ValidateOverrideType(const TokenValue& value, std::shared_ptr<Token> token);
    
    /**
     * Check if token should be filtered based on search.
     */
    bool IsTokenFiltered(std::shared_ptr<Token> token) const;
    
    // UI state
    std::string selectedTokenId_;
    int selectedThemeIndex_;
    int selectedAccessibilityIndex_;
    bool showPrimitives_;
    bool showSemantics_;
    bool showComponents_;
    char searchBuffer_[256];
    
    // For override editing
    TokenValue newOverrideValue_;
    bool addingGlobalOverride_;
};

} // namespace DesignSystem
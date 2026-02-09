#pragma once

#include <DesignSystem/Core/TokenValue.h>
#include <DesignSystem/Core/TokenType.h>
#include <optional>
#include <ostream>
#include <istream>
#include <cstdint>
#include <string>

namespace DesignSystem {

/**
 * Represents an override for a token value.
 * Override context uses THEME ONLY (not accessibility).
 * Accessibility is always applied dynamically on top of resolved values.
 */
class Override {
public:
    Override(const std::string& tokenId, const TokenValue& value);
    Override(const std::string& tokenId, const TokenValue& value, ThemeType theme);
    
    const std::string& GetTokenId() const { return tokenId_; }
    const TokenValue& GetValue() const { return value_; }
    void SetValue(const TokenValue& value) { value_ = value; }
    
    /**
     * Context checking (theme only).
     */
    bool IsGlobal() const { return !theme_.has_value(); }
    bool IsThemeSpecific() const { return theme_.has_value(); }
    const std::optional<ThemeType>& GetTheme() const { return theme_; }
    
    /**
     * Priority: Global (10) > Theme-specific (5).
     */
    int GetPriority() const { return IsGlobal() ? 10 : 5; }
    
    /**
     * Check if override applies to given theme.
     * Accessibility is NOT considered - it's applied separately.
     */
    bool AppliesTo(ThemeType theme) const;
    
    /**
     * Binary serialization.
     */
    void WriteToBinary(std::ostream& out) const;
    static Override ReadFromBinary(std::istream& in);
    
private:
    std::string tokenId_;
    TokenValue value_;
    std::optional<ThemeType> theme_;  // Optional theme context (nullopt = global)
};

} // namespace DesignSystem
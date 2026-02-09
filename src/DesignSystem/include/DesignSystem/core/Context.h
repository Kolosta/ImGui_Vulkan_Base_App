#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <string>
#include <ostream>
#include <istream>
#include <cstddef>
#include <cstdint>

namespace DesignSystem {

/**
 * Represents the current UI context: Theme + Accessibility.
 * Theme determines which token values are used.
 * Accessibility is applied as color transformation on top.
 */
class Context {
public:
    Context();
    Context(ThemeType theme, AccessibilityType accessibility);
    
    ThemeType GetTheme() const { return theme_; }
    AccessibilityType GetAccessibility() const { return accessibility_; }
    
    void SetTheme(ThemeType theme) { theme_ = theme; }
    void SetAccessibility(AccessibilityType accessibility) { accessibility_ = accessibility; }
    
    bool operator==(const Context& other) const;
    bool operator!=(const Context& other) const;
    
    /**
     * Hash support for use in maps.
     */
    struct Hash {
        std::size_t operator()(const Context& ctx) const;
    };
    
    /**
     * Binary serialization using std::ostream/istream for flexibility.
     */
    void WriteToBinary(std::ostream& out) const;
    static Context ReadFromBinary(std::istream& in);
    
private:
    ThemeType theme_;
    AccessibilityType accessibility_;
};

} // namespace DesignSystem

namespace std {
    template<> struct hash<DesignSystem::Context> {
        std::size_t operator()(const DesignSystem::Context& ctx) const {
            return DesignSystem::Context::Hash()(ctx);
        }
    };
}
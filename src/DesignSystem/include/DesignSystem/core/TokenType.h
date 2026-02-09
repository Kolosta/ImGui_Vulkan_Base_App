#pragma once

#include <string>

namespace DesignSystem {

/**
 * Defines the hierarchy level of a token in the design system.
 * Primitive tokens are base values, Semantic tokens reference primitives,
 * and Component tokens reference semantic tokens.
 */
enum class TokenLevel {
    Primitive,  
    Semantic,   
    Component   
};

/**
 * Defines the type of value stored in a token.
 */
enum class ValueType {
    Color,      // ImVec4 / RGBA color
    Float,      // Single float value
    Int,        // Integer value
    Vec2,       // 2D vector (ImVec2)
    Reference   // Reference to another token
};

/**
 * Theme context types. Each theme can define different values for tokens.
 * Accessibility is NOT part of theme - it's applied as transformation on top.
 */
enum class ThemeType {
    Dark,          
    Light,          
    MutedGreen,     
    HighContrast    
};

/**
 * Accessibility modes for color vision deficiency simulation.
 * Applied as transformation matrices on top of resolved colors.
 * NOT part of override context - always calculated dynamically.
 */
enum class AccessibilityType {
    None,           
    Protanopia,     // Red-blind
    Deuteranopia,   // Green-blind
    Tritanopia      // Blue-blind
};

// Conversion functions

inline std::string TokenLevelToString(TokenLevel level) {
    switch (level) {
        case TokenLevel::Primitive: return "Primitive";
        case TokenLevel::Semantic: return "Semantic";
        case TokenLevel::Component: return "Component";
        default: return "Unknown";
    }
}

inline TokenLevel StringToTokenLevel(const std::string& str) {
    if (str == "Primitive") return TokenLevel::Primitive;
    if (str == "Semantic") return TokenLevel::Semantic;
    if (str == "Component") return TokenLevel::Component;
    return TokenLevel::Primitive;
}

inline std::string ValueTypeToString(ValueType type) {
    switch (type) {
        case ValueType::Color: return "Color";
        case ValueType::Float: return "Float";
        case ValueType::Int: return "Int";
        case ValueType::Vec2: return "Vec2";
        case ValueType::Reference: return "Reference";
        default: return "Unknown";
    }
}

inline ValueType StringToValueType(const std::string& str) {
    if (str == "Color") return ValueType::Color;
    if (str == "Float") return ValueType::Float;
    if (str == "Int") return ValueType::Int;
    if (str == "Vec2") return ValueType::Vec2;
    if (str == "Reference") return ValueType::Reference;
    return ValueType::Float;
}

inline std::string ThemeTypeToString(ThemeType theme) {
    switch (theme) {
        case ThemeType::Dark: return "Dark";
        case ThemeType::Light: return "Light";
        case ThemeType::MutedGreen: return "MutedGreen";
        case ThemeType::HighContrast: return "HighContrast";
        default: return "Dark";
    }
}

inline ThemeType StringToThemeType(const std::string& str) {
    if (str == "Dark") return ThemeType::Dark;
    if (str == "Light") return ThemeType::Light;
    if (str == "MutedGreen") return ThemeType::MutedGreen;
    if (str == "HighContrast") return ThemeType::HighContrast;
    return ThemeType::Dark;
}

inline std::string AccessibilityTypeToString(AccessibilityType type) {
    switch (type) {
        case AccessibilityType::None: return "None";
        case AccessibilityType::Protanopia: return "Protanopia";
        case AccessibilityType::Deuteranopia: return "Deuteranopia";
        case AccessibilityType::Tritanopia: return "Tritanopia";
        default: return "None";
    }
}

inline AccessibilityType StringToAccessibilityType(const std::string& str) {
    if (str == "None") return AccessibilityType::None;
    if (str == "Protanopia") return AccessibilityType::Protanopia;
    if (str == "Deuteranopia") return AccessibilityType::Deuteranopia;
    if (str == "Tritanopia") return AccessibilityType::Tritanopia;
    return AccessibilityType::None;
}

} // namespace DesignSystem
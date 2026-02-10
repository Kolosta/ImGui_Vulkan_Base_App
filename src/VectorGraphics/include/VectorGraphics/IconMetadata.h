#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace VectorGraphics {

/**
 * Icon color scheme type
 */
enum class IconColorScheme {
    Original,     // Original SVG colors (elements without CSS classes)
    Bicolor,      // 2-3 colors (primary/secondary/tertiary from design system)
    Multicolor    // Custom palette per color zone
};

/**
 * Represents a unique color in the SVG
 */
struct ColorZone {
    uint32_t originalColor;    // Original color in RGBA format
    ImVec4 customColor;        // Custom color for multicolor mode
    std::string tokenName;     // Token name for bicolor mode ("primary", "secondary", "tertiary")
    bool hasToken;             // Whether this zone uses a design token
    
    ColorZone() 
        : originalColor(0)
        , customColor(1, 1, 1, 1)
        , tokenName("")
        , hasToken(false) 
    {}
    
    ColorZone(uint32_t orig, const std::string& token = "") 
        : originalColor(orig)
        , tokenName(token)
        , hasToken(!token.empty())
    {
        // Convert RGBA to ImVec4
        customColor.x = ((orig) & 0xFF) / 255.0f;        // R
        customColor.y = ((orig >> 8) & 0xFF) / 255.0f;   // G
        customColor.z = ((orig >> 16) & 0xFF) / 255.0f;  // B
        customColor.w = ((orig >> 24) & 0xFF) / 255.0f;  // A
    }
};

/**
 * Icon metadata - can be global (template) or local (instance-specific)
 */
struct IconMetadata {
    IconColorScheme scheme = IconColorScheme::Bicolor;
    std::vector<ColorZone> colorZones;  // Detected color zones
    
    // Copy constructor for local instances
    IconMetadata() = default;
    IconMetadata(const IconMetadata& other) = default;
    IconMetadata& operator=(const IconMetadata& other) = default;
};

// Forward declare CompiledIconData (defined in generated IconData.h)
struct ClassMapping;
struct CompiledIconData;

} // namespace VectorGraphics
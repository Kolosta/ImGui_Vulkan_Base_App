#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstdint>

namespace VectorGraphics {

/**
 * Icon color scheme type
 */
enum class IconColorScheme {
    Original,     // Use original SVG colors
    Bicolor,      // Use primary/secondary tokens from design system
    Multicolor    // Use custom colors per zone
};

/**
 * Represents a single color occurrence in the SVG
 * Each element+property combination is tracked separately
 */
struct ColorMapping {
    std::string elementId;      // Element ID or generated identifier
    std::string property;       // "fill", "stroke", "stop-color"
    uint32_t originalColor;     // Original RGBA color
    std::string cssClass;       // CSS class if present (e.g., "ds-primary")
    
    ColorMapping() 
        : originalColor(0) 
    {}
    
    ColorMapping(const std::string& elemId, const std::string& prop, 
                uint32_t color, const std::string& cls = "")
        : elementId(elemId)
        , property(prop)
        , originalColor(color)
        , cssClass(cls)
    {}
};

/**
 * Represents a unique color in the icon
 * Groups all mappings that share the same original color
 */
struct ColorZone {
    uint32_t originalColor;          // The RGBA color this zone represents
    ImVec4 customColor;              // Custom color for multicolor mode
    std::string tokenAssignment;     // "primary" or "secondary" for bicolor mode
    std::vector<int> mappingIndices; // Indices into RuntimeIcon::colorMappings
    
    ColorZone() 
        : originalColor(0)
        , customColor(1, 1, 1, 1)
        , tokenAssignment("primary")
    {}
    
    ColorZone(uint32_t color)
        : originalColor(color)
        , tokenAssignment("primary")
    {
        // Convert RGBA to ImVec4
        customColor.x = ((color) & 0xFF) / 255.0f;        // R
        customColor.y = ((color >> 8) & 0xFF) / 255.0f;   // G
        customColor.z = ((color >> 16) & 0xFF) / 255.0f;  // B
        customColor.w = ((color >> 24) & 0xFF) / 255.0f;  // A
    }
};

/**
 * Icon metadata - per-instance configuration
 */
struct IconMetadata {
    IconColorScheme scheme = IconColorScheme::Bicolor;
    std::vector<ColorZone> colorZones;
    
    IconMetadata() = default;
    IconMetadata(const IconMetadata& other) = default;
    IconMetadata& operator=(const IconMetadata& other) = default;
};

/**
 * Compiled icon data from build-time generation
 */
struct CompiledIconData {
    std::string id;
    const char* svgContent;
    float width;
    float height;
};

} // namespace VectorGraphics
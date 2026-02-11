#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <imgui.h>

namespace VectorGraphics {

// ============================================================================
// COLOR SCHEME ENUMERATION
// ============================================================================

/**
 * Defines how colors are applied to an icon
 */
enum class IconColorScheme {
    Original,    // Use original SVG colors
    Bicolor,     // Use design system tokens (primary/secondary)
    Multicolor   // Use custom colors per zone
};

// ============================================================================
// COMPILED DATA STRUCTURES (used by auto-generated IconData.h)
// ============================================================================

/**
 * Compiled color mapping - maps an SVG element property to a color
 */
struct CompiledColorMapping {
    const char* elementId;
    const char* property;
    uint32_t originalColor;
    const char* cssClass;
};

/**
 * Compiled color zone - groups mappings by original color
 */
struct CompiledColorZone {
    uint32_t originalColor;
    const char* defaultToken;
    const size_t* mappingIndices;
    size_t mappingIndicesCount;
};

/**
 * Compiled icon data - all static icon information
 */
struct CompiledIconData {
    const char* id;
    const char* svgContent;
    float width;
    float height;
    const CompiledColorMapping* colorMappings;
    size_t colorMappingsCount;
    const CompiledColorZone* colorZones;
    size_t colorZonesCount;
};

// ============================================================================
// RUNTIME DATA STRUCTURES (used by IconManager at runtime)
// ============================================================================

/**
 * Runtime color mapping
 */
struct ColorMapping {
    std::string elementId;
    std::string property;
    uint32_t originalColor;
    std::string cssClass;
};

/**
 * Runtime color zone with customization options
 */
struct ColorZone {
    uint32_t originalColor;              // Original color from SVG
    std::string tokenAssignment;         // Token name for bicolor mode (e.g., "primary", "secondary")
    ImVec4 customColor;                  // Custom color for multicolor mode
    std::vector<size_t> mappingIndices;  // Indices into ColorMapping array
    
    ColorZone() 
        : originalColor(0)
        , tokenAssignment("primary")
        , customColor(1.0f, 1.0f, 1.0f, 1.0f)
    {}
};

/**
 * Runtime icon data
 */
struct RuntimeIcon {
    std::string id;
    std::string svgContent;
    float width;
    float height;
    std::vector<ColorMapping> colorMappings;
    std::vector<ColorZone> colorZones;
};

/**
 * Icon metadata - defines how an icon instance should be rendered
 * This is the per-instance customization data
 */
struct IconMetadata {
    IconColorScheme scheme;
    std::vector<ColorZone> colorZones;
    
    IconMetadata() : scheme(IconColorScheme::Bicolor) {}
};

} // namespace VectorGraphics
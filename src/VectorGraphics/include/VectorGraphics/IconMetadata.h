// // #pragma once

// // #include <imgui.h>
// // #include <string>
// // #include <vector>
// // #include <cstdint>

// // namespace VectorGraphics {

// // /**
// //  * Icon color scheme type
// //  */
// // enum class IconColorScheme {
// //     Original,     // Use original SVG colors
// //     Bicolor,      // Use primary/secondary tokens from design system
// //     Multicolor    // Use custom colors per zone
// // };

// // /**
// //  * Represents a single color occurrence in the SVG
// //  * Each element+property combination is tracked separately
// //  */
// // struct ColorMapping {
// //     std::string elementId;      // Element ID or generated identifier
// //     std::string property;       // "fill", "stroke", "stop-color"
// //     uint32_t originalColor;     // Original RGBA color
// //     std::string cssClass;       // CSS class if present (e.g., "ds-primary")
    
// //     ColorMapping() 
// //         : originalColor(0) 
// //     {}
    
// //     ColorMapping(const std::string& elemId, const std::string& prop, 
// //                 uint32_t color, const std::string& cls = "")
// //         : elementId(elemId)
// //         , property(prop)
// //         , originalColor(color)
// //         , cssClass(cls)
// //     {}
// // };

// // /**
// //  * Represents a unique color in the icon
// //  * Groups all mappings that share the same original color
// //  */
// // struct ColorZone {
// //     uint32_t originalColor;          // The RGBA color this zone represents
// //     ImVec4 customColor;              // Custom color for multicolor mode
// //     std::string tokenAssignment;     // "primary" or "secondary" for bicolor mode
// //     std::vector<int> mappingIndices; // Indices into RuntimeIcon::colorMappings
    
// //     ColorZone() 
// //         : originalColor(0)
// //         , customColor(1, 1, 1, 1)
// //         , tokenAssignment("primary")
// //     {}
    
// //     ColorZone(uint32_t color)
// //         : originalColor(color)
// //         , tokenAssignment("primary")
// //     {
// //         // Convert RGBA to ImVec4
// //         customColor.x = ((color) & 0xFF) / 255.0f;        // R
// //         customColor.y = ((color >> 8) & 0xFF) / 255.0f;   // G
// //         customColor.z = ((color >> 16) & 0xFF) / 255.0f;  // B
// //         customColor.w = ((color >> 24) & 0xFF) / 255.0f;  // A
// //     }
// // };

// // /**
// //  * Icon metadata - per-instance configuration
// //  */
// // struct IconMetadata {
// //     IconColorScheme scheme = IconColorScheme::Bicolor;
// //     std::vector<ColorZone> colorZones;
    
// //     IconMetadata() = default;
// //     IconMetadata(const IconMetadata& other) = default;
// //     IconMetadata& operator=(const IconMetadata& other) = default;
// // };

// // /**
// //  * Compiled icon data from build-time generation
// //  */
// // struct CompiledIconData {
// //     std::string id;
// //     const char* svgContent;
// //     float width;
// //     float height;
// // };

// // } // namespace VectorGraphics






// #pragma once

// #include <cstdint>
// #include <cstddef>
// #include <string>
// #include <vector>
// #include <unordered_map>

// namespace VectorGraphics {

// // ============================================================================
// // COMPILED DATA STRUCTURES (used by auto-generated IconData.h)
// // ============================================================================

// struct CompiledColorMapping {
//     const char* elementId;
//     const char* property;
//     uint32_t originalColor;
//     const char* cssClass;
// };

// struct CompiledColorZone {
//     uint32_t originalColor;
//     const char* defaultToken;
//     const size_t* mappingIndices;
//     size_t mappingIndicesCount;
// };

// struct CompiledIconData {
//     const char* id;
//     const char* svgContent;
//     float width;
//     float height;
//     const CompiledColorMapping* colorMappings;
//     size_t colorMappingsCount;
//     const CompiledColorZone* colorZones;
//     size_t colorZonesCount;
// };

// // ============================================================================
// // RUNTIME DATA STRUCTURES (used by IconManager at runtime)
// // ============================================================================

// struct ColorMapping {
//     std::string elementId;
//     std::string property;
//     uint32_t originalColor;
//     std::string cssClass;
// };

// struct ColorZone {
//     uint32_t originalColor;
//     std::string defaultToken;
//     std::vector<size_t> mappingIndices;
// };

// struct RuntimeIcon {
//     std::string id;
//     std::string svgContent;
//     float width;
//     float height;
//     std::vector<ColorMapping> colorMappings;
//     std::vector<ColorZone> colorZones;
// };

// } // namespace VectorGraphics





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
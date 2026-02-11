// // // #pragma once

// // // #include <VectorGraphics/IconMetadata.h>
// // // #include <imgui.h>
// // // #include <string>
// // // #include <map>
// // // #include <vector>
// // // #include <vulkan/vulkan.h>

// // // namespace VectorGraphics {

// // // /**
// // //  * Vulkan texture for rendering
// // //  */
// // // struct VulkanTexture {
// // //     VkImage image = VK_NULL_HANDLE;
// // //     VkDeviceMemory memory = VK_NULL_HANDLE;
// // //     VkImageView view = VK_NULL_HANDLE;
// // //     VkSampler sampler = VK_NULL_HANDLE;
// // //     VkDescriptorSet descriptor = VK_NULL_HANDLE;
// // //     int width = 0;
// // //     int height = 0;
// // // };

// // // /**
// // //  * Runtime icon data with color mappings
// // //  */
// // // struct RuntimeIcon {
// // //     std::string id;
// // //     std::string svgContent;
// // //     std::vector<ColorMapping> colorMappings;  // All color occurrences in SVG
// // //     IconMetadata metadata;                     // Default metadata (template)
// // //     float width;
// // //     float height;
// // // };

// // // /**
// // //  * Icon manager for vector icons with runtime recoloring
// // //  * Uses resvg for high-quality SVG rendering
// // //  */
// // // class IconManager {
// // // public:
// // //     static IconManager& Instance();
    
// // //     void Initialize(
// // //         VkDevice device,
// // //         VkPhysicalDevice physicalDevice,
// // //         VkQueue queue,
// // //         VkCommandPool commandPool,
// // //         VkDescriptorPool descriptorPool
// // //     );
    
// // //     void Shutdown();
    
// // //     /**
// // //      * Load icons from compile-time generated data
// // //      */
// // //     void LoadCompiledIcons();
    
// // //     /**
// // //      * Get default metadata (template) for an icon
// // //      */
// // //     IconMetadata GetDefaultMetadata(const std::string& id) const;
    
// // //     /**
// // //      * Get color mappings for an icon
// // //      */
// // //     const std::vector<ColorMapping>& GetColorMappings(const std::string& id) const;
    
// // //     /**
// // //      * Render icon with custom metadata (instance-specific)
// // //      */
// // //     void RenderIcon(const std::string& id, float size, const IconMetadata& metadata);
    
// // //     /**
// // //      * Render icon with default metadata
// // //      */
// // //     void RenderIcon(const std::string& id, float size);
    
// // //     /**
// // //      * Invalidate cache
// // //      */
// // //     void InvalidateCache();
    
// // //     /**
// // //      * Safe cache cleanup (with vkDeviceWaitIdle)
// // //      */
// // //     void SafeClearCache();
    
// // //     /**
// // //      * Cleanup cache if needed (call at frame start)
// // //      */
// // //     void CleanupCacheIfNeeded();
    
// // //     /**
// // //      * Get list of all loaded icons
// // //      */
// // //     std::vector<std::string> GetLoadedIcons() const;

// // // private:
// // //     IconManager() = default;
// // //     ~IconManager();
    
// // //     void LoadIcon(const CompiledIconData& compiledData);
    
// // //     /**
// // //      * Extract all colors from SVG with proper XML parsing
// // //      */
// // //     void ExtractColorMappings(RuntimeIcon& icon);
    
// // //     /**
// // //      * Parse a single element recursively
// // //      */
// // //     void ParseElement(
// // //         const char* elementStart,
// // //         const char* elementEnd,
// // //         const std::string& elementName,
// // //         int depth,
// // //         std::vector<ColorMapping>& mappings
// // //     );
    
// // //     /**
// // //      * Build color zones from mappings
// // //      */
// // //     void BuildColorZones(RuntimeIcon& icon);
    
// // //     /**
// // //      * Determine default token from CSS class
// // //      */
// // //     std::string DetermineDefaultToken(const std::string& cssClass) const;
    
// // //     /**
// // //      * Parse color value (#RRGGBB format)
// // //      */
// // //     uint32_t ParseHexColor(const std::string& hexColor) const;
    
// // //     /**
// // //      * Extract attribute value from tag
// // //      */
// // //     std::string ExtractAttribute(const std::string& tag, const std::string& attrName) const;
    
// // //     /**
// // //      * Parse style attribute and extract colors
// // //      */
// // //     void ParseStyleAttribute(
// // //         const std::string& style,
// // //         const std::string& elementId,
// // //         const std::string& cssClass,
// // //         std::vector<ColorMapping>& mappings
// // //     );
    
// // //     /**
// // //      * Render SVG with color modifications
// // //      */
// // //     unsigned char* RenderSVGWithColors(
// // //         const RuntimeIcon& icon,
// // //         float size,
// // //         const IconMetadata& metadata,
// // //         int& outWidth,
// // //         int& outHeight
// // //     );
    
// // //     /**
// // //      * Modify SVG colors according to metadata
// // //      */
// // //     std::string ModifySVGColors(
// // //         const std::string& originalSVG,
// // //         const std::vector<ColorMapping>& mappings,
// // //         const IconMetadata& metadata
// // //     ) const;
    
// // //     /**
// // //      * Generate cache key
// // //      */
// // //     std::string CreateCacheKey(
// // //         const std::string& id,
// // //         float size,
// // //         const IconMetadata& metadata
// // //     ) const;
    
// // //     /**
// // //      * Color conversion utilities
// // //      */
// // //     uint32_t ImVec4ToRGBA(const ImVec4& color) const;
// // //     ImVec4 RGBAToImVec4(uint32_t rgba) const;
// // //     std::string ImVec4ToHex(const ImVec4& color) const;
// // //     std::string RGBAToHex(uint32_t rgba) const;
    
// // //     /**
// // //      * Vulkan helpers
// // //      */
// // //     VulkanTexture CreateTexture(const unsigned char* pixels, int width, int height);
// // //     void DestroyTexture(VulkanTexture& tex);
// // //     void ClearCache();
    
// // //     uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
// // //     void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
// // //                      VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
// // //     void CreateImage(int width, int height, VkFormat format, VkImageUsageFlags usage,
// // //                     VkImage& image, VkDeviceMemory& memory);
// // //     VkImageView CreateImageView(VkImage image, VkFormat format);
// // //     VkSampler CreateSampler();
    
// // //     VkCommandBuffer BeginSingleTimeCommands();
// // //     void EndSingleTimeCommands(VkCommandBuffer cmd);
// // //     void TransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
// // //     void CopyBufferToImage(VkBuffer buffer, VkImage image, int width, int height);
    
// // //     std::map<std::string, RuntimeIcon> icons_;
// // //     std::map<std::string, VulkanTexture> textureCache_;
// // //     bool cacheInvalidated_ = false;
    
// // //     VkDevice device_ = VK_NULL_HANDLE;
// // //     VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
// // //     VkQueue queue_ = VK_NULL_HANDLE;
// // //     VkCommandPool commandPool_ = VK_NULL_HANDLE;
// // //     VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
// // //     void* resvgOptions_ = nullptr;
// // // };

// // // } // namespace VectorGraphics





// // #pragma once

// // #include "IconMetadata.h"
// // #include <imgui.h>
// // #include <memory>
// // #include <string>
// // #include <unordered_map>
// // #include <vector>

// // // Forward declarations
// // struct ImDrawList;
// // struct ImVec2;
// // struct ImVec4;

// // namespace VectorGraphics {

// // // ============================================================================
// // // ICON TEXTURE CACHE
// // // ============================================================================

// // struct IconTexture {
// //     ImTextureID textureId;
// //     int width;
// //     int height;
// //     float scale;
// //     std::string colorToken;
// //     uint64_t lastAccessFrame;
    
// //     IconTexture()
// //         : textureId(ImTextureID(0))
// //         , width(0)
// //         , height(0)
// //         , scale(1.0f)
// //         , lastAccessFrame(0)
// //     {}
// // };

// // // ============================================================================
// // // ICON MANAGER
// // // ============================================================================

// // class IconManager {
// // public:
// //     static IconManager& Instance();
    
// //     // Initialization
// //     void Initialize();
// //     void Shutdown();
    
// //     // Icon loading from compiled data
// //     void LoadCompiledIcons();
    
// //     // Icon rendering
// //     void RenderIcon(
// //         const std::string& iconId,
// //         const ImVec2& size,
// //         const std::string& colorToken = "primary",
// //         float scale = 1.0f
// //     );
    
// //     void RenderIcon(
// //         ImDrawList* drawList,
// //         const std::string& iconId,
// //         const ImVec2& position,
// //         const ImVec2& size,
// //         const std::string& colorToken = "primary",
// //         float scale = 1.0f
// //     );
    
// //     // Cache management
// //     void CleanupCacheIfNeeded();
// //     void ClearCache();
// //     size_t GetCacheSize() const;
    
// //     // Icon queries
// //     bool HasIcon(const std::string& iconId) const;
// //     std::vector<std::string> GetAvailableIcons() const;
    
// //     // Get color mappings for an icon
// //     const std::vector<ColorMapping>& GetColorMappings(const std::string& iconId) const;
// //     const std::vector<ColorZone>& GetColorZones(const std::string& iconId) const;
    
// // private:
// //     IconManager();
// //     ~IconManager();
// //     IconManager(const IconManager&) = delete;
// //     IconManager& operator=(const IconManager&) = delete;
    
// //     // Icon loading helpers
// //     void LoadIcon(const CompiledIconData& iconData);
    
// //     // Color mapping extraction from SVG
// //     void ExtractColorMappings(RuntimeIcon& icon);
// //     void ParseStyleAttribute(
// //         const std::string& elementId,
// //         const std::string& attrName,
// //         const std::string& attrValue,
// //         std::vector<ColorMapping>& mappings
// //     );
// //     std::string ExtractAttribute(const std::string& tag, const std::string& attrName) const;
// //     uint32_t ParseHexColor(const std::string& hexColor) const;
    
// //     // Color zone building
// //     void BuildColorZones(RuntimeIcon& icon);
// //     std::string DetermineDefaultToken(const std::string& cssClass) const;
    
// //     // Texture generation and caching
// //     ImTextureID GetOrCreateTexture(
// //         const std::string& iconId,
// //         const ImVec2& size,
// //         const std::string& colorToken,
// //         float scale
// //     );
    
// //     std::string GenerateCacheKey(
// //         const std::string& iconId,
// //         const ImVec2& size,
// //         const std::string& colorToken,
// //         float scale
// //     ) const;
    
// //     ImTextureID RenderIconToTexture(
// //         const std::string& iconId,
// //         int width,
// //         int height,
// //         const std::string& colorToken,
// //         float scale
// //     );
    
// //     std::string ApplyColorToken(
// //         const std::string& svgContent,
// //         const RuntimeIcon& icon,
// //         const std::string& colorToken
// //     ) const;
    
// //     uint32_t GetColorForToken(const std::string& tokenName) const;
    
// //     // Cache cleanup
// //     void CleanupOldCacheEntries();
    
// // private:
// //     std::unordered_map<std::string, RuntimeIcon> icons_;
// //     std::unordered_map<std::string, IconTexture> textureCache_;
    
// //     uint64_t currentFrame_;
// //     size_t maxCacheSize_;
// //     uint64_t cacheMaxAge_;
    
// //     bool initialized_;
// // };

// // } // namespace VectorGraphics







// #pragma once

// #include "IconMetadata.h"
// #include <imgui.h>
// #include <memory>
// #include <string>
// #include <unordered_map>
// #include <vector>

// // Forward declarations
// struct ImDrawList;
// struct ImVec2;
// struct ImVec4;

// namespace VectorGraphics {

// // ============================================================================
// // ICON TEXTURE CACHE
// // ============================================================================

// struct IconTexture {
//     ImTextureID textureId;
//     int width;
//     int height;
//     uint64_t lastAccessFrame;
//     std::string cacheKey;
    
//     IconTexture()
//         : textureId(ImTextureID(0))
//         , width(0)
//         , height(0)
//         , lastAccessFrame(0)
//     {}
// };

// // ============================================================================
// // ICON MANAGER
// // ============================================================================

// class IconManager {
// public:
//     static IconManager& Instance();
    
//     // Initialization
//     void Initialize();
//     void Shutdown();
    
//     // Icon loading from compiled data
//     void LoadCompiledIcons();
    
//     // Icon rendering - simple interface (uses design system colors)
//     void RenderIcon(
//         const std::string& iconId,
//         float size
//     );
    
//     void RenderIcon(
//         const std::string& iconId,
//         float size,
//         const ImVec2& position
//     );
    
//     // Icon rendering - with metadata (full customization)
//     void RenderIcon(
//         const std::string& iconId,
//         float size,
//         const IconMetadata& metadata
//     );
    
//     void RenderIcon(
//         ImDrawList* drawList,
//         const std::string& iconId,
//         const ImVec2& position,
//         float size,
//         const IconMetadata& metadata
//     );
    
//     // Cache management
//     void CleanupCacheIfNeeded();
//     void InvalidateCache();
//     void ClearCache();
//     size_t GetCacheSize() const;
    
//     // Icon queries
//     bool HasIcon(const std::string& iconId) const;
//     std::vector<std::string> GetLoadedIcons() const;
//     std::vector<std::string> GetAvailableIcons() const;
    
//     // Metadata access
//     IconMetadata GetDefaultMetadata(const std::string& iconId) const;
//     const std::vector<ColorMapping>& GetColorMappings(const std::string& iconId) const;
//     const std::vector<ColorZone>& GetColorZones(const std::string& iconId) const;
    
// private:
//     IconManager();
//     ~IconManager();
//     IconManager(const IconManager&) = delete;
//     IconManager& operator=(const IconManager&) = delete;
    
//     // Icon loading helpers
//     void LoadIcon(const CompiledIconData& iconData);
    
//     // Texture generation and caching
//     ImTextureID GetOrCreateTexture(
//         const std::string& iconId,
//         int width,
//         int height,
//         const IconMetadata& metadata
//     );
    
//     std::string GenerateCacheKey(
//         const std::string& iconId,
//         int width,
//         int height,
//         const IconMetadata& metadata
//     ) const;
    
//     ImTextureID RenderIconToTexture(
//         const std::string& iconId,
//         int width,
//         int height,
//         const IconMetadata& metadata
//     );
    
//     std::string ApplyColorMapping(
//         const std::string& svgContent,
//         const RuntimeIcon& icon,
//         const IconMetadata& metadata
//     ) const;
    
//     uint32_t GetColorForZone(
//         const ColorZone& zone,
//         IconColorScheme scheme
//     ) const;
    
//     uint32_t GetColorForToken(const std::string& tokenName) const;
//     ImVec4 ColorU32ToVec4(uint32_t color) const;
//     uint32_t ColorVec4ToU32(const ImVec4& color) const;
    
//     // Cache cleanup
//     void CleanupOldCacheEntries();
    
// private:
//     std::unordered_map<std::string, RuntimeIcon> icons_;
//     std::unordered_map<std::string, IconTexture> textureCache_;
    
//     uint64_t currentFrame_;
//     size_t maxCacheSize_;
//     uint64_t cacheMaxAge_;
    
//     bool initialized_;
// };

// } // namespace VectorGraphics








#pragma once

#include "IconMetadata.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct ImDrawList;
struct ImVec2;
struct ImVec4;

namespace VectorGraphics {

// ============================================================================
// ICON TEXTURE CACHE
// ============================================================================

struct IconTexture {
    ImTextureID textureId;
    VkImageView imageView;
    VkImage image;
    VkDeviceMemory imageMemory;
    VkDescriptorSet descriptorSet;
    int width;
    int height;
    uint64_t lastAccessFrame;
    std::string cacheKey;
    
    IconTexture()
        : textureId(ImTextureID(0))
        , imageView(VK_NULL_HANDLE)
        , image(VK_NULL_HANDLE)
        , imageMemory(VK_NULL_HANDLE)
        , descriptorSet(VK_NULL_HANDLE)
        , width(0)
        , height(0)
        , lastAccessFrame(0)
    {}
};

// ============================================================================
// ICON MANAGER
// ============================================================================

class IconManager {
public:
    static IconManager& Instance();
    
    // Initialization
    void Initialize(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkQueue queue,
        VkCommandPool commandPool,
        VkDescriptorPool descriptorPool
    );
    void Shutdown();
    
    // Icon loading from compiled data
    void LoadCompiledIcons();
    
    // Icon rendering - simple interface (uses design system colors)
    void RenderIcon(
        const std::string& iconId,
        float size
    );
    
    void RenderIcon(
        const std::string& iconId,
        float size,
        const ImVec2& position
    );
    
    // Icon rendering - with metadata (full customization)
    void RenderIcon(
        const std::string& iconId,
        float size,
        const IconMetadata& metadata
    );
    
    void RenderIcon(
        ImDrawList* drawList,
        const std::string& iconId,
        const ImVec2& position,
        float size,
        const IconMetadata& metadata
    );
    
    // Cache management
    void CleanupCacheIfNeeded();
    void InvalidateCache();
    void ClearCache();
    size_t GetCacheSize() const;
    
    // Icon queries
    bool HasIcon(const std::string& iconId) const;
    std::vector<std::string> GetLoadedIcons() const;
    std::vector<std::string> GetAvailableIcons() const;
    
    // Metadata access
    IconMetadata GetDefaultMetadata(const std::string& iconId) const;
    const std::vector<ColorMapping>& GetColorMappings(const std::string& iconId) const;
    const std::vector<ColorZone>& GetColorZones(const std::string& iconId) const;
    
private:
    IconManager();
    ~IconManager();
    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;
    
    // Icon loading helpers
    void LoadIcon(const CompiledIconData& iconData);
    
    // Texture generation and caching
    ImTextureID GetOrCreateTexture(
        const std::string& iconId,
        int width,
        int height,
        const IconMetadata& metadata
    );
    
    std::string GenerateCacheKey(
        const std::string& iconId,
        int width,
        int height,
        const IconMetadata& metadata
    ) const;
    
    // ImTextureID RenderIconToTexture(
    //     const std::string& iconId,
    //     int width,
    //     int height,
    //     const IconMetadata& metadata
    // );
    IconTexture RenderIconToTexture(
        const std::string& iconId,
        int width,
        int height,
        const IconMetadata& metadata
    );
    
    std::string ApplyColorMapping(
        const std::string& svgContent,
        const RuntimeIcon& icon,
        const IconMetadata& metadata
    ) const;
    
    uint32_t GetColorForZone(
        const ColorZone& zone,
        IconColorScheme scheme
    ) const;
    
    uint32_t GetColorForToken(const std::string& tokenName) const;
    ImVec4 ColorU32ToVec4(uint32_t color) const;
    uint32_t ColorVec4ToU32(const ImVec4& color) const;
    
    // Vulkan texture creation
    // ImTextureID CreateVulkanTextureFromRGBA(
    //     const uint8_t* pixelData,
    //     int width,
    //     int height
    // );
    IconTexture CreateVulkanTextureFromRGBA(
        const uint8_t* pixelData,
        int width,
        int height
    );
    
    void DestroyVulkanTexture(const IconTexture& texture);
    
    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;
    
    // Cache cleanup
    void CleanupOldCacheEntries();
    
private:
    std::unordered_map<std::string, RuntimeIcon> icons_;
    std::unordered_map<std::string, IconTexture> textureCache_;
    
    uint64_t currentFrame_;
    size_t maxCacheSize_;
    uint64_t cacheMaxAge_;
    
    bool initialized_;
    
    // Vulkan resources
    VkDevice device_;
    VkPhysicalDevice physicalDevice_;
    VkQueue queue_;
    VkCommandPool commandPool_;
    VkDescriptorPool descriptorPool_;
    VkSampler sampler_;
};

} // namespace VectorGraphics
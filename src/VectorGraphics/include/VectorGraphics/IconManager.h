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
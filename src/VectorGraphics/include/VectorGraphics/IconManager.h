// 



#pragma once

#include <VectorGraphics/IconMetadata.h>
#include <imgui.h>
#include <string>
#include <map>
#include <vector>
#include <vulkan/vulkan.h>

namespace VectorGraphics {

/**
 * Vulkan texture for rendering
 */
struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

/**
 * Runtime icon data
 */
struct RuntimeIcon {
    std::string id;
    std::string svgContent;
    std::map<std::string, std::string> classToToken;  // Maps "elementId:property" to token name
    IconMetadata metadata;
    float width;
    float height;
};

/**
 * Icon manager for vector icons with runtime recoloring
 * Uses resvg for high-quality SVG rendering with full feature support
 */
class IconManager {
public:
    static IconManager& Instance();
    
    void Initialize(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkQueue queue,
        VkCommandPool commandPool,
        VkDescriptorPool descriptorPool
    );
    
    void Shutdown();
    
    /**
     * Load icons from compile-time generated data
     */
    void LoadCompiledIcons();
    
    /**
     * Set global metadata (template for this icon)
     */
    void SetIconMetadata(const std::string& id, const IconMetadata& metadata);
    
    /**
     * Get global metadata pointer (for reading/modifying template)
     */
    IconMetadata* GetIconMetadata(const std::string& id);
    
    /**
     * Get a copy of metadata for local editing (instance-specific)
     * IMPORTANT: This returns a COPY that won't affect other instances
     */
    IconMetadata GetIconMetadataCopy(const std::string& id) const;
    
    /**
     * Get color zone count
     */
    int GetColorZoneCount(const std::string& id) const;
    
    /**
     * Render icon using global metadata (template)
     */
    void RenderIcon(const std::string& id, float size);
    
    /**
     * Render icon with local metadata (instance-specific)
     * IMPORTANT: Changes to localMetadata don't affect global template
     */
    void RenderIcon(const std::string& id, float size, const IconMetadata& localMetadata);
    
    /**
     * Render bicolor with design system tokens
     */
    void RenderIcon(
        const std::string& id,
        float size,
        const std::string& primaryToken,
        const std::string& secondaryToken
    );
    
    /**
     * Render bicolor with specific colors
     */
    void RenderIcon(
        const std::string& id,
        float size,
        const ImVec4& primaryColor,
        const ImVec4& secondaryColor
    );
    
    /**
     * Invalidate cache (will be cleaned up next frame)
     */
    void InvalidateCache();
    
    /**
     * Safe cache cleanup (with vkDeviceWaitIdle)
     */
    void SafeClearCache();
    
    /**
     * Cleanup cache if needed (call at frame start)
     */
    void CleanupCacheIfNeeded();
    
    /**
     * Get list of all loaded icons
     */
    std::vector<std::string> GetLoadedIcons() const;

private:
    IconManager() = default;
    ~IconManager();
    
    void LoadIcon(const CompiledIconData& compiledData);
    void ExtractColorZones(RuntimeIcon& icon);
    
    unsigned char* RenderSVGWithColors(
        const RuntimeIcon& icon,
        float size,
        const std::map<std::string, ImVec4>& tokenColors,
        const IconMetadata& metadata,
        int& outWidth,
        int& outHeight
    );
    
    std::string ModifySVGColors(
        const std::string& originalSVG,
        const std::map<std::string, std::string>& classToToken,
        const std::map<std::string, ImVec4>& tokenColors,
        const IconMetadata& metadata
    );
    
    std::string CreateCacheKey(
        const std::string& id,
        float size,
        const IconMetadata& metadata,
        const ImVec4* primaryColor = nullptr,
        const ImVec4* secondaryColor = nullptr,
        const ImVec4* tertiaryColor = nullptr
    ) const;
    
    uint32_t ImVec4ToRGBA(const ImVec4& color) const;
    ImVec4 RGBAToImVec4(uint32_t rgba) const;
    std::string ImVec4ToHex(const ImVec4& color) const;
    
    VulkanTexture CreateTexture(const unsigned char* pixels, int width, int height);
    void DestroyTexture(VulkanTexture& tex);
    void ClearCache();
    
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void CreateImage(int width, int height, VkFormat format, VkImageUsageFlags usage,
                    VkImage& image, VkDeviceMemory& memory);
    VkImageView CreateImageView(VkImage image, VkFormat format);
    VkSampler CreateSampler();
    
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer cmd);
    void TransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, int width, int height);
    
    std::map<std::string, RuntimeIcon> icons_;
    std::map<std::string, VulkanTexture> textureCache_;
    bool cacheInvalidated_ = false;
    
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // resvg C API opaque handles
    void* resvgOptions_ = nullptr;
};

} // namespace VectorGraphics
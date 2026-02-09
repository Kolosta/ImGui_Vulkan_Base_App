// #pragma once

// #include <imgui.h>
// #include <string>
// #include <map>
// #include <vector>
// #include <vulkan/vulkan.h>

// #include <nanosvg.h>
// #include <nanosvgrast.h>

// namespace UI {

// /**
//  * Type de schéma de couleur pour une icône
//  */
// enum class IconColorScheme {
//     Original,     // Couleurs originales du SVG (par défaut, non éditable)
//     Bicolor,      // 2 couleurs (primaire + secondaire du design system)
//     Multicolor    // Palette personnalisée par zone de couleur
// };

// /**
//  * Zone de couleur dans une icône
//  * Représente une couleur unique détectée dans le SVG
//  */
// struct ColorZone {
//     uint32_t originalColor;          // Couleur d'origine en ABGR format
//     bool usePrimary = true;          // Pour bicolor : true=primary, false=secondary
//     ImVec4 customColor;              // Pour multicolor : couleur personnalisée
    
//     ColorZone() : originalColor(0), usePrimary(true), customColor(1, 1, 1, 1) {}
    
//     ColorZone(uint32_t orig) : originalColor(orig), usePrimary(true) {
//         // Convertir ABGR vers ImVec4
//         customColor.x = ((orig) & 0xFF) / 255.0f;        // R
//         customColor.y = ((orig >> 8) & 0xFF) / 255.0f;   // G
//         customColor.z = ((orig >> 16) & 0xFF) / 255.0f;  // B
//         customColor.w = ((orig >> 24) & 0xFF) / 255.0f;  // A
//     }
// };

// /**
//  * Métadonnées d'une icône
//  */
// struct IconMetadata {
//     IconColorScheme scheme = IconColorScheme::Original;
//     std::vector<ColorZone> colorZones;  // Zones de couleur auto-détectées
// };

// /**
//  * Structure représentant une icône vectorielle
//  */
// struct VectorIcon {
//     NSVGimage* svgImage = nullptr;
//     ImVec2 size;
//     IconMetadata metadata;
// };

// /**
//  * Texture Vulkan pour le rendu
//  */
// struct VulkanTexture {
//     VkImage image = VK_NULL_HANDLE;
//     VkDeviceMemory memory = VK_NULL_HANDLE;
//     VkImageView view = VK_NULL_HANDLE;
//     VkSampler sampler = VK_NULL_HANDLE;
//     VkDescriptorSet descriptor = VK_NULL_HANDLE;
//     int width = 0;
//     int height = 0;
// };

// /**
//  * Gestionnaire d'icônes vectorielles
//  */
// class IconManager {
// public:
//     static IconManager& Instance();
    
//     void Initialize(
//         VkDevice device,
//         VkPhysicalDevice physicalDevice,
//         VkQueue queue,
//         VkCommandPool commandPool,
//         VkDescriptorPool descriptorPool
//     );
    
//     void Shutdown();
    
//     void LoadIconsFromDirectory(const std::string& directory);
//     bool LoadIcon(const std::string& id, const std::string& filepath);
    
//     void SetIconMetadata(const std::string& id, const IconMetadata& metadata);
//     IconMetadata* GetIconMetadata(const std::string& id);
//     int GetColorZoneCount(const std::string& id) const;
    
//     /**
//      * Rendu d'icône (utilise automatiquement les métadonnées configurées)
//      */
//     void RenderIcon(const std::string& id, float size);
    
//     /**
//      * Rendu bicolore avec tokens design system
//      */
//     void RenderIcon(
//         const std::string& id,
//         float size,
//         const std::string& primaryToken,
//         const std::string& secondaryToken
//     );
    
//     /**
//      * Rendu bicolore avec couleurs spécifiques
//      */
//     void RenderIcon(
//         const std::string& id,
//         float size,
//         const ImVec4& primaryColor,
//         const ImVec4& secondaryColor
//     );
    
//     /**
//      * Invalider le cache (sera nettoyé au prochain frame)
//      */
//     void InvalidateCache();
    
//     /**
//      * Nettoyer le cache de manière sécurisée (avec vkDeviceWaitIdle)
//      */
//     void SafeClearCache();
    
//     /**
//      * Nettoyer le cache si invalidé (à appeler en début de frame)
//      */
//     void CleanupCacheIfNeeded();
    
//     std::vector<std::string> GetLoadedIcons() const;

// private:
//     IconManager() = default;
//     ~IconManager();
    
//     bool ParseSVG(const std::string& filepath, VectorIcon& icon);
//     void DetectColorZones(VectorIcon& icon);
    
//     unsigned char* RasterizeWithColors(
//         const VectorIcon& icon,
//         float size,
//         const std::map<uint32_t, uint32_t>& colorReplacements,
//         int& outWidth,
//         int& outHeight
//     );
    
//     std::string CreateCacheKey(
//         const std::string& id,
//         float size,
//         const IconMetadata& metadata,
//         const ImVec4* primaryColor = nullptr,
//         const ImVec4* secondaryColor = nullptr
//     ) const;
    
//     uint32_t ImVec4ToABGR(const ImVec4& color) const;
    
//     VulkanTexture CreateTexture(const unsigned char* pixels, int width, int height);
//     void DestroyTexture(VulkanTexture& tex);
//     void ClearCache();
    
//     uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
//     void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
//                      VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
//     void CreateImage(int width, int height, VkFormat format, VkImageUsageFlags usage,
//                     VkImage& image, VkDeviceMemory& memory);
//     VkImageView CreateImageView(VkImage image, VkFormat format);
//     VkSampler CreateSampler();
    
//     VkCommandBuffer BeginSingleTimeCommands();
//     void EndSingleTimeCommands(VkCommandBuffer cmd);
//     void TransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
//     void CopyBufferToImage(VkBuffer buffer, VkImage image, int width, int height);
    
//     std::map<std::string, VectorIcon> icons_;
//     std::map<std::string, VulkanTexture> textureCache_;
//     bool cacheInvalidated_ = false;
    
//     VkDevice device_ = VK_NULL_HANDLE;
//     VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
//     VkQueue queue_ = VK_NULL_HANDLE;
//     VkCommandPool commandPool_ = VK_NULL_HANDLE;
//     VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
// };

// } // namespace UI








#pragma once

#include <imgui.h>
#include <string>
#include <map>
#include <vector>
#include <vulkan/vulkan.h>

#include <nanosvg.h>
#include <nanosvgrast.h>

namespace UI {

/**
 * Icon color scheme type
 */
enum class IconColorScheme {
    Original,     // Original SVG colors (default, non-editable)
    Bicolor,      // 2 colors (primary + secondary from design system)
    Multicolor    // Custom palette per color zone
};

/**
 * Color zone in an icon - represents a unique color detected in SVG
 */
struct ColorZone {
    uint32_t originalColor;          // Original color in ABGR format
    bool usePrimary = true;          // For bicolor: true=primary, false=secondary
    ImVec4 customColor;              // For multicolor: custom color
    std::string svgId;               // Optional: SVG element ID for auto-detection
    
    ColorZone() : originalColor(0), usePrimary(true), customColor(1, 1, 1, 1) {}
    
    ColorZone(uint32_t orig) : originalColor(orig), usePrimary(true) {
        // Convert ABGR to ImVec4
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
    IconColorScheme scheme = IconColorScheme::Original;
    std::vector<ColorZone> colorZones;  // Detected color zones
    
    // Copy constructor for local instances
    IconMetadata() = default;
    IconMetadata(const IconMetadata& other) = default;
    IconMetadata& operator=(const IconMetadata& other) = default;
};

/**
 * Vector icon structure
 */
struct VectorIcon {
    NSVGimage* svgImage = nullptr;
    ImVec2 size;
    IconMetadata metadata;  // Global metadata (template)
};

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
 * Icon manager for vector icons with runtime recoloring
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
    
    void LoadIconsFromDirectory(const std::string& directory);
    bool LoadIcon(const std::string& id, const std::string& filepath);
    
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
    
    bool ParseSVG(const std::string& filepath, VectorIcon& icon);
    void DetectColorZones(VectorIcon& icon);
    void AutoConfigureFromSVGIds(VectorIcon& icon);
    
    unsigned char* RasterizeWithColors(
        const VectorIcon& icon,
        float size,
        const std::map<uint32_t, uint32_t>& colorReplacements,
        int& outWidth,
        int& outHeight
    );
    
    std::string CreateCacheKey(
        const std::string& id,
        float size,
        const IconMetadata& metadata,
        const ImVec4* primaryColor = nullptr,
        const ImVec4* secondaryColor = nullptr
    ) const;
    
    uint32_t ImVec4ToABGR(const ImVec4& color) const;
    
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
    
    std::map<std::string, VectorIcon> icons_;
    std::map<std::string, VulkanTexture> textureCache_;
    bool cacheInvalidated_ = false;
    
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace UI
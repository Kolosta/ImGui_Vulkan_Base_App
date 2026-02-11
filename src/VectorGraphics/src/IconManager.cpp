#include "VectorGraphics/IconManager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <DesignSystem/DesignSystem.h>
#include <imgui_impl_vulkan.h>

// Include the generated icon data
#include "generated/IconData.h"

// External resvg C bindings
extern "C" {
    #include "resvg_c.h"
}

namespace VectorGraphics {

// ============================================================================
// SINGLETON IMPLEMENTATION
// ============================================================================

IconManager& IconManager::Instance() {
    static IconManager instance;
    return instance;
}

IconManager::IconManager()
    : currentFrame_(0)
    , maxCacheSize_(100)
    , cacheMaxAge_(300)  // 300 frames ≈ 5 seconds at 60fps
    , initialized_(false)
    , device_(VK_NULL_HANDLE)
    , physicalDevice_(VK_NULL_HANDLE)
    , queue_(VK_NULL_HANDLE)
    , commandPool_(VK_NULL_HANDLE)
    , descriptorPool_(VK_NULL_HANDLE)
    , sampler_(VK_NULL_HANDLE)
{}

IconManager::~IconManager() {
    Shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void IconManager::Initialize(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkQueue queue,
    VkCommandPool commandPool,
    VkDescriptorPool descriptorPool
) {
    if (initialized_) {
        return;
    }
    
    device_ = device;
    physicalDevice_ = physicalDevice;
    queue_ = queue;
    commandPool_ = commandPool;
    descriptorPool_ = descriptorPool;
    
    // Initialize resvg
    resvg_init_log();
    
    // Create sampler for textures
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    
    VkResult err = vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "[IconManager] Failed to create sampler\n");
        return;
    }
    
    // Load compiled icons
    LoadCompiledIcons();
    
    initialized_ = true;
}

void IconManager::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    ClearCache();
    icons_.clear();
    
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
}

// ============================================================================
// ICON LOADING
// ============================================================================

void IconManager::LoadCompiledIcons() {
    for (size_t i = 0; i < COMPILED_ICONS_COUNT; ++i) {
        LoadIcon(COMPILED_ICONS[i]);
    }
}

void IconManager::LoadIcon(const CompiledIconData& iconData) {
    RuntimeIcon icon;
    icon.id = iconData.id;
    icon.svgContent = iconData.svgContent;
    icon.width = iconData.width;
    icon.height = iconData.height;
    
    // Convert compiled color mappings to runtime format
    for (size_t i = 0; i < iconData.colorMappingsCount; ++i) {
        const auto& compiledMapping = iconData.colorMappings[i];
        ColorMapping mapping;
        mapping.elementId = compiledMapping.elementId;
        mapping.property = compiledMapping.property;
        mapping.originalColor = compiledMapping.originalColor;
        mapping.cssClass = compiledMapping.cssClass;
        icon.colorMappings.push_back(mapping);
    }
    
    // Convert compiled color zones to runtime format
    for (size_t i = 0; i < iconData.colorZonesCount; ++i) {
        const auto& compiledZone = iconData.colorZones[i];
        ColorZone zone;
        zone.originalColor = compiledZone.originalColor;
        zone.tokenAssignment = compiledZone.defaultToken;
        
        // Initialize customColor to original color
        zone.customColor = ColorU32ToVec4(compiledZone.originalColor);
        
        for (size_t j = 0; j < compiledZone.mappingIndicesCount; ++j) {
            zone.mappingIndices.push_back(compiledZone.mappingIndices[j]);
        }
        
        icon.colorZones.push_back(zone);
    }
    
    icons_[icon.id] = std::move(icon);
}

// ============================================================================
// ICON RENDERING - SIMPLE INTERFACE
// ============================================================================

void IconManager::RenderIcon(
    const std::string& iconId,
    float size
) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    IconMetadata metadata = GetDefaultMetadata(iconId);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    RenderIcon(drawList, iconId, pos, size, metadata);
    ImGui::Dummy(ImVec2(size, size));
}

void IconManager::RenderIcon(
    const std::string& iconId,
    float size,
    const ImVec2& position
) {
    IconMetadata metadata = GetDefaultMetadata(iconId);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    RenderIcon(drawList, iconId, position, size, metadata);
}

// ============================================================================
// ICON RENDERING - WITH METADATA
// ============================================================================

void IconManager::RenderIcon(
    const std::string& iconId,
    float size,
    const IconMetadata& metadata
) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    RenderIcon(drawList, iconId, pos, size, metadata);
    ImGui::Dummy(ImVec2(size, size));
}

void IconManager::RenderIcon(
    ImDrawList* drawList,
    const std::string& iconId,
    const ImVec2& position,
    float size,
    const IconMetadata& metadata
) {
    if (!HasIcon(iconId)) {
        // Draw placeholder for missing icon
        drawList->AddRectFilled(
            position,
            ImVec2(position.x + size, position.y + size),
            IM_COL32(255, 0, 255, 128)
        );
        return;
    }
    
    int pixelSize = static_cast<int>(size);
    ImTextureID texture = GetOrCreateTexture(iconId, pixelSize, pixelSize, metadata);
    
    if (texture) {
        drawList->AddImage(
            texture,
            position,
            ImVec2(position.x + size, position.y + size)
        );
    }
}

// ============================================================================
// TEXTURE MANAGEMENT
// ============================================================================

// std::string IconManager::GenerateCacheKey(
//     const std::string& iconId,
//     int width,
//     int height,
//     const IconMetadata& metadata
// ) const {
//     std::ostringstream oss;
//     oss << iconId << "_" << width << "x" << height << "_" << static_cast<int>(metadata.scheme);
    
//     // Add zone information to cache key
//     for (const auto& zone : metadata.colorZones) {
//         oss << "_" << zone.originalColor;
        
//         if (metadata.scheme == IconColorScheme::Bicolor) {
//             oss << "_" << zone.tokenAssignment;
//         } else if (metadata.scheme == IconColorScheme::Multicolor) {
//             oss << "_" << std::hex << ColorVec4ToU32(zone.customColor);
//         }
//     }
    
//     return oss.str();
// }
std::string IconManager::GenerateCacheKey(
    const std::string& iconId,
    int width,
    int height,
    const IconMetadata& metadata
) const {
    std::ostringstream oss;
    oss << iconId << "_" << width << "x" << height << "_" << static_cast<int>(metadata.scheme);
    
    // Add zone information to cache key
    for (const auto& zone : metadata.colorZones) {
        oss << "_" << zone.originalColor;
        
        if (metadata.scheme == IconColorScheme::Bicolor) {
            // CORRECTION : Inclure la VALEUR actuelle du token, pas juste son nom
            oss << "_" << zone.tokenAssignment;
            uint32_t tokenColor = GetColorForToken(zone.tokenAssignment);
            oss << "_" << std::hex << tokenColor;
        } else if (metadata.scheme == IconColorScheme::Multicolor) {
            oss << "_" << std::hex << ColorVec4ToU32(zone.customColor);
        }
    }
    
    return oss.str();
}

// ImTextureID IconManager::GetOrCreateTexture(
//     const std::string& iconId,
//     int width,
//     int height,
//     const IconMetadata& metadata
// ) {
//     std::string cacheKey = GenerateCacheKey(iconId, width, height, metadata);
    
//     auto it = textureCache_.find(cacheKey);
//     if (it != textureCache_.end()) {
//         it->second.lastAccessFrame = currentFrame_;
//         return it->second.textureId;
//     }
    
//     // Create new texture
//     ImTextureID texture = RenderIconToTexture(iconId, width, height, metadata);
    
//     if (texture) {
//         // Entry already added in RenderIconToTexture via CreateVulkanTextureFromRGBA
        
//         // Cleanup if cache is too large
//         if (textureCache_.size() > maxCacheSize_) {
//             CleanupOldCacheEntries();
//         }
//     }
    
//     return texture;
// }
// ImTextureID IconManager::GetOrCreateTexture(
//     const std::string& iconId,
//     int width,
//     int height,
//     const IconMetadata& metadata
// ) {
//     std::string cacheKey = GenerateCacheKey(iconId, width, height, metadata);
    
//     auto it = textureCache_.find(cacheKey);
//     if (it != textureCache_.end()) {
//         it->second.lastAccessFrame = currentFrame_;
//         return it->second.textureId;
//     }
    
//     // Create new texture
//     ImTextureID texture = RenderIconToTexture(iconId, width, height, metadata);
    
//     if (texture) {
//         // CORRECTION : Ajouter réellement l'entrée au cache
//         IconTexture iconTexture;
//         iconTexture.textureId = texture;
//         iconTexture.cacheKey = cacheKey;
//         iconTexture.width = width;
//         iconTexture.height = height;
//         iconTexture.lastAccessFrame = currentFrame_;
        
//         // Les autres champs (imageView, image, etc.) ne sont pas facilement accessibles ici
//         // mais ne sont pas critiques pour le cache de base
        
//         textureCache_[cacheKey] = iconTexture;
        
//         // Cleanup if cache is too large
//         if (textureCache_.size() > maxCacheSize_) {
//             CleanupOldCacheEntries();
//         }
//     }
    
//     return texture;
// }
ImTextureID IconManager::GetOrCreateTexture(
    const std::string& iconId,
    int width,
    int height,
    const IconMetadata& metadata
) {
    std::string cacheKey = GenerateCacheKey(iconId, width, height, metadata);
    
    auto it = textureCache_.find(cacheKey);
    if (it != textureCache_.end()) {
        it->second.lastAccessFrame = currentFrame_;
        return it->second.textureId;
    }
    
    // Create new texture - retourne maintenant IconTexture complet
    IconTexture newTexture = RenderIconToTexture(iconId, width, height, metadata);
    
    if (newTexture.textureId) {
        newTexture.cacheKey = cacheKey;
        textureCache_[cacheKey] = newTexture;
        
        if (textureCache_.size() > maxCacheSize_) {
            CleanupOldCacheEntries();
        }
        
        return newTexture.textureId;
    }
    
    return ImTextureID(0);
}


// ImTextureID IconManager::RenderIconToTexture(
//     const std::string& iconId,
//     int width,
//     int height,
//     const IconMetadata& metadata
// ) {
//     auto it = icons_.find(iconId);
//     if (it == icons_.end()) {
//         return ImTextureID(0);
//     }
    
//     const RuntimeIcon& icon = it->second;
    
//     // Apply color mapping to SVG
//     std::string processedSvg = ApplyColorMapping(icon.svgContent, icon, metadata);
    
//     // Parse SVG with resvg
//     resvg_options* opt = resvg_options_create();
//     resvg_options_load_system_fonts(opt);
    
//     resvg_render_tree* tree = resvg_parse_tree_from_data(
//         processedSvg.c_str(),
//         processedSvg.length(),
//         opt
//     );
    
//     resvg_options_destroy(opt);
    
//     if (!tree) {
//         return ImTextureID(0);
//     }
    
//     // Get tree size
//     resvg_size tree_size = resvg_get_image_size(tree);
    
//     // Calculate scaling factor
//     float scaleX = width / tree_size.width;
//     float scaleY = height / tree_size.height;
//     float finalScale = std::min(scaleX, scaleY);
    
//     // Allocate pixel buffer (RGBA)
//     std::vector<uint8_t> pixmap(width * height * 4, 0);
    
//     // Render
//     resvg_fit_to fit_to;
//     fit_to.type_ = RESVG_FIT_TO_TYPE_ZOOM;
//     fit_to.value = finalScale;
    
//     resvg_render(tree, fit_to, resvg_transform_identity(), width, height, pixmap.data());
    
//     resvg_tree_destroy(tree);
    
//     // Create Vulkan texture from pixmap buffer
//     ImTextureID textureId = CreateVulkanTextureFromRGBA(pixmap.data(), width, height);
    
//     return textureId;
// }
IconTexture IconManager::RenderIconToTexture(
    const std::string& iconId,
    int width,
    int height,
    const IconMetadata& metadata
) {
    IconTexture emptyTexture; // Retour par défaut en cas d'erreur
    
    auto it = icons_.find(iconId);
    if (it == icons_.end()) {
        return emptyTexture;
    }
    
    const RuntimeIcon& icon = it->second;
    
    // Apply color mapping to SVG
    std::string processedSvg = ApplyColorMapping(icon.svgContent, icon, metadata);
    
    // Parse SVG with resvg
    resvg_options* opt = resvg_options_create();
    resvg_options_load_system_fonts(opt);
    
    resvg_render_tree* tree = resvg_parse_tree_from_data(
        processedSvg.c_str(),
        processedSvg.length(),
        opt
    );
    
    resvg_options_destroy(opt);
    
    if (!tree) {
        return emptyTexture;
    }
    
    // Get tree size
    resvg_size tree_size = resvg_get_image_size(tree);
    
    // Calculate scaling factor
    float scaleX = width / tree_size.width;
    float scaleY = height / tree_size.height;
    float finalScale = std::min(scaleX, scaleY);
    
    // Allocate pixel buffer (RGBA)
    std::vector<uint8_t> pixmap(width * height * 4, 0);
    
    // Render
    resvg_fit_to fit_to;
    fit_to.type_ = RESVG_FIT_TO_TYPE_ZOOM;
    fit_to.value = finalScale;
    
    resvg_render(tree, fit_to, resvg_transform_identity(), width, height, pixmap.data());
    
    resvg_tree_destroy(tree);
    
    // Create Vulkan texture from pixmap buffer - retourne maintenant IconTexture complet
    return CreateVulkanTextureFromRGBA(pixmap.data(), width, height);
}

std::string IconManager::ApplyColorMapping(
    const std::string& svgContent,
    const RuntimeIcon& icon,
    const IconMetadata& metadata
) const {
    if (metadata.scheme == IconColorScheme::Original) {
        return svgContent;
    }
    
    std::string result = svgContent;
    
    // Build color replacement map
    std::unordered_map<uint32_t, uint32_t> colorReplacements;
    
    for (size_t zoneIdx = 0; zoneIdx < metadata.colorZones.size() && zoneIdx < icon.colorZones.size(); ++zoneIdx) {
        const ColorZone& metaZone = metadata.colorZones[zoneIdx];
        const ColorZone& iconZone = icon.colorZones[zoneIdx];
        
        uint32_t newColor = GetColorForZone(metaZone, metadata.scheme);
        colorReplacements[iconZone.originalColor] = newColor;
    }
    
    // Replace colors in SVG
    for (const auto& [oldColor, newColor] : colorReplacements) {
        char oldColorHex[10];
        char newColorHex[10];
        
        snprintf(oldColorHex, sizeof(oldColorHex), "#%06X", oldColor & 0xFFFFFF);
        snprintf(newColorHex, sizeof(newColorHex), "#%06X", newColor & 0xFFFFFF);
        
        // Replace all occurrences
        size_t pos = 0;
        while ((pos = result.find(oldColorHex, pos)) != std::string::npos) {
            result.replace(pos, strlen(oldColorHex), newColorHex);
            pos += strlen(newColorHex);
        }
        
        // Also try lowercase
        snprintf(oldColorHex, sizeof(oldColorHex), "#%06x", oldColor & 0xFFFFFF);
        snprintf(newColorHex, sizeof(newColorHex), "#%06x", newColor & 0xFFFFFF);
        
        pos = 0;
        while ((pos = result.find(oldColorHex, pos)) != std::string::npos) {
            result.replace(pos, strlen(oldColorHex), newColorHex);
            pos += strlen(newColorHex);
        }
    }
    
    return result;
}

uint32_t IconManager::GetColorForZone(
    const ColorZone& zone,
    IconColorScheme scheme
) const {
    switch (scheme) {
        case IconColorScheme::Original:
            return zone.originalColor;
            
        case IconColorScheme::Bicolor:
            return GetColorForToken(zone.tokenAssignment);
            
        case IconColorScheme::Multicolor:
            return ColorVec4ToU32(zone.customColor);
            
        default:
            return zone.originalColor;
    }
}

uint32_t IconManager::GetColorForToken(const std::string& tokenName) const {
    auto& ds = DesignSystem::DesignSystem::Instance();
    
    // Build full token path
    std::string fullToken = "semantic.icon.color." + tokenName;
    
    try {
        ImVec4 color = ds.GetColor(fullToken);
        return ColorVec4ToU32(color);
    } catch (...) {
        // Fallback to white if token not found
        return 0xFFFFFFFF;
    }
}

ImVec4 IconManager::ColorU32ToVec4(uint32_t color) const {
    return ImVec4(
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f,
        ((color >> 24) & 0xFF) / 255.0f
    );
}

uint32_t IconManager::ColorVec4ToU32(const ImVec4& color) const {
    uint32_t r = static_cast<uint32_t>(color.x * 255.0f);
    uint32_t g = static_cast<uint32_t>(color.y * 255.0f);
    uint32_t b = static_cast<uint32_t>(color.z * 255.0f);
    uint32_t a = static_cast<uint32_t>(color.w * 255.0f);
    
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// ============================================================================
// VULKAN TEXTURE CREATION
// ============================================================================
IconTexture IconManager::CreateVulkanTextureFromRGBA(
    const uint8_t* pixelData,
    int width,
    int height
) {
    IconTexture iconTexture; // Retour vide en cas d'erreur
    
    VkResult err;
    
    // Create staging buffer
    VkDeviceSize imageSize = width * height * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    err = vkCreateBuffer(device_, &bufferInfo, nullptr, &stagingBuffer);
    if (err != VK_SUCCESS) return iconTexture;
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, stagingBuffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    err = vkAllocateMemory(device_, &allocInfo, nullptr, &stagingBufferMemory);
    if (err != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        return iconTexture;
    }
    
    vkBindBufferMemory(device_, stagingBuffer, stagingBufferMemory, 0);
    
    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(device_, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixelData, static_cast<size_t>(imageSize));
    vkUnmapMemory(device_, stagingBufferMemory);
    
    // Create image
    VkImage image;
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    err = vkCreateImage(device_, &imageInfo, nullptr, &image);
    if (err != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return iconTexture;
    }
    
    // Allocate image memory
    vkGetImageMemoryRequirements(device_, image, &memRequirements);
    
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    VkDeviceMemory imageMemory;
    err = vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory);
    if (err != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        vkDestroyBuffer(device_, stagingBuffer, nullptr);
        vkFreeMemory(device_, stagingBufferMemory, nullptr);
        return iconTexture;
    }
    
    vkBindImageMemory(device_, image, imageMemory, 0);
    
    // Transition image layout and copy buffer to image
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    // Transition to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );
    
    // Copy buffer to image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    
    vkCmdCopyBufferToImage(
        commandBuffer, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region
    );
    
    // Transition to SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );
    
    vkEndCommandBuffer(commandBuffer);
    
    // Submit and wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingBufferMemory, nullptr);
    
    // Create image view
    VkImageView imageView;
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    err = vkCreateImageView(device_, &viewInfo, nullptr, &imageView);
    if (err != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, imageMemory, nullptr);
        return iconTexture;
    }
    
    // Create descriptor set
    VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(sampler_, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    // Populate iconTexture with all the Vulkan handles
    iconTexture.textureId = (ImTextureID)descriptorSet;
    iconTexture.imageView = imageView;
    iconTexture.image = image;
    iconTexture.imageMemory = imageMemory;
    iconTexture.descriptorSet = descriptorSet;
    iconTexture.width = width;
    iconTexture.height = height;
    iconTexture.lastAccessFrame = currentFrame_;
    
    return iconTexture;
}

void IconManager::DestroyVulkanTexture(const IconTexture& texture) {
    if (texture.descriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(texture.descriptorSet);
    }
    
    if (texture.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, texture.imageView, nullptr);
    }
    
    if (texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, texture.image, nullptr);
    }
    
    if (texture.imageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, texture.imageMemory, nullptr);
    }
}

uint32_t IconManager::FindMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    return 0;
}

// ============================================================================
// CACHE MANAGEMENT
// ============================================================================

void IconManager::CleanupCacheIfNeeded() {
    currentFrame_++;
    
    if (currentFrame_ % 60 == 0) {  // Cleanup every 60 frames
        CleanupOldCacheEntries();
    }
}

void IconManager::InvalidateCache() {
    ClearCache();
}

void IconManager::CleanupOldCacheEntries() {
    std::vector<std::string> keysToRemove;
    
    for (const auto& [key, texture] : textureCache_) {
        if (currentFrame_ - texture.lastAccessFrame > cacheMaxAge_) {
            keysToRemove.push_back(key);
            DestroyVulkanTexture(texture);
        }
    }
    
    for (const auto& key : keysToRemove) {
        textureCache_.erase(key);
    }
}

void IconManager::ClearCache() {
    for (const auto& [key, texture] : textureCache_) {
        DestroyVulkanTexture(texture);
    }
    textureCache_.clear();
}

size_t IconManager::GetCacheSize() const {
    return textureCache_.size();
}

// ============================================================================
// ICON QUERIES
// ============================================================================

bool IconManager::HasIcon(const std::string& iconId) const {
    return icons_.find(iconId) != icons_.end();
}

std::vector<std::string> IconManager::GetLoadedIcons() const {
    std::vector<std::string> iconIds;
    iconIds.reserve(icons_.size());
    
    for (const auto& [id, icon] : icons_) {
        iconIds.push_back(id);
    }
    
    std::sort(iconIds.begin(), iconIds.end());
    return iconIds;
}

std::vector<std::string> IconManager::GetAvailableIcons() const {
    return GetLoadedIcons();
}

// ============================================================================
// METADATA ACCESS
// ============================================================================

IconMetadata IconManager::GetDefaultMetadata(const std::string& iconId) const {
    IconMetadata metadata;
    metadata.scheme = IconColorScheme::Bicolor;
    
    auto it = icons_.find(iconId);
    if (it != icons_.end()) {
        // Copy color zones from icon
        metadata.colorZones = it->second.colorZones;
    }
    
    return metadata;
}

const std::vector<ColorMapping>& IconManager::GetColorMappings(const std::string& iconId) const {
    static const std::vector<ColorMapping> empty;
    
    auto it = icons_.find(iconId);
    if (it != icons_.end()) {
        return it->second.colorMappings;
    }
    
    return empty;
}

const std::vector<ColorZone>& IconManager::GetColorZones(const std::string& iconId) const {
    static const std::vector<ColorZone> empty;
    
    auto it = icons_.find(iconId);
    if (it != icons_.end()) {
        return it->second.colorZones;
    }
    
    return empty;
}

} // namespace VectorGraphics
// #include <UI/IconManager.h>
// #include <DesignSystem/DesignSystem.h>
// #include <imgui_impl_vulkan.h>
// #include <filesystem>
// #include <sstream>
// #include <cstring>
// #include <algorithm>
// #include <cmath>
// #include <set>

// namespace UI {

// IconManager& IconManager::Instance() {
//     static IconManager instance;
//     return instance;
// }

// IconManager::~IconManager() {
//     Shutdown();
// }

// void IconManager::Initialize(
//     VkDevice device,
//     VkPhysicalDevice physicalDevice,
//     VkQueue queue,
//     VkCommandPool commandPool,
//     VkDescriptorPool descriptorPool)
// {
//     device_ = device;
//     physicalDevice_ = physicalDevice;
//     queue_ = queue;
//     commandPool_ = commandPool;
//     descriptorPool_ = descriptorPool;
// }

// void IconManager::Shutdown() {
//     ClearCache();
    
//     for (auto& [id, icon] : icons_) {
//         if (icon.svgImage) {
//             nsvgDelete(icon.svgImage);
//             icon.svgImage = nullptr;
//         }
//     }
//     icons_.clear();
// }

// void IconManager::LoadIconsFromDirectory(const std::string& directory) {
//     namespace fs = std::filesystem;
    
//     if (!fs::exists(directory) || !fs::is_directory(directory)) {
//         return;
//     }
    
//     for (const auto& entry : fs::recursive_directory_iterator(directory)) {
//         if (entry.is_regular_file() && entry.path().extension() == ".svg") {
//             std::string id = entry.path().stem().string();
//             std::string path = entry.path().string();
//             LoadIcon(id, path);
//         }
//     }
// }

// bool IconManager::LoadIcon(const std::string& id, const std::string& filepath) {
//     VectorIcon icon;
//     if (!ParseSVG(filepath, icon)) {
//         return false;
//     }
    
//     icons_[id] = icon;
//     return true;
// }

// void IconManager::SetIconMetadata(const std::string& id, const IconMetadata& metadata) {
//     auto it = icons_.find(id);
//     if (it != icons_.end()) {
//         it->second.metadata = metadata;
//     }
// }

// IconMetadata* IconManager::GetIconMetadata(const std::string& id) {
//     auto it = icons_.find(id);
//     if (it != icons_.end()) {
//         return &it->second.metadata;
//     }
//     return nullptr;
// }

// int IconManager::GetColorZoneCount(const std::string& id) const {
//     auto it = icons_.find(id);
//     if (it != icons_.end()) {
//         return static_cast<int>(it->second.metadata.colorZones.size());
//     }
//     return 0;
// }

// bool IconManager::ParseSVG(const std::string& filepath, VectorIcon& icon) {
//     icon.svgImage = nsvgParseFromFile(filepath.c_str(), "px", 96.0f);
//     if (!icon.svgImage) return false;
    
//     icon.size = ImVec2(icon.svgImage->width, icon.svgImage->height);
    
//     // Auto-détecter les zones de couleur
//     DetectColorZones(icon);
    
//     return true;
// }

// void IconManager::DetectColorZones(VectorIcon& icon) {
//     if (!icon.svgImage) return;
    
//     // Collecter toutes les couleurs uniques (format ABGR)
//     std::set<uint32_t> uniqueColors;
    
//     for (NSVGshape* shape = icon.svgImage->shapes; shape; shape = shape->next) {
//         // Fill
//         if (shape->fill.type == NSVG_PAINT_COLOR) {
//             uniqueColors.insert(shape->fill.color);
//         }
        
//         // Stroke
//         if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth > 0.001f) {
//             uniqueColors.insert(shape->stroke.color);
//         }
//     }
    
//     // Créer une ColorZone pour chaque couleur unique
//     icon.metadata.colorZones.clear();
//     for (uint32_t color : uniqueColors) {
//         icon.metadata.colorZones.push_back(ColorZone(color));
//     }
// }

// void IconManager::RenderIcon(const std::string& id, float size) {
//     auto it = icons_.find(id);
//     if (it == icons_.end()) {
//         return;
//     }
    
//     const VectorIcon& icon = it->second;
//     const IconMetadata& metadata = icon.metadata;
    
//     // MODE ORIGINAL : Couleurs du SVG, pas de remplacement
//     if (metadata.scheme == IconColorScheme::Original) {
//         std::map<uint32_t, uint32_t> emptyMap;  // Aucun remplacement
//         std::string cacheKey = CreateCacheKey(id, size, metadata);
        
//         auto cacheIt = textureCache_.find(cacheKey);
//         if (cacheIt != textureCache_.end()) {
//             ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
//             return;
//         }
        
//         int width, height;
//         unsigned char* pixels = RasterizeWithColors(icon, size, emptyMap, width, height);
//         if (!pixels) return;
        
//         VulkanTexture tex = CreateTexture(pixels, width, height);
//         delete[] pixels;
        
//         textureCache_[cacheKey] = tex;
//         ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
//         return;
//     }
    
//     // MODE BICOLOR : Utiliser tokens design system
//     if (metadata.scheme == IconColorScheme::Bicolor) {
//         auto& ds = DesignSystem::DesignSystem::Instance();
//         ImVec4 primaryColor = ds.GetColor("semantic.icon.color.primary");
//         ImVec4 secondaryColor = ds.GetColor("semantic.icon.color.secondary");
        
//         RenderIcon(id, size, primaryColor, secondaryColor);
//         return;
//     }
    
//     // MODE MULTICOLOR : Utiliser couleurs configurées
//     if (metadata.scheme == IconColorScheme::Multicolor) {
//         std::map<uint32_t, uint32_t> colorReplacements;
        
//         for (const auto& zone : metadata.colorZones) {
//             colorReplacements[zone.originalColor] = ImVec4ToABGR(zone.customColor);
//         }
        
//         std::string cacheKey = CreateCacheKey(id, size, metadata);
        
//         auto cacheIt = textureCache_.find(cacheKey);
//         if (cacheIt != textureCache_.end()) {
//             ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
//             return;
//         }
        
//         int width, height;
//         unsigned char* pixels = RasterizeWithColors(icon, size, colorReplacements, width, height);
//         if (!pixels) return;
        
//         VulkanTexture tex = CreateTexture(pixels, width, height);
//         delete[] pixels;
        
//         textureCache_[cacheKey] = tex;
//         ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
//     }
// }

// void IconManager::RenderIcon(
//     const std::string& id,
//     float size,
//     const std::string& primaryToken,
//     const std::string& secondaryToken)
// {
//     auto& ds = DesignSystem::DesignSystem::Instance();
//     ImVec4 primaryColor = ds.GetColor(primaryToken);
//     ImVec4 secondaryColor = ds.GetColor(secondaryToken);
    
//     RenderIcon(id, size, primaryColor, secondaryColor);
// }

// void IconManager::RenderIcon(
//     const std::string& id,
//     float size,
//     const ImVec4& primaryColor,
//     const ImVec4& secondaryColor)
// {
//     auto it = icons_.find(id);
//     if (it == icons_.end()) {
//         return;
//     }
    
//     const VectorIcon& icon = it->second;
//     const IconMetadata& metadata = icon.metadata;
    
//     // Construire la map de remplacement selon les zones configurées
//     std::map<uint32_t, uint32_t> colorReplacements;
//     for (const auto& zone : metadata.colorZones) {
//         uint32_t newColor = zone.usePrimary ? ImVec4ToABGR(primaryColor) : ImVec4ToABGR(secondaryColor);
//         colorReplacements[zone.originalColor] = newColor;
//     }
    
//     std::string cacheKey = CreateCacheKey(id, size, metadata, &primaryColor, &secondaryColor);
    
//     auto cacheIt = textureCache_.find(cacheKey);
//     if (cacheIt != textureCache_.end()) {
//         ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
//         return;
//     }
    
//     int width, height;
//     unsigned char* pixels = RasterizeWithColors(icon, size, colorReplacements, width, height);
//     if (!pixels) return;
    
//     VulkanTexture tex = CreateTexture(pixels, width, height);
//     delete[] pixels;
    
//     textureCache_[cacheKey] = tex;
//     ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
// }

// unsigned char* IconManager::RasterizeWithColors(
//     const VectorIcon& icon,
//     float size,
//     const std::map<uint32_t, uint32_t>& colorReplacements,
//     int& outWidth,
//     int& outHeight)
// {
//     NSVGrasterizer* rast = nsvgCreateRasterizer();
//     float scale = size / std::max(icon.size.x, icon.size.y);
//     outWidth = static_cast<int>(icon.size.x * scale);
//     outHeight = static_cast<int>(icon.size.y * scale);

//     unsigned char* pixels = new unsigned char[outWidth * outHeight * 4];

//     // Sauvegarder et modifier les couleurs
//     struct ShapeBackup {
//         unsigned char fillType;
//         uint32_t fillColor;
//         unsigned char strokeType;
//         uint32_t strokeColor;
//     };
//     std::vector<ShapeBackup> originalStates;
    
//     for (NSVGshape* shape = icon.svgImage->shapes; shape; shape = shape->next) {
//         ShapeBackup backup;
//         backup.fillType = shape->fill.type;
//         backup.fillColor = shape->fill.color;
//         backup.strokeType = shape->stroke.type;
//         backup.strokeColor = shape->stroke.color;
//         originalStates.push_back(backup);
        
//         // Remplacer les couleurs selon la map
//         if (shape->fill.type == NSVG_PAINT_COLOR) {
//             auto it = colorReplacements.find(shape->fill.color);
//             if (it != colorReplacements.end()) {
//                 shape->fill.color = it->second;
//             }
//         }
        
//         if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth > 0.001f) {
//             auto it = colorReplacements.find(shape->stroke.color);
//             if (it != colorReplacements.end()) {
//                 shape->stroke.color = it->second;
//             }
//         }
//     }

//     // Rasteriser
//     nsvgRasterize(rast, icon.svgImage, 0, 0, scale, pixels, outWidth, outHeight, outWidth * 4);
    
//     // Restaurer les états originaux
//     if (!originalStates.empty()) {
//         int idx = 0;
//         for (NSVGshape* shape = icon.svgImage->shapes; shape && idx < originalStates.size(); shape = shape->next, idx++) {
//             shape->fill.type = originalStates[idx].fillType;
//             shape->fill.color = originalStates[idx].fillColor;
//             shape->stroke.type = originalStates[idx].strokeType;
//             shape->stroke.color = originalStates[idx].strokeColor;
//         }
//     }
    
//     nsvgDeleteRasterizer(rast);
//     return pixels;
// }

// std::string IconManager::CreateCacheKey(
//     const std::string& id,
//     float size,
//     const IconMetadata& metadata,
//     const ImVec4* primaryColor,
//     const ImVec4* secondaryColor) const
// {
//     std::ostringstream oss;
//     oss << id << "_" << static_cast<int>(size) << "_" << static_cast<int>(metadata.scheme);
    
//     if (metadata.scheme == IconColorScheme::Bicolor && primaryColor && secondaryColor) {
//         oss << "_P" << static_cast<int>(primaryColor->x * 255)
//             << "_" << static_cast<int>(primaryColor->y * 255)
//             << "_" << static_cast<int>(primaryColor->z * 255)
//             << "_S" << static_cast<int>(secondaryColor->x * 255)
//             << "_" << static_cast<int>(secondaryColor->y * 255)
//             << "_" << static_cast<int>(secondaryColor->z * 255);
//     } else if (metadata.scheme == IconColorScheme::Multicolor) {
//         for (const auto& zone : metadata.colorZones) {
//             oss << "_C" << zone.originalColor
//                 << "_" << static_cast<int>(zone.customColor.x * 255)
//                 << "_" << static_cast<int>(zone.customColor.y * 255)
//                 << "_" << static_cast<int>(zone.customColor.z * 255);
//         }
//     }
    
//     return oss.str();
// }

// uint32_t IconManager::ImVec4ToABGR(const ImVec4& color) const {
//     return ((uint32_t)(color.w * 255) << 24) | 
//            ((uint32_t)(color.z * 255) << 16) | 
//            ((uint32_t)(color.y * 255) << 8) | 
//            ((uint32_t)(color.x * 255));
// }

// void IconManager::InvalidateCache() {
//     cacheInvalidated_ = true;
// }

// void IconManager::SafeClearCache() {
//     if (device_ != VK_NULL_HANDLE) {
//         vkDeviceWaitIdle(device_);
//     }
//     ClearCache();
// }

// void IconManager::CleanupCacheIfNeeded() {
//     if (cacheInvalidated_) {
//         SafeClearCache();
//         cacheInvalidated_ = false;
//     }
// }

// void IconManager::ClearCache() {
//     for (auto& [key, tex] : textureCache_) {
//         DestroyTexture(tex);
//     }
//     textureCache_.clear();
// }

// std::vector<std::string> IconManager::GetLoadedIcons() const {
//     std::vector<std::string> result;
//     for (const auto& [id, _] : icons_) {
//         result.push_back(id);
//     }
//     return result;
// }

// // ========== Vulkan Helpers ==========

// VulkanTexture IconManager::CreateTexture(const unsigned char* pixels, int width, int height) {
//     VulkanTexture tex{};
//     tex.width = width;
//     tex.height = height;
    
//     VkDeviceSize size = width * height * 4;
    
//     VkBuffer staging;
//     VkDeviceMemory stagingMem;
//     CreateBuffer(
//         size,
//         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//         staging,
//         stagingMem
//     );
    
//     void* data;
//     vkMapMemory(device_, stagingMem, 0, size, 0, &data);
//     memcpy(data, pixels, size);
//     vkUnmapMemory(device_, stagingMem);
    
//     CreateImage(
//         width, height,
//         VK_FORMAT_R8G8B8A8_UNORM,
//         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
//         tex.image,
//         tex.memory
//     );
    
//     TransitionImage(tex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
//     CopyBufferToImage(staging, tex.image, width, height);
//     TransitionImage(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
//     vkDestroyBuffer(device_, staging, nullptr);
//     vkFreeMemory(device_, stagingMem, nullptr);
    
//     tex.view = CreateImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM);
//     tex.sampler = CreateSampler();
    
//     tex.descriptor = ImGui_ImplVulkan_AddTexture(
//         tex.sampler,
//         tex.view,
//         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
//     );
    
//     return tex;
// }

// void IconManager::DestroyTexture(VulkanTexture& tex) {
//     if (tex.descriptor)
//         ImGui_ImplVulkan_RemoveTexture(tex.descriptor);
//     if (tex.sampler)
//         vkDestroySampler(device_, tex.sampler, nullptr);
//     if (tex.view)
//         vkDestroyImageView(device_, tex.view, nullptr);
//     if (tex.image)
//         vkDestroyImage(device_, tex.image, nullptr);
//     if (tex.memory)
//         vkFreeMemory(device_, tex.memory, nullptr);
    
//     tex = {};
// }

// uint32_t IconManager::FindMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
//     VkPhysicalDeviceMemoryProperties mem;
//     vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    
//     for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
//         if ((filter & (1 << i)) &&
//             (mem.memoryTypes[i].propertyFlags & props) == props) {
//             return i;
//         }
//     }
    
//     return 0;
// }

// void IconManager::CreateBuffer(
//     VkDeviceSize size,
//     VkBufferUsageFlags usage,
//     VkMemoryPropertyFlags props,
//     VkBuffer& buffer,
//     VkDeviceMemory& memory)
// {
//     VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
//     info.size = size;
//     info.usage = usage;
    
//     vkCreateBuffer(device_, &info, nullptr, &buffer);
    
//     VkMemoryRequirements req;
//     vkGetBufferMemoryRequirements(device_, buffer, &req);
    
//     VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
//     alloc.allocationSize = req.size;
//     alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    
//     vkAllocateMemory(device_, &alloc, nullptr, &memory);
//     vkBindBufferMemory(device_, buffer, memory, 0);
// }

// void IconManager::CreateImage(
//     int width, int height,
//     VkFormat format,
//     VkImageUsageFlags usage,
//     VkImage& image,
//     VkDeviceMemory& memory)
// {
//     VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
//     info.imageType = VK_IMAGE_TYPE_2D;
//     info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
//     info.mipLevels = 1;
//     info.arrayLayers = 1;
//     info.format = format;
//     info.tiling = VK_IMAGE_TILING_OPTIMAL;
//     info.usage = usage;
//     info.samples = VK_SAMPLE_COUNT_1_BIT;
    
//     vkCreateImage(device_, &info, nullptr, &image);
    
//     VkMemoryRequirements req;
//     vkGetImageMemoryRequirements(device_, image, &req);
    
//     VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
//     alloc.allocationSize = req.size;
//     alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
//     vkAllocateMemory(device_, &alloc, nullptr, &memory);
//     vkBindImageMemory(device_, image, memory, 0);
// }

// VkImageView IconManager::CreateImageView(VkImage image, VkFormat format) {
//     VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
//     info.image = image;
//     info.viewType = VK_IMAGE_VIEW_TYPE_2D;
//     info.format = format;
//     info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     info.subresourceRange.levelCount = 1;
//     info.subresourceRange.layerCount = 1;
    
//     VkImageView view;
//     vkCreateImageView(device_, &info, nullptr, &view);
//     return view;
// }

// VkSampler IconManager::CreateSampler() {
//     VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
//     info.magFilter = VK_FILTER_LINEAR;
//     info.minFilter = VK_FILTER_LINEAR;
//     info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//     info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
//     info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
//     VkSampler sampler;
//     vkCreateSampler(device_, &info, nullptr, &sampler);
//     return sampler;
// }

// VkCommandBuffer IconManager::BeginSingleTimeCommands() {
//     VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
//     alloc.commandPool = commandPool_;
//     alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//     alloc.commandBufferCount = 1;
    
//     VkCommandBuffer cmd;
//     vkAllocateCommandBuffers(device_, &alloc, &cmd);
    
//     VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
//     begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//     vkBeginCommandBuffer(cmd, &begin);
    
//     return cmd;
// }

// void IconManager::EndSingleTimeCommands(VkCommandBuffer cmd) {
//     vkEndCommandBuffer(cmd);
    
//     VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//     submit.commandBufferCount = 1;
//     submit.pCommandBuffers = &cmd;
    
//     vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
//     vkQueueWaitIdle(queue_);
    
//     vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
// }

// void IconManager::TransitionImage(VkImage img, VkImageLayout oldL, VkImageLayout newL) {
//     VkCommandBuffer cmd = BeginSingleTimeCommands();
    
//     VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
//     barrier.oldLayout = oldL;
//     barrier.newLayout = newL;
//     barrier.image = img;
//     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     barrier.subresourceRange.levelCount = 1;
//     barrier.subresourceRange.layerCount = 1;
    
//     vkCmdPipelineBarrier(
//         cmd,
//         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
//         VK_PIPELINE_STAGE_TRANSFER_BIT,
//         0,
//         0, nullptr,
//         0, nullptr,
//         1, &barrier
//     );
    
//     EndSingleTimeCommands(cmd);
// }

// void IconManager::CopyBufferToImage(VkBuffer buf, VkImage img, int w, int h) {
//     VkCommandBuffer cmd = BeginSingleTimeCommands();
    
//     VkBufferImageCopy region{};
//     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     region.imageSubresource.layerCount = 1;
//     region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    
//     vkCmdCopyBufferToImage(
//         cmd,
//         buf,
//         img,
//         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//         1,
//         &region
//     );
    
//     EndSingleTimeCommands(cmd);
// }

// } // namespace UI








#include <UI/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_impl_vulkan.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <set>

namespace UI {

IconManager& IconManager::Instance() {
    static IconManager instance;
    return instance;
}

IconManager::~IconManager() {
    Shutdown();
}

void IconManager::Initialize(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkQueue queue,
    VkCommandPool commandPool,
    VkDescriptorPool descriptorPool)
{
    device_ = device;
    physicalDevice_ = physicalDevice;
    queue_ = queue;
    commandPool_ = commandPool;
    descriptorPool_ = descriptorPool;
}

void IconManager::Shutdown() {
    ClearCache();
    
    for (auto& [id, icon] : icons_) {
        if (icon.svgImage) {
            nsvgDelete(icon.svgImage);
            icon.svgImage = nullptr;
        }
    }
    icons_.clear();
}

void IconManager::LoadIconsFromDirectory(const std::string& directory) {
    namespace fs = std::filesystem;
    
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        return;
    }
    
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".svg") {
            std::string id = entry.path().stem().string();
            std::string path = entry.path().string();
            LoadIcon(id, path);
        }
    }
}

bool IconManager::LoadIcon(const std::string& id, const std::string& filepath) {
    VectorIcon icon;
    if (!ParseSVG(filepath, icon)) {
        return false;
    }
    
    icons_[id] = icon;
    return true;
}

void IconManager::SetIconMetadata(const std::string& id, const IconMetadata& metadata) {
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        it->second.metadata = metadata;
    }
}

IconMetadata* IconManager::GetIconMetadata(const std::string& id) {
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        return &it->second.metadata;
    }
    return nullptr;
}

IconMetadata IconManager::GetIconMetadataCopy(const std::string& id) const {
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        // Return a COPY - modifications won't affect the global template
        return it->second.metadata;
    }
    return IconMetadata();
}

int IconManager::GetColorZoneCount(const std::string& id) const {
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        return static_cast<int>(it->second.metadata.colorZones.size());
    }
    return 0;
}

bool IconManager::ParseSVG(const std::string& filepath, VectorIcon& icon) {
    icon.svgImage = nsvgParseFromFile(filepath.c_str(), "px", 96.0f);
    if (!icon.svgImage) return false;
    
    icon.size = ImVec2(icon.svgImage->width, icon.svgImage->height);
    
    // Auto-detect color zones (enhanced with alpha check)
    DetectColorZones(icon);
    
    // Auto-configure from SVG IDs (if present)
    AutoConfigureFromSVGIds(icon);
    
    return true;
}

void IconManager::DetectColorZones(VectorIcon& icon) {
    if (!icon.svgImage) return;
    
    // Collect all unique colors (ABGR format) with sufficient opacity
    std::set<uint32_t> uniqueColors;
    
    for (NSVGshape* shape = icon.svgImage->shapes; shape; shape = shape->next) {
        // Fill color
        if (shape->fill.type == NSVG_PAINT_COLOR) {
            uint32_t color = shape->fill.color;
            uint8_t alpha = (color >> 24) & 0xFF;
            
            // CRITICAL FIX: Ignore nearly transparent colors (alpha < 10)
            // This fixes logo_carto.svg which has fill-opacity:0
            if (alpha >= 10) {
                uniqueColors.insert(color);
            }
        }
        
        // Stroke color
        if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth > 0.001f) {
            uint32_t color = shape->stroke.color;
            uint8_t alpha = (color >> 24) & 0xFF;
            
            if (alpha >= 10) {
                uniqueColors.insert(color);
            }
        }
    }
    
    // Create a ColorZone for each unique color
    icon.metadata.colorZones.clear();
    for (uint32_t color : uniqueColors) {
        icon.metadata.colorZones.push_back(ColorZone(color));
    }
}

void IconManager::AutoConfigureFromSVGIds(VectorIcon& icon) {
    // Check if SVG elements have IDs like "primary", "secondary"
    // This allows auto-configuration without code
    
    for (NSVGshape* shape = icon.svgImage->shapes; shape; shape = shape->next) {
        if (shape->id) {
            std::string id(shape->id);
            std::transform(id.begin(), id.end(), id.begin(), ::tolower);
            
            // Find the color of this shape
            uint32_t shapeColor = 0;
            if (shape->fill.type == NSVG_PAINT_COLOR) {
                shapeColor = shape->fill.color;
            } else if (shape->stroke.type == NSVG_PAINT_COLOR) {
                shapeColor = shape->stroke.color;
            }
            
            // Find corresponding color zone and configure it
            for (auto& zone : icon.metadata.colorZones) {
                if (zone.originalColor == shapeColor) {
                    if (id.find("primary") != std::string::npos) {
                        zone.usePrimary = true;
                        zone.svgId = shape->id;
                        
                        // If we found IDs, switch to bicolor mode
                        if (icon.metadata.scheme == IconColorScheme::Original) {
                            icon.metadata.scheme = IconColorScheme::Bicolor;
                        }
                    } else if (id.find("secondary") != std::string::npos) {
                        zone.usePrimary = false;
                        zone.svgId = shape->id;
                        
                        if (icon.metadata.scheme == IconColorScheme::Original) {
                            icon.metadata.scheme = IconColorScheme::Bicolor;
                        }
                    }
                }
            }
        }
    }
}

void IconManager::RenderIcon(const std::string& id, float size) {
    auto it = icons_.find(id);
    if (it == icons_.end()) {
        return;
    }
    
    const VectorIcon& icon = it->second;
    const IconMetadata& metadata = icon.metadata;  // Use global metadata
    
    // Delegate to the metadata-based render function
    RenderIcon(id, size, metadata);
}

void IconManager::RenderIcon(const std::string& id, float size, const IconMetadata& localMetadata) {
    auto it = icons_.find(id);
    if (it == icons_.end()) {
        return;
    }
    
    const VectorIcon& icon = it->second;
    
    // MODE ORIGINAL: Original SVG colors, no replacements
    if (localMetadata.scheme == IconColorScheme::Original) {
        std::map<uint32_t, uint32_t> emptyMap;  // No color replacements
        std::string cacheKey = CreateCacheKey(id, size, localMetadata);
        
        auto cacheIt = textureCache_.find(cacheKey);
        if (cacheIt != textureCache_.end()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
            return;
        }
        
        int width, height;
        unsigned char* pixels = RasterizeWithColors(icon, size, emptyMap, width, height);
        if (!pixels) return;
        
        VulkanTexture tex = CreateTexture(pixels, width, height);
        delete[] pixels;
        
        textureCache_[cacheKey] = tex;
        ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
        return;
    }
    
    // MODE BICOLOR: Use design system tokens
    if (localMetadata.scheme == IconColorScheme::Bicolor) {
        auto& ds = DesignSystem::DesignSystem::Instance();
        ImVec4 primaryColor = ds.GetColor("semantic.icon.color.primary");
        ImVec4 secondaryColor = ds.GetColor("semantic.icon.color.secondary");
        
        // Build color replacement map
        std::map<uint32_t, uint32_t> colorReplacements;
        for (const auto& zone : localMetadata.colorZones) {
            uint32_t newColor = zone.usePrimary ? ImVec4ToABGR(primaryColor) : ImVec4ToABGR(secondaryColor);
            colorReplacements[zone.originalColor] = newColor;
        }
        
        std::string cacheKey = CreateCacheKey(id, size, localMetadata, &primaryColor, &secondaryColor);
        
        auto cacheIt = textureCache_.find(cacheKey);
        if (cacheIt != textureCache_.end()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
            return;
        }
        
        int width, height;
        unsigned char* pixels = RasterizeWithColors(icon, size, colorReplacements, width, height);
        if (!pixels) return;
        
        VulkanTexture tex = CreateTexture(pixels, width, height);
        delete[] pixels;
        
        textureCache_[cacheKey] = tex;
        ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
        return;
    }
    
    // MODE MULTICOLOR: Use custom colors from metadata
    if (localMetadata.scheme == IconColorScheme::Multicolor) {
        std::map<uint32_t, uint32_t> colorReplacements;
        
        for (const auto& zone : localMetadata.colorZones) {
            colorReplacements[zone.originalColor] = ImVec4ToABGR(zone.customColor);
        }
        
        std::string cacheKey = CreateCacheKey(id, size, localMetadata);
        
        auto cacheIt = textureCache_.find(cacheKey);
        if (cacheIt != textureCache_.end()) {
            ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
            return;
        }
        
        int width, height;
        unsigned char* pixels = RasterizeWithColors(icon, size, colorReplacements, width, height);
        if (!pixels) return;
        
        VulkanTexture tex = CreateTexture(pixels, width, height);
        delete[] pixels;
        
        textureCache_[cacheKey] = tex;
        ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
    }
}

void IconManager::RenderIcon(
    const std::string& id,
    float size,
    const std::string& primaryToken,
    const std::string& secondaryToken)
{
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec4 primaryColor = ds.GetColor(primaryToken);
    ImVec4 secondaryColor = ds.GetColor(secondaryToken);
    
    RenderIcon(id, size, primaryColor, secondaryColor);
}

void IconManager::RenderIcon(
    const std::string& id,
    float size,
    const ImVec4& primaryColor,
    const ImVec4& secondaryColor)
{
    auto it = icons_.find(id);
    if (it == icons_.end()) {
        return;
    }
    
    const VectorIcon& icon = it->second;
    const IconMetadata& metadata = icon.metadata;
    
    // Build color replacement map based on zones
    std::map<uint32_t, uint32_t> colorReplacements;
    for (const auto& zone : metadata.colorZones) {
        uint32_t newColor = zone.usePrimary ? ImVec4ToABGR(primaryColor) : ImVec4ToABGR(secondaryColor);
        colorReplacements[zone.originalColor] = newColor;
    }
    
    std::string cacheKey = CreateCacheKey(id, size, metadata, &primaryColor, &secondaryColor);
    
    auto cacheIt = textureCache_.find(cacheKey);
    if (cacheIt != textureCache_.end()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
        return;
    }
    
    int width, height;
    unsigned char* pixels = RasterizeWithColors(icon, size, colorReplacements, width, height);
    if (!pixels) return;
    
    VulkanTexture tex = CreateTexture(pixels, width, height);
    delete[] pixels;
    
    textureCache_[cacheKey] = tex;
    ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
}

unsigned char* IconManager::RasterizeWithColors(
    const VectorIcon& icon,
    float size,
    const std::map<uint32_t, uint32_t>& colorReplacements,
    int& outWidth,
    int& outHeight)
{
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    float scale = size / std::max(icon.size.x, icon.size.y);
    outWidth = static_cast<int>(icon.size.x * scale);
    outHeight = static_cast<int>(icon.size.y * scale);

    unsigned char* pixels = new unsigned char[outWidth * outHeight * 4];

    // Backup and modify colors
    struct ShapeBackup {
        unsigned char fillType;
        uint32_t fillColor;
        unsigned char strokeType;
        uint32_t strokeColor;
    };
    std::vector<ShapeBackup> originalStates;
    
    for (NSVGshape* shape = icon.svgImage->shapes; shape; shape = shape->next) {
        ShapeBackup backup;
        backup.fillType = shape->fill.type;
        backup.fillColor = shape->fill.color;
        backup.strokeType = shape->stroke.type;
        backup.strokeColor = shape->stroke.color;
        originalStates.push_back(backup);
        
        // Replace colors according to map
        if (shape->fill.type == NSVG_PAINT_COLOR) {
            auto it = colorReplacements.find(shape->fill.color);
            if (it != colorReplacements.end()) {
                shape->fill.color = it->second;
            }
        }
        
        if (shape->stroke.type == NSVG_PAINT_COLOR && shape->strokeWidth > 0.001f) {
            auto it = colorReplacements.find(shape->stroke.color);
            if (it != colorReplacements.end()) {
                shape->stroke.color = it->second;
            }
        }
    }

    // Rasterize
    nsvgRasterize(rast, icon.svgImage, 0, 0, scale, pixels, outWidth, outHeight, outWidth * 4);
    
    // Restore original states
    if (!originalStates.empty()) {
        int idx = 0;
        for (NSVGshape* shape = icon.svgImage->shapes; shape && idx < originalStates.size(); shape = shape->next, idx++) {
            shape->fill.type = originalStates[idx].fillType;
            shape->fill.color = originalStates[idx].fillColor;
            shape->stroke.type = originalStates[idx].strokeType;
            shape->stroke.color = originalStates[idx].strokeColor;
        }
    }
    
    nsvgDeleteRasterizer(rast);
    return pixels;
}

std::string IconManager::CreateCacheKey(
    const std::string& id,
    float size,
    const IconMetadata& metadata,
    const ImVec4* primaryColor,
    const ImVec4* secondaryColor) const
{
    std::ostringstream oss;
    oss << id << "_" << static_cast<int>(size) << "_" << static_cast<int>(metadata.scheme);
    
    if (metadata.scheme == IconColorScheme::Bicolor && primaryColor && secondaryColor) {
        oss << "_P" << static_cast<int>(primaryColor->x * 255)
            << "_" << static_cast<int>(primaryColor->y * 255)
            << "_" << static_cast<int>(primaryColor->z * 255)
            << "_S" << static_cast<int>(secondaryColor->x * 255)
            << "_" << static_cast<int>(secondaryColor->y * 255)
            << "_" << static_cast<int>(secondaryColor->z * 255);
    } else if (metadata.scheme == IconColorScheme::Multicolor) {
        for (const auto& zone : metadata.colorZones) {
            oss << "_C" << zone.originalColor
                << "_" << static_cast<int>(zone.customColor.x * 255)
                << "_" << static_cast<int>(zone.customColor.y * 255)
                << "_" << static_cast<int>(zone.customColor.z * 255);
        }
    }
    
    return oss.str();
}

uint32_t IconManager::ImVec4ToABGR(const ImVec4& color) const {
    return ((uint32_t)(color.w * 255) << 24) | 
           ((uint32_t)(color.z * 255) << 16) | 
           ((uint32_t)(color.y * 255) << 8) | 
           ((uint32_t)(color.x * 255));
}

void IconManager::InvalidateCache() {
    cacheInvalidated_ = true;
}

void IconManager::SafeClearCache() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    ClearCache();
}

void IconManager::CleanupCacheIfNeeded() {
    if (cacheInvalidated_) {
        SafeClearCache();
        cacheInvalidated_ = false;
    }
}

void IconManager::ClearCache() {
    for (auto& [key, tex] : textureCache_) {
        DestroyTexture(tex);
    }
    textureCache_.clear();
}

std::vector<std::string> IconManager::GetLoadedIcons() const {
    std::vector<std::string> result;
    for (const auto& [id, _] : icons_) {
        result.push_back(id);
    }
    return result;
}

// ========== Vulkan Helpers ==========

VulkanTexture IconManager::CreateTexture(const unsigned char* pixels, int width, int height) {
    VulkanTexture tex{};
    tex.width = width;
    tex.height = height;
    
    VkDeviceSize size = width * height * 4;
    
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    CreateBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging,
        stagingMem
    );
    
    void* data;
    vkMapMemory(device_, stagingMem, 0, size, 0, &data);
    memcpy(data, pixels, size);
    vkUnmapMemory(device_, stagingMem);
    
    CreateImage(
        width, height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        tex.image,
        tex.memory
    );
    
    TransitionImage(tex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(staging, tex.image, width, height);
    TransitionImage(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);
    
    tex.view = CreateImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM);
    tex.sampler = CreateSampler();
    
    tex.descriptor = ImGui_ImplVulkan_AddTexture(
        tex.sampler,
        tex.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    
    return tex;
}

void IconManager::DestroyTexture(VulkanTexture& tex) {
    if (tex.descriptor)
        ImGui_ImplVulkan_RemoveTexture(tex.descriptor);
    if (tex.sampler)
        vkDestroySampler(device_, tex.sampler, nullptr);
    if (tex.view)
        vkDestroyImageView(device_, tex.view, nullptr);
    if (tex.image)
        vkDestroyImage(device_, tex.image, nullptr);
    if (tex.memory)
        vkFreeMemory(device_, tex.memory, nullptr);
    
    tex = {};
}

uint32_t IconManager::FindMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((filter & (1 << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    
    return 0;
}

void IconManager::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props,
    VkBuffer& buffer,
    VkDeviceMemory& memory)
{
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    
    vkCreateBuffer(device_, &info, nullptr, &buffer);
    
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, buffer, &req);
    
    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    
    vkAllocateMemory(device_, &alloc, nullptr, &memory);
    vkBindBufferMemory(device_, buffer, memory, 0);
}

void IconManager::CreateImage(
    int width, int height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage& image,
    VkDeviceMemory& memory)
{
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = format;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    
    vkCreateImage(device_, &info, nullptr, &image);
    
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, image, &req);
    
    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(device_, &alloc, nullptr, &memory);
    vkBindImageMemory(device_, image, memory, 0);
}

VkImageView IconManager::CreateImageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.layerCount = 1;
    
    VkImageView view;
    vkCreateImageView(device_, &info, nullptr, &view);
    return view;
}

VkSampler IconManager::CreateSampler() {
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    VkSampler sampler;
    vkCreateSampler(device_, &info, nullptr, &sampler);
    return sampler;
}

VkCommandBuffer IconManager::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &alloc, &cmd);
    
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    
    return cmd;
}

void IconManager::EndSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

void IconManager::TransitionImage(VkImage img, VkImageLayout oldL, VkImageLayout newL) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldL;
    barrier.newLayout = newL;
    barrier.image = img;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    EndSingleTimeCommands(cmd);
}

void IconManager::CopyBufferToImage(VkBuffer buf, VkImage img, int w, int h) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    
    vkCmdCopyBufferToImage(
        cmd,
        buf,
        img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    
    EndSingleTimeCommands(cmd);
}

} // namespace UI
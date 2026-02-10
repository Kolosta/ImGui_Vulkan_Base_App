#include <VectorGraphics/IconManager.h>
#include <VectorGraphics/IconMetadata.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_impl_vulkan.h>
#include <generated/IconData.h>

#include <sstream>
#include <cstring>
#include <algorithm>
#include <set>
#include <regex>
#include <iomanip>

// Forward declarations for resvg C API
extern "C" {
    typedef struct ResvgOptions ResvgOptions;
    typedef struct ResvgRenderTree ResvgRenderTree;
    
    typedef struct {
        float width;
        float height;
    } ResvgSize;
    
    typedef struct {
        float a, b, c, d, e, f;
    } ResvgTransform;
    
    typedef enum {
        RESVG_FIT_TO_TYPE_ORIGINAL = 0,
        RESVG_FIT_TO_TYPE_WIDTH,
        RESVG_FIT_TO_TYPE_HEIGHT,
        RESVG_FIT_TO_TYPE_ZOOM,
    } ResvgFitToType;
    
    typedef struct {
        ResvgFitToType fit_type;
        float value;
    } ResvgFitTo;
    
    ResvgOptions* resvg_options_create();
    void resvg_options_load_system_fonts(ResvgOptions* options);
    void resvg_options_destroy(ResvgOptions* options);
    
    ResvgRenderTree* resvg_parse_tree_from_data(
        const char* data,
        unsigned int length,
        const ResvgOptions* options
    );
    
    ResvgSize resvg_get_image_size(const ResvgRenderTree* tree);
    
    ResvgTransform resvg_transform_identity();
    
    void resvg_render(
        const ResvgRenderTree* tree,
        ResvgFitTo fit_to,
        ResvgTransform transform,
        unsigned int width,
        unsigned int height,
        char* pixels
    );
    
    void resvg_tree_destroy(ResvgRenderTree* tree);
}

namespace VectorGraphics {

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
    
    // Initialize resvg
    resvgOptions_ = resvg_options_create();
    resvg_options_load_system_fonts(static_cast<ResvgOptions*>(resvgOptions_));
}

void IconManager::Shutdown() {
    ClearCache();
    
    if (resvgOptions_) {
        resvg_options_destroy(static_cast<ResvgOptions*>(resvgOptions_));
        resvgOptions_ = nullptr;
    }
    
    icons_.clear();
}

void IconManager::LoadCompiledIcons() {
    // Load all compiled icons from generated header
    for (size_t i = 0; i < COMPILED_ICONS_COUNT; ++i) {
        LoadIcon(COMPILED_ICONS[i]);
    }
}

void IconManager::LoadIcon(const CompiledIconData& compiledData) {
    RuntimeIcon icon;
    icon.id = compiledData.id;
    icon.svgContent = compiledData.svgContent;
    icon.width = compiledData.width;
    icon.height = compiledData.height;
    
    // Build class-to-token mapping
    for (const auto& mapping : compiledData.classMappings) {
        std::string key = mapping.elementId + ":" + mapping.property;
        icon.classToToken[key] = mapping.className;
    }
    
    // Extract color zones
    ExtractColorZones(icon);
    
    // Set default scheme based on whether we have class mappings
    if (!icon.classToToken.empty()) {
        icon.metadata.scheme = IconColorScheme::Bicolor;
    } else {
        icon.metadata.scheme = IconColorScheme::Original;
    }
    
    icons_[icon.id] = icon;
}

void IconManager::ExtractColorZones(RuntimeIcon& icon) {
    // Parse SVG to find all unique colors
    std::set<uint32_t> uniqueColors;
    std::map<uint32_t, std::string> colorToToken;
    
    // Parse SVG content to extract colors and their token associations
    // Using custom delimiters (regex) to avoid issues with quotes in raw string literals
    std::regex fillRegex(R"regex(fill\s*=\s*"#([0-9a-fA-F]{6})")regex");
    std::regex strokeRegex(R"regex(stroke\s*=\s*"#([0-9a-fA-F]{6})")regex");
    std::regex stopColorRegex(R"regex(stop-color\s*:\s*#([0-9a-fA-F]{6}))regex");
    
    auto extract_colors = [&](const std::regex& regex) {
        auto matches_begin = std::sregex_iterator(icon.svgContent.begin(), icon.svgContent.end(), regex);
        auto matches_end = std::sregex_iterator();
        
        for (std::sregex_iterator i = matches_begin; i != matches_end; ++i) {
            std::smatch match = *i;
            std::string hexColor = match[1].str();
            
            // Convert hex to RGBA (assuming full opacity)
            uint32_t r, g, b;
            std::stringstream ss;
            ss << std::hex << hexColor.substr(0, 2);
            ss >> r;
            ss.clear();
            ss << std::hex << hexColor.substr(2, 2);
            ss >> g;
            ss.clear();
            ss << std::hex << hexColor.substr(4, 2);
            ss >> b;
            
            uint32_t rgba = (0xFF << 24) | (b << 16) | (g << 8) | r;
            uniqueColors.insert(rgba);
        }
    };
    
    extract_colors(fillRegex);
    extract_colors(strokeRegex);
    extract_colors(stopColorRegex);
    
    // Create color zones
    // Assign tokens based on the order they appear and class mappings
    int colorIndex = 0;
    for (uint32_t color : uniqueColors) {
        std::string tokenName = "";
        
        // Try to determine token from class mappings
        // This is a simplified heuristic - in a real implementation,
        // you'd need to track which color comes from which element
        if (!icon.classToToken.empty()) {
            if (colorIndex == 0) tokenName = "primary";
            else if (colorIndex == 1) tokenName = "secondary";
            else if (colorIndex == 2) tokenName = "tertiary";
        }
        
        icon.metadata.colorZones.push_back(ColorZone(color, tokenName));
        colorIndex++;
    }
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

void IconManager::RenderIcon(const std::string& id, float size) {
    auto it = icons_.find(id);
    if (it == icons_.end()) return;
    
    const RuntimeIcon& icon = it->second;
    RenderIcon(id, size, icon.metadata);
}

void IconManager::RenderIcon(const std::string& id, float size, const IconMetadata& localMetadata) {
    auto it = icons_.find(id);
    if (it == icons_.end()) return;
    
    const RuntimeIcon& icon = it->second;
    
    // Build token colors map
    std::map<std::string, ImVec4> tokenColors;
    
    if (localMetadata.scheme == IconColorScheme::Bicolor) {
        auto& ds = DesignSystem::DesignSystem::Instance();
        tokenColors["primary"] = ds.GetColor("semantic.icon.color.primary");
        tokenColors["secondary"] = ds.GetColor("semantic.icon.color.secondary");
        tokenColors["tertiary"] = ds.GetColor("semantic.icon.color.tertiary");
    } else if (localMetadata.scheme == IconColorScheme::Multicolor) {
        // Use custom colors from metadata
        for (const auto& zone : localMetadata.colorZones) {
            if (zone.hasToken) {
                tokenColors[zone.tokenName] = zone.customColor;
            }
        }
    }
    // For Original mode, tokenColors is empty, so original colors are used
    
    std::string cacheKey = CreateCacheKey(id, size, localMetadata);
    
    auto cacheIt = textureCache_.find(cacheKey);
    if (cacheIt != textureCache_.end()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
        return;
    }
    
    int width, height;
    unsigned char* pixels = RenderSVGWithColors(icon, size, tokenColors, localMetadata, width, height);
    if (!pixels) return;
    
    VulkanTexture tex = CreateTexture(pixels, width, height);
    delete[] pixels;
    
    textureCache_[cacheKey] = tex;
    ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
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
    if (it == icons_.end()) return;
    
    const RuntimeIcon& icon = it->second;
    
    std::map<std::string, ImVec4> tokenColors;
    tokenColors["primary"] = primaryColor;
    tokenColors["secondary"] = secondaryColor;
    
    IconMetadata tempMetadata = icon.metadata;
    tempMetadata.scheme = IconColorScheme::Bicolor;
    
    std::string cacheKey = CreateCacheKey(id, size, tempMetadata, &primaryColor, &secondaryColor);
    
    auto cacheIt = textureCache_.find(cacheKey);
    if (cacheIt != textureCache_.end()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
        return;
    }
    
    int width, height;
    unsigned char* pixels = RenderSVGWithColors(icon, size, tokenColors, tempMetadata, width, height);
    if (!pixels) return;
    
    VulkanTexture tex = CreateTexture(pixels, width, height);
    delete[] pixels;
    
    textureCache_[cacheKey] = tex;
    ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
}

std::string IconManager::ModifySVGColors(
    const std::string& originalSVG,
    const std::map<std::string, std::string>& classToToken,
    const std::map<std::string, ImVec4>& tokenColors,
    const IconMetadata& metadata)
{
    if (metadata.scheme == IconColorScheme::Original || tokenColors.empty()) {
        return originalSVG;
    }
    
    std::string modifiedSVG = originalSVG;
    
    // Parse and modify SVG based on class mappings
    // For each element with a class that maps to a token, replace its color
    for (const auto& [key, tokenName] : classToToken) {
        auto tokenIt = tokenColors.find(tokenName);
        if (tokenIt == tokenColors.end()) continue;
        
        const ImVec4& color = tokenIt->second;
        std::string hexColor = ImVec4ToHex(color);
        
        // Extract element ID and property from key
        size_t colonPos = key.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string elementId = key.substr(0, colonPos);
        std::string property = key.substr(colonPos + 1);
        
        // Build regex to find and replace the color
        // This is a simplified approach - proper XML parsing would be more robust
        if (property == "fill") {
            std::regex fillRegex(R"regex(fill\s*=\s*"#[0-9a-fA-F]{6}")regex");
            // Replace all fills - in a real implementation, we'd target specific elements
            modifiedSVG = std::regex_replace(modifiedSVG, fillRegex, "fill=\"" + hexColor + "\"");
        } else if (property == "stroke") {
            std::regex strokeRegex(R"regex(stroke\s*=\s*"#[0-9a-fA-F]{6}")regex");
            modifiedSVG = std::regex_replace(modifiedSVG, strokeRegex, "stroke=\"" + hexColor + "\"");
        } else if (property == "stop-color") {
            std::regex stopColorRegex(R"regex(stop-color\s*:\s*#[0-9a-fA-F]{6})regex");
            modifiedSVG = std::regex_replace(modifiedSVG, stopColorRegex, "stop-color:" + hexColor);
        }
    }
    
    return modifiedSVG;
}

unsigned char* IconManager::RenderSVGWithColors(
    const RuntimeIcon& icon,
    float size,
    const std::map<std::string, ImVec4>& tokenColors,
    const IconMetadata& metadata,
    int& outWidth,
    int& outHeight)
{
    // Modify SVG colors if needed
    std::string modifiedSVG = ModifySVGColors(icon.svgContent, icon.classToToken, tokenColors, metadata);
    
    // Parse and render with resvg C API
    ResvgRenderTree* tree = resvg_parse_tree_from_data(
        modifiedSVG.c_str(),
        static_cast<unsigned int>(modifiedSVG.length()),
        static_cast<const ResvgOptions*>(resvgOptions_)
    );
    
    if (!tree) {
        return nullptr;
    }
    
    ResvgSize svgSize = resvg_get_image_size(tree);
    float scale = size / std::max(svgSize.width, svgSize.height);
    
    outWidth = static_cast<int>(svgSize.width * scale);
    outHeight = static_cast<int>(svgSize.height * scale);
    
    unsigned char* pixels = new unsigned char[outWidth * outHeight * 4];
    
    ResvgFitTo fitTo;
    fitTo.fit_type = RESVG_FIT_TO_TYPE_ZOOM;
    fitTo.value = scale;
    
    resvg_render(
        tree,
        fitTo,
        resvg_transform_identity(),
        static_cast<unsigned int>(outWidth),
        static_cast<unsigned int>(outHeight),
        reinterpret_cast<char*>(pixels)
    );
    
    resvg_tree_destroy(tree);
    
    return pixels;
}

std::string IconManager::CreateCacheKey(
    const std::string& id,
    float size,
    const IconMetadata& metadata,
    const ImVec4* primaryColor,
    const ImVec4* secondaryColor,
    const ImVec4* tertiaryColor) const
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
        
        if (tertiaryColor) {
            oss << "_T" << static_cast<int>(tertiaryColor->x * 255)
                << "_" << static_cast<int>(tertiaryColor->y * 255)
                << "_" << static_cast<int>(tertiaryColor->z * 255);
        }
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

uint32_t IconManager::ImVec4ToRGBA(const ImVec4& color) const {
    return ((uint32_t)(color.w * 255) << 24) | 
           ((uint32_t)(color.z * 255) << 16) | 
           ((uint32_t)(color.y * 255) << 8) | 
           ((uint32_t)(color.x * 255));
}

ImVec4 IconManager::RGBAToImVec4(uint32_t rgba) const {
    return ImVec4(
        ((rgba) & 0xFF) / 255.0f,
        ((rgba >> 8) & 0xFF) / 255.0f,
        ((rgba >> 16) & 0xFF) / 255.0f,
        ((rgba >> 24) & 0xFF) / 255.0f
    );
}

std::string IconManager::ImVec4ToHex(const ImVec4& color) const {
    std::ostringstream oss;
    oss << "#"
        << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(color.x * 255)
        << std::setw(2) << static_cast<int>(color.y * 255)
        << std::setw(2) << static_cast<int>(color.z * 255);
    return oss.str();
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

} // namespace VectorGraphics
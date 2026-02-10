#include <VectorGraphics/IconManager.h>
#include <VectorGraphics/IconMetadata.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_impl_vulkan.h>
#include <generated/IconData.h>

#include <sstream>
#include <cstring>
#include <algorithm>
#include <set>
#include <map>
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
    
    // Extract all color mappings from SVG
    ExtractColorMappings(icon);
    
    // Build color zones
    BuildColorZones(icon);
    
    // Set default scheme
    icon.metadata.scheme = IconColorScheme::Bicolor;
    
    icons_[icon.id] = icon;
}

void IconManager::ExtractColorMappings(RuntimeIcon& icon) {
    std::vector<ColorMapping>& mappings = icon.colorMappings;
    
    // Parse SVG content with simple string scanning
    // We'll look for: <element ... fill="#..." ... class="...">
    //                 <element ... stroke="#..." ... class="...">
    //                 <element ... style="fill:#...;stroke:#..." ... class="...">
    //                 <stop ... stop-color="#..." ... class="...">
    //                 <stop ... style="stop-color:#..." ... class="...">
    
    const std::string& svg = icon.svgContent;
    size_t pos = 0;
    int elementCounter = 0;
    
    while (pos < svg.length()) {
        // Find next element opening tag
        size_t tagStart = svg.find('<', pos);
        if (tagStart == std::string::npos) break;
        
        // Skip comments and declarations
        if (svg.substr(tagStart, 4) == "<!--" || 
            svg.substr(tagStart, 2) == "<?" ||
            svg.substr(tagStart, 2) == "</" ||
            svg.substr(tagStart, 9) == "<!DOCTYPE") {
            pos = tagStart + 1;
            continue;
        }
        
        // Find tag end
        size_t tagEnd = svg.find('>', tagStart);
        if (tagEnd == std::string::npos) break;
        
        std::string tag = svg.substr(tagStart, tagEnd - tagStart + 1);
        
        // Extract element name
        size_t nameEnd = tag.find_first_of(" \t\n\r/>", 1);
        std::string elementName = tag.substr(1, nameEnd - 1);
        
        // Extract attributes
        std::string elementId = ExtractAttribute(tag, "id");
        if (elementId.empty()) {
            elementId = "elem_" + std::to_string(elementCounter++);
        }
        
        std::string cssClass = ExtractAttribute(tag, "class");
        std::string fill = ExtractAttribute(tag, "fill");
        std::string stroke = ExtractAttribute(tag, "stroke");
        std::string stopColor = ExtractAttribute(tag, "stop-color");
        std::string style = ExtractAttribute(tag, "style");
        
        // Process fill attribute
        if (!fill.empty() && fill[0] == '#' && fill.length() == 7) {
            uint32_t color = ParseHexColor(fill.substr(1));
            mappings.push_back(ColorMapping(
                elementId,
                "fill",
                color,
                cssClass
            ));
        }
        
        // Process stroke attribute
        if (!stroke.empty() && stroke[0] == '#' && stroke.length() == 7) {
            uint32_t color = ParseHexColor(stroke.substr(1));
            mappings.push_back(ColorMapping(
                elementId,
                "stroke",
                color,
                cssClass
            ));
        }
        
        // Process stop-color attribute
        if (!stopColor.empty() && stopColor[0] == '#' && stopColor.length() == 7) {
            uint32_t color = ParseHexColor(stopColor.substr(1));
            mappings.push_back(ColorMapping(
                elementId,
                "stop-color",
                color,
                cssClass
            ));
        }
        
        // Process style attribute
        if (!style.empty()) {
            ParseStyleAttribute(style, elementId, cssClass, mappings);
        }
        
        pos = tagEnd + 1;
    }
}

void IconManager::ParseStyleAttribute(
    const std::string& style,
    const std::string& elementId,
    const std::string& cssClass,
    std::vector<ColorMapping>& mappings)
{
    // Parse: "fill:#RRGGBB;stroke:#RRGGBB;stop-color:#RRGGBB"
    
    std::regex fillRegex(R"(fill\s*:\s*#([0-9a-fA-F]{6}))");
    std::regex strokeRegex(R"(stroke\s*:\s*#([0-9a-fA-F]{6}))");
    std::regex stopColorRegex(R"(stop-color\s*:\s*#([0-9a-fA-F]{6}))");
    
    std::smatch match;
    
    // Extract fill from style
    if (std::regex_search(style, match, fillRegex)) {
        uint32_t color = ParseHexColor(match[1].str());
        mappings.push_back(ColorMapping(
            elementId,
            "fill",
            color,
            cssClass
        ));
    }
    
    // Extract stroke from style
    if (std::regex_search(style, match, strokeRegex)) {
        uint32_t color = ParseHexColor(match[1].str());
        mappings.push_back(ColorMapping(
            elementId,
            "stroke",
            color,
            cssClass
        ));
    }
    
    // Extract stop-color from style
    if (std::regex_search(style, match, stopColorRegex)) {
        uint32_t color = ParseHexColor(match[1].str());
        mappings.push_back(ColorMapping(
            elementId,
            "stop-color",
            color,
            cssClass
        ));
    }
}

std::string IconManager::ExtractAttribute(const std::string& tag, const std::string& attrName) const {
    std::string pattern = attrName + "\\s*=\\s*\"([^\"]*)\"";
    std::regex attrRegex(pattern);
    std::smatch match;
    
    if (std::regex_search(tag, match, attrRegex)) {
        return match[1].str();
    }
    
    return "";
}

uint32_t IconManager::ParseHexColor(const std::string& hexColor) const {
    // Parse RRGGBB to RGBA (with full opacity)
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
    
    // Return as RGBA: 0xAABBGGRR
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void IconManager::BuildColorZones(RuntimeIcon& icon) {
    // Group mappings by color
    std::map<uint32_t, std::vector<int>> colorToIndices;
    
    for (size_t i = 0; i < icon.colorMappings.size(); ++i) {
        uint32_t color = icon.colorMappings[i].originalColor;
        colorToIndices[color].push_back(static_cast<int>(i));
    }
    
    // Create color zones
    icon.metadata.colorZones.clear();
    for (const auto& [color, indices] : colorToIndices) {
        ColorZone zone(color);
        zone.mappingIndices = indices;
        
        // Determine default token from first mapping's CSS class
        if (!indices.empty()) {
            const std::string& cssClass = icon.colorMappings[indices[0]].cssClass;
            zone.tokenAssignment = DetermineDefaultToken(cssClass);
        }
        
        icon.metadata.colorZones.push_back(zone);
    }
}

std::string IconManager::DetermineDefaultToken(const std::string& cssClass) const {
    // Check if class contains "primary" or "secondary"
    if (cssClass.find("primary") != std::string::npos ||
        cssClass.find("ds-primary") != std::string::npos) {
        return "primary";
    }
    
    if (cssClass.find("secondary") != std::string::npos ||
        cssClass.find("ds-secondary") != std::string::npos) {
        return "secondary";
    }
    
    // Default to primary
    return "primary";
}

IconMetadata IconManager::GetDefaultMetadata(const std::string& id) const {
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        return it->second.metadata;
    }
    return IconMetadata();
}

const std::vector<ColorMapping>& IconManager::GetColorMappings(const std::string& id) const {
    static std::vector<ColorMapping> empty;
    auto it = icons_.find(id);
    if (it != icons_.end()) {
        return it->second.colorMappings;
    }
    return empty;
}

void IconManager::RenderIcon(const std::string& id, float size) {
    auto it = icons_.find(id);
    if (it == icons_.end()) return;
    
    RenderIcon(id, size, it->second.metadata);
}

void IconManager::RenderIcon(const std::string& id, float size, const IconMetadata& metadata) {
    auto it = icons_.find(id);
    if (it == icons_.end()) return;
    
    const RuntimeIcon& icon = it->second;
    
    // Generate cache key
    std::string cacheKey = CreateCacheKey(id, size, metadata);
    
    // Check cache
    auto cacheIt = textureCache_.find(cacheKey);
    if (cacheIt != textureCache_.end()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
        return;
    }
    
    // Render SVG
    int width, height;
    unsigned char* pixels = RenderSVGWithColors(icon, size, metadata, width, height);
    if (!pixels) return;
    
    // Create texture
    VulkanTexture tex = CreateTexture(pixels, width, height);
    delete[] pixels;
    
    // Cache and render
    textureCache_[cacheKey] = tex;
    ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
}

unsigned char* IconManager::RenderSVGWithColors(
    const RuntimeIcon& icon,
    float size,
    const IconMetadata& metadata,
    int& outWidth,
    int& outHeight)
{
    // Modify SVG colors if needed
    std::string modifiedSVG = ModifySVGColors(icon.svgContent, icon.colorMappings, metadata);
    
    // Parse and render with resvg
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

std::string IconManager::ModifySVGColors(
    const std::string& originalSVG,
    const std::vector<ColorMapping>& mappings,
    const IconMetadata& metadata) const
{
    if (metadata.scheme == IconColorScheme::Original) {
        return originalSVG;
    }
    
    // Build replacement map: original_color -> new_color
    std::map<uint32_t, std::string> colorReplacements;
    
    if (metadata.scheme == IconColorScheme::Bicolor) {
        // Use design system tokens
        auto& ds = DesignSystem::DesignSystem::Instance();
        
        for (const auto& zone : metadata.colorZones) {
            ImVec4 newColor;
            
            if (zone.tokenAssignment == "primary") {
                newColor = ds.GetColor("semantic.icon.color.primary");
            } else if (zone.tokenAssignment == "secondary") {
                newColor = ds.GetColor("semantic.icon.color.secondary");
            } else {
                // Use original color
                continue;
            }
            
            colorReplacements[zone.originalColor] = ImVec4ToHex(newColor);
        }
    } else if (metadata.scheme == IconColorScheme::Multicolor) {
        // Use custom colors
        for (const auto& zone : metadata.colorZones) {
            colorReplacements[zone.originalColor] = ImVec4ToHex(zone.customColor);
        }
    }
    
    if (colorReplacements.empty()) {
        return originalSVG;
    }
    
    // Replace colors in SVG
    std::string modifiedSVG = originalSVG;
    
    for (const auto& [originalRGBA, newHex] : colorReplacements) {
        std::string originalHex = RGBAToHex(originalRGBA);
        
        // Replace in fill attributes
        std::regex fillRegex("fill\\s*=\\s*\"" + originalHex + "\"");
        modifiedSVG = std::regex_replace(modifiedSVG, fillRegex, "fill=\"" + newHex + "\"");
        
        // Replace in stroke attributes
        std::regex strokeRegex("stroke\\s*=\\s*\"" + originalHex + "\"");
        modifiedSVG = std::regex_replace(modifiedSVG, strokeRegex, "stroke=\"" + newHex + "\"");
        
        // Replace in stop-color attributes
        std::regex stopColorRegex("stop-color\\s*=\\s*\"" + originalHex + "\"");
        modifiedSVG = std::regex_replace(modifiedSVG, stopColorRegex, "stop-color=\"" + newHex + "\"");
        
        // Replace in style attributes (fill)
        std::regex styleFillRegex("fill\\s*:\\s*" + originalHex);
        modifiedSVG = std::regex_replace(modifiedSVG, styleFillRegex, "fill:" + newHex);
        
        // Replace in style attributes (stroke)
        std::regex styleStrokeRegex("stroke\\s*:\\s*" + originalHex);
        modifiedSVG = std::regex_replace(modifiedSVG, styleStrokeRegex, "stroke:" + newHex);
        
        // Replace in style attributes (stop-color)
        std::regex styleStopColorRegex("stop-color\\s*:\\s*" + originalHex);
        modifiedSVG = std::regex_replace(modifiedSVG, styleStopColorRegex, "stop-color:" + newHex);
    }
    
    return modifiedSVG;
}

std::string IconManager::CreateCacheKey(
    const std::string& id,
    float size,
    const IconMetadata& metadata) const
{
    std::ostringstream oss;
    oss << id << "_" << static_cast<int>(size) << "_" << static_cast<int>(metadata.scheme);
    
    if (metadata.scheme == IconColorScheme::Bicolor) {
        for (const auto& zone : metadata.colorZones) {
            oss << "_" << zone.originalColor << "=" << zone.tokenAssignment;
        }
    } else if (metadata.scheme == IconColorScheme::Multicolor) {
        for (const auto& zone : metadata.colorZones) {
            oss << "_" << zone.originalColor << "="
                << static_cast<int>(zone.customColor.x * 255)
                << "," << static_cast<int>(zone.customColor.y * 255)
                << "," << static_cast<int>(zone.customColor.z * 255);
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

std::string IconManager::RGBAToHex(uint32_t rgba) const {
    std::ostringstream oss;
    oss << "#"
        << std::hex << std::setfill('0')
        << std::setw(2) << (rgba & 0xFF)        // R
        << std::setw(2) << ((rgba >> 8) & 0xFF)  // G
        << std::setw(2) << ((rgba >> 16) & 0xFF); // B
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
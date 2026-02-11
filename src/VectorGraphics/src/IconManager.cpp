// // // #include <VectorGraphics/IconManager.h>
// // // #include <VectorGraphics/IconMetadata.h>
// // // #include <DesignSystem/DesignSystem.h>
// // // #include <imgui_impl_vulkan.h>
// // // #include <generated/IconData.h>

// // // #include <sstream>
// // // #include <cstring>
// // // #include <algorithm>
// // // #include <set>
// // // #include <map>
// // // #include <regex>
// // // #include <iomanip>

// // // // Forward declarations for resvg C API
// // // extern "C" {
// // //     typedef struct ResvgOptions ResvgOptions;
// // //     typedef struct ResvgRenderTree ResvgRenderTree;
    
// // //     typedef struct {
// // //         float width;
// // //         float height;
// // //     } ResvgSize;
    
// // //     typedef struct {
// // //         float a, b, c, d, e, f;
// // //     } ResvgTransform;
    
// // //     typedef enum {
// // //         RESVG_FIT_TO_TYPE_ORIGINAL = 0,
// // //         RESVG_FIT_TO_TYPE_WIDTH,
// // //         RESVG_FIT_TO_TYPE_HEIGHT,
// // //         RESVG_FIT_TO_TYPE_ZOOM,
// // //     } ResvgFitToType;
    
// // //     typedef struct {
// // //         ResvgFitToType fit_type;
// // //         float value;
// // //     } ResvgFitTo;
    
// // //     ResvgOptions* resvg_options_create();
// // //     void resvg_options_load_system_fonts(ResvgOptions* options);
// // //     void resvg_options_destroy(ResvgOptions* options);
    
// // //     ResvgRenderTree* resvg_parse_tree_from_data(
// // //         const char* data,
// // //         unsigned int length,
// // //         const ResvgOptions* options
// // //     );
    
// // //     ResvgSize resvg_get_image_size(const ResvgRenderTree* tree);
// // //     ResvgTransform resvg_transform_identity();
    
// // //     void resvg_render(
// // //         const ResvgRenderTree* tree,
// // //         ResvgFitTo fit_to,
// // //         ResvgTransform transform,
// // //         unsigned int width,
// // //         unsigned int height,
// // //         char* pixels
// // //     );
    
// // //     void resvg_tree_destroy(ResvgRenderTree* tree);
// // // }

// // // namespace VectorGraphics {

// // // IconManager& IconManager::Instance() {
// // //     static IconManager instance;
// // //     return instance;
// // // }

// // // IconManager::~IconManager() {
// // //     Shutdown();
// // // }

// // // void IconManager::Initialize(
// // //     VkDevice device,
// // //     VkPhysicalDevice physicalDevice,
// // //     VkQueue queue,
// // //     VkCommandPool commandPool,
// // //     VkDescriptorPool descriptorPool)
// // // {
// // //     device_ = device;
// // //     physicalDevice_ = physicalDevice;
// // //     queue_ = queue;
// // //     commandPool_ = commandPool;
// // //     descriptorPool_ = descriptorPool;
    
// // //     resvgOptions_ = resvg_options_create();
// // //     resvg_options_load_system_fonts(static_cast<ResvgOptions*>(resvgOptions_));
// // // }

// // // void IconManager::Shutdown() {
// // //     ClearCache();
    
// // //     if (resvgOptions_) {
// // //         resvg_options_destroy(static_cast<ResvgOptions*>(resvgOptions_));
// // //         resvgOptions_ = nullptr;
// // //     }
    
// // //     icons_.clear();
// // // }

// // // void IconManager::LoadCompiledIcons() {
// // //     for (size_t i = 0; i < COMPILED_ICONS_COUNT; ++i) {
// // //         LoadIcon(COMPILED_ICONS[i]);
// // //     }
// // // }

// // // void IconManager::LoadIcon(const CompiledIconData& compiledData) {
// // //     RuntimeIcon icon;
// // //     icon.id = compiledData.id;
// // //     icon.svgContent = compiledData.svgContent;
// // //     icon.width = compiledData.width;
// // //     icon.height = compiledData.height;
    
// // //     // Extract all color mappings from SVG
// // //     ExtractColorMappings(icon);
    
// // //     // Build color zones
// // //     BuildColorZones(icon);
    
// // //     // Set default scheme
// // //     icon.metadata.scheme = IconColorScheme::Bicolor;
    
// // //     icons_[icon.id] = icon;
// // // }

// // // void IconManager::ExtractColorMappings(RuntimeIcon& icon) {
// // //     std::vector<ColorMapping>& mappings = icon.colorMappings;
    
// // //     // Parse SVG content with simple string scanning
// // //     // We'll look for: <element ... fill="#..." ... class="...">
// // //     //                 <element ... stroke="#..." ... class="...">
// // //     //                 <element ... style="fill:#...;stroke:#..." ... class="...">
// // //     //                 <stop ... stop-color="#..." ... class="...">
// // //     //                 <stop ... style="stop-color:#..." ... class="...">
    
// // //     const std::string& svg = icon.svgContent;
// // //     size_t pos = 0;
// // //     int elementCounter = 0;
    
// // //     while (pos < svg.length()) {
// // //         // Find next element opening tag
// // //         size_t tagStart = svg.find('<', pos);
// // //         if (tagStart == std::string::npos) break;
        
// // //         // Skip comments and declarations
// // //         if (svg.substr(tagStart, 4) == "<!--" || 
// // //             svg.substr(tagStart, 2) == "<?" ||
// // //             svg.substr(tagStart, 2) == "</" ||
// // //             svg.substr(tagStart, 9) == "<!DOCTYPE") {
// // //             pos = tagStart + 1;
// // //             continue;
// // //         }
        
// // //         // Find tag end
// // //         size_t tagEnd = svg.find('>', tagStart);
// // //         if (tagEnd == std::string::npos) break;
        
// // //         std::string tag = svg.substr(tagStart, tagEnd - tagStart + 1);
        
// // //         // Extract element name
// // //         size_t nameEnd = tag.find_first_of(" \t\n\r/>", 1);
// // //         std::string elementName = tag.substr(1, nameEnd - 1);
        
// // //         // Extract attributes
// // //         std::string elementId = ExtractAttribute(tag, "id");
// // //         if (elementId.empty()) {
// // //             elementId = "elem_" + std::to_string(elementCounter++);
// // //         }
        
// // //         std::string cssClass = ExtractAttribute(tag, "class");
// // //         std::string fill = ExtractAttribute(tag, "fill");
// // //         std::string stroke = ExtractAttribute(tag, "stroke");
// // //         std::string stopColor = ExtractAttribute(tag, "stop-color");
// // //         std::string style = ExtractAttribute(tag, "style");
        
// // //         // Process fill attribute
// // //         if (!fill.empty() && fill[0] == '#' && fill.length() == 7) {
// // //             uint32_t color = ParseHexColor(fill.substr(1));
// // //             mappings.push_back(ColorMapping(
// // //                 elementId,
// // //                 "fill",
// // //                 color,
// // //                 cssClass
// // //             ));
// // //         }
        
// // //         // Process stroke attribute
// // //         if (!stroke.empty() && stroke[0] == '#' && stroke.length() == 7) {
// // //             uint32_t color = ParseHexColor(stroke.substr(1));
// // //             mappings.push_back(ColorMapping(
// // //                 elementId,
// // //                 "stroke",
// // //                 color,
// // //                 cssClass
// // //             ));
// // //         }
        
// // //         // Process stop-color attribute
// // //         if (!stopColor.empty() && stopColor[0] == '#' && stopColor.length() == 7) {
// // //             uint32_t color = ParseHexColor(stopColor.substr(1));
// // //             mappings.push_back(ColorMapping(
// // //                 elementId,
// // //                 "stop-color",
// // //                 color,
// // //                 cssClass
// // //             ));
// // //         }
        
// // //         // Process style attribute
// // //         if (!style.empty()) {
// // //             ParseStyleAttribute(style, elementId, cssClass, mappings);
// // //         }
        
// // //         pos = tagEnd + 1;
// // //     }
// // // }

// // // void IconManager::ParseStyleAttribute(
// // //     const std::string& style,
// // //     const std::string& elementId,
// // //     const std::string& cssClass,
// // //     std::vector<ColorMapping>& mappings)
// // // {
// // //     // Parse: "fill:#RRGGBB;stroke:#RRGGBB;stop-color:#RRGGBB"
    
// // //     std::regex fillRegex(R"(fill\s*:\s*#([0-9a-fA-F]{6}))");
// // //     std::regex strokeRegex(R"(stroke\s*:\s*#([0-9a-fA-F]{6}))");
// // //     std::regex stopColorRegex(R"(stop-color\s*:\s*#([0-9a-fA-F]{6}))");
    
// // //     std::smatch match;
    
// // //     // Extract fill from style
// // //     if (std::regex_search(style, match, fillRegex)) {
// // //         uint32_t color = ParseHexColor(match[1].str());
// // //         mappings.push_back(ColorMapping(
// // //             elementId,
// // //             "fill",
// // //             color,
// // //             cssClass
// // //         ));
// // //     }
    
// // //     // Extract stroke from style
// // //     if (std::regex_search(style, match, strokeRegex)) {
// // //         uint32_t color = ParseHexColor(match[1].str());
// // //         mappings.push_back(ColorMapping(
// // //             elementId,
// // //             "stroke",
// // //             color,
// // //             cssClass
// // //         ));
// // //     }
    
// // //     // Extract stop-color from style
// // //     if (std::regex_search(style, match, stopColorRegex)) {
// // //         uint32_t color = ParseHexColor(match[1].str());
// // //         mappings.push_back(ColorMapping(
// // //             elementId,
// // //             "stop-color",
// // //             color,
// // //             cssClass
// // //         ));
// // //     }
// // // }

// // // std::string IconManager::ExtractAttribute(const std::string& tag, const std::string& attrName) const {
// // //     std::string pattern = attrName + "\\s*=\\s*\"([^\"]*)\"";
// // //     std::regex attrRegex(pattern);
// // //     std::smatch match;
    
// // //     if (std::regex_search(tag, match, attrRegex)) {
// // //         return match[1].str();
// // //     }
    
// // //     return "";
// // // }

// // // uint32_t IconManager::ParseHexColor(const std::string& hexColor) const {
// // //     // Parse RRGGBB to RGBA (with full opacity)
// // //     uint32_t r, g, b;
// // //     std::stringstream ss;
    
// // //     ss << std::hex << hexColor.substr(0, 2);
// // //     ss >> r;
// // //     ss.clear();
    
// // //     ss << std::hex << hexColor.substr(2, 2);
// // //     ss >> g;
// // //     ss.clear();
    
// // //     ss << std::hex << hexColor.substr(4, 2);
// // //     ss >> b;
    
// // //     // Return as RGBA: 0xAABBGGRR
// // //     return 0xFF000000 | (b << 16) | (g << 8) | r;
// // // }

// // // void IconManager::BuildColorZones(RuntimeIcon& icon) {
// // //     // Group mappings by color
// // //     std::map<uint32_t, std::vector<int>> colorToIndices;
    
// // //     for (size_t i = 0; i < icon.colorMappings.size(); ++i) {
// // //         uint32_t color = icon.colorMappings[i].originalColor;
// // //         colorToIndices[color].push_back(static_cast<int>(i));
// // //     }
    
// // //     // Create color zones
// // //     icon.metadata.colorZones.clear();
// // //     for (const auto& [color, indices] : colorToIndices) {
// // //         ColorZone zone(color);
// // //         zone.mappingIndices = indices;
        
// // //         // Determine default token from first mapping's CSS class
// // //         if (!indices.empty()) {
// // //             const std::string& cssClass = icon.colorMappings[indices[0]].cssClass;
// // //             zone.tokenAssignment = DetermineDefaultToken(cssClass);
// // //         }
        
// // //         icon.metadata.colorZones.push_back(zone);
// // //     }
// // // }

// // // std::string IconManager::DetermineDefaultToken(const std::string& cssClass) const {
// // //     // Check if class contains "primary" or "secondary"
// // //     if (cssClass.find("primary") != std::string::npos ||
// // //         cssClass.find("ds-primary") != std::string::npos) {
// // //         return "primary";
// // //     }
    
// // //     if (cssClass.find("secondary") != std::string::npos ||
// // //         cssClass.find("ds-secondary") != std::string::npos) {
// // //         return "secondary";
// // //     }
    
// // //     // Default to primary
// // //     return "primary";
// // // }

// // // IconMetadata IconManager::GetDefaultMetadata(const std::string& id) const {
// // //     auto it = icons_.find(id);
// // //     if (it != icons_.end()) {
// // //         return it->second.metadata;
// // //     }
// // //     return IconMetadata();
// // // }

// // // const std::vector<ColorMapping>& IconManager::GetColorMappings(const std::string& id) const {
// // //     static std::vector<ColorMapping> empty;
// // //     auto it = icons_.find(id);
// // //     if (it != icons_.end()) {
// // //         return it->second.colorMappings;
// // //     }
// // //     return empty;
// // // }

// // // void IconManager::RenderIcon(const std::string& id, float size) {
// // //     auto it = icons_.find(id);
// // //     if (it == icons_.end()) return;
    
// // //     RenderIcon(id, size, it->second.metadata);
// // // }

// // // void IconManager::RenderIcon(const std::string& id, float size, const IconMetadata& metadata) {
// // //     auto it = icons_.find(id);
// // //     if (it == icons_.end()) return;
    
// // //     const RuntimeIcon& icon = it->second;
    
// // //     // Generate cache key
// // //     std::string cacheKey = CreateCacheKey(id, size, metadata);
    
// // //     // Check cache
// // //     auto cacheIt = textureCache_.find(cacheKey);
// // //     if (cacheIt != textureCache_.end()) {
// // //         ImGui::Image(reinterpret_cast<ImTextureID>(cacheIt->second.descriptor), ImVec2(size, size));
// // //         return;
// // //     }
    
// // //     // Render SVG
// // //     int width, height;
// // //     unsigned char* pixels = RenderSVGWithColors(icon, size, metadata, width, height);
// // //     if (!pixels) return;
    
// // //     // Create texture
// // //     VulkanTexture tex = CreateTexture(pixels, width, height);
// // //     delete[] pixels;
    
// // //     // Cache and render
// // //     textureCache_[cacheKey] = tex;
// // //     ImGui::Image(reinterpret_cast<ImTextureID>(tex.descriptor), ImVec2(size, size));
// // // }

// // // unsigned char* IconManager::RenderSVGWithColors(
// // //     const RuntimeIcon& icon,
// // //     float size,
// // //     const IconMetadata& metadata,
// // //     int& outWidth,
// // //     int& outHeight)
// // // {
// // //     // Modify SVG colors if needed
// // //     std::string modifiedSVG = ModifySVGColors(icon.svgContent, icon.colorMappings, metadata);
    
// // //     // Parse and render with resvg
// // //     ResvgRenderTree* tree = resvg_parse_tree_from_data(
// // //         modifiedSVG.c_str(),
// // //         static_cast<unsigned int>(modifiedSVG.length()),
// // //         static_cast<const ResvgOptions*>(resvgOptions_)
// // //     );
    
// // //     if (!tree) {
// // //         return nullptr;
// // //     }
    
// // //     ResvgSize svgSize = resvg_get_image_size(tree);
// // //     float scale = size / std::max(svgSize.width, svgSize.height);
    
// // //     outWidth = static_cast<int>(svgSize.width * scale);
// // //     outHeight = static_cast<int>(svgSize.height * scale);
    
// // //     unsigned char* pixels = new unsigned char[outWidth * outHeight * 4];
    
// // //     ResvgFitTo fitTo;
// // //     fitTo.fit_type = RESVG_FIT_TO_TYPE_ZOOM;
// // //     fitTo.value = scale;
    
// // //     resvg_render(
// // //         tree,
// // //         fitTo,
// // //         resvg_transform_identity(),
// // //         static_cast<unsigned int>(outWidth),
// // //         static_cast<unsigned int>(outHeight),
// // //         reinterpret_cast<char*>(pixels)
// // //     );
    
// // //     resvg_tree_destroy(tree);
    
// // //     return pixels;
// // // }

// // // std::string IconManager::ModifySVGColors(
// // //     const std::string& originalSVG,
// // //     const std::vector<ColorMapping>& mappings,
// // //     const IconMetadata& metadata) const
// // // {
// // //     if (metadata.scheme == IconColorScheme::Original) {
// // //         return originalSVG;
// // //     }
    
// // //     // Build replacement map: original_color -> new_color
// // //     std::map<uint32_t, std::string> colorReplacements;
    
// // //     if (metadata.scheme == IconColorScheme::Bicolor) {
// // //         // Use design system tokens
// // //         auto& ds = DesignSystem::DesignSystem::Instance();
        
// // //         for (const auto& zone : metadata.colorZones) {
// // //             ImVec4 newColor;
            
// // //             if (zone.tokenAssignment == "primary") {
// // //                 newColor = ds.GetColor("semantic.icon.color.primary");
// // //             } else if (zone.tokenAssignment == "secondary") {
// // //                 newColor = ds.GetColor("semantic.icon.color.secondary");
// // //             } else {
// // //                 // Use original color
// // //                 continue;
// // //             }
            
// // //             colorReplacements[zone.originalColor] = ImVec4ToHex(newColor);
// // //         }
// // //     } else if (metadata.scheme == IconColorScheme::Multicolor) {
// // //         // Use custom colors
// // //         for (const auto& zone : metadata.colorZones) {
// // //             colorReplacements[zone.originalColor] = ImVec4ToHex(zone.customColor);
// // //         }
// // //     }
    
// // //     if (colorReplacements.empty()) {
// // //         return originalSVG;
// // //     }
    
// // //     // Replace colors in SVG
// // //     std::string modifiedSVG = originalSVG;
    
// // //     for (const auto& [originalRGBA, newHex] : colorReplacements) {
// // //         std::string originalHex = RGBAToHex(originalRGBA);
        
// // //         // Replace in fill attributes
// // //         std::regex fillRegex("fill\\s*=\\s*\"" + originalHex + "\"");
// // //         modifiedSVG = std::regex_replace(modifiedSVG, fillRegex, "fill=\"" + newHex + "\"");
        
// // //         // Replace in stroke attributes
// // //         std::regex strokeRegex("stroke\\s*=\\s*\"" + originalHex + "\"");
// // //         modifiedSVG = std::regex_replace(modifiedSVG, strokeRegex, "stroke=\"" + newHex + "\"");
        
// // //         // Replace in stop-color attributes
// // //         std::regex stopColorRegex("stop-color\\s*=\\s*\"" + originalHex + "\"");
// // //         modifiedSVG = std::regex_replace(modifiedSVG, stopColorRegex, "stop-color=\"" + newHex + "\"");
        
// // //         // Replace in style attributes (fill)
// // //         std::regex styleFillRegex("fill\\s*:\\s*" + originalHex);
// // //         modifiedSVG = std::regex_replace(modifiedSVG, styleFillRegex, "fill:" + newHex);
        
// // //         // Replace in style attributes (stroke)
// // //         std::regex styleStrokeRegex("stroke\\s*:\\s*" + originalHex);
// // //         modifiedSVG = std::regex_replace(modifiedSVG, styleStrokeRegex, "stroke:" + newHex);
        
// // //         // Replace in style attributes (stop-color)
// // //         std::regex styleStopColorRegex("stop-color\\s*:\\s*" + originalHex);
// // //         modifiedSVG = std::regex_replace(modifiedSVG, styleStopColorRegex, "stop-color:" + newHex);
// // //     }
    
// // //     return modifiedSVG;
// // // }

// // // std::string IconManager::CreateCacheKey(
// // //     const std::string& id,
// // //     float size,
// // //     const IconMetadata& metadata) const
// // // {
// // //     std::ostringstream oss;
// // //     oss << id << "_" << static_cast<int>(size) << "_" << static_cast<int>(metadata.scheme);
    
// // //     if (metadata.scheme == IconColorScheme::Bicolor) {
// // //         for (const auto& zone : metadata.colorZones) {
// // //             oss << "_" << zone.originalColor << "=" << zone.tokenAssignment;
// // //         }
// // //     } else if (metadata.scheme == IconColorScheme::Multicolor) {
// // //         for (const auto& zone : metadata.colorZones) {
// // //             oss << "_" << zone.originalColor << "="
// // //                 << static_cast<int>(zone.customColor.x * 255)
// // //                 << "," << static_cast<int>(zone.customColor.y * 255)
// // //                 << "," << static_cast<int>(zone.customColor.z * 255);
// // //         }
// // //     }
    
// // //     return oss.str();
// // // }

// // // uint32_t IconManager::ImVec4ToRGBA(const ImVec4& color) const {
// // //     return ((uint32_t)(color.w * 255) << 24) | 
// // //            ((uint32_t)(color.z * 255) << 16) | 
// // //            ((uint32_t)(color.y * 255) << 8) | 
// // //            ((uint32_t)(color.x * 255));
// // // }

// // // ImVec4 IconManager::RGBAToImVec4(uint32_t rgba) const {
// // //     return ImVec4(
// // //         ((rgba) & 0xFF) / 255.0f,
// // //         ((rgba >> 8) & 0xFF) / 255.0f,
// // //         ((rgba >> 16) & 0xFF) / 255.0f,
// // //         ((rgba >> 24) & 0xFF) / 255.0f
// // //     );
// // // }

// // // std::string IconManager::ImVec4ToHex(const ImVec4& color) const {
// // //     std::ostringstream oss;
// // //     oss << "#"
// // //         << std::hex << std::setfill('0')
// // //         << std::setw(2) << static_cast<int>(color.x * 255)
// // //         << std::setw(2) << static_cast<int>(color.y * 255)
// // //         << std::setw(2) << static_cast<int>(color.z * 255);
// // //     return oss.str();
// // // }

// // // std::string IconManager::RGBAToHex(uint32_t rgba) const {
// // //     std::ostringstream oss;
// // //     oss << "#"
// // //         << std::hex << std::setfill('0')
// // //         << std::setw(2) << (rgba & 0xFF)        // R
// // //         << std::setw(2) << ((rgba >> 8) & 0xFF)  // G
// // //         << std::setw(2) << ((rgba >> 16) & 0xFF); // B
// // //     return oss.str();
// // // }

// // // void IconManager::InvalidateCache() {
// // //     cacheInvalidated_ = true;
// // // }

// // // void IconManager::SafeClearCache() {
// // //     if (device_ != VK_NULL_HANDLE) {
// // //         vkDeviceWaitIdle(device_);
// // //     }
// // //     ClearCache();
// // // }

// // // void IconManager::CleanupCacheIfNeeded() {
// // //     if (cacheInvalidated_) {
// // //         SafeClearCache();
// // //         cacheInvalidated_ = false;
// // //     }
// // // }

// // // void IconManager::ClearCache() {
// // //     for (auto& [key, tex] : textureCache_) {
// // //         DestroyTexture(tex);
// // //     }
// // //     textureCache_.clear();
// // // }

// // // std::vector<std::string> IconManager::GetLoadedIcons() const {
// // //     std::vector<std::string> result;
// // //     for (const auto& [id, _] : icons_) {
// // //         result.push_back(id);
// // //     }
// // //     return result;
// // // }

// // // // ========== Vulkan Helpers ==========

// // // VulkanTexture IconManager::CreateTexture(const unsigned char* pixels, int width, int height) {
// // //     VulkanTexture tex{};
// // //     tex.width = width;
// // //     tex.height = height;
    
// // //     VkDeviceSize size = width * height * 4;
    
// // //     VkBuffer staging;
// // //     VkDeviceMemory stagingMem;
// // //     CreateBuffer(
// // //         size,
// // //         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
// // //         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
// // //         staging,
// // //         stagingMem
// // //     );
    
// // //     void* data;
// // //     vkMapMemory(device_, stagingMem, 0, size, 0, &data);
// // //     memcpy(data, pixels, size);
// // //     vkUnmapMemory(device_, stagingMem);
    
// // //     CreateImage(
// // //         width, height,
// // //         VK_FORMAT_R8G8B8A8_UNORM,
// // //         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
// // //         tex.image,
// // //         tex.memory
// // //     );
    
// // //     TransitionImage(tex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
// // //     CopyBufferToImage(staging, tex.image, width, height);
// // //     TransitionImage(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    
// // //     vkDestroyBuffer(device_, staging, nullptr);
// // //     vkFreeMemory(device_, stagingMem, nullptr);
    
// // //     tex.view = CreateImageView(tex.image, VK_FORMAT_R8G8B8A8_UNORM);
// // //     tex.sampler = CreateSampler();
    
// // //     tex.descriptor = ImGui_ImplVulkan_AddTexture(
// // //         tex.sampler,
// // //         tex.view,
// // //         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
// // //     );
    
// // //     return tex;
// // // }

// // // void IconManager::DestroyTexture(VulkanTexture& tex) {
// // //     if (tex.descriptor)
// // //         ImGui_ImplVulkan_RemoveTexture(tex.descriptor);
// // //     if (tex.sampler)
// // //         vkDestroySampler(device_, tex.sampler, nullptr);
// // //     if (tex.view)
// // //         vkDestroyImageView(device_, tex.view, nullptr);
// // //     if (tex.image)
// // //         vkDestroyImage(device_, tex.image, nullptr);
// // //     if (tex.memory)
// // //         vkFreeMemory(device_, tex.memory, nullptr);
    
// // //     tex = {};
// // // }

// // // uint32_t IconManager::FindMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
// // //     VkPhysicalDeviceMemoryProperties mem;
// // //     vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    
// // //     for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
// // //         if ((filter & (1 << i)) &&
// // //             (mem.memoryTypes[i].propertyFlags & props) == props) {
// // //             return i;
// // //         }
// // //     }
    
// // //     return 0;
// // // }

// // // void IconManager::CreateBuffer(
// // //     VkDeviceSize size,
// // //     VkBufferUsageFlags usage,
// // //     VkMemoryPropertyFlags props,
// // //     VkBuffer& buffer,
// // //     VkDeviceMemory& memory)
// // // {
// // //     VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
// // //     info.size = size;
// // //     info.usage = usage;
    
// // //     vkCreateBuffer(device_, &info, nullptr, &buffer);
    
// // //     VkMemoryRequirements req;
// // //     vkGetBufferMemoryRequirements(device_, buffer, &req);
    
// // //     VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
// // //     alloc.allocationSize = req.size;
// // //     alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    
// // //     vkAllocateMemory(device_, &alloc, nullptr, &memory);
// // //     vkBindBufferMemory(device_, buffer, memory, 0);
// // // }

// // // void IconManager::CreateImage(
// // //     int width, int height,
// // //     VkFormat format,
// // //     VkImageUsageFlags usage,
// // //     VkImage& image,
// // //     VkDeviceMemory& memory)
// // // {
// // //     VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
// // //     info.imageType = VK_IMAGE_TYPE_2D;
// // //     info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
// // //     info.mipLevels = 1;
// // //     info.arrayLayers = 1;
// // //     info.format = format;
// // //     info.tiling = VK_IMAGE_TILING_OPTIMAL;
// // //     info.usage = usage;
// // //     info.samples = VK_SAMPLE_COUNT_1_BIT;
    
// // //     vkCreateImage(device_, &info, nullptr, &image);
    
// // //     VkMemoryRequirements req;
// // //     vkGetImageMemoryRequirements(device_, image, &req);
    
// // //     VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
// // //     alloc.allocationSize = req.size;
// // //     alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
// // //     vkAllocateMemory(device_, &alloc, nullptr, &memory);
// // //     vkBindImageMemory(device_, image, memory, 0);
// // // }

// // // VkImageView IconManager::CreateImageView(VkImage image, VkFormat format) {
// // //     VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
// // //     info.image = image;
// // //     info.viewType = VK_IMAGE_VIEW_TYPE_2D;
// // //     info.format = format;
// // //     info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
// // //     info.subresourceRange.levelCount = 1;
// // //     info.subresourceRange.layerCount = 1;
    
// // //     VkImageView view;
// // //     vkCreateImageView(device_, &info, nullptr, &view);
// // //     return view;
// // // }

// // // VkSampler IconManager::CreateSampler() {
// // //     VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
// // //     info.magFilter = VK_FILTER_LINEAR;
// // //     info.minFilter = VK_FILTER_LINEAR;
// // //     info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
// // //     info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
// // //     info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
// // //     VkSampler sampler;
// // //     vkCreateSampler(device_, &info, nullptr, &sampler);
// // //     return sampler;
// // // }

// // // VkCommandBuffer IconManager::BeginSingleTimeCommands() {
// // //     VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
// // //     alloc.commandPool = commandPool_;
// // //     alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
// // //     alloc.commandBufferCount = 1;
    
// // //     VkCommandBuffer cmd;
// // //     vkAllocateCommandBuffers(device_, &alloc, &cmd);
    
// // //     VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
// // //     begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
// // //     vkBeginCommandBuffer(cmd, &begin);
    
// // //     return cmd;
// // // }

// // // void IconManager::EndSingleTimeCommands(VkCommandBuffer cmd) {
// // //     vkEndCommandBuffer(cmd);
    
// // //     VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
// // //     submit.commandBufferCount = 1;
// // //     submit.pCommandBuffers = &cmd;
    
// // //     vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
// // //     vkQueueWaitIdle(queue_);
    
// // //     vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
// // // }

// // // void IconManager::TransitionImage(VkImage img, VkImageLayout oldL, VkImageLayout newL) {
// // //     VkCommandBuffer cmd = BeginSingleTimeCommands();
    
// // //     VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
// // //     barrier.oldLayout = oldL;
// // //     barrier.newLayout = newL;
// // //     barrier.image = img;
// // //     barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
// // //     barrier.subresourceRange.levelCount = 1;
// // //     barrier.subresourceRange.layerCount = 1;
    
// // //     vkCmdPipelineBarrier(
// // //         cmd,
// // //         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
// // //         VK_PIPELINE_STAGE_TRANSFER_BIT,
// // //         0,
// // //         0, nullptr,
// // //         0, nullptr,
// // //         1, &barrier
// // //     );
    
// // //     EndSingleTimeCommands(cmd);
// // // }

// // // void IconManager::CopyBufferToImage(VkBuffer buf, VkImage img, int w, int h) {
// // //     VkCommandBuffer cmd = BeginSingleTimeCommands();
    
// // //     VkBufferImageCopy region{};
// // //     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
// // //     region.imageSubresource.layerCount = 1;
// // //     region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
    
// // //     vkCmdCopyBufferToImage(
// // //         cmd,
// // //         buf,
// // //         img,
// // //         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
// // //         1,
// // //         &region
// // //     );
    
// // //     EndSingleTimeCommands(cmd);
// // // }

// // // } // namespace VectorGraphics







// // #include "VectorGraphics/IconManager.h"
// // #include <algorithm>
// // #include <sstream>
// // #include <iomanip>
// // #include <cstring>
// // #include <regex>
// // #include <DesignSystem/DesignSystem.h>

// // // Include the generated icon data
// // #include "generated/IconData.h"

// // // External resvg C bindings
// // extern "C" {
// //     #include "resvg_c.h"
// // }

// // namespace VectorGraphics {

// // // ============================================================================
// // // SINGLETON IMPLEMENTATION
// // // ============================================================================

// // IconManager& IconManager::Instance() {
// //     static IconManager instance;
// //     return instance;
// // }

// // IconManager::IconManager()
// //     : currentFrame_(0)
// //     , maxCacheSize_(100)
// //     , cacheMaxAge_(300)  // 300 frames ≈ 5 seconds at 60fps
// //     , initialized_(false)
// // {}

// // IconManager::~IconManager() {
// //     Shutdown();
// // }

// // // ============================================================================
// // // INITIALIZATION
// // // ============================================================================

// // void IconManager::Initialize() {
// //     if (initialized_) {
// //         return;
// //     }
    
// //     // Initialize resvg
// //     resvg_init_log();
    
// //     // Load compiled icons
// //     LoadCompiledIcons();
    
// //     initialized_ = true;
// // }

// // void IconManager::Shutdown() {
// //     if (!initialized_) {
// //         return;
// //     }
    
// //     ClearCache();
// //     icons_.clear();
    
// //     initialized_ = false;
// // }

// // // ============================================================================
// // // ICON LOADING
// // // ============================================================================

// // void IconManager::LoadCompiledIcons() {
// //     for (size_t i = 0; i < COMPILED_ICONS_COUNT; ++i) {
// //         LoadIcon(COMPILED_ICONS[i]);
// //     }
// // }

// // void IconManager::LoadIcon(const CompiledIconData& iconData) {
// //     RuntimeIcon icon;
// //     icon.id = iconData.id;
// //     icon.svgContent = iconData.svgContent;
// //     icon.width = iconData.width;
// //     icon.height = iconData.height;
    
// //     // Convert compiled color mappings to runtime format
// //     for (size_t i = 0; i < iconData.colorMappingsCount; ++i) {
// //         const auto& compiledMapping = iconData.colorMappings[i];
// //         ColorMapping mapping;
// //         mapping.elementId = compiledMapping.elementId;
// //         mapping.property = compiledMapping.property;
// //         mapping.originalColor = compiledMapping.originalColor;
// //         mapping.cssClass = compiledMapping.cssClass;
// //         icon.colorMappings.push_back(mapping);
// //     }
    
// //     // Convert compiled color zones to runtime format
// //     for (size_t i = 0; i < iconData.colorZonesCount; ++i) {
// //         const auto& compiledZone = iconData.colorZones[i];
// //         ColorZone zone;
// //         zone.originalColor = compiledZone.originalColor;
// //         zone.defaultToken = compiledZone.defaultToken;
        
// //         for (size_t j = 0; j < compiledZone.mappingIndicesCount; ++j) {
// //             zone.mappingIndices.push_back(compiledZone.mappingIndices[j]);
// //         }
        
// //         icon.colorZones.push_back(zone);
// //     }
    
// //     icons_[icon.id] = std::move(icon);
// // }

// // // ============================================================================
// // // COLOR MAPPING EXTRACTION (fallback for non-compiled icons)
// // // ============================================================================

// // void IconManager::ExtractColorMappings(RuntimeIcon& icon) {
// //     std::vector<ColorMapping> mappings;
    
// //     std::string svg = icon.svgContent;
// //     size_t pos = 0;
// //     int elementCounter = 0;
    
// //     // Simple regex-based extraction (fallback method)
// //     // This would normally use a proper XML parser
// //     std::regex tagRegex("<([a-zA-Z][a-zA-Z0-9]*)([^>]*)>");
// //     std::smatch match;
    
// //     std::string::const_iterator searchStart(svg.cbegin());
// //     while (std::regex_search(searchStart, svg.cend(), match, tagRegex)) {
// //         elementCounter++;
// //         std::string elementId = "elem_" + std::to_string(elementCounter);
// //         std::string attributes = match[2].str();
        
// //         // Extract class attribute
// //         std::string cssClass;
// //         std::regex classRegex("class\\s*=\\s*\"([^\"]*)\"");
// //         std::smatch classMatch;
// //         if (std::regex_search(attributes, classMatch, classRegex)) {
// //             cssClass = classMatch[1].str();
// //         }
        
// //         // Extract fill attribute
// //         std::regex fillRegex("fill\\s*=\\s*\"([^\"]*)\"");
// //         std::smatch fillMatch;
// //         if (std::regex_search(attributes, fillMatch, fillRegex)) {
// //             std::string fillValue = fillMatch[1].str();
// //             if (fillValue != "none" && fillValue.find("url(") == std::string::npos) {
// //                 uint32_t color = ParseHexColor(fillValue);
// //                 if (color != 0) {
// //                     ColorMapping mapping;
// //                     mapping.elementId = elementId;
// //                     mapping.property = "fill";
// //                     mapping.originalColor = color;
// //                     mapping.cssClass = cssClass;
// //                     mappings.push_back(mapping);
// //                 }
// //             }
// //         }
        
// //         // Extract stroke attribute
// //         std::regex strokeRegex("stroke\\s*=\\s*\"([^\"]*)\"");
// //         std::smatch strokeMatch;
// //         if (std::regex_search(attributes, strokeMatch, strokeRegex)) {
// //             std::string strokeValue = strokeMatch[1].str();
// //             if (strokeValue != "none" && strokeValue.find("url(") == std::string::npos) {
// //                 uint32_t color = ParseHexColor(strokeValue);
// //                 if (color != 0) {
// //                     ColorMapping mapping;
// //                     mapping.elementId = elementId;
// //                     mapping.property = "stroke";
// //                     mapping.originalColor = color;
// //                     mapping.cssClass = cssClass;
// //                     mappings.push_back(mapping);
// //                 }
// //             }
// //         }
        
// //         // Parse style attribute
// //         std::regex styleRegex("style\\s*=\\s*\"([^\"]*)\"");
// //         std::smatch styleMatch;
// //         if (std::regex_search(attributes, styleMatch, styleRegex)) {
// //             ParseStyleAttribute(elementId, "style", styleMatch[1].str(), mappings);
// //         }
        
// //         searchStart = match.suffix().first;
// //     }
    
// //     icon.colorMappings = std::move(mappings);
// // }

// // void IconManager::ParseStyleAttribute(
// //     const std::string& elementId,
// //     const std::string& attrName,
// //     const std::string& attrValue,
// //     std::vector<ColorMapping>& mappings
// // ) {
// //     // Parse CSS style properties (e.g., "fill:#ff0000;stroke:#00ff00")
// //     std::istringstream styleStream(attrValue);
// //     std::string property;
    
// //     while (std::getline(styleStream, property, ';')) {
// //         size_t colonPos = property.find(':');
// //         if (colonPos == std::string::npos) continue;
        
// //         std::string propName = property.substr(0, colonPos);
// //         std::string propValue = property.substr(colonPos + 1);
        
// //         // Trim whitespace
// //         propName.erase(0, propName.find_first_not_of(" \t"));
// //         propName.erase(propName.find_last_not_of(" \t") + 1);
// //         propValue.erase(0, propValue.find_first_not_of(" \t"));
// //         propValue.erase(propValue.find_last_not_of(" \t") + 1);
        
// //         if ((propName == "fill" || propName == "stroke" || propName == "stop-color") &&
// //             propValue != "none" && propValue.find("url(") == std::string::npos) {
            
// //             uint32_t color = ParseHexColor(propValue);
// //             if (color != 0) {
// //                 ColorMapping mapping;
// //                 mapping.elementId = elementId;
// //                 mapping.property = propName;
// //                 mapping.originalColor = color;
// //                 mapping.cssClass = "";  // Not available in style attribute
// //                 mappings.push_back(mapping);
// //             }
// //         }
// //     }
// // }

// // std::string IconManager::ExtractAttribute(const std::string& tag, const std::string& attrName) const {
// //     std::regex attrRegex(attrName + "\\s*=\\s*\"([^\"]*)\"");
// //     std::smatch match;
// //     if (std::regex_search(tag, match, attrRegex)) {
// //         return match[1].str();
// //     }
// //     return "";
// // }

// // uint32_t IconManager::ParseHexColor(const std::string& hexColor) const {
// //     if (hexColor.empty()) return 0;
    
// //     std::string color = hexColor;
    
// //     // Remove # if present
// //     if (color[0] == '#') {
// //         color = color.substr(1);
// //     }
    
// //     // Handle 3-digit hex colors (#RGB → #RRGGBB)
// //     if (color.length() == 3) {
// //         color = std::string(2, color[0]) + std::string(2, color[1]) + std::string(2, color[2]);
// //     }
    
// //     // Parse hex string
// //     if (color.length() == 6) {
// //         try {
// //             uint32_t rgb = std::stoul(color, nullptr, 16);
// //             return 0xFF000000 | rgb;  // Add full alpha
// //         } catch (...) {
// //             return 0;
// //         }
// //     } else if (color.length() == 8) {
// //         try {
// //             return std::stoul(color, nullptr, 16);
// //         } catch (...) {
// //             return 0;
// //         }
// //     }
    
// //     return 0;
// // }

// // // ============================================================================
// // // COLOR ZONE BUILDING
// // // ============================================================================

// // void IconManager::BuildColorZones(RuntimeIcon& icon) {
// //     std::unordered_map<uint32_t, std::vector<size_t>> colorMap;
// //     std::unordered_map<uint32_t, std::string> colorClasses;
    
// //     // Group mappings by color
// //     for (size_t i = 0; i < icon.colorMappings.size(); ++i) {
// //         const auto& mapping = icon.colorMappings[i];
// //         colorMap[mapping.originalColor].push_back(i);
        
// //         // Keep track of the first CSS class for each color
// //         if (colorClasses.find(mapping.originalColor) == colorClasses.end()) {
// //             colorClasses[mapping.originalColor] = mapping.cssClass;
// //         }
// //     }
    
// //     // Create color zones
// //     std::vector<ColorZone> zones;
// //     for (const auto& [color, indices] : colorMap) {
// //         ColorZone zone;
// //         zone.originalColor = color;
// //         zone.mappingIndices = indices;
// //         zone.defaultToken = DetermineDefaultToken(colorClasses[color]);
// //         zones.push_back(zone);
// //     }
    
// //     // Sort zones by color for consistency
// //     std::sort(zones.begin(), zones.end(), [](const ColorZone& a, const ColorZone& b) {
// //         return a.originalColor < b.originalColor;
// //     });
    
// //     icon.colorZones = std::move(zones);
// // }

// // std::string IconManager::DetermineDefaultToken(const std::string& cssClass) const {
// //     if (cssClass.find("primary") != std::string::npos || cssClass.find("ds-primary") != std::string::npos) {
// //         return "primary";
// //     } else if (cssClass.find("secondary") != std::string::npos || cssClass.find("ds-secondary") != std::string::npos) {
// //         return "secondary";
// //     } else if (cssClass.find("accent") != std::string::npos) {
// //         return "accent";
// //     }
// //     return "primary";
// // }

// // // ============================================================================
// // // ICON RENDERING
// // // ============================================================================

// // void IconManager::RenderIcon(
// //     const std::string& iconId,
// //     const ImVec2& size,
// //     const std::string& colorToken,
// //     float scale
// // ) {
// //     ImDrawList* drawList = ImGui::GetWindowDrawList();
// //     ImVec2 pos = ImGui::GetCursorScreenPos();
    
// //     RenderIcon(drawList, iconId, pos, size, colorToken, scale);
    
// //     // Advance cursor
// //     ImGui::Dummy(size);
// // }

// // void IconManager::RenderIcon(
// //     ImDrawList* drawList,
// //     const std::string& iconId,
// //     const ImVec2& position,
// //     const ImVec2& size,
// //     const std::string& colorToken,
// //     float scale
// // ) {
// //     if (!HasIcon(iconId)) {
// //         // Draw placeholder for missing icon
// //         drawList->AddRectFilled(
// //             position,
// //             ImVec2(position.x + size.x, position.y + size.y),
// //             IM_COL32(255, 0, 255, 128)
// //         );
// //         return;
// //     }
    
// //     ImTextureID texture = GetOrCreateTexture(iconId, size, colorToken, scale);
    
// //     if (texture) {
// //         drawList->AddImage(
// //             texture,
// //             position,
// //             ImVec2(position.x + size.x, position.y + size.y)
// //         );
// //     }
// // }

// // // ============================================================================
// // // TEXTURE MANAGEMENT
// // // ============================================================================

// // std::string IconManager::GenerateCacheKey(
// //     const std::string& iconId,
// //     const ImVec2& size,
// //     const std::string& colorToken,
// //     float scale
// // ) const {
// //     std::ostringstream oss;
// //     oss << iconId << "_"
// //         << static_cast<int>(size.x) << "x" << static_cast<int>(size.y) << "_"
// //         << colorToken << "_"
// //         << std::fixed << std::setprecision(2) << scale;
// //     return oss.str();
// // }

// // ImTextureID IconManager::GetOrCreateTexture(
// //     const std::string& iconId,
// //     const ImVec2& size,
// //     const std::string& colorToken,
// //     float scale
// // ) {
// //     std::string cacheKey = GenerateCacheKey(iconId, size, colorToken, scale);
    
// //     auto it = textureCache_.find(cacheKey);
// //     if (it != textureCache_.end()) {
// //         it->second.lastAccessFrame = currentFrame_;
// //         return it->second.textureId;
// //     }
    
// //     // Create new texture
// //     int width = static_cast<int>(size.x);
// //     int height = static_cast<int>(size.y);
    
// //     ImTextureID texture = RenderIconToTexture(iconId, width, height, colorToken, scale);
    
// //     if (texture) {
// //         IconTexture iconTexture;
// //         iconTexture.textureId = texture;
// //         iconTexture.width = width;
// //         iconTexture.height = height;
// //         iconTexture.scale = scale;
// //         iconTexture.colorToken = colorToken;
// //         iconTexture.lastAccessFrame = currentFrame_;
        
// //         textureCache_[cacheKey] = iconTexture;
        
// //         // Cleanup if cache is too large
// //         if (textureCache_.size() > maxCacheSize_) {
// //             CleanupOldCacheEntries();
// //         }
// //     }
    
// //     return texture;
// // }

// // ImTextureID IconManager::RenderIconToTexture(
// //     const std::string& iconId,
// //     int width,
// //     int height,
// //     const std::string& colorToken,
// //     float scale
// // ) {
// //     auto it = icons_.find(iconId);
// //     if (it == icons_.end()) {
// //         return ImTextureID(0);
// //     }
    
// //     const RuntimeIcon& icon = it->second;
    
// //     // Apply color token to SVG
// //     std::string processedSvg = ApplyColorToken(icon.svgContent, icon, colorToken);
    
// //     // Parse SVG with resvg
// //     resvg_options* opt = resvg_options_create();
// //     resvg_options_load_system_fonts(opt);
    
// //     resvg_render_tree* tree = resvg_parse_tree_from_data(
// //         processedSvg.c_str(),
// //         processedSvg.length(),
// //         opt
// //     );
    
// //     resvg_options_destroy(opt);
    
// //     if (!tree) {
// //         return ImTextureID(0);
// //     }
    
// //     // Get tree size
// //     resvg_size tree_size = resvg_get_image_size(tree);
    
// //     // Calculate scaling factor
// //     float scaleX = width / tree_size.width;
// //     float scaleY = height / tree_size.height;
// //     float finalScale = std::min(scaleX, scaleY) * scale;
    
// //     // Allocate pixel buffer (RGBA)
// //     std::vector<uint8_t> pixmap(width * height * 4, 0);
    
// //     // Render
// //     resvg_fit_to fit_to;
// //     fit_to.type_ = RESVG_FIT_TO_TYPE_ZOOM;
// //     fit_to.value = finalScale;
    
// //     resvg_render(tree, fit_to, resvg_transform_identity(), width, height, pixmap.data());
    
// //     resvg_tree_destroy(tree);
    
// //     // Create ImGui texture from pixmap
// //     // This is platform-specific - you'll need to implement this based on your backend
// //     // For now, returning a placeholder
// //     ImTextureID textureId = ImTextureID(0);
    
// //     // TODO: Create actual Vulkan/OpenGL/DX texture from pixmap buffer
// //     // Example for Vulkan would be:
// //     // textureId = CreateVulkanTexture(pixmap.data(), width, height);
    
// //     return textureId;
// // }

// // std::string IconManager::ApplyColorToken(
// //     const std::string& svgContent,
// //     const RuntimeIcon& icon,
// //     const std::string& colorToken
// // ) const {
// //     std::string result = svgContent;
    
// //     // Apply color zones
// //     for (const auto& zone : icon.colorZones) {
// //         // Determine which token to use for this zone
// //         std::string activeToken = zone.defaultToken;
// //         if (!colorToken.empty()) {
// //             activeToken = colorToken;
// //         }
        
// //         // Get the color for this token
// //         uint32_t newColor = GetColorForToken(activeToken);
        
// //         // Convert colors to hex strings
// //         char oldColorHex[10];
// //         char newColorHex[10];
// //         snprintf(oldColorHex, sizeof(oldColorHex), "#%06X", zone.originalColor & 0xFFFFFF);
// //         snprintf(newColorHex, sizeof(newColorHex), "#%06X", newColor & 0xFFFFFF);
        
// //         // Replace all occurrences of the old color with the new color
// //         size_t pos = 0;
// //         while ((pos = result.find(oldColorHex, pos)) != std::string::npos) {
// //             result.replace(pos, strlen(oldColorHex), newColorHex);
// //             pos += strlen(newColorHex);
// //         }
// //     }
    
// //     return result;
// // }

// // uint32_t IconManager::GetColorForToken(const std::string& tokenName) const {
// //     auto& ds = DesignSystem::DesignSystem::Instance();
    
// //     // Build full token path
// //     std::string fullToken = "semantic.color." + tokenName;
    
// //     ImVec4 color = ds.GetColor(fullToken);
    
// //     // Convert ImVec4 to RGBA32
// //     uint32_t r = static_cast<uint32_t>(color.x * 255.0f);
// //     uint32_t g = static_cast<uint32_t>(color.y * 255.0f);
// //     uint32_t b = static_cast<uint32_t>(color.z * 255.0f);
// //     uint32_t a = static_cast<uint32_t>(color.w * 255.0f);
    
// //     return (a << 24) | (r << 16) | (g << 8) | b;
// // }

// // // ============================================================================
// // // CACHE MANAGEMENT
// // // ============================================================================

// // void IconManager::CleanupCacheIfNeeded() {
// //     currentFrame_++;
    
// //     if (currentFrame_ % 60 == 0) {  // Cleanup every 60 frames
// //         CleanupOldCacheEntries();
// //     }
// // }

// // void IconManager::CleanupOldCacheEntries() {
// //     std::vector<std::string> keysToRemove;
    
// //     for (const auto& [key, texture] : textureCache_) {
// //         if (currentFrame_ - texture.lastAccessFrame > cacheMaxAge_) {
// //             keysToRemove.push_back(key);
            
// //             // TODO: Destroy the actual texture here
// //             // Example for Vulkan:
// //             // DestroyVulkanTexture(texture.textureId);
// //         }
// //     }
    
// //     for (const auto& key : keysToRemove) {
// //         textureCache_.erase(key);
// //     }
// // }

// // void IconManager::ClearCache() {
// //     for (const auto& [key, texture] : textureCache_) {
// //         // TODO: Destroy textures here
// //         // Example: DestroyVulkanTexture(texture.textureId);
// //     }
// //     textureCache_.clear();
// // }

// // size_t IconManager::GetCacheSize() const {
// //     return textureCache_.size();
// // }

// // // ============================================================================
// // // ICON QUERIES
// // // ============================================================================

// // bool IconManager::HasIcon(const std::string& iconId) const {
// //     return icons_.find(iconId) != icons_.end();
// // }

// // std::vector<std::string> IconManager::GetAvailableIcons() const {
// //     std::vector<std::string> iconIds;
// //     iconIds.reserve(icons_.size());
    
// //     for (const auto& [id, icon] : icons_) {
// //         iconIds.push_back(id);
// //     }
    
// //     std::sort(iconIds.begin(), iconIds.end());
// //     return iconIds;
// // }

// // const std::vector<ColorMapping>& IconManager::GetColorMappings(const std::string& iconId) const {
// //     static const std::vector<ColorMapping> empty;
    
// //     auto it = icons_.find(iconId);
// //     if (it != icons_.end()) {
// //         return it->second.colorMappings;
// //     }
    
// //     return empty;
// // }

// // const std::vector<ColorZone>& IconManager::GetColorZones(const std::string& iconId) const {
// //     static const std::vector<ColorZone> empty;
    
// //     auto it = icons_.find(iconId);
// //     if (it != icons_.end()) {
// //         return it->second.colorZones;
// //     }
    
// //     return empty;
// // }

// // } // namespace VectorGraphics




// #include "VectorGraphics/IconManager.h"
// #include <algorithm>
// #include <sstream>
// #include <iomanip>
// #include <cstring>
// #include <DesignSystem/DesignSystem.h>

// // Include the generated icon data
// #include "generated/IconData.h"

// // External resvg C bindings
// extern "C" {
//     #include "resvg_c.h"
// }

// namespace VectorGraphics {

// // ============================================================================
// // SINGLETON IMPLEMENTATION
// // ============================================================================

// IconManager& IconManager::Instance() {
//     static IconManager instance;
//     return instance;
// }

// IconManager::IconManager()
//     : currentFrame_(0)
//     , maxCacheSize_(100)
//     , cacheMaxAge_(300)  // 300 frames ≈ 5 seconds at 60fps
//     , initialized_(false)
// {}

// IconManager::~IconManager() {
//     Shutdown();
// }

// // ============================================================================
// // INITIALIZATION
// // ============================================================================

// void IconManager::Initialize() {
//     if (initialized_) {
//         return;
//     }
    
//     // Initialize resvg
//     resvg_init_log();
    
//     // Load compiled icons
//     LoadCompiledIcons();
    
//     initialized_ = true;
// }

// void IconManager::Shutdown() {
//     if (!initialized_) {
//         return;
//     }
    
//     ClearCache();
//     icons_.clear();
    
//     initialized_ = false;
// }

// // ============================================================================
// // ICON LOADING
// // ============================================================================

// void IconManager::LoadCompiledIcons() {
//     for (size_t i = 0; i < COMPILED_ICONS_COUNT; ++i) {
//         LoadIcon(COMPILED_ICONS[i]);
//     }
// }

// void IconManager::LoadIcon(const CompiledIconData& iconData) {
//     RuntimeIcon icon;
//     icon.id = iconData.id;
//     icon.svgContent = iconData.svgContent;
//     icon.width = iconData.width;
//     icon.height = iconData.height;
    
//     // Convert compiled color mappings to runtime format
//     for (size_t i = 0; i < iconData.colorMappingsCount; ++i) {
//         const auto& compiledMapping = iconData.colorMappings[i];
//         ColorMapping mapping;
//         mapping.elementId = compiledMapping.elementId;
//         mapping.property = compiledMapping.property;
//         mapping.originalColor = compiledMapping.originalColor;
//         mapping.cssClass = compiledMapping.cssClass;
//         icon.colorMappings.push_back(mapping);
//     }
    
//     // Convert compiled color zones to runtime format
//     for (size_t i = 0; i < iconData.colorZonesCount; ++i) {
//         const auto& compiledZone = iconData.colorZones[i];
//         ColorZone zone;
//         zone.originalColor = compiledZone.originalColor;
//         zone.tokenAssignment = compiledZone.defaultToken;
        
//         // Initialize customColor to original color
//         zone.customColor = ColorU32ToVec4(compiledZone.originalColor);
        
//         for (size_t j = 0; j < compiledZone.mappingIndicesCount; ++j) {
//             zone.mappingIndices.push_back(compiledZone.mappingIndices[j]);
//         }
        
//         icon.colorZones.push_back(zone);
//     }
    
//     icons_[icon.id] = std::move(icon);
// }

// // ============================================================================
// // ICON RENDERING - SIMPLE INTERFACE
// // ============================================================================

// void IconManager::RenderIcon(
//     const std::string& iconId,
//     float size
// ) {
//     // ImVec2 pos = ImGui::GetCursorScreenPos();
//     // IconMetadata metadata = GetDefaultMetadata(iconId);
//     // RenderIcon(iconId, size, pos, metadata);
//     // ImGui::Dummy(ImVec2(size, size));
//     ImVec2 pos = ImGui::GetCursorScreenPos();
//     IconMetadata metadata = GetDefaultMetadata(iconId);
//     ImDrawList* drawList = ImGui::GetWindowDrawList();  // ← Ajouté
//     RenderIcon(drawList, iconId, pos, size, metadata);  // ← 5 arguments
//     ImGui::Dummy(ImVec2(size, size));
// }

// void IconManager::RenderIcon(
//     const std::string& iconId,
//     float size,
//     const ImVec2& position
// ) {
//     IconMetadata metadata = GetDefaultMetadata(iconId);
//     ImDrawList* drawList = ImGui::GetWindowDrawList();
//     RenderIcon(drawList, iconId, position, size, metadata);
// }

// // ============================================================================
// // ICON RENDERING - WITH METADATA
// // ============================================================================

// void IconManager::RenderIcon(
//     const std::string& iconId,
//     float size,
//     const IconMetadata& metadata
// ) {
//     ImVec2 pos = ImGui::GetCursorScreenPos();
//     ImDrawList* drawList = ImGui::GetWindowDrawList();
//     RenderIcon(drawList, iconId, pos, size, metadata);
//     ImGui::Dummy(ImVec2(size, size));
// }

// void IconManager::RenderIcon(
//     ImDrawList* drawList,
//     const std::string& iconId,
//     const ImVec2& position,
//     float size,
//     const IconMetadata& metadata
// ) {
//     if (!HasIcon(iconId)) {
//         // Draw placeholder for missing icon
//         drawList->AddRectFilled(
//             position,
//             ImVec2(position.x + size, position.y + size),
//             IM_COL32(255, 0, 255, 128)
//         );
//         return;
//     }
    
//     int pixelSize = static_cast<int>(size);
//     ImTextureID texture = GetOrCreateTexture(iconId, pixelSize, pixelSize, metadata);
    
//     if (texture) {
//         drawList->AddImage(
//             texture,
//             position,
//             ImVec2(position.x + size, position.y + size)
//         );
//     }
// }

// // ============================================================================
// // TEXTURE MANAGEMENT
// // ============================================================================

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
//         IconTexture iconTexture;
//         iconTexture.textureId = texture;
//         iconTexture.width = width;
//         iconTexture.height = height;
//         iconTexture.lastAccessFrame = currentFrame_;
//         iconTexture.cacheKey = cacheKey;
        
//         textureCache_[cacheKey] = iconTexture;
        
//         // Cleanup if cache is too large
//         if (textureCache_.size() > maxCacheSize_) {
//             CleanupOldCacheEntries();
//         }
//     }
    
//     return texture;
// }

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
    
//     // TODO: Create actual Vulkan texture from pixmap buffer
//     // For now, returning a placeholder
//     // This needs to be implemented based on your Vulkan setup
//     ImTextureID textureId = ImTextureID(0);
    
//     // You would implement something like:
//     // textureId = CreateVulkanTextureFromRGBA(pixmap.data(), width, height);
    
//     return textureId;
// }

// std::string IconManager::ApplyColorMapping(
//     const std::string& svgContent,
//     const RuntimeIcon& icon,
//     const IconMetadata& metadata
// ) const {
//     if (metadata.scheme == IconColorScheme::Original) {
//         return svgContent;
//     }
    
//     std::string result = svgContent;
    
//     // Build color replacement map
//     std::unordered_map<uint32_t, uint32_t> colorReplacements;
    
//     for (size_t zoneIdx = 0; zoneIdx < metadata.colorZones.size() && zoneIdx < icon.colorZones.size(); ++zoneIdx) {
//         const ColorZone& metaZone = metadata.colorZones[zoneIdx];
//         const ColorZone& iconZone = icon.colorZones[zoneIdx];
        
//         uint32_t newColor = GetColorForZone(metaZone, metadata.scheme);
//         colorReplacements[iconZone.originalColor] = newColor;
//     }
    
//     // Replace colors in SVG
//     for (const auto& [oldColor, newColor] : colorReplacements) {
//         char oldColorHex[10];
//         char newColorHex[10];
        
//         snprintf(oldColorHex, sizeof(oldColorHex), "#%06X", oldColor & 0xFFFFFF);
//         snprintf(newColorHex, sizeof(newColorHex), "#%06X", newColor & 0xFFFFFF);
        
//         // Replace all occurrences
//         size_t pos = 0;
//         while ((pos = result.find(oldColorHex, pos)) != std::string::npos) {
//             result.replace(pos, strlen(oldColorHex), newColorHex);
//             pos += strlen(newColorHex);
//         }
        
//         // Also try lowercase
//         snprintf(oldColorHex, sizeof(oldColorHex), "#%06x", oldColor & 0xFFFFFF);
//         snprintf(newColorHex, sizeof(newColorHex), "#%06x", newColor & 0xFFFFFF);
        
//         pos = 0;
//         while ((pos = result.find(oldColorHex, pos)) != std::string::npos) {
//             result.replace(pos, strlen(oldColorHex), newColorHex);
//             pos += strlen(newColorHex);
//         }
//     }
    
//     return result;
// }

// uint32_t IconManager::GetColorForZone(
//     const ColorZone& zone,
//     IconColorScheme scheme
// ) const {
//     switch (scheme) {
//         case IconColorScheme::Original:
//             return zone.originalColor;
            
//         case IconColorScheme::Bicolor:
//             return GetColorForToken(zone.tokenAssignment);
            
//         case IconColorScheme::Multicolor:
//             return ColorVec4ToU32(zone.customColor);
            
//         default:
//             return zone.originalColor;
//     }
// }

// uint32_t IconManager::GetColorForToken(const std::string& tokenName) const {
//     auto& ds = DesignSystem::DesignSystem::Instance();
    
//     // Build full token path
//     std::string fullToken = "semantic.icon.color." + tokenName;
    
//     try {
//         ImVec4 color = ds.GetColor(fullToken);
//         return ColorVec4ToU32(color);
//     } catch (...) {
//         // Fallback to white if token not found
//         return 0xFFFFFFFF;
//     }
// }

// ImVec4 IconManager::ColorU32ToVec4(uint32_t color) const {
//     return ImVec4(
//         ((color >> 16) & 0xFF) / 255.0f,
//         ((color >> 8) & 0xFF) / 255.0f,
//         (color & 0xFF) / 255.0f,
//         ((color >> 24) & 0xFF) / 255.0f
//     );
// }

// uint32_t IconManager::ColorVec4ToU32(const ImVec4& color) const {
//     uint32_t r = static_cast<uint32_t>(color.x * 255.0f);
//     uint32_t g = static_cast<uint32_t>(color.y * 255.0f);
//     uint32_t b = static_cast<uint32_t>(color.z * 255.0f);
//     uint32_t a = static_cast<uint32_t>(color.w * 255.0f);
    
//     return (a << 24) | (r << 16) | (g << 8) | b;
// }

// // ============================================================================
// // CACHE MANAGEMENT
// // ============================================================================

// void IconManager::CleanupCacheIfNeeded() {
//     currentFrame_++;
    
//     if (currentFrame_ % 60 == 0) {  // Cleanup every 60 frames
//         CleanupOldCacheEntries();
//     }
// }

// void IconManager::InvalidateCache() {
//     ClearCache();
// }

// void IconManager::CleanupOldCacheEntries() {
//     std::vector<std::string> keysToRemove;
    
//     for (const auto& [key, texture] : textureCache_) {
//         if (currentFrame_ - texture.lastAccessFrame > cacheMaxAge_) {
//             keysToRemove.push_back(key);
            
//             // TODO: Destroy the actual texture here
//             // Example for Vulkan:
//             // DestroyVulkanTexture(texture.textureId);
//         }
//     }
    
//     for (const auto& key : keysToRemove) {
//         textureCache_.erase(key);
//     }
// }

// void IconManager::ClearCache() {
//     for (const auto& [key, texture] : textureCache_) {
//         // TODO: Destroy textures here
//         // Example: DestroyVulkanTexture(texture.textureId);
//     }
//     textureCache_.clear();
// }

// size_t IconManager::GetCacheSize() const {
//     return textureCache_.size();
// }

// // ============================================================================
// // ICON QUERIES
// // ============================================================================

// bool IconManager::HasIcon(const std::string& iconId) const {
//     return icons_.find(iconId) != icons_.end();
// }

// std::vector<std::string> IconManager::GetLoadedIcons() const {
//     std::vector<std::string> iconIds;
//     iconIds.reserve(icons_.size());
    
//     for (const auto& [id, icon] : icons_) {
//         iconIds.push_back(id);
//     }
    
//     std::sort(iconIds.begin(), iconIds.end());
//     return iconIds;
// }

// std::vector<std::string> IconManager::GetAvailableIcons() const {
//     return GetLoadedIcons();
// }

// // ============================================================================
// // METADATA ACCESS
// // ============================================================================

// IconMetadata IconManager::GetDefaultMetadata(const std::string& iconId) const {
//     IconMetadata metadata;
//     metadata.scheme = IconColorScheme::Bicolor;
    
//     auto it = icons_.find(iconId);
//     if (it != icons_.end()) {
//         // Copy color zones from icon
//         metadata.colorZones = it->second.colorZones;
//     }
    
//     return metadata;
// }

// const std::vector<ColorMapping>& IconManager::GetColorMappings(const std::string& iconId) const {
//     static const std::vector<ColorMapping> empty;
    
//     auto it = icons_.find(iconId);
//     if (it != icons_.end()) {
//         return it->second.colorMappings;
//     }
    
//     return empty;
// }

// const std::vector<ColorZone>& IconManager::GetColorZones(const std::string& iconId) const {
//     static const std::vector<ColorZone> empty;
    
//     auto it = icons_.find(iconId);
//     if (it != icons_.end()) {
//         return it->second.colorZones;
//     }
    
//     return empty;
// }

// } // namespace VectorGraphics









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
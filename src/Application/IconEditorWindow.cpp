// #include "Application.h"
// #include <UI/IconManager.h>
// #include <DesignSystem/DesignSystem.h>
// #include <string>
// #include <vector>

// namespace App {

// void Application::RenderIconEditorWindow() {
//     if (!showIconEditor_) return;
    
//     ImGui::Begin("Icon Editor", &showIconEditor_);
    
//     auto& iconManager = UI::IconManager::Instance();
//     auto& ds = DesignSystem::DesignSystem::Instance();
    
//     // ========== ICON SELECTOR ==========
//     static std::string selectedIcon = "";
//     static int selectedIconIdx = -1;
//     std::vector<std::string> iconList = iconManager.GetLoadedIcons();
    
//     if (!iconList.empty() && selectedIcon.empty()) {
//         selectedIcon = iconList[0];
//         selectedIconIdx = 0;
//     }
    
//     ImGui::SeparatorText("Select Icon");
    
//     if (ImGui::BeginCombo("##IconSelector", selectedIcon.c_str())) {
//         for (int i = 0; i < iconList.size(); ++i) {
//             bool isSelected = (selectedIconIdx == i);
//             if (ImGui::Selectable(iconList[i].c_str(), isSelected)) {
//                 selectedIcon = iconList[i];
//                 selectedIconIdx = i;
//             }
//             if (isSelected) {
//                 ImGui::SetItemDefaultFocus();
//             }
//         }
//         ImGui::EndCombo();
//     }
    
//     if (selectedIcon.empty()) {
//         ImGui::Text("No icon loaded");
//         ImGui::End();
//         return;
//     }
    
//     // ========== PREVIEW ==========
//     ImGui::SeparatorText("Preview");
    
//     float previewSize = 128.0f;
//     float availWidth = ImGui::GetContentRegionAvail().x;
//     ImGui::SetCursorPosX((availWidth - previewSize) * 0.5f);
    
//     iconManager.RenderIcon(selectedIcon, previewSize);
    
//     ImGui::Separator();
    
//     // ========== MODE SELECTION ==========
//     ImGui::SeparatorText("Color Mode");
    
//     UI::IconMetadata* metadata = iconManager.GetIconMetadata(selectedIcon);
//     if (!metadata) {
//         ImGui::Text("No metadata for this icon");
//         ImGui::End();
//         return;
//     }
    
//     int currentMode = static_cast<int>(metadata->scheme);
//     const char* modes[] = { "Original (SVG colors)", "Bicolor (Primary/Secondary)", "Multicolor (Custom)" };
    
//     if (ImGui::Combo("Mode", &currentMode, modes, 3)) {
//         metadata->scheme = static_cast<UI::IconColorScheme>(currentMode);
//         // IMPORTANT: Invalider le cache (sera nettoyé au prochain frame)
//         iconManager.InvalidateCache();
//     }
    
//     ImGui::Separator();
    
//     // ========== MODE ORIGINAL (non éditable) ==========
//     if (metadata->scheme == UI::IconColorScheme::Original) {
//         ImGui::SeparatorText("Original Colors");
//         ImGui::TextWrapped("This icon uses its original SVG colors. "
//                           "These colors cannot be edited. Switch to Bicolor or Multicolor mode to customize.");
        
//         int numZones = static_cast<int>(metadata->colorZones.size());
//         ImGui::Text("Detected color zones: %d", numZones);
        
//         for (int i = 0; i < numZones; ++i) {
//             ImGui::PushID(i);
//             std::string label = "Zone " + std::to_string(i);
//             ImGui::ColorButton(label.c_str(), metadata->colorZones[i].customColor, 
//                              ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
//                              ImVec2(30, 30));
//             ImGui::SameLine();
//             ImGui::Text("Color Zone %d", i);
//             ImGui::PopID();
//         }
        
//         ImGui::End();
//         return;
//     }
    
//     // ========== GLOBAL COLORS (BICOLOR MODE) ==========
//     if (metadata->scheme == UI::IconColorScheme::Bicolor) {
//         ImGui::SeparatorText("Global Colors (Design System)");
        
//         ImVec4 primaryColor = ds.GetColor("semantic.icon.color.primary");
//         ImVec4 secondaryColor = ds.GetColor("semantic.icon.color.secondary");
        
//         ImGui::Text("Primary:");
//         ImGui::ColorButton("##primary_display", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
        
//         ImGui::Text("Secondary:");
//         ImGui::ColorButton("##secondary_display", secondaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
        
//         ImGui::Text("(Tokens from design system)");
        
//         ImGui::Separator();
//     }
    
//     // ========== COLOR ZONE CONFIGURATION ==========
//     ImGui::SeparatorText("Color Zones Configuration");
    
//     int numZones = iconManager.GetColorZoneCount(selectedIcon);
//     ImGui::Text("Number of color zones: %d", numZones);
    
//     if (numZones == 0) {
//         ImGui::Text("No color zones detected in this icon");
//         ImGui::End();
//         return;
//     }
    
//     ImGui::Spacing();
    
//     // Render configuration for each zone
//     for (int zoneIdx = 0; zoneIdx < numZones; ++zoneIdx) {
//         if (zoneIdx >= metadata->colorZones.size()) break;
        
//         UI::ColorZone& zone = metadata->colorZones[zoneIdx];
        
//         ImGui::PushID(zoneIdx);
        
//         std::string zoneHeader = "Color Zone " + std::to_string(zoneIdx);
//         if (ImGui::CollapsingHeader(zoneHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
//             ImGui::Indent();
            
//             // Show original color
//             ImGui::Text("Original:");
//             ImGui::SameLine();
//             ImGui::ColorButton("##original", zone.customColor, 
//                              ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
//                              ImVec2(30, 30));
            
//             ImGui::Spacing();
            
//             if (metadata->scheme == UI::IconColorScheme::Bicolor) {
//                 // ===== BICOLOR MODE =====
//                 ImGui::Text("Assign to:");
                
//                 if (ImGui::RadioButton("Primary", zone.usePrimary)) {
//                     zone.usePrimary = true;
//                     iconManager.InvalidateCache();
//                 }
                
//                 if (ImGui::RadioButton("Secondary", !zone.usePrimary)) {
//                     zone.usePrimary = false;
//                     iconManager.InvalidateCache();
//                 }
                
//             } else if (metadata->scheme == UI::IconColorScheme::Multicolor) {
//                 // ===== MULTICOLOR MODE =====
//                 if (ImGui::ColorEdit4("Custom Color", (float*)&zone.customColor)) {
//                     iconManager.InvalidateCache();
//                 }
//             }
            
//             ImGui::Unindent();
//         }
        
//         ImGui::PopID();
//     }
    
//     ImGui::Separator();
    
//     // ========== ACTIONS ==========
//     ImGui::SeparatorText("Actions");
    
//     if (ImGui::Button("Reset to Original Colors", ImVec2(-1, 0))) {
//         // Recharger les couleurs originales
//         for (auto& zone : metadata->colorZones) {
//             zone.usePrimary = true;
//             // customColor est déjà la couleur originale
//         }
//         iconManager.InvalidateCache();
//     }
    
//     ImGui::Spacing();
    
//     if (ImGui::Button("Switch to Original Mode", ImVec2(-1, 0))) {
//         metadata->scheme = UI::IconColorScheme::Original;
//         iconManager.InvalidateCache();
//     }
    
//     ImGui::Spacing();
    
//     // Info text
//     ImGui::TextWrapped("Tip: Changes are applied in real-time. The preview updates automatically.");
//     ImGui::TextWrapped("Note: Gradients cannot be edited and will appear in Original mode.");
    
//     ImGui::End();
// }

// } // namespace App







#include "Application.h"
#include <UI/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <string>
#include <vector>
#include <map>

namespace App {

// Static storage for local metadata (per icon instance in editor)
// This is separate from the global icon template
static std::map<std::string, UI::IconMetadata> editorLocalMetadata;

void Application::RenderIconEditorWindow() {
    if (!showIconEditor_) return;
    
    ImGui::Begin("Icon Editor", &showIconEditor_);
    
    auto& iconManager = UI::IconManager::Instance();
    auto& ds = DesignSystem::DesignSystem::Instance();
    
    // ========== ICON SELECTOR ==========
    static std::string selectedIcon = "";
    static int selectedIconIdx = -1;
    std::vector<std::string> iconList = iconManager.GetLoadedIcons();
    
    if (!iconList.empty() && selectedIcon.empty()) {
        selectedIcon = iconList[0];
        selectedIconIdx = 0;
        
        // Initialize local metadata for this icon (COPY from global)
        if (editorLocalMetadata.find(selectedIcon) == editorLocalMetadata.end()) {
            editorLocalMetadata[selectedIcon] = iconManager.GetIconMetadataCopy(selectedIcon);
        }
    }
    
    ImGui::SeparatorText("Select Icon");
    
    if (ImGui::BeginCombo("##IconSelector", selectedIcon.c_str())) {
        for (int i = 0; i < iconList.size(); ++i) {
            bool isSelected = (selectedIconIdx == i);
            if (ImGui::Selectable(iconList[i].c_str(), isSelected)) {
                selectedIcon = iconList[i];
                selectedIconIdx = i;
                
                // Initialize local metadata if not present (COPY from global)
                if (editorLocalMetadata.find(selectedIcon) == editorLocalMetadata.end()) {
                    editorLocalMetadata[selectedIcon] = iconManager.GetIconMetadataCopy(selectedIcon);
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    if (selectedIcon.empty()) {
        ImGui::Text("No icon loaded");
        ImGui::End();
        return;
    }
    
    // Get LOCAL metadata for this editor instance
    UI::IconMetadata& localMetadata = editorLocalMetadata[selectedIcon];
    
    // ========== PREVIEW ==========
    ImGui::SeparatorText("Preview");
    
    float previewSize = 128.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((availWidth - previewSize) * 0.5f);
    
    // CRITICAL: Render with LOCAL metadata (won't affect other instances)
    iconManager.RenderIcon(selectedIcon, previewSize, localMetadata);
    
    ImGui::Separator();
    
    // ========== MODE SELECTION ==========
    ImGui::SeparatorText("Color Mode");
    
    int currentMode = static_cast<int>(localMetadata.scheme);
    const char* modes[] = { "Original (SVG colors)", "Bicolor (Primary/Secondary)", "Multicolor (Custom)" };
    
    if (ImGui::Combo("Mode", &currentMode, modes, 3)) {
        localMetadata.scheme = static_cast<UI::IconColorScheme>(currentMode);
        // IMPORTANT: Invalidate cache (will be cleaned up next frame)
        iconManager.InvalidateCache();
    }
    
    ImGui::Separator();
    
    // ========== MODE ORIGINAL (non editable) ==========
    if (localMetadata.scheme == UI::IconColorScheme::Original) {
        ImGui::SeparatorText("Original Colors");
        ImGui::TextWrapped("This icon uses its original SVG colors. "
                          "These colors cannot be edited. Switch to Bicolor or Multicolor mode to customize.");
        
        int numZones = static_cast<int>(localMetadata.colorZones.size());
        ImGui::Text("Detected color zones: %d", numZones);
        
        if (numZones == 0) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No color zones detected in this icon.");
            ImGui::TextWrapped("This might be because:\n"
                             "- The icon uses gradients only (like manga_icon)\n"
                             "- All colors have very low opacity\n"
                             "- The icon is empty");
        }
        
        for (int i = 0; i < numZones; ++i) {
            ImGui::PushID(i);
            std::string label = "Zone " + std::to_string(i);
            ImGui::ColorButton(label.c_str(), localMetadata.colorZones[i].customColor, 
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                             ImVec2(30, 30));
            ImGui::SameLine();
            ImGui::Text("Color Zone %d", i);
            ImGui::PopID();
        }
        
        ImGui::End();
        return;
    }
    
    // ========== GLOBAL COLORS (BICOLOR MODE) ==========
    if (localMetadata.scheme == UI::IconColorScheme::Bicolor) {
        ImGui::SeparatorText("Global Colors (Design System)");
        
        ImVec4 primaryColor = ds.GetColor("semantic.icon.color.primary");
        ImVec4 secondaryColor = ds.GetColor("semantic.icon.color.secondary");
        
        ImGui::Text("Primary:");
        ImGui::ColorButton("##primary_display", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
        
        ImGui::Text("Secondary:");
        ImGui::ColorButton("##secondary_display", secondaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
        
        ImGui::TextWrapped("(Colors from design system tokens)");
        
        ImGui::Separator();
    }
    
    // ========== COLOR ZONE CONFIGURATION ==========
    ImGui::SeparatorText("Color Zones Configuration");
    
    int numZones = static_cast<int>(localMetadata.colorZones.size());
    ImGui::Text("Number of color zones: %d", numZones);
    
    if (numZones == 0) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No color zones detected in this icon.");
        ImGui::TextWrapped("This might be because:\n"
                         "- The icon uses gradients only (like manga_icon)\n"
                         "- All colors have very low opacity (alpha < 10)\n"
                         "- The icon is empty\n\n"
                         "Try switching to Original mode to view the icon.");
        ImGui::End();
        return;
    }
    
    ImGui::Spacing();
    
    // Render configuration for each zone
    for (int zoneIdx = 0; zoneIdx < numZones; ++zoneIdx) {
        if (zoneIdx >= localMetadata.colorZones.size()) break;
        
        UI::ColorZone& zone = localMetadata.colorZones[zoneIdx];
        
        ImGui::PushID(zoneIdx);
        
        std::string zoneHeader = "Color Zone " + std::to_string(zoneIdx);
        if (ImGui::CollapsingHeader(zoneHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Show original color
            ImGui::Text("Original:");
            ImGui::SameLine();
            ImGui::ColorButton("##original", zone.customColor, 
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                             ImVec2(30, 30));
            
            ImGui::Spacing();
            
            if (localMetadata.scheme == UI::IconColorScheme::Bicolor) {
                // ===== BICOLOR MODE =====
                ImGui::Text("Assign to:");
                
                if (ImGui::RadioButton("Primary", zone.usePrimary)) {
                    zone.usePrimary = true;
                    iconManager.InvalidateCache();
                }
                
                if (ImGui::RadioButton("Secondary", !zone.usePrimary)) {
                    zone.usePrimary = false;
                    iconManager.InvalidateCache();
                }
                
            } else if (localMetadata.scheme == UI::IconColorScheme::Multicolor) {
                // ===== MULTICOLOR MODE =====
                if (ImGui::ColorEdit4("Custom Color", (float*)&zone.customColor)) {
                    iconManager.InvalidateCache();
                }
            }
            
            ImGui::Unindent();
        }
        
        ImGui::PopID();
    }
    
    ImGui::Separator();
    
    // ========== ACTIONS ==========
    ImGui::SeparatorText("Actions");
    
    if (ImGui::Button("Reset to Original Colors", ImVec2(-1, 0))) {
        // Reload from global template
        localMetadata = iconManager.GetIconMetadataCopy(selectedIcon);
        iconManager.InvalidateCache();
    }
    
    ImGui::Spacing();
    
    if (ImGui::Button("Apply to Global Template", ImVec2(-1, 0))) {
        // Apply local changes to the global template
        // This will affect NEW instances but not existing ones
        iconManager.SetIconMetadata(selectedIcon, localMetadata);
        ImGui::OpenPopup("Applied");
    }
    
    if (ImGui::BeginPopup("Applied")) {
        ImGui::Text("Changes applied to global template!");
        ImGui::Text("This affects new instances only.");
        ImGui::EndPopup();
    }
    
    ImGui::Spacing();
    
    // Info text
    ImGui::TextWrapped("Note: Changes here only affect this preview.");
    ImGui::TextWrapped("The icon in the toolbar and other parts of the app are NOT affected.");
    ImGui::TextWrapped("Click 'Apply to Global Template' to make changes permanent for new instances.");
    
    ImGui::Separator();
    
    // Debug info
    if (ImGui::TreeNode("Debug Info")) {
        ImGui::Text("Icon: %s", selectedIcon.c_str());
        ImGui::Text("Mode: %d", static_cast<int>(localMetadata.scheme));
        ImGui::Text("Zones: %d", numZones);
        
        for (int i = 0; i < numZones; ++i) {
            ImGui::Text("Zone %d: #%08X", i, localMetadata.colorZones[i].originalColor);
        }
        
        ImGui::TreePop();
    }
    
    ImGui::End();
}

} // namespace App
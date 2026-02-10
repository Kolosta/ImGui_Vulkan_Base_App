#include <VectorGraphics/editors/IconEditorWindow.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <vector>

namespace VectorGraphics {

// Static storage for local metadata
std::map<std::string, IconMetadata> IconEditorWindow::editorLocalMetadata_;

void IconEditorWindow::Render(bool* pOpen) {
    if (!pOpen || !*pOpen) return;
    
    ImGui::Begin("Icon Editor", pOpen);
    
    RenderIconSelector();
    
    if (selectedIcon_.empty()) {
        ImGui::Text("No icon selected");
        ImGui::End();
        return;
    }
    
    RenderPreview();
    ImGui::Separator();
    
    RenderModeSelector();
    ImGui::Separator();
    
    auto& localMetadata = editorLocalMetadata_[selectedIcon_];
    
    if (localMetadata.scheme == IconColorScheme::Original) {
        RenderOriginalMode();
    } else if (localMetadata.scheme == IconColorScheme::Bicolor) {
        RenderBicolorMode();
        ImGui::Separator();
        RenderColorZoneConfiguration();
    } else if (localMetadata.scheme == IconColorScheme::Multicolor) {
        RenderMulticolorMode();
        ImGui::Separator();
        RenderColorZoneConfiguration();
    }
    
    ImGui::Separator();
    RenderActions();
    
    ImGui::Separator();
    RenderDebugInfo();
    
    ImGui::End();
}

void IconEditorWindow::RenderIconSelector() {
    auto& iconManager = IconManager::Instance();
    std::vector<std::string> iconList = iconManager.GetLoadedIcons();
    
    if (!iconList.empty() && selectedIcon_.empty()) {
        selectedIcon_ = iconList[0];
        selectedIconIdx_ = 0;
        
        // Initialize local metadata for this icon (COPY from global)
        if (editorLocalMetadata_.find(selectedIcon_) == editorLocalMetadata_.end()) {
            editorLocalMetadata_[selectedIcon_] = iconManager.GetIconMetadataCopy(selectedIcon_);
        }
    }
    
    ImGui::SeparatorText("Select Icon");
    
    if (ImGui::BeginCombo("##IconSelector", selectedIcon_.c_str())) {
        for (size_t i = 0; i < iconList.size(); ++i) {
            bool isSelected = (selectedIconIdx_ == static_cast<int>(i));
            if (ImGui::Selectable(iconList[i].c_str(), isSelected)) {
                selectedIcon_ = iconList[i];
                selectedIconIdx_ = static_cast<int>(i);
                
                // Initialize local metadata if not present (COPY from global)
                if (editorLocalMetadata_.find(selectedIcon_) == editorLocalMetadata_.end()) {
                    editorLocalMetadata_[selectedIcon_] = iconManager.GetIconMetadataCopy(selectedIcon_);
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void IconEditorWindow::RenderPreview() {
    ImGui::SeparatorText("Preview");
    
    auto& iconManager = IconManager::Instance();
    IconMetadata& localMetadata = editorLocalMetadata_[selectedIcon_];
    
    float previewSize = 128.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((availWidth - previewSize) * 0.5f);
    
    // CRITICAL: Render with LOCAL metadata (won't affect other instances)
    iconManager.RenderIcon(selectedIcon_, previewSize, localMetadata);
}

void IconEditorWindow::RenderModeSelector() {
    ImGui::SeparatorText("Color Mode");
    
    auto& iconManager = IconManager::Instance();
    IconMetadata& localMetadata = editorLocalMetadata_[selectedIcon_];
    
    int currentMode = static_cast<int>(localMetadata.scheme);
    const char* modes[] = { 
        "Original (SVG colors)", 
        "Bicolor (Primary/Secondary/Tertiary)", 
        "Multicolor (Custom)" 
    };
    
    if (ImGui::Combo("Mode", &currentMode, modes, 3)) {
        localMetadata.scheme = static_cast<IconColorScheme>(currentMode);
        iconManager.InvalidateCache();
    }
}

void IconEditorWindow::RenderOriginalMode() {
    ImGui::SeparatorText("Original Colors");
    ImGui::TextWrapped("This icon uses its original SVG colors. "
                      "These colors cannot be edited. Switch to Bicolor or Multicolor mode to customize.");
    
    IconMetadata& localMetadata = editorLocalMetadata_[selectedIcon_];
    int numZones = static_cast<int>(localMetadata.colorZones.size());
    ImGui::Text("Detected color zones: %d", numZones);
    
    if (numZones == 0) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No color zones detected in this icon.");
        ImGui::TextWrapped("This might be because:\n"
                         "- The icon uses gradients only\n"
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
}

void IconEditorWindow::RenderBicolorMode() {
    ImGui::SeparatorText("Global Colors (Design System)");
    
    auto& ds = DesignSystem::DesignSystem::Instance();
    
    ImVec4 primaryColor = ds.GetColor("semantic.icon.color.primary");
    ImVec4 secondaryColor = ds.GetColor("semantic.icon.color.secondary");
    ImVec4 tertiaryColor = ds.GetColor("semantic.icon.color.tertiary");
    
    ImGui::Text("Primary:");
    ImGui::SameLine();
    ImGui::ColorButton("##primary_display", primaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
    
    ImGui::Text("Secondary:");
    ImGui::SameLine();
    ImGui::ColorButton("##secondary_display", secondaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
    
    ImGui::Text("Tertiary:");
    ImGui::SameLine();
    ImGui::ColorButton("##tertiary_display", tertiaryColor, ImGuiColorEditFlags_NoTooltip, ImVec2(30, 30));
    
    ImGui::TextWrapped("(Colors from design system tokens)");
}

void IconEditorWindow::RenderMulticolorMode() {
    ImGui::SeparatorText("Multicolor Mode");
    ImGui::TextWrapped("In multicolor mode, you can assign a custom color to each color zone.");
}

void IconEditorWindow::RenderColorZoneConfiguration() {
    ImGui::SeparatorText("Color Zones Configuration");
    
    auto& iconManager = IconManager::Instance();
    IconMetadata& localMetadata = editorLocalMetadata_[selectedIcon_];
    
    int numZones = static_cast<int>(localMetadata.colorZones.size());
    ImGui::Text("Number of color zones: %d", numZones);
    
    if (numZones == 0) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No color zones detected in this icon.");
        ImGui::TextWrapped("This might be because:\n"
                         "- The icon uses gradients only\n"
                         "- All colors have very low opacity\n"
                         "- The icon is empty\n\n"
                         "Try switching to Original mode to view the icon.");
        return;
    }
    
    ImGui::Spacing();
    
    // Render configuration for each zone
    for (int zoneIdx = 0; zoneIdx < numZones; ++zoneIdx) {
        if (zoneIdx >= static_cast<int>(localMetadata.colorZones.size())) break;
        
        ColorZone& zone = localMetadata.colorZones[zoneIdx];
        
        ImGui::PushID(zoneIdx);
        
        std::string zoneHeader = "Color Zone " + std::to_string(zoneIdx);
        if (zone.hasToken) {
            zoneHeader += " (" + zone.tokenName + ")";
        }
        
        if (ImGui::CollapsingHeader(zoneHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Show original color
            ImGui::Text("Original:");
            ImGui::SameLine();
            ImGui::ColorButton("##original", zone.customColor, 
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                             ImVec2(30, 30));
            
            ImGui::Spacing();
            
            if (localMetadata.scheme == IconColorScheme::Bicolor) {
                // ===== BICOLOR MODE =====
                if (zone.hasToken) {
                    ImGui::Text("Assigned to: %s", zone.tokenName.c_str());
                    ImGui::TextWrapped("(Defined by CSS class in SVG)");
                } else {
                    ImGui::Text("Uses original color");
                    ImGui::TextWrapped("(No CSS class assigned in SVG)");
                }
                
            } else if (localMetadata.scheme == IconColorScheme::Multicolor) {
                // ===== MULTICOLOR MODE =====
                if (ImGui::ColorEdit4("Custom Color", (float*)&zone.customColor)) {
                    iconManager.InvalidateCache();
                }
            }
            
            ImGui::Unindent();
        }
        
        ImGui::PopID();
    }
}

void IconEditorWindow::RenderActions() {
    ImGui::SeparatorText("Actions");
    
    auto& iconManager = IconManager::Instance();
    
    if (ImGui::Button("Reset to Original Colors", ImVec2(-1, 0))) {
        // Reload from global template
        editorLocalMetadata_[selectedIcon_] = iconManager.GetIconMetadataCopy(selectedIcon_);
        iconManager.InvalidateCache();
    }
    
    ImGui::Spacing();
    
    if (ImGui::Button("Apply to Global Template", ImVec2(-1, 0))) {
        // Apply local changes to the global template
        // This will affect NEW instances but not existing ones
        iconManager.SetIconMetadata(selectedIcon_, editorLocalMetadata_[selectedIcon_]);
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
}

void IconEditorWindow::RenderDebugInfo() {
    IconMetadata& localMetadata = editorLocalMetadata_[selectedIcon_];
    int numZones = static_cast<int>(localMetadata.colorZones.size());
    
    if (ImGui::TreeNode("Debug Info")) {
        ImGui::Text("Icon: %s", selectedIcon_.c_str());
        ImGui::Text("Mode: %d", static_cast<int>(localMetadata.scheme));
        ImGui::Text("Zones: %d", numZones);
        
        for (int i = 0; i < numZones; ++i) {
            ImGui::Text("Zone %d: #%08X (token: %s)", 
                i, 
                localMetadata.colorZones[i].originalColor,
                localMetadata.colorZones[i].hasToken ? localMetadata.colorZones[i].tokenName.c_str() : "none");
        }
        
        ImGui::TreePop();
    }
}

} // namespace VectorGraphics
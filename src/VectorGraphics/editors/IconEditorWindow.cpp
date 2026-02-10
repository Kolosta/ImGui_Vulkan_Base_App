#include <VectorGraphics/editors/IconEditorWindow.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <vector>
#include <iomanip>
#include <sstream>

namespace VectorGraphics {

void IconEditorWindow::Render(bool* pOpen) {
    if (!pOpen || !*pOpen) return;
    
    ImGui::Begin("Icon Editor", pOpen, ImGuiWindowFlags_AlwaysAutoResize);
    
    RenderIconSelector();
    
    if (selectedIcon_.empty()) {
        ImGui::Text("No icon selected");
        ImGui::End();
        return;
    }
    
    ImGui::Separator();
    RenderPreview();
    
    ImGui::Separator();
    RenderModeSelector();
    
    ImGui::Separator();
    RenderColorZonesConfiguration();
    
    ImGui::Separator();
    RenderActions();
    
    if (ImGui::CollapsingHeader("Debug Info")) {
        RenderDebugInfo();
    }
    
    ImGui::End();
}

void IconEditorWindow::RenderIconSelector() {
    auto& iconManager = IconManager::Instance();
    std::vector<std::string> iconList = iconManager.GetLoadedIcons();
    
    if (iconList.empty()) {
        ImGui::Text("No icons loaded");
        return;
    }
    
    // Initialize on first run
    if (selectedIcon_.empty() && !iconList.empty()) {
        selectedIcon_ = iconList[0];
        selectedIconIdx_ = 0;
        localMetadata_ = iconManager.GetDefaultMetadata(selectedIcon_);
    }
    
    ImGui::SeparatorText("Select Icon");
    
    if (ImGui::BeginCombo("##IconSelector", selectedIcon_.c_str())) {
        for (size_t i = 0; i < iconList.size(); ++i) {
            bool isSelected = (selectedIconIdx_ == static_cast<int>(i));
            if (ImGui::Selectable(iconList[i].c_str(), isSelected)) {
                selectedIcon_ = iconList[i];
                selectedIconIdx_ = static_cast<int>(i);
                localMetadata_ = iconManager.GetDefaultMetadata(selectedIcon_);
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
    
    float previewSize = 128.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((availWidth - previewSize) * 0.5f);
    
    // Render with local metadata
    iconManager.RenderIcon(selectedIcon_, previewSize, localMetadata_);
    
    ImGui::Text("(Live preview with current settings)");
}

void IconEditorWindow::RenderModeSelector() {
    ImGui::SeparatorText("Color Mode");
    
    auto& iconManager = IconManager::Instance();
    
    int currentMode = static_cast<int>(localMetadata_.scheme);
    const char* modes[] = { 
        "Original (SVG colors)", 
        "Bicolor (Primary/Secondary)", 
        "Multicolor (Custom per color)" 
    };
    
    if (ImGui::Combo("##Mode", &currentMode, modes, 3)) {
        localMetadata_.scheme = static_cast<IconColorScheme>(currentMode);
        iconManager.InvalidateCache();
    }
    
    ImGui::Spacing();
    
    // Mode description
    switch (localMetadata_.scheme) {
        case IconColorScheme::Original:
            ImGui::TextWrapped("Uses original colors from the SVG file.");
            break;
        case IconColorScheme::Bicolor:
            ImGui::TextWrapped("Uses design system tokens (primary/secondary). "
                             "Assign each color to a token below.");
            break;
        case IconColorScheme::Multicolor:
            ImGui::TextWrapped("Customize each color independently.");
            break;
    }
}

void IconEditorWindow::RenderColorZonesConfiguration() {
    ImGui::SeparatorText("Color Configuration");
    
    auto& iconManager = IconManager::Instance();
    
    int numZones = static_cast<int>(localMetadata_.colorZones.size());
    
    if (numZones == 0) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0, 1), "No colors detected in this icon.");
        ImGui::TextWrapped(
            "This could mean:\n"
            "- The icon has no fill/stroke colors\n"
            "- All elements use references (like gradients only)\n"
            "- The SVG uses unsupported color formats"
        );
        return;
    }
    
    ImGui::Text("Detected %d unique color%s:", numZones, numZones > 1 ? "s" : "");
    ImGui::Spacing();
    
    // Global design system colors (for reference in bicolor mode)
    ImVec4 dsPrimary(0, 0, 0, 1);
    ImVec4 dsSecondary(0, 0, 0, 1);
    
    if (localMetadata_.scheme == IconColorScheme::Bicolor) {
        auto& ds = DesignSystem::DesignSystem::Instance();
        dsPrimary = ds.GetColor("semantic.icon.color.primary");
        dsSecondary = ds.GetColor("semantic.icon.color.secondary");
        
        ImGui::Text("Design System Colors:");
        ImGui::SameLine();
        ImGui::ColorButton("##ds_primary", dsPrimary, 
                          ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                          ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("Primary");
        
        ImGui::SameLine(0, 20);
        ImGui::ColorButton("##ds_secondary", dsSecondary, 
                          ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                          ImVec2(20, 20));
        ImGui::SameLine();
        ImGui::Text("Secondary");
        
        ImGui::Separator();
    }
    
    // Render each color zone
    for (int zoneIdx = 0; zoneIdx < numZones; ++zoneIdx) {
        ColorZone& zone = localMetadata_.colorZones[zoneIdx];
        
        ImGui::PushID(zoneIdx);
        
        // Convert original color to hex for display
        std::ostringstream hexStream;
        hexStream << "#" << std::hex << std::setfill('0')
                  << std::setw(2) << (zone.originalColor & 0xFF)
                  << std::setw(2) << ((zone.originalColor >> 8) & 0xFF)
                  << std::setw(2) << ((zone.originalColor >> 16) & 0xFF);
        std::string originalHex = hexStream.str();
        
        std::string header = "Color " + std::to_string(zoneIdx + 1) + " (" + originalHex + ")";
        
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            // Original color preview
            ImGui::Text("Original:");
            ImGui::SameLine();
            ImVec4 originalColor = ImVec4(
                (zone.originalColor & 0xFF) / 255.0f,
                ((zone.originalColor >> 8) & 0xFF) / 255.0f,
                ((zone.originalColor >> 16) & 0xFF) / 255.0f,
                ((zone.originalColor >> 24) & 0xFF) / 255.0f
            );
            ImGui::ColorButton("##original", originalColor, 
                             ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                             ImVec2(30, 30));
            
            ImGui::Spacing();
            
            if (localMetadata_.scheme == IconColorScheme::Original) {
                ImGui::TextWrapped("Mode: Original - no customization");
                
            } else if (localMetadata_.scheme == IconColorScheme::Bicolor) {
                ImGui::Text("Assign to:");
                
                bool isPrimary = (zone.tokenAssignment == "primary");
                bool isSecondary = (zone.tokenAssignment == "secondary");
                
                if (ImGui::RadioButton("Primary", isPrimary)) {
                    zone.tokenAssignment = "primary";
                    iconManager.InvalidateCache();
                }
                
                ImGui::SameLine();
                if (ImGui::RadioButton("Secondary", isSecondary)) {
                    zone.tokenAssignment = "secondary";
                    iconManager.InvalidateCache();
                }
                
                // Preview of chosen color
                ImGui::Text("Result:");
                ImGui::SameLine();
                ImVec4 previewColor = isPrimary ? dsPrimary : dsSecondary;
                ImGui::ColorButton("##preview", previewColor, 
                                 ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, 
                                 ImVec2(30, 30));
                
            } else if (localMetadata_.scheme == IconColorScheme::Multicolor) {
                ImGui::Text("Custom Color:");
                if (ImGui::ColorEdit4("##custom", (float*)&zone.customColor, 
                                     ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                    iconManager.InvalidateCache();
                }
            }
            
            // Show how many times this color appears
            ImGui::Spacing();
            ImGui::TextDisabled("Used in %zu location(s)", zone.mappingIndices.size());
            
            ImGui::Unindent();
            ImGui::TreePop();
        }
        
        ImGui::PopID();
    }
}

void IconEditorWindow::RenderActions() {
    ImGui::SeparatorText("Actions");
    
    auto& iconManager = IconManager::Instance();
    
    if (ImGui::Button("Reset Current Mode", ImVec2(-1, 0))) {
        // Reset only within current mode
        IconMetadata defaultMeta = iconManager.GetDefaultMetadata(selectedIcon_);
        
        if (localMetadata_.scheme == IconColorScheme::Multicolor) {
            // Reset custom colors to original
            for (auto& zone : localMetadata_.colorZones) {
                zone.customColor = ImVec4(
                    (zone.originalColor & 0xFF) / 255.0f,
                    ((zone.originalColor >> 8) & 0xFF) / 255.0f,
                    ((zone.originalColor >> 16) & 0xFF) / 255.0f,
                    ((zone.originalColor >> 24) & 0xFF) / 255.0f
                );
            }
        } else if (localMetadata_.scheme == IconColorScheme::Bicolor) {
            // Reset token assignments to defaults
            localMetadata_.colorZones = defaultMeta.colorZones;
        }
        
        iconManager.InvalidateCache();
    }
    
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Note: Changes here affect only this preview. "
        "Icons used elsewhere in the application maintain their own settings."
    );
}

void IconEditorWindow::RenderDebugInfo() {
    auto& iconManager = IconManager::Instance();
    const auto& mappings = iconManager.GetColorMappings(selectedIcon_);
    
    ImGui::Text("Icon: %s", selectedIcon_.c_str());
    ImGui::Text("Mode: %d", static_cast<int>(localMetadata_.scheme));
    ImGui::Text("Unique Colors: %zu", localMetadata_.colorZones.size());
    ImGui::Text("Total Mappings: %zu", mappings.size());
    
    if (ImGui::TreeNode("Color Mappings")) {
        for (size_t i = 0; i < mappings.size(); ++i) {
            const auto& m = mappings[i];
            ImGui::Text("%zu: %s.%s = #%06X (class: %s)", 
                i,
                m.elementId.c_str(),
                m.property.c_str(),
                m.originalColor & 0xFFFFFF,
                m.cssClass.empty() ? "none" : m.cssClass.c_str()
            );
        }
        ImGui::TreePop();
    }
    
    if (ImGui::TreeNode("Color Zones")) {
        for (size_t i = 0; i < localMetadata_.colorZones.size(); ++i) {
            const auto& zone = localMetadata_.colorZones[i];
            ImGui::Text("%zu: #%06X -> %s (%zu mappings)", 
                i,
                zone.originalColor & 0xFFFFFF,
                zone.tokenAssignment.c_str(),
                zone.mappingIndices.size()
            );
        }
        ImGui::TreePop();
    }
}

} // namespace VectorGraphics
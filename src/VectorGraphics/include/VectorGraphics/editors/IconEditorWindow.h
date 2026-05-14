#pragma once

#include <VectorGraphics/IconManager.h>
#include <VectorGraphics/IconMetadata.h>
#include <string>

namespace VectorGraphics {

/**
 * Icon editor window for per-instance icon customization
 * Each editor session maintains its own local metadata
 */
class IconEditorWindow {
public:
    IconEditorWindow() = default;
    ~IconEditorWindow() = default;
    
    /**
     * Render the icon editor window
     * @param pOpen Pointer to bool controlling window visibility
     */
    void Render(bool* pOpen = nullptr);

    // Contenu seul — utilisé à l'intérieur de la fenêtre Paramètres.
    void RenderContent();

private:
    void RenderIconSelector();
    void RenderPreview();
    void RenderModeSelector();
    void RenderColorZonesConfiguration();
    void RenderActions();
    void RenderDebugInfo();
    void RenderIconList();
    void RenderIconDetails();
    void RenderIconPreview();
    
    std::string selectedIcon_;
    int selectedIconIdx_ = -1;
    IconMetadata localMetadata_;  // Current editing session metadata
    std::string selectedIconId_;
    bool        showGrid_      = true;
    float       previewScale_  = 1.0f;
};

} // namespace VectorGraphics
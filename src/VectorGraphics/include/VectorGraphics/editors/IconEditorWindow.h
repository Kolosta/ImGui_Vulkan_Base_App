#pragma once

#include <VectorGraphics/IconManager.h>
#include <VectorGraphics/IconMetadata.h>
#include <string>
#include <map>

namespace VectorGraphics {

/**
 * Icon editor window for visually editing icon color schemes
 * Allows per-instance editing without affecting the global template
 */
class IconEditorWindow {
public:
    IconEditorWindow() = default;
    ~IconEditorWindow() = default;
    
    /**
     * Render the icon editor window
     * @param pOpen Pointer to bool controlling window visibility
     */
    void Render(bool* pOpen);

private:
    void RenderIconSelector();
    void RenderPreview();
    void RenderModeSelector();
    void RenderOriginalMode();
    void RenderBicolorMode();
    void RenderMulticolorMode();
    void RenderColorZoneConfiguration();
    void RenderActions();
    void RenderDebugInfo();
    
    std::string selectedIcon_;
    int selectedIconIdx_ = -1;
    
    // Local metadata storage (per icon instance in editor)
    // This is separate from the global icon template
    static std::map<std::string, IconMetadata> editorLocalMetadata_;
};

} // namespace VectorGraphics
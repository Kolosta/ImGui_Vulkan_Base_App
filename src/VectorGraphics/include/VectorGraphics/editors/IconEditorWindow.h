// #pragma once

// #include <VectorGraphics/IconManager.h>
// #include <VectorGraphics/IconMetadata.h>
// #include <string>

// namespace VectorGraphics {

// /**
//  * Icon editor window for per-instance icon customization
//  * Each editor session maintains its own local metadata
//  */
// class IconEditorWindow {
// public:
//     IconEditorWindow() = default;
//     ~IconEditorWindow() = default;
    
//     /**
//      * Render the icon editor window
//      * @param pOpen Pointer to bool controlling window visibility
//      */
//     void Render(bool* pOpen);

// private:
//     void RenderIconSelector();
//     void RenderPreview();
//     void RenderModeSelector();
//     void RenderColorZonesConfiguration();
//     void RenderActions();
//     void RenderDebugInfo();
    
//     std::string selectedIcon_;
//     int selectedIconIdx_ = -1;
//     IconMetadata localMetadata_;  // Current editing session metadata
// };

// } // namespace VectorGraphics







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
    void Render(bool* pOpen);

private:
    void RenderIconSelector();
    void RenderPreview();
    void RenderModeSelector();
    void RenderColorZonesConfiguration();
    void RenderActions();
    void RenderDebugInfo();
    
    std::string selectedIcon_;
    int selectedIconIdx_ = -1;
    IconMetadata localMetadata_;  // Current editing session metadata
};

} // namespace VectorGraphics
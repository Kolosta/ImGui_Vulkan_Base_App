#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <imgui.h>

namespace DesignSystem {

// Utility class for color blindness simulation and correction
class ColorBlindness {
public:
    // Apply color blindness transformation to a color
    static ImVec4 ApplyColorBlindness(const ImVec4& color, AccessibilityType type);
    
    // Transform matrices for different types of color blindness
    static ImVec4 ApplyProtanopia(const ImVec4& color);
    static ImVec4 ApplyDeuteranopia(const ImVec4& color);
    static ImVec4 ApplyTritanopia(const ImVec4& color);
    
private:
    // Helper function to apply a 3x3 matrix transformation to RGB
    static ImVec4 ApplyMatrix(const ImVec4& color, const float matrix[3][3]);
    
    // Color blindness transformation matrices
    // These are simplified Brettel matrices for simulation
    static constexpr float PROTANOPIA_MATRIX[3][3] = {
        { 0.567f,  0.433f,  0.000f },
        { 0.558f,  0.442f,  0.000f },
        { 0.000f,  0.242f,  0.758f }
    };
    
    static constexpr float DEUTERANOPIA_MATRIX[3][3] = {
        { 0.625f,  0.375f,  0.000f },
        { 0.700f,  0.300f,  0.000f },
        { 0.000f,  0.300f,  0.700f }
    };
    
    static constexpr float TRITANOPIA_MATRIX[3][3] = {
        { 0.950f,  0.050f,  0.000f },
        { 0.000f,  0.433f,  0.567f },
        { 0.000f,  0.475f,  0.525f }
    };
};

} // namespace DesignSystem
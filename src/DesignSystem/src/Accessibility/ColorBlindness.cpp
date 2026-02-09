#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <algorithm>

namespace DesignSystem {

ImVec4 ColorBlindness::ApplyColorBlindness(const ImVec4& color, AccessibilityType type) {
    switch (type) {
        case AccessibilityType::Protanopia:
            return ApplyProtanopia(color);
        case AccessibilityType::Deuteranopia:
            return ApplyDeuteranopia(color);
        case AccessibilityType::Tritanopia:
            return ApplyTritanopia(color);
        case AccessibilityType::None:
        default:
            return color;
    }
}

ImVec4 ColorBlindness::ApplyProtanopia(const ImVec4& color) {
    return ApplyMatrix(color, PROTANOPIA_MATRIX);
}

ImVec4 ColorBlindness::ApplyDeuteranopia(const ImVec4& color) {
    return ApplyMatrix(color, DEUTERANOPIA_MATRIX);
}

ImVec4 ColorBlindness::ApplyTritanopia(const ImVec4& color) {
    return ApplyMatrix(color, TRITANOPIA_MATRIX);
}

ImVec4 ColorBlindness::ApplyMatrix(const ImVec4& color, const float matrix[3][3]) {
    ImVec4 result;
    
    // Apply matrix transformation to RGB components
    result.x = std::clamp(
        color.x * matrix[0][0] + color.y * matrix[0][1] + color.z * matrix[0][2],
        0.0f, 1.0f
    );
    result.y = std::clamp(
        color.x * matrix[1][0] + color.y * matrix[1][1] + color.z * matrix[1][2],
        0.0f, 1.0f
    );
    result.z = std::clamp(
        color.x * matrix[2][0] + color.y * matrix[2][1] + color.z * matrix[2][2],
        0.0f, 1.0f
    );
    
    // Keep alpha unchanged
    result.w = color.w;
    
    return result;
}

} // namespace DesignSystem
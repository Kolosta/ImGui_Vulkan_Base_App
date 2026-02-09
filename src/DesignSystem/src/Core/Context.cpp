#include <DesignSystem/Core/Context.h>
#include <cstdint>
#include <functional>

namespace DesignSystem {

Context::Context() 
    : theme_(ThemeType::Dark), accessibility_(AccessibilityType::None) {}

Context::Context(ThemeType theme, AccessibilityType accessibility)
    : theme_(theme), accessibility_(accessibility) {}

bool Context::operator==(const Context& other) const {
    return theme_ == other.theme_ && accessibility_ == other.accessibility_;
}

bool Context::operator!=(const Context& other) const {
    return !(*this == other);
}

std::size_t Context::Hash::operator()(const Context& ctx) const {
    std::size_t h1 = std::hash<int>{}(static_cast<int>(ctx.theme_));
    std::size_t h2 = std::hash<int>{}(static_cast<int>(ctx.accessibility_));
    return h1 ^ (h2 << 1);
}

/**
 * Serialize context to binary stream.
 * Uses int32_t for cross-platform compatibility.
 */
void Context::WriteToBinary(std::ostream& out) const {
    int32_t theme = static_cast<int32_t>(theme_);
    int32_t accessibility = static_cast<int32_t>(accessibility_);
    out.write(reinterpret_cast<const char*>(&theme), sizeof(int32_t));
    out.write(reinterpret_cast<const char*>(&accessibility), sizeof(int32_t));
}

/**
 * Deserialize context from binary stream.
 */
Context Context::ReadFromBinary(std::istream& in) {
    int32_t theme, accessibility;
    in.read(reinterpret_cast<char*>(&theme), sizeof(int32_t));
    in.read(reinterpret_cast<char*>(&accessibility), sizeof(int32_t));
    return Context(static_cast<ThemeType>(theme), static_cast<AccessibilityType>(accessibility));
}

} // namespace DesignSystem
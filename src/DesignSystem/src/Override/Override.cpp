#include <DesignSystem/Override/Override.h>
#include <cstdint>

namespace DesignSystem {

Override::Override(const std::string& tokenId, const TokenValue& value)
    : tokenId_(tokenId), value_(value), theme_(std::nullopt) {}

Override::Override(const std::string& tokenId, const TokenValue& value, ThemeType theme)
    : tokenId_(tokenId), value_(value), theme_(theme) {}

/**
 * Check if override applies to given theme.
 * Global overrides apply everywhere.
 * Theme-specific overrides only apply when theme matches.
 */
bool Override::AppliesTo(ThemeType theme) const {
    if (IsGlobal()) return true;
    return theme_.has_value() && *theme_ == theme;
}

/**
 * Serialize override to binary stream.
 */
void Override::WriteToBinary(std::ostream& out) const {
    // Write token ID
    uint32_t len = static_cast<uint32_t>(tokenId_.length());
    out.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
    out.write(tokenId_.data(), len);
    
    // Write value type
    int32_t valueType = static_cast<int32_t>(value_.GetType());
    out.write(reinterpret_cast<const char*>(&valueType), sizeof(int32_t));
    
    // Write value
    value_.WriteToBinary(out);
    
    // Write theme (optional)
    bool hasTheme = theme_.has_value();
    out.write(reinterpret_cast<const char*>(&hasTheme), sizeof(bool));
    if (hasTheme) {
        int32_t theme = static_cast<int32_t>(*theme_);
        out.write(reinterpret_cast<const char*>(&theme), sizeof(int32_t));
    }
}

/**
 * Deserialize override from binary stream.
 */
Override Override::ReadFromBinary(std::istream& in) {
    // Read token ID
    uint32_t len;
    in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
    std::string tokenId(len, '\0');
    in.read(&tokenId[0], len);
    
    // Read value type
    int32_t valueTypeInt;
    in.read(reinterpret_cast<char*>(&valueTypeInt), sizeof(int32_t));
    ValueType valueType = static_cast<ValueType>(valueTypeInt);
    
    // Read value
    TokenValue value = TokenValue::ReadFromBinary(in, valueType);
    
    // Read theme
    bool hasTheme;
    in.read(reinterpret_cast<char*>(&hasTheme), sizeof(bool));
    
    if (hasTheme) {
        int32_t themeInt;
        in.read(reinterpret_cast<char*>(&themeInt), sizeof(int32_t));
        return Override(tokenId, value, static_cast<ThemeType>(themeInt));
    } else {
        return Override(tokenId, value);
    }
}

} // namespace DesignSystem
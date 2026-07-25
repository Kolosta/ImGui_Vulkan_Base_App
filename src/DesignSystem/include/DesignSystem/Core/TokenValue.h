#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <imgui.h>
#include <string>
#include <variant>
#include <ostream>
#include <istream>
#include <cstdint>

namespace DesignSystem {

/// A composite text style: each field is a token id (reference) that supplies
/// one typographic axis. Resolved lazily by the consumer (font system).
struct TextStyleRefs {
    std::string family;
    std::string size;
    std::string weight;
    std::string lineHeight;
    std::string tracking;
    bool operator==(const TextStyleRefs& o) const {
        return family == o.family && size == o.size && weight == o.weight &&
               lineHeight == o.lineHeight && tracking == o.tracking;
    }
};

/**
 * Type-safe value storage. Variants: Color, Float, Int, Vec2, Reference, plus
 * Ratio (float fraction), Bezier (ImVec4 = 4 control points) and TextStyle
 * (5 token-id refs). Binary serialization for fast I/O.
 */
class TokenValue {
public:
    TokenValue();
    explicit TokenValue(const ImVec4& color);
    explicit TokenValue(float value);
    explicit TokenValue(int value);
    explicit TokenValue(const ImVec2& vec);
    explicit TokenValue(const std::string& tokenId);

    // Tagged factories for the variants that reuse a backing type.
    static TokenValue MakeRatio(float fraction);
    static TokenValue MakeBezier(const ImVec4& controlPoints);
    static TokenValue MakeTextStyle(const TextStyleRefs& refs);
    static TokenValue MakeFontFamily(const std::string& familyName);

    ValueType GetType() const { return type_; }
    bool IsReference() const { return type_ == ValueType::Reference; }

    /**
     * Type-safe getters (throw std::runtime_error if type mismatch).
     */
    ImVec4 AsColor() const;
    float AsFloat() const;
    int AsInt() const;
    ImVec2 AsVec2() const;
    std::string AsReference() const;
    float AsRatio() const;
    ImVec4 AsBezier() const;
    TextStyleRefs AsTextStyle() const;
    std::string AsFontFamily() const;

    /**
     * Type-safe setters.
     */
    void SetColor(const ImVec4& color);
    void SetFloat(float value);
    void SetInt(int value);
    void SetVec2(const ImVec2& vec);
    void SetReference(const std::string& tokenId);
    void SetRatio(float fraction);
    void SetBezier(const ImVec4& controlPoints);
    void SetTextStyle(const TextStyleRefs& refs);
    void SetFontFamily(const std::string& familyName);
    
    /**
     * Comparison operators (properly handles ImVec types).
     */
    bool operator==(const TokenValue& other) const;
    bool operator!=(const TokenValue& other) const;
    
    /**
     * Binary serialization using std::ostream/istream.
     */
    void WriteToBinary(std::ostream& out) const;
    static TokenValue ReadFromBinary(std::istream& in, ValueType type);

private:
    ValueType type_;
    std::variant<ImVec4, float, int, ImVec2, std::string, TextStyleRefs> value_;
};

} // namespace DesignSystem
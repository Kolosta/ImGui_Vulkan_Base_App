#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <imgui.h>
#include <string>
#include <variant>
#include <ostream>
#include <istream>
#include <cstdint>

namespace DesignSystem {

/**
 * Type-safe value storage supporting Color, Float, Int, Vec2, and Reference types.
 * Uses std::variant for efficient storage and binary serialization for fast I/O.
 */
class TokenValue {
public:
    TokenValue();
    explicit TokenValue(const ImVec4& color);
    explicit TokenValue(float value);
    explicit TokenValue(int value);
    explicit TokenValue(const ImVec2& vec);
    explicit TokenValue(const std::string& tokenId);
    
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
    
    /**
     * Type-safe setters.
     */
    void SetColor(const ImVec4& color);
    void SetFloat(float value);
    void SetInt(int value);
    void SetVec2(const ImVec2& vec);
    void SetReference(const std::string& tokenId);
    
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
    std::variant<ImVec4, float, int, ImVec2, std::string> value_;
};

} // namespace DesignSystem
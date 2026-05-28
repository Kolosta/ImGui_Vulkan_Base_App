#include <DesignSystem/Core/TokenValue.h>
#include <stdexcept>
#include <cstdint>
#include <type_traits>

namespace DesignSystem {

TokenValue::TokenValue() : type_(ValueType::Float), value_(0.0f) {}
TokenValue::TokenValue(const ImVec4& color) : type_(ValueType::Color), value_(color) {}
TokenValue::TokenValue(float value) : type_(ValueType::Float), value_(value) {}
TokenValue::TokenValue(int value) : type_(ValueType::Int), value_(value) {}
TokenValue::TokenValue(const ImVec2& vec) : type_(ValueType::Vec2), value_(vec) {}
TokenValue::TokenValue(const std::string& tokenId) : type_(ValueType::Reference), value_(tokenId) {}

ImVec4 TokenValue::AsColor() const {
    if (type_ != ValueType::Color) throw std::runtime_error("Not a Color");
    return std::get<ImVec4>(value_);
}

float TokenValue::AsFloat() const {
    if (type_ != ValueType::Float) throw std::runtime_error("Not a Float");
    return std::get<float>(value_);
}

int TokenValue::AsInt() const {
    if (type_ != ValueType::Int) throw std::runtime_error("Not an Int");
    return std::get<int>(value_);
}

ImVec2 TokenValue::AsVec2() const {
    if (type_ != ValueType::Vec2) throw std::runtime_error("Not a Vec2");
    return std::get<ImVec2>(value_);
}

std::string TokenValue::AsReference() const {
    if (type_ != ValueType::Reference) throw std::runtime_error("Not a Reference");
    return std::get<std::string>(value_);
}

void TokenValue::SetColor(const ImVec4& color) { type_ = ValueType::Color; value_ = color; }
void TokenValue::SetFloat(float value) { type_ = ValueType::Float; value_ = value; }
void TokenValue::SetInt(int value) { type_ = ValueType::Int; value_ = value; }
void TokenValue::SetVec2(const ImVec2& vec) { type_ = ValueType::Vec2; value_ = vec; }
void TokenValue::SetReference(const std::string& tokenId) { type_ = ValueType::Reference; value_ = tokenId; }
void TokenValue::SetRatio(float fraction) { type_ = ValueType::Ratio; value_ = fraction; }
void TokenValue::SetBezier(const ImVec4& cp) { type_ = ValueType::Bezier; value_ = cp; }
void TokenValue::SetTextStyle(const TextStyleRefs& refs) { type_ = ValueType::TextStyle; value_ = refs; }
void TokenValue::SetFontFamily(const std::string& name) { type_ = ValueType::FontFamily; value_ = name; }

TokenValue TokenValue::MakeRatio(float fraction) { TokenValue v; v.SetRatio(fraction); return v; }
TokenValue TokenValue::MakeBezier(const ImVec4& cp) { TokenValue v; v.SetBezier(cp); return v; }
TokenValue TokenValue::MakeTextStyle(const TextStyleRefs& refs) { TokenValue v; v.SetTextStyle(refs); return v; }
TokenValue TokenValue::MakeFontFamily(const std::string& name) { TokenValue v; v.SetFontFamily(name); return v; }

float TokenValue::AsRatio() const {
    if (type_ != ValueType::Ratio) throw std::runtime_error("Not a Ratio");
    return std::get<float>(value_);
}
ImVec4 TokenValue::AsBezier() const {
    if (type_ != ValueType::Bezier) throw std::runtime_error("Not a Bezier");
    return std::get<ImVec4>(value_);
}
TextStyleRefs TokenValue::AsTextStyle() const {
    if (type_ != ValueType::TextStyle) throw std::runtime_error("Not a TextStyle");
    return std::get<TextStyleRefs>(value_);
}
std::string TokenValue::AsFontFamily() const {
    if (type_ != ValueType::FontFamily) throw std::runtime_error("Not a FontFamily");
    return std::get<std::string>(value_);
}

/**
 * Properly compares values using std::visit.
 * Handles ImVec2 and ImVec4 which don't have default operator==.
 */
bool TokenValue::operator==(const TokenValue& other) const {
    if (type_ != other.type_) return false;
    return std::visit([&](const auto& a) -> bool {
        using T = std::decay_t<decltype(a)>;
        const T& b = std::get<T>(other.value_);
        if constexpr (std::is_same_v<T, ImVec2>) {
            return a.x == b.x && a.y == b.y;
        } else if constexpr (std::is_same_v<T, ImVec4>) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        } else {
            return a == b;
        }
    }, value_);
}

bool TokenValue::operator!=(const TokenValue& other) const {
    return !(*this == other);
}

/**
 * Binary serialization for fast I/O.
 * Type is written externally, only value data here.
 */
void TokenValue::WriteToBinary(std::ostream& out) const {
    switch (type_) {
        case ValueType::Color: {
            ImVec4 c = std::get<ImVec4>(value_);
            out.write(reinterpret_cast<const char*>(&c), sizeof(ImVec4));
            break;
        }
        case ValueType::Float: {
            float f = std::get<float>(value_);
            out.write(reinterpret_cast<const char*>(&f), sizeof(float));
            break;
        }
        case ValueType::Int: {
            int i = std::get<int>(value_);
            out.write(reinterpret_cast<const char*>(&i), sizeof(int));
            break;
        }
        case ValueType::Vec2: {
            ImVec2 v = std::get<ImVec2>(value_);
            out.write(reinterpret_cast<const char*>(&v), sizeof(ImVec2));
            break;
        }
        case ValueType::Reference: {
            const std::string& str = std::get<std::string>(value_);
            uint32_t len = static_cast<uint32_t>(str.length());
            out.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
            out.write(str.data(), len);
            break;
        }
        case ValueType::Ratio: {
            float f = std::get<float>(value_);
            out.write(reinterpret_cast<const char*>(&f), sizeof(float));
            break;
        }
        case ValueType::Bezier: {
            ImVec4 c = std::get<ImVec4>(value_);
            out.write(reinterpret_cast<const char*>(&c), sizeof(ImVec4));
            break;
        }
        case ValueType::TextStyle: {
            const TextStyleRefs& t = std::get<TextStyleRefs>(value_);
            auto wr = [&](const std::string& s) {
                uint32_t len = static_cast<uint32_t>(s.length());
                out.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
                out.write(s.data(), len);
            };
            wr(t.family); wr(t.size); wr(t.weight); wr(t.lineHeight); wr(t.tracking);
            break;
        }
        case ValueType::FontFamily: {
            const std::string& str = std::get<std::string>(value_);
            uint32_t len = static_cast<uint32_t>(str.length());
            out.write(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
            out.write(str.data(), len);
            break;
        }
    }
}

TokenValue TokenValue::ReadFromBinary(std::istream& in, ValueType type) {
    switch (type) {
        case ValueType::Color: {
            ImVec4 color;
            in.read(reinterpret_cast<char*>(&color), sizeof(ImVec4));
            return TokenValue(color);
        }
        case ValueType::Float: {
            float value;
            in.read(reinterpret_cast<char*>(&value), sizeof(float));
            return TokenValue(value);
        }
        case ValueType::Int: {
            int value;
            in.read(reinterpret_cast<char*>(&value), sizeof(int));
            return TokenValue(value);
        }
        case ValueType::Vec2: {
            ImVec2 vec;
            in.read(reinterpret_cast<char*>(&vec), sizeof(ImVec2));
            return TokenValue(vec);
        }
        case ValueType::Reference: {
            uint32_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
            std::string str(len, '\0');
            in.read(&str[0], len);
            return TokenValue(str);
        }
        case ValueType::Ratio: {
            float value;
            in.read(reinterpret_cast<char*>(&value), sizeof(float));
            return TokenValue::MakeRatio(value);
        }
        case ValueType::Bezier: {
            ImVec4 cp;
            in.read(reinterpret_cast<char*>(&cp), sizeof(ImVec4));
            return TokenValue::MakeBezier(cp);
        }
        case ValueType::TextStyle: {
            auto rd = [&]() {
                uint32_t len;
                in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
                std::string s(len, '\0');
                in.read(&s[0], len);
                return s;
            };
            TextStyleRefs t;
            t.family = rd(); t.size = rd(); t.weight = rd();
            t.lineHeight = rd(); t.tracking = rd();
            return TokenValue::MakeTextStyle(t);
        }
        case ValueType::FontFamily: {
            uint32_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(uint32_t));
            std::string str(len, '\0');
            in.read(&str[0], len);
            return TokenValue::MakeFontFamily(str);
        }
    }
    return TokenValue();
}

} // namespace DesignSystem
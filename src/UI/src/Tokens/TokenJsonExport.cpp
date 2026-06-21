#include <UI/Tokens/TokenJsonExport.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Exhaustive, round-trip-faithful token export (one theme layer per file).
//
//  Priority is FIDELITY to the engine model, not W3C interchange: every token
//  records its EXACT engine type, constraint, description and the layer value as
//  written for the chosen theme (a reference id, or a literal value). Importing
//  the file back must reproduce the very same token.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

namespace {
namespace DS = DesignSystem;
using DS::TokenValue;
using DS::ValueType;
using DS::ThemeType;

constexpr ThemeType kThemes[4] = {
    ThemeType::Dark, ThemeType::Light, ThemeType::MutedGreen, ThemeType::HighContrast,
};

std::string JsonEscape(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)ch < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", ch); o += buf;
                } else o += ch;
        }
    }
    return o;
}

std::string Num(double v) {
    char buf[40];
    if (v == (long long)v) std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
    else                   std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

// A literal TokenValue → JSON object tagged with its exact engine type, so the
// import is unambiguous (no type guessing).
std::string LiteralJson(const TokenValue& v) {
    std::ostringstream o;
    switch (v.GetType()) {
        case ValueType::Color: {
            ImVec4 c = v.AsColor();
            o << "{ \"type\": \"Color\", \"rgba\": ["
              << Num(c.x) << ", " << Num(c.y) << ", " << Num(c.z) << ", " << Num(c.w) << "] }";
            break;
        }
        case ValueType::Float:
            o << "{ \"type\": \"Float\", \"value\": " << Num(v.AsFloat()) << " }"; break;
        case ValueType::Int:
            o << "{ \"type\": \"Int\", \"value\": " << v.AsInt() << " }"; break;
        case ValueType::Ratio:
            o << "{ \"type\": \"Ratio\", \"value\": " << Num(v.AsRatio()) << " }"; break;
        case ValueType::Vec2: {
            ImVec2 p = v.AsVec2();
            o << "{ \"type\": \"Vec2\", \"value\": [" << Num(p.x) << ", " << Num(p.y) << "] }";
            break;
        }
        case ValueType::Bezier: {
            ImVec4 b = v.AsBezier();
            o << "{ \"type\": \"Bezier\", \"value\": ["
              << Num(b.x) << ", " << Num(b.y) << ", " << Num(b.z) << ", " << Num(b.w) << "] }";
            break;
        }
        case ValueType::FontFamily:
            o << "{ \"type\": \"FontFamily\", \"value\": \"" << JsonEscape(v.AsFontFamily()) << "\" }";
            break;
        case ValueType::TextStyle: {
            DS::TextStyleRefs r = v.AsTextStyle();
            o << "{ \"type\": \"TextStyle\", \"refs\": { "
              << "\"family\": \"" << JsonEscape(r.family) << "\", "
              << "\"size\": \"" << JsonEscape(r.size) << "\", "
              << "\"weight\": \"" << JsonEscape(r.weight) << "\", "
              << "\"lineHeight\": \"" << JsonEscape(r.lineHeight) << "\", "
              << "\"tracking\": \"" << JsonEscape(r.tracking) << "\" } }";
            break;
        }
        case ValueType::Reference:
            o << "{ \"type\": \"Reference\", \"ref\": \"" << JsonEscape(v.AsReference()) << "\" }";
            break;
    }
    return o.str();
}

// Constraint → JSON (or "null"). Mirrors ValueConstraint's fields.
std::string ConstraintJson(const DS::ValueConstraint& c) {
    if (c.IsEmpty()) return "null";
    std::ostringstream o;
    o << "{ ";
    bool first = true;
    auto comma = [&]{ if (!first) o << ", "; first = false; };
    if (!c.intervals.empty()) {
        comma(); o << "\"intervals\": [";
        for (size_t i = 0; i < c.intervals.size(); ++i) {
            const auto& iv = c.intervals[i];
            o << "{ \"min\": " << Num(iv.min) << ", \"max\": " << Num(iv.max)
              << ", \"includesMin\": " << (iv.includesMin ? "true" : "false")
              << ", \"includesMax\": " << (iv.includesMax ? "true" : "false") << " }";
            if (i + 1 < c.intervals.size()) o << ", ";
        }
        o << "]";
    }
    if (!c.allowedValues.empty()) {
        comma(); o << "\"allowedValues\": [";
        for (size_t i = 0; i < c.allowedValues.size(); ++i) {
            o << Num(c.allowedValues[i]);
            if (i + 1 < c.allowedValues.size()) o << ", ";
        }
        o << "]";
    }
    if (c.step != 0.0) { comma(); o << "\"step\": " << Num(c.step); }
    if (!c.description.empty()) { comma(); o << "\"description\": \"" << JsonEscape(c.description) << "\""; }
    o << " }";
    return o.str();
}

// The layer "as written" for `theme`: a reference id when the token references
// another (we keep the ref, ignoring value overrides), else the literal value
// (with value overrides applied — relevant for parentless first-level tokens).
//
// `wroteSomething` is false when the token does NOT redefine this layer for a
// non-Dark theme (so the importer leaves that theme inheriting the base).
std::string LayerJson(const std::shared_ptr<DS::Token>& tok, ThemeType theme,
                      bool isBase, bool& wroteSomething) {
    auto& ds = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    const std::string& id = tok->GetId();
    wroteSomething = true;

    // Override first (global beats theme): a reference layer keeps the ref; a
    // value override on a first-level token becomes a value layer.
    if (const DS::Override* o = mgr.GetBestOverride(id, theme)) {
        if (o->GetValue().GetType() == ValueType::Reference)
            return "{ \"kind\": \"reference\", \"ref\": \"" +
                   JsonEscape(o->GetValue().AsReference()) + "\" }";
        // A value override on a first-level token → emit it as a value layer.
        return "{ \"kind\": \"value\", \"value\": " + LiteralJson(o->GetValue()) + " }";
    }
    // Theme/base written layer.
    if (isBase) {
        const TokenValue& dv = tok->GetDefaultValue();
        if (dv.GetType() == ValueType::Reference)
            return "{ \"kind\": \"reference\", \"ref\": \"" + JsonEscape(dv.AsReference()) + "\" }";
        return "{ \"kind\": \"value\", \"value\": " + LiteralJson(dv) + " }";
    }
    // Non-base theme: only emit if the token actually redefines this theme.
    if (const TokenValue* tv =
            tok->GetContextValue(DS::Context(theme, DS::AccessibilityType::None))) {
        if (tv->GetType() == ValueType::Reference)
            return "{ \"kind\": \"reference\", \"ref\": \"" + JsonEscape(tv->AsReference()) + "\" }";
        return "{ \"kind\": \"value\", \"value\": " + LiteralJson(*tv) + " }";
    }
    wroteSomething = false;
    return "";   // theme inherits the base layer
}

const char* LevelStr(DS::TokenLevel l) {
    return l == DS::TokenLevel::Primitive ? "Primitive"
         : l == DS::TokenLevel::Semantic  ? "Semantic" : "Component";
}
const char* TypeStr(ValueType t) {
    switch (t) {
        case ValueType::Color: return "Color"; case ValueType::Float: return "Float";
        case ValueType::Int: return "Int"; case ValueType::Vec2: return "Vec2";
        case ValueType::Reference: return "Reference"; case ValueType::Ratio: return "Ratio";
        case ValueType::Bezier: return "Bezier"; case ValueType::TextStyle: return "TextStyle";
        case ValueType::FontFamily: return "FontFamily";
    }
    return "Float";
}

} // namespace

std::string ExportTokensJson(int themeIndex) {
    if (themeIndex < 0 || themeIndex >= 4) themeIndex = 0;
    const ThemeType theme = kThemes[themeIndex];
    const bool isBase = (themeIndex == 0);   // Dark == base layer
    auto& ds = DS::DesignSystem::Instance();
    auto all = ds.GetRegistry().GetAllTokens();
    std::sort(all.begin(), all.end(),
              [](const auto& a, const auto& b){ return a->GetId() < b->GetId(); });

    static const char* kThemeNames[4] = { "Dark", "Light", "MutedGreen", "HighContrast" };
    std::ostringstream o;
    o << "{\n";
    o << "  \"format\": \"carto-tokens-v1\",\n";
    o << "  \"theme\": \"" << kThemeNames[themeIndex] << "\",\n";
    o << "  \"isBaseLayer\": " << (isBase ? "true" : "false") << ",\n";
    o << "  \"tokens\": {\n";

    bool firstTok = true;
    for (const auto& tok : all) {
        bool wrote = false;
        std::string layer = LayerJson(tok, theme, isBase, wrote);
        // For a non-base theme, skip tokens that don't redefine this layer.
        if (!isBase && !wrote) continue;

        if (!firstTok) o << ",\n";
        firstTok = false;
        o << "    \"" << JsonEscape(tok->GetId()) << "\": {\n";
        o << "      \"level\": \"" << LevelStr(tok->GetLevel()) << "\",\n";
        o << "      \"type\": \"" << TypeStr(tok->GetValueType()) << "\",\n";
        if (!tok->GetDescription().empty())
            o << "      \"description\": \"" << JsonEscape(tok->GetDescription()) << "\",\n";
        o << "      \"constraint\": " << ConstraintJson(tok->GetConstraint()) << ",\n";
        o << "      \"layer\": " << layer << "\n";
        o << "    }";
    }
    o << "\n  }\n}\n";
    return o.str();
}

bool WriteTokensJson(const std::string& path, int themeIndex) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << ExportTokensJson(themeIndex);
    return (bool)f;
}

} // namespace UI

#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/TokenSchema.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <DesignSystem/Core/ValueConstraint.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  TokenRegistry — the runtime store.
//
//  It owns NO token data of its own: `InitializeDefaultTokens()` simply
//  materialises every row of the compile-time-validated `kTokenSchema` into a
//  `Token`. Because the schema is proven complete/acyclic/well-typed at build
//  time, this function cannot produce a broken graph. The registry stays keyed
//  by the string id (TokName) so the override store, persistence and the
//  generic editor keep working unchanged.
// ─────────────────────────────────────────────────────────────────────────────

namespace DesignSystem {

namespace {

ImVec4 ToImVec4(const Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }
ImVec2 ToImVec2(const Vec2f& v) { return ImVec2(v.x, v.y); }

// Build the concrete TokenValue for a schema definition's *default* value.
TokenValue DefaultValueOf(const TokenDef& d) {
    switch (d.type) {
        case ValueType::Color:     return TokenValue(ToImVec4(d.color));
        case ValueType::Float:     return TokenValue(d.scalar);
        case ValueType::Int:       return TokenValue(d.integer);
        case ValueType::Vec2:      return TokenValue(ToImVec2(d.vec2));
        case ValueType::Reference: return TokenValue(TokIdStr(d.ref));
        case ValueType::Ratio:     return TokenValue::MakeRatio(d.scalar);
        case ValueType::Bezier:    return TokenValue::MakeBezier(ToImVec4(d.color));
        case ValueType::TextStyle: {
            TextStyleRefs r;
            r.family     = TokIdStr(d.tsFamily);
            r.size       = TokIdStr(d.tsSize);
            r.weight     = TokIdStr(d.tsWeight);
            r.lineHeight = TokIdStr(d.tsLineHeight);
            r.tracking   = TokIdStr(d.tsTracking);
            return TokenValue::MakeTextStyle(r);
        }
        case ValueType::FontFamily:
            return TokenValue::MakeFontFamily(std::string(d.fontFamilyName));
    }
    return TokenValue(0.0f);
}

// Build the TokenValue for a theme-scoped override entry.
TokenValue ThemeValueOf(const ThemeOverride& o) {
    if (o.refValid) return TokenValue(TokIdStr(o.ref));
    if (o.kind == ValueType::Color) return TokenValue(ToImVec4(o.color));
    return TokenValue(o.scalar);
}

// Materialise the constexpr ConstraintSpec into a runtime ValueConstraint.
// (ValueConstraint owns std::vector/std::string so it can't live in the
// constexpr schema; this is the single conversion point.)
ValueConstraint ToConstraint(const ConstraintSpec& s) {
    switch (s.kind) {
        case ConstraintSpec::Kind::RangeK:
            return ValueConstraint::Range(s.lo, s.hi, 0.0,
                                          std::string(s.note));
        case ConstraintSpec::Kind::OneOfK: {
            std::vector<double> vs(s.values.begin(),
                                   s.values.begin() + s.valueCount);
            return ValueConstraint::OneOf(std::move(vs), std::string(s.note));
        }
        case ConstraintSpec::Kind::None:
        default:
            return ValueConstraint{};
    }
}

} // namespace

TokenRegistry& TokenRegistry::Instance() {
    static TokenRegistry* instance = new TokenRegistry();
    return *instance;
}

void TokenRegistry::RegisterToken(std::shared_ptr<Token> token) {
    tokens_[token->GetId()] = token;
}

std::shared_ptr<Token> TokenRegistry::GetToken(const std::string& id) const {
    auto it = tokens_.find(id);
    return it != tokens_.end() ? it->second : nullptr;
}

bool TokenRegistry::HasToken(const std::string& id) const {
    return tokens_.find(id) != tokens_.end();
}

void TokenRegistry::UnregisterToken(const std::string& id) {
    tokens_.erase(id);
}

void TokenRegistry::Clear() {
    tokens_.clear();
}

std::vector<std::shared_ptr<Token>>
TokenRegistry::GetTokensByLevel(TokenLevel level) const {
    std::vector<std::shared_ptr<Token>> result;
    for (const auto& [id, token] : tokens_)
        if (token->GetLevel() == level) result.push_back(token);
    return result;
}

std::vector<std::shared_ptr<Token>> TokenRegistry::GetAllTokens() const {
    std::vector<std::shared_ptr<Token>> result;
    result.reserve(tokens_.size());
    for (const auto& [id, token] : tokens_) result.push_back(token);
    return result;
}

void TokenRegistry::InitializeDefaultTokens() {
    Clear();
    CreateDefaultPrimitiveTokens();
    CreateDefaultSemanticTokens();
    CreateDefaultComponentTokens();
}

// The three Create* functions partition the single schema by level. They are
// kept separate to honour the existing public surface, but the data — and its
// correctness — lives entirely in TokenSchema.cpp.
void TokenRegistry::CreateDefaultPrimitiveTokens() { /* materialised below */ }
void TokenRegistry::CreateDefaultSemanticTokens()  { /* materialised below */ }

void TokenRegistry::CreateDefaultComponentTokens() {
    // Materialise the whole schema in one pass. Calling this once (it is the
    // last of the three in InitializeDefaultTokens) registers every level;
    // the other two are intentionally no-ops so we never build a token twice.
    for (const TokenDef& d : kTokenSchema) {
        auto token = std::make_shared<Token>(TokIdStr(d.id), d.level, d.type);
        token->SetDefaultValue(DefaultValueOf(d));
        if (!d.description.empty())
            token->SetDescription(std::string(d.description));
        if (!d.constraint.Empty())
            token->SetConstraint(ToConstraint(d.constraint));
        for (std::size_t k = 0; k < d.themeOverrideCount; ++k) {
            const ThemeOverride& o = d.themeOverrides[k];
            token->SetContextValue(Context(o.theme, AccessibilityType::None),
                                   ThemeValueOf(o));
        }
        RegisterToken(token);
    }
}

} // namespace DesignSystem

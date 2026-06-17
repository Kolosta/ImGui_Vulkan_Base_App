#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace DesignSystem {

namespace {

// Effective constraint of a token: its own, or — if it has none and is a
// Reference — the constraint inherited from the token it resolves to. This is
// what makes the constraint a property of the *field*: e.g. a Reference token
// like component.style.disabledAlpha still clamps to the alpha [0..1] range it
// ultimately resolves into, instead of accepting any float because the
// Reference node itself declared no range. Bounded against ref cycles.
const ValueConstraint* EffectiveConstraint(const std::string& tokenId) {
    auto& reg = TokenRegistry::Instance();
    std::string cur = tokenId;
    for (int hop = 0; hop < 16; ++hop) {
        auto token = reg.GetToken(cur);
        if (!token) return nullptr;
        if (token->HasConstraint()) return &token->GetConstraint();
        const TokenValue& dv = token->GetDefaultValue();
        if (!dv.IsReference()) return nullptr;
        cur = dv.AsReference();
    }
    return nullptr;
}

// True if `v` falls inside the effective constraint of `tokenId` (or there is
// no numeric constraint to violate). Used to *reject* impossible overrides.
bool ValueSatisfiesConstraint(const std::string& tokenId, const TokenValue& v) {
    const ValueConstraint* c = EffectiveConstraint(tokenId);
    if (!c) return true;
    if (v.GetType() == ValueType::Float) return c->Accepts(static_cast<double>(v.AsFloat()));
    if (v.GetType() == ValueType::Int)   return c->Accepts(static_cast<double>(v.AsInt()));
    return true;  // Color/Vec2/Reference: no numeric notion of range yet
}

// Clamp a TokenValue against its token's effective constraint.
// Only Float/Int are clamped; Color, Vec2 and Reference pass through.
TokenValue ClampToConstraint(const std::string& tokenId, const TokenValue& v) {
    const ValueConstraint* c = EffectiveConstraint(tokenId);
    if (!c) return v;
    if (v.GetType() == ValueType::Float)
        return TokenValue(static_cast<float>(c->Clamp(static_cast<double>(v.AsFloat()))));
    if (v.GetType() == ValueType::Int)
        return TokenValue(static_cast<int>(std::round(c->Clamp(static_cast<double>(v.AsInt())))));
    return v;
}

} // namespace

void OverrideManager::AddOverride(const Override& override) {
    // Remove existing override with same token and theme
    if (override.IsGlobal()) {
        RemoveGlobalOverride(override.GetTokenId());
    } else {
        RemoveThemeOverride(override.GetTokenId(), *override.GetTheme());
    }

    // Enforce the token's value constraint at the write site so any caller
    // (UI, scripted, deserialised) ends up with a valid override on disk.
    Override clamped = override;
    clamped.SetValue(ClampToConstraint(override.GetTokenId(), override.GetValue()));
    overrides_.push_back(clamped);
}

namespace {

// Resolve the concrete value type a token ultimately holds (following the
// reference chain), so a Float override onto a Reference token can still be
// type-checked against the field it drives. Bounded against cycles.
ValueType ResolvedValueType(const std::string& tokenId) {
    auto& reg = TokenRegistry::Instance();
    std::string cur = tokenId;
    for (int hop = 0; hop < 16; ++hop) {
        auto t = reg.GetToken(cur);
        if (!t) return ValueType::Reference;
        const TokenValue& dv = t->GetDefaultValue();
        if (!dv.IsReference()) return dv.GetType();
        cur = dv.AsReference();
    }
    return ValueType::Reference;
}

const char* TypeName(ValueType t) {
    switch (t) {
        case ValueType::Color:     return "Color";
        case ValueType::Float:     return "Float";
        case ValueType::Int:       return "Int";
        case ValueType::Vec2:      return "Vec2";
        case ValueType::Reference: return "Reference";
    }
    return "?";
}

} // namespace

OverrideManager::AddResult
OverrideManager::TryAddOverride(const Override& override) {
    const std::string& id = override.GetTokenId();
    const TokenValue&  v  = override.GetValue();
    auto token = TokenRegistry::Instance().GetToken(id);

    if (!token) {
        return { false, "Unknown token '" + id + "': no such token in the "
                          "registry; cannot create an override for it." };
    }

    // Type check: an override may itself be a Reference, otherwise its type
    // must match what the token ultimately resolves to.
    if (!v.IsReference()) {
        ValueType want = ResolvedValueType(id);
        if (want != ValueType::Reference && v.GetType() != want) {
            return { false, "Type mismatch for '" + id + "': token holds " +
                             TypeName(want) + " but the override is " +
                             TypeName(v.GetType()) + "." };
        }
    }

    // Constraint check: reject (do not clamp) impossible numeric values.
    if (!ValueSatisfiesConstraint(id, v)) {
        const ValueConstraint* c = EffectiveConstraint(id);
        std::string range = "its constraint";
        if (c) {
            auto lo = c->Min(); auto hi = c->Max();
            if (lo || hi) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "the allowed range [%.4g .. %.4g]",
                              lo.value_or(-1e9), hi.value_or(1e9));
                range = buf;
            }
        }
        char val[48];
        if (v.GetType() == ValueType::Float)
            std::snprintf(val, sizeof(val), "%.4g", v.AsFloat());
        else if (v.GetType() == ValueType::Int)
            std::snprintf(val, sizeof(val), "%d", v.AsInt());
        else val[0] = '\0';
        return { false, "Value " + std::string(val) + " is outside " + range +
                         " for token '" + id + "'; override rejected." };
    }

    AddOverride(override);  // valid → store (AddOverride still clamps defensively)
    return { true, {} };
}

void OverrideManager::RemoveGlobalOverride(const std::string& tokenId) {
    overrides_.erase(
        std::remove_if(overrides_.begin(), overrides_.end(),
            [&tokenId](const Override& o) {
                return o.GetTokenId() == tokenId && o.IsGlobal();
            }),
        overrides_.end()
    );
}

void OverrideManager::RemoveThemeOverride(const std::string& tokenId, ThemeType theme) {
    overrides_.erase(
        std::remove_if(overrides_.begin(), overrides_.end(),
            [&tokenId, theme](const Override& o) {
                return o.GetTokenId() == tokenId && 
                       o.IsThemeSpecific() && 
                       *o.GetTheme() == theme;
            }),
        overrides_.end()
    );
}

void OverrideManager::RemoveAllOverrides(const std::string& tokenId) {
    overrides_.erase(
        std::remove_if(overrides_.begin(), overrides_.end(),
            [&tokenId](const Override& o) { return o.GetTokenId() == tokenId; }),
        overrides_.end()
    );
}

void OverrideManager::Clear() {
    overrides_.clear();
}

bool OverrideManager::HasGlobalOverride(const std::string& tokenId) const {
    return std::any_of(overrides_.begin(), overrides_.end(),
        [&tokenId](const Override& o) { return o.GetTokenId() == tokenId && o.IsGlobal(); });
}

bool OverrideManager::HasThemeOverride(const std::string& tokenId, ThemeType theme) const {
    return std::any_of(overrides_.begin(), overrides_.end(),
        [&tokenId, theme](const Override& o) {
            return o.GetTokenId() == tokenId && o.IsThemeSpecific() && *o.GetTheme() == theme;
        });
}

const Override* OverrideManager::GetBestOverride(const std::string& tokenId, ThemeType theme) const {
    const Override* bestOverride = nullptr;
    int highestPriority = -1;
    
    for (const auto& override : overrides_) {
        if (override.GetTokenId() == tokenId && override.AppliesTo(theme)) {
            int priority = override.GetPriority();
            if (priority > highestPriority) {
                highestPriority = priority;
                bestOverride = &override;
            }
        }
    }
    
    return bestOverride;
}

std::vector<Override*> OverrideManager::GetAllOverrides(const std::string& tokenId) {
    std::vector<Override*> result;
    for (auto& override : overrides_) {
        if (override.GetTokenId() == tokenId) {
            result.push_back(&override);
        }
    }
    return result;
}

/**
 * Get specific override for editing.
 */
Override* OverrideManager::GetOverride(const std::string& tokenId, bool isGlobal, ThemeType theme) {
    for (auto& override : overrides_) {
        if (override.GetTokenId() == tokenId) {
            if (isGlobal && override.IsGlobal()) {
                return &override;
            }
            if (!isGlobal && override.IsThemeSpecific() && *override.GetTheme() == theme) {
                return &override;
            }
        }
    }
    return nullptr;
}

/**
 * Write overrides to binary stream.
 */
void OverrideManager::WriteToBinary(std::ostream& out) const {
    uint32_t count = static_cast<uint32_t>(overrides_.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
    for (const auto& override : overrides_) {
        override.WriteToBinary(out);
    }
}

/**
 * Read overrides from binary stream.
 * CRITICAL: Added safety check to prevent bad_alloc.
 */
void OverrideManager::ReadFromBinary(std::istream& in) {
    overrides_.clear();
    
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    
    // Safety check: reasonable maximum (prevent corrupted data causing bad_alloc)
    if (count > 10000) {
        throw std::runtime_error("Corrupted override count in binary file");
    }
    
    overrides_.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        overrides_.push_back(Override::ReadFromBinary(in));
    }
}

} // namespace DesignSystem
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace DesignSystem {

namespace {

// Clamp a TokenValue against its owning token's constraint.
// Only Float/Int are clamped; Color, Vec2 and Reference values are passed through
// (Color/Vec2 sub-channel constraints are not modelled yet, and a Reference
// stores a token id which has no numeric notion of "in range").
TokenValue ClampToConstraint(const std::string& tokenId, const TokenValue& v) {
    auto token = TokenRegistry::Instance().GetToken(tokenId);
    if (!token || !token->HasConstraint()) return v;
    const ValueConstraint& c = token->GetConstraint();
    if (v.GetType() == ValueType::Float) {
        double clamped = c.Clamp(static_cast<double>(v.AsFloat()));
        TokenValue out(static_cast<float>(clamped));
        return out;
    }
    if (v.GetType() == ValueType::Int) {
        double clamped = c.Clamp(static_cast<double>(v.AsInt()));
        TokenValue out(static_cast<int>(std::round(clamped)));
        return out;
    }
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
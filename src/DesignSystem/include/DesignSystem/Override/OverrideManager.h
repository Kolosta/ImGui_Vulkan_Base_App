#pragma once

#include <DesignSystem/Override/Override.h>
#include <DesignSystem/Core/TokenType.h>
#include <vector>
#include <ostream>
#include <istream>
#include <cstdint>
#include <string>

namespace DesignSystem {

/**
 * Manages all overrides in the system.
 * Tracks override modifications for automatic persistence.
 */
class OverrideManager {
public:
    OverrideManager() = default;
    
    /**
     * Add/modify overrides.
     *
     * AddOverride is forgiving: an out-of-range numeric value is *clamped* to
     * the token's effective constraint before being stored, so any path (UI,
     * scripted, deserialised) ends up with a valid override.
     */
    void AddOverride(const Override& override);

    /**
     * Result of a strict override attempt. `accepted` is false when the value
     * violates the token's effective constraint or its declared type; `error`
     * then holds a human-readable, caller-friendly explanation (token id,
     * expected range/type, offending value) and nothing is stored.
     */
    struct AddResult {
        bool        accepted = false;
        std::string error;
        explicit operator bool() const { return accepted; }
    };

    /**
     * Strict variant: rejects (does not clamp) a value the token cannot hold,
     * returning a descriptive error instead of silently coercing it. Use this
     * when a caller wants the design system to enforce strong typing at the
     * value level and to surface a precise reason on failure.
     */
    AddResult TryAddOverride(const Override& override);
    
    /**
     * Remove overrides.
     */
    void RemoveGlobalOverride(const std::string& tokenId);
    void RemoveThemeOverride(const std::string& tokenId, ThemeType theme);
    void RemoveAllOverrides(const std::string& tokenId);
    void Clear();
    
    /**
     * Query overrides.
     */
    bool HasGlobalOverride(const std::string& tokenId) const;
    bool HasThemeOverride(const std::string& tokenId, ThemeType theme) const;
    
    /**
     * Get the best override for a token in a given theme.
     * Returns highest priority override that applies.
     */
    const Override* GetBestOverride(const std::string& tokenId, ThemeType theme) const;
    
    /**
     * Get all overrides for a token (for UI display).
     * Returns mutable pointers for in-place editing.
     */
    std::vector<Override*> GetAllOverrides(const std::string& tokenId);
    
    /**
     * Get specific override for editing.
     */
    Override* GetOverride(const std::string& tokenId, bool isGlobal, ThemeType theme = ThemeType::Dark);
    
    /**
     * Binary serialization.
     */
    void WriteToBinary(std::ostream& out) const;
    void ReadFromBinary(std::istream& in);
    
private:
    std::vector<Override> overrides_;
};

} // namespace DesignSystem
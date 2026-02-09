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
     */
    void AddOverride(const Override& override);
    
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
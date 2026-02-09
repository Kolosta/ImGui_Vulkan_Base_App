#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenType.h>
#include <string>
#include <fstream>

namespace DesignSystem {

class OverrideManager;
class TokenRegistry;
class Override;

/**
 * Real-time binary serialization system.
 * Saves state immediately on each change (crash-safe).
 * Uses simple rewrite-entire-file approach for reliability.
 * 
 * File format:
 * [Magic Number 4 bytes][Context 8 bytes][Override Count 4 bytes][Override1...OverrideN]
 */
class Serialization {
public:
    static constexpr const char* STATE_FILE = "design_system.bin";
    static constexpr uint32_t MAGIC_NUMBER = 0x44534453;  // "DSDS" - DesignSystem
    
    /**
     * Initialize serialization system.
     */
    static void Initialize();
    
    /**
     * Shutdown and ensure all data is flushed.
     */
    static void Shutdown();
    
    /**
     * Save entire state immediately.
     * Rewrites the entire file (simple and reliable).
     */
    static void SaveState(const Context& context, const OverrideManager& overrideManager);
    
    /**
     * Load entire state from disk.
     * Returns false if file doesn't exist or is corrupted.
     */
    static bool LoadState(Context& currentContext, OverrideManager& overrideManager);
    
private:
    static bool initialized_;
    
    /**
     * Check if file exists and is readable.
     */
    static bool FileExists(const std::string& filepath);
    
    /**
     * Validate file integrity.
     */
    static bool ValidateFile(std::ifstream& file);
};

} // namespace DesignSystem
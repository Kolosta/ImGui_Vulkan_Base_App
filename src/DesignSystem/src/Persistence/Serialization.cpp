#include <DesignSystem/Persistence/Serialization.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <iostream>
#include <fstream>
#include <cstdint>

namespace DesignSystem {

bool Serialization::initialized_ = false;

void Serialization::Initialize() {
    initialized_ = true;
}

void Serialization::Shutdown() {
    initialized_ = false;
}

/**
 * CRITICAL: Actually save the state to disk.
 * Rewrites entire file for simplicity and reliability.
 * Format: [MAGIC][Context][OverrideManager]
 */
void Serialization::SaveState(const Context& context, const OverrideManager& overrideManager) {
    if (!initialized_) return;
    
    try {
        std::ofstream out(STATE_FILE, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "Failed to open state file for writing" << std::endl;
            return;
        }
        
        // Write magic number for validation
        uint32_t magic = MAGIC_NUMBER;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(uint32_t));
        
        // Write context
        context.WriteToBinary(out);
        
        // Write overrides
        overrideManager.WriteToBinary(out);
        
        out.close();
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving state: " << e.what() << std::endl;
    }
}

/**
 * CRITICAL: Actually load the state from disk.
 * Returns false if file doesn't exist (first run) or is corrupted.
 */
bool Serialization::LoadState(Context& currentContext, OverrideManager& overrideManager) {
    if (!FileExists(STATE_FILE)) {
        // First run - no saved state
        return false;
    }
    
    try {
        std::ifstream in(STATE_FILE, std::ios::binary);
        if (!in.is_open()) {
            std::cerr << "Failed to open state file for reading" << std::endl;
            return false;
        }
        
        // Validate file
        if (!ValidateFile(in)) {
            in.close();
            std::cerr << "State file corrupted or invalid" << std::endl;
            return false;
        }
        
        // Read context
        currentContext = Context::ReadFromBinary(in);
        
        // Read overrides
        overrideManager.ReadFromBinary(in);
        
        in.close();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading state: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Check if file exists.
 */
bool Serialization::FileExists(const std::string& filepath) {
    std::ifstream file(filepath);
    return file.good();
}

/**
 * Validate file integrity.
 * Checks magic number and file size.
 */
bool Serialization::ValidateFile(std::ifstream& file) {
    // Check file size
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Minimum size: magic (4) + context (8) + override count (4) = 16 bytes
    if (fileSize < 16) {
        return false;
    }
    
    // Check magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    if (magic != MAGIC_NUMBER) {
        return false;
    }
    
    return true;
}

} // namespace DesignSystem
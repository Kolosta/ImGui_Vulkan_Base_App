#pragma once

#include <DesignSystem/Core/TokenValue.h>
#include <DesignSystem/Core/TokenType.h>
#include <DesignSystem/Core/Context.h>
#include <string>
#include <unordered_map>
#include <memory>


namespace DesignSystem {

// Forward declaration
class TokenRegistry;

// Base token class that represents a design token at any level
class Token {
public:
    Token(const std::string& id, TokenLevel level, ValueType valueType);
    virtual ~Token() = default;
    
    // Identity
    const std::string& GetId() const { return id_; }
    TokenLevel GetLevel() const { return level_; }
    ValueType GetValueType() const { return valueType_; }
    
    // Default value (always defined)
    void SetDefaultValue(const TokenValue& value);
    const TokenValue& GetDefaultValue() const { return defaultValue_; }
    
    // Context-specific values (theme overrides)
    void SetContextValue(const Context& context, const TokenValue& value);
    bool HasContextValue(const Context& context) const;
    const TokenValue* GetContextValue(const Context& context) const;
    void ClearContextValue(const Context& context);
    
    // Get all context values
    const std::unordered_map<Context, TokenValue>& GetContextValues() const {
        return contextValues_;
    }
    
    // Description (optional metadata)
    void SetDescription(const std::string& desc) { description_ = desc; }
    const std::string& GetDescription() const { return description_; }

protected:
    std::string id_;                                          // Unique identifier (e.g., "color.primary")
    TokenLevel level_;                                        // Token hierarchy level
    ValueType valueType_;                                     // Type of value stored
    TokenValue defaultValue_;                                 // Default value (Dark theme)
    std::unordered_map<Context, TokenValue> contextValues_;  // Context-specific values
    std::string description_;                                 // Optional description
};

} // namespace DesignSystem
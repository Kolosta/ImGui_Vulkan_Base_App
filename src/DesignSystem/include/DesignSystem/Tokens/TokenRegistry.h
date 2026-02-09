#pragma once

#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Core/Context.h>
#include <unordered_map>
#include <memory>
#include <vector>

namespace DesignSystem {

// Singleton registry that holds all tokens
class TokenRegistry {
public:
    static TokenRegistry& Instance();
    
    // Token management
    void RegisterToken(std::shared_ptr<Token> token);
    std::shared_ptr<Token> GetToken(const std::string& id) const;
    bool HasToken(const std::string& id) const;
    void UnregisterToken(const std::string& id);
    void Clear();
    
    // Get tokens by level
    std::vector<std::shared_ptr<Token>> GetTokensByLevel(TokenLevel level) const;
    std::vector<std::shared_ptr<Token>> GetAllTokens() const;
    
    // Initialize default tokens
    void InitializeDefaultTokens();
    
private:
    TokenRegistry() = default;
    ~TokenRegistry() = default;
    TokenRegistry(const TokenRegistry&) = delete;
    TokenRegistry& operator=(const TokenRegistry&) = delete;
    
    void CreateDefaultPrimitiveTokens();
    void CreateDefaultSemanticTokens();
    void CreateDefaultComponentTokens();
    
    std::unordered_map<std::string, std::shared_ptr<Token>> tokens_;
};

} // namespace DesignSystem
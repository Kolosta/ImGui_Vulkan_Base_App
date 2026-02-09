#include <DesignSystem/Tokens/Token.h>

namespace DesignSystem {

Token::Token(const std::string& id, TokenLevel level, ValueType valueType)
    : id_(id), level_(level), valueType_(valueType) {
}

void Token::SetDefaultValue(const TokenValue& value) {
    defaultValue_ = value;
}

void Token::SetContextValue(const Context& context, const TokenValue& value) {
    contextValues_[context] = value;
}

bool Token::HasContextValue(const Context& context) const {
    return contextValues_.find(context) != contextValues_.end();
}

const TokenValue* Token::GetContextValue(const Context& context) const {
    auto it = contextValues_.find(context);
    if (it != contextValues_.end()) {
        return &it->second;
    }
    return nullptr;
}

void Token::ClearContextValue(const Context& context) {
    contextValues_.erase(context);
}


} // namespace DesignSystem
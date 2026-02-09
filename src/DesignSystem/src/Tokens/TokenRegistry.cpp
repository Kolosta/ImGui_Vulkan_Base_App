// #include <DesignSystem/Tokens/TokenRegistry.h>

// namespace DesignSystem {

// TokenRegistry& TokenRegistry::Instance() {
//     static TokenRegistry* instance = new TokenRegistry();
//     return *instance;
// }

// void TokenRegistry::RegisterToken(std::shared_ptr<Token> token) {
//     tokens_[token->GetId()] = token;
// }

// std::shared_ptr<Token> TokenRegistry::GetToken(const std::string& id) const {
//     auto it = tokens_.find(id);
//     if (it != tokens_.end()) {
//         return it->second;
//     }
//     return nullptr;
// }

// bool TokenRegistry::HasToken(const std::string& id) const {
//     return tokens_.find(id) != tokens_.end();
// }

// void TokenRegistry::UnregisterToken(const std::string& id) {
//     tokens_.erase(id);
// }

// void TokenRegistry::Clear() {
//     tokens_.clear();
// }

// std::vector<std::shared_ptr<Token>> TokenRegistry::GetTokensByLevel(TokenLevel level) const {
//     std::vector<std::shared_ptr<Token>> result;
//     for (const auto& [id, token] : tokens_) {
//         if (token->GetLevel() == level) {
//             result.push_back(token);
//         }
//     }
//     return result;
// }

// std::vector<std::shared_ptr<Token>> TokenRegistry::GetAllTokens() const {
//     std::vector<std::shared_ptr<Token>> result;
//     for (const auto& [id, token] : tokens_) {
//         result.push_back(token);
//     }
//     return result;
// }

// void TokenRegistry::InitializeDefaultTokens() {
//     Clear();
//     CreateDefaultPrimitiveTokens();
//     CreateDefaultSemanticTokens();
//     CreateDefaultComponentTokens();
// }

// void TokenRegistry::CreateDefaultPrimitiveTokens() {
//     // ===== COLORS =====
//     auto blue500 = std::make_shared<Token>("primitive.color.blue.500", TokenLevel::Primitive, ValueType::Color);
//     blue500->SetDefaultValue(TokenValue(ImVec4(0.26f, 0.59f, 0.98f, 1.0f)));
//     blue500->SetDescription("Base blue color");
//     RegisterToken(blue500);
    
//     auto green500 = std::make_shared<Token>("primitive.color.green.500", TokenLevel::Primitive, ValueType::Color);
//     green500->SetDefaultValue(TokenValue(ImVec4(0.3f, 0.7f, 0.3f, 1.0f)));
//     green500->SetDescription("Base green color");
//     RegisterToken(green500);
    
//     auto red500 = std::make_shared<Token>("primitive.color.red.500", TokenLevel::Primitive, ValueType::Color);
//     red500->SetDefaultValue(TokenValue(ImVec4(0.9f, 0.26f, 0.26f, 1.0f)));
//     red500->SetDescription("Base red color");
//     RegisterToken(red500);
    
//     auto gray900 = std::make_shared<Token>("primitive.color.gray.900", TokenLevel::Primitive, ValueType::Color);
//     gray900->SetDefaultValue(TokenValue(ImVec4(0.1f, 0.1f, 0.1f, 1.0f)));
//     gray900->SetDescription("Very dark gray");
//     RegisterToken(gray900);
    
//     auto gray800 = std::make_shared<Token>("primitive.color.gray.800", TokenLevel::Primitive, ValueType::Color);
//     gray800->SetDefaultValue(TokenValue(ImVec4(0.15f, 0.15f, 0.17f, 1.0f)));
//     gray800->SetDescription("Dark gray");
//     RegisterToken(gray800);
    
//     auto gray700 = std::make_shared<Token>("primitive.color.gray.700", TokenLevel::Primitive, ValueType::Color);
//     gray700->SetDefaultValue(TokenValue(ImVec4(0.2f, 0.2f, 0.24f, 1.0f)));
//     gray700->SetDescription("Medium dark gray");
//     RegisterToken(gray700);
    
//     auto gray500 = std::make_shared<Token>("primitive.color.gray.500", TokenLevel::Primitive, ValueType::Color);
//     gray500->SetDefaultValue(TokenValue(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)));
//     gray500->SetDescription("Medium gray");
//     RegisterToken(gray500);
    
//     auto gray300 = std::make_shared<Token>("primitive.color.gray.300", TokenLevel::Primitive, ValueType::Color);
//     gray300->SetDefaultValue(TokenValue(ImVec4(0.8f, 0.8f, 0.8f, 1.0f)));
//     gray300->SetDescription("Light gray");
//     RegisterToken(gray300);
    
//     auto white = std::make_shared<Token>("primitive.color.white", TokenLevel::Primitive, ValueType::Color);
//     white->SetDefaultValue(TokenValue(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
//     white->SetDescription("Pure white");
//     RegisterToken(white);
    
//     // ===== SPACING =====
//     auto spacing4 = std::make_shared<Token>("primitive.spacing.4", TokenLevel::Primitive, ValueType::Float);
//     spacing4->SetDefaultValue(TokenValue(4.0f));
//     spacing4->SetDescription("4px spacing");
//     RegisterToken(spacing4);
    
//     auto spacing8 = std::make_shared<Token>("primitive.spacing.8", TokenLevel::Primitive, ValueType::Float);
//     spacing8->SetDefaultValue(TokenValue(8.0f));
//     spacing8->SetDescription("8px spacing");
//     RegisterToken(spacing8);
    
//     auto spacing12 = std::make_shared<Token>("primitive.spacing.12", TokenLevel::Primitive, ValueType::Float);
//     spacing12->SetDefaultValue(TokenValue(12.0f));
//     spacing12->SetDescription("12px spacing");
//     RegisterToken(spacing12);
    
//     auto spacing16 = std::make_shared<Token>("primitive.spacing.16", TokenLevel::Primitive, ValueType::Float);
//     spacing16->SetDefaultValue(TokenValue(16.0f));
//     spacing16->SetDescription("16px spacing");
//     RegisterToken(spacing16);
    
//     // ===== RADIUS (limité à 12 max) =====
//     auto radius4 = std::make_shared<Token>("primitive.radius.4", TokenLevel::Primitive, ValueType::Float);
//     radius4->SetDefaultValue(TokenValue(4.0f));
//     radius4->SetDescription("Small border radius");
//     RegisterToken(radius4);
    
//     auto radius6 = std::make_shared<Token>("primitive.radius.6", TokenLevel::Primitive, ValueType::Float);
//     radius6->SetDefaultValue(TokenValue(6.0f));
//     radius6->SetDescription("Medium-small border radius");
//     RegisterToken(radius6);
    
//     auto radius8 = std::make_shared<Token>("primitive.radius.8", TokenLevel::Primitive, ValueType::Float);
//     radius8->SetDefaultValue(TokenValue(8.0f));
//     radius8->SetDescription("Medium border radius");
//     RegisterToken(radius8);
    
//     auto radius12 = std::make_shared<Token>("primitive.radius.12", TokenLevel::Primitive, ValueType::Float);
//     radius12->SetDefaultValue(TokenValue(12.0f));
//     radius12->SetDescription("Large border radius (max)");
//     RegisterToken(radius12);
    
//     // ===== ALPHA =====
//     auto alpha100 = std::make_shared<Token>("primitive.alpha.100", TokenLevel::Primitive, ValueType::Float);
//     alpha100->SetDefaultValue(TokenValue(1.0f));
//     alpha100->SetDescription("Fully opaque");
//     RegisterToken(alpha100);
    
//     auto alpha90 = std::make_shared<Token>("primitive.alpha.90", TokenLevel::Primitive, ValueType::Float);
//     alpha90->SetDefaultValue(TokenValue(0.9f));
//     alpha90->SetDescription("90% opacity");
//     RegisterToken(alpha90);
    
//     auto alpha75 = std::make_shared<Token>("primitive.alpha.75", TokenLevel::Primitive, ValueType::Float);
//     alpha75->SetDefaultValue(TokenValue(0.75f));
//     alpha75->SetDescription("75% opacity");
//     RegisterToken(alpha75);
    
//     auto alpha50 = std::make_shared<Token>("primitive.alpha.50", TokenLevel::Primitive, ValueType::Float);
//     alpha50->SetDefaultValue(TokenValue(0.5f));
//     alpha50->SetDescription("50% opacity");
//     RegisterToken(alpha50);
    
//     // ===== FONT SIZE =====
//     auto fontSize12 = std::make_shared<Token>("primitive.fontSize.12", TokenLevel::Primitive, ValueType::Float);
//     fontSize12->SetDefaultValue(TokenValue(12.0f));
//     fontSize12->SetDescription("Small font size");
//     RegisterToken(fontSize12);
    
//     auto fontSize14 = std::make_shared<Token>("primitive.fontSize.14", TokenLevel::Primitive, ValueType::Float);
//     fontSize14->SetDefaultValue(TokenValue(14.0f));
//     fontSize14->SetDescription("Default font size");
//     RegisterToken(fontSize14);
    
//     auto fontSize16 = std::make_shared<Token>("primitive.fontSize.16", TokenLevel::Primitive, ValueType::Float);
//     fontSize16->SetDefaultValue(TokenValue(16.0f));
//     fontSize16->SetDescription("Medium font size");
//     RegisterToken(fontSize16);
    
//     auto fontSize18 = std::make_shared<Token>("primitive.fontSize.18", TokenLevel::Primitive, ValueType::Float);
//     fontSize18->SetDefaultValue(TokenValue(18.0f));
//     fontSize18->SetDescription("Large font size");
//     RegisterToken(fontSize18);
    
//     auto fontSize20 = std::make_shared<Token>("primitive.fontSize.20", TokenLevel::Primitive, ValueType::Float);
//     fontSize20->SetDefaultValue(TokenValue(20.0f));
//     fontSize20->SetDescription("Extra large font size");
//     RegisterToken(fontSize20);
    
//     // ===== SCALE =====
//     auto scale100 = std::make_shared<Token>("primitive.scale.100", TokenLevel::Primitive, ValueType::Float);
//     scale100->SetDefaultValue(TokenValue(1.0f));
//     scale100->SetDescription("Normal scale (100%)");
//     RegisterToken(scale100);
    
//     auto scale125 = std::make_shared<Token>("primitive.scale.125", TokenLevel::Primitive, ValueType::Float);
//     scale125->SetDefaultValue(TokenValue(1.25f));
//     scale125->SetDescription("125% scale");
//     RegisterToken(scale125);
    
//     auto scale150 = std::make_shared<Token>("primitive.scale.150", TokenLevel::Primitive, ValueType::Float);
//     scale150->SetDefaultValue(TokenValue(1.5f));
//     scale150->SetDescription("150% scale");
//     RegisterToken(scale150);
    
//     auto scale75 = std::make_shared<Token>("primitive.scale.75", TokenLevel::Primitive, ValueType::Float);
//     scale75->SetDefaultValue(TokenValue(0.75f));
//     scale75->SetDescription("75% scale");
//     RegisterToken(scale75);
// }

// void TokenRegistry::CreateDefaultSemanticTokens() {
//     // ===== COLORS =====
//     auto primaryColor = std::make_shared<Token>("semantic.color.primary", TokenLevel::Semantic, ValueType::Reference);
//     primaryColor->SetDefaultValue(TokenValue("primitive.color.blue.500"));
//     primaryColor->SetDescription("Primary brand color");
//     primaryColor->SetContextValue(Context(ThemeType::MutedGreen, AccessibilityType::None), 
//                                    TokenValue("primitive.color.green.500"));
//     RegisterToken(primaryColor);
    
//     auto dangerColor = std::make_shared<Token>("semantic.color.danger", TokenLevel::Semantic, ValueType::Reference);
//     dangerColor->SetDefaultValue(TokenValue("primitive.color.red.500"));
//     dangerColor->SetDescription("Danger/error color");
//     RegisterToken(dangerColor);
    
//     auto bgColor = std::make_shared<Token>("semantic.color.background", TokenLevel::Semantic, ValueType::Reference);
//     bgColor->SetDefaultValue(TokenValue("primitive.color.gray.900"));
//     bgColor->SetDescription("Main background color");
//     RegisterToken(bgColor);
    
//     auto surfaceColor = std::make_shared<Token>("semantic.color.surface", TokenLevel::Semantic, ValueType::Reference);
//     surfaceColor->SetDefaultValue(TokenValue("primitive.color.gray.800"));
//     surfaceColor->SetDescription("Surface/card background");
//     RegisterToken(surfaceColor);
    
//     auto textColor = std::make_shared<Token>("semantic.color.text", TokenLevel::Semantic, ValueType::Reference);
//     textColor->SetDefaultValue(TokenValue("primitive.color.white"));
//     textColor->SetDescription("Primary text color");
//     RegisterToken(textColor);
    
//     auto textMuted = std::make_shared<Token>("semantic.color.text.muted", TokenLevel::Semantic, ValueType::Reference);
//     textMuted->SetDefaultValue(TokenValue("primitive.color.gray.500"));
//     textMuted->SetDescription("Muted/secondary text");
//     RegisterToken(textMuted);
    
//     // ===== SPACING =====
//     auto spacingSmall = std::make_shared<Token>("semantic.spacing.small", TokenLevel::Semantic, ValueType::Reference);
//     spacingSmall->SetDefaultValue(TokenValue("primitive.spacing.4"));
//     spacingSmall->SetDescription("Small spacing");
//     RegisterToken(spacingSmall);
    
//     auto spacingMedium = std::make_shared<Token>("semantic.spacing.medium", TokenLevel::Semantic, ValueType::Reference);
//     spacingMedium->SetDefaultValue(TokenValue("primitive.spacing.8"));
//     spacingMedium->SetDescription("Medium spacing");
//     RegisterToken(spacingMedium);
    
//     auto spacingLarge = std::make_shared<Token>("semantic.spacing.large", TokenLevel::Semantic, ValueType::Reference);
//     spacingLarge->SetDefaultValue(TokenValue("primitive.spacing.12"));
//     spacingLarge->SetDescription("Large spacing");
//     RegisterToken(spacingLarge);
    
//     // ===== RADIUS =====
//     auto radiusSmall = std::make_shared<Token>("semantic.radius.small", TokenLevel::Semantic, ValueType::Reference);
//     radiusSmall->SetDefaultValue(TokenValue("primitive.radius.4"));
//     radiusSmall->SetDescription("Small border radius");
//     RegisterToken(radiusSmall);
    
//     auto radiusDefault = std::make_shared<Token>("semantic.radius.default", TokenLevel::Semantic, ValueType::Reference);
//     radiusDefault->SetDefaultValue(TokenValue("primitive.radius.8"));
//     radiusDefault->SetDescription("Default border radius");
//     RegisterToken(radiusDefault);
    
//     auto radiusLarge = std::make_shared<Token>("semantic.radius.large", TokenLevel::Semantic, ValueType::Reference);
//     radiusLarge->SetDefaultValue(TokenValue("primitive.radius.12"));
//     radiusLarge->SetDescription("Large border radius");
//     RegisterToken(radiusLarge);
    
//     // ===== ALPHA =====
//     auto alphaDefault = std::make_shared<Token>("semantic.alpha.default", TokenLevel::Semantic, ValueType::Reference);
//     alphaDefault->SetDefaultValue(TokenValue("primitive.alpha.100"));
//     alphaDefault->SetDescription("Default global alpha");
//     RegisterToken(alphaDefault);
    
//     // ===== FONT SIZE =====
//     auto fontSizeDefault = std::make_shared<Token>("semantic.fontSize.default", TokenLevel::Semantic, ValueType::Reference);
//     fontSizeDefault->SetDefaultValue(TokenValue("primitive.fontSize.14"));
//     fontSizeDefault->SetDescription("Default font size");
//     RegisterToken(fontSizeDefault);
    
//     // ===== SCALE =====
//     auto scaleDefault = std::make_shared<Token>("semantic.scale.default", TokenLevel::Semantic, ValueType::Reference);
//     scaleDefault->SetDefaultValue(TokenValue("primitive.scale.100"));
//     scaleDefault->SetDescription("Default UI scale");
//     RegisterToken(scaleDefault);
// }

// void TokenRegistry::CreateDefaultComponentTokens() {
//     // ===== BUTTON =====
//     auto buttonBg = std::make_shared<Token>("component.button.background", TokenLevel::Component, ValueType::Reference);
//     buttonBg->SetDefaultValue(TokenValue("semantic.color.primary"));
//     buttonBg->SetDescription("Button background color");
//     RegisterToken(buttonBg);
    
//     auto buttonText = std::make_shared<Token>("component.button.text", TokenLevel::Component, ValueType::Reference);
//     buttonText->SetDefaultValue(TokenValue("primitive.color.white"));
//     buttonText->SetDescription("Button text color");
//     RegisterToken(buttonText);
    
//     auto buttonRadius = std::make_shared<Token>("component.button.radius", TokenLevel::Component, ValueType::Reference);
//     buttonRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
//     buttonRadius->SetDescription("Button border radius");
//     RegisterToken(buttonRadius);
    
//     auto buttonPadding = std::make_shared<Token>("component.button.padding", TokenLevel::Component, ValueType::Vec2);
//     buttonPadding->SetDefaultValue(TokenValue(ImVec2(10.0f, 5.0f)));
//     buttonPadding->SetDescription("Button padding (x, y)");
//     RegisterToken(buttonPadding);
    
//     // ===== FRAME/INPUT =====
//     auto frameBg = std::make_shared<Token>("component.frame.background", TokenLevel::Component, ValueType::Reference);
//     frameBg->SetDefaultValue(TokenValue("semantic.color.surface"));
//     frameBg->SetDescription("Input frame background");
//     RegisterToken(frameBg);
    
//     auto frameRadius = std::make_shared<Token>("component.frame.radius", TokenLevel::Component, ValueType::Reference);
//     frameRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
//     frameRadius->SetDescription("Frame border radius");
//     RegisterToken(frameRadius);
    
//     auto framePadding = std::make_shared<Token>("component.frame.padding", TokenLevel::Component, ValueType::Vec2);
//     framePadding->SetDefaultValue(TokenValue(ImVec2(10.0f, 5.0f)));
//     framePadding->SetDescription("Frame padding (x, y)");
//     RegisterToken(framePadding);
    
//     // ===== WINDOW =====
//     auto windowRadius = std::make_shared<Token>("component.window.radius", TokenLevel::Component, ValueType::Reference);
//     windowRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
//     windowRadius->SetDescription("Window border radius");
//     RegisterToken(windowRadius);
    
//     auto childRadius = std::make_shared<Token>("component.child.radius", TokenLevel::Component, ValueType::Reference);
//     childRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
//     childRadius->SetDescription("Child window border radius");
//     RegisterToken(childRadius);
    
//     auto popupRadius = std::make_shared<Token>("component.popup.radius", TokenLevel::Component, ValueType::Reference);
//     popupRadius->SetDefaultValue(TokenValue("semantic.radius.small"));
//     popupRadius->SetDescription("Popup border radius");
//     RegisterToken(popupRadius);
    
//     auto grabRadius = std::make_shared<Token>("component.grab.radius", TokenLevel::Component, ValueType::Reference);
//     grabRadius->SetDefaultValue(TokenValue("semantic.radius.small"));
//     grabRadius->SetDescription("Grab/slider border radius");
//     RegisterToken(grabRadius);
// }

// } // namespace DesignSystem







#include <DesignSystem/Tokens/TokenRegistry.h>

namespace DesignSystem {

TokenRegistry& TokenRegistry::Instance() {
    static TokenRegistry* instance = new TokenRegistry();
    return *instance;
}

void TokenRegistry::RegisterToken(std::shared_ptr<Token> token) {
    tokens_[token->GetId()] = token;
}

std::shared_ptr<Token> TokenRegistry::GetToken(const std::string& id) const {
    auto it = tokens_.find(id);
    if (it != tokens_.end()) {
        return it->second;
    }
    return nullptr;
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

std::vector<std::shared_ptr<Token>> TokenRegistry::GetTokensByLevel(TokenLevel level) const {
    std::vector<std::shared_ptr<Token>> result;
    for (const auto& [id, token] : tokens_) {
        if (token->GetLevel() == level) {
            result.push_back(token);
        }
    }
    return result;
}

std::vector<std::shared_ptr<Token>> TokenRegistry::GetAllTokens() const {
    std::vector<std::shared_ptr<Token>> result;
    for (const auto& [id, token] : tokens_) {
        result.push_back(token);
    }
    return result;
}

void TokenRegistry::InitializeDefaultTokens() {
    Clear();
    CreateDefaultPrimitiveTokens();
    CreateDefaultSemanticTokens();
    CreateDefaultComponentTokens();
}

void TokenRegistry::CreateDefaultPrimitiveTokens() {
    // ===== COLORS =====
    auto blue500 = std::make_shared<Token>("primitive.color.blue.500", TokenLevel::Primitive, ValueType::Color);
    blue500->SetDefaultValue(TokenValue(ImVec4(0.26f, 0.59f, 0.98f, 1.0f)));
    blue500->SetDescription("Base blue color");
    RegisterToken(blue500);
    
    auto green500 = std::make_shared<Token>("primitive.color.green.500", TokenLevel::Primitive, ValueType::Color);
    green500->SetDefaultValue(TokenValue(ImVec4(0.3f, 0.7f, 0.3f, 1.0f)));
    green500->SetDescription("Base green color");
    RegisterToken(green500);
    
    auto red500 = std::make_shared<Token>("primitive.color.red.500", TokenLevel::Primitive, ValueType::Color);
    red500->SetDefaultValue(TokenValue(ImVec4(0.9f, 0.26f, 0.26f, 1.0f)));
    red500->SetDescription("Base red color");
    RegisterToken(red500);
    
    auto gray900 = std::make_shared<Token>("primitive.color.gray.900", TokenLevel::Primitive, ValueType::Color);
    gray900->SetDefaultValue(TokenValue(ImVec4(0.1f, 0.1f, 0.1f, 1.0f)));
    gray900->SetDescription("Very dark gray");
    RegisterToken(gray900);
    
    auto gray800 = std::make_shared<Token>("primitive.color.gray.800", TokenLevel::Primitive, ValueType::Color);
    gray800->SetDefaultValue(TokenValue(ImVec4(0.15f, 0.15f, 0.17f, 1.0f)));
    gray800->SetDescription("Dark gray");
    RegisterToken(gray800);
    
    auto gray700 = std::make_shared<Token>("primitive.color.gray.700", TokenLevel::Primitive, ValueType::Color);
    gray700->SetDefaultValue(TokenValue(ImVec4(0.2f, 0.2f, 0.24f, 1.0f)));
    gray700->SetDescription("Medium dark gray");
    RegisterToken(gray700);
    
    auto gray500 = std::make_shared<Token>("primitive.color.gray.500", TokenLevel::Primitive, ValueType::Color);
    gray500->SetDefaultValue(TokenValue(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)));
    gray500->SetDescription("Medium gray");
    RegisterToken(gray500);
    
    auto gray300 = std::make_shared<Token>("primitive.color.gray.300", TokenLevel::Primitive, ValueType::Color);
    gray300->SetDefaultValue(TokenValue(ImVec4(0.8f, 0.8f, 0.8f, 1.0f)));
    gray300->SetDescription("Light gray");
    RegisterToken(gray300);
    
    auto white = std::make_shared<Token>("primitive.color.white", TokenLevel::Primitive, ValueType::Color);
    white->SetDefaultValue(TokenValue(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
    white->SetDescription("Pure white");
    RegisterToken(white);
    
    auto black = std::make_shared<Token>("primitive.color.black", TokenLevel::Primitive, ValueType::Color);
    black->SetDefaultValue(TokenValue(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)));
    black->SetDescription("Pure black");
    RegisterToken(black);

    auto orange = std::make_shared<Token>("primitive.color.orange.500", TokenLevel::Primitive, ValueType::Color);
    orange->SetDefaultValue(TokenValue(ImVec4(1.0f, 0.75f, 0.0f, 1.0f)));
    orange->SetDescription("Warning orange");
    RegisterToken(orange);
    
    // ===== SPACING =====
    auto spacing4 = std::make_shared<Token>("primitive.spacing.4", TokenLevel::Primitive, ValueType::Float);
    spacing4->SetDefaultValue(TokenValue(4.0f));
    spacing4->SetDescription("4px spacing");
    RegisterToken(spacing4);
    
    auto spacing8 = std::make_shared<Token>("primitive.spacing.8", TokenLevel::Primitive, ValueType::Float);
    spacing8->SetDefaultValue(TokenValue(8.0f));
    spacing8->SetDescription("8px spacing");
    RegisterToken(spacing8);
    
    auto spacing12 = std::make_shared<Token>("primitive.spacing.12", TokenLevel::Primitive, ValueType::Float);
    spacing12->SetDefaultValue(TokenValue(12.0f));
    spacing12->SetDescription("12px spacing");
    RegisterToken(spacing12);
    
    auto spacing16 = std::make_shared<Token>("primitive.spacing.16", TokenLevel::Primitive, ValueType::Float);
    spacing16->SetDefaultValue(TokenValue(16.0f));
    spacing16->SetDescription("16px spacing");
    RegisterToken(spacing16);
    
    // ===== RADIUS (limité à 12 max) =====
    auto radius4 = std::make_shared<Token>("primitive.radius.4", TokenLevel::Primitive, ValueType::Float);
    radius4->SetDefaultValue(TokenValue(4.0f));
    radius4->SetDescription("Small border radius");
    RegisterToken(radius4);
    
    auto radius6 = std::make_shared<Token>("primitive.radius.6", TokenLevel::Primitive, ValueType::Float);
    radius6->SetDefaultValue(TokenValue(6.0f));
    radius6->SetDescription("Medium-small border radius");
    RegisterToken(radius6);
    
    auto radius8 = std::make_shared<Token>("primitive.radius.8", TokenLevel::Primitive, ValueType::Float);
    radius8->SetDefaultValue(TokenValue(8.0f));
    radius8->SetDescription("Medium border radius");
    RegisterToken(radius8);
    
    auto radius12 = std::make_shared<Token>("primitive.radius.12", TokenLevel::Primitive, ValueType::Float);
    radius12->SetDefaultValue(TokenValue(12.0f));
    radius12->SetDescription("Large border radius (max)");
    RegisterToken(radius12);
    
    // ===== ALPHA =====
    auto alpha100 = std::make_shared<Token>("primitive.alpha.100", TokenLevel::Primitive, ValueType::Float);
    alpha100->SetDefaultValue(TokenValue(1.0f));
    alpha100->SetDescription("Fully opaque");
    RegisterToken(alpha100);
    
    auto alpha90 = std::make_shared<Token>("primitive.alpha.90", TokenLevel::Primitive, ValueType::Float);
    alpha90->SetDefaultValue(TokenValue(0.9f));
    alpha90->SetDescription("90% opacity");
    RegisterToken(alpha90);
    
    auto alpha75 = std::make_shared<Token>("primitive.alpha.75", TokenLevel::Primitive, ValueType::Float);
    alpha75->SetDefaultValue(TokenValue(0.75f));
    alpha75->SetDescription("75% opacity");
    RegisterToken(alpha75);
    
    auto alpha50 = std::make_shared<Token>("primitive.alpha.50", TokenLevel::Primitive, ValueType::Float);
    alpha50->SetDefaultValue(TokenValue(0.5f));
    alpha50->SetDescription("50% opacity");
    RegisterToken(alpha50);
    
    // ===== FONT SIZE =====
    auto fontSize12 = std::make_shared<Token>("primitive.fontSize.12", TokenLevel::Primitive, ValueType::Float);
    fontSize12->SetDefaultValue(TokenValue(12.0f));
    fontSize12->SetDescription("Small font size");
    RegisterToken(fontSize12);
    
    auto fontSize14 = std::make_shared<Token>("primitive.fontSize.14", TokenLevel::Primitive, ValueType::Float);
    fontSize14->SetDefaultValue(TokenValue(14.0f));
    fontSize14->SetDescription("Default font size");
    RegisterToken(fontSize14);
    
    auto fontSize16 = std::make_shared<Token>("primitive.fontSize.16", TokenLevel::Primitive, ValueType::Float);
    fontSize16->SetDefaultValue(TokenValue(16.0f));
    fontSize16->SetDescription("Medium font size");
    RegisterToken(fontSize16);
    
    auto fontSize18 = std::make_shared<Token>("primitive.fontSize.18", TokenLevel::Primitive, ValueType::Float);
    fontSize18->SetDefaultValue(TokenValue(18.0f));
    fontSize18->SetDescription("Large font size");
    RegisterToken(fontSize18);
    
    auto fontSize20 = std::make_shared<Token>("primitive.fontSize.20", TokenLevel::Primitive, ValueType::Float);
    fontSize20->SetDefaultValue(TokenValue(20.0f));
    fontSize20->SetDescription("Extra large font size");
    RegisterToken(fontSize20);
    
    // ===== SCALE =====
    auto scale100 = std::make_shared<Token>("primitive.scale.100", TokenLevel::Primitive, ValueType::Float);
    scale100->SetDefaultValue(TokenValue(1.0f));
    scale100->SetDescription("Normal scale (100%)");
    RegisterToken(scale100);
    
    auto scale125 = std::make_shared<Token>("primitive.scale.125", TokenLevel::Primitive, ValueType::Float);
    scale125->SetDefaultValue(TokenValue(1.25f));
    scale125->SetDescription("125% scale");
    RegisterToken(scale125);
    
    auto scale150 = std::make_shared<Token>("primitive.scale.150", TokenLevel::Primitive, ValueType::Float);
    scale150->SetDefaultValue(TokenValue(1.5f));
    scale150->SetDescription("150% scale");
    RegisterToken(scale150);
    
    auto scale75 = std::make_shared<Token>("primitive.scale.75", TokenLevel::Primitive, ValueType::Float);
    scale75->SetDefaultValue(TokenValue(0.75f));
    scale75->SetDescription("75% scale");
    RegisterToken(scale75);
}

void TokenRegistry::CreateDefaultSemanticTokens() {
    // ===== COLORS =====
    auto primaryColor = std::make_shared<Token>("semantic.color.primary", TokenLevel::Semantic, ValueType::Reference);
    primaryColor->SetDefaultValue(TokenValue("primitive.color.blue.500"));
    primaryColor->SetDescription("Primary brand color");
    primaryColor->SetContextValue(Context(ThemeType::MutedGreen, AccessibilityType::None), 
                                   TokenValue("primitive.color.green.500"));
    RegisterToken(primaryColor);
    
    auto dangerColor = std::make_shared<Token>("semantic.color.danger", TokenLevel::Semantic, ValueType::Reference);
    dangerColor->SetDefaultValue(TokenValue("primitive.color.red.500"));
    dangerColor->SetDescription("Danger/error color");
    RegisterToken(dangerColor);
    
    auto bgColor = std::make_shared<Token>("semantic.color.background", TokenLevel::Semantic, ValueType::Reference);
    bgColor->SetDefaultValue(TokenValue("primitive.color.gray.900"));
    bgColor->SetDescription("Main background color");
    RegisterToken(bgColor);
    
    auto surfaceColor = std::make_shared<Token>("semantic.color.surface", TokenLevel::Semantic, ValueType::Reference);
    surfaceColor->SetDefaultValue(TokenValue("primitive.color.gray.800"));
    surfaceColor->SetDescription("Surface/card background");
    RegisterToken(surfaceColor);
    
    auto textColor = std::make_shared<Token>("semantic.color.text", TokenLevel::Semantic, ValueType::Reference);
    textColor->SetDefaultValue(TokenValue("primitive.color.white"));
    textColor->SetDescription("Primary text color");
    RegisterToken(textColor);
    
    auto textMuted = std::make_shared<Token>("semantic.color.text.muted", TokenLevel::Semantic, ValueType::Reference);
    textMuted->SetDefaultValue(TokenValue("primitive.color.gray.500"));
    textMuted->SetDescription("Muted/secondary text");
    RegisterToken(textMuted);

    auto warning = std::make_shared<Token>("semantic.color.warning", TokenLevel::Semantic, ValueType::Color);
    warning->SetDefaultValue(TokenValue("primitive.color.orange.500"));
    warning->SetDescription("Warning color used to indicate caution or non-critical alerts");
    RegisterToken(warning);
    
    // ===== ICON COLORS =====
    auto iconPrimary = std::make_shared<Token>("semantic.icon.color.primary", TokenLevel::Semantic, ValueType::Reference);
    iconPrimary->SetDefaultValue(TokenValue("primitive.color.white"));
    iconPrimary->SetDescription("Primary icon color (for bicolor icons)");
    RegisterToken(iconPrimary);
    
    auto iconSecondary = std::make_shared<Token>("semantic.icon.color.secondary", TokenLevel::Semantic, ValueType::Reference);
    iconSecondary->SetDefaultValue(TokenValue("primitive.color.gray.700"));
    iconSecondary->SetDescription("Secondary icon color (for bicolor icons)");
    RegisterToken(iconSecondary);
    
    // ===== SPACING =====
    auto spacingSmall = std::make_shared<Token>("semantic.spacing.small", TokenLevel::Semantic, ValueType::Reference);
    spacingSmall->SetDefaultValue(TokenValue("primitive.spacing.4"));
    spacingSmall->SetDescription("Small spacing");
    RegisterToken(spacingSmall);
    
    auto spacingMedium = std::make_shared<Token>("semantic.spacing.medium", TokenLevel::Semantic, ValueType::Reference);
    spacingMedium->SetDefaultValue(TokenValue("primitive.spacing.8"));
    spacingMedium->SetDescription("Medium spacing");
    RegisterToken(spacingMedium);
    
    auto spacingLarge = std::make_shared<Token>("semantic.spacing.large", TokenLevel::Semantic, ValueType::Reference);
    spacingLarge->SetDefaultValue(TokenValue("primitive.spacing.12"));
    spacingLarge->SetDescription("Large spacing");
    RegisterToken(spacingLarge);
    
    // ===== RADIUS =====
    auto radiusSmall = std::make_shared<Token>("semantic.radius.small", TokenLevel::Semantic, ValueType::Reference);
    radiusSmall->SetDefaultValue(TokenValue("primitive.radius.4"));
    radiusSmall->SetDescription("Small border radius");
    RegisterToken(radiusSmall);
    
    auto radiusDefault = std::make_shared<Token>("semantic.radius.default", TokenLevel::Semantic, ValueType::Reference);
    radiusDefault->SetDefaultValue(TokenValue("primitive.radius.8"));
    radiusDefault->SetDescription("Default border radius");
    RegisterToken(radiusDefault);
    
    auto radiusLarge = std::make_shared<Token>("semantic.radius.large", TokenLevel::Semantic, ValueType::Reference);
    radiusLarge->SetDefaultValue(TokenValue("primitive.radius.12"));
    radiusLarge->SetDescription("Large border radius");
    RegisterToken(radiusLarge);
    
    // ===== ALPHA =====
    auto alphaDefault = std::make_shared<Token>("semantic.alpha.default", TokenLevel::Semantic, ValueType::Reference);
    alphaDefault->SetDefaultValue(TokenValue("primitive.alpha.100"));
    alphaDefault->SetDescription("Default global alpha");
    RegisterToken(alphaDefault);
    
    // ===== FONT SIZE =====
    auto fontSizeDefault = std::make_shared<Token>("semantic.fontSize.default", TokenLevel::Semantic, ValueType::Reference);
    fontSizeDefault->SetDefaultValue(TokenValue("primitive.fontSize.14"));
    fontSizeDefault->SetDescription("Default font size");
    RegisterToken(fontSizeDefault);
    
    // ===== SCALE =====
    auto scaleDefault = std::make_shared<Token>("semantic.scale.default", TokenLevel::Semantic, ValueType::Reference);
    scaleDefault->SetDefaultValue(TokenValue("primitive.scale.100"));
    scaleDefault->SetDescription("Default UI scale");
    RegisterToken(scaleDefault);
}

void TokenRegistry::CreateDefaultComponentTokens() {
    // ===== BUTTON =====
    auto buttonBg = std::make_shared<Token>("component.button.background", TokenLevel::Component, ValueType::Reference);
    buttonBg->SetDefaultValue(TokenValue("semantic.color.primary"));
    buttonBg->SetDescription("Button background color");
    RegisterToken(buttonBg);
    
    auto buttonText = std::make_shared<Token>("component.button.text", TokenLevel::Component, ValueType::Reference);
    buttonText->SetDefaultValue(TokenValue("primitive.color.white"));
    buttonText->SetDescription("Button text color");
    RegisterToken(buttonText);
    
    auto buttonRadius = std::make_shared<Token>("component.button.radius", TokenLevel::Component, ValueType::Reference);
    buttonRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
    buttonRadius->SetDescription("Button border radius");
    RegisterToken(buttonRadius);
    
    auto buttonPadding = std::make_shared<Token>("component.button.padding", TokenLevel::Component, ValueType::Vec2);
    buttonPadding->SetDefaultValue(TokenValue(ImVec2(10.0f, 5.0f)));
    buttonPadding->SetDescription("Button padding (x, y)");
    RegisterToken(buttonPadding);
    
    // ===== FRAME/INPUT =====
    auto frameBg = std::make_shared<Token>("component.frame.background", TokenLevel::Component, ValueType::Reference);
    frameBg->SetDefaultValue(TokenValue("semantic.color.surface"));
    frameBg->SetDescription("Input frame background");
    RegisterToken(frameBg);
    
    auto frameRadius = std::make_shared<Token>("component.frame.radius", TokenLevel::Component, ValueType::Reference);
    frameRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
    frameRadius->SetDescription("Frame border radius");
    RegisterToken(frameRadius);
    
    auto framePadding = std::make_shared<Token>("component.frame.padding", TokenLevel::Component, ValueType::Vec2);
    framePadding->SetDefaultValue(TokenValue(ImVec2(10.0f, 5.0f)));
    framePadding->SetDescription("Frame padding (x, y)");
    RegisterToken(framePadding);
    
    // ===== WINDOW =====
    auto windowRadius = std::make_shared<Token>("component.window.radius", TokenLevel::Component, ValueType::Reference);
    windowRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
    windowRadius->SetDescription("Window border radius");
    RegisterToken(windowRadius);
    
    auto childRadius = std::make_shared<Token>("component.child.radius", TokenLevel::Component, ValueType::Reference);
    childRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
    childRadius->SetDescription("Child window border radius");
    RegisterToken(childRadius);
    
    auto popupRadius = std::make_shared<Token>("component.popup.radius", TokenLevel::Component, ValueType::Reference);
    popupRadius->SetDefaultValue(TokenValue("semantic.radius.small"));
    popupRadius->SetDescription("Popup border radius");
    RegisterToken(popupRadius);
    
    auto grabRadius = std::make_shared<Token>("component.grab.radius", TokenLevel::Component, ValueType::Reference);
    grabRadius->SetDefaultValue(TokenValue("semantic.radius.small"));
    grabRadius->SetDescription("Grab/slider border radius");
    RegisterToken(grabRadius);
}

} // namespace DesignSystem
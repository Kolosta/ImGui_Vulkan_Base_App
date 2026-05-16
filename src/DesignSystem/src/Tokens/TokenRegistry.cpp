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

    // ===== FONT SCALE =====
    // Multiplier applied ONLY to font sizes (on top of UI scale).
    // Keeps the bitmap-font sharp scheme: imgui re-rasterises at the
    // final integer pixel size, never a post-raster zoom.
    auto fontScale100 = std::make_shared<Token>("primitive.fontScale.100", TokenLevel::Primitive, ValueType::Float);
    fontScale100->SetDefaultValue(TokenValue(1.0f));
    fontScale100->SetDescription("Normal font scale (100%)");
    RegisterToken(fontScale100);

    auto fontScale75 = std::make_shared<Token>("primitive.fontScale.75", TokenLevel::Primitive, ValueType::Float);
    fontScale75->SetDefaultValue(TokenValue(0.75f));
    fontScale75->SetDescription("75% font scale");
    RegisterToken(fontScale75);

    auto fontScale125 = std::make_shared<Token>("primitive.fontScale.125", TokenLevel::Primitive, ValueType::Float);
    fontScale125->SetDefaultValue(TokenValue(1.25f));
    fontScale125->SetDescription("125% font scale");
    RegisterToken(fontScale125);

    auto fontScale150 = std::make_shared<Token>("primitive.fontScale.150", TokenLevel::Primitive, ValueType::Float);
    fontScale150->SetDefaultValue(TokenValue(1.5f));
    fontScale150->SetDescription("150% font scale");
    RegisterToken(fontScale150);

    // ── Apply per-category value constraints.  Centralising the policy here
    // keeps each token's declaration short and makes it easy to evolve the
    // accepted range without editing many lines. The id substring drives the
    // category; this mirrors the previous TokenEditor name-heuristic but now
    // it is the *token* that carries the rule, not the UI.
    struct ConstraintRule {
        const char* idContains;
        ValueConstraint constraint;
    };
    const ConstraintRule rules[] = {
        { "spacing",   ValueConstraint::Range(0.0,  256.0, 0.0, "px, positive") },
        { "radius",    ValueConstraint::Range(0.0,   50.0, 0.0, "px, 0..50")    },
        { "alpha",     ValueConstraint::AlphaRange()                            },
        { "fontSize",  ValueConstraint::Range(6.0,   72.0, 0.0, "px")           },
        { "fontScale", ValueConstraint::Range(0.5,    3.0, 0.0, "multiplier")   },
        { "scale",     ValueConstraint::Range(0.25,   4.0, 0.0, "multiplier")   },
    };
    for (auto& [id, token] : tokens_) {
        if (token->GetValueType() != ValueType::Float) continue;
        for (const auto& r : rules) {
            // "scale" is a substring of "fontScale"; check fontScale first by
            // ordering the rules array, then break on the first match.
            if (id.find(r.idContains) != std::string::npos) {
                token->SetConstraint(r.constraint);
                break;
            }
        }
    }
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

    auto warning = std::make_shared<Token>("semantic.color.warning", TokenLevel::Semantic, ValueType::Reference);
    warning->SetDefaultValue(TokenValue("primitive.color.orange.500"));
    warning->SetDescription("Warning color used to indicate caution or non-critical alerts");
    RegisterToken(warning);

    auto success = std::make_shared<Token>("semantic.color.success", TokenLevel::Semantic, ValueType::Reference);
    success->SetDefaultValue(TokenValue("primitive.color.green.500"));
    success->SetDescription("Success color (e.g. 'no conflicts')");
    RegisterToken(success);

    auto recording = std::make_shared<Token>("semantic.color.recording", TokenLevel::Semantic, ValueType::Reference);
    recording->SetDefaultValue(TokenValue("primitive.color.red.500"));
    recording->SetDescription("Recording / capture-active indicator");
    RegisterToken(recording);

    auto surfaceElevated = std::make_shared<Token>("semantic.color.surface.elevated", TokenLevel::Semantic, ValueType::Reference);
    surfaceElevated->SetDefaultValue(TokenValue("primitive.color.gray.700"));
    surfaceElevated->SetDescription("Elevated surface (key cap face, raised chips)");
    RegisterToken(surfaceElevated);

    auto borderSubtle = std::make_shared<Token>("semantic.color.border.subtle", TokenLevel::Semantic, ValueType::Reference);
    borderSubtle->SetDefaultValue(TokenValue("primitive.color.gray.500"));
    borderSubtle->SetDescription("Subtle border (key cap, separator overlays)");
    RegisterToken(borderSubtle);

    auto statusbarBg = std::make_shared<Token>("semantic.color.statusbar.background", TokenLevel::Semantic, ValueType::Reference);
    statusbarBg->SetDefaultValue(TokenValue("primitive.color.gray.800"));
    statusbarBg->SetDescription("Bottom status bar background");
    RegisterToken(statusbarBg);

    auto statusbarText = std::make_shared<Token>("semantic.color.statusbar.text", TokenLevel::Semantic, ValueType::Reference);
    statusbarText->SetDefaultValue(TokenValue("semantic.color.text.muted"));
    statusbarText->SetDescription("Bottom status bar text");
    RegisterToken(statusbarText);

    // ===== SHORTCUT BEHAVIOUR ===============================================
    auto dragThreshold = std::make_shared<Token>("semantic.shortcut.dragThreshold", TokenLevel::Semantic, ValueType::Float);
    dragThreshold->SetDefaultValue(TokenValue(6.0f));
    dragThreshold->SetDescription("Distance (logical px) the mouse must travel before "
                                   "a drag-style shortcut triggers");
    dragThreshold->SetConstraint(ValueConstraint::Range(2.0, 64.0, 0.0, "px"));
    RegisterToken(dragThreshold);
    
    // ===== ICON COLORS =====
    auto iconPrimary = std::make_shared<Token>("semantic.icon.color.primary", TokenLevel::Semantic, ValueType::Reference);
    iconPrimary->SetDefaultValue(TokenValue("primitive.color.white"));
    iconPrimary->SetDescription("Primary icon color (for bicolor icons)");
    RegisterToken(iconPrimary);
    
    auto iconSecondary = std::make_shared<Token>("semantic.icon.color.secondary", TokenLevel::Semantic, ValueType::Reference);
    iconSecondary->SetDefaultValue(TokenValue("primitive.color.gray.700"));
    iconSecondary->SetDescription("Secondary icon color (for bicolor icons)");
    RegisterToken(iconSecondary);

    auto iconTertiary = std::make_shared<Token>("semantic.icon.color.tertiary", TokenLevel::Semantic, ValueType::Reference);
    iconTertiary->SetDefaultValue(TokenValue("primitive.color.gray.300"));
    iconTertiary->SetDescription("Tertiary icon color (for bicolor? icons)");
    RegisterToken(iconTertiary);
    
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
    scaleDefault->SetDescription("Global UI scale — multiplies every metric AND fonts");
    RegisterToken(scaleDefault);

    // ===== FONT SCALE =====
    // Extra multiplier applied ONLY to fonts (combined with semantic.scale.default).
    // Lets the user grow/shrink text independently of overall UI density.
    auto fontScaleDefault = std::make_shared<Token>("semantic.fontScale.default", TokenLevel::Semantic, ValueType::Reference);
    fontScaleDefault->SetDefaultValue(TokenValue("primitive.fontScale.100"));
    fontScaleDefault->SetDescription("Font-only scale multiplier (on top of semantic.scale.default)");
    RegisterToken(fontScaleDefault);
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
    
    // ===== FRAME/INPUT (combos, inputs, sliders…) =====
    // Frame background is intentionally a *different* surface than the
    // window/child background so combos & inputs are visible against the
    // panels they sit in.
    auto frameBg = std::make_shared<Token>("component.frame.background", TokenLevel::Component, ValueType::Reference);
    frameBg->SetDefaultValue(TokenValue("semantic.color.surface.elevated"));
    frameBg->SetDescription("Input/combo frame background");
    RegisterToken(frameBg);

    auto frameBgHover = std::make_shared<Token>("component.frame.backgroundHover", TokenLevel::Component, ValueType::Reference);
    frameBgHover->SetDefaultValue(TokenValue("semantic.color.surface.elevated"));
    frameBgHover->SetDescription("Input/combo frame background when hovered");
    RegisterToken(frameBgHover);

    auto frameBgActive = std::make_shared<Token>("component.frame.backgroundActive", TokenLevel::Component, ValueType::Reference);
    frameBgActive->SetDefaultValue(TokenValue("semantic.color.surface"));
    frameBgActive->SetDescription("Input/combo frame background when active");
    RegisterToken(frameBgActive);

    auto frameBorder = std::make_shared<Token>("component.frame.border", TokenLevel::Component, ValueType::Reference);
    frameBorder->SetDefaultValue(TokenValue("semantic.color.border.subtle"));
    frameBorder->SetDescription("Input/combo/window border colour");
    RegisterToken(frameBorder);

    auto frameBorderSize = std::make_shared<Token>("component.frame.borderSize", TokenLevel::Component, ValueType::Float);
    frameBorderSize->SetDefaultValue(TokenValue(1.0f));
    frameBorderSize->SetDescription("Input/combo frame border thickness (px)");
    frameBorderSize->SetConstraint(ValueConstraint::Range(0.0, 4.0, 0.0, "px"));
    RegisterToken(frameBorderSize);

    auto frameRadius = std::make_shared<Token>("component.frame.radius", TokenLevel::Component, ValueType::Reference);
    frameRadius->SetDefaultValue(TokenValue("semantic.radius.default"));
    frameRadius->SetDescription("Frame border radius");
    RegisterToken(frameRadius);

    auto framePadding = std::make_shared<Token>("component.frame.padding", TokenLevel::Component, ValueType::Vec2);
    framePadding->SetDefaultValue(TokenValue(ImVec2(10.0f, 5.0f)));
    framePadding->SetDescription("Frame padding (x, y)");
    RegisterToken(framePadding);

    auto popupBg = std::make_shared<Token>("component.popup.background", TokenLevel::Component, ValueType::Reference);
    popupBg->SetDefaultValue(TokenValue("semantic.color.surface.elevated"));
    popupBg->SetDescription("Popup / combo-list background");
    RegisterToken(popupBg);

    auto popupBorder = std::make_shared<Token>("component.popup.border", TokenLevel::Component, ValueType::Reference);
    popupBorder->SetDefaultValue(TokenValue("semantic.color.border.subtle"));
    popupBorder->SetDescription("Popup / combo-list border colour");
    RegisterToken(popupBorder);

    auto popupBorderSize = std::make_shared<Token>("component.popup.borderSize", TokenLevel::Component, ValueType::Float);
    popupBorderSize->SetDefaultValue(TokenValue(1.0f));
    popupBorderSize->SetDescription("Popup / combo-list border thickness (px)");
    popupBorderSize->SetConstraint(ValueConstraint::Range(0.0, 4.0, 0.0, "px"));
    RegisterToken(popupBorderSize);
    
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

    // ===== KEY CAP (kbd-style chip used to display shortcuts) =====
    auto keycapBg = std::make_shared<Token>("component.keycap.background", TokenLevel::Component, ValueType::Reference);
    keycapBg->SetDefaultValue(TokenValue("semantic.color.surface.elevated"));
    keycapBg->SetDescription("Key cap background");
    RegisterToken(keycapBg);

    auto keycapBorder = std::make_shared<Token>("component.keycap.border", TokenLevel::Component, ValueType::Reference);
    keycapBorder->SetDefaultValue(TokenValue("semantic.color.border.subtle"));
    keycapBorder->SetDescription("Key cap border");
    RegisterToken(keycapBorder);

    auto keycapText = std::make_shared<Token>("component.keycap.text", TokenLevel::Component, ValueType::Reference);
    keycapText->SetDefaultValue(TokenValue("semantic.color.text"));
    keycapText->SetDescription("Key cap text");
    RegisterToken(keycapText);

    auto keycapRadius = std::make_shared<Token>("component.keycap.radius", TokenLevel::Component, ValueType::Reference);
    keycapRadius->SetDefaultValue(TokenValue("primitive.radius.4"));
    keycapRadius->SetDescription("Key cap corner radius");
    RegisterToken(keycapRadius);

    auto keycapPadding = std::make_shared<Token>("component.keycap.padding", TokenLevel::Component, ValueType::Vec2);
    keycapPadding->SetDefaultValue(TokenValue(ImVec2(6.0f, 2.0f)));
    keycapPadding->SetDescription("Key cap inner padding (x,y)");
    RegisterToken(keycapPadding);

    auto keycapFontScale = std::make_shared<Token>("component.keycap.fontScale", TokenLevel::Component, ValueType::Float);
    keycapFontScale->SetDefaultValue(TokenValue(0.85f));
    keycapFontScale->SetDescription("Key cap text scale relative to base font");
    keycapFontScale->SetConstraint(ValueConstraint::Range(0.5, 2.0, 0.0, "multiplier"));
    RegisterToken(keycapFontScale);

    // ===== STATUS BAR =====
    auto statusbarBgC = std::make_shared<Token>("component.statusbar.background", TokenLevel::Component, ValueType::Reference);
    statusbarBgC->SetDefaultValue(TokenValue("semantic.color.statusbar.background"));
    statusbarBgC->SetDescription("Status bar fill colour");
    RegisterToken(statusbarBgC);

    auto statusbarTextC = std::make_shared<Token>("component.statusbar.text", TokenLevel::Component, ValueType::Reference);
    statusbarTextC->SetDefaultValue(TokenValue("semantic.color.statusbar.text"));
    statusbarTextC->SetDescription("Status bar text colour");
    RegisterToken(statusbarTextC);

    auto statusbarHeight = std::make_shared<Token>("component.statusbar.height", TokenLevel::Component, ValueType::Float);
    statusbarHeight->SetDefaultValue(TokenValue(22.0f));
    statusbarHeight->SetDescription("Status bar height in logical pixels");
    statusbarHeight->SetConstraint(ValueConstraint::Range(16.0, 40.0, 0.0, "px"));
    RegisterToken(statusbarHeight);

    auto statusbarPadding = std::make_shared<Token>("component.statusbar.padding", TokenLevel::Component, ValueType::Vec2);
    statusbarPadding->SetDefaultValue(TokenValue(ImVec2(8.0f, 3.0f)));
    statusbarPadding->SetDescription("Status bar inner padding (x,y)");
    RegisterToken(statusbarPadding);

    // ===== SHORTCUT EDITOR ROW STATES =====
    auto shortcutRowHover = std::make_shared<Token>("component.shortcutRow.hoverBackground", TokenLevel::Component, ValueType::Reference);
    shortcutRowHover->SetDefaultValue(TokenValue("semantic.color.surface"));
    shortcutRowHover->SetDescription("Shortcut editor row hover background");
    RegisterToken(shortcutRowHover);

    auto shortcutRowSelected = std::make_shared<Token>("component.shortcutRow.selectedBackground", TokenLevel::Component, ValueType::Reference);
    shortcutRowSelected->SetDefaultValue(TokenValue("semantic.color.primary"));
    shortcutRowSelected->SetDescription("Shortcut editor row selected background");
    RegisterToken(shortcutRowSelected);

    // ===== SHORTCUT STATES =====
    auto shortcutConflict = std::make_shared<Token>("component.shortcut.conflict", TokenLevel::Component, ValueType::Reference);
    shortcutConflict->SetDefaultValue(TokenValue("semantic.color.warning"));
    shortcutConflict->SetDescription("Shortcut soft-conflict highlight (resolvable by context)");
    RegisterToken(shortcutConflict);

    auto shortcutConflictHard = std::make_shared<Token>("component.shortcut.conflictHard", TokenLevel::Component, ValueType::Reference);
    shortcutConflictHard->SetDefaultValue(TokenValue("semantic.color.danger"));
    shortcutConflictHard->SetDescription("Shortcut hard-conflict highlight (same context)");
    RegisterToken(shortcutConflictHard);

    auto shortcutRecording = std::make_shared<Token>("component.shortcut.recording", TokenLevel::Component, ValueType::Reference);
    shortcutRecording->SetDefaultValue(TokenValue("semantic.color.recording"));
    shortcutRecording->SetDescription("Shortcut capture popup 'recording' indicator");
    RegisterToken(shortcutRecording);

    auto shortcutCaptureBg = std::make_shared<Token>("component.shortcut.captureBackground", TokenLevel::Component, ValueType::Reference);
    shortcutCaptureBg->SetDefaultValue(TokenValue("semantic.color.surface"));
    shortcutCaptureBg->SetDescription("Shortcut capture popup background");
    RegisterToken(shortcutCaptureBg);

    // ===== SECTION HEADER (used in shortcut editor tree) =====
    auto sectionHeaderText = std::make_shared<Token>("component.sectionHeader.text", TokenLevel::Component, ValueType::Reference);
    sectionHeaderText->SetDefaultValue(TokenValue("semantic.color.text"));
    sectionHeaderText->SetDescription("Section header text");
    RegisterToken(sectionHeaderText);

    auto sectionHeaderFontScale = std::make_shared<Token>("component.sectionHeader.fontScale", TokenLevel::Component, ValueType::Float);
    sectionHeaderFontScale->SetDefaultValue(TokenValue(1.1f));
    sectionHeaderFontScale->SetDescription("Section header relative font scale");
    sectionHeaderFontScale->SetConstraint(ValueConstraint::Range(0.5, 2.5, 0.0, "multiplier"));
    RegisterToken(sectionHeaderFontScale);

    // ===== SHORTCUT CAPTURE FIELD (clickable input that records key combos) =====
    auto captureBg = std::make_shared<Token>("component.captureField.background", TokenLevel::Component, ValueType::Reference);
    captureBg->SetDefaultValue(TokenValue("semantic.color.surface"));
    captureBg->SetDescription("Idle capture-field background");
    RegisterToken(captureBg);

    auto captureBgRec = std::make_shared<Token>("component.captureField.backgroundRecording", TokenLevel::Component, ValueType::Reference);
    captureBgRec->SetDefaultValue(TokenValue("semantic.color.surface.elevated"));
    captureBgRec->SetDescription("Recording capture-field background");
    RegisterToken(captureBgRec);

    auto captureBorder = std::make_shared<Token>("component.captureField.border", TokenLevel::Component, ValueType::Reference);
    captureBorder->SetDefaultValue(TokenValue("semantic.color.border.subtle"));
    captureBorder->SetDescription("Idle capture-field border");
    RegisterToken(captureBorder);

    auto captureBorderRec = std::make_shared<Token>("component.captureField.borderRecording", TokenLevel::Component, ValueType::Reference);
    captureBorderRec->SetDefaultValue(TokenValue("semantic.color.recording"));
    captureBorderRec->SetDescription("Recording capture-field border (red)");
    RegisterToken(captureBorderRec);

    auto captureText = std::make_shared<Token>("component.captureField.text", TokenLevel::Component, ValueType::Reference);
    captureText->SetDefaultValue(TokenValue("semantic.color.text"));
    captureText->SetDescription("Capture-field foreground text");
    RegisterToken(captureText);

    auto captureHintText = std::make_shared<Token>("component.captureField.hintText", TokenLevel::Component, ValueType::Reference);
    captureHintText->SetDefaultValue(TokenValue("semantic.color.text.muted"));
    captureHintText->SetDescription("Capture-field hint text (placeholder)");
    RegisterToken(captureHintText);

    auto captureRadius = std::make_shared<Token>("component.captureField.radius", TokenLevel::Component, ValueType::Reference);
    captureRadius->SetDefaultValue(TokenValue("semantic.radius.small"));
    captureRadius->SetDescription("Capture-field corner radius");
    RegisterToken(captureRadius);

    auto capturePadding = std::make_shared<Token>("component.captureField.padding", TokenLevel::Component, ValueType::Vec2);
    capturePadding->SetDefaultValue(TokenValue(ImVec2(8.0f, 4.0f)));
    capturePadding->SetDescription("Capture-field inner padding (x,y)");
    RegisterToken(capturePadding);

    auto captureMinWidth = std::make_shared<Token>("component.captureField.minWidth", TokenLevel::Component, ValueType::Float);
    captureMinWidth->SetDefaultValue(TokenValue(180.0f));
    captureMinWidth->SetDescription("Capture-field minimum width (logical px)");
    captureMinWidth->SetConstraint(ValueConstraint::Range(80.0, 600.0, 0.0, "px"));
    RegisterToken(captureMinWidth);

    auto captureHeight = std::make_shared<Token>("component.captureField.height", TokenLevel::Component, ValueType::Float);
    captureHeight->SetDefaultValue(TokenValue(28.0f));
    captureHeight->SetDescription("Capture-field height (logical px)");
    captureHeight->SetConstraint(ValueConstraint::Range(20.0, 64.0, 0.0, "px"));
    RegisterToken(captureHeight);

    // ── Capture-field extended states (hover / active-press) ────────────
    auto captureBgHover = std::make_shared<Token>("component.captureField.backgroundHover", TokenLevel::Component, ValueType::Reference);
    captureBgHover->SetDefaultValue(TokenValue(std::string("semantic.color.surface.elevated")));
    captureBgHover->SetDescription("Capture-field hover background");
    RegisterToken(captureBgHover);

    auto captureBgActive = std::make_shared<Token>("component.captureField.backgroundActive", TokenLevel::Component, ValueType::Reference);
    captureBgActive->SetDefaultValue(TokenValue(std::string("semantic.color.surface")));
    captureBgActive->SetDescription("Capture-field background while clicked (mouse held down)");
    RegisterToken(captureBgActive);

    // ── Generic toggle (Kbd|Mouse, modifier toggle, Any wildcard) ──────
    auto togBg = std::make_shared<Token>("component.toggle.background", TokenLevel::Component, ValueType::Reference);
    togBg->SetDefaultValue(TokenValue(std::string("semantic.color.surface")));
    togBg->SetDescription("Toggle button background (off state)");
    RegisterToken(togBg);

    auto togHover = std::make_shared<Token>("component.toggle.hover", TokenLevel::Component, ValueType::Reference);
    togHover->SetDefaultValue(TokenValue(std::string("semantic.color.surface.elevated")));
    togHover->SetDescription("Toggle button hover background (off state)");
    RegisterToken(togHover);

    auto togActiveBg = std::make_shared<Token>("component.toggle.activeBackground", TokenLevel::Component, ValueType::Reference);
    togActiveBg->SetDefaultValue(TokenValue(std::string("semantic.color.primary")));
    togActiveBg->SetDescription("Toggle button background when toggled on");
    RegisterToken(togActiveBg);

    auto togBorder = std::make_shared<Token>("component.toggle.border", TokenLevel::Component, ValueType::Reference);
    togBorder->SetDefaultValue(TokenValue(std::string("semantic.color.border.subtle")));
    togBorder->SetDescription("Toggle button border (off state)");
    RegisterToken(togBorder);

    auto togActiveBd = std::make_shared<Token>("component.toggle.activeBorder", TokenLevel::Component, ValueType::Reference);
    togActiveBd->SetDefaultValue(TokenValue(std::string("semantic.color.primary")));
    togActiveBd->SetDescription("Toggle button border when toggled on");
    RegisterToken(togActiveBd);

    auto togText = std::make_shared<Token>("component.toggle.text", TokenLevel::Component, ValueType::Reference);
    togText->SetDefaultValue(TokenValue(std::string("semantic.color.text.muted")));
    togText->SetDescription("Toggle button text (off state)");
    RegisterToken(togText);

    auto togActiveTx = std::make_shared<Token>("component.toggle.activeText", TokenLevel::Component, ValueType::Reference);
    togActiveTx->SetDefaultValue(TokenValue(std::string("semantic.color.text")));
    togActiveTx->SetDescription("Toggle button text when toggled on");
    RegisterToken(togActiveTx);

    auto togRadius = std::make_shared<Token>("component.toggle.radius", TokenLevel::Component, ValueType::Float);
    togRadius->SetDefaultValue(TokenValue(5.0f));
    togRadius->SetDescription("Toggle button corner radius (logical px)");
    togRadius->SetConstraint(ValueConstraint::Range(0.0, 16.0, 0.0, "px"));
    RegisterToken(togRadius);

    auto togBorderSize = std::make_shared<Token>("component.toggle.borderSize", TokenLevel::Component, ValueType::Float);
    togBorderSize->SetDefaultValue(TokenValue(1.0f));
    togBorderSize->SetDescription("Toggle / button-group border thickness (px)");
    togBorderSize->SetConstraint(ValueConstraint::Range(0.0, 4.0, 0.0, "px"));
    RegisterToken(togBorderSize);

    // ── Square icon button (used for Restore/Delete) ───────────────────
    auto ibBg = std::make_shared<Token>("component.iconButton.background", TokenLevel::Component, ValueType::Reference);
    ibBg->SetDefaultValue(TokenValue(std::string("semantic.color.surface")));
    ibBg->SetDescription("Icon button background");
    RegisterToken(ibBg);

    auto ibHover = std::make_shared<Token>("component.iconButton.hover", TokenLevel::Component, ValueType::Reference);
    ibHover->SetDefaultValue(TokenValue(std::string("semantic.color.surface.elevated")));
    ibHover->SetDescription("Icon button hover background");
    RegisterToken(ibHover);

    auto ibActive = std::make_shared<Token>("component.iconButton.active", TokenLevel::Component, ValueType::Reference);
    ibActive->SetDefaultValue(TokenValue(std::string("semantic.color.background")));
    ibActive->SetDescription("Icon button background while clicked");
    RegisterToken(ibActive);

    auto ibBorder = std::make_shared<Token>("component.iconButton.border", TokenLevel::Component, ValueType::Reference);
    ibBorder->SetDefaultValue(TokenValue(std::string("semantic.color.border.subtle")));
    ibBorder->SetDescription("Icon button border");
    RegisterToken(ibBorder);

    auto ibIcon = std::make_shared<Token>("component.iconButton.iconColor", TokenLevel::Component, ValueType::Reference);
    ibIcon->SetDefaultValue(TokenValue(std::string("semantic.color.text")));
    ibIcon->SetDescription("Default icon tint for icon button");
    RegisterToken(ibIcon);

    auto ibDanger = std::make_shared<Token>("component.iconButton.dangerIcon", TokenLevel::Component, ValueType::Reference);
    ibDanger->SetDefaultValue(TokenValue(std::string("semantic.color.danger")));
    ibDanger->SetDescription("Danger-tinted icon for destructive icon button (delete)");
    RegisterToken(ibDanger);

    auto ibRadius = std::make_shared<Token>("component.iconButton.radius", TokenLevel::Component, ValueType::Float);
    ibRadius->SetDefaultValue(TokenValue(3.0f));
    ibRadius->SetDescription("Icon button corner radius (logical px)");
    ibRadius->SetConstraint(ValueConstraint::Range(0.0, 16.0, 0.0, "px"));
    RegisterToken(ibRadius);
}

} // namespace DesignSystem
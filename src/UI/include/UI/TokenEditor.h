#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <string>
#include <vector>
#include <memory>
#include <imgui.h>

namespace DesignSystem {

class Token;
class OverrideManager;

class TokenEditor {
public:
    TokenEditor();

    // Fenêtre autonome (conservée pour usage standalone éventuel).
    void Render(Context& currentContext, OverrideManager& overrideManager,
                bool* p_open = nullptr);

    // Contenu seul, sans Begin/End — utilisé à l'intérieur de la fenêtre Paramètres.
    void RenderContent(Context& currentContext, OverrideManager& overrideManager);

private:
    void RenderContextSelector(Context& currentContext);
    void RenderTokenList();
    void RenderTokenDetails(Context& currentContext, OverrideManager& overrideManager);
    void RenderOverridePanel(Context& currentContext, OverrideManager& overrideManager);

    void InitializeNewOverrideValue(std::shared_ptr<Token> token);
    void RenderActualValue(std::shared_ptr<Token> token, const Context& currentContext);
    void RenderResolvedPreview(const std::string& refTokenId, const Context& currentContext);
    void RenderValuePreview(const char* label, const TokenValue& value,
                            const Context& currentContext, bool showLabel);
    void RenderColorPreview(const char* label, const ImVec4& color,
                            const Context& currentContext);
    void RenderFloatPreview(const char* label, float value);
    bool RenderValueEditor(const char* label, TokenValue& value,
                           std::shared_ptr<Token> token, const Context& currentContext);
    bool ValidateOverrideType(const TokenValue& value, std::shared_ptr<Token> token);
    bool IsTokenFiltered(std::shared_ptr<Token> token) const;

    std::string selectedTokenId_;
    int selectedThemeIndex_;
    int selectedAccessibilityIndex_;
    bool showPrimitives_;
    bool showSemantics_;
    bool showComponents_;
    char searchBuffer_[256];

    TokenValue newOverrideValue_;
    bool addingGlobalOverride_;
};

} // namespace DesignSystem
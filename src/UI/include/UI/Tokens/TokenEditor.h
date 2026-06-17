#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <UI/Tokens/TokenTreeView.h>
#include <UI/Tokens/TokensViewer.h>
#include <UI/Tokens/UserThemeEditor.h>
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

    // Contenu seul, sans Begin/End — utilisé à l'intérieur de la fenêtre
    // Paramètres. Affiche trois onglets : l'éditeur historique (liste), la
    // vue arbre développeur, et l'éditeur par zones côté utilisateur.
    void RenderContent(Context& currentContext, OverrideManager& overrideManager);

    // L'éditeur historique seul (liste + détails + overrides). Conservé tel
    // quel : c'est l'onglet "Design System".
    void RenderClassicEditor(Context& currentContext,
                             OverrideManager& overrideManager);

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

    // The additional tabs (dev tree + user-facing zone editor + viewer).
    TokenTreeView   treeView_;
    UserThemeEditor userThemeEditor_;
    TokensViewer    tokensViewer_;
};

} // namespace DesignSystem
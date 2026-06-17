#pragma once

#include <UI/Tokens/TokenInspector.h>
#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenType.h>
#include <string>

namespace DesignSystem {

class OverrideManager;

/**
 * "Tokens viewer" Settings tab — three sub-tabs (Primitive / Semantic /
 * Component) showing the entire token tree as a table:
 *
 *     [ Name | Value | Resolution chain | Impact ]
 *
 * • Name             — the Spectrum-2 string id (from TokName()).
 * • Value            — live swatch / numeric / vec2 (resolved through the
 *                      active context). For reference tokens, the resolved
 *                      *final* value (after the chain).
 * • Resolution chain — for Semantic/Component, the chain of references
 *                      followed back to the leaf primitive
 *                      (`component.* → semantic.* → primitive.*`).
 * • Impact           — number of distinct final COMPONENTS that ultimately
 *                      use this token (read from DesignSystem::GetUsage(),
 *                      populated each frame by ResolveScoped tracking the
 *                      whole reference chain). Click → popup with the
 *                      detailed `Component → occurrence count` table.
 *
 * Each row (except primitives, which are read-only) is expandable to reveal
 * a full per-token override editor (the shared TokenInspector property
 * table — global + per-theme override at "" scope). Primitives have no
 * override editor by design.
 */
class TokensViewer {
public:
    TokensViewer() = default;

    // Top-level render for the Settings tab.
    void Render(Context& ctx, OverrideManager& mgr);

private:
    // Render one tier's table (called once per sub-tab).
    void RenderTier(TokenLevel level, Context& ctx, OverrideManager& mgr);

    // Build the human-readable reference chain string for one token.
    // Returns "" for tokens that aren't a reference (leaf primitive values).
    std::string BuildResolutionChain(const std::string& tokenId,
                                     ThemeType theme) const;

    // Count distinct components that referenced `tokenId` this frame.
    int GetImpact(const std::string& tokenId) const;

    // Per-row state: which token row is expanded (one at a time per tier).
    std::string expanded_[3];   // index = (int)TokenLevel

    // Search filter per tier.
    char search_[3][96] = {};

    // Token whose Impact popup is currently open (empty = no popup).
    std::string popupToken_;

    // The shared inspector used to render the inline override editor.
    TokenInspector inspector_;
};

} // namespace DesignSystem

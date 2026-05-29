#pragma once

#include <DesignSystem/Core/Context.h>
#include <UI/Tokens/TokenInspector.h>
#include <string>

namespace DesignSystem {

class OverrideManager;

/**
 * Developer-facing tab: the full token graph as a *tree* (instead of the
 * flat list of the classic editor). Nodes are derived from the dotted token
 * id (e.g. component ▸ color ▸ frameBg). Three roots — Primitive / Semantic /
 * Component — mirror the hierarchy. Selecting a leaf shows the shared
 * inspector with every detail + override feature.
 *
 * This is a power tool: it exposes every token by its real id, for the
 * developer who needs to find/inspect/override anything precisely.
 */
class TokenTreeView {
public:
    TokenTreeView();
    void Render(Context& ctx, OverrideManager& mgr);

private:
    void RenderTreeForLevel(const char* rootLabel, TokenLevel level);

    TokenInspector inspector_;
    std::string    selectedTokenId_;
    char           searchBuffer_[256];
};

} // namespace DesignSystem

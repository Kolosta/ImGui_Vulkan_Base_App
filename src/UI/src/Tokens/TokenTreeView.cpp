#include <UI/Tokens/TokenTreeView.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/ScrollArea.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace DesignSystem {

namespace {

// A node of the id-derived tree. Leaves carry the full token id.
struct TreeNode {
    std::map<std::string, TreeNode> children;   // ordered → stable display
    std::string                     tokenId;    // non-empty only on leaves
    bool                            isLeaf = false;
};

// Split "component.color.frameBg" into ["component","color","frameBg"] and
// insert it, dropping the leading level segment (already the tree root).
void Insert(TreeNode& root, const std::string& id) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : id) {
        if (ch == '.') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.size() <= 1) return;

    TreeNode* node = &root;
    for (size_t i = 1; i < parts.size(); ++i) {       // skip parts[0] = level
        node = &node->children[parts[i]];
    }
    node->isLeaf  = true;
    node->tokenId = id;
}

bool MatchesFilter(const std::string& id, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    return id.find(filter) != std::string::npos;
}

} // namespace

TokenTreeView::TokenTreeView() {
    searchBuffer_[0] = '\0';
}

void TokenTreeView::RenderTreeForLevel(const char* rootLabel, TokenLevel level) {
    auto& registry = TokenRegistry::Instance();
    auto tokens = registry.GetTokensByLevel(level);

    // Sort by id so the derived tree is deterministic.
    std::sort(tokens.begin(), tokens.end(),
              [](const std::shared_ptr<Token>& a, const std::shared_ptr<Token>& b) {
                  return a->GetId() < b->GetId();
              });

    TreeNode root;
    for (const auto& t : tokens)
        if (MatchesFilter(t->GetId(), searchBuffer_))
            Insert(root, t->GetId());

    if (root.children.empty()) return;

    // When filtering, open everything so matches are visible immediately.
    if (!UI::IconTreeNode(rootLabel, rootLabel, /*defaultOpen=*/true)) return;

    // Recursive lambda over the derived tree.
    std::function<void(const std::string&, const TreeNode&)> walk =
        [&](const std::string& name, const TreeNode& node) {
            if (node.isLeaf && node.children.empty()) {
                bool selected = (selectedTokenId_ == node.tokenId);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    selectedTokenId_ = node.tokenId;
                    inspector_.SyncToToken(
                        TokenRegistry::Instance().GetToken(node.tokenId));
                }
                return;
            }
            bool nOpen = (searchBuffer_[0] != '\0');
            if (UI::IconTreeNode(name.c_str(), name.c_str(), nOpen)) {
                for (const auto& [childName, child] : node.children)
                    walk(childName, child);
                // A node can be both a branch and a leaf (rare). Offer it too.
                if (node.isLeaf) {
                    bool selected = (selectedTokenId_ == node.tokenId);
                    if (ImGui::Selectable((name + " (self)").c_str(), selected)) {
                        selectedTokenId_ = node.tokenId;
                        inspector_.SyncToToken(
                            TokenRegistry::Instance().GetToken(node.tokenId));
                    }
                }
                ImGui::TreePop();
            }
        };

    for (const auto& [childName, child] : root.children)
        walk(childName, child);

    ImGui::TreePop();
}

void TokenTreeView::Render(Context& ctx, OverrideManager& mgr) {
    ImGui::TextWrapped(
        "Developer view — the full token graph as a tree. Every token is "
        "shown by its real id; select one to inspect or override it.");
    ImGui::InputTextWithHint("##treeSearch", "Filter by id...",
                             searchBuffer_, sizeof(searchBuffer_));
    ImGui::Separator();

    UI::BeginScroll("TreePane", ImVec2(340.0f, 0.0f), ImGuiChildFlags_Borders);
    RenderTreeForLevel("Primitive Tokens", TokenLevel::Primitive);
    RenderTreeForLevel("Semantic Tokens",  TokenLevel::Semantic);
    RenderTreeForLevel("Component Tokens", TokenLevel::Component);
    UI::EndScroll();

    ImGui::SameLine();

    UI::BeginScroll("TreeDetailsPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (!selectedTokenId_.empty()) {
        inspector_.RenderDetails(selectedTokenId_, ctx, mgr);
        ImGui::Separator();
        inspector_.RenderOverridePanel(selectedTokenId_, ctx, mgr);

        // ── Scopes branch ──────────────────────────────────────────────────
        // Every token can be overridden not only globally but at any
        // registered scope (zone → sub-zone → component → element …). One
        // editable row per scope; the per-row Theme/Global selector lives in
        // RenderScopedRow itself.
        ImGui::Separator();
        if (UI::IconTreeNode("scopesBranch",
                "Scopes (per-zone / per-component overrides)",
                /*defaultOpen=*/true)) {
            ImGui::TextDisabled(
                "Edit this token at a specific scope. A scoped value cascades "
                "to its parent scope, then to the global token.");

            if (inspector_.BeginPropertyTable("##scopeTbl")) {
                // Global level first (scope = "").
                inspector_.RenderScopedRow(selectedTokenId_, "Global (no scope)",
                                           "", ctx, mgr);
                auto& ds = DesignSystem::DesignSystem::Instance();
                for (const auto& s : ds.GetScopes()) {
                    std::string indent((s.depth) * 2, ' ');
                    std::string label = indent + s.label + "  [" + s.path + "]";
                    inspector_.RenderScopedRow(selectedTokenId_, label.c_str(),
                                               s.path, ctx, mgr);
                }
                inspector_.EndPropertyTable();
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::TextWrapped("Select a token in the tree to see its details.");
    }
    UI::EndScroll();
}

} // namespace DesignSystem

#include <UI/Tokens/TokensViewer.h>
#include <UI/Widgets/IconWidgets.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace DesignSystem {

namespace {

const char* TierLabel(TokenLevel l) {
    switch (l) {
        case TokenLevel::Primitive: return "Primitive tokens";
        case TokenLevel::Semantic:  return "Semantic tokens";
        case TokenLevel::Component: return "Component tokens";
    }
    return "?";
}

bool MatchesFilter(const std::string& id, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    return id.find(filter) != std::string::npos;
}

// Sort tokens by their string id so the table is stable across frames.
std::vector<std::shared_ptr<Token>> SortedTokensOfLevel(TokenLevel l) {
    auto tokens = TokenRegistry::Instance().GetTokensByLevel(l);
    std::sort(tokens.begin(), tokens.end(),
              [](const std::shared_ptr<Token>& a, const std::shared_ptr<Token>& b) {
                  return a->GetId() < b->GetId();
              });
    return tokens;
}

// Reverse reference lookup: for each OTHER token X, walk its default
// reference chain; if X's chain reaches `target` then X is a referrer. The
// first hop landing on `target` classifies X as a direct referrer; any
// later hop classifies it as a chain (indirect) referrer. Bounded by the
// 16-hop cycle safeguard used everywhere else.
struct ReferrerSets {
    std::vector<std::string> direct;
    std::vector<std::string> chain;
};

ReferrerSets FindReferrers(const std::string& target) {
    ReferrerSets out;
    auto& reg = TokenRegistry::Instance();
    for (const auto& t : reg.GetAllTokens()) {
        if (t->GetId() == target) continue;
        std::string cur = t->GetId();
        for (int hop = 0; hop < 16; ++hop) {
            auto cur_t = reg.GetToken(cur);
            if (!cur_t) break;
            const TokenValue& dv = cur_t->GetDefaultValue();
            if (!dv.IsReference()) break;
            const std::string& ref = dv.AsReference();
            if (ref == target) {
                if (hop == 0) out.direct.push_back(t->GetId());
                else          out.chain.push_back(t->GetId());
                break;
            }
            cur = ref;
        }
    }
    std::sort(out.direct.begin(), out.direct.end());
    std::sort(out.chain.begin(),  out.chain.end());
    return out;
}

} // namespace

// Walk the token's default reference chain ("component.X → semantic.Y →
// primitive.Z"). Bounded against cycles by the same 16-hop cap the override
// resolver uses. Returns "" if the token has no reference (= leaf value).
std::string TokensViewer::BuildResolutionChain(const std::string& tokenId,
                                               ThemeType /*theme*/) const {
    auto& reg = TokenRegistry::Instance();
    std::shared_ptr<Token> tok = reg.GetToken(tokenId);
    if (!tok) return {};
    if (!tok->GetDefaultValue().IsReference()) return {};

    std::string out;
    std::string cur = tokenId;
    for (int hop = 0; hop < 16; ++hop) {
        auto t = reg.GetToken(cur);
        if (!t) break;
        if (!out.empty()) out += "  →  ";
        out += cur;
        const TokenValue& dv = t->GetDefaultValue();
        if (!dv.IsReference()) break;
        cur = dv.AsReference();
    }
    return out;
}

int TokensViewer::GetImpact(const std::string& tokenId) const {
    const auto& usage = DesignSystem::DesignSystem::Instance().GetUsage();
    auto it = usage.find(tokenId);
    return it == usage.end() ? 0 : static_cast<int>(it->second.size());
}

void TokensViewer::RenderTier(TokenLevel level, Context& ctx,
                              OverrideManager& mgr) {
    const int tierIdx = static_cast<int>(level);

    ImGui::InputTextWithHint("##search", "Filter by id...",
                             search_[tierIdx], sizeof(search_[tierIdx]));
    ImGui::Separator();

    auto tokens = SortedTokensOfLevel(level);

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_RowBg         | ImGuiTableFlags_PadOuterX        |
        ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("##tv", 4, kFlags, ImVec2(0, 0))) return;
    ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch, 0.40f);
    ImGui::TableSetupColumn("Value",    ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("Chain",    ImGuiTableColumnFlags_WidthStretch, 0.30f);
    ImGui::TableSetupColumn("Impact",   ImGuiTableColumnFlags_WidthStretch, 0.10f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    const auto& usage = DesignSystem::DesignSystem::Instance().GetUsage();
    std::string popupTokenId;   // request a popup for this token (deferred)

    for (const auto& tok : tokens) {
        const std::string& id = tok->GetId();
        if (!MatchesFilter(id, search_[tierIdx])) continue;

        ImGui::TableNextRow();
        ImGui::PushID(id.c_str());

        // ── Col 0: Name + expander chevron ─────────────────────────────────
        ImGui::TableSetColumnIndex(0);
        bool isExpanded = (expanded_[tierIdx] == id);
        // A simple toggle chevron (no recursion → no TreeNode/pop pitfalls).
        if (ImGui::ArrowButton("##exp", isExpanded
                ? ImGuiDir_Down : ImGuiDir_Right)) {
            expanded_[tierIdx] = isExpanded ? std::string() : id;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(id.c_str());

        // ── Col 1: live value preview ──────────────────────────────────────
        ImGui::TableSetColumnIndex(1);
        try {
            TokenValue v = DesignSystem::DesignSystem::Instance()
                              .ResolveScoped(id, "", ctx.GetTheme());
            inspector_.RenderValuePreview("##val", v, ctx, /*showLabel=*/true);
        } catch (...) {
            ImGui::TextDisabled("—");
        }

        // ── Col 2: resolution chain (only meaningful for refs) ─────────────
        ImGui::TableSetColumnIndex(2);
        std::string chain = BuildResolutionChain(id, ctx.GetTheme());
        if (chain.empty()) ImGui::TextDisabled("(leaf)");
        else                ImGui::TextUnformatted(chain.c_str());

        // ── Col 3: impact count + clickable popup trigger ──────────────────
        ImGui::TableSetColumnIndex(3);
        int impact = GetImpact(id);
        if (impact == 0) {
            ImGui::TextDisabled("0");
        } else {
            char label[32];
            std::snprintf(label, sizeof(label), "%d component%s",
                          impact, impact > 1 ? "s" : "");
            if (ImGui::SmallButton(label)) popupTokenId = id;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to see which components use this token");
        }

        // ── Expanded row: inline override editor (semantic/component only) ─
        if (isExpanded) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Indent(20.0f);
            if (level == TokenLevel::Primitive) {
                ImGui::TextDisabled(
                    "Primitive tokens are raw values — they cannot be "
                    "overridden. Edit a semantic or component token that "
                    "references this one to change downstream usage.");
            } else {
                // Inline override editor. RenderScopedRow edits the token at
                // "" (global) — the dev TokenTree tab already exposes the
                // full per-scope matrix.
                if (inspector_.BeginPropertyTable("##ovr")) {
                    inspector_.RenderScopedRow(id, "Override",
                                               /*scope=*/"", ctx, mgr);
                    inspector_.EndPropertyTable();
                }
            }
            ImGui::Unindent(20.0f);
        }

        ImGui::PopID();
    }
    ImGui::EndTable();

    // ── Deferred popup (must be opened OUTSIDE the table) ────────────────
    if (!popupTokenId.empty()) {
        ImGui::OpenPopup("##impactPopup");
        popupToken_ = popupTokenId;
    }
    if (ImGui::BeginPopup("##impactPopup")) {
        ImGui::TextUnformatted(popupToken_.c_str());
        ImGui::Separator();

        // ── 1. Components that consumed this token last frame ──────────
        ImGui::TextDisabled("Components that resolved through this token "
                            "during the last frame:");
        ImGui::Spacing();
        auto it = usage.find(popupToken_);
        if (it == usage.end() || it->second.empty()) {
            ImGui::TextDisabled("(no recorded usage)");
        } else {
            std::vector<std::pair<std::string, int>> rows(
                it->second.begin(), it->second.end());
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
            if (ImGui::BeginTable("##compTbl", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Component",
                                        ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("Uses / frame",
                                        ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableHeadersRow();
                for (const auto& [comp, n] : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(comp.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", n);
                }
                ImGui::EndTable();
            }
        }

        // ── 2. Tokens that reference this one (direct + via chain) ─────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Tokens that reference this one:");
        ImGui::Spacing();
        ReferrerSets refs = FindReferrers(popupToken_);
        if (refs.direct.empty() && refs.chain.empty()) {
            ImGui::TextDisabled("(no token references this one)");
        } else if (ImGui::BeginTable("##refTbl", 2,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Token",
                                    ImGuiTableColumnFlags_WidthStretch, 0.75f);
            ImGui::TableSetupColumn("Kind",
                                    ImGuiTableColumnFlags_WidthStretch, 0.25f);
            ImGui::TableHeadersRow();
            for (const auto& id : refs.direct) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(id.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f), "direct");
            }
            for (const auto& id : refs.chain) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(id.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "chain");
            }
            ImGui::EndTable();
        }

        ImGui::EndPopup();
    }
}

void TokensViewer::Render(Context& ctx, OverrideManager& mgr) {
    ImGui::TextWrapped(
        "Tokens viewer — the complete primitive / semantic / component "
        "graph. Each row shows the live value, the resolution chain back to "
        "a primitive, and the IMPACT: how many distinct UI components "
        "ultimately resolve through this token (last frame). Click the "
        "impact pill for a per-component breakdown. Expand a semantic or "
        "component row to override it inline; primitives are read-only.");
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##tvTiers")) {
        const TokenLevel tiers[3] = {
            TokenLevel::Primitive,
            TokenLevel::Semantic,
            TokenLevel::Component,
        };
        for (TokenLevel l : tiers) {
            if (ImGui::BeginTabItem(TierLabel(l))) {
                RenderTier(l, ctx, mgr);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

} // namespace DesignSystem

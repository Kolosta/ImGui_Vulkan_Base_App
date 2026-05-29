#include <UI/Tokens/TokenInspector.h>
#include <UI/Text/FontManager.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <DesignSystem/DesignSystem.h>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DesignSystem {

namespace {
// Resolve a token in the current context without ever throwing — used to
// seed an override editor with the live value as a starting point.
TokenValue SafeResolve(const std::string& tokenId, const Context& ctx) {
    try {
        return DesignSystem::Instance().ResolveTokenValue(tokenId,
                                                          ctx.GetTheme());
    } catch (...) {
        return TokenValue(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
}

// Named font weights for the discrete weight dropdown.
struct WeightChoice { const char* name; int value; };
constexpr WeightChoice kWeightChoices[] = {
    {"Thin (100)",100},{"Extra-Light (200)",200},{"Light (300)",300},
    {"Regular (400)",400},{"Medium (500)",500},{"Semi-Bold (600)",600},
    {"Bold (700)",700},{"Extra-Bold (800)",800},{"Black (900)",900},
    {"Extra-Black (950)",950},
};

// Dropdown of all discovered font families. Returns true on change.
bool FontFamilyCombo(const char* label, std::string& family) {
    bool changed = false;
    if (ImGui::BeginCombo(label, family.empty() ? "(none)" : family.c_str())) {
        for (const std::string& name : UI::FontManager::Instance().FamilyNames()) {
            bool sel = (name == family);
            if (ImGui::Selectable(name.c_str(), sel)) { family = name; changed = true; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Is this token id a font-weight token? (Drives weight dropdown vs plain int.)
bool IsWeightToken(const std::string& id) {
    return id.find("font.weight") != std::string::npos ||
           id.find("font-weight") != std::string::npos;
}
} // namespace

TokenInspector::TokenInspector()
    : newOverrideValue_(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) {}

void TokenInspector::SyncToToken(const std::shared_ptr<Token>& token) {
    if (!token) return;
    auto& ds = DesignSystem::Instance();
    try {
        newOverrideValue_ = ds.ResolveTokenValue(token->GetId(),
                                ds.GetCurrentContext().GetTheme());
        return;
    } catch (...) {}
    switch (token->GetValueType()) {
        case ValueType::Color:     newOverrideValue_ = TokenValue(ImVec4(1, 1, 1, 1)); break;
        case ValueType::Float:     newOverrideValue_ = TokenValue(0.0f); break;
        case ValueType::Int:       newOverrideValue_ = TokenValue(0); break;
        case ValueType::Vec2:      newOverrideValue_ = TokenValue(ImVec2(0, 0)); break;
        case ValueType::Reference: newOverrideValue_ = TokenValue(std::string("")); break;
        case ValueType::Ratio:     newOverrideValue_ = TokenValue::MakeRatio(0.5f); break;
        case ValueType::Bezier:    newOverrideValue_ = TokenValue::MakeBezier(ImVec4(0.25f,0.1f,0.25f,1.0f)); break;
        case ValueType::FontFamily:newOverrideValue_ = TokenValue::MakeFontFamily("NotoSans"); break;
        case ValueType::TextStyle: break;  // seeded from the resolved value above
    }
}

// ── Value preview ─────────────────────────────────────────────────────────────

void TokenInspector::RenderValuePreview(const char* label, const TokenValue& value,
                                        const Context& ctx, bool showLabel) {
    switch (value.GetType()) {
        case ValueType::Color: {
            ImVec4 color = value.AsColor();
            RenderColorPreview(label, color, ctx);
            if (showLabel) {
                ImGui::SameLine();
                ImGui::Text("RGBA(%d, %d, %d, %d)",
                    (int)(color.x * 255), (int)(color.y * 255),
                    (int)(color.z * 255), (int)(color.w * 255));
            }
            break;
        }
        case ValueType::Float: {
            float f = value.AsFloat();
            RenderFloatPreview(label, std::string(label), f);
            if (showLabel) { ImGui::SameLine(); ImGui::Text("%.2f", f); }
            break;
        }
        case ValueType::Int:
            if (showLabel) ImGui::Text("%d", value.AsInt());
            break;
        case ValueType::Vec2: {
            ImVec2 v = value.AsVec2();
            if (showLabel) ImGui::Text("(%.1f, %.1f)", v.x, v.y);
            break;
        }
        case ValueType::Reference:
            if (showLabel) ImGui::Text("Ref: %s", value.AsReference().c_str());
            break;
        case ValueType::FontFamily:
            if (showLabel) ImGui::Text("Font: %s", value.AsFontFamily().c_str());
            break;
        case ValueType::Ratio:
            if (showLabel) ImGui::Text("%.0f%%", value.AsRatio() * 100.0f);
            break;
        case ValueType::Bezier: {
            ImVec4 b = value.AsBezier();
            if (showLabel) ImGui::Text("bezier(%.2f, %.2f, %.2f, %.2f)", b.x, b.y, b.z, b.w);
            break;
        }
        case ValueType::TextStyle: {
            TextStyleRefs t = value.AsTextStyle();
            if (showLabel) ImGui::Text("%s · %s", t.size.c_str(), t.weight.c_str());
            break;
        }
    }
}

void TokenInspector::RenderColorPreview(const char* label, const ImVec4& color,
                                        const Context& ctx) {
    (void)label;
    bool hasAcc = (ctx.GetAccessibility() != AccessibilityType::None);
    ImVec4 transformed = color;
    if (hasAcc)
        transformed = ColorBlindness::ApplyColorBlindness(color, ctx.GetAccessibility());

    auto& ds = DesignSystem::Instance();
    float globalAlpha = 1.0f;
    try { globalAlpha = ds.GetFloat(Tok::S_Opacity_Default); } catch (...) {}

    // Use ImGui's own swatch with AlphaPreviewHalf so the preview matches the
    // editors. globalAlpha is folded into the displayed colour.
    constexpr ImGuiColorEditFlags kPrev =
        ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_NoTooltip;
    ImVec2 size(50.0f, ImGui::GetFrameHeight());
    if (hasAcc) {
        ImVec4 c1 = color;       c1.w *= globalAlpha;
        ImVec4 c2 = transformed; c2.w *= globalAlpha;
        ImGui::ColorButton("##orig", c1, kPrev, ImVec2(size.x * 0.5f, size.y));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::ColorButton("##corr", c2, kPrev, ImVec2(size.x * 0.5f, size.y));
    } else {
        ImVec4 c = color; c.w *= globalAlpha;
        ImGui::ColorButton("##sw", c, kPrev, size);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("RGB: %d, %d, %d",
            (int)(color.x*255), (int)(color.y*255), (int)(color.z*255));
        ImGui::Text("Hex: #%02X%02X%02X",
            (int)(color.x*255), (int)(color.y*255), (int)(color.z*255));
        ImGui::Text("Alpha: %.2f (Global: %.2f)", color.w, globalAlpha);
        if (hasAcc) {
            ImGui::Separator();
            ImGui::Text("Accessibility-corrected: %d, %d, %d",
                (int)(transformed.x*255), (int)(transformed.y*255),
                (int)(transformed.z*255));
        }
        ImGui::EndTooltip();
    }
}

void TokenInspector::RenderFloatPreview(const char* label, const std::string& tokenId,
                                        float value) {
    (void)label;
    if (tokenId.find("radius") != std::string::npos ||
        tokenId.find("Rounding") != std::string::npos) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(40.0f, 40.0f);
        float r = std::min(std::max(0.0f, value), std::min(size.x, size.y) * 0.5f);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(255, 0, 255, 50), r);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    IM_COL32(255, 0, 255, 200), r);
        ImGui::Dummy(size);
    } else {
        ImGui::Text("%.2f", value);
    }
}

// ── Value editor (constraint-driven) ──────────────────────────────────────────

bool TokenInspector::RenderValueEditor(const char* label, TokenValue& value,
                                       const std::shared_ptr<Token>& token,
                                       const Context& ctx) {
    auto& ds = DesignSystem::Instance();
    ValueType expected;
    try {
        expected = ds.ResolveTokenValue(token->GetId(), ctx.GetTheme()).GetType();
    } catch (...) {
        expected = token->GetValueType();
    }
    if (value.GetType() != expected && !value.IsReference()) {
        switch (expected) {
            case ValueType::Color: value = TokenValue(ImVec4(1, 1, 1, 1)); break;
            case ValueType::Float: value = TokenValue(0.0f); break;
            case ValueType::Int:   value = TokenValue(0); break;
            case ValueType::Vec2:  value = TokenValue(ImVec2(0, 0)); break;
            default: break;
        }
    }

    bool changed = false;
    // Use the *effective* constraint: a Reference token (e.g. disabledAlpha)
    // declares no range itself but inherits one from what it resolves to, so
    // the slider must be bounded by that — not left unconstrained.
    ValueConstraint c = ds.GetEffectiveConstraint(token->GetId());

    switch (value.GetType()) {
        case ValueType::Color: {
            ImVec4 col = value.AsColor();
            // A full-width swatch (not a fixed square) that opens a colour
            // picker popup on click. AlphaPreviewHalf shows the swatch split
            // opaque / with-alpha; the picker carries the alpha bar too.
            float w = ImGui::CalcItemWidth();
            if (w <= 0.0f) w = ImGui::GetContentRegionAvail().x;
            float h = ImGui::GetFrameHeight();
            constexpr ImGuiColorEditFlags kPrev =
                ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_NoTooltip;
            if (ImGui::ColorButton(label, col, kPrev, ImVec2(w, h)))
                ImGui::OpenPopup("##cpick");
            if (ImGui::BeginPopup("##cpick")) {
                if (ImGui::ColorPicker4("##cp", &col.x,
                        ImGuiColorEditFlags_AlphaBar |
                        ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    value.SetColor(col); changed = true;
                }
                ImGui::EndPopup();
            }
            break;
        }
        case ValueType::Float: {
            float f = value.AsFloat();
            if (!c.allowedValues.empty()) {
                int sel = 0;
                for (size_t i = 0; i < c.allowedValues.size(); ++i)
                    if ((float)c.allowedValues[i] == f) { sel = (int)i; break; }
                std::vector<std::string> items;
                for (double v : c.allowedValues) {
                    char b[32]; snprintf(b, sizeof(b), "%.3g", v); items.emplace_back(b);
                }
                std::vector<const char*> cs;
                for (auto& s : items) cs.push_back(s.c_str());
                if (ImGui::Combo(label, &sel, cs.data(), (int)cs.size())) {
                    value.SetFloat((float)c.allowedValues[sel]); changed = true;
                }
            } else {
                auto lo = c.Min(); auto hi = c.Max();
                if (lo && hi) {
                    if (ImGui::SliderFloat(label, &f, (float)*lo, (float)*hi)) {
                        value.SetFloat((float)c.Clamp(f)); changed = true;
                    }
                } else {
                    float dmin = lo ? (float)*lo : -FLT_MAX;
                    float dmax = hi ? (float)*hi :  FLT_MAX;
                    if (ImGui::DragFloat(label, &f, 0.1f, dmin, dmax)) {
                        value.SetFloat((float)c.Clamp(f)); changed = true;
                    }
                }
            }
            break;
        }
        case ValueType::Int: {
            int iv = value.AsInt();
            // Font-weight tokens: a discrete dropdown of named weights for a
            // static family; a continuous slider only makes sense for a
            // variable family, which is handled per-font in the font editor.
            if (IsWeightToken(token->GetId())) {
                int sel = 0;
                for (int i = 0; i < (int)IM_ARRAYSIZE(kWeightChoices); ++i)
                    if (kWeightChoices[i].value == iv) { sel = i; break; }
                std::vector<const char*> names;
                for (const auto& w : kWeightChoices) names.push_back(w.name);
                if (ImGui::Combo(label, &sel, names.data(), (int)names.size())) {
                    value.SetInt(kWeightChoices[sel].value); changed = true;
                }
                break;
            }
            auto lo = c.Min(); auto hi = c.Max();
            int dmin = lo ? (int)*lo : 0;
            int dmax = hi ? (int)*hi : 1000;
            if (lo && hi) {
                if (ImGui::SliderInt(label, &iv, dmin, dmax)) {
                    value.SetInt((int)c.Clamp((double)iv)); changed = true;
                }
            } else {
                if (ImGui::DragInt(label, &iv, 1, dmin, dmax)) {
                    value.SetInt((int)c.Clamp((double)iv)); changed = true;
                }
            }
            break;
        }
        case ValueType::Vec2: {
            ImVec2 v = value.AsVec2();
            if (ImGui::DragFloat2(label, &v.x, 0.1f)) { value.SetVec2(v); changed = true; }
            break;
        }
        case ValueType::Reference: {
            std::string ref = value.AsReference();
            char buf[256];
            strncpy(buf, ref.c_str(), 255);
            buf[255] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf))) {
                value.SetReference(std::string(buf)); changed = true;
            }
            break;
        }
        case ValueType::FontFamily: {
            std::string fam = value.AsFontFamily();
            if (FontFamilyCombo(label, fam)) { value.SetFontFamily(fam); changed = true; }
            break;
        }
        case ValueType::Ratio: {
            float r = value.AsRatio();
            if (ImGui::SliderFloat(label, &r, 0.0f, 1.0f, "%.2f")) {
                value.SetRatio(r); changed = true;
            }
            break;
        }
        case ValueType::Bezier: {
            ImVec4 b = value.AsBezier();
            if (ImGui::DragFloat4(label, &b.x, 0.01f, -1.0f, 2.0f, "%.2f")) {
                value.SetBezier(b); changed = true;
            }
            break;
        }
        case ValueType::TextStyle: {
            // Edit each axis of the composite; the whole struct is one override.
            TextStyleRefs t = value.AsTextStyle();
            auto editRef = [&](const char* lbl, std::string& ref) {
                char buf[256]; strncpy(buf, ref.c_str(), 255); buf[255] = '\0';
                if (ImGui::InputText(lbl, buf, sizeof(buf))) { ref = buf; changed = true; }
            };
            ImGui::PushID(label);
            editRef("family",     t.family);
            editRef("size",       t.size);
            editRef("weight",     t.weight);
            editRef("line-height",t.lineHeight);
            editRef("tracking",   t.tracking);
            ImGui::PopID();
            if (changed) value.SetTextStyle(t);
            break;
        }
    }
    return changed;
}

bool TokenInspector::ValidateOverrideType(const TokenValue& value,
                                          const std::shared_ptr<Token>& token) {
    auto& ds = DesignSystem::Instance();
    ValueType expected;
    try {
        expected = ds.ResolveTokenValue(token->GetId(),
                       ds.GetCurrentContext().GetTheme()).GetType();
    } catch (...) {
        expected = token->GetValueType();
    }
    if (!value.IsReference() && value.GetType() != expected) return false;
    const ValueConstraint& c = token->GetConstraint();
    if (c.IsEmpty()) return true;
    if (value.GetType() == ValueType::Float) return c.Accepts((double)value.AsFloat());
    if (value.GetType() == ValueType::Int)   return c.Accepts((double)value.AsInt());
    return true;
}

// ── Detail block ──────────────────────────────────────────────────────────────

void TokenInspector::RenderDetails(const std::string& tokenId,
                                   Context& ctx, OverrideManager& mgr) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(tokenId);
    if (!token) { ImGui::Text("Token not found!"); return; }

    ImGui::Text("Token: %s",  token->GetId().c_str());
    ImGui::Text("Level: %s",  TokenLevelToString(token->GetLevel()).c_str());
    ImGui::Text("Type: %s",   ValueTypeToString(token->GetValueType()).c_str());
    if (!token->GetDescription().empty())
        ImGui::TextWrapped("Description: %s", token->GetDescription().c_str());

    if (token->HasConstraint()) {
        const ValueConstraint& c = token->GetConstraint();
        if (!c.allowedValues.empty()) {
            std::string s = "Allowed: ";
            for (size_t i = 0; i < c.allowedValues.size(); ++i) {
                if (i) s += ", ";
                char b[32]; snprintf(b, sizeof(b), "%.3g", c.allowedValues[i]); s += b;
            }
            ImGui::TextDisabled("%s", s.c_str());
        } else {
            auto lo = c.Min(); auto hi = c.Max();
            char b[128];
            snprintf(b, sizeof(b), "Range: [%.3g .. %.3g]%s%s",
                     lo.value_or(-INFINITY), hi.value_or(INFINITY),
                     c.description.empty() ? "" : "   - ", c.description.c_str());
            ImGui::TextDisabled("%s", b);
        }
    }

    ImGui::Separator();
    ImGui::Text("Default Value:");
    TokenValue def = token->GetDefaultValue();
    if (def.IsReference()) {
        ImGui::Text("  Reference: %s", def.AsReference().c_str());

        // ── Full resolution chain ───────────────────────────────────────
        // Walk each ref hop to show the WHOLE chain down to the leaf
        // primitive (the user explicitly asked for this in the Tokens
        // viewer and dev tree alike). Bounded against cycles (16 hops).
        ImGui::Indent();
        std::string chain;
        std::string cur = tokenId;
        auto& reg = TokenRegistry::Instance();
        for (int hop = 0; hop < 16; ++hop) {
            auto t = reg.GetToken(cur);
            if (!t) break;
            if (!chain.empty()) chain += "  →  ";
            chain += cur;
            const TokenValue& dv = t->GetDefaultValue();
            if (!dv.IsReference()) break;
            cur = dv.AsReference();
        }
        ImGui::TextWrapped("Chain: %s", chain.c_str());
        ImGui::Unindent();
    } else {
        ImGui::Indent();
        RenderValuePreview("##defPrev", def, ctx, true);
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Text("Actual Value (theme=%s, accessibility=%s):",
                ThemeTypeToString(ctx.GetTheme()).c_str(),
                AccessibilityTypeToString(ctx.GetAccessibility()).c_str());
    ImGui::Indent();
    try {
        TokenValue resolved = DesignSystem::Instance()
                                  .ResolveTokenValue(token->GetId(), ctx.GetTheme());
        RenderValuePreview("##actPrev", resolved, ctx, true);
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
    }
    ImGui::Unindent();

    if (token->GetValueType() == ValueType::Reference || def.IsReference()) {
        ImGui::Separator();
        ImGui::Text("Reference chain:");
        auto chain = DesignSystem::Instance().GetReferenceChain(
                         token->GetId(), ctx.GetTheme());
        ImGui::Indent();
        for (size_t i = 0; i < chain.size(); ++i) {
            const auto& e = chain[i];
            if (!e.found) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1),
                                   "%s  (not registered)", e.tokenId.c_str());
                break;
            }
            ImGui::Text("%s%s", e.overridden ? "[override] " : "", e.tokenId.c_str());
            if (!e.value.IsReference()) {
                ImGui::SameLine(); ImGui::TextDisabled("=>"); ImGui::SameLine();
                RenderValuePreview(("##ch" + std::to_string(i)).c_str(),
                                   e.value, ctx, true);
            }
        }
        ImGui::Unindent();
    }

    // ── Impact: which components used this token last frame ─────────────
    // Populated by ResolveScoped(...)→RecordUsage(...) for every token
    // visited in the chain, so a primitive lists the components that
    // ultimately resolve through it (not just its direct referrers).
    ImGui::Separator();
    ImGui::Text("Impact (last frame):");
    ImGui::Indent();
    const auto& usage = DesignSystem::Instance().GetUsage();
    auto uit = usage.find(tokenId);
    if (uit == usage.end() || uit->second.empty()) {
        ImGui::TextDisabled("Not used by any tracked component.");
    } else {
        ImGui::Text("%zu distinct component%s — total reads: %d",
                    uit->second.size(),
                    uit->second.size() > 1 ? "s" : "",
                    [&]{ int t = 0; for (auto& kv : uit->second) t += kv.second;
                         return t; }());
        if (ImGui::TreeNode("Components")) {
            std::vector<std::pair<std::string, int>> rows(
                uit->second.begin(), uit->second.end());
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
            if (ImGui::BeginTable("##impactTbl", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Component",
                    ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("Reads",
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
            ImGui::TreePop();
        }
    }

    // ── Tokens that reference this one (direct + via chain) ─────────────
    // Reverse adjacency over GetAllTokens(): a token X is a referrer when
    // X's default-value ref chain reaches `tokenId`. Hop 0 = direct;
    // later hop = via chain. Cycle-guarded by the same 16-hop budget.
    {
        std::vector<std::string> direct, chain;
        auto& reg = TokenRegistry::Instance();
        for (const auto& t : reg.GetAllTokens()) {
            if (t->GetId() == tokenId) continue;
            std::string cur = t->GetId();
            for (int hop = 0; hop < 16; ++hop) {
                auto cur_t = reg.GetToken(cur);
                if (!cur_t) break;
                const TokenValue& dv = cur_t->GetDefaultValue();
                if (!dv.IsReference()) break;
                const std::string& ref = dv.AsReference();
                if (ref == tokenId) {
                    if (hop == 0) direct.push_back(t->GetId());
                    else          chain.push_back(t->GetId());
                    break;
                }
                cur = ref;
            }
        }
        std::sort(direct.begin(), direct.end());
        std::sort(chain.begin(),  chain.end());

        ImGui::Spacing();
        ImGui::Text("Referenced by %zu token%s (%zu direct, %zu via chain):",
                    direct.size() + chain.size(),
                    direct.size() + chain.size() > 1 ? "s" : "",
                    direct.size(), chain.size());
        if (!direct.empty() || !chain.empty()) {
            if (ImGui::TreeNode("##referrers", "Referrers")) {
                if (ImGui::BeginTable("##refTbl", 2,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Token",
                        ImGuiTableColumnFlags_WidthStretch, 0.75f);
                    ImGui::TableSetupColumn("Kind",
                        ImGuiTableColumnFlags_WidthStretch, 0.25f);
                    ImGui::TableHeadersRow();
                    for (const auto& id : direct) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(id.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.0f, 1.0f),
                                           "direct");
                    }
                    for (const auto& id : chain) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(id.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                                           "chain");
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::Unindent();

    ImGui::Separator();
    ImGui::Text("Override status:");
    ImGui::Indent();
    ImGui::Text("Global: %s", mgr.HasGlobalOverride(tokenId) ? "yes" : "no");
    const ThemeType themes[] = { ThemeType::Dark, ThemeType::Light,
                                 ThemeType::MutedGreen, ThemeType::HighContrast };
    for (ThemeType t : themes) {
        bool h = mgr.HasThemeOverride(tokenId, t);
        bool cur = (t == ctx.GetTheme());
        ImGui::Text("%s: %s%s", ThemeTypeToString(t).c_str(),
                    h ? "yes" : "no", cur ? "   <- current theme" : "");
    }
    ImGui::Unindent();
}

// ── Override panel ────────────────────────────────────────────────────────────

void TokenInspector::RenderOverridePanel(const std::string& tokenId,
                                         Context& ctx, OverrideManager& mgr) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(tokenId);
    if (!token) return;

    ImGui::Text("Overrides for this token:");
    ImGui::Separator();

    auto overrides = mgr.GetAllOverrides(tokenId);
    auto active    = mgr.GetBestOverride(tokenId, ctx.GetTheme());

    if (overrides.empty()) {
        ImGui::TextDisabled("No overrides defined");
    } else {
        for (auto* ov : overrides) {
            ImGui::PushID(ov);
            std::string lbl = ov->IsGlobal() ? "Global"
                                             : ThemeTypeToString(*ov->GetTheme());
            if (ov == active)
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "[ACTIVE] %s:", lbl.c_str());
            else
                ImGui::Text("%s:", lbl.c_str());

            ImGui::Indent();
            TokenValue orig = ov->GetValue();
            TokenValue edited = orig;
            bool changed = RenderValueEditor("##ovEdit", edited, token, ctx);
            if (changed && edited != orig) {
                ov->SetValue(edited);
                DesignSystem::Instance().NotifyOverrideChange();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                if (ov->IsGlobal()) mgr.RemoveGlobalOverride(tokenId);
                else                mgr.RemoveThemeOverride(tokenId, *ov->GetTheme());
                DesignSystem::Instance().NotifyOverrideChange();
                ImGui::Unindent();
                ImGui::PopID();
                break;
            }
            ImGui::Unindent();
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::Text("Add / edit override:");
    ImGui::TextDisabled(
        "Two independent editors: the Global override applies to every theme; "
        "the Theme override applies only to '%s' and wins over Global.",
        ThemeTypeToString(ctx.GetTheme()).c_str());

    auto& ds = DesignSystem::DesignSystem::Instance();

    // ── Global override editor ──────────────────────────────────────────────
    {
        ImGui::PushID("globalOv");
        bool has = mgr.HasGlobalOverride(tokenId);
        // Seed the editor with the live global value (override if any, else
        // the resolved value) so it starts from a sensible point.
        TokenValue v = has ? mgr.GetOverride(tokenId, true)->GetValue()
                           : SafeResolve(tokenId, ctx);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Global  ");
        if (has) { ImGui::SameLine();
                   ImGui::TextColored(ImVec4(0.95f,0.75f,0.2f,1), "[set]"); }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        if (RenderValueEditor("##ged", v, token, ctx) &&
            ValidateOverrideType(v, token)) {
            mgr.AddOverride(Override(tokenId, v));
            ds.NotifyOverrideChange();
        }
        if (has) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                mgr.RemoveGlobalOverride(tokenId);
                ds.NotifyOverrideChange();
            }
        }
        ImGui::PopID();
    }

    // ── Current-theme override editor ───────────────────────────────────────
    {
        ImGui::PushID("themeOv");
        bool has = mgr.HasThemeOverride(tokenId, ctx.GetTheme());
        TokenValue v = has
            ? mgr.GetOverride(tokenId, false, ctx.GetTheme())->GetValue()
            : SafeResolve(tokenId, ctx);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Theme   ");
        if (has) { ImGui::SameLine();
                   ImGui::TextColored(ImVec4(0.95f,0.75f,0.2f,1), "[set]"); }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        if (RenderValueEditor("##ted", v, token, ctx) &&
            ValidateOverrideType(v, token)) {
            mgr.AddOverride(Override(tokenId, v, ctx.GetTheme()));
            ds.NotifyOverrideChange();
        }
        if (has) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                mgr.RemoveThemeOverride(tokenId, ctx.GetTheme());
                ds.NotifyOverrideChange();
            }
        }
        ImGui::PopID();
    }
}

// ── Scope-aware property row (shared by every editor) ─────────────────────────

bool TokenInspector::RenderScopedRow(const std::string& tokenId,
                                     const char* displayLabel,
                                     const std::string& scope,
                                     Context& ctx, OverrideManager& mgr) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(tokenId);
    if (!token) return false;

    auto& ds = DesignSystem::DesignSystem::Instance();

    // The override for THIS row targets the scoped id. scope == "" means the
    // global token itself (classic behaviour).
    const std::string targetId =
        DesignSystem::DesignSystem::ScopedId(tokenId, scope);

    ImGui::PushID(targetId.c_str());

    bool isGlobal   = mgr.HasGlobalOverride(targetId);
    bool isThemeOvr = mgr.HasThemeOverride(targetId, ctx.GetTheme());
    bool overridden = isGlobal || isThemeOvr;

    // Value actually shown by this row = the scoped cascade result.
    TokenValue current(ImVec4(1, 1, 1, 1));
    try { current = ds.ResolveScoped(tokenId, scope, ctx.GetTheme()); }
    catch (...) {}

    // "Inherited" = what this row would resolve to WITHOUT its own override
    // at this exact scope (i.e. the parent scope / global cascade). That is
    // the value Reset restores to, and the "original" half of the compare.
    TokenValue inherited = current;
    {
        std::string parent;
        std::size_t slash = scope.find_last_of('/');
        if (!scope.empty())
            parent = (slash == std::string::npos) ? "" : scope.substr(0, slash);
        // Resolve the parent cascade only if there IS an override here;
        // otherwise current already equals the inherited value.
        if (overridden) {
            try {
                inherited = scope.empty()
                    ? token->GetDefaultValue().IsReference()
                          ? ds.ResolveTokenValue(tokenId, ctx.GetTheme())
                          : token->GetDefaultValue()
                    : ds.ResolveScoped(tokenId, parent, ctx.GetTheme());
            } catch (...) {}
        }
    }

    bool changed = false;

    // This row is rendered INSIDE the 3-column table opened by the caller
    // (BeginPropertyTable): col 0 = name, col 1 = Global editor, col 2 =
    // Theme editor. Headers ("Global"/"Theme") are drawn once by the caller.
    ImGui::TableNextRow();

    // ── Column 0: label + state badge + live swatch + compare tooltip ───────
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(displayLabel);
    if (overridden) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f),
                           isGlobal ? "[global]" : "[theme]");
    } else if (ds.HasThemeDefinition(tokenId, scope)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 0.95f, 1.0f), "[theme base]");
    }
    ImGui::SameLine();
    RenderValuePreview("##cur", current, ctx, false);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Token: %s", tokenId.c_str());
        if (!scope.empty()) ImGui::Text("Scope: %s", scope.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Inherited:");
        ImGui::SameLine();
        RenderValuePreview("##inh", inherited, ctx, true);
        ImGui::TextUnformatted("Actual:   ");
        ImGui::SameLine();
        RenderValuePreview("##act", current, ctx, true);
        ImGui::EndTooltip();
    }

    // Helper: one editor cell (Global or Theme) — stretches to the column.
    auto editorCell = [&](const char* id, bool isGlob) {
        ImGui::PushID(id);
        bool has = isGlob ? mgr.HasGlobalOverride(targetId)
                          : mgr.HasThemeOverride(targetId, ctx.GetTheme());
        TokenValue v = has
            ? (isGlob ? mgr.GetOverride(targetId, true)->GetValue()
                      : mgr.GetOverride(targetId, false, ctx.GetTheme())->GetValue())
            : current;
        // Leave room for a trailing clear button when an override is set.
        float w = ImGui::GetContentRegionAvail().x - (has ? 26.0f : 0.0f);
        if (w < 30.0f) w = 30.0f;
        ImGui::SetNextItemWidth(w);
        if (RenderValueEditor("##ed", v, token, ctx) &&
            ValidateOverrideType(v, token)) {
            if (isGlob) mgr.AddOverride(Override(targetId, v));
            else        mgr.AddOverride(Override(targetId, v, ctx.GetTheme()));
            ds.NotifyOverrideChange();
            changed = true;
        }
        if (has) {
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                if (isGlob) mgr.RemoveGlobalOverride(targetId);
                else        mgr.RemoveThemeOverride(targetId, ctx.GetTheme());
                ds.NotifyOverrideChange();
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(isGlob
                    ? "Clear the Global override at this scope"
                    : "Clear the current-theme override at this scope");
        }
        ImGui::PopID();
    };

    // ── Column 1: Global editor ─────────────────────────────────────────────
    ImGui::TableSetColumnIndex(1);
    editorCell("g", true);
    // ── Column 2: Theme editor ──────────────────────────────────────────────
    ImGui::TableSetColumnIndex(2);
    editorCell("t", false);

    ImGui::PopID();
    return changed;
}

// Open/close the shared 3-column property table. Call BeginPropertyTable
// before a run of RenderScopedRow() and EndPropertyTable() after, so every
// editor (Token Tree, User Theme) gets the same column-aligned layout with
// the "Global"/"Theme" headers shown exactly once.
bool TokenInspector::BeginPropertyTable(const char* id) {
    constexpr ImGuiTableFlags kF =
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable(id, 3, kF)) return false;
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.60f);
    ImGui::TableSetupColumn("Global",   ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("Theme",    ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableHeadersRow();
    return true;
}

void TokenInspector::EndPropertyTable() {
    ImGui::EndTable();
}

} // namespace DesignSystem

#include <UI/TokenEditor.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace DesignSystem {

TokenEditor::TokenEditor()
    : selectedThemeIndex_(0),
      selectedAccessibilityIndex_(0),
      showPrimitives_(true),
      showSemantics_(true),
      showComponents_(true),
      newOverrideValue_(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)),
      addingGlobalOverride_(true) {
    searchBuffer_[0] = '\0';
}

// ── Fenêtre autonome ──────────────────────────────────────────────────────────
// RenderPreview (ancienne fenêtre flottante "Theme Preview") n'est plus appelée
// ici : cette section est désormais rendue dans RenderSectionThemePreview()
// du contenu principal de l'application.

void TokenEditor::Render(Context& currentContext, OverrideManager& overrideManager,
                         bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_MenuBar;

    if (!ImGui::Begin("Design System Token Editor", p_open, kFlags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Vue")) {
            ImGui::MenuItem("Primitives",  nullptr, &showPrimitives_);
            ImGui::MenuItem("Sémantiques", nullptr, &showSemantics_);
            ImGui::MenuItem("Composants",  nullptr, &showComponents_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    RenderContent(currentContext, overrideManager);

    ImGui::End();
}

// ── Contenu seul (utilisé dans la fenêtre Paramètres) ────────────────────────

void TokenEditor::RenderContent(Context& currentContext, OverrideManager& overrideManager) {
    Shortcuts::ShortcutManager::Instance().RegisterRegionContext(
        "Token Editor", "tokenEditor", "content");

    // Filtres de visibilité inline (remplacent le menu bar absent dans les onglets)
    ImGui::Checkbox("Primitives",  &showPrimitives_);
    ImGui::SameLine();
    ImGui::Checkbox("Sémantiques", &showSemantics_);
    ImGui::SameLine();
    ImGui::Checkbox("Composants",  &showComponents_);
    ImGui::Separator();

    RenderContextSelector(currentContext);
    ImGui::Separator();

    ImGui::BeginChild("TokenListPane", ImVec2(300.0f, 0.0f), true);
    RenderTokenList();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("TokenDetailsPane", ImVec2(0.0f, 0.0f), true);
    if (!selectedTokenId_.empty()) {
        RenderTokenDetails(currentContext, overrideManager);
        ImGui::Separator();
        RenderOverridePanel(currentContext, overrideManager);
    } else {
        ImGui::TextWrapped("Sélectionner un token pour voir les détails");
    }
    ImGui::EndChild();
}

// ── Sélecteur de contexte ─────────────────────────────────────────────────────

void TokenEditor::RenderContextSelector(Context& currentContext) {
    ImGui::Text("Current Context:");

    const char* themeNames[] = { "Dark", "Light", "Muted Green", "High Contrast" };
    int themeIndex = static_cast<int>(currentContext.GetTheme());
    if (ImGui::Combo("Theme", &themeIndex, themeNames, 4)) {
        currentContext.SetTheme(static_cast<ThemeType>(themeIndex));
        DesignSystem::Instance().SetContext(currentContext);
    }

    ImGui::SameLine();
    const char* accessNames[] = { "None", "Protanopia", "Deuteranopia", "Tritanopia" };
    int accessIndex = static_cast<int>(currentContext.GetAccessibility());
    if (ImGui::Combo("Accessibility", &accessIndex, accessNames, 4)) {
        currentContext.SetAccessibility(static_cast<AccessibilityType>(accessIndex));
        DesignSystem::Instance().SetContext(currentContext);
    }
}

// ── Liste des tokens ──────────────────────────────────────────────────────────

void TokenEditor::RenderTokenList() {
    ImGui::Text("Design Tokens");
    ImGui::InputTextWithHint("##search", "Search...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::Separator();

    auto& registry = TokenRegistry::Instance();

    if (showPrimitives_) {
        if (ImGui::CollapsingHeader("Primitive Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto primitives = registry.GetTokensByLevel(TokenLevel::Primitive);
            for (const auto& token : primitives) {
                if (IsTokenFiltered(token)) continue;
                bool isSelected = (selectedTokenId_ == token->GetId());
                if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
                    selectedTokenId_ = token->GetId();
                    InitializeNewOverrideValue(token);
                }
            }
        }
    }

    if (showSemantics_) {
        if (ImGui::CollapsingHeader("Semantic Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto semantics = registry.GetTokensByLevel(TokenLevel::Semantic);
            for (const auto& token : semantics) {
                if (IsTokenFiltered(token)) continue;
                bool isSelected = (selectedTokenId_ == token->GetId());
                if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
                    selectedTokenId_ = token->GetId();
                    InitializeNewOverrideValue(token);
                }
            }
        }
    }

    if (showComponents_) {
        if (ImGui::CollapsingHeader("Component Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto components = registry.GetTokensByLevel(TokenLevel::Component);
            for (const auto& token : components) {
                if (IsTokenFiltered(token)) continue;
                bool isSelected = (selectedTokenId_ == token->GetId());
                if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
                    selectedTokenId_ = token->GetId();
                    InitializeNewOverrideValue(token);
                }
            }
        }
    }
}

void TokenEditor::InitializeNewOverrideValue(std::shared_ptr<Token> token) {
    // Pre-fill the "Add Override" form with the token's *currently resolved*
    // value.  Starting from 0 / red / empty made the form feel like a destructive
    // reset; starting from the live value makes the override a small tweak away.
    auto& ds = DesignSystem::Instance();
    try {
        newOverrideValue_ = ds.ResolveTokenValue(token->GetId(),
                                ds.GetCurrentContext().GetTheme());
        return;
    } catch (...) {
        // Token has no resolvable value yet (unregistered reference, etc.):
        // fall through to a type-appropriate zero.
    }
    switch (token->GetValueType()) {
        case ValueType::Color:     newOverrideValue_ = TokenValue(ImVec4(1, 1, 1, 1)); break;
        case ValueType::Float:     newOverrideValue_ = TokenValue(0.0f); break;
        case ValueType::Int:       newOverrideValue_ = TokenValue(0); break;
        case ValueType::Vec2:      newOverrideValue_ = TokenValue(ImVec2(0, 0)); break;
        case ValueType::Reference: newOverrideValue_ = TokenValue(std::string("")); break;
    }
}

// ── Détails du token ──────────────────────────────────────────────────────────

void TokenEditor::RenderTokenDetails(Context& currentContext,
                                     OverrideManager& overrideManager) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(selectedTokenId_);
    if (!token) { ImGui::Text("Token not found!"); return; }

    ImGui::Text("Token: %s",  token->GetId().c_str());
    ImGui::Text("Level: %s",  TokenLevelToString(token->GetLevel()).c_str());
    ImGui::Text("Type: %s",   ValueTypeToString(token->GetValueType()).c_str());

    if (!token->GetDescription().empty())
        ImGui::TextWrapped("Description: %s", token->GetDescription().c_str());

    // ── Constraint summary ──────────────────────────────────────────────────
    if (token->HasConstraint()) {
        const ValueConstraint& c = token->GetConstraint();
        if (!c.allowedValues.empty()) {
            std::string s = "Allowed: ";
            for (size_t i = 0; i < c.allowedValues.size(); ++i) {
                if (i) s += ", ";
                char buf[32]; snprintf(buf, sizeof(buf), "%.3g", c.allowedValues[i]);
                s += buf;
            }
            ImGui::TextDisabled("%s", s.c_str());
        } else {
            auto lo = c.Min(); auto hi = c.Max();
            char buf[128];
            snprintf(buf, sizeof(buf), "Range: [%.3g .. %.3g]%s%s",
                     lo.value_or(-INFINITY), hi.value_or(INFINITY),
                     c.description.empty() ? "" : "   — ",
                     c.description.c_str());
            ImGui::TextDisabled("%s", buf);
        }
    }

    ImGui::Separator();
    ImGui::Text("Default Value:");
    TokenValue defaultValue = token->GetDefaultValue();

    if (defaultValue.IsReference()) {
        ImGui::Text("  Reference: %s", defaultValue.AsReference().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("=>");
        ImGui::SameLine();
        RenderResolvedPreview(defaultValue.AsReference(), currentContext);
    } else {
        ImGui::Indent();
        RenderValuePreview("##defaultPreview", defaultValue, currentContext, true);
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::Text("Actual Value (theme=%s, accessibility=%s):",
                ThemeTypeToString(currentContext.GetTheme()).c_str(),
                AccessibilityTypeToString(currentContext.GetAccessibility()).c_str());
    ImGui::Indent();
    RenderActualValue(token, currentContext);
    ImGui::Unindent();

    // ── Reference chain ─────────────────────────────────────────────────────
    if (token->GetValueType() == ValueType::Reference || defaultValue.IsReference()) {
        ImGui::Separator();
        ImGui::Text("Reference chain:");
        auto chain = DesignSystem::Instance().GetReferenceChain(
                         token->GetId(), currentContext.GetTheme());
        ImGui::Indent();
        for (size_t i = 0; i < chain.size(); ++i) {
            const auto& entry = chain[i];
            if (!entry.found) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1),
                                   "%s  (not registered)", entry.tokenId.c_str());
                break;
            }
            const char* tag = entry.overridden ? "[override] " : "";
            ImGui::Text("%s%s", tag, entry.tokenId.c_str());
            if (!entry.value.IsReference()) {
                ImGui::SameLine();
                ImGui::TextDisabled("=>");
                ImGui::SameLine();
                RenderValuePreview(("##chain" + std::to_string(i)).c_str(),
                                    entry.value, currentContext, true);
            }
        }
        ImGui::Unindent();
    }

    // ── Override status across themes ───────────────────────────────────────
    ImGui::Separator();
    ImGui::Text("Override status:");
    ImGui::Indent();
    const char* globalTag = overrideManager.HasGlobalOverride(selectedTokenId_)
                                ? "yes" : "no";
    ImGui::Text("Global: %s", globalTag);
    const ThemeType allThemes[] = {
        ThemeType::Dark, ThemeType::Light,
        ThemeType::MutedGreen, ThemeType::HighContrast
    };
    for (ThemeType t : allThemes) {
        bool h = overrideManager.HasThemeOverride(selectedTokenId_, t);
        bool current = (t == currentContext.GetTheme());
        ImGui::Text("%s: %s%s",
                    ThemeTypeToString(t).c_str(),
                    h ? "yes" : "no",
                    current ? "   <- current theme" : "");
    }
    ImGui::Unindent();
}

void TokenEditor::RenderActualValue(std::shared_ptr<Token> token,
                                    const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(),
                                        currentContext.GetTheme());
        RenderValuePreview("##actualPreview", resolvedValue, currentContext, true);
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
    }
}

void TokenEditor::RenderResolvedPreview(const std::string& refTokenId,
                                        const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(refTokenId,
                                        currentContext.GetTheme());
        RenderValuePreview(("##refPreview" + refTokenId).c_str(),
                           resolvedValue, currentContext, false);
    } catch (...) {
        ImGui::TextDisabled("(unresolved)");
    }
}

void TokenEditor::RenderValuePreview(const char* label, const TokenValue& value,
                                     const Context& currentContext, bool showLabel) {
    switch (value.GetType()) {
        case ValueType::Color: {
            ImVec4 color = value.AsColor();
            RenderColorPreview(label, color, currentContext);
            if (showLabel) {
                ImGui::SameLine();
                ImGui::Text("RGB(%d, %d, %d, %d)",
                    (int)(color.x * 255), (int)(color.y * 255),
                    (int)(color.z * 255), (int)(color.w * 255));
            }
            break;
        }
        case ValueType::Float: {
            float f = value.AsFloat();
            RenderFloatPreview(label, f);
            if (showLabel) { ImGui::SameLine(); ImGui::Text("%.2f", f); }
            break;
        }
        case ValueType::Int: {
            if (showLabel) ImGui::Text("%d", value.AsInt());
            break;
        }
        case ValueType::Vec2: {
            ImVec2 vec = value.AsVec2();
            if (showLabel) ImGui::Text("(%.1f, %.1f)", vec.x, vec.y);
            break;
        }
        case ValueType::Reference: {
            if (showLabel) ImGui::Text("Ref: %s", value.AsReference().c_str());
            break;
        }
    }
}

void TokenEditor::RenderColorPreview(const char* label, const ImVec4& color,
                                     const Context& currentContext) {
    bool hasAccessibility = (currentContext.GetAccessibility() != AccessibilityType::None);
    ImVec4 transformedColor = color;
    if (hasAccessibility)
        transformedColor = ColorBlindness::ApplyColorBlindness(color,
                               currentContext.GetAccessibility());

    auto& ds = DesignSystem::Instance();
    float globalAlpha = 1.0f;
    try { globalAlpha = ds.GetFloat("semantic.alpha.default"); } catch (...) {}

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 size(50.0f, 25.0f);

    if (hasAccessibility) {
        ImVec4 c1 = color;           c1.w *= globalAlpha;
        ImVec4 c2 = transformedColor; c2.w *= globalAlpha;
        ImU32 col1 = ImGui::ColorConvertFloat4ToU32(c1);
        ImU32 col2 = ImGui::ColorConvertFloat4ToU32(c2);
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y), col1);
        drawList->AddRectFilled(ImVec2(pos.x + size.x * 0.5f, pos.y),
                                ImVec2(pos.x + size.x, pos.y + size.y), col2);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(200, 200, 200, (int)(255 * globalAlpha)), 3.0f);
    } else {
        ImVec4 c = color; c.w *= globalAlpha;
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                ImGui::ColorConvertFloat4ToU32(c), 3.0f);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(200, 200, 200, (int)(255 * globalAlpha)), 3.0f);
    }

    ImGui::Dummy(size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Original:");
        ImGui::Text("  RGB: %d, %d, %d",
            (int)(color.x*255), (int)(color.y*255), (int)(color.z*255));
        ImGui::Text("  Hex: #%02X%02X%02X",
            (int)(color.x*255), (int)(color.y*255), (int)(color.z*255));
        ImGui::Text("  Alpha: %.2f (Global: %.2f)", color.w, globalAlpha);
        if (hasAccessibility) {
            ImGui::Separator();
            ImGui::Text("Accessibility-corrected:");
            ImGui::Text("  RGB: %d, %d, %d",
                (int)(transformedColor.x*255),
                (int)(transformedColor.y*255),
                (int)(transformedColor.z*255));
        }
        ImGui::EndTooltip();
    }
}

void TokenEditor::RenderFloatPreview(const char* label, float value) {
    std::string tokenId = selectedTokenId_;
    auto& ds = DesignSystem::Instance();
    float globalAlpha = 1.0f;
    try { globalAlpha = ds.GetFloat("semantic.alpha.default"); } catch (...) {}

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    if (tokenId.find("radius") != std::string::npos) {
        ImVec2 size(50.0f, 50.0f);
        ImU32 borderColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
        ImU32 fillColor   = IM_COL32(255, 0, 255, (int)(50  * globalAlpha));
        float maxRadius   = std::min(size.x, size.y) * 0.5f;
        float radius      = std::min(std::max(0.0f, value), maxRadius);

        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                fillColor, radius, ImDrawFlags_RoundCornersTopLeft);
        drawList->PathLineTo(ImVec2(pos.x, pos.y + size.y));
        if (radius > 0.0f)
            drawList->PathArcTo(ImVec2(pos.x + radius, pos.y + radius),
                                radius, -M_PI, -M_PI * 0.5f, 12);
        else
            drawList->PathLineTo(ImVec2(pos.x, pos.y));
        drawList->PathLineTo(ImVec2(pos.x + size.x, pos.y));
        drawList->PathStroke(borderColor, 0, 1.0f);
        ImGui::Dummy(size);
    }
    else if (tokenId.find("spacing") != std::string::npos ||
             tokenId.find("padding") != std::string::npos) {
        ImVec2 size(60.0f, 30.0f);
        ImU32 barColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
        ImU32 gapColor = IM_COL32(255, 0, 255, (int)(50  * globalAlpha));
        float barW = 1.0f, gap = value;
        drawList->AddRectFilled(pos,
            ImVec2(pos.x + barW, pos.y + size.y), barColor);
        drawList->AddRectFilled(ImVec2(pos.x + barW, pos.y),
            ImVec2(pos.x + barW + gap, pos.y + size.y), gapColor);
        drawList->AddRectFilled(ImVec2(pos.x + barW + gap, pos.y),
            ImVec2(pos.x + barW + gap + barW, pos.y + size.y), barColor);
        ImGui::Dummy(size);
    }
    else if (tokenId.find("fontScale") != std::string::npos) {
        // Preview a font-only multiplier by pushing the current base size × value.
        // Uses imgui 1.92+ lazy rasterisation: glyphs are rebaked at the exact
        // size, no post-raster zoom, so the preview matches the real rendering.
        float baseSize = ImGui::GetStyle().FontSizeBase;
        if (baseSize < 1.0f) baseSize = 14.0f;
        ImGui::PushFont(nullptr, baseSize * value);
        ImGui::Text("Aa");
        ImGui::PopFont();
    }
    else if (tokenId.find("fontSize") != std::string::npos ||
             tokenId.find("font") != std::string::npos) {
        // Preview an absolute logical font size in pixels.
        ImGui::PushFont(nullptr, value);
        ImGui::Text("Aa");
        ImGui::PopFont();
    }
    else if (tokenId.find("alpha") != std::string::npos) {
        ImVec2 size(50.0f, 25.0f);
        for (int y = 0; y < 25; y += 5)
            for (int x = 0; x < 50; x += 5) {
                bool checker = ((x / 5) + (y / 5)) % 2 == 0;
                drawList->AddRectFilled(
                    ImVec2(pos.x + x, pos.y + y),
                    ImVec2(pos.x + x + 5, pos.y + y + 5),
                    checker ? IM_COL32(200, 200, 200, 255) : IM_COL32(100, 100, 100, 255));
            }
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                IM_COL32(255, 255, 255, (int)(255 * value)));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(200, 200, 200, 255), 0.0f, 0, 2.0f);
        ImGui::Dummy(size);
    }
    else if (tokenId.find("scale") != std::string::npos) {
        ImGui::Text("Scale: %.0f%%", value * 100.0f);
        ImVec2 size(40.0f, 40.0f);
        float scaledSize = size.x * value;
        drawList->AddRectFilled(pos,
            ImVec2(pos.x + scaledSize, pos.y + scaledSize),
            IM_COL32(255, 0, 255, (int)(128 * globalAlpha)));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(200, 200, 200, (int)(100 * globalAlpha)), 0.0f, 0, 1.0f);
        ImGui::Dummy(size);
    }
    else {
        ImGui::Text("%.2f", value);
    }
}

// ── Panneau d'overrides ───────────────────────────────────────────────────────

void TokenEditor::RenderOverridePanel(Context& currentContext,
                                      OverrideManager& overrideManager) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(selectedTokenId_);
    if (!token) return;

    ImGui::Text("Overrides for this token:");
    ImGui::Separator();

    auto overrides     = overrideManager.GetAllOverrides(selectedTokenId_);
    auto activeOverride= overrideManager.GetBestOverride(selectedTokenId_,
                             currentContext.GetTheme());

    if (overrides.empty()) {
        ImGui::TextDisabled("No overrides defined");
    } else {
        for (auto* ov : overrides) {
            ImGui::PushID(ov);

            std::string label = ov->IsGlobal() ? "Global" :
                                ThemeTypeToString(*ov->GetTheme());
            bool isActive = (ov == activeOverride);

            if (isActive)
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                   "[ACTIVE] %s:", label.c_str());
            else
                ImGui::Text("%s:", label.c_str());

            ImGui::Indent();
            TokenValue originalValue = ov->GetValue();
            TokenValue editedValue   = originalValue;

            bool changed = RenderValueEditor("##overrideEdit", editedValue,
                                            token, currentContext);
            if (changed && editedValue != originalValue) {
                ov->SetValue(editedValue);
                DesignSystem::Instance().NotifyOverrideChange();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                if (ov->IsGlobal())
                    overrideManager.RemoveGlobalOverride(selectedTokenId_);
                else
                    overrideManager.RemoveThemeOverride(selectedTokenId_,
                                                       *ov->GetTheme());
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
    ImGui::Text("Add New Override:");
    ImGui::Checkbox("Global Override", &addingGlobalOverride_);
    if (!addingGlobalOverride_) {
        ImGui::SameLine();
        ImGui::Text("(for current theme: %s)",
                    ThemeTypeToString(currentContext.GetTheme()).c_str());
    }

    RenderValueEditor("##newOverride", newOverrideValue_, token, currentContext);

    ImGui::SameLine();
    if (ImGui::Button("Add Override")) {
        if (ValidateOverrideType(newOverrideValue_, token)) {
            if (addingGlobalOverride_)
                overrideManager.AddOverride(
                    Override(selectedTokenId_, newOverrideValue_));
            else
                overrideManager.AddOverride(
                    Override(selectedTokenId_, newOverrideValue_,
                             currentContext.GetTheme()));
            DesignSystem::Instance().NotifyOverrideChange();
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Type mismatch!");
        }
    }
}

bool TokenEditor::RenderValueEditor(const char* label, TokenValue& value,
                                    std::shared_ptr<Token> token,
                                    const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    ValueType expectedType;
    try {
        expectedType = ds.ResolveTokenValue(token->GetId(),
                           currentContext.GetTheme()).GetType();
    } catch (...) {
        expectedType = token->GetValueType();
    }

    if (value.GetType() != expectedType && !value.IsReference()) {
        switch (expectedType) {
            case ValueType::Color:
                value = TokenValue(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); break;
            case ValueType::Float:
                value = TokenValue(0.0f); break;
            case ValueType::Int:
                value = TokenValue(0); break;
            case ValueType::Vec2:
                value = TokenValue(ImVec2(0.0f, 0.0f)); break;
            default: break;
        }
    }

    bool changed = false;
    const ValueConstraint& c = token->GetConstraint();

    switch (value.GetType()) {
        case ValueType::Color: {
            ImVec4 color = value.AsColor();
            if (ImGui::ColorEdit4(label, &color.x,
                                  ImGuiColorEditFlags_NoInputs |
                                  ImGuiColorEditFlags_AlphaBar)) {
                value.SetColor(color); changed = true;
            }
            break;
        }
        case ValueType::Float: {
            // Drive the widget from the constraint, not a name heuristic.
            // - constraint with finite min+max         → SliderFloat in range
            // - constraint with one-of discrete values → Combo of allowed values
            // - constraint with one open bound         → DragFloat using that bound
            // - no constraint                          → DragFloat with a wide cap
            float f = value.AsFloat();
            if (!c.allowedValues.empty()) {
                int sel = 0;
                for (size_t i = 0; i < c.allowedValues.size(); ++i)
                    if (static_cast<float>(c.allowedValues[i]) == f) { sel = static_cast<int>(i); break; }
                std::vector<std::string> items;
                items.reserve(c.allowedValues.size());
                for (double v : c.allowedValues) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.3g", v);
                    items.emplace_back(buf);
                }
                std::vector<const char*> cstrs;
                cstrs.reserve(items.size());
                for (auto& s : items) cstrs.push_back(s.c_str());
                if (ImGui::Combo(label, &sel, cstrs.data(),
                                 static_cast<int>(cstrs.size()))) {
                    value.SetFloat(static_cast<float>(c.allowedValues[sel]));
                    changed = true;
                }
            } else {
                auto lo = c.Min();
                auto hi = c.Max();
                if (lo && hi) {
                    float step = static_cast<float>(c.step > 0 ? c.step : 0.0);
                    (void)step; // ImGui Slider has no built-in step; future: snap on commit.
                    if (ImGui::SliderFloat(label, &f,
                                           static_cast<float>(*lo),
                                           static_cast<float>(*hi))) {
                        value.SetFloat(static_cast<float>(c.Clamp(f)));
                        changed = true;
                    }
                } else {
                    float drag_min = lo ? static_cast<float>(*lo) : -FLT_MAX;
                    float drag_max = hi ? static_cast<float>(*hi) :  FLT_MAX;
                    if (ImGui::DragFloat(label, &f, 0.1f, drag_min, drag_max)) {
                        value.SetFloat(static_cast<float>(c.Clamp(f)));
                        changed = true;
                    }
                }
            }
            break;
        }
        case ValueType::Int: {
            int i = value.AsInt();
            auto lo = c.Min();
            auto hi = c.Max();
            int drag_min = lo ? static_cast<int>(*lo) : 0;
            int drag_max = hi ? static_cast<int>(*hi) : 1000;
            if (lo && hi) {
                if (ImGui::SliderInt(label, &i, drag_min, drag_max)) {
                    value.SetInt(static_cast<int>(c.Clamp(static_cast<double>(i))));
                    changed = true;
                }
            } else {
                if (ImGui::DragInt(label, &i, 1, drag_min, drag_max)) {
                    value.SetInt(static_cast<int>(c.Clamp(static_cast<double>(i))));
                    changed = true;
                }
            }
            break;
        }
        case ValueType::Vec2: {
            ImVec2 vec = value.AsVec2();
            if (ImGui::DragFloat2(label, &vec.x, 0.1f))
                { value.SetVec2(vec); changed = true; }
            break;
        }
        case ValueType::Reference: {
            std::string ref = value.AsReference();
            char buffer[256];
            strncpy(buffer, ref.c_str(), 255);
            buffer[255] = '\0';
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
                { value.SetReference(std::string(buffer)); changed = true; }
            break;
        }
    }
    return changed;
}

bool TokenEditor::ValidateOverrideType(const TokenValue& value,
                                       std::shared_ptr<Token> token) {
    auto& ds = DesignSystem::Instance();
    ValueType expectedType;
    try {
        expectedType = ds.ResolveTokenValue(token->GetId(),
                           ds.GetCurrentContext().GetTheme()).GetType();
    } catch (...) {
        // No resolvable value yet; accept anything that matches the declared type.
        expectedType = token->GetValueType();
    }
    if (!value.IsReference() && value.GetType() != expectedType) return false;

    // Type matches.  Also verify the constraint accepts the numeric value;
    // ImGui sliders already clamp, but a programmatically-built TokenValue
    // (or a future code path) could still slip through.
    const ValueConstraint& c = token->GetConstraint();
    if (c.IsEmpty()) return true;
    if (value.GetType() == ValueType::Float) return c.Accepts(static_cast<double>(value.AsFloat()));
    if (value.GetType() == ValueType::Int)   return c.Accepts(static_cast<double>(value.AsInt()));
    return true;
}

bool TokenEditor::IsTokenFiltered(std::shared_ptr<Token> token) const {
    if (searchBuffer_[0] == '\0') return false;
    return token->GetId().find(searchBuffer_) == std::string::npos;
}

} // namespace DesignSystem
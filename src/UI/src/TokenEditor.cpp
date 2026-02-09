// // #include <DesignSystem/UI/TokenEditor.h>
// // #include <DesignSystem/Tokens/TokenRegistry.h>
// // #include <DesignSystem/Tokens/Token.h>
// // #include <DesignSystem/Override/OverrideManager.h>
// // #include <DesignSystem/Accessibility/ColorBlindness.h>
// // #include <DesignSystem/DesignSystem.h>
// // #include <cstring>
// // #include <iostream>
// // #include <cmath>

// // namespace DesignSystem {

// // TokenEditor::TokenEditor() 
// //     : selectedThemeIndex_(0),
// //       selectedAccessibilityIndex_(0),
// //       showPrimitives_(true),
// //       showSemantics_(true),
// //       showComponents_(true),
// //       newOverrideValue_(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)),
// //       addingGlobalOverride_(true) {
// //     searchBuffer_[0] = '\0';
// // }

// // /**
// //  * CRITICAL FIX: Force style reapplication to ensure UI updates.
// //  */
// // void TokenEditor::Render(Context& currentContext, OverrideManager& overrideManager) {
// //     DesignSystem::Instance().ApplyGlobalStyle();
    
// //     ImGui::Begin("Design System Token Editor", nullptr, ImGuiWindowFlags_MenuBar);
    
// //     if (ImGui::BeginMenuBar()) {
// //         if (ImGui::BeginMenu("View")) {
// //             ImGui::MenuItem("Show Primitives", nullptr, &showPrimitives_);
// //             ImGui::MenuItem("Show Semantics", nullptr, &showSemantics_);
// //             ImGui::MenuItem("Show Components", nullptr, &showComponents_);
// //             ImGui::EndMenu();
// //         }
// //         ImGui::EndMenuBar();
// //     }
    
// //     RenderContextSelector(currentContext);
// //     ImGui::Separator();
    
// //     ImGui::BeginChild("TokenListPane", ImVec2(300, 0), true);
// //     RenderTokenList();
// //     ImGui::EndChild();
    
// //     ImGui::SameLine();
    
// //     ImGui::BeginChild("TokenDetailsPane", ImVec2(0, 0), true);
// //     if (!selectedTokenId_.empty()) {
// //         RenderTokenDetails(currentContext, overrideManager);
// //         ImGui::Separator();
// //         RenderOverridePanel(currentContext, overrideManager);
// //     } else {
// //         ImGui::TextWrapped("Select a token from the list to view details");
// //     }
// //     ImGui::EndChild();
    
// //     ImGui::End();
    
// //     RenderPreview(currentContext);
// // }

// // void TokenEditor::RenderContextSelector(Context& currentContext) {
// //     ImGui::Text("Current Context:");
    
// //     const char* themeNames[] = { "Dark", "Light", "Muted Green", "High Contrast" };
// //     int themeIndex = static_cast<int>(currentContext.GetTheme());
// //     if (ImGui::Combo("Theme", &themeIndex, themeNames, 4)) {
// //         currentContext.SetTheme(static_cast<ThemeType>(themeIndex));
// //         DesignSystem::Instance().SetContext(currentContext);
// //     }
    
// //     ImGui::SameLine();
// //     const char* accessNames[] = { "None", "Protanopia", "Deuteranopia", "Tritanopia" };
// //     int accessIndex = static_cast<int>(currentContext.GetAccessibility());
// //     if (ImGui::Combo("Accessibility", &accessIndex, accessNames, 4)) {
// //         currentContext.SetAccessibility(static_cast<AccessibilityType>(accessIndex));
// //         DesignSystem::Instance().SetContext(currentContext);
// //     }
// // }

// // void TokenEditor::RenderTokenList() {
// //     ImGui::Text("Design Tokens");
// //     ImGui::InputTextWithHint("##search", "Search...", searchBuffer_, sizeof(searchBuffer_));
// //     ImGui::Separator();
    
// //     auto& registry = TokenRegistry::Instance();
    
// //     if (showPrimitives_) {
// //         if (ImGui::CollapsingHeader("Primitive Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
// //             auto primitives = registry.GetTokensByLevel(TokenLevel::Primitive);
// //             for (const auto& token : primitives) {
// //                 if (IsTokenFiltered(token)) continue;
// //                 bool isSelected = (selectedTokenId_ == token->GetId());
// //                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
// //                     selectedTokenId_ = token->GetId();
// //                     InitializeNewOverrideValue(token);
// //                 }
// //             }
// //         }
// //     }
    
// //     if (showSemantics_) {
// //         if (ImGui::CollapsingHeader("Semantic Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
// //             auto semantics = registry.GetTokensByLevel(TokenLevel::Semantic);
// //             for (const auto& token : semantics) {
// //                 if (IsTokenFiltered(token)) continue;
// //                 bool isSelected = (selectedTokenId_ == token->GetId());
// //                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
// //                     selectedTokenId_ = token->GetId();
// //                     InitializeNewOverrideValue(token);
// //                 }
// //             }
// //         }
// //     }
    
// //     if (showComponents_) {
// //         if (ImGui::CollapsingHeader("Component Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
// //             auto components = registry.GetTokensByLevel(TokenLevel::Component);
// //             for (const auto& token : components) {
// //                 if (IsTokenFiltered(token)) continue;
// //                 bool isSelected = (selectedTokenId_ == token->GetId());
// //                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
// //                     selectedTokenId_ = token->GetId();
// //                     InitializeNewOverrideValue(token);
// //                 }
// //             }
// //         }
// //     }
// // }

// // /**
// //  * Initialize new override value with correct type matching the token.
// //  */
// // void TokenEditor::InitializeNewOverrideValue(std::shared_ptr<Token> token) {
// //     auto& ds = DesignSystem::Instance();
    
// //     try {
// //         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
        
// //         switch (resolvedValue.GetType()) {
// //             case ValueType::Color:
// //                 newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
// //                 break;
// //             case ValueType::Float:
// //                 newOverrideValue_ = TokenValue(0.0f);
// //                 break;
// //             case ValueType::Int:
// //                 newOverrideValue_ = TokenValue(0);
// //                 break;
// //             case ValueType::Vec2:
// //                 newOverrideValue_ = TokenValue(ImVec2(0.0f, 0.0f));
// //                 break;
// //             case ValueType::Reference:
// //                 newOverrideValue_ = TokenValue(std::string(""));
// //                 break;
// //         }
// //     } catch (...) {
// //         newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
// //     }
// // }

// // void TokenEditor::RenderTokenDetails(Context& currentContext, OverrideManager& overrideManager) {
// //     auto& registry = TokenRegistry::Instance();
// //     auto token = registry.GetToken(selectedTokenId_);
// //     if (!token) {
// //         ImGui::Text("Token not found!");
// //         return;
// //     }
    
// //     ImGui::Text("Token: %s", token->GetId().c_str());
// //     ImGui::Text("Level: %s", TokenLevelToString(token->GetLevel()).c_str());
// //     ImGui::Text("Type: %s", ValueTypeToString(token->GetValueType()).c_str());
    
// //     if (!token->GetDescription().empty()) {
// //         ImGui::TextWrapped("Description: %s", token->GetDescription().c_str());
// //     }
    
// //     ImGui::Separator();
    
// //     // Display default value (read-only)
// //     ImGui::Text("Default Value:");
// //     TokenValue defaultValue = token->GetDefaultValue();
    
// //     // For references, show both the reference and the resolved value
// //     if (defaultValue.IsReference()) {
// //         ImGui::Text("  Reference: %s", defaultValue.AsReference().c_str());
// //         ImGui::SameLine();
// //         ImGui::TextDisabled("=>");
// //         ImGui::SameLine();
// //         RenderResolvedPreview(defaultValue.AsReference(), currentContext);
// //     } else {
// //         ImGui::Indent();
// //         RenderValuePreview("##defaultPreview", defaultValue, currentContext, true);
// //         ImGui::Unindent();
// //     }
    
// //     ImGui::Separator();
    
// //     // Display actual value (with overrides)
// //     ImGui::Text("Actual Value:");
// //     ImGui::Indent();
// //     RenderActualValue(token, currentContext);
// //     ImGui::Unindent();
// // }

// // /**
// //  * Render actual value for a token (resolves overrides and references).
// //  */
// // void TokenEditor::RenderActualValue(std::shared_ptr<Token> token, const Context& currentContext) {
// //     auto& ds = DesignSystem::Instance();
    
// //     try {
// //         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
// //         RenderValuePreview("##actualPreview", resolvedValue, currentContext, true);
// //     } catch (const std::exception& e) {
// //         ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
// //     }
// // }

// // /**
// //  * Render preview for a resolved reference.
// //  */
// // void TokenEditor::RenderResolvedPreview(const std::string& refTokenId, const Context& currentContext) {
// //     auto& ds = DesignSystem::Instance();
    
// //     try {
// //         TokenValue resolvedValue = ds.ResolveTokenValue(refTokenId, currentContext.GetTheme());
// //         RenderValuePreview(("##refPreview" + refTokenId).c_str(), resolvedValue, currentContext, false);
// //     } catch (...) {
// //         ImGui::TextDisabled("(unresolved)");
// //     }
// // }

// // /**
// //  * Render value preview with visual representation.
// //  * CRITICAL: Shows both original and accessibility-transformed colors if accessibility is enabled.
// //  */
// // void TokenEditor::RenderValuePreview(const char* label, const TokenValue& value, 
// //                                      const Context& currentContext, bool showLabel) {
// //     switch (value.GetType()) {
// //         case ValueType::Color: {
// //             ImVec4 color = value.AsColor();
            
// //             // Use unified color preview with accessibility split
// //             RenderColorPreview(label, color, currentContext);
            
// //             if (showLabel) {
// //                 ImGui::SameLine();
// //                 ImGui::Text("RGB(%d, %d, %d, %d)", 
// //                     static_cast<int>(color.x * 255),
// //                     static_cast<int>(color.y * 255),
// //                     static_cast<int>(color.z * 255),
// //                     static_cast<int>(color.w * 255));
// //             }
// //             break;
// //         }
// //         case ValueType::Float: {
// //             float f = value.AsFloat();
// //             RenderFloatPreview(label, f);
// //             if (showLabel) {
// //                 ImGui::SameLine();
// //                 ImGui::Text("%.2f", f);
// //             }
// //             break;
// //         }
// //         case ValueType::Int: {
// //             int i = value.AsInt();
// //             if (showLabel) {
// //                 ImGui::Text("%d", i);
// //             }
// //             break;
// //         }
// //         case ValueType::Vec2: {
// //             ImVec2 vec = value.AsVec2();
// //             if (showLabel) {
// //                 ImGui::Text("(%.1f, %.1f)", vec.x, vec.y);
// //             }
// //             break;
// //         }
// //         case ValueType::Reference: {
// //             if (showLabel) {
// //                 ImGui::Text("Ref: %s", value.AsReference().c_str());
// //             }
// //             break;
// //         }
// //     }
// // }

// // /**
// //  * CRITICAL: Unified color preview with accessibility split.
// //  * Shows left half as original, right half as accessibility-transformed if accessibility is enabled.
// //  */
// // void TokenEditor::RenderColorPreview(const char* label, const ImVec4& color, const Context& currentContext) {
// //     bool hasAccessibility = (currentContext.GetAccessibility() != AccessibilityType::None);
// //     ImVec4 transformedColor = color;
    
// //     if (hasAccessibility) {
// //         transformedColor = ColorBlindness::ApplyColorBlindness(color, currentContext.GetAccessibility());
// //     }
    
// //     ImDrawList* drawList = ImGui::GetWindowDrawList();
// //     ImVec2 pos = ImGui::GetCursorScreenPos();
// //     ImVec2 size(50, 25);
    
// //     if (hasAccessibility) {
// //         // Split preview: left = original, right = transformed
// //         ImU32 col1 = ImGui::ColorConvertFloat4ToU32(color);
// //         ImU32 col2 = ImGui::ColorConvertFloat4ToU32(transformedColor);
        
// //         drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y), col1);
// //         drawList->AddRectFilled(ImVec2(pos.x + size.x * 0.5f, pos.y), 
// //                                ImVec2(pos.x + size.x, pos.y + size.y), col2);
        
// //         // Border
// //         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(200, 200, 200, 255), 3.0f);
// //     } else {
// //         // Single color preview
// //         ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
// //         drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 3.0f);
// //         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(200, 200, 200, 255), 3.0f);
// //     }
    
// //     ImGui::Dummy(size);
    
// //     // Tooltip with details
// //     if (ImGui::IsItemHovered()) {
// //         ImGui::BeginTooltip();
// //         ImGui::Text("Original:");
// //         ImGui::Text("  RGB: %d, %d, %d", 
// //             static_cast<int>(color.x * 255),
// //             static_cast<int>(color.y * 255),
// //             static_cast<int>(color.z * 255));
// //         ImGui::Text("  Hex: #%02X%02X%02X",
// //             static_cast<int>(color.x * 255),
// //             static_cast<int>(color.y * 255),
// //             static_cast<int>(color.z * 255));
        
// //         if (hasAccessibility) {
// //             ImGui::Separator();
// //             ImGui::Text("Accessibility-corrected:");
// //             ImGui::Text("  RGB: %d, %d, %d",
// //                 static_cast<int>(transformedColor.x * 255),
// //                 static_cast<int>(transformedColor.y * 255),
// //                 static_cast<int>(transformedColor.z * 255));
// //         }
// //         ImGui::EndTooltip();
// //     }
// // }

// // /**
// //  * Render visual preview for float values.
// //  * Infers what the float represents and shows appropriate visualization.
// //  */
// // void TokenEditor::RenderFloatPreview(const char* label, float value) {
// //     std::string tokenId = selectedTokenId_;
// //     ImDrawList* drawList = ImGui::GetWindowDrawList();
// //     ImVec2 pos = ImGui::GetCursorScreenPos();
    
// //     // Infer what this float represents from token name
// //     if (tokenId.find("radius") != std::string::npos) {
// //         // Render corner radius visual
// //         ImVec2 size(40, 40);
// //         ImU32 borderColor = IM_COL32(255, 0, 255, 255);  // Magenta
// //         ImU32 fillColor = IM_COL32(255, 0, 255, 50);     // Transparent magenta
        
// //         // Draw rounded corner (top-left quadrant only)
// //         drawList->PathLineTo(pos);
// //         drawList->PathLineTo(ImVec2(pos.x + size.x, pos.y));
// //         drawList->PathArcTo(ImVec2(pos.x + size.x - value, pos.y + value), value, 0.0f, M_PI * 0.5f);
// //         drawList->PathLineTo(ImVec2(pos.x, pos.y + size.y));
// //         drawList->PathLineTo(pos);
// //         drawList->PathFillConvex(fillColor);
        
// //         // Border
// //         drawList->AddLine(pos, ImVec2(pos.x + size.x, pos.y), borderColor, 2.0f);
// //         drawList->AddLine(pos, ImVec2(pos.x, pos.y + size.y), borderColor, 2.0f);
        
// //         ImGui::Dummy(size);
// //     }
// //     else if (tokenId.find("spacing") != std::string::npos || tokenId.find("padding") != std::string::npos) {
// //         // Render spacing visual (two bars with gap)
// //         ImVec2 size(50, 30);
// //         ImU32 barColor = IM_COL32(255, 0, 255, 255);  // Magenta
// //         ImU32 gapColor = IM_COL32(255, 0, 255, 50);   // Transparent magenta
        
// //         float barWidth = 5.0f;
// //         float gap = value;
        
// //         // Left bar
// //         drawList->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + size.y), barColor);
        
// //         // Gap (transparent)
// //         drawList->AddRectFilled(ImVec2(pos.x + barWidth, pos.y), 
// //                                ImVec2(pos.x + barWidth + gap, pos.y + size.y), gapColor);
        
// //         // Right bar
// //         drawList->AddRectFilled(ImVec2(pos.x + barWidth + gap, pos.y), 
// //                                ImVec2(pos.x + barWidth + gap + barWidth, pos.y + size.y), barColor);
        
// //         ImGui::Dummy(size);
// //     }
// //     else if (tokenId.find("size") != std::string::npos || tokenId.find("font") != std::string::npos) {
// //         // Render text size preview
// //         ImGui::PushFont(ImGui::GetFont());
// //         ImGui::SetWindowFontScale(value / 16.0f);  // Assuming 16 is default
// //         ImGui::Text("Aa");
// //         ImGui::SetWindowFontScale(1.0f);
// //         ImGui::PopFont();
// //     }
// //     else {
// //         // Default: just show the number
// //         ImGui::Text("%.2f", value);
// //     }
// // }

// // /**
// //  * CRITICAL: Override panel with type validation and proper editing.
// //  * Shows all overrides, indicates which is active, allows editing values.
// //  */
// // void TokenEditor::RenderOverridePanel(Context& currentContext, OverrideManager& overrideManager) {
// //     auto& registry = TokenRegistry::Instance();
// //     auto token = registry.GetToken(selectedTokenId_);
// //     if (!token) return;
    
// //     ImGui::Text("Overrides for this token:");
// //     ImGui::Separator();
    
// //     auto overrides = overrideManager.GetAllOverrides(selectedTokenId_);
// //     auto activeOverride = overrideManager.GetBestOverride(selectedTokenId_, currentContext.GetTheme());
    
// //     if (overrides.empty()) {
// //         ImGui::TextDisabled("No overrides defined");
// //     } else {
// //         for (auto* override : overrides) {
// //             ImGui::PushID(override);
            
// //             // Show override type and active indicator
// //             std::string label = override->IsGlobal() ? "Global" : 
// //                                ThemeTypeToString(*override->GetTheme());
            
// //             bool isActive = (override == activeOverride);
// //             if (isActive) {
// //                 ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ACTIVE] %s:", label.c_str());
// //             } else {
// //                 ImGui::Text("%s:", label.c_str());
// //             }
            
// //             // CRITICAL FIX: Allow editing override value with proper type validation
// //             ImGui::Indent();
            
// //             // Store original value for comparison
// //             TokenValue originalValue = override->GetValue();
// //             TokenValue editedValue = originalValue;
            
// //             // Render editor based on type
// //             bool valueChanged = RenderValueEditor("##overrideEdit", editedValue, token, currentContext);
            
// //             // CRITICAL FIX: Only update if value actually changed
// //             if (valueChanged && editedValue != originalValue) {
// //                 override->SetValue(editedValue);
// //                 // Trigger save
// //                 DesignSystem::Instance().NotifyOverrideChange();
// //             }
            
// //             ImGui::SameLine();
            
// //             // Remove button
// //             if (ImGui::SmallButton("Remove")) {
// //                 if (override->IsGlobal()) {
// //                     overrideManager.RemoveGlobalOverride(selectedTokenId_);
// //                 } else {
// //                     overrideManager.RemoveThemeOverride(selectedTokenId_, *override->GetTheme());
// //                 }
// //                 // Trigger save
// //                 DesignSystem::Instance().NotifyOverrideChange();
// //                 ImGui::Unindent();
// //                 ImGui::PopID();
// //                 break;  // Exit loop after removal
// //             }
            
// //             ImGui::Unindent();
// //             ImGui::PopID();
// //         }
// //     }
    
// //     ImGui::Separator();
// //     ImGui::Text("Add New Override:");
    
// //     ImGui::Checkbox("Global Override", &addingGlobalOverride_);
// //     if (!addingGlobalOverride_) {
// //         ImGui::SameLine();
// //         ImGui::Text("(for current theme: %s)", ThemeTypeToString(currentContext.GetTheme()).c_str());
// //     }
    
// //     // Value editor for new override (with type validation)
// //     RenderValueEditor("##newOverride", newOverrideValue_, token, currentContext);
    
// //     ImGui::SameLine();
// //     if (ImGui::Button("Add Override")) {
// //         // Validate type matches token
// //         if (ValidateOverrideType(newOverrideValue_, token)) {
// //             if (addingGlobalOverride_) {
// //                 overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_));
// //             } else {
// //                 overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_, 
// //                                                      currentContext.GetTheme()));
// //             }
// //             // Trigger save
// //             DesignSystem::Instance().NotifyOverrideChange();
// //         } else {
// //             // Show error
// //             ImGui::SameLine();
// //             ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Type mismatch!");
// //         }
// //     }
// // }

// // /**
// //  * CRITICAL: Render value editor with type enforcement.
// //  * Returns true if value was modified.
// //  */
// // bool TokenEditor::RenderValueEditor(const char* label, TokenValue& value, 
// //                                    std::shared_ptr<Token> token, const Context& currentContext) {
// //     // Get expected type from token
// //     auto& ds = DesignSystem::Instance();
// //     ValueType expectedType;
    
// //     try {
// //         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
// //         expectedType = resolvedValue.GetType();
// //     } catch (...) {
// //         expectedType = token->GetValueType();
// //     }
    
// //     // Enforce type match
// //     if (value.GetType() != expectedType && !value.IsReference()) {
// //         // Auto-convert to expected type
// //         switch (expectedType) {
// //             case ValueType::Color:
// //                 value = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
// //                 break;
// //             case ValueType::Float:
// //                 value = TokenValue(0.0f);
// //                 break;
// //             case ValueType::Int:
// //                 value = TokenValue(0);
// //                 break;
// //             case ValueType::Vec2:
// //                 value = TokenValue(ImVec2(0.0f, 0.0f));
// //                 break;
// //             default:
// //                 break;
// //         }
// //     }
    
// //     bool changed = false;
    
// //     switch (value.GetType()) {
// //         case ValueType::Color: {
// //             ImVec4 color = value.AsColor();
// //             if (ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_NoInputs)) {
// //                 value.SetColor(color);
// //                 changed = true;
// //             }
// //             break;
// //         }
// //         case ValueType::Float: {
// //             float f = value.AsFloat();
// //             if (ImGui::DragFloat(label, &f, 0.1f, 0.0f, 1000.0f)) {
// //                 value.SetFloat(f);
// //                 changed = true;
// //             }
// //             break;
// //         }
// //         case ValueType::Int: {
// //             int i = value.AsInt();
// //             if (ImGui::DragInt(label, &i, 1, 0, 1000)) {
// //                 value.SetInt(i);
// //                 changed = true;
// //             }
// //             break;
// //         }
// //         case ValueType::Vec2: {
// //             ImVec2 vec = value.AsVec2();
// //             if (ImGui::DragFloat2(label, &vec.x, 0.1f)) {
// //                 value.SetVec2(vec);
// //                 changed = true;
// //             }
// //             break;
// //         }
// //         case ValueType::Reference: {
// //             std::string ref = value.AsReference();
// //             char buffer[256];
// //             strncpy(buffer, ref.c_str(), 255);
// //             buffer[255] = '\0';
// //             if (ImGui::InputText(label, buffer, sizeof(buffer))) {
// //                 value.SetReference(std::string(buffer));
// //                 changed = true;
// //             }
// //             break;
// //         }
// //     }
    
// //     return changed;
// // }

// // /**
// //  * Validate that override value type matches token's expected type.
// //  */
// // bool TokenEditor::ValidateOverrideType(const TokenValue& value, std::shared_ptr<Token> token) {
// //     auto& ds = DesignSystem::Instance();
    
// //     try {
// //         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
// //         return value.GetType() == resolvedValue.GetType() || value.IsReference();
// //     } catch (...) {
// //         return true;  // Allow if can't resolve
// //     }
// // }

// // void TokenEditor::RenderPreview(const Context& currentContext) {
// //     ImGui::Begin("Theme Preview");
    
// //     ImGui::Text("Preview with current context");
// //     ImGui::Separator();
    
// //     auto& ds = DesignSystem::Instance();
    
// //     try {
// //         ImVec4 bgColor = ds.GetColor("semantic.color.background");
// //         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");
        
// //         RenderColorPreview("Background", bgColor, currentContext);
// //         ImGui::SameLine();
// //         ImGui::Text("Background Color");
        
// //         RenderColorPreview("Primary", primaryColor, currentContext);
// //         ImGui::SameLine();
// //         ImGui::Text("Primary Color");
        
// //         ImGui::Separator();
// //         ImGui::Text("Sample Components:");
        
// //         if (ImGui::Button("Sample Button")) {}
        
// //         static char textBuffer[128] = "Sample Input";
// //         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));
        
// //     } catch (const std::exception& e) {
// //         ImGui::Text("Error resolving tokens: %s", e.what());
// //     }
    
// //     ImGui::End();
// // }

// // bool TokenEditor::IsTokenFiltered(std::shared_ptr<Token> token) const {
// //     if (searchBuffer_[0] == '\0') return false;
// //     return token->GetId().find(searchBuffer_) == std::string::npos;
// // }

// // } // namespace DesignSystem





// #include <DesignSystem/UI/TokenEditor.h>
// #include <DesignSystem/Tokens/TokenRegistry.h>
// #include <DesignSystem/Tokens/Token.h>
// #include <DesignSystem/Override/OverrideManager.h>
// #include <DesignSystem/Accessibility/ColorBlindness.h>
// #include <DesignSystem/DesignSystem.h>
// #include <cstring>
// #include <iostream>
// #include <cmath>

// #ifndef M_PI
// #define M_PI 3.14159265358979323846
// #endif

// namespace DesignSystem {

// TokenEditor::TokenEditor() 
//     : selectedThemeIndex_(0),
//       selectedAccessibilityIndex_(0),
//       showPrimitives_(true),
//       showSemantics_(true),
//       showComponents_(true),
//       newOverrideValue_(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)),
//       addingGlobalOverride_(true) {
//     searchBuffer_[0] = '\0';
// }

// void TokenEditor::Render(Context& currentContext, OverrideManager& overrideManager) {
//     DesignSystem::Instance().ApplyGlobalStyle();
    
//     ImGui::Begin("Design System Token Editor", nullptr, ImGuiWindowFlags_MenuBar);
    
//     if (ImGui::BeginMenuBar()) {
//         if (ImGui::BeginMenu("View")) {
//             ImGui::MenuItem("Show Primitives", nullptr, &showPrimitives_);
//             ImGui::MenuItem("Show Semantics", nullptr, &showSemantics_);
//             ImGui::MenuItem("Show Components", nullptr, &showComponents_);
//             ImGui::EndMenu();
//         }
//         ImGui::EndMenuBar();
//     }
    
//     RenderContextSelector(currentContext);
//     ImGui::Separator();
    
//     ImGui::BeginChild("TokenListPane", ImVec2(300, 0), true);
//     RenderTokenList();
//     ImGui::EndChild();
    
//     ImGui::SameLine();
    
//     ImGui::BeginChild("TokenDetailsPane", ImVec2(0, 0), true);
//     if (!selectedTokenId_.empty()) {
//         RenderTokenDetails(currentContext, overrideManager);
//         ImGui::Separator();
//         RenderOverridePanel(currentContext, overrideManager);
//     } else {
//         ImGui::TextWrapped("Select a token from the list to view details");
//     }
//     ImGui::EndChild();
    
//     ImGui::End();
    
//     RenderPreview(currentContext);
// }

// void TokenEditor::RenderContextSelector(Context& currentContext) {
//     ImGui::Text("Current Context:");
    
//     const char* themeNames[] = { "Dark", "Light", "Muted Green", "High Contrast" };
//     int themeIndex = static_cast<int>(currentContext.GetTheme());
//     if (ImGui::Combo("Theme", &themeIndex, themeNames, 4)) {
//         currentContext.SetTheme(static_cast<ThemeType>(themeIndex));
//         DesignSystem::Instance().SetContext(currentContext);
//     }
    
//     ImGui::SameLine();
//     const char* accessNames[] = { "None", "Protanopia", "Deuteranopia", "Tritanopia" };
//     int accessIndex = static_cast<int>(currentContext.GetAccessibility());
//     if (ImGui::Combo("Accessibility", &accessIndex, accessNames, 4)) {
//         currentContext.SetAccessibility(static_cast<AccessibilityType>(accessIndex));
//         DesignSystem::Instance().SetContext(currentContext);
//     }
// }

// void TokenEditor::RenderTokenList() {
//     ImGui::Text("Design Tokens");
//     ImGui::InputTextWithHint("##search", "Search...", searchBuffer_, sizeof(searchBuffer_));
//     ImGui::Separator();
    
//     auto& registry = TokenRegistry::Instance();
    
//     if (showPrimitives_) {
//         if (ImGui::CollapsingHeader("Primitive Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
//             auto primitives = registry.GetTokensByLevel(TokenLevel::Primitive);
//             for (const auto& token : primitives) {
//                 if (IsTokenFiltered(token)) continue;
//                 bool isSelected = (selectedTokenId_ == token->GetId());
//                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
//                     selectedTokenId_ = token->GetId();
//                     InitializeNewOverrideValue(token);
//                 }
//             }
//         }
//     }
    
//     if (showSemantics_) {
//         if (ImGui::CollapsingHeader("Semantic Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
//             auto semantics = registry.GetTokensByLevel(TokenLevel::Semantic);
//             for (const auto& token : semantics) {
//                 if (IsTokenFiltered(token)) continue;
//                 bool isSelected = (selectedTokenId_ == token->GetId());
//                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
//                     selectedTokenId_ = token->GetId();
//                     InitializeNewOverrideValue(token);
//                 }
//             }
//         }
//     }
    
//     if (showComponents_) {
//         if (ImGui::CollapsingHeader("Component Tokens", ImGuiTreeNodeFlags_DefaultOpen)) {
//             auto components = registry.GetTokensByLevel(TokenLevel::Component);
//             for (const auto& token : components) {
//                 if (IsTokenFiltered(token)) continue;
//                 bool isSelected = (selectedTokenId_ == token->GetId());
//                 if (ImGui::Selectable(token->GetId().c_str(), isSelected)) {
//                     selectedTokenId_ = token->GetId();
//                     InitializeNewOverrideValue(token);
//                 }
//             }
//         }
//     }
// }

// void TokenEditor::InitializeNewOverrideValue(std::shared_ptr<Token> token) {
//     auto& ds = DesignSystem::Instance();
    
//     try {
//         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
        
//         switch (resolvedValue.GetType()) {
//             case ValueType::Color:
//                 newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
//                 break;
//             case ValueType::Float:
//                 newOverrideValue_ = TokenValue(0.0f);
//                 break;
//             case ValueType::Int:
//                 newOverrideValue_ = TokenValue(0);
//                 break;
//             case ValueType::Vec2:
//                 newOverrideValue_ = TokenValue(ImVec2(0.0f, 0.0f));
//                 break;
//             case ValueType::Reference:
//                 newOverrideValue_ = TokenValue(std::string(""));
//                 break;
//         }
//     } catch (...) {
//         newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
//     }
// }

// void TokenEditor::RenderTokenDetails(Context& currentContext, OverrideManager& overrideManager) {
//     auto& registry = TokenRegistry::Instance();
//     auto token = registry.GetToken(selectedTokenId_);
//     if (!token) {
//         ImGui::Text("Token not found!");
//         return;
//     }
    
//     ImGui::Text("Token: %s", token->GetId().c_str());
//     ImGui::Text("Level: %s", TokenLevelToString(token->GetLevel()).c_str());
//     ImGui::Text("Type: %s", ValueTypeToString(token->GetValueType()).c_str());
    
//     if (!token->GetDescription().empty()) {
//         ImGui::TextWrapped("Description: %s", token->GetDescription().c_str());
//     }
    
//     ImGui::Separator();
    
//     ImGui::Text("Default Value:");
//     TokenValue defaultValue = token->GetDefaultValue();
    
//     if (defaultValue.IsReference()) {
//         ImGui::Text("  Reference: %s", defaultValue.AsReference().c_str());
//         ImGui::SameLine();
//         ImGui::TextDisabled("=>");
//         ImGui::SameLine();
//         RenderResolvedPreview(defaultValue.AsReference(), currentContext);
//     } else {
//         ImGui::Indent();
//         RenderValuePreview("##defaultPreview", defaultValue, currentContext, true);
//         ImGui::Unindent();
//     }
    
//     ImGui::Separator();
    
//     ImGui::Text("Actual Value:");
//     ImGui::Indent();
//     RenderActualValue(token, currentContext);
//     ImGui::Unindent();
// }

// void TokenEditor::RenderActualValue(std::shared_ptr<Token> token, const Context& currentContext) {
//     auto& ds = DesignSystem::Instance();
    
//     try {
//         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
//         RenderValuePreview("##actualPreview", resolvedValue, currentContext, true);
//     } catch (const std::exception& e) {
//         ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
//     }
// }

// void TokenEditor::RenderResolvedPreview(const std::string& refTokenId, const Context& currentContext) {
//     auto& ds = DesignSystem::Instance();
    
//     try {
//         TokenValue resolvedValue = ds.ResolveTokenValue(refTokenId, currentContext.GetTheme());
//         RenderValuePreview(("##refPreview" + refTokenId).c_str(), resolvedValue, currentContext, false);
//     } catch (...) {
//         ImGui::TextDisabled("(unresolved)");
//     }
// }

// void TokenEditor::RenderValuePreview(const char* label, const TokenValue& value, 
//                                      const Context& currentContext, bool showLabel) {
//     switch (value.GetType()) {
//         case ValueType::Color: {
//             ImVec4 color = value.AsColor();
//             RenderColorPreview(label, color, currentContext);
            
//             if (showLabel) {
//                 ImGui::SameLine();
//                 ImGui::Text("RGB(%d, %d, %d, %d)", 
//                     static_cast<int>(color.x * 255),
//                     static_cast<int>(color.y * 255),
//                     static_cast<int>(color.z * 255),
//                     static_cast<int>(color.w * 255));
//             }
//             break;
//         }
//         case ValueType::Float: {
//             float f = value.AsFloat();
//             RenderFloatPreview(label, f);
//             if (showLabel) {
//                 ImGui::SameLine();
//                 ImGui::Text("%.2f", f);
//             }
//             break;
//         }
//         case ValueType::Int: {
//             int i = value.AsInt();
//             if (showLabel) {
//                 ImGui::Text("%d", i);
//             }
//             break;
//         }
//         case ValueType::Vec2: {
//             ImVec2 vec = value.AsVec2();
//             if (showLabel) {
//                 ImGui::Text("(%.1f, %.1f)", vec.x, vec.y);
//             }
//             break;
//         }
//         case ValueType::Reference: {
//             if (showLabel) {
//                 ImGui::Text("Ref: %s", value.AsReference().c_str());
//             }
//             break;
//         }
//     }
// }

// void TokenEditor::RenderColorPreview(const char* label, const ImVec4& color, const Context& currentContext) {
//     bool hasAccessibility = (currentContext.GetAccessibility() != AccessibilityType::None);
//     ImVec4 transformedColor = color;
    
//     if (hasAccessibility) {
//         transformedColor = ColorBlindness::ApplyColorBlindness(color, currentContext.GetAccessibility());
//     }
    
//     // Récupérer global alpha
//     auto& ds = DesignSystem::Instance();
//     float globalAlpha = 1.0f;
//     try {
//         globalAlpha = ds.GetFloat("semantic.alpha.default");
//     } catch (...) {}
    
//     ImDrawList* drawList = ImGui::GetWindowDrawList();
//     ImVec2 pos = ImGui::GetCursorScreenPos();
//     ImVec2 size(50, 25);
    
//     if (hasAccessibility) {
//         // Split preview avec alpha
//         ImVec4 col1WithAlpha = color;
//         col1WithAlpha.w *= globalAlpha;
//         ImVec4 col2WithAlpha = transformedColor;
//         col2WithAlpha.w *= globalAlpha;
        
//         ImU32 col1 = ImGui::ColorConvertFloat4ToU32(col1WithAlpha);
//         ImU32 col2 = ImGui::ColorConvertFloat4ToU32(col2WithAlpha);
        
//         drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y), col1);
//         drawList->AddRectFilled(ImVec2(pos.x + size.x * 0.5f, pos.y), 
//                                ImVec2(pos.x + size.x, pos.y + size.y), col2);
        
//         ImU32 borderColor = IM_COL32(200, 200, 200, (int)(255 * globalAlpha));
//         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 3.0f);
//     } else {
//         ImVec4 colWithAlpha = color;
//         colWithAlpha.w *= globalAlpha;
//         ImU32 col = ImGui::ColorConvertFloat4ToU32(colWithAlpha);
        
//         drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 3.0f);
        
//         ImU32 borderColor = IM_COL32(200, 200, 200, (int)(255 * globalAlpha));
//         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 3.0f);
//     }
    
//     ImGui::Dummy(size);
    
//     if (ImGui::IsItemHovered()) {
//         ImGui::BeginTooltip();
//         ImGui::Text("Original:");
//         ImGui::Text("  RGB: %d, %d, %d", 
//             static_cast<int>(color.x * 255),
//             static_cast<int>(color.y * 255),
//             static_cast<int>(color.z * 255));
//         ImGui::Text("  Hex: #%02X%02X%02X",
//             static_cast<int>(color.x * 255),
//             static_cast<int>(color.y * 255),
//             static_cast<int>(color.z * 255));
//         ImGui::Text("  Alpha: %.2f (Global: %.2f)", color.w, globalAlpha);
        
//         if (hasAccessibility) {
//             ImGui::Separator();
//             ImGui::Text("Accessibility-corrected:");
//             ImGui::Text("  RGB: %d, %d, %d",
//                 static_cast<int>(transformedColor.x * 255),
//                 static_cast<int>(transformedColor.y * 255),
//                 static_cast<int>(transformedColor.z * 255));
//         }
//         ImGui::EndTooltip();
//     }
// }

// void TokenEditor::RenderFloatPreview(const char* label, float value) {
//     std::string tokenId = selectedTokenId_;
    
//     // Récupérer global alpha
//     auto& ds = DesignSystem::Instance();
//     float globalAlpha = 1.0f;
//     try {
//         globalAlpha = ds.GetFloat("semantic.alpha.default");
//     } catch (...) {}
    
//     ImDrawList* drawList = ImGui::GetWindowDrawList();
//     ImVec2 pos = ImGui::GetCursorScreenPos();
    
//     // Inférer le type de float
//     if (tokenId.find("radius") != std::string::npos) {
//         // RADIUS PREVIEW CORRIGÉ
//         ImVec2 size(50, 50);
        
//         // Couleurs avec alpha global
//         ImU32 borderColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
//         ImU32 fillColor = IM_COL32(255, 0, 255, (int)(50 * globalAlpha));
        
//         float radius = std::min(value, std::min(size.x, size.y) * 0.5f); // Limiter radius
        
//         // Dessiner rectangle arrondi en haut-gauche
//         // Fill
//         drawList->PathLineTo(ImVec2(pos.x + radius, pos.y));
//         drawList->PathArcTo(ImVec2(pos.x + radius, pos.y + radius), radius, -M_PI * 0.5f, 0.0f, 12);
//         drawList->PathLineTo(ImVec2(pos.x + size.x, pos.y + radius));
//         drawList->PathLineTo(ImVec2(pos.x + size.x, pos.y + size.y));
//         drawList->PathLineTo(ImVec2(pos.x, pos.y + size.y));
//         drawList->PathLineTo(ImVec2(pos.x, pos.y + radius));
//         drawList->PathLineTo(ImVec2(pos.x + radius, pos.y));
//         drawList->PathFillConvex(fillColor);
        
//         // Border (arrondi en haut-gauche)
//         drawList->PathArcTo(ImVec2(pos.x + radius, pos.y + radius), radius, -M_PI * 0.5f, 0.0f, 12);
//         drawList->PathStroke(borderColor, 0, 2.0f);
        
//         // Borders droits
//         drawList->AddLine(ImVec2(pos.x + radius, pos.y), ImVec2(pos.x + size.x, pos.y), borderColor, 2.0f);
//         drawList->AddLine(ImVec2(pos.x, pos.y + radius), ImVec2(pos.x, pos.y + size.y), borderColor, 2.0f);
//         drawList->AddLine(ImVec2(pos.x + size.x, pos.y), ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 2.0f);
//         drawList->AddLine(ImVec2(pos.x, pos.y + size.y), ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 2.0f);
        
//         ImGui::Dummy(size);
//     }
//     else if (tokenId.find("spacing") != std::string::npos || tokenId.find("padding") != std::string::npos) {
//         // SPACING PREVIEW
//         ImVec2 size(60, 30);
        
//         ImU32 barColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
//         ImU32 gapColor = IM_COL32(255, 0, 255, (int)(50 * globalAlpha));
        
//         float barWidth = 5.0f;
//         float gap = value;
        
//         drawList->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + size.y), barColor);
//         drawList->AddRectFilled(ImVec2(pos.x + barWidth, pos.y), 
//                                ImVec2(pos.x + barWidth + gap, pos.y + size.y), gapColor);
//         drawList->AddRectFilled(ImVec2(pos.x + barWidth + gap, pos.y), 
//                                ImVec2(pos.x + barWidth + gap + barWidth, pos.y + size.y), barColor);
        
//         ImGui::Dummy(size);
//     }
//     else if (tokenId.find("fontSize") != std::string::npos || tokenId.find("font") != std::string::npos) {
//         // FONT SIZE PREVIEW
//         float scale = value / 14.0f; // Base 14px
//         ImGui::SetWindowFontScale(scale * globalAlpha); // Applique alpha au scale pour visibilité
//         ImGui::Text("Aa");
//         ImGui::SetWindowFontScale(1.0f);
//     }
//     else if (tokenId.find("alpha") != std::string::npos) {
//         // ALPHA PREVIEW
//         ImVec2 size(50, 25);
//         ImU32 color = IM_COL32(255, 255, 255, (int)(255 * value));
        
//         // Damier de fond pour voir la transparence
//         for (int y = 0; y < 25; y += 5) {
//             for (int x = 0; x < 50; x += 5) {
//                 bool checker = ((x / 5) + (y / 5)) % 2 == 0;
//                 ImU32 bgCol = checker ? IM_COL32(200, 200, 200, 255) : IM_COL32(100, 100, 100, 255);
//                 drawList->AddRectFilled(
//                     ImVec2(pos.x + x, pos.y + y),
//                     ImVec2(pos.x + x + 5, pos.y + y + 5),
//                     bgCol
//                 );
//             }
//         }
        
//         drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color);
//         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(200, 200, 200, 255), 0.0f, 0, 2.0f);
        
//         ImGui::Dummy(size);
//     }
//     else if (tokenId.find("scale") != std::string::npos) {
//         // SCALE PREVIEW
//         ImGui::Text("Scale: %.0f%%", value * 100.0f);
//         ImVec2 size(40, 40);
//         float scaledSize = size.x * value;
        
//         ImU32 color = IM_COL32(255, 0, 255, (int)(128 * globalAlpha));
//         drawList->AddRectFilled(pos, ImVec2(pos.x + scaledSize, pos.y + scaledSize), color);
//         drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(200, 200, 200, (int)(100 * globalAlpha)), 0.0f, 0, 1.0f);
        
//         ImGui::Dummy(size);
//     }
//     else {
//         // Default
//         ImGui::Text("%.2f", value);
//     }
// }

// void TokenEditor::RenderOverridePanel(Context& currentContext, OverrideManager& overrideManager) {
//     auto& registry = TokenRegistry::Instance();
//     auto token = registry.GetToken(selectedTokenId_);
//     if (!token) return;
    
//     ImGui::Text("Overrides for this token:");
//     ImGui::Separator();
    
//     auto overrides = overrideManager.GetAllOverrides(selectedTokenId_);
//     auto activeOverride = overrideManager.GetBestOverride(selectedTokenId_, currentContext.GetTheme());
    
//     if (overrides.empty()) {
//         ImGui::TextDisabled("No overrides defined");
//     } else {
//         for (auto* override : overrides) {
//             ImGui::PushID(override);
            
//             std::string label = override->IsGlobal() ? "Global" : 
//                                ThemeTypeToString(*override->GetTheme());
            
//             bool isActive = (override == activeOverride);
//             if (isActive) {
//                 ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ACTIVE] %s:", label.c_str());
//             } else {
//                 ImGui::Text("%s:", label.c_str());
//             }
            
//             ImGui::Indent();
            
//             TokenValue originalValue = override->GetValue();
//             TokenValue editedValue = originalValue;
            
//             bool valueChanged = RenderValueEditor("##overrideEdit", editedValue, token, currentContext);
            
//             if (valueChanged && editedValue != originalValue) {
//                 override->SetValue(editedValue);
//                 DesignSystem::Instance().NotifyOverrideChange();
//             }
            
//             ImGui::SameLine();
            
//             if (ImGui::SmallButton("Remove")) {
//                 if (override->IsGlobal()) {
//                     overrideManager.RemoveGlobalOverride(selectedTokenId_);
//                 } else {
//                     overrideManager.RemoveThemeOverride(selectedTokenId_, *override->GetTheme());
//                 }
//                 DesignSystem::Instance().NotifyOverrideChange();
//                 ImGui::Unindent();
//                 ImGui::PopID();
//                 break;
//             }
            
//             ImGui::Unindent();
//             ImGui::PopID();
//         }
//     }
    
//     ImGui::Separator();
//     ImGui::Text("Add New Override:");
    
//     ImGui::Checkbox("Global Override", &addingGlobalOverride_);
//     if (!addingGlobalOverride_) {
//         ImGui::SameLine();
//         ImGui::Text("(for current theme: %s)", ThemeTypeToString(currentContext.GetTheme()).c_str());
//     }
    
//     RenderValueEditor("##newOverride", newOverrideValue_, token, currentContext);
    
//     ImGui::SameLine();
//     if (ImGui::Button("Add Override")) {
//         if (ValidateOverrideType(newOverrideValue_, token)) {
//             if (addingGlobalOverride_) {
//                 overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_));
//             } else {
//                 overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_, 
//                                                      currentContext.GetTheme()));
//             }
//             DesignSystem::Instance().NotifyOverrideChange();
//         } else {
//             ImGui::SameLine();
//             ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Type mismatch!");
//         }
//     }
// }

// bool TokenEditor::RenderValueEditor(const char* label, TokenValue& value, 
//                                    std::shared_ptr<Token> token, const Context& currentContext) {
//     auto& ds = DesignSystem::Instance();
//     ValueType expectedType;
    
//     try {
//         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
//         expectedType = resolvedValue.GetType();
//     } catch (...) {
//         expectedType = token->GetValueType();
//     }
    
//     if (value.GetType() != expectedType && !value.IsReference()) {
//         switch (expectedType) {
//             case ValueType::Color:
//                 value = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
//                 break;
//             case ValueType::Float:
//                 value = TokenValue(0.0f);
//                 break;
//             case ValueType::Int:
//                 value = TokenValue(0);
//                 break;
//             case ValueType::Vec2:
//                 value = TokenValue(ImVec2(0.0f, 0.0f));
//                 break;
//             default:
//                 break;
//         }
//     }
    
//     bool changed = false;
    
//     switch (value.GetType()) {
//         case ValueType::Color: {
//             ImVec4 color = value.AsColor();
//             if (ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_NoInputs)) {
//                 value.SetColor(color);
//                 changed = true;
//             }
//             break;
//         }
//         case ValueType::Float: {
//             float f = value.AsFloat();
            
//             // Contraintes spécifiques
//             std::string tokenId = token->GetId();
//             if (tokenId.find("radius") != std::string::npos) {
//                 // Radius limité à 12
//                 if (ImGui::SliderFloat(label, &f, 0.0f, 12.0f)) {
//                     value.SetFloat(f);
//                     changed = true;
//                 }
//             } else if (tokenId.find("alpha") != std::string::npos) {
//                 // Alpha entre 0 et 1
//                 if (ImGui::SliderFloat(label, &f, 0.0f, 1.0f)) {
//                     value.SetFloat(f);
//                     changed = true;
//                 }
//             } else if (tokenId.find("scale") != std::string::npos) {
//                 // Scale entre 0.5 et 2
//                 if (ImGui::SliderFloat(label, &f, 0.5f, 2.0f)) {
//                     value.SetFloat(f);
//                     changed = true;
//                 }
//             } else {
//                 // Générique
//                 if (ImGui::DragFloat(label, &f, 0.1f, 0.0f, 1000.0f)) {
//                     value.SetFloat(f);
//                     changed = true;
//                 }
//             }
//             break;
//         }
//         case ValueType::Int: {
//             int i = value.AsInt();
//             if (ImGui::DragInt(label, &i, 1, 0, 1000)) {
//                 value.SetInt(i);
//                 changed = true;
//             }
//             break;
//         }
//         case ValueType::Vec2: {
//             ImVec2 vec = value.AsVec2();
//             if (ImGui::DragFloat2(label, &vec.x, 0.1f)) {
//                 value.SetVec2(vec);
//                 changed = true;
//             }
//             break;
//         }
//         case ValueType::Reference: {
//             std::string ref = value.AsReference();
//             char buffer[256];
//             strncpy(buffer, ref.c_str(), 255);
//             buffer[255] = '\0';
//             if (ImGui::InputText(label, buffer, sizeof(buffer))) {
//                 value.SetReference(std::string(buffer));
//                 changed = true;
//             }
//             break;
//         }
//     }
    
//     return changed;
// }

// bool TokenEditor::ValidateOverrideType(const TokenValue& value, std::shared_ptr<Token> token) {
//     auto& ds = DesignSystem::Instance();
    
//     try {
//         TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
//         return value.GetType() == resolvedValue.GetType() || value.IsReference();
//     } catch (...) {
//         return true;
//     }
// }

// void TokenEditor::RenderPreview(const Context& currentContext) {
//     ImGui::Begin("Theme Preview");
    
//     ImGui::Text("Preview with current context");
//     ImGui::Separator();
    
//     auto& ds = DesignSystem::Instance();
    
//     try {
//         ImVec4 bgColor = ds.GetColor("semantic.color.background");
//         ImVec4 primaryColor = ds.GetColor("semantic.color.primary");
        
//         RenderColorPreview("Background", bgColor, currentContext);
//         ImGui::SameLine();
//         ImGui::Text("Background Color");
        
//         RenderColorPreview("Primary", primaryColor, currentContext);
//         ImGui::SameLine();
//         ImGui::Text("Primary Color");
        
//         ImGui::Separator();
//         ImGui::Text("Sample Components:");
        
//         if (ImGui::Button("Sample Button")) {}
        
//         static char textBuffer[128] = "Sample Input";
//         ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));
        
//     } catch (const std::exception& e) {
//         ImGui::Text("Error resolving tokens: %s", e.what());
//     }
    
//     ImGui::End();
// }

// bool TokenEditor::IsTokenFiltered(std::shared_ptr<Token> token) const {
//     if (searchBuffer_[0] == '\0') return false;
//     return token->GetId().find(searchBuffer_) == std::string::npos;
// }

// } // namespace DesignSystem



#include <UI/TokenEditor.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <DesignSystem/DesignSystem.h>
#include <cstring>
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <Shortcuts/ShortcutManager.h>

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

void TokenEditor::Render(Context& currentContext, OverrideManager& overrideManager) {
    DesignSystem::Instance().ApplyGlobalStyle();
    
    ImGui::Begin("Design System Token Editor", nullptr, ImGuiWindowFlags_MenuBar);
    // ShortcutManager::Instance().RegisterWindowZone("Token Editor", ShortcutZone::TokenEditor);
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Token Editor", Shortcuts::ShortcutZone::TokenEditor);
    
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Primitives", nullptr, &showPrimitives_);
            ImGui::MenuItem("Show Semantics", nullptr, &showSemantics_);
            ImGui::MenuItem("Show Components", nullptr, &showComponents_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    RenderContextSelector(currentContext);
    ImGui::Separator();
    
    ImGui::BeginChild("TokenListPane", ImVec2(300, 0), true);
    RenderTokenList();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    ImGui::BeginChild("TokenDetailsPane", ImVec2(0, 0), true);
    if (!selectedTokenId_.empty()) {
        RenderTokenDetails(currentContext, overrideManager);
        ImGui::Separator();
        RenderOverridePanel(currentContext, overrideManager);
    } else {
        ImGui::TextWrapped("Select a token from the list to view details");
    }
    ImGui::EndChild();
    
    ImGui::End();
    
    RenderPreview(currentContext);
}

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
    auto& ds = DesignSystem::Instance();
    
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
        
        switch (resolvedValue.GetType()) {
            case ValueType::Color:
                newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                break;
            case ValueType::Float:
                newOverrideValue_ = TokenValue(0.0f);
                break;
            case ValueType::Int:
                newOverrideValue_ = TokenValue(0);
                break;
            case ValueType::Vec2:
                newOverrideValue_ = TokenValue(ImVec2(0.0f, 0.0f));
                break;
            case ValueType::Reference:
                newOverrideValue_ = TokenValue(std::string(""));
                break;
        }
    } catch (...) {
        newOverrideValue_ = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
}

void TokenEditor::RenderTokenDetails(Context& currentContext, OverrideManager& overrideManager) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(selectedTokenId_);
    if (!token) {
        ImGui::Text("Token not found!");
        return;
    }
    
    ImGui::Text("Token: %s", token->GetId().c_str());
    ImGui::Text("Level: %s", TokenLevelToString(token->GetLevel()).c_str());
    ImGui::Text("Type: %s", ValueTypeToString(token->GetValueType()).c_str());
    
    if (!token->GetDescription().empty()) {
        ImGui::TextWrapped("Description: %s", token->GetDescription().c_str());
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
    
    ImGui::Text("Actual Value:");
    ImGui::Indent();
    RenderActualValue(token, currentContext);
    ImGui::Unindent();
}

void TokenEditor::RenderActualValue(std::shared_ptr<Token> token, const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
        RenderValuePreview("##actualPreview", resolvedValue, currentContext, true);
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
    }
}

void TokenEditor::RenderResolvedPreview(const std::string& refTokenId, const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(refTokenId, currentContext.GetTheme());
        RenderValuePreview(("##refPreview" + refTokenId).c_str(), resolvedValue, currentContext, false);
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
                    static_cast<int>(color.x * 255),
                    static_cast<int>(color.y * 255),
                    static_cast<int>(color.z * 255),
                    static_cast<int>(color.w * 255));
            }
            break;
        }
        case ValueType::Float: {
            float f = value.AsFloat();
            RenderFloatPreview(label, f);
            if (showLabel) {
                ImGui::SameLine();
                ImGui::Text("%.2f", f);
            }
            break;
        }
        case ValueType::Int: {
            int i = value.AsInt();
            if (showLabel) {
                ImGui::Text("%d", i);
            }
            break;
        }
        case ValueType::Vec2: {
            ImVec2 vec = value.AsVec2();
            if (showLabel) {
                ImGui::Text("(%.1f, %.1f)", vec.x, vec.y);
            }
            break;
        }
        case ValueType::Reference: {
            if (showLabel) {
                ImGui::Text("Ref: %s", value.AsReference().c_str());
            }
            break;
        }
    }
}

void TokenEditor::RenderColorPreview(const char* label, const ImVec4& color, const Context& currentContext) {
    bool hasAccessibility = (currentContext.GetAccessibility() != AccessibilityType::None);
    ImVec4 transformedColor = color;
    
    if (hasAccessibility) {
        transformedColor = ColorBlindness::ApplyColorBlindness(color, currentContext.GetAccessibility());
    }
    
    auto& ds = DesignSystem::Instance();
    float globalAlpha = 1.0f;
    try {
        globalAlpha = ds.GetFloat("semantic.alpha.default");
    } catch (...) {}
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(50, 25);
    
    if (hasAccessibility) {
        ImVec4 col1WithAlpha = color;
        col1WithAlpha.w *= globalAlpha;
        ImVec4 col2WithAlpha = transformedColor;
        col2WithAlpha.w *= globalAlpha;
        
        ImU32 col1 = ImGui::ColorConvertFloat4ToU32(col1WithAlpha);
        ImU32 col2 = ImGui::ColorConvertFloat4ToU32(col2WithAlpha);
        
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y), col1);
        drawList->AddRectFilled(ImVec2(pos.x + size.x * 0.5f, pos.y), 
                               ImVec2(pos.x + size.x, pos.y + size.y), col2);
        
        ImU32 borderColor = IM_COL32(200, 200, 200, (int)(255 * globalAlpha));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 3.0f);
    } else {
        ImVec4 colWithAlpha = color;
        colWithAlpha.w *= globalAlpha;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(colWithAlpha);
        
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, 3.0f);
        
        ImU32 borderColor = IM_COL32(200, 200, 200, (int)(255 * globalAlpha));
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 3.0f);
    }
    
    ImGui::Dummy(size);
    
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Original:");
        ImGui::Text("  RGB: %d, %d, %d", 
            static_cast<int>(color.x * 255),
            static_cast<int>(color.y * 255),
            static_cast<int>(color.z * 255));
        ImGui::Text("  Hex: #%02X%02X%02X",
            static_cast<int>(color.x * 255),
            static_cast<int>(color.y * 255),
            static_cast<int>(color.z * 255));
        ImGui::Text("  Alpha: %.2f (Global: %.2f)", color.w, globalAlpha);
        
        if (hasAccessibility) {
            ImGui::Separator();
            ImGui::Text("Accessibility-corrected:");
            ImGui::Text("  RGB: %d, %d, %d",
                static_cast<int>(transformedColor.x * 255),
                static_cast<int>(transformedColor.y * 255),
                static_cast<int>(transformedColor.z * 255));
        }
        ImGui::EndTooltip();
    }
}

void TokenEditor::RenderFloatPreview(const char* label, float value) {
    std::string tokenId = selectedTokenId_;
    
    auto& ds = DesignSystem::Instance();
    float globalAlpha = 1.0f;
    try {
        globalAlpha = ds.GetFloat("semantic.alpha.default");
    } catch (...) {}
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    
    if (tokenId.find("radius") != std::string::npos) {
        ImVec2 size(50, 50);
    
        ImU32 borderColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
        ImU32 fillColor   = IM_COL32(255, 0, 255, (int)(50 * globalAlpha));
        
        // Clamp radius to ensure it doesn't exceed half the size dimensions
        float maxRadius = std::min(size.x, size.y) * 0.5f;
        float radius = std::min(std::max(0.0f, value), maxRadius);
        float thickness = 1.0f;

        // 1. Background: Draw the filled shape using the native primitive for optimized tessellation
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fillColor, radius, ImDrawFlags_RoundCornersTopLeft);

        // 2. Border: Stroke only the Left and Top edges
        // Note: PathArcTo automatically generates a line segment from the current path 
        // position to the start of the arc, ensuring a seamless join.
        
        // Start at Bottom-Left
        drawList->PathLineTo(ImVec2(pos.x, pos.y + size.y)); 
        
        if (radius > 0.0f) {
            // Draw the Top-Left corner arc
            drawList->PathArcTo(ImVec2(pos.x + radius, pos.y + radius), radius, -M_PI, -M_PI * 0.5f, 12);
        } else {
            // Square corner fallback
            drawList->PathLineTo(ImVec2(pos.x, pos.y));
        }

        // End at Top-Right
        drawList->PathLineTo(ImVec2(pos.x + size.x, pos.y)); 
        
        // Finalize the open stroke (not closed)
        drawList->PathStroke(borderColor, 0, thickness);

        ImGui::Dummy(size);
    }
    else if (tokenId.find("spacing") != std::string::npos || tokenId.find("padding") != std::string::npos) {
        // SPACING PREVIEW
        ImVec2 size(60, 30);
        
        ImU32 barColor = IM_COL32(255, 0, 255, (int)(255 * globalAlpha));
        ImU32 gapColor = IM_COL32(255, 0, 255, (int)(50 * globalAlpha));
        
        float barWidth = 1.0f;
        float gap = value;
        
        drawList->AddRectFilled(pos, ImVec2(pos.x + barWidth, pos.y + size.y), barColor);
        drawList->AddRectFilled(ImVec2(pos.x + barWidth, pos.y), 
                               ImVec2(pos.x + barWidth + gap, pos.y + size.y), gapColor);
        drawList->AddRectFilled(ImVec2(pos.x + barWidth + gap, pos.y), 
                               ImVec2(pos.x + barWidth + gap + barWidth, pos.y + size.y), barColor);
        
        ImGui::Dummy(size);
    }
    else if (tokenId.find("fontSize") != std::string::npos || tokenId.find("font") != std::string::npos) {
        // FONT SIZE PREVIEW
        float scale = value / 14.0f;
        ImGui::SetWindowFontScale(scale);
        ImGui::Text("Aa");
        ImGui::SetWindowFontScale(1.0f);
    }
    else if (tokenId.find("alpha") != std::string::npos) {
        // ALPHA PREVIEW
        ImVec2 size(50, 25);
        ImU32 color = IM_COL32(255, 255, 255, (int)(255 * value));
        
        // Damier de fond
        for (int y = 0; y < 25; y += 5) {
            for (int x = 0; x < 50; x += 5) {
                bool checker = ((x / 5) + (y / 5)) % 2 == 0;
                ImU32 bgCol = checker ? IM_COL32(200, 200, 200, 255) : IM_COL32(100, 100, 100, 255);
                drawList->AddRectFilled(
                    ImVec2(pos.x + x, pos.y + y),
                    ImVec2(pos.x + x + 5, pos.y + y + 5),
                    bgCol
                );
            }
        }
        
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                         IM_COL32(200, 200, 200, 255), 0.0f, 0, 2.0f);
        
        ImGui::Dummy(size);
    }
    else if (tokenId.find("scale") != std::string::npos) {
        // SCALE PREVIEW (sans ScaleAllSizes pour éviter le crash)
        ImGui::Text("Scale: %.0f%%", value * 100.0f);
        ImVec2 size(40, 40);
        float scaledSize = size.x * value;
        
        ImU32 color = IM_COL32(255, 0, 255, (int)(128 * globalAlpha));
        drawList->AddRectFilled(pos, ImVec2(pos.x + scaledSize, pos.y + scaledSize), color);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                         IM_COL32(200, 200, 200, (int)(100 * globalAlpha)), 0.0f, 0, 1.0f);
        
        ImGui::Dummy(size);
    }
    else {
        ImGui::Text("%.2f", value);
    }
}

void TokenEditor::RenderOverridePanel(Context& currentContext, OverrideManager& overrideManager) {
    auto& registry = TokenRegistry::Instance();
    auto token = registry.GetToken(selectedTokenId_);
    if (!token) return;
    
    ImGui::Text("Overrides for this token:");
    ImGui::Separator();
    
    auto overrides = overrideManager.GetAllOverrides(selectedTokenId_);
    auto activeOverride = overrideManager.GetBestOverride(selectedTokenId_, currentContext.GetTheme());
    
    if (overrides.empty()) {
        ImGui::TextDisabled("No overrides defined");
    } else {
        for (auto* override : overrides) {
            ImGui::PushID(override);
            
            std::string label = override->IsGlobal() ? "Global" : 
                               ThemeTypeToString(*override->GetTheme());
            
            bool isActive = (override == activeOverride);
            if (isActive) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ACTIVE] %s:", label.c_str());
            } else {
                ImGui::Text("%s:", label.c_str());
            }
            
            ImGui::Indent();
            
            TokenValue originalValue = override->GetValue();
            TokenValue editedValue = originalValue;
            
            bool valueChanged = RenderValueEditor("##overrideEdit", editedValue, token, currentContext);
            
            if (valueChanged && editedValue != originalValue) {
                override->SetValue(editedValue);
                DesignSystem::Instance().NotifyOverrideChange();
            }
            
            ImGui::SameLine();
            
            if (ImGui::SmallButton("Remove")) {
                if (override->IsGlobal()) {
                    overrideManager.RemoveGlobalOverride(selectedTokenId_);
                } else {
                    overrideManager.RemoveThemeOverride(selectedTokenId_, *override->GetTheme());
                }
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
        ImGui::Text("(for current theme: %s)", ThemeTypeToString(currentContext.GetTheme()).c_str());
    }
    
    RenderValueEditor("##newOverride", newOverrideValue_, token, currentContext);
    
    ImGui::SameLine();
    if (ImGui::Button("Add Override")) {
        if (ValidateOverrideType(newOverrideValue_, token)) {
            if (addingGlobalOverride_) {
                overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_));
            } else {
                overrideManager.AddOverride(Override(selectedTokenId_, newOverrideValue_, 
                                                     currentContext.GetTheme()));
            }
            DesignSystem::Instance().NotifyOverrideChange();
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Type mismatch!");
        }
    }
}

bool TokenEditor::RenderValueEditor(const char* label, TokenValue& value, 
                                   std::shared_ptr<Token> token, const Context& currentContext) {
    auto& ds = DesignSystem::Instance();
    ValueType expectedType;
    
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), currentContext.GetTheme());
        expectedType = resolvedValue.GetType();
    } catch (...) {
        expectedType = token->GetValueType();
    }
    
    if (value.GetType() != expectedType && !value.IsReference()) {
        switch (expectedType) {
            case ValueType::Color:
                value = TokenValue(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                break;
            case ValueType::Float:
                value = TokenValue(0.0f);
                break;
            case ValueType::Int:
                value = TokenValue(0);
                break;
            case ValueType::Vec2:
                value = TokenValue(ImVec2(0.0f, 0.0f));
                break;
            default:
                break;
        }
    }
    
    bool changed = false;
    
    switch (value.GetType()) {
        case ValueType::Color: {
            ImVec4 color = value.AsColor();
            if (ImGui::ColorEdit4(label, &color.x, ImGuiColorEditFlags_NoInputs)) {
                value.SetColor(color);
                changed = true;
            }
            break;
        }
        case ValueType::Float: {
            float f = value.AsFloat();
            
            std::string tokenId = token->GetId();
            if (tokenId.find("radius") != std::string::npos) {
                if (ImGui::SliderFloat(label, &f, 0.0f, 12.0f)) {
                    value.SetFloat(f);
                    changed = true;
                }
            } else if (tokenId.find("alpha") != std::string::npos) {
                if (ImGui::SliderFloat(label, &f, 0.0f, 1.0f)) {
                    value.SetFloat(f);
                    changed = true;
                }
            } else if (tokenId.find("scale") != std::string::npos) {
                // CORRECTION CRASH: On affiche juste le slider sans appliquer ScaleAllSizes
                // ScaleAllSizes sera appliqué seulement au changement de thème, pas en temps réel
                if (ImGui::SliderFloat(label, &f, 0.5f, 2.0f)) {
                    value.SetFloat(f);
                    changed = true;
                }
            } else {
                if (ImGui::DragFloat(label, &f, 0.1f, 0.0f, 1000.0f)) {
                    value.SetFloat(f);
                    changed = true;
                }
            }
            break;
        }
        case ValueType::Int: {
            int i = value.AsInt();
            if (ImGui::DragInt(label, &i, 1, 0, 1000)) {
                value.SetInt(i);
                changed = true;
            }
            break;
        }
        case ValueType::Vec2: {
            ImVec2 vec = value.AsVec2();
            if (ImGui::DragFloat2(label, &vec.x, 0.1f)) {
                value.SetVec2(vec);
                changed = true;
            }
            break;
        }
        case ValueType::Reference: {
            std::string ref = value.AsReference();
            char buffer[256];
            strncpy(buffer, ref.c_str(), 255);
            buffer[255] = '\0';
            if (ImGui::InputText(label, buffer, sizeof(buffer))) {
                value.SetReference(std::string(buffer));
                changed = true;
            }
            break;
        }
    }
    
    return changed;
}

bool TokenEditor::ValidateOverrideType(const TokenValue& value, std::shared_ptr<Token> token) {
    auto& ds = DesignSystem::Instance();
    
    try {
        TokenValue resolvedValue = ds.ResolveTokenValue(token->GetId(), ds.GetCurrentContext().GetTheme());
        return value.GetType() == resolvedValue.GetType() || value.IsReference();
    } catch (...) {
        return true;
    }
}

void TokenEditor::RenderPreview(const Context& currentContext) {
    ImGui::Begin("Theme Preview");
    
    ImGui::Text("Preview with current context");
    ImGui::Separator();
    
    auto& ds = DesignSystem::Instance();
    
    try {
        ImVec4 bgColor = ds.GetColor("semantic.color.background");
        ImVec4 primaryColor = ds.GetColor("semantic.color.primary");
        
        RenderColorPreview("Background", bgColor, currentContext);
        ImGui::SameLine();
        ImGui::Text("Background Color");
        
        RenderColorPreview("Primary", primaryColor, currentContext);
        ImGui::SameLine();
        ImGui::Text("Primary Color");
        
        ImGui::Separator();
        ImGui::Text("Sample Components:");
        
        if (ImGui::Button("Sample Button")) {}
        
        static char textBuffer[128] = "Sample Input";
        ImGui::InputText("##sample", textBuffer, sizeof(textBuffer));
        
    } catch (const std::exception& e) {
        ImGui::Text("Error resolving tokens: %s", e.what());
    }
    
    ImGui::End();
}

bool TokenEditor::IsTokenFiltered(std::shared_ptr<Token> token) const {
    if (searchBuffer_[0] == '\0') return false;
    return token->GetId().find(searchBuffer_) == std::string::npos;
}

} // namespace DesignSystem
#pragma once

#include <DesignSystem/Core/TokenValue.h>
#include <imgui.h>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Shared, typed editors/previews for a single design-token value.
//
//  Extracted from the Settings Customisation page (TokenPropertyRow.cpp) so the
//  exact same widgets — colour picker with Current/Original/Default swatches,
//  constraint-bounded float/int/vec2 drags, easing-curve box, font-family combo
//  — can be reused by the Token Graph window. One source of truth: both the
//  Settings rows and the graph cards call these helpers.
//
//  These functions edit a TokenValue in place; the CALLER is responsible for
//  routing the result to the OverrideManager (AddOverride + NotifyOverrideChange
//  + ApplyGlobalStyle). Numeric editors read the token's effective constraint
//  from DesignSystem, so `tokenId` must be a real token id.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// A typed value editor of a FIXED width (so every editor — colour, slider, vec —
// lines up at the same size). Returns true (and writes `v`) when edited.
// `defaultVal` (for colour types) is shown as a "Default" swatch inside the
// picker popup, below Current/Original. Numeric editors are bounded by the
// token's effective constraint (resolved from `tokenId`).
bool TokenValueEditor(const char* id, DesignSystem::TokenValue& v,
                      const std::string& tokenId, float width,
                      const DesignSystem::TokenValue* defaultVal);

// Small read-only preview of a value (swatch / number / vec / easing box).
// `pvId` must be unique within the window (ColorButton needs a distinct id).
void TokenValuePreview(const char* pvId, const DesignSystem::TokenValue& v);

// Draw a cubic-bezier easing curve (control points cp = {x1,y1,x2,y2}) inside a
// box at the cursor, of the given size, and reserve it in layout.
void DrawBezierBox(const ImVec4& cp, ImVec2 size);

} // namespace UI

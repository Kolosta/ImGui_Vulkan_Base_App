#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Tokens/TokenIds.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Reusable property row for one design token, built on the nested Panel widget.
//
//  Collapsed (header): friendly name + an inline editor for the CURRENTLY active
//  layer (global or current-theme, per `editGlobal`) so the value can be tweaked
//  without expanding. An override badge appears when the token (global or theme)
//  is overridden; clicking it resets BOTH layers.
//
//  Expanded (body): the default (inherited) value shown read-only, then TWO
//  editors laid out side by side — Global (left) and current Theme (right), each
//  writing its own override — followed by the token id and a toggle that reveals
//  the full resolution chain (each link's value).
//
//  All editing goes through DesignSystem's OverrideManager; numeric editors are
//  bounded by the token's effective constraint.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Render one token property row. `idPrefix` disambiguates the ImGui id (e.g. the
// owning panel path). `editGlobal` selects which layer the header's inline
// editor targets (true = global, false = current theme).
void TokenPropertyRow(const char* idPrefix, const char* label,
                      DesignSystem::Tok tok,
                      DesignSystem::Context& ctx, bool editGlobal);

} // namespace UI

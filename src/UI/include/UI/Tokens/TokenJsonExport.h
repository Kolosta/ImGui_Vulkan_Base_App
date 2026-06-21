#pragma once

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Export the live design-token system to a W3C Design Tokens JSON document.
//
//  Follows the Design Tokens Format Module 2025.10 (objects with $value / $type
//  / $description, aliases written as "{group.token}") and the Color Module
//  2025.10 for colour values (an object with colorSpace / components / alpha /
//  hex). Token ids ("primitive.color.gray.25") are rebuilt into the nested group
//  structure the spec expects.
//
//  The Format Module has no native notion of themes, so per-theme values are
//  carried in a vendor extension: $extensions["com.carto.themes"][<ThemeName>].
//  The root $value is the default (Dark) value; the other themes live in the
//  extension. The export reflects the RESOLVED state including any live override
//  (edited values and re-wired references appear as real values/aliases).
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// Build the exhaustive token JSON for ONE theme layer.
//
// `themeIndex` selects the layer to export, in ThemeType order:
//   0 = Dark (the base/default layer), 1 = Light, 2 = MutedGreen, 3 = HighContrast.
//
// For each token the document records, FOR THE CHOSEN THEME, the layer "as
// written": the reference target if the token references another token, else
// the literal value (with overrides applied for parentless first-level tokens
// such as primitives). It also records the token's exact engine type, its
// constraint and its description — so an import reconstructs the token exactly.
std::string ExportTokensJson(int themeIndex);

// Convenience: write the document for `themeIndex` to `path`.
bool WriteTokensJson(const std::string& path, int themeIndex);

} // namespace UI

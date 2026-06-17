#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <DesignSystem/Core/TokenValue.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace DesignSystem {

// ─────────────────────────────────────────────────────────────────────────────
//  ThemeDefinitionStore — the *base theme* layer (NOT user overrides).
//
//  The design system resolves a token in three layers, in this order:
//
//    1. User override  (OverrideManager)   — resettable; flagged in the UI.
//    2. Theme definition (this store)       — the theme's own value for a
//                                             token at a scope; part of the
//                                             theme, authored in code, never
//                                             flagged as an override, and
//                                             what "Reset override" falls
//                                             back to (its OWN scope, not the
//                                             parent scope).
//    3. Global token   (TokenRegistry)      — schema default / theme value /
//                                             reference chain.
//
//  A theme definition entry says: "for theme T, the token `tokenId` resolved
//  inside scope `scope` is V" (V is a concrete value or a reference to
//  another token). This is how a zone gets its own look as part of the theme
//  — e.g. designExample's button color — without being a user override.
//
//  Entries are declared in code (see ThemeDefinitions.cpp) and are NOT
//  serialised: they belong to the theme, not to the user's saved state.
// ─────────────────────────────────────────────────────────────────────────────
class ThemeDefinitionStore {
public:
    /// Define `tokenId` within `scope` (""=global) for `theme`. value may be
    /// a concrete TokenValue or a Reference to another token id.
    void Define(const std::string& tokenId, const std::string& scope,
                ThemeType theme, const TokenValue& value);

    /// Look up the most specific theme definition for (tokenId, scope, theme).
    /// `scope` is walked from the given path up to "" exactly like the
    /// override cascade; returns nullptr if the theme defines nothing for it.
    const TokenValue* Find(const std::string& tokenId,
                           const std::string& scope, ThemeType theme) const;

    /// True if a theme definition exists at *exactly* this scope (no walk) —
    /// used by the editor to tell "theme base" apart from "inherited".
    bool HasExact(const std::string& tokenId, const std::string& scope,
                  ThemeType theme) const;

    /// Value of the theme definition at *exactly* this scope (no walk), or
    /// nullptr. ResolveScoped uses this to interleave override / theme-def
    /// per scope level so a more specific scope always wins, regardless of
    /// which layer supplied it.
    const TokenValue* FindExact(const std::string& tokenId,
                                const std::string& scope,
                                ThemeType theme) const;

    void Clear();

private:
    // Key = "tokenId@scope" (scope omitted when empty), value per theme.
    struct Entry { std::unordered_map<int, TokenValue> byTheme; };
    std::unordered_map<std::string, Entry> entries_;

    static std::string Key(const std::string& tokenId,
                           const std::string& scope);
};

// Populated once at startup (DesignSystem::Initialize). This is where the
// per-zone theme look is authored — replacing the old approach of seeding
// scoped *overrides*.
void InstallThemeDefinitions(ThemeDefinitionStore& store);

} // namespace DesignSystem

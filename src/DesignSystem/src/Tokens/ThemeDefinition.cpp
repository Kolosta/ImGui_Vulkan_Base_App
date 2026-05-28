#include <DesignSystem/Tokens/ThemeDefinition.h>
#include <DesignSystem/Tokens/TokenIds.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Theme definitions — the per-scope *base look* of a theme.
//
//  These are NOT user overrides: they belong to the theme, are authored in
//  code, are never flagged as overrides in the editors, and are what
//  "Reset override" falls back to (the scope's OWN definition, not the
//  parent scope). Resolution order in DesignSystem::ResolveScoped is:
//      user override  >  theme definition  >  global token.
//
//  A definition value is normally a Reference to a token id (so the scope
//  still tracks the theme/primitive palette), e.g. "in designExample, the
//  Button background IS semantic.notice.color.default (warning)".
//
//  The scopes seeded here mirror the App's scope hierarchy (registered in
//  ApplicationInit::RegisterAppScopes). Adding a theme-def at a deeper scope
//  takes precedence over a parent scope's def thanks to the cascade.
// ─────────────────────────────────────────────────────────────────────────────

namespace DesignSystem {

std::string ThemeDefinitionStore::Key(const std::string& tokenId,
                                      const std::string& scope) {
    return scope.empty() ? tokenId : (tokenId + "@" + scope);
}

void ThemeDefinitionStore::Define(const std::string& tokenId,
                                  const std::string& scope, ThemeType theme,
                                  const TokenValue& value) {
    entries_[Key(tokenId, scope)].byTheme[static_cast<int>(theme)] = value;
}

const TokenValue* ThemeDefinitionStore::Find(const std::string& tokenId,
                                             const std::string& scope,
                                             ThemeType theme) const {
    // Walk scope from most specific up to "" (global), exactly like the
    // override cascade, so a zone definition is inherited by sub-scopes
    // until one redefines it.
    std::string s = scope;
    while (true) {
        auto it = entries_.find(Key(tokenId, s));
        if (it != entries_.end()) {
            auto jt = it->second.byTheme.find(static_cast<int>(theme));
            if (jt != it->second.byTheme.end()) return &jt->second;
        }
        if (s.empty()) break;
        std::size_t slash = s.find_last_of('/');
        s = (slash == std::string::npos) ? std::string() : s.substr(0, slash);
        if (slash == std::string::npos && s.empty()) {
            // also try the bare global key once
            auto it2 = entries_.find(Key(tokenId, ""));
            if (it2 != entries_.end()) {
                auto jt2 = it2->second.byTheme.find(static_cast<int>(theme));
                if (jt2 != it2->second.byTheme.end()) return &jt2->second;
            }
            break;
        }
    }
    return nullptr;
}

bool ThemeDefinitionStore::HasExact(const std::string& tokenId,
                                    const std::string& scope,
                                    ThemeType theme) const {
    return FindExact(tokenId, scope, theme) != nullptr;
}

const TokenValue* ThemeDefinitionStore::FindExact(const std::string& tokenId,
                                                  const std::string& scope,
                                                  ThemeType theme) const {
    auto it = entries_.find(Key(tokenId, scope));
    if (it == entries_.end()) return nullptr;
    auto jt = it->second.byTheme.find(static_cast<int>(theme));
    return jt == it->second.byTheme.end() ? nullptr : &jt->second;
}

void ThemeDefinitionStore::Clear() { entries_.clear(); }

// ─────────────────────────────────────────────────────────────────────────────
//  The authored theme definitions.
//
//  Each entry is a reference for every theme, so it stays inside the design
//  system and tracks the palette. A value-typed def is used only when a
//  zone needs a literal (e.g. zero padding for editor canvases).
//
//  ZONES SEEDED HERE:
//
//    editors                 → all editor zones inherit: zero window padding
//                              so the canvas/list/timeline content is flush
//                              with the zone border.
//
//    settings                → recoloured button accent: the Settings window
//                              uses NOTICE (orange) as its Button background.
//                              Demonstrates "a floating menu scope can
//                              restyle any component within it".
//
//    settings/designSystem   → that one tab pushes a stronger frame
//                              background for inputs (visually marks the
//                              token editor as a heavier workspace).
//
//    devTest                 → DevTest floating window itself: muted child
//                              backgrounds, neutral accent.
//    devTest/icons           → Icon Test Lab: text colour bumped to muted so
//                              the lab feels secondary.
//    devTest/design          → existing designExample look (kept compatible).
//    devTest/design/print    → existing print-button look (kept compatible).
//    devTest/themePreview    → kept compatible.
//    devTest/zone1           → existing testZone1 look (kept compatible).
//    devTest/zone1/action    → existing zone1/action look (kept compatible).
//    devTest/zone2           → no defaults; uses global.
//
//  Backwards-compatibility note: a handful of older scope names
//  ("designExample", "designExample/print", "testZone1", "testZone1/action",
//  "themePreview") are still registered & pushed by older sites; their
//  defs are kept identical so the visual is unchanged at those spots, and
//  the new devTest/* names are the canonical organisation going forward.
// ─────────────────────────────────────────────────────────────────────────────
void InstallThemeDefinitions(ThemeDefinitionStore& store) {
    const ThemeType kThemes[] = { ThemeType::Dark, ThemeType::Light,
                                  ThemeType::MutedGreen, ThemeType::HighContrast };

    auto defRef = [&](Tok target, const std::string& scope, Tok refToken) {
        for (ThemeType th : kThemes)
            store.Define(TokIdStr(target), scope, th,
                         TokenValue(TokIdStr(refToken)));
    };
    auto defVal = [&](Tok target, const std::string& scope,
                      const TokenValue& v) {
        for (ThemeType th : kThemes)
            store.Define(TokIdStr(target), scope, th, v);
    };

    // ── editors: flush content, no window padding ──────────────────────────
    defVal(Tok::C_Window_Padding, "editors", TokenValue(ImVec2(0.0f, 0.0f)));

    // ── settings (floating menu): notice-coloured buttons app-wide in tab ──
    //   Proves that a SCOPE (the Settings window) re-themes every Button
    //   inside it, no matter which custom widget renders it.
    defRef(Tok::C_Button_Background,      "settings", Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_BackgroundHover, "settings", Tok::S_Color_Notice_Default);
    // ── settings/designSystem: emphasise input fields ──────────────────────
    defRef(Tok::C_Frame_Background,       "settings/designSystem",
                                                       Tok::S_Color_Background_Layer1);
    // ── settings/shortcuts: bigger accent on capture rows ──────────────────
    defRef(Tok::C_ShortcutRow_BackgroundSelected, "settings/shortcuts",
                                                       Tok::S_Color_Accent_Hover);

    // ── devTest: muted child background to clearly distinguish it ──────────
    defRef(Tok::C_Child_Background, "devTest", Tok::S_Color_Background_Layer1);

    // ── devTest/icons: secondary text accent ───────────────────────────────
    defRef(Tok::S_Color_Text_Default, "devTest/icons", Tok::S_Color_Text_Subtle);

    // ── devTest/design: warning-coloured buttons (kept backwards-compat
    //    with the legacy "designExample" scope) ──────────────────────────────
    defRef(Tok::C_Button_Background,      "devTest/design", Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_BackgroundHover, "devTest/design", Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_Background,      "designExample",  Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_BackgroundHover, "designExample",  Tok::S_Color_Notice_Default);
    // ── devTest/design/print: success-coloured (sub-component) ─────────────
    defRef(Tok::C_Button_Background,      "devTest/design/print", Tok::S_Color_Positive_Default);
    defRef(Tok::C_Button_BackgroundHover, "devTest/design/print", Tok::S_Color_Positive_Default);
    defRef(Tok::C_Button_Background,      "designExample/print",  Tok::S_Color_Positive_Default);
    defRef(Tok::C_Button_BackgroundHover, "designExample/print",  Tok::S_Color_Positive_Default);

    // ── devTest/zone1: elevated child surface + notice text; action button
    //    = danger (kept backwards-compat with legacy "testZone1") ────────────
    defRef(Tok::C_Child_Background,   "devTest/zone1", Tok::S_Color_Background_Layer2);
    defRef(Tok::S_Color_Text_Default, "devTest/zone1", Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_Background,      "devTest/zone1/action", Tok::S_Color_Negative_Default);
    defRef(Tok::C_Button_BackgroundHover, "devTest/zone1/action", Tok::S_Color_Negative_Default);
    defRef(Tok::C_Child_Background,   "testZone1",                Tok::S_Color_Background_Layer2);
    defRef(Tok::S_Color_Text_Default, "testZone1",                Tok::S_Color_Notice_Default);
    defRef(Tok::C_Button_Background,      "testZone1/action",     Tok::S_Color_Negative_Default);
    defRef(Tok::C_Button_BackgroundHover, "testZone1/action",     Tok::S_Color_Negative_Default);

    // ── editors/viewport: accent-tinted child background so the viewport
    //    visibly demonstrates the per-editor scope without looking broken. ──
    defRef(Tok::C_Child_Background, "editors/viewport", Tok::S_Color_Background_Layer1);
    // ── editors/outliner: subtle frame contrast for the tree widget. ───────
    defRef(Tok::C_Frame_Background, "editors/outliner", Tok::S_Color_Background_Layer2);
    // ── editors/timeline: dim background to read time-track separators. ────
    defRef(Tok::C_Child_Background, "editors/timeline", Tok::S_Color_Background_Default);
}

} // namespace DesignSystem

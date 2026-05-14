# Roadmap

This document tracks the project's design intent against what is actually
implemented. Each item is tagged:

- ✅ **Done** — implemented and used by the app
- 🟡 **Partial** — present but not feature-complete versus the spec
- ⬜ **Todo** — not yet implemented

The full specification of the design system, shortcuts and icon system lives
in this file's history (the prompt that created it). Below is the actionable
mapping.

---

## 1. Design System

### 1.1 Token hierarchy

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 1.1.1 | Primitive tokens (single typed value) | ✅ | [Token.h](src/DesignSystem/include/DesignSystem/Tokens/Token.h), [TokenRegistry.cpp](src/DesignSystem/src/Tokens/TokenRegistry.cpp) |
| 1.1.2 | Semantic tokens (reference a primitive or another semantic) | ✅ | Same files. Allowed targets are not enforced at registration time. |
| 1.1.3 | Component tokens (reference primitive/semantic/component **or** direct value) | 🟡 | Storage works; direct-value vs reference is not enforced by level. |
| 1.1.4 | Group tokens (token referencing several other tokens as a bundle) | ⬜ | `TokenLevel` has no `Group` variant. Needs new level + reference list. |
| 1.1.5 | Group override semantics (override *references inside* the group, not the group's shape) | ⬜ | Depends on 1.1.4. |

### 1.2 Typing and constraints

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 1.2.1 | Type tag on the token (`Color/Float/Int/Vec2/Reference`) | ✅ | [TokenType.h](src/DesignSystem/include/DesignSystem/core/TokenType.h) |
| 1.2.2 | Per-token value constraints (min/max/step/intervals/positive/etc.) | ✅ | [ValueConstraint.h](src/DesignSystem/include/DesignSystem/core/ValueConstraint.h) — Float & Int implemented; Color & Vec2 constraints planned |
| 1.2.3 | Constraints enforced on override write | ✅ | `OverrideManager::AddOverride` clamps/validates via the token's constraint |
| 1.2.4 | UI inputs respect constraints (slider min/max, allowed values) | ✅ | `TokenEditor::RenderValueEditor` uses the constraint, not a name heuristic |
| 1.2.5 | Override of a referenced token requires identical type **and** identical constraint | 🟡 | Type checked; constraint identity not yet checked when overriding with a reference |
| 1.2.6 | Constraints for color sub-channels (e.g. R in [0..200]) | ⬜ | Spec hint; not requested by any current token |
| 1.2.7 | Forbid cycles / self-reference | ⬜ | `ResolveTokenValue` will hang or stack-overflow on a cycle. Needs visited-set guard. |

### 1.3 Themes

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 1.3.1 | Theme is a context, not just a colour scheme | ✅ | [Context.h](src/DesignSystem/include/DesignSystem/core/Context.h) |
| 1.3.2 | Default ("none") theme uses the token's `defaultValue_` | ✅ | `Token::GetDefaultValue` |
| 1.3.3 | A theme may override only a subset of tokens | ✅ | `Token::contextValues_` map |
| 1.3.4 | Only one theme active at a time | ✅ | `DesignSystem::currentContext_` |
| 1.3.5 | Theme change refreshes everything immediately, no lag | ✅ | `SetContext` re-applies global style. UI re-reads tokens every frame anyway. |
| 1.3.6 | Import / export of theme to JSON (or readable format) | ⬜ | Only binary persistence (`design_system.bin`) today |
| 1.3.7 | Import is fault-tolerant: unknown tokens ignored, type mismatches ignored | ⬜ | Depends on 1.3.6 |

### 1.4 Computation layers

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 1.4.1 | A computation layer transforms resolved values | 🟡 | Only **accessibility** (colour-blindness matrices). Hard-coded, not pluggable. |
| 1.4.2 | Layer can opt in/out per-token / per-type / globally | ⬜ | Accessibility applies to every colour query |
| 1.4.3 | N stacked layers in defined order | ⬜ | Single fixed layer today |
| 1.4.4 | Toggling a layer updates everything immediately | ✅ | `SetContext` triggers re-apply (in the current single-layer case) |
| 1.4.5 | Layer manager (pluggable list, ordering UI) | ⬜ | Needs a `LayerManager` + registration API |

### 1.5 Overrides

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 1.5.1 | Override stored per-token | ✅ | [OverrideManager.h](src/DesignSystem/include/DesignSystem/Override/OverrideManager.h) |
| 1.5.2 | Two override scopes: global and per-theme | ✅ | `Override::theme_` is `optional<ThemeType>` |
| 1.5.3 | Priority resolution: theme override > global override > token default | ✅ | `OverrideManager::GetBestOverride` + `ResolveTokenValue` |
| 1.5.4 | Override persisted across restarts | ✅ | `design_system.bin` |
| 1.5.5 | Override survives theme switch (still there when you switch back) | ✅ | Theme is part of the override's identity |
| 1.5.6 | Enable / disable an override without deleting it | ⬜ | `Override` has no `enabled_` flag |
| 1.5.7 | Override of a semantic/component token may be a **value** or a **reference** of identical type | ✅ | `Override` stores a `TokenValue` (which can be a reference) |
| 1.5.8 | Override of a group token edits the group's slots, never the targets | ⬜ | Depends on 1.1.4 |

### 1.6 Accessor API

For a token in the current context:

| # | Accessor | Status | Where |
|---|----------|--------|-------|
| 1.6.1 | Id / name / level / value type | ✅ | `Token::GetId/GetLevel/GetValueType` |
| 1.6.2 | Default value (before any override) | ✅ | `Token::GetDefaultValue` |
| 1.6.3 | Final value, current context, with layers | ✅ | `DesignSystem::GetColor/GetFloat/GetInt/GetVec2` |
| 1.6.4 | Final value, current context, without layers | ✅ | `GetColor(id, /*applyAccessibility=*/false)` (colour-only) — generalised under 1.4 |
| 1.6.5 | Value in arbitrary theme (no layer change) | ✅ | `GetColorValue/GetFloatValue/...` take a `Context` |
| 1.6.6 | Value with arbitrary layer set | 🟡 | Possible for accessibility only |
| 1.6.7 | Value with arbitrary theme + layer | 🟡 | Possible because both are inputs to `Get*Value`, limited to the single layer |
| 1.6.8 | Reference chain to primitive (A → B → C → value) | ✅ | `DesignSystem::GetReferenceChain` |
| 1.6.9 | Override info: presence + value, per scope (global, per-theme) | ✅ | `OverrideManager::HasGlobalOverride / HasThemeOverride / GetOverride` |
| 1.6.10 | Description | ✅ | `Token::GetDescription` |
| 1.6.11 | Constraint metadata (for UI) | ✅ | `Token::GetConstraint` |
| 1.6.12 | Live update on token / theme / override / layer change | 🟡 | Works because the UI polls every frame; no observer pattern yet |

### 1.7 Persistence

| # | Item | Status | Where |
|---|------|--------|-------|
| 1.7.1 | Binary persistence of overrides + context | ✅ | [Serialization.h](src/DesignSystem/include/DesignSystem/Persistence/Serialization.h), `design_system.bin` |
| 1.7.2 | Auto-save on every override / context change | ✅ | `DesignSystem::NotifyOverrideChange/SetContext` |
| 1.7.3 | Human-readable theme files for import / export | ⬜ | Needs a JSON serialiser + a `LoadTheme(path)` |

### 1.8 Token Editor panel

| # | Item | Status | Where |
|---|------|--------|-------|
| 1.8.1 | List, search, filter by level | ✅ | `TokenEditor::RenderTokenList` |
| 1.8.2 | Show id / level / type / description | ✅ | `RenderTokenDetails` |
| 1.8.3 | Show default vs current resolved value with previews | ✅ | `RenderActualValue/RenderValuePreview` |
| 1.8.4 | Show constraint (range / step) | ✅ | `RenderTokenDetails` |
| 1.8.5 | Show full reference chain | ✅ | `RenderTokenDetails` |
| 1.8.6 | Show override status by scope (global + per theme) | ✅ | `RenderOverridePanel` summary row |
| 1.8.7 | "Add Override" inputs pre-filled with current value | ✅ | `InitializeNewOverrideValue` |
| 1.8.8 | "Add Override" inputs typed and clamped to constraint | ✅ | `RenderValueEditor` reads constraint |
| 1.8.9 | Enable/disable override toggle | ⬜ | Depends on 1.5.6 |
| 1.8.10 | Edit group token slots | ⬜ | Depends on 1.1.4 |

---

## 2. Shortcuts

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 2.1 | `ShortcutManager` singleton, action + zone model | ✅ | [src/Shortcuts/](src/Shortcuts/) |
| 2.2 | Zones inferred from mouse position (`RegisterWindowZone`) | ✅ | Called from each panel that wants a zone |
| 2.3 | Conflict detection across zones | ✅ | `ShortcutManager` |
| 2.4 | Persistence (`shortcuts.dat`) | ✅ | Same |
| 2.5 | Shortcut editor UI (rebind, list by zone) | ✅ | [ShortcutEditor.cpp](src/UI/src/ShortcutEditor.cpp) |
| 2.6 | Modifier-aware key capture | 🟡 | Single-key + Ctrl/Shift/Alt — function-key/scancode edge cases not exercised |
| 2.7 | Chorded shortcuts (e.g. `Ctrl+K, Ctrl+S`) | ⬜ | One-key bindings only |
| 2.8 | Import / export of binding profiles | ⬜ | Binary file only, not portable |

---

## 3. Vector Graphics / Icons

| # | Item | Status | Where / Notes |
|---|------|--------|---------------|
| 3.1 | Compile-time SVG ingestion → `IconData.h` | ✅ | `src/tools/resvg-bindings/src/icon_compiler_main.rs` |
| 3.2 | Runtime rasterisation via resvg | ✅ | [IconManager.cpp](src/VectorGraphics/src/IconManager.cpp) |
| 3.3 | Per-zone colour metadata, three schemes (Original / Bicolor / Multicolor) | ✅ | [IconMetadata.h](src/VectorGraphics/include/VectorGraphics/IconMetadata.h) |
| 3.4 | Bicolor mode: assign each zone to `primary` or `secondary` token | ✅ | `IconEditorWindow` |
| 3.5 | Per-zone alpha in bicolor (for fades to transparent) | ✅ | `bicolorAlpha` field |
| 3.6 | SVG class-based zone hinting (`ds-primary`, `ds-secondary`, …) | ✅ | `class_token_priority` in compiler |
| 3.7 | `fill-opacity` / `stroke-opacity` baked into zone alpha at compile time | ✅ | `apply_opacity` in compiler |
| 3.8 | LRU + age-based texture cache | ✅ | `IconManager::CleanupOldCacheEntries` |
| 3.9 | Cache invalidation hook when a DS token changes (avoid stale textures) | 🟡 | LRU eventually evicts; no explicit invalidation. Cheap and worth adding. |
| 3.10 | Editor: pick mode, recolour zones, live preview | ✅ | `IconEditorWindow.cpp` |
| 3.11 | Editor: per-zone alpha slider in bicolor | ✅ | Same |
| 3.12 | Editor: save customised icon instances (persistent metadata per use site) | ⬜ | Editor only changes a local preview |
| 3.13 | Texture reuse when only the colour changes (no descriptor-set churn) | ⬜ | Optimization; LRU is enough today |

---

## Priorities for the next iterations

1. **Group tokens** (1.1.4, 1.1.5, 1.5.8, 1.8.10) — the biggest missing concept.
2. **Cycle detection** (1.2.7) — small, contained, prevents real bugs.
3. **Pluggable computation layers** (1.4.1 → 1.4.5) — refactor accessibility into a registered layer first.
4. **JSON theme import / export** (1.3.6, 1.3.7, 1.7.3) — value-add for sharing themes.
5. **Enable / disable an override** (1.5.6, 1.8.9) — small spec gap, easy.
6. **Token-change observer** for the icon cache (3.9).

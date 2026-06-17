# Design tokens — tier rules

The design system is intentionally strict: tokens are strongly typed (a typo
is a *compile error*) and the schema is validated at build time
(completeness, valid references, correct tiering, no cycles). This document
defines **what belongs at which tier** so the system stays logical and
contributors can't misuse it.

The single source of truth for every token is
`src/DesignSystem/src/Tokens/TokenSchema.cpp` (declarations) +
`src/DesignSystem/include/DesignSystem/Tokens/TokenIds.h` (the `Tok` enum and
string ids). Add a token = enumerator + `TokName()` string + a schema row in
the matching group; the build refuses any inconsistency.

## The three tiers

### PRIMITIVE — `P_*` — raw, context-free values

A flat palette / scale value with no meaning attached: a gray, a px step, an
alpha, a rounding step. Primitives may carry **per-theme** values
(`ColorT` / `FloatT`) — this is how whole themes change coherently from one
place. Primitives **never reference** another token; they are concrete.

✅ `primitive.color.gray.800`, `primitive.spacing.8`, `primitive.alpha.60`
❌ a primitive named after a role (`primitive.color.accent`) — that's semantic

### SEMANTIC — `S_*` — theme base values

Two things live here:

1. **Role colors / base UI values** — `semantic.color.{background, surface,
   surfaceElevated, popupBackground, text, textMuted, textDisabled, border,
   borderStrong, accent, accentHover, accentActive, danger, warning, success,
   title*, scrollbar*, …}`. These are *theme base values*, not widgets.
   `windowBg`, `text`, `border` are **semantic, never component**.
2. **Global ImGuiStyle configuration that is not a widget** —
   `semantic.style.*`: item spacing, inner spacing, indent spacing, cell
   padding, grab min size, log-slider deadzone, anti-aliasing, hover delays,
   display padding, tessellation, button/selectable/separator text align,
   tree-line flags/size. These are layout/rendering/behaviour properties, not
   objects, so they are semantic.

Semantics reference primitives or other semantics.

✅ `semantic.color.text → primitive.color.gray.200`
✅ `semantic.style.itemSpacing` (a Vec2, not a widget)
❌ `semantic.color.button` — a button is a widget → component

### COMPONENT — `C_*` — real widgets

A component token is an **identifiable widget** the user perceives as "a
thing", grouping several coherent properties (background / hover / active /
text / border / rounding / padding…). It may have **sub-components**
(ButtonGroup → Buttons; a Card → its bg + internal button/check/input).
Components reference semantic (or primitive, only when no semantic fits, e.g.
a literal transparent table-row bg).

Real widgets in this project: `Window`, `Child`, `Popup`, `Frame`
(inputs/combos/slider track), `Button`, `Tab`, `Header`
(tree node/selectable/menu item), `Scrollbar`, `Slider`, `CheckBox`,
`Separator`, `ResizeGrip`, `Table`, `Image`, `Docking`, `DragDrop` + custom
widgets `KeyCap`, `StatusBar`, `Toggle`/`ButtonGroup`, `IconButton`,
`ShortcutCaptureField`, `ShortcutRow`, `SectionHeader`.

✅ `component.button.{background, backgroundHover, backgroundActive, text}`
✅ `component.window.{background, rounding, borderSize, padding, …}`
❌ `component.color.windowBg` — not a widget, it's a base value → semantic
❌ `component.style.itemSpacing` — not an object → `semantic.style.*`

### The semantic-vs-component test (use this when unsure)

A token is COMPONENT only if it passes **both**:

1. **Name test** — you can point at a screen element and say "*the ___*"
   ("the button", "the window", "the scrollbar"). "the item spacing" /
   "the indentation" fails → semantic.
2. **Grouping test** — it has several properties that belong together
   (bg + hover + active + text + border…). A lone value with no family →
   semantic.

## Resolution layers (how a value is decided at runtime)

`ResolveScoped(token, scope, theme)` resolves in this order:

1. **User override** — `OverrideManager` entry, global or scoped. This is the
   *only* layer flagged as an override in the editors and the only thing
   "Reset override" removes. May be Global (all themes) or current-theme,
   chosen **per property**.
2. **Theme definition** — the theme's own value for a token at a scope
   (`ThemeDefinitionStore`, authored in `ThemeDefinition.cpp`). This is part
   of the **theme**, not a user override: it is never flagged as an override,
   and a Reset of a user override falls back to *this* (the scope's own theme
   value), not the parent scope.
3. **Global token** — the schema default, the per-theme value, then the
   reference chain.

### Scopes (per-zone / per-component cascade)

A scope is a `/`-separated path (`zone/subzone/element`). A scoped value
cascades `token@a/b/c → token@a/b → token@a → token`. Scopes are NOT extra
tokens: they are resolution suffixes, so the typed token set never grows and
an unstyled zone automatically inherits the global look. Zones register
themselves via `DesignSystem::ZoneStyle("scope", "Label")` so the editors can
list every editable scope.

## Constraints & strong typing

- Numeric limits belong **on the token**, not on the widget. A `Reference`
  token inherits the effective constraint of what it resolves to.
- An out-of-range override is **clamped** (`AddOverride`) or **rejected with
  a descriptive error** (`TryAddOverride`). The UI slider reads the effective
  constraint so it can't exceed it.
- Structural correctness (completeness, valid refs, correct tiers, no
  reference cycle) is proven at **compile time** by `static_assert`s in
  `TokenSchema.cpp`. A broken schema fails the build with a precise message.

## Adding a token — checklist

1. Decide the tier with the test above.
2. Add the enumerator to `enum class Tok` (TokenIds.h) in the right group.
3. Add its string in `TokName()` (this is the persisted key — pick it
   carefully, don't rename casually).
4. Add the schema row in `TokenSchema.cpp` in the matching group, with a
   description and an appropriate constraint; reference the correct tier
   (component → semantic/component, semantic → primitive/semantic, primitive
   = concrete).
5. Build. The compile-time validators will reject any mistake.

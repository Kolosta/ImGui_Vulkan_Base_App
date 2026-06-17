# Authoring SVG icons

This project recolors icons **at runtime** from design-system tokens. To make
that work, every icon must follow the contract below. Icons live in
`resources/icons/`; at build time `icon_compiler` parses each SVG and emits
color metadata into the generated `IconData.h`, which `IconManager` uses to
tint and cache the icon as a Vulkan texture.

## TL;DR for new icons

This project's icon set is **unicolor / `primary`**: a single ink color, tinted
to one design token. To add a conformant icon:

1. One visible color only. Author the shape in a neutral ink (e.g. `#E3E3E3`
   or `#000`); the actual displayed color comes from a token at runtime.
2. Put `class="ds-primary"` on every painted element (`path`, `circle`, …).
3. Keep a `viewBox` (square, e.g. `0 0 24 24`). No `width`/`height` units
   needed.
4. No `<style>` blocks, no CSS classes other than the `ds-*` role, no scripts,
   no external references, no embedded rasters.
5. Use `fill` (preferred) with a literal color; avoid `currentColor`.
6. Name the file in lowercase kebab-case describing the concept
   (`chevron-down.svg`, `tool-pen.svg`), placed in the right category folder.

A valid SVG with no class still works (everything defaults to the `primary`
zone) — but annotate explicitly so the assignment is deterministic.

## How color zones work

Every unique color value found in `fill`, `stroke`, or `stop-color` (inline
**or** inside `style="..."`) is grouped into a **color zone**. At runtime each
zone can be remapped to a design-system token (bicolor mode) or to an
arbitrary RGBA value (multicolor mode). A single-color icon therefore has
exactly one zone.

## Semantic role via `class` (recommended)

Add a `class` attribute to declare each element's role. The compiler assigns
the default token using this **priority order** (highest wins when one color
appears on elements with different classes — assignment stays deterministic):

| Priority    | Class names recognised                                   | Token       |
|-------------|----------------------------------------------------------|-------------|
| 1 (highest) | `ds-primary`, `*primary*` (not containing "secondary")   | `primary`   |
| 2           | `ds-secondary`, `*secondary*`                            | `secondary` |
| 3           | `ds-tertiary`, `*tertiary*`                              | `tertiary`  |
| 4           | `*accent*`                                               | `accent`    |
| 5 (fallback)| any other class, or no class                             | `primary`   |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
  <path class="ds-primary" fill="#E3E3E3"
        d="M7 10l5 5 5-5z"/>
</svg>
```

### Bicolor / multicolor icons

Only needed for icons that intentionally use more than one ink (e.g. a logo).
Tag the second ink with `ds-secondary` (and `ds-tertiary`, `*accent*` as
needed):

```svg
<circle class="ds-primary"   fill="#1a73e8" />   <!-- → primary  -->
<circle class="ds-secondary" fill="#ea4335" />   <!-- → secondary -->
```

### Gradient stops

Use the same class conventions on `<stop>` elements:

```svg
<linearGradient id="grad">
  <stop class="ds-primary"   offset="0" stop-color="#1a73e8" />
  <stop class="ds-secondary" offset="1" stop-color="#ea4335" />
</linearGradient>
```

### Transparent / decorative elements

An element that should stay invisible in customized modes can be authored with
alpha 0 (e.g. `fill-opacity:0`). The runtime editor exposes a **Transparent**
option per zone so it can be kept hidden deliberately.

## Supported color syntax

The parser accepts:

- Hex: `#RGB`, `#RRGGBB`, `#RRGGBBAA`
- `rgb(r, g, b)` and `rgba(r, g, b, a)` — `a` may be `0–255` or `0.0–1.0`
- CSS named colors (`red`, `cornflowerblue`, …)

`none` and `url(...)` paint values are ignored (not turned into zones).
`fill-opacity` / `stroke-opacity` are folded into the zone's alpha.

## Do / Don't

**Do**

- Keep one square `viewBox`; design on a 24×24 grid for consistency.
- Use a single `ds-primary` ink for standard UI/tool icons.
- Keep paths simple and merged where possible (fewer elements = fewer zones).

**Don't**

- Don't ship multiple inks for a plain UI icon — it splits it into zones the
  theme can't tint coherently.
- Don't rely on `<style>`/CSS, scripts, filters, masks referencing external
  ids, or embedded bitmaps.
- Don't bake the final UI color into the SVG — the token decides it at
  runtime; the SVG ink is just a placeholder.

## File naming & layout

Icons are grouped by **functional category** under `resources/icons/`
(e.g. `tools/`, `actions/`, `navigation/`, `editor/`, `view/`, `shapes/`).
Use lowercase kebab-case names that describe the concept, not the source asset
(`chevron-down.svg`, not `arrow_drop_down_24dp_E3E3E3_FILL0_wght400.svg`).

The **iconId** used in C++ (`IconManager`, `DrawIcon`, etc.) is the file name
without its extension — `resources/icons/actions/eye.svg` → `"eye"`. Folders are
for organisation only; they are **not** part of the id, so names must be unique
across the whole tree.

## Regenerating `IconData.h`

At build time `icon_compiler` packs every SVG under `resources/icons/` into the
generated `IconData.h` (in the build tree, not committed). The CMake rule that
runs it **depends on the SVG files** (`file(GLOB_RECURSE … CONFIGURE_DEPENDS)`),
so in practice:

- **Normally you do nothing.** Add / edit / remove an SVG, then build as usual
  (IDE F5/F7 or `cmake --build …`); `IconData.h` is regenerated automatically.
  There is **no need** to delete `out/` or do a clean rebuild.
- **Fast path (icons only).** To refresh just the header in a couple of seconds
  without relinking the app — handy while iterating on a new icon — run from the
  **repository root**:

  ```powershell
  # convenience wrapper: auto-detects the newest out/build/<preset> directory
  pwsh tools/rebuild-icons.ps1

  # or invoke the CMake target directly (any working dir; quote the build path)
  cmake --build "out/build/GCC 15.2.0 x86_64-w64-mingw32 (mingw64)" --target generate_icon_data
  ```

  `generate_icon_data` is a standalone CMake target that runs only the icon
  compiler. `tools/rebuild-icons.ps1` is a thin wrapper around it that locates
  the build directory for you (override with `-BuildDir`).

If a freshly added icon still isn't found at runtime, re-run CMake configure once
(`CONFIGURE_DEPENDS` re-globs on the next build, but a brand-new file is only
guaranteed to be seen after a configure).

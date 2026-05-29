# Design Token Taxonomy

## Global Conventions

### Separators

| Separator | Usage | Example |
|---|---|---|
| `.` | Separate hierarchical segments | `color.blue.400` |
| `-` | Compound words within a segment | `line-height`, `kbd-focus`, `extra-bold` |

All tokens are **lowercase**. No camelCase, no underscore.

### Universal Segment Order

```
[namespace].[category].[subcategory?].[variant?].[state?]
```

Fundamental rule: **from most general to most specific**. State is always last.

### Token Naming Philosophy

Token names must be **decoupled from their values**. A token named after its current value (e.g., `font.line-height.150`) becomes misleading the moment the value changes to `1.40`. Names should encode **position in a scale** or **semantic intent**, never a literal value.

---

## Layer 1 — Primitive Tokens

> Source of truth. **Never consumed directly in product code.**
> Structure: `{category}.{subcategory?}.{scale}`

---

### Color

#### Main Palette

```
color.{hue}.{scale}
```

Scales: `100` `200` `300` `400` `500` `600` `700` `800` `900` `1000` `1100` `1200` `1300` `1400` `1500` `1600`

```
color.blue.400
color.blue.800
color.red.600
color.green.200
color.yellow.1000
color.magenta.500
color.purple.300
color.cyan.700
color.orange.400
color.neutral.100
color.neutral.900
```

#### Alpha (transparency variants)

```
color.alpha.{base}.{scale}
```

Bases: `black` `white`
Scales: `25` `50` `75` `100` `200` `300` `400` `500` `600` `700` `800` `900` `1000`

```
color.alpha.white.50
color.alpha.white.200
color.alpha.black.100
color.alpha.black.600
```

> **Why `color.alpha.*` and not `alpha.*`?** The `color` prefix groups all color primitives together in autocomplete. `alpha` is a subcategory of `color`.

#### Static (light/dark mode invariants)

```
color.static.{hue}.{scale}
```

Scales: `400` `600` `800` `900` `1000`

```
color.static.blue.800
color.static.red.600
color.static.white.0        ← pure white, never inverted
color.static.black.1600     ← pure black, never inverted
```

> Data visualization tokens always reference `color.static.*` to remain stable across themes.

---

### Typography

#### Font Family

```
font.family.{variant}
```

```
font.family.sans
font.family.serif
font.family.mono
font.family.cjk
font.family.cjk-serif
```

> **Why `font.family` and not `typeface`?** `font.family` mirrors the target property name and is more readable for developers. `typeface` is a print typography term.

#### Font Weight

```
font.weight.{name}
```

| Token | Weight Value |
|---|---|
| `font.weight.thin` | 100 |
| `font.weight.extra-light` | 200 |
| `font.weight.light` | 300 |
| `font.weight.regular` | 400 |
| `font.weight.medium` | 500 |
| `font.weight.semi-bold` | 600 |
| `font.weight.bold` | 700 |
| `font.weight.extra-bold` | 800 |
| `font.weight.black` | 900 |
| `font.weight.extra-black` | 950 |

#### Font Size (raw values)

```
font.size.{n}
```

The numeric scale is positional, not literal px. Values are defined in the token implementation.

```
font.size.10
font.size.20
font.size.30
font.size.40
font.size.50
font.size.60
font.size.70
font.size.80
font.size.90
font.size.100
font.size.110
font.size.120
font.size.130
font.size.140
font.size.150
font.size.160
font.size.170
font.size.180
font.size.190
```

> Semantic aliases (display, heading, body…) live in Layer 2 and reference these primitives.

#### Typographic Scale Ratio

```
font.scale.ratio     ← e.g. 1.25 (Major Third) or 1.333 (Perfect Fourth)
font.scale.base      ← base font size, typically 16
```

These are used by generation tooling, not consumed directly in rendering.

#### Line-Height

```
font.line-height.{name}
```

Names encode density intent, not literal values. Values are defined in the token implementation.

```
font.line-height.none       ← 1.00 — single-line, no breathing room
font.line-height.tight      ← 1.15 — headings, display text
font.line-height.snug       ← 1.30 — compact body
font.line-height.normal     ← 1.50 — comfortable reading
font.line-height.relaxed    ← 1.70 — open body
font.line-height.loose      ← 2.00 — very airy
```

**CJK variants:**

```
font.line-height.cjk.compact    ← CJK tight
font.line-height.cjk.normal     ← CJK standard
font.line-height.cjk.relaxed    ← CJK airy
```

> **Why `font.line-height.cjk.*` and not `cjk.font.line-height.*`?** CJK is a variant *of* `font.line-height`, not a parent category. The `font` namespace remains the grouping level.

#### Letter Spacing (Tracking)

```
font.tracking.{name}
```

```
font.tracking.tighter    ← −0.05em
font.tracking.tight      ← −0.02em
font.tracking.normal     ← 0
font.tracking.wide       ← 0.05em
font.tracking.wider      ← 0.10em
font.tracking.widest     ← 0.20em
```

#### Style

```
font.style.normal
font.style.italic
```

---

### Spacing

```
spacing.{n}
```

The numeric scale is positional, **not** the literal pixel value. The token name is a stable key; the implementation defines the actual measurement.

```
spacing.0
spacing.100
spacing.200
spacing.300
spacing.400
spacing.500
spacing.600
spacing.700
spacing.800
spacing.900
spacing.1000
spacing.1100
spacing.1200
spacing.1300
spacing.1400
spacing.1500
spacing.1600
spacing.1700
spacing.1800
spacing.1900
spacing.2000
spacing.2100
spacing.2200
spacing.2300
spacing.2400
spacing.2500
spacing.2600
```

> `spacing.*` tokens serve gaps, margins, and padding. `size.*` tokens (same scale) serve component dimensions (width, height). Despite sharing a scale, they are separate namespaces with distinct semantic roles.

```
size.{n}     ← same scale as spacing, for component dimensions only
```

---

### Radius

```
radius.{n}
```

The scale runs from `none` to `full`. Numeric steps are positional, not pixel values.

```
radius.none     ← 0, sharp corners
radius.100
radius.200
radius.300
radius.400
radius.500
radius.600
radius.700
radius.800
radius.900
radius.full     ← maximum rounding (pill / circle)
```

> **`radius.full` implementation note:** In rendering environments where a fixed large value (e.g. 9999px) creates visual artifacts, `radius.full` should be interpreted as `0.5 × min(width, height)` — a renderer-resolved "half the shortest side." Define this as a sentinel value in your token system rather than a literal number.

> **Why `none` and `full` instead of `0` and `1000`?** `none` and `full` express clear semantic extremes. The 100–900 range provides a stable scale that can be revalued without renaming tokens.

---

### Stroke

```
stroke.width.{n}
```

The numeric scale is positional, not the literal pixel weight.

```
stroke.width.100      ← finest line
stroke.width.150      ← mid-step (the period is replaced by a hyphen in a segment name: use `stroke.width.150`)
stroke.width.200
stroke.width.300
stroke.width.400      ← heaviest line
```

---

### Opacity

```
opacity.{n}
```

Scale from `0` (fully transparent) to `1000` (fully opaque), in steps of `100`.

```
opacity.0       ← 0%
opacity.100     ← 10%
opacity.200     ← 20%
opacity.300     ← 30%
opacity.400     ← 40%
opacity.500     ← 50%
opacity.600     ← 60%
opacity.700     ← 70%
opacity.800     ← 80%
opacity.900     ← 90%
opacity.1000    ← 100%
```

---

### Shadow

Shadow is defined as a set of individual property tokens rather than a single composite value. This is appropriate for C++ / custom rendering contexts where each property is consumed independently.

```
shadow.{scale}.x        ← horizontal offset
shadow.{scale}.y        ← vertical offset
shadow.{scale}.blur     ← blur radius
shadow.{scale}.spread   ← spread radius (optional, omit if unsupported)
shadow.{scale}.color    ← shadow color (references a color.alpha.* token)
```

Scales: `100` `200` `300` `400` `500` `600`

```
shadow.100.x
shadow.100.y
shadow.100.blur
shadow.100.color

shadow.200.x
shadow.200.y
shadow.200.blur
shadow.200.color

shadow.300.x
shadow.300.y
shadow.300.blur
shadow.300.spread
shadow.300.color

shadow.400.x
shadow.400.y
shadow.400.blur
shadow.400.spread
shadow.400.color

shadow.500.x
shadow.500.y
shadow.500.blur
shadow.500.spread
shadow.500.color

shadow.600.x
shadow.600.y
shadow.600.blur
shadow.600.spread
shadow.600.color
```

> The scale `100` → `600` maps from the most subtle elevation to the most dramatic. Semantic tokens in Layer 2 alias specific scales to named elevations (xs, s, m, l, xl).

---

### Layer (Z-index)

```
layer.{name}
```

Named by abstract elevation level, not by UI component. Values are defined in the token implementation.

```
layer.ground      ← 0
layer.low         ← 10
layer.mid         ← 100
layer.high        ← 200
layer.veil        ← 300
layer.raised      ← 400
layer.peak        ← 500
layer.critical    ← 600
layer.absolute    ← 9999
```

---

### Animation

#### Duration

```
duration.{name}
```

```
duration.instant    ← 0ms
duration.fast       ← 100ms
duration.normal     ← 200ms
duration.slow       ← 300ms
duration.slower     ← 500ms
duration.slowest    ← 800ms
```

#### Easing

```
easing.{name}
```

```
easing.linear
easing.ease
easing.ease-in
easing.ease-out
easing.ease-in-out
easing.spring           ← for physical interactions
easing.decelerate       ← for entrances
easing.accelerate       ← for exits
easing.overshoot        ← subtle bounce
```

---

### Gradient

```
gradient.angle.{n}      ← primitive angles in degrees
gradient.stop.{n}       ← stop positions: 0, 25, 50, 75, 100
```

Full gradient definitions (color + angle + stops) are composed in Layer 2.

---

## Layer 2 — Semantic Tokens

> Aliases with intent. Consumed by designers and in global rendering.
> Structure: `{property}.{role}.{modifier?}.{state?}`

**Fundamental rule: the property comes first.** This produces natural grouping in autocomplete (`background.*`, `text.*`, `border.*`) and mirrors how developers reason about rendering properties.

---

### Semantic Roles

| Role | Meaning |
|---|---|
| `accent` | Primary brand color, primary action |
| `neutral` | Default UI chrome, structural elements |
| `negative` | Error, danger, destructive action |
| `positive` | Success, confirmation, validation |
| `info` | Contextual information |
| `notice` | Warning, degraded state, attention required |
| `secondary` | Secondary brand color when distinct from `accent` |

**Level modifiers:**

| Modifier | Meaning |
|---|---|
| *(none)* | Default level (medium contrast) |
| `subtle` | Soft level (low contrast, backgrounds, badges). **Only defined for the `default` state.** |

> There is intentionally no `bold` modifier at the semantic layer. High-contrast solid fills are handled directly through the role's default token (which already provides sufficient contrast for primary interactive elements).

**Available states:**

```
default · hover · pressed · active · focus · kbd-focus · disabled · selected · indeterminate · visited
```

> `focus` = any focus mechanism (mouse, touch, keyboard). `kbd-focus` = keyboard-only focus, equivalent to `:focus-visible`. Both can coexist: mouse-clicking a button may trigger `focus` without triggering `kbd-focus`, so no visible ring appears for pointer users. On tab navigation, both states fire.

---

### `disabled` — Role or State?

`disabled` operates at **both abstraction levels** without conflict:

- As a **semantic color token** (`text.color.disabled`, `background.disabled`): defines the color value applied to any disabled element. This is a color role — *what color should something be when disabled?*
- As an **interaction state** on component tokens (`button.background.accent.disabled`): defines *when* to apply that color — i.e., when the element is non-interactive.

The two are related but distinct: the semantic token defines **the value**, the component state defines **the condition**. A component in its `.disabled` state references the semantic `disabled` color tokens.

---

### Background

```
background.{role}.{modifier?}.{state}
```

#### Structural backgrounds (visual hierarchy, no component name)

```
background.base.default         ← lowest layer, application canvas
background.sunken.default       ← recessed below base (inputs, code blocks)
background.raised.default       ← above base (content areas, cards)
background.floating.default     ← high above raised (popovers, dropdowns)
background.dim.default          ← dimming / scrim layer
```

#### Semantic role backgrounds

```
background.accent.subtle.default

background.accent.default
background.accent.hover
background.accent.pressed
background.accent.active
background.accent.focus
background.accent.kbd-focus
background.accent.disabled
background.accent.selected
background.accent.indeterminate

background.neutral.subtle.default

background.neutral.default
background.neutral.hover
background.neutral.pressed
background.neutral.active
background.neutral.focus
background.neutral.kbd-focus
background.neutral.disabled
background.neutral.selected

background.negative.subtle.default

background.negative.default
background.negative.hover
background.negative.pressed
background.negative.active
background.negative.focus
background.negative.kbd-focus
background.negative.disabled
background.negative.selected

background.positive.subtle.default

background.positive.default
background.positive.hover
background.positive.pressed
background.positive.active
background.positive.focus
background.positive.kbd-focus
background.positive.disabled
background.positive.selected

background.info.subtle.default

background.info.default
background.info.hover
background.info.pressed
background.info.focus
background.info.kbd-focus
background.info.disabled

background.notice.subtle.default

background.notice.default
background.notice.hover
background.notice.pressed
background.notice.focus
background.notice.kbd-focus
background.notice.disabled

background.secondary.subtle.default

background.secondary.default
background.secondary.hover
background.secondary.pressed
background.secondary.active
background.secondary.focus
background.secondary.kbd-focus
background.secondary.disabled
background.secondary.selected

background.disabled.default
```

#### Direct color backgrounds (for tagged/categorical UI elements)

These remain stable across brand changes — a blue tag stays blue even if the accent color shifts.


#### Available colors

| Color | Definition |
|---|---|
| `blue` | Luminous hue between cyan and indigo, evoking the depth of sky and sea |
| `brown` | Warm, low-luminance composite of orange, reminiscent of earth and aged wood |
| `celery/lime` | Vivid yellow-green, sharp and radiant in the high-frequency spectrum |
| `chartreuse/jade` | Green continuum from luminous yellow-green to deep, mineral-rich verdant tones |
| `cinamon/sepia` | Warm reddish-brown, echoing oxidized pigments and organic decay |
| `cyan` | Pure spectral hue between green and blue, crisp and aqueous |
| `fuchsia` | Intense purplish-red, highly saturated and perceptually vibrant |
| `gray` | Achromatic equilibrium between black and white, devoid of hue |
| `green` | Balanced hue between blue and yellow, emblematic of chlorophyll and life |
| `indigo` | Deep, contemplative blue-violet at the threshold of visible darkness |
| `magenta` | Non-spectral synthesis of red and blue, vivid and perceptually constructed |
| `orange` | Incandescent hue between red and yellow, warm and energetic |
| `pink` | Diluted red, softened by light, delicate and diffuse |
| `purple` | Harmonic blend of red and blue, rich and resonant |
| `red` | Long-wavelength hue, intense and primal in the visible spectrum |
| `seafoam/teal` | Subtle blue-green range, from pale aqueous tones to dense marine depths |
| `silver` | Luminous gray with metallic reflectance, suggestive of polished matter |
| `transparent-white` | White attenuated by opacity, a veil of light revealing what lies beneath |
| `transparent-black` | Black attenuated by opacity, a soft shadow overlaying underlying forms |
| `turquoise` | Clear blue-green, mineral and luminous, poised between cyan and green |
| `yellow` | High-luminance hue, radiant and solar, near the peak of human visual sensitivity |

Three levels per hue:
- `subtle` — muted tinted background (only `default` state)
- *(default)* — standard direct color background
- `visual` — vivid/saturated, for data visualization use

```
background.blue.subtle.default
background.blue.default
background.blue.hover
background.blue.pressed
background.blue.disabled
background.blue.visual.default

background.magenta.subtle.default
background.magenta.default
background.magenta.hover
background.magenta.visual.default

background.yellow.subtle.default
background.yellow.default
background.yellow.hover
background.yellow.visual.default

background.green.subtle.default
background.green.default
background.green.hover
background.green.visual.default

background.red.subtle.default
background.red.default
background.red.hover
background.red.visual.default

background.purple.subtle.default
background.purple.default
background.purple.hover
background.purple.visual.default

background.orange.subtle.default
background.orange.default
background.orange.hover
background.orange.visual.default

background.cyan.subtle.default
background.cyan.default
background.cyan.hover
background.cyan.visual.default
```

---

### Text Color

> Adding `.color.` disambiguates text color tokens from typographic style tokens (`text.heading.l`, `text.body.m`, etc.) which share the `text.*` namespace.

```
text.color.{role}.{modifier?}.{state}
```

```
text.color.primary.default
text.color.primary.disabled

text.color.secondary.default
text.color.secondary.disabled

text.color.tertiary.default
text.color.tertiary.disabled

text.color.disabled.default

text.color.accent.default
text.color.accent.hover
text.color.accent.disabled

text.color.negative.default
text.color.negative.hover

text.color.positive.default

text.color.info.default

text.color.notice.default

text.color.link.default
text.color.link.hover
text.color.link.visited
text.color.link.active
text.color.link.disabled

text.color.placeholder.default
text.color.placeholder.disabled

text.color.blue.default
text.color.magenta.default
text.color.green.default
text.color.red.default
text.color.yellow.default
text.color.purple.default
text.color.orange.default
text.color.cyan.default
```

---

### Icon Color

Same structure as text color, disambiguated with `.color.` to separate from icon size tokens.

```
icon.color.{role}.{modifier?}.{state}
```

```
icon.color.primary.default
icon.color.primary.disabled

icon.color.secondary.default
icon.color.secondary.disabled

icon.color.tertiary.default

icon.color.disabled.default

icon.color.accent.default
icon.color.accent.hover
icon.color.accent.disabled

icon.color.negative.default
icon.color.negative.hover

icon.color.positive.default

icon.color.info.default

icon.color.notice.default

icon.color.blue.default
icon.color.magenta.default
icon.color.green.default
icon.color.red.default
icon.color.yellow.default
icon.color.purple.default
icon.color.orange.default
icon.color.cyan.default
```

---

### Border Color

No `bold` modifier. `subtle` is only defined for the `default` state.

```
border.color.{role}.{modifier?}.{state}
```

```
border.color.default                  ← standard neutral border
border.color.subtle.default           ← divider, separator (only default)

border.color.accent.default
border.color.accent.hover
border.color.accent.focus
border.color.accent.kbd-focus
border.color.accent.disabled
border.color.accent.selected

border.color.negative.default
border.color.negative.hover
border.color.negative.focus

border.color.positive.default
border.color.positive.focus

border.color.info.default

border.color.notice.default

border.color.focus.default            ← generic focus ring (often = accent)
border.color.disabled.default

border.color.blue.default
border.color.magenta.default
border.color.green.default
border.color.red.default
border.color.yellow.default
border.color.purple.default
border.color.orange.default
border.color.cyan.default
```

### Border Width

```
border.width.thin       ← 1px (default borders, dividers)
border.width.medium     ← 2px (accents, active states)
border.width.thick      ← 3px (underlines, alternate focus)
border.width.focus      ← 2px (focus ring — may equal medium)
```

---

### Data Visualization

All data-viz tokens reference `color.static.*` primitives to remain stable across light and dark themes.

#### Scale types

**Categorical** — unordered, nominal data (e.g. Country, Category, Brand). Values have no inherent order.

```
# 6-color accessible palette (optimized for color vision deficiencies, Okabe-Ito based)
data-viz.categorical.short.1
data-viz.categorical.short.2
data-viz.categorical.short.3
data-viz.categorical.short.4
data-viz.categorical.short.5
data-viz.categorical.short.6

# 12-color extended palette (for larger category sets, continue data-viz.categorical.short.{x} palette)
data-viz.categorical.extended.7
data-viz.categorical.extended.8
data-viz.categorical.extended.9
data-viz.categorical.extended.10
data-viz.categorical.extended.11
data-viz.categorical.extended.12
```

**Ordinal** — ordered, discrete data. Values have a known order (e.g. Rating, Rank, Severity). Has a mode and a median but no true zero.

```
# 9-step ordered palette — use a subset for fewer steps (e.g. steps 1, 3, 5, 7, 9 for 5 categories)
data-viz.ordinal.1      ← lowest / lightest
data-viz.ordinal.2
data-viz.ordinal.3
data-viz.ordinal.4
data-viz.ordinal.5
data-viz.ordinal.6
data-viz.ordinal.7
data-viz.ordinal.8
data-viz.ordinal.9      ← highest / darkest
```

**Sequential** — continuous ordered data with a meaningful progression. Used for interval scales (where differences are quantifiable but zero is arbitrary, e.g. Temperature) and ratio scales (where a true zero exists, e.g. Height, Age). Values go from light (low) to dark (high).

Five palettes, each with 16 stops (100 = lightest → 1600 = darkest). All palettes are perceptually uniform and colorblind-safe.

```
# Viridis (blue-green-yellow, perceptually uniform)
data-viz.sequential.viridis.100
data-viz.sequential.viridis.200
...
data-viz.sequential.viridis.1600

# Magma (black-red-yellow-white)
data-viz.sequential.magma.100
data-viz.sequential.magma.200
...
data-viz.sequential.magma.1600

# Plasma (blue-magenta-yellow)
data-viz.sequential.plasma.100
data-viz.sequential.plasma.200
...
data-viz.sequential.plasma.1600

# Inferno (black-red-orange-yellow)
data-viz.sequential.inferno.100
...
data-viz.sequential.inferno.1600

# Cividis (blue-yellow, optimized for deuteranopia/protanopia)
data-viz.sequential.cividis.100
...
data-viz.sequential.cividis.1600
```

**Diverging** — data with two extremes and a meaningful center or baseline (e.g. positive/negative values, deviation from a norm). Each palette has 16 stops: stops 100–800 form one end, stop 800–900 is the center zone, stops 900–1600 form the other end.

Three palettes, each with 16 stops.

```
# Red–Blue (classic diverging, good for contrast)
data-viz.diverging.rd-bu.100      ← extreme negative
data-viz.diverging.rd-bu.200
...
data-viz.diverging.rd-bu.800      ← near center (negative side)
data-viz.diverging.rd-bu.900      ← near center (positive side)
...
data-viz.diverging.rd-bu.1600     ← extreme positive

# Purple–Green (colorblind-friendly alternative)
data-viz.diverging.pu-gn.100
...
data-viz.diverging.pu-gn.1600

# Brown–Teal (warm/cool, low-saturation friendly)
data-viz.diverging.br-teal.100
...
data-viz.diverging.br-teal.1600
```

#### Supporting tokens

```
data-viz.axis.default           ← axis line and tick color
data-viz.grid.default           ← grid line color
data-viz.label.default          ← axis label and annotation text color
data-viz.highlight.default      ← hover / selected data point highlight
data-viz.background.default     ← chart area background
```

---

### Typography — Semantic

#### Composite text styles (recommended)

Each token encodes the full set of typographic properties (size, weight, line-height, tracking) for a given role.

```
text.display.l
text.display.m
text.display.s

text.heading.xl
text.heading.l
text.heading.m
text.heading.s
text.heading.xs

text.body.l
text.body.m
text.body.s

text.detail.l
text.detail.m
text.detail.s

text.label.l
text.label.m
text.label.s

text.mono.l
text.mono.m
text.mono.s

text.code.m
```

#### Individual typographic properties (when composites are insufficient)

```
font.size.display.l          → ref font.size.190
font.size.heading.xl         → ref font.size.140
font.size.heading.l          → ref font.size.120
font.size.heading.m          → ref font.size.100
font.size.body.m             → ref font.size.60
font.size.detail.s           → ref font.size.30
font.size.label.m            → ref font.size.50

font.weight.display.l        → ref font.weight.bold
font.weight.heading.xl       → ref font.weight.semi-bold
font.weight.heading.m        → ref font.weight.semi-bold
font.weight.body.m           → ref font.weight.regular

font.line-height.display.l   → ref font.line-height.tight
font.line-height.heading.m   → ref font.line-height.snug
font.line-height.body.m      → ref font.line-height.normal
font.line-height.detail.s    → ref font.line-height.normal

font.line-height.cjk.heading.m   → ref font.line-height.cjk.compact
font.line-height.cjk.body.m      → ref font.line-height.cjk.normal

font.tracking.heading.xl     → ref font.tracking.tight
font.tracking.body.m         → ref font.tracking.normal
font.tracking.label.m        → ref font.tracking.wide

font.family.heading          → ref font.family.serif (or sans, per brand)
font.family.body             → ref font.family.sans
font.family.mono             → ref font.family.mono
font.family.cjk              → ref font.family.cjk
```

#### Text block spacing (typographic rhythm)

```
text.margin.top.heading.xl
text.margin.top.heading.l
text.margin.top.heading.m
text.margin.top.body.m
text.margin.bottom.heading.xl
text.margin.bottom.heading.m
text.margin.bottom.body.m
```

---

### Spacing — Semantic

Semantic spacing is split into two sub-layers:

**Sub-layer A — Abstract size scale (t-shirt sizing, role-agnostic):** these tokens are the primary reference; they are decoupled from any context.

**Sub-layer B — Contextual aliases:** these reference Sub-layer A tokens and encode *why* the space is used. Use these in implementation.

#### Sub-layer A — Spacing scale

```
spacing.xxs
spacing.xs
spacing.s
spacing.m
spacing.l
spacing.xl
spacing.2xl
spacing.3xl
spacing.4xl
spacing.5xl
spacing.6xl
```

#### Sub-layer A — Inset (padding) scale

```
inset.xxs
inset.xs
inset.s
inset.m
inset.l
inset.xl
inset.2xl
```

#### Sub-layer A — Gap scale

```
gap.xxs
gap.xs
gap.s
gap.m
gap.l
gap.xl
gap.2xl
```

#### Sub-layer A — Indent

```
indent.1      ← first level
indent.2      ← second level
indent.3      ← third level
```

#### Sub-layer B — Contextual spacing aliases

```
# Between tightly coupled siblings (e.g. icon and its label)
spacing.adjacent     → ref spacing.xxs

# Between elements in the same logical group
spacing.related      → ref spacing.xs

# Between discrete groups of elements
spacing.grouped      → ref spacing.m

# Between independent content blocks
spacing.separated    → ref spacing.xl

# Between major sections or regions
spacing.distant      → ref spacing.4xl
```

#### Sub-layer B — Contextual inset aliases

```
inset.dense        → ref inset.xs    ← dense padding (data-heavy views)
inset.compact      → ref inset.s     ← tight padding
inset.default      → ref inset.m     ← standard padding
inset.comfortable  → ref inset.l     ← spacious padding
inset.relaxed      → ref inset.xl    ← very spacious padding
```

#### Sub-layer B — Contextual gap aliases

```
gap.tight      → ref gap.xxs / xs    ← between inline or very close elements
gap.default    → ref gap.s / m       ← standard flow gap
gap.loose      → ref gap.l / xl      ← airy layout gap
```

---

### Size — Semantic

#### Sub-layer A — Abstract size scale

```
size.xxs
size.xs
size.s
size.m
size.l
size.xl
size.2xl
size.3xl
```

#### Size ratios

```
size.ratio.1-2      ← 50%
size.ratio.1-3      ← 33%
size.ratio.2-3      ← 66%
size.ratio.1-4      ← 25%
size.ratio.3-4      ← 75%
size.ratio.golden   ← 1:1.618
```

> `size.container.*` is intentionally absent — container widths are layout-specific and should be managed at the layout layer, not the token layer.

> Icon and avatar sizes are Component Token aliases that reference `size.*` semantic tokens (see Layer 3).

---

### Radius — Semantic

```
radius.none       ← sharp corners
radius.xs         ← inline badges, tight labels
radius.s          ← inputs, form fields
radius.m          ← buttons, cards
radius.l          ← modals, panels, large surfaces
radius.xl         ← feature cards, wide containers
radius.full       ← pill shape, round avatar
```

---

### Border Width — Semantic

*(Already listed under Border Color above.)*

```
border.width.thin
border.width.medium
border.width.thick
border.width.focus
```

---

### Opacity — Semantic

#### Sub-layer A — Quality-based opacity scale

```
opacity.ghost       → ref opacity.200     ← barely visible, strong transparency
opacity.faint       → ref opacity.300     ← very reduced
opacity.reduced     → ref opacity.400     ← reduced prominence (disabled content)
opacity.moderate    → ref opacity.600     ← half-dim
opacity.strong      → ref opacity.800     ← mostly opaque
opacity.full        → ref opacity.1000    ← fully opaque
```

#### Sub-layer B — Contextual opacity aliases

```
# Dimming layers
opacity.dim.light    → ref opacity.moderate    ← light veil over content
opacity.dim.heavy    → ref opacity.strong      ← strong veil over content

# Interactive state opacity
opacity.content.disabled   → ref opacity.reduced    ← disabled element content
```

> Do not name contextual opacity tokens after specific components (e.g. `opacity.skeleton`, `opacity.overlay`). The sub-layer B tokens above are the contextual entry points; components compose their own opacity from these.

---

### Shadow — Semantic

```
shadow.none
shadow.xs       → ref shadow scale 100     ← flat surface, barely lifted
shadow.s        → ref shadow scale 200     ← cards, inputs
shadow.m        → ref shadow scale 300     ← dropdowns, popovers
shadow.l        → ref shadow scale 400     ← dialogs
shadow.xl       → ref shadow scale 500     ← side panels, drawers
shadow.focus    → ref shadow scale 100 + focus color   ← focus ring shadow
shadow.focus.negative   → focus ring in negative/error state
```

---

### Animation — Semantic

```
animation.duration.enter       → ref duration.fast     ← entrance transitions
animation.duration.exit        → ref duration.fast     ← exit transitions
animation.duration.expand      → ref duration.normal   ← accordion, expand
animation.duration.collapse    → ref duration.normal   ← collapse
animation.duration.page        → ref duration.slow     ← page-level transitions
animation.duration.pulse       → ref duration.slower   ← pulsing / loading rhythms

animation.easing.enter         → ref easing.decelerate
animation.easing.exit          → ref easing.accelerate
animation.easing.interact      → ref easing.spring
animation.easing.move          → ref easing.ease-in-out
```

---

### Gradient — Semantic

Named by visual direction and quality, not by the component that uses them.

```
gradient.upward.light          ← bottom-to-top fade, light variant
gradient.upward.strong         ← bottom-to-top fade, strong variant
gradient.downward.light        ← top-to-bottom fade, light variant
gradient.downward.strong       ← top-to-bottom fade, strong variant
gradient.radial.center         ← radial glow from center
gradient.radial.edge           ← radial vignette from edges
gradient.linear.warm           ← warm-toned linear gradient
gradient.linear.cool           ← cool-toned linear gradient
gradient.pulse.a               ← animated gradient keyframe A (loading rhythms)
gradient.pulse.b               ← animated gradient keyframe B
```

---

## Layer 3 — Component Tokens

> Scoped to a single component. Allows override without touching semantic tokens.
> Structure: `{component}.{element?}.{property}.{variant?}.{state}`

### Naming Rules

1. **Component always comes first** — never `blue.tag.*`, always `tag.*.blue.*`
2. **Element (sub-part)** is omitted when the component is simple or the property is unambiguous
3. **Property** maps to a rendering property (background, color, border, radius, shadow, size, gap, padding…)
4. **Variant** (size, style, color) comes before state
5. **State** is always last

### States

```
default · hover · pressed · active · focus · kbd-focus · disabled · selected
indeterminate · negative · loading · empty · visited
```

---

### Button

```
button.background.accent.default
button.background.accent.hover
button.background.accent.pressed
button.background.accent.focus
button.background.accent.kbd-focus
button.background.accent.disabled

button.background.neutral.default
button.background.neutral.hover
button.background.neutral.pressed
button.background.neutral.disabled

button.background.negative.default
button.background.negative.hover
button.background.negative.pressed
button.background.negative.disabled

button.label.color.accent.default
button.label.color.accent.disabled
button.label.color.neutral.default
button.label.color.neutral.disabled
button.label.color.negative.default

button.icon.color.accent.default
button.icon.color.accent.disabled
button.icon.color.neutral.default

button.border.color.accent.default
button.border.color.neutral.default
button.border.color.focus.default
button.border.color.kbd-focus.default

button.border.radius.default

button.padding.horizontal.s
button.padding.horizontal.m
button.padding.horizontal.l
button.padding.vertical.s
button.padding.vertical.m
button.padding.vertical.l

button.gap.s
button.gap.m
button.gap.l

button.size.height.s
button.size.height.m
button.size.height.l
```

---

### Input / TextField

```
input.background.default
input.background.hover
input.background.focus
input.background.disabled
input.background.negative

input.border.color.default
input.border.color.hover
input.border.color.focus
input.border.color.kbd-focus
input.border.color.negative
input.border.color.positive
input.border.color.disabled
input.border.width.default
input.border.width.focus

input.label.color.default
input.label.color.negative
input.label.color.disabled

input.text.color.default
input.text.color.disabled

input.placeholder.color.default
input.placeholder.color.disabled

input.helper.color.default
input.helper.color.negative
input.helper.color.positive

input.icon.color.default
input.icon.color.negative
input.icon.color.disabled

input.border.radius.default
input.padding.horizontal.m
input.padding.vertical.m
input.gap.m

input.size.height.s        → ref size.xl
input.size.height.m        → ref size.2xl
input.size.height.l        → ref size.3xl
```

---

### Tag / Badge

```
tag.background.accent.default
tag.background.accent.hover
tag.background.neutral.default
tag.background.neutral.hover
tag.background.negative.default
tag.background.positive.default
tag.background.info.default
tag.background.notice.default

tag.background.blue.default
tag.background.blue.subtle.default
tag.background.blue.hover
tag.background.magenta.default
tag.background.magenta.subtle.default
tag.background.yellow.default
tag.background.yellow.subtle.default
tag.background.green.default
tag.background.green.subtle.default
tag.background.red.default
tag.background.red.subtle.default
tag.background.purple.default
tag.background.purple.subtle.default
tag.background.orange.default
tag.background.orange.subtle.default
tag.background.cyan.default
tag.background.cyan.subtle.default

tag.text.color.accent.default
tag.text.color.neutral.default
tag.text.color.negative.default
tag.text.color.positive.default
tag.text.color.blue.default
tag.text.color.magenta.default
tag.text.color.yellow.default
tag.text.color.green.default

tag.border.color.accent.default
tag.border.color.neutral.default
tag.border.color.blue.default
tag.border.color.magenta.default

tag.border.radius.default        → ref radius.xs
tag.padding.horizontal.m
tag.gap.m
tag.size.height.m
```

---

### Menu Item

```
menu-item.background.default
menu-item.background.hover
menu-item.background.pressed
menu-item.background.selected
menu-item.background.selected.hover
menu-item.background.disabled
menu-item.background.negative.default
menu-item.background.negative.hover

menu-item.label.color.default
menu-item.label.color.disabled
menu-item.label.color.negative.default

menu-item.description.color.default
menu-item.description.color.disabled

menu-item.icon.color.default
menu-item.icon.color.disabled
menu-item.icon.color.negative.default

menu-item.shortcut.color.default
menu-item.shortcut.color.disabled

menu-item.border.color.default     ← internal separator

menu-item.padding.horizontal.m
menu-item.gap.m
menu-item.size.height.m
menu-item.size.height.l
```

---

### Avatar

```
avatar.background.default           ← fallback when no image
avatar.background.blue.default
avatar.background.magenta.default
avatar.background.green.default
avatar.background.yellow.default
avatar.background.purple.default
avatar.background.orange.default

avatar.border.color.default
avatar.border.color.focus
avatar.border.color.kbd-focus
avatar.border.width.default

avatar.border.radius.default        → ref radius.full (round) or radius.m (square-ish)

avatar.text.color.default           ← initials

avatar.size.xs      → ref size.xxs
avatar.size.s       → ref size.xs
avatar.size.m       → ref size.s
avatar.size.l       → ref size.m
avatar.size.xl      → ref size.l
```

---

### Icon (component-level)

```
icon.size.xs        → ref size.xxs
icon.size.s         → ref size.xs
icon.size.m         → ref size.s
icon.size.l         → ref size.m
icon.size.xl        → ref size.l
```

> Icon color is consumed directly from `icon.color.*` semantic tokens.

---

### Modal / Dialog

```
modal.background.default
modal.border.color.default
modal.border.radius.default          → ref radius.l
modal.shadow.default                 → ref shadow.l

modal.overlay.background.default     ← scrim behind modal → ref background.dim
modal.overlay.opacity.default        → ref opacity.dim.heavy

modal.header.border.color.default
modal.footer.border.color.default

modal.padding.default
modal.padding.header
modal.padding.footer

modal.size.width.s
modal.size.width.m
modal.size.width.l
```

---

### Tooltip

```
tooltip.background.default
tooltip.text.color.default
tooltip.border.radius.default        → ref radius.s
tooltip.shadow.default               → ref shadow.s
tooltip.padding.horizontal.m
tooltip.padding.vertical.m
tooltip.max-width.default
tooltip.arrow.size.default
```

---

### Checkbox / Radio

```
checkbox.background.default
checkbox.background.hover
checkbox.background.selected.default
checkbox.background.selected.hover
checkbox.background.disabled

checkbox.border.color.default
checkbox.border.color.hover
checkbox.border.color.selected.default
checkbox.border.color.kbd-focus
checkbox.border.color.negative

checkbox.check.color.default           ← check mark color
checkbox.check.color.disabled

checkbox.label.color.default
checkbox.label.color.disabled

checkbox.border.radius.default         → ref radius.none for checkbox, radius.full for radio

checkbox.size.default
checkbox.gap.default                   ← gap between control and label
```

---

### Toggle / Switch

```
toggle.background.off.default
toggle.background.off.hover
toggle.background.on.default
toggle.background.on.hover
toggle.background.disabled

toggle.thumb.color.default
toggle.thumb.color.disabled

toggle.border.color.focus
toggle.border.color.kbd-focus

toggle.size.width.m
toggle.size.height.m
toggle.thumb.size.m
```

---

### Select / Dropdown Trigger

```
select.background.default
select.background.hover
select.background.focus
select.background.disabled
select.background.negative

select.border.color.default
select.border.color.hover
select.border.color.focus
select.border.color.kbd-focus
select.border.color.negative
select.border.color.disabled

select.text.color.default
select.text.color.disabled
select.placeholder.color.default

select.icon.color.default
select.icon.color.disabled

select.border.radius.default
select.padding.horizontal.m
select.padding.vertical.m
select.size.height.m
```

---

## Synthesis — Decisions

| Question | Decision | Reason |
|---|---|---|
| `color.blue.400` or `blue.400`? | `color.blue.400` | Grouping by category in autocomplete |
| `font.family.sans` or `typeface.sans`? | `font.family.sans` | Mirrors the target property; more readable for engineers |
| `font.line-height.cjk.*` or `cjk.font.line-height.*`? | `font.line-height.cjk.*` | CJK is a variant *of* `font.line-height`, not a parent |
| `font.line-height.150` or named? | Named (`tight`, `snug`, `normal`…) | Token names must not reveal values — values are implementation details |
| `space` or `spacing`? | `spacing` | More explicit, avoids abbreviation ambiguity |
| `spacing.4` (= 4px) or `spacing.300`? | `spacing.300` (positional) | Decouples name from value; renaming values doesn't break token names |
| `radius.full` = 9999 or 0.5? | Sentinel value (renderer-resolved) | 9999 causes artifacts in some renderers; `0.5 × min(w, h)` is universally correct |
| `radius.none` + `radius.100–900` + `radius.full`? | Yes | Explicit extremes + stable numeric range — good practice |
| `opacity.0–100` by 10 or `0–1000` by 100? | `0–1000` by 100 | Consistent with color and spacing scales; room for future sub-steps |
| Shadow as composite or separate properties? | Separate (`x`, `y`, `blur`, `spread`, `color`) | Required for C++ / custom rendering; each property consumed independently |
| Layer names by component? | No — universal (`ground`, `low`, `veil`…) | Component-based names break when components are reused at different levels |
| Semantic roles: `error/success/warning/brand`? | `negative/positive/notice/secondary` | Language-agnostic, non-prescriptive intent |
| `on-accent`, `on-inverse`, `inverse` roles? | Removed | Handled at the component level via direct color token references |
| `disabled` — role or state? | Both, at different layers | Semantic color token = the value; component state = the condition |
| `background.page`, `background.surface`? | `background.base`, `background.raised`… | Describes visual relationship, not the UI component |
| `text.primary` vs `text.color.primary`? | `text.color.primary` | Disambiguates from typographic style tokens in the same `text.*` namespace |
| `border.bold`? | Removed | No bold modifier in this system; roles provide sufficient contrast levels |
| `subtle` modifier — when? | Only with `.default` state | Subtle conveys a muted visual; applying it to `hover` etc. is rarely meaningful and creates noise |
| Gradient naming? | By visual quality / direction, not component | `gradient.downward.strong`, not `gradient.hero` |
| Opacity naming? | By visual quality, not component | `opacity.dim.light`, not `opacity.overlay` |
| `background.ghost` button variant? | Removed from component tokens | Ghost is a visual style; implement via transparent background + border tokens directly |
| Data-viz: categorical/ordinal/sequential/diverging? | Yes — four distinct scales | Each maps to a data measurement level with different visual encoding needs |
| Data-viz color references? | Always `color.static.*` | Stable across light and dark themes |
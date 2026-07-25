# Carto

A **design-system demonstrator** built on Dear ImGui (docking) + Vulkan +
SDL3. It showcases a strongly-typed, fully tokenised theming engine with
runtime overrides, per-zone scoped cascades, accessibility simulation, a
Blender-style hierarchical shortcut engine, and runtime-recolorable SVG icons.

## Highlights

- **Strongly-typed design tokens.** Every token is an `enum class` value
  validated *at compile time*: completeness, valid references, correct
  Primitive → Semantic → Component tiering, and absence of reference cycles
  are all enforced by `static_assert` — a malformed token graph fails the
  build with a precise message, before any test runs.
- **Three-tier theming.** Primitive → Semantic → Component tokens, each with a
  default plus per-theme values. Four themes (Dark, Light, Muted Green, High
  Contrast) and three color-vision-deficiency simulations applied at
  resolution time.
- **Scoped cascade.** Any zone, sub-zone or individual component can override
  any token through a `/`-separated scope
  (`token@zone/subzone/element → … → token`) — infinitely deep customisation
  with zero extra tokens and automatic fall-back to the global look.
- **Runtime override editor.** Three tabs: the classic list editor, a
  developer token tree, and a user-facing theme editor organised by area
  (à la Blender) with an explicit Global-vs-Theme override scope, live
  original-vs-actual preview, and constraint-aware sliders.
- **Constraint-enforced typing.** Numeric constraints live on the token (not
  the widget). Out-of-range overrides are clamped or rejected with a
  descriptive error, including for `Reference` tokens (constraint inherited
  from what they resolve to).
- **Hierarchical shortcuts.** A 5-level context (window → editor → region →
  mode → tool) resolves the highest-specificity binding; bindings persist
  across runs.
- **Runtime-recolorable SVG icons.** Icons are rasterized by `resvg` (Rust,
  via C FFI) and tinted from design tokens, cached as Vulkan textures with LRU
  eviction.

## Quick start

**Requirements:** GCC with C++20 support, CMake ≥ 3.22, Ninja, stable Rust,
Vulkan headers/loader plus `glslc`, and FreeType development files. SDL3 and
ImGui are fetched automatically by CMake; the Rust icon tooling builds during
the CMake build.

On Ubuntu 24.04, install the native build dependencies with:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config rustup \
  libfreetype6-dev libvulkan-dev vulkan-tools glslc \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libdecor-0-dev \
  libdrm-dev libgbm-dev libegl1-mesa-dev libgl1-mesa-dev \
  libdbus-1-dev libudev-dev
rustup default stable
```

Build and run:

```bash
cmake --preset default
cmake --build --preset default
./out/build/default/Carto       # Linux
# out/build/default/Carto.exe   # Windows
```

Optional token integrity test:

```bash
cmake --build --preset default --target ds_token_tests
ctest --test-dir out/build/default
```

## Project layout

| Path                       | Subsystem |
|----------------------------|-----------|
| `src/Application/`         | Vulkan/SDL3/ImGui lifecycle, layout, demo panels |
| `src/DesignSystem/`        | Token schema, registry, overrides, resolution, persistence |
| `src/Shortcuts/`           | Hierarchical shortcut + tool engine |
| `src/VectorGraphics/`      | SVG rasterization, icon cache, icon editor |
| `src/UI/`                  | Stateless ImGui editors (token, shortcut, fonts) |
| `src/tools/resvg-bindings/`| Rust `resvg` FFI + `icon_compiler` |
| `resources/`               | Fonts and SVG icons |
| `docs/`                    | Authoring docs (see SVG format) |

## Documentation

- **[CONTRIBUTING.md](CONTRIBUTING.md)** — build, architecture, coding rules,
  token conventions, branching, commits, CI. Read this before contributing.
- **[docs/SVG_FORMAT.md](docs/SVG_FORMAT.md)** — how to author SVG icons so
  they work with the runtime recoloring system.

## License

See repository for license terms.

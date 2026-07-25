# Contributing

Thanks for working on this project. This document is the **single source of
truth** for how we develop: build, architecture, coding rules, branching,
committing and shipping. Everything you need is here — read it fully before
opening a pull request.

## TL;DR

```bash
# one-time: local preset overrides (absolute toolchain/vcpkg paths)
#   CMakeUserPresets.json is gitignored — create it from the template below

# GitFlow: branch off develop, not main
git checkout develop && git pull
git checkout -b <type>/<short-topic>     # e.g. feat/shortcut-chords
# …code…
cmake --preset default && cmake --build --preset default   # must pass
git commit -m "<type>(<scope>): <imperative subject>"
git push -u origin <branch>
gh pr create --base develop               # CI must be green before merge
```

## Build

**Toolchain:** GCC with C++20 support, CMake ≥ 3.22, Ninja, stable Rust,
Vulkan headers/loader plus `glslc`, and FreeType development files. Windows
uses MSYS2 MinGW64 and the Rust target `x86_64-pc-windows-gnu` with a static
CRT; Linux uses the native Rust target. SDL3 and ImGui (docking branch) are
fetched by CMake (`FetchContent`); the Rust `resvg-bindings` crate and the
`icon_compiler` binary are built automatically during the CMake build.
`icon_compiler` pre-processes SVG icons into `IconData.h`.

Ubuntu 24.04 dependencies:

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

### Presets

`CMakePresets.json` is **portable and versioned** — it uses Ninja, resolves
`gcc`/`g++` from `PATH`, and contains no machine-specific paths. Two presets:

| Preset    | Use                                    |
|-----------|----------------------------------------|
| `default` | Local development                      |
| `ci`      | Used by GitHub Actions (do not rename) |

Per-developer absolute paths (explicit MSYS2 compiler, vcpkg toolchain, etc.)
go in **`CMakeUserPresets.json`**, which is **gitignored**. Create it once by
inheriting `default`:

```jsonc
{
  "version": 8,
  "configurePresets": [
    {
      "name": "local",
      "inherits": "default",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "C:/msys64/mingw64/bin/gcc.exe",
        "CMAKE_CXX_COMPILER": "C:/msys64/mingw64/bin/g++.exe"
      }
    }
  ]
}
```

### Commands

```bash
cmake --preset default          # configure
cmake --build --preset default  # build (parallel)
```

The executable lands in `out/build/<preset>/Carto` on Linux and
`out/build/<preset>/Carto.exe` on Windows.

### Token integrity tests

Structural correctness of the design-system token graph (completeness, valid
references, correct tiers, no reference cycle) is **proven at compile time** by
`static_assert`s in `src/DesignSystem/src/Tokens/TokenSchema.cpp` — a broken
schema fails the build with a precise message, no test run needed.

A complementary runtime value-level check is built as the `ds_token_tests`
target (enabled by default, `-DDS_BUILD_TESTS=ON`):

```bash
cmake --build --preset default --target ds_token_tests
ctest --test-dir out/build/default        # runs token_integrity
```

## Architecture

The application is a **design system demonstrator** with five static library
subsystems linked into one executable.

### Application Layer (`src/Application/`)

`Application` owns the Vulkan + SDL3 + ImGui lifecycle:

- `ApplicationInit.cpp` — Vulkan device/swapchain setup, subsystem
  initialization, scoped-theme demo seeding
- `Application.cpp` — main loop: `ProcessEvents() → Update() → Render() → Present()`
- `ApplicationUI.cpp` — main menu bar, toolbar, docking layout
- `ApplicationWindows.cpp` — demo/test content panels

The major subsystems are singletons initialized in `InitializeSubsystems()`.

### DesignSystem (`src/DesignSystem/`)

Token-based theming with three hierarchy levels: **Primitive → Semantic →
Component**. The whole token set is declared once as a compile-time-validated
schema:

- `Tokens/TokenIds.h` — one strongly-typed `enum class Tok` entry per token,
  plus the stable string id used for persistence. Referencing a non-existent
  token is a *compile error*, not a runtime exception.
- `Tokens/TokenSchema.{h,cpp}` — the `constexpr` schema table (default value,
  reference, theme overrides, constraint per token) and the `static_assert`
  validators: completeness, reference validity, tier rules, acyclicity.
- `Tokens/TokenRegistry.cpp` — materialises the schema into the runtime
  registry; declares no token of its own.

Each token stores a default value plus per-theme overrides. A `Context` is a
`(ThemeType, AccessibilityType)` pair; accessibility transformations
(Protanopia, Deuteranopia, Tritanopia) are color-matrix transforms applied at
resolution time, not stored in tokens.

Resolution order: `Override (global > theme-scoped) → Token default →
Reference (recursive)`.

**Scoped cascade.** Per-zone / per-sub-component theming is done by *scoped
resolution*, not by declaring extra tokens. A scope is a `/`-separated path
(`zone/subzone/element`); `ResolveScoped` tries the most specific scoped
override and walks up to the bare global token
(`token@a/b/c → token@a/b → token@a → token`). Scoped overrides live only as
`OverrideManager` entries keyed `tokenId@scope`, so the typed token set never
grows and uncustomised zones automatically fall back to the global value. Wrap
a zone's `BeginChild`/`EndChild` with `DesignSystem::DesignSystem::ZoneStyle`
(RAII, re-entrant — safe to nest).

`DesignSystem` is the public singleton facade. Internally, `TokenRegistry`
stores all tokens, `OverrideManager` manages runtime overrides (and **clamps
or rejects** out-of-constraint values), and `Serialization` persists state to
`design_system.bin`.

### Shortcuts (`src/Shortcuts/`)

Blender-inspired hierarchical shortcut engine. A 5-level context
(window → editor → region → mode → tool) determines which actions are active.
`EventNormalizer` drains ImGui IO into typed events (Press/Release/Click/
Drag×8/Wheel) each frame. `ShortcutManager` resolves the highest-specificity
matching action and persists bindings to `shortcuts.dat` (binary format v3).
`ToolManager` tracks the active tool and feeds the context's `tool` field.

### VectorGraphics (`src/VectorGraphics/`)

SVG icons are rasterized at runtime by `resvg` (Rust, exposed via C FFI in
`src/tools/resvg-bindings/`). `IconManager` maintains an LRU cache of Vulkan
textures keyed on `(iconId, size, colorMetadata)`. Color zones map SVG element
IDs to design tokens, enabling runtime recoloring without re-parsing SVG
files. The SVG authoring contract is in [docs/SVG_FORMAT.md](docs/SVG_FORMAT.md).

### UI (`src/UI/`)

Stateless ImGui components — `TokenEditor` (with Design System / Token Tree /
User Theme tabs), `TokenInspector`, `TokenTreeView`, `UserThemeEditor`,
`ShortcutEditor`, `FontManager` — that render into the docking layout. No
independent state; they read from and write to the singletons above.

## Language

All code, comments, commit messages, and UI-visible text (button labels, menu
items, tooltips, window titles, action names and descriptions, status bar
strings, error messages) **must be in English**. Do not write non-English
strings anywhere in source files.

## Styling — Design System Tokens (CRITICAL)

Any work that touches visual style **must go through design-system tokens**.
Never hard-code colors, sizes, radii, paddings, font scales, or border widths
as literals in UI code.

- Resolve every style value via
  `DesignSystem::Instance().GetColor/GetFloat/GetVec2(...)` — prefer the
  strongly-typed `Tok::` overload so a typo is a compile error.
- Respect the three-tier hierarchy: **Primitive → Semantic → Component**. A
  semantic token may reference another semantic token; a component token may
  reference a semantic or another component token. Add a new token at the
  right tier rather than inlining a value.
- If a needed token does not exist, **create it**: add an enumerator to `Tok`
  (`TokenIds.h`), its string in `TokName()`, and a row in the matching group
  of `TokenSchema.cpp` (with a description and an appropriate constraint). The
  build refuses any inconsistency.
- Numeric constraints belong on the token, not on the widget. A `Reference`
  token inherits the effective constraint of what it resolves to; out-of-range
  overrides are clamped (`AddOverride`) or rejected with a descriptive error
  (`TryAddOverride`).
- Fallback literals are permitted *only* inside the `SafeColor/SafeFloat/
  SafeVec2` try/catch helpers, as the last-resort default if a token is
  missing. The token is still the source of truth.
- New reusable UI widgets must read all of their dimensions/colors from
  tokens so themes stay coherent and overridable at runtime.

## Key Patterns

- **Singletons** via `::Instance()` — `DesignSystem`, `TokenRegistry`,
  `OverrideManager`, `ShortcutManager`, `IconManager`. Initialize in order
  (DesignSystem → Shortcuts → VectorGraphics) because VectorGraphics resolves
  tokens.
- **ImGui docking** is enabled (`IMGUI_ENABLE_DOCKING`). Windows use
  `ImGui::DockSpace` inside a fullscreen, no-decoration host window.
- **Vulkan textures for icons** are uploaded once via a staging command buffer
  and stored as `ImTextureID`. The descriptor pool in `Application` is shared
  (1000 sets max — don't bypass the LRU eviction in `IconManager`).
- **Rust FFI boundary** is in `src/tools/resvg-bindings/src/lib.rs`; the
  generated C header `resvg_c.h` is what C++ includes.

## Persistent State Files

| File                | Managed by                       |
|---------------------|----------------------------------|
| `design_system.bin` | `DesignSystem` / `Serialization` |
| `shortcuts.dat`     | `ShortcutManager`                |
| `imgui.ini`         | ImGui (automatic)                |

These are written to the working directory (binary output dir at runtime).
They are **not tracked by git**.

## Branching — GitFlow

This project follows **GitFlow**, with two long-lived branches and several
kinds of short-lived ones.

### Long-lived branches

| Branch    | Role | Protected | Rule |
|-----------|------|-----------|------|
| `main`    | Production. Every commit is a released, tagged version (`vX.Y.Z`). | yes | No direct pushes. Only `release/*` and `hotfix/*` merge here, each followed by a tag. |
| `develop` | Integration. The always-buildable base for the next release. | yes | No direct pushes. Features/fixes merge here via PR with green CI. |

`develop` is the **default base** for everyday work. `main` only ever moves
forward through a release or a hotfix.

### Short-lived branches (Conventional Branch)

Name every branch `<type>/<short-topic>`, kebab-case:

| Prefix      | Branches from | Merges into | For |
|-------------|---------------|-------------|-----|
| `feat/`     | `develop` | `develop` | A new feature (`feat/shortcut-chords`). |
| `fix/`      | `develop` | `develop` | A bug fix that can wait for the next release. |
| `refactor/` | `develop` | `develop` | Restructuring with no behavior change. |
| `chore/` `docs/` `test/` `build/` | `develop` | `develop` | Tooling, docs, tests, build. |
| `release/x.y.z` | `develop` | `main` **and** `develop` | Stabilising a version before shipping. |
| `hotfix/x.y.z`  | `main`    | `main` **and** `develop` | An urgent production fix. |

Workflow for a normal change:

```bash
git checkout develop && git pull
git checkout -b feat/<short-topic>
# …code + commit (Conventional Commits)…
git push -u origin feat/<short-topic>
gh pr create --base develop
```

- Keep PRs current: merge/rebase `develop` into your branch; don't let it rot.
- Delete the branch after merge (unless follow-up fixes are expected on it).

## Versioning & Releases (SemVer)

Versions follow **Semantic Versioning** — `MAJOR.MINOR.PATCH`, with optional
pre-release suffixes (`-rc.N`). The app version lives in
`src/Application/App/Application.h` (`Application::kVersion`).

- **MAJOR** — incompatible/breaking changes.
- **MINOR** — new, backward-compatible features.
- **PATCH** — backward-compatible bug fixes.

### Cutting a release

```bash
git checkout develop && git pull
git checkout -b release/0.2.0
# bump kVersion to "v0.2.0"; only stabilisation fixes from here on
# (optional) tag release candidates on this branch: git tag v0.2.0-rc.1
gh pr create --base main          # release/* targets main
# after merge into main:
git checkout main && git pull
git tag v0.2.0 && git push origin v0.2.0
# back-merge so develop keeps the release fixes + version bump:
gh pr create --base develop --head release/0.2.0   # or merge locally
```

Tags (`vX.Y.Z`) live **only on `main`** and mark every production release.

### Hotfix (urgent production fix)

```bash
git checkout main && git pull
git checkout -b hotfix/0.2.1
# fix + bump kVersion to "v0.2.1"
gh pr create --base main          # hotfix/* targets main
# after merge: tag v0.2.1 on main, then back-merge into develop
```

## Continuous Integration

`.github/workflows/ci.yml` builds the project on both `windows-latest`
(MSYS2 + Rust + Vulkan SDK) and `ubuntu-latest` (native GCC + Mesa Vulkan).
The Linux job also runs CTest and launches Carto under Xvfb with Mesa's
software Vulkan driver, catching loader and startup regressions.

- Runs automatically on **push to `main` or `develop`**, and on **every PR
  targeting `main`, `develop`, `release/**` or `hotfix/**`**.
- Can be triggered **manually from any branch**: GitHub → *Actions* → *CI* →
  *Run workflow* (`workflow_dispatch`).
- A green CI run is **required** before any PR can be merged into a protected
  branch.

If CI fails, fix the underlying issue on the branch and push again — the PR
re-runs automatically. Do not merge red.

### Branch protection (maintainers)

Both `main` and `develop` are protected: PR required, the Windows and Linux
build status checks must pass, and force-pushes / deletions are disabled.
Configure under **Settings → Branches → Branch protection rules** if not
already applied.

---

## Git Conventions

This project uses **Conventional Commits**. Every commit message must follow:

```
<type>(<scope>): <short description>

[optional body]

[optional footer(s)]
```

### Types

| Type       | Use when |
|------------|----------|
| `feat`     | New feature visible to the user or to other subsystems |
| `fix`      | Bug correction |
| `refactor` | Code restructuring with no behavior change |
| `perf`     | Performance improvement |
| `style`    | Formatting, whitespace only (no logic change) |
| `docs`     | Documentation only (comments, README, this file) |
| `test`     | Adding or updating tests |
| `build`    | Build system (CMake, Cargo.toml, vcpkg manifest) |
| `chore`    | Tooling, config, `.gitignore`, CI — anything that does not touch production code |

### Scopes

Use the subsystem name, lowercase, hyphenated:

| Scope             | Covers |
|-------------------|--------|
| `application`     | `src/Application/` |
| `design-system`   | `src/DesignSystem/` |
| `shortcuts`       | `src/Shortcuts/` |
| `vector-graphics` | `src/VectorGraphics/` |
| `ui`              | `src/UI/` |
| `icon-compiler`   | `src/tools/resvg-bindings/` |
| `assets`          | `resources/` |
| `build`           | CMake files, Cargo manifests |
| `deps`            | Dependency version bumps |

Omit the scope for cross-cutting changes (e.g. a refactor touching three
subsystems at once).

### Rules

- Subject line ≤ 72 characters, **imperative mood**, no period at end —
  "add X", not "added X" or "adds X".
- Body is optional; use it to explain **why**, not what. The diff already
  shows what changed.
- Breaking changes: add `!` after type/scope (`feat(ui)!:`) and include a
  `BREAKING CHANGE:` footer.
- One logical change per commit. Fix A and add B → two commits.
- Do not commit generated files (`IconData.h`, `resvg_c.h`) or runtime state
  (`design_system.bin`, `imgui.ini`, `shortcuts.dat`).
- Do not add machine-generated or tool-attribution trailers to commit
  messages.

### Examples

```
feat(vector-graphics): add per-zone alpha slider in bicolor mode

fix(vector-graphics): prevent VkDescriptorPool exhaustion with LRU eviction

Previously the cache only purged by age (300 frames). Dragging a
design-system color slider creates a new cache key every frame;
without size-based eviction the 1000-set descriptor pool was
exhausted in ~16 s (VK_ERROR_OUT_OF_POOL_MEMORY).

refactor(ui): rebuild TokenEditor with collapsible section layout

feat(icon-compiler): encode fill-opacity and stroke-opacity into zone alpha

fix(assets): add ds- class annotations to logo_carto.svg for bicolor detection

build: upgrade SDL3 FetchContent to last stable release tag

chore: untrack runtime state files (design_system.bin, imgui.ini)
```

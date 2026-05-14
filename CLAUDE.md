# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

**Toolchain:** GCC 15.2.0 (MinGW64), C++20, CMake 3.22+. vcpkg triplet: `x64-mingw-dynamic` at `D:/Projets/Code/libs/vcpkg`.

```bash
# Configure
cmake --preset "GCC 15.2.0 x86_64-w64-mingw32 (mingw64)"

# Build
cmake --build "out/build/GCC 15.2.0 x86_64-w64-mingw32 (mingw64)" --config Debug
```

SDL3 and ImGui (docking branch, enabled but not yet wired up) are fetched via CMake FetchContent. The Rust `resvg-bindings` library is built automatically via `cargo build` during the CMake build (target: `x86_64-pc-windows-gnu`, static CRT). The `icon_compiler` Rust binary pre-processes SVG icons into `IconData.h`.

## Architecture

The application is a **design system demonstrator** with five static library subsystems linked into one executable.

### Application Layer (`src/Application/`)

`Application` owns the Vulkan + SDL3 + ImGui lifecycle:
- `ApplicationInit.cpp` — Vulkan device/swapchain setup, subsystem initialization
- `Application.cpp` — main loop: `ProcessEvents() → Update() → Render() → Present()`
- `ApplicationUI.cpp` — main menu bar, toolbar, docking layout
- `ApplicationWindows.cpp` — demo/test content panels

All three major subsystems are singletons initialized in `InitializeSubsystems()`.

### DesignSystem (`src/DesignSystem/`)

Token-based theming with three hierarchy levels: **Primitive → Semantic → Component**. Each token stores a default value plus per-theme overrides. A `Context` is a `(ThemeType, AccessibilityType)` pair; accessibility transformations (Protanopia, Deuteranopia, Tritanopia) are color-matrix transforms applied at resolution time, not stored in tokens.

Resolution order: `Override (global > theme-scoped) → Token default → Reference (recursive)`.

`DesignSystem` is the public singleton facade. Internally, `TokenRegistry` stores all tokens, `OverrideManager` manages runtime overrides, and `Serialization` persists state to `design_system.bin`.

### Shortcuts (`src/Shortcuts/`)

Zone-based keyboard management. Each action belongs to a `ShortcutZone` (spatial region inferred from mouse position). `ShortcutManager` handles conflict detection and persists bindings to `shortcuts.dat`.

### VectorGraphics (`src/VectorGraphics/`)

SVG icons are rasterized at runtime by `resvg` (Rust, exposed via C FFI in `src/tools/resvg-bindings/`). `IconManager` maintains an LRU cache of Vulkan textures keyed on `(iconId, size, colorMetadata)`. Color zones map SVG element IDs to design tokens, enabling runtime recoloring without re-parsing SVG files.

### UI (`src/UI/`)

Stateless ImGui components — `TokenEditor`, `ShortcutEditor`, `FontManager` — that render into the docking layout. No independent state; they read from and write to the singletons above.

## Key Patterns

- **Singletons** via `::Instance()` — `DesignSystem`, `TokenRegistry`, `OverrideManager`, `ShortcutManager`, `IconManager`. Initialize in order (DesignSystem → Shortcuts → VectorGraphics) because VectorGraphics resolves tokens.
- **ImGui docking** is enabled (`IMGUI_ENABLE_DOCKING`). Windows use `ImGui::DockSpace` inside a fullscreen, no-decoration host window.
- **Vulkan textures for icons** are uploaded once via a staging command buffer and stored as `ImTextureID`. The descriptor pool in `Application` is shared (1000 sets max — don't bypass the LRU eviction in `IconManager`).
- **Rust FFI boundary** is in `src/tools/resvg-bindings/src/lib.rs`; the generated C header `resvg_c.h` is what C++ includes.

## Persistent State Files

| File | Managed by |
|------|-----------|
| `design_system.bin` | `DesignSystem` / `Serialization` |
| `shortcuts.dat` | `ShortcutManager` |
| `imgui.ini` | ImGui (automatic) |

These are written to the working directory (binary output dir at runtime). They are **not tracked by git**.

---

## Git Conventions

This project uses **Conventional Commits**. Every commit message must follow:

```
<type>(<scope>): <short description>

[optional body]

[optional footer(s)]
```

### Types

| Type | Use when |
|------|----------|
| `feat` | New feature visible to the user or to other subsystems |
| `fix` | Bug correction |
| `refactor` | Code restructuring with no behavior change |
| `perf` | Performance improvement |
| `style` | Formatting, whitespace only (no logic change) |
| `docs` | Documentation only (comments, CLAUDE.md, README) |
| `test` | Adding or updating tests |
| `build` | Build system (CMake, Cargo.toml, vcpkg manifest) |
| `chore` | Tooling, config, `.gitignore`, CI — anything that does not touch production code |

### Scopes

Use the subsystem name, lowercase, hyphenated:

| Scope | Covers |
|-------|--------|
| `application` | `src/Application/` |
| `design-system` | `src/DesignSystem/` |
| `shortcuts` | `src/Shortcuts/` |
| `vector-graphics` | `src/VectorGraphics/` |
| `ui` | `src/UI/` |
| `icon-compiler` | `src/tools/resvg-bindings/` |
| `assets` | `resources/` |
| `build` | CMake files, Cargo manifests |
| `deps` | Dependency version bumps |

Omit the scope for cross-cutting changes (e.g. a refactor touching three subsystems at once).

### Rules

- Subject line ≤ 72 characters, **imperative mood**, no period at end — "add X", not "added X" or "adds X".
- Body is optional; use it to explain **why**, not what. The diff already shows what changed.
- Breaking changes: add `!` after type/scope (`feat(ui)!:`) and include a `BREAKING CHANGE:` footer.
- One logical change per commit. Fix A and add B → two commits.
- Do not commit generated files (`IconData.h`, `resvg_c.h`) or runtime state (`design_system.bin`, `imgui.ini`, `shortcuts.dat`).

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

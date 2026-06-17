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

Icons: a normal build regenerates `IconData.h` automatically when any
`resources/icons/**` SVG changes (no clean needed). For the icons-only fast path
(`tools/rebuild-icons.ps1` / the `generate_icon_data` target) and the SVG
authoring contract, see `docs/SVG_FORMAT.md`.

## Architecture

The application is a vector graphics editor (built on a design-system foundation) with six static library subsystems linked into one executable. The vector document is rendered **exclusively in Vulkan** (`src/Renderer/`); ImGui drives only the application interface.

### Application Layer (`src/Application/`)

Organised by responsibility (not "everything in Core"). `Application` owns the
Vulkan + SDL3 + ImGui lifecycle; its methods are split across folders by concern:

- `App/` — technical core: `Application.cpp` (main loop: `ProcessEvents() → Update()
  → Render() → Present()`), `ApplicationInit.cpp` (Vulkan device/swapchain setup,
  subsystem init, the borderless hit-test), `SecondaryWindow.*` (detached OS window).
- `Chrome/` — window chrome: `TitleBar.cpp` (custom borderless title bar + window
  ops), `Splash.cpp` (start screen / About), `MainUI.cpp` (toolbar + docking layout
  host + status-bar host).
- `Editors/<Editor>/` — one folder per editor, each with its own complexity:
  `Viewport/` (`Viewport.cpp` canvas + `ViewportTools.cpp` drawing-tool state machine
  + `EditMode.cpp` vertex/edge/handle editing), `Outliner/` (object/collection/page
  tree + `OutlinerState.h`), `Properties/`, `Info/`.
- `Project/` — `Project.h` (shared project, wraps a `Renderer::Document`),
  `ProjectFile.cpp` (the `.acu` file format, see `docs/acu-format.md`), `Undo.cpp` +
  `UndoStack.h` (snapshot undo/redo).
- `Layout/` — the Blender-style dynamic zone tree (`ZoneLayout*`, `PageLayout`).
- `Util/` — small self-contained helpers (`PngWrite.h`).
- `Dev/` — debug/test panels and the design-system window (`DevPanels.cpp`,
  `Windows.cpp`).

Every sub-folder is on the include path, so source files use flat includes
(`#include "Application.h"`, `"ZoneLayout.h"`, `"ViewportTools.h"`) regardless of
folder. All three major subsystems are singletons initialized in
`InitializeSubsystems()`.

**Editors are registry-based, not a fixed enum.** `EditorRegistry`
(`Layout/EditorRegistry.h`) maps a string id (`core.viewport`, `iof.mapsettings`,
…) to an `EditorDescriptor` (name/icon/scope/draw/topBar/flags). The core registers
its editors in `Application::RegisterCoreEditors()` (MainUI.cpp); the zone layout
stores ids and draws each zone by registry lookup. The `.acu` LAYOUT blob stores
the id (v4; old enum-index blobs migrate). This is what lets modules/plugins add
editors without touching core code.

### Modules (`src/Modules/`)

A **module** specialises the app for a use case. The app boots in Classic mode;
the splash "Modules" column opens one (`Application::RequestOpenModule`), which —
after the unsaved-changes guard — creates a fresh project and applies the module's
layout/editors/Shift+A/capabilities. `ModuleAPI.h` is the stable contract (the only
header an external plugin needs); `IModule` is the interface; `ModuleHost` (which
`Application` implements) is the slice of app services a module may drive.
`ModuleRegistry` holds the catalogue; internal modules are built in
`RegisterInternal()` (`Typography/`, `IofMapping/`); external DLL plugins are
designed-for but not yet loaded. Module wiring on `Application` lives in
`ModuleManager.cpp`. **Put reusable features in the core, not in a module.** See
`src/Modules/README.md`.

### DesignSystem (`src/DesignSystem/`)

Token-based theming with three hierarchy levels: **Primitive → Semantic → Component**. Each token stores a default value plus per-theme overrides. A `Context` is a `(ThemeType, AccessibilityType)` pair; accessibility transformations (Protanopia, Deuteranopia, Tritanopia) are color-matrix transforms applied at resolution time, not stored in tokens.

Resolution order: `Override (global > theme-scoped) → Token default → Reference (recursive)`.

`DesignSystem` is the public singleton facade. Internally, `TokenRegistry` stores all tokens, `OverrideManager` manages runtime overrides, and `Serialization` persists state to `design_system.bin`.

### Shortcuts (`src/Shortcuts/`)

Blender-inspired hierarchical shortcut engine. A 5-level context (window → editor → region → mode → tool) determines which actions are active. `EventNormalizer` drains ImGui IO into typed events (Press/Release/Click/Drag×8/Wheel) each frame. `ShortcutManager` resolves the highest-specificity matching action and persists bindings to `shortcuts.dat` (binary format v3). `ToolManager` tracks the active tool and feeds the context's `tool` field.

### VectorGraphics (`src/VectorGraphics/`)

SVG icons are rasterized at runtime by `resvg` (Rust, exposed via C FFI in `src/tools/resvg-bindings/`). `IconManager` maintains an LRU cache of Vulkan textures keyed on `(iconId, size, colorMetadata)`. Color zones map SVG element IDs to design tokens, enabling runtime recoloring without re-parsing SVG files.

### Renderer (`src/Renderer/`)

The **Vulkan-only vector engine**. Owns the vector document model (`Document` → `Artboard` → `Shape`, pure data under `include/Renderer/Document/`), CPU tessellation (`Tessellator`, shapes → triangle `Mesh`), and an offscreen Vulkan pipeline (`CanvasRenderer` + `RenderTarget`). Each Viewport zone is rendered into its own offscreen `VkImage`, exposed as an `ImTextureID` and blitted by the application with `ImGui::Image`. **ImGui never draws the vector artwork** — only the editor chrome (rulers, guides, tool palette, panels). GLSL shaders in `shaders/` are compiled to SPIR-V by `glslc` at build time (target `renderer_shaders`) and loaded from `<exe-dir>/shaders` at runtime. `CanvasRenderer` shares the main Vulkan device/queue/pools; the per-view camera (pan/zoom/unitScale) mirrors the Viewport's `D2S` mapping. See `src/Renderer/README.md`.

### UI (`src/UI/`)

Stateless ImGui components — `TokenEditor`, `ShortcutEditor`, `FontManager` — that render into the docking layout. No independent state; they read from and write to the singletons above.

### Shell (`src/Shell/`) — Windows only

Makes `.acu` files show a page preview + the app logo in Explorer (like `.blend`). The `Shell` static lib does per-user registry registration (HKCU, no admin); `acu_thumbs.dll` is an `IThumbnailProvider` COM server Explorer loads to render the preview from the `.acu` `THMB` section (PNG, decoded with stb_image). The app generates the `.ico` from the logo (resvg) and calls `ShellReg::EnsureRegistered` at launch. The C++ namespace is `ShellReg`, not `Shell` — the Windows SDK already declares a global COM type named `Shell`. See `src/Shell/README.md`. Windows does NOT preview unknown formats without such a registered COM handler.

## Language

**Respond to the user in French.** All conversational replies, explanations, and questions to the user must be written in French.

Everything *in the codebase and product*, however, **must be in English**: all UI-visible text (button labels, menu items, tooltips, window titles, action names, action descriptions, status bar strings, error messages), all source code, all comments, and all commit messages. Do not write French strings anywhere in source files.

## Styling — Design System Tokens (CRITICAL)

Any work that touches visual style **must go through design-system tokens**. Never hard-code colors, sizes, radii, paddings, font scales, or border widths as literals in UI code.

- Resolve every style value via `DesignSystem::Instance().GetColor/GetFloat/GetVec2(...)` on a token name.
- Respect the three-tier hierarchy: **Primitive → Semantic → Component**. A semantic token may reference another semantic token; a component token may reference a semantic or another component token. Add a new token at the right tier rather than inlining a value.
- If a needed token does not exist, **create it** (in `TokenRegistry.cpp`, with a description and an appropriate `ValueConstraint`) or reuse an existing one — do not bypass the system with a literal.
- Fallback literals are permitted *only* inside the `SafeColor/SafeFloat/SafeVec2` try/catch helpers, as the last-resort default if a token is missing. The token is still the source of truth.
- New reusable UI widgets must read all of their dimensions/colors from tokens so themes stay coherent and overridable at runtime.

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

The **project document** is a separate, user-chosen `*.acu` file (the proprietary
project format — a single versioned binary holding the vector document *and* the
zone layout/tabs/cameras). Managed by `App::ProjectFile`; see `docs/acu-format.md`.
`.acu` files are user data, not tracked by git.

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
| `renderer` | `src/Renderer/` |
| `shell` | `src/Shell/` (Windows shell integration) |
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

### Claude-specific commit rules (CRITICAL)

- **Never** add `Co-Authored-By: Claude ...` or any self-referencing attribution to commit messages.
- **Never** commit after making code changes without an explicit user request to do so. Always wait for the user to ask before running `git commit`.

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

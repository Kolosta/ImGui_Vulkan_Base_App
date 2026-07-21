# Modules

A **module** is a specialisation of the generic vector editor for a use case
(Typography, IOF Mapping…). The app always boots in the implicit **Classic** mode
(no active module); the splash start screen's **Modules** column opens one. Opening
a module is like *New File*: it guards unsaved changes, creates a fresh project,
then applies the module's layout, editors, Shift+A menu and capabilities.

A module can:
- **Reuse** core editors by referencing their ids in its layout (`core.viewport`,
  `core.outliner`, `core.properties`, …).
- **Add** its own editors, a Shift+A "Add" menu, Viewport side-panel tabs, and a
  canvas overlay.
- **Limit** core features via `Capabilities` flags (e.g. hide the core primitives).

> Rule of thumb: if a feature isn't module-specific and could be reused, implement
> it in the core (`src/Application/…`) and let modules opt in — don't duplicate it
> in a module.

## The contract — `ModuleAPI.h`

This single header is everything a module (internal **or** external) compiles
against. Key pieces:

- `ModuleInfo` — id / name / description / icon / version (shown on the splash).
- `Capabilities` — `corePrimitivesAddMenu`, `pages`, `editMode`, …
- `LayoutSpec` (in `ZoneLayout.h`) — a declarative zone tree of editor ids, built
  with `LayoutSpec::Leaf(id)` / `LayoutSpec::Split(vertical, ratio, a, b)`.
- `IModule` — the interface to implement. Only `Info()` and `BuildLayout()` are
  required; everything else has a no-op default:
  - `OnRegister(ModuleContext&)` — add editors/shortcuts (called once at startup).
  - `ConfigureCapabilities(Capabilities&)` — gate core features.
  - `BuildAddMenu(std::vector<UI::MenuEntry>&)` — replace Shift+A (return `true`).
  - `ViewportSidePanelTabs(std::vector<UI::SidePanelTab>&)` — add "N" panel tabs.
  - `DrawViewportOverlay(min, max)` — paint on the canvas.
  - `OnActivate()` / `OnDeactivate()`.
- `ModuleHost` — the slice of app services a module may drive. Bound by the app
  before `OnActivate()`; reach it from a module via `Host()`:
  - `Document()` / `PushDocCommand()` / `MarkDirty()` / `LogInfoAction()` — the
    Ink document + the shared undo stack (module edits undo like core ones);
  - `NodePreviewTexture()` / `CanvasPreviewTexture()` / `NodeDocBounds()` —
    REAL-pipeline vignettes and zoom/pan preview canvases of any node subtree
    (module symbol thumbnails render exactly like the canvas);
  - `ArmPlacement()` / `BeginSymbolDraw()` — the two symbol tool modes: click-
    to-place with a cursor vignette, and the core pen driven with a module
    style (the drawn node is handed back for routing).

## Editors — `EditorRegistry`

Editors are identified by a **string id** (e.g. `core.viewport`, `iof.mapsettings`)
via `EditorRegistry`, not a fixed enum. A descriptor carries the id, display name,
icon, picker column, theme scope, optional switch-action, the `draw`/`topBar`
callbacks, and the `wrapInScroll` / `contentInset` flags. The zone layout stores
the id and draws a zone by looking it up — so a module simply registers
descriptors and references their ids in its `LayoutSpec`.

The `.acu` LAYOUT blob stores the editor **id** (format v4); old v≤3 files migrate
their enum index to the matching `core.*` id.

## Adding an INTERNAL module

1. Create `src/Modules/<MyModule>/MyModule.{h,cpp}` implementing `IModule`.
2. Register editors in `OnRegister`, return a `LayoutSpec` from `BuildLayout`.
3. Add the `.cpp` to `src/Application/CMakeLists.txt` (the Application target).
4. Construct it in `ModuleRegistry::RegisterInternal()`.

`Typography/` (a minimal template) is the reference skeleton. `IofMapping/` is
the full-featured reference module, rebuilt on Ink: a previewOnly symbol
library seeded by `OnDocumentCreated`, symbols styled with core tools only
(stroke dash + repeats, multi-fill, instanced fills), print-layer groups as the
z-order, locked outliner structure via `AllowReparent`, and the place / draw
symbol tools. (The pre-Ink version remains in `src/_legacy/Modules/` as
behavioural reference only.)

## Adding an EXTERNAL module (plugin) — planned

The design is plugin-ready but the dynamic loader is **not implemented yet**
(first pass). A plugin will be a shared library that:
- compiles only against `ModuleAPI.h` (+ the headers it pulls);
- exports the C-ABI factory via `CARTO_MODULE_EXPORT(MyModule)` —
  `CartoCreateModule()` + `CartoModuleAbiVersion()`;
- is dropped into `<exe>/modules/`.

`ModuleRegistry::LoadExternalModules()` will (later) scan that folder, verify
`CartoModuleAbiVersion() == kModuleAbiVersion`, then `Add()` the instance. Bump
`kModuleAbiVersion` on any breaking change to the contract.

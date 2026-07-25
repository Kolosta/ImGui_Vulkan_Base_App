# Ink — target architecture

This is the master architecture document. It defines the layers, the folder
map, who owns what, the data flow of a frame, and the locked technical
decisions. Every other doc details one layer.

## 1. Design goals

- **Strictly 2D**, viewport-quality *and* export-quality vector rendering,
  100 % Vulkan inside the canvas (content **and** editor overlays).
- **Editing-first**: the pipeline is optimised for a document that mutates
  every frame under the user's cursor. Incremental everything: dirty ranges,
  persistent caches, persistent GPU pools.
- **Instancing-native**: an instance is a reference + transform, never a
  geometry copy — through the whole stack (document → scene → GPU draw).
- **Layered and testable**: each layer is a folder with a narrow public
  interface, unit-testable without a window; the whole engine renders
  headless (benchmarks, thumbnails, exports use the same path).
- **No legacy**: zero includes from `src/_legacy/**`.

## 2. Layer stack

Dependencies point strictly downward. A layer never reaches up.

```
┌────────────────────────────────────────────────────────────┐
│  Application (src/Application)                             │  editors, tools,
│  Viewport / Outliner / Properties / undo / .acu I/O        │  UI, shortcuts
└─────────────▲──────────────────────────────▲───────────────┘
              │ Ink::View (canvas API)       │ Ink::Document (model API)
┌─────────────┴──────────────┐  ┌────────────┴───────────────┐
│  View                      │  │  Document                  │  pure data +
│  per-viewport camera,      │  │  nodes, layers, styles,    │  invariants,
│  render target, overlays,  │  │  collections, modifiers,   │  no Vulkan,
│  picking queries           │  │  change tracking           │  no ImGui
└─────────────▲──────────────┘  └────────────▲───────────────┘
              │                              │
┌─────────────┴──────────────────────────────┴───────────────┐
│  Scene                                                     │  document →
│  compiled runtime scene: modifier evaluation, instance     │  render items,
│  expansion (logical), z-order resolution, dirty diffing    │  incremental
└─────────────▲──────────────────────────▲───────────────────┘
              │                          │
┌─────────────┴───────────┐  ┌───────────┴────────────────────┐
│  Geometry               │  │  Render                        │  passes: content,
│  flatten, stroke,       │  │  render-graph passes, batching,│  clip, composite,
│  tessellate, bounds,    │  │  GPU scene data (pools,        │  overlay, picking,
│  geometry cache         │  │  instances, paints)            │  present
└─────────────▲───────────┘  └───────────▲────────────────────┘
              │                          │
┌─────────────┴──────────────────────────┴───────────────────┐
│  Graph                                                     │  frame graph:
│  passes, transient resources, barriers, submission         │  declarative
└─────────────────────────────▲──────────────────────────────┘
                              │
┌─────────────────────────────┴──────────────────────────────┐
│  RHI (Vulkan)                                              │  device, VMA,
│  Device, Allocator, Buffer/Image, Pipeline, Descriptors,   │  swap-agnostic,
│  Uploads, Frame pacing, Timestamps                         │  the only layer
└────────────────────────────────────────────────────────────┘  touching vulkan.h
              (Core: math/ids/color/units — used by every layer)
```

Notes:

- **Document** and **Geometry** are windowless, GPU-less, deterministic —
  fully unit-testable.
- **Scene** is the keystone: it is the *only* consumer of Document and the
  *only* producer of render items. Editors mutate the Document; the Scene
  diffing turns mutations into minimal geometry/GPU work.
- **View** is the app-facing façade for a canvas: the Viewport editor holds an
  `Ink::View`, feeds it a camera + overlay list, and receives an
  `ImTextureID` (or a swapchain composite hook) to place in its zone.
- **RHI** wraps raw Vulkan so upper layers never juggle `VkPipeline` /
  barriers / VMA directly. It is deliberately thin — not a general-purpose
  abstraction, just this engine's needs, typed and RAII-owned.

## 3. Folder map (target)

```
src/Ink/
├── CMakeLists.txt                 # static lib Ink + ink_shaders + ink_bench
├── README.md                      # points to docs/Ink/
├── include/Ink/                   # public headers (what other code includes)
│   ├── Core/                      #   Vec2, Mat23, Rect, Color, Id, hashing, units
│   ├── Document/                  #   Document, Page, Node, Style, Paint,
│   │                              #   Collection, Modifier, ChangeLog,
│   │                              #   CompGraph/CompNode/CompPort (planned,
│   │                              #   NODE_GRAPH.md — the per-layer
│   │                              #   Compositing Graph, NOT the frame Graph/
│   │                              #   RenderGraph below)
│   ├── Scene/                     #   Scene, SceneItem, CompileOptions, DirtySet
│   ├── Geometry/                  #   PathFlattener, Stroker, Tessellator,
│   │                              #   GeometryCache, Bounds, HitTest
│   ├── Graph/                     #   RenderGraph, Pass, ResourceDesc
│   ├── Render/                    #   Renderer (engine root), GpuScene, Batcher,
│   │                              #   passes/* public config
│   ├── RHI/                       #   Device, Allocator, Buffer, Image, Pipeline,
│   │                              #   Descriptors, Upload, FrameSync, Timestamps
│   └── View/                      #   View (camera/target/overlays/picking)
├── src/                           # implementation, mirrors include/Ink/
│   ├── Core/  Document/  Scene/  Geometry/  Graph/  RHI/  View/
│   └── Render/
│       ├── GpuScene.cpp           # pools, instance tables, paint table
│       ├── Batcher.cpp            # cull + batch build (indirect args)
│       └── Passes/                # one file per pass
│           ├── ContentPass.cpp
│           ├── ClipPass.cpp
│           ├── CompositePass.cpp
│           ├── OverlayPass.cpp
│           ├── PickingPass.cpp
│           └── PresentPass.cpp
├── shaders/                       # GLSL → SPIR-V (ink_shaders target)
│   ├── content/  overlay/  composite/  picking/
└── tests/
    ├── unit/                      # geometry & document invariants (ctest)
    └── perf/                      # ink_bench (see PERF_TESTING.md)
```

Rules, same spirit as the rest of the repo (CLAUDE.md · File Organisation):
split a file as soon as content separates cleanly; one class may span several
`.cpp` grouped by concern; shared internal helpers live in a small `inline`
header next to their users.

## 4. Frame data flow

```
Editor mutates Document (via typed ops)          ── CPU, app thread
        │  ChangeLog (node ids + kind of change)
        ▼
Scene::Compile(doc, changes)                     ── incremental
        │  • re-evaluates modifiers whose inputs changed
        │  • re-expands instances logically (refs, not copies)
        │  • re-resolves z-order for touched layer subtrees (planned,
        │    NODE_GRAPH.md: by evaluating each layer's Compositing Graph —
        │    auto-generated from tree order unless pinned — same DirtySet
        │    contract, no change to Scene's public interface)
        │  ▼ DirtySet {items added/removed/geom-dirty/style-dirty/xform-dirty}
        ▼
GeometryCache::Refresh(dirty items, zoom tier)   ── flatten/stroke/tessellate
        │  only dirty geometry, keyed (pathHash, styleHash, tier)
        ▼
GpuScene::Sync(dirty)                            ── staged uploads into pools
        │  vertex/index pools, instance table, paint table, transforms
        ▼
per View: RenderGraph::Execute(view)             ── GPU
        │  Cull+Batch → Content → Clip → Composite → Overlay → Picking → Present
        ▼
Viewport zone shows the view target; app reads picking results next frame
```

An idle frame (no document change, no camera change) re-records **nothing**
for content: the compiled batches and targets persist; only the present/
composite of the cached target runs (and even that can early-out to a cached
blit). Ink targets *zero* steady-state CPU cost for a static canvas.

## 5. Threading model

- **Lot 1–n (initial):** single-threaded on the app thread, but all layer
  interfaces are written against *jobs by contract*: `Scene::Compile`,
  `GeometryCache::Refresh` and `GpuScene::Sync` take explicit input/output
  sets and never touch globals, so they can later fan out to a worker pool
  without interface change.
- **Planned (perf lots):** geometry refresh fans out per-item on a small
  worker pool (flatten/stroke/tessellate are embarrassingly parallel);
  recording stays on the render thread; picking readback is already async.
- ImGui, SDL and all editor code stay on the main thread, as today.

## 6. Locked technical decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Vulkan baseline | **Vulkan 1.3**: dynamic rendering, synchronization2, timeline semaphores; descriptor indexing (bindless) where present | Device creation already enables these (kept from Lot 0); fallback path = fail Ink init with a clear message, app shell still runs |
| Allocation | **VMA**, one allocator owned by `RHI::Device` | Battle-tested; pools/aliasing for transient graph resources |
| Color space | Internal rendering **linear, premultiplied alpha**; sRGB encode at present/export | Correct blending/AA math; matches W3C compositing model |
| Anti-aliasing | **MSAA ×4** on content/overlay targets for v1 (resolve in graph); analytic-AA fringes later as an optimisation lot | Simple, correct with arbitrary blending; the graph hides the resolve so the technique can change without touching passes |
| Precision | Document coordinates **double**; GPU works in **view-relative float32**: the camera subtracts a per-view anchor in double, and GPU transforms are rebased against that anchor (re-anchored when the camera strays far / crosses zoom tiers) | **Unbounded zoom** (µm → km unit scales, README req. 9) without float wobble — precision by construction, never by clamping. The Lot 1 demo still clamps (its GPU tables are f32 document-space); the clamp dies with the Scene rebasing (Lots 2–3) |
| Z-order | **Painter's order** from the Layers tree (stable sort by resolved index); no depth buffer for content. *Stacking order stays here even after the Compositing Graph lands (NODE_GRAPH.md, planned Lots 12–14) — what moves into the graph is a layer's* content*, never which layer draws over which.* | 2D correctness with transparency/blends; depth only in picking pass (id resolve) |
| Blend modes | Group/node isolation targets + backdrop-sampling composite shaders (W3C/SVG set) | Arbitrary blend modes without dual-source limits; isolation is also what clipping/effects need |
| Geometry source | **CPU geometry for v1** (flatten/stroke/tessellate, cached, dirty-only) behind the `Geometry` interface; GPU compute geometry is a later, measured lot | Correctness and debuggability first; the interface (mesh ranges in pools) is identical either way |
| Draw submission | Persistent vertex/index/instance pools + per-batch **indirect draws**; per-instance data in SSBOs; push constants only carry indices | One code path for 1 object or 10 000 instances |
| Textures (images, pattern rasters) | Bindless (descriptor-indexed) texture table when supported; small bound-array fallback | Images/patterns mix freely in one batch |
| Picking | GPU id pass on demand (view-rect scissored to cursor), **async readback (N−1)**, CPU exact test as fallback for the click frame | Pixel-exact picking incl. strokes/patterns without stalls |
| Shaders | GLSL, compiled by `glslc` at build (`ink_shaders` target) into `<exe>/shaders/ink/` | Same toolchain as before, separate output dir to avoid clashes |
| Pipeline cache | `VkPipelineCache` persisted to disk under the app prefs dir | Cold-start cost amortised |
| Naming | namespace `Ink`; sub-namespaces `Ink::rhi`, `Ink::graph` for the low layers; no `I`-prefixed interfaces (small structs of function refs or virtual only where a real seam exists) | Grep-ability and honesty about seams |

## 7. Integration contract with the application

The Viewport editor (rebuilt in Lot 1+) talks **only** to:

- `Ink::Renderer` — engine root: owns Device, GpuScene, GeometryCache, Scene;
  created once at app init (after `VectorGraphics`), `Shutdown()` before
  device destroy. Receives the app's `VkInstance/VkPhysicalDevice/VkDevice/
  queue/command pool` (shared device model, as today).
- `Ink::View` — one per Viewport zone (keyed by `EditorState*`, like the old
  per-zone targets): `SetCamera(pan, zoom, unitScale)`, `SetViewport(px size)`,
  `SubmitOverlay(OverlayList)`, `Render() → ImTextureID`, `Pick(x, y)`.
- `Ink::Document` — owned by `App::Project`; editors mutate it through typed
  operations that feed the ChangeLog (which is also the undo journal source).

ImGui composites the view texture with `ImGui::Image` (as the legacy did) —
**one** blit per viewport zone, everything inside the texture is Ink's. If a
later lot moves to swapchain-direct composition, only `PresentPass` and the
zone-background handling change (the Compositor already proved both models).

## 8. What Ink explicitly does NOT do

- No ImGui inside the engine (not even for debug HUDs — debug data is
  *exposed* via `Ink::Stats` and drawn by the app's Dev panels).
- No file I/O in Document (serialisation lives app-side in `ProjectFile`,
  reading/writing the Document through its public API — the model stays
  format-agnostic).
- No design-token resolution inside the engine: canvas *content* colors come
  from the document; editor-overlay colors are passed in by the app (which
  resolves them from design tokens) as plain RGBA.

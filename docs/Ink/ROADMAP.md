# Ink — roadmap

Lot-by-lot build order. Each lot lands compiling, F7-verifiable, with its
benchmark/tests, and updates these docs. No lot references `_legacy` code.

- [x] **Lot 0 — Disconnection** *(this branch)*: Renderer + Compositor + old
  document quarantined under `src/_legacy/`, removed from the build; app
  shell (chrome, zones, design system, shortcuts, icons, module catalogue,
  splash, secondary windows) runs with placeholder Viewport/Outliner/
  Properties; Project stripped of the old document; save/open disabled.
- [x] **Lot 1 — Viewport bootstrap** (detailed below): Ink renders inside the
  Viewport zone. RHI + graph skeleton + content & overlay passes on a
  hard-coded demo scene; camera pan/zoom live; `bootstrap`/`steady`/`empty`
  benches. *(F7-verified 2026-07-07; bootstrap ≈ 0.25 ms/frame @1015
  instances, steady-state skips 200/200 frames.)*
- [x] **Lot 2 — Document & scene core**: Document (nodes, pages, layer tree,
  ChangeLog), Scene compile + dirty diffing, GeometryCache v1, solid fills +
  center strokes; the demo scene becomes a real in-memory Document;
  **double coordinates end-to-end** (document, camera state, scene compile —
  README req. 9); `paths_10k` bench; unit tests for model + fill tess.
  *(Delivered 2026-07-08; paths_10k: 15 001 drawables / 800 k tris →
  1.45 ms/frame, first full build 425 ms.)*
- [x] **Lot 3 — Full stroking & tiers**: align inside/center/outside (open
  paths incl.), caps/joins/miter, dashes, viewport-space widths; zoom tiers +
  hysteresis; camera-relative rebasing of GPU transforms (per-view instance
  tables + snapped double anchors) → **the viewport zoom clamp is gone**
  (unbounded zoom, GEOMETRY.md §6); view culling (conservative bounds; drops
  draws, never inputs — GEOMETRY.md §7; the pixel-exact culled-vs-unculled
  image gate needs the readback path and lands with it); `edit_heavy` +
  `zoom_sweep` benches; stroker golden tests. *(Delivered 2026-07-08;
  edit_heavy: 12.3 ms/frame dominated by the v1 full-walk compile — the
  dirty-range target for the perf lots; zoom_sweep p50 3.4 ms, tier-change
  spikes ~120 ms re-tessellation to absorb asynchronously later.)*
- [x] **Lot 4 — Layers compositing**: groups as layers (a group with
  opacity<1 / non-Normal blend / isolate / clip opens a composite scope), the
  W3C "Compositing and Blending Level 1" model verbatim (the SVG/CSS/Canvas/PDF
  standard — separable blends + Erase), a per-view reserved isolation-target
  stack played back in post-order (child renders + composites before its
  parent continues; ping-pong linear pair per level avoids attachment
  feedback), page substrate as a non-layer backdrop; `blend_groups` bench.
  *(Delivered 2026-07-08. Known limits, deferred to follow-ups: (a) clip is
  wired end-to-end in the RHI — stencil pipelines + graph stencil attachment +
  the Scene emits clip-source drawables — but the playback does not yet bind
  the mask, so clip scopes isolate WITHOUT masking; (b) isolation levels
  render ×1 (MSAA only on iso[0]), so a blended group's edges are aliased;
  (c) one fullscreen composite per group → blend_groups 500 groups ≈ 49 ms
  GPU, the tile/bounds-scoped-composite target for the perf lots.)*
- [x] **Lot 5 — Instancing**: InstanceNode (renders a target subtree at its
  own transform, depth-clamped, self-ref refused), pattern fills (a motif node
  instanced on a lattice over the host bbox), Array + AlongPath modifiers (an
  ordered stack expanded at Scene compile into a set of transforms). All three
  are ONE mechanism: the Scene emits many drawables sharing the source's
  pathHash, so they merge into one instanced indirect draw — logical
  expansion, never geometry duplication. *(Delivered 2026-07-08. Benches:
  instances_100k = 100 001 instances in **4 draw calls, 0.80 ms GPU** (the
  instancing claim proven; the 6.8 ms CPU is per-instance bbox culling — the
  GPU-cull target); pattern_fill 107 k motifs → 4 draws / 0.50 ms; along_path
  20 k ticks → 43 draws / 0.22 ms. Known limit: pattern lattice is clipped to
  the host BBOX, not the exact shape — that rides on the clip-mask follow-up.)*
- [ ] **Lot 6 — Images**: DEFERRED by the product owner (2026-07-08) to run
  later, after the interaction/persistence lots. Scope unchanged: import
  (decode app-side), bindless texture table, image paints & ImageNode;
  `images` bench. Its slot moves to the end of the list (before "Later").
- [x] **Lot 7 — Relations**: object parenting (transform inheritance —
  `parentId` distinct from the layer-tree position, DOCUMENT_MODEL.md §2;
  SetParent/ClearParent preserve world position and refuse cycles), the
  Boolean modifier (Union/Subtract/Intersect/Xor via edge-splitting polygon
  clipping → a derived path evaluated at Scene compile). Every relation
  (parentId, modifier operand/path/target refs) is a declared id-edge, so the
  dependency graph for EXACT incremental re-evaluation is fully expressible —
  wiring the Scene to recompile only affected nodes (instead of the current
  change-gated full walk) is a perf-lot task. *(Delivered 2026-07-08. Boolean
  is exact on non-degenerate input; collinear edge coincidences are a
  documented v1 approximation.)*
- [x] **Lot 8 — Editing loop**: CPU-exact picking on the compiled Scene
  (`Ink::PickTop` topmost-first by fill-rule / stroke half-width; `PickBox` by
  per-owner bounds — the async GPU path is a later perf lot and this stays its
  correctness reference), selection + bbox-handle + Edit-Mode-anchor + modal
  overlays (all drawn by Ink's OverlayPass, 100 % Vulkan), the Select /
  Rectangle / Ellipse tools and the modal Move/Rotate/Scale (G/R/S with X/Y
  axis constraint, orientation basis, pivot modes, Ctrl/magnet snapping),
  Object/Edit mode split (Object-Mode scale stretches strokes; Edit-Mode moves
  anchors) with **Apply Scale** (`Document::ApplyScale` bakes scale into
  geometry — DOCUMENT_MODEL.md §4), the Shift+A Add menu, delete/duplicate, and
  **command-based document undo** (`DocUndoStack`: reversible commands over the
  typed ops, `Document::CopySubtree`/`RestoreSubtree`/`DuplicateSubtree` for
  exact add/delete round-trips). The **Viewport top bar returns** on the Ink
  model (mode / orientation / pivot / snap / overlay); controls whose Ink
  feature has not landed (rulers, non-Increment snap modes, 2D cursor,
  page-layout overlays, metrics) are shown GREYED until their pass.
  `pick_storm` bench (CPU-only). *(Delivered 2026-07-09. Known v1 limits:
  box-select uses conservative per-node bounds, not exact geometry; the 2D
  cursor and its cursor-pivot/orientation return with a later pass;
  Edit-Mode edits move anchors only — handle/segment editing and the curve
  tool are a follow-up.)*
- [x] **Lot 9 — Organisation UI**: **Collections** land in the Document
  (DOCUMENT_MODEL.md §7 — organisational sets, many-to-many membership, a
  per-set visibility that filters at Scene compile: a node hidden by ANY route
  is culled, never changing what IS drawn) alongside the organisation ops the
  editors drive (rename, lock, reorder, reparent-preserving-world `MoveTo`,
  `GroupNodes`/`UngroupNode`). The **Outliner** is rebuilt on the Ink model
  with a **Layers** view (pages → layer trees, top-of-stack first, z-order +
  per-node visibility + inline rename + expand/collapse) and a **Collections**
  view (sets, per-set visibility, members), a top bar (display toggle, search,
  kind filter) and a context menu (group/ungroup, delete, duplicate, new
  collection / add-to-collection). The **Properties** editor is rebuilt on the
  unified style: transform (with Apply Scale), multi-fill and multi-stroke
  lists (add/remove pieces, paint pickers, width/align/cap/join), group
  opacity/clip — every edit through the typed ops, continuous drags folding
  into one undo command. Selection is the shared EditContext, so Outliner,
  Viewport and Properties always agree. Layout blob → v7 (Outliner state
  migrates from v5/v6). *(Delivered 2026-07-09. Known v1 limits: Outliner
  Shift-range select adds the clicked node rather than the full run;
  drag-and-drop reordering in the tree is a follow-up — reorder/reparent are
  exposed as ops and used by grouping; collection delete is not yet on the
  undo stack.)*
- [x] **Lot 10 — Persistence**: `.acu` v2 (clean break), save/open/recent
  re-enabled, thumbnails re-enabled through headless Ink render.
  *(Delivered 2026-07-12. App-side codec `AcuFile` (docs/acu-format.md):
  same container frame as v1 — magic/sections, so the shell thumbnail DLL
  needs no update — with container version 2 and a fresh Ink DOC payload
  (pages, full nodes, styles, modifiers, collections, f64 throughout); v1
  files are refused with a clear message. Ink gained `Document::Restore`
  (bulk verbatim-id install, structural validation, dangling-ref
  sanitising) and `Renderer::ReadViewPixels` (synchronous display
  readback). Saves are two-phase inside one frame: the page-1 thumbnail
  renders through the REAL pipeline in an off-screen view, is read back
  and PNG-encoded, then the file writes; the unsaved-changes dialog's
  chained intent commits only after the write. Open guards dirty projects
  through the same dialog; recent files re-enabled.)*
- [ ] **Lot 11 — Modules re-entry**: ModuleAPI regains document hooks (typed,
  Ink-based); IofMapping and Typography rebuilt on Ink where they belong.
  *(Contract layer delivered 2026-07-13, ABI v2: ModuleHost exposes the Ink
  document services — `Document()`, `PushDocCommand` (module edits land on the
  shared undo stack), `LogInfoAction` — and IModule gained
  `OnDocumentCreated(Ink::Document&)`; opening a module now sizes the default
  page per `DefaultPageSize()` and seeds via the module's typed ops instead of
  the Classic demo; opening an .acu re-activates the module recorded in its
  META (keeping the file's layout; an uninstalled module falls back to Classic
  without losing the id). Typography rides the hooks (1000-unit em canvas +
  Guides collection: em square, baseline, x-height, cap height as viewport-px
  hairlines). REMAINING: the IofMapping rebuild on Ink — symbol set, print
  layers, symbol viewer, map settings — which completes this lot. The full
  legacy feature inventory and the phased plan (core marks → curves/NURBS/pen
  → module) live in `docs/Ink/IOF_CORE_PLAN.md`.)*
- [ ] **Lot 6 (deferred) — Images**: run here, after modules. Import
  (decode app-side), bindless texture table, image paints & ImageNode;
  `images` bench.
- [ ] **Later, measured**: GPU-driven culling/geometry, analytic AA, effects
  pass (blur/shadow), gradients, text (with Typography), export pipeline.

Ordering rationale: correctness spine first (model → geometry → compositing),
then the two claims that shape all data structures (instancing, relations),
then interaction, then persistence/UI breadth. Undo arrives with the editing
loop (Lot 8) because command-based undo needs the typed ops that tools
exercise.

---

# Lot 1 — Viewport bootstrap (the detailed first step)

**Goal:** the Viewport zone's canvas is rendered by Ink, 100 % Vulkan,
end-to-end through the real architecture (RHI → Graph → passes → View →
`ImTextureID`), with live camera. Content is a hard-coded demo scene built
directly as GPU data (the Document/Scene layers arrive in Lot 2 — nothing
here is throwaway: the demo feeds the pools through the same GpuScene API the
Scene will use).

## Deliverables

1. **Ink static lib + build wiring**
   - `src/Ink/CMakeLists.txt`: lib `Ink` (+ `ink_shaders` glslc target →
     `<exe>/shaders/ink/`), linked into `Application`/exe; VMA via the root
     FetchContent (already present).
2. **RHI** (`Ink::rhi`)
   - `Device` (adopts the app's instance/physical/device/queue — shared-device
     model; asserts the 1.3 features), `Allocator` (VMA), `Buffer`, `Image`,
     `PipelineBuilder` (dynamic rendering), `DescriptorTable` (bindless-ready,
     array fallback), `Upload` (staging ring + per-frame garbage), `FrameSync`
     (2 in flight, timeline semaphore), `Timestamps`.
3. **Graph** (`Ink::graph`)
   - Minimal but real: passes declare read/write on named resources; the
     graph resolves order + `sync2` barriers + transient allocation. Unit
     test: declared 3-pass chain produces expected barriers.
4. **Render**
   - `GpuScene` v0: vertex/index pool, item + instance + paint tables (SSBO),
     prebuilt per-batch indirect commands.
   - `ContentPass` (MSAA ×4 RGBA16F, premultiplied linear) with the MSAA
     resolve as its attachment, `OverlayPass` (same MSAA target),
     `PresentPass` (sRGB encode into the sampled display texture; ordering
     vs the app's UI pass via same-queue barriers — RENDER_GRAPH.md §5).
5. **View** (`Ink::View`)
   - `SetViewport(w, h)` (target lifecycle on resize), `SetCamera(pan, zoom,
     unitScale)`, `SubmitOverlay(prims)`, `Render()` → `ImTextureID`,
     `Stats()`.
6. **App integration** (`src/Application`)
   - `InitializeSubsystems()` creates `Ink::Renderer` (after VectorGraphics);
     `Shutdown()` tears it down before device destroy; the app's UI pass needs
     no semaphore (same-queue barrier ordering — RENDER_GRAPH.md §5).
   - New `Editors/Viewport/Viewport.cpp` (fresh file, minimal): owns an
     `Ink::View` per zone leaf (keyed on `EditorState*`, evicted when the
     zone dies), computes the same `D2S` camera mapping as before from
     `EditorState.pan/zoom`, drives middle-drag pan + wheel zoom, submits a
     few overlay primitives (page-frame rect demo + cursor crosshair) and
     blits with `ImGui::Image`.
   - Demo content: ~a dozen filled+stroked paths + one 1 000-instance grid,
     loaded into GpuScene at init (exercises pools, instancing path, and
     gives the bench a floor).
7. **Bench + stats**
   - `ink_bench` skeleton with the `empty` scene (and the demo scene as
     `bootstrap`); `Ink::Stats` wired; Dev panel shows frame/GPU numbers for
     the hovered viewport.

## Acceptance criteria

- Two Viewport zones side by side show the demo scene with independent
  cameras; pan/zoom is smooth; zone resize/split/join never validation-errors
  (validation layer clean run).
- All canvas pixels come from Ink (the only ImGui call for the canvas is the
  single `ImGui::Image`); overlays (page frame, crosshair) are drawn by
  OverlayPass, not ImGui.
- Static scene → `recordMs ≈ 0` on idle frames (steady-state short-circuit
  works); `ink_bench --scene bootstrap` produces a JSON with non-zero GPU
  pass timings.
- No include of anything under `src/_legacy/**` (grep gate).

## Suggested implementation order (compilable checkpoints)

1. CMake lib + empty `Ink::Renderer::Initialize/Shutdown` called by the app
   (links, runs, does nothing).
2. RHI Device/Allocator/Upload/FrameSync + a clear-only View target presented
   into the Viewport zone (a colored rectangle in the zone = first pixels).
3. Graph skeleton moving that clear into a declared ContentPass (+ Resolve +
   Present) — same pixels, now through the graph.
4. GpuScene pools + content shaders + demo paths (triangles appear).
5. Instance table + the 1 000-instance grid (instancing proven).
6. OverlayPass + app-side OverlayList (crosshair follows the mouse).
7. Camera wiring (pan/zoom), per-zone views, resize lifecycle.
8. Timestamps/Stats + ink_bench skeleton + validation-layer clean pass.

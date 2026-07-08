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
  W3C separable blend set + Erase (straight-output iso composite shader —
  un-premultiply, blend, re-premultiply), a per-view reserved isolation-target
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
- [ ] **Lot 5 — Instancing**: InstanceNode, pattern paints (instanced motifs,
  current parameter set re-specified), along-path + array modifiers;
  `instances_100k`, `pattern_fill`, `along_path` benches.
- [ ] **Lot 6 — Images**: import (decode app-side), bindless texture table,
  image paints & ImageNode; `images` bench.
- [ ] **Lot 7 — Relations**: object parenting (transform inheritance),
  modifier dependency graph + exact re-evaluation, boolean modifier v1.
- [ ] **Lot 8 — Editing loop**: GPU picking (async) + CPU exact fallback,
  selection/handles/gizmo overlays, rebuilt Select/Move/Rotate/Scale tools
  and draw tools on the new model, Object/Edit mode split (Blender semantics:
  Object-Mode scale stretches strokes, Edit-Mode vertex edits don't) with
  **Apply Scale** (DOCUMENT_MODEL.md §4), command-based undo; `pick_storm`
  bench.
- [ ] **Lot 9 — Organisation UI**: Outliner rebuilt on Layers + Collections
  views; Properties rebuilt (style editor incl. multi-fill/multi-stroke).
- [ ] **Lot 10 — Persistence**: `.acu` v2 (clean break), save/open/recent
  re-enabled, thumbnails re-enabled through headless Ink render.
- [ ] **Lot 11 — Modules re-entry**: ModuleAPI regains document hooks (typed,
  Ink-based); IofMapping and Typography rebuilt on Ink where they belong.
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

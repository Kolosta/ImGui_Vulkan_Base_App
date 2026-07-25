# Compositor — the second Vulkan render engine (SUPERSEDED)

> **Status: SUPERSEDED / QUARANTINED.** The Compositor (and the legacy
> Renderer) were disconnected from the build and moved to `src/_legacy/`; the
> rendering stack is being rewritten from scratch as the **Ink** engine — see
> **`docs/Ink/`** (README, ARCHITECTURE, DOCUMENT_MODEL, RENDER_GRAPH,
> GEOMETRY, PERF_TESTING, ROADMAP). This document is kept only as a historical
> record of what the Compositor did and the lessons it produced; do not build
> new work from it.
>
> `RENDER_PIPELINE.md` (next to this file) is the generic industry example that
> seeded this design — kept as reference, not as the plan.

---

## 1 — Original brief (why this exists)

The current Vulkan pipeline (`Renderer::CanvasRenderer`) is an efficient
per-view offscreen renderer, but **structurally narrow**: a single
`VkRenderPass`, a purely-vector data model (`Document → Artboard → Shape`), and no
generic notion of **layers / blend / masks / raster content / effects**. It can
neither support the intended future uses nor reach the fluidity we want.

Goal: design and implement a **second Vulkan render engine, rebuilt from
scratch**, architecturally complete and **industrial-grade**, organised in
**logical layers** (never thematic / per-feature) where every future capability
plugs in — *without removing or reusing the current pipeline's base*. The two
coexist; we switch between them to compare performance and behaviour. Long
project, run **lot by lot**, with this written trace to stay coherent over time.

Future capabilities the architecture must structurally host: powerful vector
display/editing; non-vector image/photo editing (Lightroom/Photoshop-style);
layer blending; booleans; subtractive layers; effects/filters; and the dozens of
features not yet imagined — each landing inside a *logical* pass, not a bespoke
one.

### Locked decisions
1. **Switching** — exactly **one engine alive at a time, globally**. The inactive
   one is *not instantiated* (zero cost, no desync risk). The global toggle does a
   *clean swap* (`vkDeviceWaitIdle` → shut down the old → init the new). The
   initial kind is config/flag-driven.
2. **v1 deliverable** — **visual parity** with the legacy on the current vector
   document. Future layers are present in the architecture but activated in later
   lots.
3. **Tech base** — modern Vulkan: `dynamic rendering`, `synchronization2`,
   `descriptor indexing` (bindless), `timeline semaphores`, a **linear
   premultiplied** working space — and memory via **VMA** (FetchContent). Distinct
   from the legacy base (VkRenderPass + manual allocation), which stays intact.

   **What is separate vs shared (important, often re-asked):**
   - **The render engine (Vulkan) is 100% separate.** All of `src/Compositor/` is
     new — its own device layer, pipelines, shaders, render graph, passes. It
     shares NO code with the legacy `CanvasRenderer`; one engine is alive at a time.
   - **The data model (`Document`/`Shape`/`Path`/`Paint`) is deliberately SHARED.**
     It's pure data (no Vulkan). Both engines read the SAME document — that's the
     whole point of comparing them, and avoids forking the editor / tools / undo /
     `.acu` / Outliner. `IViewRenderer.h` lives in `Renderer/` because it's the
     shared contract BOTH engines implement. This is why we diverge from
     `RENDER_PIPELINE.md` (a standalone app with its own `scene/`+`editor/`): the
     Compositor plugs into an EXISTING app; the editor/tools/model stay put.
   - **The `Tessellator` is shared TEMPORARILY** (CPU geometry, not Vulkan) to reach
     parity fast — Lot 2 says "reuse the existing Tessellator first, then grow its
     own". The target is a clean `Compositor/Geometry/` (Tessellator/StrokeBuilder/
     CurveFlatten/PathBoolean) that REPLACES this dependency. Small additions to the
     shared Tessellator for the Compositor (e.g. `ObjDraw::opacity`, ignored by the
     legacy) migrate with it. **Debt to repay** (planned around the optimisation /
     raster lots).
4. **ImGui never touches the canvas — display is pure Vulkan.** The Compositor
   renders each view to an offscreen `VkImage` (Vulkan) **and composites those
   images onto the swapchain via its OWN Vulkan pass** (a textured quad recorded
   into the main pass, before ImGui's draw data). ImGui is used **exclusively for
   the chrome around the canvas** — it never samples or blits the canvas
   (`ImGui_ImplVulkan_AddTexture`/`ImGui::Image` are NOT used for the document).
   The editor only supplies each view's destination rect. *(The legacy engine
   keeps its ImGui blit; this is the new engine's contract.)*

### Engine scope vs the rest of the app (key project adaptation)
**Through parity (Lots 0–3)** the engine renders **only document content** into a
per-view texture; the **editor overlays** (selection outlines, origin points,
linked-object lines, edit-mode handles, page outlines, 2D cursor, grid, rulers,
HUD) and the **tools** stay in `src/Application/`, drawn in ImGui *over* the
texture — **pipeline-agnostic**, shared by both engines (so they already appear
over the Compositor's canvas today). The **tools** logic stays in Application
permanently (it's input/state, not rendering).

**Long-term target (post-parity lot):** move the overlay *rendering* into the
engine as a dedicated **Overlay/HUD pass** (Pass G/H of the reference) so the new
pipeline draws gizmos/handles/selection/page-outline/2D-cursor in Vulkan, not
ImGui. The editor would then submit overlay primitives to the engine instead of
to an ImGui draw list. Tracked as Lot 12 below. (We do **not** fork the editor
state/tools — only the draw path moves.)

---

## 2 — Pipeline overview (full graph)

```
0 · SCENE (CPU, persistent)
    Scene = generic layer tree. LayerNode {transform, blendMode, opacity, clip,
    isolation, maskRef, enabled}; kinds: Group · Vector · Raster · Text ·
    Adjustment · Mask. TransformTree (TRS+pivot, matrix cache) · BoundsTree (AABB,
    dirty).  ── v1: DocumentAdapter builds the Scene from Renderer::Document ──
        ▼
1 · FRAME LOOP (CPU, per frame)
    DirtyTracker (objects + regions/tiles) → Culler (view rect) → Geometry prep
    (CurveFlatten · StrokeBuilder · PathBoolean → Mesh, LOD cache) → FrameBuilder
    (Scene → RenderList: z-sort / blend bucket / batch) → VMA upload (ring/staging)
    → UBO ring per frame → FrameGraph build.
        ▼
2 · RENDER GRAPH (GPU; dynamic rendering + sync2; inferred barriers)
    P1 CONTENT     produce a leaf's intrinsic pixels
                   ├ VectorContent: fill (solid/gradient/pattern) + stroke
                   ├ RasterContent: images/photo, brush/paint canvas
                   └ TextContent:   SDF/bitmap glyph atlas, shaping
    P2 MASK/CLIP   modulate coverage (stencil/alpha/clip-to-below); clip paths,
                   layer masks, mask from another layer's alpha
    P3 FILTER/FX   transform a layer's pixels (extensible chain): blur, shadow/
                   glow, color (levels/curves/HSL/exposure/WB), LUT3D, sharpen,
                   distort (+ Adjustment layers = FX over the backdrop below)
    P4 COMPOSITE   merge layers bottom→top: blend modes (Porter-Duff
                   over/in/out/atop/xor = subtractive + separable/non-separable
                   Multiply…Luminosity), opacity, isolation, group flatten
    P5 OUTPUT      finalize: color management (linear→sRGB/wide-gamut), tonemap/
                   exposure, dithering, resolve/downsample (SSAA/MSAA) → ViewTarget
                   (ImTextureID) the editor blits
        │
        ├─ PICKING (async): R32UI id pass · readback N-1 frames · HitResolver
        └─ SYNC: timeline semaphores · frames-in-flight (2–3) · the main ImGui
                 pass waits on the offscreen semaphores
        ▼  (the editor draws overlays/HUD/grid in ImGui OVER the ViewTarget)
GPU MEMORY (VMA): transient ResourcePool (aliasing) · VertexPool · UBO ring ·
    texture/glyph atlas · bindless DescriptorHeap · PipelineCache
        ▼
OPTIMISATIONS: dirty-region/tiling · geometry cache (hash+LOD) · indirect draw ·
    bindless · disk pipeline cache · async compute (FX / heavy tessellation)
```

### Pass catalogue — *logical, not thematic*
| Pass | Generic role | Hosted features (present & future) |
|---|---|---|
| **P1 Content** | produce a leaf's intrinsic pixels | vector fill (solid/gradient/pattern), stroke (cap/join/dash/align), images/photo, brush/paint, SDF text |
| **P2 Mask/Clip** | modulate coverage (generic alpha source) | clip path, layer mask, vector/raster mask, clip-to-below, **booleans** (coverage), **subtractive** (Porter-Duff op) |
| **P3 Filter/FX** | transform a layer's pixels | blur, shadow/glow, color adjust, LUT3D, sharpen, distort, **adjustment layers** |
| **P4 Composite** | merge layers | native & custom blend modes, opacity, isolation groups, group flatten |
| **P5 Output** | finalize for display | color management, tonemap, dithering, SSAA/MSAA resolve |
| **Picking** | per-pixel identity (async) | precise curve/vertex/handle selection, no CPU raycast |

---

## 3 — Folder layout (complete engine; industrial-grade)

New subsystem = dedicated static lib **`Compositor`** (`namespace Comp`), sibling
of `Renderer`. Depends on `Renderer` (shared `Renderer::Document`) + Vulkan + VMA.
The `Document` stays the shared data; both engines read it. The contract header
lives in `Renderer`.

> **This tree is the TARGET (all lots done), grown lot by lot — not scaffolded
> empty up front.** A logical stage gets its file/folder when its lot lands, not
> before (empty placeholders would be noise). What exists *today* (post-Lot 7a):
> `Engine.{h,cpp}` + `Internal.h` (shared helpers + push layouts) + the GPU layer
> (`GPU/Allocator`, `GPU/VmaImpl`, `GPU/Targets`), `Pipelines/Pipelines.cpp` (all
> PSOs), `Frame/Signature.cpp` (content signature), and `Passes/RenderView.cpp`
> (the P1→P2→P4 orchestrator). The Engine class is deliberately **split by concern
> across several .cpp** (one class, many TUs — CLAUDE.md "File Organisation"); as
> more stages land (a real `Graph/`, distinct `Passes/`, `Effects/`, `Scene/`,
> `Text/`, …) each is promoted to its own file/folder rather than piling into
> `Engine.cpp`. Concretely the next promotions: `Passes/PickingPass` (Lot 8),
> per-mode pipeline files under `Pipelines/` if they multiply, `Scene/` +
> `Graph/` when the layer stack (Lot 11) makes a declarative graph worthwhile.

```
src/Renderer/include/Renderer/IViewRenderer.h   # shared contract (legacy + compositor)

src/Compositor/
├─ include/Compositor/
│  ├─ Engine.h                 # public façade, implements IViewRenderer
│  ├─ EngineConfig.h           # feature flags, limits, working-space
│  ├─ Scene/    {Scene, LayerNode, GroupLayer, VectorLayer, RasterLayer, TextLayer,
│  │             AdjustmentLayer, MaskRef, BlendMode, Transform2D, Bounds,
│  │             DocumentAdapter}
│  ├─ Geometry/ {Tessellator, StrokeBuilder, CurveFlatten, PathBoolean, Mesh,
│  │             GeometryCache}
│  ├─ Frame/    {FrameContext, DirtyTracker, Culler, RenderList, FrameBuilder}
│  ├─ Graph/    {FrameGraph, PassBuilder, GraphResource, ResourcePool, Barrier}
│  ├─ Passes/   {Pass, ContentPass, VectorContent, RasterContent, TextContent,
│  │             MaskPass, FilterPass, CompositePass, OutputPass, PickingPass}
│  ├─ Pipelines/{PipelineLibrary, Fill, Stroke, Gradient, Pattern, Raster, Blend,
│  │             Filter, Mask, Picking, PipelineCacheStore}
│  ├─ Effects/  {Effect, EffectGraph, GaussianBlur, Shadow, ColorAdjust, Lut3D,
│  │             Sharpen}
│  ├─ Text/     {FontManager, GlyphAtlas, TextShaper}
│  ├─ GPU/      {Device, Allocator(VMA), GpuBuffer, GpuImage, Sampler,
│  │             DescriptorHeap, CommandContext, RingBuffer, StagingPool, Sync,
│  │             ViewTarget, ShaderModuleCache}
│  ├─ Color/    {ColorSpace, Tonemap}
│  └─ Util/     {Handle, Hash, Arena, GpuProfiler}
├─ src/…                       # mirror .cpp (one per concern; split when long)
│  └─ GPU/VmaImpl.cpp          # #define VMA_IMPLEMENTATION (single TU)
└─ shaders/                    # GLSL → SPIR-V (dedicated glslc target → shaders/compositor)
   fill · gradient · pattern · stroke · raster · blend · mask · blur.comp ·
   shadow.comp · color_adjust.comp · lut · sharpen.comp · composite · output ·
   picking
```

**Arch rules**: `GPU/` knows nothing of the scene; `Passes/` touch Vulkan only via
`GPU/`; `FrameGraph` declares dependencies and barriers are inferred; shaders
compiled offline; non-native blend modes in custom shaders; editor/tools stay in
`src/Application/` and are pipeline-agnostic.

---

## 4 — Coexistence & switching (clean swap; one engine alive)

- Shared interface **`Renderer::IViewRenderer`** (extracted from the current
  `CanvasRenderer` signatures): `Initialize / Shutdown / IsInitialized /
  BeginFrame / RenderView / EndFrame / FrameWaitSemaphores / GetMetrics` + the
  one-shots `RenderToRGBA` / `RenderGlyphCached` (`.acu` thumbnails + Symbol
  Viewer). `struct Metrics` + `struct Camera` live in the contract header so the
  numbers are directly comparable.
- `CanvasRenderer` (legacy) `: public IViewRenderer` (override; logic unchanged).
  `Comp::Engine` (new) implements the same interface.
- **Application** holds `std::unique_ptr<Renderer::IViewRenderer> renderer_` +
  `Renderer::IViewRenderer::Kind`. All call sites go through `renderer_->…`.
- **Transparency must go ALL the way down the ImGui stack.** The composite is
  recorded FIRST in the main pass (under all of ImGui), so EVERY opaque ImGui
  layer above the canvas hides it. It's not enough to make the viewport *zone*
  transparent — the fullscreen host window `##MainLayout` also paints an opaque
  `WindowBg` (the app base) that covered the composite. Fix: `##MainLayout` is
  given `NoBackground` when `renderer_->PresentsViaSwapchain()` (MainUI.cpp), so
  the swapchain clear (same app-base colour) shows in the gaps and the composite
  shows in the transparent viewport zone. (Lot 2 bug: the canvas rendered nothing
  until this — a solid-magenta composite test confirmed the quad never reached the
  swapchain because the host bg overdrew it.)
- **Global switch** = clean swap: `vkDeviceWaitIdle; renderer_->Shutdown();
  renderer_.reset(); renderer_ = Make(kind); renderer_->Initialize(...)`. The
  inactive engine no longer exists → zero cost, no desync. Brief hitch on switch.
  The swap is **staged** (`pendingRendererKind_`) and applied at the **top of the
  next Update, between frames** — never mid-frame, or the in-flight ImGui draw
  data would still reference the outgoing engine's offscreen textures we tear down
  (→ GPU hang → TDR → `VK_ERROR_DEVICE_LOST`).
- Toggle exposed in **Preferences ▸ Dev** (`SettingsWindow::SetDevPageExtra` hook,
  so the UI lib stays decoupled from Application) — a test tool.
- **Single branch point**: `Viewport.cpp` `RenderView` call site.

---

## 5 — Roadmap (lots)

- [x] **Lot 0 — Scaffolding, contract, switch infra.**
  - [x] `IViewRenderer.h` + shared `Metrics`/`Camera`; `CanvasRenderer` implements it.
  - [x] Application routes through `unique_ptr<IViewRenderer> renderer_` +
        `rendererKind_`; legacy-only ⇒ behaviour identical (regression guard).
  - [x] `Compositor` static lib skeleton (`Comp::Engine` stub implementing the
        interface, `RenderView` returns 0 ⇒ blank canvas) + CMake wiring
        (add_subdirectory + link in root and Application).
  - [x] Dev-panel toggle ("Render Engine" section) + clean swap
        (`Application::SwitchViewRenderer`).
  - [x] Commit scope `compositor` added to CLAUDE.md.
  - → VMA via FetchContent + modern device features **moved to Lot 1** (kept next
        to their first use, to keep Lot 0 a low-risk pure-refactor regression
        guard).
- [~] **Lot 1 — GPU layer + Graph + output (end-to-end plumbing).**
  - [x] **1a** — VMA via FetchContent + `GPU/Allocator` (created in
        `Engine::Initialize`); modern device features enabled in `SetupVulkan`
        (Vulkan 1.3: dynamicRendering + synchronization2 + timelineSemaphore +
        descriptor indexing) via query-then-enable, with instance `apiVersion` set;
        `Application::compositorSupported_` gates the Preferences ▸ Dev toggle (and
        `MakeViewRenderer` falls back to Legacy if unsupported). `VkInstance` added
        to `IViewRenderer::Initialize`. Compositor still renders blank.
  - [x] **1b** — per-view `ViewTarget` (VMA image + view + blit descriptor),
        per-view submit slot (binary semaphore + fence), offscreen pass cleared via
        **dynamic rendering** (`vkCmdBeginRendering`), and a **pure-Vulkan composite
        onto the swapchain** (`Engine::CompositeMainPass`, a textured quad recorded
        into the main render pass under ImGui's draw data). ImGui never touches the
        canvas. Presentation choice (user): **separate Vulkan pass + transparent
        zone** — the Viewport zone paints no opaque ImGui bg when the Compositor is
        active (`ZoneLayout::SetCanvasZoneTransparent`, gated to `core.viewport`),
        so the composite shows through; the Viewport reports its dest rect in NDC
        via `PlaceView` and repaints only its ruler strips. Geometry is Lot 2 (the
        offscreen pass is clear-only for now → an empty backdrop-coloured canvas).
  - [ ] `Graph/` (FrameGraph + ResourcePool) + the rest of `GPU/` (CommandContext,
        DescriptorHeap, ShaderModuleCache) land alongside Lot 2 as needed.
  - Known Lot-1b cosmetics (refine later): zone rounded corners + ruler-band
    blending are approximate under the transparent zone; resize does a
    `vkDeviceWaitIdle` (stutters while dragging a splitter).
- [x] **Lot 2 — P1 vector content (base parity).** The offscreen pass now draws
      the real document: `Tessellator::BuildDocumentSegmented` (cover/decor = null
      ⇒ fills + strokes + patterns + decorators **baked** into one mesh) → per-view
      VMA vertex buffer, rebuilt only on a content-signature change
      (`Engine::BuildSignature`, pan/zoom within a detail bucket reuse it) with the
      shared per-shape `Tessellator::Cache`. Drawn via a dynamic-rendering shape
      pipeline (`shaders/compositor/shape.*`, camera identical to the legacy) +
      **2× SSAA** (offscreen rendered 2× larger, downsampled by the composite's
      linear sampler). Visual parity for fills/strokes/patterns/decorators on the
      current document.
      Known gaps (later lots): transparent strokes/patterns are baked (may show
      alpha-doubling seams — Lot 3 brings the stencil-coverage path); no per-page
      `clipContents` scissor yet; the global `Tessellator` detail scale isn't
      pinned per-view.
- [x] **Lot 3 — P1 fills/patterns + P2 Mask (full parity = v1 milestone). ✅ v1 PARITY REACHED.**
  - [x] **3a** — per-page scissor + per-page draws from the `PageSeg` structure
        (baked build): each page's geometry is clipped to its own rect (no overflow
        over the void / a foreign page; honours `clipContents`). `px = (doc·unitScale
        − pan)·zoom·ssaa`, matching shape.vert.
  - [x] **3b — P2 Mask/Coverage stage.** Stencil attachment on the view target
        (dynamic rendering, D24_S8) + the coverage build
        (`BuildDocumentSegmented(..., outCover=&cover, outDecor=null)` ⇒ patterns →
        cut-polygons + `SurfaceDraw`, transparent strokes → ribbon + `StrokeDraw`;
        decorators stay **baked** into the base, so no instanced-decor pipeline /
        base meshes needed) + 3 pipelines on one 96-byte push layout (stencil-mask
        REPLACE / pattern-fill procedural EQUAL / stroke-fill EQUAL, stencil ref
        dynamic per surface). Per-object render loop (base → cut-poly→stencil +
        procedural motif masked → ribbon→stencil + bbox-quad filled once). Fixes the
        transparent-stroke alpha-doubling seams + clips patterns exactly to the
        contour (procedural motif, not baked elements). Reuses `pattern_fill.{vert,
        frag}` (ported) + the Tessellator coverage geometry. **v1 parity milestone.**
        Note: per-object draws are the correct trade-off here; GPU-driven indirect
        is a Lot 10 optimisation.
- [x] **Lot 4 — P4 Composite/blend** (per-object opacity + blend modes; the first
      capability the legacy lacks).
  - [x] **4a-1** — per-object **opacity**. `Shape::opacity` (additive Document
        field, `.acu` v29, default 1) + `ObjDraw::opacity` (Tessellator copies it) +
        Properties "Opacity" slider. The Compositor bakes it into the base + stroke
        alpha (and the pattern push); the build signature includes it. The legacy
        ignores it (renders opaque) — demonstrates the Compositor's value. *Known:
        bake doubles alpha on a stroked object's self-overlap at opacity<1 — fixed
        by 4a-2.*
  - [x] **4a-2** — per-object **isolation** (the real P4 infra). An object with
        opacity<1 is rendered at FULL opacity into a lazily-allocated isolation layer
        (its own color+stencil, page-clipped dynamic-rendering pass), then composited
        onto the canvas with its opacity (`iso_composite.*`) — correct self-overlap,
        no alpha doubling. The canvas pass is paused/resumed (LOAD) around each
        isolation, with a store→load barrier preserving it. Opacity is no longer
        baked. *Note: per opacity<1 object = 2 passes + 3 barriers (z-order
        interleaving is inherent); GPU-driven batching is a Lot 10 optimisation. The
        same isolation hosts blend modes (4b).*
  - [x] **4b** — per-object **blend modes** (Multiply/Screen/Overlay/…/Luminosity,
        the 16 W3C/SVG modes). `Shape::blendMode` (additive Document field, `.acu`
        v30, default Normal) + `ObjDraw::blendMode` (Tessellator copies it) +
        Properties blend-mode dropdown. Built on the 4a-2 isolation: an object with a
        non-Normal mode is isolated at FULL opacity, then the canvas page region is
        copied (`vkCmdCopyImage`, outside any pass) into a lazily-allocated
        **backdrop** image; the blend composite samples {iso = src, backdrop = dst}
        and runs the mode math in `iso_blend.frag` (separable per-channel +
        non-separable HSL Hue/Saturation/Color/Luminosity), writing
        `mix(dst, blend(dst, src), src.a·opacity)`. Chosen over the advanced-blend
        device extension (self-contained, no device-extension coordination, exact
        W3C control). Normal+opacity keeps the cheaper `iso_composite` path (no copy).
        *Note: per blend object = 2 passes + 1 image copy + 8 image barriers; the
        backdrop is page-region-only and reused across objects. The legacy ignores
        blendMode (renders opaque, Normal).*
- [~] **Lot 7 — P2 advanced**: rich masks, **booleans**, **subtractive**.
  - [x] **7a — erase / subtractive** (Affinity's erase blend mode). `Shape::erase`
        (additive Document field, `.acu` v31, default false) + `ObjDraw::erase`
        (Tessellator copies it) + a Properties "Erase (knock-out)" checkbox (overrides
        the blend mode, which it disables in the UI). Built on the 4a-2 isolation: an
        erase object is isolated at full opacity, then composited with a **dst-out**
        pipeline (`srcFactor=0, dstFactor=1−srcA` on both colour and alpha) — reusing
        the iso layout + shaders, no backdrop copy. Result: `canvas.a →
        canvas.a·(1−src.a·opacity)`. Cheapest of the iso paths (one fixed blend state,
        no extra shader/sampler). The legacy ignores erase. *Priority pick over
        booleans: reuses Lot 4 infra and is the erase mode the future Layers/Groups
        (11b) need.*

        **Page substrate is NOT a layer (correctness fix).** Erase must cut the OBJECT
        STACK down to the page (revealing the page background), never punch through the
        page itself — the page bg is a display substrate (white / grid / transparent,
        and always exports white-empty). So per page the render order became: **clear
        page region → object stack (Normal/opacity/blend/erase between them) → page
        substrate LAST, DST-OVER** (`backdropPipeline_`, same layout/shaders as
        `shapePipeline_`, `srcFactor=1−dstA, dstFactor=1`). The substrate slides under
        the stack and under any erase hole, so an erase reveals the page, not the app
        bg. Consequence: the page bg is also **excluded from blend modes** (a Multiply
        object multiplies with the objects below it, not with the white page) — which
        is the intended semantics; `iso_blend.frag` now lerps the blended colour toward
        the source where the (object-only) backdrop is transparent and preserves the
        backdrop's own coverage. *Scope note: erase currently affects the whole page
        object stack; once Layers/Groups (11b) land, erase will be scoped to its
        group/layer (and Collection-mode modifiers will target a specific object/group)
        — the user agreed to defer that until groups exist.*
  - [ ] **7b — booleans** (union/inter/diff/xor, `PathBoolean` on contours) + rich
        masks (clip-to-below, vector/raster mask) — needs an inter-object reference in
        `Shape` (modifiers), a larger geometry effort. **Comes AFTER Lot 8** (user).
- [x] **Lot 8 — Picking** (GPU id-pass + async readback). `IViewRenderer::Pick(key,
      px,py)→Shape::id` (default 0 → caller's CPU hit-test; legacy has no id-pass).
      `ObjDraw::shapeId` carries the object identity. Per view (lazy on first pick):
      an **R32UI id image** + a 1-px host readback buffer + a fence. In RenderView,
      when a view was asked to pick, a separate dynamic-rendering pass into the id
      image draws each object's base geometry with a 1-based objectId (painter order
      → topmost wins per pixel), building `pickIds[objectId-1] = shapeId`; the
      requested pixel is then `vkCmdCopyImageToBuffer`'d out. The submit's fence is
      remembered; `Pick()` next frame polls it **non-blocking** (`vkGetFenceStatus`),
      reads the id, resolves via the in-flight map, and caches the last result per
      pixel. The Viewport's Select tool primes `Pick` every hovered frame (so the
      async result is ready at click) and uses it when non-zero, else the CPU
      `PickShape` (which stays for the legacy engine + as the click-frame fallback).
      Shaders: `picking.{vert,frag}` (reuse shape.vert projection + a uint id push).
      *Note: async N−1 by design — no stall; sub-element (vertex/edge/handle) ids are
      a later extension of the same pass.*
- [⏸] **Lot 5 — P3 Filter/FX + Effects** (blur, shadow, color, LUT) + adjustment
      layers. **DEFERRED** (user priority: vector rendering before filters/FX/raster).
      Wire-in only if a later lot needs it; full implementation later.
- [⏸] **Lot 6 — RasterLayer / RasterContent** (images & photo editing). **DEFERRED**
      with Lot 5 (same priority call).
- [⏸] **Lot 9 — Text** (SDF atlas, shaping). **DEFERRED** — needs dedicated, precise
      work done together with the **Typography Module** (`src/Modules/Typography/`),
      not a generic engine lot in isolation.
- [ ] **Lot 10 — Optimisations**: dirty-region/tiling, indirect draw, full
      bindless, async compute, disk pipeline cache.
- [ ] **Lot 11 — Promote the layer stack** into `Renderer::Document` + `.acu`.
- [ ] **Lot 11b — Layers & Groups (Affinity-style), beyond per-object.** Today every
      compositing/FX param (blend, opacity, and later filters) lives **on the object**,
      and the Outliner has only two views: the **Object view** (visual hierarchy =
      collections / unrelated groups, OR relational hierarchy = parent/child, and
      *later* object/collection references for modifiers — booleans, instancing, …,
      Blender-like but not yet implemented), and the **Layer view**, which currently
      only reorders the **Z-index** of objects independently of parenting/collection.
      This lot adds a real **Layer / Group container** (Affinity-like): a node that
      groups objects (or other layers) and itself carries **blend mode, opacity,
      filters, and an *erase* blend mode** (knock-out), composited as a unit. Each
      layer/group shows an **isolated rendered preview** (the node composited apart
      from the rest, like Affinity's thumbnails) — which the Compositor's per-node
      isolation targets (the 4a-2 / 4b iso layer) are the right mechanism to produce.
      Couples with Lot 11 (the layer stack must live in the document/`.acu`) and reuses
      P4 isolation + (deferred) P3 filters at the *group* level, not just per object.

  **Order (user): 11 → 12 → 10. Lots 11 + 11b done together (user), as vertical
  sub-lots:**
  - [x] **11-1 — Model: a layer GROUP in `Document`.** A group is a LAYER, not an
        organisation node. Design (revised after the first attempt put groups in the
        collection tree and broke page attachment): the group is a `Collection` with
        `isLayerGroup = true` that carries ONLY the compositing params + name and is
        **kept OUT of the collection/page tree** (`parentId = 0`, no children); objects
        link to it via a SEPARATE `Shape::groupId` (distinct from `collectionId`, which
        stays the organisation link untouched). Groups are **page-local** (all members
        on one page, enforced by `AllOnSamePage` at group time). Helpers: `IsGroup`,
        `GroupOfShape` (= `Shape::groupId`), `GroupSelection` (refuses cross-page) +
        `MakeGroupContiguous` (members adjacent in draw order → the renderer isolates a
        group as one run), `Ungroup` (clears groupId, drops the group collection),
        `SetGroupVisible`/`GroupHidden` (the Layers eye).
  - [x] **11-2 — `.acu` persistence** (v32: isLayerGroup + opacity + blendMode + erase
        on each collection; v33: `Shape::groupId`. Older files default to no group.)
  - [x] **11-3 — Group rendering (Compositor).** `ObjDraw` carries `groupId` + the
        group's `opacity`/`blend`/`erase` (Tessellator's `resolveGroup`, const-correct).
        RenderView walks the stack and, for a maximal run of consecutive objects with
        the same non-zero groupId, isolates the WHOLE run into the iso layer and
        composites it once with the group params — `isolateObject` was generalised to
        `isolateInto(fillIso, …, opacity, blend, erase)` (one object → fillIso draws
        one; a group → fillIso draws the run). *v1: group members composite Normal
        between them (per-object blend/erase INSIDE a group renders as its plain fill);
        full nested isolation is a later refinement.*
  - [~] **11-4 — Editing.** Done: **Group** (Ctrl+G, refuses cross-page) / **Ungroup**
        (Ctrl+Shift+G) actions; group opacity/blend/erase in the **Properties** panel
        (when the active object is in a group); group params + `Shape::groupId` mixed
        into the Compositor content signature so edits apply in real time. **Outliner**:
        Collection view shows a `[GroupName]` badge on each member's row (a group has NO
        hierarchy of its own there — it's a layer, not an organisation node); Layers
        view shows a **group header row** over the contiguous members with a working
        **eye** (`SetGroupVisible`) + collapse. TODO: group reordering / drag in the
        Layers view; nested groups.
  - [~] **11-4b — Group as a first-class layer (Affinity parity, in progress).** Done:
        **Erase is now a BLEND MODE** (`BlendMode::Erase`, appended last; `.acu` v34
        folds the old separate erase byte in via migration; UI dropdowns list "Erase";
        Compositor routes `blendMode==Erase` to the dst-out pipeline) — no more separate
        checkbox. **Group selection**: a group is its own selectable entity
        (`Document::activeGroup_` + `SelectGroup` selects the members too); clicking the
        Layers header, right-click ▸ **Select Group**, or **G+click** in the viewport
        select it; the Properties panel then shows the GROUP (name/opacity/blend).
        **Drag in Layers**: dropping an object onto another adopts the target's groupId
        (drag a member OUT → groupId←0; onto a grouped object → joins it), with the
        touched groups recompacted — fixes the duplicate-ImGui-id crash. TODO: drag a
        whole GROUP up/down (z-index) + group-via-drag/nesting; **intra-group blend** (a
        member's own blend/erase composites against the members below it within the
        group iso — `drawMemberIntoIso` is wired but currently draws members Normal; needs
        a 2nd isolation level).
  - [x] **11-4c — Crash + data-loss fixes (2026-06-24).** (1) **use-after-free crash**:
        a Layers drop mutated `ab.shapes` (vector move) DURING the row loop → ImGui drew
        a freed string. Fixed: drops record a deferred `OutlinerReorderReq`, applied by
        `ApplyOutlinerReorder()` AFTER the Outliner is drawn. (2) **objects on other
        pages / loose vanished** (legacy + compositor → data bug): `MakeGroupContiguous`
        moved every shape out of a vector FIRST and only THEN checked `members.empty()`,
        leaving non-member pages full of moved-from shells. Fixed: early-out before
        moving anything. (3) **group selection**: `SelectGroup` now selects the group as
        an ENTITY (does NOT add members to the object selection); every object-select
        path (`SelectOnly/SelectAdd/SetActive/ClearSelection/DeselectAll`) clears
        `activeGroup_`, so Properties stops being stuck on the group. (4) group header is
        now a **drag source + drop target** (reorder the whole group's z-index; drop an
        object onto a header to join the group) — deferred-applied.
  - [~] **11-4d — Group as a transformable object (in progress).** Done: a selected
        group **transforms as a unit** — `SelectionWithDescendants` + `ComputePivot` +
        `Action_BeginTransform`'s guard treat `ActiveGroup()` as its members
        (`Document::GroupMembers`), so G/R/S move/rotate/scale the whole group; the group
        header row now **lights up** as selected/active in the Layers view (forceSel /
        forceActive from `ActiveGroup()`). TODO: group-level fill/stroke applied to all
        children (a future gradient spanning the group as one), per-child re-override, and
        a per-object **lock** to exclude it from group-wide edits.
  - [x] **11-4e — Hierarchical blend (Affinity/PS) + NESTED GROUPS — DONE.** Blend is
        GENERAL across the layer hierarchy: each object/layer blends with everything
        composited BELOW it in the SAME level; a container (group) isolates, composites
        its children (each with their own blend, against each other), then merges its
        result up to its parent with the container's blend/opacity — recursively.
        **Nested groups**: a group is a `Collection{isLayerGroup}` whose `parentId` is
        its PARENT GROUP (0 = top-level on its page); an object's `Shape::groupId` is its
        INNERMOST group; the chain is groupId → parentId → … (`GroupChainFrom`,
        `ShapeInGroup`, `GroupInGroup`). `GroupSelection` nests under the selection's
        deepest common group; `Ungroup` re-homes objects + sub-groups to the parent;
        drag a group onto another header re-parents it (cycle-guarded). `ObjDraw.groups`
        carries the full outermost→innermost chain (Tessellator `resolveGroup`).
        **Renderer**: `ViewTarget::IsoLevel` STACK (`EnsureIsoLevel(depth, withBackdrop)`,
        lazy per depth) + `beginLevel(depth, load, area)` + a recursive
        `composeRange(objs, lo, hi, depth, sc)` driving `isolateOnto(fillIso, sc, op,
        blend, depth)` — isolate onto level depth+1, composite onto level depth (blend
        reads the target level as backdrop). Builds, links, runs (no crash/validation
        error at startup). Visual parity check pending (F7).
  - [x] **11-5 — Isolated previews — DONE (incl. Compositor).** Layers-view rows are
        ×2.5 taller (`UI::ListRowSetBandScale(2.5)`); `OutlinerPreviewSlot` draws a white
        card + an isolated render of the object (or all a group's members) via
        `RenderGlyphTexture`, cached by content hash, replacing the type icon. The
        Compositor's `RenderGlyphCached` is now IMPLEMENTED (was a 0-stub → blank on
        Compositor): a per-key `GlyphTex` cache (small VMA image + view + ImGui texture
        via `ImGui_ImplVulkan_AddTexture`), auto-framed, tessellated through the document
        path (base mesh) and rendered SYNCHRONOUSLY (one-shot cmd buffer + fence wait).
  - [x] **Premultiplied-alpha fix (per-group greying bug).** A transparent object inside
        a group rendered darker, more so per nesting level. Cause: the isolation layer is
        PREMULTIPLIED (objects drawn into a transparent target with src-over premultiply
        rgb·a), but `isoCompPipeline_` composited it with `srcColour = SRC_ALPHA` (straight)
        → rgb multiplied by alpha a SECOND time per level. Fix: composite premultiplied-
        over (`srcColour = ONE`); `iso_composite.frag` outputs `texel·opacity` (keeps it
        premultiplied); `iso_blend.frag` un-premultiplies src+backdrop for the W3C math
        then re-premultiplies the output.
  - [ ] **11-6 — Group-level filters** — BLOCKED on P3/Lot 5 (no filter system exists
        yet; nothing to apply). Deferred with Lot 5.
- [~] **Lot 12 — Overlay/HUD pass (canvas overlays in Vulkan, full-GPU).** Scope
      (user): move the RENDERING of the canvas overlays off ImGui into the Compositor —
      the editor PANELS/menus stay ImGui (a full UI rewrite is out of scope). Today the
      editor draws ~130 ImGui `dl->Add*` calls for selection outlines, origin points,
      linked-object lines, edit-mode handles/vertices, page outlines, the 2D cursor,
      grid, rulers and the metrics HUD — over the canvas, so they show on BOTH engines.
      This lot makes the Compositor own them; tool/selection STATE stays CPU in
      `src/Application/`. Sub-lots:
  - [x] **12-1 — Infra DONE.** Contract: `IViewRenderer::OverlayVertex {x,y,rgba}` +
        `SubmitOverlay(verts, indices)`. Compositor: `overlay.{vert,frag}` (NDC pos +
        unpacked RGBA), `CreateOverlayPipeline(renderPass)` (alpha-blended triangles,
        built lazily for the main RP like the blit), host vbo/ibo filled by
        `SubmitOverlay`, drawn in `CompositeMainPass` AFTER the canvas blit / UNDER ImGui;
        freed in Shutdown. Editor: `App::OverlayList` (Util/) — a tiny immediate-mode
        builder (line/rect/circle/convex-fill/polyline → triangles in screen px), `ToNDC`
        against the main viewport, submitted before EndFrame when the engine presents via
        swapchain (legacy stays on ImGui). Validated with a debug frame rect. Builds/links.
  - [ ] **12-2 — Geometry primitives + migration.** Editor-side tessellation helpers
        (thick AA line, rect/filled, circle/filled, polyline, convex fill) that push
        triangles into the OverlayList; migrate the geometric overlays (selection,
        handles, page outlines, parent lines, grid, 2D cursor) off `dl->Add*`.
  - [x] **12-2 — Geometry primitives + migration DONE.** `OverlayList` gained AA
        (1px transparent fringe on lines/circles, ImGui-style). A drop-in `OverlayDL`
        wrapper mirrors the ImDrawList API and routes GEOMETRY to the GPU overlay when
        the Compositor presents (else ImGui); text/images stay ImGui. All ~130
        `dl->Add*` canvas overlays (selection outlines, handles, page outlines, parent
        lines, grid, 2D cursor, curve/line-mark previews) across the 11 viewport files
        migrated (`ImDrawList* dl` → `OverlayDL& dl`, `dl->` → `dl.`; ImGui-only calls
        via `dl.imgui()->`). The metrics HUD + Pages list stay ImGui (text-heavy).
        Legacy unchanged. Verified by user: overlays render full-GPU on Compositor.
  - [—] **12-3 / 12-4 — Text + rulers: NOT NEEDED (user).** Frontier locked: the
        real-time CANVAS rendering (overlays, selection outlines, curves, previews) is
        Vulkan; the INTERFACE (menus, panels, AND the RULERS — they're chrome, not
        canvas) stays ImGui. There is no text ON the canvas today, so no glyph atlas is
        needed for Lot 12. **Lot 12 considered done** for its real goal.

---

## Lot 13 — GPU-driven geometry (the real perf rework)

**Goal (user):** Blender-class fluidity — thousands of complex static objects (with
transparency / blend modes) at silky zoom, AND real-time editing of very heavy objects,
all GPU-driven. Today the Compositor gives NO FPS gain over legacy because BOTH share the
CPU `Renderer::Tessellator` and its cache; the Compositor only adds work (isolation, 2×
SSAA, multi-pass). The geometry path must move onto the GPU and the engine must stop
re-touching every object every frame.

### Diagnosis (where the time goes today)
- `Tessellator` already caches per-shape meshes (an unchanged shape isn't re-tessellated;
  pan/zoom is free — camera lives in the vertex shader). Good, but:
- **Per frame, O(N objects)**: `BuildSignature` walks EVERY shape to hash it; a detail-
  bucket change (zoom) or any page move rebuilds the WHOLE segmented document and
  **re-uploads the entire VBO**. With thousands of objects this is the CPU wall — and
  it's identical legacy vs Compositor.
- Curve flattening / stroke expansion / triangulation are CPU (the cache hides it when
  static, but heavy edits re-run it every frame on the CPU).

### Strategy — incremental, each step measurable (NOT a big-bang rewrite)
Build it lot by lot under `Compositor/Geometry/`, `Compositor/Frame/`, `Compositor/GPU/`,
moving toward the industrial folder layout. Migrate off the shared `Tessellator` piece by
piece; keep legacy on the old path (it stays as the comparison baseline).
- [x] **13-0 — MEASURE first — INSTRUMENTED (build OK).** `Metrics` gained `sigMs`
      (signature O(N) walk), `tessMs`, `uploadMs`, `recordMs`, `gpuMs` (GPU timestamp
      query, 2 per submit-slot, read back after the fence — no stall), plus the cache
      counts (built/cached/culled/drawn) now piped from `cache_`. The viewport HUD shows
      the breakdown (line 4: `sig/tess/up/rec ms`, line 5: `gpu / gpu-wait ms`).
      **AWAITING USER PROFILE**: open a heavy scene (thousands of complex objects) on the
      Compositor, read the HUD while (a) idle/pan/zoom and (b) editing a heavy object.
      The dominant number picks the next step: sig/tess/upload dominate → 13-1 (DirtyTracker
      + persistent pool); gpu ≈ frame time → GPU-bound, focus fragment/overdraw; gpu-wait
      high with low gpu → CPU-bound elsewhere.
- **13-1 — Kill the per-frame full rebuild + re-upload (the static-masses wall).**
  Today ANY change (edit one object, move a page, cross a zoom bucket) bumps the ONE
  global signature → the WHOLE segmented document is rebuilt (cache spares the ear-clip
  but every shape is re-walked + re-copied cache→scratch) AND the ENTIRE VBO is
  re-uploaded. With thousands of objects, editing one costs O(all). Fix: a persistent
  per-shape GPU pool so only the changed shape's slice is rewritten.
  - [x] **13-1a — Per-shape dirty detection — build OK.** `Compositor/Frame/DirtyTracker.h`:
        per-view `Shape::id → hash` baseline; each build `Begin()`/`Feed(id, hash)`/`Finish()`
        → a diff (added/changed/removed). `BuildSignature` became `BuildSignatureAndDiff`:
        ONE O(N) walk now yields BOTH the rebuild-gate signature AND the per-shape diff
        (the per-shape hash folds geometry+paint+transform+opacity+blend+group+bucket, and
        is mixed into the global sig). The HUD line 2 shows `dirty N` (shapes actually
        changed since last build). This tells us WHAT changed, not just THAT something did;
        it drives 13-1b and, right now, exposes the over-rebuild (edit 1 of thousands →
        `dirty 1` but a full rebuild + full VBO upload still fires). No render-behaviour
        change → no regression risk. **AWAITING USER**: on a big scene, edit one object and
        confirm `dirty` shows ~1 while `up`/`tess` still reflect a full rebuild — quantifies
        the 13-1b win before building the pool.
  - **13-1b — Persistent per-shape vertex pool.** `Compositor/GPU/ShapePool.{h,cpp}`: a
        generic host-visible, persistently-mapped pool with a best-fit free-list. Each
        shape owns a STABLE slice (rounded-up capacity so small edits reuse it in place);
        `BeginFrame`/`Write`(dirty)/`Touch`(unchanged)/`EndFrame`(free removed)/`FlushWrites`
        (one ranged flush). A grown shape re-allocs (old block coalesced); a removed shape
        frees its block.
    - [x] **13-1b (fills) — pool the fill stream + upload only dirty — build OK.** The
          Lot-13-4 stencil-then-cover fill stream (`FanVertex`: each pure-fill object's
          fans + its 6-vert cover quad are contiguous) now lives in a per-view `fillPool`.
          On a rebuild, an object the dirty tracker marks changed/added rewrites its slice
          (`Write`); an unchanged object is `Touch`ed (kept, no re-upload). `FillObject`
          fanFirst/coverFirst become pool-relative and the draw binds `fillPool.Buffer()`.
          So editing one pure-fill object re-uploads ONLY its slice; the other thousands
          are never re-copied to the GPU. (`up` drops for the fill stream.) `dirtyIds` is
          exposed by the tracker to gate Write vs Touch. **AWAITING USER F7**: pure-fill
          scene identical; editing one object shows `up` far lower than before (only 1
          slice), `dirty ~1`.
    - [ ] **13-1b (base+cover) — pool the baked streams.** The baked base VBO (mixes page
          backdrops + objects) and the cover VBO (patterns/strokes, with INTERNALLY SHARED
          offsets between layers) are trickier to slice per-object; still monolithic. Pool
          them once the fill pool is proven (backdrops get their own non-shape slots; the
          cover's shared sub-ranges are split per object).
    - [x] **13-1b-3 (fills) — skip re-flattening unchanged fills — build OK.** The fill
          build is now INCREMENTAL: `BuildDocumentFills` takes `dirtyIds` + the previous
          build's `id→FillObject` map. An unchanged object is COPIED from the prior build
          (its pool-relative offsets + geometry preserved, `fromScratch=false`) and NOT
          re-flattened; only dirty/new objects are flattened into scratch (`fromScratch=
          true`). The upload step then WRITEs only fromScratch objects and TOUCHes the
          rest. So for the fill stream, editing one object of thousands is O(dirty) for
          BOTH `tess` (flatten) AND `up` (upload) — the full O(all)→O(dirty) win. A zoom
          bucket change folds into the per-shape hash, so it correctly marks all fills
          dirty (re-flatten at the new detail). A group-param-only edit flattens nothing
          (0 dirty). **AWAITING USER F7**: on a big pure-fill scene, editing one object
          keeps `tess` AND `up` near-zero (`dirty ~1`); pan/zoom within a bucket free;
          crossing a bucket re-flattens all (expected). Still to pool: the baked base +
          cover streams (13-1b base+cover) for patterned/stroked/mixed objects.
  - [ ] **13-1c — Zoom-independent storage (optional).** Store geometry at a canonical
        detail and pick the LOD by range / on the GPU, so crossing a zoom bucket doesn't
        invalidate every slice. Deferred until 13-1a/b show bucket-cross is still a cost.
- [ ] **13-2 — GPU culling + indirect draw.** Per-object bounds on the GPU; a compute
      pass builds the visible draw list into an indirect buffer; one `vkCmdDrawIndexedIndirect`.
      Removes the per-object CPU draw loop.
- [ ] **13-3 — Bindless.** Descriptor-indexing for per-object params (colour, transform,
      blend) in SSBOs; no per-draw descriptor binds.
- **13-4 — Kill the ear-clip: stencil-then-cover fills.** The user's profile showed the
  bottleneck is CPU **tessellation while editing** (`tess` 20–30 ms; idle/pan/zoom already
  free — cache works). The interior triangulation (ear-clip, O(contour²)) is the cost. So
  the base fill moves to **stencil-then-cover** (NV path rendering / Skia): flatten the
  contour, fan it TRIVIALLY into the stencil under a non-zero winding rule (front INCR_WRAP
  / back DECR_WRAP — overlaps + holes resolve by winding, NO triangulation), then draw a
  bbox quad once where stencil ≠ 0. Cost drops to O(contour). The flatten stays CPU here;
  13-4c lifts it to compute.
  - [x] **13-4a — Coverage path + pipelines (CPU flatten, no ear-clip) — build OK.** New
        `Compositor/Geometry/FillGeometry.{h,cpp}`: walks the Document like the Tessellator
        but for each **pure solid-fill** object emits a winding fan + a cover quad (reusing
        the Tessellator's PURE outline helpers `OutlinePartSub*`, geometry maths — NOT the
        legacy renderer). Two new pipelines (`fill_stencil` winding write, `fill_cover`
        quad, `fill_cover.frag`) + a per-view fan VBO. `RenderView` routes a pure-fill
        object's base to fan+cover, skipping its baked base triangles; patterns / strokes /
        mixed objects keep the baked path (parity). **Eligibility is strict** (fill only,
        no stroke / fill-layers / marks / Polyline) so the first cut is safe + measurable.
        *Note:* the Tessellator STILL bakes those fills into the VBO (just not drawn), so
        13-4a is a **correctness** step — the FPS win lands in 13-4b.
        **AWAITING USER F7**: on the Compositor, a pure solid-fill scene must look identical
        to legacy (contours, holes, colours, AA); check for unfilled/leaking interiors.
  - [x] **13-4b — Skip the baked fill (the win) — build OK.** `BuildDocumentSegmented`
        gained an opt-in `skipShapeIds` set (default null = legacy path unchanged): a
        skipped shape's BASE is NOT tessellated/ear-clipped, but it STILL gets an ObjDraw
        (baseCount 0) so it keeps its z-order slot + group chain + compositing params. The
        render loop is untouched — `drawObjectContent` sees baseCount 0 and fills the base
        from `fillById` (fan+cover). `RenderView` now builds fills first, collects their
        ids, and passes them as `skipShapeIds` to the segment build. So a routed pure-fill
        object's interior triangulation never runs on the CPU → `tess` drops during heavy
        editing. (The stencil `vkCmdClearAttachments` per routed object resets its bbox so
        residual pattern/stroke stencil refs can't leak the cover — the ordering-bug fix.)
        **AWAITING USER F7**: heavy pure-fill editing should show `tess` far lower than
        legacy, with identical visuals + correct z-order (no bbox-coloured leaks).
  - **13-4c — Flatten in compute.** Move the contour flatten onto the GPU. Two hard
    facts shape the approach: (a) the fill flatten is ALREADY incremental (13-1b-3), so
    primitives (Rectangle/Ellipse — trivial, few verts) gain nothing from compute; the
    real cost is heavy Bézier/NURBS, which are the RISKIEST to port to GLSL; (b) the fill
    pool is a HOST-visible, CPU-`memcpy`-filled buffer, whereas a compute shader must
    write a DEVICE-local SSBO — incompatible fill models in one buffer. So compute needs
    a PARALLEL device-local path, not a drop-in. Built isolated + non-destructive first,
    so the proven CPU-incremental path keeps working while the GPU path matures.
    - [~] **13-4c-0 — Compute scaffold: shader + device buffer ready (build OK).**
          `shaders/flatten_prim.comp` (first compute shader in the engine) flattens
          PRIMITIVES (Rectangle/Ellipse) per object from a `PrimIn[]` SSBO into a fan SSBO,
          mirroring the CPU `WorldTransform` + contour + fan + cover-quad layout. Compiles
          (glslc). `Allocator::CreateDeviceBuffer` added (device-local SSBO, compute-write
          + vertex-read). **Held before wiring**: routing primitives through compute would
          REGRESS the 13-1b-3 CPU-incremental path (primitives would leave the incremental
          `fillPool` for a per-frame-recomputed device buffer) for ZERO gain — primitives
          are already trivial + incremental on the CPU. Compute only pays off for HEAVY
          CURVES with a device-local INCREMENTAL path, which is 13-4c-1 proper. So the
          scaffold stays compiled + isolated (no live effect) until the curve path is
          designed to be incremental too. The shader + device-buffer helper are the
          reusable groundwork; the dispatch/descriptor wiring lands with 13-4c-1.
    - [ ] **13-4c-1 — Bézier in compute (the win).** Once the scaffold is proven, flatten
          cubic Béziers on the GPU (chord-error segment count in the shader). Route heavy
          Bézier fills to the compute path; keep primitives + NURBS + everything else on
          the CPU-incremental path. This is where flatten actually leaves the CPU for the
          costly case.
    - [ ] **13-4c-2 — NURBS in compute.** Port the rational B-spline eval (weights/knots/
          endpoint/bezier/follow-curve) to GLSL. The hardest; last.
- [ ] **13-5 — Own Tessellator.** Once the GPU path covers the cases, the Compositor stops
      depending on `Renderer::Tessellator` entirely (the documented debt is paid); legacy
      keeps its copy.
- [ ] **13-6 — Disk pipeline cache, async compute, tiling/dirty-region** (the old Lot 10
      items) once the above lands.

> This SUPERSEDES the old "Lot 10 — Optimisations" bullet (same items, now sequenced
> behind the geometry rework). Order chosen with the user (perf for both static masses AND
> heavy real-time editing, full GPU-driven, Blender-like).

---

## 6 — Verification per lot

The user builds via F5 and verifies via F7. Per lot: syntax-check changed TUs;
F5 build (legacy must stay identical; Compositor selectable via the Dev toggle);
F7/visual check. From Lot 2: switching Legacy↔Compositor on the **same document**
must render **visually identical**; compare the metrics overlay (triangles, draw
calls, timings) for performance.

# Ink — render graph

The per-frame GPU pipeline: passes, resources, batching, the GPU data model,
and how the result reaches the screen. Industry-standard frame-graph
structure, sized for 2D.

## 1. Why a graph

Passes declare the resources they read/write; the graph derives execution
order, image layouts and `sync2` barriers, and allocates transient resources
(pooled/aliased via VMA). Benefits for us:

- adding a pass (effects, new overlay kind) is local — no hand-threading of
  barriers through the frame;
- per-view execution is data-driven: N viewports = N executions of the same
  graph with different view contexts, sharing the immutable scene data;
- headless targets (bench, thumbnails, export) run the identical graph minus
  PresentPass.

Kept deliberately simple: single graphics queue, no multi-queue scheduling,
no automatic pass reordering beyond topological order. It is a *frame
structure*, not a research project.

## 2. The frame

```
                        ┌──────────────────────────────────────────────┐
 CPU (per frame)        │ GPU (per view)                               │
                        │                                              │
 Scene::Compile ──────► │                                              │
 GeometryCache::Refresh │                                              │
 GpuScene::Sync ──────► │  (pools/tables now current)                  │
                        │                                              │
 Batcher::Build(view) ─►│  P0 (implicit) indirect args + instance list │
                        │        │                                     │
                        │        ▼                                     │
                        │  P1 ContentPass          color_ms (MSAA)     │
                        │   painter-order batches  stencil_ms          │
                        │        │                                     │
                        │  P2 ClipPass (interleaved with P1: stencil   │
                        │      write/test scopes for clip groups)      │
                        │        │                                     │
                        │  P3 CompositePass        iso targets stack   │
                        │   group isolation, blend modes, opacity      │
                        │   (backdrop copy only when blend ≠ Normal)   │
                        │        │                                     │
                        │  P4 EffectsPass (reserved — no-op v1)        │
                        │        │                                     │
                        │  P5 OverlayPass          over resolved color │
                        │   editor visuals: outlines, handles, gizmos, │
                        │   grid, page frames, snap glyphs (AA prims)  │
                        │        │                                     │
                        │  P6 ResolvePass          color_ms → color    │
                        │        │                                     │
                        │  P7 PickingPass (on demand, scissored)       │
                        │   id image → 1px readback, async fence       │
                        │        │                                     │
                        │  P8 PresentPass                              │
                        │   view color → sampled ImTextureID           │
                        └──────────────────────────────────────────────┘
 App: ImGui::Image(view texture) inside the Viewport zone
 Main swapchain pass renders ImGui; submit waits on per-view semaphores
 (fragment stage) — no CPU stall on offscreen work.
```

Pass order inside one isolation level is painter's order; P1–P3 recurse per
isolation level (a group with blend/opacity/clip opens a level: render subtree
into `iso[depth]`, composite onto `iso[depth-1]`). Levels are a pre-reserved
stack of targets (max depth documented = 8, clamped) — lesson learned from the
Compositor's reallocation crash: reserve once, never grow mid-recording.

### Steady-state short-circuit

Per view, the graph keys its output on
`(scene content signature, camera, viewport size, overlay signature)`. If
nothing changed, P1–P6 are skipped entirely and P8 re-presents the cached
target. A static canvas costs ~zero GPU and ~zero CPU record time.

## 3. GPU data model (`GpuScene`)

Persistent, pool-based; everything indexed, nothing rebound per draw.

| Resource | Content | Update policy |
|----------|---------|---------------|
| Vertex/index pools | tessellated fill + stroke meshes, free-list suballocated (device-local, staged) | only dirty items re-upload; ranges stable otherwise |
| Item table (SSBO) | per scene item: transform (view-relative f32), paint index, flags, z-key, clip/iso scope | dirty ranges only |
| Instance table (SSBO) | per drawn instance: item index + per-instance transform (expanded instancing: patterns, along-path, InstanceNodes) | regenerated per dirty definition, GPU-resident |
| Paint table (SSBO) | solid colors, pattern params, image indices, (gradient stops later) | dirty paints only |
| Texture table | bindless array: images, rasterised motif atlases if a motif is cheaper rasterised | on import/change |
| Per-view UBO | camera matrix, viewport, tier info | per frame (tiny) |
| Indirect args | one `VkDrawIndexedIndirectCommand` per batch, written by Batcher (CPU v1; GPU cull lot later moves this to a compute pass) | per view per frame, small |

**A draw is:** `vkCmdDrawIndexedIndirect` over a batch = a contiguous run of
instances sharing (pipeline, isolation scope). The vertex shader reads its
instance record → item record → paint; push constants carry only
(batch base index). One object or 10 000 pattern motifs: same path.

**Batch break conditions** (kept minimal by design): pipeline change (content
vs image vs special paint), isolation-scope boundary (blend/opacity group),
clip-scope change. Solid fills, solid strokes and pattern instances all share
the content pipeline.

## 4. Pass details

### ContentPass (P1)
- Target: `color_ms` (RGBA16F, MSAA ×4, linear premultiplied), cleared to
  transparent; page substrate drawn LAST in dst-over (validated Compositor
  rule: erase/blends never punch through the page background).
- Input: batches in painter order. No depth buffer; stencil shared with P2.

### ClipPass (P2)
- Stencil-scoped: a clip group writes its clip source region (increment),
  children test, exit decrements. Nested clips = stencil depth. Non-rectangular
  soft masks (feathered) are a later effects-lot feature via mask textures.

### CompositePass (P3)
- Normal-blend items composite directly in P1 (hardware premultiplied-over).
- A node/group with blend ≠ Normal or opacity < 1 or isolate=true renders to
  `iso[d]`, then a fullscreen-in-scissor composite applies opacity +
  blend shader; blend modes that need the backdrop copy the scissored
  backdrop region once (`iso[d].backdrop`). Shaders un-premultiply, apply the
  W3C formula, re-premultiply — the straight-output rule that fixed the
  Compositor's double-premultiply greying is baked in from day one.
- Erase is `BlendMode::Erase` (dst-out), same path, no special casing.

### OverlayPass (P5)
- Draws the editor's per-frame `OverlayList`: an immediate-mode primitive
  buffer the app fills each frame (world- or screen-space points):
  polylines with round caps, rects, circles, filled convex polys, dashed
  guides, handle glyphs (squares/diamonds/circles from a tiny SDF set).
- CPU builds a vertex/index buffer per view per frame (host-visible ring);
  colors arrive as plain RGBA resolved from design tokens app-side.
- **No text** on the canvas (locked rule from Lot 12 of the Compositor era):
  labels, rulers, HUD stay in ImGui outside/above the canvas texture.

### PickingPass (P7)
- On demand (a view was hovered & the app primed a query): re-draws base
  geometry of candidate batches with 1-based ids into `R32UI`, scissored to a
  small rect around the cursor, copies the pixel to a host buffer, fence
  polled non-blocking next frame (async N−1 pattern, validated by the
  Compositor's Lot 8). Falls back to CPU exact hit-test on the click frame.

### PresentPass (P8)
- v1: transition resolved color to `SHADER_READ_ONLY`, expose as sampled
  `ImTextureID` (one `ImGui::Image` per zone, like the legacy engine — proven,
  simple, and keeps zone backgrounds opaque).
- The swapchain-direct composite (Compositor style, under-ImGui) is kept as a
  known alternative if measured blit cost ever matters; only this pass and the
  app's zone-transparency flag would change.

## 5. Synchronisation

- `sync2` barriers derived by the graph from declared usages.
- **No semaphores on the single graphics queue**: the graph's final
  export-to-sampled barriers have submission-order scopes, so the canvas
  writes are ordered before ImGui's later fragment sampling on the same
  queue. (Per-view semaphores only become necessary if a later lot moves
  canvas work to a second queue.)
- Frame pacing: 2 frames in flight; per-frame upload ring + garbage list
  (resources retired with frame index, freed when the fence passes — which,
  by fence-signal submission ordering, also covers the previous frame's UI
  submit that last sampled them).
- Timestamps: per-pass GPU ms via query pool, read after fence (no stall),
  exposed in `Ink::Stats` for the perf HUD and `ink_bench`.

## 6. Shaders (initial set)

```
shaders/content/vector.vert/.frag     item/instance/paint fetch, solid+pattern
shaders/content/image.frag            sampled image paint
shaders/composite/iso.frag            opacity + Normal composite (straight out)
shaders/composite/iso_blend.frag      backdrop blend modes (W3C set + Erase)
shaders/overlay/prim.vert/.frag       AA primitives (fringe or MSAA)
shaders/picking/id.vert/.frag         flat id write
```

# Ink — the 2D vector engine

**Ink** is the from-scratch rewrite of the whole rendering + document stack:
document model, scene compilation, geometry, Vulkan render graph, viewport
integration, picking, overlays and the `.acu` v2 file format. It replaces
**both** previous engines — the legacy `Renderer` (CanvasRenderer) and the
`Compositor` — and the old `Renderer::Document`.

> **Ground rule (absolute):** Ink never includes, calls, or copies code from
> `src/_legacy/**` (the quarantined `Renderer`, `Compositor` and old
> application editor code). Features are *re-designed and re-implemented*,
> not ported. The legacy tree exists only as a behavioural reference
> ("how did the old stroke dash spacing feel?"), never as a code source.
>
> **Implement to industry standards, not to the old engines.** We are
> rewriting *because* Renderer and Compositor were poorly designed — so a
> feature is built the way current-standard vector/compositing systems build
> it (e.g. W3C Compositing & Blending for blend modes, the PDF/SVG paint
> model, standard camera-relative precision for deep zoom), NOT by imitating
> what the old engines did. If a doc ever cites the old engine as a *design
> authority*, that is a smell to fix; citing it as a fact ("this hazard
> exists, avoid it") is fine.

## Why a codename

"Renderer" and "Compositor" are both burned as names in this repo. `Ink` is
short, grep-able and unambiguous: C++ namespace `Ink`, static library `Ink`,
folder `src/Ink/`, docs `docs/Ink/`. Renaming is a mechanical find/replace at
this stage if a better name is ever preferred.

## Document index

| Doc | Contents |
|-----|----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layering, folder map, ownership, threading, key technical decisions |
| [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) | Nodes, Layers vs Collections, unified fill/stroke style, paints, instancing, modifiers |
| [RENDER_GRAPH.md](RENDER_GRAPH.md) | The frame: passes, resources, batching, GPU data model, picking, ImGui integration |
| [GEOMETRY.md](GEOMETRY.md) | Flattening, stroking (inside/center/outside on open paths), tessellation, caching, hit-testing |
| [PERF_TESTING.md](PERF_TESTING.md) | The automated benchmark harness (`ink_bench`), scenes, metrics, baselines |
| [NODE_GRAPH.md](NODE_GRAPH.md) | Planned (Lots 12–14): the per-layer Compositing Graph, its auto-generation from the Outliner + manual-override ("pin") model, the generic Node Graph Editor UI, and Outliner sub-component drill-down/routing |
| [NODE_UI.md](NODE_UI.md) | Planned: replacing the Node Graph Editor's ImGui-based rendering with fully custom, Vulkan-native components (zoom-correct text/controls, Blender-parity) |
| [ROADMAP.md](ROADMAP.md) | Lot-by-lot build plan; **Lot 1 = the detailed Viewport integration plan** |

## Status

- **Lot 0 (this branch, `feat/engine`):** legacy engines and the old document
  disconnected from the build and quarantined under `src/_legacy/`; the app
  shell (chrome, zone layout, design system, shortcuts, icons, modules
  catalogue) builds and runs with placeholder editors. Ink does not exist in
  code yet — these docs are its specification.
- **Lot 1:** Ink bootstrap rendered inside the Viewport zone (see ROADMAP.md).

## Non-negotiable requirements (from the product owner)

1. Strictly 2D vector pipeline; Vulkan renders **everything inside the canvas**
   — shapes, strokes, fills, patterns, images, and *all* editor visuals
   (selection outlines, object centers, Bézier/NURBS handles, gizmos,
   construction previews). ImGui draws only the surrounding interface
   (panels, top bars, rulers, menus, status bar).
2. Unified object model: **no technical shape/stroke split**. Any path can
   carry fills and strokes at once; a stroke's area is fillable with any paint.
3. Stroke alignment **inside / center / outside**, defined and rendered even
   on **open (non-cyclic) paths**.
4. Raster content: imported images are first-class scene items.
5. Two organisation systems, both first-class:
   - **Layers** — hierarchical (groups, sub-groups), owns the document
     z-order, opacity and blend modes;
   - **Collections** — organisational sets with **no** z-order role, plus
     object **parenting** relations and **modifiers** that reference
     objects/collections (Blender-style: relations, not layer position).
6. **Real instancing**: rendering N instances of an object costs ~O(1) extra
   CPU and no geometry duplication; includes pattern-fill motifs and
   instancing along a stroke.
7. Extremely fluid editing: dirty-driven incremental updates, persistent GPU
   residency, no per-frame full re-tessellation, async picking.
8. Every performance-relevant lot ships with an automated benchmark
   (`ink_bench`) so regressions are measured, not felt.
9. **Unbounded canvas**: no engine-imposed zoom or extent limits — documents
   may work at micrometre or kilometre unit scales and the camera must zoom
   fluidly through the whole range. Precision comes from double document
   space + camera-relative GPU data (GEOMETRY.md §6), never from clamping;
   and view culling must NEVER change the rendered result (GEOMETRY.md §7 —
   correctness first, optimisation behind it).

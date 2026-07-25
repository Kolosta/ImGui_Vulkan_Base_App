# Node UI — Vulkan-native node graph components

> **Status: DELIVERED (2026-07-24), same day as this doc was first written.**
> The Node Graph Editor's canvas is now 100% Vulkan — boxes, borders, ports,
> cables, text and live preview vignettes all render through Ink (`Ink::View`
> + `Overlay()`/`NodeUI()`, a new textured-quad pipeline + FreeType glyph
> atlas), and interaction (pan/zoom/select/drag/box-select/cable-connect,
> the Shift+A add-flow, the layer picker, the blend-mode selector) is a
> hand-rolled hit-test/drag state machine with zero ImGui items on the
> canvas. Full build + `ink_tests`/`ds_token_tests` green; `UI::NodeGraph`
> (the ImGui widget this superseded) is deleted, nothing consumes it anymore.
> **Not yet done**: visual/interactive verification (the user's F5 pass —
> camera math, hit-test rects and popup placement are unverified against a
> running app); rounded node corners (`OverlayList` has no rounded-rect
> primitive, so boxes are sharp-cornered — a v2 polish item, not a
> functional gap); Phase 2's interaction is a first pass, not necessarily
> feature-complete vs. every nuance the old ImGui widget had refined over
> several rounds (multi-select edge cases, exact popup styling). Sections
> below are kept as the as-built record + the remaining task list.

Roadmap + design spec for replacing the Node Graph Editor's current
ImGui-based rendering with fully custom, Vulkan-native **components**
(deliberately not called "widgets" — that word stays reserved for ImGui:
`UI::Panel`, `UI::Dropdown`, etc. A **component** is drawn entirely by this
system, never through a single ImGui call). Also the authoritative, precise
task list for everything raised in the 2026-07-24 product-owner feedback
that follow-up to the Merge/Clip/Mask/Blend round — written specifically so
nothing on it gets silently dropped again.

## 0. Why

The Node Graph Editor (`Application/Editors/NodeGraph/NodeGraphEditor.cpp`)
draws through the generic `UI::NodeGraph` widget (`src/UI/Widgets/
NodeGraph.h/.cpp`), which is built on ImGui: node boxes/ports/cables are
`ImDrawList` primitives (these DO scale correctly with the editor's own
pan/zoom, since they're issued at already-camera-transformed screen
coordinates), but every **label, port name, and in-node control** (the
picker field, the future blend-mode dropdown) is a real ImGui widget/text
draw at ImGui's own loaded font size and control metrics — entirely
independent of the node graph's zoom. The result, exactly as reported:
zooming scales the boxes and cables but not the text or the controls inside
them — nothing like Blender, where the box, its text, its sockets, its
cables and its in-node widgets all scale together as one picture.

This is not just a cosmetic gap — it contradicts an **existing, already-
locked non-negotiable requirement** (`README.md` §"Non-negotiable
requirements", item 1): *"Vulkan renders everything inside the canvas —
shapes, strokes, fills, patterns, images, and all editor visuals... ImGui
draws only the surrounding interface."* A node's ports/cables/labels/
controls are canvas CONTENT (the thing being edited), not chrome around it —
by the project's own standing rule they belong on the Vulkan side already;
the Node Graph Editor has simply never been held to it until now.

## 1. Scope

- **v1 target: the Node Graph Editor only** — its node boxes, ports, cables,
  in-node controls (layer picker, blend-mode selector, mute/collapse
  affordances), and the live preview vignette. Nothing else changes.
- Every OTHER ImGui surface in the app (Properties, Outliner, Settings,
  top bars, the breadcrumb header above the canvas, Shift+A's eventual
  popup chrome) **stays ImGui** — untouched, no regression risk, no timeline
  pressure from this effort.
- A broader migration of other widgets to the same component system is
  explicitly a **future, separate initiative** — no lot number reserved, not
  scheduled, revisit only after this system is built and proven here.

## 2. Existing foundations this reuses (not starting from zero)

This is a genuinely large piece of engineering, but the codebase already has
every LOW-level primitive it needs — this is an integration/extension effort
more than a from-scratch rendering system:

- **`Ink::OverlayList`** (`Ink/View/OverlayList.h/.cpp`, `Render/Passes/
  OverlayPass.cpp`) — the EXACT precedent: a per-frame, CPU-built,
  screen-space, Vulkan-batched triangle list for editor visuals (today: the
  Viewport's selection outlines, handles, guides). `AddRectFilled`/`AddRect`/
  `AddLine`/`AddCircle*`/`AddMarker` already cover node boxes, port dots,
  header bands and straight cable segments. **Gaps to close**: no bezier
  primitive (cables need one) and no textured/UV variant (text and the
  preview vignette need one) — both additive, not a rewrite of what exists.
- **Bezier flattening** — Ink's geometry kernel already adaptively flattens
  cubic beziers for real path stroking (`GEOMETRY.md`); a cable's curve
  reuses the SAME flattener to produce a polyline, then either a sequence of
  `OverlayList::AddLine` segments (v1, simplest) or a thin stroked mesh via
  the existing stroker (v2, if segment joins look rough at shallow angles).
- **FreeType is already a build dependency** (`CMakeLists.txt`: "FreeType —
  replaces ImGui's default stb_truetype rasterizer", `src/UI/src/Text/
  FontManager.cpp`) — no new font-rendering dependency needed. A Node UI
  font atlas rasterizes the needed glyph set ONCE via FreeType into a
  bitmap, uploaded as a Vulkan texture exactly the way `VectorGraphics::
  IconManager` already uploads rasterized SVG icons (staging buffer →
  sampled texture, LRU-cached) — same upload pattern, different rasterizer
  input.
- **`VectorGraphics::IconManager`** — the existing icon-quad rendering (node
  header icons, port-type glyphs) already works at ANY size the caller
  requests and is texture-based; it can very likely keep serving icons
  as-is (worth confirming during Phase 1 that its `RenderIcon` overload
  taking a raw `ImDrawList*` can be pointed at a Node UI draw list instead,
  or that its underlying texture handles can be sampled directly by a Node
  UI quad — implementation-time decision, not a blocker).
- **`Application::NodePreviewTexture`** — the live Vulkan vignette mechanism
  is already texture-based (an `ImTextureID`/raw Vulkan handle), so moving it
  from "drawn via `ImGui::AddImage`" to "drawn via a Node UI textured quad"
  is a drawing-call change only, not a re-implementation.

## 3. Architecture — DECIDED (2026-07-24), grounded in existing code

**Question settled: reuse the existing Viewport/content Vulkan pipeline, or
a separate one?** Neither extreme — the right split follows what already
exists at each layer:

- **Reuse `Ink::View` and the `BeginFrame`/`EndFrame` frame protocol
  WHOLESALE** (`Renderer.h`, `Renderer.cpp`'s per-view dirty-tracking/
  record/submit loop). `View` is already a general "one rendered rectangle,
  acquired by an opaque key" abstraction — Viewport zones, Outliner preview
  vignettes and `NodePreviewTexture` all already go through
  `Renderer::AcquireView`/`SetViewport`/`SetCamera`/`Texture()`, not
  something Viewport-specific. The Node Graph Editor gets its OWN `View`
  (keyed like any other zone) instead of inventing a second "get a Vulkan
  image on screen" mechanism. Its content pass renders NOTHING from the
  Document (a Node Graph is not a view onto the document's geometry) — the
  whole picture comes from what's described below.
- **Reuse `Ink::OverlayList` + the existing `overlayPipeline` AS-IS for
  every UNTEXTURED shape**: node boxes, header bands, borders, port dots,
  and cables (flattened to a polyline, drawn as `AddLine` segments — no new
  bezier primitive needed for v1; a thin stroked mesh is a v2 polish item
  only if joins look rough). This is not a new pipeline at all: the Node
  Graph's `View` has its OWN private `OverlayList` instance (one per View,
  confirmed in `RendererInternal.h`) — filling it with node-graph shapes
  instead of viewport gizmos is exactly what the mechanism is already for,
  zero interference with any Viewport zone's own overlay.
- **One genuinely NEW pipeline for TEXTURED quads** — nothing existing
  supports this shape (`OverlayList`'s vertex has no UV; the content
  pipeline's paint tables are a completely different, Document-coupled
  mechanism; `IconManager`'s textures are uploaded into ImGui's OWN
  descriptor pool via `ImGui_ImplVulkan_AddTexture`, which is NOT bindable
  by a foreign pipeline with a different descriptor set layout — confirmed
  by reading its upload code). Two uses share this one pipeline:
  - **Text**: one FreeType-rasterized glyph atlas (built once, uploaded via
    the exact staging-buffer technique `IconManager::
    CreateVulkanTextureFromRGBA` already uses, just with our OWN descriptor
    set/layout instead of ImGui's), sampled by glyph quads laid out in
    CANVAS space — the actual fix for "text doesn't zoom with the node":
    the SAME camera matrix that transforms box/cable vertices transforms
    glyph quads too, so nothing can ever scale independently again.
  - **Preview vignettes**: each node's live preview is already a rendered
    `Ink::View`/`NodePreviewTexture` result — drawn as one textured quad per
    visible preview, sampling that View's OWN image view directly through
    our descriptor set/layout (still cheap: at most a few dozen visible
    nodes, one draw call each, same order of magnitude as today's
    `ImGui::AddImage` calls).
- **Deliberately NOT reused: the content pipeline** (`contentPipeline` and
  its clip/erase/dedup siblings). That pipeline's entire design — GPU-
  resident tessellated mesh cache, instance tables, paint tables, indirect
  multi-draw — exists to scale to a large, persisted DOCUMENT's geometry.
  Node UI's boxes/text are arbitrary, interaction-driven, per-frame CPU
  content with no relationship to the open document; forcing it through
  that pipeline would mean either faking Document nodes for UI chrome (a
  real layering violation — UI state does not belong in the document
  model) or bypassing its residency/caching machinery entirely while still
  paying for its specific stencil/clip-oriented pipeline variants that Node
  UI has no use for. Reusing Overlay's PATTERN (cheap, CPU-driven, no
  document coupling) is the correct-shaped precedent; reusing Content's
  PATTERN would be forcing a square peg into a round hole for the sake of
  reusing pipeline objects that do not fit the workload.

```
NodeGraphEditor.cpp (App)
  acquires its own Ink::View (AcquireView(key), same call Viewport/preview
  vignettes already use) → SetViewport(zone size) → SetCamera unused (Node
  UI keeps its OWN pan/zoom, unrelated to a document camera) → every frame:
    view->Overlay().AddRectFilled/AddLine/AddCircleFilled(...)   // boxes, cables, ports — EXISTING pipeline
    NodeUI::GlyphList (new) .AddText(...) / .AddPreviewQuad(...) // NEW pipeline, this doc's actual new work
  blit: ImGui::GetWindowDrawList()->AddImage(view->Texture(), zoneMin, zoneMax)
  input: IsWindowHovered(...) + raw ImGui::IsMouseDown/IsKeyPressed, EXACTLY
         Viewport's HandleViewportInput pattern — no ImGui widgets, no
         InvisibleButton; the zone is host-rect + input-forwarding only.
```

**Interaction model (Phase 2, the hard part — no existing precedent in this
codebase to lean on, budget real design time before writing code):** ImGui's
IsItemActive/Hovered/Clicked machinery does not exist here. A minimal,
purpose-built immediate-mode interaction layer is needed: per-frame, the
Node Graph Editor still emits a list of interactive components (a text
label is passive; a dropdown/button/text-field is not), each carrying its
CANVAS-space rect; the Canvas hit-tests the current mouse position
(converted screen→canvas through the SAME camera the drawing used) against
that frame's interactive rects, in reverse z-order, and reports
hover/press/release/drag exactly like `UI::NodeGraphResult` already does for
node moves/cable drags today — same shape of contract, new implementation
underneath. State (which dropdown is open, a text field's edit buffer)
lives on `Application` next to `ngSelected_`/`ngCollapsed_` today, the same
"caller owns persistent state, canvas is stateless between frames" rule
`UI::NodeGraph`'s own header comment already documents.

## 4. Phasing

**No ImGui in the Node Graph editor's canvas content is the hard requirement
(2026-07-24) — Phase 1/2/3 are build ORDER within one continuous effort, not
separate sessions with an ImGui seam accepted in between.**

1. **Phase 1 — static, zoom-correct primitives.** `Ink::View` for the Node
   Graph zone + the font atlas + the new textured-quad pipeline. Node boxes,
   headers, port dots, cables and every text label render through
   Overlay/the new pipeline, correctly scaling as ONE picture at any zoom
   (Blender-parity for the *visual*). Verifiable on its own (zoom the graph,
   everything scales together) before interaction is wired.
2. **Phase 2 — interaction.** A minimal, purpose-built hit-testing layer
   (no ImGui item state exists on this canvas at all): per-frame CANVAS-
   space rects for anything clickable, hit-tested against the raw mouse
   position converted through the SAME camera transform used for drawing
   — node dragging, box-select, port drag-to-connect, and the in-node
   controls (layer picker, blend-mode selector) all route through this one
   mechanism instead of ImGui widgets.
3. **Phase 3 — cutover.** `NodeGraphEditor.cpp` stops calling `UI::
   DrawNodeGraph`/ImGui for anything inside the canvas rect. The breadcrumb
   header above the canvas and the Shift+A popup chrome are OUTSIDE that
   rect (surrounding interface, not canvas content) and may reasonably stay
   ImGui per ARCHITECTURE.md's own rule — revisit only if that reads
   inconsistently once built. `UI::NodeGraph` (the ImGui widget) is deleted
   once nothing consumes it.

## 5. Precise task list (2026-07-24 feedback, so nothing is dropped again)

Status legend: **[build now]** — fixed/built this same round, on the CURRENT
ImGui-based widget, forward-compatible with the Phase 1/2/3 rewrite above
(same data/interaction shape, just re-hosted later). **[Phase 1/2/3]** —
depends on the rewrite above. **[§7]** — depends on `NODE_GRAPH.md` §7's
real per-edge model, tracked there, cross-referenced here only.

1. **Full custom Vulkan node components** (this doc's whole subject) —
   **[Phase 1/2/3]**, see §§2–4.
2. **Shift+A becomes a real "Add" menu with a node-type list, and placement
   follows the mouse until a click drops it** (Blender's exact flow: open
   the menu, pick a type, a ghost node tracks the cursor, left-click
   commits it at that position; the node's own layer-picker then starts
   EMPTY and is filled in afterward — placement and targeting are two
   separate steps, not one). — **[build now]**, on the current widget: a
   transient "pending placement" node (not yet a `compInputs` entry — it
   has no real target yet, so it cannot be committed to the Document until
   one is chosen) follows the mouse each frame and commits on click.
3. **Live preview vignette moves ABOVE the node header** (not inside the
   body), gets a shortcut to toggle it on/off, and must scale WITH the node
   — sized to the node's current on-screen width at every zoom level,
   instead of the fixed-pixel cap it has today (the actual bug: the
   thumbnail's display size was clamped to a constant `64×gs`, independent
   of the node graph's own `ngZoom_`, so it visibly stopped growing once the
   node itself kept growing under zoom). — **[build now]**: needs a new
   `NodeGraphNode::topHeight`/`topDraw` extension point (mirrors the
   existing `bodyHeight`/`bodyDraw`, just anchored above the header instead
   of below the ports) in `UI::NodeGraph`, a display-size fix (always
   render at the node's full on-screen width, only cap the underlying
   TEXTURE fetch resolution, not the displayed size), and a new shortcut
   action (`nodegraph.togglePreview` or similar, `ShortcutDefaults.cpp`).
4. **Blend nodes need an in-node control to choose the blend mode** — today
   the Blend node shows no way to pick Multiply/Screen/etc. from the graph
   itself. — **[build now]**: a `bodyDraw` dropdown (today's ImGui
   `pr::DropdownRow`/similar, same list already used in
   `PropCompositingSection`) wired to `Action_SetBlendMode`; becomes a real
   Node UI dropdown component in Phase 2.
5. **Object layers show no input at all today — "Output connected to
   nothing"; a real bug, not a missing feature.** A Path/Instance layer
   (anything with its OWN paint stack, not a Group) needs a real **Object**
   input node — representing "this node's own resolved geometry" — wired to
   whatever comes next (directly to Output for a plain shape; through
   Merge alongside its children for an Affinity clip/mask host, painted
   FIRST/bottom to match painter order; through Clip/Mask/Blend if those
   apply). — **[build now]**, engine-side: `Ink::CompNode` gains
   `isObjectInput` (mirrors `isMaskSourceInput`); `BuildAutoGraph`
   (`CompGraph.cpp`) always emits one for `layer.kind != NodeKind::Group`
   (self-targeting: `target.node == layer.id`), counted into `mergeCount`;
   the Merge-creation condition becomes `layer.kind == NodeKind::Group ||
   mergeCount > 1` (was `mergeCount > 0`, which — now that a non-Group layer
   ALWAYS has at least the Object input — would wrongly force a pointless
   1-input Merge on every plain shape). Editor-side: the Object input gets a
   UI node titled "Object" (not "Layer Input"), no retargeting picker (it is
   fixed to "this node"), just its preview; `sinkKey`'s initial fallback
   changes from the sentinel `kOutputKey` to the first built ordinary input
   key (the Object, when Merge doesn't exist) so Blend/Clip-Mask/Output
   correctly wire FROM a real node instead of from Output itself.
6. **Directly caused by #5's absence: a Blend node's edges were backwards**
   — the graph showed `Output → Blend(content)` AND `Blend(result) →
   Output`, i.e. an edge leaving Output, which cannot mean anything (an
   Output has no outgoing port in this model) and is a straight consequence
   of `sinkKey` defaulting to `kOutputKey` when nothing real fed it — same
   root cause and same fix as #5, not a separate change. Blend must always
   sit strictly BETWEEN a real source (the Object input, or Merge, or
   Clip/Mask if present) and Output — never adjacent to Output on both
   sides.
7. **Cross-layer sub-component drag & drop (the "pseudo-duplication" of an
   Object layer elsewhere in the graph, to give one of its pieces a
   different z-index) is still not built** — **[§7]**. Selecting a fill/
   stroke row in the Outliner now works (fixed last round); dragging one to
   a foreign layer, and having it show a duplicated Object-layer row there,
   needs the real per-edge `CompEdge` model `NODE_GRAPH.md` §7 designs and
   `§7.4` specifically describes for the Outliner side. Restated here only
   so it stays on ONE list — no new design, `NODE_GRAPH.md` §7 remains the
   authority on how it will work.

## 6. Open risks / questions to settle during Phase 1

- **Font atlas rebuild strategy at extreme zoom.** Blender itself does not
  infinitely re-rasterize text either — a reasonably high base atlas
  resolution + GPU bilinear filtering is the industry-normal tradeoff.
  Confirm a base size (e.g. rasterize at a fixed "reference" em size well
  above the Node Graph's typical on-screen text size) and accept some
  softening at extreme zoom-in rather than a dynamic re-atlas system, unless
  it looks bad enough in practice to justify the extra complexity.
- **Where the new library lives** (`Ink::` proper vs. a new sibling static
  lib, e.g. `src/NodeUI/`) — `Ink::` is arguably correct (it is Vulkan/RHI
  code, same rule as `OverlayList`), but it is APPLICATION-specific UI
  concern (node graph editing is not a generic vector-engine feature) —
  decide before writing the CMake target so it is not moved later.
- **IconManager reuse vs. a parallel icon path** — confirm during Phase 1
  whether `IconManager::RenderIcon(ImDrawList*, ...)` can be pointed at a
  compatible list, or whether Node UI needs its own tiny icon-quad path
  reading the same cached textures.

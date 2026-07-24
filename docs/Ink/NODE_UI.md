# Node UI — Vulkan-native node graph components

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

## 3. Architecture (Phase 1 target)

```
NodeUI::Canvas (new, lives beside OverlayList — Ink:: or a sibling library,
                decide at implementation time)
├── Camera (panX, panY, zoom — SAME convention as today's ngPanX_/ngPanY_/
│           ngZoom_, just owned by the new system instead of the ImGui widget)
├── ShapeList   — extends/wraps OverlayList's existing primitives (boxes,
│                 port dots, header bands) + a NEW AddBezier(p0,c0,c1,p1,
│                 color, thickness) for cables
├── GlyphList   — NEW: per-frame textured quad list. One font atlas texture
│                 (FreeType-rasterized, built once, cached), UV per glyph
│                 from a shelf-packed atlas layout. AddText(pos, sizePx,
│                 str, color) shapes+lays out glyphs in CANVAS space, so
│                 the SAME camera matrix that scales boxes/cables scales
│                 text too — this is the actual fix for the reported bug.
└── Vulkan draw — a new pass (or a new pipeline pair inside the existing
                  OverlayPass) consuming ShapeList (untextured, `PremultipliedOver`
                  blend, matches OverlayList's existing pipeline) and GlyphList
                  (textured, atlas-sampled, alpha-blended) each frame the
                  Node Graph editor is open.
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

1. **Phase 1 — static, zoom-correct primitives.** `NodeUI::Canvas` +
   `ShapeList`/`GlyphList` + font atlas + the Vulkan pass. Node boxes,
   headers, port dots, cables and every text label render through it,
   correctly scaling as ONE picture at any zoom (Blender-parity for the
   *visual*). Interactive controls (picker, blend-mode selector, mute/
   collapse) temporarily STAY ImGui `bodyDraw` callbacks layered on top,
   same as today — a visual seam, not a functional regression, while
   Phase 2 is designed. Deliverable is benchable/verifiable on its own
   (zoom the graph, everything scales together) before touching interaction.
2. **Phase 2 — interactive components.** The hit-testing/interaction layer
   above, plus the actual component set the editor needs today: a
   Blender-style dropdown-equivalent (layer picker, blend-mode selector),
   drag-to-connect on ports, node-box dragging, box-select, mute/collapse/
   delete affordances — feature parity with what `UI::NodeGraph` already
   does, on the new rendering.
3. **Phase 3 — full migration.** `NodeGraphEditor.cpp` moves entirely off
   `UI::NodeGraph`/ImGui for its canvas content (the breadcrumb header above
   the canvas and the Shift+A popup chrome may reasonably stay ImGui — they
   are closer to "surrounding interface" than canvas content, revisit if
   that reads inconsistently once built). `UI::NodeGraph` (the ImGui widget)
   is deleted once nothing consumes it.

**Not scheduled a lot number yet** — this is new, sizeable scope (realistically
comparable to standing up a small text-rendering + immediate-mode UI
subsystem) and deserves its own implementation pass with room to get Phase 1
right before Phase 2 is attempted, rather than being squeezed into the
current Node Graph Editor session. Tracked here so it is never lost; picked
up as its own dedicated effort.

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

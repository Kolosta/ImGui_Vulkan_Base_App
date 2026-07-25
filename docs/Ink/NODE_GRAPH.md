# Ink — compositing graph & node editor

Specification for the **Compositing Graph**: the document-level dataflow model
that decides what each Layer renders, plus the generic node-graph UI that lets
a user edit it directly.

> **Status: Lots 12–13 delivered and revised (2026-07-24), Lot 14 partially
> started.** This doc is the ORIGINAL spec, written before implementation;
> ROADMAP.md's Lot 12/13 entries carry the as-built facts and supersede this
> doc wherever they disagree. The biggest divergence: manual editing turned
> out simpler than sketched below — a layer is either fully automatic or
> fully hand-authored as ONE UNIT (`Node::compInputs`, a plain reorder/filter
> of that layer's own children), not a per-`CompPort` `pinned` flag on an
> arbitrary edge. **§3's single "Mask & Blend" node was later split** (same
> day, after product-owner review) into four separate, single-purpose kinds —
> `Merge` (always present, N ordered inputs, no compositing math of its own),
> `Clip`, `Mask` and `Blend` — see the corrected node list below and
> `Ink/Scene/CompGraph.h`'s header comment, which is the ground truth. Read
> ROADMAP.md's Lot 12/13 entries first if you're implementing Lot 14 —
> they're shorter and current; come back here for the parts that didn't
> change (the Z-order reasoning in §1, the naming rule right below). **§7 is
> new**: the real per-edge, cross-layer graph model that bidirectional link-
> dragging, cross-layer piece routing and Outliner object-encapsulation (§4)
> all actually need — none of which the shipped whole-graph `compInputs`
> model can do yet. Read §7 before starting Lot 14's drag & drop.

> **Naming — do not confuse with the frame render graph.** `docs/Ink/
> RENDER_GRAPH.md` already owns the name "graph" for the **per-frame GPU
> graph** (`Ink::graph::RenderGraph`, passes/resources/barriers, ARCHITECTURE.md
> §2 "Graph" layer). This document specifies a **different, document-level**
> graph: a persisted, windowless, GPU-less dataflow model that decides *what
> a Layer's content is*, evaluated once per `Scene::Compile`. To keep the two
> apart in code and conversation, this one is always the **Compositing
> Graph** (`CompGraph`/`CompNode`/`CompPort`/`CompEdge`, all under
> `Ink::Document`) — never "the graph" unqualified, never `Ink::graph`.

## 0. Core philosophy

The rendering engine is **node-first**: every Layer's rendered content is,
underneath, the output of a small Compositing Graph belonging to that Layer.
There is no bypass — a plain layer with two children and Normal blending is
*not* special-cased; it is the graph's most common auto-generated shape (an
`Input` per child feeding a `Merge` feeding `Output`). This matters because it
is the guarantee behind the product owner's constraint *"aucune fonctionnalité
n'utilise de bypass hors du graph"*: nothing a future feature does "outside
the graph" can silently diverge from what the graph would produce.

Two editing surfaces read and write the *same* Compositing Graph — neither
owns a separate copy of the truth:

| Surface | Who it's for | What it shows |
|---|---|---|
| **Outliner, Layers mode** (Lot 9, extended Lot 14) | Everyone, by default | The classic list: layer order, nesting, blend/opacity/clip fields. Never shows a box or a wire. |
| **Layer Graph Editor** (new, Lot 13) | Users who need custom routing | The actual `Input`/`Output`/`Merge`/`Mask & Blend` nodes and cables for one Layer's local graph, Blender-Shader-Editor / DaVinci-Fusion style. |

This is the **DaVinci Resolve precedent** the product owner named for Mask &
Blend: the Edit page's track order (V1 over V2 over V3…) *auto-generates* a
default Fusion composite per clip; opening the Fusion page and rewiring one
clip overrides *only that clip*, without turning the timeline into a second
system to keep in sync. Ported here:

- **The Outliner is a generator, not a second source of truth.** As long as
  nobody has opened the Layer Graph Editor for a given layer (or dragged a
  sub-component in the Outliner, Lot 14 — same mechanism, §4), that layer's
  Compositing Graph is fully regenerated every compile from: the Outliner's
  layer order (top-of-stack-first, unchanged from Lot 9) + each node's
  opacity/blend/isolate/clip fields (unchanged Properties fields, Lot 9). A
  user who never opens a node editor sees **exactly today's Outliner**,
  nothing added, nothing to learn.
- **A manual edge is pinned.** The moment a `CompEdge` is created or rewired
  by hand (Layer Graph Editor, or an Outliner sub-component drag — §4), that
  edge stops being silently regenerated. The owning layer is marked
  "customized" (a small graph-glyph badge in the Outliner row, §5); a context
  menu action **"Reset to automatic"** drops all pinned edges for that layer
  and reverts to pure auto-generation.
- **Z-index is not touched by any of this.** A Layer's position in the
  document-wide stacking order still comes from the Layers tree list order,
  exactly as today (ARCHITECTURE.md §6) — see §2 for why this is safe rather
  than a loophole.

## 1. Why this does not reopen the Z-order decision

ARCHITECTURE.md §6 locks *"Z-order: painter's order from the Layers tree"*.
The Compositing Graph does not repeal this — it narrows what "the Layers
tree" governs.

Read literally, "the Z-index must be exclusively dictated by the graph"
could mean either:

- **(A) Layers keep their stacking position** (the Outliner list order, as
  today); only a layer's *content* is now computed by a graph whose `Input`
  nodes can target any node/sub-component in the document, regardless of
  tree membership. The final merge still executes in list order.
- **(B) There is no more list order at all** — one document-wide dataflow
  graph, and the final composition order is whatever the graph's topology
  says, needing explicit ordering nodes and real cycle detection (an `Input`
  in Layer B depending on Layer A's `Output`, and vice-versa, must be
  refused) since there is no fallback list to break ties.

**This spec adopts (A).** The deciding evidence is the routing example
itself (product-owner spec, point 3): Layer 2's `Input` targets **Object X**
directly, never Layer 1's `Output`. The two layers' graphs never reference
each other — each pulls straight from the Document. That is exactly why (A)
is sufficient and (B)'s cycle-detection machinery is not needed: **Input
nodes source only from Document nodes/sub-components, never from another
Layer's `Output`.** This is a hard rule (§3), not an incidental property —
it's what keeps the Layers list authoritative for stacking order while the
graph is authoritative for content.

So, precisely: **the Layers tree still owns which layer stacks over which**
(list order, unchanged mechanism). **The Compositing Graph owns what
renders inside a given layer's slot** (previously: "this layer's tree
children", now: "whatever this layer's `Input` nodes reference — by default
its tree children, generated automatically; by override, anything").
Blend modes, masks and clips move fully into the graph (Mask & Blend nodes,
§3) — technically, a `Group` node's compositing fields stop being consulted
directly by the Scene and become *inputs to the auto-generator that builds
the graph*, per the product owner's own correction: *"techniquement les
Layers ne doivent gérer que blend mode/mask, pas le Z-index."*

## 2. Data model

**Implemented (Lot 12), refined from the original design during
implementation — this section reflects the shipped code, not the initial
proposal.** Types: `Ink/Scene/CompGraph.h`, `src/Scene/CompGraph.cpp`.

Lives in `Ink::Scene`, not `Ink::Document` — the same placement as
`CompositeScope`/`ScopeId` (Scene.h), and for the same reason: as long as
nothing is pinned (no manual edit exists yet — that arrives with Lot 13), a
`CompGraph` is 100 % derivable from the Document every compile, so keeping it
in Document would mean persisting/versioning data that is pure computation.
`CompNode::id` is a **small index local to one `CompGraph`** (not drawn from
`Document`'s global id pool) — nothing outside this one transient,
per-compile graph references it yet. When Lot 13 introduces pinning, the
pinned edges (not the whole graph) get a small persisted table in `Document`;
the auto-generated portion stays fully derivable and is never written — that
part of the original plan is unchanged.

```
CompGraph
├── layer: NodeId
├── nodes: [CompNode]
└── output: index into nodes  // always the one CompNodeKind::Output
```

```
CompNode { id (local index), kind: CompNodeKind, target: CompInputTarget,
           opacity, blend, isolate, clipNode, hasClipMask,   // Merge/MaskBlend only
           in: [CompPort], out: [CompPort] }

CompNodeKind = Input      // target: CompInputTarget { node | fill | stroke }
             | Output     // exactly one per CompGraph, the layer's sink
             | Merge      // auto-inserted only, never user-placed — see §3
             | MaskBlend  // Mask/LayerA/LayerB in, Result out — DaVinci-Fusion-style

CompPort { type: CompPortType, pinned: bool }
CompPortType = RenderOutput   // any object/fill/stroke/layer's resolved result
             | LayerOutput    // strictly a Layer's Output (MaskBlend's A/B ports)
```

- **Port wiring (which node's output feeds which node's input) is
  deliberately NOT represented in Lot 12.** It has no behavioral consequence
  yet — nothing evaluates edges: `Scene::EmitNode` still walks `Node::children`
  directly for paint order, unchanged by this lot — and the right arity/
  connection model depends on the generic Node Graph UI's actual interaction
  needs (Lot 13), which aren't designed yet. `BuildAutoGraph` produces a real,
  inspectable set of nodes (one `Input` per child, a `Merge` iff the layer
  composites, one `Output`) without committing to an edge representation that
  Lot 13 would likely have to redesign anyway once it knows what drawing
  cables actually requires.
- **The Merge decision is split out as `ComputeAutoMergeParams`** (same
  header), returning just `{composites, opacity, blend, isolate, clipNode,
  hasClipMask}` with no node/port allocation. `Scene::OpenScopeIfNeeded` (the
  hot path — called once per node, every dirty compile) calls this directly
  instead of building a full `CompGraph`, avoiding per-child `Input`-node
  allocations on every compile; `BuildAutoGraph` calls the *same* function for
  its `Merge` node, so the two can never drift out of sync. This is what
  makes Lot 12 low-risk: the observable predicate lives in exactly one place.
- **`pinned`**: reserved for Lot 13+ (a port a manual edit has set). The
  auto-generator never touches a pinned port — moot today since nothing can
  set one yet; every `CompPort` in Lot 12 has `pinned == false`.
- **Deleting a referenced node/fill/stroke**: same rule as every other
  reference in the Document (DOCUMENT_MODEL §2) — an `Input` targeting it
  would resolve to "missing" — not yet exercised since no `Input` is
  persisted/consulted for rendering in Lot 12.
- `FillId`/`StrokeId` (DOCUMENT_MODEL §4, `Document/Types.h`) are drawn from
  the **same global id pool as `NodeId`** (`Document::NextId()`), not a
  separate per-node counter — simpler than the original proposal and
  consistent with how every other Document id already works, at the cost of
  being document-unique rather than merely node-unique (a strictly stronger
  guarantee, not a weaker one). `Document::StampStyleIds` assigns them: called
  from `AddPath`/`SetStyle`/`Restore` (fills in only null ids) and from
  `DuplicateSubtree` with `force = true` (a duplicate must never share its
  source's piece id).

## 3. Node types (v1)

- **Input** — imports the data of **any** layer, object, fill or stroke in
  the project (product-owner spec, point 2). One output port,
  `PortType::RenderOutput`.
- **Output** — the layer's sink. Exactly one per `CompGraph`. At creation
  (default setup, product-owner spec point 2), a fresh layer's graph is
  exactly one `Input` (targeting "all child layers, merged" — see Merge
  below) wired straight to `Output`.
- **Merge** — **auto-inserted only**; never appears in the "Shift+A"-style
  add menu of the Layer Graph Editor. Combines N ordered inputs into one
  output, painter's order, **no compositing math of its own** — that is now
  Clip/Mask/Blend's job (see below), a later correction from the original
  single-node design this paragraph used to describe (moved to §3's status
  callout). Present for **every** Group with ordinary children, and for an
  Affinity path-parent with more than just its mask/clip source among its
  children — `RENDER_GRAPH.md`'s existing painter-order content walk,
  re-expressed as a node. One `In` port **per current ordinary child** (not
  one shared multi-input port), each landing its own cable in order —
  reordering a child means dragging its box up/down OR re-dropping its cable
  on a different Merge row (§7.2).
- **Clip** / **Mask** — mutually exclusive per layer (never both: a
  Group-clip and an Affinity mask-child cannot coincide on the same node,
  `ComputeAutoMergeParams::isMaskChild`). Two `In` ports (`Content`, fed by
  Merge's `Result`; `Mask`, fed by a dedicated mask-source `Input`) and one
  `Out`. Same shape, different real operation, hence different kinds instead
  of one doing double duty: `Clip` = a group-clip's first path child or a
  plain Affinity clip host's own fill; `Mask` = a dedicated Affinity `isMask`
  child (DOCUMENT_MODEL §2.1).
- **Blend** — opacity / W3C blend mode / isolation (`RENDER_GRAPH.md`
  §CompositePass), independent of Clip/Mask: a layer can be clipped AND
  blended, clipped only, blended only, or neither (in which case the node
  doesn't exist at all). One `In`, one `Out`. Node Graph "Mute" (M) targets
  **this node specifically** (`Node::compBlendMuted`) — bypasses opacity/
  blend/isolation only, while Clip/Mask (if any) keep working, since muting a
  blend a user is experimenting with should not also un-clip the layer.
  **Implemented (Lot 12, revised same day) as `ComputeAutoMergeParams`**
  (`CompGraph.h`), split into two independent halves so a mute override can
  recombine them without re-deriving the predicate: `blendTrigger` (opacity
  < 1, non-Normal blend, or isolate — Blend's reasons) and `otherTrigger`
  (clip, an Affinity path-parent, an enabled Subtract `AlongPath` modifier,
  or an enabled Erase-blend piece — Clip/Mask's reasons, and the underlying
  isolation Scene still needs for the last two even with no Clip/Mask node).
  Behavior-parity requirement (met): for a layer that has never been touched
  in the Layer Graph Editor, the generated shape composites **identically**
  to the current Lot 4 isolation-stack walk — regression gate
  `TestCompositeScopes` + `TestCompGraphAutoGenerate` pass.
- ~~**Mask & Blend**~~ — the original single DaVinci-Fusion-style node this
  section specified (three input ports, one output, product-owner spec point
  2) was replaced by the four kinds above after the product owner flagged
  that it conflated four distinct real operations ("Réfléchi à nouveau à
  l'utilité de chaque node... la logique par rapport au graphe réel du
  moteur"). The strictly-Layer-typed `Layer A`/`Layer B` cross-layer blend
  ports it would have offered are exactly what §7.3's future `LayerRef` input
  kind restores, once real per-edge storage exists to route one layer's
  `Output` into another's graph.

### 3.1 Fill/Stroke become addressable

Today (DOCUMENT_MODEL.md §4), `Style::fills`/`strokes` are plain ordered
array elements with no id — fine for painting, not enough to be an `Input`
target. This spec adds a stable per-piece id:

```
Fill   { id: FillId,   paint, fillRule, enabled }
Stroke { id: StrokeId, paint, width, align, cap, join, miterLimit, dash,
         enabled, widthSpace, marks, ... }        // fields unchanged, + id
```

`FillId`/`StrokeId` are `uint64`, unique within their owning node, stable
across reorders (assigned once, at creation — reordering the fills/strokes
list, Lot 9's vignette drag-reorder, never reassigns them). An `Input`
targeting a `FillId`/`StrokeId` reads that piece's **already-resolved,
post-compute visual result** — the exact same constraint the Outliner
sub-component view relies on (§4): a routed fill/stroke is treated as an
opaque, pre-composited item at the point it enters another layer's Merge
chain. Its own internal blending/marks (e.g. a stroke's mark objects
resolving in their own isolation scope, RENDER_GRAPH.md §Erase) are **not**
recomputed or decomposed — they already happened before the piece became
available as a Compositing Graph source. This is a direct consequence of
`Scene::Compile`'s existing per-item dirty/compile order (ARCHITECTURE.md
§4): a piece's own paint resolves before it can be read as another node's
input.

## 4. Outliner integration (Lot 14)

Two independent behaviors, both already implied by the product-owner spec
but worth separating cleanly since they have different costs:

- **Recursive display** (point 1, all modes: Layers **and** Collections). A
  chevron on any object discloses its fills/strokes (icon per piece); a
  fill/stroke that itself carries further resolved sub-content (a pattern's
  motif, an `InstanceNode`'s target, an `Array`/`AlongPath` modifier's
  expansion — DOCUMENT_MODEL §5–6) is disclosed one level further,
  recursively, reusing the exact recursive-row mechanism the Layers tree
  already has (Lot 9's `Outliner.cpp`). **Pure display** — no new Document
  state, no dependency on the Compositing Graph existing.
- **Drag & drop to another layer** (point 1, **Layers mode only** per the
  product-owner spec). Confirmed by the product owner to be **the same
  mechanism as the Layer Graph Editor, not a lighter bypass**: dragging a
  fill/stroke row onto another layer writes a real, pinned `CompEdge` — an
  `Input(FillId/StrokeId X)` appears (or is reused) in the target layer's
  `CompGraph`, wired to that layer's `Merge` chain; the source layer's
  auto-generated `Input` for that same piece is dropped. This is why Lot 14
  is sequenced **after** Lot 12 (the graph must exist to pin an edge into)
  **and** Lot 13 (so the same edge, once created by a drag, is visible and
  further editable in the Layer Graph Editor — one truth, two surfaces, per
  §0).
  - **Visual duplication rule** (product-owner spec, point 1): when a piece
    is pinned to a layer other than its owning object's own row, the
    Outliner shows the parent object's row again, nested under the *target*
    layer, to hold that one piece — **display only**, no second Document
    node. Both rows resolve to the same object id.
  - **Selection/movement stay unified** (product-owner spec, point 1):
    selecting the object selects every row that represents it; the object is
    one entity with one transform, and moves as a block in the Viewport
    regardless of how many Outliner rows show its pieces. Only *rendering*
    (which layer's Merge chain each piece feeds) is split by the routing.

## 5. Generic Node Graph UI (Core Node UI)

Product-owner spec point 2 asks for this to be built as a **reusable
system**, not a one-off Layer Graph panel — the same way `UI::Panel`
(list-drag/reorder) and `UI::Dropdown` are shared building blocks other
editors already reuse (memory: modifier-stack panels, Properties tabs).

Home: **`src/UI/`** (ARCHITECTURE.md's "no ImGui inside the engine" rule,
§8, means this cannot live in `src/Ink/` — it is app-side, ImGui-drawn,
reading/writing `CompGraph` only through Document's typed ops, same pattern
as Outliner/Properties). A new `Application/Editors/LayerGraph/` folder
hosts the first concrete consumer (the Layer Graph Editor panel), the same
way `Editors/Viewport/`, `Editors/Outliner/` etc. are one-editor-per-folder.

Minimum generic widget set (all design-system-token-driven, CLAUDE.md
Styling rules apply — no hard-coded colors/sizes):

- Node box: header (icon + name), body listing its ports.
- Port: typed socket (color/shape from `PortType`, via a design token so
  future port types stay themeable), left = input, right = output.
- Cable: bezier link between two ports, drawn in the node canvas' own
  overlay (ImGui draw list, not Ink — this UI has no canvas-content
  requirement, it never touches the Vulkan viewport).
- Drag-to-connect: press on a port, drag, drop on a compatible port
  (`PortType` match) commits one typed op (sets `CompPort::source` +
  `pinned = true`); dropping on empty canvas or an incompatible port cancels.
- Type validation: an incompatible drop is refused visually (the standard
  "no" cursor state) before commit — never a silent no-op that leaves the
  user unsure whether the connection was made.
- Pan/zoom of the node canvas (own camera, unrelated to the Viewport's).

Layer Graph Editor specifics on top of the generic widgets:

- Opening "Open Layer Graph" (Outliner context menu, or a Properties button
  on a Group) shows that layer's `CompGraph` — default setup is `Input`
  (targeting "merged children", i.e. the auto-generated chain) → `Output`
  (product-owner spec point 2).
- A "Node-only" workflow (product-owner spec point 1) means this panel must
  be usable **without** ever opening the Outliner's Layers mode — e.g. from
  a project that only uses Collections for organisation and drives every
  layer's content from its Layer Graph. Out of scope for Lot 13 itself
  (which targets parity with the Outliner-driven default first); noted here
  so Lot 13's port/type contracts don't accidentally assume an Outliner
  round-trip.

## 6. Open items (flagged for product-owner confirmation before Lot 13 locks the port table)

- ~~**Mask & Blend's `Mask` port type**~~ — moot: the Mask & Blend node no
  longer exists (§3). `Clip`/`Mask`'s own `Mask` `In` port is `RenderOutput`
  today (fed by a dedicated `Input`, §3) since only a single mask SOURCE is
  ever wired per layer in the shipped model — revisit if §7 ever lets a
  `Mask` node take an arbitrary cross-layer source.
- **DOC version number** for the persisted pinned-edge subset: still
  unassigned — moot until §7.1's `CompEdge` table actually needs persisting;
  `Node::compInputs` (the shipped Lot 13 model) added no new `.acu` section,
  it reuses the node's own block.
- **Cross-page targeting**: unchanged open item — see §7.1, same answer
  (an id is page-agnostic; restrict for now, lift later without a data-model
  change).
- ~~**Per-piece Merge not expressed in the graph**~~ — resolved as far as
  *display* goes: an enabled Erase-blend fill/stroke is one of `otherTrigger`'s
  triggers (§3), so the owning node's own Clip/Mask/Blend shape already
  reacts to it; the piece's own isolation (`Scene::OpenPieceScope`) is still
  the untouched Lot-4-era inline mechanism underneath and is not itself a
  graph node — unchanged scope, not a regression.
- **Superseded by §7**: bidirectional link-dragging across layers, cross-
  layer piece routing (§4's drag & drop) and Outliner object-encapsulation
  are NOT open items to confirm — the product owner has already asked for
  all three explicitly (2026-07-24 feedback). §7 is the design; the open
  question is scheduling (which increment ships first), not scope.

## 7. Real per-edge graph model (planned — not yet built)

**Status: designed, not implemented.** The shipped Lot 12/13 model
(`Node::compInputs`, a whole-graph reorder/filter of one layer's OWN
children — §2, ROADMAP Lot 13) is sufficient for everything built so far:
reordering/excluding/muting a layer's own children, shown and edited through
the Node Graph Editor. It is **not** sufficient for three things the product
owner has explicitly asked for (2026-07-24 feedback, verbatim below), all of
which need a real edge to exist somewhere other than "this layer's own
ordered list of its own children":

> "IMPORTANT: ... il faut absolument pouvoir drag & drop un link soit depuis
> son entrée, soit depuis sa sortie... Le graph behind the scene... doit être
> extrèmement flexible et robuste."
>
> "il faut alors revoir tout le fonctionnement pour intégrer proprement
> toutes ces fonctionnalités dans le graph engine... si on drag & drop un
> sous-composant... il sera encapsulé dans un parent qui sera le object
> layer..."

### 7.1 Why `compInputs` cannot grow into this

`Node::compInputs` is shaped as *"this layer's own children, reordered or
filtered"* — every entry's `node` is validated against the owning layer's
`children` (Document.h's own comment on `SetCompInputs`). Three things this
structurally cannot express, no matter how the validation is loosened:

1. **A cross-layer edge.** Layer B's Merge taking Object X as an input when X
   is layer A's child (not B's) means B's `compInputs` would hold a `NodeId`
   that is NOT in B's `children` — the exact case `SetCompInputs` refuses
   today, on purpose (§2: "Lot 13's scope is reordering/filtering a layer's
   OWN children"). Allowing it naively reintroduces the "does X still also
   render at its structural position in A" ambiguity §4 already flags for
   the Outliner drag.
2. **Bidirectional cable dragging that can also START from an existing
   connection's destination**, not just an `Input`'s source — Blender lets
   you grab a link at EITHER end and redrop it. Grabbing the `Merge`-side end
   of a cable to move it to a different downstream node (say, a future
   second Merge, or a cross-layer Blend input) has nothing to update in
   `compInputs`, because `compInputs` has no notion of "this edge" as an
   addressable thing — only "this layer's ordered list of sources".
3. **Object encapsulation** (a fill/stroke or a whole object appearing in
   MORE THAN ONE place in the Outliner because it feeds more than one
   layer) needs a real, queryable "what feeds what" relation to render two
   rows that both resolve to the same underlying id and stay selection-
   unified (§4's "Visual duplication rule") — `compInputs` only answers "what
   does layer L show", never "what layers does object X feed into", which is
   the reverse lookup encapsulation needs.

**The fix**: a real, global, addressable edge list — `CompEdge` — living on
`Document`, not per-layer:

```
CompEdge {
    id: CompEdgeId,          // stable, document-unique — an edge is a thing,
                              // not a position in a list
    from: CompEndpoint,      // a node / fill / stroke / (future) another
                              // layer's Output — never re-derived from position
    to: CompEndpoint,        // a node's Merge slot / Clip-Mask's Mask port /
                              // Blend's input / (future) a foreign layer's
                              // Merge slot
    muted: bool,
}
CompEndpoint { node: NodeId, fill: FillId, stroke: StrokeId, port: uint8 }
```

`Document::compEdges: std::vector<CompEdge>` (or a small map keyed by
`CompEdgeId`) replaces `Node::compInputs` as the persisted source of truth;
`BuildAutoGraph` starts from the SAME auto-generation rule (§2/§3) and layers
`compEdges` on top exactly the way `compInputs` does today — this is a
storage-shape change, not a behavior change, and should stay behind the same
"customized" badge / "Reset to automatic" action per layer (§0). A migration
path converts every existing `compInputs` entry into one `CompEdge` at load
time (`kDocVersion` bump, DOCUMENT_MODEL.md §10) — no data is lost, no user
action required.

### 7.2 Bidirectional link dragging on top of `CompEdge`

With `CompEdge` addressable by id, both drag origins become the same
operation — "pick up an existing `CompEdge` (or start a fresh one from a
bare `Out` port) and redrop one of its two ends":

- Drag starts on an **`Out` port with no existing edge** (an `Input`'s only
  port, or a node that doesn't feed anything yet): today's behavior, creates
  a new `CompEdge` on drop.
- Drag starts on an **`Out` port that already feeds an edge**: same as
  above — Out ports fan out to many `In`s in general (§3's Merge already
  needs N inputs), so grabbing the port always starts a NEW cable rather than
  detaching the existing one (matches Blender: dragging from an output
  socket always adds a link, never moves one).
- Drag starts on an **`In` port that already has an edge landing on it**:
  **picks that `CompEdge` UP** (by id) rather than creating a new one — an
  `In` port accepts at most one edge (§3.1 below), so grabbing it can only
  mean "move this one". Re-dropping on a different compatible `In` port
  updates that same `CompEdge`'s `to`; dropping in empty space (or an
  incompatible port) deletes it — both are what `UI::NodeGraphResult::
  cableDragEnded`/`connected` (NodeGraph.h) already exist to report; today's
  Node Graph Editor consumes them against `compInputs` position-reordering as
  a stopgap (NodeGraphEditor.cpp) — swapping the backing store to `CompEdge`
  changes what that handler writes to, not the widget contract.
- **Colour-coded compatibility while dragging** (product owner: "colorisation
  du lien si c'est sur un in/out incompatible") is already implemented at the
  widget level (`NodeGraph.cpp`'s per-frame hover-compatibility colouring) —
  §7 only changes what a valid drop COMMITS to, not the drag feedback itself.

### 7.3 Cross-layer routing and a future `LayerRef` input

Once `CompEdge::from`/`to` can name a node that is NOT a child of the layer
owning the edge, an `Input` can legitimately read from anywhere in the
Document — restoring exactly the routing example from the ORIGINAL spec
(§1: "Layer 2's Input targets Object X directly"). Two refinements this
unlocks, deliberately NOT scoped into the first `CompEdge` increment:

- **"Don't also render it at its structural position"** (§2's `CompInputOverride`
  doc comment already flags this as the deliberate follow-on): once Object
  X's piece feeds Layer B, does it still ALSO paint at its own tree position
  under Layer A? Default answer, matching Blender's "used-elsewhere" node
  behaviour: **yes, unless explicitly excluded** — a `CompEdge` is an
  ADDITIONAL consumer, not a move, so encapsulation (§7.4) is a DISPLAY
  convention for showing that reuse, not a hidden mutation of where X paints.
  An explicit per-edge "exclude from structural position" flag is future
  scope if a use case needs it.
- **A strictly-Layer-typed input** (`LayerRef`, reviving the original Mask &
  Blend node's confirmed-strict `Layer A`/`Layer B` ports, §3): reads
  another layer's fully-resolved `Output`, for a future cross-layer Blend
  node. Distinct from the ordinary `Input` kind (§1's hard rule: an ordinary
  `Input` never reads another layer's `Output`) — `LayerRef` is the
  deliberate, explicit exception, gated behind its own node kind so the rule
  in §1 stays true for every OTHER input.

### 7.4 Object encapsulation in the Outliner

With `CompEdge` giving a real "what feeds what" relation, the Outliner can
answer the reverse query §4 needs ("which layers does object X's fill feed")
by scanning `compEdges` for entries whose `from` matches X — no separate
index needed at this scale (a document's edge count is small relative to its
node count). Rendering follows §4's already-specified rule unchanged:

- A fill/stroke row still nests under its OWNER object by default (today's
  Lot 14 display).
- For every `CompEdge` whose `from` is that piece, the owner's row ALSO
  appears (display-only, no second Document node — §4's "Visual duplication
  rule") nested under whichever layer the edge's `to` feeds, so the same
  piece is reachable from every place it actually renders.
- Selecting ANY of these rows selects the one underlying object (§4:
  "Selection/movement stay unified") — the Outliner's existing `OutlinerRow`
  already carries an `id` separate from its position in the flattened list,
  so two rows sharing an `id` selecting together is a lookup, not a new
  mechanism.
- Fixing the current Lot 14 display rows' selectability (duplicate ImGui id,
  §4) is a prerequisite fixed the same day this section was written
  (`Outliner.cpp`'s `OutlinerChildKind` discriminator) — encapsulation adds
  MORE rows sharing a piece's id; they need to already be individually
  selectable before a second row per piece is worth building.

### 7.5 Suggested increment order

Not a commitment, a recommendation for whoever picks this up next:

1. `CompEdge` storage + migration from `compInputs` (§7.1) — mechanical,
   lowest risk, unlocks everything else.
2. Bidirectional dragging within ONE layer (§7.2) on the new storage — no
   user-visible change from today's Node Graph Editor except that grabbing an
   `In`-side cable now works too.
3. Cross-layer `Input` targeting (§7.3, minus `LayerRef`) — the actual
   product-owner ask ("drag & drop un sous-composant... encapsulé").
4. Outliner encapsulation display (§7.4) — needs (3) to have real edges to
   query.
5. `LayerRef` / cross-layer Blend (§7.3's future refinement) — no known
   product-owner ask yet beyond restoring Mask & Blend's original A/B ports;
   lowest priority of the five.

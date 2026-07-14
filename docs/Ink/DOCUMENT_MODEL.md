# Ink — document model

The new document model, designed from scratch. It replaces
`Renderer::Document/Artboard/Shape` entirely; no concept is carried over
without being re-justified.

## 1. Overview

```
Document
├── meta                        (name-independent doc settings, units, grid)
├── pages: [Page]               (work surfaces, laid out in one doc space)
├── layerRoot per Page: Node*   (THE z-order & compositing hierarchy)
├── collections: [Collection]   (organisational sets — no z-order role)
├── resources                   (images, pattern motif definitions, later fonts)
└── changeLog                   (typed change journal — feeds Scene + undo)
```

Two **independent** organisation systems coexist by design:

| | Layers tree | Collections |
|---|---|---|
| Structure | Hierarchical (groups, sub-groups, arbitrary nesting) | Flat sets (a collection can list member collections, but this nesting is organisational only) |
| Owns z-order | **Yes** — document draw order = depth-first walk | **No** — membership never affects stacking |
| Owns compositing | **Yes** — opacity, blend mode, isolation, clip per node | No |
| Membership | Every node sits at **exactly one** place in the tree | A node may belong to **any number** of collections |
| Typical use | "What the image looks like" (Affinity/Photoshop layers) | "What things mean" (symbol classes, print layers, selection sets, modifier targets) |

**Parenting** (object → parent object transform inheritance) is a third,
orthogonal relation: it lives on the node (`parentId`) and affects only the
resolved transform — never z-order, never compositing (Blender semantics).

**Modifiers** reference objects and collections through ids — they follow
these *relations*, and deliberately ignore where their inputs sit in the
Layers tree (Blender semantics: modifiers don't care about layers/groups).

## 2. Nodes (the Layers tree)

A `Node` is one entry in a page's layer tree. Kinds:

```
Node (common: id, name, parent link, transform, visibility, lock,
      opacity, blendMode, isolate flag)
├── Group        children: [Node]; optional clip source (its own geometry or
│                first child) — a group is a layer that composites as a unit
├── PathNode     geometry: PathData; style: Style (fills + strokes)
├── ImageNode    imageRef + sampling/fit params (raster content)
├── InstanceNode targetId (any node or group) + style/param overrides
└── TextNode     (reserved — built with the Typography module lot)
```

Key semantics:

- **Group = layer** (the Affinity/Photoshop model): a group carries
  opacity/blend/isolation and composites its subtree as a unit. There is no
  separate "folder" concept.
- **Transforms** are affine 2D (`Mat23`, stored as translate/rotate/scale/
  skew components + cached matrix). Resolved transform =
  `parentChain(parentId) × local`. The parent chain is object parenting, NOT
  the layer-tree position (a child in the tree does not inherit transform
  from its group unless the group is also its `parentId` — groups get an
  optional "transform children" toggle whose default is ON, which simply sets
  members' parentId to the group; explicit and visible, no hidden coupling).
- **Ids** are `uint64`, document-unique, never reused (monotonic counter
  persisted in the file). All cross-references (parenting, instances,
  modifiers, collections) are by id.
- **Deleting** a referenced node: references resolve to "missing" (instances
  render nothing, modifiers skip the input) and the UI surfaces it — no
  cascade deletes, no dangling pointers (ids, not pointers).

### 2.1 Clipping & masking (Affinity model)

Two distinct, composable mechanisms — both implemented with the render
graph's stencil clip pass (RENDER_GRAPH §ClipPass), so both are vector-exact
at any zoom:

- **Per-node clip / mask layer.** ANY node can hold children (a path's
  children are *nested inside it*). A child is by default a **clip child**:
  it paints only where its parent's fill covers (`node.isMask == false`). A
  child flagged `isMask` is a **mask layer** instead: it does not paint — its
  coverage masks the parent (and the parent's other children), so they show
  only through the mask's shape. The parent's own fill still paints. This is
  the "drag a layer onto another (clip) or onto its thumbnail (mask)"
  gesture. The mask/clip source is the child itself, chosen by direct
  nesting; nothing is ambiguous.

- **Clip group** (`Group.clip`). A *group* masks its WHOLE subtree by its
  **first path child** (the topmost child in the list — SVG clip-path
  semantics). Every other member is clipped by that one shape. This is the
  "clip many sibling objects with one shared shape" case: instead of nesting
  each object, drop them all into a group and mark the group `clip`. The clip
  shape is that first child (so in the demo the ellipse "clip shape" clips
  every "dot"). The clip source is thus *implicit* (first child) rather than
  per-object. Toggle it from the Layers outliner context menu ("Clip to First
  Child") or the Properties Compositing panel ("Clip"). A clip group also
  composites as a unit (opacity/blend apply to the masked result), which a
  loose per-node clip does not.

Rule of thumb: per-node clip/mask nests one thing inside one thing; a clip
group shares one mask across a whole group.

## 3. Geometry: PathData

One geometry type for everything (no Mesh/Curve split — the old model's
Part/PartType families are gone):

```
PathData
└── subpaths: [Subpath]
    └── points: [Anchor { pos, in-handle?, out-handle?, kind }], closed: bool
```

- Anchor kinds: corner / smooth (aligned) / symmetric (mirrored) — the
  editing semantics live in the *editor*; the model stores handles.
- **Spline type per subpath** (`SplineType { Bezier, Nurbs, Poly }`, the legacy
  spline families, IOF_CORE_PLAN Phase B — DONE): `Bezier` interprets the
  anchors as on-curve points with in/out cubic handles; `Poly` is a straight
  polyline through the anchors; `Nurbs` treats them as CONTROL POINTS of a
  rational uniform B-spline of degree `orderU − 1`, with a per-anchor rational
  `weight` and two knot-mode flags — `nurbsEndpoint` (clamp an open curve to its
  end control points) and `nurbsBezier` (interior knots at full multiplicity =
  consecutive rational Bézier segments, the exact-circle/arc form). Flattening
  per type happens in `geom::Flatten` at the view tolerance; all spline fields
  fold into `PathData::Hash`.
- Booleans/derived geometry are **modifiers**, not baked path edits (the old
  destructive edits become ops that write PathData through the ChangeLog).

## 4. Style: unified fill + stroke

Requirement: *"no technical shape/stroke split — any stroke can be filled,
any shape can have a stroke"*.

```
Style
├── fills:   [Fill  { paint: Paint, fillRule: NonZero|EvenOdd, enabled }]
└── strokes: [Stroke { paint: Paint, width, align: Center|Inside|Outside,
                       cap: Butt|Round|Square, join: Miter|Round|Bevel,
                       miterLimit, dash: {pattern[], offset}, enabled,
                       widthSpace: Document|Viewport,
                       marks: [StrokeMark …] }]
```

- **`StrokeMark`** (the legacy LineMark, IOF_CORE_PLAN Phase A — DONE): a manual
  glyph or phase pin at arc-length `t` on one subpath —
  `{ kind: SlopeTick|Crossing|Bridge|Pylon|DashAnchor, sub, t, side, gap, size,
  thickness, outsideMeasure, square, nodeAnchor }`. Applied by the stroker (gap
  cuts, dash re-phasing, glyph meshes — see GEOMETRY.md §2); edited by the
  line-mark tool and the Strokes Properties "Marks" list.

- Both lists are ordered (paint order: fills bottom-up, then strokes
  bottom-up) and both take **any** `Paint` — a stroke filled with a pattern
  is just `Stroke{paint: Pattern…}`. There is exactly one styling code path.
- A stroke is *geometry generation* (the stroker turns path + stroke params
  into an area — see GEOMETRY.md, including inside/outside on open paths);
  painting that area uses the same machinery as a fill. This is what unifies
  the model.
- `widthSpace: Viewport` gives non-scaling strokes (hairlines/annotations).
- **Strokes under object transforms (Blender semantics, user-locked):** the
  stroke area is generated in object-local space, then the object transform
  applies — so a non-uniform Object-Mode scale stretches strokes with the
  object (a stretched square outline is thicker on two sides, by design).
  Edit-Mode vertex edits reshape the path without touching the transform, so
  strokes stay uniform. An **Apply Scale** operation (Lot 8, with the editing
  loop) bakes the transform into PathData — restoring uniform 1:1 stroking
  while keeping the object's current shape.

### Paints

```
Paint = Solid   { rgba (linear) }
      | Pattern { motifRef → node/group id or resource, spacing, phase,
                  rotation, scale, offset, clipToShape }   // instanced motifs
      | Image   { imageRef, fit, tile }
      | Gradient{ linear/radial, stops }                    // later lot
```

`Pattern` keeps the current feature set (the old pattern params are
re-specified, not copied): the motif is a *referenced* definition rendered
through the same instancing machinery as InstanceNode — a pattern fill with
10 000 motif repeats is one instanced draw, not 10 000 geometries.

## 5. Instancing

Three faces of one mechanism (see RENDER_GRAPH.md §GPU data for how this maps
to instanced/indirect draws):

1. **InstanceNode** — a node whose content is "render target subtree with my
   transform (+ overrides)". Editing the source updates every instance;
   instances are selectable/transformable objects in their own right.
2. **Pattern paint** — the fill region is covered by instances of the motif
   on a lattice (spacing/rotation/phase params), clipped to the region.
3. **Along-path modifier** — generates instance transforms by sampling a
   target path by arc-length (count or spacing, aligned-to-tangent option,
   start/end trim). The instances' source is a node/collection reference.

The Scene expands instances *logically* (an item that references a compiled
definition), never by duplicating geometry. Nested instances are allowed with
a depth clamp (documented limit, default 8) and cycle refusal at edit time.

## 6. Modifiers

Non-destructive operators attached to a node (ordered stack), evaluated at
Scene compile. They reference other objects/collections **by id** and ignore
Layers placement. Initial set (grown lot by lot):

```
Modifier = AlongPath   { pathRef, spacing|count, align, trim }     (instancing)
         | Array       { count, offset transform }                 (instancing)
         | Boolean     { op: Union|Subtract|Intersect|Xor, operandRef }
         | Mask/Clip   { sourceRef, mode }                          (render-level)
         | ...         (open set; each modifier declares its inputs so the
                        Scene knows the dependency edges for dirty tracking)
```

Contract: a modifier is a pure function
`(inputs: geometry/instances, params, referenced items) → (geometry/instances)`.
Declared dependencies make incremental recompile exact: editing a referenced
path re-evaluates only the modifiers that consume it.

## 7. Collections

```
Collection { id, name, color tag, members: [nodeId], childCollections: [id] }
```

- Membership is many-to-many; no z-order, no compositing.
- Uses: Outliner "Collections" view, selection sets, module semantics (e.g.
  IOF print layers when that module is rebuilt), and **modifier/tool
  targeting** ("scatter members of collection X along this path").
- Per-collection visibility/selectability toggles apply as *filters* at Scene
  compile (an item hidden by ANY route — layer, collection, page — is culled).

## 8. Pages

`Page { id, name, pos, size, background }` — work surfaces sharing one
document coordinate space (unchanged concept, reworked implementation). Each
page owns a layer tree root. The page *background* is a display substrate,
**not** a layer: erase/blend inside the stack never punches through it (rule
— the substrate is a display backdrop, not a layer).

## 9. Change tracking & undo

Every mutation goes through typed operations
(`doc.ops().SetStrokeWidth(id, …)`, `doc.ops().AddNode(…)`, …) which:

1. apply the change to the model,
2. append a `Change { nodeId, field-class }` to the frame's ChangeLog
   (consumed by Scene::Compile for exact dirtying),
3. emit an undo record (the op + its inverse — command-based undo replaces
   the old whole-document snapshot diffing; measured, bounded, and it gives
   labelled steps for free).

## 10. Serialisation (.acu, DOC v3)

- New chunked container (magic/version/sections with sizes; unknown sections
  skipped) — **a clean break**: no migration from old .acu (the format
  version gates a clear "old file" error; the old reader lives only in
  `_legacy`).
- Sections: META, DOCUMENT (model), LAYOUT (zone tree — kept concept),
  THMB (PNG thumbnail — kept concept, Windows shell integration unchanged).
- The DOCUMENT blob is versioned independently (`kDocVersion`): **v2** added the
  array placement modes + instance copy flags; **v3** adds the subpath spline
  params (`spline`, `orderU`, endpoint/bezier flags), the per-anchor `weight`,
  and the stroke `marks`. Readers are `ver`-gated, so a v2 file still loads (the
  new fields take their defaults).
- Written/read by `App::ProjectFile` (app side) through the Document public API.

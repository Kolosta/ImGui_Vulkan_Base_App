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

- **Group = layer** (Affinity model, kept from the Compositor experience): a
  group carries opacity/blend/isolation and composites its subtree as a unit.
  There is no separate "folder" concept.
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
- NURBS-style weighted control (from the old SplineType) is folded into a
  per-subpath interpolation tag (`bezier` default, `poly`, room for more);
  Lot 2 ships `bezier` + `poly`, others only when a real need returns.
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
                       widthSpace: Document|Viewport }]
```

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
inherited from the Compositor's validated design).

## 9. Change tracking & undo

Every mutation goes through typed operations
(`doc.ops().SetStrokeWidth(id, …)`, `doc.ops().AddNode(…)`, …) which:

1. apply the change to the model,
2. append a `Change { nodeId, field-class }` to the frame's ChangeLog
   (consumed by Scene::Compile for exact dirtying),
3. emit an undo record (the op + its inverse — command-based undo replaces
   the old whole-document snapshot diffing; measured, bounded, and it gives
   labelled steps for free).

## 10. Serialisation (.acu v2)

- New chunked container (magic/version/sections with sizes; unknown sections
  skipped) — **v2 is a clean break**: no migration from old .acu (the format
  version gates a clear "old file" error; the old reader lives only in
  `_legacy`).
- Sections: META, DOCUMENT (model), LAYOUT (zone tree — kept concept),
  THMB (PNG thumbnail — kept concept, Windows shell integration unchanged).
- Written/read by `App::ProjectFile` (app side) through the Document public
  API. Detailed spec written when the lot lands (`docs/acu-format-v2.md`).

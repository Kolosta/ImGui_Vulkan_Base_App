# IOF core re-entry — legacy feature inventory & phased plan

The IOF Mapping module returns on Ink (ROADMAP Lot 11 remainder). Everything
below was shipped in the legacy stack (quarantined under `src/_legacy/`,
behavioural reference ONLY) and must be rebuilt on the Ink document/scene/
render-graph. The CORE features land first (usable in Classic mode, in the
normal Property Editor); the module then curates them (special display,
restrictions, ISOM presets).

**Status: Phase A and Phase B are DONE in core** (this pass). The GENERIC mark
model + isolated-stroke rendering, the line-mark tool, the marks Properties
editor, the NURBS/Poly spline model + evaluation, the Curve Properties panel,
the Shapes/Curves Shift+A menu (with draw-on-create for shapes AND curves), the
pen tool and the legacy selection/vertex colours all landed and are covered by
`ink_tests` (`TestNurbs`, `TestStrokeMarks`). Persistence rides `.acu` DOC
**v4**. Phase C (the module curation) is the remaining work.

## Phase A — Marks on strokes (core) — DONE (generic model)

The core mark is a **generic** annotation — NOT the typed IOF glyphs. It is a
point on a stroke, addressed by arc length, that (a) optionally re-phases the
dash run and (b) carries a list of OBJECTS. The IOF-specific ticks / crossings /
bridges / pylons are rebuilt from these primitives BY THE MODULE (Phase C), so
core has no notion of ISOM "kinds".

- **Model** (per mark): `sub`, `t` ∈ [0,1], `phase` (Neutral / Dash / Gap —
  Dash/Gap force a dash element / gap to land centred here, Neutral does no
  re-phasing), `side` (Center / Left / Right), `offset` (signed distance to the
  line for Left/Right, may be negative), `nodeAnchor` (pin to a control point;
  −1 = free), and `objects[]`.
- **Objects** (`MarkObject`): `shape` ∈ { Circle, Rectangle, Diamond, their
  Inverted forms (per w3.org/TR/svg-markers), Instance }, `mode` (Add /
  Subtract), `blend` (Add only), `size`, `rotation`, `nodeRef` (for Instance),
  `color` / `useStrokeColor` (primitive fill). Several objects per mark.
- **Rendering**: the Scene emits a stroke-with-objects into its OWN isolation
  scope; Add objects paint (nested scope for a non-Normal blend), Subtract
  objects use an absolute geometric ERASE (`ClipRole::EraseWrite`, dst-out
  `contentErasePipeline`) so the shape CUTS the stroke, and Instance objects
  route a node's subtree at the mark. The stroke then composites into the layer
  above with real holes — see RENDER_GRAPH.md §Erase. The stroker only does the
  dash re-phasing (`AnchorDashOffset`).
- **Tool** (`tool.linemark`): handles render only while active
  (Normal/Hover/Selected, phase-indicator diamond/square); click drops a
  neutral mark with one default object (the top-bar SHAPE + add/subtract
  toggle); a handle click selects (Shift toggles, Alt deletes) and arms a slide;
  **G** slides along the curve, **R** cycles the side (Center→Left→Right), **X**
  deletes; ghost preview under the cursor.
- **Properties**: a "Marks" editor on the stroke — phase / side / offset per
  mark, then a per-mark object list (shape, mode, blend, size, rotation, node
  picker for Instance, colour for primitives), all undoable via the style
  commit machinery.
- **Persistence**: marks ride the Stroke record (.acu DOC v4).

Landed in: `Ink::StrokeMark` / `Ink::MarkObject` on `Ink::Stroke` (Style.h),
folded into `Stroke::GeometryHash`; the stroker
(`src/Ink/src/Geometry/Stroker.cpp`) re-phases dashes only; the Scene
(`Scene::EmitStrokeMarks`) opens the isolation scope and emits the objects; the
RHI gains `BlendKind::Erase` + `contentErasePipeline` and the Scene gains
`ClipRole::EraseWrite`. Tool + Properties in
`src/Application/Editors/Viewport/ViewportMarkTool.cpp` and
`PropertiesPaint.cpp`.

## Phase B — Curves, NURBS & the pen workflow (core) — DONE

- **Spline types** (legacy `SplineType`): Bezier (anchors ON the curve,
  in/out handles — today's model), **Nurbs** (anchors are CONTROL POINTS off
  the curve; uniform B-spline of degree `orderU − 1`; handles ignored;
  per-point **weights**; endpoint clamping setting), Poly (straight
  polyline). Lives per SUBPATH; flattening in `geom::Flatten` per zoom tier.
- **Curve properties panel**: spline type, order U, resolution, cyclic,
  `openFillStraight` (open curve's fill closes straight vs follows the
  curve's own evaluation), convert-to (Bezier ↔ NURBS ↔ Poly), Join rules
  (same family only, like legacy Mesh/Curve part families).
- **Shift+A menu returns to the legacy two-column split**: **Shapes**
  (Rectangle, Ellipse/Circle, Triangle, …) / **Curves** (Bézier, Bézier
  Circle, NURBS, NURBS Circle, Poly Line).
- **Draw-on-create option** (the legacy checkbox): instead of spawning a
  ready-made object at the 2D cursor, Shift+A arms a LIVE construction:
  - Curves → the **pen tool**: click = corner anchor, click-drag = smooth
    anchor with handles, Ctrl = snap, Backspace = drop last anchor,
    Enter/double-click = finish open, close on first anchor = cyclic,
    Esc = cancel. Legacy precision-drag semantics.
  - Shapes → **drag-drawing**: press-drag defines the rect/ellipse extent
    (Blender/IOF style), with the shape GHOST PREVIEW at the cursor's
    bottom-right (exactly the IOF symbol-placement preview treatment).
- **Follow-curve tracing** (legacy OOMapper-style): while drawing or editing
  a curve/shape, the legacy shortcut snaps the run to ANOTHER shape's
  outline, picking the START and END of the followed span; works with the
  snap-while-drawing behaviour already proven in legacy.

Landed as: `SplineType { Bezier, Nurbs, Poly }` + `orderU` + `nurbsEndpoint` +
`nurbsBezier` per `Ink::Subpath`, and a per-anchor rational `weight`, all folded
into `PathData::Hash`. `geom::Flatten` (`src/Ink/src/Geometry/Flatten.cpp`)
dispatches per subpath: `FlattenNurbs` is a full homogeneous de-Boor evaluator
over the three knot modes (endpoint-clamped / full-multiplicity-Bézier /
uniform-periodic) with adaptive chord-error subdivision; Poly is the control
polygon verbatim. Builders `PathData::NurbsCircle` (8-point square hull, order 3,
√2/2 corners — an exact circle) and `PathData::Nurbs` (open clamped order 4) live
in `Document.cpp`. The Shift+A menu splits into **Shapes** / **Curves** submenus
with a **Draw on Create** toggle. When on:
- **Shapes** (rect/ellipse/triangle) and the **circle** curves (Bézier Circle,
  NURBS Circle) arm a drag-to-place: a press-drag on the canvas defines the box
  and `SpawnShapeInRect` builds the shape at that size/position, with a live
  flattened ghost during the drag (`CanvasDrag::Kind::DrawShape`,
  `BuildShapeGeometry` shared via `ViewportShapes.h`).
- **Open curves** (Bézier / NURBS Path / Poly) arm the pen tool
  (`BeginPenDraw`/`HandlePenInput`/`CommitPenDraw`): click = corner anchor,
  click-drag = symmetric handles (Bézier), Backspace = drop last, Enter /
  double-click = finish, click-first-anchor = close, Esc = cancel. A
  handle-less spline (NURBS / Poly) shows the LIVE curve to the cursor (a
  phantom control point flattened each frame — the drag has no effect there).

The Curve Properties panel (`PropCurveSection` in `Properties.cpp`) exposes
spline type, cyclic, Order U, Endpoint/Bezier U, and the selected control
points' weight (Edit mode). The NURBS control hull draws in the edit overlay
(`C_EditHandle_NurbsHull`). *Not yet ported* (deferred, low value now):
resolution/convert-to/join-family rows, `openFillStraight`, follow-curve
tracing.

## Phase C — IOF Mapping module re-entry

On top of A+B, the module curates:

- **IofSpec catalogue**: the full ISOM symbol table (codes, names, colours,
  print layers, geometry classes, descriptions) + `AutoMarkKindFor` presets
  (101–103 → SlopeTick, 510/511 → Pylon, dashed corners → DashAnchor) and
  `ApplyMarkPreset` (mm sizes × map scale via the `symbolScale` capability).
- **Symbol placement**: the Shift+A symbol picker with the cursor-corner
  ghost preview; `previewPlacement` capability forces it.
- **Print-layer organisation**: the Outliner grouped by exact ISOM print
  layer (SpotColor vs PrintLayer split), locked tree (`lockOutlinerTree` —
  symbols stay in their layer collections), mm document unit.
- **Symbol Viewer editor**: the 3-pane rework (resizable symbol grid,
  zoom/pan example canvas, precise symbol plates, full descriptions).
- **Map settings editor** (scale, colours) and **Course Settings** overlay
  (the course line drawn over control objects via the module viewport-overlay
  hook, which already exists in ModuleAPI).
- **Glyph rendering service**: cached symbol thumbnails (the ModuleHost
  glyph-texture service of the legacy host) — on Ink: per-symbol off-screen
  views, the same mechanism as the paint vignettes.

## Order & dependencies

A and B are independent of the module and land in core lots (A first — it
only extends Stroke + Scene; B touches the geometry kernel with NURBS
evaluation). C depends on both plus the Lot 11 module hooks (done). Each
phase updates DOCUMENT_MODEL/GEOMETRY docs and .acu DOC versioning, with
ink_tests coverage (mark arc-length placement, dash re-phasing, NURBS
evaluation against known B-splines, follow-curve span extraction).

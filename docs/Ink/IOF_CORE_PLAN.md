# IOF core re-entry — legacy feature inventory & phased plan

The IOF Mapping module returns on Ink (ROADMAP Lot 11 remainder). Everything
below was shipped in the legacy stack (quarantined under `src/_legacy/`,
behavioural reference ONLY) and must be rebuilt on the Ink document/scene/
render-graph. The CORE features land first (usable in Classic mode, in the
normal Property Editor); the module then curates them (special display,
restrictions, ISOM presets).

## Phase A — Marks on strokes (core)

The legacy `LineMark` (Renderer/Document/Paint.h) — a point ON a stroke,
addressed by arc length, that decorates or re-phases the line WITHOUT touching
its geometry:

- **Model** (per mark): kind, subpath index, `t` ∈ [0,1] arc-length position,
  `side` (+1 left of travel / −1 right), `gap`, `size`, `thickness` (0 = base
  line width), `outsideMeasure` (size measured from the stroke's OUTER edge —
  ISOM "OM" convention), `square` (pylon box variant), `nodeAnchor` (pin the
  mark to a control point so it tracks edits; −1 = free at `t`).
- **Kinds**: SlopeTick (contour/form-line downhill tick, 101–103), Crossing
  (519: a gap cut in the line + two ticks across), Bridge (512: gap + two
  facing brackets), Pylon (510/511: pinned crossbar, optional box), DashAnchor
  (NO geometry: forces a dash/pattern ELEMENT (+1) or GAP (−1) to land centred
  there and re-phases the run independently on each side).
- **Implicit START/MIDDLE/END markers** on every open stroke (invisible,
  always present, zero cost until used): the anchor points for end arrows /
  start decorations — settled design question: they are ordinary marks with
  reserved positions, so arrowheads become a mark kind later.
- **Rendering**: derived geometry at Scene compile (per zoom tier like
  booleans): gap-splitting the stroke's dash run, tick/bracket/bar meshes —
  EXACT legacy shapes and colours, through the normal Vulkan content pass.
- **Tool** (`tool.linemark`, same shortcut & visibility rules as legacy):
  marks render handles only while the tool is active (Normal/Hover/Selected
  states, legacy colours); click places (with ISOM auto-preset in the module);
  click modes: cycle form-line, toggle dash-anchor side; G/R/S act on the
  SELECTED marks (slide along the curve / flip side / scale size) with the
  precision-drag + Ctrl-snap behaviour; X deletes selected marks
  (quasi-objects); ghost preview under the cursor before placing.
- **Properties**: a "Marks" list on the stroke (vignette-rail or row list per
  the existing Paint panels), fields per kind, undoable via the style commit
  machinery.
- **Persistence**: marks ride the Stroke record (.acu DOC v3).

## Phase B — Curves, NURBS & the pen workflow (core)

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

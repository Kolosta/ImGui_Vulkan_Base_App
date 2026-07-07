# Ink — geometry pipeline

CPU geometry layer: how a `PathData` + `Style` becomes triangles in the GPU
pools. Windowless, deterministic, unit-tested. The interface is
backend-agnostic so a later GPU-compute geometry lot can swap the
implementation without touching Scene or Render.

## 1. Stages

```
PathData ──flatten──► Polyline(s) ──┬─fill──► FillTess ──► mesh range (pool)
        (per zoom tier, adaptive)   └─stroke─► Stroker ──► StrokeTess ──► mesh range
```

- **Flatten**: adaptive subdivision of Bézier segments to a screen-space
  error tolerance derived from the item's **zoom tier** (see §4). Output
  polylines are shared by fill, stroke, hit-testing and snapping.
- **FillTess**: polygon triangulation with holes (non-zero / even-odd rule
  applied at classification). v1 algorithm: monotone decomposition
  (ear-clipping fallback for degenerate input); robustness beats elegance.
- **Stroker**: converts (polyline, width, align, caps, joins, miterLimit,
  dash) into an outline polygon → triangulated. See §2, the interesting part.

## 2. Stroking semantics (the spec)

### Alignment — Center / Inside / Outside, including OPEN paths

- **Center**: offset ±w/2 both sides of the spine. Trivially defined for open
  and closed paths.
- **Closed** paths: Inside/Outside are defined by the subpath's signed area
  (winding): Inside offsets toward the interior (offset w on the interior
  side), Outside away from it.
- **Open** paths (the requirement the old engine never met): Inside/Outside
  are defined by the path's **local orientation**: walking the path from its
  first to last anchor, *Inside = the right-hand side, Outside = the
  left-hand side*. Equivalently: the stroke band is `[0, w]` (Inside) or
  `[-w, 0]` (Outside) along the signed normal, instead of `[-w/2, +w/2]`.
  - Deterministic, editable (reversing the path flips the side — an editor
    op "Reverse direction" makes this a feature, and the edit overlay can
    show the direction), and consistent with how the closed-path case
    degenerates when a path is cut open.
  - Caps apply at the band's ends exactly as for center strokes (the cap is
    drawn on the band, not the spine).
- End caps: Butt / Round / Square. Joins: Miter (with limit → bevel
  fallback) / Round / Bevel.

### Dashes

Dash pattern + phase are applied by arc-length along the spine **before**
outlining, so each dash is a correctly capped mini-stroke. Dash lengths are
in document units (or viewport units when `widthSpace: Viewport`).

### Self-intersection policy

v1: the stroke outline is emitted with non-zero fill rule so overlaps
(tight joins, small radii) render solid without expensive outline booleans.
A later quality lot can add exact outline union if artefacts show at low
opacity (documented known-limit: an overlapping translucent stroke darkens).

## 3. Caching

```
GeometryCache key:  (pathHash, geomParamsHash, tier)
  pathHash        = content hash of PathData (anchors/handles/closed)
  geomParamsHash  = stroke geometry params (width/align/caps/joins/dash) —
                    NOT paints/colors (style-only edits never re-tessellate)
  tier            = zoom tier index
value: { fill mesh range, stroke mesh ranges, bounds, flatten polylines }
```

- Eviction: LRU by bytes + "not referenced by any live item" sweep.
- The strict key discipline is a hard rule learned from the old engines:
  *anything not in the key must not affect the cached output* — and
  conversely, render-only params (opacity/blend/paint) must NOT be in the key
  (they invalidated nothing but caused full rebuilds in the legacy cache),
  they live in the item/paint tables instead.

## 4. Zoom tiers

Flattening error is screen-space, but re-flattening every item on every zoom
tick would defeat the caches. Zoom is bucketed into tiers (×2 steps); an item
re-tessellates only when its view's tier changes, and tiers are hysteresis-
banded so zooming around a boundary doesn't thrash. Per-item override: tiny
on-screen items degrade to coarser tiers (bounded triangle budget).

## 5. Bounds & hit-testing

- Every item caches conservative doc-space bounds (geometry ∪ stroke bands ∪
  pattern overflow) — used by culling, fit-view, snapping candidates and the
  CPU hit-test.
- CPU exact hit-test (used by click-frame picking fallback and box/lasso
  select): point-in-fill via winding over the flatten polylines; stroke test
  via distance-to-spine ≤ band; both reuse cached polylines. GPU id-picking
  covers the hover case (RENDER_GRAPH.md §PickingPass).

## 6. Units & precision — the unbounded canvas

- **No engine zoom/extent limits** (README req. 9): a document may work in
  micrometres or kilometres and the camera zooms fluidly through the whole
  range. Unit scales are a document/display setting; the engine computes in
  abstract doc units, in `double`, with no magic ranges anywhere.
- Document space is `double`; geometry emits **f32 relative to a per-view
  anchor** (the camera origin, subtracted in double before narrowing; GPU
  instance translations are rebased against the anchor, which re-snaps when
  the camera strays or crosses zoom tiers) so deep zoom never wobbles. This
  applies to the whole chain — including the viewport's camera state itself
  (pan/zoom in double, not float).
- All tolerances (flatten error, snap radius, hit radius) are defined in
  screen px and converted through the view's effective zoom — behaviour is
  resolution- and zoom-independent by construction. Zoom tiers (§4) keep
  re-tessellation bounded across the range.

## 7. Culling — performance never at the cost of exactness

View culling (and every later optimisation) drops **work**, never **inputs**:

- An item outside the view is not rasterised, but it still participates in
  everything that affects visible pixels: modifier evaluation (a visible
  instance whose source path/collection is off-screen), clip sources,
  blend-mode backdrops, bounds and relations.
- Partially visible items render exactly (the scissor clips pixels; geometry
  is never truncated or approximated at the view edge).
- When zoomed far in, off-view objects and off-view parts of visible objects
  cost as little as possible (bounds culling, later finer-grained ranges) —
  but an "optimisation" that could change one visible pixel is a bug.
- The perf suite pins this: a culled render and a force-unculled render of
  the same view must produce identical images (PERF_TESTING.md).

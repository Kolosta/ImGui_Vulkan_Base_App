#pragma once

#include "Renderer/Document/Document.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  Tessellator — turns vector shapes into flat triangles for Vulkan.
//
//  CPU tessellation (no external lib): every shape becomes a list of coloured
//  vertices in DOCUMENT units. The vertex shader applies the per-view camera
//  (pan/zoom + target size), so the triangle buffer is camera-independent.
//
//  Capabilities:
//   - Fill: rectangle/ellipse/triangle as triangle fans; arbitrary closed
//     polylines/curves via ear-clipping (handles convex and simple concave).
//   - Stroke: each segment expanded to a quad, with round joins/caps, so open
//     and closed contours render at a given width.
//   - Curves: cubic Bézier segments are flattened with an adaptive step count
//     (more samples when zoomed in) before fill/stroke.
// ─────────────────────────────────────────────────────────────────────────────

// Matches the vertex shader input layout (location 0: vec2 pos, 1: vec4 color).
struct Vertex {
    float x, y;           // document-space position
    float r, g, b, a;     // straight RGBA
};

struct Mesh {
    std::vector<Vertex> vertices;   // 3 per triangle (non-indexed)
    bool empty() const { return vertices.empty(); }
    void clear() { vertices.clear(); }
};

// ── Instanced fill patterns (Phase 2) ─────────────────────────────────────────
//  A surface's pattern layer (dots / triangles / dashes) is drawn as ONE small
//  UNIT base mesh stamped N times, instead of N glyphs of real triangles. Each
//  element is one PatternInstance; the GPU clips the whole pattern to the surface
//  via a stencil mask, so elements never spill past the contour.
enum class PatternElementKind : uint8_t {
    Disc     = 0,   // radius-0.5 unit disc — Dots / PairDots
    Triangle = 1,   // unit 8:6:5 scalene triangle — (fills only, legacy)
    Quad     = 2,   // unit 1×1 quad — ticks/bars/pickets/cross-arms/chevron-arms;
                    //   length on local X, thickness on local Y (sx=len, sy=thick)
    HalfDisc = 3,   // r-0.5 half-disc, flat diameter on local X, bulge +Y — HalfDots
};

// Matches pattern.vert (binding 1, per-instance): centre + non-uniform scale +
// rotation + straight RGBA. Baked PAGE-LOCAL in the cache (centre shifted by
// pageOrigin on append), exactly like the triangle Vertex stream. `sx,sy` lets one
// unit Quad cover thin ticks/bars (sx=thickness, sy=length); discs set sx=sy.
struct PatternInstance {
    float cx, cy;         // element centre, document units
    float sx, sy;         // non-uniform scale (doc-units per unit-mesh extent)
    float rot;            // element rotation, radians
    float r, g, b, a;     // straight RGBA (includes the layer opacity)
};

// One contiguous run of instances of a single element kind for one surface — the
// renderer issues vkCmdDraw(baseVerts(kind), count, 0, firstInstance) per batch,
// stencil-tested EQUAL to `surfaceRef`. Plus the surface's triangulated CUT
// polygon (mask) vertex range, written to the stencil before the instanced draw.
struct PatternBatch {
    PatternElementKind kind = PatternElementKind::Disc;
    uint32_t firstInstance  = 0;   // into the shape's instance list
    uint32_t instanceCount  = 0;
    uint32_t maskFirstVertex= 0;   // into the shape's mask-vertex list
    uint32_t maskVertexCount= 0;
};

// ── Procedural fill patterns (Phase 3) ────────────────────────────────────────
//  A surface's pattern layer is NOT generated element by element. The renderer
//  draws the surface's CUT polygon once as a cover quad and a fragment shader
//  (pattern_fill.frag) paints the motif from these parameters + the document-space
//  position, clipped to the contour by the stencil. Editing spacing/size/angle/
//  offset/contour is therefore O(1) on the CPU. `kind` is the FillPattern enum
//  value (1 Dots 2 Lines 3 Triangles 4 RandomDots 5 Grid 6 CrossHatch); Solid (0)
//  is baked as a plain polygon and never produces a PatternParams.
struct PatternParams {
    uint8_t  kind = 1;
    Color    color{0,0,0,1};
    float    spacing = 1.0f, size = 0.3f, angle = 0.0f;
    Vec2     offset{0,0};
    uint32_t seed = 1u;
    float    dash = 0.0f, dashGap = 0.0f;
    uint8_t  altPhase = 0;
    Vec2     center{0,0};   // surface bbox centre, doc-units (lattice origin)
};

// Optional pattern outputs for the shape tessellator. When set, procedural patterns
// emit a triangulated cover polygon (= cut polygon) into `cover` and one PatternRec
// per layer into `recs`, instead of baking pattern triangles into the mesh.
struct PatternRec {
    uint32_t coverFirst = 0;   // into `cover` (the build's cover-vertex stream)
    uint32_t coverCount = 0;
    PatternParams params;      // page-local centre (shifted on append)
};
// A transparent stroke's ribbon, emitted as COVERAGE (not coloured triangles) so it
// can be drawn into the stencil then filled ONCE — overlapping ribbon triangles would
// otherwise double the alpha and show dark seams/corners. Page-local; the colour +
// bbox travel with it. Opaque strokes skip this (no overlap artifact at alpha=1).
struct StrokeRec {
    uint32_t coverFirst = 0;   // into the build's stroke-cover stream
    uint32_t coverCount = 0;
    Color    color{0,0,0,1};
    Vec2     bbMin{0,0}, bbMax{0,0};   // page-local, from the REAL ribbon verts
};
struct PatternSink {
    Mesh*                      cover = nullptr;   // cut-polygon triangles
    std::vector<PatternRec>*   recs  = nullptr;
    // Transparent-stroke coverage (Lot A). null → strokes baked coloured as today.
    Mesh*                      strokeCover = nullptr;
    std::vector<StrokeRec>*    strokeRecs  = nullptr;
};

// ── Core curve instancing (Phase 4) ───────────────────────────────────────────
//  Periodic line decorators (LineDecor glyphs) are stamped as GPU INSTANCES of a
//  unit base mesh along the curve, instead of baked triangles — so a long dense
//  styled curve costs one vkCmdDraw(baseVerts, N) per glyph kind, not N glyphs of
//  real triangles. One DecorBatch is a contiguous run of one element kind.
struct DecorBatch {
    PatternElementKind kind = PatternElementKind::Quad;
    uint32_t firstInstance = 0;   // into the shape's decor-instance list
    uint32_t instanceCount = 0;
};
// Sink for the instanced decorator path. When set on AppendShapeImpl, periodic
// glyphs are emitted here (page-local) instead of baked; continuous/composite rails
// are still baked into the mesh.
struct DecorSink {
    std::vector<PatternInstance>* instances = nullptr;
    std::vector<DecorBatch>*      batches   = nullptr;
};

class Tessellator {
public:
    // Tessellation quality scales with the on-screen zoom (px per doc-unit), so
    // curves and circles stay smooth when zoomed in without wasting triangles
    // when zoomed out. Pass the current view zoom; 1.0 is a sane default.
    static void AppendShape(const Shape& shape, Mesh& out, float zoom = 1.0f,
                            Vec2 pageOrigin = {0, 0});
    // Procedural variant (Phase 3 + 4): bakes fill (solid) + stroke + marks + the
    // continuous/composite decorator rails into `out`, but emits each surface PATTERN
    // as a cover polygon (`cover`) + PatternRec (`recs`), and each PERIODIC line
    // decorator glyph as instances (`decorInst`) + DecorBatch (`decorBatches`),
    // instead of triangles. All page-LOCAL (pageOrigin applied by the caller on
    // append to the per-view buffers). Used by the per-shape cache.
    static void AppendShapePatterned(const Shape& shape, Mesh& out, Mesh& cover,
                                     std::vector<PatternRec>& recs,
                                     std::vector<PatternInstance>& decorInst,
                                     std::vector<DecorBatch>& decorBatches,
                                     Mesh& strokeCover,
                                     std::vector<StrokeRec>& strokeRecs,
                                     float zoom = 1.0f);
    // Fast path for a PARAM-only edit (geometry unchanged): refresh the cached recs'
    // motif params from `s` WITHOUT re-tessellating, keeping each rec's cover range
    // + lattice centre. Returns false if the rec count can't be matched 1:1 (e.g. a
    // multi-subpath patterned part) → caller falls back to a full rebuild.
    static bool RefreshPatternParams(const Shape& s, std::vector<PatternRec>& recs);
    // Fast path for a DECOR-param-only edit: regenerate the decorator instance list
    // from `s` (re-walk the arc-length stations) WITHOUT re-tessellating the ribbon.
    // The ribbon (`verts`) is reused. Always succeeds (it rebuilds the list fully).
    static void RefreshDecorInstances(const Shape& s,
                                      std::vector<PatternInstance>& decorInst,
                                      std::vector<DecorBatch>& decorBatches);
    static void BuildArtboard(const Artboard& ab, Mesh& out, float zoom = 1.0f);
    // Page white backdrop / page shapes, split so the document builder can
    // interleave them for correct inter-page z-order.
    static void BuildArtboardBackdrop(const Artboard& ab, Mesh& out);
    static void BuildArtboardShapes(const Artboard& ab, Mesh& out, float zoom = 1.0f);
    // Build the whole document (all pages) with correct inter-page z-order: an
    // object never paints over another page's white. Use this instead of looping
    // BuildArtboard per page.
    static void BuildDocument(const Document& doc, Mesh& out, float zoom = 1.0f);

    // A resolved procedural fill draw for ONE surface layer: the cover-polygon
    // vertex range (into the build's cover stream, world-shifted), the pattern
    // parameters for pattern_fill.frag, the world bbox (per-surface scissor) and a
    // per-surface stencil reference. The renderer writes the cover to the stencil,
    // then draws the cover again with pattern_fill.frag stencil-tested EQUAL ref.
    struct SurfaceDraw {
        uint32_t coverFirstVertex = 0;   // into the build's cover-vertex stream
        uint32_t coverVertexCount = 0;
        PatternParams params;
        Vec2     bbMin{0,0}, bbMax{0,0};  // world doc-units, for scissor
        uint8_t  stencilRef = 1;
    };

    // A resolved instanced decorator draw for ONE batch on a page: a unit base mesh
    // (of `kind`) stamped `instanceCount` times from the per-view decor-instance
    // stream. No cover, no stencil (decorators aren't surface-clipped).
    struct DecorDraw {
        PatternElementKind kind = PatternElementKind::Quad;
        uint32_t firstInstance = 0;   // into the build's decor-instance stream
        uint32_t instanceCount = 0;
    };

    // One document object's painter slice: its base-triangle range, then its OWN
    // fill patterns + decorators — so each object paints fully (base → patterns →
    // decor) BEFORE the next, giving true document z-order (a lower object's pattern
    // never paints over a higher object).
    // A resolved transparent-stroke draw: the ribbon coverage range + a single bbox
    // QUAD range (both into the build's cover stream, world-shifted) + colour + bbox +
    // stencil ref. The renderer writes the ribbon to the stencil (REPLACE ref), then
    // draws the bbox quad ONCE stencil-tested EQUAL ref with the stroke colour — so
    // each covered pixel blends exactly once (no alpha doubling on overlap).
    struct StrokeDraw {
        uint32_t coverFirstVertex = 0;   // ribbon coverage tris (stencil write)
        uint32_t coverVertexCount = 0;
        uint32_t quadFirstVertex  = 0;   // 6 verts = bbox quad (coloured fill pass)
        Vec2     bbMin{0,0}, bbMax{0,0}; // world doc-units, for scissor
        uint8_t  stencilRef = 1;
    };

    struct ObjDraw {
        uint32_t baseFirst = 0;   // into the base mesh (world-shifted)
        uint32_t baseCount = 0;
        std::vector<SurfaceDraw> patterns;
        std::vector<StrokeDraw>  strokes;   // transparent strokes (stencil coverage)
        std::vector<DecorDraw>   decor;
        // Object opacity [0,1] (Shape::opacity), copied here so a renderer can
        // composite the object with it. 1 = opaque. The legacy renderer ignores it.
        float    opacity = 1.0f;
        // Object blend mode (Shape::blendMode as a uint8_t). 0 = Normal; the highest
        // value (BlendMode::Erase) = knock-out. The legacy renderer ignores it; the
        // Compositor composites the object with it.
        uint8_t  blendMode = 0;
        // Owning object identity (Shape::id), so a renderer's GPU picking id-pass
        // can map a rendered pixel back to the document object. 0 = unknown.
        uint64_t shapeId = 0;
        // Owning LAYER GROUP chain (Lot 11/11-4e): the object's group hierarchy from
        // OUTERMOST (top-level on the page) to INNERMOST. Empty = the object is not in
        // any group (composited directly on its page/level). Each entry carries the
        // group id + its compositing params, so the Compositor can isolate each level
        // and composite it onto its parent with the group's blend/opacity — recursive,
        // Affinity/PS-style. The legacy renderer ignores grouping.
        struct GroupLevel { uint64_t id = 0; float opacity = 1.0f; uint8_t blend = 0; };
        std::vector<GroupLevel> groups;   // [0] = outermost … [n-1] = innermost
        // Convenience: innermost group id (0 = none). Kept for picking / quick checks.
        uint64_t groupId() const { return groups.empty() ? 0 : groups.back().id; }
    };

    // One page's slice of the segmented document: the page rect (for scissor), the
    // white backdrop range (drawn FIRST, under everything), then the objects in
    // document order. Scissoring each page is the only painter-correct way to stop
    // an object drawing over a foreign page.
    struct PageSeg {
        Vec2     min{0,0};      // page rect, doc-units
        Vec2     size{0,0};
        bool     fullScissor = false;  // true → draw unclipped (loose page-less objects)
        uint32_t backdropFirst = 0;    // page white backdrop range (no patterns)
        uint32_t backdropCount = 0;
        std::vector<ObjDraw> objects;  // document order
    };
    // Optional per-page placement override (Lot 3 per-viewport layout): where a
    // page is DISPLAYED (doc-units) and whether it shows at all. Parallel to
    // doc.artboards; pass empty to use each page's own ab.pos and show all.
    struct PagePlacement { Vec2 origin; bool visible; };

    // ── Tessellation cache (per-shape, content-hashed) ────────────────────────
    //  The dominant cost was re-tessellating EVERY object EVERY frame even on a
    //  plain pan/zoom (geometry unchanged). The cache stores each shape's baked
    //  WORLD-space triangle list keyed by a content hash (geometry + paint +
    //  transform + page origin); an unchanged shape is copied from the cache
    //  instead of re-tessellated. The camera (pan/zoom) lives in the vertex
    //  shader, so it never invalidates the cache → zoom/pan are free.
    struct CachedShape {
        uint64_t            hash = 0;        // full content hash at last build
        uint64_t            geomHash = 0;    // tessellation-only hash (cover/verts)
        std::vector<Vertex> verts;          // baked page-local triangles (no pattern)
        uint64_t            lastUsedFrame = 0;
        // Procedural fill patterns (Phase 3). The pattern layers are NOT baked into
        // `verts`; the surface's cut polygon is triangulated once into `coverVerts`
        // (page-LOCAL, shifted on append) and one PatternRec per layer holds the
        // motif parameters. The fragment shader paints the motif, clipped to the
        // contour by the stencil — so editing motif params is O(1) on the CPU.
        std::vector<Vertex>      coverVerts;   // cut-polygon triangles (cover + stencil)
        std::vector<PatternRec>  recs;
        // Instanced curve decorators (Phase 4). Periodic glyphs are NOT baked into
        // `verts`; they live here as page-local instances + per-kind batches. A
        // decor-param-only edit refreshes just these (RefreshDecorInstances).
        std::vector<PatternInstance> decorInstances;
        std::vector<DecorBatch>      decorBatches;
        // Transparent-stroke coverage (Lot A): the ribbon as page-local coverage tris
        // + one StrokeRec per stroked part. Opaque strokes stay baked into `verts`.
        std::vector<Vertex>      strokeCoverVerts;
        std::vector<StrokeRec>   strokeRecs;
        bool patternedBuild = false;   // built via the procedural path (vs legacy bake)
    };
    struct Cache {
        std::unordered_map<uint64_t, CachedShape> byId;   // (shapeId,bucket) → mesh
        uint64_t frame = 0;
        // Stats for the metrics overlay (reset each BuildDocumentSegmented call).
        int builtShapes = 0, cachedShapes = 0, culledShapes = 0, drawnShapes = 0;
        // Per-frame rebuild budget (Lot 4): a heavy frame (zoom bucket cross, paste of
        // many shapes) is spread over frames instead of stalling. A FULL rebuild costs
        // its vertex count against `rebuildVertBudget`; once exhausted, a shape that
        // already has a prior mesh draws STALE and is counted in `deferredShapes` (the
        // view then re-enters next frame to finish it). A shape with NO prior mesh is
        // always built (can't draw nothing). `minRebuilds` guarantees forward progress.
        // Budget is set by the caller each build; 0 / very large = effectively no cap.
        size_t   rebuildVertBudget = (size_t)-1;
        int      minRebuilds       = 1;
        int      builtThisBuild    = 0;   // FULL rebuilds done in the current build
        int      deferredShapes    = 0;   // shapes drawn stale because budget ran out
        void BeginFrame() { ++frame; }
        void BeginBudget(size_t vertBudget, int minBuilds) {
            rebuildVertBudget = vertBudget; minRebuilds = minBuilds;
            builtThisBuild = 0; deferredShapes = 0;
        }
        // Drop entries not touched within the grace window + a safety LRU cap.
        void Evict();
    };

    // Axis-aligned cull rect in WORLD doc-units; shapes whose bounds miss it are
    // skipped. Pass a degenerate/huge rect to disable culling.
    struct CullRect { Vec2 min{-1e30f,-1e30f}, max{1e30f,1e30f};
                      bool Hit(Vec2 mn, Vec2 mx) const {
                          return !(mx.x < min.x || mn.x > max.x ||
                                   mx.y < min.y || mn.y > max.y); } };

    // Adjust the global flattening quality (segments per doc-unit of arc). Higher
    // = smoother curves, more triangles. Default ~0.35.
    static void SetQuality(float segPerDocUnit);
    static float GetQuality();
    // On-screen detail multiplier (zoom-driven). Offscreen renders that manage
    // their own quality (glyph thumbnails / ghosts) pin this to 1.
    static void SetDetailScale(float s);
    static float GetDetailScale();
    // The detail SCALE BuildDocumentSegmented would pick for `zoom` (px per
    // doc-unit): floor(log2)-quantised to coarse 2× steps. Quantised so the cache
    // re-tessellates only on a real detail step (not every zoom frame); a same-step
    // zoom is free. Lower bound 1× (never coarser than authored); upper bound is
    // effectively unbounded (sub-pixel curves at any magnification). Pure fn of zoom.
    static float DetailScaleForZoom(float zoom);
    // The INTEGER bucket index that DetailScaleForZoom maps to (= floor(log2 zoom),
    // clamped [0, kMaxDetailBucket]). This is the per-view detail identity: it keys
    // the shape cache (so views at different zooms don't fight) and is mixed into the
    // build signature — instead of mixing the global gDetailScale, which leaked
    // per-view zoom into the shared per-shape hashes and thrashed the cache.
    static int   DetailBucketIndex(float zoom);
    static constexpr int kMaxDetailBucket = 18;   // ~262144× — far past any real zoom

    // Stable 64-bit content hash of a shape (geometry + paint + transform). Two
    // shapes with the same visual produce the same hash; any edit changes it.
    static uint64_t HashShape(const Shape& s, Vec2 pageOrigin);
    // Hash of ONLY the fields that change the baked TESSELLATION (geometry, fill
    // colour, stroke, marks, and each fill layer's pattern KIND + clip edge). It
    // deliberately EXCLUDES procedural-pattern parameters (spacing/size/angle/
    // offset/seed/colour/dash) so editing those reuses the cached cover/verts and
    // only refreshes the per-layer params — no re-flatten, no ear-clip. Used by the
    // per-shape cache to skip retessellation on a param-only edit (big-document perf).
    static uint64_t GeomHashShape(const Shape& s);

    // Build the document as ONE mesh, but with each page's [backdrop+shapes]
    // CONTIGUOUS and reported as a PageSeg, so the renderer draws each page slice
    // under a per-page scissor (objects clipped to their page). With `placements`
    // a page is drawn at its display origin (geometry offset accordingly) and
    // skipped if not visible. Returns the per-page segments (in artboard order).
    //
    // Procedural fill patterns (Phase 3) + instanced decorators (Phase 4): when
    // `outCover` is given, surface patterns emit their cut-polygon triangles into
    // `outCover` + a SurfaceDraw into PageSeg.patterns (instead of baked triangles);
    // when `outDecor` is given, periodic line decorators emit instances into
    // `outDecor` + a DecorDraw into PageSeg.decor. Pass null (default) to bake both
    // as triangles into `out` (legacy path — thumbnails/glyphs).
    static std::vector<PageSeg> BuildDocumentSegmented(
        const Document& doc, Mesh& out, float zoom = 1.0f,
        const std::vector<PagePlacement>* placements = nullptr,
        bool includeLoose = true,    // append page-less (loose) objects?
        Cache* cache = nullptr,      // reuse unchanged shapes' meshes (perf)
        const CullRect* cull = nullptr,   // skip shapes outside this world rect
        Mesh* outCover = nullptr,
        std::vector<PatternInstance>* outDecor = nullptr,
        // Shapes the CALLER renders itself (Compositor stencil-then-cover fills, Lot
        // 13-4b): skip them ENTIRELY here — no tessellation, no ObjDraw, no cache
        // build — so the ear-clip never runs for them. The caller must render every
        // skipped id (else it vanishes). null/empty = tessellate all (legacy path,
        // unchanged). The set holds Shape::id.
        const std::unordered_set<uint64_t>* skipShapeIds = nullptr);

    // Map an OBJECT-LOCAL point to WORLD doc-units through a shape's origin +
    // transform, then translate by the owning PAGE's origin:
    //   world = pageOrigin + T + R(rot)·S(scale)·(local − origin) + origin.
    // Object geometry is stored PAGE-RELATIVE (Lot 2), so moving a page (or
    // moving an object to another page) just changes `pageOrigin`. Callers that
    // don't know/need a page pass {0,0} (the default), giving the old behaviour.
    static Vec2 WorldTransform(const Shape& s, Vec2 local, Vec2 pageOrigin = {0, 0});
    // Inverse of the above (world → object-local), for hit-testing in local space.
    static Vec2 InverseTransform(const Shape& s, Vec2 world, Vec2 pageOrigin = {0, 0});

    // Flatten ONE part's outline into WORLD-space points (origin+transform of
    // the owning shape + page origin applied). `closed` reports a closed loop.
    // For a multi-subpath part this returns ONLY the first subpath (callers that
    // need every strand use OutlinePartSub over SubpathCount).
    static std::vector<Vec2> OutlinePart(const Shape& shape, const Part& part,
                                         float zoom, bool& closed,
                                         Vec2 pageOrigin = {0, 0});
    // Number of subpaths (strands) a part flattens to (1 for primitives / a plain
    // path; ≥1 for a branched path).
    static int SubpathCount(const Part& part);
    // Flatten subpath `sub` of `part` into WORLD-space points.
    static std::vector<Vec2> OutlinePartSub(const Shape& shape, const Part& part,
                                            int sub, float zoom, bool& closed,
                                            Vec2 pageOrigin = {0, 0});
    // Convenience: the outline of the shape's FIRST part.
    static std::vector<Vec2> Outline(const Shape& shape, float zoom, bool& closed,
                                     Vec2 pageOrigin = {0, 0});
    // World-space bounding box over ALL parts (min,max). false if no geometry.
    static bool WorldBounds(const Shape& shape, float zoom, Vec2& outMin, Vec2& outMax,
                            Vec2 pageOrigin = {0, 0});

    // Bézier flattening: append the cubic p0→p1 (controls c0,c1) to `out`,
    // excluding the start point (caller seeds it), with `steps` subdivisions.
    // Public so the edit-mode/curve preview can reuse it.
    static void FlattenCubic(Vec2 p0, Vec2 c0, Vec2 c1, Vec2 p1,
                             int steps, std::vector<Vec2>& out);

    // Fill a polygon (concave-safe ear-clip). Public so the surface fill-layer
    // generator (Solid screen %) can reuse it.
    static void FillPolygonEarClip(const std::vector<Vec2>& poly, const Color& c, Mesh& out);

    // World-space outline of subpath `sub` flattened AS CLOSED (the closing edge is
    // generated from the endpoints' outer handles — a curve if they exist, a
    // straight chord if not). Used to FILL an OPEN curve/area: the stroke stays
    // open, but the fill closes virtually between the endpoints.
    static std::vector<Vec2> OutlinePartSubFilled(const Shape& shape, const Part& part,
                                                  int sub, float zoom, Vec2 pageOrigin = {0,0});

private:
    // Flatten one part in OBJECT-LOCAL space (no transform applied). `sub` selects
    // a subpath (−1 = the whole single-subpath path / primitives). `forceClosed`
    // flattens an open path AS a ring (closing edge from the outer handles) — for
    // filling open curves.
    static std::vector<Vec2> OutlinePartLocal(const Part& part, float zoom,
                                              bool& closed, int sub = -1,
                                              bool forceClosed = false);

    // Geometry builders.
    static void FillConvexFan(const std::vector<Vec2>& poly, const Color& c, Mesh& out);
    static void StrokePolyline(const std::vector<Vec2>& poly, bool closed,
                               float width, const Color& c, Mesh& out);
    // Shared body of AppendShape / AppendShapePatterned. `sink` null → fill patterns
    // baked as triangles; `decorSink` null → decorators baked as triangles; else each
    // is emitted as instances/cover. A member so it can reach the private helpers.
    static void AppendShapeImpl(const Shape& shape, Mesh& out, float zoom,
                                Vec2 pageOrigin, const PatternSink* sink,
                                const DecorSink* decorSink = nullptr);
};

} // namespace Renderer

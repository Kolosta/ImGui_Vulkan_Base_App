#pragma once

#include "Ink/Document/Document.h"
#include "Ink/Geometry/Geometry.h"

#include <deque>
#include <unordered_map>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Scene — the compiled runtime scene (docs/Ink/ARCHITECTURE.md §2): the only
//  consumer of the Document and the only producer of render items. A compile
//  walks pages → layer trees in painter order and emits one Drawable per
//  enabled style piece (fills bottom-up, then strokes) of every visible path
//  node, with the world transform resolved in double.
//
//  Compositing (docs/Ink/DOCUMENT_MODEL.md §2, RENDER_GRAPH.md §4): a group is
//  a LAYER. A group that carries opacity<1, a non-Normal blend, isolate, or a
//  clip opens a COMPOSITE SCOPE — its subtree renders into its own isolation
//  target, then composites onto the parent as a unit. The compile records the
//  scope tree and tags every drawable with the scope it belongs to; the
//  Renderer plays scopes deepest-first and composites back up.
//
//  Lot 2/4 granularity: the walk is O(nodes) and runs only when the ChangeLog
//  is non-empty; finer per-change diffing layers on in the perf lots.
// ─────────────────────────────────────────────────────────────────────────────

// index into Scene::Scopes(); 0 = the page root (no isolation).
using ScopeId = std::uint32_t;
inline constexpr ScopeId kRootScope = 0;

struct CompositeScope {
    NodeId    node   = kNullNode;
    ScopeId   parent = kRootScope;
    float     opacity = 1.0f;
    BlendMode blend   = BlendMode::Normal;
    bool      isolate = false;
    // Clip: the scope's contents are masked by the clip source geometry (the
    // group's first path child, Lot 4). kNullNode = no clip.
    NodeId    clipNode = kNullNode;
    // The scope carries a stencil CLIP MASK: a group clip OR a path-parent
    // (Affinity layer, whose children clip to the path's / mask child's
    // coverage). Drives the Compile post-pass that tags scope drawables.
    bool      hasClipMask = false;
    // Opened during the preview-only pass (library content): a normal view
    // skips this scope entirely (no empty content/composite passes).
    bool      previewOnly = false;
    // Filled by the Renderer while playing the scope tree (transient).
    int       depth = 0;
};

// How a drawable interacts with the pass's stencil CLIP mask (docs/Ink/
// RENDER_GRAPH.md §ClipPass). The mask value is always 1: MaskWrite rasterises
// the mask (no colour), MaskClear erases it back to 0 (so sequential clipped
// regions in one pass never leak into each other), Clipped draws colour only
// where the mask is set. Clipping is resolved at VIEW tolerance by the normal
// mesh pipeline — vector-exact at any zoom.
// EraseWrite: the drawable rasterises OPAQUE with the dst-out pipeline, cutting
// its coverage out of the isolation layer (a subtractive mark object — the
// stroke layer shows a hole where the shape was; docs/Ink/RENDER_GRAPH.md
// §Erase). It never reads/writes the stencil.
// EraseClipped: the same cut, but stencil-tested like Clipped — an ERASING
// pattern / instanced fill still has to stop at its fill-clip edge. Stencil
// test and colour blend are independent, so this is simply both at once.
enum class ClipRole : std::uint8_t {
    None = 0, MaskWrite, MaskClear, Clipped, EraseWrite, EraseClipped };

// "This drawable is not on any plate" — it keeps its layer-tree position even
// when the document renders in print order, and the separation previews leave
// it out of every channel.
inline constexpr int kNoPlate = 0x7FFFFFFF;

struct Drawable {
    NodeId          node = kNullNode;
    // The node SELECTION maps to (picking, outlines): an instance's subtree
    // picks the instance, a pattern motif copy picks its host. Equals `node`
    // for plain content.
    NodeId          owner = kNullNode;
    DMat23          world;              // node-local → document (double)
    std::uint64_t   pathHash = 0;
    const PathData* path = nullptr;     // borrowed; valid until the next compile
    bool            isStroke = false;
    std::uint8_t    pieceIndex = 0;     // index into style.fills / style.strokes
    // The OWNER's paint piece this drawable belongs to (drives the per-piece
    // preview filter — a fill vignette renders ONE fill in isolation): plain
    // content mirrors pieceIndex/isStroke; every drawable a pattern fill
    // expands into (motif copies, their strokes, the stencil masks) carries
    // the HOST FILL's index with ownerPieceStroke = false.
    std::uint8_t    ownerPiece = 0;
    bool            ownerPieceStroke = false;
    FillRule        rule = FillRule::NonZero;   // fill pieces
    Stroke          stroke;                     // stroke pieces (geometry params)
    Color           color;              // linear straight (premultiplied later)
    // The document colour this drawable follows, when it follows one. Recorded
    // at emit time and resolved into `color` by a Compile post-pass, so every
    // paint source (fill, stroke, pattern element, line-set, repeat, mark)
    // funnels through ONE place — which is also what lets the print previews
    // and the colour-usage editor work off the drawable list alone.
    SwatchId        swatch = kNullSwatch;
    // The plate this drawable prints on, resolved from its swatch: lower is
    // laid down first (underneath). kNoPlate = takes no part in the stack and
    // keeps its position in the layer tree. Also what the separation and
    // overprint previews read.
    int             plate = kNoPlate;
    // Everything the PRINT previews need about this drawable's colour, carried
    // here so the transform can run per VIEW (in the GPU style tables) instead
    // of being baked into `color` once for the whole app. A vignette and a
    // proofing viewport must be able to disagree.
    Cmyk            plateInk;                  // the separation definition
    Color           spotColor{ 0, 0, 0, 1 };   // when the plate prints as spot
    bool            hasSpot = false;
    ScopeId         scope = kRootScope; // the composite scope this belongs to
    ClipRole        clip = ClipRole::None;  // stencil interaction (see above)
    bool            clipPinned = false;     // clip role already decided; the
                                            // Compile post-pass must NOT reroute
    bool            isClipSource = false;   // mask geometry — never picked/painted
    // Boolean-modified nodes: the ops re-run at each zoom tier's tolerance in
    // the GeometryCache (vector-exact at any zoom). `path`/`pathHash` then
    // hold the Scene's coarse evaluation (picking) / the PROGRAM hash.
    const geom::BoolProgram* boolProg = nullptr;
    // Library (Node::previewOnly) content: compiled but dropped from normal
    // views, picking and bounds — drawn only when a preview filter selects it.
    bool previewOnly = false;
};

class Scene {
public:
    // Recompile if the document changed since the last compile (or `force`).
    // Drains the document's ChangeLog. Returns true when the drawable list
    // was rebuilt (GPU tables must resync).
    bool Compile(Document& doc, bool force = false);

    const std::vector<Drawable>&      Drawables() const { return drawables_; }
    // The artwork that could NOT go to a print separation as it stands —
    // translucent, blended or cutting — one entry per PIECE in document space.
    // Pieces rather than objects because a single object routinely mixes
    // flattened and clean parts: a pattern cell, or one repeat of a stroke, may
    // need flattening while the rest of the object does not. A stroke reports
    // its SPINE plus its width, so it is marked as the band it actually paints
    // instead of as the area its path would enclose. Filled only while the
    // document is in the Flattener preview.
    struct FlattenRegion {
        // A FILL reports its outline ring (hatched by scanline). A STROKE
        // reports the TRIANGLES the stroker actually produces — dashes, caps,
        // repeats and the Inside/Outside/Left/Right offset included — because
        // its painted band is not derivable from the spine, and following the
        // spine by hand is what tears at curves.
        std::vector<DVec2>        ring;      // fill only
        std::vector<DVec2>        tris;      // stroke only, 3 points per tri
        bool                      isStroke = false;
    };
    const std::vector<FlattenRegion>& FlattenRegions() const {
        return flattenRings_;
    }
    // Flattener analysis is not free (it re-runs the stroker over every
    // translucent stroke), so it only happens while a view actually shows it.
    void SetWantFlattenRegions(bool on) {
        if (wantFlatten_ != on) { wantFlatten_ = on; flattenDirty_ = true; }
    }
    bool WantFlattenRegions() const { return wantFlatten_; }
    const std::vector<CompositeScope>& Scopes()   const { return scopes_; }
    // Deepest composite-scope nesting in the scene (drives the isolation
    // target reservation — RENDER_GRAPH.md §2).
    int MaxScopeDepth() const { return maxDepth_; }
    // Document version this scene reflects (mixed into view signatures).
    std::uint64_t Version() const { return version_; }
    // Document-space bounds (pages ∪ node anchor boxes) — drives fit-view.
    Rect Bounds() const { return bounds_; }
    // Document-space bounds of one node's rendered content (all its drawables
    // incl. modifier copies; an instance's id covers its rendered subtree).
    // Conservative: Bézier control-point hull + outward stroke extent.
    // Returns false when the node produced nothing this compile.
    bool NodeBounds(NodeId owner, DRect& out) const {
        auto it = nodeBounds_.find(owner);
        if (it == nodeBounds_.end() || !it->second.valid) return false;
        out = it->second;
        return true;
    }

private:
    // Emit `n` (and its subtree) once per instancing transform generated by
    // its modifier stack, composed onto `parentWorld`. `instDepth` guards
    // InstanceNode recursion (cycle/blow-up clamp). `owner` = the selection
    // owner to stamp on drawables (kNullNode → the node itself).
    // `forceVisible` bypasses the node's own visible flag (instance targets —
    // a linked duplicate stays visible when the original is hidden).
    void EmitNode(const Document& doc, const Node& n, const DMat23& parentWorld,
                  ScopeId scope, int instDepth, NodeId owner = kNullNode,
                  bool forceVisible = false);
    // Emit the node's OWN content (a path's style pieces, a group's children,
    // an instance's target) at exactly `world` — the leaf of the modifier
    // expansion.
    void EmitContent(const Document& doc, const Node& n, const DMat23& world,
                     ScopeId scope, int instDepth, NodeId owner = kNullNode);
    // Emit the style pieces (fills incl. pattern expansion, then strokes) of a
    // path node at `world`. `forceClip` PINS the drawables' clip role so the
    // Compile post-pass leaves them alone (used for the Affinity layer host:
    // Unclipped for a clip layer, Clipped for a mask layer). `AutoRoute` keeps
    // the default (the post-pass decides).
    enum class HostClip { AutoRoute, Unclipped, Clipped };
    void EmitPath(const Document& doc, const Node& n, const DMat23& world,
                  ScopeId scope, NodeId owner,
                  HostClip forceClip = HostClip::AutoRoute);
    // The node's effective geometry: its own PathData, or a Boolean-modifier
    // derived path (stored stably in derivedPaths_). Returns the path + its
    // content hash. `n` must be a path node.
    const PathData* ResolveGeometry(const Document& doc, const Node& n,
                                    std::uint64_t& hashOut);
    // Expand a pattern fill: motif instances on a lattice, cut by the host's
    // stencil clip mask (`geo`/`geoHash`/`geoProg` = the host's resolved
    // geometry, its render hash and its boolean program when modified).
    void EmitPattern(const Document& doc, const Fill& fill, const Node& host,
                     const PathData* geo, std::uint64_t geoHash,
                     const geom::BoolProgram* geoProg,
                     const DMat23& world, ScopeId scope, NodeId owner,
                     std::size_t fillIndex);
    // Expand an INSTANCED fill: procedurally generated primitive shapes and/or
    // families of parallel lines on a grid or scatter layout, each cut by the
    // host's stencil clip mask (same clip machinery as EmitPattern). Fusion
    // elements of one colour paint their union ONCE (translucent fields never
    // double-darken); Blend stack; Subtract erase.
    void EmitInstancedFill(const Document& doc, const Fill& fill, const Node& host,
                           const PathData* geo, std::uint64_t geoHash,
                           const geom::BoolProgram* geoProg,
                           const DMat23& world, ScopeId scope, NodeId owner,
                           std::size_t fillIndex);
    // A group that composites as a unit opens a scope; returns its id (or the
    // parent scope when the group is a plain pass-through layer).
    ScopeId OpenScopeIfNeeded(const Document& doc, const Node& group,
                              ScopeId parent, int depth);
    // A style piece that composites on its own (Fill::blend != Normal).
    ScopeId OpenPieceScope(NodeId node, ScopeId parent, BlendMode blend);
    // Turn every drawable a style piece just produced into a CUT (see .cpp).
    void MakePieceErase(std::size_t begin);
    // A stroke carrying mark OBJECTS renders in its OWN isolation scope so the
    // subtractive objects (dst-out) cut it cleanly before it composites into
    // the parent. Emits the base stroke + every mark object (SVG-marker shapes
    // as generated geometry, or an instance of a node) at the mark points along
    // the stroke's flattened subpaths.
    void EmitStrokeMarks(const Document& doc, const Node& n, const Stroke& s,
                         std::size_t strokeIndex, const PathData* geo,
                         const DMat23& world, ScopeId scope, NodeId owner,
                         int instDepth);
    void GrowBounds(DVec2 p);
    // Stable storage for the primitive mark-object shapes (circle/rect/diamond)
    // the Scene generates — drawables borrow these until the next compile.
    std::deque<PathData> markShapes_;

    // Instanced-fill placement CACHE — the generated poses (grid/scatter, in
    // anchor space, with jitter + element choice baked in) keyed by a hash of
    // everything that affects them. SURVIVES recompiles so a pure move/pan/zoom
    // (or an unrelated edit) never re-scatters tens of thousands of instances.
    // One pose = an instance's anchor-space transform + which element it stamps.
    struct InstPose { DVec2 pos; double rot; std::int32_t elem; };
    std::vector<std::pair<std::uint64_t, std::vector<InstPose>>> instPoseCache_;

    // Preview-only (library) subtrees: the main walk records them and returns;
    // a second pass compiles them with pvPass_ set, then tags their drawables/
    // scopes previewOnly and reverts their bounds contribution — so the library
    // renders in vignettes but never on canvas, in picking or in fit-view.
    bool pvPass_ = false;
    std::vector<std::pair<NodeId, DMat23>> pvPending_;

    std::vector<Drawable>       drawables_;
    std::vector<FlattenRegion>  flattenRings_;
    bool                        wantFlatten_ = false;
    bool                        flattenDirty_ = false;
    std::vector<CompositeScope> scopes_;
    std::vector<PathData>       pageRects_;   // stable storage for page substrates
    // Boolean-modifier results (stable addresses; drawables borrow them):
    // one COARSE evaluation for picking/bounds + the per-tier PROGRAM the
    // render path re-evaluates.
    std::deque<PathData>        derivedPaths_;
    std::unordered_map<NodeId, PathData*> derivedByNode_;
    std::deque<geom::BoolProgram>                boolPrograms_;
    std::unordered_map<NodeId, geom::BoolProgram*> progByNode_;
    // Per-owner rendered bounds (selection outlines, box select, fit-selection).
    std::unordered_map<NodeId, DRect> nodeBounds_;
    int           maxDepth_ = 0;
    std::uint64_t version_  = 0;
    bool          compiled_ = false;
    Rect          bounds_{};
    bool          boundsValid_ = false;
};

} // namespace Ink

#pragma once

#include "Ink/Document/Document.h"

#include <cstdint>
#include <vector>

// The Compositing Graph (docs/Ink/NODE_GRAPH.md) — a per-layer dataflow model
// that decides what a layer renders. NOT the frame render graph (Ink::graph::
// RenderGraph, RENDER_GRAPH.md) — that one schedules GPU passes per frame;
// this one is Document-level, evaluated once per Scene::Compile.
//
// ROADMAP Lot 12 scope: pure auto-generation. Every CompGraph is rebuilt from
// scratch each compile, from the Layers tree's child order + each node's
// compositing fields. It is transient and Scene-owned, like CompositeScope
// (Scene.h) — there is nothing to persist until a later lot introduces
// manual pinning (NODE_GRAPH.md §0/§2): only the PINNED overrides will need a
// small persisted table in Document; the auto-generated portion stays fully
// derivable and is never written.
//
// Node kinds (revised after product-owner review — each node now represents
// exactly ONE operation the real engine performs, not a bundle):
//   Input  — imports one child's (or, for the mask/clip source, the
//            designated masking child's) resolved content.
//   Merge  — ALWAYS present for a layer with ordinary children: combines N
//            ordered inputs into one output, painter's order, no
//            compositing math of its own (docs/Ink/RENDER_GRAPH.md's
//            existing painter-order content walk, re-expressed as a node).
//   Clip   — masks Merge's combined result by a clip SOURCE (a group-clip's
//            first child, or a plain Affinity clip host's own fill).
//   Mask   — masks Merge's combined result by a dedicated Affinity MASK
//            child (DOCUMENT_MODEL.md §2.1) — a DISTINCT node from Clip:
//            same shape (content + mask source → result) but a different
//            real operation, so it gets its own kind rather than sharing
//            "Merge" (which used to wrongly do double duty as "combine
//            children" AND "clip" AND "mask" AND "blend" all at once).
//   Blend  — applies opacity/blend-mode/isolation. Independent of
//            Clip/Mask: a layer can be clipped AND blended, clipped only,
//            blended only, or neither (in which case neither node exists).
//   Output — the layer's sink, exactly one In port (never multi-input —
//            confirmed by the product owner; Merge is the only node whose
//            arity varies with the child count).
//
// Port WIRING (which node's output feeds which node's input) is deliberately
// NOT represented as PERSISTED data yet — the auto-generator's topology is
// recomputed fresh every call (see BuildAutoGraph) and is what the Node
// Graph Editor currently displays/edits by construction, not by reading a
// stored edge list. Real per-edge storage (needed for cross-layer piece
// routing and true bidirectional link-dragging) is ROADMAP's next Compositing
// Graph lot — see docs/Ink/NODE_GRAPH.md §7.

namespace Ink {

enum class CompNodeKind : std::uint8_t {
    Input = 0, Merge = 1, Clip = 2, Mask = 3, Blend = 4, Output = 5
};

// What an Input node reads. Exactly one of node/fill/stroke is non-null —
// kept as a small tagged struct (not a variant) so it stays a plain value
// through the Document's typed-op / ChangeLog plumbing once a later lot lets
// a user retarget one by hand.
struct CompInputTarget {
    NodeId   node   = kNullNode;
    FillId   fill   = kNullFill;
    StrokeId stroke = kNullStroke;

    bool IsNull() const {
        return node == kNullNode && fill == kNullFill && stroke == kNullStroke;
    }
};

enum class CompPortType : std::uint8_t {
    RenderOutput = 0,  // any object/fill/stroke/layer's resolved visual result
    LayerOutput  = 1,  // strictly a Layer's Output (reserved for a future
                       // cross-layer blend node — NODE_GRAPH.md §3)
};

struct CompPort {
    CompPortType type   = CompPortType::RenderOutput;
    // Set by a manual edit (a later lot). The auto-generator never touches a
    // pinned port — always false today, since nothing can pin one yet.
    bool         pinned = false;
};

// One node in a layer's Compositing Graph. `in`/`out` are populated by the
// factory that creates the node (see CompGraph.cpp) so both the auto-
// generator and the generic node-graph UI can iterate ports WITHOUT a
// per-kind switch (NODE_GRAPH.md §5 — the UI must stay node-kind-agnostic).
struct CompNode {
    // A small index, unique WITHIN this one CompGraph (not drawn from
    // Document's global id pool — nothing outside this transient, per-
    // compile graph references it yet).
    std::uint64_t id   = 0;
    CompNodeKind  kind = CompNodeKind::Input;
    // Input: the content source. Clip/Mask: the mask/clip SOURCE (target.node
    // may be kNullNode for a plain Affinity clip host, whose OWN fill is the
    // mask — there is no separate source node in that case).
    CompInputTarget target;
    // True for the Input that represents a Clip/Mask node's mask SOURCE
    // (excluded from Merge's ordinary children, wired to Clip/Mask's Mask
    // port instead) — lets a consumer distinguish it from an ordinary
    // merged child without re-deriving the predicate.
    bool isMaskSourceInput = false;
    // True for the Input that represents THIS layer's OWN resolved paint
    // stack (target.node == the layer's own id — a self-reference, never a
    // child) — a Path/Instance node's content is a real Merge input exactly
    // like a child's, just not one anyone can retarget (it always means
    // "this node itself"). Never set for a Group (which has no paint of its
    // own — only its children do). Without this, a plain leaf shape had NO
    // Input at all ("Output connected to nothing") and Blend, with nothing
    // real to sit between, fell back to a sentinel that looked like an edge
    // LEAVING Output — see BuildAutoGraph and NODE_GRAPH.md §3/§7.
    bool isObjectInput = false;

    // Blend-only params (meaningful only for kind == Blend).
    float     opacity = 1.0f;
    BlendMode blend   = BlendMode::Normal;
    bool      isolate = false;

    // Node Graph "Mute" (M): Input mirrors CompInputOverride::muted; Blend
    // mirrors Node::compBlendMuted (Merge/Clip/Mask are never muted — see
    // the file header comment on what each kind means to mute).
    bool      muted   = false;

    std::vector<CompPort> in;
    std::vector<CompPort> out;
};

// One per Layer. `nodes[output]` is always the one CompNodeKind::Output
// (exactly one per graph, single In port, never multi-input).
struct CompGraph {
    NodeId                 layer  = kNullNode;
    std::vector<CompNode>  nodes;
    std::size_t            output = 0;
    // True when `layer.compInputs` is non-empty (Document.h): the Input list
    // is a hand-authored reorder/filter of the layer's own children, not a
    // live mirror of them. Drives the Outliner's "customized" badge /
    // "Reset to automatic" action.
    bool                    customized = false;

    const CompNode* Find(CompNodeKind k) const {
        for (const CompNode& n : nodes) if (n.kind == k) return &n;
        return nullptr;
    }
    // Convenience aliases (kept — TestCompGraphAutoGenerate and the editor
    // both read these a lot).
    const CompNode* FindMerge() const { return Find(CompNodeKind::Merge); }
    const CompNode* FindBlend() const { return Find(CompNodeKind::Blend); }
    // Whichever of Clip/Mask applies (mutually exclusive per layer — see
    // ComputeAutoMergeParams::isMaskChild).
    const CompNode* FindClipOrMask() const {
        if (const CompNode* c = Find(CompNodeKind::Clip)) return c;
        return Find(CompNodeKind::Mask);
    }
};

// The compositing predicate, split into its two INDEPENDENT halves so a
// caller can recombine them under a mute override without re-deriving the
// logic (docs/Ink/RENDER_GRAPH.md §CompositePass / Scene.cpp is the ground
// truth this mirrors):
//   blendTrigger — opacity < 1, non-Normal blend, or isolate: the reasons a
//                  BLEND node would exist. Muting Blend (Node::
//                  compBlendMuted) bypasses this half ONLY.
//   otherTrigger — clip, an Affinity path-parent, an enabled Subtract
//                  AlongPath modifier, or an enabled Erase-blend piece: the
//                  reasons Clip/Mask (or, for the last two, the underlying
//                  isolation Scene still needs) apply — NEVER affected by
//                  Blend's mute.
// `composites` is their union, kept for callers that just want "does this
// layer need ANY isolation scope" (Scene::OpenScopeIfNeeded folds
// `compBlendMuted` in on top of `blendTrigger` itself, see Scene.cpp).
struct CompAutoMergeParams {
    bool      composites    = false;
    bool      blendTrigger  = false;
    bool      otherTrigger  = false;
    float     opacity       = 1.0f;
    BlendMode blend         = BlendMode::Normal;
    bool      isolate       = false;
    NodeId    clipNode      = kNullNode;   // kNullNode for a plain Affinity self-clip host
    bool      hasClipMask   = false;
    // True when `clipNode` is a dedicated Affinity MASK child (→ a Mask
    // node); false when it's a group-clip's first child or a plain Affinity
    // clip host's own fill (→ a Clip node).
    bool      isMaskChild   = false;
};
CompAutoMergeParams ComputeAutoMergeParams(const Document& doc, const Node& layer);

// Builds `layer`'s full CompGraph purely from the Document's current Layers-
// tree order + compositing fields (docs/Ink/NODE_GRAPH.md §0/§3): Input
// nodes for its ordinary children (targeting each child's NodeId directly —
// never another layer's Output, which is why evaluating layers never needs
// a topological sort, NODE_GRAPH.md §1) feed a Merge (always present for a
// Group with children), then optionally Clip/Mask (via a separate Input for
// the mask source), then optionally Blend, then Output. Not on the hot
// compile path (see ComputeAutoMergeParams, which Scene::OpenScopeIfNeeded
// calls directly) — for the Node Graph Editor and for tests that want to
// assert the graph's shape.
CompGraph BuildAutoGraph(const Document& doc, const Node& layer);

} // namespace Ink

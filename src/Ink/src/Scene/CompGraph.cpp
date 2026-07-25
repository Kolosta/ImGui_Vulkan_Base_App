#include "Ink/Scene/CompGraph.h"

namespace Ink {

CompAutoMergeParams ComputeAutoMergeParams(const Document& doc, const Node& layer) {
    // The exact predicate Scene::OpenScopeIfNeeded used before Lot 12
    // (docs/Ink/RENDER_GRAPH.md §CompositePass / Scene.cpp), split into its
    // two independent halves — see the struct's doc comment.
    NodeId clip = kNullNode;
    bool isMaskChild = false;
    if (layer.clip) {
        // Group-clip source = the layer's first PATH child (Lot 4 rule).
        for (NodeId c : layer.children) {
            if (const Node* ch = doc.Find(c))
                if (ch->kind == NodeKind::Path) { clip = c; break; }
        }
    }
    // A PATH with children clips them to its OWN fill (Affinity layer rule),
    // UNLESS one of those children is flagged `isMask`, in which case IT is
    // the mask source instead (DOCUMENT_MODEL.md §2.1's two Affinity forms).
    const bool pathParent =
        layer.kind == NodeKind::Path && !layer.children.empty();
    if (pathParent) {
        for (NodeId c : layer.children) {
            if (const Node* ch = doc.Find(c); ch && ch->isMask &&
                ch->kind == NodeKind::Path && !ch->path.Empty()) {
                clip = c;
                isMaskChild = true;
                break;
            }
        }
    }
    // A SUBTRACT along-path modifier erases within the node's own layer.
    bool cutModifier = false;
    for (const Modifier& mm : layer.modifiers)
        if (mm.enabled && mm.kind == ModifierKind::AlongPath &&
            mm.alongMode == MarkObjectMode::Subtract)
            cutModifier = true;
    // An ERASE piece makes the node isolate (see Scene.cpp for the full
    // rationale — the cut must stop at this object's own paint stack).
    bool blendPiece = false;
    for (const Fill& f : layer.style.fills)
        if (f.enabled && f.blend == BlendMode::Erase) { blendPiece = true; break; }
    if (!blendPiece)
        for (const Stroke& st : layer.style.strokes)
            if (st.enabled && st.blend == BlendMode::Erase) { blendPiece = true; break; }

    CompAutoMergeParams p;
    p.blendTrigger = layer.opacity < 0.999f || layer.blend != BlendMode::Normal ||
                    layer.isolate;
    p.otherTrigger = clip != kNullNode || pathParent || cutModifier || blendPiece;
    p.composites   = p.blendTrigger || p.otherTrigger;
    p.opacity     = layer.opacity;
    p.blend       = layer.blend;
    p.isolate     = layer.isolate;
    p.clipNode    = clip;
    p.hasClipMask = (clip != kNullNode) || pathParent;
    p.isMaskChild = isMaskChild;
    return p;
}

namespace {

CompNode MakeNode(std::uint64_t localId, CompNodeKind kind, int mergeInputCount = 0) {
    CompNode n;
    n.id   = localId;
    n.kind = kind;
    switch (kind) {
    case CompNodeKind::Input:
        n.out.push_back({ CompPortType::RenderOutput, false });
        break;
    case CompNodeKind::Merge:
        // One In port PER CURRENT ORDINARY CHILD (not one shared port): each
        // incoming cable lands at its own row, in order — the elongated
        // multi-input behaviour the product owner asked for, achieved with
        // the SAME per-direction row-stacking the generic node-graph widget
        // already does for any node with several same-direction ports.
        for (int i = 0; i < mergeInputCount; ++i)
            n.in.push_back({ CompPortType::RenderOutput, false });
        n.out.push_back({ CompPortType::RenderOutput, false });
        break;
    case CompNodeKind::Clip:
    case CompNodeKind::Mask:
        n.in.push_back({ CompPortType::RenderOutput, false });   // Content
        n.in.push_back({ CompPortType::RenderOutput, false });   // Mask source
        n.out.push_back({ CompPortType::RenderOutput, false });
        break;
    case CompNodeKind::Blend:
        n.in.push_back({ CompPortType::RenderOutput, false });
        n.out.push_back({ CompPortType::RenderOutput, false });
        break;
    case CompNodeKind::Output:
        n.in.push_back({ CompPortType::RenderOutput, false });
        break;
    }
    return n;
}

} // namespace

CompGraph BuildAutoGraph(const Document& doc, const Node& layer) {
    CompGraph g;
    g.layer = layer.id;
    std::uint64_t next = 1;

    const CompAutoMergeParams p = ComputeAutoMergeParams(doc, layer);
    // kNullNode for a plain Affinity self-clip host (nothing to exclude —
    // its own fill is the mask, not a separate child).
    const NodeId maskSource = p.hasClipMask ? p.clipNode : kNullNode;

    g.customized = !layer.compInputs.empty();

    int mergeCount = 0;

    // A Path/Instance layer's OWN resolved paint stack is a real source too
    // — a self-targeting "Object" Input (NODE_GRAPH.md §3/§7), always the
    // BOTTOM Merge input: Scene::EmitNode paints a Path-parent's own content
    // via EmitPath BEFORE recursing into its children (both the Mask-layer
    // and the Clip-layer branches), so painter order puts it first. A Group
    // has no paint of its own — only its children do — so it never gets one.
    if (layer.kind != NodeKind::Group) {
        CompNode obj = MakeNode(next++, CompNodeKind::Input);
        obj.target.node = layer.id;
        obj.isObjectInput = true;
        g.nodes.push_back(std::move(obj));
        ++mergeCount;
    }

    // Ordinary children (excluding the mask/clip source, which Clip/Mask
    // consumes separately below — mirrors Scene::EmitContent's own exclusion
    // of that child from normal painting).
    if (g.customized) {
        for (const CompInputOverride& ov : layer.compInputs) {
            if (ov.node == maskSource) continue;
            CompNode in = MakeNode(next++, CompNodeKind::Input);
            in.target = { ov.node, ov.fill, ov.stroke };
            in.muted  = ov.muted;
            g.nodes.push_back(std::move(in));
            ++mergeCount;
        }
    } else {
        for (NodeId childId : layer.children) {
            if (childId == maskSource) continue;
            CompNode in = MakeNode(next++, CompNodeKind::Input);
            in.target.node = childId;
            g.nodes.push_back(std::move(in));
            ++mergeCount;
        }
    }

    // Merge — ALWAYS present for a Group layer (combining however many
    // ordinary children into one output, in order, IS what a Group is).
    // For a Path/Instance, only when there is MORE than just its own Object
    // input (an Affinity path-parent with real children beyond itself) —
    // now that every Path/Instance always carries at least the Object input,
    // `mergeCount > 0` would force a pointless 1-input Merge on every plain
    // shape; a lone Object input feeds straight through to whatever's next
    // instead. Never muted/blended/clipped on its own — those are
    // Clip/Mask/Blend's job (see the header comment on what changed).
    if (layer.kind == NodeKind::Group || mergeCount > 1)
        g.nodes.push_back(MakeNode(next++, CompNodeKind::Merge, mergeCount));

    // Clip or Mask (mutually exclusive per layer — see ComputeAutoMergeParams
    // for why a Group-clip and an Affinity Path-parent never coincide).
    if (p.hasClipMask) {
        if (maskSource != kNullNode) {
            CompNode maskIn = MakeNode(next++, CompNodeKind::Input);
            maskIn.target.node = maskSource;
            maskIn.isMaskSourceInput = true;
            g.nodes.push_back(std::move(maskIn));
        }
        CompNode cm = MakeNode(next++, p.isMaskChild ? CompNodeKind::Mask : CompNodeKind::Clip);
        cm.target.node = maskSource;   // convenience mirror of the Input above (kNullNode for a self-clip host)
        g.nodes.push_back(std::move(cm));
    }

    // Blend — opacity/blend-mode/isolation, independent of Clip/Mask.
    if (p.blendTrigger) {
        CompNode blend = MakeNode(next++, CompNodeKind::Blend);
        blend.opacity = p.opacity;
        blend.blend   = p.blend;
        blend.isolate = p.isolate;
        blend.muted   = layer.compBlendMuted;
        g.nodes.push_back(std::move(blend));
    }

    g.output = g.nodes.size();
    g.nodes.push_back(MakeNode(next++, CompNodeKind::Output));

    return g;
}

} // namespace Ink

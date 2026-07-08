#include "Ink/Scene/Scene.h"

#include <algorithm>

namespace Ink {

void Scene::GrowBounds(DVec2 p) {
    const Vec2 f{ (float)p.x, (float)p.y };
    if (!boundsValid_) {
        bounds_.min = bounds_.max = f;
        boundsValid_ = true;
        return;
    }
    bounds_.min.x = std::min(bounds_.min.x, f.x);
    bounds_.min.y = std::min(bounds_.min.y, f.y);
    bounds_.max.x = std::max(bounds_.max.x, f.x);
    bounds_.max.y = std::max(bounds_.max.y, f.y);
}

// A group composites as a unit (opens an isolation scope) when it carries
// opacity < 1, a non-Normal blend, isolate, or a clip. Otherwise it is a
// plain pass-through layer (organisation + transform only) and its children
// stay in the parent scope.
ScopeId Scene::OpenScopeIfNeeded(const Document& doc, const Node& group,
                                 ScopeId parent, int depth) {
    NodeId clip = kNullNode;
    if (group.clip) {
        // Clip source = the group's first PATH child (Lot 4 rule).
        for (NodeId c : group.children) {
            if (const Node* ch = doc.Find(c))
                if (ch->kind == NodeKind::Path) { clip = c; break; }
        }
    }
    const bool composites = group.opacity < 0.999f ||
                            group.blend != BlendMode::Normal ||
                            group.isolate || clip != kNullNode;
    if (!composites) return parent;

    CompositeScope s;
    s.node    = group.id;
    s.parent  = parent;
    s.opacity = group.opacity;
    s.blend   = group.blend;
    s.isolate = group.isolate;
    s.clipNode = clip;
    s.depth   = depth;
    scopes_.push_back(s);
    if (depth > maxDepth_) maxDepth_ = depth;
    return (ScopeId)(scopes_.size() - 1);
}

void Scene::EmitNode(const Document& doc, const Node& n,
                     const DMat23& parentWorld, ScopeId scope) {
    if (!n.visible) return;
    const DMat23 world = parentWorld.Compose(n.transform.Matrix());

    if (n.kind == NodeKind::Group) {
        const ScopeId childScope =
            OpenScopeIfNeeded(doc, n, scope, scopes_[scope].depth + 1);
        // The clip source path defines the scope's mask (SVG clip-path
        // semantics): emitted FIRST as a stencil-only drawable, and NOT
        // painted (skipped in the child walk below). Its fills/strokes, if
        // any, are ignored — a clip source is a mask, not content.
        const NodeId clipNode = scopes_[childScope].clipNode;
        if (childScope != scope && clipNode != kNullNode) {
            if (const Node* clip = doc.Find(clipNode); clip && !clip->path.Empty()) {
                const DMat23 cw = world.Compose(clip->transform.Matrix());
                Drawable d;
                d.node = clip->id;  d.world = cw;
                d.pathHash = clip->path.Hash();  d.path = &clip->path;
                d.rule = clip->style.fills.empty() ? FillRule::NonZero
                         : clip->style.fills.front().rule;
                d.scope = childScope;
                d.isClipSource = true;
                drawables_.push_back(std::move(d));
            }
        }
        for (NodeId c : n.children) {
            if (c == clipNode) continue;   // the mask never paints
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, world, childScope);
        }
        return;
    }
    if (n.path.Empty()) return;

    const std::uint64_t pathHash = n.path.Hash();

    // Anchor-box bounds (cheap conservative fit-view input).
    for (const Subpath& sp : n.path.subpaths)
        for (const Anchor& a : sp.anchors)
            GrowBounds(world.Apply(a.pos));

    // Fills bottom-up, then strokes bottom-up — the unified paint order
    // (docs/Ink/DOCUMENT_MODEL.md §4).
    for (std::size_t i = 0; i < n.style.fills.size(); ++i) {
        const Fill& f = n.style.fills[i];
        if (!f.enabled) continue;
        Drawable d;
        d.node = n.id;  d.world = world;
        d.pathHash = pathHash;  d.path = &n.path;
        d.isStroke = false;  d.pieceIndex = (std::uint8_t)i;
        d.rule = f.rule;  d.color = f.paint.color;
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < n.style.strokes.size(); ++i) {
        const Stroke& s = n.style.strokes[i];
        if (!s.enabled || s.width <= 0.0) continue;
        Drawable d;
        d.node = n.id;  d.world = world;
        d.pathHash = pathHash;  d.path = &n.path;
        d.isStroke = true;  d.pieceIndex = (std::uint8_t)i;
        d.stroke = s;  d.color = s.paint.color;
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
}

bool Scene::Compile(Document& doc, bool force) {
    if (!force && compiled_ && !doc.HasPendingChanges() &&
        version_ == doc.Version())
        return false;
    doc.DrainChanges();   // Lot 2: exact per-change diffing arrives with the
                          // perf lots; the walk is O(nodes) and change-gated.

    drawables_.clear();
    pageRects_.clear();
    scopes_.clear();
    maxDepth_ = 0;
    boundsValid_ = false;
    bounds_ = {};
    pageRects_.reserve(doc.Pages().size());   // stable addresses for borrows

    // Scope 0 is the page root (no isolation) — always present.
    scopes_.push_back(CompositeScope{});

    for (const Page& page : doc.Pages()) {
        // Page substrate: a display backdrop, not a layer
        // (docs/Ink/DOCUMENT_MODEL.md §8) — drawn first within the page.
        if (page.size.x > 0.0 && page.size.y > 0.0) {
            pageRects_.push_back(PathData::Rect(0, 0, page.size.x, page.size.y));
            Drawable d;
            d.node = page.id;
            d.world = DMat23::Translation(page.pos.x, page.pos.y);
            d.path = &pageRects_.back();
            d.pathHash = pageRects_.back().Hash();
            d.rule = FillRule::NonZero;
            d.color = page.background;
            drawables_.push_back(std::move(d));
            GrowBounds({ page.pos.x, page.pos.y });
            GrowBounds({ page.pos.x + page.size.x, page.pos.y + page.size.y });
        }
        const DMat23 pageWorld = DMat23::Translation(page.pos.x, page.pos.y);
        for (NodeId c : page.children)
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, pageWorld, kRootScope);
    }

    version_  = doc.Version();
    compiled_ = true;
    return true;
}

} // namespace Ink

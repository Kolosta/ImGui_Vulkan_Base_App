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

void Scene::EmitNode(const Document& doc, const Node& n,
                     const DMat23& parentWorld) {
    if (!n.visible) return;
    const DMat23 world = parentWorld.Compose(n.transform.Matrix());

    if (n.kind == NodeKind::Group) {
        for (NodeId c : n.children)
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, world);
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
    boundsValid_ = false;
    bounds_ = {};
    pageRects_.reserve(doc.Pages().size());   // stable addresses for borrows

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
                EmitNode(doc, *child, pageWorld);
    }

    version_  = doc.Version();
    compiled_ = true;
    return true;
}

} // namespace Ink

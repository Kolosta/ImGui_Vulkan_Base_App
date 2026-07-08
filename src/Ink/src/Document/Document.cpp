#include "Ink/Document/Document.h"

#include <algorithm>
#include <cmath>

namespace Ink {

// ── Ops ──────────────────────────────────────────────────────────────────────

NodeId Document::AddPage(std::string name, DVec2 pos, DVec2 size) {
    Page p;
    p.id   = NextId();
    p.name = std::move(name);
    p.pos  = pos;
    p.size = size;
    pages_.push_back(std::move(p));
    Log(pages_.back().id, ChangeKind::Added);
    return pages_.back().id;
}

NodeId Document::AddGroup(NodeId parent, std::string name) {
    Node n;
    n.id   = NextId();
    n.kind = NodeKind::Group;
    n.name = std::move(name);
    if (Node* pg = FindMutable(parent)) {          // parent is a group
        if (pg->kind != NodeKind::Group) return kNullNode;
        n.parent = parent;
        n.page   = pg->page;
        pg->children.push_back(n.id);
    } else if (Page* pp = const_cast<Page*>(FindPage(parent))) {   // page root
        n.page = parent;
        pp->children.push_back(n.id);
    } else {
        return kNullNode;
    }
    const NodeId id = n.id;
    nodes_.emplace(id, std::move(n));
    Log(id, ChangeKind::Added);
    return id;
}

NodeId Document::AddPath(NodeId parent, PathData path, Style style,
                         std::string name) {
    Node n;
    n.id    = NextId();
    n.kind  = NodeKind::Path;
    n.name  = std::move(name);
    n.path  = std::move(path);
    n.style = std::move(style);
    if (Node* pg = FindMutable(parent)) {
        if (pg->kind != NodeKind::Group) return kNullNode;
        n.parent = parent;
        n.page   = pg->page;
        pg->children.push_back(n.id);
    } else if (Page* pp = const_cast<Page*>(FindPage(parent))) {
        n.page = parent;
        pp->children.push_back(n.id);
    } else {
        return kNullNode;
    }
    const NodeId id = n.id;
    nodes_.emplace(id, std::move(n));
    Log(id, ChangeKind::Added);
    return id;
}

NodeId Document::AddInstance(NodeId parent, NodeId target, std::string name) {
    Node n;
    n.id        = NextId();
    n.kind      = NodeKind::Instance;
    n.name      = std::move(name);
    n.targetRef = target;
    if (Node* pg = FindMutable(parent)) {
        if (pg->kind != NodeKind::Group) return kNullNode;
        n.parent = parent;
        n.page   = pg->page;
        pg->children.push_back(n.id);
    } else if (Page* pp = const_cast<Page*>(FindPage(parent))) {
        n.page = parent;
        pp->children.push_back(n.id);
    } else {
        return kNullNode;
    }
    const NodeId id = n.id;
    nodes_.emplace(id, std::move(n));
    Log(id, ChangeKind::Added);
    return id;
}

void Document::SetModifiers(NodeId node, std::vector<Modifier> modifiers) {
    if (Node* n = FindMutable(node)) {
        n->modifiers = std::move(modifiers);
        Log(node, ChangeKind::Geometry);   // changes the emitted drawable set
    }
}

void Document::SetPath(NodeId node, PathData path) {
    if (Node* n = FindMutable(node); n && n->kind == NodeKind::Path) {
        n->path = std::move(path);
        Log(node, ChangeKind::Geometry);
    }
}

void Document::SetStyle(NodeId node, Style style) {
    if (Node* n = FindMutable(node); n && n->kind == NodeKind::Path) {
        // A stroke-geometry parameter change (width/align/caps/joins) needs a
        // re-tessellation, a paint-only change does not — compare the
        // geometry-affecting hashes to log the cheapest exact change.
        auto strokesGeomHash = [](const Style& s) {
            std::uint64_t h = 0x0DA5ULL;   // small fixed seed
            for (const Stroke& st : s.strokes) {
                h = HashBytes(&st.enabled, sizeof st.enabled, h);
                h ^= st.GeometryHash();
                h *= 1099511628211ull;
            }
            std::uint64_t fills = s.fills.size();
            for (const Fill& f : s.fills) {
                h = HashBytes(&f.enabled, sizeof f.enabled, h);
                h = HashBytes(&f.rule, sizeof f.rule, h);
            }
            return HashBytes(&fills, sizeof fills, h);
        };
        const bool geomChanged = strokesGeomHash(n->style) != strokesGeomHash(style);
        n->style = std::move(style);
        Log(node, geomChanged ? ChangeKind::Geometry : ChangeKind::StyleChanged);
    }
}

void Document::SetTransform(NodeId node, const Transform2D& t) {
    if (Node* n = FindMutable(node)) {
        n->transform = t;
        Log(node, ChangeKind::Moved);
    }
}

void Document::SetVisible(NodeId node, bool visible) {
    if (Node* n = FindMutable(node); n && n->visible != visible) {
        n->visible = visible;
        Log(node, ChangeKind::Hierarchy);
    }
}

void Document::SetOpacity(NodeId group, float opacity) {
    if (Node* n = FindMutable(group)) {
        n->opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
        Log(group, ChangeKind::Hierarchy);   // may open/close a composite scope
    }
}

void Document::SetBlend(NodeId group, BlendMode blend) {
    if (Node* n = FindMutable(group)) { n->blend = blend; Log(group, ChangeKind::Hierarchy); }
}

void Document::SetIsolate(NodeId group, bool isolate) {
    if (Node* n = FindMutable(group)) { n->isolate = isolate; Log(group, ChangeKind::Hierarchy); }
}

void Document::SetClip(NodeId group, bool clip) {
    if (Node* n = FindMutable(group)) { n->clip = clip; Log(group, ChangeKind::Hierarchy); }
}

void Document::DetachFromParent(const Node& n) {
    std::vector<NodeId>* siblings = nullptr;
    if (n.parent != kNullNode) {
        if (Node* pg = FindMutable(n.parent)) siblings = &pg->children;
    } else if (Page* pp = const_cast<Page*>(FindPage(n.page))) {
        siblings = &pp->children;
    }
    if (siblings)
        siblings->erase(std::remove(siblings->begin(), siblings->end(), n.id),
                        siblings->end());
}

void Document::RemoveSubtree(NodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return;
    // Copy the child list — erasing nodes invalidates the parent reference.
    const std::vector<NodeId> children = it->second.children;
    for (NodeId c : children) RemoveSubtree(c);
    nodes_.erase(id);
    Log(id, ChangeKind::Removed);
}

void Document::Remove(NodeId id) {
    if (const Node* n = Find(id)) {
        DetachFromParent(*n);
        RemoveSubtree(id);
        return;
    }
    // A page: remove it and every node it owns.
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        if (it->id != id) continue;
        const std::vector<NodeId> children = it->children;
        for (NodeId c : children) RemoveSubtree(c);
        pages_.erase(it);
        Log(id, ChangeKind::Removed);
        return;
    }
}

void Document::Clear() {
    nodes_.clear();
    pages_.clear();
    changes_.clear();
    Log(kNullNode, ChangeKind::Removed);   // one "everything changed" marker
}

// ── Queries ──────────────────────────────────────────────────────────────────

const Node* Document::Find(NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

Node* Document::FindMutable(NodeId id) {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const Page* Document::FindPage(NodeId id) const {
    for (const Page& p : pages_)
        if (p.id == id) return &p;
    return nullptr;
}

DMat23 Document::WorldTransform(NodeId id) const {
    return WorldTransformDepth(id, 0);
}

DMat23 Document::WorldTransformDepth(NodeId id, int depth) const {
    const Node* n = Find(id);
    if (!n) return {};
    const DMat23 local = n->transform.Matrix();

    // OBJECT PARENTING takes precedence (docs/Ink/DOCUMENT_MODEL.md §2): a
    // parented node inherits its parentId's world, NOT its layer-tree group.
    // Cycles are refused at edit time, but the depth clamp is a belt-and-braces
    // guard against a corrupted document.
    if (n->parentId != kNullNode && depth < 64) {
        if (Find(n->parentId))
            return WorldTransformDepth(n->parentId, depth + 1).Compose(local);
    }

    // Unparented: the layer-tree chain carries the transform (a group moves
    // its members). pageOrigin ∘ ancestorChain ∘ local.
    DMat23 world = local;
    NodeId parent = n->parent;
    while (parent != kNullNode) {
        const Node* p = Find(parent);
        if (!p) break;
        world  = p->transform.Matrix().Compose(world);
        parent = p->parent;
    }
    if (const Page* page = FindPage(n->page))
        world = DMat23::Translation(page->pos.x, page->pos.y).Compose(world);
    return world;
}

namespace {
// True if `ancestor` is `node` or reachable from it up the parentId chain.
bool IsParentAncestor(const Document& doc, NodeId node, NodeId ancestor) {
    int guard = 0;
    while (node != kNullNode && guard++ < 256) {
        if (node == ancestor) return true;
        const Node* n = doc.Find(node);
        if (!n) break;
        node = n->parentId;
    }
    return false;
}
// Invert an affine 2×3 (assumes non-degenerate; identity fallback).
DMat23 Invert(const DMat23& m) {
    const double det = m.m[0] * m.m[4] - m.m[1] * m.m[3];
    DMat23 r;
    if (std::abs(det) < 1e-18) return r;   // identity
    const double inv = 1.0 / det;
    r.m[0] =  m.m[4] * inv;
    r.m[1] = -m.m[1] * inv;
    r.m[3] = -m.m[3] * inv;
    r.m[4] =  m.m[0] * inv;
    r.m[2] = -(r.m[0] * m.m[2] + r.m[1] * m.m[5]);
    r.m[5] = -(r.m[3] * m.m[2] + r.m[4] * m.m[5]);
    return r;
}
} // namespace

bool Document::SetParent(NodeId child, NodeId parent, bool keepWorld) {
    Node* c = FindMutable(child);
    if (!c || child == parent) return false;
    if (parent != kNullNode) {
        if (!Find(parent)) return false;
        // Refuse a cycle: `child` must not be an ancestor of `parent`.
        if (IsParentAncestor(*this, parent, child)) return false;
    }
    if (keepWorld) {
        // Preserve the on-screen position: new local = parentWorld⁻¹ ∘ oldWorld.
        const DMat23 oldWorld = WorldTransform(child);
        const DMat23 parentWorld =
            parent != kNullNode ? WorldTransform(parent)
                                : (FindPage(c->page)
                                       ? DMat23::Translation(FindPage(c->page)->pos.x,
                                                             FindPage(c->page)->pos.y)
                                       : DMat23{});
        const DMat23 newLocal = Invert(parentWorld).Compose(oldWorld);
        c->transform = Transform2D::FromMatrix(newLocal);
    }
    c->parentId = parent;
    Log(child, ChangeKind::Moved);
    return true;
}

void Document::ClearParent(NodeId child, bool keepWorld) {
    SetParent(child, kNullNode, keepWorld);
}

// ── Change tracking ──────────────────────────────────────────────────────────

void Document::Log(NodeId id, ChangeKind kind) {
    changes_.push_back({ id, kind });
    ++version_;
}

std::vector<Change> Document::DrainChanges() {
    std::vector<Change> out;
    out.swap(changes_);
    return out;
}

// ── PathData builders ────────────────────────────────────────────────────────

PathData PathData::Rect(double x, double y, double w, double h) {
    return Polygon({ { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } });
}

PathData PathData::Polygon(const std::vector<DVec2>& points, bool closed) {
    PathData p;
    Subpath sp;
    sp.closed = closed;
    for (const DVec2& pt : points) {
        Anchor a;
        a.pos = pt;
        sp.anchors.push_back(a);
    }
    p.subpaths.push_back(std::move(sp));
    return p;
}

PathData PathData::Ellipse(double cx, double cy, double rx, double ry) {
    // Four cubic arcs with the standard circle kappa.
    constexpr double k = 0.5522847498307936;
    PathData p;
    Subpath sp;
    sp.closed = true;
    auto anchor = [&](double px, double py, double ix, double iy,
                      double ox, double oy) {
        Anchor a;
        a.pos = { px, py };
        a.in  = { ix, iy };  a.hasIn  = true;
        a.out = { ox, oy }; a.hasOut = true;
        a.kind = AnchorKind::Smooth;
        sp.anchors.push_back(a);
    };
    anchor(cx + rx, cy,      0,  ry * k,  0, -ry * k);
    anchor(cx,      cy - ry, rx * k,  0, -rx * k,  0);
    anchor(cx - rx, cy,      0, -ry * k,  0,  ry * k);
    anchor(cx,      cy + ry, -rx * k, 0,  rx * k,  0);
    p.subpaths.push_back(std::move(sp));
    return p;
}

} // namespace Ink

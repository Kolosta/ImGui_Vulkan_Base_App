#include "Ink/Document/Document.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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

void Document::SetInstanceTransformCopy(NodeId inst, bool loc, bool rot,
                                        bool scale) {
    Node* n = FindMutable(inst);
    if (!n || n->kind != NodeKind::Instance) return;
    if (n->instCopyLoc == loc && n->instCopyRot == rot &&
        n->instCopyScale == scale)
        return;
    n->instCopyLoc   = loc;
    n->instCopyRot   = rot;
    n->instCopyScale = scale;
    Log(inst, ChangeKind::Moved);
}

void Document::SetInstanceTarget(NodeId inst, NodeId target) {
    Node* n = FindMutable(inst);
    if (!n || n->kind != NodeKind::Instance || target == inst) return;
    n->targetRef = target;
    Log(inst, ChangeKind::Geometry);       // changes the emitted drawable set
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

void Document::SetMask(NodeId node, bool isMask) {
    if (Node* n = FindMutable(node)) { n->isMask = isMask; Log(node, ChangeKind::Hierarchy); }
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
    collections_.clear();
    changes_.clear();
    Log(kNullNode, ChangeKind::Removed);   // one "everything changed" marker
}

// ── Persistence (Lot 10) ─────────────────────────────────────────────────────

bool Document::Restore(std::vector<Page> pages, std::vector<Node> nodes,
                       std::vector<Collection> collections, NodeId nextId) {
    // ── Validate STRUCTURE first (nothing installed on failure) ──────────────
    // Id uniqueness across pages + nodes + collections (one allocator pool).
    std::unordered_set<NodeId> ids;
    NodeId maxId = 0;
    auto claim = [&](NodeId id) {
        if (id == kNullNode || !ids.insert(id).second) return false;
        maxId = std::max(maxId, id);
        return true;
    };
    for (const Page& p : pages)
        if (!claim(p.id)) return false;
    std::unordered_set<NodeId> nodeIds;
    for (const Node& n : nodes) {
        if (!claim(n.id)) return false;
        nodeIds.insert(n.id);
    }
    for (const Collection& c : collections)
        if (!claim(c.id)) return false;

    // Bidirectional layer-tree consistency: every child list entry exists and
    // points back; every node is reachable through exactly its declared owner.
    std::unordered_map<NodeId, const Node*> nodeIdx;
    nodeIdx.reserve(nodes.size());
    for (const Node& n : nodes) nodeIdx.emplace(n.id, &n);
    std::unordered_map<NodeId, const Page*> pageIdx;
    pageIdx.reserve(pages.size());
    for (const Page& p : pages) pageIdx.emplace(p.id, &p);
    auto nodeAt = [&](NodeId id) -> const Node* {
        auto it = nodeIdx.find(id);
        return it == nodeIdx.end() ? nullptr : it->second;
    };
    auto pageAt = [&](NodeId id) -> const Page* {
        auto it = pageIdx.find(id);
        return it == pageIdx.end() ? nullptr : it->second;
    };
    auto listed = [](const std::vector<NodeId>& v, NodeId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    for (const Page& p : pages)
        for (NodeId c : p.children) {
            const Node* n = nodeAt(c);
            if (!n || n->parent != kNullNode || n->page != p.id) return false;
        }
    for (const Node& n : nodes) {
        for (NodeId c : n.children) {
            const Node* ch = nodeAt(c);
            if (!ch || ch->parent != n.id || ch->page != n.page) return false;
        }
        if (n.parent != kNullNode) {
            const Node* pg = nodeAt(n.parent);
            if (!pg || !listed(pg->children, n.id)) return false;
        } else {
            const Page* pp = pageAt(n.page);
            if (!pp || !listed(pp->children, n.id)) return false;
        }
    }

    // ── Sanitise NON-structural references (missing target → null/dropped) ──
    for (Node& n : nodes) {
        if (n.parentId != kNullNode &&
            (n.parentId == n.id || !nodeIds.count(n.parentId)))
            n.parentId = kNullNode;
        if (n.targetRef != kNullNode &&
            (n.targetRef == n.id || !nodeIds.count(n.targetRef)))
            n.targetRef = kNullNode;
        for (Modifier& m : n.modifiers) {
            if (m.motifRef != kNullNode && !nodeIds.count(m.motifRef))
                m.motifRef = kNullNode;
            if (m.operandRef != kNullNode && !nodeIds.count(m.operandRef))
                m.operandRef = kNullNode;
        }
    }
    std::unordered_set<NodeId> collIds;
    for (const Collection& c : collections) collIds.insert(c.id);
    for (Collection& c : collections) {
        c.members.erase(std::remove_if(c.members.begin(), c.members.end(),
                            [&](NodeId id) { return !nodeIds.count(id); }),
                        c.members.end());
        c.childCollections.erase(
            std::remove_if(c.childCollections.begin(), c.childCollections.end(),
                           [&](NodeId id) {
                               return id == c.id || !collIds.count(id);
                           }),
            c.childCollections.end());
    }

    // ── Install ──────────────────────────────────────────────────────────────
    nodes_.clear();
    for (Node& n : nodes) {
        const NodeId id = n.id;
        nodes_.emplace(id, std::move(n));
    }
    pages_       = std::move(pages);
    collections_ = std::move(collections);
    nextId_      = std::max(nextId, maxId + 1);
    changes_.clear();
    Log(kNullNode, ChangeKind::Removed);   // "everything changed" marker
    for (const Page& p : pages_) Log(p.id, ChangeKind::Added);
    return true;
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

// ── Organisation ops (Lot 9) ──────────────────────────────────────────────────

std::vector<NodeId>* Document::SiblingsOf(const Node& n) {
    if (n.parent != kNullNode) {
        if (Node* pg = FindMutable(n.parent)) return &pg->children;
        return nullptr;
    }
    if (Page* pp = const_cast<Page*>(FindPage(n.page))) return &pp->children;
    return nullptr;
}
const std::vector<NodeId>* Document::SiblingsOf(const Node& n) const {
    if (n.parent != kNullNode) {
        if (const Node* pg = Find(n.parent)) return &pg->children;
        return nullptr;
    }
    if (const Page* pp = FindPage(n.page)) return &pp->children;
    return nullptr;
}

void Document::SetName(NodeId node, std::string name) {
    if (Node* n = FindMutable(node)) {
        n->name = std::move(name);
        Log(node, ChangeKind::StyleChanged);   // metadata only (no re-tess)
    }
}

void Document::SetLocked(NodeId node, bool locked) {
    if (Node* n = FindMutable(node)) { n->locked = locked; Log(node, ChangeKind::StyleChanged); }
}

void Document::SetPropLocks(NodeId node, std::uint32_t locks) {
    if (Node* n = FindMutable(node)) {
        if (n->propLocks == locks) return;
        n->propLocks = locks;
        Log(node, ChangeKind::StyleChanged);   // metadata only (no re-tess)
    }
}

void Document::SetPreviewOnly(NodeId node, bool previewOnly) {
    if (Node* n = FindMutable(node)) {
        if (n->previewOnly == previewOnly) return;
        n->previewOnly = previewOnly;
        Log(node, ChangeKind::Hierarchy);      // changes what normal views draw
    }
}

void Document::ReorderChild(NodeId node, int to) {
    Node* n = FindMutable(node);
    if (!n) return;
    std::vector<NodeId>* sib = SiblingsOf(*n);
    if (!sib) return;
    auto it = std::find(sib->begin(), sib->end(), node);
    if (it == sib->end()) return;
    sib->erase(it);
    to = std::clamp(to, 0, (int)sib->size());
    sib->insert(sib->begin() + to, node);
    Log(node, ChangeKind::Hierarchy);
}

namespace {
// True if `maybeDescendant` is `root` or inside its layer-tree subtree.
bool InSubtree(const Document& doc, NodeId root, NodeId maybeDescendant) {
    if (root == maybeDescendant) return true;
    const Node* r = doc.Find(root);
    if (!r) return false;
    for (NodeId c : r->children)
        if (InSubtree(doc, c, maybeDescendant)) return true;
    return false;
}
} // namespace

bool Document::MoveTo(NodeId node, NodeId newParent, int index) {
    Node* n = FindMutable(node);
    if (!n || node == newParent) return false;

    // Resolve the destination sibling list + page. ANY node nests children
    // (Affinity layer semantics: a path's children are clipped inside it, a
    // group's compose through it) — only self/descendant cycles are refused.
    std::vector<NodeId>* dst = nullptr;
    NodeId dstParent = kNullNode, dstPage = kNullNode;
    if (Node* pg = FindMutable(newParent)) {
        if (InSubtree(*this, node, newParent)) return false; // no self/descendant
        dst = &pg->children; dstParent = newParent; dstPage = pg->page;
    } else if (Page* pp = const_cast<Page*>(FindPage(newParent))) {
        dst = &pp->children; dstParent = kNullNode; dstPage = newParent;
    } else {
        return false;
    }

    // Preserve world position across the reparent.
    const DMat23 world = WorldTransform(node);

    // Detach from the old siblings.
    if (std::vector<NodeId>* src = SiblingsOf(*n))
        src->erase(std::remove(src->begin(), src->end(), node), src->end());

    n->parent = dstParent;
    // Re-page the moved subtree (page id propagates to descendants).
    std::vector<NodeId> stack{ node };
    while (!stack.empty()) {
        NodeId id = stack.back(); stack.pop_back();
        if (Node* m = FindMutable(id)) {
            m->page = dstPage;
            for (NodeId c : m->children) stack.push_back(c);
        }
    }

    const int at = index < 0 ? (int)dst->size()
                             : std::clamp(index, 0, (int)dst->size());
    dst->insert(dst->begin() + at, node);

    // New local = newParentWorld⁻¹ ∘ world (keeps the on-screen placement).
    const DMat23 pw = dstParent != kNullNode
        ? WorldTransform(dstParent)
        : (FindPage(dstPage) ? DMat23::Translation(FindPage(dstPage)->pos.x,
                                                   FindPage(dstPage)->pos.y)
                             : DMat23{});
    n->transform = Transform2D::FromMatrix(Invert(pw).Compose(world));
    Log(node, ChangeKind::Hierarchy);
    return true;
}

NodeId Document::GroupNodes(const std::vector<NodeId>& nodes, std::string name) {
    if (nodes.empty()) return kNullNode;
    // All members must share one parent (page or group) — group in place.
    const Node* first = Find(nodes.front());
    if (!first) return kNullNode;
    const NodeId parent = first->parent;
    const NodeId page   = first->page;
    for (NodeId id : nodes) {
        const Node* n = Find(id);
        if (!n || n->parent != parent || n->page != page) return kNullNode;
    }
    // Topmost member's index becomes the group's slot.
    const NodeId parentId = parent != kNullNode ? parent : page;
    int topIndex = 0;
    { const Node* n = Find(nodes.front());
      std::vector<NodeId>* sib = SiblingsOf(*const_cast<Node*>(n));
      if (sib) {
          topIndex = (int)sib->size();
          for (NodeId id : nodes) {
              auto it = std::find(sib->begin(), sib->end(), id);
              if (it != sib->end()) topIndex = std::min(topIndex, (int)(it - sib->begin()));
          }
      }
    }
    const NodeId group = AddGroup(parentId, std::move(name));
    if (group == kNullNode) return kNullNode;
    // AddGroup appended it; move it to the top member's slot, then reparent.
    ReorderChild(group, topIndex);
    for (NodeId id : nodes) MoveTo(id, group, -1);
    Log(group, ChangeKind::Hierarchy);
    return group;
}

std::vector<NodeId> Document::UngroupNode(NodeId group) {
    Node* g = FindMutable(group);
    if (!g || g->kind != NodeKind::Group) return {};
    const std::vector<NodeId> children = g->children;   // copy (moves mutate it)
    // Insert the children where the group sits, in order.
    const int at = IndexInParent(group);
    const NodeId dstParent = g->parent != kNullNode ? g->parent : g->page;
    int insertAt = at < 0 ? -1 : at;
    for (NodeId c : children) {
        MoveTo(c, dstParent, insertAt);
        if (insertAt >= 0) ++insertAt;
    }
    Remove(group);
    return children;
}

// ── Collections (Lot 9) ────────────────────────────────────────────────────────

const Collection* Document::FindCollection(NodeId id) const {
    for (const Collection& c : collections_)
        if (c.id == id) return &c;
    return nullptr;
}

NodeId Document::AddCollection(std::string name, NodeId parent) {
    Collection c;
    c.id = NextId();
    c.name = std::move(name);
    const NodeId id = c.id;
    collections_.push_back(std::move(c));
    if (parent != kNullNode)
        for (Collection& p : collections_)
            if (p.id == parent) { p.childCollections.push_back(id); break; }
    Log(id, ChangeKind::Added);
    return id;
}

bool Document::IsChildCollection(NodeId id) const {
    for (const Collection& c : collections_)
        if (std::find(c.childCollections.begin(), c.childCollections.end(), id)
            != c.childCollections.end())
            return true;
    return false;
}

void Document::MoveCollection(NodeId coll, NodeId parent) {
    if (coll == parent || !FindCollection(coll)) return;
    if (parent != kNullNode) {
        if (!FindCollection(parent)) return;
        // Refuse a cycle: `coll` must not be an ancestor of `parent`.
        NodeId walk = parent;
        int guard = 0;
        while (walk != kNullNode && guard++ < 256) {
            if (walk == coll) return;
            NodeId up = kNullNode;
            for (const Collection& c : collections_)
                if (std::find(c.childCollections.begin(), c.childCollections.end(),
                              walk) != c.childCollections.end()) { up = c.id; break; }
            walk = up;
        }
    }
    for (Collection& c : collections_)
        c.childCollections.erase(std::remove(c.childCollections.begin(),
            c.childCollections.end(), coll), c.childCollections.end());
    if (parent != kNullNode)
        for (Collection& p : collections_)
            if (p.id == parent) { p.childCollections.push_back(coll); break; }
    Log(coll, ChangeKind::Hierarchy);
}

void Document::ReorderCollection(NodeId coll, int to) {
    if (!FindCollection(coll)) return;
    // Nested: reorder within the parent's childCollections vector.
    for (Collection& p : collections_) {
        auto& kids = p.childCollections;
        auto it = std::find(kids.begin(), kids.end(), coll);
        if (it == kids.end()) continue;
        kids.erase(it);
        to = std::clamp(to, 0, (int)kids.size());
        kids.insert(kids.begin() + to, coll);
        Log(coll, ChangeKind::Hierarchy);
        return;
    }
    // Top-level: reorder among the top-level entries of collections_ by moving
    // the element itself (the vector's relative order IS the top-level order).
    int idx = -1;
    std::vector<int> tops;   // indices of top-level collections in collections_
    for (int i = 0; i < (int)collections_.size(); ++i) {
        if (IsChildCollection(collections_[i].id)) continue;
        if (collections_[i].id == coll) idx = (int)tops.size();
        tops.push_back(i);
    }
    if (idx < 0) return;
    to = std::clamp(to, 0, (int)tops.size() - 1);
    if (to == idx) return;
    Collection moved = std::move(collections_[tops[idx]]);
    collections_.erase(collections_.begin() + tops[idx]);
    // Insert BEFORE the element that must follow `moved` in the final order:
    //   to < idx  → before the element at original top slot `to` (unshifted);
    //   to > idx  → before original top slot `to+1`, shifted −1 by the erase
    //               (or append when `to` is the last slot).
    int destPos;
    if (to < idx)                       destPos = tops[to];
    else if (to + 1 < (int)tops.size()) destPos = tops[to + 1] - 1;
    else                                destPos = (int)collections_.size();
    destPos = std::clamp(destPos, 0, (int)collections_.size());
    collections_.insert(collections_.begin() + destPos, std::move(moved));
    Log(coll, ChangeKind::Hierarchy);
}

void Document::RemoveCollection(NodeId coll, bool deleteContents) {
    const Collection* c = FindCollection(coll);
    if (!c) return;
    // Copies — the erase below invalidates `c`.
    const std::vector<NodeId> members  = c->members;
    const std::vector<NodeId> children = c->childCollections;
    const bool wasHiding = !c->visible && !members.empty();

    if (deleteContents) {
        for (NodeId child : children) RemoveCollection(child, true);
        for (NodeId m : members) Remove(m);
    }
    // Detach from any parent collection.
    for (Collection& p : collections_)
        p.childCollections.erase(std::remove(p.childCollections.begin(),
            p.childCollections.end(), coll), p.childCollections.end());
    for (auto it = collections_.begin(); it != collections_.end(); ++it)
        if (it->id == coll) {
            collections_.erase(it);
            // Removing a hiding collection may reveal members → recompile.
            Log(coll, wasHiding ? ChangeKind::Hierarchy : ChangeKind::Removed);
            return;
        }
}

void Document::SetCollectionColor(NodeId coll, const Color& colorTag) {
    for (Collection& c : collections_)
        if (c.id == coll) { c.colorTag = colorTag; Log(coll, ChangeKind::StyleChanged); return; }
}

void Document::SetCollectionName(NodeId coll, std::string name) {
    for (Collection& c : collections_)
        if (c.id == coll) { c.name = std::move(name); Log(coll, ChangeKind::StyleChanged); return; }
}

void Document::SetCollectionVisible(NodeId coll, bool visible) {
    for (Collection& c : collections_)
        if (c.id == coll && c.visible != visible) {
            c.visible = visible;
            Log(coll, ChangeKind::Hierarchy);   // filter changed → recompile
            return;
        }
}

void Document::AddToCollection(NodeId coll, NodeId node) {
    for (Collection& c : collections_)
        if (c.id == coll) {
            if (std::find(c.members.begin(), c.members.end(), node) == c.members.end()) {
                c.members.push_back(node);
                Log(coll, c.visible ? ChangeKind::StyleChanged : ChangeKind::Hierarchy);
            }
            return;
        }
}

void Document::RemoveFromCollection(NodeId coll, NodeId node) {
    for (Collection& c : collections_)
        if (c.id == coll) {
            auto it = std::find(c.members.begin(), c.members.end(), node);
            if (it != c.members.end()) {
                c.members.erase(it);
                Log(coll, c.visible ? ChangeKind::StyleChanged : ChangeKind::Hierarchy);
            }
            return;
        }
}

bool Document::HiddenByCollection(NodeId node) const {
    for (const Collection& c : collections_)
        if (!c.visible &&
            std::find(c.members.begin(), c.members.end(), node) != c.members.end())
            return true;
    return false;
}

// ── Editing / undo support (Lot 8) ───────────────────────────────────────────

int Document::IndexInParent(NodeId id) const {
    const Node* n = Find(id);
    if (!n) return -1;
    const std::vector<NodeId>* siblings = nullptr;
    if (n->parent != kNullNode) {
        if (const Node* pg = Find(n->parent)) siblings = &pg->children;
    } else if (const Page* pp = FindPage(n->page)) {
        siblings = &pp->children;
    }
    if (!siblings) return -1;
    for (std::size_t i = 0; i < siblings->size(); ++i)
        if ((*siblings)[i] == id) return (int)i;
    return -1;
}

Document::SubtreeSnapshot Document::CopySubtree(NodeId root) const {
    SubtreeSnapshot snap;
    snap.indexInParent = IndexInParent(root);
    // Pre-order copy (parents before children so a restore can re-link).
    std::vector<NodeId> stack{ root };
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node* n = Find(id);
        if (!n) continue;
        snap.nodes.push_back(*n);
        // Reverse push keeps the children's relative order in the snapshot.
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            stack.push_back(*it);
    }
    return snap;
}

bool Document::RestoreSubtree(const SubtreeSnapshot& snap) {
    if (snap.nodes.empty()) return false;
    const Node& root = snap.nodes.front();
    if (Find(root.id)) return false;   // already present
    // The insertion point must still exist.
    std::vector<NodeId>* siblings = nullptr;
    if (root.parent != kNullNode) {
        Node* pg = FindMutable(root.parent);
        if (!pg || pg->kind != NodeKind::Group) return false;
        siblings = &pg->children;
    } else if (Page* pp = const_cast<Page*>(FindPage(root.page))) {
        siblings = &pp->children;
    } else {
        return false;
    }
    for (const Node& n : snap.nodes) {
        nodes_.emplace(n.id, n);
        if (n.id >= nextId_) nextId_ = n.id + 1;   // ids stay never-reused
        Log(n.id, ChangeKind::Added);
    }
    const int idx = snap.indexInParent < 0 ? (int)siblings->size()
                  : std::min(snap.indexInParent, (int)siblings->size());
    siblings->insert(siblings->begin() + idx, root.id);
    return true;
}

NodeId Document::DuplicateSubtree(NodeId src) {
    const Node* srcNode = Find(src);
    if (!srcNode) return kNullNode;

    // Collect the subtree (pre-order) and assign fresh ids.
    std::vector<const Node*> order;
    std::vector<NodeId> stack{ src };
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node* n = Find(id);
        if (!n) continue;
        order.push_back(n);
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            stack.push_back(*it);
    }
    std::unordered_map<NodeId, NodeId> remap;
    for (const Node* n : order) remap[n->id] = NextId();
    auto mapped = [&](NodeId id) {
        auto it = remap.find(id);
        return it == remap.end() ? id : it->second;   // external refs kept
    };

    for (const Node* n : order) {
        Node c = *n;
        c.id     = remap[n->id];
        c.parent = n->id == src ? n->parent : mapped(n->parent);
        c.parentId  = mapped(c.parentId);
        c.targetRef = mapped(c.targetRef);
        for (NodeId& ch : c.children) ch = mapped(ch);
        for (Modifier& m : c.modifiers) {
            m.motifRef   = mapped(m.motifRef);
            m.operandRef = mapped(m.operandRef);
        }
        for (Fill& f : c.style.fills)
            f.pattern.motifRef = mapped(f.pattern.motifRef);
        const NodeId newId = c.id;
        nodes_.emplace(newId, std::move(c));
        Log(newId, ChangeKind::Added);
    }

    // Insert the copy right after the source among its siblings.
    const NodeId copyRoot = remap[src];
    std::vector<NodeId>* siblings = nullptr;
    if (srcNode->parent != kNullNode) {
        if (Node* pg = FindMutable(srcNode->parent)) siblings = &pg->children;
    } else if (Page* pp = const_cast<Page*>(FindPage(srcNode->page))) {
        siblings = &pp->children;
    }
    if (siblings) {
        auto it = std::find(siblings->begin(), siblings->end(), src);
        siblings->insert(it == siblings->end() ? siblings->end() : it + 1,
                         copyRoot);
    }
    // The copy joins the SAME collections as the source (so it lands next to
    // it in the Collections view too, not orphaned at the project root).
    for (Collection& col : collections_)
        if (std::find(col.members.begin(), col.members.end(), src)
                != col.members.end())
            col.members.push_back(copyRoot);
    return copyRoot;
}

void Document::ApplyScale(NodeId node) {
    Node* n = FindMutable(node);
    if (!n || n->kind != NodeKind::Path) return;
    const double sx = n->transform.sx, sy = n->transform.sy;
    if (sx == 1.0 && sy == 1.0) return;

    for (Subpath& sp : n->path.subpaths)
        for (Anchor& a : sp.anchors) {
            a.pos.x *= sx;  a.pos.y *= sy;
            a.in.x  *= sx;  a.in.y  *= sy;
            a.out.x *= sx;  a.out.y *= sy;
        }
    // Uniform-equivalent factor for the scalar style lengths.
    const double s = std::sqrt(std::abs(sx * sy));
    for (Stroke& st : n->style.strokes) {
        if (st.widthSpace == WidthSpace::Document) st.width *= s;
        for (double& d : st.dashPattern) d *= s;
        st.dashOffset *= s;
    }
    for (Fill& f : n->style.fills) {
        if (f.kind == FillKind::Pattern) {
            f.pattern.spacingX *= std::abs(sx);
            f.pattern.spacingY *= std::abs(sy);
            f.pattern.phaseX   *= std::abs(sx);
            f.pattern.phaseY   *= std::abs(sy);
            f.pattern.scale    *= s;
        } else if (f.kind == FillKind::Instanced) {
            // The layout carries no X/Y-aligned pitch (its axes are angled), so
            // scale every length by the uniform-equivalent factor.
            InstancedFill& in = f.instanced;
            for (double& sp : in.spacing) sp *= s;
            in.scatterMinDist *= s;
            in.scatterMaxDist *= s;
            in.posJitter *= s;
            for (InstElement& e : in.elements) {
                e.sizeA *= s; e.sizeB *= s; e.sizeC *= s;
            }
            for (InstLineSet& l : in.lines) {
                l.spacing *= s;  l.phase *= s;
                l.line.width *= s;
                for (double& d : l.line.dashPattern) d *= s;
                l.line.dashOffset *= s;
            }
        }
    }
    n->transform.sx = n->transform.sy = 1.0;
    Log(node, ChangeKind::Geometry);       // re-tessellate (+ covers style)
    Log(node, ChangeKind::Moved);          // transform changed too
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

PathData PathData::NurbsCircle(double cx, double cy, double r) {
    // The classic EXACT rational circle: an 8-point square hull, order 3
    // (rational quadratic), full-multiplicity (Bézier) periodic knots, √2/2
    // weights on the corners — four exact quarter arcs.
    constexpr double w = 0.7071067811865476;
    PathData p;
    Subpath sp;
    sp.closed = true;
    sp.spline = SplineType::Nurbs;
    sp.orderU = 3;
    sp.nurbsBezier = true;
    auto cp = [&](double px, double py, double weight) {
        Anchor a;
        a.pos = { px, py };
        a.weight = weight;
        sp.anchors.push_back(a);
    };
    cp(cx + r, cy,     1.0);   // E
    cp(cx + r, cy - r, w);     // NE corner
    cp(cx,     cy - r, 1.0);   // N
    cp(cx - r, cy - r, w);     // NW
    cp(cx - r, cy,     1.0);   // W
    cp(cx - r, cy + r, w);     // SW
    cp(cx,     cy + r, 1.0);   // S
    cp(cx + r, cy + r, w);     // SE
    p.subpaths.push_back(std::move(sp));
    return p;
}

PathData PathData::Nurbs(const std::vector<DVec2>& points, bool closed) {
    PathData p;
    Subpath sp;
    sp.closed = closed;
    sp.spline = SplineType::Nurbs;
    sp.orderU = 4;
    for (const DVec2& pt : points) {
        Anchor a;
        a.pos = pt;
        sp.anchors.push_back(a);
    }
    p.subpaths.push_back(std::move(sp));
    return p;
}

} // namespace Ink

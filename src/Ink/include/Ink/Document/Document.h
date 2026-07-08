#pragma once

#include "Ink/Document/Modifier.h"
#include "Ink/Document/PathData.h"
#include "Ink/Document/Style.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Document — the persistent model (docs/Ink/DOCUMENT_MODEL.md). Pure data +
//  invariants: no Vulkan, no ImGui, no file I/O (serialisation lives app-side,
//  Lot 10). Owned by App::Project; the Scene is its only render-side consumer.
//
//  Structure (Lot 2 scope):
//    Document → pages: [Page { children }] → layer tree of Nodes
//    Node = Group { children } | Path { PathData + Style }
//  InstanceNode/ImageNode/TextNode, Collections and modifiers arrive with
//  their lots (5/6/7/9) on top of this same storage.
//
//  Every mutation goes through the typed operations below, which (1) apply
//  the change, (2) append a Change to the log consumed by Scene::Compile for
//  exact dirtying, (3) bump the version. Undo records hook in here at Lot 8.
// ─────────────────────────────────────────────────────────────────────────────

// Instance: renders another node's subtree with this node's transform (Lot 5).
enum class NodeKind : std::uint8_t { Group = 0, Path = 1, Instance = 2 };

struct Node {
    NodeId      id   = kNullNode;
    NodeKind    kind = NodeKind::Path;
    std::string name;
    NodeId      parent = kNullNode;   // owning group (kNullNode = page root)
    NodeId      page   = kNullNode;   // owning page
    // Object PARENTING (docs/Ink/DOCUMENT_MODEL.md §2), a relation DISTINCT
    // from the layer-tree position above: the resolved transform inherits the
    // parentId chain, not the group nesting. kNullNode = unparented. Editing
    // the parent moves the child; z-order/compositing are unaffected.
    NodeId      parentId = kNullNode;
    Transform2D transform;
    bool        visible = true;
    bool        locked  = false;
    float       opacity = 1.0f;                    // compositing (Lot 4)
    BlendMode   blend   = BlendMode::Normal;       // compositing (Lot 4)
    bool        isolate = false;                   // compositing (Lot 4)
    // kind == Group: clip the subtree by the group's first path child
    // (docs/Ink/RENDER_GRAPH.md §ClipPass). Ignored on a path node.
    bool        clip    = false;

    // kind == Path
    PathData path;
    Style    style;
    // kind == Group — children in painter order (bottom → top)
    std::vector<NodeId> children;
    // kind == Instance — the node/subtree this instance renders (Lot 5).
    NodeId   targetRef = kNullNode;

    // Instancing modifiers (Lot 5): an ordered stack evaluated at Scene
    // compile. Each turns this node's rendered content into many copies at
    // generated transforms (docs/Ink/DOCUMENT_MODEL.md §6).
    std::vector<Modifier> modifiers;
};

struct Page {
    NodeId      id = kNullNode;
    std::string name;
    DVec2       pos{ 0, 0 };
    DVec2       size{ 0, 0 };
    Color       background{ 1, 1, 1, 1 };   // display substrate, NOT a layer
    std::vector<NodeId> children;           // painter order (bottom → top)
};

// What changed, at the granularity the Scene needs for exact dirtying.
enum class ChangeKind : std::uint8_t {
    Added,        // node/page created
    Removed,      // node/page destroyed
    Geometry,     // PathData edited            → re-tessellate + re-batch
    StyleChanged, // paints/opacity/etc edited  → paint/item tables only
    Moved,        // transform edited           → instance records only
    Hierarchy,    // reparent/reorder/visibility→ recompile order
};

struct Change {
    NodeId     node;
    ChangeKind kind;
};

class Document {
public:
    // ── Typed operations (the ONLY mutation path) ────────────────────────────
    NodeId AddPage(std::string name, DVec2 pos, DVec2 size);
    NodeId AddGroup(NodeId parent, std::string name);   // parent = group or page
    NodeId AddPath(NodeId parent, PathData path, Style style, std::string name);
    // An instance of `target`'s subtree (Lot 5). Renders target's content with
    // this node's transform; editing target updates every instance.
    NodeId AddInstance(NodeId parent, NodeId target, std::string name);
    void   SetPath(NodeId node, PathData path);
    void   SetStyle(NodeId node, Style style);
    // Replace a node's instancing modifier stack (Lot 5).
    void   SetModifiers(NodeId node, std::vector<Modifier> modifiers);
    void   SetTransform(NodeId node, const Transform2D& t);
    void   SetVisible(NodeId node, bool visible);
    // Object parenting (docs/Ink/DOCUMENT_MODEL.md §2). `keepWorld` preserves
    // the child's on-screen position by folding the inherited transform into
    // its local one. Refused (no-op) if it would create a cycle. ClearParent
    // detaches, optionally keeping the world position.
    bool   SetParent(NodeId child, NodeId parent, bool keepWorld = true);
    void   ClearParent(NodeId child, bool keepWorld = true);
    // Group compositing (docs/Ink/DOCUMENT_MODEL.md §2). Setting any of these
    // to a non-default value makes the group composite its subtree as a unit.
    void   SetOpacity(NodeId group, float opacity);
    void   SetBlend(NodeId group, BlendMode blend);
    void   SetIsolate(NodeId group, bool isolate);
    void   SetClip(NodeId group, bool clip);
    void   Remove(NodeId node);      // node or page (subtree included)
    void   Clear();                  // everything (fresh document)

    // ── Editing / undo support (Lot 8) ───────────────────────────────────────
    // A detached copy of a node subtree with its placement — the currency of
    // command-based undo (Remove ↔ Restore round-trips exactly).
    struct SubtreeSnapshot {
        std::vector<Node> nodes;     // pre-order; [0] = the root (ids preserved)
        int indexInParent = -1;      // root's position among its siblings
    };
    SubtreeSnapshot CopySubtree(NodeId root) const;
    // Reinsert a subtree removed earlier. Ids restore VERBATIM (ids are never
    // reused, so this is the same logical nodes coming back). False if the
    // root id still exists or its parent is gone.
    bool RestoreSubtree(const SubtreeSnapshot& snap);
    // Deep copy of `src` (fresh ids) inserted right after it among its
    // siblings. Intra-subtree references (children, parentId, targetRef,
    // modifier refs) are remapped; references OUTSIDE the subtree are kept.
    NodeId DuplicateSubtree(NodeId src);
    // Bake the node's scale into its geometry (Blender's Apply Scale —
    // docs/Ink/DOCUMENT_MODEL.md §4): anchors/handles scale, Document-space
    // stroke widths scale by the geometric mean of |sx|,|sy|, then sx/sy
    // reset to 1. World appearance is unchanged (local matrix is R·S — S
    // folds into the geometry exactly). Path nodes only.
    void   ApplyScale(NodeId node);
    int    IndexInParent(NodeId id) const;   // −1 when not found

    // ── Queries ──────────────────────────────────────────────────────────────
    const Node* Find(NodeId id) const;
    Node*       FindMutable(NodeId id);   // for editors; pair with NotifyEdited
    // After mutating through FindMutable, report WHAT was edited so the
    // change log stays exact (transitional escape hatch for tools; typed ops
    // are preferred).
    void        NotifyEdited(NodeId id, ChangeKind kind) { Log(id, kind); }

    const std::vector<Page>&  Pages() const { return pages_; }
    const Page* FindPage(NodeId id) const;
    std::size_t NodeCount() const { return nodes_.size(); }

    // Resolved node → document transform. Object parenting (parentId) takes
    // precedence over the layer-tree position (docs/Ink/DOCUMENT_MODEL.md §2).
    DMat23 WorldTransform(NodeId id) const;

    // ── Change tracking ──────────────────────────────────────────────────────
    // Monotonic content version (bumped by every op) — mixed into view
    // signatures so any edit re-renders.
    std::uint64_t Version() const { return version_; }
    // Drain the pending changes (consumed by Scene::Compile once per frame).
    std::vector<Change> DrainChanges();
    bool HasPendingChanges() const { return !changes_.empty(); }

private:
    DMat23 WorldTransformDepth(NodeId id, int depth) const;   // parent recursion
    void   Log(NodeId id, ChangeKind kind);
    NodeId NextId() { return nextId_++; }
    // Remove `id` from its parent's children list (page or group).
    void   DetachFromParent(const Node& n);
    void   RemoveSubtree(NodeId id);

    std::unordered_map<NodeId, Node> nodes_;
    std::vector<Page>                pages_;
    NodeId                           nextId_  = 1;
    std::uint64_t                    version_ = 0;
    std::vector<Change>              changes_;
};

// Transitional demo content (Lot 2): the Lot 1 hard-coded scene rebuilt as
// real document content — a page, filled+stroked shapes and a 1 000-node
// grid (identical paths dedup into ONE cached mesh). Removed when the
// drawing tools land (Lot 8).
void SeedDemoDocument(Document& doc);

} // namespace Ink

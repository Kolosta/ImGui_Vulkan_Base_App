#pragma once

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

enum class NodeKind : std::uint8_t { Group = 0, Path = 1 };

struct Node {
    NodeId      id   = kNullNode;
    NodeKind    kind = NodeKind::Path;
    std::string name;
    NodeId      parent = kNullNode;   // owning group (kNullNode = page root)
    NodeId      page   = kNullNode;   // owning page
    Transform2D transform;
    bool        visible = true;
    bool        locked  = false;
    float       opacity = 1.0f;                    // acts from Lot 4
    BlendMode   blend   = BlendMode::Normal;       // acts from Lot 4
    bool        isolate = false;                   // acts from Lot 4

    // kind == Path
    PathData path;
    Style    style;
    // kind == Group — children in painter order (bottom → top)
    std::vector<NodeId> children;
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
    void   SetPath(NodeId node, PathData path);
    void   SetStyle(NodeId node, Style style);
    void   SetTransform(NodeId node, const Transform2D& t);
    void   SetVisible(NodeId node, bool visible);
    void   Remove(NodeId node);      // node or page (subtree included)
    void   Clear();                  // everything (fresh document)

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

    // Resolved node → document transform: pageOrigin ∘ parentChain ∘ local.
    DMat23 WorldTransform(NodeId id) const;

    // ── Change tracking ──────────────────────────────────────────────────────
    // Monotonic content version (bumped by every op) — mixed into view
    // signatures so any edit re-renders.
    std::uint64_t Version() const { return version_; }
    // Drain the pending changes (consumed by Scene::Compile once per frame).
    std::vector<Change> DrainChanges();
    bool HasPendingChanges() const { return !changes_.empty(); }

private:
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

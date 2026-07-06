#pragma once

#include "Shape.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  Document — the vector model: artboards holding shapes, organised into
//  collections, with a multi-selection and a 2D cursor.
//
//  Pure DATA layer (no Vulkan, no ImGui). It is what the .acu file serialises,
//  what the tools mutate, and what the CanvasRenderer walks to produce triangles.
//  ONE Document is owned by App::Project; every Viewport zone renders the same
//  Document with its own camera.
// ─────────────────────────────────────────────────────────────────────────────

// Reserved id of the single, non-removable "Project" root collection that holds
// the whole unified Outliner tree (collections + pages). Allocator starts above.
constexpr uint64_t kProjectRootId = 1;

struct Artboard {
    uint64_t           id = 0;
    std::string        name;
    Vec2               pos{0, 0};         // top-left, doc-units
    Vec2               size{1920, 1080};  // doc-units
    std::vector<Shape> shapes;
    // Document-wide page visibility (Outliner eye). A hidden page drops out of
    // EVERY viewport's layout (it re-flows) and isn't rendered. Distinct from a
    // viewport's per-view show/hide list (PageLayout.hiddenPages, 8d). Hiding a
    // page does NOT affect its objects' own `visible` flags.
    bool               pageVisible = true;
    // When true, this page's objects are clipped strictly to its bounds at
    // render time (overflow into the void is cut, only the selection outline
    // shows it). Off by default, per-page (Page menu / Outliner). Independent of
    // the global rule that an object never draws OVER another page's white.
    bool               clipContents = false;
    // The collection that contains this page in the unified Outliner tree (the
    // Project root by default). A page is a tree node like a collection (8b).
    uint64_t           parentId = kProjectRootId;
    // Collections nested UNDER this page (8c). A collection under a page keeps its
    // objects bound to this page (page-relative); a collection with no page
    // ancestor holds page-less "loose" objects. So a page is a full tree node.
    std::vector<uint64_t> children;
};

// The unified Outliner tree (Blender-style). EVERYTHING lives under a single
// non-removable "Project" root collection (id == kProjectRootId): collections,
// pages, and (via their collectionId) objects. A collection's `children` is an
// ORDERED, HETEROGENEOUS list of ids that may be EITHER nested collections OR
// pages (artboards) — so a page can sit inside a collection and a collection
// inside a page's collection, freely. Resolve a child id with FindCollection()
// vs FindArtboardById(). Objects still belong to a page (ab.shapes) AND to a
// collection (Shape.collectionId); the page-relative render is unchanged.
struct Collection {
    uint64_t              id = 0;
    std::string           name;
    uint64_t              parentId = 0;       // owning collection (0 only for root)
    std::vector<uint64_t> children;           // ordered child collection AND page ids
    // Outliner icon colour. colorIndex 0 = default (theme text colour); 1..N =
    // a predefined palette hue (resolved through design-system tokens by the UI
    // so it follows the theme); −1 = a custom RGBA stored in customColor.
    int                   colorIndex = 0;
    Color                 customColor{0.6f, 0.6f, 0.65f, 1.0f};

    // ── Layer/Group compositing (Lot 11/11b, Affinity-style) ───────────────────
    // When `isLayerGroup` is set this collection is a GROUP that composites as a
    // unit: its whole subtree is rendered in isolation, then merged with the rest
    // using these params (exactly the per-object compositing of Lot 4/7, lifted to
    // the group level). An ordinary organisational collection has isLayerGroup =
    // false and these are ignored (it's just an Outliner folder). A render-time
    // param, Compositor-only — the legacy renderer ignores it. See
    // docs/Vulkan/COMPOSITOR_PIPELINE.md (Lot 11).
    bool                  isLayerGroup = false;
    float                 opacity   = 1.0f;            // group opacity [0,1]
    BlendMode             blendMode = BlendMode::Normal; // group blend (Erase = knock-out)

    bool IsRoot() const { return id == kProjectRootId; }
};

// ── Edit-mode element selection ───────────────────────────────────────────────
// The fundamental editable unit is a VERTEX, addressed by (shape, part, node).
// Edges and faces are derived from vertex selection: an edge = two consecutive
// selected vertices; a face = all vertices of a closed part. The sub-mode only
// changes what a click selects and how things are drawn.
struct VertRef {
    uint64_t shape = 0;
    int      part  = 0;
    int      node  = 0;
    bool operator==(const VertRef& o) const {
        return shape == o.shape && part == o.part && node == o.node;
    }
};

// Addresses ONE Bézier handle (a node's in or out tangent). Selectable on its own
// so G/R/S can transform a single handle (per its node's HandleMode rules).
struct HandleRef {
    uint64_t shape = 0;
    int      part  = 0;
    int      node  = 0;
    bool     outSide = false;       // false = IN handle, true = OUT handle
    bool valid() const { return shape != 0; }
    bool operator==(const HandleRef& o) const {
        return shape == o.shape && part == o.part &&
               node == o.node && outSide == o.outSide;
    }
};

enum class SelectElementMode { Vertex, Edge, Face };

// Addresses ONE line mark (slope tick / crossing / bridge / pylon) on a part. A
// mark is a quasi-object: selectable on its own, but it lives INSIDE its host
// shape's part (it can't leave the object). `index` is the position in
// part.marks; it shifts if marks are inserted/removed, so callers re-resolve.
struct MarkRef {
    uint64_t shape = 0;
    int      part  = 0;
    int      index = 0;
    bool operator==(const MarkRef& o) const {
        return shape == o.shape && part == o.part && index == o.index;
    }
};

class Document {
public:
    std::vector<Artboard>   artboards;
    std::vector<Collection> collections;
    // Objects that belong to NO page (moved into a collection that isn't under a
    // page). Their geometry is interpreted in raw DOCUMENT space (pageOrigin
    // {0,0}); they don't move with any page and aren't framed by a page. They can
    // still be selected/active and serve as a pivot reference. They live in the
    // tree via their collectionId only.
    std::vector<Shape>      looseShapes;

    void Clear() {
        artboards.clear();
        collections.clear();
        looseShapes.clear();
        nextId_ = kProjectRootId + 1;       // ids above the reserved root
        EnsureProjectRoot();
        selection_.clear();
        active_ = 0;
        activePage_ = 0;
        cursor = {0, 0};
    }

    // Create the reserved "Project" root collection if it isn't there yet (called
    // by Clear and on load, so the tree always has exactly one root).
    void EnsureProjectRoot() {
        if (FindCollection(kProjectRootId)) return;
        Collection root;
        root.id = kProjectRootId; root.name = "Project"; root.parentId = 0;
        collections.insert(collections.begin(), std::move(root));
        if (nextId_ <= kProjectRootId) nextId_ = kProjectRootId + 1;
    }
    Collection& ProjectRoot() { EnsureProjectRoot(); return *FindCollection(kProjectRootId); }

    // Monotonic id allocator — stable identities for artboards/shapes/collections,
    // persisted so ids stay unique across save/load.
    uint64_t AllocId() { return nextId_++; }
    uint64_t PeekNextId() const { return nextId_; }
    void     SetNextId(uint64_t v) { nextId_ = v; }

    int AddArtboard(const std::string& name, Vec2 pos, Vec2 size) {
        Artboard ab;
        ab.id = AllocId(); ab.name = name; ab.pos = pos; ab.size = size;
        ab.parentId = kProjectRootId;
        artboards.push_back(std::move(ab));
        // Append the page to the root's ordered child list (unified tree).
        EnsureProjectRoot();
        FindCollection(kProjectRootId)->children.push_back(artboards.back().id);
        return (int)artboards.size() - 1;
    }

    Artboard* FindArtboardById(uint64_t id) {
        for (Artboard& ab : artboards) if (ab.id == id) return &ab;
        return nullptr;
    }

    // Remove a page (and its shapes) from the document and unlink it from its
    // parent collection's child order. Its nested collection children are
    // re-parented to the page's parent so they aren't lost. No-op if `id` isn't a
    // page. Returns true on a real removal.
    bool RemoveArtboard(uint64_t id) {
        int idx = ArtboardIndexById(id);
        if (idx < 0) return false;
        // Re-home nested collection children under this page to the page's parent.
        uint64_t parent = artboards[(size_t)idx].parentId;
        std::vector<uint64_t> kids = artboards[(size_t)idx].children;
        for (uint64_t c : kids) MoveNode(c, parent ? parent : kProjectRootId);
        // Unlink from the parent's ordered children.
        if (std::vector<uint64_t>* pc = ChildrenPtr(parent ? parent : kProjectRootId))
            pc->erase(std::remove(pc->begin(), pc->end(), id), pc->end());
        // Drop selection of this page's shapes, then erase the artboard.
        for (const Shape& s : artboards[(size_t)idx].shapes) Deselect(s.id);
        if (activePage_ == id) activePage_ = 0;
        artboards.erase(artboards.begin() + idx);
        ReflowLooseShapes();
        return true;
    }
    int ArtboardIndexById(uint64_t id) const {
        for (int i = 0; i < (int)artboards.size(); ++i)
            if (artboards[(size_t)i].id == id) return i;
        return -1;
    }

    bool empty() const { return artboards.empty(); }

    // ── Shapes ────────────────────────────────────────────────────────────
    // Append `shape` (id assigned here) to artboard `abIndex`; returns its id,
    // or 0 if the index is invalid. The new shape becomes the sole selection.
    uint64_t AddShape(int abIndex, Shape shape) {
        if (abIndex < 0 || abIndex >= (int)artboards.size()) return 0;
        shape.id = AllocId();
        uint64_t id = shape.id;
        artboards[(size_t)abIndex].shapes.push_back(std::move(shape));
        SelectOnly(id);
        return id;
    }

    Shape* FindShape(uint64_t id) {
        if (id == 0) return nullptr;
        for (Artboard& ab : artboards)
            for (Shape& s : ab.shapes)
                if (s.id == id) return &s;
        for (Shape& s : looseShapes)            // page-less objects
            if (s.id == id) return &s;
        return nullptr;
    }
    // True if the shape currently belongs to no page (lives in looseShapes).
    bool IsLooseShape(uint64_t id) const {
        for (const Shape& s : looseShapes) if (s.id == id) return true;
        return false;
    }
    // Artboard index that owns shape `id` (−1 if none / loose).
    int ArtboardOfShape(uint64_t id) const {
        for (int i = 0; i < (int)artboards.size(); ++i)
            for (const Shape& s : artboards[(size_t)i].shapes)
                if (s.id == id) return i;
        return -1;
    }
    // The page origin (artboard top-left, doc-units) that a shape's geometry is
    // relative to — {0,0} if the shape has no owning page (loose object, raw
    // document space). Object geometry is PAGE-RELATIVE, so this is the offset to
    // apply when mapping to world.
    Vec2 PageOriginOfShape(uint64_t id) const {
        int ab = ArtboardOfShape(id);
        return ab >= 0 ? artboards[(size_t)ab].pos : Vec2{0, 0};
    }

    void EraseShape(uint64_t id) {
        if (id == 0) return;
        for (Artboard& ab : artboards) {
            auto& v = ab.shapes;
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i].id == id) { v.erase(v.begin() + (long)i); break; }
        }
        looseShapes.erase(std::remove_if(looseShapes.begin(), looseShapes.end(),
            [&](const Shape& s){ return s.id == id; }), looseShapes.end());
        // Orphan any children whose parent was just deleted (no dangling parentId).
        for (Artboard& ab : artboards)
            for (Shape& s : ab.shapes) if (s.parentId == id) s.parentId = 0;
        for (Shape& s : looseShapes)  if (s.parentId == id) s.parentId = 0;
        Deselect(id);
    }

    // Stable-sort a page's shapes by a caller-supplied RANK (lower rank drawn
    // first → underneath). The order WITHIN one rank is preserved (draw order), so
    // it acts as a layering pass that never disturbs same-layer order. `rankOf`
    // takes a const Shape& and returns an int. Used by the IOF module to keep the
    // page in print-layer (z) order. No-op if the page id is unknown.
    template <class RankFn>
    void SortPageShapesStable(uint64_t pageId, RankFn&& rankOf) {
        Artboard* ab = FindArtboardById(pageId);
        if (!ab) return;
        std::stable_sort(ab->shapes.begin(), ab->shapes.end(),
            [&](const Shape& a, const Shape& b){ return rankOf(a) < rankOf(b); });
    }

    // Reorder shape `id` within ITS page so it sits just BEFORE shape `beforeId`
    // in draw order (lower index = drawn first = underneath). If `beforeId` is 0 or
    // not on the same page, `id` is moved to the END (top of the z-stack). Both
    // must live on the same page (no cross-page move here). Returns true on a real
    // change. This is the z-index editing primitive behind the Layers view.
    bool MoveShapeBeforeInPage(uint64_t id, uint64_t beforeId) {
        if (id == 0 || id == beforeId) return false;
        int abi = ArtboardOfShape(id);
        if (abi < 0) return false;
        Artboard& ab = artboards[(size_t)abi];
        auto& v = ab.shapes;
        size_t from = v.size();
        for (size_t i = 0; i < v.size(); ++i) if (v[i].id == id) { from = i; break; }
        if (from == v.size()) return false;
        // Target index = position of beforeId (END if absent / on another page).
        size_t to = v.size();
        if (beforeId != 0)
            for (size_t i = 0; i < v.size(); ++i) if (v[i].id == beforeId) { to = i; break; }
        Shape moved = std::move(v[from]);
        v.erase(v.begin() + (long)from);
        if (to > from) --to;                      // erase shifted indices after `from`
        if (to > v.size()) to = v.size();
        v.insert(v.begin() + (long)to, std::move(moved));
        return true;
    }

    // Detach a shape from its page → looseShapes. Keeps it visually put by baking
    // the old page origin into translate (world = pageOrigin + translate + …, so
    // now with pageOrigin {0,0} we add the old page pos). collectionId is set by
    // the caller (it's being dropped into a page-less collection).
    void DetachShapeFromPage(uint64_t id) {
        int ab = ArtboardOfShape(id);
        if (ab < 0) return;                     // already loose / unknown
        Artboard& from = artboards[(size_t)ab];
        for (size_t i = 0; i < from.shapes.size(); ++i)
            if (from.shapes[i].id == id) {
                Shape moved = std::move(from.shapes[i]);
                moved.transform.translate.x += from.pos.x;   // keep world position
                moved.transform.translate.y += from.pos.y;
                from.shapes.erase(from.shapes.begin() + (long)i);
                looseShapes.push_back(std::move(moved));
                return;
            }
    }
    // Re-attach a loose shape to page `dstIndex`, keeping it visually put (undo
    // the bake: translate -= new page pos).
    bool AttachShapeToPage(uint64_t id, int dstIndex) {
        if (dstIndex < 0 || dstIndex >= (int)artboards.size()) return false;
        for (size_t i = 0; i < looseShapes.size(); ++i)
            if (looseShapes[i].id == id) {
                Shape moved = std::move(looseShapes[i]);
                moved.transform.translate.x -= artboards[(size_t)dstIndex].pos.x;
                moved.transform.translate.y -= artboards[(size_t)dstIndex].pos.y;
                looseShapes.erase(looseShapes.begin() + (long)i);
                artboards[(size_t)dstIndex].shapes.push_back(std::move(moved));
                return true;
            }
        return false;
    }

    // Duplicate shape `id` into its own artboard (new id, " copy" name, nudged a
    // little so it's visible). Returns the new id (0 if the source is gone). The
    // copy is NOT auto-selected (callers decide).
    uint64_t DuplicateShape(uint64_t id, Vec2 nudge = {12, 12}) {
        int ab = ArtboardOfShape(id);
        if (ab < 0) return 0;
        Shape* src = nullptr;
        for (Shape& s : artboards[(size_t)ab].shapes) if (s.id == id) { src = &s; break; }
        if (!src) return 0;
        Shape copy = *src;                       // deep copy (parts, transform…)
        copy.id = AllocId();
        copy.name = src->name.empty() ? "Object copy" : src->name + " copy";
        copy.transform.translate.x += nudge.x;
        copy.transform.translate.y += nudge.y;
        artboards[(size_t)ab].shapes.push_back(std::move(copy));
        return artboards[(size_t)ab].shapes.back().id;
    }

    // Move shape `id` to artboard `dstIndex`. Geometry is page-relative, so to
    // keep the object VISUALLY put (keepWorldPos = true, the default) we shift
    // its transform.translate by the page-origin delta: translate += oldPos −
    // newPos. With keepWorldPos = false the object keeps its page-relative coords
    // (so it lands at the same spot WITHIN the new page). No-op if already there
    // or indices invalid. Returns true on a real move.
    bool MoveShapeToArtboard(uint64_t id, int dstIndex, bool keepWorldPos = true) {
        if (id == 0 || dstIndex < 0 || dstIndex >= (int)artboards.size()) return false;
        int src = ArtboardOfShape(id);
        if (src < 0 || src == dstIndex) return false;
        Artboard& from = artboards[(size_t)src];
        Shape* sp = nullptr;
        size_t idx = 0;
        for (size_t i = 0; i < from.shapes.size(); ++i)
            if (from.shapes[i].id == id) { sp = &from.shapes[i]; idx = i; break; }
        if (!sp) return false;
        Shape moved = std::move(*sp);
        if (keepWorldPos) {
            Vec2 oldPo = from.pos, newPo = artboards[(size_t)dstIndex].pos;
            moved.transform.translate.x += oldPo.x - newPo.x;
            moved.transform.translate.y += oldPo.y - newPo.y;
        }
        from.shapes.erase(from.shapes.begin() + (long)idx);
        artboards[(size_t)dstIndex].shapes.push_back(std::move(moved));
        return true;
    }

    // ── Selection (multi; `active_` = last selected, 0 = none) ──────────────
    const std::vector<uint64_t>& Selection() const { return selection_; }
    bool   IsSelected(uint64_t id) const {
        return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
    }
    uint64_t ActiveId() const { return active_; }
    Shape*   ActiveShape() { return FindShape(active_); }
    bool     HasSelection() const { return !selection_.empty(); }

    // Full reset: no selection AND no active object (used by New/Open/Undo/Delete
    // where the active object may no longer exist or context fully changed).
    void ClearSelection() { selection_.clear(); active_ = 0; activeGroup_ = 0; }
    // Blender-style "deselect all": empties the selection but KEEPS the last
    // active object as active (its origin stays visible, "Active Element" pivot
    // still works) — only it isn't part of the selection anymore. Used when the
    // user clicks empty canvas.
    void DeselectAll() { selection_.clear(); activeGroup_ = 0; }   // active_ kept on purpose

    void SelectOnly(uint64_t id) {
        selection_.clear();
        if (id) { selection_.push_back(id); active_ = id; }
        else    { active_ = 0; }
        activeGroup_ = 0;               // a direct object selection isn't a group
    }
    void SelectAdd(uint64_t id) {       // add + make active
        if (!id) return;
        if (!IsSelected(id)) selection_.push_back(id);
        active_ = id;
        activeGroup_ = 0;               // selecting an object leaves group mode
    }

    // ── Active layer group (Lot 11) ─────────────────────────────────────────────
    // A group can be the active selection on its own (clicked in the Outliner Layers
    // view, or via "Select Group"): then the Properties panel shows the GROUP. Any
    // object selection clears it (an object and a group aren't co-active).
    uint64_t ActiveGroup() const { return activeGroup_; }
    // Select a group AS AN ENTITY (Properties shows the group). The member objects are
    // NOT added to the object selection — a group is its own thing, like an object
    // with sub-objects. Transforms on a group act through its groupId, not the object
    // selection. `active_` is cleared so the Properties panel routes to the group.
    void SelectGroup(uint64_t gid) {
        activeGroup_ = gid;
        selection_.clear();
        active_ = 0;
    }
    void ClearActiveGroup() { activeGroup_ = 0; }
    // Member object ids of a layer group at ANY depth (document order). Empty if
    // `gid` isn't a group. Used to transform / operate on a whole group as a unit.
    std::vector<uint64_t> GroupMembers(uint64_t gid) {
        std::vector<uint64_t> out;
        if (!IsGroup(gid)) return out;
        for (Artboard& ab : artboards)
            for (Shape& s : ab.shapes) if (ShapeInGroup(s.id, gid)) out.push_back(s.id);
        for (Shape& s : looseShapes) if (ShapeInGroup(s.id, gid)) out.push_back(s.id);
        return out;
    }
    void Deselect(uint64_t id) {
        selection_.erase(std::remove(selection_.begin(), selection_.end(), id),
                         selection_.end());
        if (active_ == id) active_ = selection_.empty() ? 0 : selection_.back();
    }
    // Shift+click semantics: toggle membership; on add, become active.
    void SelectToggle(uint64_t id) {
        if (!id) return;
        if (IsSelected(id)) Deselect(id);
        else                SelectAdd(id);
    }
    void SetActive(uint64_t id) {       // ensure selected + active
        if (!id) return;
        if (!IsSelected(id)) selection_.push_back(id);
        active_ = id;
        activeGroup_ = 0;               // selecting an object leaves group mode
    }

    // ── Active page (Blender-like "active object's page") ────────────────────
    // The page new Shift+A objects spawn on. It's the last page an object was
    // selected on, OR a page the user clicked. 0 = none → new objects are added
    // page-less (loose, under the root collection). Cleared when clicking empty
    // canvas. SyncActivePageToSelection keeps it = the active object's page.
    uint64_t ActivePage() const { return activePage_; }
    void SetActivePage(uint64_t pageId) { activePage_ = pageId; }
    void ClearActivePage() { activePage_ = 0; }
    void SyncActivePageToSelection() {
        if (!active_) return;                 // keep last when nothing active
        int ab = ArtboardOfShape(active_);
        if (ab >= 0) activePage_ = artboards[(size_t)ab].id;   // loose → unchanged
    }

    // ── Collections (unified tree) ────────────────────────────────────────────
    // parentId defaults to the Project root. The parent may be a collection OR a
    // PAGE (a page is a full tree node), so a layer collection can sit under a
    // page. The new collection is appended to its parent's ordered `children`.
    uint64_t AddCollection(const std::string& name, uint64_t parentId = kProjectRootId) {
        EnsureProjectRoot();
        // Accept any valid tree node (collection or page); else fall back to root.
        if (!ChildrenPtr(parentId)) parentId = kProjectRootId;
        Collection c; c.id = AllocId(); c.name = name; c.parentId = parentId;
        collections.push_back(std::move(c));
        ChildrenPtr(parentId)->push_back(collections.back().id);
        return collections.back().id;
    }
    Collection* FindCollection(uint64_t id) {
        for (Collection& c : collections) if (c.id == id) return &c;
        return nullptr;
    }

    // ── Layer groups (Lot 11/11b) ──────────────────────────────────────────────
    // A layer group is a LAYER concept, NOT an organisation node: a Collection with
    // isLayerGroup = true that ONLY carries the group's compositing params + a name.
    // It is NOT in the collection/page tree for objects (objects link via
    // Shape::groupId, a separate link from collectionId), so it never disturbs the
    // Outliner Collection hierarchy or the page reflow. Groups NEST: a group's
    // `parentId` is its PARENT GROUP (0 = top-level on its page). An object's
    // Shape::groupId points at its INNERMOST group; the hierarchy is the chain
    // groupId → parentId → … → 0. Groups are page-local (all members one page).
    bool IsGroup(uint64_t id) {
        Collection* c = FindCollection(id);
        return c && c->isLayerGroup;
    }
    // The innermost layer group a shape belongs to (Shape::groupId), or 0 if none.
    uint64_t GroupOfShape(uint64_t shapeId) {
        Shape* s = FindShape(shapeId);
        return s ? s->groupId : 0;
    }
    // The parent group of a group (Collection::parentId for an isLayerGroup), or 0.
    uint64_t GroupParent(uint64_t gid) {
        Collection* c = FindCollection(gid);
        return (c && c->isLayerGroup) ? c->parentId : 0;
    }
    // The chain of groups from the INNERMOST (`gid`) up to the top-level group, in
    // ascending order (innermost first). Cycle-safe.
    std::vector<uint64_t> GroupChainFrom(uint64_t gid) {
        std::vector<uint64_t> out;
        uint64_t cur = gid; int guard = 0;
        while (cur && IsGroup(cur) && guard++ < 4096) {
            out.push_back(cur);
            cur = GroupParent(cur);
        }
        return out;
    }
    // True if shape `shapeId` is inside group `gid` at ANY depth (gid is in its
    // group chain). Used so a group's "members" include nested sub-group members.
    bool ShapeInGroup(uint64_t shapeId, uint64_t gid) {
        if (!gid) return false;
        uint64_t cur = GroupOfShape(shapeId); int guard = 0;
        while (cur && guard++ < 4096) {
            if (cur == gid) return true;
            cur = GroupParent(cur);
        }
        return false;
    }
    // True if group `inner` is `gid` or nested anywhere under it (descend the parent
    // links upward from `inner`).
    bool GroupInGroup(uint64_t inner, uint64_t gid) {
        uint64_t cur = inner; int guard = 0;
        while (cur && guard++ < 4096) {
            if (cur == gid) return true;
            cur = GroupParent(cur);
        }
        return false;
    }

    // True if every shape id in `ids` lives on the SAME page (or all are loose).
    // Used to forbid grouping objects across pages — a layer is page-local.
    bool AllOnSamePage(const std::vector<uint64_t>& ids) {
        int page = -2;   // -2 = unset, -1 = loose
        for (uint64_t id : ids) {
            int ab = ArtboardOfShape(id);   // -1 = loose
            if (page == -2) page = ab;
            else if (page != ab) return false;
        }
        return true;
    }

    // The deepest group common to every shape in `ids` (0 = none / page-level). Used
    // to nest a new group under the right parent when grouping a sub-selection.
    uint64_t CommonGroup(const std::vector<uint64_t>& ids) {
        if (ids.empty()) return 0;
        std::vector<uint64_t> chain = GroupChainFrom(GroupOfShape(ids.front())); // innermost→top
        for (size_t i = 1; i < ids.size(); ++i) {
            uint64_t g = GroupOfShape(ids[i]);
            // Keep only chain entries that also contain ids[i].
            std::vector<uint64_t> kept;
            for (uint64_t c : chain) if (ShapeInGroup(ids[i], c)) kept.push_back(c);
            chain.swap(kept);
            (void)g;
            if (chain.empty()) break;
        }
        return chain.empty() ? 0 : chain.front();   // deepest common
    }

    // Group the current selection into a NEW layer group. Refuses if the selection
    // spans multiple pages (a layer is page-local). NESTS the new group under the
    // selection's deepest common group (so grouping inside a group makes a sub-group).
    // Each selected object's sub-tree directly below the common parent is re-homed
    // into the new group. Returns the group id, or 0 if nothing selected / cross-page.
    uint64_t GroupSelection(const std::string& name = "Group") {
        if (selection_.empty()) return 0;
        if (!AllOnSamePage(selection_)) return 0;   // cross-page → refuse
        uint64_t parent = CommonGroup(selection_);  // 0 = top-level on the page
        Collection g;
        g.id = AllocId(); g.name = name; g.parentId = parent;
        g.isLayerGroup = true;
        collections.push_back(std::move(g));
        uint64_t gid = collections.back().id;
        // For each selected object, find the node DIRECTLY under `parent` in its
        // chain (the object itself if its innermost group IS the parent, else the
        // sub-group whose parent is `parent`) and re-home that node into `gid`.
        for (uint64_t sid : selection_) {
            Shape* s = FindShape(sid);
            if (!s) continue;
            uint64_t inner = s->groupId;
            if (inner == parent || inner == 0) {
                s->groupId = gid;               // object sat directly in `parent`
            } else {
                // Walk up to the child-of-parent group and re-parent it to gid.
                uint64_t cur = inner, prev = inner;
                while (cur && GroupParent(cur) != parent && cur != parent) { prev = cur; cur = GroupParent(cur); }
                uint64_t childOfParent = (cur && GroupParent(cur) == parent) ? cur : prev;
                if (Collection* cc = FindCollection(childOfParent)) cc->parentId = gid;
                else s->groupId = gid;
            }
        }
        MakeGroupContiguous(gid);
        return gid;
    }

    // Reorder a group's members (at ANY nesting depth) so they sit CONSECUTIVELY in
    // their page's draw order — the renderer isolates a group as one contiguous run.
    // Members keep their relative order; the run is anchored at the FIRST member.
    // Idempotent. (Nested sub-groups stay contiguous within the parent run because
    // their members are a sub-range of the parent's members.)
    void MakeGroupContiguous(uint64_t gid) {
        if (!IsGroup(gid)) return;
        auto pack = [&](std::vector<Shape>& v){
            // EARLY-OUT BEFORE moving anything (don't leave moved-from shells).
            bool hasMember = false;
            for (const Shape& s : v) if (ShapeInGroup(s.id, gid)) { hasMember = true; break; }
            if (!hasMember) return;
            std::vector<Shape> members, others;
            size_t firstMember = v.size();
            for (size_t i = 0; i < v.size(); ++i) {
                if (ShapeInGroup(v[i].id, gid)) {
                    if (firstMember == v.size()) firstMember = others.size();
                    members.push_back(std::move(v[i]));
                } else {
                    others.push_back(std::move(v[i]));
                }
            }
            if (firstMember > others.size()) firstMember = others.size();
            std::vector<Shape> out;
            out.reserve(v.size());
            for (size_t i = 0; i < firstMember; ++i) out.push_back(std::move(others[i]));
            for (auto& m : members)                 out.push_back(std::move(m));
            for (size_t i = firstMember; i < others.size(); ++i) out.push_back(std::move(others[i]));
            v = std::move(out);
        };
        for (Artboard& ab : artboards) pack(ab.shapes);
        pack(looseShapes);
    }

    // Dissolve a layer group: its DIRECT object members fall back to the group's
    // parent group (or page-level), its DIRECT sub-groups are re-parented to the
    // parent, then the group collection is dropped.
    void Ungroup(uint64_t id) {
        if (!IsGroup(id)) return;
        uint64_t parent = GroupParent(id);
        auto clear = [&](std::vector<Shape>& v){
            for (Shape& s : v) if (s.groupId == id) s.groupId = parent;
        };
        for (Artboard& ab : artboards) clear(ab.shapes);
        clear(looseShapes);
        for (Collection& c : collections) if (c.isLayerGroup && c.parentId == id) c.parentId = parent;
        collections.erase(std::remove_if(collections.begin(), collections.end(),
            [&](const Collection& c){ return c.id == id; }), collections.end());
        if (parent) MakeGroupContiguous(parent);
    }

    // Show/hide a whole layer group (Layers eye): toggles `visible` on every member
    // object at any depth. Hidden members drop out of the render but stay selectable.
    void SetGroupVisible(uint64_t id, bool visible) {
        if (!IsGroup(id)) return;
        auto apply = [&](std::vector<Shape>& v){
            for (Shape& s : v) if (ShapeInGroup(s.id, id)) s.visible = visible;
        };
        for (Artboard& ab : artboards) apply(ab.shapes);
        apply(looseShapes);
    }
    bool GroupHidden(uint64_t id) {
        if (!IsGroup(id)) return false;
        bool any = false, allHidden = true;
        auto scan = [&](std::vector<Shape>& v){
            for (Shape& s : v) if (ShapeInGroup(s.id, id)) { any = true; if (s.visible) allHidden = false; }
        };
        for (Artboard& ab : artboards) scan(ab.shapes);
        scan(looseShapes);
        return any && allHidden;
    }
    bool IsCollectionId(uint64_t id) {
        for (const Collection& c : collections) if (c.id == id) return true;
        return false;
    }
    bool IsPageId(uint64_t id) { return FindArtboardById(id) != nullptr; }

    // The children vector of a tree node (collection OR page), or null if the id
    // is neither (e.g. an object — objects aren't tree-node children, they hang
    // off a collection via collectionId).
    std::vector<uint64_t>* ChildrenPtr(uint64_t nodeId) {
        if (Collection* c = FindCollection(nodeId)) return &c->children;
        if (Artboard* a = FindArtboardById(nodeId)) return &a->children;
        return nullptr;
    }

    // Re-parent a tree node (collection OR page) under `newParent` (a collection
    // OR a page), appending to its child order. Guards against cycles and never
    // moves the root. Recomputes page attachment afterwards (objects whose
    // collection lost/gained a page ancestor flip loose⇄attached).
    void MoveNode(uint64_t nodeId, uint64_t newParent) {
        if (nodeId == kProjectRootId || nodeId == newParent) return;
        std::vector<uint64_t>* npc = ChildrenPtr(newParent);
        if (!npc) return;
        // Cycle check: can't move a node into its own subtree.
        std::vector<uint64_t> sub; CollectSubtreeNodes(nodeId, sub);
        if (std::find(sub.begin(), sub.end(), newParent) != sub.end()) return;
        uint64_t oldParent = ParentOf(nodeId);
        if (std::vector<uint64_t>* opc = ChildrenPtr(oldParent))
            opc->erase(std::remove(opc->begin(), opc->end(), nodeId), opc->end());
        npc->push_back(nodeId);
        if (Collection* c = FindCollection(nodeId)) c->parentId = newParent;
        else if (Artboard* a = FindArtboardById(nodeId)) a->parentId = newParent;
        ReflowLooseShapes();
    }
    uint64_t ParentOf(uint64_t nodeId) {
        if (Collection* c = FindCollection(nodeId)) return c->parentId;
        if (Artboard* a = FindArtboardById(nodeId)) return a->parentId;
        return 0;
    }

    // Depth-first collect of ALL tree-node ids (collections AND pages) under `id`,
    // including `id`. Used for cycle checks and subtree ops.
    void CollectSubtreeNodes(uint64_t id, std::vector<uint64_t>& out) {
        out.push_back(id);
        if (std::vector<uint64_t>* ch = ChildrenPtr(id))
            for (uint64_t c : *ch) CollectSubtreeNodes(c, out);
    }
    // Depth-first collect of nested COLLECTION ids under `id` (incl. `id` if it's
    // a collection). Traverses through pages too (a page can hold collections).
    void CollectCollections(uint64_t id, std::vector<uint64_t>& out) {
        if (IsCollectionId(id)) out.push_back(id);
        if (std::vector<uint64_t>* ch = ChildrenPtr(id))
            for (uint64_t c : *ch) CollectCollections(c, out);
    }
    // Depth-first collect of PAGE ids in the subtree under `id`.
    void CollectPages(uint64_t id, std::vector<uint64_t>& out) {
        if (IsPageId(id)) out.push_back(id);
        if (std::vector<uint64_t>* ch = ChildrenPtr(id))
            for (uint64_t c : *ch) CollectPages(c, out);
    }

    // Walk up parents from a node; return the nearest PAGE ancestor id, or 0 if
    // the node has no page in its ancestry (it's under the root / a page-less
    // collection). A page is its own page-ancestor.
    uint64_t PageAncestorOf(uint64_t nodeId) {
        uint64_t cur = nodeId;
        int guard = 0;
        while (cur && cur != kProjectRootId && guard++ < 4096) {
            if (IsPageId(cur)) return cur;
            cur = ParentOf(cur);
        }
        return IsPageId(cur) ? cur : 0;
    }

    // Recompute every object's page attachment from its collection's ancestry,
    // so an object is ALWAYS physically located under the page that owns it in the
    // tree (the strong invariant the Outliner relies on):
    //   • collection has a PAGE ancestor → the object belongs to that exact page
    //     (attach / move there, keeping world position);
    //   • no page ancestor → the object is page-less (detach into looseShapes).
    // Objects with collectionId 0 stay on their current page (loose under it).
    // Called after any tree move / collection change so the tree stays consistent.
    void ReflowLooseShapes() {
        // 1) Attached objects: detach if their collection has NO page ancestor, or
        //    MOVE to the right page if their collection lives under a DIFFERENT one.
        for (size_t ai = 0; ai < artboards.size(); ++ai) {
            Artboard& ab = artboards[ai];
            for (size_t i = 0; i < ab.shapes.size();) {
                uint64_t coll = ab.shapes[i].collectionId;
                uint64_t pageId = (coll != 0) ? PageAncestorOf(coll) : 0;
                if (coll != 0 && pageId == 0) {
                    uint64_t sid = ab.shapes[i].id;
                    ++i;                       // DetachShapeFromPage re-finds it
                    DetachShapeFromPage(sid);
                } else if (pageId != 0 && pageId != ab.id) {
                    uint64_t sid = ab.shapes[i].id;
                    int dst = ArtboardIndexById(pageId);
                    if (dst >= 0) { MoveShapeToArtboard(sid, dst, /*keepWorldPos=*/true); }
                    else ++i;                  // shouldn't happen; skip to avoid a loop
                } else ++i;
            }
        }
        // 2) Loose objects whose collection now HAS a page ancestor → attach.
        for (size_t i = 0; i < looseShapes.size();) {
            uint64_t coll = looseShapes[i].collectionId;
            uint64_t pageId = (coll != 0) ? PageAncestorOf(coll) : 0;
            int dst = pageId ? ArtboardIndexById(pageId) : -1;
            if (dst >= 0) { uint64_t sid = looseShapes[i].id; AttachShapeToPage(sid, dst); }
            else ++i;
        }
    }

    // Delete a collection. deleteContents: erase the whole subtree (nested
    // collections + pages + their objects). Otherwise re-parent the direct
    // children up to this collection's parent (Blender's Delete vs Delete
    // Hierarchy). The root is never deleted.
    void EraseCollection(uint64_t id, bool deleteContents) {
        if (id == kProjectRootId) return;
        Collection* self = FindCollection(id);
        if (!self) return;
        uint64_t parent = self->parentId;

        if (Collection* p = FindCollection(parent)) {       // unlink from parent
            auto& ch = p->children;
            ch.erase(std::remove(ch.begin(), ch.end(), id), ch.end());
        }

        if (deleteContents) {
            std::vector<uint64_t> cols; CollectCollections(id, cols);
            std::vector<uint64_t> pages; CollectPages(id, pages);
            auto inCols = [&](uint64_t c){ return std::find(cols.begin(), cols.end(), c) != cols.end(); };
            auto isPage = [&](uint64_t p){ return std::find(pages.begin(), pages.end(), p) != pages.end(); };
            // Erase whole pages in the subtree; for other pages, erase objects
            // whose collection is in the subtree.
            for (Artboard& ab : artboards)
                for (Shape& s : ab.shapes) Deselect(s.id);    // simplest: clear sel of removed
            artboards.erase(std::remove_if(artboards.begin(), artboards.end(),
                [&](const Artboard& a){ return isPage(a.id); }), artboards.end());
            for (Artboard& ab : artboards) {
                auto& v = ab.shapes;
                v.erase(std::remove_if(v.begin(), v.end(),
                    [&](const Shape& s){ return inCols(s.collectionId); }), v.end());
            }
            collections.erase(std::remove_if(collections.begin(), collections.end(),
                [&](const Collection& c){ return inCols(c.id); }), collections.end());
        } else {
            // Re-parent direct children (collections AND pages) + loose objects up.
            for (uint64_t child : self->children) {
                if (Collection* cc = FindCollection(child)) cc->parentId = parent;
                else if (Artboard* a = FindArtboardById(child)) a->parentId = parent;
                if (Collection* p = FindCollection(parent)) p->children.push_back(child);
            }
            for (Artboard& ab : artboards)
                for (Shape& s : ab.shapes)
                    if (s.collectionId == id) s.collectionId = parent;
            collections.erase(std::remove_if(collections.begin(), collections.end(),
                [&](const Collection& c){ return c.id == id; }), collections.end());
        }
    }

    // Set visibility for a whole collection subtree: every object whose
    // collection is in the subtree, plus every page in the subtree (pageVisible).
    // Hiding does NOT deselect (hidden stays selectable/active).
    void SetCollectionVisible(uint64_t id, bool visible) {
        std::vector<uint64_t> cols; CollectCollections(id, cols);
        std::vector<uint64_t> pages; CollectPages(id, pages);
        auto inCols = [&](uint64_t c){ return std::find(cols.begin(), cols.end(), c) != cols.end(); };
        for (Artboard& ab : artboards) {
            if (std::find(pages.begin(), pages.end(), ab.id) != pages.end())
                ab.pageVisible = visible;
            for (Shape& s : ab.shapes)
                if (inCols(s.collectionId)) s.visible = visible;
        }
    }
    // True if everything in the subtree (objects + pages) is currently hidden.
    bool CollectionHidden(uint64_t id) {
        std::vector<uint64_t> cols; CollectCollections(id, cols);
        std::vector<uint64_t> pages; CollectPages(id, pages);
        auto inCols = [&](uint64_t c){ return std::find(cols.begin(), cols.end(), c) != cols.end(); };
        bool any = false, allHidden = true;
        for (Artboard& ab : artboards) {
            if (std::find(pages.begin(), pages.end(), ab.id) != pages.end()) {
                any = true; if (ab.pageVisible) allHidden = false;
            }
            for (Shape& s : ab.shapes)
                if (inCols(s.collectionId)) { any = true; if (s.visible) allHidden = false; }
        }
        return any && allHidden;
    }

    // Reveal (show) every hidden object in the document — Blender's Alt+H.
    void RevealAllShapes() {
        for (Artboard& ab : artboards)
            for (Shape& s : ab.shapes) s.visible = true;
    }

    // ── Edit-mode element (vertex) selection ────────────────────────────────
    SelectElementMode elementMode = SelectElementMode::Vertex;
    const std::vector<VertRef>& VertSelection() const { return vertSel_; }
    bool   HasVertSelection() const { return !vertSel_.empty(); }
    const VertRef& ActiveVert() const { return activeVert_; }
    bool   IsVertSelected(const VertRef& v) const {
        return std::find(vertSel_.begin(), vertSel_.end(), v) != vertSel_.end();
    }
    void   ClearVertSelection() { vertSel_.clear(); activeVert_ = {}; handleSel_.clear(); }
    void   VertSelectOnly(const VertRef& v) {
        vertSel_.clear(); handleSel_.clear();
        for (const VertRef& g : JunctionGroup(v)) vertSel_.push_back(g);
        activeVert_ = v;
    }
    void   VertSelectAdd(const VertRef& v) {
        // A junction is ONE vertex: selecting any of its coincident nodes selects the
        // whole group (so the dot reads selected and ALL its handles, ≥3, show).
        for (const VertRef& g : JunctionGroup(v))
            if (!IsVertSelected(g)) vertSel_.push_back(g);
        activeVert_ = v;
    }
    void   VertDeselect(const VertRef& v) {
        for (const VertRef& g : JunctionGroup(v))
            vertSel_.erase(std::remove(vertSel_.begin(), vertSel_.end(), g), vertSel_.end());
        if (std::find(vertSel_.begin(), vertSel_.end(), activeVert_) == vertSel_.end())
            activeVert_ = vertSel_.empty() ? VertRef{} : vertSel_.back();
    }
    void   VertSelectToggle(const VertRef& v) {
        if (IsVertSelected(v)) VertDeselect(v); else VertSelectAdd(v);
    }
    // The set of node refs that form ONE editable vertex: just `v` for an ordinary
    // node, or all coincident nodes sharing v's junctionId (same shape+part) — the
    // multi-path branch vertex. Returned by value (small).
    std::vector<VertRef> JunctionGroup(const VertRef& v) {
        std::vector<VertRef> g{ v };
        Shape* s = FindShape(v.shape);
        if (!s || v.part < 0 || v.part >= (int)s->parts.size()) return g;
        auto& nodes = s->parts[(size_t)v.part].path.nodes;
        if (v.node < 0 || v.node >= (int)nodes.size()) return g;
        uint32_t jid = nodes[(size_t)v.node].junctionId;
        if (jid == 0) return g;
        g.clear();
        for (int i = 0; i < (int)nodes.size(); ++i)
            if (nodes[(size_t)i].junctionId == jid)
                g.push_back(VertRef{ v.shape, v.part, i });
        return g;
    }

    // ── Handle selection (Bézier tangents) ──────────────────────────────────────
    // A SET of selected handles, COEXISTING with the vertex selection (a point and
    // its handle can both be selected). G/R/S act on the selected handle(s) when any
    // is selected; otherwise on the selected vertices. activeHandle_ = last picked.
    const std::vector<HandleRef>& HandleSelection() const { return handleSel_; }
    const HandleRef& ActiveHandle() const { return activeHandle_; }
    bool   HasHandleSelection() const { return !handleSel_.empty(); }
    bool   IsHandleSelected(const HandleRef& h) const {
        return std::find(handleSel_.begin(), handleSel_.end(), h) != handleSel_.end();
    }
    void   ClearHandleSelection() { handleSel_.clear(); activeHandle_ = {}; }
    void   HandleSelectOnly(const HandleRef& h) {
        vertSel_.clear(); activeVert_ = {};      // a fresh handle pick clears verts
        handleSel_.clear(); handleSel_.push_back(h); activeHandle_ = h;
    }
    void   HandleSelectAdd(const HandleRef& h) {  // Shift+click: extend
        if (!IsHandleSelected(h)) handleSel_.push_back(h);
        activeHandle_ = h;
    }
    void   HandleDeselect(const HandleRef& h) {
        handleSel_.erase(std::remove(handleSel_.begin(), handleSel_.end(), h), handleSel_.end());
        if (activeHandle_ == h) activeHandle_ = handleSel_.empty() ? HandleRef{} : handleSel_.back();
    }
    void   HandleSelectToggle(const HandleRef& h) {
        if (IsHandleSelected(h)) HandleDeselect(h); else HandleSelectAdd(h);
    }
    // First selected handle (for a single-handle op convenience). Invalid if none.
    const HandleRef& SelectedHandle() const {
        static const HandleRef kNone{}; return handleSel_.empty() ? kNone : handleSel_.front();
    }

    // ── Line-mark selection (quasi-objects under a curve) ───────────────────────
    const std::vector<MarkRef>& MarkSelection() const { return markSel_; }
    bool   HasMarkSelection() const { return !markSel_.empty(); }
    const MarkRef& ActiveMark() const { return activeMark_; }
    bool   IsMarkSelected(const MarkRef& m) const {
        return std::find(markSel_.begin(), markSel_.end(), m) != markSel_.end();
    }
    void   ClearMarkSelection() { markSel_.clear(); activeMark_ = {}; }
    void   MarkSelectOnly(const MarkRef& m) { markSel_.clear(); markSel_.push_back(m); activeMark_ = m; }
    void   MarkSelectAdd(const MarkRef& m) {
        if (!IsMarkSelected(m)) markSel_.push_back(m);
        activeMark_ = m;
    }
    void   MarkDeselect(const MarkRef& m) {
        markSel_.erase(std::remove(markSel_.begin(), markSel_.end(), m), markSel_.end());
        if (activeMark_ == m) activeMark_ = markSel_.empty() ? MarkRef{} : markSel_.back();
    }
    void   MarkSelectToggle(const MarkRef& m) {
        if (IsMarkSelected(m)) MarkDeselect(m); else MarkSelectAdd(m);
    }

    // ── Object parenting (Blender Ctrl+P) ──────────────────────────────────────
    // parentId on Shape is the link. These walk the OBJECT parent graph (distinct
    // from the collection/page tree above). Guarded against cycles by a depth cap.
    //
    // Direct children of object `id` (shapes whose parentId == id), across pages +
    // loose. Order is document order.
    std::vector<uint64_t> ChildrenOf(uint64_t id) {
        std::vector<uint64_t> out;
        if (!id) return out;
        auto scan = [&](std::vector<Shape>& v){
            for (Shape& s : v) if (s.parentId == id) out.push_back(s.id);
        };
        for (Artboard& ab : artboards) scan(ab.shapes);
        scan(looseShapes);
        return out;
    }
    // All DESCENDANTS of `id` (depth-first, excluding `id`). Cycle-safe.
    std::vector<uint64_t> DescendantsOf(uint64_t id) {
        std::vector<uint64_t> out;
        std::vector<uint64_t> stack = ChildrenOf(id);
        int guard = 0;
        while (!stack.empty() && guard++ < 100000) {
            uint64_t c = stack.back(); stack.pop_back();
            if (std::find(out.begin(), out.end(), c) != out.end()) continue;  // cycle guard
            out.push_back(c);
            for (uint64_t g : ChildrenOf(c)) stack.push_back(g);
        }
        return out;
    }
    // True if `ancestor` is `id` or any ancestor of `id` (walks parentId up). Used
    // to forbid creating a parenting cycle.
    bool IsAncestorOrSelf(uint64_t ancestor, uint64_t id) {
        uint64_t cur = id; int guard = 0;
        while (cur && guard++ < 100000) {
            if (cur == ancestor) return true;
            Shape* s = FindShape(cur);
            cur = s ? s->parentId : 0;
        }
        return false;
    }
    // Parent `child` under `parent` unless that would form a cycle (or self-parent).
    // Returns true on a real change. The CALLER keeps the child visually put (parent
    // is purely a future-motion relationship; binding doesn't move anything).
    bool SetParent(uint64_t child, uint64_t parent) {
        if (!child || child == parent) return false;
        Shape* cs = FindShape(child);
        if (!cs) return false;
        if (parent && IsAncestorOrSelf(child, parent)) return false;  // cycle
        if (cs->parentId == parent) return false;
        cs->parentId = parent;
        return true;
    }
    bool ClearParent(uint64_t child) {
        Shape* cs = FindShape(child);
        if (!cs || cs->parentId == 0) return false;
        cs->parentId = 0;
        return true;
    }

    // The 2D cursor (doc-units): transform pivot + spawn point (Blender-style).
    Vec2 cursor{0, 0};
    // 2D cursor ORIENTATION (radians). Rotating the cursor (R while the 2D Cursor
    // tool is active) turns the "Cursor" transform orientation's axes. 0 = aligned
    // with the document axes.
    float cursorRotation = 0.0f;

private:
    uint64_t              nextId_ = kProjectRootId + 1;  // ids above the reserved root
    std::vector<uint64_t> selection_;   // selected shape ids (order = pick order)
    uint64_t              active_ = 0;  // active = last selected
    uint64_t              activeGroup_ = 0;  // active layer group (Properties shows it); 0 = none
    uint64_t              activePage_ = 0;  // active page (Shift+A target); 0 = none
    std::vector<VertRef>  vertSel_;     // edit-mode selected vertices
    VertRef               activeVert_;  // active = last selected vertex
    std::vector<HandleRef> handleSel_;  // selected Bézier handles (coexist with verts)
    HandleRef             activeHandle_; // active = last selected handle
    std::vector<MarkRef>  markSel_;     // selected line marks (Line-Mark tool)
    MarkRef               activeMark_;  // active = last selected mark
};

} // namespace Renderer

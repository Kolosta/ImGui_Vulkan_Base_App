#include "Application.h"

#include "OutlinerRowLayout.h"
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner drag & drop (legacy feature set, on the Ink model):
//   • Collections view — organisation ONLY: an object drops INTO a collection
//     (single-collection move + un-parent). The target resolves from ANYWHERE
//     inside that collection — hovering a member row highlights + targets its
//     ENCLOSING collection (object parenting is NOT a drop; it goes through
//     the viewport menu / shortcuts). The project-root row (and the space
//     below the rows) pulls objects out of any collection/parent. Collections
//     drag too: edge zones reorder among siblings, the centre nests.
//   • Layers view — drag an object between two rows (grey insert line) to
//     reorder it in the stack; onto a GROUP row's centre to move into it.
//  Drop targets cover the FULL zebra stripe (custom rects tiling at the row
//  pitch — no dead gaps between rows). Every drop is one undoable command.
//  Multi-drag: dragging a row that is part of the selection drags the whole
//  selection (legacy rule).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

constexpr const char* kObjPayload  = "OUTLINER_OBJ";    // Ink::NodeId
constexpr const char* kCollPayload = "OUTLINER_COLL";   // collection id
constexpr const char* kModPayload  = "OUTLINER_MOD";    // ModPayload

// A dragged modifier row: the owning object + the stack index.
struct ModPayload {
    Ink::NodeId obj = Ink::kNullNode;
    int         index = -1;
};

// Per-object membership + parenting snapshot for undo.
struct ObjPlacement {
    Ink::NodeId id = Ink::kNullNode;
    Ink::NodeId parentId = Ink::kNullNode;   // object parenting (Lot 7)
    Ink::Transform2D transform;
    std::vector<Ink::NodeId> collections;    // memberships at capture time
};

ObjPlacement CapturePlacement(Ink::Document& doc, Ink::NodeId id) {
    ObjPlacement p;
    p.id = id;
    if (const Ink::Node* n = doc.Find(id)) {
        p.parentId = n->parentId;
        p.transform = n->transform;
    }
    for (const Ink::Collection& c : doc.Collections())
        if (std::find(c.members.begin(), c.members.end(), id) != c.members.end())
            p.collections.push_back(c.id);
    return p;
}

void RestorePlacement(Ink::Document& doc, const ObjPlacement& p) {
    if (!doc.Find(p.id)) return;
    // Memberships: clear then re-add the captured set.
    for (const Ink::Collection& c : doc.Collections())
        doc.RemoveFromCollection(c.id, p.id);
    for (Ink::NodeId cid : p.collections) doc.AddToCollection(cid, p.id);
    // Parenting: restore the exact transform (SetParent(keepWorld=false) sets
    // the link; the captured local transform restores the exact placement).
    if (p.parentId != Ink::kNullNode) doc.SetParent(p.id, p.parentId, false);
    else doc.ClearParent(p.id, false);
    doc.SetTransform(p.id, p.transform);
}
} // namespace

void Application::OutlinerDragSet(Ink::NodeId trigger,
                                  std::vector<Ink::NodeId>& objs,
                                  std::vector<Ink::NodeId>& colls) const {
    objs.clear();
    colls.clear();
    if (!project_.document) return;
    const Ink::Document& doc = *project_.document;
    const bool inSel = edit_.IsSelected(trigger) ||
                       (outlinerCur_ && outlinerCur_->RowSelected(trigger));
    if (inSel) {
        for (Ink::NodeId id : edit_.selection)
            if (doc.Find(id)) objs.push_back(id);
        if (outlinerCur_)
            for (std::uint64_t id : outlinerCur_->sel)
                if (doc.FindCollection(id)) colls.push_back(id);
        return;
    }
    if (doc.Find(trigger)) objs.push_back(trigger);
    else if (doc.FindCollection(trigger)) colls.push_back(trigger);
}

std::vector<Ink::NodeId> Application::OutlinerDraggedIds(Ink::NodeId trigger) const {
    std::vector<Ink::NodeId> objs, colls;
    OutlinerDragSet(trigger, objs, colls);
    if (objs.empty() && colls.empty()) return { trigger };
    return objs;
}

// Where a collection sits in the tree, enough to put it back exactly.
namespace {
struct CollPlace { Ink::NodeId id, parent; int index; };
}

// ONE drop, ONE undo step. The user made a single gesture; unwinding it must
// not take several. So nothing here delegates to the single-kind operations
// (each of which pushes its own command) — the whole mixture is captured
// before, applied, captured after, and pushed as one.
void Application::OutlinerDropMixed(const std::vector<Ink::NodeId>& objs,
                                    const std::vector<Ink::NodeId>& colls,
                                    Ink::NodeId targetColl) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;

    // A collection may not be dropped into itself, nor into anything it
    // contains — MoveCollection refuses both — and a selection that includes
    // the destination simply leaves it where it is.
    std::vector<Ink::NodeId> moveColls;
    for (Ink::NodeId c : colls)
        if (c != targetColl && doc.FindCollection(c) &&
            OutlinerStructureAllowed({ c }, targetColl))
            moveColls.push_back(c);
    std::vector<Ink::NodeId> moveObjs;
    for (Ink::NodeId id : objs)
        if (doc.Find(id) && OutlinerStructureAllowed({ id }, targetColl))
            moveObjs.push_back(id);
    if (moveObjs.empty() && moveColls.empty()) return;

    auto captureColl = [&](Ink::NodeId id) {
        CollPlace p{ id, Ink::kNullNode, 0 };
        for (const Ink::Collection& c : doc.Collections()) {
            const auto it = std::find(c.childCollections.begin(),
                                      c.childCollections.end(), id);
            if (it != c.childCollections.end()) {
                p.parent = c.id;
                p.index = (int)(it - c.childCollections.begin());
                return p;
            }
        }
        int t = 0;
        for (const Ink::Collection& c : doc.Collections()) {
            if (doc.IsChildCollection(c.id)) continue;
            if (c.id == id) { p.index = t; break; }
            ++t;
        }
        return p;
    };

    std::vector<ObjPlacement> objBefore, objAfter;
    std::vector<CollPlace>    collBefore, collAfter;
    for (Ink::NodeId id : moveObjs)  objBefore.push_back(CapturePlacement(doc, id));
    for (Ink::NodeId c  : moveColls) collBefore.push_back(captureColl(c));

    for (Ink::NodeId id : moveObjs) {
        // Single-collection membership, and out of any object parent.
        for (const Ink::Collection& c : doc.Collections())
            doc.RemoveFromCollection(c.id, id);
        if (targetColl != Ink::kNullNode) doc.AddToCollection(targetColl, id);
        doc.ClearParent(id, /*keepWorld=*/true);
    }
    for (Ink::NodeId c : moveColls) doc.MoveCollection(c, targetColl);

    for (Ink::NodeId id : moveObjs)  objAfter.push_back(CapturePlacement(doc, id));
    for (Ink::NodeId c  : moveColls) collAfter.push_back(captureColl(c));

    auto apply = [](Ink::Document& d, const std::vector<ObjPlacement>& o,
                    const std::vector<CollPlace>& c) {
        // Collections first: an object's membership names a collection, so the
        // tree it belongs to has to exist in its old shape before the objects
        // are put back into it.
        for (const CollPlace& p : c) {
            d.MoveCollection(p.id, p.parent);
            d.ReorderCollection(p.id, p.index);
        }
        for (const ObjPlacement& p : o) RestorePlacement(d, p);
    };
    const char* name = targetColl != Ink::kNullNode ? "Move to Collection"
                                                    : "Move to Project Root";
    PushDocCommand(name,
        [objBefore, collBefore, apply](Ink::Document& d) {
            apply(d, objBefore, collBefore);
        },
        [objAfter, collAfter, apply](Ink::Document& d) {
            apply(d, objAfter, collAfter);
        });

    // What moved, and where to. The command above stores none of this — it is
    // the feed's business, not the undo stack's.
    InfoFields f;
    const Ink::Collection* dst = doc.FindCollection(targetColl);
    f.push_back({ "to", dst ? (dst->name.empty() ? "(collection)" : dst->name)
                            : "Project root" });
    if (!moveObjs.empty())  f.push_back({ "objects", DescribeNodes(moveObjs) });
    if (!moveColls.empty()) f.push_back({ "collections", DescribeCollections(moveColls) });
    if (moveObjs.size() > 1) {
        std::string names;
        for (std::size_t i = 0; i < moveObjs.size() && i < 12; ++i) {
            if (i) names += ", ";
            const Ink::Node* n = doc.Find(moveObjs[i]);
            names += n && !n->name.empty() ? n->name : "(unnamed)";
        }
        if (moveObjs.size() > 12) names += ", …";
        f.push_back({ "object names", names });
    }
    if (moveColls.size() > 1) {
        std::string names;
        for (std::size_t i = 0; i < moveColls.size() && i < 12; ++i) {
            if (i) names += ", ";
            const Ink::Collection* c = doc.FindCollection(moveColls[i]);
            names += c && !c->name.empty() ? c->name : "(unnamed)";
        }
        if (moveColls.size() > 12) names += ", …";
        f.push_back({ "collection names", names });
    }
    f.push_back({ "membership", "single (previous memberships cleared)" });
    f.push_back({ "object parenting", "cleared, world position kept" });
    LogInfoAction(name,
                  targetColl != Ink::kNullNode ? "outliner.move_to_collection"
                                               : "outliner.move_to_root", f);
}

// ── Drop operations (each = one undo command) ────────────────────────────────

// Module policy gate for STRUCTURAL outliner drags. With an active module,
// every dragged id is submitted to AllowReparent(id, target) — IOF keeps its
// symbols inside their fixed print-layer groups and whitelists only its
// editable layout layers. Without a module, lockOutlinerTree (a capability a
// module could leave armed) refuses everything; Classic allows everything.
bool Application::OutlinerStructureAllowed(const std::vector<Ink::NodeId>& ids,
                                           Ink::NodeId target) const {
    if (activeModule_) {
        for (Ink::NodeId id : ids)
            if (!activeModule_->AllowReparent(id, target)) return false;
        return true;
    }
    return !activeCapabilities_.lockOutlinerTree;
}

void Application::OutlinerDropParentTo(const std::vector<Ink::NodeId>& ids,
                                       Ink::NodeId parent) {
    if (!project_.document || parent == Ink::kNullNode) return;
    if (!OutlinerStructureAllowed(ids, parent)) return;
    Ink::Document& doc = *project_.document;
    std::vector<ObjPlacement> before;
    std::vector<Ink::NodeId> done;
    for (Ink::NodeId id : ids) {
        if (id == parent) continue;
        before.push_back(CapturePlacement(doc, id));
        if (doc.SetParent(id, parent, /*keepWorld=*/true)) done.push_back(id);
        else before.pop_back();   // refused (cycle) — drop the snapshot
    }
    if (done.empty()) return;
    std::vector<ObjPlacement> after;
    for (Ink::NodeId id : done) after.push_back(CapturePlacement(doc, id));
    PushDocCommand("Parent",
        [before](Ink::Document& d) { for (const auto& p : before) RestorePlacement(d, p); },
        [after](Ink::Document& d)  { for (const auto& p : after)  RestorePlacement(d, p); });
    LogInfoAction("Parent");
}

void Application::OutlinerDropToCollection(const std::vector<Ink::NodeId>& ids,
                                           Ink::NodeId coll) {
    if (!project_.document || !project_.document->FindCollection(coll)) return;
    if (!OutlinerStructureAllowed(ids, coll)) return;
    Ink::Document& doc = *project_.document;
    std::vector<ObjPlacement> before;
    for (Ink::NodeId id : ids) before.push_back(CapturePlacement(doc, id));
    for (Ink::NodeId id : ids) {
        // Single-collection move semantics + pulled out of any object parent.
        for (const Ink::Collection& c : doc.Collections())
            doc.RemoveFromCollection(c.id, id);
        doc.AddToCollection(coll, id);
        doc.ClearParent(id, /*keepWorld=*/true);
    }
    std::vector<ObjPlacement> after;
    for (Ink::NodeId id : ids) after.push_back(CapturePlacement(doc, id));
    PushDocCommand("Move to Collection",
        [before](Ink::Document& d) { for (const auto& p : before) RestorePlacement(d, p); },
        [after](Ink::Document& d)  { for (const auto& p : after)  RestorePlacement(d, p); });
    LogInfoAction("Move to Collection");
}

void Application::OutlinerDropToRoot(const std::vector<Ink::NodeId>& ids) {
    if (!project_.document) return;
    if (!OutlinerStructureAllowed(ids, Ink::kNullNode)) return;
    Ink::Document& doc = *project_.document;
    std::vector<ObjPlacement> before;
    for (Ink::NodeId id : ids) before.push_back(CapturePlacement(doc, id));
    bool changed = false;
    for (Ink::NodeId id : ids) {
        for (const Ink::Collection& c : doc.Collections()) {
            if (std::find(c.members.begin(), c.members.end(), id) != c.members.end())
                changed = true;
            doc.RemoveFromCollection(c.id, id);
        }
        if (const Ink::Node* n = doc.Find(id); n && n->parentId != Ink::kNullNode) {
            doc.ClearParent(id, /*keepWorld=*/true);
            changed = true;
        }
    }
    if (!changed) return;
    std::vector<ObjPlacement> after;
    for (Ink::NodeId id : ids) after.push_back(CapturePlacement(doc, id));
    PushDocCommand("Unparent / Uncollection",
        [before](Ink::Document& d) { for (const auto& p : before) RestorePlacement(d, p); },
        [after](Ink::Document& d)  { for (const auto& p : after)  RestorePlacement(d, p); });
    LogInfoAction("Unparent");
}

// Affinity CLIP / MASK drop: nest each dragged node under `target` (as a clip
// child, or a mask child when `asMask`), preserving its world position, as ONE
// undoable command. Capturing parent + index + transform + isMask before and
// after makes Ctrl+Z restore the exact previous placement (the earlier version
// did a bare MoveTo/SetMask with no command, so undo unwound unrelated edits).
void Application::OutlinerDropClipMask(const std::vector<Ink::NodeId>& ids,
                                       Ink::NodeId target, bool asMask) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    if (!doc.Find(target)) return;
    if (!OutlinerStructureAllowed(ids, target)) return;

    struct Place { Ink::NodeId id; Ink::NodeId parent; int index;
                   Ink::Transform2D t; bool mask; };
    auto capture = [&](Ink::NodeId id) {
        const Ink::Node* n = doc.Find(id);
        return Place{ id, n->parent != Ink::kNullNode ? n->parent : n->page,
                      doc.IndexInParent(id), n->transform, n->isMask };
    };
    std::vector<Place> before, after;
    std::vector<Ink::NodeId> done;
    for (Ink::NodeId id : ids) {
        if (id == target || !doc.Find(id)) continue;
        before.push_back(capture(id));
        if (doc.MoveTo(id, target, -1)) {   // world position preserved
            doc.SetMask(id, asMask);
            done.push_back(id);
        } else {
            before.pop_back();              // refused (cycle / invalid)
        }
    }
    if (done.empty()) return;
    for (Ink::NodeId id : done) after.push_back(capture(id));
    auto apply = [](Ink::Document& d, const std::vector<Place>& v) {
        for (const Place& p : v) {
            d.MoveTo(p.id, p.parent, p.index);
            d.SetTransform(p.id, p.t);
            d.SetMask(p.id, p.mask);
        }
    };
    PushDocCommand(asMask ? "Mask Layer" : "Clip Layer",
        [before, apply](Ink::Document& d) { apply(d, before); },
        [after, apply](Ink::Document& d)  { apply(d, after); });
    LogInfoAction(asMask ? "Mask Layer" : "Clip Layer");
}

void Application::OutlinerDropReorder(const std::vector<Ink::NodeId>& ids,
                                      Ink::NodeId target, bool above) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* tn = doc.Find(target);
    if (!tn) return;
    const Ink::NodeId destParent =
        tn->parent != Ink::kNullNode ? tn->parent : tn->page;
    if (!OutlinerStructureAllowed(ids, destParent)) return;

    // Capture the full sibling orders for undo (simple + exact).
    struct Order { Ink::NodeId id; Ink::NodeId parent; int index; Ink::Transform2D t; };
    std::vector<Order> before;
    for (Ink::NodeId id : ids) {
        const Ink::Node* n = doc.Find(id);
        if (!n) continue;
        before.push_back({ id, n->parent != Ink::kNullNode ? n->parent : n->page,
                           doc.IndexInParent(id), n->transform });
    }
    // Rows list top-of-stack first, so "visually above the target" = just
    // AFTER it in painter order; "below" = just before it.
    for (Ink::NodeId id : ids) {
        if (id == target) continue;
        const int ti = doc.IndexInParent(target);
        doc.MoveTo(id, destParent, above ? ti + 1 : ti);
    }
    std::vector<Order> after;
    for (Ink::NodeId id : ids) {
        const Ink::Node* n = doc.Find(id);
        if (!n) continue;
        after.push_back({ id, n->parent != Ink::kNullNode ? n->parent : n->page,
                          doc.IndexInParent(id), n->transform });
    }
    auto apply = [](Ink::Document& d, const std::vector<Order>& v) {
        for (const Order& o : v) {
            d.MoveTo(o.id, o.parent, o.index);
            d.SetTransform(o.id, o.t);
        }
    };
    PushDocCommand("Reorder",
        [before, apply](Ink::Document& d) { apply(d, before); },
        [after, apply](Ink::Document& d)  { apply(d, after); });
    LogInfoAction("Reorder");
}

void Application::OutlinerRemoveFromCollections(const std::vector<Ink::NodeId>& ids) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    std::vector<ObjPlacement> before;
    for (Ink::NodeId id : ids) before.push_back(CapturePlacement(doc, id));
    bool changed = false;
    for (Ink::NodeId id : ids)
        for (const Ink::Collection& c : doc.Collections())
            if (std::find(c.members.begin(), c.members.end(), id) != c.members.end()) {
                doc.RemoveFromCollection(c.id, id);
                changed = true;
            }
    if (!changed) return;
    std::vector<ObjPlacement> after;
    for (Ink::NodeId id : ids) after.push_back(CapturePlacement(doc, id));
    PushDocCommand("Remove from Collections",
        [before](Ink::Document& d) { for (const auto& p : before) RestorePlacement(d, p); },
        [after](Ink::Document& d)  { for (const auto& p : after)  RestorePlacement(d, p); });
    LogInfoAction("Remove from Collections");
}

void Application::OutlinerUnparent(const std::vector<Ink::NodeId>& ids) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    std::vector<ObjPlacement> before;
    bool changed = false;
    for (Ink::NodeId id : ids) {
        const Ink::Node* n = doc.Find(id);
        if (!n || n->parentId == Ink::kNullNode) continue;
        before.push_back(CapturePlacement(doc, id));
        doc.ClearParent(id, /*keepWorld=*/true);
        changed = true;
    }
    if (!changed) return;
    std::vector<ObjPlacement> after;
    for (const auto& p : before) after.push_back(CapturePlacement(doc, p.id));
    PushDocCommand("Unparent",
        [before](Ink::Document& d) { for (const auto& p : before) RestorePlacement(d, p); },
        [after](Ink::Document& d)  { for (const auto& p : after)  RestorePlacement(d, p); });
    LogInfoAction("Unparent");
}

// ── Row source + target ───────────────────────────────────────────────────────

namespace {
// Drop-zone split (Blender): the top / bottom quarters of a row are INSERT
// zones (a grey line between the zebra rows shows where the item lands); the
// middle is the INTO zone (the row's selection BAND — not the full stripe —
// highlights with the notice-orange outline over a grey fill).
enum class DropZone { Into, Above, Below };

DropZone ZoneAt(const UI::ListRow& lr, bool edgesAllowed) {
    if (!edgesAllowed) return DropZone::Into;
    const float my = ImGui::GetIO().MousePos.y;
    const float h = lr.StripeBottom() - lr.StripeTop();
    if (my < lr.StripeTop() + h * 0.25f) return DropZone::Above;
    if (my > lr.StripeBottom() - h * 0.25f) return DropZone::Below;
    return DropZone::Into;
}

void DrawInsertLine(const UI::ListRow& lr, bool above) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = above ? lr.StripeTop() : lr.StripeBottom();
    const ImVec4 grey = [] {
        try { return DS::DesignSystem::Instance().GetColor(Tok::S_Color_Text_Subtle); }
        catch (...) { return ImVec4(0.6f, 0.6f, 0.6f, 1); }
    }();
    dl->AddLine(ImVec2(lr.BandLeft(), y), ImVec2(lr.BandRight(), y),
                ImGui::ColorConvertFloat4ToU32(grey), 2.0f);
}

}  // namespace

// The drop frame: grey fill + notice-orange contour. On Application because the
// row drawing needs it too - the preview square's frame can only be placed once
// the square itself has been laid out.
void Application::OutlinerDrawDropFrame(ImVec2 a, ImVec2 b) const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto& ds = DS::DesignSystem::Instance();
    ImVec4 fill(0.5f, 0.5f, 0.5f, 0.20f);
    ImVec4 line(0.95f, 0.55f, 0.15f, 1.0f);
    try { fill = ds.GetColor(Tok::S_Color_Background_Layer2); fill.w = 0.55f; } catch (...) {}
    try { line = ds.GetColor(Tok::S_Color_Notice_Default); } catch (...) {}
    float rnd = 4.0f;
    try { rnd = ds.GetFloat(Tok::S_CornerRadius_Control) * ds.GetGlobalScale(); } catch (...) {}
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(fill), rnd);
    dl->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(line), rnd, 0, 1.5f);
}


void Application::OutlinerRowDragDrop(const OutlinerRow& row, const UI::ListRow& lr) {
    if (!project_.document || !outlinerCur_ || outlinerSuppressInput_) return;
    Ink::Document& doc = *project_.document;
    const bool collectionsMode =
        outlinerCur_->display == OutlinerDisplayMode::Collections;

    // A drag that carries the SELECTION carries several rows, and no single row
    // can stand in for them. Only a one-row drag turns into the moving row (the
    // whole point of it is that the thing you grabbed is the thing you see); a
    // multi-row drag keeps the older language — a preview listing what is being
    // carried, plus the insert line / collection outline for the destination.
    std::vector<Ink::NodeId> dragObjs, dragColls;
    OutlinerDragSet(row.id, dragObjs, dragColls);
    const bool multi = (row.kind == OutlinerRow::Kind::Object ||
                        row.kind == OutlinerRow::Kind::CollectionHeader) &&
                       dragObjs.size() + dragColls.size() > 1;
    const ImGuiDragDropFlags kSrcFlags =
        ImGuiDragDropFlags_SourceAllowNullID |
        (multi ? 0 : ImGuiDragDropFlags_SourceNoPreviewTooltip);
    auto grab = [&] {
        if (outlinerDrag_ && !multi) outlinerDrag_->SetSource(row.flatIndex);
    };

    if (row.kind == OutlinerRow::Kind::Object) {
        if (ImGui::BeginDragDropSource(kSrcFlags)) {
            ImGui::SetDragDropPayload(kObjPayload, &row.id, sizeof row.id);
            grab();
            if (multi) OutlinerDragTooltip(dragObjs, dragColls);
            ImGui::EndDragDropSource();
        }
    } else if (row.kind == OutlinerRow::Kind::CollectionHeader && collectionsMode) {
        if (ImGui::BeginDragDropSource(kSrcFlags)) {
            ImGui::SetDragDropPayload(kCollPayload, &row.id, sizeof row.id);
            grab();
            if (multi) OutlinerDragTooltip(dragObjs, dragColls);
            ImGui::EndDragDropSource();
        }
    } else if (row.kind == OutlinerRow::Kind::Modifier) {
        // A modifier drags as a COPY payload — droppable only onto a
        // compatible object (nothing else accepts it, so dropping anywhere
        // else simply does nothing).
        if (ImGui::BeginDragDropSource(kSrcFlags)) {
            ModPayload mp{ row.id, row.modIndex };
            ImGui::SetDragDropPayload(kModPayload, &mp, sizeof mp);
            grab();
            ImGui::EndDragDropSource();
        }
    }

    // Target: an EXPLICIT full-stripe rect (edge to edge, tiling at the row
    // pitch) so consecutive rows leave no dead gap while dragging.
    const ImRect stripe(ImVec2(ol::RowLeft(), lr.StripeTop()),
                        ImVec2(ol::RowRight(), lr.StripeBottom()));
    const ImGuiID targetId = ImGui::GetID((void*)(uintptr_t)
        (row.id ^ ((std::uint64_t)row.kind << 56) ^ 0xD5A60000ull));
    if (!ImGui::BeginDragDropTargetCustom(stripe, targetId)) return;
    constexpr ImGuiDragDropFlags kPeek =
        ImGuiDragDropFlags_AcceptBeforeDelivery |
        ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

    // Band rect of ANOTHER flat row (the enclosing collection's header) from
    // the draw-loop geometry published by RenderOutliner.
    auto bandRectOf = [&](int flatIndex) {
        const float top = ImGui::GetWindowPos().y - ImGui::GetScrollY() +
                          outlinerRowsStartY_ +
                          (float)flatIndex * outlinerStripeH_ + 1.0f;
        return ImRect(ImVec2(lr.BandLeft(), top),
                      ImVec2(lr.BandRight(), top + outlinerStripeH_ - 2.0f));
    };

    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kObjPayload, kPeek)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        if (collectionsMode) {
            // Collections view: an object drop is ALWAYS "into a collection".
            // Hovering any row inside a collection targets that collection
            // (its header row highlights); the root row / loose rows target
            // the project root (pull out of every collection + parent).
            Ink::NodeId targetColl = Ink::kNullNode;
            int headerRow = 0;                       // root row by default
            if (row.kind == OutlinerRow::Kind::CollectionHeader) {
                targetColl = row.id;
                headerRow = row.flatIndex;
            } else if (row.kind == OutlinerRow::Kind::Object ||
                       row.kind == OutlinerRow::Kind::Modifier ||
                       row.kind == OutlinerRow::Kind::LinkedData) {
                targetColl = row.ownerColl;
                headerRow = row.ownerRow >= 0 ? row.ownerRow : 0;
            }
            if (outlinerRows_) {
                const ImRect r = bandRectOf(headerRow);
                OutlinerDrawDropFrame(r.Min, r.Max);
            }
            // The collection keeps its outline — that IS the target, and the
            // drop does not let you choose a rank inside it. The gap still
            // opens at the row the object will really land on, which the
            // alphabetical member order decides (not the end of the list).
            if (outlinerDrag_) {
                const int b = OutlinerCollectionDropRow(targetColl, headerRow,
                                                        dragged);
                if (b >= 0) {
                    outlinerDrag_->SetLandingAtBoundary(b);
                    // One level under the collection it is joining: the guides
                    // need the destination depth, not the one it is leaving.
                    if (outlinerRows_ && headerRow < (int)outlinerRows_->size())
                        outlinerDropDepth_ =
                            (*outlinerRows_)[(std::size_t)headerRow].depth + 1;
                } else {
                    outlinerDrag_->SetNoGap();
                }
            }
            if (p->IsDelivery()) {
                std::vector<Ink::NodeId> o2, c2;
                OutlinerDragSet(dragged, o2, c2);
                OutlinerDropMixed(o2, c2, targetColl);
            }
        } else if (row.kind == OutlinerRow::Kind::Object) {
            // Layers view (Affinity semantics): drop the dragged node(s)
            //   • onto this row's PREVIEW SQUARE → MASK child (masks this
            //     node's content); highlight only the preview square.
            //   • onto the row's CENTRE → CLIP child (nested INSIDE, clipped
            //     to this node — any node, not only groups).
            //   • onto an EDGE → reorder among siblings (grey insert line).
            // Preview-square rect, computed from the row geometry (matches the
            // Layers-view layout: dot gutter + depth indents + chevron slot).
            const float sq = lr.RowH() * 0.86f;
            const float sqX0 = lr.ContentX() + ol::DotGutterW() +
                               (float)(row.depth + 1) * ol::ChevronSlotW();
            const float sqY0 = lr.RowTop() + (lr.RowH() - sq) * 0.5f;
            outlinerLayerPreviewMin_ = ImVec2(sqX0, sqY0);
            outlinerLayerPreviewMax_ = ImVec2(sqX0 + sq, sqY0 + sq);
            outlinerLayerPreviewValid_ = true;
            const bool onSquare =
                ImGui::GetIO().MousePos.x >= outlinerLayerPreviewMin_.x &&
                ImGui::GetIO().MousePos.x <= outlinerLayerPreviewMax_.x &&
                ImGui::GetIO().MousePos.y >= lr.StripeTop() &&
                ImGui::GetIO().MousePos.y <= lr.StripeBottom();
            const DropZone z = ZoneAt(lr, /*edgesAllowed=*/!onSquare);
            const bool self = (dragged == row.id);
            // Only an EDGE drop is a reorder; nesting as a clip or a mask is a
            // change of parent, and the list has no slot to open for it.
            if (outlinerDrag_) {
                if (onSquare || z == DropZone::Into || self) {
                    outlinerDrag_->SetNoGap();
                } else {
                    outlinerDrag_->SetLandingAtBoundary(
                        row.flatIndex + (z == DropZone::Below ? 1 : 0));
                    outlinerDropDepth_ = row.depth;   // becomes its sibling
                }
            }
            if (onSquare && !self) {
                // Named, not drawn: the square's true rect is only known once
                // the row lays it out, a few calls from now.
                outlinerMaskFrameRow_ = row.id;
            } else if (z == DropZone::Into && !self) {
                OutlinerDrawDropFrame(ImVec2(lr.BandLeft(), lr.RowTop()),
                                  ImVec2(lr.BandRight(), lr.RowTop() + lr.RowH()));
            } else if (!outlinerDrag_ || !outlinerDrag_->Active()) {
                // The opened gap already says where the row lands; the line is
                // only needed when nothing moves (a multi-row drag).
                DrawInsertLine(lr, z != DropZone::Below);
            }
            if (p->IsDelivery() && !self) {
                const auto ids = OutlinerDraggedIds(dragged);
                if (onSquare) {
                    OutlinerDropClipMask(ids, row.id, /*asMask=*/true);
                } else if (z == DropZone::Into) {
                    OutlinerDropClipMask(ids, row.id, /*asMask=*/false);
                } else {
                    // Rows list top-of-stack first: visually ABOVE the target
                    // = AFTER it in painter order.
                    OutlinerDropReorder(ids, row.id, z != DropZone::Below);
                }
            }
        }
    }

    // A dragged MODIFIER: droppable only onto a compatible object — a PATH
    // row, or anywhere inside an expanded object (its modifier / linked-data
    // child rows), which highlights and targets that object. Dropping copies
    // the modifier below the target's own stack. Everything else ignores the
    // payload (the drop does nothing).
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kModPayload, kPeek)) {
        const ModPayload mp = *(const ModPayload*)p->Data;
        Ink::NodeId targetObj = Ink::kNullNode;
        int objRow = -1;
        if (row.kind == OutlinerRow::Kind::Object) {
            targetObj = row.id; objRow = row.flatIndex;
        } else if (row.kind == OutlinerRow::Kind::Modifier ||
                   row.kind == OutlinerRow::Kind::LinkedData) {
            targetObj = row.id; objRow = row.objRow;
        }
        if (outlinerDrag_) outlinerDrag_->SetNoGap();
        const Ink::Node* tn = doc.Find(targetObj);
        if (tn && tn->kind == Ink::NodeKind::Path) {
            if (outlinerRows_ && objRow >= 0) {
                const ImRect r = bandRectOf(objRow);
                OutlinerDrawDropFrame(r.Min, r.Max);
            } else {
                OutlinerDrawDropFrame(ImVec2(lr.BandLeft(), lr.RowTop()),
                                  ImVec2(lr.BandRight(), lr.RowTop() + lr.RowH()));
            }
            if (p->IsDelivery())
                OutlinerDropModifierCopy(mp.obj, mp.index, targetObj);
        }
    }

    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kCollPayload, kPeek)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        // Module policy: a module can freeze the collection hierarchy too.
        const bool collAllowed = OutlinerStructureAllowed({ dragged }, row.id);
        if (collAllowed && row.kind == OutlinerRow::Kind::CollectionHeader) {
            const DropZone z = ZoneAt(lr, /*edgesAllowed=*/true);
            if (outlinerDrag_) {
                if (z == DropZone::Into) {
                    // Nesting: it comes to rest above the target's objects,
                    // under whatever sub-collections it already has.
                    const int b = OutlinerCollectionNestRow(row.id, row.flatIndex);
                    if (b >= 0) {
                        outlinerDrag_->SetLandingAtBoundary(b);
                        outlinerDropDepth_ = row.depth + 1;
                    } else {
                        outlinerDrag_->SetNoGap();
                    }
                } else if (z == DropZone::Below) {
                    // Below an UNFOLDED collection is below everything it
                    // shows: a slot opening between the header and its first
                    // child would read as landing INSIDE it, which is the one
                    // thing this edge does not mean.
                    outlinerDrag_->SetLandingAtBoundary(
                        OutlinerSubtreeEnd(row.flatIndex) + 1);
                    outlinerDropDepth_ = row.depth;
                } else {
                    outlinerDrag_->SetLandingAtBoundary(row.flatIndex);
                    outlinerDropDepth_ = row.depth;
                }
            }
            if (z == DropZone::Into) OutlinerDrawDropFrame(ImVec2(lr.BandLeft(), lr.RowTop()),
                                  ImVec2(lr.BandRight(), lr.RowTop() + lr.RowH()));
            else if (!outlinerDrag_ || !outlinerDrag_->Active())
                DrawInsertLine(lr, z == DropZone::Above);
            if (p->IsDelivery() && dragged != row.id) {
                std::vector<Ink::NodeId> o2, c2;
                OutlinerDragSet(dragged, o2, c2);
                if (z == DropZone::Into) {
                    OutlinerDropMixed(o2, c2, row.id);
                } else {
                    // Insert as a SIBLING of the target, above or below it.
                    // First match the target's parent, then take its slot.
                    Ink::NodeId parent = Ink::kNullNode;
                    for (const Ink::Collection& c : doc.Collections())
                        if (std::find(c.childCollections.begin(),
                                      c.childCollections.end(),
                                      row.id) != c.childCollections.end()) {
                            parent = c.id; break;
                        }
                    doc.MoveCollection(dragged, parent);
                    // Sibling index of the target within its parent / top level.
                    int idx = 0;
                    if (parent != Ink::kNullNode) {
                        const Ink::Collection* pc = doc.FindCollection(parent);
                        for (int i = 0; i < (int)pc->childCollections.size(); ++i)
                            if (pc->childCollections[i] == row.id) { idx = i; break; }
                    } else {
                        int t = 0;
                        for (const Ink::Collection& c : doc.Collections()) {
                            if (doc.IsChildCollection(c.id)) continue;
                            if (c.id == row.id) { idx = t; break; }
                            ++t;
                        }
                    }
                    doc.ReorderCollection(dragged,
                                          z == DropZone::Above ? idx : idx + 1);
                    LogInfoAction("Reorder Collection");
                    // Objects carried along join the level the collection just
                    // landed on — the same place, read for an object.
                    if (!o2.empty()) OutlinerDropMixed(o2, {}, parent);
                }
            }
        } else if (collAllowed && row.kind == OutlinerRow::Kind::ProjectRoot) {
            if (outlinerDrag_) outlinerDrag_->SetNoGap();
            OutlinerDrawDropFrame(ImVec2(lr.BandLeft(), lr.RowTop()),
                                  ImVec2(lr.BandRight(), lr.RowTop() + lr.RowH()));   // un-nest to the top level
            if (p->IsDelivery()) {
                std::vector<Ink::NodeId> o2, c2;
                OutlinerDragSet(dragged, o2, c2);
                OutlinerDropMixed(o2, c2, Ink::kNullNode);
            }
        } else if (collAllowed && collectionsMode &&
                   (row.kind == OutlinerRow::Kind::Object ||
                    row.kind == OutlinerRow::Kind::Modifier ||
                    row.kind == OutlinerRow::Kind::LinkedData)) {
            // Anywhere inside a collection nests INTO that collection (same
            // rule as objects — no dead rows while dragging a collection).
            const Ink::NodeId targetColl = row.ownerColl;
            const int headerRow = row.ownerRow >= 0 ? row.ownerRow : 0;
            // Hovering the OBJECTS of a collection still nests into it, and the
            // slot is the same one: above them all.
            if (outlinerDrag_) {
                const int b = targetColl != Ink::kNullNode
                    ? OutlinerCollectionNestRow(targetColl, headerRow) : -1;
                if (b >= 0) {
                    outlinerDrag_->SetLandingAtBoundary(b);
                    if (outlinerRows_ && headerRow < (int)outlinerRows_->size())
                        outlinerDropDepth_ =
                            (*outlinerRows_)[(std::size_t)headerRow].depth + 1;
                } else {
                    outlinerDrag_->SetNoGap();
                }
            }
            if (outlinerRows_) {
                const ImRect r = bandRectOf(headerRow);
                OutlinerDrawDropFrame(r.Min, r.Max);
            }
            if (p->IsDelivery() && dragged != targetColl) {
                std::vector<Ink::NodeId> o2, c2;
                OutlinerDragSet(dragged, o2, c2);
                OutlinerDropMixed(o2, c2, targetColl);
            }
        }
    }
    ImGui::EndDragDropTarget();
}

void Application::OutlinerDropModifierCopy(Ink::NodeId srcObj, int modIndex,
                                           Ink::NodeId dstObj) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* src = doc.Find(srcObj);
    const Ink::Node* dst = doc.Find(dstObj);
    if (!src || !dst || dst->kind != Ink::NodeKind::Path) return;
    if (modIndex < 0 || modIndex >= (int)src->modifiers.size()) return;
    Ink::Modifier copy = src->modifiers[(std::size_t)modIndex];
    // An AlongPath copy landing on the same object it instances would
    // self-reference — refuse that one combination.
    if (copy.kind == Ink::ModifierKind::AlongPath && copy.motifRef == dstObj)
        return;
    const std::vector<Ink::Modifier> before = dst->modifiers;
    std::vector<Ink::Modifier> after = before;
    after.push_back(std::move(copy));
    doc.SetModifiers(dstObj, after);
    PushDocCommand("Copy Modifier",
        [dstObj, before](Ink::Document& d) { d.SetModifiers(dstObj, before); },
        [dstObj, after](Ink::Document& d)  { d.SetModifiers(dstObj, after); });
    LogInfoAction("Copy Modifier");
}

// The empty area below the rows: dropping there pulls objects out of any
// collection AND any object parent; a collection dropped there becomes
// top-level.
void Application::OutlinerBackgroundDropTarget(ImVec2 rectMin, ImVec2 rectMax) {
    if (!project_.document) return;
    if (rectMax.y - rectMin.y < 4.0f) return;
    const ImRect r(rectMin, rectMax);
    if (!ImGui::BeginDragDropTargetCustom(r, ImGui::GetID("##outlinerBgDrop")))
        return;
    constexpr ImGuiDragDropFlags kBgPeek =
        ImGuiDragDropFlags_AcceptBeforeDelivery |
        ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kObjPayload, kBgPeek)) {
        // Below the last row means "as far down as it goes", not "stay where
        // you are": an overshoot is an instruction, not a miss.
        if (outlinerDrag_ && outlinerRows_) {
            outlinerDrag_->SetLandingAtBoundary((int)outlinerRows_->size());
            outlinerDropDepth_ = 1;
        }
        if (p->IsDelivery()) {
            const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
            std::vector<Ink::NodeId> o2, c2;
            OutlinerDragSet(dragged, o2, c2);
            OutlinerDropMixed(o2, c2, Ink::kNullNode);
        }
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kCollPayload)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        std::vector<Ink::NodeId> o2, c2;
        OutlinerDragSet(dragged, o2, c2);
        OutlinerDropMixed(o2, c2, Ink::kNullNode);
    }
    ImGui::EndDragDropTarget();
}

} // namespace App

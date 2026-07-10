#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner drag & drop (legacy feature set, on the Ink model):
//   • Collections view — drag an object ONTO an object to PARENT it (Lot 7
//     object parenting, world position preserved); onto a COLLECTION header to
//     move it into that collection (single-collection move semantics, and it
//     un-parents — the object is pulled out to collection level); onto the
//     BACKGROUND to un-parent + un-collection. Collections drag too: onto a
//     collection to nest, onto the background to become top-level.
//   • Layers view — drag an object onto an object to reorder it just above the
//     target in the stack (same-parent) or move it next to it (cross-parent);
//     onto a GROUP row to move into that group.
//  Every drop is one undoable command. Multi-drag: dragging a row that is part
//  of the selection drags the whole selection (legacy rule).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
constexpr const char* kObjPayload  = "OUTLINER_OBJ";    // Ink::NodeId
constexpr const char* kCollPayload = "OUTLINER_COLL";   // collection id

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

std::vector<Ink::NodeId> Application::OutlinerDraggedIds(Ink::NodeId trigger) const {
    if (edit_.IsSelected(trigger)) {
        std::vector<Ink::NodeId> ids;
        for (Ink::NodeId id : edit_.selection)
            if (project_.document && project_.document->Find(id)) ids.push_back(id);
        return ids;
    }
    return { trigger };
}

// ── Drop operations (each = one undo command) ────────────────────────────────

void Application::OutlinerDropParentTo(const std::vector<Ink::NodeId>& ids,
                                       Ink::NodeId parent) {
    if (!project_.document || parent == Ink::kNullNode) return;
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

void Application::OutlinerDropReorder(const std::vector<Ink::NodeId>& ids,
                                      Ink::NodeId target) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* tn = doc.Find(target);
    if (!tn) return;
    const Ink::NodeId destParent =
        tn->parent != Ink::kNullNode ? tn->parent : tn->page;

    // Capture the full sibling orders for undo (simple + exact).
    struct Order { Ink::NodeId id; Ink::NodeId parent; int index; Ink::Transform2D t; };
    std::vector<Order> before;
    for (Ink::NodeId id : ids) {
        const Ink::Node* n = doc.Find(id);
        if (!n) continue;
        before.push_back({ id, n->parent != Ink::kNullNode ? n->parent : n->page,
                           doc.IndexInParent(id), n->transform });
    }
    // "Above the target in the stack" = just AFTER it in painter order.
    for (Ink::NodeId id : ids) {
        if (id == target) continue;
        doc.MoveTo(id, destParent, doc.IndexInParent(target) + 1);
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

void Application::OutlinerRowDragDrop(const OutlinerRow& row) {
    if (!project_.document || !outlinerCur_) return;
    Ink::Document& doc = *project_.document;
    const bool collectionsMode =
        outlinerCur_->display == OutlinerDisplayMode::Collections;

    // Source (the ListRow's InvisibleButton is the current last item).
    if (row.kind == OutlinerRow::Kind::Object) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kObjPayload, &row.id, sizeof row.id);
            const Ink::Node* n = doc.Find(row.id);
            ImGui::TextUnformatted(n && !n->name.empty() ? n->name.c_str() : "Object");
            ImGui::EndDragDropSource();
        }
    } else if (row.kind == OutlinerRow::Kind::CollectionHeader && collectionsMode) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kCollPayload, &row.id, sizeof row.id);
            const Ink::Collection* c = doc.FindCollection(row.id);
            ImGui::TextUnformatted(c && !c->name.empty() ? c->name.c_str() : "Collection");
            ImGui::EndDragDropSource();
        }
    }

    // Target.
    if (!ImGui::BeginDragDropTarget()) return;
    if (row.kind == OutlinerRow::Kind::Object) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kObjPayload)) {
            const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
            const auto ids = OutlinerDraggedIds(dragged);
            if (collectionsMode) OutlinerDropParentTo(ids, row.id);
            else                 OutlinerDropReorder(ids, row.id);
        }
        // Layers view: dropping onto a GROUP row moves into the group.
        if (!collectionsMode) {
            const Ink::Node* n = doc.Find(row.id);
            if (n && n->kind == Ink::NodeKind::Group)
                if (const ImGuiPayload* p2 = ImGui::AcceptDragDropPayload(kObjPayload)) {
                    const Ink::NodeId dragged = *(const Ink::NodeId*)p2->Data;
                    for (Ink::NodeId id : OutlinerDraggedIds(dragged))
                        doc.MoveTo(id, row.id, -1);
                    LogInfoAction("Move into Group");
                }
        }
    } else if (row.kind == OutlinerRow::Kind::CollectionHeader) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kObjPayload)) {
            const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
            OutlinerDropToCollection(OutlinerDraggedIds(dragged), row.id);
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kCollPayload)) {
            const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
            if (dragged != row.id) {
                project_.document->MoveCollection(dragged, row.id);
                LogInfoAction("Nest Collection");
            }
        }
    }
    ImGui::EndDragDropTarget();
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
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kObjPayload)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        OutlinerDropToRoot(OutlinerDraggedIds(dragged));
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kCollPayload)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        project_.document->MoveCollection(dragged, Ink::kNullNode);
        LogInfoAction("Un-nest Collection");
    }
    ImGui::EndDragDropTarget();
}

} // namespace App

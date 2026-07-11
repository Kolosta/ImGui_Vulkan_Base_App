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
                                      Ink::NodeId target, bool above) {
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

// The band-rect "into" highlight (grey fill + notice-orange contour).
void DrawIntoHighlightRect(ImVec2 a, ImVec2 b) {
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

void DrawIntoHighlight(const UI::ListRow& lr) {
    DrawIntoHighlightRect(ImVec2(lr.BandLeft(), lr.RowTop()),
                          ImVec2(lr.BandRight(), lr.RowTop() + lr.RowH()));
}
} // namespace

void Application::OutlinerRowDragDrop(const OutlinerRow& row, const UI::ListRow& lr) {
    if (!project_.document || !outlinerCur_ || outlinerSuppressInput_) return;
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
    } else if (row.kind == OutlinerRow::Kind::Modifier) {
        // A modifier drags as a COPY payload — droppable only onto a
        // compatible object (nothing else accepts it, so dropping anywhere
        // else simply does nothing).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ModPayload mp{ row.id, row.modIndex };
            ImGui::SetDragDropPayload(kModPayload, &mp, sizeof mp);
            const Ink::Node* n = doc.Find(row.id);
            const Ink::Modifier* m =
                n && row.modIndex >= 0 && row.modIndex < (int)n->modifiers.size()
                    ? &n->modifiers[row.modIndex] : nullptr;
            ImGui::TextUnformatted(!m ? "Modifier"
                : m->kind == Ink::ModifierKind::Array ? "Array"
                : m->kind == Ink::ModifierKind::AlongPath ? "Along Path"
                : "Boolean");
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
                DrawIntoHighlightRect(r.Min, r.Max);
            }
            if (p->IsDelivery()) {
                const auto ids = OutlinerDraggedIds(dragged);
                if (targetColl != Ink::kNullNode)
                    OutlinerDropToCollection(ids, targetColl);
                else
                    OutlinerDropToRoot(ids);
            }
        } else if (row.kind == OutlinerRow::Kind::Object) {
            // Layers view: strict stacking — a grey line between two rows
            // reorders; a GROUP row's centre moves into the group.
            const Ink::Node* n = doc.Find(row.id);
            const bool isGroup = n && n->kind == Ink::NodeKind::Group;
            const DropZone z = ZoneAt(lr, /*edgesAllowed=*/true);
            if (z == DropZone::Into && isGroup) DrawIntoHighlight(lr);
            else if (z == DropZone::Into) DrawInsertLine(lr, /*above=*/true);
            else DrawInsertLine(lr, z == DropZone::Above);
            if (p->IsDelivery()) {
                const auto ids = OutlinerDraggedIds(dragged);
                if (z == DropZone::Into && isGroup) {
                    for (Ink::NodeId id : ids) doc.MoveTo(id, row.id, -1);
                    LogInfoAction("Move into Group");
                } else {
                    // Rows list top-of-stack first: visually ABOVE the target
                    // = AFTER it in painter order.
                    OutlinerDropReorder(ids, row.id,
                                        z != DropZone::Below);
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
        const Ink::Node* tn = doc.Find(targetObj);
        if (tn && tn->kind == Ink::NodeKind::Path) {
            if (outlinerRows_ && objRow >= 0) {
                const ImRect r = bandRectOf(objRow);
                DrawIntoHighlightRect(r.Min, r.Max);
            } else {
                DrawIntoHighlight(lr);
            }
            if (p->IsDelivery())
                OutlinerDropModifierCopy(mp.obj, mp.index, targetObj);
        }
    }

    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kCollPayload, kPeek)) {
        const Ink::NodeId dragged = *(const Ink::NodeId*)p->Data;
        if (row.kind == OutlinerRow::Kind::CollectionHeader) {
            const DropZone z = ZoneAt(lr, /*edgesAllowed=*/true);
            if (z == DropZone::Into) DrawIntoHighlight(lr);
            else DrawInsertLine(lr, z == DropZone::Above);
            if (p->IsDelivery() && dragged != row.id) {
                if (z == DropZone::Into) {
                    doc.MoveCollection(dragged, row.id);
                    LogInfoAction("Nest Collection");
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
                }
            }
        } else if (row.kind == OutlinerRow::Kind::ProjectRoot) {
            DrawIntoHighlight(lr);   // un-nest to the top level
            if (p->IsDelivery() && doc.IsChildCollection(dragged)) {
                doc.MoveCollection(dragged, Ink::kNullNode);
                LogInfoAction("Un-nest Collection");
            }
        } else if (collectionsMode &&
                   (row.kind == OutlinerRow::Kind::Object ||
                    row.kind == OutlinerRow::Kind::Modifier ||
                    row.kind == OutlinerRow::Kind::LinkedData)) {
            // Anywhere inside a collection nests INTO that collection (same
            // rule as objects — no dead rows while dragging a collection).
            const Ink::NodeId targetColl = row.ownerColl;
            const int headerRow = row.ownerRow >= 0 ? row.ownerRow : 0;
            if (outlinerRows_) {
                const ImRect r = bandRectOf(headerRow);
                DrawIntoHighlightRect(r.Min, r.Max);
            }
            if (p->IsDelivery() && dragged != targetColl) {
                if (targetColl != Ink::kNullNode) {
                    doc.MoveCollection(dragged, targetColl);
                    LogInfoAction("Nest Collection");
                } else if (doc.IsChildCollection(dragged)) {
                    doc.MoveCollection(dragged, Ink::kNullNode);
                    LogInfoAction("Un-nest Collection");
                }
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

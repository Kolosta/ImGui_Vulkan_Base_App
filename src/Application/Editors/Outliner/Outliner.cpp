#include "Application.h"

#include "OutlinerRowLayout.h"
#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner editor — the Ink document's organisation tree, restoring the legacy
//  design (docs/Ink/ROADMAP.md Lot 9 rework) but rebuilt around a TWO-PASS,
//  culled renderer so it stays fluid on huge documents:
//
//    1. FLATTEN — walk the tree (collapse + filter + search) into a flat list
//       of visible rows. Pure computation, no ImGui, no drawing.
//    2. WINDOWED DRAW — advance the layout cursor row by row but only actually
//       build (ListRow, icons, text, buttons) the rows inside the scroll
//       viewport. Off-screen rows cost a single cursor advance.
//
//  This makes the editor O(visible rows), not O(document) — the fix for the
//  1000-node demo lag. Layers view rows are taller and carry a live,
//  draw-list-only vector preview of the object (no Vulkan target / texture).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

// The SINGLE source of truth for an object row's type icon, shared by the row
// itself, the collapsed-contents summary and the child rows so an object and
// its summary badge can never show different glyphs.
const char* NodeKindIcon(Ink::NodeKind kind) {
    switch (kind) {
        case Ink::NodeKind::Group:    return "folder";
        case Ink::NodeKind::Instance: return "swap_horiz";
        default:                      return "shape-category";
    }
}
const char* NodeIcon(const Ink::Node& n) { return NodeKindIcon(n.kind); }
// Icons used for the sub-rows a Collections-view object unfolds.
constexpr const char* kModifierIcon   = "settings";
constexpr const char* kLinkedDataIcon = "three_balls";

// Layers-view preview scale (rows this many ui-units tall). 1 = normal row.
constexpr float kLayersRowScale = 2.4f;

// Case-insensitive alphabetical sort by node name (Collections view rule: a
// collection's contents are ALWAYS alphabetical — their order is never manual).
void SortIdsByName(const Ink::Document& doc, std::vector<Ink::NodeId>& ids) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    std::sort(ids.begin(), ids.end(), [&](Ink::NodeId a, Ink::NodeId b) {
        const Ink::Node* na = doc.Find(a);
        const Ink::Node* nb = doc.Find(b);
        return lower(na ? na->name : "") < lower(nb ? nb->name : "");
    });
}
} // namespace

// ── Filters / search ──────────────────────────────────────────────────────────

bool Application::OutlinerSearchHit(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_ || outlinerCur_->search[0] == '\0') return false;
    const Ink::Node* n = project_.document->Find(id);
    return n && ol::ContainsCI(n->name, outlinerCur_->search);
}

bool Application::OutlinerSubtreeSearchHit(Ink::NodeId id) const {
    if (!project_.document) return false;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return false;
    if (OutlinerSearchHit(id)) return true;
    for (Ink::NodeId c : n->children)
        if (OutlinerSubtreeSearchHit(c)) return true;
    return false;
}

bool Application::OutlinerPassesFilter(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_) return true;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return false;
    OutlinerState& o = *outlinerCur_;
    bool ok = true;
    if (n->kind == Ink::NodeKind::Group) { if (!o.showGroups) ok = false; }
    else                                  { if (!o.showObjects) ok = false; }
    if (ok) switch (o.objState) {
        case ObjStateFilter::Visible:    ok = n->visible; break;
        case ObjStateFilter::Selected:   ok = edit_.IsSelected(id); break;
        case ObjStateFilter::Active:     ok = (edit_.active == id); break;
        case ObjStateFilter::Selectable: ok = !n->locked; break;
        case ObjStateFilter::All:        default: break;
    }
    return o.invertFilter ? !ok : ok;
}

bool Application::OutlinerRowSelected(Ink::NodeId id) const {
    if (edit_.IsSelected(id)) return true;
    return outlinerCur_ && outlinerCur_->RowSelected(id);
}

bool Application::OutlinerInAnyCollection(Ink::NodeId id) const {
    if (!project_.document) return false;
    for (const Ink::Collection& c : project_.document->Collections())
        if (std::find(c.members.begin(), c.members.end(), id) != c.members.end())
            return true;
    return false;
}

// ── Selection click (plain / Shift-range / Ctrl / Alt) ────────────────────────

void Application::OutlinerSelectClick(Ink::NodeId id, bool isObject) {
    (void)isObject;
    ImGuiIO& io = ImGui::GetIO();
    OutlinerState& o = *outlinerCur_;

    if (io.KeyShift && o.active != 0) {
        const auto& order = o.rowOrder;
        int ia = -1, ib = -1;
        for (int i = 0; i < (int)order.size(); ++i) {
            if (order[i] == o.active) ia = i;
            if (order[i] == id)       ib = i;
        }
        if (ia >= 0 && ib >= 0) {
            if (ia > ib) std::swap(ia, ib);
            edit_.Clear();
            for (int i = ia; i <= ib; ++i)
                if (project_.document->Find(order[i])) edit_.SelectAdd(order[i]);
            edit_.active = o.active;
            return;
        }
    }
    if (io.KeyCtrl) {
        if (edit_.IsSelected(id)) edit_.Deselect(id); else edit_.SelectAdd(id);
        o.active = id;
        return;
    }
    if (io.KeyAlt) { edit_.SelectAdd(id); o.active = id; return; }
    edit_.SelectOnly(id);
    o.active = id;
}

// ── Pass 1: flatten the visible tree ──────────────────────────────────────────

// Rebuild the parentId → children index (Collections view nesting, Lot 7).
void Application::OutlinerBuildParentIndex() {
    outlinerParentKids_.clear();
    outlinerRowKids_.clear();
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    for (const Ink::Page& page : doc.Pages()) {
        std::vector<Ink::NodeId> stack(page.children.begin(), page.children.end());
        while (!stack.empty()) {
            const Ink::NodeId id = stack.back(); stack.pop_back();
            const Ink::Node* n = doc.Find(id);
            if (!n) continue;
            if (n->parentId != Ink::kNullNode && doc.Find(n->parentId))
                outlinerParentKids_[n->parentId].push_back(id);
            for (Ink::NodeId c : n->children) stack.push_back(c);
        }
    }
}

// The children a row shows in the CURRENT view. Layers: the layer-tree
// children (stacking is the point there). Collections: ONLY the objects
// PARENTED to this node (parentId, Lot 7), alphabetical — the layer tree
// (groups, z-order) is a Layers concept and never structures this view.
const std::vector<Ink::NodeId>*
Application::OutlinerRowChildren(const Ink::Node& n) const {
    if (outlinerCur_ && outlinerCur_->display == OutlinerDisplayMode::Collections) {
        auto& cache = const_cast<Application*>(this)->outlinerRowKids_;
        auto cit = cache.find(n.id);
        if (cit != cache.end())
            return cit->second.empty() ? nullptr : &cit->second;
        Ink::Document& doc = *project_.document;
        std::vector<Ink::NodeId> kids;
        auto it = outlinerParentKids_.find(n.id);
        if (it != outlinerParentKids_.end()) kids = it->second;
        SortIdsByName(doc, kids);
        auto& slot = cache[n.id];
        slot = std::move(kids);
        return slot.empty() ? nullptr : &slot;
    }
    return n.children.empty() ? nullptr : &n.children;
}

void Application::OutlinerFlattenNode(Ink::NodeId id, int depth,
                                      std::vector<OutlinerRow>& out,
                                      Ink::NodeId ownerColl, int ownerRow) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    OutlinerState& o = *outlinerCur_;
    const bool collections = (o.display == OutlinerDisplayMode::Collections);
    const bool searching = o.search[0] != '\0';

    const bool passes = OutlinerPassesFilter(id);
    const bool searchOk = !searching || OutlinerSubtreeSearchHit(id);
    const bool drawSelf = passes && searchOk;
    const std::vector<Ink::NodeId>* kids = OutlinerRowChildren(*n);
    const bool hasKids = kids && !kids->empty();
    // Collections view: an object's children are its MODIFIER stack, the
    // instance's linked data, and the objects parented to it.
    const int  nMods = collections ? (int)n->modifiers.size() : 0;
    const bool hasLinked = collections && n->kind == Ink::NodeKind::Instance &&
                           doc.Find(n->targetRef) != nullptr;
    const bool anyChild = hasKids || nMods > 0 || hasLinked;

    const int myRow = (int)out.size();
    if (drawSelf) {
        OutlinerRow r; r.id = id; r.kind = OutlinerRow::Kind::Object;
        r.depth = depth; r.hasChildren = anyChild;
        r.ownerColl = ownerColl; r.ownerRow = ownerRow;
        out.push_back(r);
        o.rowOrder.push_back(id);
    }
    // Collections view expands the Blender way: collapsed by default,
    // unfolded only while the object is in `expandedObjects`.
    const bool expanded = collections ? o.ObjExpanded(id) : !o.IsCollapsed(id);
    if (!anyChild || !expanded) return;
    const int childDepth = drawSelf ? depth + 1 : depth;

    if (drawSelf && collections) {
        for (int mi = 0; mi < nMods; ++mi) {
            OutlinerRow r; r.id = id; r.kind = OutlinerRow::Kind::Modifier;
            r.depth = childDepth; r.modIndex = mi;
            r.ownerColl = ownerColl; r.ownerRow = ownerRow; r.objRow = myRow;
            out.push_back(r);
        }
        if (hasLinked) {
            OutlinerRow r; r.id = id; r.kind = OutlinerRow::Kind::LinkedData;
            r.depth = childDepth; r.refId = n->targetRef;
            r.ownerColl = ownerColl; r.ownerRow = ownerRow; r.objRow = myRow;
            out.push_back(r);
        }
    }
    if (hasKids) {
        // Layers: top-of-stack first (reverse painter order) for a stack
        // read. Collections: the list is already alphabetical.
        if (collections) {
            for (Ink::NodeId c : *kids)
                OutlinerFlattenNode(c, childDepth, out, ownerColl, ownerRow);
        } else {
            for (auto it = kids->rbegin(); it != kids->rend(); ++it)
                OutlinerFlattenNode(*it, childDepth, out, ownerColl, ownerRow);
        }
    }
}

void Application::OutlinerBuildRows(EditorState& st, std::vector<OutlinerRow>& out) {
    Ink::Document& doc = *project_.document;
    OutlinerState& o = st.outliner;
    OutlinerBuildParentIndex();

    if (o.display == OutlinerDisplayMode::Collections) {
        // The whole tree hangs off the single "Project" ROOT row (legacy):
        // the project title row; collections and loose objects nest under it.
        {
            OutlinerRow r; r.kind = OutlinerRow::Kind::ProjectRoot;
            r.hasChildren = true;
            out.push_back(r);
        }
        const bool rootOpen = !o.IsCollapsed(kProjectRootRowId);
        if (!rootOpen) return;   // single root row; its flatIndex is already 0

        // A parented object is listed UNDER its parent, never at a top level.
        auto isParented = [&](Ink::NodeId id) {
            const Ink::Node* n = doc.Find(id);
            return n && n->parentId != Ink::kNullNode && doc.Find(n->parentId);
        };
        // Recursive collection flatten: header row, then CHILD collections,
        // then members (parented members nest under their in-collection parent).
        std::function<void(const Ink::Collection&, int, Ink::NodeId, int)> flattenColl =
            [&](const Ink::Collection& c, int depth, Ink::NodeId ownerColl,
                int ownerRow) {
                const int myRow = (int)out.size();
                OutlinerRow r; r.id = c.id; r.kind = OutlinerRow::Kind::CollectionHeader;
                r.depth = depth;
                r.hasChildren = !c.members.empty() || !c.childCollections.empty();
                r.ownerColl = ownerColl; r.ownerRow = ownerRow;
                out.push_back(r);
                if (o.IsCollapsed(c.id)) return;
                for (Ink::NodeId cc : c.childCollections)
                    if (const Ink::Collection* child = doc.FindCollection(cc))
                        flattenColl(*child, depth + 1, c.id, myRow);
                // Members are ALPHABETICAL (never manually ordered). GROUPS
                // never list here: a group is Layers-only structure (z-order /
                // compositing), not a real object — its children stand on
                // their own in this view.
                std::vector<Ink::NodeId> members;
                for (Ink::NodeId m : c.members) {
                    const Ink::Node* mn = doc.Find(m);
                    if (!mn || mn->kind == Ink::NodeKind::Group) continue;
                    if (mn->parentId != Ink::kNullNode &&
                        std::find(c.members.begin(), c.members.end(), mn->parentId)
                            != c.members.end()) continue;   // nests under parent
                    members.push_back(m);
                }
                SortIdsByName(doc, members);
                for (Ink::NodeId m : members)
                    OutlinerFlattenNode(m, depth + 1, out, c.id, myRow);
            };
        if (o.showCollections)
            for (const Ink::Collection& c : doc.Collections())
                if (!doc.IsChildCollection(c.id))   // top-level roots only
                    flattenColl(c, 1, Ink::kNullNode, 0);
        // EVERY document object that is in no collection and not parented
        // lands flat under the project root, ALPHABETICAL — the layer tree
        // (group nesting, z-order) never structures this view, so a group's
        // children list here too when no collection claims them. GROUPS
        // themselves are skipped: a group is Layers-only structure, not a
        // real object of the Collections organisation.
        std::vector<Ink::NodeId> loose;
        for (const Ink::Page& page : doc.Pages()) {
            std::vector<Ink::NodeId> stack(page.children.begin(),
                                           page.children.end());
            while (!stack.empty()) {
                const Ink::NodeId id = stack.back(); stack.pop_back();
                const Ink::Node* n = doc.Find(id);
                if (!n) continue;
                for (Ink::NodeId c : n->children) stack.push_back(c);
                if (n->kind == Ink::NodeKind::Group) continue;
                if (!OutlinerInAnyCollection(id) && !isParented(id))
                    loose.push_back(id);
            }
        }
        SortIdsByName(doc, loose);
        for (Ink::NodeId id : loose)
            OutlinerFlattenNode(id, 1, out, Ink::kNullNode, 0);
    } else {
        // Layers: page header + its layer tree, top of stack first.
        for (const Ink::Page& page : doc.Pages()) {
            if (o.showPages) {
                OutlinerRow r; r.id = page.id; r.kind = OutlinerRow::Kind::PageHeader;
                r.depth = 0; r.hasChildren = !page.children.empty();
                out.push_back(r);
            }
            if (o.showPages && o.IsCollapsed(page.id)) continue;
            const int d = o.showPages ? 1 : 0;
            for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
                OutlinerFlattenNode(*it, d, out);
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) out[i].flatIndex = (int)i;
}

// ── Live lightweight vector preview (Layers view) ─────────────────────────────

void Application::OutlinerDrawPreview(Ink::NodeId id, ImVec2 mn, ImVec2 mx) {
    auto& ds = DS::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Card background + frame.
    const float rad = ol::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * 0.5f * ol::Gs();
    dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)), rad);
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Border_Default, ImVec4(0.4f,0.4f,0.4f,1))), rad);
    if (!ink_ || !project_.document) return;

    // The thumbnail is rendered by the REAL Ink pipeline into its own tiny
    // off-screen View, so strokes (dash/cap/align/width), transparency,
    // patterns, instances, arrays and booleans all come out EXACTLY as on the
    // canvas, MSAA and all. The View filters the scene to this node's own
    // subtree (owner ∈ subtree) and isolates it (clips/masks inherited from an
    // ancestor are dropped), fit to the subtree's rendered bounds. Views are
    // cached per node id and evicted when unused, so only the handful of
    // visible Layers rows cost anything.

    // Owner filter = the node's LAYER subtree (its own drawables + every
    // descendant, so GROUPS show their children too). Instanced copies of
    // OTHER nodes stamp their own owner, so they stay excluded (an along-path
    // tick shows only itself; an array keeps its own copies).
    std::vector<std::uint64_t> owners;
    {
        std::vector<Ink::NodeId> stack{ id };
        while (!stack.empty()) {
            const Ink::NodeId c = stack.back(); stack.pop_back();
            owners.push_back(c);
            if (const Ink::Node* n = project_.document->Find(c))
                for (Ink::NodeId k : n->children) stack.push_back(k);
        }
    }

    // Bounds = the UNION of every subtree node's rendered bounds (a group /
    // clip layer has no bounds of its own — its geometry lives in its
    // children), so a group frames its whole content.
    Ink::DRect bb;
    for (std::uint64_t o : owners) {
        Ink::DRect nb;
        if (ink_->NodeBounds(o, nb) && nb.valid) { bb.Grow(nb.min); bb.Grow(nb.max); }
    }
    if (!bb.valid) { (void)ds; return; }

    const float gs = ol::Gs();
    const std::uint32_t pxW = (std::uint32_t)std::max(8.0f, (mx.x - mn.x) - 2.0f * gs);
    const std::uint32_t pxH = (std::uint32_t)std::max(8.0f, (mx.y - mn.y) - 2.0f * gs);

    // Fit the bbox into the pixel area with a small padding (aspect
    // preserving). The camera maps screen_px = (doc - pan) · zoom.
    const double bw = std::max(1e-6, bb.max.x - bb.min.x);
    const double bh = std::max(1e-6, bb.max.y - bb.min.y);
    const double padF = 0.14;
    const double zoom = std::min((double)pxW * (1 - 2*padF) / bw,
                                 (double)pxH * (1 - 2*padF) / bh);
    const double cx = (bb.min.x + bb.max.x) * 0.5, cyd = (bb.min.y + bb.max.y) * 0.5;
    const double panX = cx - (double)pxW * 0.5 / zoom;
    const double panY = cyd - (double)pxH * 0.5 / zoom;

    // A stable per-node view key (bit-tagged so it never collides with the
    // viewport zone keys, which are EditorState pointers).
    const void* key = (const void*)(std::uintptr_t)((id << 1) | 1u);
    Ink::View* view = ink_->AcquireView(key);
    view->SetViewport(pxW, pxH);
    view->SetCamera(panX, panY, zoom);
    view->SetBackground(Ink::SrgbToLinearPremultiplied(1, 1, 1, 1));   // white card
    view->SetPreviewFilter(owners);

    dl->PushClipRect(mn, mx, true);
    if (auto tex = view->Texture()) {
        const ImVec2 imn(mn.x + gs, mn.y + gs);
        dl->AddImage((ImTextureID)tex, imn,
                     ImVec2(imn.x + (float)pxW, imn.y + (float)pxH));
    }
    dl->PopClipRect();
    (void)ds;
}

// ── Collapsed-contents summary (legacy design) ────────────────────────────────
//  Next to a COLLAPSED container's name: one type icon per direct-content
//  category (collection swatch / group / shape / instance) with a small count
//  badge at its lower-right when more than one. When a summarised item is part
//  of the viewport selection, the icon gets the row-selected colour behind it.

void Application::OutlinerCollapsedSummary(const OutlinerRow& rrow, float x,
                                           float rowTopY, float maxX) {
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();

    struct Cat { const char* icon; int count = 0; bool selected = false; };
    // Same glyph sources as the object rows (NodeKindIcon / the sub-row
    // constants) so a collapsed summary can never disagree with the rows.
    Cat colls{ nullptr },
        groups{ NodeKindIcon(Ink::NodeKind::Group) },
        shapes{ NodeKindIcon(Ink::NodeKind::Path) },
        instances{ NodeKindIcon(Ink::NodeKind::Instance) },
        mods{ kModifierIcon }, linked{ kLinkedDataIcon };
    auto tally = [&](Ink::NodeId id) {
        const Ink::Node* n = doc.Find(id);
        if (!n) return;
        Cat& c = n->kind == Ink::NodeKind::Group    ? groups
               : n->kind == Ink::NodeKind::Instance ? instances : shapes;
        ++c.count;
        if (edit_.IsSelected(id)) c.selected = true;
    };
    if (rrow.kind == OutlinerRow::Kind::CollectionHeader) {
        const Ink::Collection* c = doc.FindCollection(rrow.id);
        if (!c) return;
        colls.count = (int)c->childCollections.size();
        for (Ink::NodeId m : c->members) tally(m);
    } else {
        const Ink::Node* n = doc.Find(rrow.id);
        if (!n) return;
        if (const std::vector<Ink::NodeId>* kids = OutlinerRowChildren(*n))
            for (Ink::NodeId k : *kids) tally(k);
        // Collections view: the folded object also summarises its modifier
        // stack and (for an instance) the shared data reference.
        if (outlinerCur_ &&
            outlinerCur_->display == OutlinerDisplayMode::Collections) {
            mods.count = (int)n->modifiers.size();
            if (n->kind == Ink::NodeKind::Instance && doc.Find(n->targetRef))
                linked.count = 1;
        }
    }

    const float gs = ol::Gs(), icon = ol::IconSize(), rowH = ol::RowH();
    const ImVec4 tint = ol::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(.6f,.6f,.6f,1));
    const ImU32 selBg = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::C_Outliner_Row_Selected, ImVec4(0.2f, 0.4f, 0.7f, 1)));
    const ImU32 badgeCol = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1)));
    auto& im = VectorGraphics::IconManager::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto drawCat = [&](const Cat& c, bool swatch) {
        if (c.count <= 0) return;
        x += 6.0f * gs;
        if (x + icon > maxX) return;   // never run under the eye button
        const float y = rowTopY + (rowH - icon) * 0.5f;
        if (c.selected)
            dl->AddRectFilled(ImVec2(x - 1.5f * gs, y - 1.5f * gs),
                              ImVec2(x + icon + 1.5f * gs, y + icon + 1.5f * gs),
                              selBg, 2.0f * gs);
        if (swatch) {
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + icon, y + icon),
                ImGui::ColorConvertFloat4ToU32(tint), 2.0f * gs);
        } else {
            auto md = im.GetDefaultMetadata(c.icon);
            for (auto& z : md.colorZones) z.customColor = tint;
            im.RenderIcon(dl, c.icon, ImVec2(x, y), icon, md);
        }
        if (c.count > 1) {   // count badge, lower-right (legacy placement)
            char b[8]; std::snprintf(b, sizeof b, "%d", c.count);
            const ImVec2 ts = ImGui::CalcTextSize(b);
            dl->AddText(ImVec2(x + icon - ts.x * 0.5f, y + icon - ts.y * 0.7f),
                        badgeCol, b);
        }
        x += icon + (c.count > 1 ? 8.0f * gs : 0.0f);
    };
    drawCat(colls, /*swatch=*/true);
    drawCat(groups, false);
    drawCat(shapes, false);
    drawCat(instances, false);
    drawCat(mods, false);
    drawCat(linked, false);
}

// ── Pass 2: draw one flattened row ────────────────────────────────────────────

void Application::OutlinerDrawRow(EditorState& st, const OutlinerRow& rrow, float) {
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();
    OutlinerState& o = st.outliner;
    const bool layers = (o.display == OutlinerDisplayMode::Layers);
    const bool preview = layers && rrow.kind == OutlinerRow::Kind::Object;

    // Common ListRow config. The hit id mixes kind + modifier index so child
    // rows sharing the object's document id stay unique ImGui items.
    UI::ListRowConfig cfg;
    cfg.id = ImGui::GetID((void*)(uintptr_t)
        (rrow.id ^ ((uint64_t)rrow.kind << 48) ^
         ((uint64_t)(rrow.modIndex + 1) << 56)));
    cfg.zebraOdd = (UI::ListRowZebraIndex() & 1);
    cfg.zebraColor = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
    cfg.bandMarginLeft = ol::BandMargin();
    cfg.cornerRadius = ol::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * ol::Gs();

    // ── Project root (Collections view; legacy design) ──
    if (rrow.kind == OutlinerRow::Kind::ProjectRoot) {
        UI::ListRow row(cfg);
        OutlinerRowDragDrop(rrow, row);   // drop target: pull to project root
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID("##prjroot");
        ol::DotGutter();
        bool open = !o.IsCollapsed(kProjectRootRowId);
        ol::Chevron("##prch", open);
        if (open == o.IsCollapsed(kProjectRootRowId))
            o.ToggleCollapsed(kProjectRootRowId);
        ol::SlotIcon("folder", ol::SafeColor(Tok::C_Outliner_Text,
                                             ImVec4(0.85f, 0.85f, 0.85f, 1)));
        const char* title = !project_.name.empty() ? project_.name.c_str()
                                                   : "Project";
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetCursorScreenPos().x + 4.0f * ol::Gs(),
                   row.RowTop() + (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
            ol::LabelColor(false, false), title);
        ImGui::PopID();
        return;
    }

    // ── Collection header ──
    if (rrow.kind == OutlinerRow::Kind::CollectionHeader) {
        const Ink::Collection* c = doc.FindCollection(rrow.id);
        if (!c) { UI::ListRow dummy(cfg); return; }
        cfg.selected = o.RowSelected(rrow.id);
        ImVec4 selc = ds.GetColor(Tok::C_Outliner_Row_Selected);
        cfg.colors.hover    = ImGui::ColorConvertFloat4ToU32(ImVec4(selc.x, selc.y, selc.z, 0.55f));
        cfg.colors.selected = ImGui::ColorConvertFloat4ToU32(selc);
        UI::ListRow row(cfg);
        OutlinerRowDragDrop(rrow, row);   // drag source + drop target on the row item
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)rrow.id);
        ol::DotGutter();
        for (int d = 0; d < rrow.depth; ++d) ol::ChevronSpacer();
        bool open = !o.IsCollapsed(rrow.id);
        ol::Chevron("##cch", open);
        if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
        ol::SlotSwatch(ImVec4(c->colorTag.r, c->colorTag.g, c->colorTag.b, c->colorTag.a));
        const float nameX = ImGui::GetCursorScreenPos().x + 4.0f * ol::Gs();
        const float eyeSlot = ol::RowH();
        const float eyeX = row.BandRight() - 6.0f * ol::Gs() - eyeSlot;
        if (o.renaming == rrow.id) {
            bool deactivated = false;
            if (ol::RenameField("##crename", o.renameBuf, sizeof o.renameBuf,
                                nameX, row.RowTop(), eyeX - nameX - 4.0f,
                                o.renameTakeFocus, &deactivated)) {
                doc.SetCollectionName(rrow.id, o.renameBuf); o.renaming = 0;
            }
            o.renameTakeFocus = false;
            if (deactivated) o.renaming = 0;
        } else {
            const char* label = c->name.empty() ? "(collection)" : c->name.c_str();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(nameX, row.RowTop() + (ol::RowH()-ImGui::GetTextLineHeight())*0.5f),
                ol::LabelColor(false, !c->visible), label);
            if (rrow.hasChildren && o.IsCollapsed(rrow.id))
                OutlinerCollapsedSummary(rrow, nameX + ImGui::CalcTextSize(label).x,
                                         row.RowTop(), eyeX - 4.0f * ol::Gs());
        }
        row.SuppressInputIn(eyeX, eyeX + eyeSlot);
        {
            auto& im = VectorGraphics::IconManager::Instance();
            const char* icon = c->visible ? "eye" : "eye-closed";
            const float isz = ol::IconSize();
            const ImRect er(ImVec2(eyeX, row.RowTop()), ImVec2(eyeX+eyeSlot, row.RowTop()+ol::RowH()));
            const bool ehov = er.Contains(ImGui::GetIO().MousePos);
            ImVec4 tint = ol::SafeColor(c->visible ? Tok::S_Color_Text_Subtle : Tok::S_Color_Text_Disabled, ImVec4(.6f,.6f,.6f,1));
            if (ehov) tint = ds.GetColor(Tok::S_Color_Accent_Default);
            auto md = im.GetDefaultMetadata(icon);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            im.RenderIcon(ImGui::GetWindowDrawList(), icon,
                ImVec2(eyeX+(eyeSlot-isz)*0.5f, row.RowTop()+(ol::RowH()-isz)*0.5f), isz, md);
            if (ehov && !outlinerSuppressInput_ &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                doc.SetCollectionVisible(rrow.id, !c->visible);
        }
        ImGui::PopID();
        if (!outlinerSuppressInput_) {
            const UI::ListRowInput& in = row.Input();
            if (in.doubleClicked) {
                o.renaming = rrow.id; o.renameTakeFocus = true;
                std::snprintf(o.renameBuf, sizeof o.renameBuf, "%s", c->name.c_str());
            } else if (in.clicked) {
                // A collection is SELECTABLE (legacy): picking it clears the
                // viewport object selection (synced) and selects the set here.
                edit_.Clear();
                o.sel.clear();
                o.sel.push_back(rrow.id);
                o.active = rrow.id;
            }
            // Right-click ONLY opens the menu — never selects (Blender rule).
            if (in.rightClicked) {
                outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos;
                outlinerCtxNode_ = rrow.id;
                outlinerCtxLinkedRef_ = Ink::kNullNode;
                ImGui::OpenPopup("##outlinerCtx");
            }
        }
        return;
    }

    // ── Page header ──
    if (rrow.kind == OutlinerRow::Kind::PageHeader) {
        const Ink::Page* page = doc.FindPage(rrow.id);
        if (!page) { UI::ListRow dummy(cfg); return; }
        UI::ListRow row(cfg);
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)rrow.id);
        ol::DotGutter();
        bool open = !o.IsCollapsed(rrow.id);
        ol::Chevron("##pch", open);
        if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
        ol::SlotIcon("folder", ds.GetColor(Tok::S_Color_Text_Subtle));
        char label[160];
        std::snprintf(label, sizeof label, "%s  (%dx%d)",
                      page->name.empty() ? "Page" : page->name.c_str(),
                      (int)page->size.x, (int)page->size.y);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetCursorScreenPos().x, row.RowTop() + (ol::RowH()-ImGui::GetTextLineHeight())*0.5f),
            ol::LabelColor(false, false), label);
        ImGui::PopID();
        return;
    }

    // ── Modifier / Linked-data child rows (Collections view) ──
    if (rrow.kind == OutlinerRow::Kind::Modifier ||
        rrow.kind == OutlinerRow::Kind::LinkedData) {
        const Ink::Node* n = doc.Find(rrow.id);
        if (!n) { UI::ListRow dummy(cfg); return; }
        ImVec4 hov = ol::SafeColor(Tok::C_Outliner_Row_Hover,
                                   ImVec4(0.3f, 0.5f, 0.9f, 1));
        hov.w = 0.35f;
        cfg.colors.hover = ImGui::ColorConvertFloat4ToU32(hov);
        // This child row reads as SELECTED when it is the picked child.
        const bool childSel =
            o.selChildObj == rrow.id &&
            ((rrow.kind == OutlinerRow::Kind::Modifier &&
              o.selChildMod == rrow.modIndex) ||
             (rrow.kind == OutlinerRow::Kind::LinkedData && o.selChildMod == -1));
        if (childSel) {
            cfg.selected = true;
            cfg.colors.selected = ImGui::ColorConvertFloat4ToU32(
                ol::SafeColor(Tok::C_Outliner_Row_Selected,
                              ImVec4(0.2f, 0.4f, 0.7f, 1)));
        }
        UI::ListRow row(cfg);
        OutlinerRowDragDrop(rrow, row);   // modifier drag source / obj targets
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)cfg.id);
        ol::DotGutter();
        for (int d = 0; d < rrow.depth; ++d) ol::ChevronSpacer();
        ol::ChevronSpacer();
        if (rrow.kind == OutlinerRow::Kind::Modifier) {
            const int mi = rrow.modIndex;
            const Ink::Modifier* m =
                mi >= 0 && mi < (int)n->modifiers.size() ? &n->modifiers[mi]
                                                         : nullptr;
            if (!m) { ImGui::PopID(); return; }
            ol::SlotIcon("settings", ol::SafeColor(Tok::S_Color_Text_Subtle,
                                                   ImVec4(.6f, .6f, .6f, 1)));
            const char* label =
                m->kind == Ink::ModifierKind::Array ? "Array"
                : m->kind == Ink::ModifierKind::AlongPath ? "Along Path"
                : "Boolean";
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(ImGui::GetCursorScreenPos().x + 4.0f * ol::Gs(),
                       row.RowTop() + (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
                ol::LabelColor(false, !m->enabled), label);
        } else {
            // The instance's SHARED DATA — a reference view, styled dimmed so
            // it never reads as the real object (and it cannot be dragged).
            const Ink::Node* target = doc.Find(rrow.refId);
            ol::SlotIcon("three_balls", ol::SafeColor(Tok::S_Color_Text_Disabled,
                                                      ImVec4(.5f, .5f, .5f, 1)));
            char label[160];
            std::snprintf(label, sizeof label, "%s  (data)",
                          target && !target->name.empty() ? target->name.c_str()
                                                          : "(missing)");
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(ImGui::GetCursorScreenPos().x + 4.0f * ol::Gs(),
                       row.RowTop() + (ol::RowH() - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::ColorConvertFloat4ToU32(ol::SafeColor(
                    Tok::S_Color_Text_Subtle, ImVec4(.6f, .6f, .6f, 1))),
                label);
        }
        ImGui::PopID();
        if (!outlinerSuppressInput_) {
            const UI::ListRowInput& in = row.Input();
            // Child rows read as SELECTED in the tree, but the DOCUMENT
            // selection is the relevant OBJECT:
            //   • a modifier row → its OWNING object is selected (subtle, with
            //     the orange active dot); the modifier row itself highlights
            //     and becomes the Properties focus.
            //   • a linked-data row → the LINKED object (the instance) is
            //     selected; the ORIGINAL that holds the data shows in the
            //     "linked" tint (not selected) so it stands out.
            if (in.clicked) {
                if (ObjectPickActive()) {
                    DeliverObjectPick(rrow.kind == OutlinerRow::Kind::LinkedData
                                          ? rrow.refId : rrow.id);
                } else if (rrow.kind == OutlinerRow::Kind::Modifier) {
                    o.sel.clear();
                    OutlinerSelectClick(rrow.id, true);
                    o.activeModifier = rrow.modIndex;
                    o.selChildObj = rrow.id; o.selChildMod = rrow.modIndex;
                } else if (doc.Find(rrow.refId)) {
                    // Select the LINKED object (the instance owning this row),
                    // not the original data holder.
                    o.sel.clear();
                    OutlinerSelectClick(rrow.id, true);
                    o.selChildObj = rrow.id; o.selChildMod = -1;
                }
            }
            if (in.rightClicked) {
                outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos;
                outlinerCtxNode_ = rrow.id;
                outlinerCtxLinkedRef_ =
                    rrow.kind == OutlinerRow::Kind::LinkedData ? rrow.refId
                                                              : Ink::kNullNode;
                ImGui::OpenPopup("##outlinerCtx");
            }
        }
        return;
    }

    // ── Object / group ──
    const Ink::Node* n = doc.Find(rrow.id);
    if (!n) { UI::ListRow dummy(cfg); return; }
    const bool searching = o.search[0] != '\0';
    const bool selfHit = OutlinerSearchHit(rrow.id);
    const bool selected = OutlinerRowSelected(rrow.id);
    const bool active = (edit_.active == rrow.id);
    // When a CHILD row of this object is the picked one, the object is still
    // the document selection (orange active dot) but reads SUBTLE rather than
    // fully selected, so the child row is what stands out.
    const bool childOwnsSel = (o.selChildObj == rrow.id);
    // The ORIGINAL data holder of a selected linked-data row: a violet
    // "linked" tint (not selected — just a marker), so it is easy to spot.
    bool linkedMarker = false;
    if (o.selChildObj != Ink::kNullNode && o.selChildMod == -1) {
        const Ink::Node* sc = doc.Find(o.selChildObj);
        if (sc && sc->kind == Ink::NodeKind::Instance &&
            sc->targetRef == rrow.id && rrow.id != o.selChildObj)
            linkedMarker = true;
    }

    cfg.selected = selected && !childOwnsSel;
    cfg.active = active;
    auto colf = [&](Tok normal, Tok search, float a) {
        ImVec4 cc = ol::SafeColor(selfHit && searching ? search : normal, ImVec4(0.3f,0.5f,0.9f,1));
        cc.w = a; return ImGui::ColorConvertFloat4ToU32(cc);
    };
    if (childOwnsSel) {
        // Subtle wash (the object is selected but de-emphasised).
        ImVec4 sub = ol::SafeColor(Tok::C_Outliner_Row_Selected, ImVec4(0.2f,0.4f,0.7f,1));
        sub.w = 0.32f;
        cfg.selected = true;
        cfg.colors.selected = ImGui::ColorConvertFloat4ToU32(sub);
    } else if (linkedMarker) {
        // "Linked" marker: a light violet idle band (not a selection).
        ImVec4 v = ol::SafeColor(Tok::P_Color_Purple_500, ImVec4(0.55f,0.4f,0.8f,1));
        v.w = 0.30f;
        cfg.colors.idle = ImGui::ColorConvertFloat4ToU32(v);
    }
    cfg.colors.hover         = colf(Tok::C_Outliner_Row_Hover, Tok::C_Outliner_Search_Hover, 0.55f);
    cfg.colors.selected      = colf(Tok::C_Outliner_Row_Selected, Tok::C_Outliner_Search_Selected, 1.0f);
    cfg.colors.selectedHover = colf(Tok::C_Outliner_Row_SelectedHover, Tok::C_Outliner_Search_SelectedHover, 1.0f);
    cfg.colors.active        = colf(Tok::C_Outliner_Row_Active, Tok::C_Outliner_Search_Active, 1.0f);
    cfg.colors.activeHover   = colf(Tok::C_Outliner_Row_ActiveHover, Tok::C_Outliner_Search_ActiveHover, 1.0f);
    if (selfHit && searching && !selected)
        cfg.colors.idle = colf(Tok::C_Outliner_Search_Visual, Tok::C_Outliner_Search_Visual, 0.45f);

    UI::ListRow row(cfg);
    OutlinerRowDragDrop(rrow, row);   // drag source + drop target on the row item
    ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
    ImGui::PushID((int)rrow.id);

    ol::DotGutter();
    for (int d = 0; d < rrow.depth; ++d) ol::ChevronSpacer();
    // A badge shares the CHEVRON slot for a clip/mask CHILD (crop-free = clip,
    // contrast-square = mask) OR a CLIP GROUP (crop-free — the group is masked
    // by its first child).
    const bool clipMaskChild =
        layers && (n->parent != Ink::kNullNode ||
                   (n->kind == Ink::NodeKind::Group && n->clip));
    const bool badgeIsMask = n->isMask;   // group-clip → crop-free
    if (rrow.hasChildren) {
        // Collections view: Blender expansion (collapsed by default, present
        // in expandedObjects = open). Layers keeps the collapsed-set default.
        // When a chevron is also needed the badge stacks ABOVE it; otherwise
        // it takes the whole slot.
        if (clipMaskChild) {
            const ImVec2 slot0 = ImGui::GetCursorScreenPos();
            const char* badge = badgeIsMask ? "contrast-square" : "crop-free";
            const float bsz = ol::IconSize() * 0.78f;
            auto& im = VectorGraphics::IconManager::Instance();
            if (im.HasIcon(badge)) {
                auto md = im.GetDefaultMetadata(badge);
                const ImVec4 tint = ol::SafeColor(
                    badgeIsMask ? Tok::S_Color_Accent_Default : Tok::S_Color_Text_Subtle,
                    ImVec4(.6f, .6f, .6f, 1));
                for (auto& z : md.colorZones) z.customColor = tint;
                const float bx = slot0.x + (ol::ChevronSlotW() - bsz) * 0.5f;
                // Badge centred at ~28% of the row height (a small gap from the
                // top); the chevron sits at ~72% (a matching gap from the
                // bottom) — spread from the edges and from each other.
                const float by = row.RowTop() + row.RowH() * 0.28f - bsz * 0.5f;
                im.RenderIcon(ImGui::GetWindowDrawList(), badge,
                              ImVec2(bx, by), bsz, md);
            }
        }
        // The chevron drops to the lower part of the slot so it clears the
        // clip/mask badge above it.
        const float chevY = clipMaskChild ? 0.72f : 0.5f;
        if (!layers) {
            bool open = o.ObjExpanded(rrow.id);
            ol::Chevron("##ch", open, chevY);
            if (open != o.ObjExpanded(rrow.id)) o.ToggleObjExpanded(rrow.id);
        } else {
            bool open = !o.IsCollapsed(rrow.id);
            ol::Chevron("##ch", open, chevY);
            if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
        }
    } else if (clipMaskChild) {
        // Clip/mask child (or clip group) with NO own children: the badge
        // takes the chevron slot alone.
        const char* badge = badgeIsMask ? "contrast-square" : "crop-free";
        const float bsz = ol::IconSize() * 0.82f;
        auto& im = VectorGraphics::IconManager::Instance();
        const ImVec2 slot0 = ImGui::GetCursorScreenPos();
        if (im.HasIcon(badge)) {
            auto md = im.GetDefaultMetadata(badge);
            const ImVec4 tint = ol::SafeColor(
                badgeIsMask ? Tok::S_Color_Accent_Default : Tok::S_Color_Text_Subtle,
                ImVec4(.6f, .6f, .6f, 1));
            for (auto& z : md.colorZones) z.customColor = tint;
            im.RenderIcon(ImGui::GetWindowDrawList(), badge,
                ImVec2(slot0.x + (ol::ChevronSlotW() - bsz) * 0.5f,
                       row.RowTop() + (row.RowH() - bsz) * 0.5f), bsz, md);
        }
        ol::ChevronSpacer();
    } else {
        ol::ChevronSpacer();
    }

    const float eyeSlot = ol::RowH();
    const float eyeX = row.BandRight() - 6.0f * ol::Gs() - eyeSlot;

    // Preview card (Layers view) or the flat type icon. In the Collections
    // view a GROUP is just an object (no folder icon — the layer hierarchy is
    // a Layers concept and does not exist here).
    if (preview) {
        const float sz = row.RowH() * 0.86f;
        const ImVec2 pmin(ImGui::GetCursorScreenPos().x, row.RowTop() + (row.RowH()-sz)*0.5f);
        OutlinerDrawPreview(rrow.id, pmin, ImVec2(pmin.x + sz, pmin.y + sz));
        ImGui::Dummy(ImVec2(sz + 6.0f * ol::Gs(), row.RowH()));
        ImGui::SameLine(0.0f, 0.0f);
    } else {
        const char* icon = (!layers && n->kind == Ink::NodeKind::Group)
                               ? "shape-category" : NodeIcon(*n);
        ol::SlotIcon(icon, ds.GetColor(Tok::S_Color_Text_Default));
    }

    const float nameX = ImGui::GetCursorScreenPos().x + 4.0f * ol::Gs();
    if (o.renaming == rrow.id) {
        bool deactivated = false;
        if (ol::RenameField("##rename", o.renameBuf, sizeof o.renameBuf,
                            nameX, row.RowTop(), eyeX - nameX - 4.0f,
                            o.renameTakeFocus, &deactivated)) {
            Action_RenameNode(rrow.id, o.renameBuf); o.renaming = 0;
        }
        o.renameTakeFocus = false;
        if (deactivated) o.renaming = 0;
    } else {
        const char* label = n->name.empty() ? "(unnamed)" : n->name.c_str();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(nameX, row.RowTop() + (row.RowH()-ImGui::GetTextLineHeight())*0.5f),
            ol::LabelColor(selfHit && searching, !n->visible), label);
        const bool folded = layers ? o.IsCollapsed(rrow.id)
                                   : !o.ObjExpanded(rrow.id);
        if (rrow.hasChildren && folded)
            OutlinerCollapsedSummary(rrow, nameX + ImGui::CalcTextSize(label).x,
                                     row.RowTop(), eyeX - 4.0f * ol::Gs());
    }

    if (active) {
        ImVec4 dc = ol::SafeColor(Tok::S_State_Active_OnPage, ImVec4(0.95f,0.6f,0.2f,1));
        ol::ActiveDotAt(row.BandLeft(), row.RowTop(), ImGui::ColorConvertFloat4ToU32(dc));
    }

    row.SuppressInputIn(eyeX, eyeX + eyeSlot);
    {
        auto& im = VectorGraphics::IconManager::Instance();
        const char* icon = n->visible ? "eye" : "eye-closed";
        const float isz = ol::IconSize();
        const ImRect er(ImVec2(eyeX, row.RowTop()), ImVec2(eyeX+eyeSlot, row.RowTop()+ol::RowH()));
        const bool ehov = er.Contains(ImGui::GetIO().MousePos);
        ImVec4 tint = ol::SafeColor(n->visible ? Tok::S_Color_Text_Subtle : Tok::S_Color_Text_Disabled, ImVec4(.6f,.6f,.6f,1));
        if (ehov) tint = ds.GetColor(Tok::S_Color_Accent_Default);
        auto md = im.GetDefaultMetadata(icon);
        if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
        im.RenderIcon(ImGui::GetWindowDrawList(), icon,
            ImVec2(eyeX+(eyeSlot-isz)*0.5f, row.RowTop()+(ol::RowH()-isz)*0.5f), isz, md);
        if (ehov && !outlinerSuppressInput_ &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            Action_ToggleNodeVisible(rrow.id);
    }
    ImGui::PopID();

    if (outlinerSuppressInput_) return;   // sync-picking owns the mouse
    const UI::ListRowInput& in = row.Input();
    // Object eyedropper: hovering a row shows its name (same tooltip as
    // elsewhere) so the pick has feedback in the Outliner too.
    if (ObjectPickActive() && in.hovered)
        UI::DrawTooltip(n->name.empty() ? "(unnamed)" : n->name.c_str(),
                        ImGui::GetIO().MousePos);
    if (in.doubleClicked) {
        o.renaming = rrow.id;
        o.renameTakeFocus = true;
        std::snprintf(o.renameBuf, sizeof o.renameBuf, "%s", n->name.c_str());
    } else if (in.clicked) {
        // Object eyedropper active: deliver this node to the picker instead of
        // selecting it.
        if (ObjectPickActive()) { DeliverObjectPick(rrow.id); }
        else {
            // An object click drops any collection-row / child-row selection.
            o.sel.clear();
            o.ClearChildSel(); o.activeModifier = -1;
            OutlinerSelectClick(rrow.id, n->kind != Ink::NodeKind::Group);
        }
    }
    // Right-click ONLY opens the menu — never a selection change (Blender rule:
    // the menu acts on the current selection; the row is context only).
    if (in.rightClicked) {
        outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos;
        outlinerCtxNode_ = rrow.id;
        outlinerCtxLinkedRef_ = Ink::kNullNode;
        ImGui::OpenPopup("##outlinerCtx");
    }
}

// ── Tree guide lines (legacy design) ──────────────────────────────────────────
//  A vertical line descends under the chevron of every EXPANDED container,
//  spanning its visible descendants. Style encodes the container type:
//    • collection      → its colour tag, solid
//    • group / page    → border colour, solid
//    • parented object → text-subtle, dotted (Collections view relation)
//  Spans come from ONE stack pass over the flat row list, then are culled to
//  the scroll window, so the cost stays O(rows) with tiny constants.

void Application::OutlinerDrawGuideLines(EditorState& st,
                                         const std::vector<OutlinerRow>& rows,
                                         float startY, float stripeH) {
    if (rows.empty() || !project_.document) return;
    Ink::Document& doc = *project_.document;
    OutlinerState& o = st.outliner;
    const bool layers = (o.display == OutlinerDisplayMode::Layers);

    const float winTop = ImGui::GetWindowPos().y;
    const float scrollY = ImGui::GetScrollY();
    const float viewTop = winTop, viewBot = winTop + ImGui::GetWindowHeight();
    auto rowTopY = [&](std::size_t i) {
        return winTop - scrollY + startY + (float)i * stripeH;
    };
    const float x0 = ol::RowLeft() + ol::BandMargin() + ol::DotGutterW();

    const ImU32 solid = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Border_Default, ImVec4(0.4f, 0.4f, 0.4f, 1)));
    const ImU32 dotted = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));

    auto emit = [&](std::size_t parent, std::size_t last) {
        if (last <= parent) return;
        // From under the parent's stripe to the BOTTOM of its last descendant
        // (ol::TreeLine applies the same small inset at both ends).
        const float ys = rowTopY(parent + 1);
        const float ye = rowTopY(last) + stripeH;
        if (ye < viewTop || ys > viewBot) return;   // fully off-screen
        const float x = x0 + ((float)rows[parent].depth + 0.5f) * ol::ChevronSlotW();
        ImU32 col = solid; bool dot = false;
        switch (rows[parent].kind) {
            case OutlinerRow::Kind::CollectionHeader:
                if (const Ink::Collection* c = doc.FindCollection(rows[parent].id))
                    col = ImGui::ColorConvertFloat4ToU32(ImVec4(
                        c->colorTag.r, c->colorTag.g, c->colorTag.b, c->colorTag.a));
                break;
            case OutlinerRow::Kind::Object: {
                const Ink::Node* n = doc.Find(rows[parent].id);
                const bool group = n && n->kind == Ink::NodeKind::Group;
                if (!layers && !group) { col = dotted; dot = true; }   // parenting
                break;
            }
            case OutlinerRow::Kind::PageHeader: default: break;
        }
        ol::TreeLine(x, ys, ye, col, dot);
    };

    // One pass: push every row; when depth falls back, close the spans above it.
    std::vector<std::size_t> stack;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        while (!stack.empty() && rows[i].depth <= rows[stack.back()].depth) {
            emit(stack.back(), i - 1);
            stack.pop_back();
        }
        if (rows[i].depth > rows[i - 1].depth) stack.push_back(i - 1);
    }
    while (!stack.empty()) { emit(stack.back(), rows.size() - 1); stack.pop_back(); }
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderOutliner(EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();
    outlinerCur_ = &st.outliner;

    // Shortcut context + hovered-leaf tracking (numpad-. targets the hovered
    // editor — only the Viewport used to register itself, so "frame selected"
    // never reached the Outliner).
    Shortcuts::ShortcutManager::Instance()
        .RegisterRegionContext("##zone", "outliner", "content");
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        zoneLayout_.SetHoveredEditorState(&st);

    if (!project_.document) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No document");
        ImGui::PopStyleColor();
        return;
    }
    edit_.Prune(*project_.document);
    st.outliner.rowOrder.clear();
    if (edit_.active != Ink::kNullNode) st.outliner.active = edit_.active;
    // The child-row selection (a modifier / linked-data sub-row that reads as
    // selected, with its owner shown subtle + the linked original in violet) is
    // only coherent while its OWNER is the sole active document selection.
    // Selecting anything else — a row here, or an object in the viewport, which
    // never touches OutlinerState — must drop it, exactly like every other
    // selection. Invalidate it once per frame when the document selection has
    // moved off the owning object (or grown past it).
    if (st.outliner.selChildObj != 0 &&
        (edit_.active != st.outliner.selChildObj ||
         edit_.selection.size() != 1)) {
        st.outliner.ClearChildSel();
        st.outliner.activeModifier = -1;
    }

    // Numpad "." must reach a selection buried in a COLLAPSED hierarchy:
    // unfold every ancestor FIRST (the flat-row scroll below only finds rows
    // that exist). Layers: the layer-tree parent chain + the owning page.
    // Collections: the Project root, every collection containing the node
    // (and their parent collections), and the object-parent chain (those
    // child rows unfold via expandedObjects).
    if (st.outliner.reqScrollToActive && edit_.active != Ink::kNullNode) {
        OutlinerState& o = st.outliner;
        Ink::Document& d = *project_.document;
        if (const Ink::Node* an = d.Find(edit_.active)) {
            for (Ink::NodeId p = an->parent; p != Ink::kNullNode;) {
                o.collapsed.erase(p);
                const Ink::Node* pn = d.Find(p);
                p = pn ? pn->parent : Ink::kNullNode;
            }
            o.collapsed.erase(an->page);
            if (o.display == OutlinerDisplayMode::Collections) {
                o.collapsed.erase(kProjectRootRowId);
                std::unordered_map<Ink::NodeId, Ink::NodeId> collParent;
                for (const Ink::Collection& c : d.Collections())
                    for (Ink::NodeId k : c.childCollections)
                        collParent[k] = c.id;
                for (const Ink::Collection& c : d.Collections()) {
                    if (std::find(c.members.begin(), c.members.end(),
                                  edit_.active) == c.members.end())
                        continue;
                    for (Ink::NodeId cc = c.id; cc != Ink::kNullNode;) {
                        o.collapsed.erase(cc);
                        auto it = collParent.find(cc);
                        cc = it == collParent.end() ? Ink::kNullNode
                                                    : it->second;
                    }
                }
                for (Ink::NodeId p = an->parentId; p != Ink::kNullNode;) {
                    o.expandedObjects.insert(p);
                    const Ink::Node* pn = d.Find(p);
                    p = pn ? pn->parentId : Ink::kNullNode;
                }
            }
        }
    }
    // Object and collection selection are EXCLUSIVE: selecting objects
    // anywhere (viewport click, Shift+click extend, box select) drops any
    // selected collection rows — the two never read as selected together.
    if (!edit_.selection.empty() && !st.outliner.sel.empty())
        st.outliner.sel.clear();

    // While the sync-picking gesture is (or was, this frame) active, every row
    // is input-inert: the cancelling right-click / Esc must ONLY cancel the
    // gesture, never select a row or open a menu underneath.
    outlinerSuppressInput_ = (outlinerPickingState_ != nullptr);

    // Viewport-sync upkeep.
    if (st.outliner.syncTarget &&
        !zoneLayout_.IsLiveEditorState(st.outliner.syncTarget, CoreEditor::Viewport))
        st.outliner.syncTarget = nullptr;
    if (st.outliner.syncPicking) {
        const bool noViewport = zoneLayout_.CountEditors(CoreEditor::Viewport) == 0;
        if (noViewport || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            st.outliner.syncPicking = false;
            if (outlinerPickingState_ == &st.outliner) outlinerPickingState_ = nullptr;
        } else {
            UI::DrawTooltipTranslucent("Select a viewport to synchronise with",
                                       ImGui::GetIO().MousePos, ds.GetFloat(Tok::S_Opacity_Moderate));
        }
    }

    // ── Pass 1: flatten ──
    std::vector<OutlinerRow> rows;
    rows.reserve(256);
    OutlinerBuildRows(st, rows);

    // ── Pass 2: windowed draw ──
    if (UI::BeginScroll("##outlinerScroll", ImVec2(0, 0))) {
        UI::ListRowResetZebra();
        const bool layers = (st.outliner.display == OutlinerDisplayMode::Layers);
        // Set the per-row band scale ONCE for the whole list (Layers = taller).
        UI::ListRowSetBandScale(layers ? kLayersRowScale : 1.0f);
        const float stripeH = UI::ListRowStripeHeight();

        const float scrollY = ImGui::GetScrollY();
        const float viewTop = scrollY;
        const float viewBot = scrollY + ImGui::GetWindowHeight();
        const float startY = ImGui::GetCursorPosY();   // local Y of the first row

        // Publish the flat list + geometry for the drag & drop (it draws the
        // drop highlight on OTHER rows — e.g. the enclosing collection).
        outlinerRows_       = &rows;
        outlinerRowsStartY_ = startY;
        outlinerStripeH_    = stripeH;

        // Numpad "." — frame the selection: scroll straight to the active row's
        // index. Computed from the FLAT row list, so it works even when that row
        // is culled (SetScrollHereY only fires for rows we actually draw).
        if (st.outliner.reqScrollToActive && edit_.active != Ink::kNullNode) {
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (rows[i].id == edit_.active && rows[i].kind == OutlinerRow::Kind::Object) {
                    const float rowTop = startY + (float)i * stripeH;
                    const float target = rowTop - (ImGui::GetWindowHeight() - stripeH) * 0.5f;
                    ImGui::SetScrollY(std::max(0.0f, target));
                    break;
                }
        }

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const float rowTop = startY + (float)i * stripeH;
            const float rowBot = rowTop + stripeH;
            // Cull rows fully outside the visible window: advance the cursor
            // only (one Dummy), do NOT build the row.
            if (rowBot < viewTop - stripeH || rowTop > viewBot + stripeH) {
                ImGui::SetCursorPosY(rowTop);
                ImGui::Dummy(ImVec2(1.0f, stripeH));
                UI::ListRowAdvanceZebra();   // keep parity in step with row index
                continue;
            }
            ImGui::SetCursorPosY(rowTop);
            OutlinerDrawRow(st, rows[i], stripeH);
        }
        // Reserve the full content height so the scrollbar range is correct.
        ImGui::SetCursorPosY(startY + (float)rows.size() * stripeH);
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        UI::ListRowSetBandScale(1.0f);
        OutlinerDrawGuideLines(st, rows, startY, stripeH);

        // Empty-space clicks (only when the pointer isn't over a row).
        if (!outlinerSuppressInput_ &&
            ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            const float my = ImGui::GetMousePos().y - ImGui::GetWindowPos().y + scrollY;
            const bool belowRows = my > startY + (float)rows.size() * stripeH;
            if (belowRows && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) edit_.Clear();
            if (belowRows && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos;
                outlinerCtxNode_ = Ink::kNullNode;
                outlinerCtxLinkedRef_ = Ink::kNullNode;
                ImGui::OpenPopup("##outlinerCtx");
            }
        }
        // The empty area below the rows is a drop target: dropping an object
        // there un-parents / un-collections it; a collection becomes top-level.
        {
            const ImVec2 winPos = ImGui::GetWindowPos();
            const float rowsBotY = winPos.y - scrollY + startY +
                                   (float)rows.size() * stripeH;
            OutlinerBackgroundDropTarget(
                ImVec2(winPos.x, std::max(rowsBotY, winPos.y)),
                ImVec2(winPos.x + ImGui::GetWindowWidth(),
                       winPos.y + ImGui::GetWindowHeight()));
        }
        // Render the context menu (and the collection colour picker) INSIDE the
        // scroll child, the same window scope their OpenPopup was issued from
        // (an id-string popup is scoped to the current window — opening and
        // rendering must share that scope).
        RenderOutlinerContextMenu(st);
        RenderOutlinerColorPicker();
        outlinerRows_ = nullptr;   // rows is loop-local; never dangle
    }
    UI::EndScroll();
    st.outliner.reqScrollToActive = false;
}

} // namespace App

#include "ZoneLayout.h"
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Dropdown.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace App {
const char* EditorKindName(EditorKind k) {
    switch (k) {
        case EditorKind::Viewport:  return "Viewport";
        case EditorKind::Outliner:  return "Outliner";
        case EditorKind::Timeline:  return "Timeline";
        case EditorKind::DevPanels: return "Dev Panels";
        default:                    return "?";
    }
}
const char* EditorKindIcon(EditorKind k) {
    switch (k) {
        case EditorKind::Viewport:  return "image";
        case EditorKind::Outliner:  return "checklist";
        case EditorKind::Timeline:  return "find-replace";
        case EditorKind::DevPanels: return "draw";
        default:                    return "";
    }
}
// Shortcut action id that switches the hovered zone to this editor (the

ZoneLayout::ZoneLayout() {
    // Initial layout = Blender-like: [ (Viewport / Timeline) | Outliner ].
    auto mkLeaf = [](EditorKind k) {
        auto n = std::make_unique<Node>();
        n->tabs.push_back(Tab{k, {}});
        n->activeTab = 0;
        return n;
    };
    auto left = std::make_unique<Node>();
    left->vertical  = false;           // stacked: viewport over timeline
    left->initRatio = 0.80f;
    left->a = mkLeaf(EditorKind::Viewport);
    left->b = mkLeaf(EditorKind::Timeline);

    root_ = std::make_unique<Node>();
    root_->vertical  = true;           // side by side: left block | outliner
    root_->initRatio = 0.80f;
    root_->a = std::move(left);
    root_->b = mkLeaf(EditorKind::Outliner);
}

// ── Layout pass: assign pos/size to every node ────────────────────────────────
// ABSOLUTE sizing (Blender-style): the split's first child gets `firstPx`
// along the split axis (clamped so BOTH sides keep at least `kMinZonePx`),
// the second child gets the rest. firstPx is only ever changed by a direct
// separator drag on THIS split — never recomputed from a ratio — so resizing
// one separator does not cascade into sibling sub-zones. If the window
// shrinks below what the children need, the first child yields first.
void ZoneLayout::Layout(Node* n, ImVec2 pos, ImVec2 size, float gap) {
    n->pos = pos; n->size = size;
    if (n->isLeaf()) return;

    const float total  = n->vertical ? size.x : size.y;
    const float usable = std::max(0.0f, total - gap);
    const float minPx  = kMinZonePx_;

    // First Layout: derive the absolute size from the requested init ratio.
    if (n->firstPx < 0.0f) {
        n->firstPx = usable * n->initRatio;
    } else if (n->lastUsable > 0.0f && usable > 0.0f &&
               std::abs(usable - n->lastUsable) > 0.01f && n != sepDragging_) {
        // The window (or a parent zone) resized: keep THIS split's ratio by
        // rescaling firstPx proportionally. The split being actively dragged
        // is exempt — a drag stays absolute so it doesn't push siblings.
        n->firstPx *= usable / n->lastUsable;
    }
    n->lastUsable = usable;

    // Clamp so neither side goes under the minimum (unless usable is so small
    // both can't fit, in which case split it evenly).
    float aExt;
    if (usable < 2.0f * minPx) {
        aExt = usable * 0.5f;
    } else {
        aExt = std::clamp(n->firstPx, minPx, usable - minPx);
    }
    float bExt = usable - aExt;

    if (n->vertical) {                 // split on X (left | right)
        Layout(n->a.get(), pos, ImVec2(aExt, size.y), gap);
        Layout(n->b.get(), ImVec2(pos.x + aExt + gap, pos.y),
                ImVec2(bExt, size.y), gap);
    } else {                           // split on Y (top / bottom)
        Layout(n->a.get(), pos, ImVec2(size.x, aExt), gap);
        Layout(n->b.get(), ImVec2(pos.x, pos.y + aExt + gap),
                ImVec2(size.x, bExt), gap);
    }
}

// Deepest leaf whose rect contains p. Zones conceptually meet at the SEPARATOR
// MIDLINE: when p lands in a gap, the descent still picks one side, so there
// is no dead zone at junctions and a point on the line always resolves to a
// real leaf on one side or the other.
ZoneLayout::Node* ZoneLayout::LeafAt(ImVec2 p) {
    Node* n = root_.get();
    while (n && !n->isLeaf()) {
        if (n->vertical) {
            // Boundary = midline of the gap between child A and child B.
            float mid = (n->a->pos.x + n->a->size.x
                         + n->b->pos.x) * 0.5f;
            n = (p.x < mid) ? n->a.get() : n->b.get();
        } else {
            float mid = (n->a->pos.y + n->a->size.y
                         + n->b->pos.y) * 0.5f;
            n = (p.y < mid) ? n->a.get() : n->b.get();
        }
    }
    return n;
}

// ── One leaf: an in-flow ImGui child (token padding/rounding, no border) ─────

bool ZoneLayout::IsInSubtree(Node* subtree, Node* target) {
    if (!subtree || !target) return false;
    if (subtree == target)   return true;
    if (subtree->isLeaf())   return false;
    return IsInSubtree(subtree->a.get(), target) ||
           IsInSubtree(subtree->b.get(), target);
}

void ZoneLayout::DimSubtree(Node* n, ImDrawList* dl, ImU32 col) {
    if (!n) return;
    if (n->isLeaf()) {
        dl->AddRectFilled(n->pos,
            ImVec2(n->pos.x + n->size.x, n->pos.y + n->size.y), col);
        return;
    }
    DimSubtree(n->a.get(), dl, col);
    DimSubtree(n->b.get(), dl, col);
}

void ZoneLayout::HighlightSubtree(Node* n, ImDrawList* dl, ImU32 col) {
    if (!n) return;
    if (n->isLeaf()) {
        dl->AddRectFilled(n->pos,
            ImVec2(n->pos.x + n->size.x, n->pos.y + n->size.y), col);
        return;
    }
    HighlightSubtree(n->a.get(), dl, col);
    HighlightSubtree(n->b.get(), dl, col);
}

// Parent split of `node` (nullptr if root).
ZoneLayout::Node* ZoneLayout::ParentOf(Node* node) {
    if (root_.get() == node) return nullptr;
    std::vector<Node*> stack{root_.get()};
    while (!stack.empty()) {
        Node* n = stack.back(); stack.pop_back();
        if (n->isLeaf()) continue;
        if (n->a.get() == node || n->b.get() == node) return n;
        stack.push_back(n->a.get());
        stack.push_back(n->b.get());
    }
    return nullptr;
}

// Smallest common ancestor split of two leaves, but only if they actually
// touch across its separator (share a real border). Returns nullptr if the
// two leaves are not directly adjacent.
ZoneLayout::Node* ZoneLayout::AdjacencySplit(Node* leafA, Node* leafB) {
    if (!leafA || !leafB || leafA == leafB) return nullptr;

    // Geometric adjacency: their rects must touch along one full shared edge
    // (overlapping range on the other axis), within a small tolerance that
    // covers the inter-zone gap.
    const float tol = 64.0f;
    auto aR = leafA->pos.x + leafA->size.x, aB = leafA->pos.y + leafA->size.y;
    auto bR = leafB->pos.x + leafB->size.x, bB = leafB->pos.y + leafB->size.y;
    bool yOverlap = (leafA->pos.y < bB && leafB->pos.y < aB);
    bool xOverlap = (leafA->pos.x < bR && leafB->pos.x < aR);
    bool horizAdj = yOverlap &&
        (std::abs(aR - leafB->pos.x) <= tol ||
         std::abs(bR - leafA->pos.x) <= tol);
    bool vertAdj  = xOverlap &&
        (std::abs(aB - leafB->pos.y) <= tol ||
         std::abs(bB - leafA->pos.y) <= tol);
    if (!horizAdj && !vertAdj) return nullptr;

    // Lowest common ancestor of the two leaves in the tree.
    auto pathTo = [&](Node* leaf) {
        std::vector<Node*> path;
        std::function<bool(Node*)> dfs = [&](Node* n) -> bool {
            path.push_back(n);
            if (n == leaf) return true;
            if (!n->isLeaf() &&
                (dfs(n->a.get()) || dfs(n->b.get()))) return true;
            path.pop_back();
            return false;
        };
        dfs(root_.get());
        return path;
    };
    std::vector<Node*> pa = pathTo(leafA), pb = pathTo(leafB);
    Node* lca = nullptr;
    for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
        if (pa[i] == pb[i]) lca = pa[i];
        else break;
    }
    return (lca && !lca->isLeaf()) ? lca : nullptr;
}

void ZoneLayout::ReplaceNode(Node* target, std::unique_ptr<Node> replacement) {
    if (!target || !replacement) return;
    if (target == root_.get()) { root_ = std::move(replacement); return; }
    Node* parent = ParentOf(target);
    if (!parent) return;
    if (parent->a.get() == target) parent->a = std::move(replacement);
    else                           parent->b = std::move(replacement);
}

// Merge ONLY the two specified adjacent leaves. `keep` survives, `remove`
// disappears; the third zone adjacent to `remove` automatically fills the
// freed residual space. CAS 1 = perfect siblings; CAS 2/3 = unequal sizes
// where one side is the right-clicked split S; fallback = deeply nested.

void ZoneLayout::BorderLeaves(Node* s, Node** outA, Node** outB,
                              ImVec2 mousePos) {
    if (!s || s->isLeaf()) { *outA = nullptr; *outB = nullptr; return; }
    ImVec2 pA = mousePos, pB = mousePos;
    if (s->vertical) {
        pA.x = s->a->pos.x + s->a->size.x - 1.0f;
        pB.x = s->b->pos.x + 1.0f;
    } else {
        pA.y = s->a->pos.y + s->a->size.y - 1.0f;
        pB.y = s->b->pos.y + 1.0f;
    }
    *outA = LeafAt(pA);
    *outB = LeafAt(pB);
}

// Remove `leaf`: its sibling collapses into the parent split.

bool ZoneLayout::NodeAlive(Node* node) {
    if (!node) return false;
    std::vector<Node*> stack{root_.get()};
    while (!stack.empty()) {
        Node* n = stack.back(); stack.pop_back();
        if (n == node) return true;
        if (n->isLeaf()) continue;
        stack.push_back(n->a.get());
        stack.push_back(n->b.get());
    }
    return false;
}

// ── Add-area phase 1 (per leaf, IDLE only) ───────────────────────────────────
// Detect the cursor near a corner and ARM the gesture on click. The live
// gesture (spawn / follow / commit / cancel) is driven once per frame in
// UpdateAddArea(), NOT here — because once spawned, addArm_.leaf becomes a
// split and would never be revisited by the per-leaf recursion (that was the
// "everything blocks after add" bug).

void ZoneLayout::Render(
    const DrawEditorFn& drawEditor, const TopBarExtraFn& topBarExtras) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs  = ds.GetGlobalScale();
    float gap = ds.GetFloat(DesignSystem::Tok::C_Zone_SeparatorSize) * gs;
    if (gap < 1.0f) gap = 1.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail  = ImGui::GetContentRegionAvail();
    if (avail.x < 50.0f || avail.y < 50.0f) return;

    hoveredState_ = nullptr;
    hoveredLeaf_  = nullptr;
    primarySep_   = nullptr;
    reqCursor_    = Cursor::None;   // reset; hit-test code may request one
    // Min zone extent = the editor top-bar height (control-height + 2 insets).
    {
        const float controlH = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gs;
        ImVec2 pad = ds.GetVec2(DesignSystem::Tok::C_Dropdown_Padding);
        kMinZonePx_ = controlH + pad.y * 2.0f * gs;
    }

    // Occlusion the IMGUI-NATIVE way: we're currently inside ##LayoutBody.
    // IsWindowHovered(ChildWindows) is TRUE only when the mouse is over the
    // layout (or a zone child) AND no floating window (Settings/Dev/Demo) is
    // on top — ImGui resolves that for us. Everything geometric (separators,
    // corners) is gated on this, so nothing reacts through a floating window
    // and nothing is "forced on top".
    overlayHov_ = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_ChildWindows |
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // A popup (e.g. an open dropdown menu) is an ImGui window stacked ABOVE the
    // layout. The separator / corner hit-test is geometric on io.MousePos and
    // doesn't know about it, so without this guard the cursor would still turn
    // into a resize/add icon — and a click would fire that action — through an
    // open menu. Suspend all zone hit-testing while any popup is open.
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel))
        overlayHov_ = false;

    Layout(root_.get(), origin, avail, gap);

    // Drive any armed add-area gesture (spawn / follow / commit / cancel)
    // BEFORE drawing, then re-layout so the spawned zone follows the mouse
    // this same frame (no 1-frame lag). Tree-independent — survives the
    // restructure that the add gesture performs.
    UpdateAddArea();
    Layout(root_.get(), origin, avail, gap);

    // Only resolve the hovered separator when the layout is genuinely the
    // top-most thing under the mouse — a floating window above must not let
    // a separator highlight (or react) through it.
    if (overlayHov_)
        PickPrimarySeparator(root_.get(), gap);

    // Dwell gate: the resize highlight appears only after the mouse has rested
    // on the SAME separator for a short delay, so sweeping the cursor from one
    // editor to the next doesn't flash a resize line. A separator being dragged
    // is exempt (it must always show). Reset the moment the hovered separator
    // changes or the mouse leaves any separator.
    {
        const float kDwell = 0.35f;   // seconds before the highlight appears
        if (primarySep_ != primarySepDwell_) {
            primarySepDwell_ = primarySep_;
            primarySepTimer_ = 0.0f;
        } else if (primarySep_) {
            primarySepTimer_ += ImGui::GetIO().DeltaTime;
        }
        // Suppress the highlight until the dwell delay elapses (drag exempt).
        if (primarySep_ && primarySep_ != sepDragging_ &&
            primarySepTimer_ < kDwell)
            primarySep_ = nullptr;
    }

    // Resolve the leaf under the mouse (any editor kind), so the editor.*
    // switch shortcuts can target the zone the user is pointing at. Gated on
    // overlayHov_ so a floating window on top suppresses zone targeting.
    if (overlayHov_)
        hoveredLeaf_ = LeafAt(ImGui::GetIO().MousePos);

    // Drop a stale "active zone" pointer after a tree restructure.
    if (activeLeaf_ && !NodeAlive(activeLeaf_)) activeLeaf_ = nullptr;

    // Animate the solo-zone tab-bar reveal toward its target (set by the tab
    // drag). Runs every frame — including AFTER the drag ends — so the bar can
    // slide back out. revealLeaf_ is owned by UpdateTabDrag; here we only ease
    // the height and forget the leaf once fully collapsed AND no drag is asking
    // for it (so the dwell-in phase, where target is still 0, isn't cleared).
    if (revealLeaf_ && !NodeAlive(revealLeaf_)) { revealLeaf_ = nullptr; revealTarget_ = 0.0f; }
    {
        float tau = std::max(0.0001f,
            ds.GetFloat(DesignSystem::Tok::C_ZoneTab_PreviewAnimDuration));
        float a = 1.0f - std::exp(-ImGui::GetIO().DeltaTime / tau);
        revealAnim_ += (revealTarget_ - revealAnim_) * a;
        bool dragging = tabDrag_.active || tabDrag_.armed;
        if (revealTarget_ == 0.0f && revealAnim_ < 0.01f && !dragging) {
            revealAnim_ = 0.0f;
            revealLeaf_ = nullptr;
        }
    }

    // Pass 1: each leaf is its own in-flow ImGui child (token rounding,
    // no native border — the separator bar is the single divider).
    DrawLeaves(root_.get(), gap, drawEditor, topBarExtras);

    // Pass 2: separators / join / add-area / split-guide. Drawn inside a
    // dedicated transparent OVERLAY window placed right after the zone
    // children: it sits ABOVE the zones (so the hover highlight and the
    // Join/Split previews are visible) but BELOW any floating window
    // (Settings/Dev/Demo are created later in the frame, so they stack on
    // top of this overlay — a separator must never cover a floating window).
    // Hit-testing is geometric on io.MousePos, GATED on overlayHov_ (computed
    // above from ##LayoutBody's native hover). The overlay is a CHILD of
    // ##LayoutBody created AFTER the zone children, so its draw list is above
    // the editors yet below any floating window. It is **NoInputs**: it must
    // never steal hover/clicks from the zone widgets (combos, tool buttons,
    // shortcuts) below it — that was the "whole UI blocked" bug. It only
    // carries the draw-list z-order; ImGui handles real input + occlusion.
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##ZoneOverlay", avail, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse |
                      ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoInputs);
    // Order on the overlay (all ABOVE editor content):
    //  1. rounded-corner clip + border  → border is the strict visual limit
    //  2. faint colour-coded corner zones → show draggable corners
    //  3. separators / join previews     → on top at junctions
    //  4. split-guide preview
    DrawZoneFrames(root_.get());
    DrawCornerZones(root_.get());
    DrawNode(root_.get(), gap, drawEditor, topBarExtras);
    DrawSplitPreview();
    // Tab drag: promote/draw/dispatch on the overlay draw list (above zones).
    UpdateTabDrag(gap);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Right-click separator menu, rendered in the ##LayoutBody context (the
    // overlay is NoInputs and cannot host an interactive popup). DrawSeparator
    // recorded which split was right-clicked into menuSplit_.
    if (menuSplit_) {
        char mid[40];
        std::snprintf(mid, sizeof(mid), "##sepmenu_%p", (void*)menuSplit_);
        if (menuOpenRequest_) { ImGui::OpenPopup(mid);
                                menuOpenRequest_ = false; }
        if (ImGui::BeginPopup(mid)) {
            Node* s = menuSplit_;
            if (ImGui::MenuItem("Swap Areas")) {
                Node* la = nullptr; Node* lb = nullptr;
                BorderLeaves(s, &la, &lb, contextMenuPos_);
                if (la && lb) {
                    std::swap(la->tabs,      lb->tabs);
                    std::swap(la->activeTab, lb->activeTab);
                }
            }
            if (ImGui::MenuItem("Join Areas")) {
                join_.active         = true;
                join_.splitNode      = s;
                join_.borderVertical = s->vertical;
                join_.fromDrag       = false;
                join_.anchor         = contextMenuPos_;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Vertical Split")) {
                splitArm_.active   = true;
                splitArm_.vertical = true;
            }
            if (ImGui::MenuItem("Horizontal Split")) {
                splitArm_.active   = true;
                splitArm_.vertical = false;
            }
            ImGui::EndPopup();
        } else if (!menuOpenRequest_) {
            menuSplit_ = nullptr;            // popup closed → forget it
        }
    }

    // Apply any custom SVG cursor requested by the hit-test code this frame.
    ApplyCursor();

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(avail);
}

// Hide the OS cursor and draw the requested SVG icon at the mouse position
// on the FOREGROUND draw list (above everything, like a real cursor). The
// icon is rasterised once and LRU-cached by IconManager (cheap per frame).

void ZoneLayout::ApplyCursor() {
    if (reqCursor_ == Cursor::None) return;

    const char* iconId = nullptr;
    switch (reqCursor_) {
        // Directional corner cursors (note: the up-right asset has no
        // "-cur" suffix — the icon id is the file stem as authored).
        case Cursor::CornerUL: iconId = "corner-actions-add-up-left-cur";   break;
        case Cursor::CornerUR: iconId = "corner-actions-add-up-right";      break;
        case Cursor::CornerDL: iconId = "corner-actions-add-down-left-cur"; break;
        case Cursor::CornerDR: iconId = "corner-actions-add-down-right-cur";break;
        case Cursor::Block:           iconId = "block-cur";             break;
        case Cursor::SplitVertical:   iconId = "split-join-vertical-cur";   break;
        case Cursor::SplitHorizontal: iconId = "split-join-horizontal-cur"; break;
        case Cursor::JoinUp:          iconId = "join-up-cur";           break;
        case Cursor::JoinDown:        iconId = "join-down-cur";         break;
        case Cursor::JoinLeft:        iconId = "join-left-cur";         break;
        case Cursor::JoinRight:       iconId = "join-right-cur";        break;
        default: return;
    }
    auto& im = VectorGraphics::IconManager::Instance();
    if (!im.HasIcon(iconId)) return;             // fall back to OS cursor

    // Hide the OS/ImGui cursor while ours is shown.
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float sz = 28.0f * ds.GetGlobalScale();
    ImVec2 m = ImGui::GetIO().MousePos;
    // Centre the icon on the hot-spot (cursor tip ≈ icon centre).
    ImVec2 p(m.x - sz * 0.5f, m.y - sz * 0.5f);
    // Force the cursor icon white (theme-invariant). The default metadata is
    // Bicolor (zones resolve to semantic.icon.color.*), which ignores
    // customColor — so switch to Multicolor and paint every zone the cursor
    // token colour.
    ImVec4 cursorCol = ds.GetColor(DesignSystem::Tok::C_Cursor_Color);
    auto md = im.GetDefaultMetadata(iconId);
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = cursorCol;
    im.RenderIcon(ImGui::GetForegroundDrawList(), iconId, p, sz, md);
}

// ── Tab navigation (shortcut targets) ────────────────────────────────────────

void ZoneLayout::SetHoveredEditor(EditorKind k) {
    if (hoveredLeaf_ && hoveredLeaf_->isLeaf() && !hoveredLeaf_->tabs.empty())
        ActiveTab(hoveredLeaf_).kind = k;
}

void ZoneLayout::HoveredTabCycle(int dir) {
    Node* n = hoveredLeaf_;
    if (!n || !n->isLeaf() || n->tabs.size() < 2) return;
    int cnt = (int)n->tabs.size();
    n->activeTab = ((n->activeTab + dir) % cnt + cnt) % cnt;
}

void ZoneLayout::ActiveTabSelectEdge(bool last) {
    // The "active zone" is the last one clicked/focused; fall back to hovered
    // if it died or none was recorded.
    Node* n = (activeLeaf_ && NodeAlive(activeLeaf_)) ? activeLeaf_ : hoveredLeaf_;
    if (!n || !n->isLeaf() || n->tabs.empty()) return;
    n->activeTab = last ? (int)n->tabs.size() - 1 : 0;
}

// ── Tab bar of one leaf ───────────────────────────────────────────────────────
// A horizontal strip of Icon+title tabs above the editor menu bar. Each tab is
// a real ImGui InvisibleButton (so hover/click never clashes with the geometric
// overlay). Left-click selects; press + a small move arms a drag (driven later
// by UpdateTabDrag). Records each tab's screen rect for the insertion line.

} // namespace App

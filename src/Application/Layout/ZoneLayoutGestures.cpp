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
void ZoneLayout::DrawSeparator(Node* s, float gap) {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    ImGuiIO& io = ImGui::GetIO();
    const bool  vert = s->vertical;

    ImVec2 bp, bs;
    if (vert) {
        float x = s->a->pos.x + s->a->size.x;
        bp = ImVec2(x, s->pos.y);
        bs = ImVec2(gap, s->size.y);
    } else {
        float y = s->a->pos.y + s->a->size.y;
        bp = ImVec2(s->pos.x, y);
        bs = ImVec2(s->size.x, gap);
    }

    // Hit-test: a small grab band centred on the separator midline so it
    // stays catchable even with a 1px visual gap. Corner add-handles get
    // priority — when the add-gesture is armed elsewhere, don't grab.
    const float grab = std::max(bs.x, bs.y) < 6.0f
                       ? 6.0f : std::max(gap, 6.0f);
    ImVec2 hMin, hMax;
    if (vert) {
        float cx = bp.x + bs.x * 0.5f;
        hMin = ImVec2(cx - grab * 0.5f, bp.y);
        hMax = ImVec2(cx + grab * 0.5f, bp.y + bs.y);
    } else {
        float cy = bp.y + bs.y * 0.5f;
        hMin = ImVec2(bp.x, cy - grab * 0.5f);
        hMax = ImVec2(bp.x + bs.x, cy + grab * 0.5f);
    }
    ImVec2 mp = io.MousePos;
    // overlayHov_ is false when a floating window covers this point → the
    // separator must NOT react through it (no grab, no cursor change).
    bool overBand = overlayHov_ &&
                    (mp.x >= hMin.x && mp.x <= hMax.x &&
                     mp.y >= hMin.y && mp.y <= hMax.y) &&
                    !addArm_.armed && !join_.active && !splitArm_.active &&
                    !tabDrag_.active;

    // Persistent drag ownership keyed on this split (a separator may be
    // hovered while another is being dragged).
    const bool hov = overBand;
    const bool act = (sepDragging_ == s);
    if (overBand && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        sepDragging_ = s;
    if (sepDragging_ == s && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        sepDragging_ = nullptr;

    if (hov || act)
        ImGui::SetMouseCursor(vert ? ImGuiMouseCursor_ResizeEW
                                   : ImGuiMouseCursor_ResizeNS);

    // Right-click: record the request. The actual popup is opened/rendered
    // later in Render(), back in the ##LayoutBody context — this separator
    // code runs inside the NoInputs overlay which can't host a popup.
    if (overBand && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        contextMenuPos_  = io.MousePos;
        menuSplit_       = s;
        menuOpenRequest_ = true;
    }

    // ── Visual ────────────────────────────────────────────────────────────
    // NOTHING at rest: the separator line shows ONLY when the mouse is
    // genuinely in this split's gap (primarySep_ == s) or it is being
    // dragged. Then we draw, in semantic.color.border:
    //   • the WHOLE straight line — this split's span plus every colinear
    //     separator of other splits on the same geometric line — at REDUCED
    //     opacity (these are all the editors that shift if you drag it);
    //   • the STRICT segment shared by exactly the two leaves the cursor is
    //     between — at FULL opacity (the swap/join base pair).
    if (primarySep_ == s || act) {
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        // Primary segment (under cursor) + colinear continuation: distinct
        // tokens so each can carry its own colour, and the continuation its own
        // opacity (component.zone.separator.*).
        ImVec4 cFull = ds.GetColor(DesignSystem::Tok::C_Zone_SeparatorColor);
        ImVec4 cDim  = ds.GetColor(DesignSystem::Tok::C_Zone_SeparatorColorContinuation);
        cDim.w = ds.GetFloat(DesignSystem::Tok::C_Zone_SeparatorContinuationOpacity);
        ImU32 colFull = ImGui::GetColorU32(cFull);
        ImU32 colDim  = ImGui::GetColorU32(cDim);
        // The highlight band fills EXACTLY the inter-zone gap (no +border), so
        // it never bleeds onto a zone's own 1px border. Pixel-snapped so the
        // band edges stay crisp (no anti-aliased smear onto a 2nd pixel).
        const float half = gap * 0.5f;
        const float sCoord = std::roundf(vert ? (bp.x + bs.x * 0.5f)
                                              : (bp.y + bs.y * 0.5f));
        const float lo = std::roundf(sCoord - half);
        const float hi = std::roundf(sCoord + half);

        // Reduced-opacity continuation: every split with the same
        // orientation whose separator lies on the same line (±half) — its
        // full span. Covers all editors that would be pushed.
        std::function<void(Node*)> cont = [&](Node* k) {
            if (!k || k->isLeaf()) return;
            cont(k->a.get()); cont(k->b.get());
            if (k->vertical != vert) return;
            if (vert) {
                float kx = k->a->pos.x + k->a->size.x + gap * 0.5f;
                if (std::abs(kx - sCoord) <= half + 1.0f)
                    sdl->AddRectFilled(
                        ImVec2(lo, std::roundf(k->pos.y)),
                        ImVec2(hi, std::roundf(k->pos.y + k->size.y)),
                        colDim);
            } else {
                float ky = k->a->pos.y + k->a->size.y + gap * 0.5f;
                if (std::abs(ky - sCoord) <= half + 1.0f)
                    sdl->AddRectFilled(
                        ImVec2(std::roundf(k->pos.x), lo),
                        ImVec2(std::roundf(k->pos.x + k->size.x), hi),
                        colDim);
            }
        };
        cont(root_.get());

        // Full-opacity strict segment = overlap of the two leaves the
        // cursor is actually between.
        Node* bl = nullptr; Node* brr = nullptr;
        BorderLeaves(s, &bl, &brr, io.MousePos);
        if (bl && brr) {
            if (vert) {
                float y0 = std::roundf(std::max(bl->pos.y, brr->pos.y));
                float y1 = std::roundf(std::min(bl->pos.y + bl->size.y,
                                             brr->pos.y + brr->size.y));
                if (y1 > y0)
                    sdl->AddRectFilled(ImVec2(lo, y0), ImVec2(hi, y1), colFull);
            } else {
                float x0 = std::roundf(std::max(bl->pos.x, brr->pos.x));
                float x1 = std::roundf(std::min(bl->pos.x + bl->size.x,
                                             brr->pos.x + brr->size.x));
                if (x1 > x0)
                    sdl->AddRectFilled(ImVec2(x0, lo), ImVec2(x1, hi), colFull);
            }
        }
    }

    // ── Correlated drag: ABSOLUTE — move ONLY this split's boundary by the
    //    raw mouse delta in px. Nested sub-zones keep their own firstPx, so
    //    nothing else moves (no proportional/flex domino). Layout() clamps
    //    both sides to the min zone size.
    if (act) {
        float d = vert ? io.MouseDelta.x : io.MouseDelta.y;
        s->firstPx = (s->firstPx < 0.0f ? 0.0f : s->firstPx) + d;
        // Clamp here too so the value doesn't drift far outside the usable
        // range while dragging fast off-window.
        float total  = (vert ? s->size.x : s->size.y);
        float usable = std::max(0.0f, total - gap);
        s->firstPx = std::clamp(s->firstPx,
                                std::min(kMinZonePx_, usable * 0.5f),
                                std::max(usable - kMinZonePx_, usable * 0.5f));
    }
    // The Swap/Join/Split context menu is rendered in Render() (the overlay
    // hosting this code is NoInputs and cannot own an interactive popup).
}

// Returns true if `target` is `subtree` itself or any descendant.

void ZoneLayout::JoinLeaves(Node* keep, Node* remove) {
    if (!keep || !remove || keep == remove) return;

    Node* S = join_.splitNode;
    if (!S) return;

    Node* parentKeep   = ParentOf(keep);
    Node* parentRemove = ParentOf(remove);
    if (!parentKeep || !parentRemove) return;

    // CAS 1: both windows are direct siblings (a perfect rectangle).
    if (parentKeep == parentRemove) {
        std::unique_ptr<Node> promoted = std::move(
            parentKeep->a.get() == keep ? parentKeep->a : parentKeep->b);
        ReplaceNode(parentKeep, std::move(promoted));
        return;
    }

    // CAS 2: `remove` spans the big side (it IS S's child). `keep` is small.
    // keep grows full-length locally; remove's residual shares with the
    // third zone (sibling).
    if (parentRemove == S) {
        bool keepIsA = (parentKeep->a.get() == keep);
        std::unique_ptr<Node> keepNode    =
            std::move(keepIsA ? parentKeep->a : parentKeep->b);
        std::unique_ptr<Node> siblingNode =
            std::move(keepIsA ? parentKeep->b : parentKeep->a);

        bool removeIsA = (S->a.get() == remove);
        std::unique_ptr<Node> removeNode =
            std::move(removeIsA ? S->a : S->b);

        auto newSplit = std::make_unique<Node>();
        newSplit->vertical = S->vertical;
        newSplit->firstPx  = S->firstPx;       // keep S's absolute boundary
        if (removeIsA) {
            newSplit->a = std::move(removeNode);
            newSplit->b = std::move(siblingNode);
        } else {
            newSplit->a = std::move(siblingNode);
            newSplit->b = std::move(removeNode);
        }

        if (keepIsA) {
            parentKeep->a = std::move(keepNode);
            parentKeep->b = std::move(newSplit);
        } else {
            parentKeep->a = std::move(newSplit);
            parentKeep->b = std::move(keepNode);
        }

        std::unique_ptr<Node> subtree = std::move(removeIsA ? S->b : S->a);
        ReplaceNode(S, std::move(subtree));
        return;
    }

    // CAS 3: `keep` spans the big side (it IS S's child). It swallows
    // `remove` and the third zone (sibling) stretches to fill the hole.
    if (parentKeep == S) {
        bool removeIsA = (parentRemove->a.get() == remove);
        std::unique_ptr<Node> siblingNode =
            std::move(removeIsA ? parentRemove->b : parentRemove->a);

        bool keepIsA = (S->a.get() == keep);
        std::unique_ptr<Node> keepNode =
            std::move(keepIsA ? S->a : S->b);

        if (removeIsA) {
            parentRemove->a = std::move(keepNode);
            parentRemove->b = std::move(siblingNode);
        } else {
            parentRemove->a = std::move(siblingNode);
            parentRemove->b = std::move(keepNode);
        }

        std::unique_ptr<Node> subtree = std::move(keepIsA ? S->b : S->a);
        ReplaceNode(S, std::move(subtree));
        return;
    }

    // FALLBACK: deeply nested both sides — just collapse `remove`'s parent.
    std::unique_ptr<Node> promoted = std::move(
        parentRemove->a.get() == remove ? parentRemove->b : parentRemove->a);
    ReplaceNode(parentRemove, std::move(promoted));
}

// The two leaves bordering split `s`'s separator, resolved geometrically at
// `mousePos` by sampling one pixel inside each child along the cursor.

void ZoneLayout::RemoveLeaf(Node* leaf) {
    Node* parent = ParentOf(leaf);
    if (!parent) return;
    std::unique_ptr<Node> sibling =
        std::move(parent->a.get() == leaf ? parent->b : parent->a);
    *parent = std::move(*sibling);
}

// Replace `leaf` with a split: old content (all its tabs) one side, a FRESH
// single-Viewport zone on the other.
void ZoneLayout::SplitLeaf(Node* leaf, bool vertical, bool freshFirst) {
    auto oldContent = std::make_unique<Node>();
    oldContent->tabs      = std::move(leaf->tabs);
    oldContent->activeTab = leaf->activeTab;

    auto fresh = std::make_unique<Node>();
    fresh->tabs.push_back(Tab{CoreEditor::Viewport, {}});
    fresh->activeTab = 0;

    // Fresh zone ≈ 30% of the leaf's extent along the split axis, in
    // ABSOLUTE px (clamped to the min zone size). firstPx is the FIRST
    // child's extent, so it depends on which side the fresh zone is on.
    float ext = vertical ? leaf->size.x : leaf->size.y;
    float freshPx = std::max(kMinZonePx_, ext * 0.30f);

    leaf->tabs.clear();
    leaf->activeTab = 0;
    leaf->vertical = vertical;
    if (freshFirst) {
        leaf->a = std::move(fresh);
        leaf->b = std::move(oldContent);
        leaf->firstPx = freshPx;               // first child = fresh
    } else {
        leaf->a = std::move(oldContent);
        leaf->b = std::move(fresh);
        leaf->firstPx = std::max(kMinZonePx_, ext - freshPx); // first = old
    }
}

// Split `leaf` 50/50, placing the carried-in tab `moved` in the FRESH half and
// the leaf's existing tabs in the other. Used by the tab-drag detach/move.

void ZoneLayout::SplitLeafDuplicate(Node* leaf, bool vertical) {
    auto first  = std::make_unique<Node>();
    first->tabs      = leaf->tabs;          // copy (snapshot)
    first->activeTab = leaf->activeTab;

    auto second = std::make_unique<Node>();
    second->tabs      = leaf->tabs;         // copy (snapshot)
    second->activeTab = leaf->activeTab;

    // First child's extent = the mouse position along the split axis, in
    // ABSOLUTE px (clamped to the min zone size both sides).
    ImVec2 m = ImGui::GetIO().MousePos;
    float ext = vertical ? leaf->size.x : leaf->size.y;
    float fpx = vertical ? (m.x - leaf->pos.x) : (m.y - leaf->pos.y);

    leaf->tabs.clear();
    leaf->activeTab = 0;
    leaf->vertical = vertical;
    leaf->firstPx  = std::clamp(fpx, kMinZonePx_,
                                std::max(kMinZonePx_, ext - kMinZonePx_));
    leaf->a = std::move(first);
    leaf->b = std::move(second);
}

// ── Split-guide preview: a white line across the hovered window ───────────────
void ZoneLayout::DrawSplitPreview() {
    if (!splitArm_.active) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        splitArm_ = SplitArm{};
        return;
    }

    ImVec2 m = ImGui::GetIO().MousePos;
    Node*  leaf = (overlayHov_) ? LeafAt(m) : nullptr;
    if (!leaf) { RequestCursor(Cursor::Block); return; }

    RequestCursor(splitArm_.vertical ? Cursor::SplitVertical
                                     : Cursor::SplitHorizontal);
    ImDrawList* dl = ImGui::GetWindowDrawList();   // below floating windows
    ImU32 col = ImGui::GetColorU32(DesignSystem::DesignSystem::Instance().GetColor(
        DesignSystem::Tok::C_ZoneOverlay_SplitLine));

    if (splitArm_.vertical) {
        float x = std::clamp(m.x, leaf->pos.x + 4.0f,
                             leaf->pos.x + leaf->size.x - 4.0f);
        dl->AddLine(ImVec2(x, leaf->pos.y),
                    ImVec2(x, leaf->pos.y + leaf->size.y), col, 2.0f);
    } else {
        float y = std::clamp(m.y, leaf->pos.y + 4.0f,
                             leaf->pos.y + leaf->size.y - 4.0f);
        dl->AddLine(ImVec2(leaf->pos.x, y),
                    ImVec2(leaf->pos.x + leaf->size.x, y), col, 2.0f);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        SplitLeafDuplicate(leaf, splitArm_.vertical);
        splitArm_ = SplitArm{};
    }
}

// ── Rounded frame + rounded-corner clip, per leaf, on the overlay ────────────
// ImGui child windows draw bg/border BEFORE content, and our nested editor
// children draw on top — so a native border is overdrawn at the corners.
// We instead, on the overlay draw list (above ALL editor content): mask the
// four outside-arc corner nibs with the background colour (this emulates a
// rounded clip — nothing pokes past the rounding) then stroke the rounded
// border on top, so the border is strictly the visual limit.

void ZoneLayout::PickPrimarySeparator(Node* n, float gap) {
    if (n->isLeaf()) return;
    PickPrimarySeparator(n->a.get(), gap);
    PickPrimarySeparator(n->b.get(), gap);

    ImVec2 m = ImGui::GetIO().MousePos;
    ImVec2 bp, bs;
    if (n->vertical) {
        float x = n->a->pos.x + n->a->size.x;
        bp = ImVec2(x, n->pos.y); bs = ImVec2(gap, n->size.y);
    } else {
        float y = n->a->pos.y + n->a->size.y;
        bp = ImVec2(n->pos.x, y); bs = ImVec2(n->size.x, gap);
    }
    if (m.x >= bp.x && m.x <= bp.x + bs.x &&
        m.y >= bp.y && m.y <= bp.y + bs.y)
        primarySep_ = n;
}

// ── Pass 1: draw every leaf (a real ImGui window each) ───────────────────────

void ZoneLayout::DrawNode(Node* n, float gap) {
    if (n->isLeaf()) { HandleAddArea(n, gap); return; }
    DrawNode(n->a.get(), gap);
    DrawNode(n->b.get(), gap);
    DrawSeparator(n, gap);

    // Join preview & commit. The pair of target leaves is FROZEN at the
    // right-click position (join_.anchor) so it never changes while the mouse
    // moves; the hovered one survives, the other is removed.
    if (join_.active && join_.splitNode == n) {
        ImDrawList* dl = ImGui::GetWindowDrawList();   // below floating windows
        auto& ds       = DesignSystem::DesignSystem::Instance();
        const float rnd = ds.GetFloat(DesignSystem::Tok::C_Window_CornerRadius)
                          * ds.GetGlobalScale();

        Node* leftLeaf  = nullptr;
        Node* rightLeaf = nullptr;
        BorderLeaves(n, &leftLeaf, &rightLeaf, join_.anchor);

        Node* hovered = LeafAt(ImGui::GetIO().MousePos);
        const bool valid = hovered && leftLeaf && rightLeaf &&
                           (hovered == leftLeaf || hovered == rightLeaf);

        if (!valid) {
            RequestCursor(Cursor::Block);
            // Menu-Join: a click on an invalid target cancels. Drag-Join:
            // the corner-drag owns the gesture, UpdateAddArea handles it.
            if (!join_.fromDrag &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                join_ = JoinState{};
        } else {
            // The zone the mouse is over is the one ABSORBED; the opposite
            // border zone survives and grows into the freed space.
            Node* remove = hovered;
            Node* keep   = (hovered == leftLeaf) ? rightLeaf : leftLeaf;

            // Directional join cursor: which way is `remove` from `keep`?
            if (n->vertical)
                RequestCursor(remove->pos.x > keep->pos.x
                              ? Cursor::JoinRight : Cursor::JoinLeft);
            else
                RequestCursor(remove->pos.y > keep->pos.y
                              ? Cursor::JoinDown : Cursor::JoinUp);

            ImVec2 final_p0{}, final_p1{};
            struct Rect { ImVec2 p0, p1; };
            std::vector<Rect> residuals;

            if (n->vertical) {
                final_p0.x = std::min(leftLeaf->pos.x, rightLeaf->pos.x);
                final_p1.x = std::max(leftLeaf->pos.x + leftLeaf->size.x,
                                      rightLeaf->pos.x + rightLeaf->size.x);
                final_p0.y = std::max(leftLeaf->pos.y, rightLeaf->pos.y);
                final_p1.y = std::min(leftLeaf->pos.y + leftLeaf->size.y,
                                      rightLeaf->pos.y + rightLeaf->size.y);

                if (leftLeaf->pos.y < final_p0.y)
                    residuals.push_back({ leftLeaf->pos,
                        ImVec2(leftLeaf->pos.x + leftLeaf->size.x,
                               final_p0.y) });
                if (leftLeaf->pos.y + leftLeaf->size.y > final_p1.y)
                    residuals.push_back({
                        ImVec2(leftLeaf->pos.x, final_p1.y),
                        ImVec2(leftLeaf->pos.x + leftLeaf->size.x,
                               leftLeaf->pos.y + leftLeaf->size.y) });
                if (rightLeaf->pos.y < final_p0.y)
                    residuals.push_back({ rightLeaf->pos,
                        ImVec2(rightLeaf->pos.x + rightLeaf->size.x,
                               final_p0.y) });
                if (rightLeaf->pos.y + rightLeaf->size.y > final_p1.y)
                    residuals.push_back({
                        ImVec2(rightLeaf->pos.x, final_p1.y),
                        ImVec2(rightLeaf->pos.x + rightLeaf->size.x,
                               rightLeaf->pos.y + rightLeaf->size.y) });
            } else {
                final_p0.y = std::min(leftLeaf->pos.y, rightLeaf->pos.y);
                final_p1.y = std::max(leftLeaf->pos.y + leftLeaf->size.y,
                                      rightLeaf->pos.y + rightLeaf->size.y);
                final_p0.x = std::max(leftLeaf->pos.x, rightLeaf->pos.x);
                final_p1.x = std::min(leftLeaf->pos.x + leftLeaf->size.x,
                                      rightLeaf->pos.x + rightLeaf->size.x);

                if (leftLeaf->pos.x < final_p0.x)
                    residuals.push_back({ leftLeaf->pos,
                        ImVec2(final_p0.x,
                               leftLeaf->pos.y + leftLeaf->size.y) });
                if (leftLeaf->pos.x + leftLeaf->size.x > final_p1.x)
                    residuals.push_back({
                        ImVec2(final_p1.x, leftLeaf->pos.y),
                        ImVec2(leftLeaf->pos.x + leftLeaf->size.x,
                               leftLeaf->pos.y + leftLeaf->size.y) });
                if (rightLeaf->pos.x < final_p0.x)
                    residuals.push_back({ rightLeaf->pos,
                        ImVec2(final_p0.x,
                               rightLeaf->pos.y + rightLeaf->size.y) });
                if (rightLeaf->pos.x + rightLeaf->size.x > final_p1.x)
                    residuals.push_back({
                        ImVec2(final_p1.x, rightLeaf->pos.y),
                        ImVec2(rightLeaf->pos.x + rightLeaf->size.x,
                               rightLeaf->pos.y + rightLeaf->size.y) });
            }

            dl->AddRectFilled(keep->pos,
                ImVec2(keep->pos.x + keep->size.x,
                       keep->pos.y + keep->size.y),
                ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_ZoneOverlay_JoinKeep)));

            ImVec2 rm0(std::max(remove->pos.x, final_p0.x),
                       std::max(remove->pos.y, final_p0.y));
            ImVec2 rm1(std::min(remove->pos.x + remove->size.x, final_p1.x),
                       std::min(remove->pos.y + remove->size.y, final_p1.y));
            if (rm1.x > rm0.x && rm1.y > rm0.y)
                dl->AddRectFilled(rm0, rm1, ImGui::GetColorU32(ds.GetColor(
                    DesignSystem::Tok::C_ZoneOverlay_JoinRemove)));

            for (const auto& res : residuals)
                dl->AddRectFilled(res.p0, res.p1, ImGui::GetColorU32(ds.GetColor(
                    DesignSystem::Tok::C_ZoneOverlay_JoinResidual)));

            dl->AddRect(final_p0, final_p1,
                        ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_ZoneOverlay_JoinFrame)),
                        rnd, 0, 2.0f);

            // Menu-Join commits on CLICK; drag-Join commits on RELEASE
            // (the mouse button is held down for the whole corner-drag).
            bool commit = join_.fromDrag
                ? !ImGui::IsMouseDown(ImGuiMouseButton_Left)
                : ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (commit) {
                JoinLeaves(keep, remove);
                join_   = JoinState{};
                addArm_ = AddArm{};          // end the corner-drag too
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            join_   = JoinState{};
            addArm_ = AddArm{};
        }
    }
}

// True if `node` still exists somewhere under root_ (used to detect a stale
// pointer after a tree restructure).

void ZoneLayout::HandleAddArea(Node* leaf, float gap) {
    (void)gap;
    if (addArm_.armed) return;               // a gesture is already running
    if (join_.active || splitArm_.active || sepDragging_) return;
    if (tabDrag_.armed || tabDrag_.active) return;   // a tab drag owns the mouse
    if (!overlayHov_) return;                // a floating window is on top

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs     = ds.GetGlobalScale();
    const float corner = 14.0f * gs;
    ImVec2 m = ImGui::GetIO().MousePos;

    float lL = leaf->pos.x, lR = leaf->pos.x + leaf->size.x;
    float lT = leaf->pos.y, lB = leaf->pos.y + leaf->size.y;

    // Directional corner cursor from the corner signs.
    auto cornerCursor = [](float sx, float sy) {
        if (sy < 0.0f) return sx < 0.0f ? Cursor::CornerUL : Cursor::CornerUR;
        return            sx < 0.0f ? Cursor::CornerDL : Cursor::CornerDR;
    };

    // The corner hot-zone is the INNER square of the leaf only:
    // [lL, lL+corner] × [lT, lT+corner], etc. — it does NOT bleed outside
    // the leaf rect. So when the mouse approaches a 3-window junction from
    // the middle of a LARGE window's border (a point that is a corner of the
    // two small windows but only an EDGE-mid of the large one), no corner of
    // the small windows arms here (the cursor is on the large window's side,
    // outside their rects). The separator owns it instead → resize cursor.
    // Exactly Blender: a mid-border is not a corner. The deep-tree corner
    // resolution is what makes add-vs-join robust at junctions.
    struct C { float x0, y0, x1, y1, sx, sy; };
    const C corners[4] = {
        { lL,          lT,          lL + corner, lT + corner, -1, -1 },
        { lR - corner, lT,          lR,          lT + corner, +1, -1 },
        { lL,          lB - corner, lL + corner, lB,          -1, +1 },
        { lR - corner, lB - corner, lR,          lB,          +1, +1 },
    };
    for (const C& c : corners) {
        if (m.x >= c.x0 && m.x <= c.x1 && m.y >= c.y0 && m.y <= c.y1) {
            RequestCursor(cornerCursor(c.sx, c.sy));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                addArm_          = AddArm{};
                addArm_.armed    = true;
                addArm_.start    = m;
                addArm_.leaf     = leaf;
                addArm_.cornerSx = c.sx;
                addArm_.cornerSy = c.sy;
            }
            return;                          // one corner is enough
        }
    }
}

// Collapse the add-spawned split back to its original content (cancel).
void ZoneLayout::CancelAddSpawn() {
    if (addArm_.spawned && addArm_.split && NodeAlive(addArm_.split)) {
        Node* sp = addArm_.split;
        std::unique_ptr<Node> survivor =
            std::move(addArm_.freshFirst ? sp->b : sp->a);
        *sp = std::move(*survivor);
    }
    addArm_.spawned = false;
    addArm_.split   = nullptr;
}


// ── Add-area phase 2 (ONCE per frame, tree-independent) ───────────────────────
// Phases:
//  1. While the mouse is still INSIDE the corner zone → plain hand cursor,
//     nothing happens, mode stays free.
//  2. Once it LEAVES the corner zone and the mode is not locked yet, resolve
//     a CANDIDATE by where the mouse is:
//       • geometrically inside the corner's own leaf rect → ADD (cursor
//         shows the split axis); the new zone SPAWNS only past the min-size
//         threshold and that LOCKS mode = Add;
//       • over an adjacent leaf → JOIN; showing the join_ preview LOCKS
//         mode = Join;
//       • over a non-adjacent leaf → not-allowed, nothing.
//  3. Once LOCKED the mode never switches again:
//       • Add  → the fresh zone follows the mouse along the drag axis;
//       • Join → fully delegated to the join_ preview (the mouse freely
//         chooses which side to absorb, exactly like a normal Join, even
//         back over the corner zone).
// RMB / Esc cancels everything. Release commits the current mode.
void ZoneLayout::UpdateAddArea() {
    if (!addArm_.armed) return;

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs   = ds.GetGlobalScale();
    const float barH = 24.0f * gs;                // spawned zone min size
    const float corner = 14.0f * gs;              // corner zone radius
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 m = io.MousePos;

    // Cancel: RMB / Esc.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        CancelAddSpawn();
        join_   = JoinState{};
        addArm_ = AddArm{};
        return;
    }

    // ───────────────────────── LOCKED: Join ──────────────────────────────
    // Hand the whole gesture to the join_ preview. The mouse may roam over
    // either side (incl. back over the corner zone) — join_ resolves the
    // pair from its anchor and the cursor, exactly like a menu Join, and
    // commits on release (fromDrag). No switching back to Add.
    if (addArm_.mode == AddMode::Join) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            addArm_ = AddArm{};                   // join_ block commits it
        return;
    }

    // ───────────────────────── LOCKED: Add ───────────────────────────────
    if (addArm_.mode == AddMode::Add) {
        RequestCursor(addArm_.vertical ? Cursor::SplitVertical
                                       : Cursor::SplitHorizontal);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            addArm_ = AddArm{};                   // release → commit as-is
            return;
        }
        if (addArm_.split && NodeAlive(addArm_.split)) {
            Node* sp = addArm_.split;
            // Follow the mouse in ABSOLUTE px; Layout() enforces the min
            // zone size on both sides (min = editor top-bar height).
            float pos = addArm_.vertical ? (m.x - sp->pos.x)
                                         : (m.y - sp->pos.y);
            sp->firstPx = pos;
        } else {
            addArm_ = AddArm{};                   // split vanished — bail
        }
        return;
    }

    // ───────────────────── FREE: still resolving ─────────────────────────
    // The corner the drag started from, in absolute coords (NOT the click
    // point — the whole corner zone must behave identically, so direction
    // is measured from the true corner, not wherever inside it we clicked).
    ImVec2 cornerPt(
        addArm_.cornerSx < 0.0f ? addArm_.leaf->pos.x
                                : addArm_.leaf->pos.x + addArm_.leaf->size.x,
        addArm_.cornerSy < 0.0f ? addArm_.leaf->pos.y
                                : addArm_.leaf->pos.y + addArm_.leaf->size.y);

    // Phase 1: still inside the corner zone → CornerActions cursor, nothing
    // happens, mode stays free. Only leaving the corner resolves the mode.
    bool inCornerZone =
        std::abs(m.x - cornerPt.x) <= corner &&
        std::abs(m.y - cornerPt.y) <= corner;
    addArm_.leftCorner = !inCornerZone;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        addArm_ = AddArm{};                       // released early → no-op
        return;
    }
    if (!addArm_.leftCorner) {
        RequestCursor(
            addArm_.cornerSy < 0.0f
                ? (addArm_.cornerSx < 0.0f ? Cursor::CornerUL
                                           : Cursor::CornerUR)
                : (addArm_.cornerSx < 0.0f ? Cursor::CornerDL
                                           : Cursor::CornerDR));
        return;
    }

    if (!NodeAlive(addArm_.leaf)) { addArm_ = AddArm{}; return; }

    // ADD vs JOIN is decided by the SAME mid-gap ownership rule the
    // separators use: LeafAt(m) tells which leaf the mouse belongs to (a
    // point in the gap belongs to the nearer side — no overlap, no double
    // count). If it is still the corner's own leaf → ADD; if it is a
    // different (adjacent) leaf → JOIN. Using the leaf's raw logical rect
    // (PtInRect) was the bug: that rect reaches the mid-gap AND can be
    // stale after a re-layout, so near a junction it flip-flopped between
    // ADD and JOIN. The corner guard (leftCorner + barH reach) already
    // prevents an accidental ADD right next to the corner.
    Node* underMouse = LeafAt(m);

    if (underMouse == addArm_.leaf) {
        // ADD candidate. Direction measured FROM THE CORNER (consistent for
        // the whole corner zone): pulling away from the corner along X →
        // vertical split (side/side); along Y → horizontal split (stacked).
        ImVec2 d(m.x - cornerPt.x, m.y - cornerPt.y);
        bool vertical = std::abs(d.x) > std::abs(d.y);  // true = side/side
        float reach   = vertical ? std::abs(d.x) : std::abs(d.y);
        RequestCursor(vertical ? Cursor::SplitVertical
                               : Cursor::SplitHorizontal);
        if (reach < barH) return;                 // not big enough yet

        bool freshFirst = vertical ? (addArm_.cornerSx < 0.0f)
                                   : (addArm_.cornerSy < 0.0f);
        addArm_.vertical   = vertical;
        addArm_.freshFirst = freshFirst;
        SplitLeaf(addArm_.leaf, vertical, freshFirst);
        addArm_.split   = addArm_.leaf;           // leaf is now the split
        addArm_.spawned = true;
        addArm_.mode    = AddMode::Add;           // LOCK
        return;
    }

    // Mouse over a DIFFERENT leaf → JOIN candidate if it is adjacent to the
    // corner leaf. Reuse the same LeafAt() result as the ADD test above so
    // the decision is single-sourced and can't disagree within a frame.
    Node* mouseLeaf = underMouse;
    if (mouseLeaf && mouseLeaf != addArm_.leaf) {
        Node* split = AdjacencySplit(addArm_.leaf, mouseLeaf);
        if (split) {
            join_.active         = true;
            join_.splitNode      = split;
            join_.borderVertical = split->vertical;
            join_.fromDrag       = true;
            join_.anchor = ImVec2(
                (addArm_.leaf->pos.x + addArm_.leaf->size.x * 0.5f +
                 mouseLeaf->pos.x + mouseLeaf->size.x * 0.5f) * 0.5f,
                (addArm_.leaf->pos.y + addArm_.leaf->size.y * 0.5f +
                 mouseLeaf->pos.y + mouseLeaf->size.y * 0.5f) * 0.5f);
            addArm_.mode = AddMode::Join;          // LOCK
            return;
        }
    }
    // Non-adjacent / off-window → action impossible here, stay free.
    RequestCursor(Cursor::Block);
}

// ── Public entry ──────────────────────────────────────────────────────────────

} // namespace App

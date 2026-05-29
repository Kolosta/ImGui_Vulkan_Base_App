#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  ZoneLayout — a Blender-style DYNAMIC zone tree, with NO ImGui native
//  docking UX (no window drag&drop, no drop overlays).
//
//  The screen is a binary split tree: every internal node splits its rect
//  horizontally or vertically at a ratio; every leaf hosts one editor and a
//  top bar with a shared left-most editor-selector (zones are
//  interchangeable). Borders carry a token-sized gap that is the drag
//  hit-zone (cursor turns into a resize arrow; dragging resizes the two
//  neighbours, correlated). Dragging from an outer corner / a junction tears
//  a NEW viewport zone in the drag direction. Right-clicking a border opens
//  Swap / Join. Join shows a directional chevron cursor, dims the area to be
//  replaced, lightens the surviving one, draws the merged outline, Esc
//  cancels, click merges (the survivor fills the freed space; non-adjacent
//  target → X cursor, click does nothing).
// ─────────────────────────────────────────────────────────────────────────────

enum class EditorKind {
    Viewport, Outliner, Timeline, DevPanels, Count
};
const char* EditorKindName(EditorKind k);
const char* EditorKindIcon(EditorKind k);

// Per-leaf VIEW state. The project / artboards are shared (App::Project),
// not stored here — every Viewport shows the same pages. Only the view
// (camera, zoom, unit, pending view requests) is per-zone.
struct EditorState {
    // Camera: screen = canvasMin + (doc - pan) * zoom.
    ImVec2 pan{0, 0};
    float  zoom = 1.0f;
    int    docUnit = 0;             // index into the viewport unit table
    // Pending view requests, consumed by this leaf's RenderViewport only.
    bool   reqFitDoc      = false;  // frame the project's artboards
    bool   reqResetOrigin = false;
    bool   openNewDoc     = false;  // request opening the New Artboard popup
};

// One tab inside a zone: an editor kind plus its own independent view state.
// Moving a tab between zones carries its state (Tab is a value type).
struct Tab {
    EditorKind  kind = EditorKind::Viewport;
    EditorState state;
};

// drawEditor(kind, contentSize, leafState) — called once per leaf.
using DrawEditorFn  = std::function<void(EditorKind, ImVec2, EditorState&)>;
// topBarExtras(kind, leafState) — drawn in the leaf top bar, right of the
// editor selector (e.g. the Viewport "+" new-document button). Optional.
using TopBarExtraFn = std::function<void(EditorKind, EditorState&)>;

class ZoneLayout {
public:
    ZoneLayout();

    // Draw the whole tree into the current window content region.
    void Render(const DrawEditorFn& drawEditor,
                const TopBarExtraFn& topBarExtras = {});

    // The editor state of the Viewport leaf the mouse is currently over
    // (and not occluded by a floating window). nullptr if none — camera
    // actions become no-ops. Valid after Render() of the current frame.
    EditorState* HoveredEditorState() { return hoveredState_; }
    // Called by RenderViewport when it determines its leaf is the genuinely
    // hovered, non-occluded one (strict IsWindowHovered). Last writer wins.
    void SetHoveredEditorState(EditorState* st) { hoveredState_ = st; }

    // Switch the editor kind of the leaf currently under the mouse (resolved
    // each Render() into hoveredLeaf_). No-op if no zone is hovered. Used by
    // the editor.* switch shortcuts so they target the zone the user points at.
    // Replaces the ACTIVE tab's kind (same behaviour as the old single-editor
    // leaf). Defined out-of-line because it needs the private ActiveTab helper.
    void SetHoveredEditor(EditorKind k);

    // Tab navigation (driven by shortcuts).
    //   Cycle next/previous tab of the HOVERED zone (wraps).
    void HoveredTabCycle(int dir);
    //   Select the first / last tab of the ACTIVE zone.
    void ActiveTabSelectEdge(bool last);

private:
    struct Node {
        // Leaf when children are null. Split otherwise.
        bool   vertical = false;     // split orientation (true = side by side)
        // Blender-style ABSOLUTE sizing: the first child's extent in px along
        // the split axis. <0 = uninitialised (derive 50/50 the first frame).
        // Dragging a separator changes ONLY this split's firstPx, so nested
        // sub-zones keep their own absolute sizes — no proportional/flex
        // domino. Sizes are only rescaled when the WHOLE window resizes.
        float  firstPx  = -1.0f;
        float  initRatio = 0.5f;     // only used to derive firstPx the very
                                     // first Layout (then absolute forever)
        // The `usable` extent (along the split axis) seen on the previous
        // Layout. When the window resizes, firstPx is rescaled by
        // new_usable / lastUsable so every split KEEPS ITS RATIO — the
        // rightmost / bottom zones grow and shrink proportionally instead of
        // absorbing the whole delta. -1 = not yet laid out.
        float  lastUsable = -1.0f;
        std::unique_ptr<Node> a, b;  // children (split only)
        // Leaf only: the zone's tabs (≥1 when a leaf) and the active one.
        // Only the active tab is rendered; the others are dormant editors.
        std::vector<Tab> tabs;
        int              activeTab = 0;
        // Filled each frame by layout pass:
        ImVec2 pos{}, size{};
        bool   isLeaf() const { return !a && !b; }
    };

    // Active-tab accessors (leaf only). activeTab is clamped defensively so a
    // stale index after a tab erase never reads out of bounds.
    static Tab& ActiveTab(Node* n) {
        int i = n->tabs.empty() ? 0
              : (n->activeTab < 0 ? 0
              : (n->activeTab >= (int)n->tabs.size()
                 ? (int)n->tabs.size() - 1 : n->activeTab));
        return n->tabs[(size_t)i];
    }
    static EditorKind  LeafKind(Node* n)  { return ActiveTab(n).kind; }
    static EditorState& LeafState(Node* n) { return ActiveTab(n).state; }

    // Interaction state for the pending Join gesture.
    struct JoinState {
        bool   active = false;
        Node*  splitNode = nullptr;  // the split whose border was clicked
        ImVec2 anchor{};             // mouse at right-click
        bool   borderVertical = false;
        bool   fromDrag = false;     // started by a corner-drag (commit on
                                     // release, not on click)
    };

    // Interaction state for the pending corner-drag gesture (Blender-style).
    // Armed at click on a leaf corner. The gesture then resolves every frame
    // by where the mouse is:
    //   • inside the corner's own leaf  → ADD a new zone (fresh on the
    //     corner side, axis from the dominant drag direction), spawned only
    //     past the min-size threshold, then follows the mouse;
    //   • over another ADJACENT leaf    → JOIN (hovered absorbed), shown via
    //     the normal join_ preview, committed on release;
    //   • over a NON-adjacent leaf      → not-allowed cursor, no-op;
    //   • still on the corner / too short→ plain hand cursor, no-op.
    // Locked once a concrete action appears (spawn / join preview). Before
    // that the gesture is still free; after, it can no longer switch.
    enum class AddMode { None, Add, Join };

    struct AddArm {
        bool    armed     = false;
        ImVec2  start{};
        Node*   leaf      = nullptr; // the leaf that OWNS the grabbed corner
        float   cornerSx  = 0.0f;    // corner X side: -1 = left,  +1 = right
        float   cornerSy  = 0.0f;    // corner Y side: -1 = top,   +1 = bottom
        bool    leftCorner= false;   // mouse is still inside the corner zone
        AddMode mode      = AddMode::None;  // locked once engaged
        bool    spawned   = false;   // the fresh zone exists in the tree
        Node*   split     = nullptr; // the split that hosts the fresh zone
        bool    vertical  = false;   // committed split axis (true = side/side)
        bool    freshFirst= false;   // fresh zone is first child (left/top)
    };

    // Interaction state for the pending Split (from the context menu).
    struct SplitArm {
        bool active   = false;
        bool vertical = false;       // true = vertical guide line (left|right)
    };

    // Graphite-style tab drag. Armed on press over a tab (or, for a solitary
    // zone, the empty menu-bar background); promoted to a live drag past a
    // small movement threshold. On release it reorders / moves / detaches.
    struct TabDrag {
        bool   armed   = false;      // pressed, below the move threshold
        bool   active  = false;      // past threshold → live drag
        Node*  srcLeaf = nullptr;    // leaf the dragged tab belongs to
        int    srcTab  = -1;         // its index in srcLeaf->tabs
        Tab    payload;              // snapshot taken when the drag goes live
        ImVec2 grabPos{};            // mouse position at arm time
    };
    // Where the mouse is over the target leaf — picks the drop outcome.
    enum class DropRegion { None, Center, Left, Right, Top, Bottom };

    TabDrag    tabDrag_;
    // Animated white drop preview. previewLeaf_ tracks which leaf the preview
    // is currently anchored to; when it changes the rect snaps instantly,
    // otherwise it eases toward the target region rect.
    Node*      previewLeaf_  = nullptr;
    ImVec4     previewRect_{};        // (min.x, min.y, max.x, max.y), animated
    bool       previewInit_  = false;
    // The leaf whose tab/zone was last activated (clicked/focused). Drives the
    // "active zone" first/last-tab shortcuts. Validated via NodeAlive.
    Node*      activeLeaf_   = nullptr;
    // Per-frame scratch: screen rects of the hovered leaf's tab-bar tabs, used
    // for the insertion line and reorder hit-test. Cleared each frame.
    struct TabRect { ImVec2 mn, mx; };
    std::vector<TabRect> tabRects_;
    Node*      tabRectsLeaf_ = nullptr;  // leaf tabRects_ was captured for

    // Solo-zone tab-bar reveal during a drag: hovering the menu-bar band of a
    // solitary zone for a moment slides a 1-tab bar in (animated), turning it
    // into a drop target. revealLeaf_ is the zone being revealed; revealAnim_
    // eases 0→1 (shown) and back; revealDwell_ times the hover-in / hover-out
    // delays. Driven by UpdateTabDrag, consumed by DrawLeaf the next frame.
    Node*      revealLeaf_   = nullptr;
    float      revealAnim_   = 0.0f;   // 0 hidden … 1 fully shown (animated)
    float      revealTarget_ = 0.0f;   // where revealAnim_ eases toward
    float      revealDwell_  = 0.0f;   // seconds the cursor has dwelt in/out

    // Custom SVG mouse cursors (icons in resources/icons/cursors/). None =
    // leave the standard ImGui/OS cursor untouched. Requested per frame via
    // RequestCursor(); applied once at the end of Render().
    enum class Cursor {
        None,
        // Hovering a corner (before the drag starts) — directional per which
        // corner of the editor the cursor is in.
        CornerUL, CornerUR, CornerDL, CornerDR,
        Block,           // action impossible here
        SplitVertical,   // add: vertical split (side/side)
        SplitHorizontal, // add: horizontal split (stacked)
        JoinUp, JoinDown, JoinLeft, JoinRight  // join: absorbed side
    };
    Cursor      reqCursor_ = Cursor::None;     // requested this frame
    void RequestCursor(Cursor c) { reqCursor_ = c; }

    // Minimum zone extent (px) = the editor top-bar height. An absolute
    // value, NOT a % of the window, so add/resize behave identically
    // whatever the window size. Recomputed each Render() from the DPI scale.
    float       kMinZonePx_ = 24.0f;
    // Hide the OS cursor and draw the requested SVG icon at the mouse.
    void ApplyCursor();

    std::unique_ptr<Node> root_;
    JoinState    join_;
    AddArm       addArm_;
    SplitArm     splitArm_;
    EditorState* hoveredState_ = nullptr;  // recomputed every Render()
    Node*        hoveredLeaf_   = nullptr;  // leaf under the mouse (any editor),
                                           // recomputed every Render() — drives
                                           // editor.* switch shortcuts
    Node*        primarySep_   = nullptr;  // split whose separator is hovered
    Node*        primarySepDwell_ = nullptr; // last split the dwell timer tracks
    float        primarySepTimer_ = 0.0f;  // seconds the mouse has dwelt on it;
                                           // the highlight shows only past a
                                           // short delay so quick moves between
                                           // editors don't flash a resize line
    Node*        sepDragging_  = nullptr;  // split whose separator is dragging
    ImVec2       contextMenuPos_{0, 0};    // mouse pos captured at right-click
    Node*        menuSplit_    = nullptr;  // split whose context menu is open
    bool         menuOpenRequest_ = false; // ask to OpenPopup next (in body)
    bool         overlayHov_   = false;    // overlay is top-most under mouse
                                           // (false if a floating window is
                                           // above → suppresses all hit-test)

    static bool IsInSubtree(Node* subtree, Node* target);
    static void DimSubtree(Node* n, ImDrawList* dl, ImU32 col);
    static void HighlightSubtree(Node* n, ImDrawList* dl, ImU32 col);

    // Replace `target` in the tree by `replacement`.
    void ReplaceNode(Node* target, std::unique_ptr<Node> replacement);
    // Merge ONLY the two specified adjacent leaves (CAS 1/2/3).
    void JoinLeaves(Node* keep, Node* remove);

    // Recursively compute pos/size for the subtree.
    void Layout(Node* n, ImVec2 pos, ImVec2 size, float gap);
    // Pass 1: draw every leaf as a real ImGui window.
    void DrawLeaves(Node* n, float gap,
                    const DrawEditorFn& drawEditor,
                    const TopBarExtraFn& topBarExtras);
    // Pass 2: separators + join + add-area, on the overlay (above zones).
    void DrawNode(Node* n, float gap,
                  const DrawEditorFn& drawEditor,
                  const TopBarExtraFn& topBarExtras);
    // Draw one leaf (top bar selector + content child).
    void DrawLeaf(Node* n, float gap,
                  const DrawEditorFn& drawEditor,
                  const TopBarExtraFn& topBarExtras);
    // Separator between a split's two children: cursor, correlated drag,
    // right-click Swap/Join/Split menu, Join preview/commit.
    void DrawSeparator(Node* split, float gap);
    // The split-guide preview (white line across the hovered window).
    void DrawSplitPreview();
    // Per-leaf, on the overlay (top-most): cut the 4 rounded corners (mask
    // the outside-arc nibs in the background colour, emulating a rounded
    // clip) then stroke the rounded border ON TOP of all editor content.
    void DrawZoneFrames(Node* n);
    // The translucent, per-corner-coloured drag hot-zones of each leaf.
    void DrawCornerZones(Node* n);
    // Remove `leaf` from the tree: its sibling collapses into the parent.
    void RemoveLeaf(Node* leaf);
    // Parent split of `node` (nullptr if root). Searches from root_.
    Node* ParentOf(Node* node);
    // The two leaves bordering split `s`'s separator at `mousePos`.
    void BorderLeaves(Node* s, Node** outA, Node** outB, ImVec2 mousePos);
    // Smallest common ancestor split of two leaves IF they are directly
    // adjacent across its separator (i.e. that split has one leaf on each
    // side at their shared border). Returns nullptr if not adjacent.
    Node* AdjacencySplit(Node* leafA, Node* leafB);
    // Add-area, phase 1 (per leaf, idle only): detect the cursor near a
    // corner and ARM the gesture on click. Does NOT drive a live gesture.
    void HandleAddArea(Node* n, float gap);
    // Add-area, phase 2 (once per frame, tree-independent): drive the armed
    // gesture — spawn past threshold, follow mouse, commit/cancel. Survives
    // the tree restructure (keyed on addArm_.split, re-validated each frame).
    void UpdateAddArea();
    // Undo a still-pending add-spawn (collapse the fresh split back).
    void CancelAddSpawn();
    // True if `node` still exists somewhere under root_.
    bool NodeAlive(Node* node);
    // Deepest leaf whose rect contains p (separators count as belonging to
    // the nearer leaf — zones meet at the separator midline, no dead zone).
    Node* LeafAt(ImVec2 p);
    // Replace `leaf` with a split: old content one side, fresh Viewport other.
    void SplitLeaf(Node* leaf, bool vertical, bool freshFirst);
    // Split `leaf` in two at the mouse, DUPLICATING its editor + view state.
    void SplitLeafDuplicate(Node* leaf, bool vertical);
    // Pre-pass: pick the split whose separator hit-rect the mouse is over.
    void PickPrimarySeparator(Node* n, float gap);

    // Draw the tab bar of one leaf (only when it has >1 tab or the always-show
    // preference is on). Arms a tab drag on press; switches active tab on click.
    void DrawTabBar(Node* n, float barH, ImVec2 origin, ImVec2 size);
    // Once-per-frame: promote an armed tab drag past the threshold, compute the
    // drop region/preview, and on release reorder / move / detach the tab.
    void UpdateTabDrag(float gap);
    // Split `leaf` 50/50 placing `moved` (a tab carried from elsewhere) in the
    // fresh half. vertical = side-by-side; freshFirst = moved tab on left/top.
    void SplitLeafWithTab(Node* leaf, const Tab& moved,
                          bool vertical, bool freshFirst);
    // Drop region of the mouse over leaf `n` (center deadzone vs dominant edge).
    DropRegion DropRegionAt(Node* n, ImVec2 mouse) const;
    // The rect (min.x,min.y,max.x,max.y) a drop in `region` of `n` would create.
    ImVec4 DropTargetRect(Node* n, DropRegion region, float inset) const;
};

} // namespace App

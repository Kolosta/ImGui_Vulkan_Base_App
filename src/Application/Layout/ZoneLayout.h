#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>
#include <UI/Widgets/SidePanel.h>
#include "OutlinerState.h"
#include "EditorRegistry.h"

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

// Editors are identified by STRING id via EditorRegistry (see EditorRegistry.h),
// not a fixed enum, so modules/plugins can add their own. Core ids live in the
// CoreEditor:: namespace. The historical enum order is preserved ONLY as a
// migration table for old (v<4) .acu LAYOUT blobs — see ZoneLayoutCore.cpp.

// Per-leaf VIEW state. The project / artboards are shared (App::Project),
// not stored here — every Viewport shows the same pages. Only the view
// (camera, zoom, unit, pending view requests) is per-zone.
// ── Per-viewport PAGE LAYOUT (Lot 3) ──────────────────────────────────────────
// Pages have ONE shared position (Artboard::pos) used by Manual mode. An auto
// layout does NOT move the pages — it computes, PER VIEWPORT, a DISPLAY OFFSET
// applied to each page only when rendering/picking in that viewport. So two
// Viewports can show the same document in different arrangements at once.
enum class PageLayoutMode : uint8_t {
    Manual = 0,   // use Artboard::pos as-is (free move)
    LeftToRight,  // pages in a row, left→right
    RightToLeft,  // pages in a row, right→left
    TopToBottom,  // pages in a column, top→bottom
    BottomToTop,  // pages in a column, bottom→top
    Grid,         // wrapped grid (auto columns)
    BookLeft,     // 2-up spreads, first page on the LEFT
    BookRight,    // 2-up spreads, first page on the RIGHT
    SinglePage,   // ONE page at a time (the N-panel "Pages" tab switches it)
    SingleBookLeft,  // one SPREAD at a time, first page on the left
    SingleBookRight, // one spread at a time, first page on the right
};
const char* PageLayoutModeName(PageLayoutMode m);

struct PageLayout {
    PageLayoutMode mode = PageLayoutMode::Manual;
    float gap = 40.0f;          // doc-unit gutter between pages (auto modes)
    int   gridCols = 3;         // columns for Grid mode
    // For the Single Spread modes: show the FIRST page alone (a cover), then pair
    // 2-3, 4-5… (like a book's cover). Off → straight pairs 1-2, 3-4…
    bool  spreadCover = false;
    bool  singlePage = false;   // show only `pageIndex` in this viewport
    int   pageIndex = 0;        // which page when singlePage (clamped on use)
    // Per-viewport hidden pages (distinct from object Hide): page id → hidden.
    // Stored as ids so it survives reorder. Empty = all visible.
    std::vector<uint64_t> hiddenPages;
};

struct EditorState {
    // Camera: screen = canvasMin + (doc - pan) * zoom. DOUBLE end-to-end —
    // the Ink engine's unbounded-canvas requirement (docs/Ink README req. 9)
    // includes the viewport camera state itself.
    double panX = 0.0, panY = 0.0;
    double zoom = 1.0;
    int    docUnit = 0;             // index into the viewport unit table
    // PRINT PROOFING, per viewport (Ink::PrintPreview). Deliberately not a
    // document setting: proofing one viewport must leave the others — and the
    // symbol vignettes, which are views too — on the plain screen render.
    int    printPreview  = 0;       // 0 Normal · 1 Overprint · 2 Separations · 3 Flattener
    int    printChannels = 0x0F;    // C M Y K bits (Separations only)
    // Pending view requests, consumed by this leaf's RenderViewport only.
    bool   reqFitDoc       = false;  // frame the project's artboards
    bool   reqFitSelection = false;  // frame the selected/active object(s) (Numpad .)
    bool   reqResetOrigin  = false;
    bool   openNewDoc      = false;  // request opening the New Artboard popup
    // This viewport's page arrangement (Lot 3). Per-leaf, so each Viewport can
    // show a different layout / page subset of the SAME shared document.
    PageLayout pageLayout;
    // Ruler coordinate space: Viewport = absolute document coords; Page =
    // coords RELATIVE to the active/selected page's top-left (0,0 at the page
    // corner). Falls back to Viewport when no page is implied by the selection.
    enum class RulerSpace : uint8_t { Viewport = 0, Page };
    RulerSpace rulerSpace = RulerSpace::Viewport;
    // Which of the four rulers are visible (any combination). Default = the
    // legacy pair: top + left. Persisted in the LAY blob (v9).
    bool rulerTop = true, rulerLeft = true, rulerRight = false, rulerBottom = false;

    // Right-side reusable EditorSidePanel (the "N" panel). Generic UI state
    // (stage/width/tab) lives in `sidePanel`; viewport-specific options are kept
    // alongside it. The panel holds the "Pages" tab (Single* modes only).
    UI::SidePanelState sidePanel;
    bool   nPanelShowOrphans = true; // Pages tab: show page-less (orphan) objects

    // Screen rects of the floating UI overlays drawn OVER this viewport leaf's
    // canvas (tool palette, operator redo panel, …). The canvas hover-test excludes
    // them so a click on a panel/button never falls through to the canvas (e.g.
    // selecting an object underneath the operator panel). Published each frame (the
    // overlays draw after the tool handler runs, so these are last frame's rects —
    // fine, the overlays are stable corner anchors). Cleared at the start of the
    // leaf's render, repopulated by each overlay.
    std::vector<ImVec4> overlayRects;   // each = (minX,minY,maxX,maxY) screen px

    // Per-Outliner state (display/filters/search/selection/viewport-sync). Only
    // meaningful for an Outliner leaf, but carried by every EditorState so a tab
    // can switch kind / move between zones without losing it. See OutlinerState.h.
    OutlinerState outliner;

    // Properties editor page (Blender-style top-bar tabs): Object = transform /
    // compositing / instance target; Paint = the fill & stroke stacks (path
    // nodes); Modifiers = the modifier stack; Document = document-wide settings
    // (the display-unit system, …).
    enum class PropTab : uint8_t { Object = 0, Paint = 1, Modifiers = 2, Document = 3 };
    PropTab propTab = PropTab::Object;
};

// One tab inside a zone: an editor id plus its own independent view state.
// Moving a tab between zones carries its state (Tab is a value type).
struct Tab {
    std::string editorId = CoreEditor::Viewport;
    EditorState state;
};

// An editor's top bar (right of the always-present editor-kind picker) is laid
// out in THREE groups — left, middle (centred), right — that never overlap:
//   • the middle group is centred but pushed aside (keeping the gap) when it
//     would touch the left group, and likewise the right group pushes the middle;
//   • if the right group still can't fit, it overflows OFF the editor's right
//     edge and is clipped (overflow:hidden — not drawn/clickable outside).
// The hook fills this: a draw callback + the group's natural pixel width (so the
// layout can place each group before drawing it). Any group may be empty.
struct EditorBarGroup {
    std::function<void(ImVec2 pos, float height)> draw;   // draws at the given pos
    float width = 0.0f;                                   // natural pixel width
};
struct EditorBar {
    EditorBarGroup left, middle, right;
};

// Predefined zone arrangements offered on the splash "New File" start screen.
//   General — the default workspace (Viewport/Timeline | Outliner/Properties).
//   Layout  — no timeline; large Viewport (single spread, right) + Outliner over
//             a small Properties on the right.
//   Data    — large Outliner on the left; small Viewport (top), Properties and
//             Info stacked on the right.
enum class LayoutPreset { General, Layout, Data };

// A declarative, copyable zone-tree spec a module returns from BuildLayout().
// Either a LEAF (editorId set, no children) or a SPLIT (two children + ratio).
// Built with the Leaf()/Split() helpers; consumed by ZoneLayout::BuildFromSpec.
struct LayoutSpec {
    std::string                 editorId;          // leaf editor id ("" for split)
    bool                        vertical = false;  // split axis (true = side by side)
    float                       ratio    = 0.5f;   // first child fraction
    std::shared_ptr<LayoutSpec> a, b;              // children (split only)

    bool isLeaf() const { return !a && !b; }

    static LayoutSpec Leaf(std::string id) {
        LayoutSpec s; s.editorId = std::move(id); return s;
    }
    static LayoutSpec Split(bool vertical, float ratio, LayoutSpec a, LayoutSpec b) {
        LayoutSpec s; s.vertical = vertical; s.ratio = ratio;
        s.a = std::make_shared<LayoutSpec>(std::move(a));
        s.b = std::make_shared<LayoutSpec>(std::move(b));
        return s;
    }
};

class ZoneLayout {
public:
    ZoneLayout();

    // Replace the whole zone tree with a predefined arrangement (splash presets).
    // Rebuilds root_ from scratch — any current splits/tabs are discarded.
    void ApplyPreset(LayoutPreset preset);
    // Replace the whole tree from a module-provided spec (editor ids). See
    // ModuleAPI.h LayoutSpec. Discards the current tree.
    void BuildFromSpec(const LayoutSpec& spec);

    // Restrict the per-zone editor picker to these ids (Classic = core ids, a
    // module = its AllowedEditors()). A zone keeps showing its own editor even if
    // it's not in the list. Empty = no restriction (every registered editor).
    void SetEditorFilter(std::vector<std::string> ids) { editorFilter_ = std::move(ids); }

    // Set the document-unit index (viewport unit table) on EVERY tab's EditorState
    // across the whole tree. Used when a module declares a default unit (IOF → mm)
    // and on return to Classic (→ px). A no-op for unit < 0.
    void ApplyDocUnitToAll(int unit);

    // Draw the whole tree into the current window content region. Editors are
    // drawn by looking each zone's id up in EditorRegistry — no per-call hooks.
    void Render();

    // The editor state of the Viewport leaf the mouse is currently over
    // (and not occluded by a floating window). nullptr if none — camera
    // actions become no-ops. Valid after Render() of the current frame.
    EditorState* HoveredEditorState() { return hoveredState_; }
    // Called by RenderViewport when it determines its leaf is the genuinely
    // hovered, non-occluded one (strict IsWindowHovered). Last writer wins.
    void SetHoveredEditorState(EditorState* st) { hoveredState_ = st; }

    // When true, the Viewport zone paints NO opaque ImGui background — for a
    // render engine that composites its canvas onto the swapchain itself, UNDER
    // ImGui (kept for Ink's swapchain-direct present option; unused while the
    // engine presents via a sampled texture).
    void SetCanvasZoneTransparent(bool b) { canvasZoneTransparent_ = b; }

    // True while a layout-level gesture owns the mouse: dragging a separator,
    // an armed corner add/join, a context-menu split guide, or a live tab drag.
    // Viewport tools query this to avoid arming the canvas box-select when the
    // user is really resizing/splitting/moving a zone over the canvas.
    bool LayoutGestureActive() const {
        return sepDragging_ != nullptr || addArm_.armed || splitArm_.active ||
               join_.active || tabDrag_.armed || tabDrag_.active;
    }

    // How many leaves currently have a tab of editor `id` (counts every tab, not
    // just the active one). Used to disable the Outliner sync button when no
    // Viewport exists.
    int  CountEditors(const std::string& id) const { return CountEditorsRec(root_.get(), id); }
    // True if `st` is the EditorState of some live tab of editor `id` in the tree.
    // Validates the Outliner's sync target against the current layout each frame.
    bool IsLiveEditorState(const EditorState* st, const std::string& id) const {
        return st && FindEditorStateRec(root_.get(), st, id);
    }

    // Switch the editor of the leaf currently under the mouse (resolved each
    // Render() into hoveredLeaf_). No-op if no zone is hovered. Used by the
    // editor.* switch shortcuts so they target the zone the user points at.
    // Replaces the ACTIVE tab's editor id. Defined out-of-line (needs ActiveTab).
    void SetHoveredEditor(const std::string& id);

    // Tab navigation (driven by shortcuts).
    //   Cycle next/previous tab of the HOVERED zone (wraps).
    void HoveredTabCycle(int dir);
    //   Select the first / last tab of the ACTIVE zone.
    void ActiveTabSelectEdge(bool last);

    // ── Persistence (.acu LAYOUT section) ───────────────────────────────────
    // Serialize the whole zone tree (splits: orientation + absolute firstPx +
    // init ratio; leaves: tabs with their EditorKind + per-leaf EditorState
    // camera/unit) into a compact binary blob, and rebuild it from one. The
    // blob is versioned internally so the format can evolve with migration.
    // Deserialize replaces the current tree; on a malformed blob it leaves the
    // existing tree untouched and returns false.
    std::vector<uint8_t> Serialize() const;
    bool                 Deserialize(const std::vector<uint8_t>& blob);

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
    static const std::string& LeafEditorId(Node* n) { return ActiveTab(n).editorId; }
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
    bool         canvasZoneTransparent_ = false;  // viewport zone bg off (swapchain-direct engine)
    std::vector<std::string> editorFilter_; // ids selectable in the picker (empty=all)
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
    // Screen band (min.x,min.y,max.x,max.y) of the separator currently
    // hovered / dragged — the corner blocker seals it so a resize click never
    // also lands on the neighbour editor's overlay scrollbar or content.
    ImVec4       sepBlockRect_{0, 0, 0, 0};
    ImVec2       contextMenuPos_{0, 0};    // mouse pos captured at right-click
    Node*        menuSplit_    = nullptr;  // split whose context menu is open
    bool         menuOpenRequest_ = false; // ask to OpenPopup next (in body)
    bool         overlayHov_   = false;    // overlay is top-most under mouse
                                           // (false if a floating window is
                                           // above → suppresses all hit-test)

    static bool IsInSubtree(Node* subtree, Node* target);
    static void DimSubtree(Node* n, ImDrawList* dl, ImU32 col);
    static void HighlightSubtree(Node* n, ImDrawList* dl, ImU32 col);
    // Tree walks backing CountEditors / IsLiveEditorState (see public wrappers).
    static int  CountEditorsRec(const Node* n, const std::string& id);
    static bool FindEditorStateRec(const Node* n, const EditorState* st, const std::string& id);

    // Leaf/split Node builders shared by ApplyPreset and BuildFromSpec.
    static std::unique_ptr<Node> MakeLeafNode(std::string id, EditorState st);
    static std::unique_ptr<Node> MakeSplitNode(bool vertical, float initRatio,
                                               std::unique_ptr<Node> a,
                                               std::unique_ptr<Node> b);

    // Replace `target` in the tree by `replacement`.
    void ReplaceNode(Node* target, std::unique_ptr<Node> replacement);
    // Merge ONLY the two specified adjacent leaves (CAS 1/2/3).
    void JoinLeaves(Node* keep, Node* remove);

    // Recursively compute pos/size for the subtree.
    void Layout(Node* n, ImVec2 pos, ImVec2 size, float gap);
    // Pass 1: draw every leaf as a real ImGui window.
    void DrawLeaves(Node* n, float gap);
    // Pass 2: separators + join + add-area, on the overlay (above zones).
    void DrawNode(Node* n, float gap);
    // Draw one leaf (top bar selector + content child).
    void DrawLeaf(Node* n, float gap);
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

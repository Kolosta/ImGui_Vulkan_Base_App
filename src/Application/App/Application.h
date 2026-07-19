#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <UI/Tokens/TokenEditor.h>
#include <UI/Shortcuts/ShortcutEditor.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Settings/SettingsWindow.h>
#include <UI/Tokens/TokenGraphWindow.h>
#include <VectorGraphics/editors/IconEditorWindow.h>
#include <Ink/Render/Renderer.h>   // the 2D vector engine (docs/Ink/)
#include <UI/Widgets/ListRow.h>   // UI::ListRow (Outliner row geometry for DnD)
#include "ZoneLayout.h"
#include "Project.h"
#include "SecondaryWindow.h"
#include "UndoStack.h"
#include "ViewportEditing.h"   // EditContext / DocUndoStack / TransformOp (Lot 8)
#include "ModuleAPI.h"      // module contract (Modules::IModule / Capabilities)

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Application — Ink-rework transitional shell (Lot 0).
//
//  The legacy engines (Renderer / Compositor) and the old document are
//  quarantined under src/_legacy/ and fully disconnected. What remains is the
//  application shell: SDL3 + Vulkan + ImGui lifecycle, the borderless window
//  chrome (title bar / splash / about), the Blender-style zone layout, the
//  design-system / shortcut / icon subsystems, the module catalogue, and
//  placeholder Viewport / Outliner / Properties editors.
//
//  The Ink engine (docs/Ink/) is wired in since ROADMAP Lot 1:
//  InitializeSubsystems() creates Ink::Renderer on the shared device, the
//  Viewport editor drives one Ink::View per zone leaf, and Update() brackets
//  the UI build with ink_->BeginFrame()/EndFrame() (canvas work is ordered
//  before ImGui's sampling by same-queue barriers — no semaphores).
// ─────────────────────────────────────────────────────────────────────────────
class Application : public Modules::ModuleHost {
public:
    static constexpr const char* kVersion = "v0.1.0";

    Application();
    ~Application();

    bool Initialize();
    void Run();
    void Shutdown();

    // One full frame: ProcessEvents + Update + Render + Present.
    // Called from the main loop and from the SDL event watch during the OS
    // modal resize loop (Windows blocks SDL_PollEvent during live resize).
    void RenderFrame();

private:
    void ProcessEvents();
    void Update();
    void Render();
    void Present();

    // Init (ApplicationInit.cpp)
    void SetupVulkan();
    void SetupVulkanWindow();
    void InitializeSubsystems();
    void LoadResources();
    void RegisterDefaultShortcuts();
    // Register the built-in editors into the EditorRegistry (MainUI.cpp). Called
    // once at init, before the first layout render. Modules add theirs on top.
    void RegisterCoreEditors();
    // Resolve font design-system tokens (family role + weight) into the active
    // default font. Called at init and re-callable when a font token changes.
    void ApplyFontTokens();

    // Custom application title bar (TitleBar.cpp) — replaces the native OS
    // title bar with one borderless, fully styled bar (logo menu, File/Edit/
    // Windows menus, centred project name, redrawn OS window buttons). Window
    // drag / snap / resize stay native via the SDL hit-test callback.
    void RenderTitleBar();
    // After all UI is submitted, register any floating ImGui window that overlaps
    // the title-bar band as an extra hit-test blocker, so a click there hits the
    // floating window (focus / move it) instead of grabbing the native title bar.
    // Must run AFTER RenderFloatingWindows()/RenderSplash() so those windows exist.
    void PublishOverlayTitleBarBlockers();
    // SDL hit-test: classifies a point in the borderless window as draggable
    // (title-bar background), a resize border, or normal (interactive widget).
    static SDL_HitTestResult SDLCALL HitTestCallback(SDL_Window* win,
                                                     const SDL_Point* area,
                                                     void* data);
    // Home-grown "maximize": SDL 3.4's borderless OS-maximize covers the taskbar
    // (its WM_GETMINMAXINFO reports the full monitor) and resizing after the
    // fact desyncs the swapchain. So we NEVER let the OS maximize — we size the
    // window to the display's usable bounds ourselves and track the state.
    void SetMaximized(bool on);
    // Restore a maximized window mid-drag, Windows-style: keep the grabbed
    // point of the title bar under the cursor instead of snapping back to the
    // old restore position.
    void RestoreFromDragAtCursor();
    // Cancel an OS-initiated maximize (caption double-click / drag-to-top) and
    // convert it into our own usable-bounds maximize.
    void InterceptOsMaximize();
    // F11: borderless fullscreen-desktop toggle (title bar stays visible).
    void ToggleFullscreen();

    // Splash screen (Splash.cpp) — Blender-style start screen shown on launch
    // and from the logo menu; "About Carto" info popup; image decode helper.
    void RenderSplash();
    void RenderAbout();
    void LoadSplashTexture();

    // Layout host (MainUI.cpp)
    void RenderMainLayout();
    void RenderMainContent();
    void RenderStatusBar();

    // ── Editors (placeholders until Ink lands — docs/Ink/ROADMAP.md) ─────────
    // The 2D canvas editor — renders through an Ink::View per zone leaf
    // (100 % Vulkan canvas), drives the camera and (Lot 8) the editing loop.
    void RenderViewport(ImVec2 size, EditorState& st);
    // The Viewport top bar (mode/rulers/swatches | orient/pivot/snap | overlay
    // — MainUI wires it as the descriptor's topBar). Split across
    // ViewportToolbar.cpp with the palette + snap widget.
    void BuildViewportTopBar(EditorState& st, EditorBar& bar);
    void RenderToolPalette(ImVec2 origin, EditorState& st);   // floating tool column
    void DrawSnapWidget(ImVec2 pos, float widthPx);           // magnet + Snap menu
    void DrawDefaultColorSwatches(float barHeight);           // fill/stroke chips
    void RenderAddMenu();                                     // Shift+A spawn menu
    void RenderViewportContextMenu();                        // right-click menu
    // The reusable right-side "N" panel (UI::EditorSidePanel). Item tab (active
    // object transform) is always present; Marks tab only in Line-Mark mode.
    // ViewportSidePanel.cpp.
    void RenderViewportSidePanel(EditorState& st, ImVec2 cMin, ImVec2 cMax);
    void DrawSidePanelItemTab(ImVec2 cMin, ImVec2 cMax);
    void DrawSidePanelMarksTab(ImVec2 cMin, ImVec2 cMax);
    // The object the N-panel Item tab shows: the active one, or — once the
    // selection is fully cleared — the LAST active one (kept until deleted), so
    // the tab keeps editing it while deselected. kNullNode = the tab is hidden.
    Ink::NodeId ItemTabNode() const;

    // ── Editing loop (ViewportInput.cpp / ViewportTools.cpp / ViewportModal.cpp) ─
    // Camera helpers, shared by input + overlay drawing.
    struct ViewCam {   // screen = canvasMin + (doc − pan)·zoom
        ImVec2 canvasMin; double panX = 0, panY = 0, zoom = 1;
        Ink::Vec2  DocToView(double dx, double dy) const {
            return { (float)((dx - panX) * zoom), (float)((dy - panY) * zoom) };
        }
        Ink::DVec2 ScreenToDoc(double sx, double sy) const {
            return { (sx - canvasMin.x) / zoom + panX,
                     (sy - canvasMin.y) / zoom + panY };
        }
    };
    // Route mouse/keyboard on the hovered canvas to the active tool / modal op.
    void HandleViewportInput(EditorState& st, const ViewCam& cam, bool hovered,
                             const ImVec2& canvasMin, const ImVec2& canvasSize);
    // The active-tool press/drag/release (select, draw-rect/ellipse, cursor).
    void ToolMousePress(EditorState& st, const ViewCam& cam, Ink::DVec2 doc,
                        bool shift);
    void ToolMouseDrag(EditorState& st, const ViewCam& cam, Ink::DVec2 doc);
    void ToolMouseRelease(EditorState& st, const ViewCam& cam, Ink::DVec2 doc);
    // Modal G/R/S: begin (uses hoveredCam_), update each frame (preview),
    // confirm/cancel.
    void BeginTransform(TransformOp::Kind kind, EditorState& st);
    void UpdateTransform(const ViewCam& cam);
    void ConfirmTransform();
    void CancelTransform();
    // Editing actions (bound to shortcuts + the Add menu).
    void Action_SelectAll();
    void Action_DeselectAll();
    void Action_DeleteSelection();
    void Action_DuplicateSelection();
    void Action_EnterEditMode();
    void Action_ExitEditMode();
    void Action_ToggleEditMode();
    void Action_ToggleLineMarkMode();   // Shift+Tab: enter/leave Line-Mark mode
    void SetEditorMode(EditorMode mode); // switch mode + apply its defaults
    void Action_ApplyScale();
    void Action_BeginMove();
    void Action_BeginRotate();
    void Action_BeginScale();
    void Action_ConstrainAxisX();
    void Action_ConstrainAxisY();
    void Action_OpenAddMenu();
    // Edit mode handle-type ops (the V menu). SetHandleType applies a legacy
    // handle mode (0 Free / 1 Aligned / 2 Mirrored / 3 Aligned+Mirrored /
    // 4 Vector) to the touched anchors; RemoveHandles clears them; the menu is
    // opened by V and rendered unconditionally like the Add menu.
    void Action_SetHandleType(int mode);
    void Action_RemoveHandles();
    void Action_OpenHandleMenu();
    void RenderHandleTypeMenu();
    void Action_DeleteVertices();
    bool   handleMenuOpen_ = false, handleMenuRequested_ = false;
    ImVec2 handleMenuPos_{};
    // ── Pen (draw-on-create, the legacy live path construction) ─────────────
    // Armed by the Curve/Shape tools (pen kinds) or the Add menu's Free entry:
    // click = corner anchor, click-drag = symmetric handles (Bézier),
    // Backspace = drop the last anchor, Enter / double-click = finish open, a
    // click on the first anchor closes, Esc cancels. One undo command on commit.
    void BeginPenDraw(const char* kind);
    bool HandlePenInput(EditorState& st, const ViewCam& cam, bool hovered);
    void CommitPenDraw(bool keep);
    // The pen's companion FILL-ONLY node: while a FREE shape is drawn, it
    // carries the default fills over the CLOSED preview path (frozen anchors
    // + the pending one), kept just BELOW the pen node — the real pipeline
    // shows every fill (patterns included) live, with the closing edge
    // following the outer handles. Removed on commit/cancel (the fills then
    // move onto the finished node).
    void UpdatePenFillPreview(Ink::Document& doc);
    Ink::NodeId    penFillNode_ = Ink::kNullNode;
    // The DRAG-shape (rect/ellipse/triangle/curve-box) preview: a REAL node
    // identical to what a release would spawn (same geometry, centred origin,
    // default fills+strokes) but at a reduced node OPACITY — so the pipeline
    // renders the translucent preview EXACTLY like the final object (fills cut
    // at the contour, patterns instanced with the correct Object/Document
    // anchor). Rebuilt each frame while dragging, removed on release/cancel.
    void UpdateShapePreview(Ink::Document& doc, const char* kind,
                            Ink::DVec2 boxMin, Ink::DVec2 boxMax);
    void ClearShapePreview();
    Ink::NodeId    shapePreviewNode_ = Ink::kNullNode;
    // The node opacity used for every in-progress draw preview.
    static constexpr float kDrawPreviewAlpha = 0.5f;
    bool           penActive_   = false;
    bool           penDragging_ = false;       // pulling the new anchor's handles
    Ink::NodeId    penNode_     = Ink::kNullNode;
    Ink::SplineType penSpline_  = Ink::SplineType::Bezier;
    // A FREE shape (an area) keeps a fill and previews it when closing; a plain
    // curve (Bézier / NURBS / Poly) has NO fill — closing previews only the
    // closing STROKE segment, never a fill.
    bool           penIsArea_   = false;
    // The LAST point is kept OUT of the node while it is being defined: only the
    // FROZEN anchors live in `penNode_` (rendered opaque); the pending one is
    // drawn as a TRANSLUCENT preview in the overlay (its segment from the last
    // frozen anchor, plus the rubber-band to the cursor). A click freezes the
    // pending into the node and starts a new pending; drag pulls its handles.
    Ink::Anchor    penPending_{};
    bool           penHasPending_ = false;
    // Snap-to-close: the close-zone test + preview are computed locally in the
    // input / overlay phases; no persistent state is needed here.

    // ── Follow-curve (Shift while the pen is active) — OpenOrienteering-Mapper
    //    "follow path": trace the drawn path EXACTLY along a target curve.
    //    ViewportFollowCurve.cpp. Returns true while following (the pen suppresses
    //    its own placement that frame). `committed` = a click froze a piece.
    bool UpdatePenFollowCurve(const ViewCam& cam, bool hovered,
                              Ink::OverlayList& ov, bool& committed);
    // Append the Bézier anchors of `piece` (node-local, handles relative) onto
    // the pen node's open subpath; the pen's last anchor adopts the piece's first
    // OUT handle so the join leaves along the curve. FollowCurve.cpp helper.
    struct FollowPieceAnchor { Ink::DVec2 pos, in, out; bool hasIn, hasOut; };
    bool           followLocked_ = false;   // an entry point was placed → tracing
    Ink::NodeId    followNode_   = Ink::kNullNode;  // the target curve's node
    int            followSub_    = -1;      // its subpath index
    double         followAnchorArc_ = 0.0;  // where the last frozen point sits
    double         followCursorArc_ = 0.0;  // the current cursor projection (unwrapped)
    // Draw-on-Create feedback: hide the OS cursor and draw a crosshair on the
    // foreground list, plus a small preview GLYPH of the shape being created at
    // the bottom-right of the cursor. `kind` = the Add-menu shape/curve id.
    // ViewportFollowCurve.cpp. Called each frame while a create tool is armed.
    void DrawCreateCursor(const char* kind);
    // The kind currently armed for draw-on-create (pen spline or shape drag-box),
    // or empty. Drives DrawCreateCursor.
    std::string CreateCursorKind() const;
    // ── Line-Mark MODE (EditorMode::LineMark) — generic mark editing ─────────
    // Handles/ghosts only render while the tool is active. Click places a
    // neutral mark (default object = `markPlaceShape_`) on the nearest stroked
    // line; a click on an existing handle selects (Shift toggles, Alt deletes)
    // and arms a drag ALONG the curve; G slides / R cycles side / X deletes the
    // selected marks. All edits go through SetStyle (one undo command each).
    void HandleMarkTool(EditorState& st, const ViewCam& cam,
                        Ink::OverlayList& ov, bool hovered);
    void BeginMarkTransform(TransformOp::Kind kind);
    void DeleteSelectedMarks();
    void Action_CycleMarkSide();  // V: Center → Left → Right on the selection
    void Action_CycleMarkPhase(); // P: Neutral → Dash → Gap on the selection
    void Action_CopyMarks();      // Ctrl+C the selected marks
    void Action_PasteMarks();     // Ctrl+V: arm the translucent paste
    bool MarkModeActive() const;  // the Line-Mark editor mode is active
    bool MarkToolArmed() const;   // mode active + marks selected (redirect G/R/S/X)
    // Mark clipboard + the armed paste (a translucent preview follows the
    // cursor until a click drops it or RMB/Esc cancels).
    std::vector<Ink::StrokeMark> markClipboard_;
    bool markPasteActive_ = false;
    // The default object a click drops on the line (top-bar picker): a shape
    // and whether it ADDS to or SUBTRACTS from the stroke. The picker's clear
    // cross empties the selection ("Empty Mark"): a click then places an
    // OBJECTLESS mark (a pure position/phase anchor).
    Ink::MarkShape markPlaceShape_ = Ink::MarkShape::Circle;
    bool           markPlaceSubtract_ = false;
    bool           markPlaceEmpty_ = false;
    // LIVE mark preview: while a Subtract/Gap mark is placed or moved, a
    // TEMPORARY style is applied to the host node so the REAL pipeline shows
    // the effect (partial erase / dimmed gap span). Strictly frame-scoped:
    // applied while building the overlays, restored right after the Ink frame
    // is recorded (before action dispatch / saves), so undo, hit-testing and
    // persistence never see it.
    // One saved original per previewed node (a multi-mark drag can span nodes).
    std::vector<std::pair<Ink::NodeId, Ink::Style>> markPreviewSaved_;
    void ApplyMarkPreviewStyle(Ink::NodeId node, const Ink::Style& preview);
    void ClearMarkPreviewStyle();
    struct MarkDrag {           // click-drag of one grabbed mark (+ group Δt)
        bool armed = false, active = false;
        EditContext::MarkRef ref;
        ImVec2 pressPos{};
        double dragT0 = 0.0;    // the grabbed mark's t at press
        double dragT = 0.0;     // its previewed t this frame
    } markDrag_;
    struct MarkGrab {           // modal G/R/S over the selected marks
        int op = 0;             // 0 none, 1 slide(G), 2 rotate(R), 3 scale(S)
        int axis = -1;          // R/S constraint: -1 free, 0 = X/length, 1 = Y/width
        const void* owner = nullptr;
        ImVec2 startMouse{};
        std::vector<EditContext::MarkRef> refs;
        std::vector<double> t0;         // G: each mark's t at press
        std::vector<double> rot0, size0, width0;  // R/S: each object's start
        bool Active() const { return op != 0; }
        void Reset() { *this = MarkGrab{}; }
    } markGrab_;
    struct MarkBox {            // box-select over mark handles
        bool active = false; const void* owner = nullptr;
        Ink::DVec2 start{}; bool additive = false;
    } markBox_;
    // Snap pie (Shift+S — the legacy 2D adaptation of Blender's snap pie):
    // move the SELECTION to targets (cursor / active / grid) or the 2D CURSOR
    // to references (active / selected / world origin / page origin / grid).
    void Action_OpenSnapMenu();
    void RenderSnapPieMenu();
    // Translate a node so its ORIGIN lands on a document point / snap every
    // selected node as one undo command / the snap grid step (the move
    // increment, 50 doc-units fallback).
    void   SnapNodeOriginTo(Ink::NodeId id, Ink::DVec2 to);
    void   SnapSelection(const char* label,
                         std::function<Ink::DVec2(Ink::NodeId)> targetFor);
    double SnapGridStep() const;
    // The interactive Grid snap step for a given zoom: the move increment,
    // COARSENED by a nice factor (×1 / 2 / 5·10ⁿ) so on-screen crossings never
    // fall below a legible spacing — this BOUNDS the number of grid dots at any
    // zoom (a fixed doc-space step explodes when dezoomed and gets truncated).
    // Display and snap share it, so the dots always mark exactly the crossings
    // you snap to. At working zoom the factor is 1 (== the raw increment).
    double GridSnapStep(double zoom) const;

    // ── Interactive snapping during a modal G/R/S (the legacy Snap To system) ──
    // Modes: Increment (round the displacement), Grid (land on an absolute grid
    // crossing), Vertex/Edge/Face/EdgeCenter (snap onto document geometry within
    // a screen-pixel radius). Snap engages when the magnet is on OR Ctrl is held,
    // gated per transform by the Affect toggles. Snap Base picks WHICH point of
    // the moving selection lands on the target.
    struct SnapResult { bool snapped = false; Ink::DVec2 pos{}; bool showMark = false; };
    // True if snapping should apply to `kind` right now (magnet/Ctrl + Affect).
    bool SnapActiveFor(TransformOp::Kind kind) const;
    // Apply the active Snap To mode to a Move op's displacement `moveD`
    // (in place), publishing the indicator for Grid / geometry snaps.
    // `cursorDoc` is the effective cursor in doc space.
    void ApplyMoveSnap(const ViewCam& cam, Ink::DVec2 cursorDoc,
                       Ink::DVec2& moveD, bool precise);
    // Find the geometry snap target for doc point `cursor` under the current
    // mode. `exclude` = node ids NOT to snap to (the moving objects); `rejectPts`
    // = world points to skip (the moving selection's current positions, so it
    // never snaps onto itself); `rejectSegs` = world segment pairs (a,b,a,b,…) of
    // the moving selection's edges. `zoom` = screen px per doc-unit.
    SnapResult ComputeSnap(Ink::DVec2 cursor, double zoom,
                           const std::vector<Ink::NodeId>& exclude,
                           const std::vector<Ink::DVec2>& rejectPts,
                           const std::vector<Ink::DVec2>& rejectSegs) const;
    // Discrete snap candidates (world) for the DISCRETE modes (Vertex anchors /
    // EdgeCenter node-segment midpoints / Face centroids), excluding the moving
    // selection. Shared by ComputeSnap and the candidate overlay.
    std::vector<Ink::DVec2> CollectSnapPoints(
        const std::vector<Ink::NodeId>& exclude,
        const std::vector<Ink::DVec2>& rejectPts) const;
    // Snap SOURCE point(s) of the moving selection under the current Snap Base,
    // at their PRE-MOVE world positions (Closest = every moving control point).
    std::vector<Ink::DVec2> SnapBaseSources() const;
    // Every moving vertex (edit mode) at its PRE-MOVE world position — rejected
    // as a snap target so the selection never snaps onto itself. Empty in object
    // mode (the whole shape is id-excluded instead).
    std::vector<Ink::DVec2> MovingSelectionPoints() const;
    // The moving selection's EDGES as PRE-MOVE world segment pairs (edit mode):
    // an edge is "moving" when BOTH endpoints are selected. Keeps Edge/EdgeCenter
    // off the moving geometry.
    std::vector<Ink::DVec2> MovingSelectionEdges() const;
    // The live snap indicator (world pos + visible), published during a transform
    // so the overlay draws the accent glyph. Cleared when no snap is active.
    SnapResult snapIndicator_;
    // Overlay: every possible snap point for the current mode (violet), shown
    // during a snap-active transform so the user sees where they can land; the
    // grid draws dots at its crossings. Hidden once a geometry snap engages.
    void DrawSnapCandidates(const ViewCam& cam, Ink::OverlayList& ov);
    // Overlay: the snap indicator glyph (shape encodes the mode) at snapIndicator_.
    void DrawSnapIndicatorGlyph(const ViewCam& cam, Ink::OverlayList& ov);
    void Action_SelectionToCursor();
    void Action_SelectionToActive();
    void Action_SelectionToGrid();
    void Action_Cursor2DToActive();
    void Action_Cursor2DToWorldOrigin();
    void Action_Cursor2DToGrid();
    bool   snapMenuOpen_ = false, snapMenuRequested_ = false;
    ImVec2 snapMenuPos_{};
    bool   show2DCursor_ = true;   // draw the 2D cursor overlay
    // Reset the 2D cursor to the document/page origin, or to the selection.
    void   Action_Cursor2DToOrigin();
    void   Action_Cursor2DToSelection();
    // Set Origin ▸ (legacy parity). The node's origin is its transform's
    // translation; moving it re-bases the geometry so the shape stays put.
    void   MoveOriginTo(Ink::NodeId id, Ink::DVec2 worldTarget);
    void   Action_OriginToGeometry();    // origin → geometry centre
    void   Action_GeometryToOrigin();    // geometry re-centred on the origin
    void   Action_OriginTo2DCursor();    // origin → 2D cursor
    void   Action_SelectGroup();         // select the active object's parent group
    void   Action_ParentToActive();      // Ctrl+P: parent selection to active
    void   Action_DuplicateLinked();     // Alt+D: instance copies + grab
    void   Action_DuplicateGrab();       // Shift+D: deep copy + grab
    // Create a shape at the 2D cursor / view centre and select it.
    Ink::NodeId SpawnShape(const char* kind);
    // Draw-on-create: build `kind` to fill the dragged box (doc space).
    Ink::NodeId SpawnShapeInRect(const char* kind, Ink::DVec2 mn, Ink::DVec2 mx);
    Ink::NodeId FinishSpawn(Ink::NodeId id, const std::string& name);
    // The MULTI-TOOL variants: which shape / curve kind the Shape and Curve
    // palette buttons currently create (picked from their right-click menu).
    std::string toolShapeKind_ = "rect";
    std::string toolCurveKind_ = "curve";
    // The tool ids available in an editor MODE (Object gets the creation
    // tools; Edit / Line-Mark keep Select + 2D Cursor for now). The palette,
    // the activation action and the mode switch all share this table.
    static const std::vector<const char*>& ToolsForMode(EditorMode mode);
    // The EDST .acu section: per-mode tools + multi-tool variants (opaque,
    // self-versioned — Application-owned editing-session state).
    std::vector<std::uint8_t> BuildEditorStateBlob() const;
    void ApplyEditorStateBlob(const std::vector<std::uint8_t>& blob);
    // The palette variant menu: which multi-tool button it is open for
    // ("tool.shape" / "tool.curve", empty = closed), where it opens, and which
    // viewport leaf owns it (popups are id-scoped per zone child window).
    std::string toolMenuFor_;
    ImVec2      toolMenuPos_{};
    bool        toolMenuOpen_ = false;
    const void* toolMenuLeaf_ = nullptr;
    // Build the default Style (fills+strokes) from the EditContext defaults.
    Ink::Style DefaultStyle() const;
    // Push an already-applied reversible command onto the doc undo stack.
    // Also a Modules::ModuleHost service (module edits undo like core ones).
    void PushDocCommand(const std::string& label,
                        std::function<void(Ink::Document&)> undo,
                        std::function<void(Ink::Document&)> redo) override;
    // Recompute the selection's basis (orientation) and pivot (doc space).
    void ComputeTransformFrame(Ink::DVec2& pivot, Ink::DVec2& bx, Ink::DVec2& by) const;
    // Selection bbox in document space (Object Mode; false when empty).
    bool SelectionBounds(Ink::DRect& out) const;
    // Draw selection outlines / handles / modal feedback into the view overlay.
    void DrawEditOverlays(EditorState& st, const ViewCam& cam,
                          Ink::OverlayList& ov, bool hovered);
    // Object organisation trees (Layers + Collections views on the Ink model —
    // Lot 9). Outliner.cpp draws the tree; OutlinerMenus.cpp the top bar,
    // context menu and the organisation commands (group/ungroup/collections).
    void RenderOutliner(EditorState& st);
    void BuildOutlinerTopBar(EditorState& st, EditorBar& bar);
    // One flattened Outliner row (built once per frame; drawn only if within
    // the visible scroll window — the culling that keeps the editor O(visible)
    // instead of O(document), so a 100k-object document stays fluid).
    struct OutlinerRow {
        enum class Kind : uint8_t { Object, CollectionHeader, PageHeader,
                                    ProjectRoot,
                                    // Collections view children of an object:
                                    Modifier,     // one row per stack entry
                                    LinkedData }; // an instance's shared data
        uint64_t id = 0;        // node / collection / page id (root: 0);
                                // Modifier/LinkedData: the OWNING object's id
        Kind     kind = Kind::Object;
        int      depth = 0;
        bool     hasChildren = false;
        // Collections view: the ENCLOSING collection of this row (kNullNode =
        // project root) and its flat row index — the drop target a dragged
        // object resolves to from anywhere inside that collection.
        uint64_t ownerColl = 0;
        int      ownerRow  = -1;   // flat index of the owner's header (0 = root)
        int      flatIndex = 0;    // this row's own flat index
        int      modIndex  = -1;   // Kind::Modifier: index into the stack
        uint64_t refId     = 0;    // Kind::LinkedData: the referenced node
        int      objRow    = -1;   // Modifier/LinkedData: owning object's row
    };
    // Append the visible rows of a node subtree (respecting collapse + filter +
    // search) to `out`. Pure computation, no drawing. In the Collections view
    // the recursion follows OBJECT PARENTING (parentId, Lot 7) so parented
    // children nest under their parent; in Layers it follows the layer tree.
    void OutlinerFlattenNode(Ink::NodeId id, int depth,
                             std::vector<OutlinerRow>& out,
                             Ink::NodeId ownerColl = Ink::kNullNode,
                             int ownerRow = -1);
    // Per-frame draw context for the row list (set by RenderOutliner before
    // the draw loop; consumed by the drag & drop to reach OTHER rows' geometry
    // — e.g. highlighting the enclosing collection of the hovered row).
    const std::vector<OutlinerRow>* outlinerRows_ = nullptr;
    float outlinerRowsStartY_ = 0.0f;   // window-local Y of flat row 0
    float outlinerStripeH_    = 0.0f;   // row pitch during the draw loop
    // The Layers-view preview-square rect of the row CURRENTLY being drawn —
    // read by the drag & drop to detect a mask drop onto the square.
    ImVec2 outlinerLayerPreviewMin_{};
    ImVec2 outlinerLayerPreviewMax_{};
    bool   outlinerLayerPreviewValid_ = false;
    // parentId → children, rebuilt once per Outliner frame (Collections view),
    // plus a per-frame cache of each row's combined child list.
    std::unordered_map<Ink::NodeId, std::vector<Ink::NodeId>> outlinerParentKids_;
    std::unordered_map<Ink::NodeId, std::vector<Ink::NodeId>> outlinerRowKids_;
    void OutlinerBuildParentIndex();
    // The children a row shows in the CURRENT view (layer children or parented).
    const std::vector<Ink::NodeId>* OutlinerRowChildren(const Ink::Node& n) const;
    void OutlinerBuildRows(EditorState& st, std::vector<OutlinerRow>& out);
    // Draw one already-flattened row at the current cursor (a ListRow stripe).
    void OutlinerDrawRow(EditorState& st, const OutlinerRow& r, float rowStripeH);
    // Vertical tree guide lines under every expanded container (legacy design:
    // collection colour / border solid, parenting dotted). One pass, culled.
    void OutlinerDrawGuideLines(EditorState& st, const std::vector<OutlinerRow>& rows,
                                float startY, float stripeH);
    // Collapsed container rows: inline type icons + count badges of the direct
    // contents, selected-tinted when a summarised item is in the selection.
    void OutlinerCollapsedSummary(const OutlinerRow& rrow, float x, float rowTopY,
                                  float maxX);
    // Filter + selection helpers (legacy parity).
    bool OutlinerPassesFilter(Ink::NodeId id) const;   // kind + state + invert
    bool OutlinerSearchHit(Ink::NodeId id) const;      // own name matches search
    bool OutlinerSubtreeSearchHit(Ink::NodeId id) const; // it or a descendant
    bool OutlinerRowSelected(Ink::NodeId id) const;    // shared selection or sel[]
    void OutlinerSelectClick(Ink::NodeId id, bool isObject);
    // Draw a lightweight live vector preview of a node into the rect (Layers
    // view tall rows). Flattens the node's paths on the fly (visible-only, so
    // it never costs for off-screen rows) — no Vulkan target, no texture.
    void OutlinerDrawPreview(Ink::NodeId id, ImVec2 min, ImVec2 max);
    // In-collection membership test (Collections view page-orphan listing).
    bool OutlinerInAnyCollection(Ink::NodeId id) const;
    // Right-click context menu for the Outliner (opened over a row or empty).
    void RenderOutlinerContextMenu(EditorState& st);
    // Drag & drop (OutlinerDragDrop.cpp): rows are drag sources (objects and
    // collections) and drop targets — Collections view: object→object parents,
    // object→collection re-collections, collection→collection nests; Layers
    // view: object→object reorders in the stack, object→group moves into it.
    // The background (below the rows) un-parents / un-collections.
    void OutlinerRowDragDrop(const OutlinerRow& row, const UI::ListRow& lr);
    void OutlinerBackgroundDropTarget(ImVec2 rectMin, ImVec2 rectMax);
    // True while the sync-picking gesture owns the mouse: every Outliner row is
    // input-inert (no select, no eye toggle, no context menu, no drag) so the
    // cancel click can never leak into the tree.
    bool outlinerSuppressInput_ = false;
    // The dragged object set: the whole selection when the payload id is part
    // of it, else just that id (legacy multi-drag rule).
    std::vector<Ink::NodeId> OutlinerDraggedIds(Ink::NodeId trigger) const;
    // Undoable drop / organisation operations (OutlinerDragDrop.cpp).
    void OutlinerDropParentTo(const std::vector<Ink::NodeId>& ids, Ink::NodeId parent);
    void OutlinerDropToCollection(const std::vector<Ink::NodeId>& ids, Ink::NodeId coll);
    void OutlinerDropToRoot(const std::vector<Ink::NodeId>& ids);
    void OutlinerDropReorder(const std::vector<Ink::NodeId>& ids, Ink::NodeId target,
                             bool above);
    // Affinity clip/mask drop (undoable): nest ids under target as clip
    // children, or mask children when `asMask`.
    void OutlinerDropClipMask(const std::vector<Ink::NodeId>& ids,
                              Ink::NodeId target, bool asMask);
    // Modifier drag & drop: COPY the source object's stack entry onto a
    // compatible (path) object, appended below its own modifiers. Undoable.
    void OutlinerDropModifierCopy(Ink::NodeId srcObj, int modIndex,
                                  Ink::NodeId dstObj);
    void OutlinerRemoveFromCollections(const std::vector<Ink::NodeId>& ids);
    void OutlinerUnparent(const std::vector<Ink::NodeId>& ids);
    // The collection-colour picker popup (Custom… in the Icon Colour submenu).
    void RenderOutlinerColorPicker(EditorState& st);
    Ink::NodeId outlinerColorPickColl_ = Ink::kNullNode;
    bool        outlinerColorPickRequested_ = false;
    const void* outlinerColorPickOwner_ = nullptr;   // leaf owning the popup
    // Organisation commands (undoable), shared by the Outliner + shortcuts.
    void Action_SetBlendMode(const std::vector<Ink::NodeId>& ids, Ink::BlendMode mode);
    void Action_GroupSelection();
    void Action_UngroupSelection();
    void Action_ToggleNodeVisible(Ink::NodeId id);
    void Action_RenameNode(Ink::NodeId id, const std::string& name);
    void Action_NewCollectionFromSelection();
    // The Outliner state the top-bar lambdas + row builders act on this frame
    // (all zones' top bars build first, draw later — legacy pattern).
    OutlinerState* outlinerCur_ = nullptr;
    // The Outliner currently in "pick a viewport to sync" mode (or nullptr).
    OutlinerState* outlinerPickingState_ = nullptr;

    // ── Object eyedropper (Properties node pickers) ─────────────────────────
    // When active, the cursor becomes an eyedropper and the next object click
    // (in a viewport OR an outliner) commits that node id through the fold
    // callback. Clicking anything that is NOT an object just cancels the pick.
    bool objPickActive_ = false;
    std::function<void(Ink::NodeId)> objPickCommit_;
    bool ObjectPickActive() const { return objPickActive_; }
    void BeginObjectPick(Ink::NodeId* target, std::function<void(Ink::NodeId)> commit) {
        (void)target;   // the commit lambda owns the write (re-fetches the node)
        objPickActive_ = true; objPickCommit_ = std::move(commit);
    }
    void CancelObjectPick() { objPickActive_ = false; objPickCommit_ = nullptr; }
    // Deliver a picked node to the armed eyedropper (returns true if consumed).
    bool DeliverObjectPick(Ink::NodeId id) {
        if (!objPickActive_) return false;
        if (id != Ink::kNullNode && objPickCommit_) objPickCommit_(id);
        CancelObjectPick();
        return true;
    }

    // Active object's full property editor (legacy Compositor layout) — every
    // node property is visible and editable: transform, compositing, the
    // unified paint stack (multi fills incl. patterns, multi strokes incl.
    // hairlines), the modifier stack and instance targeting.
    void RenderProperties(EditorState& st);
    // Properties top bar: the centred Object / Paint / Modifiers page tabs.
    void BuildPropertiesTopBar(EditorState& st, EditorBar& bar);
    // The page actually shown (Paint falls back to Object off path nodes).
    EditorState::PropTab PropsEffectiveTab(const EditorState& st) const;
    // Document settings tab (display-unit system, …) — selection-independent.
    void DrawPropertiesDocument();

    // ── Rulers + per-viewport display unit ────────────────────────────────────
    // The viewport's effective unit SYSTEM: EditorState.docUnit selects it
    // (0 = follow the document; 1..4 = force Metric / Imperial / Typographic /
    // Pixel). Followed by this viewport's rulers + its N-panel Item tab inputs.
    UI::Units::UnitSystem ViewportUnitSystem(const EditorState& st) const;
    // The ruler band thickness (px) and the per-side insets a viewport's ENABLED
    // rulers claim (ImVec4 = left, top, right, bottom; 0 where that ruler is off).
    // Tool palette + side panel offset by these so they never sit over a ruler.
    float  RulerWidth() const;
    ImVec4 RulerInsets(const EditorState& st) const;
    // The system the N-panel Item-tab inputs display in — set to the hosting
    // viewport's ViewportUnitSystem() each frame (they follow the ruler unit,
    // not the document one, per the design).
    UI::Units::UnitSystem sidePanelUnitSys_ = UI::Units::UnitSystem::Pixel;
    // Draw the top + left rulers (translucent chrome over the canvas edges):
    // adaptive nice ticks labelled in the viewport unit, cursor guides, and the
    // corner unit button (cycles the viewport unit). Adds its bands to
    // st.overlayRects so canvas input never falls through them.
    void DrawRulers(EditorState& st, ImVec2 canvasMin, ImVec2 canvasSize);
    // Property sub-sections (Properties.cpp / PropertiesPaint.cpp /
    // PropertiesModifiers.cpp).
    void PropTransformSection(Ink::NodeId id);
    void PropCompositingSection(Ink::NodeId id);
    void PropCurveSection(Ink::NodeId id);   // spline type / NURBS params
    void PropFillsSection(Ink::NodeId id);
    void PropStrokesSection(Ink::NodeId id);
    // The SHARED fills / strokes stack UI (vignette rail + selected item's
    // properties) working on a style COPY: Properties wraps it per-node, the
    // Stroke/Fill EDITORS wrap it selection-wide. `node` = the pattern-preview /
    // eyedropper target (kNullNode when editing the default style). `sel` = the
    // rail selection storage of the caller.
    void DrawFillsStackBody(Ink::Style& style, Ink::NodeId node, int& sel,
                            const std::function<void(const char*, bool)>& liveApply,
                            bool& structural, const char*& structLabel);
    void DrawStrokesStackBody(Ink::Style& style, int& sel,
                              const std::function<void(const char*, bool)>& liveApply,
                              bool& structural, const char*& structLabel);
    // ── Stroke / Fill EDITORS (PaintEditors.cpp) ─────────────────────────────
    // Same stack UI as Properties, but edits apply to the WHOLE selection (all
    // selected path nodes), and with NOTHING selected they edit the DEFAULT
    // style used for new objects (which mirrors the last active object).
    void RenderStrokeEditor(EditorState& st);
    void RenderFillEditor(EditorState& st);
    // Mirror the active path node's style into the defaults (so deselecting
    // keeps the last style displayed / used for new objects).
    void SyncDefaultStyleFromActive();
    // Apply the CURRENT defaults (edit_.defaultFills / defaultStrokes) to every
    // selected path node — live while a drag lasts, ONE undo command when
    // `released`. No-ops on the document when nothing is selected.
    void ApplyDefaultFillsEdit(const char* label, bool released);
    void ApplyDefaultStrokesEdit(const char* label, bool released);
    std::vector<Ink::NodeId> SelectedPathNodes() const;
    bool paintEdActive_ = false;   // selection-wide gesture fold state
    std::vector<std::pair<Ink::NodeId, Ink::Style>> paintEdBefore_;
    int  strokeEdSel_ = 0;         // the editors' own rail selections
    int  fillEdSel_   = 0;
    // Compact editor for a single mark object (shape/mode/bend/size/side/blend/
    // colour) — shared by the gap start/end marker lists. Sets `structural` and
    // `structLabel` on a structural edit. No Gap / nested gap.
    bool PropMarkObjectCompact(Ink::MarkObject& o, double strokeWidth,
                               Ink::Document& doc, Ink::NodeId hostId,
                               bool& structural, const char*& structLabel);
    // Full editor for ONE mark (position/side/phase + its object list, gaps and
    // markers) — used by the viewport "Marks" side-panel tab. Handles its own
    // style copy + undo commit. `node`/`strokeIdx`/`markIdx` address the mark.
    void DrawMarkEditor(Ink::NodeId node, int strokeIdx, int markIdx);
    void PropModifiersSection(Ink::NodeId id);
    void PropInstanceSection(Ink::NodeId id);
    // Commit a whole-style edit as one undoable command (captures before/after).
    void CommitStyleEdit(Ink::NodeId id, const Ink::Style& before,
                         const std::string& label);
    // Same for a whole-modifier-stack edit.
    void CommitModifiersEdit(Ink::NodeId id, const std::vector<Ink::Modifier>& before,
                             const std::string& label);
    // The Outliner context-menu request (opened next frame at this position).
    bool   outlinerCtxOpen_ = false;
    // Armed by the click sites; RenderOutlinerContextMenu opens the popup ONCE
    // from its own (stable, scroll-child) scope — the same request pattern the
    // viewport menus use, immune to open/render scope-ordering subtleties.
    bool   outlinerCtxRequested_ = false;
    // The OUTLINER LEAF owning the open menu: with several Outliners, every
    // leaf calls RenderOutlinerContextMenu, but the popup id only resolves in
    // the OWNER's scroll-child scope — a non-owner would read "closed" and
    // wrongly clear the shared flag (the menu then closed instantly).
    const void* outlinerCtxOwner_ = nullptr;
    ImVec2 outlinerCtxPos_{};
    Ink::NodeId outlinerCtxNode_ = Ink::kNullNode;
    // Non-null when the menu was opened on a LINKED-DATA row (the instance's
    // shared data): the referenced node id — builds the linked-data menu.
    Ink::NodeId outlinerCtxLinkedRef_ = Ink::kNullNode;
    // Selection anchor for Shift-range clicks in the Outliner (last plain click).
    Ink::NodeId outlinerRangeAnchor_ = Ink::kNullNode;
    // Live property editing: the style captured when a drag-edit began, so the
    // whole drag folds into ONE undo command committed on release.
    Ink::Style      propEditBefore_;
    Ink::PathData   pathBeforeScratch_;         // curve-panel drag before-state
    Ink::Transform2D transformBeforeScratch_;   // transform drag before-state
    std::vector<Ink::Modifier> modifiersBeforeScratch_;   // modifier drag before
    Ink::NodeId     propEditNode_ = Ink::kNullNode;
    bool            propEditActive_ = false;
    // Paint page selection (the fill/stroke vignette rails): which item the
    // right-hand property column edits. Reset when the active node changes.
    Ink::NodeId     propPaintNode_ = Ink::kNullNode;
    int             propFillSel_   = 0;
    int             propStrokeSel_ = 0;
    // Real-pipeline render of ONE fill of the node (128 px, white bg, 100 %
    // zoom, the fill isolated via the per-piece preview filter) for the
    // PATTERN vignettes + their hover tooltips. 0 when unavailable.
    ImTextureID PaintPatternPreview(Ink::NodeId id, int fillIndex);
    // A SELF-CONTAINED pattern swatch: the fill is RE-CREATED inside the
    // vignette (white plate + a lattice of the motif's own render at a legible
    // zoom) — never a crop of the host shape, so it reads identically with or
    // without a selected object and fills the whole tile. False = no drawable
    // motif (the caller falls back to the flat sample).
    bool PaintPatternSwatch(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                            const Ink::Fill& f);

    // ── Info log (Blender-style action feed) + Dev data editor ───────────────
    struct InfoEntry { uint64_t frame; std::string text; std::string detail; };
    std::vector<InfoEntry> infoLog_;
    void LogInfoAction(const std::string& text) override;   // ModuleHost service
    void LogInfoAction(const std::string& text, const std::string& detail);
    static std::string FormatActionDetail(
        const std::vector<std::pair<std::string, std::string>>& kv);
    void RenderInfoEditor();     // "Info" editor — live action feed
    void RenderDevDataEditor();  // "Dev Panel" editor — live debug data

    // Content sections of the Dev Test window (DevPanels.cpp)
    void RenderSectionIconTestLab();
    void RenderSectionDesignExample();
    void RenderSectionThemePreview();
    void RenderSectionTestZone1();
    void RenderSectionTestZone2();

    // Floating windows (Dev/Windows.cpp)
    void RenderFloatingWindows();
    void RenderSettings();             // no-op shim (Preferences = own OS window)
    void RenderDesignSystemWindow();   // old DS/Shortcuts/Icons tabs window
    void RenderDevTestWindow();        // dev test panels

    // Generate the .acu extension icon from the app logo (SVG → multi-size .ico
    // next to the exe) and register the per-user shell integration (icon +
    // thumbnail provider DLL). Called once at startup; cheap/idempotent.
    void RegisterShellIntegration();

    // Set the OS window icon (taskbar / Alt-Tab) from the app logo SVG.
    void SetWindowIconFromLogo();

    // ── File actions (Actions.cpp) ───────────────────────────────────────────
    // Save/open are DISABLED during the Ink rework: the .acu v1 codec lives in
    // src/_legacy/ and the v2 format arrives with ROADMAP Lot 10. The actions
    // remain (menus/shortcuts/splash keep working) but only log an Info entry.
    void Action_NewFile();    // = new empty project
    void Action_OpenFile();   // disabled — logs "unavailable"
    void Action_SaveFile();   // disabled — logs "unavailable"
    void Action_SaveFileAs(); // disabled — logs "unavailable"

    // New File from the splash, with a layout preset. If the current project has
    // unsaved changes, opens the "Unsaved changes" dialog first (Save / Don't
    // Save / Cancel); otherwise creates the new project immediately.
    void RequestNewFile(LayoutPreset preset);
    void DoNewFile(LayoutPreset preset, bool applyLayout);
    void RenderUnsavedDialog();   // the modal; called each frame from Update()
    // Fresh document into project_ + hand it to the Ink engine. `seedDemo`
    // seeds the transitional Classic demo content (module projects skip it —
    // the module seeds its own via OnDocumentCreated); `pageW/pageH` size the
    // default page (modules override via DefaultPageSize).
    void ResetDocument(bool seedDemo = true,
                       double pageW = 1920.0, double pageH = 1080.0);

    // ── Modules ──────────────────────────────────────────────────────────────
    void RegisterModules();
    void RequestOpenModule(const std::string& moduleId);
    void DoOpenModule(const std::string& moduleId);
    // nullptr = Classic mode. rebuildLayout=false keeps the current zone tree.
    void ActivateModule(Modules::IModule* mod, bool rebuildLayout = true);
    // Modules::ModuleHost — the app services a module may drive (the Ink
    // document services landed with Lot 11; PushDocCommand / LogInfoAction
    // above are also host services).
    void MarkDirty() override;
    Ink::Document* Document() override { return project_.document.get(); }
    // Commit the pending new-file/open-module intent (preset vs module).
    void CommitPendingNew();
    Modules::IModule*     activeModule_ = nullptr;     // nullptr = Classic
    Modules::Capabilities activeCapabilities_{};       // gates core features

    // ── Project persistence — .acu v2 (ProjectIO.cpp, docs/Ink/ROADMAP Lot 10) ─
    // Apply a pending open/save resolved from the (async) file dialog. Called
    // from RenderFrame, OUTSIDE the ImGui frame. An open with unsaved changes
    // routes through the "Unsaved changes" dialog (pendingOpenPath_); a save is
    // ARMED here and committed at the end of the frame (FinishSavePass) so the
    // page-1 thumbnail can render through the normal Ink frame first.
    void ProcessPendingFileOp();
    // Load `path`, replacing the document, editing state and zone layout.
    void LoadProjectFromFile(const std::string& path);
    // Around ink_->EndFrame(): set up the thumbnail view / read it back and
    // write the armed .acu (both no-ops while no save is pending).
    void PrepareSavePass();
    void FinishSavePass();
    // Async SDL dialog callbacks (any thread): stash into pendingFile_ only.
    static void DialogOpenChosen(void* user, const char* const* files, int filter);
    static void DialogSaveChosen(void* user, const char* const* files, int filter);

    // Recent files (most-recent first), shown on the splash and persisted in the
    // OS user-prefs folder (SDL_GetPrefPath) — NOT the working dir.
    std::string RecentFilesPath() const;          // <prefs>/recent.txt
    void        LoadRecentFiles();                // read + drop missing files
    void        SaveRecentFiles() const;          // write the current list
    void        AddRecentFile(const std::string& path);  // dedup, front, cap, save

    // ── Generic actions (Actions.cpp) ────────────────────────────────────────
    void Action_Quit();
    void Action_ToggleSettings();
    void Action_ToggleTokenGraph();
    void Action_ToggleImGuiDemo();
    static void Action_Zone1();
    static void Action_Zone2();
    static void Action_ThemePreviewCycle();
    // Set the active tool by id (ToolManager). Tools act again from Ink Lot 8.
    void Action_ActivateNamedTool(const std::string& toolId);
    // Per-leaf view requests, consumed by the (future) Viewport render.
    void Action_ViewFitDocument();
    void Action_ViewFitSelection();
    void Action_ViewResetOrigin();

    // ── Undo / Redo ──────────────────────────────────────────────────────────
    // The main (document) history was snapshot-based on the old document and is
    // quarantined; Ink brings command-based undo with Lot 8. The PREFERENCES
    // window keeps its own working history (design-system override snapshots).
    enum class UndoTarget { Viewport, Preferences };
    UndoTarget             activeUndoTarget_ = UndoTarget::Viewport;
    UndoStack<std::string> prefsUndo_;
    std::string            prefsUndoLast_;     // last committed overrides blob
    bool                   prefsUndoInited_ = false;
    int                    undoBufferSteps_ = 256;   // Preferences ▸ General
    void InitPrefsUndo();              // wire capture/restore, seed baseline
    void CommitPrefsUndoIfChanged();   // end of the Preferences frame
    std::string CapturePrefsOverrides() const;        // serialise overrides
    void RestorePrefsOverrides(const std::string&);   // deserialise + apply
    void Action_Undo();   // routes on activeUndoTarget_ (viewport = no-op, Lot 8)
    void Action_Redo();

    // Core
    SDL_Window* window_      = nullptr;
    bool        running_     = true;
    // True once ImGui + Vulkan backends are fully initialized. The SDL event
    // watch (live-resize) must NOT render before this.
    bool        initialized_ = false;
    // Guards against nested RenderFrame() calls from the SDL resize event watch.
    bool        inRenderFrame_ = false;
    float       mainScale_   = 1.0f;

    // Vulkan (shared device model — Ink adopts these handles at Lot 1).
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    ImGui_ImplVulkanH_Window mainWindowData_;
    // True when the device was created with the modern Vulkan 1.3 features Ink
    // requires (dynamic rendering + synchronization2 + timeline semaphores,
    // plus descriptor indexing where present). Detected in SetupVulkan().
    bool modernVulkanSupported_ = false;

    // Last font (family, weight) applied from design-system tokens.
    std::string lastFontFamily_;
    int         lastFontWeight_ = -1;

    // Pending file operation resolved from the ASYNC SDL file dialog. The
    // dialog callback may fire on another thread, so it only stashes the result
    // here (guarded); ProcessPendingFileOp() applies it on the main thread,
    // outside the ImGui frame. kind: 0 none, 1 open, 2 save-to-path.
    struct PendingFileOp {
        std::mutex  mtx;
        int         kind = 0;
        std::string path;
    };
    PendingFileOp pendingFile_;

    // Recent .acu paths, most-recent first (max kMaxRecentFiles).
    std::vector<std::string> recentFiles_;
    static constexpr size_t  kMaxRecentFiles = 10;

    // UI state. Floating windows are unique and non-dockable.
    bool showSettings_      = false;  // Preferences window (own OS window)
    bool showTokenGraph_    = false;  // Token Graph editor window
    bool showDesignSystem_  = false;  // old Design System / Shortcuts / Icons window
    bool showImGuiDemo_     = false;
    bool showDevWindow_     = false;  // dev test panels; off by default

    // Title bar + splash state.
    bool   showSplash_ = true;     // Blender-style start screen, shown on launch
    // True for the FIRST frame after showSplash_ flips on, so the "click outside
    // dismisses" test is skipped that frame.
    bool   splashJustOpened_ = false;
    bool   showAbout_  = false;    // "About Carto" popup request

    // Pending "New File" intent from the splash (see RequestNewFile).
    bool         unsavedDialogOpen_ = false;  // the modal is up
    LayoutPreset pendingNewPreset_  = LayoutPreset::General;
    // When non-empty, the pending new-file intent targets a MODULE.
    std::string  pendingModuleId_;
    // Set when the unsaved dialog chose "Save": the pending new/open intent
    // commits only after the save actually writes (FinishSavePass).
    bool         newFileAfterSave_  = false;
    // Open intent held while the "Unsaved changes" dialog resolves.
    std::string  pendingOpenPath_;
    // Armed save: the path to write at the END of this frame, after the
    // thumbnail view rendered through ink_->EndFrame() (ProjectIO.cpp).
    std::string  pendingSavePath_;
    // Address = the Ink view key of the off-screen page-thumbnail view.
    int          thumbViewKey_ = 0;
    // Window op requested by a system button, DEFERRED to the next frame's
    // ProcessEvents (SDL window calls mid-frame re-enter RenderFrame).
    enum class WindowOp { None, Minimize, ToggleMaximize, ToggleFullscreen, Close };
    WindowOp pendingWindowOp_ = WindowOp::None;
    bool     maximized_ = false;             // home-grown maximize state
    SDL_Rect restoreRect_ = { 0, 0, 0, 0 };  // window rect before maximizing
    bool     fullscreen_  = false;           // F11 borderless-desktop fullscreen
    // Set while WE move/resize the window (SetMaximized), so the
    // SDL_EVENT_WINDOW_MOVED watch ignores our own moves.
    bool     programmaticMove_ = false;
    float  titleBarHeightPx_ = 0.0f;  // physical height, published for hit-test
    // Screen rects (physical px) of interactive title-bar widgets the SDL
    // hit-test must treat as NORMAL (not draggable). Rebuilt every frame.
    std::vector<SDL_Rect> titleBarBlockers_;
    // Splash image, decoded once into a Vulkan texture.
    ImTextureID splashTex_    = ImTextureID(0);
    int         splashTexW_   = 0;
    int         splashTexH_   = 0;

    // The shared project (owns the Ink::Document — see Project.h).
    Project project_;

    // ── Editing loop state (Lot 8) ───────────────────────────────────────────
    // The selection/mode/pivot/orientation/snap context is PER-DOCUMENT (every
    // Viewport zone shares it, like Blender). The command-based document undo
    // replaces the quarantined snapshot history.
    EditContext  edit_;
    DocUndoStack docUndo_;
    TransformOp  transformOp_;   // modal G/R/S in flight (kind == None if idle)
    CanvasDrag   canvasDrag_;     // box-select / draw-shape gesture in flight
    // Viewport popups. RULE (learned the hard way): an id-string popup is scoped
    // to the window that calls OpenPopup, so OpenPopup and BeginPopup must run in
    // the SAME window scope — and BeginPopup must be called every frame or the
    // frame freezes. Hence: a click only ARMS `*Requested`; Update() (root scope,
    // unconditional) issues the single OpenPopup and renders the menu.
    bool         addMenuOpen_ = false, addMenuRequested_ = false;
    ImVec2       addMenuPos_{};
    bool         viewportCtxOpen_ = false, viewportCtxRequested_ = false;
    ImVec2       viewportCtxPos_{};
    Ink::NodeId  viewportCtxNode_ = Ink::kNullNode;   // clicked object (or null)
    // Convenience: the currently hovered viewport leaf's EditorState (set each
    // frame by RenderViewport when hovered), used by mode-less shortcut actions.
    EditorState* hoveredViewport_ = nullptr;
    // The hovered leaf's camera this frame — lets G/R/S (fired by keyboard,
    // outside RenderViewport) map the mouse to document space.
    ViewCam      hoveredCam_{};
    bool         osCursorHidden_ = false;   // we hid the OS cursor for a modal op
    // Screen rect of the hovered viewport's canvas this frame (wrap bounds).
    ImVec2       canvasRectMin_{}, canvasRectMax_{};
    // Modal mouse capture (ViewportModal.cpp): SDL RELATIVE mouse mode for the
    // whole op — the OS cursor is grabbed/hidden, raw xrel/yrel deltas are
    // accumulated in ProcessEvents, and NO warp ever happens during the op
    // (Blender's grab model: drift is impossible by construction).
    bool         modalRelMode_ = false;
    ImVec2       modalRelAccum_{};          // raw px since last UpdateTransform
    void   SetModalMouseCapture(bool on);
    // Release the capture and land the OS cursor on the displayed cursor.
    void   EndModalCapture();
    // Fold an unbounded virtual point into the canvas rect (pure math).
    ImVec2 WrapPointInCanvas(ImVec2 p) const;
    // Draw the legacy transform cursor icon (multi-directional for Move; the
    // double-arrow parallel to the guide for Scale, tangent for Rotate) at the
    // wrapped virtual position, on the ImGui foreground list.
    void   DrawTransformCursor(const ViewCam& cam);

    // The Ink render engine (docs/Ink/). Shares the app's Vulkan device;
    // frame protocol: BeginFrame() before the UI build, per-zone views during
    // it (Viewport.cpp), EndFrame() after — the recorded canvas work is
    // ordered before ImGui's sampling by same-queue barriers, no semaphores.
    // nullptr when the device lacks the Vulkan 1.3 features (the Viewport
    // shows a placeholder and the rest of the app keeps working).
    std::unique_ptr<Ink::Renderer> ink_;

    // Blender-style dynamic zone tree (no native docking UX).
    ZoneLayout zoneLayout_;

    // UI components
    DesignSystem::TokenEditor        tokenEditor_;
    UI::ShortcutEditor               shortcutEditor_;
    VectorGraphics::IconEditorWindow iconEditor_;
    UI::SettingsWindow               settingsWindow_;
    // The Preferences UI lives in its OWN OS window (separate SDL+Vulkan
    // window + 2nd ImGui context); settingsWindow_ draws its content there.
    SecondaryWindow                  settingsHost_;

    // The Token Graph editor, in its own detached OS window.
    UI::TokenGraphWindow             tokenGraphWindow_;
    SecondaryWindow                  tokenGraphHost_;

    // Every detached OS window the app drives. ProcessEvents routes events to
    // them and RenderFrame renders them after the main frame.
    std::vector<SecondaryWindow*>    secondaryWindows_;

    // Singleton-self for non-static actions (callbacks captured by lambda).
    static Application* s_instance_;
};

} // namespace App

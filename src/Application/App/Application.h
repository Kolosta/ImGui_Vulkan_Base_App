#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <UI/Widgets/ListRow.h>
#include <UI/Tokens/TokenEditor.h>
#include <UI/Shortcuts/ShortcutEditor.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Settings/SettingsWindow.h>
#include <UI/Tokens/TokenGraphWindow.h>
#include <VectorGraphics/editors/IconEditorWindow.h>
#include <Renderer/Render/CanvasRenderer.h>
#include "ZoneLayout.h"
#include "PageLayout.h"
#include "Project.h"
#include "SecondaryWindow.h"
#include "UndoStack.h"
#include "ModuleAPI.h"      // module contract (Modules::IModule / Capabilities)

namespace UI { struct MenuEntry; }   // for BuildMergeSubmenu (PopupMenu.h in .cpp)
#include "ViewportTools.h"

namespace App {

// Application implements Modules::ModuleHost so an active module can reach the
// document + a few core operations without touching Application internals.
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
    // title bar AND the old ImGui main menu bar with one borderless, fully
    // styled bar (logo menu, File/Edit/Windows menus, project tabs, a test
    // dropdown, and redrawn OS window buttons). Window drag / snap / resize
    // stay native via the SDL hit-test callback.
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
    // old restore position (which would yank the window away from the mouse).
    // The native HTCAPTION drag keeps following the cursor afterwards.
    void RestoreFromDragAtCursor();
    // Cancel an OS-initiated maximize (caption double-click / drag-to-top) and
    // convert it into our own usable-bounds maximize, before the swapchain ever
    // sees a full-monitor size.
    void InterceptOsMaximize();
    // F11: borderless fullscreen-desktop toggle (title bar stays visible).
    void ToggleFullscreen();

    // Splash screen (Splash.cpp) — Blender-style start screen shown on launch
    // and from the logo menu; "About Carto" info popup; image decode helper.
    void RenderSplash();
    void RenderAbout();
    void LoadSplashTexture();

    // Layout (ApplicationUI.cpp)
    void RenderMainLayout();
    void RenderToolbar();           // standalone palette (legacy / unused now)
    void RenderToolbarInto(ImVec2 origin, EditorState& st);  // floating tools inside a canvas
    // Default fill/stroke colour swatches in the Viewport top bar (filled disc =
    // fill, ring = stroke). Click opens a token-styled colour picker; the chosen
    // colours seed new shapes/curves. `barH` is the editor-bar height.
    void DrawDefaultColorSwatches(float barH);
    // Seed every part of a freshly built shape with the default fill/stroke colours
    // (keeps each part's enabled flags as the caller set them).
    void ApplyDefaultColors(Renderer::Shape& s) const;
    // Right-side reusable EditorSidePanel for the Viewport: declares its tabs
    // (only "Pages", in Single* modes) and delegates the chrome to the shared
    // UI::EditorSidePanel widget. `cMin/cMax` = the canvas rect (screen px).
    void RenderViewportSidePanel(EditorState& st, ImVec2 cMin, ImVec2 cMax);
    void RenderViewportPagesTab(EditorState& st, ImVec2 conMin, ImVec2 conMax);

    // True when something OTHER than the canvas owns the pointer this frame, so
    // the viewport tools must NOT arm the blue box-select: a layout gesture
    // (separator resize / corner split-swap / tab drag, via ZoneLayout) or any
    // active ImGui item (side-panel handle/tab, a top-bar button, the "+"…).
    // Without this guard, click-dragging those overlays also rubber-bands the
    // canvas underneath.
    bool ToolsBlockedByOtherAction() const {
        return zoneLayout_.LayoutGestureActive() || ImGui::IsAnyItemActive() ||
               (outlinerPickingState_ != nullptr);   // a click is choosing a sync target
    }
    void RenderMainContent();
    void RenderStatusBar();

    // The 2D canvas editor: artboard, unit-aware rulers (top + left), unit
    // label at their crossing, blue cursor guides, floating tool palette pinned
    // left inside the canvas, and the Vulkan-rendered vector document. Drawn
    // into the given zone size. Camera/document live in the leaf's EditorState.
    void RenderViewport(ImVec2 size, EditorState& st);

    // Draw the real-time performance metrics overlay (FPS, frame ms, triangles,
    // draw calls, shapes drawn/cached/culled, tessellation + GPU-wait time) in the
    // bottom-left of the canvas. Gated by `showMetrics_` (toggle in the viewport bar).
    void DrawMetricsOverlay(ImVec2 canvasMin, ImVec2 canvasMax);
    bool showMetrics_ = false;
    // While snapping is active during a transform, preview every possible snap point
    // for the current mode in VIOLET (vertex dots, edge-center triangles, face
    // squares, grid dots) — EXCEPT the moving selection. Hidden once a snap actually
    // engages (snapIndicator_.snapped) so only the orange snap glyph shows. Edge mode
    // shows nothing (you can snap anywhere on an edge). d2sDoc/effZoom from the leaf.
    void DrawSnapCandidates(const std::function<ImVec2(Renderer::Vec2)>& d2sDoc,
                            float effZoom, ImVec2 canvasMin, ImVec2 canvasMax,
                            ImDrawList* dl);

    // The hovered viewport's mouse position in RAW document units, refreshed each
    // frame while a Viewport is scope-hovered. Lets globally-dispatched actions
    // (e.g. the E extrude shortcut) read the cursor direction without re-deriving
    // the per-leaf screen→doc mapping. Valid only when `lastHoverValid_`.
    Renderer::Vec2 lastHoverDoc_{0, 0};
    bool           lastHoverValid_ = false;
    // Extrude (E) cycle state: pressing E repeatedly on the SAME active vertex
    // cycles the extrude mode (insert prev/next ↔ branch/outward). Reset when the
    // active vertex changes.
    int            extrudeCycle_ = 0;
    Renderer::VertRef extrudeSourceVert_{};   // where the current E-chain began
    Renderer::VertRef extrudeCreatedVert_{};  // the point the last E created
    bool           extrudeJustCreated_ = false; // last E created a point not yet moved
    bool           extrudeWasBranch_   = false; // last E branched (added 2 nodes)

    // Drive the active drawing tool over this Viewport leaf (defined in
    // ViewportTools.cpp). `s2d`/`d2s` convert between screen px and RAW document
    // units (the unit scale is folded into the converters, so tools work in the
    // document's own coordinates). `effZoom` is the effective px-per-doc-unit
    // (st.zoom * unitScale) for adaptive tessellation + hit-test tolerance.
    // `hovered` gates whether this leaf owns the gesture; `dl` is the overlay
    // draw list for in-progress previews + handles.
    void HandleViewportTools(EditorState& st,
                             const std::function<Renderer::Vec2(ImVec2)>& s2d,
                             const std::function<ImVec2(Renderer::Vec2)>& d2s,
                             float effZoom, bool hovered, ImDrawList* dl);

    // Curve tool (tool.curve, Edit Mode): build a Bézier curve interactively.
    // Click = a straight (Vector) point; click-drag = a Bézier point (pull the
    // out-tangent, in-handle mirrored). Double-click / Enter finishes; Esc / RMB
    // cancels. Defined in ViewportTools.cpp; same converter/zoom/hover contract.
    void HandleCurveTool(EditorState& st,
                         const std::function<Renderer::Vec2(ImVec2)>& s2d,
                         const std::function<ImVec2(Renderer::Vec2)>& d2s,
                         float effZoom, bool hovered, ImDrawList* dl);
    // Finish the in-progress curve gesture into a real Bézier Shape (no-op if
    // fewer than 2 points). `closed` makes it a filled area.
    void FinishCurveGesture(bool closed);
    // Build the Bézier Path for the current curve gesture's points/tangents. If
    // `provisional` is given, it is appended as a trailing anchor (the mouse) with
    // no handles. `closed` marks the path cyclic. Shared by FinishCurveGesture and
    // the live preview so they always match.
    Renderer::Path BuildCurvePath(bool closed, const Renderer::Vec2* provisional) const;
    // Snap the curve-tool cursor `world` to document geometry when snapping is on
    // (magnet enabled OR Ctrl held), using the active snap mode — exactly like the
    // transform snap, but anchored on the cursor itself (a new point being placed).
    // Excludes the object the gesture is editing (`exclude`, e.g. shapeContinue) so
    // the curve never snaps onto its own just-placed points. Publishes snapIndicator_
    // so RenderViewport draws the same orange glyph. Returns the (possibly) snapped
    // world point; if no snap applies it returns `world` unchanged.
    Renderer::Vec2 CurveSnapPoint(Renderer::Vec2 world, float effZoom, uint64_t exclude);
    // Follow-curve (Shift) for the curve tool: project `mRaw` onto the nearest
    // target curve/edge, draw the blue diamond + the traced segment preview, and on
    // a Shift-click APPEND the exact target nodes between the anchor and the cursor
    // to the gesture. Returns true when following is active this frame (the caller
    // then suppresses its normal point-add / preview for the trailing segment).
    // `committed` is set true when a Shift-click froze a traced piece this frame.
    bool UpdateFollowCurve(const std::function<ImVec2(Renderer::Vec2)>& d2s,
                           Renderer::Vec2 mRaw, float effZoom, bool lpressed,
                           ImDrawList* dl, bool& committed);
    // Recompute the IN handles (toolState_.tangentsIn) of the in-progress curve
    // from its points + dragged OUT handles, OpenOrienteering-Mapper style: each
    // point's IN handle is ALIGNED (collinear-opposite) to its OUT handle but its
    // LENGTH is derived from the chord to the previous point so the curve stays
    // smooth (no distortion) — the user only ever drags the OUTER handle.
    void RecomputeCurveInHandles();
    // Live preview of the styled curve while drawing (core style or IOF symbol):
    // committed segments SOLID, the in-progress segment TRANSLUCENT; for a surface,
    // the provisional CLOSED area is filled solid with only its trailing stroke
    // translucent. Drawn under the blue rubber-band guide. Returns nothing.
    void DrawStyledCurvePreview(const std::function<ImVec2(Renderer::Vec2)>& d2s,
                                float effZoom, Renderer::Vec2 mouse, bool snapClose);

    // Line-mark tool (tool.linemark): move/delete existing line marks, or drop a
    // new one on a compatible curve — the KIND is auto-chosen from the curve
    // symbol (slope tick on contours 101-103, pylon on power lines 510/511).
    // Crossing points are placed via the 519 symbol, not here. Click a mark to
    // delete it (on release); click-drag to move it. Defined in ViewportTools.cpp.
    void HandleLineMarkTool(EditorState& st,
                            const std::function<Renderer::Vec2(ImVec2)>& s2d,
                            const std::function<ImVec2(Renderer::Vec2)>& d2s,
                            float effZoom, bool hovered, ImDrawList* dl);
    // Translucent preview of a line mark at world point `p` (tangent `tan`),
    // mirroring the tessellator render. Drawn while hovering a compatible line.
    // `curve` (optional, world-space flattened subpath) + `closed` let crossing /
    // bridge ghosts sample their gap-end ticks ALONG the curve (arc-length) so they
    // follow the path; if null they fall back to a straight tangent offset.
    void DrawLineMarkGhost(const Renderer::Shape& s, const Renderer::Part& part,
                           const Renderer::LineMark& m, Renderer::Vec2 p,
                           Renderer::Vec2 tan,
                           const std::function<ImVec2(Renderer::Vec2)>& d2s, float zoom,
                           const std::vector<Renderer::Vec2>* curve = nullptr,
                           bool curveClosed = false);
    // A mark's HANDLE (the clickable point shown by the Line-Mark tool), drawn in
    // the geometry-point style: a filled dot (violet = dash-anchor "dash", green =
    // "gap", accent = other marks; orange when selected) + a select/hover overlay
    // (ring for normal marks; a diamond/square for dash-anchors so the mode reads).
    // `tanScreen` is the curve tangent at the point (screen space) for orientation.
    enum class MarkHandleState { Normal, Hover, Selected };
    void DrawMarkHandle(ImVec2 sp, Renderer::Vec2 tanScreen,
                        const Renderer::LineMark& m, MarkHandleState state);
    // Active drag of an EXISTING line mark (click-drag to move it along the line /
    // flip its side). `armed` = pressed on a mark, not yet dragged past threshold.
    // While dragging, the REAL mark stays put and only a translucent ghost follows
    // the cursor at (dragT, dragSide); the move is committed on release.
    // A mark-type change that a plain CLICK (press + release with no drag past the
    // threshold) performs on an already-selected mark. Deferred to release so a
    // click-drag MOVES instead (Blender: drag the grabbed item). None = the click
    // just selects/keeps the mark.
    enum class MarkClickMode { None, CycleFormLine, ToggleDashAnchor };
    struct MarkDrag {
        bool     active = false;
        bool     armed  = false;     // pressed, awaiting move past threshold
        uint64_t shape  = 0;
        int      part   = -1;
        int      index  = -1;        // index into part.marks
        ImVec2   pressPos{0, 0};
        float    dragT    = 0.0f;    // ghost target arc-length fraction
        int      dragSide = +1;      // ghost target side
        bool     selectionDrag = false;  // dragging the whole mark selection (not just one)
        // Pending type change applied on release if the press becomes a click (no
        // drag). Resolved against the mark's current kind at release time.
        MarkClickMode pendingMode = MarkClickMode::None;
    } markDrag_;

    // Line marks behave as quasi-objects under the Line-Mark tool: select (single /
    // Shift / box), G = move along the curve, R = flip slope-tick side, S = scale a
    // crossing's interval. These modal states drive the keyboard ops (mirroring the
    // object transform op, but specialised to the "along the curve" motion).
    enum class MarkOp { None, Grab, Scale };
    struct MarkGrab {
        MarkOp                op = MarkOp::None;
        const void*           owner = nullptr;
        ImVec2                startMouse{0, 0};
        std::vector<Renderer::MarkRef> refs;     // affected marks
        std::vector<float>    t0;                 // their t at start (per ref)
        std::vector<int>      side0;              // their side at start
        std::vector<float>    gap0;               // their gap at start (Scale)
        bool Active() const { return op != MarkOp::None; }
        void Reset() { op = MarkOp::None; owner = nullptr; refs.clear();
                       t0.clear(); side0.clear(); gap0.clear(); }
    } markGrab_;
    // Box-select of marks (Line-Mark tool).
    struct MarkBox {
        bool active = false; const void* owner = nullptr;
        Renderer::Vec2 start{}, now{}; bool additive = false;
    } markBox_;
    // World position + tangent of a mark (for hit-test / overlay / drag). Returns
    // false if the mark or its host can't be resolved. ViewportTools.cpp.
    bool MarkWorldPoint(const Renderer::MarkRef& ref, float zoom,
                        Renderer::Vec2& outPos, Renderer::Vec2& outTan);
    // Start a G/R/S op on the selected marks. Move/Scale arm the modal `markGrab_`;
    // Rotate is instantaneous (flips the side of every flippable mark). Mixed
    // selections are handled per-mark (only flippable marks flip, only crossings
    // scale). ViewportTools.cpp.
    void BeginMarkTransform(TransformKind kind);
    // Delete every selected line mark. ViewportTools.cpp.
    void DeleteSelectedMarks();

    // Edit Mode (Blender-style vertex/edge/face editing of the selected objects),
    // defined in EditMode.cpp. Drives selection, handle editing, snap/merge while
    // editorMode_ == Edit. Same converter/zoom/hover contract as HandleViewportTools.
    void HandleEditMode(EditorState& st,
                        const std::function<Renderer::Vec2(ImVec2)>& s2d,
                        const std::function<ImVec2(Renderer::Vec2)>& d2s,
                        float effZoom, bool hovered, ImDrawList* dl);
    // Overlay for Edit Mode (points/edges/faces + handles), drawn every frame for
    // each Viewport leaf showing the editable objects.
    void DrawEditOverlay(const std::function<ImVec2(Renderer::Vec2)>& d2s,
                         float effZoom, ImDrawList* dl);
    // Extrude tool (tool.extrude, Edit Mode): draws a ring around the active
    // vertex; grabbing inside it extrudes a new connected point that follows the
    // mouse and drops on release. Defined in EditMode.cpp (uses NodeWorld).
    void HandleExtrudeTool(EditorState& st,
                           const std::function<Renderer::Vec2(ImVec2)>& s2d,
                           const std::function<ImVec2(Renderer::Vec2)>& d2s,
                           float effZoom, bool hovered, ImDrawList* dl);
    // Insert a new node connected to the active vertex (endpoint extension), make
    // it the sole vertex selection. Returns false if there's no extrudable active
    // vertex. Shared by the E shortcut and the Extrude tool.
    bool ExtrudeFromActiveVertex(Renderer::VertRef& outNew);
    // E shortcut: extrude the active vertex and start a modal Move (click to drop).
    void Action_ExtrudeActiveVertex();
    // One extrude step with cycle bookkeeping (freshChain resets the cycle); and
    // the revert of the last extrude (for cyclic E re-application).
    bool DoExtrudeStep(bool freshChain, Renderer::VertRef& outNew);
    void RevertLastExtrude();

    // Edit-mode actions (also reachable from the context menu / shortcuts).
    void Action_MergeVertices(int mode);   // 0 center, 1 cursor, 2 by-distance
    void Action_DeleteElements();          // delete selected verts/edges/faces
    void Action_SetHandleType(Renderer::HandleMode mode); // V-menu on active verts
    void Action_RemoveHandles();           // strip handles → straight corner point
    void Action_ToggleCloseCurve();        // close/open the active object's path
    void Action_ConvertSelectionTo(Renderer::PartType target); // Mesh ⇄ Curve family
    void Action_SetSplineType(Renderer::SplineType target);    // Bézier/NURBS/Poly
    // If a creation tool just made a NEW shape while in Edit Mode, fold its parts
    // into object `hostId` (rebased to its local space) and delete the new shape
    // — so drawing inside Edit Mode adds geometry to the edited object.
    void FoldNewShapeIntoObject(uint64_t hostId);
    // Edit-mode menus (reuse UI::ContextMenu, same style as the object menu).
    void RenderEditContextMenu();          // RMB: Merge ▸ / Set Handle Type ▸ / Convert ▸ / Delete / Close
    void RenderMergeMenu();                // M: Merge ▸ (At Center / Cursor / First / Last / By Distance)
    void RenderHandleTypeMenu();           // V: Free / Aligned / Mirrored / Vector
    // Shared submenu builders (members so they reach the private Action_*).
    // Returns are by value of UI::MenuEntry (forward-declared); PopupMenu.h is
    // included in the .cpp that defines/uses them.
    std::vector<UI::MenuEntry> BuildMergeSubmenu();
    std::vector<UI::MenuEntry> BuildHandleTypeSubmenu();

    // The object context menu (right-click in the Viewport): Delete / Join /
    // Set Origin… — each row shows its shortcut. Defined in ViewportTools.cpp.
    void RenderViewportContextMenu();

    // Cancel the in-progress Viewport gesture (Esc / right-click): revert a move
    // to its original translate, drop a half-placed polyline/curve, etc., and
    // reset the tool state. Returns true if a gesture was actually cancelled
    // (so the caller knows not to also open the context menu). ViewportTools.cpp.
    bool CancelViewportGesture();
    // Draw the multi-directional move cursor at the mouse (during a grab-move).
    void ShowMoveCursor();
    // Draw a cursor icon at the mouse, rotated by `angleRad` (for the rotate /
    // scale directional cursors that align to the pivot→mouse line).
    void ShowOrientedCursor(const char* iconId, float angleRad);
    // ── Drift-free wrapped-drag helper (reusable by any editor) ──────────────
    // GestureMouseDelta() returns the REAL pointer motion (screen px) since the
    // last call, correctly excluding any cursor warp WE performed — by tracking
    // our own reference position rather than trusting io.MouseDelta (which jumps
    // across a warp). Call BeginGestureMouseTracking() once when a drag starts.
    void   BeginGestureMouseTracking();          // seed the reference to current pos
    ImVec2 GestureMouseDelta();                   // real motion since last call
    // ── Global precision-drag (Shift) ────────────────────────────────────────
    // Blender-style "finer movement": holding Shift during ANY drag slows the
    // RELATIVE motion of what's being dragged — NOT the cursor — by this factor.
    // Returns 1.0 normally, a small value (kPrecisionFactor) while Shift is held.
    // Used wherever a drag integrates motion (G/R/S transforms, line-mark moves,
    // vertex/handle drags) and by UI sliders for sub-unit precision. Centralised
    // here so every drag site shares one definition and one feel.
    float  PrecisionDragFactor() const;
    // Adaptive "nice" grid step in doc-units for Ctrl snapping during a transform,
    // matching the ruler subdivision at the current zoom (so a snap is always
    // meaningful on screen). effZoom = st.zoom * unitScale.
    float  SnapGridStep(float effZoom) const;
    // Blender-style infinite drag: if the cursor left the screen rect
    // [rectMin,rectMax] (the active ZONE's canvas), warp it to the opposite edge
    // and update the reference so the NEXT GestureMouseDelta excludes the warp
    // jump but keeps the user's real motion. Returns true if it warped.
    bool   WrapMouseInRect(ImVec2 rectMin, ImVec2 rectMax);
    // The active gesture's canvas rect (screen px), published by RenderViewport
    // each frame for the owning leaf so the modal transform can wrap correctly.
    ImVec2 gestureCanvasMin_{0, 0};
    ImVec2 gestureCanvasMax_{0, 0};
    // Middle-drag (or Hand + left-drag) camera pan. Captured on the leaf where the
    // drag began and kept until the button releases — so the pan keeps running and
    // wraps the cursor edge-to-edge even when the cursor leaves the canvas rect
    // (an instantaneous hover test would otherwise stop the pan at the border,
    // before the wrap could fire). nullptr = no pan in progress.
    EditorState* panOwner_ = nullptr;
    // Our self-tracked reference cursor position (screen px) for the active drag,
    // updated by GestureMouseDelta() and reset to the warp target by
    // WrapMouseInRect() — the key to drift-free wrapping.
    ImVec2 gestureMouseRef_{0, 0};

    // Outliner editor: a Blender-style tree of the shared project →
    // collections → objects, with drag&drop, rename and alpha sort.
    void RenderOutliner(EditorState& st);                // st = this Outliner leaf's state
    void BuildOutlinerTopBar(EditorState& st, EditorBar& bar);  // Sync | Display | Search | Filter
    void OutlinerObjectRow(Renderer::Shape& s);          // selectable + rename + drag
    // Object row + its parented child objects nested beneath it (Blender), limited
    // to children in the same scope (scopeColl = collection id, or 0 = bare on page).
    void OutlinerObjectSubtree(uint64_t id, uint64_t scopeColl);
    void OutlinerMarkRows(Renderer::Shape& s);           // line marks as sub-objects
    void OutlinerCollectionNode(uint64_t collectionId);  // recursive collection tree
    void OutlinerPageNode(int abIndex);                  // page row (rename + hide)
    void OutlinerPageLayersNode(int abIndex);            // page row + objects in z-order (Layers mode)
    void OutlinerDropIntoCollection(uint64_t collectionId);  // drop-target reparent (objects)
    void OutlinerReorderDropOnObject(uint64_t targetId);     // Layers mode: z-index reorder
    void OutlinerNodeDragSource(uint64_t nodeId, const char* label); // drag a coll/page
    void OutlinerNodeDropInto(uint64_t collectionId);        // drop a coll/page into collection
    std::vector<uint64_t> OutlinerDraggedIds(uint64_t triggerId); // multi-drag set
    void OutlinerEyeButton(bool& visible, const char* id); // per-row visibility toggle
    // Collapsed node summary: a row of type icons (+count badge) for the direct
    // contents of `nodeId` (a collection id|kCollBit, a page id|kPageBit, or a
    // shape id), drawn inline after the name when the node is collapsed.
    void OutlinerCollapsedSummary(uint64_t nodeId);
    // Inline rename field (DragValue-styled: ui-unit tall, no focus ring, spans
    // only the name column → before the eye). Returns true on Enter.
    bool OutlinerRenameField(char* buf, size_t bufSize, bool hasIcon);
    void RenderOutlinerContextMenus();                   // the deferred RMB popups

    // ── Outliner state ───────────────────────────────────────────────────────
    // The per-Outliner state (selection / filters / search / display mode /
    // viewport-sync) now lives in EditorState (see OutlinerState.h) so each
    // Outliner zone is independent. `outlinerCur_` points at the leaf currently
    // being rendered; it is set at the top of RenderOutliner(st) and used by all
    // the Outliner helpers (which run only during that render). The OBJECT part
    // of the selection is kept in sync with the document/viewport selection.
    // Click modifiers: plain = only; Shift = range; Ctrl = add+active; Alt = add.
    OutlinerState* outlinerCur_ = nullptr;
    // The Outliner whose sync button is CURRENTLY in "pick a viewport" mode (only
    // one at a time — it's a modal interaction). Set by the button, read by every
    // Viewport (to paint the orange preview + capture the click) and by
    // ToolsBlockedByOtherAction. nullptr = nobody is picking.
    OutlinerState* outlinerPickingState_ = nullptr;
    // True if `id` (an object) is visible in this Outliner's synced viewport.
    // Always true when no viewport is synced. Folds into OutlinerPassesFilter.
    bool OutlinerSyncShowsShape(uint64_t id) const;
    bool OutlinerSyncShowsPage(int abIndex) const;   // same, for a page row

    // Per-row interaction result (filled by OutlinerRowBegin).
    struct RowResult { bool clicked = false; bool hovered = false; bool pressed = false;
                       bool doubleClicked = false; bool rightClicked = false; };
    // Selection helpers (handle the click modifiers + object↔viewport sync).
    void OutlinerSelectClick(uint64_t id, bool isObject);
    void OutlinerHandleRowInput(uint64_t id, const RowResult& rr, bool isObject);
    bool OutlinerIsSelected(uint64_t id) const;
    // Begin a menu-style Outliner row: lays a full-width invisible hitbox over the
    // (already-drawn) zebra, paints the state background (default/hover/selected
    // active|inactive, search-green), then rewinds the cursor so the row's content
    // (chevron/icon/label) draws on top. `kind` 0 obj/1 page/2 coll. Returns the
    // interaction. `searchHit` true → this row's own name matched the search.
    // `forceSel`/`forceActive` (−1 = auto from the id) let non-id rows (line marks)
    // reuse the exact same row chrome with their own selection state.
    RowResult OutlinerRowBegin(uint64_t id, int kind, bool searchHit,
                               int forceSel = -1, int forceActive = -1);
    // Top Y (screen space) and band extents of the row currently being drawn, set
    // by OutlinerRowBegin so the eye (and any absolute-positioned chrome) can place
    // itself on THIS row without depending on the layout cursor (which has already
    // advanced to the next line by the time the eye is drawn).
    float outlinerRowTopY_  = 0.0f;
    float outlinerBandLeft_ = 0.0f;   // selection-band left edge (with margin)
    float outlinerBandRight_= 0.0f;   // selection-band right edge (with margin)
    // The generic zebra row currently being drawn (RAII). OutlinerRowBegin opens a
    // new one (closing the previous); OutlinerRowFinish closes the last one at the
    // end of the tree. Its destructor advances the layout cursor one stripe.
    std::optional<UI::ListRow> outlinerRow_;
    void OutlinerRowFinish();         // close the open row (end of the tree)
    // Stripe extents (screen Y) of the row most recently opened by OutlinerRowBegin,
    // used to place vertical tree-guide lines deterministically (the live layout
    // cursor is unreliable because rows close lazily). A parent reads its own stripe
    // bottom as the first child's top, and outlinerLastStripeBottom_ after its
    // children as the last child's bottom.
    float outlinerLastStripeTop_    = 0.0f;
    float outlinerLastStripeBottom_ = 0.0f;
    // Search/filter predicates.
    bool OutlinerPassesFilter(uint64_t id, int kind) const;  // kind 0 obj,1 page,2 coll
    bool OutlinerSearchVisible(uint64_t id) const;           // matches or has a matching descendant
    void OutlinerRebuildSearch();                            // recompute matches each frame
    // Right-click context state (the popup opens next frame at this position).
    enum class OutlinerCtxKind { None, Object, Collection, Background };
    OutlinerCtxKind outlinerCtxKind_ = OutlinerCtxKind::None;
    uint64_t        outlinerCtxId_   = 0;     // shape id or collection id
    ImVec2          outlinerCtxPos_{0, 0};
    bool            outlinerCtxOpen_ = false; // request to open the popup
    // ── Internal copy/paste clipboard (NOT the OS clipboard) ──────────────────
    // Holds deep copies of whole OBJECTS and/or PAGES (with their objects), so a
    // selection — from the Viewport or the Outliner — can be copied/cut/pasted.
    // Pages keep their full Artboard (size, name, shapes); objects keep their
    // page-relative geometry + the source page origin so paste can reproduce the
    // world position (or nudge a same-page paste).
    struct ClipObject {
        Renderer::Shape shape;       // deep copy (new id assigned on paste)
        Renderer::Vec2  pageOrigin{0, 0};  // source page top-left (world offset)
    };
    struct Clipboard {
        std::vector<ClipObject>     objects;
        std::vector<Renderer::Artboard> pages;   // deep copies (incl. their shapes)
        bool empty() const { return objects.empty() && pages.empty(); }
        void clear() { objects.clear(); pages.clear(); }
    };
    Clipboard clipboard_;
    // Copy/cut/paste the CURRENT selection. `ids` (when given) overrides the source
    // selection (e.g. the Outliner's node+object set); empty → use the document
    // object selection. Cut = copy then delete. Paste clones with fresh ids:
    // objects onto the active page (nudged), pages as new artboards.
    void ClipboardGather(const std::vector<uint64_t>& ids);   // fill clipboard_ from ids
    std::vector<uint64_t> ClipboardSourceIds() const;  // outliner nodes, or {} for doc sel
    void ClipboardCopy(const std::vector<uint64_t>& ids = {});
    void ClipboardCut(const std::vector<uint64_t>& ids = {});
    void ClipboardPaste();
    void Action_Copy();    // copy the active context's selection (viewport/outliner)
    void Action_Cut();
    void Action_Paste();

    uint64_t outlinerColorPickColl_ = 0;   // collection whose custom-colour picker is open
    bool     outlinerColorPickOpen_ = false; // one-shot: open the picker this frame
    void Action_OutlinerDuplicate(uint64_t shapeId);
    // Properties editor: the active object's name/transform + per-part fill/
    // stroke (and, in Edit Mode, the active point's handle type). Defined in
    // Editors/Properties.cpp. Moved here out of the Outliner.
    void RenderProperties();
    // Per-mark Properties pane (active line mark). Returns true if it rendered.
    bool RenderMarkProperties();

    // Content sections — inline, no Begin/End (ApplicationWindows.cpp)
    void RenderSectionIconTestLab();
    void RenderSectionDesignExample();
    void RenderSectionThemePreview();
    void RenderSectionTestZone1();
    void RenderSectionTestZone2();

    // Floating windows (ApplicationWindows.cpp)
    void RenderFloatingWindows();
    // The new Blender-style Preferences window (real OS window via multi-
    // viewport; see UI::SettingsWindow). Gated by showSettings_.
    void RenderSettings();
    // The OLD settings content (Design System editor + Shortcuts + Icons tabs),
    // kept verbatim in its own classic ImGui window. Gated by showDesignSystem_.
    void RenderDesignSystemWindow();
    // "Dev Test Window": all the former main-area demo panels gathered into
    // one floating, non-dockable window (same organisation as before).
    void RenderDevTestWindow();

    // Regenerate the .acu thumbnail (PNG). The no-arg form uses the project's
    // stored framing (thumbArtboard + thumbRegion*, default = whole Page 1). The
    // parametric form frames a specific artboard, optionally a sub-region (in
    // doc-units; size.x/y <= 1 → whole page), persists that framing, and renders
    // it offscreen via the CanvasRenderer → PNG into project_.thumbnailPng.
    void Action_UpdateThumbnail();
    void Action_UpdateThumbnail(int artboard,
                                Renderer::Vec2 regionMin,
                                Renderer::Vec2 regionSize);

    // Page (artboard) context-menu actions, invoked from RenderPageContextMenu.
    void RenderPageContextMenu();        // the "##pageCtx" popup body
    void BeginThumbnailCrop(int artboard);   // enter interactive Zone crop
    // d2sDoc: doc-units → screen px; s2dDoc: screen px → doc-units (mirrors the
    // RAW-unit converters built in RenderViewport).
    void HandleThumbnailCrop(const std::function<ImVec2(Renderer::Vec2)>& d2sDoc,
                             const std::function<Renderer::Vec2(ImVec2)>& s2dDoc,
                             float pxPer, bool hovered, ImDrawList* dl);

    // Generate the .acu extension icon from the app logo (SVG → multi-size .ico
    // next to the exe) and register the per-user shell integration (icon +
    // thumbnail provider DLL). Called once at startup; cheap/idempotent.
    void RegisterShellIntegration();

    // Set the OS window icon (taskbar / Alt-Tab) from the app logo SVG,
    // rasterised to an SDL_Surface. Called once after the window is created.
    void SetWindowIconFromLogo();

    // File actions — the .acu project lifecycle (ProjectFile + SDL dialogs).
    void Action_NewFile();    // = new empty project (one default page)
    void Action_OpenFile();   // open .acu via dialog → load doc + layout

    // New File from the splash, with a layout preset. If the current project has
    // unsaved changes, opens the "Unsaved changes" dialog first (Save / Don't
    // Save / Cancel); otherwise creates the new project immediately. DoNewFile
    // performs the actual reset + preset application (shared by both paths).
    void RequestNewFile(LayoutPreset preset);
    void DoNewFile(LayoutPreset preset, bool applyLayout);
    void RenderUnsavedDialog();   // the modal; called each frame from Update()

    // ── Modules ──────────────────────────────────────────────────────────────
    // Build the module catalogue (Typography, IOF Mapping, …) and let each add
    // its editors. Called once at init, after RegisterCoreEditors().
    void RegisterModules();
    // Open a module from the splash: like New File (unsaved guard → fresh project)
    // then apply the module's layout / editors / capabilities. Empty id = Classic.
    void RequestOpenModule(const std::string& moduleId);
    void DoOpenModule(const std::string& moduleId);   // reset doc + ActivateModule
    // nullptr = Classic mode. rebuildLayout=false keeps the current zone tree
    // (used on file Load, whose saved layout is authoritative).
    void ActivateModule(Modules::IModule* mod, bool rebuildLayout = true);
    // Modules::ModuleHost — the app services a module may drive.
    Renderer::Document& Document() override;
    void CreateObject(const std::string& presetKind, const std::string& name) override;
    uint64_t CreateObjectSpec(const Modules::ObjectSpec& spec) override;
    uint64_t AddBakedShape(const Renderer::Shape& shape,
                           bool loose, uint64_t collectionId,
                           Modules::ModuleHost::PlaceMode mode =
                               Modules::ModuleHost::PlaceMode::Stamp) override;
    void SetPlacementPreview(const Renderer::Shape& preview) override;
    int  ArmedSymbolCode() const override;
    void MarkDirty() override;
    ImTextureID RenderGlyphTexture(uint64_t key, uint64_t contentHash,
                                   const std::vector<Renderer::Shape>& shapes,
                                   int widthPx, int heightPx, float padFrac,
                                   bool transparent = false,
                                   bool exactFit = false,
                                   const Renderer::Vec2* frameMin = nullptr,
                                   const Renderer::Vec2* frameMax = nullptr) override;
    // Commit the pending new-file/open-module intent (preset vs module). Shared by
    // the direct path, the unsaved dialog, and the post-(async-)save path.
    void CommitPendingNew();
    Modules::IModule*     activeModule_ = nullptr;     // nullptr = Classic
    Modules::Capabilities activeCapabilities_{};       // gates core features
    // True if the active module (if any) permits reparenting `shapeId` into
    // `targetColl`. Consulted by the Outliner drag/drop before re-collectioning.
    bool ModuleAllowsReparent(uint64_t shapeId, uint64_t targetColl) {
        return !activeModule_ || activeModule_->AllowReparent(shapeId, targetColl);
    }
    // True when the active module locks the Outliner tree structure (no drag/drop
    // reorder or re-parent of objects / collections / pages). The Outliner skips
    // every drag source and drop target while this holds.
    bool OutlinerTreeLocked() const { return activeCapabilities_.lockOutlinerTree; }
    void Action_SaveFile();   // save to current path, or prompt if none yet
    void Action_SaveFileAs(); // always prompt for a new path
    // Sync the live menu-bar settings (pivot/orientation/snap/cursor/metrics) INTO
    // project_.editorSettings before a save, and OUT of it after a load — so they
    // persist per .acu and restore on reopen.
    void SyncSettingsToProject();
    void ApplySettingsFromProject();
    // Apply a pending open/save resolved from the (async) file dialog. Called
    // from ProcessEvents, OUTSIDE the ImGui frame.
    void ProcessPendingFileOp();

    // Recent files (most-recent first), shown on the splash and persisted in the
    // OS user-prefs folder (SDL_GetPrefPath) — NOT the working dir. Loaded at
    // init; updated after a successful open / save-to-path.
    std::string RecentFilesPath() const;          // <prefs>/recent.txt
    void        LoadRecentFiles();                // read + drop missing files
    void        SaveRecentFiles() const;          // write the current list
    void        AddRecentFile(const std::string& path);  // dedup, front, cap, save
    void Action_Quit();
    void Action_ToggleSettings();
    void Action_ToggleTokenGraph();
    void Action_ToggleImGuiDemo();
    static void Action_Zone1();
    static void Action_Zone2();
    static void Action_ThemePreviewCycle();
    // Set the active drawing tool by id (cancels any in-progress gesture).
    void Action_ActivateNamedTool(const std::string& toolId);
    void Action_CycleTool();
    void Action_NewDocument();   // add an artboard to the current project
    void Action_NewProject();    // reset to a fresh empty project
    void Action_ViewFitDocument();
    void Action_ViewFitSelection();   // Numpad . — frame selected/active (viewport + Outliner)
    void Action_ViewResetOrigin();

    // Editing actions (Object/Edit mode).
    void Action_ToggleEditMode();   // Tab: Object ⇄ Edit
    void Action_DeleteSelection();  // X: delete the selected objects (or elements)
    void Action_JoinSelection();    // merge selected objects into the active one
    void Action_HideSelection();    // H: hide the selected objects (Object mode)
    void Action_RevealAll();        // Alt+H: reveal every hidden object
    // Ctrl+P: parent every selected object to the ACTIVE one (Blender object
    // parenting). The active object becomes the parent; moving it moves them.
    void Action_ParentSelection();
    // Alt+P: clear the parent of every selected object (keeps them visually put).
    void Action_ClearParent();

    // ── Selection families (Blender Shift+G / Shift+L / Shift+Numpad±) ──────────
    // Select Grouped: extend the selection from the ACTIVE object by a relationship.
    enum class GroupedMode {
        Children = 0,      // all hierarchical descendants of the active object
        ImmediateChildren, // direct children only
        Parent,            // the active object's parent
        Siblings,          // objects sharing the active object's parent
        Type,              // objects of the same geometry type (Mesh/Curve family)
        Collection,        // objects in the active object's collection
        Color,             // objects with the same fill+stroke colour
    };
    // Run Select Grouped in `mode` from the active object (extends the selection).
    void Action_SelectGrouped(GroupedMode mode);
    // Select Linked (Shift+L): objects sharing the active object's "data" — here,
    // same geometry family/kind (no shared data-blocks in this model). Extends.
    void Action_SelectLinked();
    // Select More / Less (Shift+Numpad +/−): grow/shrink the selection by one
    // parent/child ring (Object mode) or by adjacent vertices (Edit mode). `grow`
    // true = More, false = Less.
    void Action_SelectMoreLess(bool grow);
    // Edit-mode element grow/shrink: More adds the path-adjacent nodes of every
    // selected vertex; Less drops vertices that have an UN-selected path neighbour.
    void Action_SelectMoreLessElements(bool grow);
    // Select All / None (A / Alt+A) already exist via other paths; declared if needed.
    // Convert the whole selection to one type, then Join — resolves a mixed
    // (Mesh + curve-like) selection that Join alone refuses (Lot 6).
    void Action_ConvertAllAndJoin(Renderer::PartType target);

    // ── Operator redo panel (Blender's bottom-left F6 box) ─────────────────────
    // After an operator runs it records itself here: a title + adjustable params +
    // a `rerun` closure. The panel (bottom-left of the viewport) shows the title and
    // the params; editing a param calls `rerun()` so the operator re-applies with
    // the new value (from the original snapshot, so tweaks don't compound).
    struct OperatorParam {
        enum class Kind { Enum, Bool, Float };
        std::string label;
        Kind        kind = Kind::Enum;
        int         value = 0;                 // enum index OR bool (0/1)
        float       fvalue = 0.0f;             // Kind::Float value
        std::vector<std::string> options;      // enum labels (Kind::Enum)
    };
    struct OperatorRecord {
        bool        active = false;
        std::string title;
        std::vector<OperatorParam> params;
        std::function<void()> rerun;           // re-applies with current params
        void Reset() { active = false; title.clear(); params.clear(); rerun = nullptr; }
    } lastOperator_;
    // Operator panel expand state: collapsed by default; once the user expands it,
    // it stays expanded for subsequent operations too (Blender-style sticky).
    bool operatorPanelExpanded_ = false;
    // True only while lastOperator_.rerun is executing, so the commit it triggers
    // does NOT dismiss the panel (any OTHER action does).
    bool operatorRerunning_ = false;
    // Draw the operator redo panel into the viewport's bottom-left (under metrics).
    void DrawOperatorPanel(ImVec2 canvasMin, ImVec2 canvasMax, EditorState& st);
    // Record `op` as the last operator (replaces any previous), shown in the panel.
    void SetLastOperator(OperatorRecord op) { lastOperator_ = std::move(op);
                                              lastOperator_.active = true; }
    // Hide the operator panel (any action other than the panel's own re-run clears
    // it — Blender dismisses the redo box as soon as you do something else).
    void DismissOperatorPanel() { if (!operatorRerunning_) lastOperator_.Reset(); }

    // Begin a modal G/R/S transform on the current selection (no-op if empty).
    void Action_BeginTransform(TransformKind kind);
    // After a Move is confirmed: if an object's origin now lies over a DIFFERENT
    // page, re-parent it there (keeping its visual position) — unless Ctrl is
    // held (then it stays on its page, just repositioned). `ids` = moved shapes.
    void MaybeTransferMovedObjects(const std::vector<uint64_t>& ids);
    // On a confirmed G/R/S: log the rich action (Info feed) + publish the operator
    // redo panel with adjustable params. dx/dy/angle/scale = the final amounts.
    void PublishTransformOperator(float dx, float dy, float angle, float scale);
    // Re-apply a transform to `snap`'d shapes (`ids`) about `pivot` in the captured
    // basis, reading the amount from the operator panel's `params`. Used by the
    // operator redo box to adjust a finished Move/Rotate/Scale.
    void ApplyTransformFromSnapshot(const std::vector<uint64_t>& ids,
                                    const std::vector<Renderer::Transform>& snap,
                                    Renderer::Vec2 pivot,
                                    Renderer::Vec2 aX, Renderer::Vec2 aY,
                                    TransformAxis axis, TransformKind kind,
                                    const std::vector<OperatorParam>& params);
    // Same, for an EDIT-mode (vertex) transform: re-apply to the snapshotted nodes
    // `vsnap` of `vrefs` about `pivot` in the basis, reading the amount from params.
    void ApplyElementTransformFromSnapshot(
                                    const std::vector<Renderer::VertRef>& vrefs,
                                    const std::vector<Renderer::Node>& vsnap,
                                    Renderer::Vec2 pivot,
                                    Renderer::Vec2 aX, Renderer::Vec2 aY,
                                    TransformAxis axis, TransformKind kind,
                                    const std::vector<OperatorParam>& params);

    // Publish ultra-contextual status-bar hints for the CURRENT viewport state
    // (active transform → X/Y/Enter/Esc; edit submode; tool…). Called each frame
    // after input is dispatched. See ShortcutManager::SetTransientHints.
    void PublishStatusHints();

    // Compute the world-space pivot for the current selection under `pivotMode_`.
    Renderer::Vec2 ComputePivot() const;
    // Same, for the edit-mode VERTEX selection.
    Renderer::Vec2 ComputeVertPivot() const;
    // Orthonormal X/Y axes (unit, world doc-units) of the current Transform
    // Orientation for the active selection. Drives the G/R/S X/Y axis constraint.
    // Global/View/Cursor → document axes today; Local → the active object's rotated
    // axes; Parent → its parent's rotated axes. Out params are written; both default
    // to the document axes when there's nothing meaningful to rotate by.
    void ComputeOrientationBasis(Renderer::Vec2& outX, Renderer::Vec2& outY) const {
        ComputeOrientationBasis(outX, outY, transformOrientation_);
    }
    void ComputeOrientationBasis(Renderer::Vec2& outX, Renderer::Vec2& outY,
                                 TransformOrientation orient) const;
    // The selection's transitive closure over object children (parenting): the
    // selected ids plus EVERY descendant. A transform op targets this whole set
    // (transformed about the SAME pivot), so children follow their parent rigidly —
    // the clean, propagation-free way to make "move the parent moves the children".
    std::vector<uint64_t> SelectionWithDescendants() const;
    // Drive the modal transform each frame from the owning leaf (preview + apply
    // + confirm/cancel). Called from HandleViewportTools. ViewportTools.cpp.
    void UpdateTransformOp(EditorState& st,
                           const std::function<Renderer::Vec2(ImVec2)>& s2d,
                           const std::function<ImVec2(Renderer::Vec2)>& d2s,
                           float effZoom, bool hovered, ImDrawList* dl);
    // The Shift+S radial pie menu (cursor/selection snapping). ViewportTools.cpp.
    void RenderViewportPieMenu();

    // Core
    SDL_Window* window_      = nullptr;
    bool        running_     = true;
    // True once ImGui + Vulkan backends are fully initialized. The SDL event
    // watch (live-resize) must NOT render before this, or it dereferences an
    // uninitialized ImGui backend (assert: "Did you call ImGui_ImplSDL3_Init").
    bool        initialized_ = false;
    // Guards against nested RenderFrame() calls from the SDL resize event watch
    // (see Application::RenderFrame). Nested entry would corrupt ImGui state.
    bool        inRenderFrame_ = false;
    float       mainScale_   = 1.0f;

    // Vulkan
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkCommandPool    commandPool_    = VK_NULL_HANDLE;
    uint32_t         queueFamily_    = 0;
    ImGui_ImplVulkanH_Window mainWindowData_;

    // Layout
    float toolbarWidth_ = 64.0f;

    // Last font (family, weight) applied from design-system tokens. Lets
    // ApplyFontTokens() be called every frame yet only rebuild the default
    // font when a token actually changed.
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

    // Recent .acu paths, most-recent first (max kMaxRecentFiles). Persisted via
    // SaveRecentFiles(); shown on the splash start screen.
    std::vector<std::string> recentFiles_;
    static constexpr size_t  kMaxRecentFiles = 10;

    // ── Undo / Redo (snapshot-based; viewport/document history) ───────────────
    // The MAIN window's history: each step is the vector Document SERIALISED to
    // a byte blob (same codec as the .acu DOCUMENT section). Comparing the live
    // document's blob to the last committed one is how a finished change is
    // detected (selection is NOT in the blob, so picking objects never creates
    // a step). Preferences has its OWN history (Blender-style: independent).
    // Default 256 steps, tunable in Preferences ▸ General.
    UndoStack<std::string> undo_;
    std::string            undoLast_;     // last committed document blob
    // History depth (number of undo steps kept), Preferences ▸ General. Default
    // 256. Applied to undo_ (and the Preferences history) via SetCapacity.
    int undoBufferSteps_ = 256;

    // The Preferences window's SEPARATE history: each step is the serialised
    // design-system overrides (Customisation/Theme/Accessibility edits). Undo in
    // Preferences only affects this; undo in the viewport only affects undo_.
    UndoStack<std::string> prefsUndo_;
    std::string            prefsUndoLast_;     // last committed overrides blob
    bool                   prefsUndoInited_ = false;
    // Which window an Undo/Redo action currently targets, set just before each
    // window dispatches its shortcuts (main vs Preferences).
    enum class UndoTarget { Viewport, Preferences };
    UndoTarget             activeUndoTarget_ = UndoTarget::Viewport;
    void InitPrefsUndo();              // wire capture/restore, seed baseline
    void CommitPrefsUndoIfChanged();   // end of the Preferences frame
    std::string CapturePrefsOverrides() const;        // serialise overrides
    void RestorePrefsOverrides(const std::string&);   // deserialise + apply
    // Label applied to the NEXT committed undo step (set by an action just
    // before it mutates the document; reset to "Edit" after each commit).
    std::string pendingUndoLabel_  = "Edit";
    // Set by an action that already logged a RICH Info entry for the change it's
    // about to commit, so CommitUndoIfPending doesn't ALSO log the bare label
    // (avoids a duplicate feed line). Reset after each commit.
    bool        richLoggedPendingCommit_ = false;
    void LogInfoActionRich(const std::string& text, const std::string& detail) {
        LogInfoAction(text, detail);
        richLoggedPendingCommit_ = true;
    }
    void InitUndo();                 // wire capture/restore, seed baseline
    void ResetUndoHistory();         // re-seed on new/open project
    void MarkUndoLabel(const std::string& label) {
        pendingUndoLabel_ = label;
        // Any fresh action dismisses the operator redo panel (Blender). The panel's
        // own re-run is guarded by operatorRerunning_, so it survives a param tweak.
        DismissOperatorPanel();
    }
    bool AnyViewportGestureActive() const;  // true mid drag/transform/edit/crop
    void CommitUndoIfPending();      // called at end of Update()
    std::string CaptureDocBlob() const;             // serialise the document
    void        RestoreDocBlob(const std::string&); // deserialise into the doc
    void Action_Undo();
    void Action_Redo();

    // ── Info log (Blender-style action feed) + debug Dev panel ────────────────
    // Every notable action appends a one-line entry here (move/rotate/scale/
    // create/join/colour/undo/redo/…). Bounded ring (newest at the back). The
    // "Info" editor lists it; LogInfoAction is called from action sites.
    // Whether the 2D cursor is drawn on the canvas (toggle in the Viewport top
    // bar). It always HAS a position (transforms/snaps using it keep working);
    // this only hides the drawing.
    bool show2DCursor_ = true;

    // ── Preview placement ─────────────────────────────────────────────────────
    // When on (Inputs preference in Classic, forced in IOF), choosing an object to
    // add ARMS a placement: the object preview follows the mouse and is dropped on
    // click (right-click / Esc cancels), instead of appearing at the 2D cursor.
    enum class PlacementSource { Core, ModuleSpec, Baked };
    struct Placement {
        bool                armed = false;
        PlacementSource     source = PlacementSource::Core;
        Modules::ObjectSpec spec;                 // ModuleSpec path
        std::string         coreKind;             // Core path ("rectangle"…)
        Renderer::Shape     baked;                // Baked path (full geometry)
        bool                bakedLoose = false;
        uint64_t            bakedColl = 0;
        // Curve-draw symbols (line/area): the baked shape is a STYLE TEMPLATE; the
        // user draws the geometry point-by-point. `drawClosed` = area (filled).
        Modules::ModuleHost::PlaceMode mode = Modules::ModuleHost::PlaceMode::Stamp;
        // Compact preview (short sample / small swatch) for the mini-ghost; empty →
        // fall back to `baked`.
        Renderer::Shape     bakedPreview;
        bool                hasPreview = false;
    };
    Placement placement_;
    bool placementCommitting_ = false;   // re-entry guard: create directly, don't re-arm
    // Tool that was active when a placement was armed; restored when the placement
    // is cancelled (Esc / right-click) so the user returns to Select / 2D Cursor /
    // Line Mark / … exactly where they were.
    std::string placementPrevTool_;
    void EndPlacement();   // cancel arming + restore the previous tool
    // True if new objects should be placed via the cursor-following preview.
    bool PreviewPlacementEnabled();
    // Arm a placement (called by the Add paths instead of creating immediately).
    void RequestPlacementCore(const std::string& kind);
    void RequestPlacementSpec(const Modules::ObjectSpec& spec);
    void RequestPlacementBaked(const Renderer::Shape& shape, bool loose, uint64_t coll,
                               Modules::ModuleHost::PlaceMode mode =
                                   Modules::ModuleHost::PlaceMode::Stamp);
    // Apply a baked symbol's PART STYLE (stroke/fill/decor/marks) to a shape — used
    // when a line/area symbol is drawn point-by-point (the drawn nodes get the
    // symbol's look). `tpl` is the style template's first part.
    void ApplySymbolStyle(Renderer::Shape& target, const Renderer::Shape& tpl, bool closed);
    // A tiny transparent thumbnail of `shape` drawn just below-right of the cursor
    // at `mp` — shows WHICH symbol is being placed while drawing a styled curve.
    void DrawPlacementMiniGhost(const Renderer::Shape& shape, ImVec2 mp, float effZoom);
    // Per-frame: draw the preview + crosshair and commit/cancel. Called from the
    // hovered Viewport with its doc↔screen mappings. Returns true if it consumed
    // the canvas this frame (tools should stand down).
    bool UpdatePlacement(EditorState& st,
                         const std::function<Renderer::Vec2(ImVec2)>& s2d,
                         const std::function<ImVec2(Renderer::Vec2)>& d2s,
                         float effZoom, bool hovered, ImDrawList* dl);
    void ShowCrosshairCursor();          // thin crosshair w/ 1px centre hole

    // One entry in the action feed. `text` is the headline (e.g. "Move"); `detail`
    // is an optional Blender-style parameter dump shown indented under it (e.g.
    // "value=(0.06, -0.07) orient=GLOBAL"). Both are rendered by the Info editor and
    // summarised in the Dev panel.
    struct InfoEntry { uint64_t frame; std::string text; std::string detail; };
    std::vector<InfoEntry> infoLog_;
    void LogInfoAction(const std::string& text);
    // Append a rich action with a structured detail string (key=value pairs). The
    // detail is built by FormatActionDetail from the (key,value) list.
    void LogInfoAction(const std::string& text, const std::string& detail);
    // Build a Blender-ish "key=value, key=value" string from a list of pairs.
    static std::string FormatActionDetail(
        const std::vector<std::pair<std::string, std::string>>& kv);
    void RenderInfoEditor();     // EditorKind::Info — live action feed
    void RenderDevDataEditor();  // EditorKind::DevData — live debug data

    // UI state. Floating windows are unique and non-dockable.
    bool showSettings_      = false;  // new Preferences window
    bool showTokenGraph_    = false;  // Token Graph editor window
    bool showDesignSystem_  = false;  // old Design System / Shortcuts / Icons window
    bool showImGuiDemo_     = false;
    bool showDevWindow_     = false;   // dev test panels (former main area); off by default

    // Title bar + splash state.
    bool   showSplash_ = true;     // Blender-style start screen, shown on launch
    // True for the FIRST frame after showSplash_ flips on, so the "click outside
    // dismisses" test is skipped that frame: otherwise the very click that chose
    // "Show splash screen" in the logo menu (still registering as a press the
    // frame the splash appears) would dismiss it instantly.
    bool   splashJustOpened_ = false;
    bool   showAbout_  = false;    // "About Carto" popup request

    // Pending "New File" intent from the splash. RequestNewFile() either runs
    // DoNewFile immediately (no unsaved changes) or arms the unsaved-changes
    // dialog, remembering which layout preset to apply once resolved.
    bool         unsavedDialogOpen_ = false;  // the modal is up
    LayoutPreset pendingNewPreset_  = LayoutPreset::General;
    // When non-empty, the pending new-file intent targets a MODULE (open it after
    // the reset) rather than a plain preset. Empty = preset path.
    std::string  pendingModuleId_;
    // Set when the unsaved dialog chose "Save" but the project has no path yet:
    // the Save-As dialog is async, so DoNewFile must run only after it resolves.
    bool         newFileAfterSave_  = false;
    // Window op requested by a system button, DEFERRED to the next frame's
    // ProcessEvents. Calling SDL_Minimize/MaximizeWindow mid-frame makes the
    // live-resize event watch re-enter RenderFrame() and trip an ImGui assert.
    enum class WindowOp { None, Minimize, ToggleMaximize, ToggleFullscreen, Close };
    WindowOp pendingWindowOp_ = WindowOp::None;
    bool     maximized_ = false;             // home-grown maximize state
    SDL_Rect restoreRect_ = { 0, 0, 0, 0 };  // window rect before maximizing
    bool     fullscreen_  = false;           // F11 borderless-desktop fullscreen
    // Set while WE move/resize the window (SetMaximized), so the
    // SDL_EVENT_WINDOW_MOVED watch ignores our own moves and only reacts to a
    // user drag of a maximized window (→ restore it, like Windows does).
    bool     programmaticMove_ = false;
    float  titleBarHeightPx_ = 0.0f;  // physical height, published for hit-test
    // Screen rects (physical px) of interactive title-bar widgets the SDL
    // hit-test must treat as NORMAL (not draggable). Rebuilt every frame.
    std::vector<SDL_Rect> titleBarBlockers_;
    // Splash image, decoded once into a Vulkan texture.
    ImTextureID splashTex_    = ImTextureID(0);
    int         splashTexW_   = 0;
    int         splashTexH_   = 0;

    // Camera + view requests live per-leaf in ZoneLayout::EditorState (each
    // Viewport zone has its own). Camera actions target
    // zoneLayout_.HoveredEditorState().

    // The shared project: ONE model read/written by every Viewport zone and
    // the Outliner. Created empty on launch. (File save/open: later.)
    Project project_;

    // The Vulkan-only vector renderer: every Viewport zone's canvas is rendered
    // into an offscreen texture by this, then blitted with ImGui::Image. ImGui
    // never draws the vector document itself. Shares the main Vulkan device /
    // queue / pools; initialized in InitializeSubsystems after VectorGraphics.
    Renderer::CanvasRenderer canvasRenderer_;
    VkSampler                canvasSampler_ = VK_NULL_HANDLE;  // for offscreen textures

    // Transient state of the in-progress drawing gesture (one at a time, app-
    // global). The active tool id itself lives in ToolManager.
    ViewportToolState toolState_;
    // Transient Edit-Mode gesture (vertex/handle drag, element box-select).
    EditDragState     editDrag_;
    // A right-click in Edit Mode opens the Merge/handle context menu; a Shift+S
    // / M pie/menu request is deferred to RenderViewport like the object ones.
    bool              mergeMenuRequest_ = false;
    bool              handleMenuRequest_ = false;   // V: handle-type menu
    ImVec2            editMenuPos_{0, 0};
    // Shift+A "Add" menu (Lot 5): create objects via a Blender-style spawn menu
    // instead of creation tools. Deferred to RenderViewport (the popup must open
    // in the hovered zone's window), opened at addMenuPos_. Spawned objects are
    // placed at the 2D cursor.
    bool              addMenuRequest_ = false;
    ImVec2            addMenuPos_{0, 0};
    EditorState*      addMenuState_ = nullptr;  // viewport leaf the Add menu opened in
    void RenderAddMenu();                 // the "##addMenu" popup body
    // Shift+G "Select Grouped" picker, deferred like the others (opens in the
    // hovered zone's window).
    bool              selectGroupedMenuRequest_ = false;
    ImVec2            selectGroupedMenuPos_{0, 0};
    void RenderSelectGroupedMenu();       // the "##selectGroupedMenu" popup body

    // ── Operator redo panel (Blender's bottom-left F6 box) ─────────────────────
    // After an operator runs it records itself here: a title + a set of adjustable
    // parameters + a `rerun` closure. The panel (bottom-left of the viewport) shows
    // the title and the params; editing a param calls `rerun()` so the operator
    // re-applies with the new value (re-running from the ORIGINAL selection
    // snapshot, so repeated tweaks don't compound).
    // Spawn a new object of `what` at the 2D cursor (Blender Add menu). `what` is
    // one of: "rectangle","ellipse","triangle","hexagon" (meshes) /
    // "bezier","circle" (Bézier curves), "nurbs","nurbs_circle" (NURBS curves).
    // In Edit Mode the new geometry is folded into the active object (like Join).
    void Action_AddShape(const std::string& what);

    // ── Drag-to-page drop PREVIEW (shared across viewports) ───────────────────
    // The viewport that OWNS the move gesture decides which page the active
    // object would land on (dstAb, in ITS layout). It publishes that decision
    // here; EVERY viewport then draws the drop feedback on THAT page at its OWN
    // display position. So all viewports agree on the destination page even
    // though their layouts differ (the target is the owner's, not each one's
    // local hit-test). Refreshed each frame; active=false when no move gesture.
    struct DropPreview {
        bool     active = false;
        uint64_t activeId = 0;   // the moved object
        int      srcAb = -1;     // its current page
        int      dstAb = -1;     // page it would move to (owner's decision)
        bool     keepPage = false;  // Alt held → keep on srcAb (frame src in amber)
        // Rebase offset (doc-units) the OWNER would apply on drop to keep the
        // object visually put: displayA(owner) − displayD(owner). The preview on
        // ANY viewport draws the object at CurPageOrigin(dstAb) + rebase, so it
        // lands exactly where it will after the drop, in that viewport's layout.
        Renderer::Vec2 rebase{0, 0};
    };
    DropPreview dropPreview_;

    // Blender-style Object/Edit mode (document-wide). Tab toggles it over the
    // Viewport. Object Mode manipulates whole shapes; Edit Mode (Lot E) edits
    // their vertices/edges/faces.
    EditorMode editorMode_ = EditorMode::Object;

    // Separate tool memory per mode (Blender keeps a distinct active tool for each
    // interaction mode). `objectModeTool_` is restored when leaving Edit Mode;
    // `editToolByObject_` remembers, per edited object id, the tool it last used in
    // Edit Mode (so re-entering Edit on the same object restores its tool). Both are
    // persisted in the `.acu` so they survive close/reopen.
    std::string objectModeTool_ = "tool.select";
    std::map<uint64_t, std::string> editToolByObject_;

    // Default fill / stroke colours for newly created shapes & curves. Shown as two
    // swatches in the Viewport top bar (filled disc = fill, ring = stroke) and used
    // by MakeShape / Action_AddShape / FinishCurveGesture. Persisted in TAG_VSET.
    Renderer::Color defaultFill_   { 0.80f, 0.80f, 0.82f, 1.0f };
    Renderer::Color defaultStroke_ { 0.10f, 0.10f, 0.12f, 1.0f };
    // Apply the right tool for the current mode + active object after a mode switch.
    // `prevMode` is the mode we are leaving (so its tool is saved first).
    void SyncToolForMode(EditorMode prevMode);
    // The object whose Edit-Mode tool memory applies right now (active, else front).
    uint64_t EditToolObject() const;

    // Modal G/R/S transform in progress, and the pivot point it uses.
    TransformOp transformOp_;
    PivotMode   pivotMode_ = PivotMode::MedianPoint;
    // Modal G/R/S on a SINGLE Bézier handle (the selected HandleRef). Pivots about
    // the handle's own node; the opposite handle follows per the node's HandleMode
    // (Aligned=collinear, Mirrored=equal length, AlignedMirrored=both, Free=none).
    struct HandleOp {
        TransformKind   kind = TransformKind::None;
        const void*     owner = nullptr;
        Renderer::HandleRef ref;
        Renderer::Node  snapshot;            // node state at start (cancel/apply base)
        Renderer::Vec2  startMouse{};        // doc-units at start
        bool Active() const { return kind != TransformKind::None; }
        void Reset() { kind = TransformKind::None; owner = nullptr; ref = {}; }
    } handleOp_;
    void BeginHandleTransform(TransformKind kind);
    // Drive the handle op each frame from the owning leaf (preview + confirm/cancel).
    void UpdateHandleTransform(EditorState& st,
                               const std::function<Renderer::Vec2(ImVec2)>& s2d,
                               const std::function<ImVec2(Renderer::Vec2)>& d2s,
                               float effZoom, bool hovered, ImDrawList* dl);
    // Enforce a node's HandleMode after one of its handles moved (`movedOut` =
    // the OUT handle moved): the opposite handle follows (Aligned=collinear,
    // Mirrored=equal length, AlignedMirrored=both, Free/Vector=none). Shared by the
    // handle drag and the unified element transform of selected handles.
    static void ApplyHandleMode(Renderer::Node& n, bool movedOut);
    // Modal rotation of the 2D CURSOR (R while the 2D Cursor tool is active). Turns
    // doc.cursorRotation; LMB/Enter confirm, Esc/RMB cancel. owner = the leaf that
    // drives it; startAngle/startRot snapshot the reference at press.
    struct CursorRotate {
        bool        active = false;
        const void* owner  = nullptr;
        bool        seeded = false;      // startAngle captured on the first frame
        float       startAngle = 0.0f;   // atan2(mouse−cursor) at start
        float       startRot   = 0.0f;   // doc.cursorRotation at start
        bool        Active() const { return active; }
        void        Reset() { active = false; owner = nullptr; seeded = false; }
    } cursorRotate_;
    // The reference frame G/R/S (and X/Y axis constraints) operate in (Blender's
    // Transform Orientation dropdown). Default Global.
    TransformOrientation transformOrientation_ = TransformOrientation::Global;

    // ── Snapping (Blender's magnet + snap dropdown) ────────────────────────────
    // Snapping is ON when the magnet `enabled` is set OR Ctrl is held during a
    // transform. `mode` picks what to snap to; `base` picks the reference point;
    // `affect*` gate which transforms snap; the rotation increments drive Rotate.
    struct SnapSettings {
        bool enabled = false;                            // magnet (always-on snap)
        enum class Mode  { Increment, Grid, Vertex, Edge, Face, EdgeCenter };
        enum class Base  { Closest, Center, Median, Active };
        Mode  mode = Mode::Increment;
        Base  base = Base::Closest;
        bool  affectMove = true, affectRotate = true, affectScale = true;
        float rotIncrement = 45.0f;                      // Ctrl rotate step (deg)
        float rotPrecisionIncrement = 5.0f;              // Ctrl+Shift rotate step (deg)
    } snap_;
    // The snap group widget (magnet button + dropdown) in the Viewport top bar.
    void DrawSnapWidget(ImVec2 pos, float widthPx);
    // True if snapping should apply to `kind` right now (magnet on, or Ctrl held),
    // honouring the per-transform Affect toggles.
    bool SnapActiveFor(TransformKind kind) const;
    // Result of a snap query: whether a target was found + its world position + a
    // label of what it snapped to (for the indicator). Increment/Grid always snap
    // (geometric grid); Vertex/Edge/Face/EdgeCenter snap to document geometry within
    // a screen-pixel radius (excluding the moving objects themselves).
    struct SnapResult { bool snapped = false; Renderer::Vec2 pos{}; bool showMark = false; };
    // Find the snap target for world point `world` under the current snap mode.
    // `exclude` = shape ids NOT to snap to (the moving objects). `rejectPts` = world
    // points to SKIP as candidates (the moving selection's CURRENT positions — so a
    // vertex can't snap onto its own/the selection's moving geometry, e.g. in edit
    // mode where ComputeSnap can't exclude individual vertices by id). effZoom = px
    // per doc-unit.
    // `rejectSegs` = world SEGMENTS (consecutive pairs a,b,a,b,…) of the moving
    // selection's EDGES — a candidate within ~1px of one is skipped (so Edge/
    // EdgeCenter never snap onto a moving edge; the points alone can't express which
    // pairs are real edges).
    SnapResult ComputeSnap(Renderer::Vec2 world, float effZoom,
                           const std::vector<uint64_t>& exclude,
                           const std::vector<Renderer::Vec2>& rejectPts = {},
                           const std::vector<Renderer::Vec2>& rejectSegs = {}) const;
    // World reference point(s) of the MOVING selection used as the snap source under
    // the current Snap Base: Center=bbox centre, Median=median origin, Active=active
    // element, Closest=every moving outline vertex (the nearest one wins). Reads the
    // active transform op (object ids or vertex refs).
    std::vector<Renderer::Vec2> SnapBaseSources() const;
    // The moving selection's EDGES as world segment pairs (a,b,a,b,…) at their CURRENT
    // positions — edit mode only (object mode excludes the whole shape). An edge is
    // "moving" when BOTH its endpoint nodes are in the vertex selection. Used to keep
    // Edge/EdgeCenter snap off the moving selection.
    std::vector<Renderer::Vec2> MovingSelectionEdges() const;
    // ALL the moving selection's PRE-MOVE world points (every selected vertex; edit
    // mode), independent of the Snap Base — so the snap can reject the WHOLE moving
    // selection as targets (else a non-source moving vertex feeds back → flicker).
    std::vector<Renderer::Vec2> MovingSelectionPoints() const;
    // The live snap indicator (world pos + visible) published during a transform so
    // RenderViewport can draw the orange square. Cleared when no snap is active.
    SnapResult snapIndicator_;
    // Draw the snap indicator glyph (orange, shape per snap mode) at snapIndicator_'s
    // position, if a mark is published. Shared by the transform path and the curve
    // tool. `accentColor` overrides the orange cue (e.g. blue for follow-curve).
    void DrawSnapIndicatorGlyph(const std::function<ImVec2(Renderer::Vec2)>& d2s,
                                ImDrawList* dl, float gs, ImU32 accentColor = 0);
    // A Shift+S pie menu open request (deferred to RenderViewport, like the
    // context menu, so the popup lives in the hovered zone's window).
    bool        pieMenuRequest_ = false;

    // A right-click in the Viewport requests opening the object context menu next
    // frame (OpenPopup must run inside the zone's window, so the request is
    // deferred to RenderViewport). Captured in doc-units for menu actions.
    bool           viewportMenuRequest_ = false;
    ImVec2         viewportMenuPos_{0, 0};   // screen pos where the menu opens
    // Set true the frame a modal transform consumes the RMB to cancel itself, so
    // the same click does not also open the context menu.
    bool           rmbConsumedByTransform_ = false;

    // ── Page (artboard) context menu + thumbnail framing ──────────────────────
    // Right-clicking a page's name label opens "##pageCtx" for that artboard:
    // Define Thumbnail ▸ (Whole Page / Zone) and Resize Page. The target index
    // is captured when the menu opens (the menu lives in the hovered zone).
    bool   pageCtxRequest_   = false;   // open the page menu next frame
    int    pageCtxArtboard_  = -1;      // which artboard the menu acts on
    ImVec2 pageCtxPos_{0, 0};           // screen pos where it opens
    // "Resize Page" reuses the New-Document popup, pre-filled with the page's
    // current size; on Create it resizes this artboard instead of adding one.
    bool   resizePageRequest_ = false;  // open the resize popup next frame
    int    resizePageArtboard_ = -1;
    // "Rename Page" popup: a small InputText seeded with the page's current name.
    bool   renamePageRequest_  = false;
    int    renamePageArtboard_ = -1;
    char   renamePageBuf_[128] = {0};
    // Interactive "Zone" thumbnail crop: drag a rectangle over the page, adjust,
    // press Enter to render the thumbnail from it (Esc/RMB cancels). Active only
    // while cropArtboard_ >= 0. Rect is in doc-units (artboard-relative absolute).
    int            cropArtboard_ = -1;  // -1 = not cropping
    Renderer::Vec2 cropMin_{0, 0};      // doc-units, current rectangle
    Renderer::Vec2 cropMax_{0, 0};
    // Drag anchor (grab model, no teleport): which part is being dragged, the
    // doc-units cursor position when the drag began, and the rect at that moment.
    // cropDrag_: -1 none, -2 move body, -3 defining, 0..7 = handle index.
    int            cropDrag_ = -1;
    Renderer::Vec2 cropDragRef_{0, 0};  // cursor pos (doc) at mouse-down
    Renderer::Vec2 cropRect0Min_{0, 0}; // rect at mouse-down
    Renderer::Vec2 cropRect0Max_{0, 0};
    // For a resize, which REAL component the held edge maps to, captured at grab
    // time so it stays correct even if the rect inverts mid-drag (the held edge
    // keeps following the cursor and crosses the other side). -1 = min comp,
    // +1 = max comp, 0 = this axis is not driven by the held handle.
    int            cropEdgeX_ = 0;
    int            cropEdgeY_ = 0;
    // The zone (leaf) that owns the active crop, so only that Viewport handles it
    // (multi-viewport safe). Set by RenderViewport before calling the handler.
    void*          cropOwner_ = nullptr;

    // ── Free page move: drag a page's NAME LABEL to move the whole page (and,
    // since geometry is page-relative, all its objects). −1 = not dragging. The
    // anchor is the page's pos and the cursor's doc-pos at grab time. Owned by a
    // single leaf (multi-viewport safe), like the crop.
    int            pageDrag_ = -1;
    Renderer::Vec2 pageDragPos0_{0, 0};   // page pos at grab
    Renderer::Vec2 pageDragRef_{0, 0};    // cursor doc-pos at grab
    void*          pageDragOwner_ = nullptr;

    // ── Per-viewport page layout (Lot 3) ──────────────────────────────────────
    // RenderViewport fills these for the leaf it is drawing, so the shared
    // picking/transform/chrome helpers use the page DISPLAY origins of the
    // current viewport (auto layouts relocate/hide pages per viewport without
    // touching the shared Artboard::pos). Reset to identity outside a viewport.
    std::vector<PageView> curPageViews_;
    // Page display origin / visibility for the viewport being drawn. Falls back
    // to ab.pos / visible when no layout is active or the index is unknown.
    Renderer::Vec2 CurPageOrigin(int abIndex) const;
    Renderer::Vec2 CurPageOriginOfShape(uint64_t shapeId) const;
    bool           CurPageVisible(int abIndex) const;

    // Blender-style dynamic zone tree (no native docking UX).
    ZoneLayout zoneLayout_;

    // UI components
    DesignSystem::TokenEditor        tokenEditor_;
    UI::ShortcutEditor               shortcutEditor_;
    VectorGraphics::IconEditorWindow iconEditor_;
    UI::SettingsWindow               settingsWindow_;
    // The Preferences UI lives in its OWN OS window (separate SDL+Vulkan
    // window + 2nd ImGui context); settingsWindow_ draws its content there.
    // It is one SecondaryWindow among potentially several (future detached
    // editors / dev console / render window) — see secondaryWindows_.
    SecondaryWindow                  settingsHost_;

    // The Token Graph editor: a Geometry-Nodes-style view of every design token,
    // in its own detached OS window (a second SecondaryWindow). Gated by
    // showTokenGraph_.
    UI::TokenGraphWindow             tokenGraphWindow_;
    SecondaryWindow                  tokenGraphHost_;

    // Every detached OS window the app drives. Each shares the main Vulkan
    // device + the main ImGui style (settings stay in sync automatically; see
    // SecondaryWindow). ProcessEvents routes events to them and RenderFrame
    // renders them after the main frame — so adding a window is just: construct
    // it, Init() with a Config + content lambda, and register it here.
    std::vector<SecondaryWindow*>    secondaryWindows_;

    // Singleton-self for non-static actions (callbacks captured by lambda).
    static Application* s_instance_;
};

} // namespace App
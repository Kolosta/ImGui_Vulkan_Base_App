#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui_impl_vulkan.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <UI/Tokens/TokenEditor.h>
#include <UI/Shortcuts/ShortcutEditor.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Settings/SettingsWindow.h>
#include <UI/Tokens/TokenGraphWindow.h>
#include <VectorGraphics/editors/IconEditorWindow.h>
#include <Ink/Render/Renderer.h>   // the 2D vector engine (docs/Ink/)
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
    void Action_ApplyScale();
    void Action_BeginMove();
    void Action_BeginRotate();
    void Action_BeginScale();
    void Action_ConstrainAxisX();
    void Action_ConstrainAxisY();
    void Action_OpenAddMenu();
    // Create a shape at the 2D cursor / view centre and select it.
    Ink::NodeId SpawnShape(const char* kind);
    // Build the default Style (fill+stroke) from the EditContext swatches.
    Ink::Style DefaultStyle() const;
    // Push an already-applied reversible command onto the doc undo stack.
    void PushDocCommand(const std::string& label,
                        std::function<void(Ink::Document&)> undo,
                        std::function<void(Ink::Document&)> redo);
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
    // One Layers-view row (recursive): a node + its subtree. Returns the row
    // rect height consumed (for range logic). `depth` drives indentation.
    void OutlinerLayersRow(EditorState& st, Ink::NodeId id, int depth);
    void OutlinerCollectionsView(EditorState& st);
    // Right-click context menu for the Outliner (opened over a row or empty).
    void RenderOutlinerContextMenu(EditorState& st);
    // Organisation commands (undoable), shared by the Outliner + shortcuts.
    void Action_GroupSelection();
    void Action_UngroupSelection();
    void Action_ToggleNodeVisible(Ink::NodeId id);
    void Action_RenameNode(Ink::NodeId id, const std::string& name);
    void Action_NewCollectionFromSelection();

    // Active object's style/transform editor (multi-fill / multi-stroke) — Lot 9.
    void RenderProperties();
    // Property sub-sections (Properties.cpp).
    void PropTransformSection(Ink::NodeId id);
    void PropFillsSection(Ink::NodeId id);
    void PropStrokesSection(Ink::NodeId id);
    // Commit a whole-style edit as one undoable command (captures before/after).
    void CommitStyleEdit(Ink::NodeId id, const Ink::Style& before,
                         const std::string& label);
    // The Outliner context-menu request (opened next frame at this position).
    bool   outlinerCtxOpen_ = false;
    ImVec2 outlinerCtxPos_{};
    Ink::NodeId outlinerCtxNode_ = Ink::kNullNode;
    // Selection anchor for Shift-range clicks in the Outliner (last plain click).
    Ink::NodeId outlinerRangeAnchor_ = Ink::kNullNode;
    // Live property editing: the style captured when a drag-edit began, so the
    // whole drag folds into ONE undo command committed on release.
    Ink::Style      propEditBefore_;
    Ink::Transform2D transformBeforeScratch_;   // transform drag before-state
    Ink::NodeId     propEditNode_ = Ink::kNullNode;
    bool            propEditActive_ = false;

    // ── Info log (Blender-style action feed) + Dev data editor ───────────────
    struct InfoEntry { uint64_t frame; std::string text; std::string detail; };
    std::vector<InfoEntry> infoLog_;
    void LogInfoAction(const std::string& text);
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
    // Fresh document into project_ + hand it to the Ink engine. Transitional:
    // also seeds the demo content (until the drawing tools land, Lot 8).
    void ResetDocument();

    // ── Modules ──────────────────────────────────────────────────────────────
    void RegisterModules();
    void RequestOpenModule(const std::string& moduleId);
    void DoOpenModule(const std::string& moduleId);
    // nullptr = Classic mode. rebuildLayout=false keeps the current zone tree.
    void ActivateModule(Modules::IModule* mod, bool rebuildLayout = true);
    // Modules::ModuleHost — the app services a module may drive. The document
    // services return with Ink (ROADMAP Lot 11).
    void MarkDirty() override;
    // Commit the pending new-file/open-module intent (preset vs module).
    void CommitPendingNew();
    Modules::IModule*     activeModule_ = nullptr;     // nullptr = Classic
    Modules::Capabilities activeCapabilities_{};       // gates core features

    // Apply a pending open/save resolved from the (async) file dialog. Called
    // from ProcessEvents, OUTSIDE the ImGui frame. (Currently: logs only.)
    void ProcessPendingFileOp();

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
    // Set when the unsaved dialog chose "Save" but the project has no path yet.
    bool         newFileAfterSave_  = false;
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
    // Add menu (Shift+A) request: opened at the cursor in the hovered zone.
    bool         addMenuOpen_ = false;
    ImVec2       addMenuPos_{};
    // Convenience: the currently hovered viewport leaf's EditorState (set each
    // frame by RenderViewport when hovered), used by mode-less shortcut actions.
    EditorState* hoveredViewport_ = nullptr;
    // The hovered leaf's camera this frame — lets G/R/S (fired by keyboard,
    // outside RenderViewport) map the mouse to document space.
    ViewCam      hoveredCam_{};

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

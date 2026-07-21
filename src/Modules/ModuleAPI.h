#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Carto Module API — the stable contract a module implements.
//
//  A MODULE is a specialisation of the generic vector editor for a use case
//  (Typography, IOF Mapping…). It can REUSE core features (by referencing core
//  editor ids in its layout), ADD its own editors / Shift+A objects / side-panel
//  tabs, and LIMIT core features via capability flags. The app always boots in
//  the implicit "Classic" mode (no active module); opening a module from the
//  splash creates a fresh project and applies the module's layout + config.
//
//  This header is the ONLY thing an external plugin needs to include. Keep it
//  dependency-light: it references the editor registry, the layout spec, and a
//  couple of UI value types — no Application internals. Internal modules live in
//  src/Modules/<Name>/ and are registered in ModuleRegistry::RegisterInternal();
//  external modules will (later) be shared libraries exporting CreateModule().
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "EditorRegistry.h"   // EditorRegistry / EditorDescriptor (add editors)
#include "ZoneLayout.h"       // LayoutSpec (declarative zone tree)

namespace Shortcuts { class ShortcutManager; }
namespace UI { struct MenuEntry; struct SidePanelTab; }
namespace Ink { class Document; struct Style; }

namespace App::Modules {

// A "place symbol" arming (point symbols): the viewport shows `iconNode`'s
// vignette riding the cursor; a canvas click calls `onPlace` with the document
// position. `repeat` keeps the tool armed after each click (Esc / right-click
// disarms). `iconNode` is a document node id (usually a preview-only library
// specimen) rendered through NodePreviewTexture.
struct PlacementRequest {
    std::uint64_t iconNode = 0;
    bool          repeat   = true;
    std::function<void(double docX, double docY)> onPlace;
};

// A "draw symbol" arming (line / area symbols): the core pen runs with the
// symbol's STYLE as its default (live previews included) and `iconNode`'s
// vignette rides the cursor; when the drawn path commits, `onCommit` receives
// the new node so the module can route it (print-layer group, collections,
// property locks) BEFORE the undo snapshot is taken.
struct SymbolDrawRequest {
    std::string  penKind = "curve";   // "curve" (line) / "free" (closed area)
    const Ink::Style* style = nullptr; // borrowed for the call; copied by core
    std::uint64_t iconNode = 0;
    std::function<void(std::uint64_t placedNode)> onCommit;
};

// The slice of app services a module is allowed to drive (implemented by the
// Application). Kept deliberately small and dependency-light so the contract
// stays plugin-friendly.
//
// Document services (docs/Ink/ROADMAP.md Lot 11): the module reaches the OPEN
// Ink document and mutates it ONLY through the document's typed operations
// (docs/Ink/DOCUMENT_MODEL.md) — every change flows through the ChangeLog so
// the engine recompiles exactly what moved. Pair each user-visible edit with
// PushDocCommand so it lands on the shared undo stack like any core edit.
class ModuleHost {
public:
    virtual ~ModuleHost() = default;
    // Mark the project as having unsaved changes.
    virtual void MarkDirty() = 0;
    // The open document (nullptr before initialisation). Owned by the app —
    // never cache the pointer across frames (New/Open recreate it).
    virtual Ink::Document* Document() = 0;
    // Push one undoable command onto the shared document undo stack. `undo` /
    // `redo` re-apply the edit through the typed ops (capture VALUES, never
    // node pointers). Marks the project dirty.
    virtual void PushDocCommand(const std::string& label,
                                std::function<void(Ink::Document&)> undo,
                                std::function<void(Ink::Document&)> redo) = 0;
    // Blender-style action feed entry (the Info editor).
    virtual void LogInfoAction(const std::string& text) = 0;
    // Real-pipeline vignette of a node's subtree (preview filter + off-screen
    // Ink view): the returned handle is an ImTextureID. `px` is the square
    // render size; `padFrac` the fitted margin. 0 while unavailable. The view
    // is cached per (node, px) and re-renders with the document.
    virtual std::uint64_t NodePreviewTexture(std::uint64_t node, int px,
                                             float padFrac = 0.12f) = 0;
    // Same isolation render with an EXPLICIT camera (screen_px = (doc − pan)
    // · zoom) into a `w`×`h` view — a module-driven zoom/pan preview canvas
    // (the IOF Symbol Viewer). `viewKey` keeps distinct canvases distinct.
    virtual std::uint64_t CanvasPreviewTexture(std::uint64_t node,
                                               std::uint32_t viewKey,
                                               int w, int h, double panX,
                                               double panY, double zoom) = 0;
    // Document-space bounds of a node's rendered subtree (union), for module
    // fit-view logic. out = { minX, minY, maxX, maxY }. False when empty.
    virtual bool NodeDocBounds(std::uint64_t node, double out[4]) = 0;
    // Arm / cancel the "place symbol" viewport tool (point symbols).
    virtual void ArmPlacement(const PlacementRequest& req) = 0;
    virtual void CancelPlacement() = 0;
    // Begin / end symbol drawing (line & area symbols on the core pen).
    virtual void BeginSymbolDraw(const SymbolDrawRequest& req) = 0;
    virtual void EndSymbolDraw() = 0;
    // State of the two symbol tool modes (so a module can keep a tool "hot" —
    // re-arm after a placement/draw lapsed).
    virtual bool IsPlacementArmed() const = 0;
    virtual bool IsSymbolDrawing()  const = 0;
    // Make `toolId` the active Viewport tool (validated per editor mode, with
    // per-mode memory) — same path as the core palette / keymap.
    virtual void ActivateTool(const std::string& toolId) = 0;
    // The ToolManager's active tool id (so a module reflects selection state).
    virtual std::string ActiveTool() const = 0;
    // Register a Viewport tool so it can be made active + validated per mode
    // (id / name / icon). A module calls this in OnRegister for its tools.
    virtual void RegisterTool(const std::string& id, const std::string& name,
                              const std::string& icon) = 0;
};

// ABI version of this contract. Bumped on any breaking change to IModule /
// the structs below. An external plugin reports the version it was built against
// (ModuleAbiVersion()); the loader refuses a mismatch.
//   v2 — Ink document services on ModuleHost + IModule::OnDocumentCreated
//        (docs/Ink/ROADMAP.md Lot 11).
//   v3 — symbol tooling: NodePreviewTexture, ArmPlacement, BeginSymbolDraw
//        (place / draw symbol modes with cursor vignettes) + wired hooks.
inline constexpr int kModuleAbiVersion = 3;

// Identity + presentation of a module (shown on the splash "Modules" column).
struct ModuleInfo {
    std::string id;            // unique, e.g. "typography", "iof-mapping"
    std::string name;          // display name, e.g. "Typography"
    std::string description;   // one-line summary
    std::string icon;          // icon id (resources/icons), may be empty
    std::string version = "0.1.0";
};

// Which core features the active module enables. The core reads these flags and
// turns features on/off accordingly. Defaults = the full Classic editor.
struct Capabilities {
    bool corePrimitivesAddMenu = true;  // Shift+A offers the core primitives/curves
    bool pages                 = true;  // multi-page document (vs single page / infinite)
    bool editMode              = true;  // Tab → vertex/edge/face edit mode
    bool curveTool             = true;  // the Edit-Mode "Curve" tool (free-draw new curves)
    bool previewPlacement      = false; // force the cursor-following placement preview
    bool lockTransformsForced  = false; // scale/rotation locks are module-managed (UI read-only)
    // Lock the Outliner TREE structure: no drag/drop reordering or re-parenting of
    // objects, collections or pages. The module owns the hierarchy (IOF: symbols
    // must stay in their fixed print-layer collections under the page). Selection,
    // rename, visibility still work — only structural drags are disabled.
    bool lockOutlinerTree      = false;
    // DOCUMENT unit system applied when the module opens a fresh project
    // (UI::Units::UnitSystem: 0=Metric(mm), 1=Imperial, 2=Typographic(pt),
    // 3=Pixel). -1 = keep the core default (px). Persisted with the document;
    // a loaded file's own saved unit always wins. IOF = Metric (ISOM symbols
    // are mm-defined), Typography = Typographic.
    int  documentUnit          = -1;
    // The document COLOUR MODE the module pins: -1 = leave the user choice
    // (core default RGB), 0 = force RGB, 1 = force CMYK (the Document
    // properties switch renders read-only while a module pins it).
    int  colorMode             = -1;
    // mm → doc-unit factor the module builds its symbols at (1.0 at 1:15 000 for
    // IOF). The core uses it so manually-placed annotations (line marks) land at
    // the spec mm sizes for the current map scale. 0 = not applicable (1.0).
    float symbolScale          = 0.0f;
};

// What a module receives when it registers (once, at startup): the registries it
// extends. Editors added here appear in every zone's editor picker.
struct ModuleContext {
    EditorRegistry&             editors;
    Shortcuts::ShortcutManager& shortcuts;
};

// The module interface. A module overrides only what it needs; every hook has a
// no-op default so a minimal module is just Info() + BuildLayout().
class IModule {
public:
    virtual ~IModule() = default;

    // Identity / presentation.
    virtual ModuleInfo Info() const = 0;

    // Register editors / shortcuts (called ONCE when the module is discovered).
    virtual void OnRegister(ModuleContext&) {}

    // The zone arrangement applied when the module is opened (editor ids).
    virtual LayoutSpec BuildLayout() const = 0;

    // Editor ids selectable in this module's zone picker — its OWN editors plus
    // any CORE editors it reuses (e.g. "core.viewport"). The picker is restricted
    // to this set, so a module is a focused workspace (no Timeline in Typography,
    // etc.). Empty = no restriction (every registered editor — discouraged).
    virtual std::vector<std::string> AllowedEditors() const { return {}; }

    // Tune which core features are available while this module is active.
    virtual void ConfigureCapabilities(Capabilities&) const {}

    // Size (doc px) of the default page created when the module is opened.
    // {0,0} = keep the core default (1920×1080). e.g. IOF → A4 landscape.
    virtual std::pair<float, float> DefaultPageSize() const { return {0.0f, 0.0f}; }

    // Seed the FRESH document created when the module is opened (Lot 11):
    // called once, after the blank page (sized per DefaultPageSize) exists and
    // before the module layout applies — build guides, base collections,
    // starter symbols… through the document's typed ops. NOT called when a
    // saved .acu opens (its content is authoritative). Host() is bound.
    virtual void OnDocumentCreated(Ink::Document&) {}

    // Replace the Viewport Shift+A "Add" menu. Return true to OVERRIDE the core
    // menu with `out`; false to keep the core menu (subject to capabilities).
    virtual bool BuildAddMenu(std::vector<UI::MenuEntry>& /*out*/) { return false; }

    // Extra tabs appended to the Viewport's right-side ("N") panel.
    virtual void ViewportSidePanelTabs(std::vector<UI::SidePanelTab>& /*out*/) {}

    // Tool ids this module ADDS to the Viewport's OBJECT mode (appended to the
    // core Select/Cursor/Shape/Curve). They live only in Object mode (hidden in
    // Edit / Line-Mark), and the module renders them itself via DrawToolButtons
    // (vignettes) rather than the core icon palette. Registered in OnRegister.
    virtual void ObjectTools(std::vector<std::string>& /*out*/) {}

    // Draw MODULE tool buttons in the Viewport tool palette, BELOW the core
    // tools (IOF: the six ISOM theme buttons — each a symbol vignette, a
    // right-click picks the theme's symbol from a grid menu). `origin` = the
    // strip's top-left, just under the core palette; `size` = one button side
    // (px, already scaled). The module renders vignettes through
    // Host()->NodePreviewTexture, drives its own clicks (ArmPlacement /
    // BeginSymbolDraw) and popups, and PUSHES each button's screen rect into
    // `outRects` so the canvas excludes the strip from its hit-testing.
    virtual void DrawToolButtons(ImVec2 /*origin*/, float /*size*/,
                                 std::vector<ImVec4>& /*outRects*/) {}

    // Draw a per-frame overlay on the Viewport canvas. `docToScreen` maps a
    // document-space point (raw doc units) to screen px for THIS viewport (its
    // pan/zoom). Used e.g. by IOF Course Settings to draw the course line over the
    // control objects at their real positions. Optional.
    virtual void DrawViewportOverlay(ImVec2 /*canvasMin*/, ImVec2 /*canvasMax*/,
                                     const std::function<ImVec2(ImVec2)>& /*docToScreen*/) {}

    // Veto an Outliner drag/drop reparent. Return false to forbid moving `shapeId`
    // into `targetCollectionId`. Default allows everything; IOF uses it to keep a
    // symbol inside its print-layer collection (sub-collections of the same layer
    // are still allowed). Consulted by the Outliner before it re-collections.
    virtual bool AllowReparent(uint64_t /*shapeId*/, uint64_t /*targetCollectionId*/) {
        return true;
    }

    // A Shift-held NUMPAD SEQUENCE finished (Shift released): `digits` are the
    // numpad digits typed in order while Shift was down. IOF: a single 1-6
    // activates the matching theme tool; two+ digits are the ISOM code (108 →
    // Small erosion gully). Optional.
    virtual void OnNumpadSequence(const std::string& /*digits*/) {}

    // Called once per frame BEFORE the UI is built, while this module is active.
    // A module uses it to keep document invariants it owns (e.g. IOF keeps each
    // page's shapes in print-layer z-order). Reach services via Host(). Optional.
    virtual void OnFrameSync() {}

    // Lifecycle: when the module becomes / stops being the active one.
    virtual void OnActivate() {}
    virtual void OnDeactivate() {}

    // The app binds the host before OnActivate(); module hooks (BuildAddMenu
    // onClick, overlays…) use Host() to reach the document and core operations.
    void BindHost(ModuleHost* h) { host_ = h; }
protected:
    ModuleHost* Host() const { return host_; }
private:
    ModuleHost* host_ = nullptr;
};

}  // namespace App::Modules

// ── External plugin factory (C ABI) ──────────────────────────────────────────
// A future external module is a shared library exporting these two symbols. The
// loader checks CartoModuleAbiVersion() == kModuleAbiVersion, then takes
// ownership of the IModule* from CartoCreateModule(). Internal modules don't use
// these — they are constructed directly in ModuleRegistry. The macro stamps both
// symbols for a given concrete module type.
extern "C" {
typedef App::Modules::IModule* (*CartoCreateModuleFn)();
typedef int (*CartoModuleAbiVersionFn)();
}
#define CARTO_MODULE_EXPORT(ModuleType)                                        \
    extern "C" App::Modules::IModule* CartoCreateModule() {                    \
        return new ModuleType();                                               \
    }                                                                          \
    extern "C" int CartoModuleAbiVersion() {                                   \
        return App::Modules::kModuleAbiVersion;                                \
    }

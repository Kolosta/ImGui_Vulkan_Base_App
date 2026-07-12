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
namespace Ink { class Document; }

namespace App::Modules {

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
};

// ABI version of this contract. Bumped on any breaking change to IModule /
// the structs below. An external plugin reports the version it was built against
// (ModuleAbiVersion()); the loader refuses a mismatch.
//   v2 — Ink document services on ModuleHost + IModule::OnDocumentCreated
//        (docs/Ink/ROADMAP.md Lot 11).
inline constexpr int kModuleAbiVersion = 2;

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
    // Default document unit index the Viewport uses while this module is active
    // (matches the core unit table: 0=px, 1=pt, 2=mm, 3=cm, 4=in). -1 = leave the
    // core default (px). IOF works in millimetres (ISOM symbols are mm-defined).
    int  documentUnit          = -1;
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

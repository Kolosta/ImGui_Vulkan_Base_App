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
namespace Renderer { class Document; struct Shape; struct Vec2; }

namespace App::Modules {

// A simple object a module can place into the document (an ISOM symbol, a course
// control…). The app turns this into a real Shape at the 2D cursor, so it is then
// selectable / movable / deletable like any object. RGBA in [0,1].
struct ObjectSpec {
    enum class Geom { Point, Line, Area } geom = Geom::Point;
    std::string name;                 // object name (e.g. "101 Contour", "Control 31")
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;   // primary colour
    float size  = 30.0f;              // doc-units extent
    bool  loose = false;              // place page-less (overprint layer) vs on the page
    bool  lockScale    = false;       // fixed-size symbol → Scale (S) disabled
    bool  lockRotation = false;       // north-oriented symbol → Rotate (R) disabled
    uint64_t collectionId = 0;        // target collection (0 = default); IOF print layer
};

// The slice of app services a module is allowed to drive (implemented by the
// Application). Kept deliberately small and dependency-light so the contract
// stays plugin-friendly: a module reaches the document and a few high-level
// operations through here rather than touching Application internals.
class ModuleHost {
public:
    virtual ~ModuleHost() = default;
    virtual Renderer::Document& Document() = 0;
    // Create a default object of `presetKind` ("rectangle"/"ellipse"/…) and name
    // it `name` — reuses the core object-creation path (Action_AddShape).
    virtual void CreateObject(const std::string& presetKind,
                              const std::string& name) = 0;
    // Create an object from a spec (geometry + colour) at the 2D cursor; returns
    // the new shape's id (0 on failure). Used for simple module objects.
    virtual uint64_t CreateObjectSpec(const ObjectSpec& spec) = 0;
    // How a baked symbol is placed:
    //   Stamp     → the glyph follows the cursor and drops a copy on click (points,
    //               and any fixed-size symbol).
    //   DrawLine  → the symbol's line STYLE is applied to a curve the user draws
    //               point-by-point (open path); used for line symbols.
    //   DrawArea  → same, but the drawn curve is closed + filled (area symbols).
    enum class PlaceMode { Stamp = 0, DrawLine = 1, DrawArea = 2 };
    // Add a fully-baked shape (geometry already authored in doc units, centred at
    // the local origin). For Stamp it lands at the 2D cursor honouring preview
    // placement (follows the cursor, drops on click). For DrawLine/DrawArea the
    // shape is used as a STYLE TEMPLATE and the user draws the geometry. `loose` =
    // page-less; `collectionId` = target Outliner collection (0 = none).
    virtual uint64_t AddBakedShape(const Renderer::Shape& shape,
                                   bool loose, uint64_t collectionId,
                                   PlaceMode mode = PlaceMode::Stamp) = 0;
    // Optional compact preview of the symbol (short line sample / small swatch) for
    // the placement mini-ghost; if not set the full shape is used. Called right
    // after AddBakedShape when arming a DrawLine/DrawArea placement.
    virtual void SetPlacementPreview(const Renderer::Shape& /*preview*/) {}
    // The isomCode of the symbol currently ARMED for placement (the baked ghost
    // following the cursor / being drawn), or 0 if none. Lets the catalogue UI
    // highlight the active symbol and clear it when the user cancels. Default 0.
    virtual int ArmedSymbolCode() const { return 0; }
    virtual void MarkDirty() = 0;

    // Render a set of shapes (a symbol + optional companions) into a CACHED,
    // SSAA-smoothed offscreen texture (the same Vulkan pipeline the viewport uses),
    // auto-framed with `padFrac` margin. `key` is caller-stable (e.g. a hash of
    // symbol code+scale+size); `contentHash` triggers a rebuild only when the
    // geometry changes. Returns an ImTextureID to blit with ImGui::Image (0 on
    // failure). Lets the Symbol Viewer / placement ghost look as smooth as the
    // viewport instead of hard CPU-blitted triangles. Default = unsupported (0).
    virtual ImTextureID RenderGlyphTexture(uint64_t /*key*/, uint64_t /*contentHash*/,
                                           const std::vector<Renderer::Shape>& /*shapes*/,
                                           int /*widthPx*/, int /*heightPx*/,
                                           float /*padFrac*/,
                                           bool /*transparent*/ = false,
                                           bool /*exactFit*/ = false,
                                           const Renderer::Vec2* /*frameMin*/ = nullptr,
                                           const Renderer::Vec2* /*frameMax*/ = nullptr) { return ImTextureID(0); }
};

// ABI version of this contract. Bumped on any breaking change to IModule /
// the structs below. An external plugin reports the version it was built against
// (ModuleAbiVersion()); the loader refuses a mismatch.
inline constexpr int kModuleAbiVersion = 1;

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

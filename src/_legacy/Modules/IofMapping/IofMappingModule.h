#pragma once

#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

#include "ModuleAPI.h"
#include "IofSpec.h"   // PrintLayer, IofElement (print-layer collections)

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  IOF Mapping module — orienteering map authoring (ISOM 2017-2), first pass.
//
//  Reuses the core Viewport / Outliner / Properties editors (by id) and adds:
//   • a specialised Shift+A "Add" menu = the ISOM catalogue grouped by type,
//     plus a Course-planning group (Control / Start / Finish) — no core
//     primitives;
//   • a Viewport "Map elements" side-panel tab (preview + description + place);
//   • a Map Settings editor (scale, contour interval, grid);
//   • a Course Settings editor: a tab per course (+ "All controls"), each with
//     an ordered control card you build by joining placed control OBJECTS, an
//     editable control number, and a live schematic — and the active course's
//     line is drawn over the map on the Viewport.
//
//  Controls / Start / Finish are REAL document objects (created via Shift+A), so
//  they are selectable / movable / deletable like any object. A course is an
//  ordered list of those objects' ids. Map data + courses live in memory for now.
// ─────────────────────────────────────────────────────────────────────────────

// A course = an ordered list of control object ids (into the document) + a name.
struct IofCourse {
    std::string           name;
    std::vector<uint64_t> controls;   // ordered shape ids (placed control objects)
};

class IofMappingModule final : public IModule {
public:
    ModuleInfo Info() const override;
    void       OnRegister(ModuleContext& ctx) override;
    LayoutSpec BuildLayout() const override;
    void       ConfigureCapabilities(Capabilities& caps) const override;
    std::pair<float, float> DefaultPageSize() const override { return {297.0f, 210.0f}; }  // A4 landscape (mm)
    std::vector<std::string> AllowedEditors() const override;
    bool       BuildAddMenu(std::vector<UI::MenuEntry>& out) override;
    void       ViewportSidePanelTabs(std::vector<UI::SidePanelTab>& out) override;
    void       DrawViewportOverlay(ImVec2 canvasMin, ImVec2 canvasMax,
                                   const std::function<ImVec2(ImVec2)>& docToScreen) override;
    bool       AllowReparent(uint64_t shapeId, uint64_t targetCollectionId) override;
    void       OnFrameSync() override;
    void       OnActivate() override;

private:
    // Ensure the print-layer collections exist, in print order, UNDER the page,
    // and cache their ids by layer. Re-runs cheaply (reuses existing collections).
    void EnsureLayerCollections();
    // Collection id for an exact print layer (0 if not created yet).
    uint64_t LayerCollection(PrintLayer layer) const;
    // The print-layer collection a symbol belongs to (0 if none / unknown).
    uint64_t CollectionForSymbol(const IofElement& e) const;
    // The page the map is authored on (the module's single default page). 0 if
    // none yet. Symbols + their layer collections all live under it (no orphans).
    uint64_t MapPageId() const;
    // Render RANK of a shape = its print-layer position in the DRAW stack (lower =
    // drawn first = underneath). Derived from the symbol's isomCode → print layer;
    // bottom-of-print-stack layers (yellow) get the lowest rank. Unknown → top.
    int      PrintRankOf(const Renderer::Shape& s) const;
    // Re-sort the map page's shapes into print-layer z-order (stable within a
    // layer). Cheap-guarded by a content signature so it only runs on a change.
    void     SyncPrintOrder();
    // Editor bodies (registered as descriptors in OnRegister, capturing `this`).
    void DrawMapSettings();
    void DrawCourseSettings();
    void DrawMapElementsTab(ImVec2 contentMin, ImVec2 contentMax);
    void DrawSymbolViewer(ImVec2 size);
    // Blit a symbol's glyph into rect [mn,mx] as an SSAA Vulkan texture (smooth),
    // falling back to a CPU triangle blit if the host can't render textures.
    // `keySalt` distinguishes different sizes/contexts of the same symbol.
    void DrawGlyphImage(ImDrawList* dl, const IofElement& e, float scale,
                        ImVec2 mn, ImVec2 mx, uint64_t keySalt, float padFrac = 0.18f);
    // Same, but the blitted image is ROUNDED (corner radius `rounding`) so the
    // thumbnail matches the cell's rounded outline.
    void DrawGlyphImageRounded(ImDrawList* dl, const IofElement& e, float scale,
                               ImVec2 mn, ImVec2 mx, uint64_t keySalt, float padFrac,
                               float rounding);

    // Bake a symbol's exact ISOM glyph and place it at the cursor (via the host).
    void PlaceSymbol(const IofElement& e, const std::string& nameOverride);

    // Place a course object (control auto-numbered / start / finish) at the cursor.
    void AddControlObject();
    void AddStartObject();
    void AddFinishObject();
    int  NextControlNumber() const;   // max existing control number + 1

    // Symbol-size factor for the current map scale, relative to the ISOM base
    // 1:15 000 (where symbol dimensions are given in mm). 1:10 000 → 1.5, etc.
    float MapScaleFactor() const;

    // ── Map settings (in memory) ──
    int   scaleIndex_      = 0;     // index into kScales
    int   contourInterval_ = 5;     // metres
    bool  showGrid_        = true;

    // ── Symbol Viewer (in memory) ──
    int   viewerSelected_  = -1;    // flat catalogue index of the selected symbol
    // Example canvas camera (like the Viewport): zoom = px per mm-at-display-scale,
    // pan = the doc-mm point centred. -1 zoom = "fit on next draw / on selection
    // change". The grid pane is horizontally resizable; gridW_ is its width (px).
    float viewerZoom_      = -1.0f; // <0 → re-fit to the examples next frame
    ImVec2 viewerPan_      = {0,0}; // doc-mm point at the canvas centre
    int   viewerFitSel_    = -2;    // last selection we auto-fit for (re-fit on change)
    float viewerGridW_     = 230.0f;// resizable thumbnail-grid pane width (px)

    // Draw the example canvas (zoom/pan, all examples on one white sheet, dims).
    void DrawExampleCanvas(const IofElement& e, ImVec2 canvasMin, ImVec2 canvasMax);

    // ── Courses (in memory; controls are document objects) ──
    std::vector<IofCourse> courses_;
    int  activeCourse_ = -1;        // -1 = "All controls" view

    // Print-layer collection ids, one per exact ISOM 2017-2 print layer, created
    // on activation UNDER the page in printing order so the Outliner groups
    // symbols by the real colour-separation stack. Indexed by (int)PrintLayer.
    // 0 = not created yet.
    uint64_t layerColl_[kPrintLayerCount] = {};

    // Content signature of the map page's shape list (ids × ranks), so SyncPrintOrder
    // only re-sorts when shapes were added / removed / re-layered (not every frame).
    uint64_t printOrderSig_ = 0;
};

}  // namespace App::Modules::IofMapping

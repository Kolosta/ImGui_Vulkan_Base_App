#pragma once

#include <cstdint>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "ModuleAPI.h"
#include "IofSpec.h"

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  IOF Mapping module — orienteering map authoring (ISOM 2017-2), rebuilt on
//  the Ink engine.
//
//  Symbols are ordinary Ink content:
//   • a hidden SYMBOL LIBRARY (a previewOnly group seeded by OnDocumentCreated)
//     holds one specimen node per catalogue element, styled with CORE tools
//     only (stroke dash + repeats, multi-fill, instanced fills). Vignettes
//     everywhere are the REAL pipeline (ModuleHost::NodePreviewTexture).
//   • POINT symbols place as INSTANCES of their specimen (editing the library
//     re-skins every placement — e.g. a map-scale change);
//   • LINE / AREA symbols run the core pen in symbol-draw mode (the symbol's
//     exact style, the symbol vignette riding the cursor); the committed node
//     is routed into its print-layer group.
//
//  Structure (kept by EnsureStructure, resolved by NAME so saved files reopen
//  into the same skeleton):
//   • layer tree:  page → "Map symbols"  → one group per ISOM print layer, in
//                  painter order (reverse print stack) — fixed, auto z-order;
//                  page → "Map layout"   → "Layout" + "Extras" (the ONLY
//                  editable containers) — above the symbols;
//   • collections: "Map annotations" { "Layout", "Extras" } (editable, on
//                  top) and "IOF Cartography" { one per element type } (fixed;
//                  every placed symbol auto-joins its type collection).
//  The outliner tree is locked (capability) and AllowReparent whitelists only
//  the editable containers.
// ─────────────────────────────────────────────────────────────────────────────

// The ISOM competition scales offered by Map Settings (base scale first).
inline constexpr int kIofScales[]   = { 15000, 10000, 7500, 5000, 4000 };
inline constexpr int kIofScaleCount = 5;

class IofMappingModule final : public IModule {
public:
    ModuleInfo Info() const override;
    void       OnRegister(ModuleContext& ctx) override;
    LayoutSpec BuildLayout() const override;
    void       ConfigureCapabilities(Capabilities& caps) const override;
    std::pair<float, float> DefaultPageSize() const override {
        // A4 landscape in BASE units (css px @96 dpi): 297 × 210 mm.
        constexpr float kPxPerMm = 96.0f / 25.4f;
        return { 297.0f * kPxPerMm, 210.0f * kPxPerMm };
    }
    std::vector<std::string> AllowedEditors() const override;
    void       OnDocumentCreated(Ink::Document& doc) override;
    bool       BuildAddMenu(std::vector<UI::MenuEntry>& out) override;
    void       ViewportSidePanelTabs(std::vector<UI::SidePanelTab>& out) override;
    void       ObjectTools(std::vector<std::string>& out) override;
    void       DrawToolButtons(ImVec2 origin, float size,
                               std::vector<ImVec4>& outRects) override;
    bool       AllowReparent(uint64_t shapeId, uint64_t target) override;
    void       OnFrameSync() override;
    void       OnNumpadSequence(const std::string& digits) override;
    void       OnActivate() override;
    void       OnDeactivate() override;

    // Symbol-size factor for the current map scale, relative to the ISOM base
    // 1:15 000 (1:10 000 → 1.5 …).
    float MapScaleFactor() const;

private:
    // Resolve-or-create the fixed document skeleton (library, print-layer
    // groups, layout groups, collections) — by NAME, cached by id, re-run
    // cheaply (validates the cache against the live document).
    void EnsureStructure();
    // Seed / refresh the symbol library: one "SYM <code>" group per element,
    // its parts rebuilt at the current map scale (children replaced in place —
    // instance targets reference the group, so placements re-skin live).
    void SeedLibrary(bool rebuildExisting);
    // Seed the document's COLOUR TABLE from the official ISOM print-layer list:
    // one locked swatch per separation, carrying its exact CMYK and its place
    // in the plate stack (the table is already in printing order). Matched by
    // name so re-opening a file adopts the swatches it already has instead of
    // duplicating them.
    void SeedPalette();
    // The document swatch of a print layer (0 before SeedPalette has run).
    std::uint64_t LayerSwatch(PrintLayer layer) const;
    // Bind every paint of a built symbol to the document swatch whose colour it
    // uses. A symbol legitimately spans SEVERAL plates — 402 lays yellow 75 %
    // and white dots, 509 black and white — so the binding is per PAINT, not
    // per element. Matching is by colour because the symbol tables and the
    // plate table are built from the same ink constants.
    void BindSwatches(Ink::Style& style, IofType type) const;
    // The library specimen group of an element (0 if absent).
    std::uint64_t LibNode(int code) const;

    // Activate the symbol's TOOL: point → place mode (armed placement with the
    // cursor vignette); line / area → the core pen in symbol-draw mode.
    void SelectSymbol(const IofElement& e);
    // Drop a point-symbol instance at the document position (undoable).
    void PlacePointSymbol(const IofElement& e, double docX, double docY);
    // Route a pen-drawn line/area node into its print layer + collections +
    // locks (called by the core BEFORE the draw undo snapshot).
    void RouteDrawnSymbol(const IofElement& e, std::uint64_t node);
    // Non-cartographic LAYOUT tools (frame, free area, guide line): draw a
    // plain, EDITABLE object into the "Extras" layer/collection — no spec lock.
    void SelectLayoutTool(const char* kind);
    void RouteLayoutObject(std::uint64_t node);

    // Editor bodies (IofMappingPanels.cpp).
    void DrawSymbolsTab(ImVec2 cMin, ImVec2 cMax);
    void DrawSymbolCell(const IofElement& e, float side);  // one vignette tile
    void DrawSymbolViewer(ImVec2 size);
    void DrawMapSettings();

    // ── Map settings ──
    int scaleIndex_ = 0;              // index into kScales (15000 first)
    int contourInterval_ = 5;         // metres

    // ── Cached skeleton ids (validated against the document each sync) ──
    std::uint64_t libRoot_ = 0;
    std::uint64_t mapRoot_ = 0;                       // "Map symbols"
    std::uint64_t layerGroup_[kPrintLayerCount] = {};
    std::uint64_t layoutRoot_ = 0;                    // "Map layout"
    std::uint64_t layoutLayer_ = 0, extrasLayer_ = 0;
    std::uint64_t collIof_ = 0, collAnnot_ = 0;
    // One collection per ISOM THEME group (Landforms, Rock and boulders, …),
    // keyed by the group name — the Outliner Collections organisation.
    std::unordered_map<std::string, std::uint64_t> collTheme_;
    std::uint64_t collLayout_ = 0, collExtras_ = 0;
    std::unordered_map<int, std::uint64_t> libByCode_;
    // Print layer → its document swatch, resolved by SeedPalette.
    std::uint64_t swatchByLayer_[kPrintLayerCount] = {};
    std::uint64_t structureVersion_ = ~0ull;          // doc version at last sync

    // ── Theme tool buttons (viewport palette) ──
    // Six buttons: Landforms / Rock / Water / Vegetation / Man-made /
    // (Technical + Course). Each remembers its currently-selected symbol code.
    int    themeSel_[6] = { 1010, 2040, 3040, 4010, 5030, 7030 };
    // Keep the module arming in sync with the active tool (OnFrameSync): the
    // theme index currently armed + which symbol code, so a tool/symbol change
    // (or a lapsed placement/draw after Esc) re-arms — the tools stay "hot".
    int    armedTheme_  = -1;
    int    armedSymbol_ = -1;
    // Activate a theme tool: make it the active Viewport tool + arm its symbol.
    void   ActivateThemeTool(int idx);
    // Per-frame: reconcile the module arming against the active Viewport tool.
    void   SyncThemeTool();

    // ── Symbol Viewer state ──
    int    viewerSel_  = 1010;        // selected element code
    double viewerZoom_ = -1.0;        // <0 → fit on next draw
    double viewerPanX_ = 0.0, viewerPanY_ = 0.0;
    float  viewerGridW_ = 230.0f;     // resizable grid pane width (px)
};

}  // namespace App::Modules::IofMapping

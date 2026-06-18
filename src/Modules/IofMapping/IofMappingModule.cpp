#include "IofMappingModule.h"
#include "IofSpec.h"
#include "IofGlyph.h"

#include <imgui.h>
#include <UI/Widgets/PopupMenu.h>     // UI::MenuEntry
#include <UI/Widgets/SidePanel.h>     // UI::SidePanelTab
#include <DesignSystem/DesignSystem.h>
#include <Renderer/Document/Document.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "IofMappingShared.h"

namespace App::Modules::IofMapping {

ModuleInfo IofMappingModule::Info() const {
    return { "iof-mapping", "IOF Mapping",
             "Orienteering map authoring (ISOM 2017-2)", "image", "0.1.0" };
}

void IofMappingModule::OnRegister(ModuleContext& ctx) {
    auto& reg = ctx.editors;
    {
        EditorDescriptor d;
        d.id = "iof.mapsettings"; d.name = "Map Settings"; d.icon = "settings";
        d.column = 0; d.themeScope = "editors";
        d.draw = [this](ImVec2, EditorState&) { DrawMapSettings(); };
        reg.Register(std::move(d));
    }
    {
        EditorDescriptor d;
        d.id = "iof.coursesettings"; d.name = "Course Settings"; d.icon = "checklist";
        d.column = 0; d.themeScope = "editors";
        d.draw = [this](ImVec2, EditorState&) { DrawCourseSettings(); };
        reg.Register(std::move(d));
    }
    {
        EditorDescriptor d;
        d.id = "iof.symbolviewer"; d.name = "Symbol Viewer"; d.icon = "grid-view";
        d.column = 0; d.themeScope = "editors";
        d.wrapInScroll = false; d.contentInset = false;   // draws its own panes
        d.draw = [this](ImVec2 size, EditorState&) { DrawSymbolViewer(size); };
        reg.Register(std::move(d));
    }
}

LayoutSpec IofMappingModule::BuildLayout() const {
    using L = LayoutSpec;
    // Left: Viewport (big) over Course Settings. Right: Outliner / Properties /
    // Map Settings stacked. Reuses the core editors by id.
    L left  = L::Split(false, 0.74f, L::Leaf("core.viewport"),
                                     L::Leaf("iof.coursesettings"));
    L right = L::Split(false, 0.40f, L::Leaf("core.outliner"),
              L::Split(false, 0.55f, L::Leaf("core.properties"),
                                     L::Leaf("iof.mapsettings")));
    return L::Split(true, 0.74f, std::move(left), std::move(right));
}

void IofMappingModule::ConfigureCapabilities(Capabilities& caps) const {
    caps.corePrimitivesAddMenu = false;   // Add menu = ISOM catalogue + course
    caps.pages                 = true;
    caps.editMode              = true;
    caps.previewPlacement      = true;    // placement preview is the default here
    caps.lockTransformsForced  = true;    // scale/rotation locks are module-managed
    caps.lockOutlinerTree      = true;    // fixed print-layer hierarchy: no DnD reorg
    caps.documentUnit          = 2;       // millimetres (ISOM symbols are mm-defined)
    caps.curveTool             = false;   // ISOM symbols aren't free-drawn curves
    caps.symbolScale           = MapScaleFactor();  // mm → doc factor for marks
}

std::vector<std::string> IofMappingModule::AllowedEditors() const {
    return { "core.viewport", "core.outliner", "core.properties",
             "iof.mapsettings", "iof.coursesettings", "iof.symbolviewer" };
}

void IofMappingModule::OnActivate() {
    if (courses_.empty()) courses_.push_back({ "Course 1", {} });
    activeCourse_ = -1;
    EnsureLayerCollections();
    // Author on the map page by default, so every placed symbol lands ON the page
    // (in its layer collection) instead of as a page-less orphan.
    if (ModuleHost* h = Host())
        if (uint64_t pageId = MapPageId()) h->Document().SetActivePage(pageId);
}

// The single map page the module authors on (its default A4 page). Everything —
// the print-layer collections and the symbols — lives under it (no orphans).
uint64_t IofMappingModule::MapPageId() const {
    ModuleHost* h = Host(); if (!h) return 0;
    Renderer::Document& doc = h->Document();
    return doc.artboards.empty() ? 0 : doc.artboards.front().id;
}

// Draw rank of a shape = its position in the print STACK, bottom-up. The print
// layers are enumerated top-of-stack first (UpperPurple = 0 … Yellow50Area last),
// so the draw rank is the REVERSE: bottom layers (yellow) draw first (rank 0),
// the purple overprint draws last (highest rank), sitting on top. A shape with no
// known ISOM code sorts to the very top (drawn last) so stray objects stay visible.
int IofMappingModule::PrintRankOf(const Renderer::Shape& s) const {
    if (s.isomCode != 0)
        if (const IofElement* e = IofFindByCode(s.isomCode))
            return (kPrintLayerCount - 1) - (int)e->layer;
    return kPrintLayerCount;   // unknown → on top
}

// Keep the map page's shape list in print-layer z-order (stable within a layer,
// so same-layer objects keep their draw order). Runs only when the page's shape
// set / ranks changed, tracked by a content signature, so it's free on idle frames.
void IofMappingModule::SyncPrintOrder() {
    ModuleHost* h = Host(); if (!h) return;
    Renderer::Document& doc = h->Document();
    const uint64_t pageId = MapPageId();
    Renderer::Artboard* ab = pageId ? doc.FindArtboardById(pageId) : nullptr;
    if (!ab) return;
    // Signature = fold of (id, rank) over the page's shapes — changes on add /
    // remove / re-layer (e.g. a symbol whose code/layer changed).
    uint64_t sig = 1469598103934665603ull;
    auto mix = [&](uint64_t v){ sig = (sig ^ v) * 1099511628211ull; };
    for (const Renderer::Shape& s : ab->shapes) { mix(s.id); mix((uint64_t)PrintRankOf(s)); }
    if (sig == printOrderSig_) return;            // nothing relevant changed
    doc.SortPageShapesStable(pageId, [this](const Renderer::Shape& s){ return PrintRankOf(s); });
    // Recompute the signature on the SORTED list so a stable order doesn't re-fire.
    sig = 1469598103934665603ull;
    for (const Renderer::Shape& s : ab->shapes) { mix(s.id); mix((uint64_t)PrintRankOf(s)); }
    printOrderSig_ = sig;
}

void IofMappingModule::OnFrameSync() {
    SyncPrintOrder();
}

// ── Print-layer collections (Outliner organisation + strict lock) ────────────
// Create one collection per exact ISOM 2017-2 print layer, in PRINTING ORDER
// (top of the stack first → top of the Outliner list), nested UNDER the page so
// the symbols placed into them stay bound to the page (never orphaned). Re-runs
// idempotently (reuses a collection found by name, e.g. from a loaded file).
void IofMappingModule::EnsureLayerCollections() {
    ModuleHost* h = Host(); if (!h) return;
    Renderer::Document& doc = h->Document();
    const uint64_t pageId = MapPageId();
    for (int i = 0; i < kPrintLayerCount; ++i) {
        const PrintLayer layer = (PrintLayer)i;
        const char* nm = LayerName(layer);
        uint64_t id = 0;
        for (const Renderer::Collection& c : doc.collections)
            if (c.name == nm) { id = c.id; break; }          // reuse (e.g. loaded file)
        if (!id) {
            // Create the collection directly under the page (a page is a full tree
            // node), so its objects are page-bound — not loose under the root.
            id = doc.AddCollection(nm, pageId ? pageId : Renderer::kProjectRootId);
            if (Renderer::Collection* c = doc.FindCollection(id)) {
                IofRgb col = LayerRenderColor(layer);
                c->colorIndex  = -1;                          // custom RGBA swatch
                c->customColor = { col.r, col.g, col.b, 1.0f };
            }
        } else if (pageId && doc.PageAncestorOf(id) != pageId) {
            // Reused collection that isn't under the page yet (old file / earlier
            // layout) → move it under the page so its objects bind to it.
            doc.MoveNode(id, pageId);
        }
        layerColl_[i] = id;
    }
    // Collections were (re)created under the page; bind any objects accordingly.
    doc.ReflowLooseShapes();
}

uint64_t IofMappingModule::LayerCollection(PrintLayer layer) const {
    int i = (int)layer;
    return (i >= 0 && i < kPrintLayerCount) ? layerColl_[i] : 0;
}

uint64_t IofMappingModule::CollectionForSymbol(const IofElement& e) const {
    return LayerCollection(e.layer);
}

bool IofMappingModule::AllowReparent(uint64_t shapeId, uint64_t targetCollectionId) {
    ModuleHost* h = Host(); if (!h) return true;
    Renderer::Shape* s = h->Document().FindShape(shapeId);
    if (!s) return true;
    const uint64_t cur = s->collectionId;
    // A symbol that lives in a print-layer collection must stay there (only a
    // no-op / same-layer move is allowed; sub-collections would be too if any).
    for (int i = 0; i < kPrintLayerCount; ++i)
        if (layerColl_[i] != 0 && cur == layerColl_[i])
            return targetCollectionId == cur;
    return true;   // not in a layer collection → unrestricted
}

// ── Course object creation (via Shift+A) ─────────────────────────────────────
int IofMappingModule::NextControlNumber() const {
    int maxN = 0;
    if (ModuleHost* h = Host()) {
        ForEachCourseObject(h->Document(), [&](Renderer::Shape& s) {
            if (!IsControl(s)) return;
            int n = std::atoi(s.name.c_str() + 7);   // after "Control"
            maxN = std::max(maxN, n);
        });
    }
    return maxN + 1;
}

// Bake `e`'s exact ISOM glyph (mm × map scale) and place it at the 2D cursor via
// the host (honours preview placement). `nameOverride` lets course objects carry
// "Control 3" etc. EVERY symbol — including the purple course overprint — lands ON
// the page, inside its print-layer collection (no page-less orphans).
void IofMappingModule::PlaceSymbol(const IofElement& e, const std::string& nameOverride) {
    ModuleHost* h = Host(); if (!h) return;
    // Make sure new objects spawn on the map page (in case nothing is active yet).
    if (uint64_t pageId = MapPageId()) h->Document().SetActivePage(pageId);
    Renderer::Shape sh = BuildSymbolShape(e, MapScaleFactor());
    if (!nameOverride.empty()) sh.name = nameOverride;
    // Line/area symbols are DRAWN point-by-point with the symbol's style; points
    // and fixed-size glyphs are stamped at the cursor.
    ModuleHost::PlaceMode mode =
        e.type == IofType::Line ? ModuleHost::PlaceMode::DrawLine :
        e.type == IofType::Area ? ModuleHost::PlaceMode::DrawArea :
                                  ModuleHost::PlaceMode::Stamp;
    h->AddBakedShape(sh, /*loose=*/false, CollectionForSymbol(e), mode);
    // For drawn (line/area) symbols, give the mini-ghost a compact preview.
    if (mode != ModuleHost::PlaceMode::Stamp)
        h->SetPlacementPreview(PreviewShape(e, MapScaleFactor()));
}

void IofMappingModule::AddControlObject() {
    if (const IofElement* e = IofFindByCode(7030)) {
        char nm[24]; std::snprintf(nm, sizeof(nm), "Control %d", NextControlNumber());
        PlaceSymbol(*e, nm);
    }
}
void IofMappingModule::AddStartObject()  { if (const IofElement* e = IofFindByCode(7010)) PlaceSymbol(*e, "Start"); }
void IofMappingModule::AddFinishObject() { if (const IofElement* e = IofFindByCode(7060)) PlaceSymbol(*e, "Finish"); }

// ── Shift+A: the ISOM catalogue (grouped) + course planning ──────────────────
bool IofMappingModule::BuildAddMenu(std::vector<UI::MenuEntry>& out) {
    for (const IofGroup& g : IofCatalogue()) {
        // The purple course symbols are placed via the dedicated "Course planning"
        // submenu below (auto-numbered, proper geometry) — don't duplicate them.
        if (std::string(g.name) == "Course (overprint)") continue;
        UI::MenuEntry group; group.label = g.name;
        for (const IofElement& e : g.elements) {
            UI::MenuEntry leaf;
            leaf.label   = IofElementLabel(e);
            leaf.tooltip = e.desc;
            const IofElement el = e;                         // copy (static strings)
            leaf.onClick = [this, el] { PlaceSymbol(el, {}); };
            group.submenu.push_back(std::move(leaf));
        }
        out.push_back(std::move(group));
    }
    // Course planning (overprint) — real, movable course objects.
    UI::MenuEntry course; course.label = "Course planning";
    { UI::MenuEntry e; e.label = "Control"; e.tooltip = "Place a control (auto-numbered)";
      e.onClick = [this]{ AddControlObject(); }; course.submenu.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "Start";   e.tooltip = "Place the start triangle";
      e.onClick = [this]{ AddStartObject();  }; course.submenu.push_back(std::move(e)); }
    { UI::MenuEntry e; e.label = "Finish";  e.tooltip = "Place the finish (double circle)";
      e.onClick = [this]{ AddFinishObject(); }; course.submenu.push_back(std::move(e)); }
    out.push_back(std::move(course));
    return true;
}

// ── Viewport side panel: "Map elements" browser (preview + description) ──────
void IofMappingModule::ViewportSidePanelTabs(std::vector<UI::SidePanelTab>& out) {
    UI::SidePanelTab tab; tab.name = "Map elements";
    tab.draw = [this](ImVec2 cMin, ImVec2 cMax) { DrawMapElementsTab(cMin, cMax); };
    out.push_back(std::move(tab));
}


}  // namespace App::Modules::IofMapping

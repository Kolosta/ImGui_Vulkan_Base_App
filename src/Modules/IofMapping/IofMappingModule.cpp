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

namespace App::Modules::IofMapping {

namespace {
const char* kScales[] = { "1:15000", "1:10000", "1:7500", "1:5000", "1:4000" };
constexpr int kScaleCount = (int)(sizeof(kScales) / sizeof(kScales[0]));

// Course overprint colour (ISOM purple), straight RGB.
constexpr float kPurpleR = 0.80f, kPurpleG = 0.0f, kPurpleB = 0.55f;

// Is this shape a placed course object, and of which kind?
bool IsControl(const Renderer::Shape& s) { return s.name.rfind("Control", 0) == 0; }
bool IsStart  (const Renderer::Shape& s) { return s.name == "Start"; }
bool IsFinish (const Renderer::Shape& s) { return s.name == "Finish"; }
bool IsCourseObject(const Renderer::Shape& s) { return IsControl(s) || IsStart(s) || IsFinish(s); }

// A course object's raw DOCUMENT-space centre (world coords). Object geometry is
// page-relative, so world = pageOrigin + translate + origin. Course objects now
// live ON the page, so the page offset must be added (loose objects → {0,0}).
ImVec2 DocCentre(Renderer::Document& doc, const Renderer::Shape& s) {
    Renderer::Vec2 po = doc.PageOriginOfShape(s.id);
    return ImVec2(po.x + s.transform.translate.x + s.origin.x,
                  po.y + s.transform.translate.y + s.origin.y);
}

// Apply `fn` to every placed course OBJECT in the document (on a page or loose),
// so course tooling works whether symbols live on the page or as legacy orphans.
template <class Fn>
void ForEachCourseObject(Renderer::Document& doc, Fn&& fn) {
    for (Renderer::Artboard& ab : doc.artboards)
        for (Renderer::Shape& s : ab.shapes) if (IsCourseObject(s)) fn(s);
    for (Renderer::Shape& s : doc.looseShapes) if (IsCourseObject(s)) fn(s);
}

// A pre-tessellated glyph thumbnail (mesh in LOCAL mm + its bounds), built once
// and reused across frames — tessellation is zoom-independent now, so the same
// mesh is just remapped into whatever rect each frame (no per-frame tessellation,
// which was tanking the Symbol Viewer / Map-elements tab with ~90 glyphs).
struct GlyphThumb {
    Renderer::Mesh mesh;
    Renderer::Vec2 bmin{}, bmax{};
    bool           ok = false;
};

// Module-wide thumbnail cache, keyed by (isomCode, quantized scale). Cleared via
// the generation counter when the map scale changes.
GlyphThumb& ThumbFor(const IofElement& e, float scale) {
    struct Key { int code; int scaleq; bool operator==(const Key& o) const {
        return code == o.code && scaleq == o.scaleq; } };
    struct KeyHash { size_t operator()(const Key& k) const {
        return (size_t)k.code * 131u + (size_t)k.scaleq; } };
    static std::unordered_map<Key, GlyphThumb, KeyHash> cache;
    Key key{ e.code, (int)std::lround(scale * 1000.0f) };
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    GlyphThumb t;
    Renderer::Shape sh = BuildSymbolShape(e, scale);
    if (Renderer::Tessellator::WorldBounds(sh, 1.0f, t.bmin, t.bmax)) {
        Renderer::Tessellator::AppendShape(sh, t.mesh, 1.0f, {0, 0});
        t.ok = true;
    }
    return cache.emplace(key, std::move(t)).first->second;
}

// A cached PREVIEW shape + its content hash, keyed by (isomCode, quantized scale).
// Building a symbol (BuildSymbolShape inside PreviewShape) and hashing it every
// frame for all ~90 thumbnails was the steady-state cost that dropped fps while the
// side panel / Symbol Viewer were open — the texture was already cached, but the
// KEY computation rebuilt + re-hashed the glyph each frame. This memoises both.
struct PreviewEntry { Renderer::Shape shape; uint64_t hash = 0; };
const PreviewEntry& PreviewFor(const IofElement& e, float scale) {
    struct Key { int code; int scaleq; bool operator==(const Key& o) const {
        return code == o.code && scaleq == o.scaleq; } };
    struct KeyHash { size_t operator()(const Key& k) const {
        return (size_t)k.code * 131u + (size_t)k.scaleq; } };
    static std::unordered_map<Key, PreviewEntry, KeyHash> cache;
    Key key{ e.code, (int)std::lround(scale * 1000.0f) };
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    PreviewEntry pe;
    pe.shape = PreviewShape(e, scale);
    pe.hash  = Renderer::Tessellator::HashShape(pe.shape, {0, 0});
    return cache.emplace(key, std::move(pe)).first->second;
}

// Blit a cached glyph thumbnail mesh into rect [mn,mx], fit + centred, white card.
void DrawGlyphThumbCached(ImDrawList* dl, const GlyphThumb& t,
                          ImVec2 mn, ImVec2 mx, float padFrac = 0.18f,
                          bool whiteCard = true) {
    if (whiteCard) {
        dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), 3.0f);
        dl->AddRect(mn, mx, IM_COL32(0, 0, 0, 40), 3.0f);
    }
    if (!t.ok) return;
    float gw = std::max(0.01f, t.bmax.x - t.bmin.x);
    float gh = std::max(0.01f, t.bmax.y - t.bmin.y);
    float bw = (mx.x - mn.x) * (1.0f - 2 * padFrac);
    float bh = (mx.y - mn.y) * (1.0f - 2 * padFrac);
    float zoom = std::min(bw / gw, bh / gh);
    ImVec2 ctr((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    Renderer::Vec2 gctr((t.bmin.x + t.bmax.x) * 0.5f, (t.bmin.y + t.bmax.y) * 0.5f);
    const auto& v = t.mesh.vertices;
    auto map = [&](const Renderer::Vertex& vert) {
        return ImVec2(ctr.x + (vert.x - gctr.x) * zoom, ctr.y + (vert.y - gctr.y) * zoom);
    };
    const ImDrawListFlags saved = dl->Flags;   // no per-triangle AA seams
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (size_t i = 0; i + 3 <= v.size(); i += 3) {
        const Renderer::Vertex& v0 = v[i];
        dl->AddTriangleFilled(map(v[i]), map(v[i + 1]), map(v[i + 2]),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(v0.r, v0.g, v0.b, v0.a)));
    }
    dl->Flags = saved;
}

// Convenience: build (or fetch) the cached thumbnail for `e` and blit it.
void DrawGlyphThumb(ImDrawList* dl, const IofElement& e, float scale,
                    ImVec2 mn, ImVec2 mx, float padFrac = 0.18f, bool whiteCard = true) {
    DrawGlyphThumbCached(dl, ThumbFor(e, scale), mn, mx, padFrac, whiteCard);
}
}  // namespace

// Blit a symbol's glyph as a smooth (SSAA) Vulkan texture via the host; falls
// back to the cached CPU triangle blit when the host can't render textures.
//
// CRITICAL: the texture is rendered at a FIXED resolution (not the on-screen cell
// size) and just blit-scaled to [mn,mx]. The cache key therefore stays stable
// while the panel resizes — otherwise every resize frame allocated a fresh GPU
// target per symbol (vkDeviceWaitIdle each time → severe lag, then descriptor /
// device-memory exhaustion → VK_ERROR_OUT_OF_POOL_MEMORY / device-lost crash).
void IofMappingModule::DrawGlyphImage(ImDrawList* dl, const IofElement& e, float scale,
                                      ImVec2 mn, ImVec2 mx, uint64_t keySalt, float padFrac) {
    DrawGlyphImageRounded(dl, e, scale, mn, mx, keySalt, padFrac, 0.0f);
}

void IofMappingModule::DrawGlyphImageRounded(ImDrawList* dl, const IofElement& e,
                                             float scale, ImVec2 mn, ImVec2 mx,
                                             uint64_t keySalt, float padFrac, float rounding) {
    // Render the thumbnail at ~2× the on-screen cell size so the curve tessellation
    // (which scales with the render-target zoom) is fine and the SSAA resolves crisp
    // edges — a small target left contours faceted + stair-stepped. The pixel size is
    // BUCKETED to a 32-px step and folded into the key, so small resizes reuse the
    // cached GPU target (a new size = vkDeviceWaitIdle + realloc; the old fixed-64
    // path avoided that but at the cost of softness). The PREVIEW shape and its hash
    // are memoised (PreviewFor) so we don't rebuild + re-hash 90 glyphs every frame —
    // that per-frame cost is what dropped fps while these panels were open.
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float cellPx = std::max(std::fabs(mx.x - mn.x), std::fabs(mx.y - mn.y));
    int thumbPx = (int)std::ceil(cellPx * ds.GetGlobalScale() * 2.0f);   // 2× supersample
    thumbPx = std::clamp(((thumbPx + 31) / 32) * 32, 64, 384);           // bucket to 32px
    // Curve and surface symbols FILL the cell (no whitespace); points keep a small
    // margin so the glyph isn't clipped to the edges.
    const float pad = (e.type == IofType::Line || e.type == IofType::Area) ? 0.0f : padFrac;
    ModuleHost* h = Host();
    if (h) {
        // Memoised compact preview (short line sample / small area swatch) + its hash.
        const PreviewEntry& pe = PreviewFor(e, scale);
        // Key depends on (symbol, salt, bucketed size) — stable across small resizes.
        uint64_t key = ((uint64_t)(uint32_t)e.code) ^ (keySalt << 40)
                     ^ ((uint64_t)thumbPx << 28) ^ (0xC0FFEEull << 16);
        uint64_t chash = pe.hash ^ ((uint64_t)thumbPx << 1);
        std::vector<Renderer::Shape> shapes = { pe.shape };
        ImTextureID tex = h->RenderGlyphTexture(key, chash, shapes, thumbPx, thumbPx, pad);
        if (tex) {
            if (rounding > 0.5f)
                dl->AddImageRounded(tex, mn, mx, ImVec2(0,0), ImVec2(1,1),
                                    IM_COL32_WHITE, rounding);
            else
                dl->AddImage(tex, mn, mx);
            return;
        }
    }
    DrawGlyphThumb(dl, e, scale, mn, mx, padFrac, true);   // CPU fallback
}

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

void IofMappingModule::DrawMapElementsTab(ImVec2 contentMin, ImVec2 contentMax) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = MapScaleFactor();
    ImGui::SetCursorScreenPos(ImVec2(contentMin.x + 8.0f, contentMin.y + 6.0f));
    ImGui::BeginChild("##mapElems", ImVec2(contentMax.x - contentMin.x - 8.0f,
                                           contentMax.y - contentMin.y - 8.0f),
                      false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_None);
    ImGui::TextDisabled("ISOM 2017-2 elements");
    ImGui::Spacing();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Responsive grid: pick a cell size that fits an integer number of columns in
    // the available width, then re-flow as the panel resizes. Spacing + rounding
    // come from design-system tokens (no panel background — the SidePanel owns it).
    const float avail   = std::max(48.0f, ImGui::GetContentRegionAvail().x);
    const float gap     = ds.GetFloat(DesignSystem::Tok::P_Spacing_50);
    const float rounding= ds.GetFloat(DesignSystem::Tok::C_Dropdown_CornerRadius);
    const float target  = 48.0f;                // preferred cell size (px)
    int cols = std::max(1, (int)std::floor((avail + gap) / (target + gap)));
    const float cell = std::max(28.0f, (avail - gap * (cols - 1)) / (float)cols);

    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
        ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    const int armed = Host() ? Host()->ArmedSymbolCode() : 0;
    int uid = 0;
    for (const IofGroup& g : IofCatalogue()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", g.name);
        int col = 0;
        for (const IofElement& e : g.elements) {
            if (col != 0) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(uid++);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 pmax(p.x + cell, p.y + cell);
            bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cell, cell));
            bool hov = ImGui::IsItemHovered();
            bool sel = (armed != 0 && armed == e.code);
            // No filled background (the panel provides it). Subtle hover tint only.
            if (hov && !sel) dl->AddRectFilled(p, pmax, IM_COL32(255, 255, 255, 22), rounding);
            DrawGlyphImageRounded(dl, e, scale, p, pmax, /*keySalt=*/2, 0.14f, rounding);
            // Accent outline around the ARMED symbol; thin accent on hover.
            if (sel)      dl->AddRect(p, pmax, accent, rounding, 0, 2.0f);
            else if (hov) dl->AddRect(p, pmax, accent, rounding, 0, 1.0f);
            if (hov) {
                // Styled tooltip (name + brief description), not the default one.
                std::string tip = IofElementLabel(e);
                if (e.desc && e.desc[0]) { tip += "\n"; tip += e.desc; }
                UI::DrawTooltip(tip.c_str(), ImGui::GetIO().MousePos);
            }
            if (clicked) PlaceSymbol(e, {});
            ImGui::PopID();
            col = (col + 1) % cols;
        }
    }
    ImGui::EndChild();
}

// ── Symbol Viewer editor ─────────────────────────────────────────────────────
// Three panes: a RESIZABLE thumbnail grid (auto-reflow) | a single white example
// CANVAS with its own zoom/pan (like the Viewport) showing every example on one
// sheet at the ISOM base scale | a DESCRIPTION panel (title, type, brief + full
// spec text). The canvas draws the hand-authored precise plate when one exists
// (SymbolPlateFor), else the legacy SymbolExamples/SymbolDims layout on one sheet.
void IofMappingModule::DrawSymbolViewer(ImVec2 size) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = MapScaleFactor();

    // Flatten the catalogue into a single addressable list.
    struct Item { const IofElement* e; };
    std::vector<Item> items;
    for (const IofGroup& g : IofCatalogue())
        for (const IofElement& el : g.elements) items.push_back({ &el });
    if (viewerSelected_ < 0 || viewerSelected_ >= (int)items.size())
        viewerSelected_ = items.empty() ? -1 : 0;

    // Pane widths: grid (resizable) | canvas (flex) | description (fixed-ish).
    const float gs = ds.GetGlobalScale();
    const float minGrid = 130.0f * gs, minCanvas = 200.0f * gs;
    const float descW = std::min(std::max(260.0f * gs, size.x * 0.26f), 420.0f * gs);
    viewerGridW_ = std::clamp(viewerGridW_, minGrid, std::max(minGrid, size.x - minCanvas - descW - 40.0f));
    const float splitterW = 6.0f;

    // ── Left: the symbol grid (auto-reflow) ───────────────────────────────────
    ImGui::BeginChild("##symGrid", ImVec2(viewerGridW_, size.y), true);
    {
        ImGui::TextDisabled("ISOM 2017-2 symbols");
        ImGui::Spacing();
        const float gap      = ds.GetFloat(DesignSystem::Tok::P_Spacing_50);
        const float rounding = ds.GetFloat(DesignSystem::Tok::C_Dropdown_CornerRadius);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float pref  = 48.0f * gs;
        int cols = std::max(1, (int)std::floor((avail + gap) / (pref + gap)));
        const float cell = std::max(28.0f, (avail - gap * (cols - 1)) / (float)cols);
        const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
            ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int idx = 0; int col = 0;
        for (const IofGroup& g : IofCatalogue()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", g.name);
            col = 0;
            for (const IofElement& el : g.elements) {
                if (col != 0) ImGui::SameLine(0.0f, gap);
                ImGui::PushID(idx);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec2 pmax(p.x + cell, p.y + cell);
                bool sel = (idx == viewerSelected_);
                if (ImGui::InvisibleButton("##c", ImVec2(cell, cell))) viewerSelected_ = idx;
                bool hov = ImGui::IsItemHovered();
                if (hov && !sel) dl->AddRectFilled(p, pmax, IM_COL32(255,255,255,22), rounding);
                DrawGlyphImageRounded(dl, el, scale, p, pmax, /*keySalt=*/1, 0.16f, rounding);
                if (sel)      dl->AddRect(p, pmax, accent, rounding, 0, 2.0f);
                else if (hov) dl->AddRect(p, pmax, accent, rounding, 0, 1.0f);
                if (hov) UI::DrawTooltip(
                    (IofElementLabel(el) + "\n" + (el.desc ? el.desc : "")).c_str(),
                    ImGui::GetIO().MousePos);
                ImGui::PopID();
                ++idx; col = (col + 1) % cols;
            }
        }
    }
    ImGui::EndChild();

    // ── Splitter: drag to resize the grid pane (horizontal) ───────────────────
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##gridSplit", ImVec2(splitterW, size.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) viewerGridW_ += ImGui::GetIO().MouseDelta.x;
    {
        ImVec2 sp = ImGui::GetItemRectMin(), spx = ImGui::GetItemRectMax();
        ImU32 c = ImGui::ColorConvertFloat4ToU32(ds.GetColor(
            (ImGui::IsItemActive() || ImGui::IsItemHovered())
                ? DesignSystem::Tok::S_Color_Accent_Default
                : DesignSystem::Tok::S_Color_Border_Default));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2((sp.x+spx.x)*0.5f-0.5f, sp.y), ImVec2((sp.x+spx.x)*0.5f+0.5f, spx.y), c);
    }
    ImGui::SameLine(0.0f, 0.0f);

    // ── Middle: the example canvas (zoom/pan, one white sheet) ────────────────
    const float canvasW = std::max(minCanvas, size.x - viewerGridW_ - splitterW - descW - 16.0f);
    ImGui::BeginChild("##symCanvas", ImVec2(canvasW, size.y), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (viewerSelected_ >= 0 && viewerSelected_ < (int)items.size()) {
        ImVec2 cMin = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        DrawExampleCanvas(*items[(size_t)viewerSelected_].e,
                          cMin, ImVec2(cMin.x + avail.x, cMin.y + avail.y));
    } else {
        ImGui::TextDisabled("Select a symbol from the grid.");
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 8.0f);

    // ── Right: the description panel ──────────────────────────────────────────
    ImGui::BeginChild("##symDesc", ImVec2(0, size.y), true);
    if (viewerSelected_ >= 0 && viewerSelected_ < (int)items.size()) {
        const IofElement& e = *items[(size_t)viewerSelected_].e;
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
        ImGui::TextWrapped("%s", IofElementLabel(e).c_str());
        ImGui::PopStyleColor();
        const char* typeName = e.type == IofType::Point ? "Point (P)"
                             : e.type == IofType::Line  ? "Line (L)"
                             : e.type == IofType::Area  ? "Area (A)" : "Text (T)";
        ImGui::TextDisabled("%s   ·   %s", typeName, LayerName(e.layer));
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
        ImGui::TextWrapped("%s", e.desc);                    // brief
        ImGui::PopStyleColor();
        const char* full = IofFullDescription(e.code);
        if (full && full[0]) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
            ImGui::TextWrapped("%s", full);                  // full spec text
            ImGui::PopStyleColor();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Place this symbol")) PlaceSymbol(e, {});
    }
    ImGui::EndChild();
}

// Draw the example CANVAS: one white sheet with its own Viewport-style zoom/pan
// (wheel = zoom at cursor, middle-drag = pan, Ctrl/Shift+wheel = pan), the zoom
// readout top-left, every example positioned on the same sheet (ISOM base scale),
// and the red dimension callouts placed in plate mm. Falls back to the legacy
// per-example layout (laid out on one sheet) when no hand-authored plate exists.
void IofMappingModule::DrawExampleCanvas(const IofElement& e, ImVec2 cMin, ImVec2 cMax) {
    auto& ds   = DesignSystem::DesignSystem::Instance();
    ModuleHost* host = Host();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float baseScale = 2.0f;        // draw at 1:7 500 (×2 vs the 1:15 000 base)
    const ImU32 red  = IM_COL32(220, 30, 30, 255);
    const ImU32 blue = IM_COL32(46, 44, 126, 255);

    // White sheet background + frame, clipped to the canvas.
    dl->AddRectFilled(cMin, cMax, IM_COL32(255,255,255,255), 4.0f);
    dl->AddRect(cMin, cMax, IM_COL32(0,0,0,40), 4.0f);
    dl->PushClipRect(cMin, cMax, true);

    // Gather the plate (preferred) or build a fallback scene from SymbolExamples,
    // laying multiple legacy examples out left→right on the single sheet.
    SymbolPlate plate = SymbolPlateFor(e, baseScale);
    if (!plate.valid()) {
        std::vector<SymbolExample> exs = SymbolExamples(e, baseScale);
        std::vector<DimAnnotation> dims = SymbolDims(e);
        float ox = 0.0f;
        for (size_t i = 0; i < exs.size(); ++i) {
            // Place each example to the right of the previous one (bbox-spaced).
            Renderer::Vec2 mn{1e30f,1e30f}, mx{-1e30f,-1e30f}; bool any=false;
            for (Renderer::Shape s : exs[i].shapes) {
                Renderer::Vec2 a,b; if (Renderer::Tessellator::WorldBounds(s,1.0f,a,b)) {
                    mn.x=std::min(mn.x,a.x);mn.y=std::min(mn.y,a.y);
                    mx.x=std::max(mx.x,b.x);mx.y=std::max(mx.y,b.y);any=true; } }
            float w = any ? (mx.x-mn.x) : 4.0f*baseScale;
            for (Renderer::Shape s : exs[i].shapes) {
                s.transform.translate.x += ox - (any?mn.x:0);
                plate.shapes.push_back(std::move(s));
            }
            if (i == 0) for (const DimAnnotation& a : dims) {   // dims on the 1st example
                PlateDim d; d.a={a.a.x*baseScale - (any?mn.x:0), a.a.y*baseScale};
                d.b={a.b.x*baseScale - (any?mn.x:0), a.b.y*baseScale};
                d.labelPos={ (mx.x-mn.x) + 1.0f*baseScale - (any?mn.x:0), a.a.y*baseScale };
                d.mm=a.mm; d.label=a.label; d.withSpan = !(a.a.x==a.b.x && a.a.y==a.b.y);
                plate.dims.push_back(d);
            }
            ox += w + 6.0f * baseScale;
        }
    }

    // World bbox of the whole plate (mm) → used for the initial fit.
    Renderer::Vec2 wmn{1e30f,1e30f}, wmx{-1e30f,-1e30f}; bool any=false;
    for (const Renderer::Shape& s : plate.shapes) {
        Renderer::Vec2 a,b; if (Renderer::Tessellator::WorldBounds(s,1.0f,a,b)) {
            wmn.x=std::min(wmn.x,a.x);wmn.y=std::min(wmn.y,a.y);
            wmx.x=std::max(wmx.x,b.x);wmx.y=std::max(wmx.y,b.y);any=true; } }
    for (const PlateDim& d : plate.dims) {   // include labels in the fit
        wmn.x=std::min(wmn.x,std::min(d.a.x,d.labelPos.x)); wmn.y=std::min(wmn.y,std::min(d.a.y,d.labelPos.y));
        wmx.x=std::max(wmx.x,std::max(d.b.x,d.labelPos.x+8.0f)); wmx.y=std::max(wmx.y,std::max(d.b.y,d.labelPos.y));
        any=true; }
    for (const PlateFrame& f : plate.frames) {
        wmn.x=std::min(wmn.x,f.min.x); wmn.y=std::min(wmn.y,f.min.y);
        wmx.x=std::max(wmx.x,f.min.x+f.size.x); wmx.y=std::max(wmx.y,f.min.y+f.size.y); any=true; }
    if (!any) { wmn={-4,-4}; wmx={4,4}; }

    // Fit on selection change or when un-zoomed (viewerZoom_ < 0).
    const ImVec2 cSize(cMax.x - cMin.x, cMax.y - cMin.y);
    if (viewerZoom_ < 0.0f || viewerFitSel_ != viewerSelected_) {
        float gw = std::max(0.01f, wmx.x-wmn.x), gh = std::max(0.01f, wmx.y-wmn.y);
        viewerZoom_ = std::min((cSize.x*0.82f)/gw, (cSize.y*0.82f)/gh);
        viewerPan_  = { (wmn.x+wmx.x)*0.5f, (wmn.y+wmx.y)*0.5f };
        viewerFitSel_ = viewerSelected_;
    }
    // doc-mm → screen, centred on viewerPan_ at viewerZoom_ px/mm.
    ImVec2 ctr((cMin.x+cMax.x)*0.5f, (cMin.y+cMax.y)*0.5f);
    auto toScr = [&](Renderer::Vec2 v){
        return ImVec2(ctr.x + (v.x - viewerPan_.x)*viewerZoom_,
                      ctr.y + (v.y - viewerPan_.y)*viewerZoom_); };

    // ── Camera input (Viewport semantics) ──
    ImGui::SetCursorScreenPos(cMin);
    ImGui::InvisibleButton("##canvasHit", cSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hov = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (hov) {
        if (io.MouseWheel != 0.0f) {
            if (io.KeyCtrl)       viewerPan_.y -= io.MouseWheel * 30.0f / viewerZoom_;
            else if (io.KeyShift) viewerPan_.x -= io.MouseWheel * 30.0f / viewerZoom_;
            else {
                // Zoom centred on the cursor (keep the doc-point under it fixed).
                Renderer::Vec2 before{ viewerPan_.x + (io.MousePos.x-ctr.x)/viewerZoom_,
                                       viewerPan_.y + (io.MousePos.y-ctr.y)/viewerZoom_ };
                float f = io.MouseWheel > 0 ? 1.1f : 1.0f/1.1f;
                viewerZoom_ = std::clamp(viewerZoom_ * f, 0.05f, 5000.0f);
                Renderer::Vec2 after{ viewerPan_.x + (io.MousePos.x-ctr.x)/viewerZoom_,
                                      viewerPan_.y + (io.MousePos.y-ctr.y)/viewerZoom_ };
                viewerPan_.x += before.x - after.x; viewerPan_.y += before.y - after.y;
            }
        }
        if (io.MouseWheelH != 0.0f) viewerPan_.x -= io.MouseWheelH * 30.0f / viewerZoom_;
    }
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
         ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))) {
        viewerPan_.x -= io.MouseDelta.x / viewerZoom_;
        viewerPan_.y -= io.MouseDelta.y / viewerZoom_;
    }

    // ── Blue reference frames (the "min." boxes) ──
    for (const PlateFrame& f : plate.frames) {
        ImVec2 a = toScr(f.min), b = toScr({ f.min.x+f.size.x, f.min.y+f.size.y });
        dl->AddRect(a, b, blue, 0, 0, 1.4f);
    }

    // ── Symbols: one cached SSAA texture for the whole plate, exact-fit blit ──
    if (host && !plate.shapes.empty()) {
        // Render the union bbox of the SYMBOLS into a texture and blit it 1:1 at the
        // mapped rect, so it shares the canvas zoom/pan and stays crisp (exact-fit).
        Renderer::Vec2 smn{1e30f,1e30f}, smx{-1e30f,-1e30f}; bool sany=false;
        for (const Renderer::Shape& s : plate.shapes) {
            Renderer::Vec2 a,b; if (Renderer::Tessellator::WorldBounds(s,1.0f,a,b)) {
                smn.x=std::min(smn.x,a.x);smn.y=std::min(smn.y,a.y);
                smx.x=std::max(smx.x,b.x);smx.y=std::max(smx.y,b.y);sany=true; } }
        if (sany) {
            ImVec2 p0 = toScr(smn), p1 = toScr(smx);
            int wpx = std::max(8, (int)std::lround(std::fabs(p1.x-p0.x)));
            int hpx = std::max(8, (int)std::lround(std::fabs(p1.y-p0.y)));
            // Bucket the pixel size so a small zoom step reuses the cached target.
            wpx = std::clamp((wpx/24)*24, 24, 2048); hpx = std::clamp((hpx/24)*24, 24, 2048);
            uint64_t key = ((uint64_t)(uint32_t)e.code << 20) ^ 0x9101A7Eull;
            uint64_t chash = ((uint64_t)wpx<<10) ^ ((uint64_t)hpx<<28);
            for (const Renderer::Shape& s : plate.shapes) chash ^= Renderer::Tessellator::HashShape(s,{0,0});
            ImTextureID tex = host->RenderGlyphTexture(key, chash, plate.shapes, wpx, hpx,
                                /*padFrac=*/0.0f, /*transparent=*/true, /*exactFit=*/true,
                                &smn, &smx);
            if (tex) dl->AddImage(tex, ImVec2(std::min(p0.x,p1.x),std::min(p0.y,p1.y)),
                                       ImVec2(std::max(p0.x,p1.x),std::max(p0.y,p1.y)));
        }
    }

    // ── Red dimension callouts (placed in plate mm) ──
    for (const PlateDim& d : plate.dims) {
        if (d.withSpan) {
            ImVec2 sa = toScr(d.a), sb = toScr(d.b);
            dl->AddLine(sa, sb, red, 1.2f);
            ImVec2 dd(sb.x-sa.x, sb.y-sa.y); float l=std::sqrt(dd.x*dd.x+dd.y*dd.y); if(l<1e-3f)l=1;
            ImVec2 nrm(-dd.y/l*3.0f, dd.x/l*3.0f);
            dl->AddLine(ImVec2(sa.x-nrm.x,sa.y-nrm.y), ImVec2(sa.x+nrm.x,sa.y+nrm.y), red, 1.2f);
            dl->AddLine(ImVec2(sb.x-nrm.x,sb.y-nrm.y), ImVec2(sb.x+nrm.x,sb.y+nrm.y), red, 1.2f);
        }
        char buf[48]; std::string lbl = d.label;
        if (lbl.empty()) { std::snprintf(buf,sizeof buf,"%.2f", d.mm); lbl = buf; }
        ImVec2 lp = toScr(d.labelPos);
        dl->AddText(lp, red, lbl.c_str());
    }

    dl->PopClipRect();

    // ── Zoom readout, top-left: the plate is drawn at 1:7 500; show the on-screen
    // magnification relative to that drawing (zoom 100% = 1 screen-px per plate mm).
    char zb[64];
    std::snprintf(zb, sizeof zb, "1:7 500   ·   zoom %.0f%%", (viewerZoom_ / baseScale) * 100.0f);
    ImVec2 tp(cMin.x + 8.0f, cMin.y + 6.0f);
    dl->AddRectFilled(ImVec2(tp.x-4, tp.y-2),
        ImVec2(tp.x + ImGui::CalcTextSize(zb).x + 4, tp.y + ImGui::GetTextLineHeight()+2),
        IM_COL32(255,255,255,200), 3.0f);
    dl->AddText(tp, ImGui::ColorConvertFloat4ToU32(
        ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle)), zb);
}

// Symbol-size factor for the current map scale (ISOM mm are given at 1:15 000).
// 1:15000→1.0, 1:10000→1.5, 1:7500→2.0, 1:5000→3.0, 1:4000→3.75.
float IofMappingModule::MapScaleFactor() const {
    static const float kDenom[] = { 15000.0f, 10000.0f, 7500.0f, 5000.0f, 4000.0f };
    int i = (scaleIndex_ >= 0 && scaleIndex_ < kScaleCount) ? scaleIndex_ : 0;
    return 15000.0f / kDenom[i];
}

// ── Map Settings editor ──────────────────────────────────────────────────────
void IofMappingModule::DrawMapSettings() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    ImGui::TextUnformatted("Map Settings");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Scale", kScales[scaleIndex_])) {
        for (int i = 0; i < kScaleCount; ++i)
            if (ImGui::Selectable(kScales[i], i == scaleIndex_)) scaleIndex_ = i;
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputInt("Contour interval (m)", &contourInterval_);
    if (contourInterval_ < 1) contourInterval_ = 1;
    ImGui::Checkbox("Show magnetic-north grid", &showGrid_);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
    ImGui::TextWrapped("Settings are stored in memory for now (not yet in the .acu).");
    ImGui::PopStyleColor();
}

// ── Course Settings editor ───────────────────────────────────────────────────
void IofMappingModule::DrawCourseSettings() {
    auto& ds  = DesignSystem::DesignSystem::Instance();
    ModuleHost* host = Host();
    Renderer::Document* doc = host ? &host->Document() : nullptr;

    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    ImGui::TextUnformatted("Course Settings");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Course"))
        courses_.push_back({ "Course " + std::to_string((int)courses_.size() + 1), {} });

    if (ImGui::BeginTabBar("##courses")) {
        // "All controls" — every placed course object in the document.
        if (ImGui::BeginTabItem("All controls")) {
            activeCourse_ = -1;
            int count = 0;
            if (doc) ForEachCourseObject(*doc, [&](Renderer::Shape& s) {
                ImGui::BulletText("%s", s.name.c_str()); ++count; });
            if (count == 0)
                ImGui::TextDisabled("No course objects yet. Use Shift+A ▸ Course planning.");
            ImGui::EndTabItem();
        }

        for (int ci = 0; ci < (int)courses_.size(); ++ci) {
            IofCourse& course = courses_[(size_t)ci];
            if (!ImGui::BeginTabItem(course.name.c_str())) continue;
            activeCourse_ = ci;

            // Join: add the currently-selected control objects to this course.
            if (ImGui::Button("Add selected controls") && doc) {
                for (uint64_t id : doc->Selection()) {
                    Renderer::Shape* s = doc->FindShape(id);
                    if (s && IsCourseObject(*s) &&
                        std::find(course.controls.begin(), course.controls.end(), id)
                            == course.controls.end())
                        course.controls.push_back(id);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) course.controls.clear();
            ImGui::TextDisabled("Control card — %d leg(s)", (int)course.controls.size());
            ImGui::Separator();

            // The ordered control card: number (editable), reorder, remove. Prune
            // ids whose object was deleted from the document.
            for (int i = 0; i < (int)course.controls.size(); ) {
                uint64_t id = course.controls[(size_t)i];
                Renderer::Shape* s = doc ? doc->FindShape(id) : nullptr;
                if (!s) { course.controls.erase(course.controls.begin() + i); continue; }
                ImGui::PushID(i);
                ImGui::Text("%d.", i + 1);
                ImGui::SameLine();
                // Editable control number (rewrites the object's name).
                if (IsControl(*s)) {
                    int num = std::atoi(s->name.c_str() + 7);
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::InputInt("##num", &num)) {
                        if (num < 0) num = 0;
                        char nm[24]; std::snprintf(nm, sizeof(nm), "Control %d", num);
                        s->name = nm; if (host) host->MarkDirty();
                    }
                } else {
                    ImGui::SetNextItemWidth(70.0f);
                    ImGui::TextUnformatted(s->name.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Up")   && i > 0)
                    std::swap(course.controls[(size_t)i], course.controls[(size_t)i - 1]);
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && i + 1 < (int)course.controls.size())
                    std::swap(course.controls[(size_t)i], course.controls[(size_t)i + 1]);
                ImGui::SameLine();
                bool removed = ImGui::SmallButton("X");
                ImGui::PopID();
                if (removed) { course.controls.erase(course.controls.begin() + i); continue; }
                ++i;
            }

            // Live schematic: numbered nodes + connecting line, auto-fit.
            ImGui::Spacing();
            ImGui::TextDisabled("Course preview");
            ImVec2 area = ImGui::GetContentRegionAvail();
            float h = std::max(80.0f, std::min(area.y, 180.0f));
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + area.x, p0.y + h);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, p1,
                ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Surface_Canvas)), 4.0f);
            // Gather control doc positions; auto-fit their bounding box into the box.
            std::vector<ImVec2> pts;
            if (doc) for (uint64_t id : course.controls)
                if (Renderer::Shape* s = doc->FindShape(id)) pts.push_back(DocCentre(*doc, *s));
            if (pts.size() >= 1) {
                ImVec2 mn = pts[0], mx = pts[0];
                for (ImVec2 q : pts) { mn.x=std::min(mn.x,q.x); mn.y=std::min(mn.y,q.y);
                                       mx.x=std::max(mx.x,q.x); mx.y=std::max(mx.y,q.y); }
                ImVec2 span(std::max(1.0f, mx.x-mn.x), std::max(1.0f, mx.y-mn.y));
                const float pad = 18.0f;
                auto fit = [&](ImVec2 q) {
                    float fx = (q.x - mn.x) / span.x, fy = (q.y - mn.y) / span.y;
                    return ImVec2(p0.x + pad + fx * (area.x - 2*pad),
                                  p0.y + pad + fy * (h - 2*pad));
                };
                ImU32 pc = ImGui::ColorConvertFloat4ToU32(ImVec4(kPurpleR, kPurpleG, kPurpleB, 1.0f));
                ImU32 tc = ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
                for (size_t i = 1; i < pts.size(); ++i)
                    dl->AddLine(fit(pts[i-1]), fit(pts[i]), pc, 2.0f);
                for (size_t i = 0; i < pts.size(); ++i) {
                    ImVec2 q = fit(pts[i]);
                    dl->AddCircle(q, 9.0f, pc, 0, 2.0f);
                    char num[8]; std::snprintf(num, sizeof(num), "%d", (int)i + 1);
                    dl->AddText(ImVec2(q.x + 11.0f, q.y - 7.0f), tc, num);
                }
            } else {
                ImGui::SetCursorScreenPos(ImVec2(p0.x + 10, p0.y + 10));
                ImGui::TextDisabled("Select controls and 'Add selected controls'.");
            }
            ImGui::Dummy(ImVec2(area.x, h));
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ── Viewport overlay: the active course's line over the control objects ──────
void IofMappingModule::DrawViewportOverlay(ImVec2 canvasMin, ImVec2 canvasMax,
                                           const std::function<ImVec2(ImVec2)>& docToScreen) {
    ModuleHost* host = Host();
    if (!host) return;
    Renderer::Document& doc = host->Document();
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImU32 pc = ImGui::ColorConvertFloat4ToU32(ImVec4(kPurpleR, kPurpleG, kPurpleB, 1.0f));
    ImU32 tc = ImGui::ColorConvertFloat4ToU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));

    // Only the active course (a real circuit) draws its connecting line; "All
    // controls" leaves the placed objects to render on their own.
    if (activeCourse_ < 0 || activeCourse_ >= (int)courses_.size()) return;
    const IofCourse& course = courses_[(size_t)activeCourse_];

    ImVec2 prev{}; bool havePrev = false; int leg = 1;
    for (uint64_t id : course.controls) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) continue;
        ImVec2 sp = docToScreen(DocCentre(doc, *s));
        // Clip-ish: skip if far outside the canvas (cheap guard).
        if (sp.x < canvasMin.x - 200 || sp.x > canvasMax.x + 200 ||
            sp.y < canvasMin.y - 200 || sp.y > canvasMax.y + 200) { prev = sp; havePrev = true; continue; }
        if (havePrev) dl->AddLine(prev, sp, pc, 2.0f);
        dl->AddCircle(sp, 13.0f, pc, 0, 2.5f);
        char num[8]; std::snprintf(num, sizeof(num), "%d", leg);
        dl->AddText(ImVec2(sp.x + 15.0f, sp.y - 8.0f), tc, num);
        prev = sp; havePrev = true; ++leg;
    }
}

}  // namespace App::Modules::IofMapping

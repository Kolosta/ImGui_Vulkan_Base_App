#include "IofMappingModule.h"

#include "IofStyles.h"
#include <Ink/Document/Document.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace App::Modules::IofMapping {

namespace {
constexpr const char* kLibName    = "Symbol Library";
constexpr const char* kMapName    = "Map symbols";
constexpr const char* kLayoutName = "Map layout";
constexpr const char* kLayoutSub  = "Layout";
constexpr const char* kExtrasSub  = "Extras";
constexpr const char* kCollIof    = "IOF Cartography";
constexpr const char* kCollAnnot  = "Map annotations";

std::string LibNodeName(int code) { return "SYM " + std::to_string(code); }
}  // namespace

float IofMappingModule::MapScaleFactor() const {
    const int idx = std::clamp(scaleIndex_, 0, kIofScaleCount - 1);
    return 15000.0f / (float)kIofScales[idx];
}

ModuleInfo IofMappingModule::Info() const {
    return { "iof-mapping", "IOF Mapping",
             "Orienteering map authoring (ISOM 2017-2)", "shape-category",
             "0.2.0" };
}

void IofMappingModule::ConfigureCapabilities(Capabilities& caps) const {
    caps.corePrimitivesAddMenu = false;   // the Shift+A menu IS the catalogue
    caps.pages                 = false;   // one map sheet
    caps.editMode              = true;    // symbol curves stay editable
    caps.lockOutlinerTree      = true;    // fixed skeleton (AllowReparent gates)
    caps.documentUnit          = 0;       // Metric — the document displays mm
    caps.colorMode             = 1;       // CMYK workflow, pinned (ISOM inks)
    // mm → base-unit factor at the current map scale (annotation sizing).
    caps.symbolScale           = MapScaleFactor() * 96.0f / 25.4f;
}

std::vector<std::string> IofMappingModule::AllowedEditors() const {
    // Fill / Stroke editors stay available: the non-cartographic layout (title,
    // sponsors, frame…) is authored with ordinary styling. IOF symbols keep
    // their spec style locked, but a free object is fully editable.
    return { "core.viewport", "core.outliner", "core.properties", "core.info",
             "core.fills", "core.strokes", "core.palette", "core.colorusage",
             "core.nodegraph", "iof.symbolviewer", "iof.mapsettings" };
}

LayoutSpec IofMappingModule::BuildLayout() const {
    using L = LayoutSpec;
    // [ Viewport (big) | right column: Outliner over Properties ].
    L right = L::Split(false, 0.45f, L::Leaf("core.outliner"),
                                     L::Leaf("core.properties"));
    return L::Split(true, 0.76f, L::Leaf("core.viewport"), std::move(right));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Document skeleton
// ─────────────────────────────────────────────────────────────────────────────

void IofMappingModule::EnsureStructure() {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc || doc->Pages().empty()) return;
    const Ink::Page& pg = doc->Pages().front();

    // Cheap skip: every cached id still resolves and the doc didn't change.
    auto alive  = [&](std::uint64_t id) { return id && doc->Find(id); };
    auto aliveC = [&](std::uint64_t id) {
        return id && doc->FindCollection(id);
    };
    if (structureVersion_ == doc->Version() && alive(libRoot_) &&
        alive(mapRoot_) && alive(layoutRoot_) && aliveC(collIof_) &&
        aliveC(collAnnot_))
        return;

    // Resolve top-level groups by NAME under the page (stable across saves).
    auto findChildGroup = [&](Ink::NodeId parent,
                              const std::string& name) -> Ink::NodeId {
        const std::vector<Ink::NodeId>* kids = nullptr;
        if (parent == pg.id) kids = &pg.children;
        else if (const Ink::Node* p = doc->Find(parent)) kids = &p->children;
        if (!kids) return Ink::kNullNode;
        for (Ink::NodeId c : *kids)
            if (const Ink::Node* n = doc->Find(c))
                if (n->kind == Ink::NodeKind::Group && n->name == name)
                    return c;
        return Ink::kNullNode;
    };
    auto ensureGroup = [&](Ink::NodeId parent, const std::string& name) {
        Ink::NodeId id = findChildGroup(parent, name);
        if (id == Ink::kNullNode) id = doc->AddGroup(parent, name);
        return id;
    };

    // Painter order (bottom → top): Map symbols first, Map layout above it,
    // the library last (never drawn anyway).
    mapRoot_ = ensureGroup(pg.id, kMapName);
    // Print layers inside, REVERSE print-stack order (enum = top first).
    for (int i = kPrintLayerCount - 1; i >= 0; --i)
        layerGroup_[i] = ensureGroup(mapRoot_, LayerName((PrintLayer)i));
    layoutRoot_  = ensureGroup(pg.id, kLayoutName);
    layoutLayer_ = ensureGroup(layoutRoot_, kLayoutSub);
    extrasLayer_ = ensureGroup(layoutRoot_, kExtrasSub);
    libRoot_     = ensureGroup(pg.id, kLibName);
    if (const Ink::Node* lr = doc->Find(libRoot_); lr && !lr->previewOnly)
        doc->SetPreviewOnly(libRoot_, true);

    // Collections: "Map annotations" (editable, on top) then "IOF Cartography"
    // with one child per element type.
    auto findColl = [&](Ink::NodeId parent,
                        const std::string& name) -> Ink::NodeId {
        for (const Ink::Collection& c : doc->Collections()) {
            if (c.name != name) continue;
            if (parent == Ink::kNullNode) {
                if (!doc->IsChildCollection(c.id)) return c.id;
            } else if (const Ink::Collection* p = doc->FindCollection(parent)) {
                if (std::find(p->childCollections.begin(),
                              p->childCollections.end(),
                              c.id) != p->childCollections.end())
                    return c.id;
            }
        }
        return Ink::kNullNode;
    };
    auto ensureColl = [&](Ink::NodeId parent, const std::string& name) {
        Ink::NodeId id = findColl(parent, name);
        if (id == Ink::kNullNode) id = doc->AddCollection(name, parent);
        return id;
    };
    collAnnot_  = ensureColl(Ink::kNullNode, kCollAnnot);
    collLayout_ = ensureColl(collAnnot_, kLayoutSub);
    collExtras_ = ensureColl(collAnnot_, kExtrasSub);
    collIof_    = ensureColl(Ink::kNullNode, kCollIof);
    // One child collection per ISOM THEME (Landforms, Rock and boulders, …) —
    // the doc-order groups; every placed symbol auto-joins its theme.
    collTheme_.clear();
    for (const IofGroup& g : IofCatalogue())
        collTheme_[g.name] = ensureColl(collIof_, g.name);
    doc->ReorderCollection(collAnnot_, 0);   // editable set on top

    // Library specimens by code.
    libByCode_.clear();
    if (const Ink::Node* lr = doc->Find(libRoot_))
        for (Ink::NodeId c : lr->children)
            if (const Ink::Node* n = doc->Find(c))
                if (n->name.rfind("SYM ", 0) == 0)
                    libByCode_[std::atoi(n->name.c_str() + 4)] = c;

    structureVersion_ = doc->Version();
}

void IofMappingModule::SeedPalette() {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc) return;
    // Adopt by NAME first: a reopened file already carries the table, and the
    // ids in its paints must keep resolving.
    for (int i = 0; i < kPrintLayerCount; ++i) swatchByLayer_[i] = 0;
    for (const Ink::Swatch& sw : doc->Swatches())
        for (int i = 0; i < kPrintLayerCount; ++i)
            if (sw.name == LayerName((PrintLayer)i)) swatchByLayer_[i] = sw.id;

    const Ink::PrintTechnique tech = doc->PrintTech();
    for (int i = 0; i < kPrintLayerCount; ++i) {
        const PrintLayer pl = (PrintLayer)i;
        const PrintLayerDef& d = LayerDef(pl);
        Ink::Swatch sw;
        sw.name    = d.name;
        sw.display = LayerInkColor(pl);
        sw.ink     = { (double)d.c, (double)d.m, (double)d.y, (double)d.k };
        // The table runs TOP FIRST (index 0 = upper purple, the topmost plate),
        // whereas printOrder counts from the BOTTOM — lowest is laid down first
        // and ends up underneath. So the index has to be flipped.
        sw.hasPrintOrder = true;
        sw.printOrder    = kPrintLayerCount - 1 - i;
        // Course overprint (specification §5). UPPER purple is the course
        // itself and always stays at the very top of the stack. LOWER purple is
        // the one that moves: on an offset press it is a genuine extra pass
        // over the finished map, so it sits just under the upper purple; in
        // plain CMYK that pass does not exist, and the specification says to
        // SIMULATE the effect by dropping it BELOW the black, brown and blue
        // 100 % colours, so those stay legible through the course.
        if (pl == PrintLayer::Brown100Line || pl == PrintLayer::Brown100Point) {
            sw.hasSpot = true;
            sw.spotName = "PMS 471";
            sw.spotDisplay = LayerInkColor(pl);
        } else if (pl == PrintLayer::UpperPurple ||
                   pl == PrintLayer::LowerPurple) {
            sw.hasSpot = true;
            sw.spotName = "PMS Purple";
            sw.spotDisplay = LayerInkColor(pl);
        }
        // The course overprint is exactly that: the three plates printed over
        // the finished map let it show through instead of knocking it out.
        sw.overprint = pl == PrintLayer::UpperPurple ||
                       pl == PrintLayer::LowerPurple ||
                       pl == PrintLayer::WhiteOverprint;
        if (swatchByLayer_[i]) {
            sw.id = swatchByLayer_[i];
            // Keep whatever overprint the map author set; everything else is
            // re-asserted from the spec.
            if (const Ink::Swatch* old = doc->FindSwatch(sw.id))
                sw.overprint = old->overprint;
            doc->SetSwatch(sw.id, sw);
        } else {
            swatchByLayer_[i] = doc->AddSwatch(sw);
        }
    }
}

namespace {
// ISOM splits several inks across three plates of the SAME colour — blue point,
// blue line and blue area are all Process Blue. Colour alone therefore cannot
// say which plate a paint belongs on: what decides is what the paint IS. A fill
// goes on an area plate, a stroke or a line-set on a line plate, a point symbol
// on a point plate.
enum class PlateKind { Point, Line, Area, Generic };

PlateKind KindOfPlate(PrintLayer l) {
    switch (l) {
        case PrintLayer::Blue100Point:
        case PrintLayer::Brown100Point:
        case PrintLayer::Green100Point:   return PlateKind::Point;
        case PrintLayer::Blue100Line:
        case PrintLayer::Brown100Line:
        case PrintLayer::DarkGreenLine:   return PlateKind::Line;
        case PrintLayer::Blue100Area:
        case PrintLayer::Blue70Area:
        case PrintLayer::Blue50Area:
        case PrintLayer::Green100Area:
        case PrintLayer::Green60Area:
        case PrintLayer::Green30Area:
        case PrintLayer::Black30Area:
        case PrintLayer::Yellow100Area:
        case PrintLayer::Yellow75Area:
        case PrintLayer::Yellow50Area:    return PlateKind::Area;
        default:                          return PlateKind::Generic;
    }
}
}  // namespace

void IofMappingModule::BindSwatches(Ink::Style& style, IofType type) const {
    const Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc) return;
    // Quantise before comparing: the two tables compute the same tint through
    // the same constants, but nothing guarantees bit-identical floats.
    auto key = [](const Ink::Color& c) {
        auto q = [](float v) { return (long long)std::lround(v * 4096.0f); };
        return std::to_string(q(c.r)) + ',' + std::to_string(q(c.g)) + ',' +
               std::to_string(q(c.b));
    };
    // Per colour, the candidate plate of each kind.
    struct Cand { std::uint64_t byKind[4] = { 0, 0, 0, 0 }; };
    std::unordered_map<std::string, Cand> byColor;
    for (int i = 0; i < kPrintLayerCount; ++i) {
        if (!swatchByLayer_[i]) continue;
        const PrintLayer pl = (PrintLayer)i;
        Cand& cd = byColor[key(LayerInkColor(pl))];
        std::uint64_t& slot = cd.byKind[(int)KindOfPlate(pl)];
        if (!slot) slot = swatchByLayer_[i];   // topmost of that kind wins
    }
    // A POINT symbol puts everything on the point plates; an area or line
    // symbol splits fills onto area plates and strokes onto line plates.
    const PlateKind fillKind = type == IofType::Point ? PlateKind::Point
                                                      : PlateKind::Area;
    const PlateKind lineKind = type == IofType::Point ? PlateKind::Point
                                                      : PlateKind::Line;
    auto bind = [&](const Ink::Color& c, std::uint64_t& out, PlateKind want) {
        // An explicit plate hint wins: the symbol asked for THIS plate because
        // its colour cannot name it (white, K100, purple all repeat). Resolve
        // straight to that plate — or clear the hint if it was never seeded, so
        // the sentinel never leaks (the paint then falls back to its literal
        // colour, which is the right one anyway).
        PrintLayer hinted;
        if (IofDecodePlateHint(out, hinted)) { out = LayerSwatch(hinted); return; }
        auto it = byColor.find(key(c));
        if (it == byColor.end()) return;
        const Cand& cd = it->second;
        std::uint64_t id = cd.byKind[(int)want];
        if (!id) id = cd.byKind[(int)PlateKind::Generic];
        // Last resort: any plate carrying this ink (white, black, purple…).
        if (!id) for (std::uint64_t v : cd.byKind) if (v) { id = v; break; }
        if (id) out = id;
    };
    for (Ink::Fill& f : style.fills) {
        // A piece that ERASES is a mask, not paint: it lays no ink, so it has
        // no plate and must never turn up as a colour user or on a separation.
        if (f.blend == Ink::BlendMode::Erase) {
            f.paint.swatch = Ink::kNullSwatch;
            for (Ink::InstElement& e : f.instanced.elements) e.swatch = Ink::kNullSwatch;
            for (Ink::InstLineSet& l : f.instanced.lines)    l.swatch = Ink::kNullSwatch;
            continue;
        }
        bind(f.paint.color, f.paint.swatch, fillKind);
        // A pattern's shapes and line-sets are drawn INSIDE an area, but they
        // are point-like and line-like marks: they take those plates.
        for (Ink::InstElement& e : f.instanced.elements)
            bind(e.color, e.swatch, PlateKind::Point);
        for (Ink::InstLineSet& l : f.instanced.lines)
            bind(l.color, l.swatch, lineKind);
    }
    for (Ink::Stroke& s : style.strokes) {
        if (s.blend == Ink::BlendMode::Erase) {   // a mask carries no ink
            s.paint.swatch = Ink::kNullSwatch;
            continue;
        }
        bind(s.paint.color, s.paint.swatch, lineKind);
        for (Ink::StrokeRepeat& rp : s.repeats) bind(rp.color, rp.swatch, lineKind);
        for (Ink::StrokeMark& m : s.marks)
            for (Ink::MarkObject& o : m.objects) bind(o.color, o.swatch, lineKind);
    }
}

std::uint64_t IofMappingModule::LayerSwatch(PrintLayer layer) const {
    const int i = (int)layer;
    if (i < 0 || i >= kPrintLayerCount) return 0;
    return swatchByLayer_[i];
}

void IofMappingModule::SeedLibrary(bool rebuildExisting) {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc || !libRoot_ || !doc->Find(libRoot_)) return;
    const float sf = MapScaleFactor();

    for (const IofGroup& g : IofCatalogue()) {
        for (const IofElement& e : g.elements) {
            auto it = libByCode_.find(e.code);
            Ink::NodeId sym = it != libByCode_.end() ? it->second
                                                     : Ink::kNullNode;
            if (sym != Ink::kNullNode && !rebuildExisting) continue;
            SymbolDef def = BuildSymbol(e, sf);
            if (def.parts.empty()) continue;
            if (sym == Ink::kNullNode) {
                sym = doc->AddGroup(libRoot_, LibNodeName(e.code));
                libByCode_[e.code] = sym;
            } else if (const Ink::Node* sn = doc->Find(sym)) {
                // Replace the parts in place — instance targets reference the
                // GROUP, so existing placements re-skin live.
                std::vector<Ink::NodeId> old = sn->children;
                for (Ink::NodeId c : old) doc->Remove(c);
            }
            for (SymbolPart& p : def.parts) {
                // Each part follows the PLATE its ink belongs to, so a symbol
                // that spans several separations stays separable.
                BindSwatches(p.style, e.type);
                doc->AddPath(sym, std::move(p.path), std::move(p.style),
                             p.name);
            }
        }
    }
    structureVersion_ = doc->Version();
}

void IofMappingModule::OnDocumentCreated(Ink::Document& doc) {
    (void)doc;
    EnsureStructure();
    SeedPalette();
    // Always REBUILD: a library specimen is derived purely from the compiled
    // catalogue + the map scale, never edited by the user. Keeping an existing
    // one would pin the vignettes (and every placed instance, which targets the
    // same group) to whatever definition was current when the file was made.
    SeedLibrary(/*rebuildExisting=*/true);
}

void IofMappingModule::OnActivate() {
    // Register the six ISOM THEME tools (Object-mode tools; Host is bound now,
    // unlike OnRegister). RegisterTool is keyed by id → idempotent on re-open.
    if (Host())
        for (int i = 0; i < 6; ++i)
            Host()->RegisterTool("iof.theme" + std::to_string(i),
                                 "IOF symbol", "shape-category");
    // A LOADED file already carries the skeleton + library; a fresh module open
    // seeded it in OnDocumentCreated. Either way the specimens are RE-BUILT from
    // the current catalogue, so vignettes and placements always show the real,
    // up-to-date symbol definition rather than the one baked into the file.
    EnsureStructure();
    SeedPalette();
    SeedLibrary(/*rebuildExisting=*/true);
    armedTheme_ = -1; armedSymbol_ = -1;
}

void IofMappingModule::OnDeactivate() {
    libByCode_.clear();
    structureVersion_ = ~0ull;
}

void IofMappingModule::OnFrameSync() {
    // Keep the skeleton resolved/healed (cheap when nothing changed); the
    // structure is the z-order, so no per-frame sort is needed.
    EnsureStructure();
    SyncThemeTool();
}

// The six theme tool ids (registered in OnRegister, offered in Object mode).
void IofMappingModule::ObjectTools(std::vector<std::string>& out) {
    for (int i = 0; i < 6; ++i)
        out.push_back("iof.theme" + std::to_string(i));
}

void IofMappingModule::OnNumpadSequence(const std::string& digits) {
    if (digits.empty()) return;
    // A single 1..6 → the matching theme tool.
    if (digits.size() == 1) {
        const int d = digits[0] - '0';
        if (d >= 1 && d <= 6) { ActivateThemeTool(d - 1); }
        return;
    }
    // Two+ digits → an ISOM code: 108 → my ×10 code 1080; a 4-digit like 1051
    // (105.1) matches directly.
    const int n = std::atoi(digits.c_str());
    const IofElement* e = IofFindByCode(n);
    if (!e) e = IofFindByCode(n * 10);
    if (!e) return;
    const int idx = IofThemeButtonIndex(e->code);
    if (idx >= 0) {
        themeSel_[idx] = e->code;   // reflect on the theme tool + make it active
        ActivateThemeTool(idx);
    }
}

void IofMappingModule::ActivateThemeTool(int idx) {
    if (!Host() || idx < 0 || idx >= 6) return;
    Host()->ActivateTool("iof.theme" + std::to_string(idx));
    // Arm immediately (SyncThemeTool would do it next frame; do it now so the
    // first click acts without a one-frame gap).
    armedTheme_ = idx;
    armedSymbol_ = themeSel_[idx];
    if (const IofElement* e = IofFindByCode(themeSel_[idx])) SelectSymbol(*e);
}

void IofMappingModule::SyncThemeTool() {
    if (!Host()) return;
    const std::string active = Host()->ActiveTool();
    int want = -1;
    if (active.rfind("iof.theme", 0) == 0 && active.size() == 10) {
        const char c = active[9];
        if (c >= '0' && c <= '5') want = c - '0';
    }
    if (want < 0) {
        // A non-theme tool is active — drop any module arming ONCE.
        if (armedTheme_ != -1) {
            armedTheme_ = -1; armedSymbol_ = -1;
            Host()->CancelPlacement();
            Host()->EndSymbolDraw();
        }
        return;
    }
    const IofElement* e = IofFindByCode(themeSel_[want]);
    if (!e) return;
    // (Re)arm when the tool or its symbol changed …
    if (armedTheme_ != want || armedSymbol_ != themeSel_[want]) {
        armedTheme_ = want; armedSymbol_ = themeSel_[want];
        SelectSymbol(*e);
        return;
    }
    // … or when the gesture lapsed (Esc'd draw / cancelled placement) so the
    // active tool keeps producing symbols (a hot creation tool, Blender-style).
    const bool live = (e->type == IofType::Point) ? Host()->IsPlacementArmed()
                                                  : Host()->IsSymbolDrawing();
    if (!live) SelectSymbol(*e);
}

std::uint64_t IofMappingModule::LibNode(int code) const {
    auto it = libByCode_.find(code);
    return it != libByCode_.end() ? it->second : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner policy
// ─────────────────────────────────────────────────────────────────────────────

bool IofMappingModule::AllowReparent(uint64_t shapeId, uint64_t target) {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc) return false;
    // Collections never restructure (fixed tree).
    if (doc->FindCollection(shapeId)) return false;
    // Module-owned nodes (placed symbols, skeleton groups) never move.
    if (const Ink::Node* n = doc->Find(shapeId)) {
        if (n->propLocks & Ink::PropLockManaged) return false;
        if (n->kind == Ink::NodeKind::Group &&
            (shapeId == mapRoot_ || shapeId == layoutRoot_ ||
             shapeId == libRoot_ || shapeId == layoutLayer_ ||
             shapeId == extrasLayer_))
            return false;
        for (int i = 0; i < kPrintLayerCount; ++i)
            if (shapeId == layerGroup_[i]) return false;
    }
    // Free annotations may move ONLY between the editable containers.
    return target == layoutLayer_ || target == extrasLayer_ ||
           target == collLayout_ || target == collExtras_ ||
           target == collAnnot_;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Symbol tools (place / draw)
// ─────────────────────────────────────────────────────────────────────────────

void IofMappingModule::SelectSymbol(const IofElement& e) {
    if (!Host()) return;
    EnsureStructure();
    const std::uint64_t lib = LibNode(e.code);
    if (e.type == IofType::Point) {
        PlacementRequest req;
        req.iconNode = lib;
        req.repeat   = true;
        req.onPlace  = [this, &e](double x, double y) {
            PlacePointSymbol(e, x, y);
        };
        Host()->ArmPlacement(req);
        Host()->LogInfoAction("Place symbol: " + IofElementLabel(e) +
                              " (click to place, Esc to finish)");
    } else {
        const SymbolDef def = BuildSymbol(e, MapScaleFactor());
        SymbolDrawRequest req;
        req.penKind  = e.type == IofType::Area ? "free" : "curve";
        Ink::Style style = e.type == IofType::Area ? def.areaStyle
                                                   : def.lineStyle;
        BindSwatches(style, e.type);
        req.style    = &style;
        req.iconNode = lib;
        req.onCommit = [this, &e](std::uint64_t node) {
            RouteDrawnSymbol(e, node);
        };
        Host()->BeginSymbolDraw(req);
        Host()->LogInfoAction("Draw symbol: " + IofElementLabel(e));
    }
}

void IofMappingModule::PlacePointSymbol(const IofElement& e, double docX,
                                        double docY) {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc || doc->Pages().empty()) return;
    EnsureStructure();
    const std::uint64_t lib = LibNode(e.code);
    const std::uint64_t parent = layerGroup_[(int)e.layer];
    if (!lib || !parent) return;

    const Ink::NodeId inst = doc->AddInstance(parent, lib, e.name);
    if (inst == Ink::kNullNode) return;
    const Ink::Page& pg = doc->Pages().front();
    Ink::Transform2D t;
    t.tx = docX - pg.pos.x;
    t.ty = docY - pg.pos.y;
    doc->SetTransform(inst, t);
    // ISOM-fixed channels: size always; orientation when north-locked; the
    // style lives on the library target (spec colours) — locked either way.
    std::uint32_t locks = Ink::PropLockScale | Ink::PropLockDimensions |
                          Ink::PropLockStyle | Ink::PropLockManaged;
    if (e.northLocked) locks |= Ink::PropLockRotation;
    doc->SetPropLocks(inst, locks);
    if (auto it = collTheme_.find(IofGroupOf(e.code)); it != collTheme_.end())
        doc->AddToCollection(it->second, inst);

    const auto snap = doc->CopySubtree(inst);
    Host()->PushDocCommand("Place Symbol",
        [inst](Ink::Document& d) { d.Remove(inst); },
        [snap](Ink::Document& d) { d.RestoreSubtree(snap); });
    Host()->MarkDirty();
}

void IofMappingModule::RouteDrawnSymbol(const IofElement& e,
                                        std::uint64_t node) {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc || !doc->Find(node)) return;
    EnsureStructure();
    const std::uint64_t parent = layerGroup_[(int)e.layer];
    if (parent) doc->MoveTo(node, parent, -1);
    doc->SetName(node, e.name);
    // The geometry stays editable (curves ARE the feature); the STYLE is the
    // specification — locked and module-managed.
    doc->SetPropLocks(node, Ink::PropLockStyle | Ink::PropLockManaged);
    if (auto it = collTheme_.find(IofGroupOf(e.code)); it != collTheme_.end())
        doc->AddToCollection(it->second, node);
}

void IofMappingModule::SelectLayoutTool(const char* kind) {
    if (!Host()) return;
    EnsureStructure();
    SymbolDrawRequest req;
    req.penKind = std::strcmp(kind, "area") == 0 ? "free" : "curve";
    // A plain black outline — fully editable in the Fill / Stroke editors.
    static Ink::Style layoutStyle =
        Ink::Style::Stroked({ 0, 0, 0, 1 }, 3.0);
    req.style    = &layoutStyle;
    req.iconNode = 0;
    req.onCommit = [this](std::uint64_t node) { RouteLayoutObject(node); };
    Host()->BeginSymbolDraw(req);
    Host()->LogInfoAction("Draw layout object");
}

void IofMappingModule::RouteLayoutObject(std::uint64_t node) {
    Ink::Document* doc = Host() ? Host()->Document() : nullptr;
    if (!doc || !doc->Find(node)) return;
    EnsureStructure();
    if (extrasLayer_) doc->MoveTo(node, extrasLayer_, -1);
    doc->SetName(node, "Layout object");
    if (collExtras_) doc->AddToCollection(collExtras_, node);
    // No property locks — the layout is the user's to edit freely.
}

}  // namespace App::Modules::IofMapping

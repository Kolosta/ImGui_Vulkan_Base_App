#pragma once

#include <string>
#include <vector>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  ISOM 2017-2 (Revision 6) element catalogue — exact data.
//
//  Each symbol carries its real ISOM code (×10 so 105.1 → 1051, 203.2 → 2032,
//  101 → 1010), a name, a TYPE (Point/Line/Area/Text), a print LAYER (ISOM
//  colour + Outliner collection), a north-orientation flag, a short description,
//  and a GLYPH KIND that tells the glyph builder which exact vector shape to bake
//  (with the ISOM mm dimensions, drawn at the 1:15 000 base scale × the map
//  scale factor). Colours come from the ISOM CMYK definitions (LayerColor).
//
//  All dimensions used by the builder are in millimetres at 1:15 000 — the doc
//  unit for the IOF module is the millimetre.
// ─────────────────────────────────────────────────────────────────────────────

// Symbol type, per the ISOM "Type of symbols" key.
enum class IofType { Point, Line, Area, Text };

// The six ISOM SPOT colours a symbol draws in. This is the *ink* of the glyph,
// used by the glyph builder (LayerColor / ScreenColor); it is NOT the print
// layer (see PrintLayer below). A spot colour is shared by many print layers
// (e.g. Brown is split into "Brown 100% point", "Brown 100% line", "Brown 50%
// for road infill"…), so the two are kept separate.
enum class SpotColor { Purple, Black, Blue, Brown, Green, Yellow };

// ─────────────────────────────────────────────────────────────────────────────
//  ISOM 2017-2 PRINT LAYERS — the exact colour-printing stack, in printing
//  order. This is the official "Printing and colour definitions" colour table
//  (ISOM 2017-2 column), one entry per separated colour. The TOP of the stack
//  (printed LAST, drawn on top) comes first, so the enum order == the Outliner
//  collection order (top entry shows at the top of the print-layer list).
//
//  Each symbol carries ONE of these as its exact layer. A symbol's layer is the
//  refinement of its spot colour by its TYPE and screen %: e.g. a brown LINE →
//  Brown100Line, a brown POINT → Brown100Point, "Rough open" (yellow 50% area)
//  → Yellow50Area. The concrete CMYK + render RGB live in LayerDef().
// ─────────────────────────────────────────────────────────────────────────────
enum class PrintLayer {
    UpperPurple,          // Upper purple for course overprint  (C35 M85)
    WhiteOverprint,       // White for course overprint         (0)
    Black100,             // Black 100%                         (K100)
    Blue100Point,         // Blue 100% point symbols            (C100)
    Brown100Point,        // Brown 100% point symbols           (M56 Y100 K18)
    Green100Point,        // Green 100% point symbols           (C76 Y91)
    Blue100Line,          // Blue 100% line symbols             (C100)
    DarkGreenLine,        // Dark green line symbols            (C100 Y80 K30)
    Brown100Line,         // Brown 100% line symbols            (M56 Y100 K18)
    LowerPurple,          // Lower purple for course overprint  (C35 M85)
    Brown50RoadInfill,    // Brown 50% for road infill          (M28 Y50 K9)
    Black100RoadOutline,  // Black 100% for road outline        (K100)
    Black50,              // Black 50% for large buildings/tram (K50)
    Black20Canopy,        // Black 20% for canopy               (K20)
    Blue100Area,          // Blue 100% area symbols             (C100)
    Blue70Area,           // Blue 70% area symbols              (C70)
    Blue50Area,           // Blue 50% area symbols              (C50)
    WhiteOverGreenBrown,  // White over green and brown         (0)
    Brown50PavedArea,     // Brown 50% for paved area           (M28 Y50 K9)
    Yellow100Green50,     // Yellow 100% + Green 50%            (C38 M27 Y100)
    Green100Area,         // Green 100% area symbols            (C76 Y91)
    Green60Area,          // Green 60% area symbols              (C46 Y55)
    Green30Area,          // Green 30% area symbols             (C24 Y27)
    Black30Area,          // Black 30% area symbols             (K30)
    WhiteOverYellow,      // White over Yellow                  (0)
    BlackCultivated,      // Black for cultivated land / sand   (K100)
    Yellow100Area,        // Yellow 100% area symbols           (M27 Y79)
    Yellow75Area,         // Yellow 75% area symbols            (M20 Y59)
    Yellow50Area,         // Yellow 50% area symbols            (M14 Y40)
    Count                 // sentinel — number of print layers
};
constexpr int kPrintLayerCount = (int)PrintLayer::Count;

struct IofRgb { float r, g, b; };

// CMYK / name / render-colour definition of one print layer (the colour table).
struct PrintLayerDef {
    PrintLayer  layer;
    const char* name;     // exact table name (Outliner collection name)
    int         c, m, y, k;   // CMYK percent (the official ISOM 2017-2 values)
    IofRgb      rgb;      // accepted on-screen sRGB for this separation
};

// The print-layer table, in printing order (index == (int)PrintLayer).
const PrintLayerDef& LayerDef(PrintLayer layer);
// Outliner collection name of a print layer (the exact table name).
const char* LayerName(PrintLayer layer);
// On-screen colour of a print layer (its separation's accepted sRGB).
IofRgb      LayerRenderColor(PrintLayer layer);

// Straight RGB for a SPOT colour (the glyph ink), from the ISOM CMYK base.
IofRgb      LayerColor(SpotColor color);
// Opaque "screen %" tint of a spot colour over white paper (e.g. Brown 50%,
// Black 50%) — a solid named colour, NOT an alpha blend.
IofRgb      ScreenColor(SpotColor color, float pct);

// Pick the exact print layer for a symbol from its spot colour, type and screen
// % — the table-lookup rule the catalogue uses (e.g. brown line → Brown100Line).
// `pct` is the screen percentage (1.0 = full tone). Used to author the catalogue.
PrintLayer  ResolvePrintLayer(SpotColor color, IofType type, float pct);

// Which exact glyph the builder bakes. Each kind hard-codes its ISOM mm cotes
// (widths, gaps, diameters, tag lengths, angles) in IofGlyph.cpp. Generic kinds
// (GenericPoint/Line/Area) are the fallback for symbols whose precise glyph is
// not yet modelled — they draw a clean point/line/area in the right colour.
enum class IofGlyphKind {
    // ── generic fallbacks ──
    GenericPoint, GenericLine, GenericArea,
    // ── point glyphs (P) ──
    KnollDot,            // 109 small knoll — filled dot ø0.5
    ElongatedKnoll,      // 110 — two dots / short bar
    SmallDepression,     // 111 — half-circle (arc) opening up, ø0.8 / 0.18
    Pit,                 // 112 — V opening UP (apex down), 0.7 wide / 0.18
    RockyPit,            // 203.1 — V opening up (rotatable), 0.7 / 0.16
    DangerousPit,        // 203.2 — filled dot in ring
    ProminentLandform,   // 115 — brown triangle outline 0.9
    BoulderDot,          // 204 — filled dot ø0.4
    LargeBoulderDot,     // 205 — filled dot ø0.6
    BoulderTriangle,     // 207 — filled triangle (north)
    Waterhole,           // 303 — blue V opening UP, 0.7 / 0.18
    Well,                // 311 — blue square outline 0.8 / 0.18
    Spring,              // 312 — blue half-circle (arc) + downstream tail
    ProminentWater,      // 313 — blue 5-arm asterisk cross (72°)
    KnollGreen,          // 418 — green filled dot ø0.6 w/ white centre
    LargeTree,           // 417 — green ring ø0.9/0.18 w/ white mask
    VegFeatureX,         // 419 — green X 0.9 w/ white mask (north)
    Cairn,               // 526 — ring ø0.8/0.14 w/ centre dot
    Tower,               // 524 — filled dot ø0.8 + cross arms (north)
    SmallTower,          // 525 — T over vertical base (north)
    FodderRack,          // 527 — Y (roof up) over vertical post (north)
    FeatureRing,         // 530 — ring ø0.8 / 0.16
    FeatureX,            // 531 — X 0.8 (north)
    SpotHeight,          // 603 — small dot ø0.3 (+ text handled separately)
    RegistrationMark,    // 602 — large + cross (4 mm, 0.1)
    MapIssuePoint,       // 702 — purple triangle outline (upper purple)
    FirstAid,            // 712 — purple filled plus
    Refreshment,         // 713 — purple cup outline
    ContinuingPoint,     // 715 — purple triangle outline ø5
    // ── line glyphs (L) — ONE editable styled curve, pattern via StrokeStyle ──
    Contour,             // 101 — plain 0.14 brown
    IndexContour,        // 102 — plain 0.25 brown
    FormLine,            // 103 — dashed brown 0.14 (2.0 dash / 0.2 gap)
    EarthBank,           // 104 — line 0.25 + downhill tags (one side)
    EarthWall,           // 105.1 — line 0.18 + dots straddling (ø0.45 @ 2.0)
    RetainingEarthWall,  // 105.2 — line 0.18 + half-dots on one side
    RuinedEarthWall,     // 106 — dashed line 0.18 + dots in gaps
    ErosionGully,        // 107 — solid line tapering to a point at the end
    SmallErosionGully,   // 108 — dotted brown ø0.25 @ 0.45
    ImpassableCliff,     // 201 — thick line 0.35 + downhill tags 0.12
    Cliff,               // 202 — line 0.25 + short downhill tags 0.12
    Trench,              // 215 — two parallel 0.10 lines, 0.10 apart
    Watercourse,         // 304 — plain blue 0.30
    SmallWatercourse,    // 305 — plain blue 0.18
    SeasonalChannel,     // 306 — dashed blue 0.18 (1.25 / 0.25)
    NarrowMarsh,         // 309 — dotted blue ø0.25 @ 0.45
    Wall,                // 513.1 — line 0.14 + dots straddling (ø0.4 @ 2.0)
    RetainingWall,       // 513.2 — line 0.14 + half-dots on one side
    RuinedWall,          // 514 — dashed line 0.14 + dots in gaps
    ImpassableWall,      // 515 — line 0.14 + large dots straddling (ø0.6 @ 3.0)
    Road,                // 503 — plain black 0.35
    WideRoad,            // 502 — two black 0.14 edges + brown 50% infill (0.3)
    VehicleTrack,        // 504 — dashed black 0.35 (3.0 / 0.25)
    Footpath,            // 505 — dashed black 0.25 (2.0 / 0.25)
    SmallPath,           // 506 — dashed black 0.18 (1.0 / 0.25)
    LessDistinctPath,    // 507 — double-dash black 0.18 (1.0 dash, 0.25/0.8 gaps)
    NarrowRide,          // 508 — dashed black 0.14 (2.0 / 0.25)
    Railway,             // 509 — twin rails + cross-ties (black)
    PowerLine,           // 510 — line 0.14 + pylon bars (5.0 spacing)
    MajorPowerLine,      // 511 — double line + pylon bars
    Fence,               // 516 — line 0.14 + oblique pickets (60°) one side
    RuinedFence,         // 517 — dashed line 0.14 + pickets in gaps
    ImpassableFence,     // 518 — double line + oblique pickets (60°)
    ProminentLineFeature,    // 528 — line 0.14 + small arrow ticks (45°)
    ProminentUncrossableLine,// 529 — double line + arrow ticks (45°)
    DistinctBoundary,    // 415 — plain thin black 0.10
    VegBoundaryDots,     // 416 — dotted black ø0.22 @ 0.45
    VegBoundaryGreen,    // 416 — dark-green dashed 0.14 (alt implementation)
    MagneticNorth,       // 601 — plain vertical line 0.10, with arrowhead (north)
    Stairway,            // 532 — two rails + perpendicular rungs (not a curve)
    CourseLine,          // 705 — plain purple 0.35
    MarkedRoute,         // 707 — dashed purple (upper)
    OOBBoundary,         // 708 — solid purple line 0.7 + ticks
    OOBRoute,            // 711 — purple line of × symbols
    // ── two-part / special non-curve symbols ──
    BridgeTunnel,        // 512 — two short curved-out brackets (over a crossing)
    CrossingPointFence,  // 519 — gap bracket inserted into a line
    // ── area glyphs (A) ──
    WaterArea,           // 301 — blue fill + black outline 0.12
    ShallowWater,        // 302 — blue 50% + blue outline
    MarshArea,           // 307/308/310 — blue horizontal hatch
    OpenLand,            // 401 — yellow fill
    OpenLandDots,        // 402/404 — yellow + scattered dots (white holes)
    RoughOpen,           // 403 — yellow 50%
    ForestWhite,         // 405 — white (outline only)
    VegGreen1,           // 406 — green 30%
    VegGreen2,           // 408 — green 60%
    VegGreen3,           // 410 — green 100%
    VegStripes,          // 407/409 — green vertical stripes (good visibility)
    Vineyard,            // 414 — yellow 50% + vertical rows (green)
    Orchard,             // 413 — yellow 50% + green dot rows
    SandyGround,         // 213 — yellow 50% + black dots
    StonyGround,         // 210/211/212/113/114 — dots (black/brown)
    BoulderField,        // 208/209 — black triangles 8:6:5
    BareRock,            // 214 — grey (black 35%)
    Building,            // 521/206 — black fill
    GiganticBoulder,     // 206 — black filled pentagon-ish
    PavedArea,           // 501 — brown 50% + black outline
    Canopy,              // 522 — black 20% + outline
    Ruin,                // 523 — small black square outline (L)
    OutOfBounds,         // 520/709/7090 — olive / purple cross-hatch
    CultivatedLand,      // 412 — yellow + black dot grid
    // ── course / overprint glyphs ──
    Start,               // 701 — triangle ø6 (north / to first control)
    Control,             // 703 — circle ø5 / 0.35
    Finish,              // 706 — double circle ø4 / ø6
};

struct IofElement {
    int          code;        // ISOM code ×10 (105.1→1051, 101→1010)
    const char*  name;
    IofType      type;
    SpotColor    color;       // glyph INK (drives LayerColor / ScreenColor)
    PrintLayer   layer;       // exact PRINT LAYER → Outliner collection + order
    IofGlyphKind glyph;       // exact glyph the builder bakes
    bool         northLocked; // oriented to north → rotation disabled
    const char*  desc;
};

struct IofGroup {
    const char*             name;      // theme heading (relief / rock / water / …)
    std::vector<IofElement> elements;
};

// The catalogue, in canonical group order (relief → rock → water → vegetation →
// man-made → technical → course).
const std::vector<IofGroup>& IofCatalogue();

// Find an element by its ×10 code (nullptr if unknown).
const IofElement* IofFindByCode(int code);

// "<code>  <name>" label for menus / lists (renders 1051 as "105.1").
std::string IofElementLabel(const IofElement& e);

// The FULL symbol description (extracted from the official ISOM 2017-2 text), for
// the Symbol Viewer's right-hand panel. Empty string if not authored yet (the
// viewer then shows the short `desc` only). Lookup by ×10 code.
const char* IofFullDescription(int code);

}  // namespace App::Modules::IofMapping

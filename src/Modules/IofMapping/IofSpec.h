#pragma once

#include <string>
#include <vector>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  ISOM 2017-2 element catalogue — engine-agnostic DATA (codes, names, types,
//  print layers, colours, orientation locks, descriptions).
//
//  Every symbol carries its real ISOM code ×10 (105.1 → 1051, 101 → 1010), a
//  TYPE, the SPOT COLOUR its ink draws in, its exact PRINT LAYER (the official
//  colour-separation stack → the Outliner layer it lives in), a north lock and
//  a short description. Symbol dimensions are in MILLIMETRES at the 1:15 000
//  base scale; the style builder (IofStyles) multiplies them by the map-scale
//  factor. The document unit for the IOF module is the millimetre.
// ─────────────────────────────────────────────────────────────────────────────

// Symbol type, per the ISOM "Type of symbols" key.
enum class IofType { Point, Line, Area, Text };

// The six ISOM spot colours a symbol's ink draws in (NOT the print layer — a
// spot colour is shared by several separations). On-screen sRGB uses the
// official PMS spot equivalents (Process Blue, PMS 361/471/136/Purple), not a
// naive process conversion — so the blue/green/brown look like a printed map.
enum class SpotColor { Purple, Black, Blue, Brown, Green, Yellow };

// ── ISOM 2017-2 PRINT LAYERS — the exact colour-printing stack, in printing
// order: the TOP of the stack (printed last, drawn on top) comes FIRST, so the
// enum order is the top-down Outliner order. The painter (bottom→top) order of
// the document layer tree is therefore the REVERSE enum order.
enum class PrintLayer {
    UpperPurple,          // Upper purple for course overprint  (C35 M85)
    WhiteOverprint,       // White for course overprint         (0)
    WhiteRailway,         // White for railway                  (0)
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
    Black50,              // Black 50% for large buildings      (K50)
    Black20Canopy,        // Black 20% for canopy               (K20)
    Blue100Area,          // Blue 100% area symbols             (C100)
    Blue70Area,           // Blue 70% area symbols              (C70)
    Blue50Area,           // Blue 50% area symbols              (C50)
    WhiteOverGreenBrown,  // White over green and brown         (0)
    Brown50PavedArea,     // Brown 50% for paved area           (M28 Y50 K9)
    Yellow100Green50,     // Yellow 100% + Green 50%            (C38 M27 Y100)
    Green100Area,         // Green 100% area symbols            (C76 Y91)
    Green60Area,          // Green 60% area symbols             (C46 Y55)
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

// CMYK / name / on-screen colour of one print layer (the official table).
struct PrintLayerDef {
    PrintLayer  layer;
    const char* name;         // exact table name (the Outliner layer name)
    int         c, m, y, k;   // CMYK percent (official ISOM 2017-2 values)
    IofRgb      rgb;          // derived on-screen sRGB for this separation
};

// The print-layer table, in printing order (index == (int)PrintLayer).
const PrintLayerDef& LayerDef(PrintLayer layer);
// Outliner layer name of a print layer (the exact table name).
const char* LayerName(PrintLayer layer);
// On-screen colour of a print layer.
IofRgb      LayerRenderColor(PrintLayer layer);
// Straight sRGB of a SPOT colour at full tone (the glyph ink).
IofRgb      SpotRgb(SpotColor color);
// Opaque "screen %" tint of a spot colour over white paper (Brown 50 %…) — a
// solid colour, NOT an alpha blend.
IofRgb      ScreenRgb(SpotColor color, float pct);

struct IofElement {
    int          code;        // ISOM code ×10 (105.1 → 1051, 101 → 1010)
    const char*  name;
    IofType      type;
    SpotColor    color;       // ink (SpotRgb / ScreenRgb)
    PrintLayer   layer;       // exact print layer → Outliner layer + z-order
    bool         northLocked; // oriented to north → rotation locked
    const char*  desc;        // short description (side panel / viewer)
};

struct IofGroup {
    const char*             name;      // theme heading (Landforms / Rock / …)
    std::vector<IofElement> elements;
};

// The catalogue, in canonical group order (landforms → rock → water →
// vegetation → man-made → technical → course overprint).
const std::vector<IofGroup>& IofCatalogue();

// Find an element by its ×10 code (nullptr if unknown).
const IofElement* IofFindByCode(int code);

// The theme group NAME an element belongs to (Landforms / Rock and boulders /
// … / Course overprint), for the Outliner theme collections. "" if unknown.
const char* IofGroupOf(int code);

// The THEME TOOL BUTTON index (0..5) covering an element: Landforms 0, Rock 1,
// Water 2, Vegetation 3, Man-made 4, (Technical + Course) 5. -1 if unknown.
int IofThemeButtonIndex(int code);

// "101  Contour" label (renders 1051 as "105.1").
std::string IofElementLabel(const IofElement& e);

}  // namespace App::Modules::IofMapping

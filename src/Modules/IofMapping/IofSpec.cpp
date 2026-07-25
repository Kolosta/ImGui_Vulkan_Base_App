#include "IofSpec.h"

#include <cstdio>
#include <cstring>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  Print-layer table — the official ISOM 2017-2 "Printing and colour
//  definitions" stack in printing order (the exact CMYK values of the table).
//  On-screen sRGB is CALIBRATED: each separation renders as its spot ink's PMS
//  equivalent (Process Blue / PMS 361 / PMS 471 / PMS 136 / PMS Purple) at its
//  screen percentage over white — so blue/green/brown look like a printed map,
//  not like a naive process conversion.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// PMS spot equivalents (sRGB 0-1).
constexpr IofRgb kInkBlue   = { 0.000f, 0.522f, 0.792f };   // Process Blue
constexpr IofRgb kInkGreen  = { 0.263f, 0.690f, 0.165f };   // PMS 361
constexpr IofRgb kInkBrown  = { 0.722f, 0.380f, 0.145f };   // PMS 471
constexpr IofRgb kInkYellow = { 1.000f, 0.749f, 0.247f };   // PMS 136
constexpr IofRgb kInkPurple = { 0.733f, 0.161f, 0.733f };   // PMS Purple
constexpr IofRgb kInkBlack  = { 0.0f, 0.0f, 0.0f };
constexpr IofRgb kInkDarkGreen = { 0.000f, 0.430f, 0.260f };   // C100 Y80 K30
constexpr IofRgb kInkYellowGreen = { 0.600f, 0.680f, 0.100f }; // Y100 + G50

// Screen % of an ink over white paper (an opaque tint, not an alpha blend).
constexpr IofRgb Screen(IofRgb ink, float pct) {
    return { 1.0f + (ink.r - 1.0f) * pct,
             1.0f + (ink.g - 1.0f) * pct,
             1.0f + (ink.b - 1.0f) * pct };
}

const PrintLayerDef kLayers[kPrintLayerCount] = {
    { PrintLayer::UpperPurple,         "Upper purple for course overprint", 35, 85, 0, 0,  kInkPurple },
    { PrintLayer::WhiteOverprint,      "White for course overprint",         0,  0, 0, 0,  { 1, 1, 1 } },
    { PrintLayer::WhiteRailway,        "White for railway",                  0,  0, 0, 0,  { 1, 1, 1 } },
    { PrintLayer::Black100,            "Black 100%",                         0,  0, 0, 100, kInkBlack },
    { PrintLayer::Blue100Point,        "Blue 100% point symbols",          100,  0, 0, 0,  kInkBlue },
    { PrintLayer::Brown100Point,       "Brown 100% point symbols",           0, 56, 100, 18, kInkBrown },
    { PrintLayer::Green100Point,       "Green 100% point symbols",          76,  0, 91, 0,  kInkGreen },
    { PrintLayer::Blue100Line,         "Blue 100% line symbols",           100,  0, 0, 0,  kInkBlue },
    { PrintLayer::DarkGreenLine,       "Dark green line symbols",          100,  0, 80, 30, kInkDarkGreen },
    { PrintLayer::Brown100Line,        "Brown 100% line symbols",            0, 56, 100, 18, kInkBrown },
    { PrintLayer::LowerPurple,         "Lower purple for course overprint", 35, 85, 0, 0,  kInkPurple },
    { PrintLayer::Brown50RoadInfill,   "Brown 50% for road infill",          0, 28, 50, 9,  Screen(kInkBrown, 0.5f) },
    { PrintLayer::Black100RoadOutline, "Black 100% for road outline",        0,  0, 0, 100, kInkBlack },
    { PrintLayer::Black50,             "Black 50% for large buildings and tramway", 0, 0, 0, 50, Screen(kInkBlack, 0.5f) },
    { PrintLayer::Black20Canopy,       "Black 20% for canopy",               0,  0, 0, 20,  Screen(kInkBlack, 0.2f) },
    { PrintLayer::Blue100Area,         "Blue 100% area symbols",           100,  0, 0, 0,  kInkBlue },
    { PrintLayer::Blue70Area,          "Blue 70% area symbols",             70,  0, 0, 0,  Screen(kInkBlue, 0.7f) },
    { PrintLayer::Blue50Area,          "Blue 50% area symbols",             50,  0, 0, 0,  Screen(kInkBlue, 0.5f) },
    { PrintLayer::WhiteOverGreenBrown, "White over green and brown",         0,  0, 0, 0,  { 1, 1, 1 } },
    { PrintLayer::Brown50PavedArea,    "Brown 50% for paved area",           0, 28, 50, 9,  Screen(kInkBrown, 0.5f) },
    { PrintLayer::Yellow100Green50,    "Yellow 100% + Green 50%",           38, 27, 100, 0, kInkYellowGreen },
    { PrintLayer::Green100Area,        "Green 100% area symbols",           76,  0, 91, 0,  kInkGreen },
    { PrintLayer::Green60Area,         "Green 60% area symbols",            46,  0, 55, 0,  Screen(kInkGreen, 0.6f) },
    { PrintLayer::Green30Area,         "Green 30% area symbols",            24,  0, 27, 0,  Screen(kInkGreen, 0.3f) },
    { PrintLayer::Black30Area,         "Black 30% area symbols",             0,  0, 0, 30,  Screen(kInkBlack, 0.3f) },
    { PrintLayer::WhiteOverYellow,     "White over Yellow",                  0,  0, 0, 0,  { 1, 1, 1 } },
    { PrintLayer::BlackCultivated,     "Black for cultivated land and sandy ground", 0, 0, 0, 100, kInkBlack },
    { PrintLayer::Yellow100Area,       "Yellow 100% area symbols",           0, 27, 79, 0,  kInkYellow },
    { PrintLayer::Yellow75Area,        "Yellow 75% area symbols",            0, 20, 59, 0,  Screen(kInkYellow, 0.75f) },
    { PrintLayer::Yellow50Area,        "Yellow 50% area symbols",            0, 14, 40, 0,  Screen(kInkYellow, 0.5f) },
};
}  // namespace

const PrintLayerDef& LayerDef(PrintLayer layer) {
    int i = (int)layer;
    if (i < 0 || i >= kPrintLayerCount) i = 0;
    return kLayers[i];
}
const char* LayerName(PrintLayer layer)      { return LayerDef(layer).name; }
IofRgb      LayerRenderColor(PrintLayer layer) { return LayerDef(layer).rgb; }

IofRgb SpotRgb(SpotColor color) {
    switch (color) {
        case SpotColor::Purple: return kInkPurple;
        case SpotColor::Black:  return kInkBlack;
        case SpotColor::Blue:   return kInkBlue;
        case SpotColor::Brown:  return kInkBrown;
        case SpotColor::Green:  return kInkGreen;
        case SpotColor::Yellow: return kInkYellow;
    }
    return { 0, 0, 0 };
}

IofRgb ScreenRgb(SpotColor color, float pct) {
    return Screen(SpotRgb(color), pct);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Catalogue — ISOM 2017-2, grouped by theme. Codes ×10.
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<IofGroup>& IofCatalogue() {
    using T = IofType;
    using S = SpotColor;
    using L = PrintLayer;
    static const std::vector<IofGroup> kCat = {
        { "Landforms", {
            { 1010, "Contour",              T::Line,  S::Brown, L::Brown100Line, false,
              "A line joining points of equal height. 0.14 mm brown." },
            { 1020, "Index contour",        T::Line,  S::Brown, L::Brown100Line, false,
              "Every fifth contour, drawn thicker (0.25 mm)." },
            { 1030, "Form line",            T::Line,  S::Brown, L::Brown100Line, false,
              "Intermediate contour for small but distinct landforms (dashed)." },
            { 1040, "Earth bank",           T::Line,  S::Brown, L::Brown100Line, false,
              "Abrupt change in ground level; 0.25 mm line with downhill tags." },
            { 1051, "Earth wall",           T::Line,  S::Brown, L::Brown100Line, false,
              "Distinct earth wall; 0.18 mm line with dots astride it." },
            { 1052, "Retaining earth wall", T::Line,  S::Brown, L::Brown100Line, false,
              "Reinforced earth bank; brown line with half-circles on one side." },
            { 1060, "Ruined earth wall",    T::Line,  S::Brown, L::Brown100Line, false,
              "Earth wall no longer continuous; the wall line broken by gaps." },
            { 1070, "Erosion gully",        T::Line,  S::Brown, L::Brown100Line, false,
              "Erosion gully or trench; the line tapers to a point." },
            { 1080, "Small erosion gully",  T::Line,  S::Brown, L::Brown100Line, false,
              "Small dry ditch, shown with a dotted line." },
            { 1090, "Small knoll",          T::Point, S::Brown, L::Brown100Point, false,
              "Small obvious mound; brown dot 0.5 mm." },
            { 1100, "Small elongated knoll", T::Point, S::Brown, L::Brown100Point, false,
              "Small obvious elongated mound; brown ellipse 0.8 × 0.4 mm." },
            { 1130, "Broken ground",        T::Area,  S::Brown, L::Brown100Point, false,
              "Broken or pitted ground; scattered brown dots, 3-4 per mm²." },
            { 1140, "Very broken ground",   T::Area,  S::Brown, L::Brown100Point, false,
              "Densely pitted ground; scattered brown dots, 7-9 per mm²." },
            { 1110, "Small depression",     T::Point, S::Brown, L::Brown100Point, true,
              "Small shallow depression; half-circle open to the north." },
            { 1120, "Pit",                  T::Point, S::Brown, L::Brown100Point, true,
              "Pit or hole with distinct steep sides; V opening north." },
            { 1150, "Prominent landform",   T::Point, S::Brown, L::Brown100Point, true,
              "Special prominent landform feature; brown triangle." },
        } },
        { "Rock and boulders", {
            { 2010, "Impassable cliff",     T::Line,  S::Black, L::Black100, false,
              "Impassable cliff; 0.35 mm line with downhill tags." },
            { 2020, "Cliff",                T::Line,  S::Black, L::Black100, false,
              "Passable rock face; 0.25 mm line, optional tags." },
            { 2031, "Rocky pit or cave",    T::Point, S::Black, L::Black100, true,
              "Rocky pit, hole or cave entrance; black V, 0.16 mm." },
            { 2032, "Dangerous pit",        T::Point, S::Black, L::Black100, false,
              "Pit or hole dangerous to the runner; 0.9 mm black ring." },
            { 2040, "Boulder",              T::Point, S::Black, L::Black100, false,
              "Distinct boulder (> 1 m); black dot 0.4 mm." },
            { 2050, "Large boulder",        T::Point, S::Black, L::Black100, false,
              "Particularly large boulder; black dot 0.6 mm." },
            { 2060, "Gigantic boulder",     T::Area,  S::Black, L::Black100, false,
              "Gigantic boulder or rock pillar, drawn to shape; solid black." },
            { 2070, "Boulder cluster",      T::Point, S::Black, L::Black100, true,
              "Small distinct group of boulders; filled triangle, 0.8 mm side." },
            { 2071, "Boulder cluster 120%", T::Point, S::Black, L::Black100, true,
              "The cluster enlarged to 120% (0.96 mm side), as the spec allows." },
            { 2080, "Boulder field",        T::Area,  S::Black, L::Black100, false,
              "Area covered with boulders; scattered solid triangles." },
            { 2100, "Stony ground, slow",   T::Area,  S::Black, L::Black100, false,
              "Stony ground reducing running speed; scattered dots." },
            { 2130, "Sandy ground",         T::Area,  S::Yellow, L::Yellow50Area, false,
              "Soft sandy ground; yellow 50% with black dots." },
            { 2140, "Bare rock",            T::Area,  S::Black, L::Black30Area, false,
              "Runnable bare rock; grey (black 30%)." },
            { 2150, "Trench",               T::Line,  S::Black, L::Black100, false,
              "Erosion trench in rock; black 0.30 line under a white 0.10 one." },
        } },
        { "Water and marsh", {
            { 3010, "Uncrossable body of water", T::Area, S::Blue, L::Blue100Area, false,
              "Lake or pond; blue with a black bank line." },
            { 3020, "Shallow body of water",     T::Area, S::Blue, L::Blue70Area, false,
              "Shallow or seasonal water; blue 70%." },
            { 3030, "Waterhole",                 T::Point, S::Blue, L::Blue100Point, true,
              "Water-filled pit or waterhole; blue V, 0.18 mm." },
            { 3040, "Crossable watercourse",     T::Line, S::Blue, L::Blue100Line, false,
              "Crossable watercourse ≥ 2 m wide; 0.30 mm blue." },
            { 3050, "Small crossable watercourse", T::Line, S::Blue, L::Blue100Line, false,
              "Watercourse < 2 m wide; 0.18 mm blue." },
            { 3060, "Seasonal water channel",    T::Line, S::Blue, L::Blue100Line, false,
              "Seasonal / intermittent channel; dashed blue." },
            { 3070, "Uncrossable marsh",         T::Area, S::Blue, L::Blue100Area, false,
              "Marsh, dangerous / uncrossable; blue horizontal lines + bank." },
            { 3080, "Marsh",                     T::Area, S::Blue, L::Blue100Area, false,
              "Crossable marsh; blue horizontal line pattern." },
            { 3090, "Narrow marsh",              T::Line, S::Blue, L::Blue100Line, false,
              "Marsh or trickle too narrow for the area symbol; dotted blue." },
            { 3100, "Indistinct marsh",          T::Area, S::Blue, L::Blue100Area, false,
              "Indistinct / seasonal marsh; blue dashed horizontal lines." },
            { 3110, "Well",                      T::Point, S::Blue, L::Blue100Point, true,
              "Well or captive spring; blue square outline." },
            { 3120, "Spring",                    T::Point, S::Blue, L::Blue100Point, false,
              "Source of a watercourse; blue half-circle." },
            { 3130, "Prominent water feature",   T::Point, S::Blue, L::Blue100Point, true,
              "Special small water feature; blue asterisk." },
        } },
        { "Vegetation", {
            { 4010, "Open land",             T::Area, S::Yellow, L::Yellow100Area, false,
              "Cultivated or open runnable land; yellow 100%." },
            { 4020, "Open land with scattered trees", T::Area, S::Yellow, L::Yellow75Area, false,
              "Open land with scattered trees; yellow 75% with white dots." },
            { 4021, "Open land w/ scattered trees (green)", T::Area, S::Yellow, L::Yellow75Area, false,
              "Open land with scattered trees; yellow 75% with green 60% dots." },
            { 4030, "Rough open land",       T::Area, S::Yellow, L::Yellow50Area, false,
              "Heath, moorland; yellow 50%." },
            { 4040, "Rough open land w/ scattered trees", T::Area, S::Yellow, L::Yellow50Area, false,
              "Rough open land with scattered trees; yellow 50% with white dots." },
            { 4041, "Rough open land w/ trees (green)", T::Area, S::Yellow, L::Yellow50Area, false,
              "Rough open land with scattered trees; yellow 50% with green 60% dots." },
            { 4050, "Forest",                T::Area, S::Yellow, L::WhiteOverYellow, false,
              "Runnable open forest; white (no colour)." },
            { 4060, "Vegetation, slow running", T::Area, S::Green, L::Green30Area, false,
              "Dense vegetation, slow running; green 30%." },
            { 4061, "Vegetation, slow (white stripes)", T::Area, S::Green, L::Green30Area, false,
              "Slow running, runnable one way; green 30% with white stripes." },
            { 4070, "Vegetation, slow, good visibility", T::Area, S::Green, L::Green100Area, true,
              "Slow running but good visibility; 0.12 mm green stripes 0.84 mm apart." },
            { 4080, "Vegetation, walk",      T::Area, S::Green, L::Green60Area, false,
              "Dense vegetation, walking speed; green 60%." },
            { 4081, "Vegetation, walk (white stripes)", T::Area, S::Green, L::Green60Area, false,
              "Walking speed, runnable one way; green 60% with white stripes." },
            { 4082, "Vegetation, walk (green stripes)", T::Area, S::Green, L::Green60Area, false,
              "Walking speed, runnable one way; green 60% with green 30% stripes." },
            { 4090, "Vegetation, walk, good visibility", T::Area, S::Green, L::Green100Area, true,
              "Walking speed but good visibility; 0.14 mm green stripes 0.42 mm apart." },
            { 4100, "Vegetation, fight",     T::Area, S::Green, L::Green100Area, false,
              "Very dense vegetation, barely passable; green 100%." },
            { 4101, "Vegetation, fight (white stripes)", T::Area, S::Green, L::Green100Area, false,
              "Barely passable, runnable one way; green with white stripes." },
            { 4102, "Vegetation, fight (green 30 stripes)", T::Area, S::Green, L::Green100Area, false,
              "Barely passable, runnable one way; green with green 30% stripes." },
            { 4103, "Vegetation, fight (green 60 stripes)", T::Area, S::Green, L::Green100Area, false,
              "Barely passable, runnable one way; green with green 60% stripes." },
            { 4120, "Cultivated land",       T::Area, S::Yellow, L::BlackCultivated, true,
              "Cultivated land, seasonally out of bounds; yellow + black dots." },
            { 4130, "Orchard",               T::Area, S::Green, L::Green100Area, true,
              "Orchard; yellow 100% with rows of green dots." },
            { 4131, "Orchard (yellow 50%)",  T::Area, S::Green, L::Green100Area, true,
              "Orchard on rough open land; yellow 50% with rows of green dots." },
            { 4140, "Vineyard",              T::Area, S::Green, L::Green100Area, true,
              "Vineyard; yellow 100% with dashed green rows." },
            { 4141, "Vineyard (yellow 50%)", T::Area, S::Green, L::Green100Area, true,
              "Vineyard on rough open land; yellow 50% with dashed green rows." },
            { 4150, "Distinct cultivation boundary", T::Line, S::Black, L::Black100, false,
              "Distinct boundary of cultivated land; 0.10 mm black." },
            { 4160, "Distinct vegetation boundary",  T::Line, S::Black, L::Black100, false,
              "Distinct forest edge; black dotted line." },
            { 4161, "Distinct vegetation boundary (green)", T::Line, S::Green, L::DarkGreenLine, false,
              "Distinct forest edge; dark green dashed line, 0.14 mm." },
            { 4170, "Prominent large tree",  T::Point, S::Green, L::Green100Point, false,
              "Particularly prominent tree; 0.9 mm green ring on a white disc "
              "clearing the vegetation." },
            { 4180, "Prominent bush or small tree", T::Point, S::Green, L::Green100Point, false,
              "Prominent bush / small tree; 0.6 mm green ring on a white disc." },
            { 4190, "Prominent vegetation feature", T::Point, S::Green, L::Green100Point, true,
              "Special vegetation feature; green X." },
        } },
        { "Man-made features", {
            { 5010, "Paved area",            T::Area, S::Brown, L::Brown50PavedArea, false,
              "Paved area; brown 50% with a black outline." },
            { 5020, "Wide road",             T::Line, S::Black, L::Black100RoadOutline, false,
              "Road wider than 5 m; two black edges, brown infill." },
            { 5030, "Road",                  T::Line, S::Black, L::Black100, false,
              "Maintained road; 0.35 mm black." },
            { 5040, "Vehicle track",         T::Line, S::Black, L::Black100, false,
              "Distinct vehicle track; dashed 0.35 mm black." },
            { 5050, "Footpath",              T::Line, S::Black, L::Black100, false,
              "Large footpath; dashed 0.25 mm black." },
            { 5060, "Small footpath",        T::Line, S::Black, L::Black100, false,
              "Small footpath; dashed 0.18 mm black." },
            { 5070, "Less distinct small footpath", T::Line, S::Black, L::Black100, false,
              "Barely visible path; double-dashed 0.18 mm black." },
            { 5080, "Narrow ride",           T::Line, S::Black, L::Black100, false,
              "Narrow forest ride; dashed 0.14 mm black." },
            { 5081, "Narrow ride (yellow band)", T::Line, S::Black, L::Black100, false,
              "Narrow ride over a 0.45 mm yellow band." },
            { 5082, "Narrow ride (green 30 band)", T::Line, S::Black, L::Black100, false,
              "Narrow ride over a 0.45 mm green 30% band." },
            { 5083, "Narrow ride (green 60 band)", T::Line, S::Black, L::Black100, false,
              "Narrow ride over a 0.45 mm green 60% band." },
            { 5084, "Narrow ride (white band)", T::Line, S::Black, L::Black100, false,
              "Narrow ride over a 0.45 mm white band." },
            { 5090, "Railway",               T::Line, S::Black, L::Black100, false,
              "Railway; line with cross-ties." },
            { 5100, "Power line",            T::Line, S::Black, L::Black100, false,
              "Power line; 0.14 mm line with pylon bars." },
            { 5110, "Major power line",      T::Line, S::Black, L::Black100, false,
              "Major power line; double line with pylon bars." },
            { 5131, "Wall",                  T::Line, S::Black, L::Black100, false,
              "Stone wall; 0.14 mm line with dots astride it." },
            { 5132, "Retaining wall",        T::Line, S::Black, L::Black100, false,
              "Retaining wall; 0.14 mm line with tags on the lower side." },
            { 5140, "Ruined wall",           T::Line, S::Black, L::Black100, false,
              "Ruined wall; dashed line with dots." },
            { 5150, "Impassable wall",       T::Line, S::Black, L::Black100, false,
              "High impassable wall; 0.14 mm line with large dots." },
            { 5160, "Fence",                 T::Line, S::Black, L::Black100, false,
              "Fence; 0.14 mm line with tags every 2 mm." },
            { 5170, "Ruined fence",          T::Line, S::Black, L::Black100, false,
              "Fence no longer continuous; the fence line broken by gaps." },
            { 5180, "Impassable fence",      T::Line, S::Black, L::Black100, false,
              "High impassable fence; paired tags every 2.5 mm." },
            { 5200, "Area that shall not be entered", T::Area, S::Green, L::Yellow100Green50, false,
              "Out-of-bounds area; olive (yellow 100% + green 50%), black edge." },
            { 5210, "Building",              T::Area, S::Black, L::Black100, false,
              "Building; solid black." },
            { 5211, "Large building",        T::Area, S::Black, L::Black50, false,
              "Large building / tramway; black 50% area with a 0.1 mm black "
              "100% outline." },
            { 5220, "Canopy",                T::Area, S::Black, L::Black20Canopy, false,
              "Covered passable area; black 20% with outline." },
            { 5230, "Ruin",                  T::Area,  S::Black, L::Black100, false,
              "Ruin drawn to shape; dashed black edge, no fill." },
            { 5231, "Ruin (small)",          T::Point, S::Black, L::Black100, true,
              "Ruin too small to draw to shape; 0.8 mm black square." },
            { 5240, "High tower",            T::Point, S::Black, L::Black100, true,
              "High tower or mast; dot with cross arms." },
            { 5250, "Small tower",           T::Point, S::Black, L::Black100, true,
              "Small tower or high seat; a T of 1.0 mm bars, 0.16 mm." },
            { 5260, "Cairn",                 T::Point, S::Black, L::Black100, false,
              "Cairn, memorial or boundary stone; 0.8 mm ring with centre dot." },
            { 5270, "Fodder rack",           T::Point, S::Black, L::Black100, true,
              "Fodder rack; a 0.9 mm stem under two arms 30° below horizontal." },
            { 5280, "Prominent line feature", T::Line, S::Black, L::Black100, false,
              "Special line feature; 0.14 mm line with mirrored 45° tags." },
            { 5290, "Prominent uncrossable line feature", T::Line, S::Black, L::Black100, false,
              "Uncrossable special line feature; 0.25 mm line, paired tags." },
            { 5300, "Prominent man-made feature (ring)", T::Point, S::Black, L::Black100, false,
              "Special man-made feature; black ring." },
            { 5310, "Prominent man-made feature (X)", T::Point, S::Black, L::Black100, true,
              "Special man-made feature; black X." },
        } },
        { "Technical symbols", {
            { 6010, "Magnetic north line",   T::Line, S::Black, L::Black100, true,
              "Magnetic north line; 0.10 mm, oriented to north." },
            { 6011, "Magnetic north line (blue)", T::Line, S::Blue, L::Blue100Line, true,
              "Magnetic north line in blue; 0.12 mm, oriented to north." },
            { 6020, "Registration mark",     T::Point, S::Black, L::Black100, true,
              "Registration cross for colour printing." },
            { 6030, "Spot height",           T::Point, S::Black, L::Black100, false,
              "Spot height dot 0.3 mm (value written beside)." },
        } },
        { "Course overprint", {
            { 7010, "Start",                 T::Point, S::Purple, L::LowerPurple, true,
              "Start; equilateral triangle 6 mm, mitred, 0.35 mm." },
            { 7020, "Map issue point",       T::Point, S::Purple, L::UpperPurple, true,
              "Map issue point; purple bar 2.5 × 0.6 mm." },
            { 7030, "Control point",         T::Point, S::Purple, L::LowerPurple, false,
              "Control point; circle 5 mm, 0.35 mm." },
            { 7050, "Course line",           T::Line, S::Purple, L::LowerPurple, false,
              "Line between controls; 0.35 mm purple." },
            { 7060, "Finish",                T::Point, S::Purple, L::LowerPurple, false,
              "Finish; concentric circles 4 and 6 mm, 0.35 mm." },
            { 7070, "Marked route",          T::Line, S::Purple, L::UpperPurple, false,
              "Marked route; dashed 0.35 mm purple, 2.0 / 0.5 mm." },
            { 7080, "Out-of-bounds boundary", T::Line, S::Purple, L::LowerPurple, false,
              "Boundary of an out-of-bounds area; 0.7 mm purple, mitred." },
            { 7090, "Out-of-bounds area",    T::Area, S::Purple, L::UpperPurple, false,
              "Out-of-bounds area; purple cross-hatch with a solid edge." },
            { 7091, "Out-of-bounds area (dashed edge)", T::Area, S::Purple, L::UpperPurple, false,
              "Out-of-bounds area; cross-hatch with a dashed edge." },
            { 7092, "Out-of-bounds area (no edge)", T::Area, S::Purple, L::UpperPurple, false,
              "Out-of-bounds area; cross-hatch only, no edge." },
            { 7120, "First aid post",        T::Point, S::Purple, L::LowerPurple, true,
              "First aid post; purple cross." },
            { 7150, "Continuing point after map exchange", T::Point, S::Purple, L::LowerPurple, true,
              "Map-exchange continuation; 5 mm circle with an inscribed triangle." },
        } },
    };
    return kCat;
}

const IofElement* IofFindByCode(int code) {
    for (const IofGroup& g : IofCatalogue())
        for (const IofElement& e : g.elements)
            if (e.code == code) return &e;
    return nullptr;
}

const char* IofGroupOf(int code) {
    for (const IofGroup& g : IofCatalogue())
        for (const IofElement& e : g.elements)
            if (e.code == code) return g.name;
    return "";
}

int IofThemeButtonIndex(int code) {
    const char* g = IofGroupOf(code);
    if (!std::strcmp(g, "Landforms"))          return 0;
    if (!std::strcmp(g, "Rock and boulders"))  return 1;
    if (!std::strcmp(g, "Water and marsh"))    return 2;
    if (!std::strcmp(g, "Vegetation"))         return 3;
    if (!std::strcmp(g, "Man-made features"))  return 4;
    if (!std::strcmp(g, "Technical symbols") ||
        !std::strcmp(g, "Course overprint"))   return 5;
    return -1;
}

std::string IofElementLabel(const IofElement& e) {
    char buf[64];
    if (e.code % 10)
        std::snprintf(buf, sizeof buf, "%d.%d  %s", e.code / 10, e.code % 10,
                      e.name);
    else
        std::snprintf(buf, sizeof buf, "%d  %s", e.code / 10, e.name);
    return buf;
}

}  // namespace App::Modules::IofMapping

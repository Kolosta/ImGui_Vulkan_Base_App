#include "IofSpec.h"
#include <cstdio>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  ISOM 2017-2 (Revision 6) catalogue — exact data. One row per symbol with its
//  real code (×10), type (P/L/A/T), print layer (colour + Outliner collection),
//  north-orientation flag, glyph kind (drives the exact vector glyph) and a
//  tooltip. Dimensions live with the glyph builder (IofGlyph.cpp); colours come
//  from the ISOM CMYK definitions below.
// ─────────────────────────────────────────────────────────────────────────────

// ISOM "Printing and Colour Definitions" SPOT colours, as the accepted DIGITAL
// (sRGB) values rather than a naive CMYK→RGB conversion: pure-cyan C100 converts
// to (0,255,255) which reads as a wrong, garish cyan on screen — the standard
// ISOM screen blue is a proper mid-blue. Likewise the other spot colours use the
// commonly-used OCAD/ISOM sRGB equivalents so the map looks correct on screen.
// These drive the GLYPH INK; the per-separation print layers reuse them below.
IofRgb LayerColor(SpotColor color) {
    auto rgb = [](int r, int g, int b) {
        return IofRgb{ r / 255.0f, g / 255.0f, b / 255.0f };
    };
    switch (color) {
        case SpotColor::Purple: return rgb(165,  68, 154);   // lower purple (M85 C35)
        case SpotColor::Black:  return rgb(  0,   0,   0);
        case SpotColor::Blue:   return rgb(  0, 147, 221);   // ISOM blue (C100 sRGB)
        case SpotColor::Brown:  return rgb(208, 111,  43);   // ISOM brown (M56 Y100 K18)
        case SpotColor::Green:  return rgb(  0, 168,  80);   // ISOM green (C76 Y91)
        case SpotColor::Yellow: return rgb(255, 190,  35);   // ISOM yellow (M27 Y79)
    }
    return { 0, 0, 0 };
}

// An opaque "screen %" colour: a `pct` screen of a spot colour over WHITE paper
// is a solid tint (white + pct·(base − white)), NOT an alpha blend. So "Brown
// 50%", "Black 50%", "Green 50%"… are real opaque named colours. Used for the
// flat-screen areas (paved/road/out-of-bounds/building 50%, veg greens, etc.).
IofRgb ScreenColor(SpotColor color, float pct) {
    IofRgb b = LayerColor(color);
    float p = pct < 0 ? 0 : (pct > 1 ? 1 : pct);
    return { 1.0f + p * (b.r - 1.0f), 1.0f + p * (b.g - 1.0f), 1.0f + p * (b.b - 1.0f) };
}

// ── Print-layer colour table (ISOM 2017-2 printing order, top of stack first) ──
// The render RGB is the opaque on-screen tint of the separation: for a screened
// layer it is the spot colour at that %; for the "white" overprint layers it is
// paper white. Used both for the Outliner collection swatch and the table.
const PrintLayerDef& LayerDef(PrintLayer layer) {
    using S = SpotColor;
    auto pct = [](S c, float p) {                // opaque screen tint over white
        IofRgb b = LayerColor(c);
        return IofRgb{ 1.0f + p*(b.r-1.0f), 1.0f + p*(b.g-1.0f), 1.0f + p*(b.b-1.0f) };
    };
    auto rgb = [](int r,int g,int b){ return IofRgb{ r/255.0f, g/255.0f, b/255.0f }; };
    const IofRgb white = rgb(255, 255, 255);
    const IofRgb darkGreen = rgb(0, 95, 50);     // C100 Y80 K30 (dark green lines)
    // index == (int)PrintLayer; keep IN SYNC with the enum order.
    static const PrintLayerDef kTable[] = {
        { PrintLayer::UpperPurple,         "Upper purple for course overprint",         35, 85,  0,  0, pct(S::Purple, 1.0f) },
        { PrintLayer::WhiteOverprint,      "White for course overprint",                 0,  0,  0,  0, white },
        { PrintLayer::Black100,            "Black 100%",                                 0,  0,  0,100, pct(S::Black, 1.0f) },
        { PrintLayer::Blue100Point,        "Blue 100% point symbols",                  100,  0,  0,  0, pct(S::Blue, 1.0f) },
        { PrintLayer::Brown100Point,       "Brown 100% point symbols",                   0, 56,100, 18, pct(S::Brown, 1.0f) },
        { PrintLayer::Green100Point,       "Green 100% point symbols",                  76,  0, 91,  0, pct(S::Green, 1.0f) },
        { PrintLayer::Blue100Line,         "Blue 100% line symbols",                   100,  0,  0,  0, pct(S::Blue, 1.0f) },
        { PrintLayer::DarkGreenLine,       "Dark green line symbols",                  100,  0, 80, 30, darkGreen },
        { PrintLayer::Brown100Line,        "Brown 100% line symbols",                    0, 56,100, 18, pct(S::Brown, 1.0f) },
        { PrintLayer::LowerPurple,         "Lower purple for course overprint",         35, 85,  0,  0, pct(S::Purple, 1.0f) },
        { PrintLayer::Brown50RoadInfill,   "Brown 50% for road infill",                  0, 28, 50,  9, pct(S::Brown, 0.5f) },
        { PrintLayer::Black100RoadOutline, "Black 100% for road outline",                0,  0,  0,100, pct(S::Black, 1.0f) },
        { PrintLayer::Black50,             "Black 50% for large buildings and tramway",  0,  0,  0, 50, pct(S::Black, 0.5f) },
        { PrintLayer::Black20Canopy,       "Black 20% for canopy",                       0,  0,  0, 20, pct(S::Black, 0.2f) },
        { PrintLayer::Blue100Area,         "Blue 100% area symbols",                   100,  0,  0,  0, pct(S::Blue, 1.0f) },
        { PrintLayer::Blue70Area,          "Blue 70% area symbols",                     70,  0,  0,  0, pct(S::Blue, 0.7f) },
        { PrintLayer::Blue50Area,          "Blue 50% area symbols",                     50,  0,  0,  0, pct(S::Blue, 0.5f) },
        { PrintLayer::WhiteOverGreenBrown, "White over green and brown",                 0,  0,  0,  0, white },
        { PrintLayer::Brown50PavedArea,    "Brown 50% for paved area",                   0, 28, 50,  9, pct(S::Brown, 0.5f) },
        { PrintLayer::Yellow100Green50,    "Yellow 100% + Green 50%",                   38, 27,100,  0, rgb(160, 165,  35) },
        { PrintLayer::Green100Area,        "Green 100% area symbols",                   76,  0, 91,  0, pct(S::Green, 1.0f) },
        { PrintLayer::Green60Area,         "Green 60% area symbols",                    46,  0, 55,  0, pct(S::Green, 0.6f) },
        { PrintLayer::Green30Area,         "Green 30% area symbols",                    24,  0, 27,  0, pct(S::Green, 0.3f) },
        { PrintLayer::Black30Area,         "Black 30% area symbols",                     0,  0,  0, 30, pct(S::Black, 0.3f) },
        { PrintLayer::WhiteOverYellow,     "White over Yellow",                          0,  0,  0,  0, white },
        { PrintLayer::BlackCultivated,     "Black for cultivated land and sandy ground", 0,  0,  0,100, pct(S::Black, 1.0f) },
        { PrintLayer::Yellow100Area,       "Yellow 100% area symbols",                   0, 27, 79,  0, pct(S::Yellow, 1.0f) },
        { PrintLayer::Yellow75Area,        "Yellow 75% area symbols",                    0, 20, 59,  0, pct(S::Yellow, 0.75f) },
        { PrintLayer::Yellow50Area,        "Yellow 50% area symbols",                    0, 14, 40,  0, pct(S::Yellow, 0.5f) },
    };
    static_assert(sizeof(kTable) / sizeof(kTable[0]) == kPrintLayerCount,
                  "print-layer table out of sync with the PrintLayer enum");
    int i = (int)layer;
    if (i < 0 || i >= kPrintLayerCount) i = (int)PrintLayer::Black100;
    return kTable[i];
}

const char* LayerName(PrintLayer layer)     { return LayerDef(layer).name; }
IofRgb      LayerRenderColor(PrintLayer layer) { return LayerDef(layer).rgb; }

// Map a symbol's (spot colour, type, screen %) to its exact print layer — the
// table-lookup rule used to author the catalogue. POINTS and LINES use the full
// (100%) tone layer of their colour; AREAS pick the % layer. Purple is split by
// the caller (upper vs lower) — default here is lower purple.
PrintLayer ResolvePrintLayer(SpotColor color, IofType type, float pct) {
    const bool area = (type == IofType::Area);
    const bool line = (type == IofType::Line);
    switch (color) {
        case SpotColor::Purple: return PrintLayer::LowerPurple;
        case SpotColor::Black:  return PrintLayer::Black100;
        case SpotColor::Blue:
            if (!area) return line ? PrintLayer::Blue100Line : PrintLayer::Blue100Point;
            if (pct <= 0.55f) return PrintLayer::Blue50Area;
            if (pct <= 0.75f) return PrintLayer::Blue70Area;
            return PrintLayer::Blue100Area;
        case SpotColor::Brown:
            return line ? PrintLayer::Brown100Line : PrintLayer::Brown100Point;
        case SpotColor::Green:
            if (line) return PrintLayer::DarkGreenLine;
            if (!area) return PrintLayer::Green100Point;
            if (pct <= 0.45f) return PrintLayer::Green30Area;
            if (pct <= 0.8f)  return PrintLayer::Green60Area;
            return PrintLayer::Green100Area;
        case SpotColor::Yellow:
            if (pct <= 0.6f)  return PrintLayer::Yellow50Area;
            if (pct <= 0.85f) return PrintLayer::Yellow75Area;
            return PrintLayer::Yellow100Area;
    }
    return PrintLayer::Black100;
}

const std::vector<IofGroup>& IofCatalogue() {
    using T = IofType;
    using C = SpotColor;     // glyph INK
    using P = PrintLayer;    // exact print LAYER (Outliner collection + order)
    using G = IofGlyphKind;
    // {code×10, name, type, spotColor, printLayer, glyph, northLocked, desc}
    // The print layer is the colour-table refinement of the spot colour by the
    // symbol TYPE and screen % (ISOM 2017-2 "Printing and colour definitions").
    static const std::vector<IofGroup> kCatalogue = {
        // ── 1xx Landforms (brown) ────────────────────────────────────────────
        { "Landforms (relief)", {
            { 1010, "Contour",                 T::Line,  C::Brown,  P::Brown100Line,   G::Contour,            false, "A line joining points of equal height (5 m interval)." },
            { 1020, "Index contour",           T::Line,  C::Brown,  P::Brown100Line,   G::IndexContour,       false, "Every fifth contour, drawn thicker." },
            { 1030, "Form line",               T::Line,  C::Brown,  P::Brown100Line,   G::FormLine,           false, "An intermediate dashed contour for extra detail." },
            { 1040, "Earth bank",              T::Line,  C::Brown,  P::Brown100Line,   G::EarthBank,          false, "An abrupt change in ground level, with tags." },
            { 1051, "Earth wall",              T::Line,  C::Brown,  P::Brown100Line,   G::EarthWall,          false, "A distinct earth wall." },
            { 1052, "Retaining earth wall",    T::Line,  C::Brown,  P::Brown100Line,   G::RetainingEarthWall, false, "A retaining earth wall (peat edge, terrace)." },
            { 1060, "Ruined earth wall",       T::Line,  C::Brown,  P::Brown100Line,   G::RuinedEarthWall,    false, "A ruined or less distinct earth wall." },
            { 1070, "Erosion gully",           T::Line,  C::Brown,  P::Brown100Line,   G::ErosionGully,       false, "An erosion gully shown by a single tapering line." },
            { 1080, "Small erosion gully",     T::Line,  C::Brown,  P::Brown100Line,   G::SmallErosionGully,  false, "A small erosion gully or dry ditch (dotted)." },
            { 1090, "Small knoll",             T::Point, C::Brown,  P::Brown100Point,  G::KnollDot,           false, "A small, distinct mound." },
            { 1100, "Small elongated knoll",   T::Point, C::Brown,  P::Brown100Point,  G::ElongatedKnoll,     true,  "A small elongated mound." },
            { 1110, "Small depression",        T::Point, C::Brown,  P::Brown100Point,  G::SmallDepression,    true,  "A small shallow hollow." },
            { 1120, "Pit",                     T::Point, C::Brown,  P::Brown100Point,  G::Pit,                true,  "A distinct hole with steep sides." },
            { 1130, "Broken ground",           T::Area,  C::Brown,  P::Brown100Point,  G::StonyGround,        false, "An area of pits/knolls (brown dots)." },
            { 1140, "Very broken ground",      T::Area,  C::Brown,  P::Brown100Point,  G::StonyGround,        false, "Densely broken ground (brown dots)." },
            { 1150, "Prominent landform feature", T::Point, C::Brown, P::Brown100Point, G::ProminentLandform, true, "A special landform feature (defined on the map)." },
        }},
        // ── 2xx Rock and boulders (black / grey) ─────────────────────────────
        { "Rock and boulders", {
            { 2010, "Impassable cliff",        T::Line,  C::Black,  P::Black100,       G::ImpassableCliff,   false, "A high, steep rock face — impassable." },
            { 2020, "Cliff",                   T::Line,  C::Black,  P::Black100,       G::Cliff,             false, "A passable cliff or crag, with downhill tags." },
            { 2031, "Rocky pit or cave",       T::Point, C::Black,  P::Black100,       G::RockyPit,          false, "A rocky pit, hole or cave (orientable)." },
            { 2032, "Dangerous pit",           T::Point, C::Black,  P::Black100,       G::DangerousPit,      false, "A highly dangerous pit or vertical shaft." },
            { 2040, "Boulder",                 T::Point, C::Black,  P::Black100,       G::BoulderDot,        false, "A distinct boulder (>1 m)." },
            { 2050, "Large boulder",           T::Point, C::Black,  P::Black100,       G::LargeBoulderDot,   false, "A particularly large boulder (>2 m)." },
            { 2060, "Gigantic boulder / pillar", T::Area, C::Black, P::Black100,       G::GiganticBoulder,  false, "A gigantic boulder or rock pillar (plan shape)." },
            { 2070, "Boulder cluster",         T::Point, C::Black,  P::Black100,       G::BoulderTriangle,   true,  "A distinct group of boulders." },
            { 2080, "Boulder field",           T::Area,  C::Black,  P::Black100,       G::BoulderField,      false, "An area strewn with many boulders." },
            { 2090, "Dense boulder field",     T::Area,  C::Black,  P::Black100,       G::BoulderField,      false, "A densely boulder-strewn area." },
            { 2100, "Stony ground, slow",      T::Area,  C::Black,  P::Black100,       G::StonyGround,       false, "Stony ground (60–80% speed)." },
            { 2110, "Stony ground, walk",      T::Area,  C::Black,  P::Black100,       G::StonyGround,       false, "Stony ground (20–60% speed)." },
            { 2120, "Stony ground, fight",     T::Area,  C::Black,  P::Black100,       G::StonyGround,       false, "Stony ground (<20% speed)." },
            { 2130, "Sandy ground",            T::Area,  C::Yellow, P::BlackCultivated,G::SandyGround,       true,  "Soft sandy ground (yellow 50% + black dots)." },
            { 2140, "Bare rock",               T::Area,  C::Black,  P::Black30Area,    G::BareRock,          false, "A runnable area of bare rock (grey, black 35%)." },
            { 2150, "Trench",                  T::Line,  C::Black,  P::Black100,       G::Trench,            false, "A rocky or artificial trench (twin lines)." },
        }},
        // ── 3xx Water and marsh (blue) ───────────────────────────────────────
        { "Water and marsh", {
            { 3010, "Uncrossable body of water", T::Area, C::Blue,  P::Blue100Area,    G::WaterArea,        false, "Deep water — uncrossable (black bank line)." },
            { 3020, "Shallow body of water",   T::Area,  C::Blue,   P::Blue50Area,     G::ShallowWater,      false, "Crossable shallow water (blue 50%)." },
            { 3030, "Waterhole",               T::Point, C::Blue,   P::Blue100Point,   G::Waterhole,         true,  "A water-filled pit, too small to scale." },
            { 3040, "Crossable watercourse",   T::Line,  C::Blue,   P::Blue100Line,    G::Watercourse,       false, "A stream more than 2 m wide." },
            { 3050, "Small crossable watercourse", T::Line, C::Blue, P::Blue100Line,   G::SmallWatercourse, false, "A stream less than 2 m wide." },
            { 3060, "Minor / seasonal water channel", T::Line, C::Blue, P::Blue100Line, G::SeasonalChannel, false, "A minor or seasonal water channel (dashed)." },
            { 3070, "Uncrossable marsh",       T::Area,  C::Blue,   P::Blue100Area,    G::MarshArea,         true,  "An uncrossable marsh (black outline)." },
            { 3080, "Marsh",                   T::Area,  C::Blue,   P::Blue100Area,    G::MarshArea,         true,  "A crossable marsh (blue hatch)." },
            { 3090, "Narrow marsh",            T::Line,  C::Blue,   P::Blue100Line,    G::NarrowMarsh,       false, "A marsh too narrow to show as an area (dotted)." },
            { 3100, "Indistinct marsh",        T::Area,  C::Blue,   P::Blue100Area,    G::MarshArea,         true,  "An indistinct or seasonal marsh." },
            { 3110, "Well, fountain or tank",  T::Point, C::Blue,   P::Blue100Point,   G::Well,              true,  "A prominent well, fountain or water tank." },
            { 3120, "Spring",                  T::Point, C::Blue,   P::Blue100Point,   G::Spring,            false, "The source of a stream (open downstream)." },
            { 3130, "Prominent water feature", T::Point, C::Blue,   P::Blue100Point,   G::ProminentWater,    true,  "A special water feature (defined on the map)." },
        }},
        // ── 4xx Vegetation (green / yellow) ──────────────────────────────────
        { "Vegetation", {
            { 4010, "Open land",               T::Area,  C::Yellow, P::Yellow100Area,  G::OpenLand,         false, "Open, runnable land (full yellow)." },
            { 4020, "Open land w/ scattered trees", T::Area, C::Yellow, P::Yellow100Area, G::OpenLandDots,  true,  "Open land with scattered trees (dotted yellow)." },
            { 4030, "Rough open land",         T::Area,  C::Yellow, P::Yellow50Area,   G::RoughOpen,        false, "Open land with rough vegetation (yellow 50%)." },
            { 4040, "Rough open w/ scattered trees", T::Area, C::Yellow, P::Yellow50Area, G::OpenLandDots, true,  "Rough open land with scattered trees." },
            { 4050, "Forest (runnable)",       T::Area,  C::Green,  P::WhiteOverGreenBrown, G::ForestWhite, false, "Typical open forest (white)." },
            { 4060, "Vegetation: slow run",    T::Area,  C::Green,  P::Green30Area,    G::VegGreen1,        false, "Dense vegetation, 60–80% speed (green 30%)." },
            { 4070, "Veg: slow, good visibility", T::Area, C::Green, P::Green30Area,   G::VegStripes,      true,  "Undergrowth, good visibility (green vertical lines)." },
            { 4080, "Vegetation: walk",        T::Area,  C::Green,  P::Green60Area,    G::VegGreen2,        false, "Dense vegetation, 20–60% speed (green 60%)." },
            { 4090, "Veg: walk, good visibility", T::Area, C::Green, P::Green60Area,   G::VegStripes,      true,  "Undergrowth, walk, good visibility." },
            { 4100, "Vegetation: fight",       T::Area,  C::Green,  P::Green100Area,   G::VegGreen3,        false, "Very dense vegetation (green 100%)." },
            { 4120, "Cultivated land",         T::Area,  C::Yellow, P::BlackCultivated, G::CultivatedLand,  true,  "Cultivated land (yellow + black dots)." },
            { 4130, "Orchard",                 T::Area,  C::Yellow, P::Yellow50Area,   G::Orchard,          true,  "An orchard or plantation (dot rows)." },
            { 4140, "Vineyard or similar",     T::Area,  C::Yellow, P::Yellow50Area,   G::Vineyard,         false, "A vineyard; rows show the planting direction." },
            { 4150, "Distinct cultivation boundary", T::Line, C::Black, P::Black100,   G::DistinctBoundary, false, "A boundary of cultivated land." },
            { 4160, "Distinct vegetation boundary (dots)", T::Line, C::Black, P::Black100, G::VegBoundaryDots, false, "A vegetation boundary within the forest (black dots)." },
            { 4161, "Distinct vegetation boundary (green)", T::Line, C::Green, P::DarkGreenLine, G::VegBoundaryGreen, false, "A vegetation boundary (dark-green dashed line)." },
            { 4170, "Prominent large tree",    T::Point, C::Green,  P::Green100Point,  G::LargeTree,        false, "A very large single tree (green ring)." },
            { 4180, "Prominent bush or tree",  T::Point, C::Green,  P::Green100Point,  G::KnollGreen,       false, "A bush or small tree (green dot)." },
            { 4190, "Prominent vegetation feature", T::Point, C::Green, P::Green100Point, G::VegFeatureX,  true,  "A special vegetation feature (green X)." },
        }},
        // ── 5xx Man-made features (black) ────────────────────────────────────
        { "Man-made features", {
            { 5010, "Paved area",              T::Area,  C::Brown,  P::Brown50PavedArea, G::PavedArea,       false, "A paved or sealed surface (brown 50%)." },
            { 5020, "Wide road",               T::Line,  C::Black,  P::Black100RoadOutline, G::WideRoad,     false, "A maintained road wider than 5 m (twin edges + brown)." },
            { 5030, "Road",                    T::Line,  C::Black,  P::Black100,       G::Road,              false, "A maintained road less than 5 m wide." },
            { 5040, "Vehicle track",           T::Line,  C::Black,  P::Black100,       G::VehicleTrack,      false, "A track for slow vehicles (long dashes)." },
            { 5050, "Footpath",                T::Line,  C::Black,  P::Black100,       G::Footpath,          false, "An easily runnable path." },
            { 5060, "Small footpath",          T::Line,  C::Black,  P::Black100,       G::SmallPath,         false, "A small runnable path." },
            { 5070, "Less distinct small footpath", T::Line, C::Black, P::Black100,    G::LessDistinctPath,  false, "A faint or seasonal small path (double dashes)." },
            { 5080, "Narrow ride",             T::Line,  C::Black,  P::Black100,       G::NarrowRide,        false, "A forest ride or linear trace (thin dashes)." },
            { 5090, "Railway",                 T::Line,  C::Black,  P::Black100,       G::Railway,           false, "A railway or railed track (twin rails + ties)." },
            { 5100, "Power line / cableway",   T::Line,  C::Black,  P::Black100,       G::PowerLine,         false, "A power line, cableway or skilift." },
            { 5110, "Major power line",        T::Line,  C::Black,  P::Black100,       G::MajorPowerLine,    false, "A major (double-line) power line." },
            { 5120, "Bridge / tunnel",         T::Point, C::Black,  P::Black100,       G::BridgeTunnel,      false, "A bridge or tunnel crossing (two brackets)." },
            { 5131, "Wall",                    T::Line,  C::Black,  P::Black100,       G::Wall,              false, "A stone, concrete or wooden wall." },
            { 5132, "Retaining wall",          T::Line,  C::Black,  P::Black100,       G::RetainingWall,     false, "A wall seen only from one side." },
            { 5140, "Ruined wall",             T::Line,  C::Black,  P::Black100,       G::RuinedWall,        false, "A ruined or less distinct wall." },
            { 5150, "Impassable wall",         T::Line,  C::Black,  P::Black100,       G::ImpassableWall,    false, "An impassable wall (>1.5 m)." },
            { 5160, "Fence",                   T::Line,  C::Black,  P::Black100,       G::Fence,             false, "A passable fence (<1.5 m, oblique pickets)." },
            { 5170, "Ruined fence",            T::Line,  C::Black,  P::Black100,       G::RuinedFence,       false, "A ruined or less distinct fence." },
            { 5180, "Impassable fence",        T::Line,  C::Black,  P::Black100,       G::ImpassableFence,   false, "An impassable fence (>1.5 m, double + pickets)." },
            { 5190, "Crossing point",          T::Point, C::Black,  P::Black100,       G::CrossingPointFence, false, "A way through/over a wall or fence." },
            { 5200, "Area that shall not be entered", T::Area, C::Yellow, P::Yellow100Green50, G::OutOfBounds, false, "An out-of-bounds area (olive)." },
            { 5210, "Building",                T::Area,  C::Black,  P::Black100,       G::Building,          false, "A building (black plan)." },
            { 5220, "Canopy",                  T::Area,  C::Black,  P::Black20Canopy,  G::Canopy,            false, "A roof / canopy over open ground (black 20%)." },
            { 5230, "Ruin",                    T::Area,  C::Black,  P::Black100,       G::Ruin,              false, "A ruined building (plan)." },
            { 5240, "High tower",              T::Point, C::Black,  P::Black100,       G::Tower,             true,  "A high tower or large pylon." },
            { 5250, "Small tower",             T::Point, C::Black,  P::Black100,       G::SmallTower,        true,  "A small tower or elevated platform." },
            { 5260, "Cairn",                   T::Point, C::Black,  P::Black100,       G::Cairn,             false, "A cairn, memorial or boundary stone." },
            { 5270, "Fodder rack",             T::Point, C::Black,  P::Black100,       G::FodderRack,        true,  "A fodder rack." },
            { 5280, "Prominent line feature",  T::Line,  C::Black,  P::Black100,       G::ProminentLineFeature,     false, "A prominent man-made line (low pipeline)." },
            { 5290, "Prominent uncrossable line feature", T::Line, C::Black, P::Black100, G::ProminentUncrossableLine, false, "An uncrossable man-made line (high pipeline)." },
            { 5300, "Prominent man-made feature - ring", T::Point, C::Black, P::Black100, G::FeatureRing,   false, "A special man-made point feature (ring)." },
            { 5310, "Prominent man-made feature - x", T::Point, C::Black, P::Black100,  G::FeatureX,         true,  "A special man-made point feature (X)." },
            { 5320, "Stairway",                T::Line,  C::Black,  P::Black100,       G::Stairway,          false, "A distinct stairway through the terrain." },
        }},
        // ── 6xx Technical symbols (black / blue) ─────────────────────────────
        { "Technical symbols", {
            { 6010, "Magnetic north line",     T::Line,  C::Black,  P::Black100,       G::MagneticNorth,     true,  "A magnetic-north reference line (black 0.1)." },
            { 6011, "Magnetic north line (blue)", T::Line, C::Blue, P::Blue100Line,    G::MagneticNorth,    true,  "A magnetic-north reference line (blue 0.12)." },
            { 6020, "Registration mark",       T::Point, C::Black,  P::Black100,       G::RegistrationMark,  false, "A printing registration cross (+)." },
            { 6030, "Spot height",             T::Point, C::Brown,  P::Brown100Point,  G::SpotHeight,        true,  "A precise height point." },
        }},
        // ── 7xx Course planning (purple overprint) ───────────────────────────
        { "Course (overprint)", {
            { 7010, "Start",                   T::Point, C::Purple, P::LowerPurple,    G::Start,            true,  "The start triangle (points to the first control)." },
            { 7020, "Map issue point",         T::Point, C::Purple, P::UpperPurple,    G::MapIssuePoint,    true,  "The map issue point (triangle)." },
            { 7030, "Control point",           T::Point, C::Purple, P::LowerPurple,    G::Control,          false, "A control circle (ø5.0 mm)." },
            { 7040, "Control number",          T::Text,  C::Purple, P::LowerPurple,    G::GenericPoint,     true,  "The control number (Arial 4.0 mm)." },
            { 7050, "Course line",             T::Line,  C::Purple, P::LowerPurple,    G::CourseLine,       false, "The line joining controls in order." },
            { 7060, "Finish",                  T::Point, C::Purple, P::LowerPurple,    G::Finish,           false, "The finish (double circle ø4 / ø6)." },
            { 7070, "Marked route",            T::Line,  C::Purple, P::UpperPurple,    G::MarkedRoute,      false, "A compulsory marked route (dashed)." },
            { 7080, "Out-of-bounds boundary",  T::Line,  C::Purple, P::LowerPurple,    G::OOBBoundary,      false, "A boundary that must not be crossed." },
            { 7090, "Out-of-bounds area",      T::Area,  C::Purple, P::LowerPurple,    G::OutOfBounds,      false, "A course area that must not be entered." },
            { 7100, "Crossing point",          T::Point, C::Purple, P::LowerPurple,    G::BridgeTunnel,     true,  "A marked crossing or passage." },
            { 7110, "Out-of-bounds route",     T::Line,  C::Purple, P::LowerPurple,    G::OOBRoute,         false, "A route that must not be used (× symbols)." },
            { 7120, "First aid post",          T::Point, C::Purple, P::LowerPurple,    G::FirstAid,         true,  "A first-aid post (purple cross)." },
            { 7130, "Refreshment point",       T::Point, C::Purple, P::LowerPurple,    G::Refreshment,      true,  "A refreshment point (cup)." },
            { 7150, "Continuing point",        T::Point, C::Purple, P::UpperPurple,    G::ContinuingPoint,  true,  "Continuation after a map exchange (triangle)." },
        }},
    };
    return kCatalogue;
}

const IofElement* IofFindByCode(int code) {
    for (const IofGroup& g : IofCatalogue())
        for (const IofElement& e : g.elements)
            if (e.code == code) return &e;
    return nullptr;
}

std::string IofElementLabel(const IofElement& e) {
    char buf[96];
    const int whole = e.code / 10, frac = e.code % 10;
    if (frac) std::snprintf(buf, sizeof(buf), "%d.%d  %s", whole, frac, e.name);
    else      std::snprintf(buf, sizeof(buf), "%d  %s", whole, e.name);
    return buf;
}

}  // namespace App::Modules::IofMapping

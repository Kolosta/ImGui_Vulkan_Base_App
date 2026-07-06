#include "IofSpec.h"
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
//  Full symbol descriptions, transcribed from the official ISOM 2017-2 text
//  (docs/Doc IOF/IOF ISOM 2017-2 Revision 6 January 2024.txt). The PDF extraction
//  splits words across line wraps and interleaves dimension cotes — the text here
//  is cleaned: words re-joined, stray cotes removed, kept as the running spec
//  prose for the Symbol Viewer's description panel. Keyed by ×10 ISOM code.
//
//  Not every symbol is filled in yet; IofFullDescription returns "" for missing
//  ones and the viewer then shows only the short `desc`.
// ─────────────────────────────────────────────────────────────────────────────

namespace App::Modules::IofMapping {

const char* IofFullDescription(int code) {
    static const std::unordered_map<int, const char*> kText = {
        { 1010,
          "A line joining points of equal height. The standard vertical interval "
          "between contours is 5 m. A contour interval of 2.5 m may be used for flat "
          "terrains.\n"
          "Slope lines may be drawn on the lower side of a contour line to clarify the "
          "direction of slope. When used, they should be placed in re-entrants.\n"
          "A closed contour represents a knoll or a depression. A depression has to "
          "have at least one slope line. Minimum height/depth should be 1 m.\n"
          "Relationships between adjacent contour lines are important. Adjacent "
          "contour lines show form and structure. Small details on contours should be "
          "avoided because they tend to hide the main features of the terrain.\n"
          "Prominent features such as depressions, re-entrants, spurs, earth banks and "
          "terraces may have to be exaggerated.\n"
          "Absolute height accuracy is of little importance, but the relative height "
          "difference between neighbouring features should be represented on the map "
          "as accurately as possible. It is permissible to alter the height of a "
          "contour slightly if this improves the representation of a feature. This "
          "deviation should not exceed 25% of the contour interval.\n"
          "The smallest bend in a contour line is 0.25 mm from centre to centre of the "
          "line. The mouth of a re-entrant or a spur must be wider than 0.5 mm from "
          "centre to centre of the line.\n"
          "The minimum length of a contour knoll is 0.9 mm and the minimum width is "
          "0.6 mm outside measure. A depression must accommodate a slope line, so the "
          "minimum length is 1.1 mm and the minimum width is 0.7 mm outside measure.\n"
          "Colour: brown." },
        { 1020,
          "Every fifth contour shall be drawn with a thicker line. This is an aid to "
          "the quick assessment of height difference and the overall shape of the "
          "terrain surface.\n"
          "An index contour may be represented as an ordinary contour line in an area "
          "with much detail. Small contour knolls and depressions are normally not "
          "represented using index contours.\n"
          "The index contour level must be carefully selected in flat terrain. The "
          "ideal level for the index contour is the central contour in the most "
          "prominent slopes.\n"
          "An index contour may have a height value assigned. It shall be orientated "
          "so that the top of the label is on the higher side of the contour. The "
          "index value shall be 1.5 mm high and represented in a sans-serif font.\n"
          "Colour: brown." },
        { 1030,
          "Form lines are used where more information must be given about the shape of "
          "the ground. Form lines are added only where representation would be "
          "incomplete with ordinary contours. They shall not be used as intermediate "
          "contours.\n"
          "Only one form line should be used between neighbouring contours. It is very "
          "important that a form line fits logically into the contour system, so the "
          "start and end of a form line should be parallel to the neighbouring "
          "contours. The gaps between the form line dashes must be placed on "
          "reasonably straight sections of the form line.\n"
          "Minimum length of a form line, knoll or depression: 1.1 mm outside "
          "measure.\n"
          "Colour: brown." },
        { 1040,
          "An earth bank is an abrupt change in ground level which can be clearly "
          "distinguished from its surroundings, e.g. gravel or sand pits, road and "
          "railway cuttings or embankments.\n"
          "Minimum height: 1 m. An earth bank may impact runnability. The tags "
          "represent the full extent of the earth bank.\n"
          "For long earth banks it is allowed to use tags shorter than the minimum "
          "length at the ends. If two earth banks are close together, tags may be "
          "omitted. Impassable earth banks shall be represented using symbol "
          "Impassable cliff (201).\n"
          "Minimum length: 0.6 mm.\nColour: brown." },
        { 1051,
          "Distinct earth wall. Minimum height: 1 m.\n"
          "Minimum length: 1.4 mm.\nColour: brown." },
        { 1052,
          "A retaining earth wall is an abrupt change in ground level which can be "
          "clearly distinguished from its surroundings, used for minor peat edges and "
          "cultivation terraces. If such a feature is higher than 1 m, it should be "
          "drawn with the symbol Earth bank (104).\n"
          "Minimum height: 0.5 m, minimum length (isolated): 1.4 mm.\n"
          "Colour: brown." },
        { 1060,
          "A ruined or less distinct earth wall. Minimum height: 0.5 m.\n"
          "Minimum length: two dashes (3.65 mm). If shorter, the symbol must be "
          "exaggerated to the minimum length or changed to symbol Earth wall (105).\n"
          "Colour: brown." },
        { 1070,
          "An erosion gully which is too small to be shown using symbol Earth bank "
          "(104) is shown by a single line. Contour lines may be broken around this "
          "symbol for better readability.\n"
          "Minimum depth: 1 m.\nMinimum length: 1.15 mm.\nColour: brown." },
        { 1080,
          "A small erosion gully or dry ditch. Minimum depth: 0.5 m.\n"
          "Minimum length (isolated): two dots (0.7 mm). Contour lines shall be broken "
          "around this symbol.\nColour: brown." },
        { 1090,
          "An obvious mound or knoll which cannot be drawn to scale with a contour. "
          "Minimum height: 1 m. The symbol shall not touch or overlap contours. "
          "Footprint: 7.5 m x 7.5 m.\nColour: brown." },
        { 1100,
          "An obvious elongated knoll which cannot be drawn to scale with a contour. "
          "Minimum height: 1 m. The symbol shall not touch or overlap contours. "
          "Footprint: 12 m x 6 m.\nColour: brown." },
        { 1110,
          "A small depression or hollow without steep sides that is too small to be "
          "shown by contours. Minimum depth: 1 m, minimum width: 2 m.\n"
          "Small depressions with steep sides are represented with symbol Pit (112). "
          "The symbol shall not touch or overlap other brown symbols. Location is the "
          "centre of gravity of the symbol, and the symbol is orientated to north.\n"
          "Footprint: 12 m x 6 m.\nColour: brown." },
        { 1120,
          "A pit or hole with distinct steep sides which cannot be shown to scale "
          "using symbol Earth bank (104). Minimum depth: 1 m, minimum width: 1 m.\n"
          "A pit larger than 5 m x 5 m should normally be exaggerated and drawn using "
          "Earth bank (104). Pits without steep sides are represented with symbol "
          "Small depression (111).\n"
          "The symbol shall not touch or overlap other brown symbols. Location is the "
          "centre of gravity of the symbol, and the symbol is orientated to north.\n"
          "Footprint: 10.5 m x 12 m.\nColour: brown." },
    };
    auto it = kText.find(code);
    return it != kText.end() ? it->second : "";
}

}  // namespace App::Modules::IofMapping

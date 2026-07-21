#pragma once

#include "IofSpec.h"
#include <Ink/Document/PathData.h>
#include <Ink/Document/Style.h>
#include <string>
#include <vector>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  IofStyles — every ISOM symbol expressed with CORE Ink tools only:
//    • line symbols  = a Stroke (width / cap / DASH pattern) + STROKE REPEATS
//                      (tags, pickets, pylon bars, dots astride the line);
//    • area symbols  = a MULTI-FILL stack (solid screens + INSTANCED fills for
//                      dot fields, stripe/hatch line-sets, cut holes) + an
//                      optional outline stroke;
//    • point symbols = small geometry parts (mm) with plain fills/strokes.
//  No custom renderer: a symbol is an ordinary Ink style, so everything the
//  core can do (editing, previews, vignettes) works on symbols for free.
//
//  Dimensions are ISOM millimetres at 1:15 000, multiplied by `scale`
//  (the map-scale factor: 1.0 at 1:15 000, 1.5 at 1:10 000…).
// ─────────────────────────────────────────────────────────────────────────────

// The document base unit is the css px (@96 dpi); ISOM dimensions are mm.
inline constexpr double kPxPerMm = 96.0 / 25.4;

// One drawable part of a symbol specimen (bottom→top).
struct SymbolPart {
    Ink::PathData path;
    Ink::Style    style;
    std::string   name;
};

struct SymbolDef {
    // The library SPECIMEN (centred on the local origin): the exact glyph for
    // a point symbol, a short sample segment for a line, a small swatch for an
    // area — what the vignettes and the cursor ghost show.
    std::vector<SymbolPart> parts;
    // Line symbols: the style the core pen draws with (dash + repeats).
    bool       isLine = false;
    Ink::Style lineStyle;
    // Area symbols: the style a drawn closed area receives (multi-fill +
    // instanced fills + optional outline).
    bool       isArea = false;
    Ink::Style areaStyle;
};

// Build the full symbol definition at the given map-scale factor.
SymbolDef BuildSymbol(const IofElement& e, float scale);

// Ink linear-light colour of a spot ink / screen tint (straight alpha).
Ink::Color InkColor(SpotColor c, float screenPct = 1.0f);
Ink::Color LayerInkColor(PrintLayer layer);

}  // namespace App::Modules::IofMapping

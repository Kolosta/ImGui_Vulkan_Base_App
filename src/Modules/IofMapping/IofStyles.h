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

// ── Explicit plate hint ──────────────────────────────────────────────────────
// Some plates cannot be told apart by colour: the four whites are all white,
// the road-outline black and the cultivated-land black are both K100, upper and
// lower purple are the same purple. A symbol that needs a SPECIFIC one of them
// tags the paint's swatch with a hint, and BindSwatches resolves it to that
// exact plate instead of colour-matching. The sentinel sits far above any real
// swatch id (allocated from 1), so it can never collide, and BindSwatches always
// overwrites it — it never reaches the document.
inline constexpr Ink::SwatchId kIofPlateHintBase = 0xF0F0F0F000000000ull;
inline Ink::SwatchId IofPlateHint(PrintLayer l) {
    return kIofPlateHintBase | (Ink::SwatchId)(unsigned)(int)l;
}
inline bool IofDecodePlateHint(Ink::SwatchId id, PrintLayer& out) {
    if ((id & ~(Ink::SwatchId)0xFF) != kIofPlateHintBase) return false;
    const int i = (int)(id & 0xFF);
    if (i < 0 || i >= kPrintLayerCount) return false;
    out = (PrintLayer)i;
    return true;
}

}  // namespace App::Modules::IofMapping

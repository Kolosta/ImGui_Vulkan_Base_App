#pragma once

#include "Ink/Document/Types.h"
#include <string>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Swatches — the document's named colour table (docs/Ink/DOCUMENT_MODEL.md
//  §Colour). A swatch is a colour used as a VARIABLE: every paint site in the
//  document may reference one instead of carrying a literal colour, so editing
//  the swatch restyles every user at once.
//
//  A swatch optionally carries the two things a PRINT workflow needs and a
//  screen colour cannot express:
//
//    • a PRINT ORDER — its position in the plate stack. Printing is sequential:
//      the lowest order is laid down first and ends up UNDERNEATH. When a
//      document renders in print order this replaces the layer tree's z as the
//      ordering that matters, the tree only breaking ties inside one plate.
//
//    • an OVERPRINT flag — whether the ink knocks out what is below it (the
//      default: all four channels are written, zeros included) or overprints
//      (only the channels this ink actually uses are written and the rest show
//      through, so the two inks mix on paper).
//
//  `ink` is the print truth; `display` is what the screen shows and is NOT a
//  naive conversion of it — a spot ink's on-screen appearance is its calibrated
//  PMS equivalent, which no CMYK→sRGB formula reproduces.
// ─────────────────────────────────────────────────────────────────────────────

using SwatchId = std::uint64_t;
inline constexpr SwatchId kNullSwatch = 0;

// Process-ink coverage, each channel a PERCENTAGE (0-100) as the print
// specifications quote them.
struct Cmyk {
    double c = 0.0, m = 0.0, y = 0.0, k = 0.0;

    bool Equals(const Cmyk& o) const {
        return c == o.c && m == o.m && y == o.y && k == o.k;
    }
    // Which channels this ink actually lays down — the OVERPRINT mask. A
    // channel at 0 is "not printed here" and lets whatever is below show
    // through; under knockout it would instead be written as zero (white).
    bool UsesC() const { return c > 0.0; }
    bool UsesM() const { return m > 0.0; }
    bool UsesY() const { return y > 0.0; }
    bool UsesK() const { return k > 0.0; }
};

// How the document is printed. It is not a rendering preference — it changes
// which ink a colour is actually made of, and therefore what a proof means.
//
//   Cmyk      — every colour is a mix of the four process inks. The cheap,
//               universal option; its one weakness is that thin lines built
//               from several inks lose sharpness (brown line work above all).
//   CmykPlusB — the hybrid the IOF specification recommends for offset: the
//               colours that carry a SPOT definition are pulled out of the
//               process build and printed as their own ink, the rest stays
//               CMYK. That removes exactly the weakness above.
//   Pms       — everything printed as spot inks. Solid 100 % colours and the
//               sharpest line work, but it overprints, costs more, and cannot
//               carry process artwork (logos, adverts) on the same sheet.
enum class PrintTechnique : std::uint8_t {
    Cmyk = 0, CmykPlusB = 1, Pms = 2 };

struct Swatch {
    SwatchId    id = kNullSwatch;
    std::string name;
    Color       display{ 0, 0, 0, 1 };   // linear-light straight (screen)
    Cmyk        ink;                     // the separation definition
    // Optional SPOT definition: the same colour as a named ink of its own
    // (PMS 471, PMS Purple…). Used instead of `ink` when the document prints
    // CMYK+B or PMS — a spot ink is laid solid and OVERPRINTS what is under it,
    // which is exactly what keeps a brown contour sharp.
    bool        hasSpot = false;
    std::string spotName;
    Color       spotDisplay{ 0, 0, 0, 1 };
    // A swatch without a print order takes no part in the plate stack: it
    // renders purely by the layer tree, and export treats it as artwork that
    // still has to be assigned an ink.
    bool        hasPrintOrder = false;
    int         printOrder = 0;          // lower prints FIRST (underneath)
    bool        overprint = false;
    // Locked swatches come from a specification (the module seeds the ISOM
    // plate list) — editable only deliberately, never renamed or removed by
    // an ordinary palette edit.
    bool        locked = false;
};

// How the canvas simulates PRINT. A display transform only — the document is
// never flattened or converted, so it stays parametric and editable.
//
//   Off         — the screen compositor: layers, blend modes, alpha.
//   Overprint   — the press. Each drawable goes back to its plate's CMYK and is
//                 laid down as ink over paper: a KNOCKOUT plate writes all four
//                 channels (its zeros erase what is under it), an OVERPRINTING
//                 one writes only the channels it actually uses and lets the
//                 rest show through, so the inks mix as they do on paper.
//   Separations — the same ink simulation with only the selected channels kept.
//   Flattener   — leaves the colours alone and reports the artwork that cannot
//                 go to a separation as it stands (translucent, blended, or
//                 cutting), for the app to mark up.
enum class PrintPreview : std::uint8_t {
    Off = 0, Overprint = 1, Separations = 2, Flattener = 3 };

// CMYK channel bits for the separations preview.
enum PrintChannel : std::uint8_t {
    PrintChannelC = 1u << 0, PrintChannelM = 1u << 1,
    PrintChannelY = 1u << 2, PrintChannelK = 1u << 3,
    PrintChannelAll = 0x0F,
};

// Ink over WHITE paper: each process ink subtracts from the paper. Good enough
// for a screen proof, and — unlike a naive CMYK→RGB matrix — it composes
// correctly when several inks land on the same spot.
//
// The subtraction is done in DISPLAY (sRGB) space, which is where ink coverage
// reads linearly to the eye: 50 % cyan should look like a mid tone, not like
// half the light energy. The result is then converted to the linear-light space
// the engine paints in — skipping that conversion is what makes a proof come
// out visibly washed out.
inline Color InkOverPaper(const Cmyk& c, std::uint8_t channels) {
    const double C = (channels & PrintChannelC) ? c.c * 0.01 : 0.0;
    const double M = (channels & PrintChannelM) ? c.m * 0.01 : 0.0;
    const double Y = (channels & PrintChannelY) ? c.y * 0.01 : 0.0;
    const double K = (channels & PrintChannelK) ? c.k * 0.01 : 0.0;
    const double k = 1.0 - K;
    return Color{ SrgbChannelToLinear((float)((1.0 - C) * k)),
                  SrgbChannelToLinear((float)((1.0 - M) * k)),
                  SrgbChannelToLinear((float)((1.0 - Y) * k)), 1.0f };
}

// A naive RGB→CMYK for artwork that has NO plate assigned. It is not a colour
// conversion anyone should print from — there is no profile behind it — but it
// lets unassigned work appear in a separations preview instead of silently
// passing through every channel filter, which is precisely what you want to
// notice before an export.
inline Cmyk NaiveCmyk(const Color& linear) {
    auto toSrgb = [](float v) {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return v <= 0.0031308f ? v * 12.92f
                               : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };
    const double r = toSrgb(linear.r), g = toSrgb(linear.g), b = toSrgb(linear.b);
    const double hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const double k = 1.0 - hi;
    if (k > 0.999) return { 0, 0, 0, 100 };
    const double d = 1.0 - k;
    return { (1.0 - r - k) / d * 100.0, (1.0 - g - k) / d * 100.0,
             (1.0 - b - k) / d * 100.0, k * 100.0 };
}

// Does this colour print as its own SPOT ink under `t`? CMYK builds everything
// from process inks; CMYK+B and PMS honour a spot definition when there is one.
inline bool SwatchPrintsSpot(const Swatch& sw, PrintTechnique t) {
    return sw.hasSpot && t != PrintTechnique::Cmyk;
}

// How much ink this plate actually lays down (0 = nothing, 1 = solid). It is
// what an OVERPRINTING plate is drawn at: where it lays nothing it must be
// fully transparent so the inks below show through, and where it is solid it
// covers. Taken from the coverages themselves rather than derived from the
// resulting RGB, which would confuse a pale ink with a sparse one.
inline float InkCoverage(const Cmyk& c, std::uint8_t channels) {
    double m = 0.0;
    if (channels & PrintChannelC) m = m > c.c ? m : c.c;
    if (channels & PrintChannelM) m = m > c.m ? m : c.m;
    if (channels & PrintChannelY) m = m > c.y ? m : c.y;
    if (channels & PrintChannelK) m = m > c.k ? m : c.k;
    return (float)(m * 0.01);
}

// Every paint site in Style.h pairs a literal `Color color` with a `SwatchId
// swatch`. The swatch WINS whenever it resolves; the literal is what a free
// colour uses, and the fallback if the swatch is ever deleted. They are two
// plain members rather than a wrapper type so that the hundreds of existing
// `.color = …` assignments (module symbol tables above all) stay valid.

} // namespace Ink

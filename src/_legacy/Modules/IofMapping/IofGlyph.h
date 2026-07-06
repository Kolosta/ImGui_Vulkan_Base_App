#pragma once

#include "IofSpec.h"
#include <Renderer/Document/Shape.h>
#include <string>
#include <vector>

namespace App::Modules::IofMapping {

// ─────────────────────────────────────────────────────────────────────────────
//  IofGlyph — bakes the exact ISOM vector glyph of a symbol into a Renderer::Shape
//  (multi-part geometry, mm at 1:15 000 × the map scale factor). Point symbols get
//  their precise glyph; line symbols get a default-length styled segment (dashes /
//  tags / double-lines baked as real parts); area symbols get a filled default
//  square. The shape is centred at the local origin {0,0} so the placement code
//  drops it under the cursor; isomCode and the fixed-size / north-orientation
//  locks are stamped on it.
//
//  `scale` is MapScaleFactor() (1.0 at 1:15 000, 1.5 at 1:10 000…).
// ─────────────────────────────────────────────────────────────────────────────

Renderer::Shape BuildSymbolShape(const IofElement& e, float scale);

// A compact, well-framed PREVIEW of a symbol for a small SQUARE thumbnail / ghost:
//   • Line  → a SHORT straight sample (a few mm) so the pattern is legible (the
//             full default length is far too long for a tiny square).
//   • Area  → a SMALL surface swatch sized to fill the square when zoomed.
//   • Point → the plain glyph.
// Built at `scale`; centred at the local origin. Use this (not BuildSymbolShape)
// for thumbnails and the placement mini-ghost so the preview reads clearly.
Renderer::Shape PreviewShape(const IofElement& e, float scale);

// ─────────────────────────────────────────────────────────────────────────────
//  Symbol documentation (for the Symbol Viewer editor).
//
//  A symbol shows one or more EXAMPLES (the plain glyph, a curved variant, the
//  minimum-length case, or the symbol combined with others to explain its use —
//  e.g. a bridge over a path + river), plus DYNAMIC DIMENSION annotations: each
//  carries the spec mm value so the viewer can (a) verify it against the ISOM
//  specification and (b) re-label it as the map scale changes (mm × scale).
// ─────────────────────────────────────────────────────────────────────────────

// One worked example: a list of already-built shapes (in mm × scale, positioned
// in local mm), plus a caption. Built by SymbolExamples() at the current scale.
struct SymbolExample {
    std::string                  caption;
    std::vector<Renderer::Shape> shapes;   // drawn together, local mm coords
};

// How a dimension callout is drawn: a measured span (two points, local mm) with
// a label = formatted (mm) value; `kind` hints the leader style.
struct DimAnnotation {
    enum class Kind { Length, Width, Diameter, Gap, Thickness, Angle };
    Kind          kind = Kind::Length;
    Renderer::Vec2 a{0, 0};   // span start (local mm, at scale 1.0)
    Renderer::Vec2 b{0, 0};   // span end   (local mm, at scale 1.0)
    float          mm   = 0;  // the spec value (mm @ 1:15 000); shown × scale
    std::string    label;     // optional override; else formatted from mm
};

// Examples for a symbol at `scale` (positions already × scale). At least one.
std::vector<SymbolExample> SymbolExamples(const IofElement& e, float scale);

// Dimension annotations for a symbol (positions in mm @ scale 1.0; the viewer
// multiplies by the preview zoom and labels mm × scale). May be empty.
std::vector<DimAnnotation> SymbolDims(const IofElement& e);

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolPlate — a precise worked-example "plate" reproducing the ISOM precise-
//  definition figures (like docs/Doc IOF .pdf). Everything is in DOCUMENT MILLI-
//  METRES at the ISOM base scale (1:15 000); the viewer draws it on one white sheet
//  with its own zoom/pan, so several examples share the same canvas. A plate may
//  hold many symbols (e.g. contour + index contour + slope ticks), free-positioned
//  RED dimension annotations, a measured tick, and a blue "minimum" frame.
// ─────────────────────────────────────────────────────────────────────────────

// A red dimension annotation placed directly on the plate (mm). It draws the
// measured span [a,b] with end ticks and a label at `labelPos`. `mm` feeds the
// scale-aware label when `label` is empty.
struct PlateDim {
    Renderer::Vec2 a{0,0}, b{0,0};   // measured span (plate mm); a==b → label only
    Renderer::Vec2 labelPos{0,0};    // label anchor (plate mm)
    float          mm = 0.0f;
    std::string    label;            // explicit text (e.g. "0.5 (CC)"); else from mm
    bool           withSpan = true;  // draw the measured span line + end ticks
};

// A thin blue rectangle outline (the "min." reference frame in the plates).
struct PlateFrame { Renderer::Vec2 min{0,0}, size{0,0}; };

// A whole worked-example plate. Shapes are pre-built in plate mm (positioned).
struct SymbolPlate {
    std::vector<Renderer::Shape> shapes;   // symbols, positioned in plate mm
    std::vector<PlateDim>        dims;      // red dimension callouts (plate mm)
    std::vector<PlateFrame>      frames;    // blue reference frames (plate mm)
    bool valid() const { return !shapes.empty(); }
};

// The precise plate for a symbol, built at `scale` (mm × scale). Empty/invalid if
// no hand-authored plate exists yet — the viewer then falls back to SymbolExamples.
SymbolPlate SymbolPlateFor(const IofElement& e, float scale);

}  // namespace App::Modules::IofMapping

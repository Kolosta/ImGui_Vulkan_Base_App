#pragma once

#include <cstdint>
#include <optional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Units — the document/display unit system + a Blender-style unit-aware value
//  parser/formatter, shared by every numeric input (UI::DragValue) and the
//  viewport rulers.
//
//  ONE stored value per field, in a fixed BASE unit — Length in CSS px @96 DPI,
//  Angle in degrees, Percent as a 0..1 fraction, Scalar as-is — converted to the
//  active display unit only for showing/editing. Switching the unit re-displays
//  with conversion; the stored geometry never changes.
//
//  The active DISPLAY unit is a (UnitSystem, LengthScale) pair resolved to a
//  concrete LengthUnit; each input declares its Quantity + LengthScale, and the
//  scope (document vs viewport) supplies the UnitSystem.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI::Units {

// The document / viewport unit SYSTEM (family). Each input's display unit is
// resolved WITHIN the active system from the input's LengthScale.
enum class UnitSystem  : std::uint8_t { Metric = 0, Imperial, Typographic, Pixel };
// What a numeric input represents (drives the base unit + suffix + parsing).
enum class Quantity    : std::uint8_t { Scalar = 0, Length, Angle, Percent };
// An input's size class within a system (Metric mm vs m, Imperial in vs ft).
enum class LengthScale : std::uint8_t { Normal = 0, Large };
// A concrete length unit (the display target; base is always Px).
enum class LengthUnit  : std::uint8_t { Px = 0, Pt, Mm, Cm, M, In, Ft };

constexpr int kUnitSystemCount = 4;

// (system, scale) → the concrete length unit an input displays in.
LengthUnit  Resolve(UnitSystem sys, LengthScale scale);
// Base-px per one of the unit (px 1, pt 1.3333, mm 3.7795, …).
double      PxPer(LengthUnit u);
const char* Name(LengthUnit u);         // "mm", "pt", …
const char* SystemName(UnitSystem s);   // "Metric", "Imperial", …

// The trailing suffix shown after the number (" mm" / " °" / " %" / "").
std::string Suffix(Quantity q, UnitSystem sys, LengthScale scale);
// The bare unit label, no leading space ("mm" / "°" / "%" / "").
std::string UnitLabel(Quantity q, UnitSystem sys, LengthScale scale);

// BASE value → the numeric value shown in the display unit (raw, unformatted),
// and its inverse (a display-unit number → the base value). Used by the drag
// field so its maths runs in display units and only stores the base.
double DisplayValue(double base, Quantity q, UnitSystem sys, LengthScale scale);
double DisplayToBase(double disp, Quantity q, UnitSystem sys, LengthScale scale);

// Format a BASE value for display, WITH the unit suffix: "1741.690 mm".
std::string Format(double base, Quantity q, UnitSystem sys, LengthScale scale,
                   int decimals);
// Just the numeric part in the display unit, WITHOUT the suffix and at full
// precision — the seed shown when a field enters manual text edit.
std::string FormatNumber(double base, Quantity q, UnitSystem sys,
                         LengthScale scale);

// Parse a user string → BASE value. Accepts arithmetic EXPRESSIONS with optional
// per-operand unit tags (Blender-style): "15*10+2", "(9.4+3,4)*78", "15,6 pt",
// "10px+15pt/2". Comma or dot decimals; spaces around unit tokens ignored; unit
// tokens are case-insensitive. A bare number uses the input's resolved display
// unit; a compatible unit token converts. Returns nullopt on a syntax error or
// an incompatible unit for the Quantity.
std::optional<double> Parse(const std::string& text, Quantity q, UnitSystem sys,
                            LengthScale scale);

// The current DOCUMENT system — the convenience default most inputs use (the app
// pushes the project's system here each frame). Per-viewport inputs pass their
// own system explicitly instead.
UnitSystem DocumentSystem();
void       SetDocumentSystem(UnitSystem s);

} // namespace UI::Units

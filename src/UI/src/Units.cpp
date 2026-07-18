#include "UI/Units.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace UI::Units {

namespace {
UnitSystem g_docSystem = UnitSystem::Pixel;
constexpr double kPi = 3.14159265358979323846;

// One of the display unit → base-px factor.
double PxPerU(LengthUnit u) {
    switch (u) {
        case LengthUnit::Px: return 1.0;
        case LengthUnit::Pt: return 96.0 / 72.0;          // 1.33333
        case LengthUnit::Mm: return 96.0 / 25.4;          // 3.77953
        case LengthUnit::Cm: return 960.0 / 25.4;         // 37.7953
        case LengthUnit::M:  return 96000.0 / 25.4;       // 3779.53
        case LengthUnit::In: return 96.0;
        case LengthUnit::Ft: return 1152.0;
    }
    return 1.0;
}

// Format a number to `decimals` fixed places (display at rest / with suffix).
std::string FixedNum(double v, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 12) decimals = 12;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

// Format a number at high precision, trimming trailing zeros (manual-edit seed).
std::string TrimNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

// ── Expression parser (recursive descent) ─────────────────────────────────────
// Each PRIMARY (number + optional unit) is converted to the field's BASE value;
// arithmetic then runs in base. A bare number uses the input's resolved display
// unit; a unit token converts. Incompatible tokens fail the whole parse.
struct Parser {
    const std::string& s;   // pre-normalised (',' → '.', ASCII lowercased)
    std::size_t i = 0;
    bool ok = true;
    Quantity q;
    UnitSystem sys;
    LengthScale scale;

    void skip() { while (i < s.size() && s[i] == ' ') ++i; }
    char peek() { skip(); return i < s.size() ? s[i] : '\0'; }

    // Convert `num` tagged with `unit` (may be empty = the field's display unit)
    // into the field's base value. Sets ok=false on an incompatible unit.
    double toBase(double num, const std::string& unit) {
        switch (q) {
            case Quantity::Length: {
                if (unit.empty()) return num * PxPerU(Resolve(sys, scale));
                LengthUnit u;
                if      (unit == "px")            u = LengthUnit::Px;
                else if (unit == "pt")            u = LengthUnit::Pt;
                else if (unit == "mm")            u = LengthUnit::Mm;
                else if (unit == "cm")            u = LengthUnit::Cm;
                else if (unit == "m")             u = LengthUnit::M;
                else if (unit == "in")            u = LengthUnit::In;
                else if (unit == "ft")            u = LengthUnit::Ft;
                else { ok = false; return 0.0; }
                return num * PxPerU(u);
            }
            case Quantity::Angle:
                if (unit.empty() || unit == "deg" || unit == "d") return num;
                if (unit == "rad" || unit == "r") return num * 180.0 / kPi;
                ok = false; return 0.0;
            case Quantity::Percent:
                if (unit.empty() || unit == "pct") return num / 100.0;
                ok = false; return 0.0;
            case Quantity::Scalar:
                if (unit.empty()) return num;
                ok = false; return 0.0;
        }
        return num;
    }

    // A unit token after a number: a run of ASCII letters, or the byte sequences
    // for '°' (0xC2 0xB0), '%', '"' (=in), '\'' (=ft). Returns "" if none.
    std::string readUnit() {
        // NB: no skip() — a unit must abut its number (spaces already collapsed
        // by peek/skip callers, but "10 px" is allowed: skip leading spaces).
        skip();
        if (i + 1 < s.size() && (unsigned char)s[i] == 0xC2 &&
            (unsigned char)s[i + 1] == 0xB0) { i += 2; return "deg"; }
        if (i < s.size() && s[i] == '%')  { ++i; return "pct"; }
        if (i < s.size() && s[i] == '"')  { ++i; return "in"; }
        if (i < s.size() && s[i] == '\'') { ++i; return "ft"; }
        std::string u;
        while (i < s.size() && std::isalpha((unsigned char)s[i])) u += s[i++];
        return u;
    }

    double primary() {
        skip();
        if (peek() == '(') {
            ++i;
            double v = expr();
            if (peek() == ')') ++i; else ok = false;
            return v;
        }
        // A named CONSTANT (pi / tau / e), Blender-style: a bare dimensionless
        // number in the field's display unit (a trailing unit tag still allowed,
        // e.g. "pi rad"). Recognised only when a LETTER starts the primary — a
        // unit tag can never appear here (it always follows a number).
        if (i < s.size() && std::isalpha((unsigned char)s[i])) {
            std::string id;
            while (i < s.size() && std::isalpha((unsigned char)s[i])) id += s[i++];
            double cval;
            if      (id == "pi")  cval = kPi;
            else if (id == "tau") cval = 2.0 * kPi;
            else if (id == "e")   cval = 2.718281828459045;
            else { ok = false; return 0.0; }
            return toBase(cval, readUnit());
        }
        // A number (digits + one dot).
        const char* start = s.c_str() + i;
        char* end = nullptr;
        double num = std::strtod(start, &end);
        if (end == start) { ok = false; return 0.0; }
        i += (std::size_t)(end - start);
        return toBase(num, readUnit());
    }

    double factor() {
        skip();
        if (peek() == '-') { ++i; return -factor(); }
        if (peek() == '+') { ++i; return  factor(); }
        return primary();
    }

    double term() {
        double v = factor();
        for (;;) {
            const char c = peek();
            if (c == '*') { ++i; v *= factor(); }
            else if (c == '/') { ++i; double d = factor(); v = d != 0.0 ? v / d : 0.0; }
            else break;
        }
        return v;
    }

    double expr() {
        double v = term();
        for (;;) {
            const char c = peek();
            if (c == '+') { ++i; v += term(); }
            else if (c == '-') { ++i; v -= term(); }
            else break;
        }
        return v;
    }
};
} // namespace

LengthUnit Resolve(UnitSystem sys, LengthScale scale) {
    const bool large = scale == LengthScale::Large;
    switch (sys) {
        case UnitSystem::Metric:      return large ? LengthUnit::M  : LengthUnit::Mm;
        case UnitSystem::Imperial:    return large ? LengthUnit::Ft : LengthUnit::In;
        case UnitSystem::Typographic: return LengthUnit::Pt;
        case UnitSystem::Pixel:       return LengthUnit::Px;
    }
    return LengthUnit::Px;
}

double PxPer(LengthUnit u) { return PxPerU(u); }

const char* Name(LengthUnit u) {
    switch (u) {
        case LengthUnit::Px: return "px";
        case LengthUnit::Pt: return "pt";
        case LengthUnit::Mm: return "mm";
        case LengthUnit::Cm: return "cm";
        case LengthUnit::M:  return "m";
        case LengthUnit::In: return "in";
        case LengthUnit::Ft: return "ft";
    }
    return "px";
}

const char* SystemName(UnitSystem s) {
    switch (s) {
        case UnitSystem::Metric:      return "Metric";
        case UnitSystem::Imperial:    return "Imperial";
        case UnitSystem::Typographic: return "Typographic";
        case UnitSystem::Pixel:       return "Pixel";
    }
    return "Pixel";
}

std::string Suffix(Quantity q, UnitSystem sys, LengthScale scale) {
    switch (q) {
        case Quantity::Length:  return std::string(" ") + Name(Resolve(sys, scale));
        case Quantity::Angle:   return " \xC2\xB0";   // ' °'
        case Quantity::Percent: return " %";
        case Quantity::Scalar:  return "";
    }
    return "";
}

std::string UnitLabel(Quantity q, UnitSystem sys, LengthScale scale) {
    switch (q) {
        case Quantity::Length:  return Name(Resolve(sys, scale));
        case Quantity::Angle:   return "\xC2\xB0";   // '°'
        case Quantity::Percent: return "%";
        case Quantity::Scalar:  return "";
    }
    return "";
}

// Base value → the number shown in the display unit.
static double ToDisplay(double base, Quantity q, UnitSystem sys, LengthScale scale) {
    switch (q) {
        case Quantity::Length:  return base / PxPerU(Resolve(sys, scale));
        case Quantity::Angle:   return base;             // degrees
        case Quantity::Percent: return base * 100.0;     // fraction → %
        case Quantity::Scalar:  return base;
    }
    return base;
}

double DisplayValue(double base, Quantity q, UnitSystem sys, LengthScale scale) {
    return ToDisplay(base, q, sys, scale);
}

double DisplayToBase(double disp, Quantity q, UnitSystem sys, LengthScale scale) {
    switch (q) {
        case Quantity::Length:  return disp * PxPerU(Resolve(sys, scale));
        case Quantity::Angle:   return disp;
        case Quantity::Percent: return disp / 100.0;
        case Quantity::Scalar:  return disp;
    }
    return disp;
}

std::string Format(double base, Quantity q, UnitSystem sys, LengthScale scale,
                   int decimals) {
    return FixedNum(ToDisplay(base, q, sys, scale), decimals) +
           Suffix(q, sys, scale);
}

std::string FormatNumber(double base, Quantity q, UnitSystem sys,
                         LengthScale scale) {
    return TrimNum(ToDisplay(base, q, sys, scale));
}

std::optional<double> Parse(const std::string& text, Quantity q, UnitSystem sys,
                            LengthScale scale) {
    // Normalise: ',' → '.', ASCII upper → lower (unit tokens are case-free);
    // non-ASCII bytes (the '°' sign) pass through untouched.
    std::string s;
    s.reserve(text.size());
    for (char c : text) {
        if (c == ',') s += '.';
        else if (c >= 'A' && c <= 'Z') s += (char)(c - 'A' + 'a');
        else s += c;
    }
    // Empty / whitespace only → no value.
    bool anyGraph = false;
    for (char c : s) if (c != ' ') { anyGraph = true; break; }
    if (!anyGraph) return std::nullopt;

    Parser p{ s, 0, true, q, sys, scale };
    double v = p.expr();
    p.skip();
    if (!p.ok || p.i != s.size()) return std::nullopt;   // trailing junk / error
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

UnitSystem DocumentSystem() { return g_docSystem; }
void SetDocumentSystem(UnitSystem s) { g_docSystem = s; }

} // namespace UI::Units

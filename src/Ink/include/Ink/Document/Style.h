#pragma once

#include "Ink/Document/Types.h"
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Style — unified fill + stroke (docs/Ink/DOCUMENT_MODEL.md §4): a node
//  carries ordered LISTS of fills and strokes; both take any Paint. A stroke
//  is geometry generation (GEOMETRY.md §2) painted by the same machinery as a
//  fill — there is no technical shape/stroke split.
//
//  Lot 2 scope: solid paints, Center stroke alignment, Butt caps, Bevel-ish
//  joins. Inside/Outside, Round/Square caps, Miter joins, dashes and
//  viewport-space widths complete in Lot 3; Pattern/Image/Gradient paints in
//  Lots 5/6/later. The enums already carry the full vocabulary so documents
//  built now stay valid.
// ─────────────────────────────────────────────────────────────────────────────

// Solid color paint (LINEAR-light, straight alpha — premultiplication happens
// when the scene builds the GPU paint table).
struct Paint {
    Color color{ 0, 0, 0, 1 };
};

enum class FillRule : std::uint8_t { NonZero = 0, EvenOdd = 1 };

struct Fill {
    Paint    paint;
    FillRule rule    = FillRule::NonZero;
    bool     enabled = true;
};

enum class StrokeAlign : std::uint8_t { Center = 0, Inside = 1, Outside = 2 };
enum class CapStyle    : std::uint8_t { Butt = 0, Round = 1, Square = 2 };
enum class JoinStyle   : std::uint8_t { Miter = 0, Round = 1, Bevel = 2 };

struct Stroke {
    Paint       paint;
    double      width      = 1.0;               // node-local units
    StrokeAlign align      = StrokeAlign::Center;
    CapStyle    cap        = CapStyle::Butt;
    JoinStyle   join       = JoinStyle::Bevel;
    double      miterLimit = 4.0;
    bool        enabled    = true;

    // Geometry-affecting parameters only (paints excluded — a color edit must
    // NOT re-tessellate; docs/Ink/GEOMETRY.md §3).
    std::uint64_t GeometryHash() const {
        std::uint64_t h = 0x57120CEULL;
        h = HashDouble(width, h);
        const std::uint8_t packed[3] = { (std::uint8_t)align, (std::uint8_t)cap,
                                         (std::uint8_t)join };
        h = HashBytes(packed, sizeof packed, h);
        h = HashDouble(miterLimit, h);
        return h;
    }
};

struct Style {
    std::vector<Fill>   fills;
    std::vector<Stroke> strokes;

    static Style Filled(const Color& linearStraight) {
        Style s;
        s.fills.push_back({ Paint{ linearStraight } });
        return s;
    }
    static Style Stroked(const Color& linearStraight, double width) {
        Style s;
        Stroke st;
        st.paint.color = linearStraight;
        st.width = width;
        s.strokes.push_back(st);
        return s;
    }
    Style& WithStroke(const Color& linearStraight, double width) {
        Stroke st;
        st.paint.color = linearStraight;
        st.width = width;
        strokes.push_back(st);
        return *this;
    }
};

} // namespace Ink

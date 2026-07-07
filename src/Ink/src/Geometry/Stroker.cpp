#include "Ink/Geometry/Geometry.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Stroke tessellation v1 (docs/Ink/GEOMETRY.md §2 — Lot 2 slice): Center
//  alignment, Butt caps, Bevel joins. Each polyline segment becomes a quad
//  offset ±w/2 along its normal; at every interior vertex a bevel triangle
//  fills the outer gap between adjacent segment quads. The outline overlaps
//  itself at tight joins — rendered with the fill pipeline this can darken a
//  translucent stroke slightly (documented v1 limit; exact outline union is a
//  later quality lot). Inside/Outside alignment, Round/Square caps, Miter
//  joins and dashes complete the stroker in Lot 3.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::geom {
namespace {

struct V2 { double x, y; };

V2 Normal(DVec2 a, DVec2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) return { 0.0, 0.0 };
    return { -dy / len, dx / len };
}

} // namespace

Mesh TessellateStroke(const std::vector<Polyline>& polylines,
                      const Stroke& stroke) {
    Mesh out;
    const double h = stroke.width * 0.5;
    if (h <= 0.0) return out;

    auto vertex = [&](double x, double y) {
        out.positions.push_back((float)x);
        out.positions.push_back((float)y);
        return out.VertexCount() - 1;
    };
    auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
    };

    for (const Polyline& pl : polylines) {
        const std::size_t n = pl.points.size();
        if (n < 2) continue;
        const std::size_t segCount = pl.closed ? n : n - 1;

        std::uint32_t firstL = 0, firstR = 0;   // first segment's start edge
        std::uint32_t prevL = 0, prevR = 0;     // previous segment's end edge
        V2 prevNormal{ 0, 0 };

        for (std::size_t s = 0; s < segCount; ++s) {
            const DVec2 a = pl.points[s];
            const DVec2 b = pl.points[(s + 1) % n];
            const V2 nm = Normal(a, b);
            if (nm.x == 0.0 && nm.y == 0.0) continue;   // zero-length segment

            const std::uint32_t aL = vertex(a.x + nm.x * h, a.y + nm.y * h);
            const std::uint32_t aR = vertex(a.x - nm.x * h, a.y - nm.y * h);
            const std::uint32_t bL = vertex(b.x + nm.x * h, b.y + nm.y * h);
            const std::uint32_t bR = vertex(b.x - nm.x * h, b.y - nm.y * h);
            tri(aL, bL, bR);
            tri(aL, bR, aR);

            if (s == 0) {
                firstL = aL; firstR = aR;
            } else {
                // Bevel join at `a`: fill the outer wedge between the previous
                // segment's end edge and this segment's start edge. The turn
                // side picks which pair gapes; covering both sides is cheap
                // and keeps the join watertight.
                tri(prevL, aL, prevR);
                tri(prevR, aL, aR);
            }
            prevL = bL; prevR = bR;
            prevNormal = nm;
        }
        (void)prevNormal;

        // Seam join of a closed polyline (last segment back to the first).
        if (pl.closed && out.VertexCount() >= 4) {
            tri(prevL, firstL, prevR);
            tri(prevR, firstL, firstR);
        }
        // Open ends: Butt caps = nothing to add (Round/Square in Lot 3).
    }
    return out;
}

} // namespace Ink::geom

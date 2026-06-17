#include "Renderer/Document/Shape.h"

namespace Renderer {

// Cubic Bézier circle/ellipse-quadrant constant.
static constexpr float kKappa = 0.5522847498f;

void Part::EnsurePath() {
    if (!IsParametric()) return;

    Path p;
    if (kind == ShapeKind::Rectangle) {
        const float x0 = pos.x,          y0 = pos.y;
        const float x1 = pos.x + size.x, y1 = pos.y + size.y;
        Vec2 corners[4] = { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} };
        for (Vec2 c : corners) {
            Node n(c);
            n.mode = HandleMode::Vector;
            p.nodes.push_back(n);
        }
        p.closed = true;
        kind = ShapeKind::Path;
    } else {  // Ellipse → 4 cubic quadrants
        const float cx = pos.x + size.x * 0.5f;
        const float cy = pos.y + size.y * 0.5f;
        const float rx = size.x * 0.5f, ry = size.y * 0.5f;
        struct A { Vec2 pos; Vec2 tan; };
        A as[4] = {
            { {cx + rx, cy}, {0.0f,  kKappa * ry} },
            { {cx, cy + ry}, {-kKappa * rx, 0.0f} },
            { {cx - rx, cy}, {0.0f, -kKappa * ry} },
            { {cx, cy - ry}, {kKappa * rx, 0.0f} },
        };
        for (const A& a : as) {
            Node n(a.pos);
            n.hasIn = n.hasOut = true;
            n.mode  = HandleMode::AlignedMirrored;   // collinear + equal (symmetric)
            n.hOut  = { a.pos.x + a.tan.x, a.pos.y + a.tan.y };
            n.hIn   = { a.pos.x - a.tan.x, a.pos.y - a.tan.y };
            p.nodes.push_back(n);
        }
        p.closed = true;
        kind = ShapeKind::Path;
    }
    path = std::move(p);
}

} // namespace Renderer

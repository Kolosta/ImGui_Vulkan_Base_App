#include "Ink/View/OverlayList.h"

#include <cmath>

namespace Ink {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// The shared unit octagon (cos/sin at 8 steps) — computed ONCE at load, so a
// marker expands with only adds/muls, never per-item trig. This is what makes
// thousands of identical glyphs cheap (see OverlayList::AddMarker).
struct UnitOctagon {
    Vec2 p[8];
    UnitOctagon() {
        for (int i = 0; i < 8; ++i) {
            const float a = (2.0f * kPi * (float)i) / 8.0f;
            p[i] = { std::cos(a), std::sin(a) };
        }
    }
};
const UnitOctagon kUnitOct;
} // namespace

void OverlayList::AddTriangle(Vec2 a, Vec2 b, Vec2 c, const Color& col) {
    vertices_.push_back({ a.x, a.y, col.r, col.g, col.b, col.a });
    vertices_.push_back({ b.x, b.y, col.r, col.g, col.b, col.a });
    vertices_.push_back({ c.x, c.y, col.r, col.g, col.b, col.a });
}

void OverlayList::AddQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, const Color& col) {
    AddTriangle(a, b, c, col);
    AddTriangle(a, c, d, col);
}

void OverlayList::AddLine(Vec2 a, Vec2 b, const Color& col, float thickness) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    const float h = 0.5f * thickness;
    const float nx = -dy / len * h, ny = dx / len * h;
    AddQuad({ a.x + nx, a.y + ny }, { b.x + nx, b.y + ny },
            { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny }, col);
}

void OverlayList::AddRectFilled(Vec2 min, Vec2 max, const Color& col) {
    AddQuad({ min.x, min.y }, { max.x, min.y }, { max.x, max.y },
            { min.x, max.y }, col);
}

void OverlayList::AddRect(Vec2 min, Vec2 max, const Color& col, float thickness) {
    const float h = 0.5f * thickness;
    // Four edge bands, corners covered by extending the horizontal edges.
    AddRectFilled({ min.x - h, min.y - h }, { max.x + h, min.y + h }, col);
    AddRectFilled({ min.x - h, max.y - h }, { max.x + h, max.y + h }, col);
    AddRectFilled({ min.x - h, min.y + h }, { min.x + h, max.y - h }, col);
    AddRectFilled({ max.x - h, min.y + h }, { max.x + h, max.y - h }, col);
}

void OverlayList::AddCircle(Vec2 c, float radius, const Color& col,
                            float thickness, int segments) {
    const float h = 0.5f * thickness;
    float prevX = c.x + radius, prevY = c.y;
    for (int i = 1; i <= segments; ++i) {
        const float a = (2.0f * kPi * (float)i) / (float)segments;
        const float x = c.x + std::cos(a) * radius;
        const float y = c.y + std::sin(a) * radius;
        AddLine({ prevX, prevY }, { x, y }, col, 2.0f * h);
        prevX = x; prevY = y;
    }
}

void OverlayList::BeginDedup() {
    if (dedupOpen_) EndDedup();
    dedups_.push_back({ (std::uint32_t)vertices_.size(), 0 });
    dedupOpen_ = true;
}

void OverlayList::EndDedup() {
    if (!dedupOpen_) return;
    dedupOpen_ = false;
    DedupGroup& g = dedups_.back();
    g.count = (std::uint32_t)vertices_.size() - g.first;
    if (g.count == 0) dedups_.pop_back();   // empty group — drop it
}

void OverlayList::AddCircleFilled(Vec2 c, float radius, const Color& col,
                                  int segments) {
    float prevX = c.x + radius, prevY = c.y;
    for (int i = 1; i <= segments; ++i) {
        const float a = (2.0f * kPi * (float)i) / (float)segments;
        const float x = c.x + std::cos(a) * radius;
        const float y = c.y + std::sin(a) * radius;
        AddTriangle({ c.x, c.y }, { prevX, prevY }, { x, y }, col);
        prevX = x; prevY = y;
    }
}

void OverlayList::AddMarker(Vec2 c, float r, const Color& col,
                            MarkerShape shape, float thickness) {
    auto oct = [&](int i) -> Vec2 {
        return { c.x + kUnitOct.p[i & 7].x * r, c.y + kUnitOct.p[i & 7].y * r };
    };
    switch (shape) {
        case MarkerShape::DotFilled:               // a 3-verts-each quad
            AddRectFilled({ c.x - r, c.y - r }, { c.x + r, c.y + r }, col);
            break;
        case MarkerShape::DiscFilled:              // filled octagon (8 tris)
            for (int i = 0; i < 8; ++i)
                AddTriangle(c, oct(i), oct(i + 1), col);
            break;
        case MarkerShape::RingOutline:             // octagon outline
            for (int i = 0; i < 8; ++i)
                AddLine(oct(i), oct(i + 1), col, thickness);
            break;
        case MarkerShape::SquareOutline:
            AddRect({ c.x - r, c.y - r }, { c.x + r, c.y + r }, col, thickness);
            break;
        case MarkerShape::TriangleOutline: {
            const Vec2 p0{ c.x, c.y - r }, p1{ c.x - r, c.y + r },
                       p2{ c.x + r, c.y + r };
            AddLine(p0, p1, col, thickness);
            AddLine(p1, p2, col, thickness);
            AddLine(p2, p0, col, thickness);
            break;
        }
        case MarkerShape::DiamondOutline: {
            const Vec2 p0{ c.x, c.y - r }, p1{ c.x + r, c.y },
                       p2{ c.x, c.y + r }, p3{ c.x - r, c.y };
            AddLine(p0, p1, col, thickness);
            AddLine(p1, p2, col, thickness);
            AddLine(p2, p3, col, thickness);
            AddLine(p3, p0, col, thickness);
            break;
        }
    }
}

} // namespace Ink

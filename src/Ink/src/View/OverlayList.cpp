#include "Ink/View/OverlayList.h"

#include <cmath>

namespace Ink {

namespace {
constexpr float kPi = 3.14159265358979323846f;
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

} // namespace Ink

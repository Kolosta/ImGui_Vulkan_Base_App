#pragma once

#include "Ink/Core/Math.h"
#include <cstdint>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  OverlayList — the per-frame editor-overlay builder (docs/Ink/RENDER_GRAPH.md
//  §OverlayPass). The application fills it each frame with screen-space
//  primitives (selection outlines, handles, guides, crosshairs…); the engine
//  draws the resulting triangles into the view's MSAA target, over the
//  content, under nothing — the canvas is 100 % Vulkan.
//
//  CPU-only: primitives are emitted straight as a triangle list (no indices).
//  Positions are in view pixels; colors are LINEAR PREMULTIPLIED (the app
//  converts its sRGB design-token colors via SrgbToLinearPremultiplied).
//  MSAA provides the anti-aliasing for Lot 1.
// ─────────────────────────────────────────────────────────────────────────────
class OverlayList {
public:
    struct Vertex {
        float x, y;          // view px
        float r, g, b, a;    // linear premultiplied
    };

    void Clear() { vertices_.clear(); }
    bool Empty() const { return vertices_.empty(); }

    void AddTriangle(Vec2 a, Vec2 b, Vec2 c, const Color& col);
    void AddQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, const Color& col); // convex, CCW/CW
    void AddLine(Vec2 a, Vec2 b, const Color& col, float thickness = 1.0f);
    void AddRectFilled(Vec2 min, Vec2 max, const Color& col);
    void AddRect(Vec2 min, Vec2 max, const Color& col, float thickness = 1.0f);
    void AddCircle(Vec2 centre, float radius, const Color& col,
                   float thickness = 1.0f, int segments = 48);
    void AddCircleFilled(Vec2 centre, float radius, const Color& col,
                         int segments = 48);

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    std::size_t ByteSize() const { return vertices_.size() * sizeof(Vertex); }

private:
    std::vector<Vertex> vertices_;
};

} // namespace Ink

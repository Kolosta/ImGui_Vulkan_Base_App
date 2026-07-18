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
    // A DEDUP GROUP: a contiguous vertex range whose triangles blend as ONE
    // translucent unit — self-overlapping triangles draw exactly once (the
    // renderer plays the range through the stroke-dedup stencil pipeline with
    // its own tag). Used by translucent MESH previews (a tessellated stroke's
    // join overlaps must not double-darken). Groups may not nest.
    struct DedupGroup {
        std::uint32_t first = 0;   // first vertex of the range
        std::uint32_t count = 0;   // vertex count (multiple of 3)
    };

    void Clear() { vertices_.clear(); dedups_.clear(); dedupOpen_ = false; }
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

    // ── Instanced MARKERS (many identical small glyphs) ───────────────────────
    // For dense point sets — grid crossings, snap candidates, vertex dots — where
    // AddCircle* is far too heavy (a filled 48-gon is 144 verts + 96 trig calls
    // PER dot). Every marker reuses ONE precomputed unit template, expanded with
    // adds/muls only (no per-item trig) into a handful of verts — the CPU analog
    // of instancing (the overlay is already ONE batched draw, so the cost that
    // matters is vertices, not draw calls). `radius` is the glyph half-extent.
    enum class MarkerShape : std::uint8_t {
        DotFilled,        // tiny filled square — cheapest, for dense grids (6 verts)
        DiscFilled,       // filled octagon — a round point
        RingOutline,      // octagon outline
        SquareOutline,
        TriangleOutline,  // apex up
        DiamondOutline,
    };
    void AddMarker(Vec2 centre, float radius, const Color& col,
                   MarkerShape shape, float thickness = 1.0f);

    // Open/close a dedup group around the triangles emitted in between.
    void BeginDedup();
    void EndDedup();

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    const std::vector<DedupGroup>& Dedups() const { return dedups_; }
    std::size_t ByteSize() const { return vertices_.size() * sizeof(Vertex); }

private:
    std::vector<Vertex>     vertices_;
    std::vector<DedupGroup> dedups_;
    bool                    dedupOpen_ = false;
};

} // namespace Ink

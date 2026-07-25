#pragma once
// Internal tessellation helpers shared by the Tessellation translation units
// (Tessellator.cpp and TessFlatten.cpp). The small vector math + Push helpers
// and the two flatten-quality globals were file-static in Tessellator.cpp;
// `inline` here gives one definition shared by both .cpp. Not a public API.
#include "Renderer/Tessellation/Tessellator.h"
#include <cmath>
#include <vector>

namespace Renderer {

// ── Small vector helpers ──────────────────────────────────────────────────────
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float Dot(Vec2 a, Vec2 b)   { return a.x * b.x + a.y * b.y; }
inline float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline float Len(Vec2 a)           { return std::sqrt(Dot(a, a)); }
inline Vec2 Norm(Vec2 a) {
    float l = Len(a);
    return l > 1e-6f ? Vec2{a.x / l, a.y / l} : Vec2{0, 0};
}

inline void PushVert(Mesh& m, Vec2 p, const Color& c) {
    m.vertices.push_back(Vertex{p.x, p.y, c.r, c.g, c.b, c.a});
}
inline void PushTri(Mesh& m, Vec2 a, Vec2 b, Vec2 c, const Color& col) {
    PushVert(m, a, col); PushVert(m, b, col); PushVert(m, c, col);
}

// Curve flattening quality: segments per DOC-UNIT of (approx) arc length. This
// is DELIBERATELY independent of the view zoom — geometry is tessellated once at
// a fixed, high quality and cached, so panning/zooming never re-tessellates (the
// camera is applied in the vertex shader). The SSAA + sampler keep it smooth on
// screen at any zoom. Tuned so a typical curve is smooth without exploding the
// triangle count for large documents.
// Curve flattening quality, expressed as segments per doc-unit of (approx) arc
// length. Tessellation is CACHED per shape (zoom-independent), so we can afford a
// HIGH fixed quality — curves must look smooth even when a small (e.g. mm-sized)
// curve is displayed large. The per-edge step count also has a generous floor so
// even a short curve gets enough segments to read as a true curve, not a polygon.
inline float gQualityPerUnit = 4.0f;    // ≈ 4 segments / doc-unit of arc
// On-screen DETAIL multiplier: flattening detail scales with the effective zoom so
// a curve stays smooth at ANY zoom / object scale (true vector — no faceting when
// zoomed in, no stair-stepping at extreme scale). Set per build from the view zoom,
// QUANTISED into buckets so the cache only re-tessellates on a real detail change
// (not every pixel of zoom). 1.0 = the authored doc-unit detail.
inline float gDetailScale = 1.0f;

// Flatten one edge between two nodes into `out` (defined in TessFlatten.cpp).
void FlattenEdge(const Node& a, const Node& b, float zoom, std::vector<Vec2>& out);

// Evaluate a (uniform/NURBS) B-spline control polygon into a flattened polyline
// (defined in TessFlatten.cpp).
void EvalBSpline(const std::vector<Node>& ctrl, int order, bool closed,
                 bool endpoint, bool bezier, float zoom, std::vector<Vec2>& out);

} // namespace Renderer

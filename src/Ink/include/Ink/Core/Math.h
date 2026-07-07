#pragma once

#include <cmath>
#include <cstdint>

// Ink core math — tiny, engine-owned value types. Document space is double
// (deep-zoom precision, see docs/Ink/GEOMETRY.md §6); GPU-facing data is f32.
namespace Ink {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct DVec2 {
    double x = 0.0, y = 0.0;
};

// Linear-light RGBA. The engine works in linear premultiplied alpha
// throughout (docs/Ink/ARCHITECTURE.md §6); sRGB encode happens in the
// present pass only.
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    // Multiply rgb by alpha (straight → premultiplied).
    Color Premultiplied() const { return { r * a, g * a, b * a, a }; }
};

// One sRGB-encoded channel → linear light.
inline float SrgbChannelToLinear(float c) {
    return (c <= 0.04045f) ? c / 12.92f
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// sRGB straight-alpha color (what UI/design tokens carry) → linear
// premultiplied (what the engine consumes).
inline Color SrgbToLinearPremultiplied(float r, float g, float b, float a) {
    Color c{ SrgbChannelToLinear(r), SrgbChannelToLinear(g),
             SrgbChannelToLinear(b), a };
    return c.Premultiplied();
}

// Row-major 2×3 affine transform: x' = m0·x + m1·y + m2 ; y' = m3·x + m4·y + m5.
struct Mat23 {
    float m[6] = { 1, 0, 0, 0, 1, 0 };

    static Mat23 TRS(float tx, float ty, float sx, float sy, float rad = 0.0f) {
        const float c = std::cos(rad), s = std::sin(rad);
        Mat23 r;
        r.m[0] = c * sx;  r.m[1] = -s * sy; r.m[2] = tx;
        r.m[3] = s * sx;  r.m[4] =  c * sy; r.m[5] = ty;
        return r;
    }
};

struct Rect {
    Vec2 min, max;
    float Width()  const { return max.x - min.x; }
    float Height() const { return max.y - min.y; }
};

// FNV-1a 64 — used for the per-view steady-state signatures.
inline std::uint64_t HashBytes(const void* data, std::size_t size,
                               std::uint64_t seed = 1469598103934665603ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

} // namespace Ink

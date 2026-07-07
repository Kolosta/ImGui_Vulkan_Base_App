#pragma once

#include "Ink/Core/Math.h"
#include <cstdint>
#include <cstring>

// Document-level value types (docs/Ink/DOCUMENT_MODEL.md). Everything spatial
// is DOUBLE — the unbounded-canvas requirement (README req. 9) starts here.
namespace Ink {

// Document-unique node/page identifier. 0 = null. Monotonic, never reused.
using NodeId = std::uint64_t;
inline constexpr NodeId kNullNode = 0;

// Row-major 2×3 affine in double (the document-space sibling of Mat23).
struct DMat23 {
    double m[6] = { 1, 0, 0, 0, 1, 0 };

    DVec2 Apply(DVec2 p) const {
        return { m[0] * p.x + m[1] * p.y + m[2],
                 m[3] * p.x + m[4] * p.y + m[5] };
    }
    // this ∘ other (apply `other` first, then this).
    DMat23 Compose(const DMat23& o) const {
        DMat23 r;
        r.m[0] = m[0] * o.m[0] + m[1] * o.m[3];
        r.m[1] = m[0] * o.m[1] + m[1] * o.m[4];
        r.m[2] = m[0] * o.m[2] + m[1] * o.m[5] + m[2];
        r.m[3] = m[3] * o.m[0] + m[4] * o.m[3];
        r.m[4] = m[3] * o.m[1] + m[4] * o.m[4];
        r.m[5] = m[3] * o.m[2] + m[4] * o.m[5] + m[5];
        return r;
    }
    static DMat23 Translation(double tx, double ty) {
        DMat23 r; r.m[2] = tx; r.m[5] = ty; return r;
    }
};

// Node transform, stored as TRS components (editable independently) with the
// matrix derived. Rotation in radians. Blender semantics: a non-uniform scale
// stretches the node's strokes (they are generated in local space).
struct Transform2D {
    double tx = 0.0, ty = 0.0;
    double sx = 1.0, sy = 1.0;
    double rotation = 0.0;

    DMat23 Matrix() const {
        const double c = std::cos(rotation), s = std::sin(rotation);
        DMat23 r;
        r.m[0] = c * sx;  r.m[1] = -s * sy; r.m[2] = tx;
        r.m[3] = s * sx;  r.m[4] =  c * sy; r.m[5] = ty;
        return r;
    }
};

// Compositing blend mode. Lot 2 renders Normal only; the full W3C set (+
// Erase) activates with the compositing lot (docs/Ink/ROADMAP.md Lot 4).
enum class BlendMode : std::uint8_t { Normal = 0 };

// Hash helper for doubles (bit pattern, so 0.0 == 0.0 deterministically).
inline std::uint64_t HashDouble(double v, std::uint64_t seed) {
    std::uint64_t bits;
    static_assert(sizeof bits == sizeof v);
    std::memcpy(&bits, &v, sizeof bits);
    return HashBytes(&bits, sizeof bits, seed);
}

} // namespace Ink

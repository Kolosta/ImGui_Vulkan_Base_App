#pragma once

#include <Ink/Document/Types.h>
#include <cmath>

// Small math helpers shared by the Viewport editing units (ViewportInput.cpp,
// ViewportModal.cpp). Header-inline on purpose: a non-inline definition in a
// header included by several .cpp would be a multiple-definition link error.

namespace App {
namespace vpm {

inline Ink::DVec2 Rotate(Ink::DVec2 v, double c, double s) {
    return { v.x * c - v.y * s, v.x * s + v.y * c };
}

inline double SnapTo(double v, double inc) {
    return inc > 0.0 ? std::round(v / inc) * inc : v;
}

// Invert an affine 2×3 (non-degenerate; identity fallback).
inline Ink::DMat23 InvertAffine(const Ink::DMat23& m) {
    const double det = m.m[0] * m.m[4] - m.m[1] * m.m[3];
    Ink::DMat23 r;
    if (std::abs(det) < 1e-18) return r;
    const double inv = 1.0 / det;
    r.m[0] =  m.m[4] * inv; r.m[1] = -m.m[1] * inv;
    r.m[3] = -m.m[3] * inv; r.m[4] =  m.m[0] * inv;
    r.m[2] = -(r.m[0] * m.m[2] + r.m[1] * m.m[5]);
    r.m[5] = -(r.m[3] * m.m[2] + r.m[4] * m.m[5]);
    return r;
}

} // namespace vpm
} // namespace App

#pragma once

#include "Ink/Document/Types.h"
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  PathData — the one geometry type (docs/Ink/DOCUMENT_MODEL.md §3): ordered
//  subpaths of cubic-Bézier anchors, in NODE-LOCAL double coordinates. No
//  Mesh/Curve split — a polygon is a path whose anchors carry no handles.
// ─────────────────────────────────────────────────────────────────────────────

// The editing semantics of an anchor's handles (how the editor constrains
// them); the model just stores the handles themselves.
enum class AnchorKind : std::uint8_t { Corner = 0, Smooth = 1, Symmetric = 2 };

struct Anchor {
    DVec2 pos;                    // node-local
    DVec2 in{ 0, 0 };             // incoming handle, RELATIVE to pos
    DVec2 out{ 0, 0 };            // outgoing handle, RELATIVE to pos
    bool  hasIn  = false;
    bool  hasOut = false;
    AnchorKind kind = AnchorKind::Corner;
};

struct Subpath {
    std::vector<Anchor> anchors;
    bool closed = false;
};

struct PathData {
    std::vector<Subpath> subpaths;

    bool Empty() const {
        for (const Subpath& s : subpaths)
            if (s.anchors.size() >= 2) return false;
        return true;
    }

    // Content hash — the GeometryCache key component (docs/Ink/GEOMETRY.md
    // §3): identical geometry shares one cache entry (and one GPU mesh),
    // whatever node it belongs to.
    std::uint64_t Hash() const {
        std::uint64_t h = 0xA11CE0FULL;
        for (const Subpath& s : subpaths) {
            h = HashBytes(&s.closed, sizeof s.closed, h);
            for (const Anchor& a : s.anchors) {
                h = HashDouble(a.pos.x, h); h = HashDouble(a.pos.y, h);
                if (a.hasIn)  { h = HashDouble(a.in.x, h);  h = HashDouble(a.in.y, h); }
                if (a.hasOut) { h = HashDouble(a.out.x, h); h = HashDouble(a.out.y, h); }
                const std::uint8_t flags =
                    (std::uint8_t)((a.hasIn ? 1 : 0) | (a.hasOut ? 2 : 0) |
                                   ((std::uint8_t)a.kind << 2));
                h = HashBytes(&flags, 1, h);
            }
        }
        return h ? h : 1;
    }

    // Convenience builders (used by the demo document, benches and tests).
    static PathData Rect(double x, double y, double w, double hgt);
    static PathData Ellipse(double cx, double cy, double rx, double ry);
    static PathData Polygon(const std::vector<DVec2>& points, bool closed = true);
};

} // namespace Ink

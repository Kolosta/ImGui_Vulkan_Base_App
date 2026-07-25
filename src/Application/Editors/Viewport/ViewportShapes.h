#pragma once

#include <Ink/Document/PathData.h>
#include <cstring>
#include <string>

// Shape geometry builders shared by the Viewport units that CREATE shapes
// (ViewportTools.cpp — the Add menu / draw-on-create) and the one that PREVIEWS
// them (ViewportOverlays.cpp — the draw-on-create ghost). Header-inline: a
// non-inline definition included by several .cpp is a multiple-definition link
// error (CLAUDE.md file-organisation rule).

namespace App {

// Build one shape's geometry centred on the LOCAL ORIGIN, with half-extents
// (hw, hh). Circle/curve forms use max(hw,hh) so a square drag gives a round
// circle; rect/triangle honour the box aspect. Returns the PathData and writes
// the display name.
inline Ink::PathData BuildShapeGeometry(const char* kind, double hw, double hh,
                                        std::string& name) {
    const double r = std::max(hw, hh);
    if (!std::strcmp(kind, "rect")) {
        name = "Rectangle";
        return Ink::PathData::Rect(-hw, -hh, hw * 2, hh * 2);
    }
    if (!std::strcmp(kind, "ellipse")) {
        name = "Ellipse";
        return Ink::PathData::Ellipse(0, 0, hw, hh);
    }
    if (!std::strcmp(kind, "triangle")) {
        name = "Triangle";
        return Ink::PathData::Polygon({ { 0, -hh }, { hw, hh }, { -hw, hh } });
    }
    if (!std::strcmp(kind, "curve")) {
        name = "Bézier";
        Ink::PathData path;
        Ink::Subpath sp; sp.closed = false;
        auto anchor = [&](double x, double y, double ix, double iy) {
            Ink::Anchor an; an.pos = { x, y };
            an.in = { -ix, -iy }; an.out = { ix, iy };
            an.hasIn = an.hasOut = true; an.kind = Ink::AnchorKind::Symmetric;
            sp.anchors.push_back(an);
        };
        anchor(-hw, 0,        0,  hh * 0.6);
        anchor(0,  -hh * 0.6, hw * 0.6, 0);
        anchor(hw,  0,        0, -hh * 0.6);
        path.subpaths.push_back(std::move(sp));
        return path;
    }
    if (!std::strcmp(kind, "beziercircle")) {
        name = "Bézier Circle";
        return Ink::PathData::Ellipse(0, 0, hw, hh);   // four kappa cubic arcs
    }
    if (!std::strcmp(kind, "nurbs")) {
        name = "NURBS Path";
        return Ink::PathData::Nurbs({ { -hw, 0 }, { -hw * 0.33, -hh * 0.8 },
                                      { hw * 0.33, hh * 0.8 }, { hw, 0 } });
    }
    if (!std::strcmp(kind, "nurbscircle")) {
        name = "NURBS Circle";
        return Ink::PathData::NurbsCircle(0, 0, r);
    }
    if (!std::strcmp(kind, "poly")) {
        name = "Poly Line";
        Ink::PathData pp =
            Ink::PathData::Polygon({ { -hw, 0 }, { 0, -hh * 0.6 }, { hw, 0 } },
                                   /*closed=*/false);
        pp.subpaths.front().spline = Ink::SplineType::Poly;
        return pp;
    }
    name = "Shape";
    return Ink::PathData::Rect(-hw, -hh, hw * 2, hh * 2);
}

} // namespace App

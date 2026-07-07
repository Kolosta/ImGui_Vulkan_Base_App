#include "Ink/Document/Document.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Transitional demo content (Lot 2): the Lot 1 hard-coded scene rebuilt as
//  REAL document content through the typed ops — a page, filled + genuinely
//  stroked shapes (the stroker runs on them now), and a 1 000-node diamond
//  grid whose identical paths dedup into a single cached mesh (instancing by
//  content hash). Removed when the drawing tools land (Lot 8).
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Demo colors are authored in sRGB; the document stores linear straight.
Color Srgb(float r, float g, float b, float a) {
    return { SrgbChannelToLinear(r), SrgbChannelToLinear(g),
             SrgbChannelToLinear(b), a };
}

PathData Star(double rOut, double rIn, int branches) {
    std::vector<DVec2> pts;
    for (int i = 0; i < branches * 2; ++i) {
        const double r = (i % 2 == 0) ? rOut : rIn;
        const double a = -kPi / 2.0 + (kPi * (double)i) / (double)branches;
        pts.push_back({ std::cos(a) * r, std::sin(a) * r });
    }
    return PathData::Polygon(pts);
}

Transform2D TRS(double tx, double ty, double s = 1.0, double rot = 0.0) {
    Transform2D t;
    t.tx = tx; t.ty = ty;
    t.sx = t.sy = s;
    t.rotation = rot;
    return t;
}

} // namespace

void SeedDemoDocument(Document& doc) {
    NodeId page = doc.Pages().empty() ? doc.AddPage("Page 1", { 0, 0 },
                                                    { 1920, 1080 })
                                      : doc.Pages().front().id;
    const Color ink = Srgb(0.10f, 0.10f, 0.12f, 1.0f);

    // The 1 000-diamond grid: ONE shared PathData content (hash-identical →
    // one cached mesh, one merged indirect command), per-node transforms.
    {
        const PathData diamond =
            PathData::Polygon({ { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } });
        const Style style = Style::Filled(Srgb(0.24f, 0.55f, 0.90f, 0.45f));
        for (int gy = 0; gy < 25; ++gy)
            for (int gx = 0; gx < 40; ++gx) {
                const NodeId id = doc.AddPath(page, diamond, style, "grid");
                doc.SetTransform(id, TRS(320.0 + 32.0 * gx, 250.0 + 26.0 * gy,
                                         9.0, 0.35 * ((gx + gy) % 7)));
            }
    }

    // Filled + stroked discs (real strokes through the stroker now).
    {
        auto addDisc = [&](double x, double y, double r, Color fill,
                           bool stroked, const char* name) {
            Style s = Style::Filled(fill);
            if (stroked) s.WithStroke(ink, r * 0.14);
            const NodeId id =
                doc.AddPath(page, PathData::Ellipse(0, 0, r, r), s, name);
            doc.SetTransform(id, TRS(x, y));
        };
        addDisc(430, 780, 150, Srgb(0.93f, 0.42f, 0.22f, 1), true,  "disc A");
        addDisc(1240, 260, 90, Srgb(0.36f, 0.72f, 0.34f, 1), true,  "disc B");
        addDisc(1650, 820, 120, Srgb(0.94f, 0.77f, 0.20f, 0.85f), false, "disc C");
        // A stroke-only circle (no fill — the unified model at work).
        const NodeId ring = doc.AddPath(
            page, PathData::Ellipse(0, 0, 140, 140),
            Style::Stroked(Srgb(0.55f, 0.27f, 0.68f, 1), 18.0), "ring");
        doc.SetTransform(ring, TRS(820, 300));
    }

    // Rotated rectangles: one filled+stroked, one filled, one stroke-only.
    {
        const NodeId a = doc.AddPath(
            page, PathData::Rect(-130, -80, 260, 160),
            Style::Filled(Srgb(0.27f, 0.51f, 0.79f, 0.9f)), "rect A");
        doc.SetTransform(a, TRS(340, 300, 1.0, 0.30));

        Style bStyle = Style::Filled(Srgb(0.80f, 0.33f, 0.45f, 0.75f));
        bStyle.WithStroke(ink, 10.0);
        const NodeId b = doc.AddPath(
            page, PathData::Rect(-90, -160, 180, 320), bStyle, "rect B");
        doc.SetTransform(b, TRS(1500, 520, 1.0, -0.18));

        const NodeId c = doc.AddPath(
            page, PathData::Rect(-200, -110, 400, 220),
            Style::Stroked(ink, 12.0), "rect C");
        doc.SetTransform(c, TRS(960, 700, 1.0, 0.08));
    }

    // Triangles + stars.
    {
        const PathData tri =
            PathData::Polygon({ { 0, -1 }, { 0.94, 0.7 }, { -0.94, 0.7 } });
        const NodeId t1 = doc.AddPath(page, tri,
            Style::Filled(Srgb(0.20f, 0.62f, 0.62f, 1)), "tri A");
        doc.SetTransform(t1, TRS(700, 850, 90.0, 0.5));
        const NodeId t2 = doc.AddPath(page, tri,
            Style::Filled(Srgb(0.85f, 0.55f, 0.25f, 1)), "tri B");
        doc.SetTransform(t2, TRS(1100, 900, 60.0, -0.9));

        const PathData star = Star(1.0, 0.45, 5);
        const NodeId s1 = doc.AddPath(page, star,
            Style::Filled(Srgb(0.94f, 0.77f, 0.20f, 1)), "star A");
        doc.SetTransform(s1, TRS(180, 160, 90.0));
        Style s2Style = Style::Filled(Srgb(0.36f, 0.72f, 0.34f, 0.8f));
        s2Style.WithStroke(ink, 0.06);   // stroked in LOCAL units → stretches
        const NodeId s2 = doc.AddPath(page, star, s2Style, "star B");
        doc.SetTransform(s2, TRS(1780, 150, 70.0, 0.3));
    }

    // An open stroked polyline (open-path stroking, butt caps).
    {
        const PathData zig = PathData::Polygon(
            { { -180, 40 }, { -90, -40 }, { 0, 40 }, { 90, -40 }, { 180, 40 } },
            /*closed=*/false);
        const NodeId z = doc.AddPath(page, zig,
            Style::Stroked(Srgb(0.20f, 0.35f, 0.60f, 1), 14.0), "zigzag");
        doc.SetTransform(z, TRS(960, 120));
    }
}

} // namespace Ink

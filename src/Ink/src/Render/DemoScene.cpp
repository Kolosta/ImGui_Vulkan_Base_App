#include "Ink/Document/Document.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Transitional demo content: the feature showcase built as REAL document
//  content through the typed ops. Every object is organised into a NAMED,
//  COLOUR-TAGGED collection (the Collections view of the Outliner is the
//  organisation truth); groups are used only where they carry a LAYERS
//  meaning (compositing units: blend/opacity/clip). Removed when project
//  templates land (Lot 10).
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Demo colors are authored in sRGB; the document stores linear straight.
Color Srgb(float r, float g, float b, float a) {
    return { SrgbChannelToLinear(r), SrgbChannelToLinear(g),
             SrgbChannelToLinear(b), a };
}

// Collection colour tags are DISPLAY-referred (the Outliner swatch and the
// colour picker read/write them raw).
Color Tag(float r, float g, float b) { return { r, g, b, 1.0f }; }

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

    // The organisation: one collection per showcase, colour-tagged.
    const NodeId cGrid    = doc.AddCollection("Grid");
    const NodeId cShapes  = doc.AddCollection("Shapes");
    const NodeId cStrokes = doc.AddCollection("Strokes");
    const NodeId cComp    = doc.AddCollection("Compositing");
    const NodeId cInst    = doc.AddCollection("Instancing");
    const NodeId cRel     = doc.AddCollection("Relations");
    doc.SetCollectionColor(cGrid,    Tag(0.35f, 0.55f, 0.90f));   // blue
    doc.SetCollectionColor(cShapes,  Tag(0.95f, 0.60f, 0.20f));   // orange
    doc.SetCollectionColor(cStrokes, Tag(0.65f, 0.45f, 0.85f));   // purple
    doc.SetCollectionColor(cComp,    Tag(0.90f, 0.35f, 0.55f));   // pink
    doc.SetCollectionColor(cInst,    Tag(0.35f, 0.75f, 0.45f));   // green
    doc.SetCollectionColor(cRel,     Tag(0.30f, 0.75f, 0.75f));   // cyan

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
                doc.AddToCollection(cGrid, id);
            }
    }

    // Filled + stroked discs (real strokes through the stroker).
    {
        auto addDisc = [&](double x, double y, double r, Color fill,
                           bool stroked, const char* name) {
            Style s = Style::Filled(fill);
            if (stroked) s.WithStroke(ink, r * 0.14);
            const NodeId id =
                doc.AddPath(page, PathData::Ellipse(0, 0, r, r), s, name);
            doc.SetTransform(id, TRS(x, y));
            doc.AddToCollection(cShapes, id);
        };
        addDisc(430, 780, 150, Srgb(0.93f, 0.42f, 0.22f, 1), true,  "disc A");
        addDisc(1240, 260, 90, Srgb(0.36f, 0.72f, 0.34f, 1), true,  "disc B");
        addDisc(1650, 820, 120, Srgb(0.94f, 0.77f, 0.20f, 0.85f), false, "disc C");
        // A stroke-only circle (no fill — the unified model at work).
        const NodeId ring = doc.AddPath(
            page, PathData::Ellipse(0, 0, 140, 140),
            Style::Stroked(Srgb(0.55f, 0.27f, 0.68f, 1), 18.0), "ring");
        doc.SetTransform(ring, TRS(820, 300));
        doc.AddToCollection(cShapes, ring);
    }

    // Rotated rectangles: one filled+stroked, one filled, one stroke-only.
    {
        const NodeId a = doc.AddPath(
            page, PathData::Rect(-130, -80, 260, 160),
            Style::Filled(Srgb(0.27f, 0.51f, 0.79f, 0.9f)), "rect A");
        doc.SetTransform(a, TRS(340, 300, 1.0, 0.30));
        doc.AddToCollection(cShapes, a);

        Style bStyle = Style::Filled(Srgb(0.80f, 0.33f, 0.45f, 0.75f));
        bStyle.WithStroke(ink, 10.0);
        const NodeId b = doc.AddPath(
            page, PathData::Rect(-90, -160, 180, 320), bStyle, "rect B");
        doc.SetTransform(b, TRS(1500, 520, 1.0, -0.18));
        doc.AddToCollection(cShapes, b);

        const NodeId c = doc.AddPath(
            page, PathData::Rect(-200, -110, 400, 220),
            Style::Stroked(ink, 12.0), "rect C");
        doc.SetTransform(c, TRS(960, 700, 1.0, 0.08));
        doc.AddToCollection(cShapes, c);
    }

    // Triangles + stars.
    {
        const PathData tri =
            PathData::Polygon({ { 0, -1 }, { 0.94, 0.7 }, { -0.94, 0.7 } });
        const NodeId t1 = doc.AddPath(page, tri,
            Style::Filled(Srgb(0.20f, 0.62f, 0.62f, 1)), "tri A");
        doc.SetTransform(t1, TRS(700, 850, 90.0, 0.5));
        doc.AddToCollection(cShapes, t1);
        const NodeId t2 = doc.AddPath(page, tri,
            Style::Filled(Srgb(0.85f, 0.55f, 0.25f, 1)), "tri B");
        doc.SetTransform(t2, TRS(1100, 900, 60.0, -0.9));
        doc.AddToCollection(cShapes, t2);

        const PathData star = Star(1.0, 0.45, 5);
        const NodeId s1 = doc.AddPath(page, star,
            Style::Filled(Srgb(0.94f, 0.77f, 0.20f, 1)), "star A");
        doc.SetTransform(s1, TRS(180, 160, 90.0));
        doc.AddToCollection(cShapes, s1);
        Style s2Style = Style::Filled(Srgb(0.36f, 0.72f, 0.34f, 0.8f));
        s2Style.WithStroke(ink, 0.06);   // stroked in LOCAL units → stretches
        const NodeId s2 = doc.AddPath(page, star, s2Style, "star B");
        doc.SetTransform(s2, TRS(1780, 150, 70.0, 0.3));
        doc.AddToCollection(cShapes, s2);
    }

    // An open stroked polyline with ROUND joins + ROUND caps.
    {
        const PathData zig = PathData::Polygon(
            { { -180, 40 }, { -90, -40 }, { 0, 40 }, { 90, -40 }, { 180, 40 } },
            /*closed=*/false);
        Style s = Style::Stroked(Srgb(0.20f, 0.35f, 0.60f, 1), 14.0);
        s.strokes[0].join = JoinStyle::Round;
        s.strokes[0].cap  = CapStyle::Round;
        const NodeId z = doc.AddPath(page, zig, s, "zigzag");
        doc.SetTransform(z, TRS(960, 120));
        doc.AddToCollection(cStrokes, z);
    }

    // Stroking showcase: dashes, Inside/Outside alignment (open path), and a
    // non-scaling viewport-width hairline.
    {
        // Dashed ellipse (round caps make the dashes read as beads).
        Style dashed = Style::Stroked(Srgb(0.85f, 0.30f, 0.25f, 1), 8.0);
        dashed.strokes[0].dashPattern = { 26.0, 18.0 };
        dashed.strokes[0].cap = CapStyle::Round;
        const NodeId d = doc.AddPath(page, PathData::Ellipse(0, 0, 110, 70),
                                     dashed, "dashed ring");
        doc.SetTransform(d, TRS(240, 520));
        doc.AddToCollection(cStrokes, d);

        // The SAME open arc stroked Inside vs Outside (walk-direction rule).
        PathData arc = PathData::Ellipse(0, 0, 90, 90);
        arc.subpaths[0].closed = false;
        arc.subpaths[0].anchors.resize(3);   // first-to-third quadrant arc
        Style inside = Style::Stroked(Srgb(0.36f, 0.55f, 0.85f, 1), 16.0);
        inside.strokes[0].align = StrokeAlign::Inside;
        const NodeId ai = doc.AddPath(page, arc, inside, "arc inside");
        doc.SetTransform(ai, TRS(640, 520));
        doc.AddToCollection(cStrokes, ai);
        Style outside = Style::Stroked(Srgb(0.90f, 0.62f, 0.20f, 1), 16.0);
        outside.strokes[0].align = StrokeAlign::Outside;
        const NodeId ao = doc.AddPath(page, arc, outside, "arc outside");
        doc.SetTransform(ao, TRS(640, 520));
        doc.AddToCollection(cStrokes, ao);

        // Viewport-space hairline (constant on-screen width at any zoom).
        Style hair = Style::Stroked(Srgb(0.10f, 0.10f, 0.12f, 1), 1.5);
        hair.strokes[0].widthSpace = WidthSpace::Viewport;
        const NodeId h = doc.AddPath(
            page,
            PathData::Polygon({ { -260, 0 }, { 260, 0 } }, /*closed=*/false),
            hair, "hairline");
        doc.SetTransform(h, TRS(960, 980));
        doc.AddToCollection(cStrokes, h);
    }

    // Compositing showcase — GROUPS here are real LAYER units (blend/opacity/
    // clip apply to the subtree as one), which is exactly what a group means;
    // the group nodes belong to the Compositing collection for organisation.
    {
        auto discGroup = [&](double x, double y, BlendMode blend, float opacity,
                             const char* name) {
            const NodeId g = doc.AddGroup(page, name);
            doc.SetTransform(g, TRS(x, y));
            auto disc = [&](double dx, double dy, Color c) {
                const NodeId n = doc.AddPath(g, PathData::Ellipse(0, 0, 55, 55),
                                             Style::Filled(c), "d");
                doc.SetTransform(n, TRS(dx, dy));
            };
            disc(-28, 0, Srgb(0.90f, 0.20f, 0.25f, 1));
            disc( 28, 0, Srgb(0.20f, 0.45f, 0.90f, 1));
            disc(  0, 34, Srgb(0.25f, 0.80f, 0.35f, 1));
            if (opacity < 1.0f) doc.SetOpacity(g, opacity);
            if (blend != BlendMode::Normal) doc.SetBlend(g, blend);
            doc.AddToCollection(cComp, g);
        };
        discGroup(1360, 560, BlendMode::Normal,   0.45f, "opacity 45%");
        discGroup(1560, 560, BlendMode::Multiply, 1.00f, "multiply");
        discGroup(1560, 760, BlendMode::Screen,   1.00f, "screen");

        // A clip group: a big ellipse mask over a dense grid of dots (the mask
        // is the group's first path child).
        const NodeId clipG = doc.AddGroup(page, "clip group");
        doc.SetTransform(clipG, TRS(360, 300));
        const NodeId mask = doc.AddPath(clipG, PathData::Ellipse(0, 0, 90, 60),
                                        Style::Filled(Srgb(0, 0, 0, 1)), "mask");
        (void)mask;
        for (int gy = -3; gy <= 3; ++gy)
            for (int gx = -4; gx <= 4; ++gx) {
                const NodeId dot = doc.AddPath(
                    clipG, PathData::Ellipse(0, 0, 10, 10),
                    Style::Filled(Srgb(0.85f, 0.45f, 0.15f, 1)), "dot");
                doc.SetTransform(dot, TRS(gx * 24.0, gy * 22.0));
            }
        doc.SetClip(clipG, true);
        doc.AddToCollection(cComp, clipG);
    }

    // Instancing showcase.
    {
        // Array modifier: one star, 8 copies stepping right + rotating. The
        // step is authored in PARENT space (46 document units per copy — the
        // node's ×18 scale shapes each star, not the chain).
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.count = 8;
        arr.step.tx = 46.0;
        arr.step.rotation = 0.4;
        arr.stepSpace = ArrayStepSpace::Parent;
        const NodeId star = doc.AddPath(page, Star(1.0, 0.45, 5),
            Style::Filled(Srgb(0.85f, 0.55f, 0.20f, 1)), "array star");
        doc.SetTransform(star, TRS(120, 980, 18.0));
        doc.SetModifiers(star, { arr });
        doc.AddToCollection(cInst, star);

        // Along-path modifier ON THE PATH (Blender's rule): the wave carries
        // the modifier and instances the tick object along its own spine.
        // The tick stays a plain, single object — editing it is fluid (every
        // copy shares its mesh) and hiding it never hides the copies.
        const NodeId tick = doc.AddPath(page,
            PathData::Polygon({ { 0, -8 }, { 4, 8 }, { -4, 8 } }),
            Style::Filled(Srgb(0.20f, 0.62f, 0.62f, 1)), "along tick");
        doc.SetTransform(tick, TRS(60, 640));
        doc.AddToCollection(cInst, tick);

        PathData wave;
        {
            Subpath sp; sp.closed = false;
            for (int i = 0; i <= 6; ++i) {
                Anchor a; a.pos = { i * 60.0, (i % 2 ? 40.0 : -40.0) };
                sp.anchors.push_back(a);
            }
            wave.subpaths.push_back(sp);
        }
        Modifier along;
        along.kind = ModifierKind::AlongPath;
        along.motifRef = tick;
        along.distribute = AlongDistribute::ByCount;
        along.alongCount = 24;
        along.align = AlongAlign::Tangent;
        const NodeId wavePath = doc.AddPath(page, wave,
            Style::Stroked(Srgb(0.5f, 0.5f, 0.55f, 0.5f), 2.0), "wave");
        doc.SetTransform(wavePath, TRS(120, 700));
        doc.SetModifiers(wavePath, { along });
        doc.AddToCollection(cInst, wavePath);

        // Pattern fill: an ellipse filled with an instanced disc motif, cut
        // exactly at the INNER edge of its stroke (the legacy fill clip).
        const NodeId motif = doc.AddPath(page, PathData::Ellipse(0, 0, 6, 6),
            Style::Filled(Srgb(0.30f, 0.45f, 0.80f, 1)), "motif");
        doc.SetVisible(motif, false);   // the motif itself is a definition
        doc.AddToCollection(cInst, motif);
        Fill pf;
        pf.kind = FillKind::Pattern;
        pf.pattern.motifRef = motif;
        pf.pattern.spacingX = 22.0;
        pf.pattern.spacingY = 22.0;
        pf.pattern.clip = PatternClip::StrokeInner;
        Style patStyle;
        patStyle.fills.push_back(pf);
        patStyle.WithStroke(ink, 6.0);
        const NodeId patHost = doc.AddPath(page,
            PathData::Ellipse(0, 0, 150, 90), patStyle, "pattern disc");
        doc.SetTransform(patHost, TRS(430, 1000));
        doc.AddToCollection(cInst, patHost);

        // Instance node: a second copy of the array-star object elsewhere
        // (editing the source updates both).
        const NodeId inst = doc.AddInstance(page, star, "star instance");
        doc.SetTransform(inst, TRS(760, 260, 1.0, 0.6));
        doc.AddToCollection(cInst, inst);
    }

    // Relations showcase.
    {
        // Object parenting: three small satellites parented to a hub. Moving
        // the hub moves them; z-order is independent.
        const NodeId hub = doc.AddPath(page, PathData::Ellipse(0, 0, 26, 26),
            Style::Filled(Srgb(0.30f, 0.35f, 0.45f, 1)), "hub");
        doc.SetTransform(hub, TRS(1780, 900));
        doc.AddToCollection(cRel, hub);
        for (int i = 0; i < 3; ++i) {
            const NodeId sat = doc.AddPath(page, PathData::Ellipse(0, 0, 9, 9),
                Style::Filled(Srgb(0.85f, 0.55f, 0.25f, 1)), "satellite");
            const double a = 2.0 * kPi * i / 3.0;
            doc.SetTransform(sat, TRS(1780 + std::cos(a) * 55.0,
                                      900 + std::sin(a) * 55.0));
            doc.SetParent(sat, hub, /*keepWorld=*/true);
            doc.AddToCollection(cRel, sat);
        }

        // Boolean modifier: a rounded plaque = big rect UNION two discs, then
        // SUBTRACT a smaller rect window (a non-destructive derived shape).
        const NodeId opDisc1 = doc.AddPath(page, PathData::Ellipse(0, 0, 40, 40),
            Style::Filled(Srgb(0, 0, 0, 1)), "op disc 1");
        doc.SetTransform(opDisc1, TRS(-70, 0));
        doc.SetVisible(opDisc1, false);
        doc.AddToCollection(cRel, opDisc1);
        const NodeId opDisc2 = doc.AddPath(page, PathData::Ellipse(0, 0, 40, 40),
            Style::Filled(Srgb(0, 0, 0, 1)), "op disc 2");
        doc.SetTransform(opDisc2, TRS(70, 0));
        doc.SetVisible(opDisc2, false);
        doc.AddToCollection(cRel, opDisc2);
        const NodeId opWindow = doc.AddPath(page, PathData::Rect(-30, -18, 60, 36),
            Style::Filled(Srgb(0, 0, 0, 1)), "op window");
        doc.SetVisible(opWindow, false);
        doc.AddToCollection(cRel, opWindow);

        const NodeId plaque = doc.AddPath(page, PathData::Rect(-70, -40, 140, 80),
            Style::Filled(Srgb(0.36f, 0.62f, 0.55f, 1)).WithStroke(
                Srgb(0.10f, 0.12f, 0.14f, 1), 3.0), "boolean plaque");
        doc.SetTransform(plaque, TRS(1500, 900));
        Modifier u1; u1.kind = ModifierKind::Boolean; u1.op = BooleanOp::Union;
        u1.operandRef = opDisc1;
        Modifier u2; u2.kind = ModifierKind::Boolean; u2.op = BooleanOp::Union;
        u2.operandRef = opDisc2;
        Modifier sub; sub.kind = ModifierKind::Boolean; sub.op = BooleanOp::Subtract;
        sub.operandRef = opWindow;
        doc.SetModifiers(plaque, { u1, u2, sub });
        doc.AddToCollection(cRel, plaque);
    }
}

} // namespace Ink

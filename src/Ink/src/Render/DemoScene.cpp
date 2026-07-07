#include "Ink/Render/GpuScene.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Lot 1 demo content — hand-tessellated unit definitions instanced through
//  the real pools/tables/indirect path. Exercises: multiple mesh definitions,
//  per-instance transforms (incl. rotation), per-instance items/paints,
//  premultiplied translucency, and a 1 000-instance grid (the instancing
//  floor for ink_bench). Replaced by the real Document → Scene → Geometry
//  path in Lots 2–3; nothing here leaks past UploadDemoScene().
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct MeshBuilder {
    std::vector<ContentVertex> vertices;
    std::vector<std::uint32_t> indices;

    // Begin a definition; returns its MeshRange once finished.
    MeshRange Begin() {
        MeshRange r;
        r.firstIndex   = (std::uint32_t)indices.size();
        r.vertexOffset = (std::int32_t)vertices.size();
        return r;
    }
    void End(MeshRange& r) {
        r.indexCount = (std::uint32_t)indices.size() - r.firstIndex;
    }
    // Local-index helpers (indices are relative to the range's vertexOffset).
    std::uint32_t V(float x, float y) {
        vertices.push_back({ x, y });
        return (std::uint32_t)vertices.size() - 1
               - (std::uint32_t)currentBase_;
    }
    void Tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        indices.push_back(a); indices.push_back(b); indices.push_back(c);
    }
    void SetBase(std::int32_t base) { currentBase_ = base; }

private:
    std::int32_t currentBase_ = 0;
};

// Unit quad [-1,1]².
MeshRange BuildQuad(MeshBuilder& mb) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t a = mb.V(-1, -1), b = mb.V(1, -1), c = mb.V(1, 1), d = mb.V(-1, 1);
    mb.Tri(a, b, c); mb.Tri(a, c, d);
    mb.End(r); return r;
}

// Unit-radius disc (triangle fan around the centre).
MeshRange BuildDisc(MeshBuilder& mb, int segments) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t centre = mb.V(0, 0);
    std::uint32_t first = 0, prev = 0;
    for (int i = 0; i < segments; ++i) {
        const float a = (2.0f * kPi * (float)i) / (float)segments;
        std::uint32_t v = mb.V(std::cos(a), std::sin(a));
        if (i == 0) first = v;
        else        mb.Tri(centre, prev, v);
        prev = v;
    }
    mb.Tri(centre, prev, first);
    mb.End(r); return r;
}

// Annulus rIn..1 — the "center stroke" look of a circle until the real
// stroker lands (Lot 3).
MeshRange BuildRing(MeshBuilder& mb, int segments, float rIn) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t firstO = 0, firstI = 0, prevO = 0, prevI = 0;
    for (int i = 0; i < segments; ++i) {
        const float a = (2.0f * kPi * (float)i) / (float)segments;
        const float c = std::cos(a), s = std::sin(a);
        std::uint32_t o = mb.V(c, s);
        std::uint32_t in = mb.V(c * rIn, s * rIn);
        if (i == 0) { firstO = o; firstI = in; }
        else { mb.Tri(prevO, o, in); mb.Tri(prevO, in, prevI); }
        prevO = o; prevI = in;
    }
    mb.Tri(prevO, firstO, firstI); mb.Tri(prevO, firstI, prevI);
    mb.End(r); return r;
}

// Rectangle outline of half-extent 1, band thickness t (inside the edge).
MeshRange BuildQuadOutline(MeshBuilder& mb, float t) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    const float k = 1.0f - t;
    std::uint32_t o[4] = { mb.V(-1, -1), mb.V(1, -1), mb.V(1, 1), mb.V(-1, 1) };
    std::uint32_t in[4] = { mb.V(-k, -k), mb.V(k, -k), mb.V(k, k), mb.V(-k, k) };
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        mb.Tri(o[i], o[j], in[j]);
        mb.Tri(o[i], in[j], in[i]);
    }
    mb.End(r); return r;
}

// Unit triangle (apex up).
MeshRange BuildTriangle(MeshBuilder& mb) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t a = mb.V(0, -1), b = mb.V(0.94f, 0.7f), c = mb.V(-0.94f, 0.7f);
    mb.Tri(a, b, c);
    mb.End(r); return r;
}

// 5-branch star (outer 1, inner rIn) as a fan from the centre — the polygon
// is star-shaped from its centroid, so the fan is valid.
MeshRange BuildStar(MeshBuilder& mb, float rIn) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t centre = mb.V(0, 0);
    std::uint32_t first = 0, prev = 0;
    for (int i = 0; i < 10; ++i) {
        const float rad = (i % 2 == 0) ? 1.0f : rIn;
        const float a = -kPi / 2.0f + (2.0f * kPi * (float)i) / 10.0f;
        std::uint32_t v = mb.V(std::cos(a) * rad, std::sin(a) * rad);
        if (i == 0) first = v;
        else        mb.Tri(centre, prev, v);
        prev = v;
    }
    mb.Tri(centre, prev, first);
    mb.End(r); return r;
}

// Unit diamond.
MeshRange BuildDiamond(MeshBuilder& mb) {
    MeshRange r = mb.Begin(); mb.SetBase(r.vertexOffset);
    std::uint32_t a = mb.V(0, -1), b = mb.V(1, 0), c = mb.V(0, 1), d = mb.V(-1, 0);
    mb.Tri(a, b, c); mb.Tri(a, c, d);
    mb.End(r); return r;
}

} // namespace

bool UploadDemoScene(rhi::Device& dev, GpuScene& scene) {
    MeshBuilder mb;
    const MeshRange quad     = BuildQuad(mb);
    const MeshRange disc     = BuildDisc(mb, 64);
    const MeshRange ring     = BuildRing(mb, 64, 0.86f);
    const MeshRange outline  = BuildQuadOutline(mb, 0.07f);
    const MeshRange triangle = BuildTriangle(mb);
    const MeshRange star     = BuildStar(mb, 0.45f);
    const MeshRange diamond  = BuildDiamond(mb);

    std::vector<InstanceRecord> instances;
    std::vector<ItemRecord>     items;
    std::vector<PaintRecord>    paints;
    std::vector<Batch>          batches;

    auto paint = [&](float r, float g, float b, float a) {
        const Color c = SrgbToLinearPremultiplied(r, g, b, a);
        paints.push_back({ { c.r, c.g, c.b, c.a } });
        return (std::uint32_t)paints.size() - 1;
    };
    auto item = [&](std::uint32_t paintIdx) {
        items.push_back({ paintIdx, 0, { 0, 0 } });
        return (std::uint32_t)items.size() - 1;
    };
    auto instance = [&](const Mat23& m, std::uint32_t itemIdx) {
        InstanceRecord rec{};
        for (int i = 0; i < 6; ++i) rec.m[i] = m.m[i];
        rec.itemIndex = itemIdx;
        instances.push_back(rec);
    };
    // A batch = one mesh × the run of instances appended by `emit`.
    auto batch = [&](const MeshRange& mesh, auto&& emit) {
        Batch b;
        b.mesh          = mesh;
        b.firstInstance = (std::uint32_t)instances.size();
        emit();
        b.instanceCount = (std::uint32_t)instances.size() - b.firstInstance;
        if (b.instanceCount > 0) batches.push_back(b);
    };

    // Painter's order = batch order (docs/Ink/RENDER_GRAPH.md §3).

    // 1 — page substrate: a white 1920×1080 page at the origin.
    batch(quad, [&] {
        instance(Mat23::TRS(960, 540, 960, 540), item(paint(1, 1, 1, 1)));
    });

    // 2 — the 1 000-instance diamond grid (one shared item/paint).
    batch(diamond, [&] {
        const std::uint32_t it = item(paint(0.24f, 0.55f, 0.90f, 0.45f));
        for (int gy = 0; gy < 25; ++gy)
            for (int gx = 0; gx < 40; ++gx)
                instance(Mat23::TRS(320.0f + 32.0f * (float)gx,
                                    250.0f + 26.0f * (float)gy, 9, 9,
                                    0.35f * (float)((gx + gy) % 7)), it);
    });

    // 3 — filled discs.
    batch(disc, [&] {
        instance(Mat23::TRS(430, 780, 150, 150), item(paint(0.93f, 0.42f, 0.22f, 1)));
        instance(Mat23::TRS(1240, 260, 90, 90),  item(paint(0.36f, 0.72f, 0.34f, 1)));
        instance(Mat23::TRS(1650, 820, 120, 120),item(paint(0.94f, 0.77f, 0.20f, 0.85f)));
    });

    // 4 — their ring "strokes" (+ one free-standing ring).
    batch(ring, [&] {
        const std::uint32_t ink = item(paint(0.10f, 0.10f, 0.12f, 1));
        instance(Mat23::TRS(430, 780, 150, 150), ink);
        instance(Mat23::TRS(1240, 260, 90, 90),  ink);
        instance(Mat23::TRS(820, 300, 140, 140), item(paint(0.55f, 0.27f, 0.68f, 1)));
    });

    // 5 — rotated filled rectangles (transform path incl. rotation).
    batch(quad, [&] {
        instance(Mat23::TRS(340, 300, 130, 80, 0.30f),
                 item(paint(0.27f, 0.51f, 0.79f, 0.9f)));
        instance(Mat23::TRS(1500, 520, 90, 160, -0.18f),
                 item(paint(0.80f, 0.33f, 0.45f, 0.75f)));
    });

    // 6 — rectangle outlines ("strokes" of unfilled rects).
    batch(outline, [&] {
        const std::uint32_t ink = item(paint(0.10f, 0.10f, 0.12f, 1));
        instance(Mat23::TRS(1500, 520, 90, 160, -0.18f), ink);
        instance(Mat23::TRS(960, 700, 200, 110, 0.08f), ink);
    });

    // 7 — triangles + stars.
    batch(triangle, [&] {
        instance(Mat23::TRS(700, 850, 90, 90, 0.5f),
                 item(paint(0.20f, 0.62f, 0.62f, 1)));
        instance(Mat23::TRS(1100, 900, 60, 60, -0.9f),
                 item(paint(0.85f, 0.55f, 0.25f, 1)));
    });
    batch(star, [&] {
        instance(Mat23::TRS(180, 160, 90, 90),
                 item(paint(0.94f, 0.77f, 0.20f, 1)));
        instance(Mat23::TRS(1780, 150, 70, 70, 0.3f),
                 item(paint(0.36f, 0.72f, 0.34f, 0.8f)));
    });

    const Rect bounds{ { -60.0f, -60.0f }, { 1980.0f, 1140.0f } };
    return scene.UploadStatic(dev, mb.vertices, mb.indices, instances, items,
                              paints, batches, bounds);
}

} // namespace Ink

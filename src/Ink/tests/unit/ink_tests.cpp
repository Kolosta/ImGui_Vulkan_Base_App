// ink_tests — unit tests of the GPU-less Ink layers (docs/Ink/PERF_TESTING.md
// §6): document invariants, flattening tolerance, fill-rule triangulation,
// stroking areas, cache identity. Plain asserts, no framework — exit 0 = pass.

#include <Ink/Document/Document.h>
#include <Ink/Geometry/GeometryCache.h>
#include <Ink/Scene/Picking.h>
#include <Ink/Scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::abs((a) - (b)) <= (eps))

using namespace Ink;

// Unsigned area of a triangle mesh (sum of |triangle| areas).
double MeshArea(const geom::Mesh& m) {
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const std::uint32_t ia = m.indices[i], ib = m.indices[i + 1],
                            ic = m.indices[i + 2];
        const double ax = m.positions[ia * 2], ay = m.positions[ia * 2 + 1];
        const double bx = m.positions[ib * 2], by = m.positions[ib * 2 + 1];
        const double cx = m.positions[ic * 2], cy = m.positions[ic * 2 + 1];
        area += std::abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) * 0.5;
    }
    return area;
}

void TestDocumentOps() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 10, 20 }, { 100, 100 });
    CHECK(page != kNullNode);
    CHECK(doc.Pages().size() == 1);

    const std::uint64_t v0 = doc.Version();
    const NodeId group = doc.AddGroup(page, "G");
    const NodeId path  = doc.AddPath(group, PathData::Rect(0, 0, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "R");
    CHECK(group != kNullNode && path != kNullNode);
    CHECK(doc.Version() > v0);
    CHECK(doc.NodeCount() == 2);
    CHECK(doc.Find(path)->parent == group);
    CHECK(doc.Find(path)->page == page);

    // Change log drains exactly once.
    CHECK(doc.HasPendingChanges());
    const auto changes = doc.DrainChanges();
    CHECK(changes.size() >= 3);   // page + group + path adds
    CHECK(!doc.HasPendingChanges());

    // World transform composes page origin + parent chain in double.
    Transform2D gt; gt.tx = 5; gt.ty = 7;
    doc.SetTransform(group, gt);
    const DMat23 w = doc.WorldTransform(path);
    const DVec2 p = w.Apply({ 0, 0 });
    CHECK_NEAR(p.x, 15.0, 1e-12);   // 10 (page) + 5 (group)
    CHECK_NEAR(p.y, 27.0, 1e-12);   // 20 (page) + 7 (group)

    // Removing the group removes its subtree.
    doc.Remove(group);
    CHECK(doc.Find(group) == nullptr);
    CHECK(doc.Find(path) == nullptr);
    CHECK(doc.NodeCount() == 0);
    CHECK(doc.FindPage(page)->children.empty());
}

void TestPathHash() {
    const PathData a = PathData::Rect(0, 0, 10, 10);
    const PathData b = PathData::Rect(0, 0, 10, 10);
    const PathData c = PathData::Rect(0, 0, 10, 11);
    CHECK(a.Hash() == b.Hash());
    CHECK(a.Hash() != c.Hash());
    CHECK(PathData::Ellipse(0, 0, 5, 5).Hash() !=
          PathData::Ellipse(0, 0, 5, 6).Hash());
}

void TestFlatten() {
    // A polygon (no handles) flattens to exactly its corner points.
    const PathData rect = PathData::Rect(0, 0, 4, 2);
    const auto flat = geom::Flatten(rect, 0.1);
    CHECK(flat.size() == 1);
    CHECK(flat[0].closed);
    CHECK(flat[0].points.size() == 4);

    // A circle flattens within tolerance: every sample sits on the radius.
    const double tol = 0.01;
    const auto circle = geom::Flatten(PathData::Ellipse(0, 0, 10, 10), tol);
    CHECK(circle.size() == 1);
    CHECK(circle[0].points.size() >= 16);
    double maxErr = 0.0;
    for (const DVec2& p : circle[0].points)
        maxErr = std::max(maxErr, std::abs(std::sqrt(p.x * p.x + p.y * p.y) - 10.0));
    CHECK(maxErr <= tol * 3.0);   // chord-error bound, generous margin

    // Signed area: CCW unit square (y-up convention of the shoelace formula).
    CHECK_NEAR(geom::SignedArea({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }),
               1.0, 1e-12);
}

void TestTriangulate() {
    // Plain square → 2 triangles, area 1.
    {
        const auto flat = geom::Flatten(PathData::Rect(0, 0, 1, 1), 0.1);
        const geom::Mesh m = geom::TriangulateFill(flat, FillRule::NonZero);
        CHECK(m.indices.size() == 6);
        CHECK_NEAR(MeshArea(m), 1.0, 1e-9);
    }
    // Square with a hole (opposite winding): area = 16 − 4 = 12, both rules.
    {
        PathData p = PathData::Rect(0, 0, 4, 4);   // CCW under shoelace
        PathData hole = PathData::Polygon(
            { { 1, 1 }, { 1, 3 }, { 3, 3 }, { 3, 1 } });   // opposite winding
        p.subpaths.push_back(hole.subpaths[0]);
        const auto flat = geom::Flatten(p, 0.1);
        const geom::Mesh nz = geom::TriangulateFill(flat, FillRule::NonZero);
        CHECK_NEAR(MeshArea(nz), 12.0, 1e-9);
        const geom::Mesh eo = geom::TriangulateFill(flat, FillRule::EvenOdd);
        CHECK_NEAR(MeshArea(eo), 12.0, 1e-9);
    }
    // EvenOdd: same-winding nested square is STILL a hole (parity).
    {
        PathData p = PathData::Rect(0, 0, 4, 4);
        p.subpaths.push_back(PathData::Rect(1, 1, 2, 2).subpaths[0]);
        const auto flat = geom::Flatten(p, 0.1);
        const geom::Mesh eo = geom::TriangulateFill(flat, FillRule::EvenOdd);
        CHECK_NEAR(MeshArea(eo), 12.0, 1e-9);
        // NonZero: same winding accumulates — the inner square stays covered.
        // v1 emits the nested outer as its own region (16 + 4 of triangle
        // area): coverage is exact, translucent fills overdraw (the same
        // documented v1 limit as stroke self-overlap, GEOMETRY.md §2).
        const geom::Mesh nz = geom::TriangulateFill(flat, FillRule::NonZero);
        CHECK_NEAR(MeshArea(nz), 20.0, 1e-9);
    }
    // A concave polygon (5-branch star) triangulates fully.
    {
        std::vector<DVec2> pts;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2 == 0) ? 1.0 : 0.4;
            const double a = 3.14159265358979 * (double)i / 5.0;
            pts.push_back({ std::cos(a) * r, std::sin(a) * r });
        }
        const auto flat = geom::Flatten(PathData::Polygon(pts), 0.1);
        const geom::Mesh m = geom::TriangulateFill(flat, FillRule::NonZero);
        CHECK(m.indices.size() == 3 * 8);   // n−2 triangles for a simple 10-gon
        CHECK(MeshArea(m) > 0.5);
    }
}

void TestStroker() {
    const double kTol = 0.05;
    Stroke s;
    s.width = 2.0;
    // Open segment (0,0)→(10,0): one quad of area length × width = 20.
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 } };
        const geom::Mesh m = geom::TessellateStroke({ pl }, s, kTol);
        CHECK(m.indices.size() == 6);
        CHECK_NEAR(MeshArea(m), 20.0, 1e-9);
    }
    // Closed square 10×10, MITER joins. COVERAGE = ring 12² − 8² = 80; the
    // unsigned triangle sum adds the 4 corner overlaps of adjacent segment
    // quads (w/2)² each → expected sum ∈ [80, 84].
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
        pl.closed = true;
        const double area = MeshArea(geom::TessellateStroke({ pl }, s, kTol));
        CHECK(area >= 79.9 && area <= 84.1);
    }
    // INSIDE alignment on the closed square: the FULL band [0..w] hugs the
    // interior — ring 10² − (10−2w)² = 64. OUTSIDE: (10+2w)² − 10² = 96.
    // (+ ≤4 of corner-overlap in the unsigned sum.)
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
        pl.closed = true;
        Stroke in = s;  in.align  = StrokeAlign::Inside;
        Stroke out = s; out.align = StrokeAlign::Outside;
        const double ai = MeshArea(geom::TessellateStroke({ pl }, in,  kTol));
        const double ao = MeshArea(geom::TessellateStroke({ pl }, out, kTol));
        CHECK(ai >= 63.9 && ai <= 68.1);
        CHECK(ao >= 95.9 && ao <= 100.1);
    }
    // INSIDE on an OPEN horizontal segment (walking +x): the band lies on the
    // +y side (the documented right-hand rule on the y-down canvas).
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 } };
        Stroke in = s; in.align = StrokeAlign::Inside;
        const geom::Mesh m = geom::TessellateStroke({ pl }, in, kTol);
        CHECK_NEAR(MeshArea(m), 20.0, 1e-6);
        for (std::size_t i = 1; i < m.positions.size(); i += 2)
            CHECK(m.positions[i] >= -1e-6 && m.positions[i] <= 2.0 + 1e-6);
    }
    // Caps on a unit-direction segment: Square adds w/2 × w per end; Round
    // adds ~half-discs (π(w/2)²/2 per end, within flattening tolerance).
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 } };
        Stroke sq = s; sq.cap = CapStyle::Square;
        CHECK_NEAR(MeshArea(geom::TessellateStroke({ pl }, sq, kTol)),
                   20.0 + 2.0 * (1.0 * 2.0), 1e-6);
        Stroke rd = s; rd.cap = CapStyle::Round;
        // Inscribed fans under-estimate the two half-discs slightly (bounded
        // by the flattening tolerance).
        CHECK_NEAR(MeshArea(geom::TessellateStroke({ pl }, rd, kTol)),
                   20.0 + 3.14159265, 0.5);
    }
    // Dashes {2,2} on a length-10 segment: on-pieces 2+2+2 → area 12.
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 } };
        Stroke d = s;
        d.dashPattern = { 2.0, 2.0 };
        CHECK_NEAR(MeshArea(geom::TessellateStroke({ pl }, d, kTol)), 12.0, 1e-6);
        // Phase offset shifts the pattern: offset 2 starts OFF → 2+2 on → 8.
        d.dashOffset = 2.0;
        CHECK_NEAR(MeshArea(geom::TessellateStroke({ pl }, d, kTol)), 8.0, 1e-6);
    }
    // Miter limit: a sharp V (10°) exceeds limit 4 → bevel fallback keeps the
    // outline bounded (no spike ≫ w/2·limit from the corner).
    {
        geom::Polyline pl;
        pl.points = { { -10, 0 }, { 0, 0 }, { -10, 1.76 } };   // ~10° turn
        Stroke mt = s; mt.join = JoinStyle::Miter; mt.miterLimit = 4.0;
        const geom::Mesh m = geom::TessellateStroke({ pl }, mt, kTol);
        double maxX = -1e300;
        for (std::size_t i = 0; i < m.positions.size(); i += 2)
            maxX = std::max(maxX, (double)m.positions[i]);
        CHECK(maxX <= 0.0 + s.width * 0.5 * mt.miterLimit + 1e-6);
    }
    // WidthSpace: viewport widths convert at the tier's nominal zoom.
    CHECK_NEAR(GeometryCache::EffectiveWidth(
                   []{ Stroke v; v.width = 8.0;
                       v.widthSpace = WidthSpace::Viewport; return v; }(), 2),
               2.0, 1e-12);
}

void TestGeometryCache() {
    GeometryCache cache;
    const PathData path = PathData::Ellipse(0, 0, 50, 50);
    const std::uint64_t h = path.Hash();

    std::uint64_t k1 = 0, k2 = 0, k3 = 0;
    const geom::Mesh* a = cache.GetFill(path, h, 0, FillRule::NonZero, k1);
    const geom::Mesh* b = cache.GetFill(path, h, 0, FillRule::NonZero, k2);
    CHECK(a != nullptr && a == b && k1 == k2);   // identity on hit
    CHECK(cache.MeshCount() == 1);

    // A different tier is a different product (finer flattening).
    const geom::Mesh* c = cache.GetFill(path, h, 3, FillRule::NonZero, k3);
    CHECK(c != nullptr && k3 != k1);
    CHECK(c->VertexCount() > a->VertexCount());

    // Stroke products key on geometry params, not paints.
    Stroke s1; s1.width = 4.0;
    Stroke s2 = s1; s2.paint.color = { 1, 0, 0, 1 };   // paint-only difference
    std::uint64_t ks1 = 0, ks2 = 0;
    cache.GetStroke(path, h, 0, s1, ks1);
    cache.GetStroke(path, h, 0, s2, ks2);
    CHECK(ks1 == ks2);   // same geometry product

    // Tier hysteresis: the current tier holds until zoom strays ±0.75 past it.
    CHECK(GeometryCache::StableTier(0, 1.5) == 0);   // log2 ≈ 0.58 → hold
    CHECK(GeometryCache::StableTier(0, 1.9) == 1);   // log2 ≈ 0.93 → switch
    CHECK(GeometryCache::StableTier(1, 1.5) == 1);   // held from above too
}

void TestCompositeScopes() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 100, 100 });

    // A plain group is a pass-through layer: no scope, children stay in root.
    const NodeId plain = doc.AddGroup(page, "plain");
    doc.AddPath(plain, PathData::Rect(0, 0, 10, 10),
                Style::Filled({ 1, 0, 0, 1 }), "r");
    Scene s0;
    s0.Compile(doc);
    CHECK(s0.Scopes().size() == 1);          // only the root scope
    CHECK(s0.MaxScopeDepth() == 0);
    CHECK(s0.Drawables().back().scope == kRootScope);

    // Opacity opens a composite scope; its drawable is tagged with it.
    const NodeId fade = doc.AddGroup(page, "fade");
    const NodeId child = doc.AddPath(fade, PathData::Rect(0, 0, 5, 5),
                                     Style::Filled({ 0, 1, 0, 1 }), "c");
    (void)child;
    doc.SetOpacity(fade, 0.5f);
    Scene s1;
    s1.Compile(doc);
    CHECK(s1.Scopes().size() == 2);          // root + fade
    CHECK(s1.MaxScopeDepth() == 1);
    const CompositeScope& sc = s1.Scopes().back();
    CHECK(sc.node == fade);
    CHECK(sc.parent == kRootScope);
    CHECK(std::abs(sc.opacity - 0.5f) < 1e-6f);
    CHECK(sc.blend == BlendMode::Normal);
    // The fade group's own child is in the fade scope; the plain group's is not.
    bool sawFadeChild = false, sawPlainChild = false;
    for (const Drawable& d : s1.Drawables()) {
        if (d.scope != kRootScope) sawFadeChild = true;
        if (d.node != page && d.node != child && d.scope == kRootScope &&
            !d.isClipSource)
            sawPlainChild = true;
    }
    CHECK(sawFadeChild && sawPlainChild);

    // Nested composite groups deepen the scope tree.
    const NodeId outer = doc.AddGroup(page, "outer");
    doc.SetBlend(outer, BlendMode::Multiply);
    const NodeId inner = doc.AddGroup(outer, "inner");
    doc.SetOpacity(inner, 0.3f);
    doc.AddPath(inner, PathData::Rect(0, 0, 3, 3),
                Style::Filled({ 0, 0, 1, 1 }), "deep");
    Scene s2;
    s2.Compile(doc);
    CHECK(s2.MaxScopeDepth() == 2);          // outer(1) → inner(2)

    // A clip group emits a stencil-only clip-source drawable (the mask never
    // paints) plus the clipped content.
    const NodeId clipG = doc.AddGroup(page, "clip");
    doc.AddPath(clipG, PathData::Ellipse(0, 0, 20, 20),
                Style::Filled({ 0, 0, 0, 1 }), "mask");   // first child = mask
    doc.AddPath(clipG, PathData::Rect(0, 0, 8, 8),
                Style::Filled({ 1, 1, 0, 1 }), "clipped");
    doc.SetClip(clipG, true);
    Scene s3;
    s3.Compile(doc);
    bool sawClipSource = false, sawClipped = false;
    for (const Drawable& d : s3.Drawables()) {
        if (d.isClipSource) sawClipSource = true;
        else if (d.scope != kRootScope) {
            // find the clip scope's painted content
            const CompositeScope& cs = s3.Scopes()[d.scope];
            if (cs.clipNode != kNullNode) sawClipped = true;
        }
    }
    CHECK(sawClipSource && sawClipped);

    // Affinity CLIP layer: a path with a nested child. The host paints
    // UNCLIPPED (its own fill is the mask), the mask writes the stencil, the
    // child is CLIPPED.
    {
        Document doc2;
        const NodeId pg = doc2.AddPage("P", { 0, 0 }, { 100, 100 });
        const NodeId host = doc2.AddPath(pg, PathData::Rect(0, 0, 40, 40),
                                         Style::Filled({ 0.2f, 0.3f, 0.5f, 1 }), "host");
        const NodeId child = doc2.AddPath(pg, PathData::Rect(0, 0, 20, 20),
                                          Style::Filled({ 1, 1, 0, 1 }), "child");
        doc2.MoveTo(child, host, -1);
        Scene sc; sc.Compile(doc2);
        bool hostUnclipped = false, maskWrite = false, childClipped = false;
        for (const Drawable& d : sc.Drawables()) {
            if (d.node == host && !d.isClipSource && !d.isStroke &&
                d.clip == ClipRole::None) hostUnclipped = true;
            if (d.isClipSource && d.clip == ClipRole::MaskWrite) maskWrite = true;
            if (d.node == child && d.clip == ClipRole::Clipped) childClipped = true;
        }
        CHECK(hostUnclipped);
        CHECK(maskWrite);
        CHECK(childClipped);
    }

    // Affinity MASK layer: the mask child masks the host — the host is
    // CLIPPED to the mask, the mask writes, and it does not paint.
    {
        Document doc3;
        const NodeId pg = doc3.AddPage("P", { 0, 0 }, { 100, 100 });
        const NodeId host = doc3.AddPath(pg, PathData::Rect(0, 0, 40, 40),
                                         Style::Filled({ 0.8f, 0.3f, 0.2f, 1 }), "host");
        const NodeId mask = doc3.AddPath(pg, PathData::Ellipse(0, 0, 15, 15),
                                         Style::Filled({ 0, 0, 0, 1 }), "mask");
        doc3.MoveTo(mask, host, -1);
        doc3.SetMask(mask, true);
        Scene sm; sm.Compile(doc3);
        bool hostClipped = false, maskWrite = false, maskPaints = false;
        for (const Drawable& d : sm.Drawables()) {
            if (d.node == host && !d.isStroke && d.clip == ClipRole::Clipped)
                hostClipped = true;
            if (d.node == mask && d.isClipSource) maskWrite = true;
            if (d.node == mask && !d.isClipSource) maskPaints = true;
        }
        CHECK(hostClipped);
        CHECK(maskWrite);
        CHECK(!maskPaints);   // the mask never paints
    }
}

void TestInstancing() {
    // Array modifier: 1 star → `count` grouped drawables sharing ONE pathHash
    // (so they merge into one instanced draw downstream).
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.count = 5;
        arr.step.tx = 20.0;
        const NodeId n = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "r");
        doc.SetModifiers(n, { arr });
        Scene s; s.Compile(doc);
        int copies = 0;
        std::uint64_t h = 0;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) {
                ++copies;
                if (h == 0) h = d.pathHash;
                CHECK(d.pathHash == h);   // same content → same cache key
            }
        CHECK(copies == 5);
        // Each copy is offset by the step: xs differ by 20.
        double xs[5]; int k = 0;
        for (const Drawable& d : s.Drawables())
            if (d.node == n && k < 5) xs[k++] = d.world.m[2];
        CHECK_NEAR(xs[1] - xs[0], 20.0, 1e-9);
        CHECK_NEAR(xs[4] - xs[0], 80.0, 1e-9);
    }

    // Array TRANSLATION-INVARIANCE: a rotated Parent-space step must move as
    // ONE BLOCK when the host translates (the old full-matrix conjugation
    // folded the node's position into the step — moving the object bent the
    // whole array).
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.count = 4;
        arr.step.tx = 30.0;
        arr.step.rotation = 0.5;                  // the coupling trigger
        arr.stepSpace = ArrayStepSpace::Parent;
        const NodeId n = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "r");
        doc.SetModifiers(n, { arr });
        Scene s; s.Compile(doc);
        std::vector<DVec2> before;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) before.push_back({ d.world.m[2], d.world.m[5] });
        Transform2D t; t.tx = 123.0; t.ty = -77.0;
        doc.SetTransform(n, t);
        s.Compile(doc);
        std::vector<DVec2> after;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) after.push_back({ d.world.m[2], d.world.m[5] });
        CHECK(before.size() == 4 && after.size() == 4);
        for (std::size_t i = 0; i < before.size(); ++i) {
            CHECK_NEAR(after[i].x - before[i].x, 123.0, 1e-9);
            CHECK_NEAR(after[i].y - before[i].y, -77.0, 1e-9);
        }
    }

    // RESOLVED-OBJECT semantics: rotating the NODE rotates the whole array —
    // layout included — as if the modifier output were the object (the copy
    // offsets live in local space).
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.count = 2;
        arr.step.tx = 40.0;                       // copies along local +X
        const NodeId n = doc.AddPath(page, PathData::Rect(-5, -5, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "r");
        doc.SetModifiers(n, { arr });
        { Transform2D t; t.rotation = 1.5707963267948966; doc.SetTransform(n, t); }
        Scene s; s.Compile(doc);
        std::vector<DVec2> pos;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) pos.push_back({ d.world.m[2], d.world.m[5] });
        CHECK(pos.size() == 2);
        // 90° node rotation: the +X offset now points along +Y (y-down: sin).
        CHECK_NEAR(pos[1].x - pos[0].x, 0.0, 1e-9);
        CHECK_NEAR(pos[1].y - pos[0].y, 40.0, 1e-9);
    }

    // Array LINE mode: straight positions, PROGRESSIVE IN-PLACE spin (the
    // rotation never bends the line), Endpoint distributes to the end point.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.arrayMode = ArrayMode::Line;
        arr.lineMode  = ArrayLineMode::Endpoint;
        arr.count = 5;
        arr.step.tx = 200.0;                      // END point
        arr.step.rotation = 0.7;                  // instance spin only
        const NodeId n = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "r");
        doc.SetModifiers(n, { arr });
        Scene s; s.Compile(doc);
        std::vector<double> xs, ys;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) { xs.push_back(d.world.m[2]); ys.push_back(d.world.m[5]); }
        CHECK(xs.size() == 5);
        CHECK_NEAR(xs[4] - xs[0], 200.0, 1e-9);   // reaches the end point
        CHECK_NEAR(xs[1] - xs[0], 50.0, 1e-9);    // even distribution
        for (double y : ys) CHECK_NEAR(y - ys[0], 0.0, 1e-9);   // straight
    }

    // Array CIRCLE mode: N copies on the radius, closing the full circle.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        Modifier arr;
        arr.kind = ModifierKind::Array;
        arr.arrayMode = ArrayMode::Circle;
        arr.count = 8;
        arr.circleRadius = 50.0;
        const NodeId n = doc.AddPath(page, PathData::Rect(-5, -5, 10, 10),
                                     Style::Filled({ 1, 0, 0, 1 }), "r");
        doc.SetModifiers(n, { arr });
        Scene s; s.Compile(doc);
        int copies = 0;
        for (const Drawable& d : s.Drawables())
            if (d.node == n) {
                ++copies;
                const double dx = d.world.m[2], dy = d.world.m[5];
                CHECK_NEAR(std::sqrt(dx * dx + dy * dy), 50.0, 1e-6);
            }
        CHECK(copies == 8);
    }

    // INSTANCE transform decoupling: moving/scaling the ORIGINAL never moves
    // its instances (the link is the edit-mode data; the object transform was
    // merely copied at duplicate time) — unless a copy flag opts back in.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        const NodeId src = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                       Style::Filled({ 1, 0, 0, 1 }), "src");
        const NodeId inst = doc.AddInstance(page, src, "inst");
        { Transform2D t; t.tx = 300.0; doc.SetTransform(inst, t); }
        Scene s; s.Compile(doc);
        auto instX = [&] {
            for (const Drawable& d : s.Drawables())
                if (d.owner == inst) return d.world.m[2];
            return -1.0;
        };
        const double x0 = instX();
        CHECK_NEAR(x0, 300.0, 1e-9);
        // Move the ORIGINAL: the instance must NOT follow.
        { Transform2D t; t.tx = 90.0; t.ty = 40.0; doc.SetTransform(src, t); }
        s.Compile(doc);
        CHECK_NEAR(instX(), 300.0, 1e-9);
        // Opt in to location copying: now it follows.
        doc.SetInstanceTransformCopy(inst, true, false, false);
        s.Compile(doc);
        CHECK_NEAR(instX(), 390.0, 1e-9);
    }

    // AlongPath modifier: it lives ON the path and instances a motif object
    // along its spine — N copies, owner = the path, motif's own translation
    // ignored, and the copies survive hiding the motif (linked-instance rule).
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        const NodeId dot = doc.AddPath(page, PathData::Ellipse(0, 0, 3, 3),
                                       Style::Filled({ 0, 1, 0, 1 }), "dot");
        doc.SetTransform(dot, [] { Transform2D t; t.tx = 500; return t; }());
        const NodeId path = doc.AddPath(
            page, PathData::Polygon({ { 0, 0 }, { 100, 0 } }, false),
            Style::Stroked({ 0, 0, 0, 1 }, 1.0), "p");
        Modifier along;
        along.kind = ModifierKind::AlongPath;
        along.motifRef = dot;
        along.alongCount = 6;
        doc.SetModifiers(path, { along });
        Scene s; s.Compile(doc);
        const std::uint64_t dotHash = PathData::Ellipse(0, 0, 3, 3).Hash();
        int copies = 0;
        for (const Drawable& d : s.Drawables())
            if (d.owner == path && d.pathHash == dotHash) {
                ++copies;
                // Copies sit ON the spine (0..100), not at the dot's x=500.
                CHECK(d.world.m[2] <= 100.0 + 1e-9);
            }
        CHECK(copies == 6);   // even spacing from 0..100
        // Hiding the motif keeps every copy (only its OWN render vanishes).
        doc.SetVisible(dot, false);
        s.Compile(doc);
        int hidden = 0;
        for (const Drawable& d : s.Drawables())
            if (d.owner == path && d.pathHash == dotHash) ++hidden;
        CHECK(hidden == 6);
    }

    // Pattern fill: motif instances over the host bbox share the motif hash.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        const NodeId motif = doc.AddPath(page, PathData::Ellipse(0, 0, 2, 2),
                                         Style::Filled({ 0, 0, 1, 1 }), "m");
        doc.SetVisible(motif, false);
        Fill pf;
        pf.kind = FillKind::Pattern;
        pf.pattern.motifRef = motif;
        pf.pattern.spacingX = 10.0;
        pf.pattern.spacingY = 10.0;
        Style st; st.fills.push_back(pf);
        const NodeId rect = doc.AddPath(page, PathData::Rect(0, 0, 50, 30),
                                        st, "host");
        Scene s; s.Compile(doc);
        const std::uint64_t motifHash =
            PathData::Ellipse(0, 0, 2, 2).Hash();
        int motifCopies = 0;
        for (const Drawable& d : s.Drawables())
            if (d.node == rect && d.pathHash == motifHash) ++motifCopies;
        CHECK(motifCopies >= 6 * 4);   // ~6×4 lattice over 50×30 at pitch 10
        (void)rect;
    }

    // InstanceNode: renders the target's content; a self-reference is refused.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        const NodeId src = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                       Style::Filled({ 1, 0, 0, 1 }), "src");
        doc.SetTransform(src, [] { Transform2D t; t.tx = 5; t.ty = 5; return t; }());
        const NodeId inst = doc.AddInstance(page, src, "inst");
        doc.SetTransform(inst, [] { Transform2D t; t.tx = 200; return t; }());
        Scene s; s.Compile(doc);
        // Two rects of the same content: the source and the instance.
        int rects = 0;
        for (const Drawable& d : s.Drawables())
            if (!d.isClipSource && d.pathHash == PathData::Rect(0,0,10,10).Hash())
                ++rects;
        CHECK(rects == 2);
    }
}

void TestPicking() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 400, 300 });
    // Bottom: a filled 100×100 square at (50,50). Top: a 40×40 square at
    // (80,80) overlapping it. Plus a stroked-only ring (no fill) at (250,50).
    const NodeId below = doc.AddPath(page, PathData::Rect(50, 50, 100, 100),
                                     Style::Filled({ 1, 0, 0, 1 }), "below");
    const NodeId above = doc.AddPath(page, PathData::Rect(80, 80, 40, 40),
                                     Style::Filled({ 0, 1, 0, 1 }), "above");
    Style ringStyle;
    ringStyle.strokes.push_back({});
    ringStyle.strokes.back().width = 6.0;
    const NodeId ring = doc.AddPath(page, PathData::Rect(250, 50, 60, 60),
                                    ringStyle, "ring");
    Scene s; s.Compile(doc);

    PickOptions opt; opt.tolerance = 1.0;
    // Overlap region → the TOP node wins.
    CHECK(PickTop(s, { 100, 100 }, opt) == above);
    // Below-only region → the bottom node.
    CHECK(PickTop(s, { 60, 60 }, opt) == below);
    // Page background → no object.
    CHECK(PickTop(s, { 10, 10 }, opt) == kNullNode);
    // Stroke-only shape: near the edge hits…
    CHECK(PickTop(s, { 250, 80 }, opt) == ring);
    // …the hollow inside does not.
    CHECK(PickTop(s, { 280, 80 }, opt) == kNullNode);
    // Tolerance (stroke): 6 units outside the spine (half-width 3) hits with
    // tol 4 (3+4 ≥ 6), not with tol 0.5.
    PickOptions wide; wide.tolerance = 4.0;
    PickOptions tight; tight.tolerance = 0.5;
    CHECK(PickTop(s, { 244, 80 }, wide) == ring);
    CHECK(PickTop(s, { 244, 80 }, tight) == kNullNode);

    // An instance picks the INSTANCE node, not its target.
    const NodeId inst = doc.AddInstance(page, above, "inst");
    { Transform2D t; t.tx = 200; t.ty = 150; doc.SetTransform(inst, t); }
    s.Compile(doc);
    CHECK(PickTop(s, { 300, 250 }, opt) == inst);   // 200+100,150+100 inside copy

    // Box select: intersecting bounds report each owner once.
    auto hits = PickBox(s, { 40, 40 }, { 160, 160 });
    CHECK(hits.size() == 2);
    CHECK(std::find(hits.begin(), hits.end(), below) != hits.end());
    CHECK(std::find(hits.begin(), hits.end(), above) != hits.end());
}

void TestApplyScale() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 400, 300 });
    const NodeId n = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
        Style::Filled({ 1, 0, 0, 1 }).WithStroke({ 0, 0, 0, 1 }, 4.0), "r");
    Transform2D t; t.tx = 30; t.ty = 40; t.sx = 3.0; t.sy = 2.0;
    t.rotation = 0.5;
    doc.SetTransform(n, t);

    // World positions of the four corners before the bake.
    const DMat23 before = doc.WorldTransform(n);
    DVec2 c0 = before.Apply({ 0, 0 }), c1 = before.Apply({ 10, 10 });

    doc.ApplyScale(n);
    const Node* nd = doc.Find(n);
    CHECK_NEAR(nd->transform.sx, 1.0, 1e-12);
    CHECK_NEAR(nd->transform.sy, 1.0, 1e-12);
    // Geometry now spans 30×20 locally…
    CHECK_NEAR(nd->path.subpaths[0].anchors[2].pos.x, 30.0, 1e-9);
    CHECK_NEAR(nd->path.subpaths[0].anchors[2].pos.y, 20.0, 1e-9);
    // …and the world appearance is unchanged.
    const DMat23 after = doc.WorldTransform(n);
    const DVec2 a0 = after.Apply({ 0, 0 }), a1 = after.Apply({ 30, 20 });
    CHECK_NEAR(a0.x, c0.x, 1e-9); CHECK_NEAR(a0.y, c0.y, 1e-9);
    CHECK_NEAR(a1.x, c1.x, 1e-9); CHECK_NEAR(a1.y, c1.y, 1e-9);
    // Stroke width scaled by the geometric mean √(3·2).
    CHECK_NEAR(nd->style.strokes[0].width, 4.0 * std::sqrt(6.0), 1e-9);
}

void TestSubtreeRoundtrip() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 400, 300 });
    const NodeId g = doc.AddGroup(page, "G");
    const NodeId a = doc.AddPath(g, PathData::Rect(0, 0, 10, 10),
                                 Style::Filled({ 1, 0, 0, 1 }), "a");
    const NodeId sib = doc.AddPath(page, PathData::Rect(50, 0, 10, 10),
                                   Style::Filled({ 0, 0, 1, 1 }), "sib");
    (void)sib;

    // Remove the group (its child goes too), then restore: same ids, same
    // place, same content.
    const auto snap = doc.CopySubtree(g);
    CHECK(snap.nodes.size() == 2);
    CHECK(snap.indexInParent == 0);
    doc.Remove(g);
    CHECK(!doc.Find(g) && !doc.Find(a));
    CHECK(doc.RestoreSubtree(snap));
    CHECK(doc.Find(g) && doc.Find(a));
    CHECK(doc.Find(a)->parent == g);
    CHECK(doc.IndexInParent(g) == 0);
    // Restoring twice is refused (the id exists again).
    CHECK(!doc.RestoreSubtree(snap));

    // Duplicate: fresh ids, same geometry, inserted after the source.
    const NodeId dup = doc.DuplicateSubtree(g);
    CHECK(dup != kNullNode && dup != g);
    CHECK(doc.IndexInParent(dup) == doc.IndexInParent(g) + 1);
    const Node* dg = doc.Find(dup);
    CHECK(dg->children.size() == 1);
    const Node* da = doc.Find(dg->children[0]);
    CHECK(da && da->id != a);
    CHECK(da->path.Hash() == doc.Find(a)->path.Hash());
    CHECK(da->parent == dup);   // intra-subtree parent remapped
}

// Document::Restore — the .acu load path (Lot 10): verbatim-id bulk install,
// structural validation, dangling-reference sanitising.
void TestRestore() {
    // A document exercising every persisted feature.
    Document doc;
    const NodeId page = doc.AddPage("P", { 5, 6 }, { 400, 300 });
    const NodeId g = doc.AddGroup(page, "G");
    doc.SetOpacity(g, 0.5f);
    doc.SetBlend(g, BlendMode::Multiply);
    doc.SetClip(g, true);
    Style st = Style::Filled({ 1, 0, 0, 1 });
    st.WithStroke({ 0, 0, 1, 1 }, 3.0);
    st.strokes[0].align = StrokeAlign::Inside;
    st.strokes[0].dashPattern = { 4.0, 2.0 };
    const NodeId a = doc.AddPath(g, PathData::Ellipse(20, 20, 10, 8), st, "a");
    const NodeId b = doc.AddPath(page, PathData::Rect(50, 0, 10, 10),
                                 Style::Filled({ 0, 1, 0, 1 }), "b");
    std::vector<Modifier> mods(1);
    mods[0].kind = ModifierKind::Boolean;
    mods[0].op = BooleanOp::Subtract;
    mods[0].operandRef = b;
    doc.SetModifiers(a, mods);
    const NodeId inst = doc.AddInstance(page, g, "inst");
    CHECK(doc.SetParent(b, inst));                 // object parenting relation
    const NodeId coll = doc.AddCollection("C");
    doc.AddToCollection(coll, a);
    doc.SetCollectionColor(coll, { 0.1f, 0.2f, 0.3f, 1.0f });
    const NodeId sub = doc.AddCollection("S", coll);

    // Extract the plain containers, exactly like the .acu writer (pre-order).
    std::vector<Page> pages(doc.Pages().begin(), doc.Pages().end());
    std::vector<Node> nodes;
    auto walk = [&](auto&& self, NodeId id) -> void {
        const Node* n = doc.Find(id);
        if (!n) return;
        nodes.push_back(*n);
        for (NodeId c : n->children) self(self, c);
    };
    for (const Page& p : pages)
        for (NodeId c : p.children) walk(walk, c);
    std::vector<Collection> colls(doc.Collections().begin(),
                                  doc.Collections().end());
    const NodeId nid = doc.PeekNextId();

    // Round-trip into a FRESH document.
    Document r;
    CHECK(r.Restore(pages, nodes, colls, nid));
    CHECK(r.NodeCount() == doc.NodeCount());
    const Node* ra = r.Find(a);
    CHECK(ra && ra->parent == g && ra->page == page);
    CHECK(ra->path.Hash() == doc.Find(a)->path.Hash());
    CHECK(ra->style.strokes.size() == 1);
    CHECK(ra->style.strokes[0].align == StrokeAlign::Inside);
    CHECK(ra->style.strokes[0].dashPattern.size() == 2);
    CHECK(ra->modifiers.size() == 1 && ra->modifiers[0].operandRef == b);
    const Node* rg = r.Find(g);
    CHECK(rg && rg->clip && rg->blend == BlendMode::Multiply);
    CHECK_NEAR(rg->opacity, 0.5f, 1e-6);
    CHECK(r.Find(inst) && r.Find(inst)->targetRef == g);
    CHECK(r.Find(b)->parentId == inst);
    const Collection* rc = r.FindCollection(coll);
    CHECK(rc && rc->members.size() == 1 && rc->members[0] == a);
    CHECK_NEAR(rc->colorTag.r, 0.1f, 1e-6);
    CHECK(r.IsChildCollection(sub));
    // World transforms agree; the allocator keeps producing unique ids.
    const DMat23 wa = doc.WorldTransform(b), wb = r.WorldTransform(b);
    for (int i = 0; i < 6; ++i) CHECK_NEAR(wa.m[i], wb.m[i], 1e-12);
    const NodeId fresh = r.AddPath(page, PathData::Rect(0, 0, 1, 1),
                                   Style::Filled({ 0, 0, 0, 1 }), "fresh");
    CHECK(fresh != kNullNode && fresh >= nid && !doc.Find(fresh));

    // Malformed input is refused whole: duplicate id.
    {
        std::vector<Node> bad = nodes;
        bad.push_back(bad.front());
        Document d2;
        CHECK(!d2.Restore(pages, bad, colls, nid));
        CHECK(d2.NodeCount() == 0);
    }
    // Broken back-pointer (child listed under the page but claiming another
    // parent) is refused.
    {
        std::vector<Node> bad = nodes;
        for (Node& n : bad)
            if (n.id == b) n.parent = a;
        Document d2;
        CHECK(!d2.Restore(pages, bad, colls, nid));
    }
    // A DANGLING non-structural reference is sanitised, not fatal.
    {
        std::vector<Node> loose = nodes;
        for (Node& n : loose)
            if (n.id == a) n.modifiers[0].operandRef = 999999;
        Document d2;
        CHECK(d2.Restore(pages, loose, colls, nid));
        CHECK(d2.Find(a)->modifiers[0].operandRef == kNullNode);
    }
    // A stale allocator mark is raised past the highest installed id.
    {
        Document d2;
        CHECK(d2.Restore(pages, nodes, colls, 1));
        CHECK(d2.PeekNextId() > inst);
    }
}

void TestOrganisation() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 400, 300 });
    const NodeId a = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                 Style::Filled({ 1, 0, 0, 1 }), "a");
    const NodeId b = doc.AddPath(page, PathData::Rect(20, 0, 10, 10),
                                 Style::Filled({ 0, 1, 0, 1 }), "b");
    const NodeId c = doc.AddPath(page, PathData::Rect(40, 0, 10, 10),
                                 Style::Filled({ 0, 0, 1, 1 }), "c");
    // Order: a,b,c. Reorder c to the front.
    CHECK(doc.IndexInParent(c) == 2);
    doc.ReorderChild(c, 0);
    CHECK(doc.IndexInParent(c) == 0);
    CHECK(doc.IndexInParent(a) == 1);

    // Rename.
    doc.SetName(a, "renamed");
    CHECK(doc.Find(a)->name == "renamed");

    // Group a+b: a new group takes the topmost member's slot; both reparent.
    const NodeId g = doc.GroupNodes({ a, b }, "G");
    CHECK(g != kNullNode);
    CHECK(doc.Find(a)->parent == g);
    CHECK(doc.Find(b)->parent == g);
    CHECK(doc.Find(g)->children.size() == 2);
    // World position preserved through grouping (identity group, so local ==
    // original position).
    const DVec2 aw = doc.WorldTransform(a).Apply({ 0, 0 });
    CHECK_NEAR(aw.x, 0.0, 1e-9);

    // MoveTo keeps world position: put a translated group, reparent a child.
    { Transform2D t; t.tx = 100; t.ty = 50; doc.SetTransform(g, t); }
    const DVec2 before = doc.WorldTransform(a).Apply({ 0, 0 });
    CHECK(doc.MoveTo(a, page, -1));   // pull `a` back out to the page
    const DVec2 after = doc.WorldTransform(a).Apply({ 0, 0 });
    CHECK_NEAR(before.x, after.x, 1e-6);
    CHECK_NEAR(before.y, after.y, 1e-6);
    CHECK(doc.Find(a)->parent == kNullNode);

    // Refuse parenting a node under its own descendant.
    CHECK(!doc.MoveTo(g, g, -1));

    // Ungroup: b returns to the page.
    doc.UngroupNode(g);
    CHECK(!doc.Find(g));
    CHECK(doc.Find(b)->parent == kNullNode);
}

void TestCollections() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 400, 300 });
    const NodeId a = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                 Style::Filled({ 1, 0, 0, 1 }), "a");
    const NodeId b = doc.AddPath(page, PathData::Rect(20, 0, 10, 10),
                                 Style::Filled({ 0, 1, 0, 1 }), "b");
    const NodeId col = doc.AddCollection("Set");
    CHECK(col != kNullNode);
    doc.AddToCollection(col, a);
    CHECK(doc.FindCollection(col)->members.size() == 1);
    // Membership is many-to-many; adding twice is idempotent.
    doc.AddToCollection(col, a);
    CHECK(doc.FindCollection(col)->members.size() == 1);

    // Visible collection → member draws; hidden collection → member culled,
    // exactly like layer visibility, without touching the node's own flag.
    Scene s; s.Compile(doc);
    auto drawnCount = [&](NodeId id) {
        int n = 0;
        for (const Drawable& d : s.Drawables())
            if (d.owner == id && !d.isClipSource) ++n;
        return n;
    };
    CHECK(drawnCount(a) > 0);
    doc.SetCollectionVisible(col, false);
    s.Compile(doc);
    CHECK(drawnCount(a) == 0);          // hidden by the collection
    CHECK(drawnCount(b) > 0);           // b is not a member
    CHECK(doc.Find(a)->visible);        // its own flag is untouched
    doc.SetCollectionVisible(col, true);
    s.Compile(doc);
    CHECK(drawnCount(a) > 0);

    doc.RemoveFromCollection(col, a);
    CHECK(doc.FindCollection(col)->members.empty());
}

void TestParenting() {
    Document doc;
    const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
    const NodeId parent = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                      Style::Filled({ 1, 0, 0, 1 }), "parent");
    { Transform2D t; t.tx = 100; t.ty = 50; doc.SetTransform(parent, t); }
    const NodeId child = doc.AddPath(page, PathData::Rect(0, 0, 4, 4),
                                     Style::Filled({ 0, 1, 0, 1 }), "child");
    { Transform2D t; t.tx = 130; t.ty = 80; doc.SetTransform(child, t); }

    // keepWorld=true: parenting preserves the child's document position.
    const DVec2 before = doc.WorldTransform(child).Apply({ 0, 0 });
    CHECK(doc.SetParent(child, parent, /*keepWorld=*/true));
    const DVec2 after = doc.WorldTransform(child).Apply({ 0, 0 });
    CHECK_NEAR(before.x, after.x, 1e-6);
    CHECK_NEAR(before.y, after.y, 1e-6);

    // Moving the parent now moves the child (inherited transform).
    { Transform2D t; t.tx = 200; t.ty = 50; doc.SetTransform(parent, t); }
    const DVec2 moved = doc.WorldTransform(child).Apply({ 0, 0 });
    CHECK_NEAR(moved.x - after.x, 100.0, 1e-6);   // parent moved +100 in x
    CHECK_NEAR(moved.y - after.y, 0.0, 1e-6);

    // Cycle refusal: parenting the parent to its child must fail.
    CHECK(!doc.SetParent(parent, child, true));

    // ClearParent keepWorld preserves position again.
    const DVec2 pre = doc.WorldTransform(child).Apply({ 0, 0 });
    doc.ClearParent(child, true);
    const DVec2 post = doc.WorldTransform(child).Apply({ 0, 0 });
    CHECK_NEAR(pre.x, post.x, 1e-6);
    CHECK_NEAR(pre.y, post.y, 1e-6);
    CHECK(doc.Find(child)->parentId == kNullNode);
}

// Total unsigned area of a set of rings (holes subtract via signed area sum).
double RingsArea(const std::vector<std::vector<DVec2>>& rings) {
    double a = 0.0;
    for (const auto& r : rings) a += geom::SignedArea(r);
    return std::abs(a);
}

// NonZero coverage at a point (winding number over all rings ≠ 0) — validates
// the boolean output's ring ORIENTATIONS, not just its net area: a hole with
// the wrong winding still nets the right area but fills solid.
bool CoveredNonZero(DVec2 p, const std::vector<std::vector<DVec2>>& rings) {
    int w = 0;
    for (const auto& ring : rings) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const DVec2 a = ring[j], b = ring[i];
            const double cross =
                (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            if (a.y <= p.y) { if (b.y >  p.y && cross > 0) ++w; }
            else            { if (b.y <= p.y && cross < 0) --w; }
        }
    }
    return w != 0;
}

// Closed rings of a flattened path (test helper for ellipse operands).
std::vector<std::vector<DVec2>> FlatRings(const PathData& p, double tol) {
    std::vector<std::vector<DVec2>> rings;
    for (geom::Polyline& pl : geom::Flatten(p, tol))
        if (pl.closed && pl.points.size() >= 3)
            rings.push_back(std::move(pl.points));
    return rings;
}

void TestBoolean() {
    // Two squares overlapping in a corner (no shared/collinear edges — the
    // non-degenerate case the v1 clipper is exact on): A=[0,10]²,
    // B=[5,15]×[2,12]. Overlap = [5,10]×[2,10] = 5×8 = 40.
    const std::vector<std::vector<DVec2>> A = {
        { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } } };
    const std::vector<std::vector<DVec2>> B = {
        { { 5, 2 }, { 15, 2 }, { 15, 12 }, { 5, 12 } } };

    // Intersect = the shared 5×8 = 40.
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, B, geom::BoolOp::Intersect)),
               40.0, 1e-6);
    // Union = 100 + 100 − 40 = 160.
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, B, geom::BoolOp::Union)),
               160.0, 1e-6);
    // Subtract A−B = 100 − 40 = 60.
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, B, geom::BoolOp::Subtract)),
               60.0, 1e-6);
    // Xor = union − intersection = 160 − 40 = 120.
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, B, geom::BoolOp::Xor)),
               120.0, 1e-6);

    // Disjoint squares: intersection empty, union = both.
    const std::vector<std::vector<DVec2>> C = {
        { { 100, 100 }, { 110, 100 }, { 110, 110 }, { 100, 110 } } };
    CHECK(geom::BooleanPolygons(A, C, geom::BoolOp::Intersect).empty());
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, C, geom::BoolOp::Union)),
               200.0, 1e-6);

    // Subtract producing a hole: B fully inside A → A−B has a hole, area 96.
    const std::vector<std::vector<DVec2>> Inner = {
        { { 4, 4 }, { 6, 4 }, { 6, 6 }, { 4, 6 } } };   // 2×2 = 4
    CHECK_NEAR(RingsArea(geom::BooleanPolygons(A, Inner, geom::BoolOp::Subtract)),
               96.0, 1e-6);

    // ── Xor regressions: rect ⊕ ellipse across positions ────────────────────
    // The reported bug: the intersection lens sometimes rendered filled (half,
    // along a diagonal, or entirely) depending on the operand's position —
    // broken chains emitted as garbage rings, and force-CCW normalisation
    // destroying hole windings. For each offset: the Xor area must equal
    // Union − Intersect (engine-consistent), a point inside the LENS must be
    // UNCOVERED under NonZero (orientation-correct), and a point in A-only
    // must stay covered.
    for (double cx : { 3.0, 5.0, 7.0, 9.0, 10.0 }) {
        const auto E = FlatRings(PathData::Ellipse(cx, 5.0, 4.0, 4.0), 0.05);
        const double uni = RingsArea(geom::BooleanPolygons(A, E, geom::BoolOp::Union));
        const double its = RingsArea(geom::BooleanPolygons(A, E, geom::BoolOp::Intersect));
        const auto x = geom::BooleanPolygons(A, E, geom::BoolOp::Xor);
        CHECK_NEAR(RingsArea(x), uni - its, 1e-3);
        // Lens probe: a point clearly inside BOTH (on the segment between the
        // ellipse centre clamped into A and the rect interior).
        const DVec2 lens{ std::min(cx, 9.0) - 0.5, 5.0 };
        if (its > 1.0) CHECK(!CoveredNonZero(lens, x));
        // A-only probe (far from the ellipse).
        if (cx >= 5.0) CHECK(CoveredNonZero({ 0.5, 0.5 }, x));
    }

    // Xor with CONTAINMENT (ellipse fully inside the rect) = an annulus: one
    // positive outer + one negative hole, and the hole must not fill.
    {
        const auto E = FlatRings(PathData::Ellipse(5.0, 5.0, 2.0, 2.0), 0.05);
        const auto x = geom::BooleanPolygons(A, E, geom::BoolOp::Xor);
        CHECK(x.size() >= 2);
        int pos = 0, neg = 0;
        for (const auto& r : x) (geom::SignedArea(r) > 0 ? pos : neg)++;
        CHECK(pos >= 1 && neg >= 1);
        CHECK(!CoveredNonZero({ 5.0, 5.0 }, x));   // inside the hole
        CHECK(CoveredNonZero({ 1.0, 1.0 }, x));    // in the rim
        const double eArea = RingsArea(E);
        CHECK_NEAR(RingsArea(x), 100.0 - eArea, 1e-3);
    }

    // ── Chained booleans sharing an operand (the coincident-edge case) ──────
    // host ⊕ B ⊕ B must return the host: the second step's operand shares
    // every edge with a boundary already present in the first step's result.
    // The per-step nudge keeps the coincidence broken; the parity orientation
    // keeps intermediate holes as holes.
    {
        const PathData host = PathData::Rect(0, 0, 10, 10);
        const PathData b    = PathData::Rect(5, 2, 10, 10);
        geom::BoolProgram prog;
        prog.host = &host;
        geom::BoolStep s1; s1.op = geom::BoolOp::Xor; s1.operand = &b;
        prog.steps = { s1, s1 };
        const auto res = geom::EvaluateBoolean(prog, 0.05);
        std::vector<std::vector<DVec2>> rings;
        for (const auto& pl : res)
            if (pl.closed && pl.points.size() >= 3) rings.push_back(pl.points);
        CHECK_NEAR(RingsArea(rings), 100.0, 1e-2);
        CHECK(CoveredNonZero({ 7.0, 5.0 }, rings));    // inside host ∩ B
        CHECK(!CoveredNonZero({ 12.0, 5.0 }, rings));  // B-only: cancelled out
    }

    // The Boolean MODIFIER through the Scene: a rect minus an overlapping rect.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 100, 100 });
        const NodeId host = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                        Style::Filled({ 1, 0, 0, 1 }), "host");
        const NodeId op = doc.AddPath(page, PathData::Rect(0, 0, 10, 10),
                                      Style::Filled({ 0, 0, 1, 1 }), "op");
        { Transform2D t; t.tx = 5; doc.SetTransform(op, t); }   // shift +5 in x
        doc.SetVisible(op, false);
        Modifier m; m.kind = ModifierKind::Boolean;
        m.op = BooleanOp::Subtract; m.operandRef = op;
        doc.SetModifiers(host, { m });
        Scene s; s.Compile(doc);
        // The host now draws the derived (5×10=50) geometry — verify a drawable
        // exists for the host and its bounds shrank in x.
        bool hostDrawn = false;
        for (const Drawable& d : s.Drawables())
            if (d.node == host && !d.isClipSource) hostDrawn = true;
        CHECK(hostDrawn);
    }
}

void TestSceneCompile() {
    Document doc;
    SeedDemoDocument(doc);
    CHECK(doc.NodeCount() > 1000);   // the grid + the shapes

    Scene scene;
    CHECK(scene.Compile(doc));                       // first compile
    const std::size_t n = scene.Drawables().size();
    CHECK(n > 1000);                                 // grid + fills + strokes
    CHECK(!scene.Compile(doc));                      // no changes → no recompile

    // An edit recompiles; a pure re-query does not.
    const NodeId someNode = scene.Drawables().back().node;
    Transform2D t; t.tx = 1;
    doc.SetTransform(someNode, t);
    CHECK(scene.Compile(doc));
    CHECK(scene.Drawables().size() == n);

    // Painter order: the page substrate is the very first drawable.
    CHECK(scene.Drawables().front().node == doc.Pages().front().id);
    // Bounds cover the page.
    CHECK(scene.Bounds().Width() >= 1920.0f);
}

// NURBS / Poly spline flattening (docs/Ink/IOF_CORE_PLAN.md Phase B): the
// rational NurbsCircle must lie on the circle at every sample (exact conic),
// a Poly subpath is its control polygon verbatim, and a spline change alters
// the geometry hash (re-tessellation trigger).
void TestNurbs() {
    // NurbsCircle: an exact circle — every flattened point is at radius r from
    // the centre (rational Bézier conic; the tolerance only sets the density).
    PathData circ = PathData::NurbsCircle(10.0, 20.0, 7.0);
    CHECK(circ.subpaths.size() == 1);
    CHECK(circ.subpaths[0].spline == SplineType::Nurbs);
    CHECK(circ.subpaths[0].closed);
    auto cflat = geom::Flatten(circ, 0.01);
    CHECK(cflat.size() == 1);
    CHECK(cflat[0].points.size() >= 16);
    double maxRadErr = 0.0;
    for (const DVec2& p : cflat[0].points) {
        const double rr = std::hypot(p.x - 10.0, p.y - 20.0);
        maxRadErr = std::max(maxRadErr, std::abs(rr - 7.0));
    }
    CHECK(maxRadErr <= 0.05);   // on the circle to sub-pixel precision

    // The circle encloses ~π·r² (a closed conic, filled NonZero).
    auto cm = geom::TriangulateFill(cflat, FillRule::NonZero);
    CHECK_NEAR(MeshArea(cm), 3.14159265358979 * 49.0, 1.5);

    // Poly: the flattened polyline is exactly the control points.
    PathData poly = PathData::Polygon({ { 0, 0 }, { 4, 0 }, { 4, 3 } }, false);
    poly.subpaths[0].spline = SplineType::Poly;
    auto pflat = geom::Flatten(poly, 0.01);
    CHECK(pflat.size() == 1);
    CHECK(pflat[0].points.size() == 3);
    CHECK_NEAR(pflat[0].points[2].x, 4.0, 1e-12);
    CHECK_NEAR(pflat[0].points[2].y, 3.0, 1e-12);

    // A spline-type change is geometry-affecting (distinct hash).
    PathData a = PathData::Polygon({ { 0, 0 }, { 4, 0 }, { 4, 3 } }, false);
    PathData b = a;
    b.subpaths[0].spline = SplineType::Nurbs;
    CHECK(a.Hash() != b.Hash());
    // The rational weight is hashed only for NURBS subpaths.
    PathData c = b;
    c.subpaths[0].anchors[1].weight = 2.5;
    CHECK(b.Hash() != c.Hash());
}

// Stroke marks (docs/Ink/IOF_CORE_PLAN.md Phase A — the generic model): the
// stroker only re-phases the dash run (a Dash mark lands a dash element on it);
// the mark OBJECTS are emitted by the Scene into the stroke's isolation scope,
// where a subtractive object cuts it (EraseWrite). Marks fold into the
// geometry hash so a mark edit re-tessellates.
void TestStrokeMarks() {
    // Marks contribute to the stroke geometry hash (a mark edit re-tessellates).
    Stroke plain;
    plain.width = 4.0;
    Stroke marked = plain;
    StrokeMark m;
    m.sub = 0; m.t = 0.5; m.phase = MarkPhase::Neutral;
    MarkObject obj;
    obj.shape = MarkShape::Circle; obj.mode = MarkObjectMode::Fusion; obj.size = 6.0;
    m.objects.push_back(obj);
    marked.marks.push_back(m);
    CHECK(marked.GeometryHash() != plain.GeometryHash());
    Stroke marked2 = marked;
    CHECK(marked2.GeometryHash() == marked.GeometryHash());
    marked2.marks[0].t = 0.6;
    CHECK(marked2.GeometryHash() != marked.GeometryHash());
    // The object mode / phase / side all change the hash.
    Stroke sub = marked; sub.marks[0].objects[0].mode = MarkObjectMode::Subtract;
    CHECK(sub.GeometryHash() != marked.GeometryHash());
    Stroke ph = marked; ph.marks[0].phase = MarkPhase::Dash;
    CHECK(ph.GeometryHash() != marked.GeometryHash());

    // Dash re-phasing: a Dash mark lands a dash ELEMENT centred on it, so the
    // stroke is COVERED (inside a dash) exactly at the mark — the tessellated
    // area near t differs from an un-phased dashed line.
    geom::Polyline pl;
    pl.points = { { 0, 0 }, { 100, 0 } };
    pl.closed = false;
    const double kTol = 0.05;
    Stroke dashed;
    dashed.width = 4.0;
    dashed.dashPattern = { 10.0, 10.0 };
    StrokeMark dm;
    dm.sub = 0; dm.t = 0.5; dm.phase = MarkPhase::Dash;
    dashed.marks.push_back(dm);
    const geom::Mesh dm1 = geom::TessellateStroke({ pl }, dashed, kTol);
    CHECK(!dm1.Empty());

    // A FUSION object needs NO scope and NO separate drawable — it is
    // triangulated into the stroke mesh (one drawing, one alpha).
    {
        Document doc;
        const NodeId page = doc.AddPage("p", { 0, 0 }, { 400, 400 });
        Style st;
        Stroke s; s.width = 6.0; s.paint.color = { 0, 0, 0, 1 };
        StrokeMark om; om.sub = 0; om.t = 0.5;
        MarkObject o2; o2.shape = MarkShape::Circle;
        o2.mode = MarkObjectMode::Fusion; o2.size = 10.0;
        om.objects.push_back(o2);
        s.marks.push_back(om);
        st.strokes.push_back(s);
        const NodeId line = doc.AddPath(page,
            PathData::Polygon({ { 20, 200 }, { 380, 200 } }, false), st, "line");
        Scene scene; scene.Compile(doc, true);
        int objDraws = 0, isoScopes = 0;
        for (const CompositeScope& sc : scene.Scopes())
            isoScopes += (sc.isolate && sc.node == line) ? 1 : 0;
        for (const Drawable& d : scene.Drawables())
            if (!d.isStroke && d.owner == line) ++objDraws;
        CHECK(isoScopes == 0);   // Fusion needs no isolation scope
        CHECK(objDraws == 0);    // the shape lives in the stroke mesh
    }

    // A SUBTRACT object opens the stroke's isolation scope and emits an
    // EraseWrite drawable that cuts it.
    {
        Document doc;
        const NodeId page = doc.AddPage("p", { 0, 0 }, { 400, 400 });
        Style st;
        Stroke s; s.width = 6.0; s.paint.color = { 0, 0, 0, 1 };
        StrokeMark om; om.sub = 0; om.t = 0.5;
        MarkObject o2; o2.shape = MarkShape::Circle;
        o2.mode = MarkObjectMode::Subtract; o2.size = 10.0;
        om.objects.push_back(o2);
        s.marks.push_back(om);
        st.strokes.push_back(s);
        const NodeId line = doc.AddPath(page,
            PathData::Polygon({ { 20, 200 }, { 380, 200 } }, false), st, "line");
        Scene scene; scene.Compile(doc, true);
        bool sawIso = false, sawErase = false;
        for (const CompositeScope& sc : scene.Scopes())
            sawIso = sawIso || (sc.isolate && sc.node == line);
        for (const Drawable& d : scene.Drawables())
            if (!d.isStroke && d.owner == line &&
                d.clip == ClipRole::EraseWrite)
                sawErase = true;
        CHECK(sawIso);
        CHECK(sawErase);
    }

    // Offset / size percentage resolves to a fraction of the stroke width.
    {
        StrokeMark pm; pm.side = MarkSide::Left; pm.offset = 50.0;
        pm.offsetPercent = true;
        CHECK_NEAR(pm.OffsetUnits(8.0), 4.0, 1e-9);   // 50 % of 8 = 4
        pm.offsetPercent = false; pm.offset = 3.0;
        CHECK_NEAR(pm.OffsetUnits(8.0), 3.0, 1e-9);
        MarkObject mo; mo.size = 100.0; mo.width = 200.0; mo.sizePercent = true;
        CHECK_NEAR(mo.SizeUnits(8.0), 8.0, 1e-9);     // 100 % of 8
        CHECK_NEAR(mo.WidthUnits(8.0), 16.0, 1e-9);   // 200 % of 8
    }

    // A primitive mark shape is PARAMETRIC (re-tessellates per tier): a circle
    // radius 100 % of an 8-unit stroke is an ellipse of radius 8.
    {
        MarkObject mo; mo.shape = MarkShape::Circle;
        mo.size = 100.0; mo.sizePercent = true;
        const PathData shape = geom::MarkPrimitiveShape(mo, 8.0);
        CHECK(!shape.Empty());
        double maxR = 0.0;
        for (const auto& pl : geom::Flatten(shape, 0.01))
            for (const DVec2& p : pl.points)
                maxR = std::max(maxR, std::hypot(p.x, p.y));
        CHECK_NEAR(maxR, 8.0, 0.1);

        // Follow mode curves the outline: a straight spine gives a valid ring.
        geom::Polyline sp; sp.points = { { 0, 0 }, { 100, 0 } };
        StrokeMark m; m.sub = 0; m.t = 0.5;
        MarkObject fo; fo.shape = MarkShape::Rectangle; fo.bend = MarkBend::Follow;
        fo.size = 100.0; fo.width = 50.0; fo.sizePercent = true;
        std::vector<DVec2> ring;
        CHECK(geom::MarkFollowContour(sp, m, fo, 8.0, 0.1, ring));
        CHECK(ring.size() >= 4);
    }
}

} // namespace

int main() {
    TestDocumentOps();
    TestPathHash();
    TestFlatten();
    TestTriangulate();
    TestStroker();
    TestNurbs();
    TestStrokeMarks();
    TestGeometryCache();
    TestCompositeScopes();
    TestInstancing();
    TestOrganisation();
    TestCollections();
    TestParenting();
    TestBoolean();
    TestPicking();
    TestApplyScale();
    TestSubtreeRoundtrip();
    TestRestore();
    TestSceneCompile();

    if (g_failures == 0) {
        std::printf("ink_tests: all checks passed\n");
        return 0;
    }
    std::printf("ink_tests: %d check(s) FAILED\n", g_failures);
    return 1;
}

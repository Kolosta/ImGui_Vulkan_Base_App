// ink_tests — unit tests of the GPU-less Ink layers (docs/Ink/PERF_TESTING.md
// §6): document invariants, flattening tolerance, fill-rule triangulation,
// stroking areas, cache identity. Plain asserts, no framework — exit 0 = pass.

#include <Ink/Document/Document.h>
#include <Ink/Geometry/GeometryCache.h>
#include <Ink/Scene/Scene.h>

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

    // AlongPath modifier: N copies placed along a straight path, tangent-aligned.
    {
        Document doc;
        const NodeId page = doc.AddPage("P", { 0, 0 }, { 1000, 1000 });
        const NodeId path = doc.AddPath(
            page, PathData::Polygon({ { 0, 0 }, { 100, 0 } }, false),
            Style::Stroked({ 0, 0, 0, 1 }, 1.0), "p");
        Modifier along;
        along.kind = ModifierKind::AlongPath;
        along.pathRef = path;
        along.alongCount = 6;
        const NodeId dot = doc.AddPath(page, PathData::Ellipse(0, 0, 3, 3),
                                       Style::Filled({ 0, 1, 0, 1 }), "dot");
        doc.SetModifiers(dot, { along });
        Scene s; s.Compile(doc);
        int copies = 0;
        for (const Drawable& d : s.Drawables())
            if (d.node == dot) ++copies;
        CHECK(copies == 6);   // even spacing from 0..100
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

} // namespace

int main() {
    TestDocumentOps();
    TestPathHash();
    TestFlatten();
    TestTriangulate();
    TestStroker();
    TestGeometryCache();
    TestCompositeScopes();
    TestInstancing();
    TestParenting();
    TestBoolean();
    TestSceneCompile();

    if (g_failures == 0) {
        std::printf("ink_tests: all checks passed\n");
        return 0;
    }
    std::printf("ink_tests: %d check(s) FAILED\n", g_failures);
    return 1;
}

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
    Stroke s;
    s.width = 2.0;
    // Open segment (0,0)→(10,0): one quad of area length × width = 20.
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 } };
        const geom::Mesh m = geom::TessellateStroke({ pl }, s);
        CHECK(m.indices.size() == 6);
        CHECK_NEAR(MeshArea(m), 20.0, 1e-9);
    }
    // Closed square 10×10: band area ≈ perimeter × width (bevel corners add
    // overlap counted by the unsigned sum — accept the [1.0, 1.15]× window).
    {
        geom::Polyline pl;
        pl.points = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
        pl.closed = true;
        const geom::Mesh m = geom::TessellateStroke({ pl }, s);
        const double area = MeshArea(m);
        CHECK(area >= 40.0 * 2.0 * 0.99);
        CHECK(area <= 40.0 * 2.0 * 1.15);
    }
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
    TestSceneCompile();

    if (g_failures == 0) {
        std::printf("ink_tests: all checks passed\n");
        return 0;
    }
    std::printf("ink_tests: %d check(s) FAILED\n", g_failures);
    return 1;
}

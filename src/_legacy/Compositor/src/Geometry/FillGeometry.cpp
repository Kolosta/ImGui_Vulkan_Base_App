#include "Compositor/Geometry/FillGeometry.h"

#include "Renderer/Tessellation/Tessellator.h"
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - Geometry/FillGeometry (impl): document → stencil-then-cover fills.
//
//  Walks the same shapes the Tessellator does, but for each SOLID fill emits a
//  trivial winding FAN (pivot → each contour edge) instead of an ear-clipped
//  interior. The fan is drawn into the stencil under a non-zero rule; a cover
//  quad then paints the interior once. No triangulation, so a very heavy path is
//  O(contour) here, not O(contour^2) as the ear-clip is.
//
//  Curve/NURBS flattening reuses the Tessellator's PURE outline helpers
//  (OutlinePartSub / OutlinePartSubFilled) — geometry maths, NOT the legacy
//  renderer — so both engines sample curves identically during the transition.
//  The flatten detail follows the on-screen zoom via the SAME quantised bucket
//  the Tessellator uses (SetDetailScale below), so a same-bucket pan/zoom reuses
//  the buffer (the caller keys the build signature on the bucket, not the zoom).
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

using Renderer::Tessellator;
using Renderer::Vec2;

namespace {

// Eligible for the stencil-then-cover base path (Lot 13-4a, first sub-lot): a
// PURE solid-fill object whose whole baked base range is exactly the solid fill —
// so routing the fill to fan+cover and skipping the baked base loses nothing.
// Excluded (kept on the baked path for now): any stroke (opaque strokes bake into
// the base range), fill layers / patterns, line marks (decorators), and Polyline
// (never filled). Every part must be a plain solid fill. These migrate in later
// sub-lots; this keeps the first cut safe and its win measurable.
bool IsPureSolidFill(const Renderer::Shape& s) {
    if (s.parts.empty()) return false;
    bool anyFill = false;
    for (const Renderer::Part& p : s.parts) {
        if (p.kind == Renderer::ShapeKind::Polyline) return false;
        if (p.stroke.enabled)      return false;
        if (!p.fillLayers.empty()) return false;
        if (!p.marks.empty())      return false;
        if (p.fill.enabled)        anyFill = true;
    }
    return anyFill;
}

// Append one closed contour as a winding FAN into `fans`. The interior is NOT
// triangulated: from the pivot poly[0], each edge poly[i]→poly[i+1] makes one
// triangle (pivot, poly[i], poly[i+1]). Overlapping triangles are fine — the
// stencil counts winding, so interior pixels end non-zero and exterior zero. The
// implicit closing edge poly[n-1]→poly[0] and poly[0]→poly[1] pass through the
// pivot (zero area) and need no explicit triangle. A degenerate contour (<3
// points) contributes nothing.
void AppendContourFan(const std::vector<Vec2>& poly, std::vector<FanVertex>& fans) {
    const size_t n = poly.size();
    if (n < 3) return;
    fans.reserve(fans.size() + (n - 2) * 3);
    const FanVertex pivot{ poly[0].x, poly[0].y };
    for (size_t i = 1; i + 1 < n; ++i) {
        fans.push_back(pivot);
        fans.push_back(FanVertex{ poly[i].x,     poly[i].y });
        fans.push_back(FanVertex{ poly[i + 1].x, poly[i + 1].y });
    }
}

// Accumulate the world bbox over a contour.
inline void GrowBounds(const std::vector<Vec2>& poly,
                       float& mnx, float& mny, float& mxx, float& mxy) {
    for (const Vec2& p : poly) {
        mnx = std::min(mnx, p.x); mny = std::min(mny, p.y);
        mxx = std::max(mxx, p.x); mxy = std::max(mxy, p.y);
    }
}

// Build ONE shape's solid-fill coverage (all parts / subpaths) into `fans`, and
// push a FillObject if it produced any coverage. Mirrors the Tessellator's base
// solid-fill selection (fill.enabled, non-Polyline, closed contour or an open
// curve-like area closed virtually), but never ear-clips. `pageOrigin` places the
// object on its page (world doc-units).
//
// Incremental (Lot 13-1b-3): if `prevById` is non-null AND this shape is NOT in
// `dirtyIds` AND it has a prior FillObject, REUSE that verbatim (fromScratch=false,
// pool offsets preserved) — no flatten. Otherwise (re)flatten fresh into `fans`.
void AppendShapeFill(const Renderer::Shape& s, Vec2 pageOrigin, float zoom,
                     std::vector<FanVertex>& fans, std::vector<FillObject>& out,
                     const std::unordered_set<uint64_t>* dirtyIds,
                     const std::unordered_map<uint64_t, FillObject>* prevById) {
    if (!s.visible) return;
    if (!IsPureSolidFill(s)) return;   // non-pure objects stay on the baked path
    // Reuse an unchanged object's prior slice without re-flattening (the tess win).
    if (prevById && dirtyIds && dirtyIds->count(s.id) == 0) {
        auto it = prevById->find(s.id);
        if (it != prevById->end()) {
            FillObject o = it->second;   // pool-relative offsets + geometry preserved
            o.fromScratch = false;       // pool will Touch (keep), not Write
            out.push_back(std::move(o));
            return;
        }
        // No prior entry (first sighting though not in dirty) → fall through + flatten.
    }
    const uint32_t fanFirst = (uint32_t)fans.size();
    float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
    // The fill colour is taken from the FIRST solid-filled part (the object's base
    // colour). A multi-part object with different fill colours is uncommon here and
    // uses the first part's colour — the same object-level colour the cover pass
    // paints; per-part solid colours are a later refinement.
    Renderer::Color color{ 0, 0, 0, 1 };
    bool haveColor = false;

    for (const Renderer::Part& part : s.parts) {
        const bool wantsFill = part.fill.enabled &&
                               part.kind != Renderer::ShapeKind::Polyline;
        if (!wantsFill) continue;
        const int subs = Tessellator::SubpathCount(part);
        for (int sp = 0; sp < subs; ++sp) {
            bool closed = false;
            std::vector<Vec2> poly =
                Tessellator::OutlinePartSub(s, part, sp, zoom, closed, pageOrigin);
            const std::vector<Vec2>* fp = nullptr;
            std::vector<Vec2> filled;
            if (closed && poly.size() >= 3) {
                fp = &poly;
            } else if (!closed && part.IsCurveLike()) {
                filled = Tessellator::OutlinePartSubFilled(s, part, sp, zoom, pageOrigin);
                if (filled.size() >= 3) fp = &filled;
            }
            if (!fp) continue;
            AppendContourFan(*fp, fans);
            GrowBounds(*fp, mnx, mny, mxx, mxy);
            if (!haveColor) { color = part.fill.color; haveColor = true; }
        }
    }

    const uint32_t fanCount = (uint32_t)fans.size() - fanFirst;
    if (fanCount == 0 || mxx <= mnx || mxy <= mny) return;
    // Cover quad: 6 verts (two triangles) over the world bbox, appended right after
    // the fans in the SAME stream. The cover pass draws these stencil-tested != 0.
    const uint32_t coverFirst = (uint32_t)fans.size();
    fans.push_back(FanVertex{ mnx, mny });
    fans.push_back(FanVertex{ mxx, mny });
    fans.push_back(FanVertex{ mxx, mxy });
    fans.push_back(FanVertex{ mnx, mny });
    fans.push_back(FanVertex{ mxx, mxy });
    fans.push_back(FanVertex{ mnx, mxy });

    FillObject o;
    o.shapeId    = s.id;
    o.fanFirst   = fanFirst;
    o.fanCount   = fanCount;
    o.coverFirst = coverFirst;
    o.bbMinX = mnx; o.bbMinY = mny; o.bbMaxX = mxx; o.bbMaxY = mxy;
    o.r = color.r; o.g = color.g; o.b = color.b; o.a = color.a;
    out.push_back(std::move(o));
}

} // namespace

std::vector<FillPage> BuildDocumentFills(
    const Renderer::Document& doc, std::vector<FanVertex>& outFans, float zoom,
    const std::vector<Tessellator::PagePlacement>* placements, bool includeLoose,
    const std::unordered_set<uint64_t>* dirtyIds,
    const std::unordered_map<uint64_t, FillObject>* prevById) {
    // Match the Tessellator's flatten detail for this zoom (quantised bucket), then
    // restore — the curve sampling must agree so both engines look identical.
    const float prevDetail = Tessellator::GetDetailScale();
    Tessellator::SetDetailScale(Tessellator::DetailScaleForZoom(zoom));

    std::vector<FillPage> pages;
    pages.reserve(doc.artboards.size() + 1);

    for (size_t i = 0; i < doc.artboards.size(); ++i) {
        const Renderer::Artboard& ab = doc.artboards[i];
        Vec2 origin = ab.pos;
        bool visible = true;
        if (placements && i < placements->size()) {
            origin  = (*placements)[i].origin;
            visible = (*placements)[i].visible;
        }
        if (!visible) continue;
        FillPage page;
        page.pageIndex = (int)i;
        for (const Renderer::Shape& s : ab.shapes)
            AppendShapeFill(s, origin, zoom, outFans, page.objects, dirtyIds, prevById);
        pages.push_back(std::move(page));
    }

    if (includeLoose && !doc.looseShapes.empty()) {
        FillPage page;
        page.pageIndex = -1;
        for (const Renderer::Shape& s : doc.looseShapes)
            AppendShapeFill(s, Vec2{ 0, 0 }, zoom, outFans, page.objects, dirtyIds, prevById);
        if (!page.objects.empty()) pages.push_back(std::move(page));
    }

    Tessellator::SetDetailScale(prevDetail);
    return pages;
}

} // namespace Comp

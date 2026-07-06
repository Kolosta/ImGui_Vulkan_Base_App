#include "Compositor/Engine.h"
#include "../Internal.h"

#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - Frame/Signature: the per-view content signature.
//
//  An FNV-1a hash of everything RenderView would build for a document (shape ids
//  + per-shape cache hashes + compositing params + placements + includeLoose +
//  detail bucket). An unchanged signature lets RenderView reuse the per-view
//  vertex buffer, so pan/zoom within a detail bucket cost no re-tessellation.
//  Render-only params absent from HashShape (opacity, blend mode, erase) are
//  mixed in here so editing them rebuilds the view immediately.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

uint64_t Engine::BuildSignatureAndDiff(const Renderer::Document& doc,
                                       const std::vector<Renderer::Tessellator::PagePlacement>* placements,
                                       bool includeLoose, int detailBucket,
                                       DirtyTracker& dt, DirtyTracker::Diff& outDiff) const {
    uint64_t h = 1469598103934665603ull;          // FNV-1a offset
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        while (n--) { h ^= *b++; h *= 1099511628211ull; }
    };
    mix(&detailBucket, sizeof(detailBucket));
    uint8_t il = includeLoose ? 1 : 0; mix(&il, 1);
    if (placements)
        for (const auto& pp : *placements) {
            mix(&pp.origin.x, sizeof(float));
            mix(&pp.origin.y, sizeof(float));
            uint8_t v = pp.visible ? 1 : 0; mix(&v, 1);
        }

    dt.Begin();   // start a per-shape diff pass; Feed() each shape below

    // One PER-SHAPE hash of everything that affects THIS shape's render (geometry +
    // paint + transform via HashShape, plus the render-only compositing params +
    // detail bucket). It both feeds the dirty tracker AND is mixed into the global
    // signature — so a single O(N) walk yields the rebuild gate and the change diff.
    auto perShape = [&](const Renderer::Shape& s, Renderer::Vec2 pageOrigin) {
        uint64_t sh = Renderer::Tessellator::HashShape(s, pageOrigin);
        // FNV-1a fold of (geom hash, opacity, blend, group, detail bucket) → one id.
        uint64_t ph = 1469598103934665603ull;
        auto fold = [&](const void* p, size_t n) {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            while (n--) { ph ^= *b++; ph *= 1099511628211ull; }
        };
        uint8_t bm = (uint8_t)s.blendMode;
        fold(&sh, sizeof(sh)); fold(&s.opacity, sizeof(float)); fold(&bm, 1);
        fold(&s.groupId, sizeof(s.groupId)); fold(&detailBucket, sizeof(detailBucket));
        dt.Feed(s.id, ph);
        // Global signature: id + the same per-shape hash (order-sensitive walk).
        mix(&s.id, sizeof(s.id)); mix(&ph, sizeof(ph));
    };

    for (const Renderer::Artboard& ab : doc.artboards) {
        mix(&ab.id, sizeof(ab.id));
        mix(&ab.pos.x, sizeof(float)); mix(&ab.pos.y, sizeof(float));
        mix(&ab.size.x, sizeof(float)); mix(&ab.size.y, sizeof(float));
        uint8_t pv = ab.pageVisible ? 1 : 0; mix(&pv, 1);
        for (const Renderer::Shape& s : ab.shapes) perShape(s, ab.pos);
    }
    if (includeLoose)
        for (const Renderer::Shape& s : doc.looseShapes) perShape(s, Renderer::Vec2{ 0, 0 });

    // Layer-group compositing (Lot 11): a group's own opacity/blend isn't on any
    // shape, so mix every group collection's params — editing a group rebuilds the
    // view immediately (same reason opacity/blend are mixed above). Not per-shape, so
    // these don't feed the tracker (a group-param edit shows as a rebuild with 0
    // dirty shapes, which is accurate — no shape geometry changed).
    for (const Renderer::Collection& c : doc.collections) {
        if (!c.isLayerGroup) continue;
        uint8_t gb = (uint8_t)c.blendMode;
        mix(&c.id, sizeof(c.id)); mix(&c.opacity, sizeof(float)); mix(&gb, 1);
    }

    outDiff = dt.Finish();
    return h;
}

// ── Per-view target lifecycle ──────────────────────────────────────────────────

} // namespace Comp

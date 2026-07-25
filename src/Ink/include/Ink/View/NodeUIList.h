#pragma once

#include "Ink/Core/Math.h"
#include <cstdint>
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  NodeUIList — the per-frame TEXTURED-quad builder for a view with no
//  document content (docs/Ink/NODE_UI.md — the Node Graph Editor's own
//  canvas). Sibling to OverlayList (untextured shapes: node boxes, borders,
//  cables, port dots — still drawn through Overlay(), unchanged), this list
//  is specifically for glyph text and live preview vignettes, the two things
//  Overlay's vertex format (no UV) cannot express.
//
//  CPU-only, cleared every frame after recording — same contract as
//  OverlayList. Positions are in VIEW PIXELS (already camera-transformed by
//  the caller, exactly like Overlay()), colors are LINEAR PREMULTIPLIED.
//
//  One shared pipeline draws both uses (ImGui's own "white-pixel + atlas"
//  trick, reimplemented without ImGui): `outColor = texture(tex, uv) * tint`.
//  A glyph quad samples the font atlas (stored PREMULTIPLIED white, i.e.
//  rgb=coverage, a=coverage) with `tint` = the desired text color, so the
//  multiply yields a correctly colored, anti-aliased, premultiplied glyph. A
//  preview quad samples another View's own already-rendered image with
//  `tint` left at opaque white (1,1,1,1) — pass-through — or dimmed for a
//  muted node by using a uniform gray tint.
// ─────────────────────────────────────────────────────────────────────────────
class NodeUIList {
public:
    struct Vertex {
        float x, y;       // view px
        float u, v;       // atlas or source-view UV [0,1]
        float r, g, b, a; // linear premultiplied tint
    };

    void Clear();
    bool Empty() const { return vertices_.empty(); }

    // A quad sampling the shared font glyph atlas — `uvMin`/`uvMax` from
    // GlyphAtlas::Metrics (below). Used for a single glyph OR any other
    // atlas-packed sprite (the atlas keeps a small solid-white cell too, so a
    // future caller could use this for a tinted icon without a second
    // pipeline — not exercised yet, kept simple: text only, v1).
    void AddAtlasQuad(Vec2 min, Vec2 max, Vec2 uvMin, Vec2 uvMax, const Color& tint);

    // A quad sampling ANOTHER view's CURRENTLY rendered image (a live
    // preview vignette) — `sourceDescriptorSet` is that view's
    // `View::PreviewDescriptorSet()` (an opaque handle; 0 = skip, e.g. the
    // source view has no render targets yet).
    void AddPreviewQuad(Vec2 min, Vec2 max, std::uint64_t sourceDescriptorSet,
                        const Color& tint = Color{ 1, 1, 1, 1 });

    // One contiguous vertex range sharing a texture: `sourceSet == 0` means
    // "the shared font atlas", any other value is an opaque descriptor-set
    // handle from AddPreviewQuad (Renderer-internal code reinterprets it back
    // to a VkDescriptorSet — this header stays RHI-type-free, same rule as
    // OverlayList.h).
    struct Batch {
        std::uint32_t first = 0, count = 0;
        std::uint64_t sourceSet = 0;
    };

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    const std::vector<Batch>&  Batches()  const { return batches_; }
    std::size_t ByteSize() const { return vertices_.size() * sizeof(Vertex); }

private:
    void PushQuad(Vec2 min, Vec2 max, Vec2 uvMin, Vec2 uvMax, const Color& tint,
                 std::uint64_t sourceSet);

    std::vector<Vertex> vertices_;
    std::vector<Batch>  batches_;
};

} // namespace Ink

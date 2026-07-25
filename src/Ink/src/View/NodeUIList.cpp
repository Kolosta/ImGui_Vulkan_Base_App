#include "Ink/View/NodeUIList.h"

namespace Ink {

void NodeUIList::Clear() {
    vertices_.clear();
    batches_.clear();
}

void NodeUIList::PushQuad(Vec2 min, Vec2 max, Vec2 uvMin, Vec2 uvMax,
                         const Color& tint, std::uint64_t sourceSet) {
    const std::uint32_t first = (std::uint32_t)vertices_.size();
    auto vtx = [&](float x, float y, float u, float v) {
        vertices_.push_back({ x, y, u, v, tint.r, tint.g, tint.b, tint.a });
    };
    vtx(min.x, min.y, uvMin.x, uvMin.y);
    vtx(max.x, min.y, uvMax.x, uvMin.y);
    vtx(max.x, max.y, uvMax.x, uvMax.y);
    vtx(min.x, min.y, uvMin.x, uvMin.y);
    vtx(max.x, max.y, uvMax.x, uvMax.y);
    vtx(min.x, max.y, uvMin.x, uvMax.y);

    // Coalesce into the last batch when it shares the same texture —
    // consecutive glyphs of one text run (and consecutive draws of the same
    // preview) become ONE draw call instead of one per quad.
    if (!batches_.empty() && batches_.back().sourceSet == sourceSet) {
        batches_.back().count += 6;
    } else {
        batches_.push_back({ first, 6, sourceSet });
    }
}

void NodeUIList::AddAtlasQuad(Vec2 min, Vec2 max, Vec2 uvMin, Vec2 uvMax,
                              const Color& tint) {
    PushQuad(min, max, uvMin, uvMax, tint, 0);
}

void NodeUIList::AddPreviewQuad(Vec2 min, Vec2 max,
                                std::uint64_t sourceDescriptorSet,
                                const Color& tint) {
    if (sourceDescriptorSet == 0) return;   // no target yet — nothing to sample
    PushQuad(min, max, { 0.0f, 0.0f }, { 1.0f, 1.0f }, tint, sourceDescriptorSet);
}

} // namespace Ink

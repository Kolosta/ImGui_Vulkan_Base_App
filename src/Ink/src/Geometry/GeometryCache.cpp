#include "Ink/Geometry/GeometryCache.h"

#include <cmath>

namespace Ink {

namespace {
std::uint64_t FlattenKey(std::uint64_t pathHash, int tier) {
    std::uint64_t h = pathHash;
    h = HashBytes(&tier, sizeof tier, h);
    return h;
}
} // namespace

int GeometryCache::TierFromZoom(double zoom) {
    if (zoom <= 0.0) return 0;
    const int t = (int)std::lround(std::log2(zoom));
    return t < -24 ? -24 : (t > 24 ? 24 : t);
}

double GeometryCache::ToleranceForTier(int tier) {
    // kTolerancePx at the tier's nominal zoom (2^tier px per doc unit).
    return kTolerancePx / std::exp2((double)tier);
}

const std::vector<geom::Polyline>&
GeometryCache::GetFlattened(const PathData& path, std::uint64_t pathHash,
                            int tier) {
    const std::uint64_t key = FlattenKey(pathHash, tier);
    auto it = flattened_.find(key);
    if (it == flattened_.end())
        it = flattened_.emplace(key,
                 geom::Flatten(path, ToleranceForTier(tier))).first;
    return it->second;
}

const geom::Mesh* GeometryCache::GetFill(const PathData& path,
                                         std::uint64_t pathHash, int tier,
                                         FillRule rule, std::uint64_t& keyOut) {
    std::uint64_t key = FlattenKey(pathHash, tier);
    const std::uint8_t tag[2] = { 0xF1, (std::uint8_t)rule };
    key = HashBytes(tag, sizeof tag, key);
    keyOut = key;
    auto it = meshes_.find(key);
    if (it == meshes_.end())
        it = meshes_.emplace(key,
                 geom::TriangulateFill(GetFlattened(path, pathHash, tier),
                                       rule)).first;
    return it->second.Empty() ? nullptr : &it->second;
}

const geom::Mesh* GeometryCache::GetStroke(const PathData& path,
                                           std::uint64_t pathHash, int tier,
                                           const Stroke& stroke,
                                           std::uint64_t& keyOut) {
    std::uint64_t key = FlattenKey(pathHash, tier);
    const std::uint8_t tag = 0x57;
    key = HashBytes(&tag, 1, key);
    const std::uint64_t geo = stroke.GeometryHash();
    key = HashBytes(&geo, sizeof geo, key);
    keyOut = key;
    auto it = meshes_.find(key);
    if (it == meshes_.end())
        it = meshes_.emplace(key,
                 geom::TessellateStroke(GetFlattened(path, pathHash, tier),
                                        stroke)).first;
    return it->second.Empty() ? nullptr : &it->second;
}

const geom::Mesh* GeometryCache::Find(std::uint64_t key) const {
    auto it = meshes_.find(key);
    return it == meshes_.end() ? nullptr : &it->second;
}

} // namespace Ink

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

int GeometryCache::StableTier(int current, double zoom) {
    if (zoom <= 0.0) return current;
    const double t = std::log2(zoom);
    if (t > (double)current + 0.75 || t < (double)current - 0.75)
        return TierFromZoom(zoom);
    return current;
}

double GeometryCache::EffectiveWidth(const Stroke& stroke, int tier) {
    if (stroke.widthSpace == WidthSpace::Viewport)
        return stroke.width / std::exp2((double)tier);   // px → doc at tier
    return stroke.width;
}

const std::vector<geom::Polyline>&
GeometryCache::GetFlattened(const PathData& path, std::uint64_t pathHash,
                            int tier, const geom::BoolProgram* prog) {
    const std::uint64_t key = FlattenKey(pathHash, tier);
    auto it = flattened_.find(key);
    if (it == flattened_.end()) {
        // A boolean program re-evaluates its ops at THIS tier's tolerance so
        // the derived outline stays vector-smooth at any zoom.
        it = flattened_.emplace(key,
                 prog ? geom::EvaluateBoolean(*prog, ToleranceForTier(tier))
                      : geom::Flatten(path, ToleranceForTier(tier))).first;
    }
    return it->second;
}

const geom::Mesh* GeometryCache::GetFill(const PathData& path,
                                         std::uint64_t pathHash, int tier,
                                         FillRule rule, std::uint64_t& keyOut,
                                         const geom::BoolProgram* prog) {
    std::uint64_t key = FlattenKey(pathHash, tier);
    const std::uint8_t tag[2] = { 0xF1, (std::uint8_t)rule };
    key = HashBytes(tag, sizeof tag, key);
    keyOut = key;
    auto it = meshes_.find(key);
    if (it == meshes_.end())
        it = meshes_.emplace(key,
                 geom::TriangulateFill(GetFlattened(path, pathHash, tier, prog),
                                       rule)).first;
    return it->second.Empty() ? nullptr : &it->second;
}

const geom::Mesh* GeometryCache::GetStroke(const PathData& path,
                                           std::uint64_t pathHash, int tier,
                                           const Stroke& stroke,
                                           std::uint64_t& keyOut,
                                           const geom::BoolProgram* prog) {
    std::uint64_t key = FlattenKey(pathHash, tier);
    const std::uint8_t tag = 0x57;
    key = HashBytes(&tag, 1, key);
    const std::uint64_t geo = stroke.GeometryHash();
    key = HashBytes(&geo, sizeof geo, key);
    keyOut = key;
    auto it = meshes_.find(key);
    if (it == meshes_.end()) {
        // WidthSpace resolution + arc tolerance follow the tier (both are in
        // the key via `tier` itself). The source path resolves node-pinned
        // dash anchors — only meaningful on the path's own outline (a boolean
        // program's derived rings no longer map to subpaths).
        Stroke eff = stroke;
        eff.width = EffectiveWidth(stroke, tier);
        it = meshes_.emplace(key,
                 geom::TessellateStroke(GetFlattened(path, pathHash, tier, prog),
                                        eff, ToleranceForTier(tier),
                                        prog ? nullptr : &path)).first;
    }
    return it->second.Empty() ? nullptr : &it->second;
}

const geom::LocalBounds&
GeometryCache::GetLocalBounds(const PathData& path, std::uint64_t pathHash,
                              int tier, const geom::BoolProgram* prog) {
    const std::uint64_t key = FlattenKey(pathHash, tier);
    auto it = bounds_.find(key);
    if (it == bounds_.end())
        it = bounds_.emplace(key,
                 geom::PolylineBounds(GetFlattened(path, pathHash, tier,
                                                   prog))).first;
    return it->second;
}

const geom::Mesh* GeometryCache::Find(std::uint64_t key) const {
    auto it = meshes_.find(key);
    return it == meshes_.end() ? nullptr : &it->second;
}

} // namespace Ink

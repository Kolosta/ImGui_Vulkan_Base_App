#pragma once

#include "Ink/Geometry/Geometry.h"
#include <unordered_map>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  GeometryCache — CPU products keyed by content (docs/Ink/GEOMETRY.md §3):
//
//    flatten key = (pathHash, tier)                     → polylines
//    fill    key = (pathHash, tier, "fill", rule)       → triangle mesh
//    stroke  key = (pathHash, tier, "stroke", geomHash) → triangle mesh
//
//  The strict key discipline: geometry params are IN the key, paints/opacity
//  are NOT (a color edit never re-tessellates). Identical paths share entries
//  whatever node they belong to (the 1 000-shape grid = one mesh).
//
//  v1: no eviction (documents small; LRU-by-bytes lands with the zoom-tier
//  hysteresis in Lot 3). GPU residency lives in GpuScene, keyed by the same
//  product keys.
// ─────────────────────────────────────────────────────────────────────────────
class GeometryCache {
public:
    // Zoom → tier (×2 buckets; hysteresis in Lot 3). Flattening tolerance is
    // ~kTolerancePx at the tier's nominal zoom, converted to doc units.
    static int    TierFromZoom(double zoom);
    static double ToleranceForTier(int tier);
    static constexpr double kTolerancePx = 0.25;

    // Get-or-build. `keyOut` is the stable product key (GPU residency key).
    const geom::Mesh* GetFill(const PathData& path, std::uint64_t pathHash,
                              int tier, FillRule rule, std::uint64_t& keyOut);
    const geom::Mesh* GetStroke(const PathData& path, std::uint64_t pathHash,
                                int tier, const Stroke& stroke,
                                std::uint64_t& keyOut);
    // Lookup only (GpuScene pool re-upload after growth).
    const geom::Mesh* Find(std::uint64_t key) const;

    const std::vector<geom::Polyline>&
    GetFlattened(const PathData& path, std::uint64_t pathHash, int tier);

    std::size_t MeshCount() const { return meshes_.size(); }
    void Clear() { flattened_.clear(); meshes_.clear(); }

private:
    std::unordered_map<std::uint64_t, std::vector<geom::Polyline>> flattened_;
    std::unordered_map<std::uint64_t, geom::Mesh>                  meshes_;
};

} // namespace Ink

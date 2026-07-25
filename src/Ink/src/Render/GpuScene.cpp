#include "Ink/Render/GpuScene.h"

#include <cmath>
#include <cstring>

namespace Ink {

namespace {
constexpr std::uint32_t kInitialVertices = 1u << 16;   // 64k verts (512 KB)
constexpr std::uint32_t kInitialIndices  = 1u << 17;   // 128k indices (512 KB)

std::uint64_t ColorKey(const PaintRecord& p) {
    return HashBytes(p.rgba, sizeof p.rgba);
}
} // namespace

bool GpuScene::Initialize(rhi::Device&) {
    // Buffers are created lazily (first residency / first sync).
    return true;
}

void GpuScene::Shutdown(rhi::Device& dev) {
    rhi::DestroyBuffer(dev, vertexPool_);
    rhi::DestroyBuffer(dev, indexPool_);
    rhi::DestroyBuffer(dev, itemTable_);
    rhi::DestroyBuffer(dev, paintTable_);
    for (rhi::Buffer& s : staging_) rhi::DestroyBuffer(dev, s);
    resident_.clear();
    itemOfDrawable_.clear();
    vtxUsed_ = vtxCap_ = idxUsed_ = idxCap_ = 0;
    pendingData_.clear();
    pendingCopies_.clear();
}

void GpuScene::Queue(VkBuffer dst, VkDeviceSize dstOffset, const void* data,
                     std::size_t size) {
    if (!dst || size == 0) return;
    PendingCopy c;
    c.dst       = dst;
    c.dstOffset = dstOffset;
    c.srcOffset = pendingData_.size();
    c.size      = size;
    pendingData_.insert(pendingData_.end(),
                        static_cast<const std::uint8_t*>(data),
                        static_cast<const std::uint8_t*>(data) + size);
    pendingCopies_.push_back(c);
}

bool GpuScene::GrowPools(rhi::Device& dev, std::uint32_t vtxNeeded,
                         std::uint32_t idxNeeded, const GeometryCache& cache,
                         const DeferFn& defer) {
    std::uint32_t newVtxCap = vtxCap_ ? vtxCap_ : kInitialVertices;
    std::uint32_t newIdxCap = idxCap_ ? idxCap_ : kInitialIndices;
    while (newVtxCap < vtxNeeded) newVtxCap *= 2;
    while (newIdxCap < idxNeeded) newIdxCap *= 2;
    if (newVtxCap == vtxCap_ && newIdxCap == idxCap_ && vertexPool_ && indexPool_)
        return true;

    if (vertexPool_) { rhi::Buffer old = vertexPool_;
        defer([&dev, old]() mutable { rhi::DestroyBuffer(dev, old); }); }
    if (indexPool_)  { rhi::Buffer old = indexPool_;
        defer([&dev, old]() mutable { rhi::DestroyBuffer(dev, old); }); }

    vertexPool_ = rhi::CreateDeviceBuffer(
        dev, (VkDeviceSize)newVtxCap * sizeof(ContentVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    indexPool_ = rhi::CreateDeviceBuffer(
        dev, (VkDeviceSize)newIdxCap * sizeof(std::uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!vertexPool_ || !indexPool_) return false;
    vtxCap_ = newVtxCap;
    idxCap_ = newIdxCap;

    // Fresh buffers: every already-resident mesh re-uploads to its (kept)
    // range — the CPU products live in the cache, ranges stay stable.
    for (const auto& [key, range] : resident_) {
        const geom::Mesh* mesh = cache.Find(key);
        if (!mesh) continue;
        Queue(vertexPool_.buffer,
              (VkDeviceSize)range.vertexOffset * sizeof(ContentVertex),
              mesh->positions.data(),
              mesh->positions.size() * sizeof(float));
        Queue(indexPool_.buffer,
              (VkDeviceSize)range.firstIndex * sizeof(std::uint32_t),
              mesh->indices.data(),
              mesh->indices.size() * sizeof(std::uint32_t));
    }
    return true;
}

MeshRange GpuScene::EnsureResident(rhi::Device& dev, std::uint64_t key,
                                   const geom::Mesh& mesh,
                                   const GeometryCache& cache,
                                   const DeferFn& defer) {
    if (auto it = resident_.find(key); it != resident_.end())
        return it->second;
    if (mesh.Empty()) return {};

    const std::uint32_t vtxCount = mesh.VertexCount();
    const std::uint32_t idxCount = (std::uint32_t)mesh.indices.size();
    if (vtxUsed_ + vtxCount > vtxCap_ || idxUsed_ + idxCount > idxCap_)
        if (!GrowPools(dev, vtxUsed_ + vtxCount, idxUsed_ + idxCount, cache, defer))
            return {};

    MeshRange range;
    range.vertexOffset = (std::int32_t)vtxUsed_;
    range.firstIndex   = idxUsed_;
    range.indexCount   = idxCount;
    vtxUsed_ += vtxCount;
    idxUsed_ += idxCount;

    Queue(vertexPool_.buffer,
          (VkDeviceSize)range.vertexOffset * sizeof(ContentVertex),
          mesh.positions.data(), mesh.positions.size() * sizeof(float));
    Queue(indexPool_.buffer,
          (VkDeviceSize)range.firstIndex * sizeof(std::uint32_t),
          mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));

    resident_.emplace(key, range);
    return range;
}

bool GpuScene::EnsureTable(rhi::Device& dev, rhi::Buffer& buf,
                           VkDeviceSize bytes, const DeferFn& defer,
                           bool& recreated) {
    if (buf && buf.size >= bytes) return true;
    VkDeviceSize cap = buf ? buf.size : 4096;
    while (cap < bytes) cap *= 2;
    if (buf) {
        rhi::Buffer old = buf;
        defer([&dev, old]() mutable { rhi::DestroyBuffer(dev, old); });
    }
    buf = rhi::CreateDeviceBuffer(dev, cap, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    recreated = true;
    return (bool)buf;
}

// The colour a drawable shows under one print configuration. Off leaves it
// exactly as compiled; the proofing modes rebuild it from the ink its plate
// actually lays, keeping the object's own alpha. A SPOT ink is not built from
// the process set — it is laid as its own measured colour, and so belongs to no
// CMYK channel at all, which is why it drops out of a separations view.
static Color PrintedColor(const Drawable& d,
                          const GpuScene::PrintConfig& cfg) {
    if (cfg.mode != PrintPreview::Overprint &&
        cfg.mode != PrintPreview::Separations)
        return d.color;
    const bool sep = cfg.mode == PrintPreview::Separations;
    const std::uint8_t ch = sep ? cfg.channels : PrintChannelAll;
    Color out = d.color;
    if (d.plate != kNoPlate && d.hasSpot) {
        if (sep) { out.a = 0.0f; return out; }
        const float a = out.a;
        out = d.spotColor;
        out.a = a;
    } else if (d.plate != kNoPlate) {
        const float a = out.a;
        out = InkOverPaper(d.plateInk, ch);
        out.a = a;
    } else if (sep) {
        const float a = out.a;
        out = InkOverPaper(NaiveCmyk(out), ch);
        out.a = a;
    }
    return out;
}

bool GpuScene::SyncStyleTables(rhi::Device& dev,
                               const std::vector<Drawable>& drawables,
                               const std::vector<PrintConfig>& configs,
                               const DeferFn& defer) {
    // Painter order; paints and items dedup. The drawable → item map feeds
    // the per-view instance builds (instance index == drawable index).
    //
    // ONE BLOCK OF ITEMS PER CONFIGURATION: the geometry, the instances and the
    // draw commands are shared by every view, and the only thing a proofing
    // view needs to differ in is the colour it resolves to. Giving each
    // configuration its own item block and letting a view offset its item
    // indices into it is what makes the print previews per-viewport without
    // duplicating anything else.
    std::vector<ItemRecord>  items;
    std::vector<PaintRecord> paints;
    std::unordered_map<std::uint64_t, std::uint32_t> paintIndex;
    itemOfDrawable_.clear();
    itemOfDrawable_.reserve(drawables.size());
    itemsPerBlock_ = 0;

    // Items are deduped on the drawable's whole PRINT IDENTITY, not just its
    // colour: two drawables that look alike today may sit on different plates
    // and diverge the moment a view proofs them. Keying on the identity keeps
    // every block the same length and in the same order, which is the whole
    // reason a view can reach its block by adding one stride.
    auto identity = [](const Drawable& d) {
        auto mix = [](std::uint64_t h, std::uint64_t v) {
            h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        };
        auto f = [](float v) {
            std::uint32_t b; std::memcpy(&b, &v, 4); return (std::uint64_t)b;
        };
        auto q = [](double v) { return (std::uint64_t)(std::int64_t)std::llround(v * 100.0); };
        std::uint64_t h = 0x9E3779B9ull;
        h = mix(h, f(d.color.r)); h = mix(h, f(d.color.g));
        h = mix(h, f(d.color.b)); h = mix(h, f(d.color.a));
        h = mix(h, (std::uint64_t)(std::int64_t)d.plate);
        if (d.plate != kNoPlate) {
            h = mix(h, q(d.plateInk.c)); h = mix(h, q(d.plateInk.m));
            h = mix(h, q(d.plateInk.y)); h = mix(h, q(d.plateInk.k));
            h = mix(h, d.hasSpot ? 1ull : 0ull);
            if (d.hasSpot) {
                h = mix(h, f(d.spotColor.r)); h = mix(h, f(d.spotColor.g));
                h = mix(h, f(d.spotColor.b));
            }
        }
        return h;
    };
    std::unordered_map<std::uint64_t, std::uint32_t> slotOf;   // identity → slot
    std::vector<std::uint32_t> slotRep;                        // slot → drawable
    for (std::uint32_t i = 0; i < (std::uint32_t)drawables.size(); ++i) {
        const std::uint64_t key = identity(drawables[i]);
        auto it = slotOf.find(key);
        if (it == slotOf.end()) {
            it = slotOf.emplace(key, (std::uint32_t)slotRep.size()).first;
            slotRep.push_back(i);
        }
        itemOfDrawable_.push_back(it->second);
    }
    itemsPerBlock_ = (std::uint32_t)slotRep.size();

    const std::size_t nCfg = configs.empty() ? 1 : configs.size();
    items.reserve(slotRep.size() * nCfg);
    for (std::size_t b = 0; b < nCfg; ++b) {
        const PrintConfig cfg = configs.empty() ? PrintConfig{} : configs[b];
        for (std::uint32_t rep : slotRep) {
            PaintRecord p;
            const Color c = PrintedColor(drawables[rep], cfg).Premultiplied();
            p.rgba[0] = c.r; p.rgba[1] = c.g; p.rgba[2] = c.b; p.rgba[3] = c.a;
            const std::uint64_t ck = ColorKey(p);
            auto pit = paintIndex.find(ck);
            if (pit == paintIndex.end()) {
                pit = paintIndex.emplace(ck, (std::uint32_t)paints.size()).first;
                paints.push_back(p);
            }
            items.push_back({ pit->second, 0, { 0, 0 } });
        }
    }
    if (items.empty()) return false;

    bool recreated = false;
    if (!EnsureTable(dev, itemTable_,
                     items.size() * sizeof(ItemRecord), defer, recreated) ||
        !EnsureTable(dev, paintTable_,
                     paints.size() * sizeof(PaintRecord), defer, recreated))
        return recreated;

    Queue(itemTable_.buffer, 0, items.data(), items.size() * sizeof(ItemRecord));
    Queue(paintTable_.buffer, 0, paints.data(),
          paints.size() * sizeof(PaintRecord));
    return recreated;
}

bool GpuScene::SyncViewInstances(rhi::Device& dev,
                                 const std::vector<Drawable>& drawables,
                                 DVec2 anchor, std::uint32_t printBlock,
                                 rhi::Buffer& buf, const DeferFn& defer) {
    if (drawables.empty() || itemOfDrawable_.size() != drawables.size())
        return false;
    std::vector<InstanceRecord> instances;
    instances.reserve(drawables.size());
    for (std::size_t i = 0; i < drawables.size(); ++i) {
        const Drawable& d = drawables[i];
        InstanceRecord rec{};
        // Rotation/scale narrow directly; translations subtract the view
        // anchor IN DOUBLE first — this is what holds precision at any zoom
        // (docs/Ink/GEOMETRY.md §6).
        rec.m[0] = (float)d.world.m[0];
        rec.m[1] = (float)d.world.m[1];
        rec.m[2] = (float)(d.world.m[2] - anchor.x);
        rec.m[3] = (float)d.world.m[3];
        rec.m[4] = (float)d.world.m[4];
        rec.m[5] = (float)(d.world.m[5] - anchor.y);
        rec.itemIndex = itemOfDrawable_[i] + printBlock * itemsPerBlock_;
        instances.push_back(rec);
    }
    bool recreated = false;
    if (!EnsureTable(dev, buf, instances.size() * sizeof(InstanceRecord),
                     defer, recreated))
        return recreated;
    Queue(buf.buffer, 0, instances.data(),
          instances.size() * sizeof(InstanceRecord));
    return recreated;
}

std::size_t GpuScene::FlushUploads(rhi::Device& dev, VkCommandBuffer cmd,
                                   std::uint32_t slot, const DeferFn& defer) {
    if (pendingCopies_.empty()) return 0;
    const std::size_t bytes = pendingData_.size();

    rhi::Buffer& staging = staging_[slot % kStagingRing];
    if (!staging || staging.size < bytes) {
        if (staging) {
            rhi::Buffer old = staging;
            defer([&dev, old]() mutable { rhi::DestroyBuffer(dev, old); });
        }
        VkDeviceSize cap = staging ? staging.size : 1u << 16;
        while (cap < bytes) cap *= 2;
        staging = rhi::CreateHostBuffer(dev, cap, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (!staging) { pendingData_.clear(); pendingCopies_.clear(); return 0; }
    }
    std::memcpy(staging.mapped, pendingData_.data(), bytes);

    // Order the copies against BOTH sides on this single queue: the previous
    // frame may still read the destinations; this frame's passes read the
    // fresh data (docs/Ink/RENDER_GRAPH.md §5).
    auto memoryBarrier = [&](VkPipelineStageFlags2 srcStage,
                             VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage,
                             VkAccessFlags2 dstAccess) {
        VkMemoryBarrier2 mb{};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mb.srcStageMask  = srcStage;
        mb.srcAccessMask = srcAccess;
        mb.dstStageMask  = dstStage;
        mb.dstAccessMask = dstAccess;
        VkDependencyInfo dep{};
        dep.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers    = &mb;
        vkCmdPipelineBarrier2(cmd, &dep);
    };
    const VkPipelineStageFlags2 kConsumers =
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    memoryBarrier(kConsumers, VK_ACCESS_2_MEMORY_READ_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    for (const PendingCopy& c : pendingCopies_) {
        VkBufferCopy region{ (VkDeviceSize)c.srcOffset, c.dstOffset,
                             (VkDeviceSize)c.size };
        vkCmdCopyBuffer(cmd, staging.buffer, c.dst, 1, &region);
    }

    memoryBarrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  kConsumers,
                  VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                  VK_ACCESS_2_INDEX_READ_BIT |
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

    pendingData_.clear();
    pendingCopies_.clear();
    return bytes;
}

} // namespace Ink

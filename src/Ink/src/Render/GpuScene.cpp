#include "Ink/Render/GpuScene.h"

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

bool GpuScene::SyncStyleTables(rhi::Device& dev,
                               const std::vector<Drawable>& drawables,
                               const DeferFn& defer) {
    // Painter order; paints and items dedup. The drawable → item map feeds
    // the per-view instance builds (instance index == drawable index).
    std::vector<ItemRecord>  items;
    std::vector<PaintRecord> paints;
    std::unordered_map<std::uint64_t, std::uint32_t> paintIndex;
    std::unordered_map<std::uint32_t, std::uint32_t> itemIndex;
    itemOfDrawable_.clear();
    itemOfDrawable_.reserve(drawables.size());

    for (const Drawable& d : drawables) {
        PaintRecord p;
        const Color c = d.color.Premultiplied();
        p.rgba[0] = c.r; p.rgba[1] = c.g; p.rgba[2] = c.b; p.rgba[3] = c.a;
        const std::uint64_t ck = ColorKey(p);
        auto pit = paintIndex.find(ck);
        if (pit == paintIndex.end()) {
            pit = paintIndex.emplace(ck, (std::uint32_t)paints.size()).first;
            paints.push_back(p);
        }
        auto iit = itemIndex.find(pit->second);
        if (iit == itemIndex.end()) {
            iit = itemIndex.emplace(pit->second,
                                    (std::uint32_t)items.size()).first;
            items.push_back({ pit->second, 0, { 0, 0 } });
        }
        itemOfDrawable_.push_back(iit->second);
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
                                 DVec2 anchor, rhi::Buffer& buf,
                                 const DeferFn& defer) {
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
        rec.itemIndex = itemOfDrawable_[i];
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

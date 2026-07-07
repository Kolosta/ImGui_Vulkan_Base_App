#include "Ink/Render/GpuScene.h"

namespace Ink {

bool GpuScene::Initialize(rhi::Device&) {
    // Buffers are created lazily by UploadStatic (their sizes depend on the
    // content). Incremental pool growth arrives with the Scene lots.
    return true;
}

void GpuScene::Shutdown(rhi::Device& dev) {
    rhi::DestroyBuffer(dev, vertexPool_);
    rhi::DestroyBuffer(dev, indexPool_);
    rhi::DestroyBuffer(dev, instanceTable_);
    rhi::DestroyBuffer(dev, itemTable_);
    rhi::DestroyBuffer(dev, paintTable_);
    rhi::DestroyBuffer(dev, indirect_);
}

bool GpuScene::UploadStatic(rhi::Device& dev,
                            const std::vector<ContentVertex>&  vertices,
                            const std::vector<std::uint32_t>&  indices,
                            const std::vector<InstanceRecord>& instances,
                            const std::vector<ItemRecord>&     items,
                            const std::vector<PaintRecord>&    paints,
                            const std::vector<Batch>&          batches,
                            Rect bounds) {
    if (vertices.empty() || indices.empty() || instances.empty() ||
        items.empty() || paints.empty() || batches.empty())
        return false;

    // (Re)create device-local buffers sized to the content.
    Shutdown(dev);
    auto make = [&](rhi::Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage,
                    const void* data) {
        b = rhi::CreateDeviceBuffer(dev, size, usage);
        return b && rhi::UploadToBuffer(dev, b, data, size);
    };

    // The indirect commands: one per batch, prebuilt (static demo scene).
    std::vector<VkDrawIndexedIndirectCommand> cmds;
    cmds.reserve(batches.size());
    triangleCount_ = 0;
    instanceCount_ = 0;
    for (const Batch& b : batches) {
        VkDrawIndexedIndirectCommand c{};
        c.indexCount    = b.mesh.indexCount;
        c.instanceCount = b.instanceCount;
        c.firstIndex    = b.mesh.firstIndex;
        c.vertexOffset  = b.mesh.vertexOffset;
        c.firstInstance = b.firstInstance;   // gl_InstanceIndex base (Vulkan)
        cmds.push_back(c);
        triangleCount_ += (b.mesh.indexCount / 3) * b.instanceCount;
        instanceCount_ += b.instanceCount;
    }

    const bool ok =
        make(vertexPool_, vertices.size() * sizeof(ContentVertex),
             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data()) &&
        make(indexPool_, indices.size() * sizeof(std::uint32_t),
             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data()) &&
        make(instanceTable_, instances.size() * sizeof(InstanceRecord),
             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, instances.data()) &&
        make(itemTable_, items.size() * sizeof(ItemRecord),
             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, items.data()) &&
        make(paintTable_, paints.size() * sizeof(PaintRecord),
             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, paints.data()) &&
        make(indirect_, cmds.size() * sizeof(VkDrawIndexedIndirectCommand),
             VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, cmds.data());
    if (!ok) { Shutdown(dev); return false; }

    batchCount_ = (std::uint32_t)batches.size();
    bounds_     = bounds;
    ++version_;
    return true;
}

} // namespace Ink

#include "Render/RendererInternal.h"

namespace Ink::detail {

// Node UI pass (docs/Ink/NODE_UI.md) — textured quads for a view with no
// document content: glyph text (sampling the shared font atlas) and live
// preview vignettes (each sampling another view's own rendered image).
// Batches are emission-ordered, contiguous runs sharing one texture
// (NodeUIList coalesces them); a batch with `sourceSet == 0` binds the font
// atlas, any other value is an opaque VkDescriptorSet from
// View::PreviewDescriptorSet (docs/Ink/NODE_UI.md — laundered through a
// uint64_t so NodeUIList.h stays RHI-type-free, same rule as OverlayList.h).
void RecordNodeUIPass(RendererImpl& r, VkCommandBuffer cmd,
                      const PushCamera& pxToNdc, VkBuffer vertexBuffer,
                      const NodeUIList::Batch* batches, std::uint32_t batchCount) {
    if (batchCount == 0 || vertexBuffer == VK_NULL_HANDLE) return;
    if (r.nodeUIPipeline == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.nodeUIPipeline);
    vkCmdPushConstants(cmd, r.nodeUILayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                      sizeof(PushCamera), &pxToNdc);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &zero);

    VkDescriptorSet bound = VK_NULL_HANDLE;
    for (std::uint32_t i = 0; i < batchCount; ++i) {
        const NodeUIList::Batch& b = batches[i];
        if (b.count == 0) continue;
        const VkDescriptorSet set = b.sourceSet == 0
            ? r.fontAtlas.descriptorSet
            : (VkDescriptorSet)(void*)(std::uintptr_t)b.sourceSet;
        if (set == VK_NULL_HANDLE) continue;   // font atlas failed to load / stale preview
        if (set != bound) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    r.nodeUILayout, 0, 1, &set, 0, nullptr);
            bound = set;
        }
        vkCmdDraw(cmd, b.count, 1, b.first, 0);
    }
}

} // namespace Ink::detail

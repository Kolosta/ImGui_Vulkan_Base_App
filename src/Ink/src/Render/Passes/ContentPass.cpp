#include "Render/RendererInternal.h"

namespace Ink::detail {

// P1 Content (docs/Ink/RENDER_GRAPH.md §4): draw the view's command list with
// ONE multi-draw indirect call — the vertex shader chases instance → item →
// paint, so 1 object or 10 000 instances is the same path. Painter's order =
// command order (built per view: mesh ranges follow the view's zoom tier).
// Records inside the MSAA rendering scope opened by the graph.
void RecordContentPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& worldToNdc, VkBuffer indirect,
                       std::uint32_t commandCount) {
    const GpuScene& s = r.gpu;
    if (commandCount == 0 || indirect == VK_NULL_HANDLE ||
        r.sceneSet == VK_NULL_HANDLE || !s.TablesReady())
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.contentPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.contentLayout, 0, 1, &r.sceneSet, 0, nullptr);
    vkCmdPushConstants(cmd, r.contentLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushCamera), &worldToNdc);

    const VkDeviceSize zero = 0;
    VkBuffer vb = s.VertexPool().buffer;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cmd, s.IndexPool().buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect(cmd, indirect, 0, commandCount,
                             sizeof(VkDrawIndexedIndirectCommand));
}

} // namespace Ink::detail

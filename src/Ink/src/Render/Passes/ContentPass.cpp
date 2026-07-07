#include "Render/RendererInternal.h"

namespace Ink::detail {

// P1 Content (docs/Ink/RENDER_GRAPH.md §4): draw every batch of the GpuScene
// with ONE multi-draw indirect call — the vertex shader chases
// instance → item → paint, so 1 object or 10 000 instances is the same path.
// Painter's order = indirect command order. Records inside the MSAA rendering
// scope opened by the graph (clear = the view background).
void RecordContentPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& worldToNdc) {
    const GpuScene& s = r.scene;
    if (s.BatchCount() == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.contentPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.contentLayout, 0, 1, &r.sceneSet, 0, nullptr);
    vkCmdPushConstants(cmd, r.contentLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushCamera), &worldToNdc);

    const VkDeviceSize zero = 0;
    VkBuffer vb = s.VertexPool().buffer;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cmd, s.IndexPool().buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect(cmd, s.IndirectBuffer().buffer, 0, s.BatchCount(),
                             sizeof(VkDrawIndexedIndirectCommand));
}

} // namespace Ink::detail

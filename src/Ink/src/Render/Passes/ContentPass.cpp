#include "Render/RendererInternal.h"

namespace Ink::detail {

// P1 Content (docs/Ink/RENDER_GRAPH.md §4): draw the run's command list in
// SEGMENTS — one multi-draw indirect per contiguous slice sharing a stencil
// role. Plain content ignores the stencil; a MaskWrite slice rasterises the
// clip mask (stencil ← 1, no colour), MaskClear erases it (stencil ← 0, so
// sequential clipped regions in one pass never leak), Clipped draws colour
// only where the mask is set. Painter's order = segment/command order. The
// vertex shader chases instance → item → paint, so 1 object or 10 000
// instances is the same path. Records inside the MSAA rendering scope opened
// by the graph (which binds the stencil attachment).
void RecordContentPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& worldToNdc, VkBuffer indirect,
                       const CmdSegment* segments, std::uint32_t segmentCount,
                       VkDescriptorSet sceneSet) {
    const GpuScene& s = r.gpu;
    if (segmentCount == 0 || indirect == VK_NULL_HANDLE ||
        sceneSet == VK_NULL_HANDLE || !s.StyleTablesReady())
        return;

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.contentLayout, 0, 1, &sceneSet, 0, nullptr);
    vkCmdPushConstants(cmd, r.contentLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushCamera), &worldToNdc);

    const VkDeviceSize zero = 0;
    VkBuffer vb = s.VertexPool().buffer;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
    vkCmdBindIndexBuffer(cmd, s.IndexPool().buffer, 0, VK_INDEX_TYPE_UINT32);

    VkPipeline bound = VK_NULL_HANDLE;
    for (std::uint32_t i = 0; i < segmentCount; ++i) {
        const CmdSegment& seg = segments[i];
        if (seg.cmdCount == 0) continue;
        VkPipeline pipe = r.contentPipeline;
        switch (seg.role) {
            case ClipRole::MaskWrite: pipe = r.clipMaskPipeline; break;
            case ClipRole::MaskClear: pipe = r.clipClearPipeline; break;
            case ClipRole::Clipped:   pipe = r.contentClipPipeline; break;
            case ClipRole::None: default: break;
        }
        if (pipe != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            bound = pipe;
        }
        vkCmdDrawIndexedIndirect(
            cmd, indirect,
            seg.cmdOffset * (std::uint32_t)sizeof(VkDrawIndexedIndirectCommand),
            seg.cmdCount, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace Ink::detail

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
            case ClipRole::MaskWrite:  pipe = r.clipMaskPipeline; break;
            case ClipRole::MaskClear:  pipe = r.clipClearPipeline; break;
            case ClipRole::Clipped:    pipe = r.contentClipPipeline; break;
            case ClipRole::EraseWrite: pipe = r.contentErasePipeline; break;
            case ClipRole::EraseClipped: pipe = r.contentEraseClipPipeline; break;
            case ClipRole::None: default: break;
        }
        // Translucent-stroke self-overlap dedup: the segment is one stroke,
        // drawn where the stencil differs from its tag and tagging as it goes
        // — its own overlapping triangles blend exactly once. The reference is
        // dynamic on this pipeline (one pipeline, per-draw tags).
        if (seg.stencilTag != 0) pipe = r.strokeDedupPipeline;
        if (pipe != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            bound = pipe;
        }
        // The stencil reference is DYNAMIC state on every pipeline, so a
        // pipeline's own `stencilRef` is never used — it has to be set here, for
        // EVERY segment. Setting it only for the dedup segments left the mask
        // roles running on whatever value happened to be current: a mask could
        // be written under one reference and tested under another, which is how
        // an erase ended up cutting the whole shape instead of its pattern.
        //   write / test the mask → 1,   clear it → 0,   a dedup run → its tag.
        std::uint32_t ref = seg.role == ClipRole::MaskClear ? 0u : 1u;
        if (seg.stencilTag != 0) ref = seg.stencilTag;
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdDrawIndexedIndirect(
            cmd, indirect,
            seg.cmdOffset * (std::uint32_t)sizeof(VkDrawIndexedIndirectCommand),
            seg.cmdCount, sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace Ink::detail

#include "Render/RendererInternal.h"

namespace Ink::detail {

// P5 Overlay (docs/Ink/RENDER_GRAPH.md §4): the editor's per-frame primitives
// (selection outlines, handles, guides, crosshair…), pre-triangulated by
// OverlayList in view pixels, drawn over the content INSIDE the MSAA target so
// they get the same anti-aliasing. No text — labels/rulers stay ImGui outside
// the canvas (locked rule).
void RecordOverlayPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& pxToNdc, VkBuffer vertexBuffer,
                       std::uint32_t vertexCount) {
    if (vertexCount == 0 || vertexBuffer == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.overlayPipeline);
    vkCmdPushConstants(cmd, r.overlayLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushCamera), &pxToNdc);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &zero);
    vkCmdDraw(cmd, vertexCount, 1, 0, 0);
}

} // namespace Ink::detail

#include "Render/RendererInternal.h"

namespace Ink::detail {

// P5 Overlay (docs/Ink/RENDER_GRAPH.md §4): the editor's per-frame primitives
// (selection outlines, handles, guides, crosshair…), pre-triangulated by
// OverlayList in view pixels, drawn over the content INSIDE the MSAA target so
// they get the same anti-aliasing. No text — labels/rulers stay ImGui outside
// the canvas (locked rule).
//
// DEDUP groups (translucent mesh previews): each group's vertex range plays
// through the overlay stencil-dedup pipeline (NOT_EQUAL + replace, one dynamic
// tag per group continuing `baseTag`) so a tessellated stroke preview's
// self-overlapping triangles blend exactly once — the same technique as the
// content pass's translucent-stroke dedup. Everything is drawn in emission
// order, so groups still layer correctly with the plain primitives.
void RecordOverlayPass(RendererImpl& r, VkCommandBuffer cmd,
                       const PushCamera& pxToNdc, VkBuffer vertexBuffer,
                       std::uint32_t vertexCount,
                       const OverlayList::DedupGroup* dedups,
                       std::uint32_t dedupCount, std::uint32_t baseTag) {
    if (vertexCount == 0 || vertexBuffer == VK_NULL_HANDLE) return;

    vkCmdPushConstants(cmd, r.overlayLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushCamera), &pxToNdc);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &zero);

    VkPipeline bound = VK_NULL_HANDLE;
    auto draw = [&](std::uint32_t first, std::uint32_t count, VkPipeline pipe) {
        if (count == 0) return;
        if (pipe != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            bound = pipe;
        }
        vkCmdDraw(cmd, count, 1, first, 0);
    };

    std::uint32_t at = 0;
    std::uint32_t tag = baseTag < 2 ? 2 : baseTag;
    for (std::uint32_t i = 0; i < dedupCount; ++i) {
        const OverlayList::DedupGroup& g = dedups[i];
        if (g.first > at) draw(at, g.first - at, r.overlayPipeline);
        if (r.overlayDedupPipeline != VK_NULL_HANDLE) {
            if (r.overlayDedupPipeline != bound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  r.overlayDedupPipeline);
                bound = r.overlayDedupPipeline;
            }
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, tag);
            tag = tag >= 255 ? 2 : tag + 1;
            vkCmdDraw(cmd, g.count, 1, g.first, 0);
        } else {
            draw(g.first, g.count, r.overlayPipeline);   // no-stencil fallback
        }
        at = g.first + g.count;
    }
    if (at < vertexCount) draw(at, vertexCount - at, r.overlayPipeline);
}

} // namespace Ink::detail

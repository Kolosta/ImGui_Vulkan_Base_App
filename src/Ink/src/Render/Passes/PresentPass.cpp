#include "Render/RendererInternal.h"

namespace Ink::detail {

// P8 Present (docs/Ink/RENDER_GRAPH.md §4): fullscreen triangle sampling the
// resolved linear-premultiplied canvas and writing the sRGB-encoded display
// image the UI samples (encode-at-present rule, docs/Ink/ARCHITECTURE.md §6).
void RecordPresentPass(RendererImpl& r, VkCommandBuffer cmd,
                       VkDescriptorSet presentSet) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.presentPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.presentLayout, 0, 1, &presentSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace Ink::detail

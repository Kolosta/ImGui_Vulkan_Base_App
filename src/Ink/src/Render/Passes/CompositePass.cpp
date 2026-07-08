#include "Render/RendererInternal.h"

namespace Ink::detail {

// Composite pass (docs/Ink/RENDER_GRAPH.md §CompositePass): a fullscreen draw
// that samples a scope's isolation target (source) and its parent (backdrop)
// and writes the parent with the scope's opacity + blend mode applied
// (iso.frag). The parent attachment is the destination; the backdrop is read
// as a SAMPLED copy (the resolved parent linear), so there is no attachment
// feedback loop.
void RecordCompositePass(RendererImpl& r, VkCommandBuffer cmd,
                         VkDescriptorSet compositeSet, const PushComposite& pc) {
    if (compositeSet == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r.compositeLayout, 0, 1, &compositeSet, 0, nullptr);
    vkCmdPushConstants(cmd, r.compositeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushComposite), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace Ink::detail

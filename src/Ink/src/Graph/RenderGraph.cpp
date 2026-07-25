#include "Ink/Graph/RenderGraph.h"

namespace Ink::graph {

void TransitionImage(VkCommandBuffer cmd, rhi::Image& img,
                     VkImageLayout layout, VkPipelineStageFlags2 stage,
                     VkAccessFlags2 access) {
    VkImageMemoryBarrier2 b{};
    b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask  = img.stage == VK_PIPELINE_STAGE_2_NONE
                          ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : img.stage;
    b.srcAccessMask = img.access;
    b.dstStageMask  = stage;
    b.dstAccessMask = access;
    b.oldLayout     = img.layout;
    b.newLayout     = layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image             = img.image;
    b.subresourceRange  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &b;
    vkCmdPipelineBarrier2(cmd, &dep);

    img.layout = layout;
    img.stage  = stage;
    img.access = access;
}

void RenderGraph::AddRenderPass(const char* name, const ColorTarget& target,
                                std::vector<rhi::Image*> sampled,
                                RecordFn record) {
    passes_.push_back({ name, target, std::move(sampled), std::move(record) });
}

void RenderGraph::ExportSampled(rhi::Image* image) {
    exports_.push_back(image);
}

void RenderGraph::Execute(VkCommandBuffer cmd) {
    for (Pass& p : passes_) {
        // Inputs first: everything the pass samples becomes SHADER_READ_ONLY.
        for (rhi::Image* s : p.sampled)
            TransitionImage(cmd, *s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        // Attachment (and resolve destination) to COLOR_ATTACHMENT. Always
        // emitted even when the layout already matches — the execution
        // dependency orders this frame's writes after last frame's.
        rhi::Image& color = *p.target.image;
        TransitionImage(cmd, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
        if (p.target.resolveTo)
            TransitionImage(cmd, *p.target.resolveTo,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo att{};
        att.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        att.imageView   = color.view;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp      = p.target.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                         : VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        for (int i = 0; i < 4; ++i)
            att.clearValue.color.float32[i] = p.target.clearColor[i];
        if (p.target.resolveTo) {
            att.resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
            att.resolveImageView   = p.target.resolveTo->view;
            att.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        // Optional stencil attachment (clip masks). It shares the color's
        // sample count and extent; the depth/stencil layout is used.
        VkRenderingAttachmentInfo stencilAtt{};
        if (p.target.stencil) {
            TransitionImage(cmd, *p.target.stencil,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
            stencilAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            stencilAtt.imageView   = p.target.stencil->view;
            stencilAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAtt.loadOp      = p.target.clearStencil
                                         ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                         : VK_ATTACHMENT_LOAD_OP_LOAD;
            stencilAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAtt.clearValue.depthStencil.stencil = 0;
        }

        VkRenderingInfo ri{};
        ri.sType      = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea = { { 0, 0 }, { color.width, color.height } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &att;
        if (p.target.stencil) ri.pStencilAttachment = &stencilAtt;

        vkCmdBeginRendering(cmd, &ri);
        // Dynamic viewport/scissor cover the whole target for every pipeline.
        VkViewport vp{ 0.0f, 0.0f, (float)color.width, (float)color.height,
                       0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { color.width, color.height } };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        p.record(cmd);
        vkCmdEndRendering(cmd);
    }

    for (rhi::Image* e : exports_)
        TransitionImage(cmd, *e, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    passes_.clear();
    exports_.clear();
}

} // namespace Ink::graph

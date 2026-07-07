#pragma once

#include "Ink/RHI/Resources.h"
#include <functional>
#include <vector>

namespace Ink::graph {

// ─────────────────────────────────────────────────────────────────────────────
//  RenderGraph — the minimal-but-real frame graph of docs/Ink/RENDER_GRAPH.md.
//
//  Passes declare what they render to (one color attachment, optional MSAA
//  resolve) and what they sample; Execute() walks the list in order, derives
//  the sync2 image barriers from each image's tracked state (rhi::Image
//  layout/stage/access), begins/ends dynamic rendering around each pass and
//  invokes its record callback. ExportSampled() transitions an image for
//  sampling by work submitted AFTER the graph (the app's UI pass reading the
//  view texture) — on a single queue, submission-order barriers make that
//  ordering sufficient without semaphores.
//
//  Deliberately small: one queue, one color attachment per pass, no aliasing,
//  no reordering. It is a frame structure, not a scheduler; it grows with the
//  lots (stencil/clip attachments, transient pooling) behind this same API.
// ─────────────────────────────────────────────────────────────────────────────
class RenderGraph {
public:
    struct ColorTarget {
        rhi::Image* image = nullptr;   // rendered-to attachment (may be MSAA)
        bool  clear = false;
        float clearColor[4] = { 0, 0, 0, 0 };
        rhi::Image* resolveTo = nullptr;   // optional MSAA resolve destination
    };

    using RecordFn = std::function<void(VkCommandBuffer)>;

    // Append a render pass. `sampled` lists the images the pass reads in the
    // fragment stage (they are transitioned to SHADER_READ_ONLY before it).
    void AddRenderPass(const char* name, const ColorTarget& target,
                       std::vector<rhi::Image*> sampled, RecordFn record);

    // Transition `image` for sampling after the graph (final consumer barrier).
    void ExportSampled(rhi::Image* image);

    // Emit every barrier + pass into `cmd`, in declaration order, then clear
    // the graph for reuse.
    void Execute(VkCommandBuffer cmd);

    bool Empty() const { return passes_.empty() && exports_.empty(); }

private:
    struct Pass {
        const char*              name;
        ColorTarget              target;
        std::vector<rhi::Image*> sampled;
        RecordFn                 record;
    };
    std::vector<Pass>        passes_;
    std::vector<rhi::Image*> exports_;
};

// Record a sync2 image barrier moving `img` to (layout, stage, access) from
// its tracked current state, and update that state. Emits an execution-only
// barrier when the layout already matches (write-after-write ordering, e.g.
// re-clearing last frame's MSAA target). Shared with pass internals.
void TransitionImage(VkCommandBuffer cmd, rhi::Image& img,
                     VkImageLayout layout, VkPipelineStageFlags2 stage,
                     VkAccessFlags2 access);

} // namespace Ink::graph

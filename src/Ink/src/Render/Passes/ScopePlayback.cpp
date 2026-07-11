#include "Render/RendererInternal.h"

#include <algorithm>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  Scope playback (docs/Ink/RENDER_GRAPH.md §CompositePass) — the compositing
//  core of Lot 4.
//
//  A composite scope (a node with opacity<1, a non-Normal blend, isolate, or
//  a clip) renders its content into its own isolation target, then composites
//  that target onto its parent as a unit. BuildScopePlan assigns each scope an
//  iso LEVEL by nesting depth (capped) and interleaves two phases: CONTENT in
//  PRE-ORDER (a scope's own pass clears + resolves its target, so it must run
//  before any child composites into it) and COMPOSITE in POST-ORDER (a child
//  blends onto its parent right after its subtree completed). PlayScopes
//  records them: content into iso[level] (MSAA→resolved), then a fullscreen
//  composite of iso[level].linear onto the parent with the scope's opacity +
//  blend — reading the parent's current linear as the backdrop and writing the
//  parent's OTHER linear (ping-pong; a composite cannot read and write one
//  image). The root content + overlay live in iso[0]; present samples iso[0].
//
//  Clip source geometry is emitted by the Scene as stencil-only drawables; the
//  stencil mask activation is wired through the RHI (Pipeline stencil modes,
//  graph stencil attachment) and completes in a follow-up (see ROADMAP Lot 4
//  note) — clip scopes currently isolate without masking.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::detail {

std::uint32_t BuildScopePlan(const Scene& scene, ViewImpl& v) {
    v.scopeRuns.clear();
    const auto& scopes = scene.Scopes();

    // Level of each scope = its nesting depth, clamped so it never exceeds the
    // reserved iso targets. Scope 0 (root) is level 0.
    const std::uint32_t maxLevel = v.isoLevels;   // reserved above iso[0]
    std::vector<std::uint32_t> level(scopes.size(), 0);
    std::uint32_t used = 0;
    for (std::size_t i = 1; i < scopes.size(); ++i) {
        std::uint32_t lv = (std::uint32_t)scopes[i].depth;
        if (lv > maxLevel) lv = maxLevel;   // deep nesting shares the top level
        level[i] = lv;
        used = std::max(used, lv);
    }

    // Interleaved plan: a scope's CONTENT must render into iso[level] BEFORE
    // its children (its content pass clears + resolves the target — running
    // it after a child's composite would erase that composite), and each
    // child COMPOSITES onto the parent right after its own subtree finished.
    // So: content pre-order, composites post-order.
    std::vector<std::vector<ScopeId>> childrenOf(scopes.size());
    for (std::size_t i = 1; i < scopes.size(); ++i)
        childrenOf[scopes[i].parent].push_back((ScopeId)i);

    std::function<void(ScopeId)> emit = [&](ScopeId s) {
        ScopeRun content;
        content.phase       = ScopePhase::Content;
        content.scope       = s;
        content.level       = level[s];
        content.parentLevel = level[scopes[s].parent];
        v.scopeRuns.push_back(content);
        for (ScopeId c : childrenOf[s]) {
            emit(c);
            ScopeRun comp;
            comp.phase       = ScopePhase::Composite;
            comp.scope       = c;
            comp.level       = level[c];
            comp.parentLevel = level[s];
            comp.opacity     = scopes[c].opacity;
            comp.blend       = (std::uint32_t)scopes[c].blend;
            v.scopeRuns.push_back(comp);
        }
    };
    emit(kRootScope);

    return used;
}

namespace {

// Allocate a transient composite descriptor set (2 samplers: source, backdrop)
// and retire it through the garbage ring after this frame.
VkDescriptorSet MakeCompositeSet(RendererImpl& r, VkImageView source,
                                 VkImageView backdrop) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = r.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &r.compositeSetLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(r.device.vk(), &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkDescriptorImageInfo imgs[2] = {
        { r.device.linearSampler(), source,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { r.device.linearSampler(), backdrop,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
    VkWriteDescriptorSet w[2]{};
    for (int i = 0; i < 2; ++i) {
        w[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet          = set;
        w[i].dstBinding      = (std::uint32_t)i;
        w[i].descriptorCount = 1;
        w[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo      = &imgs[i];
    }
    vkUpdateDescriptorSets(r.device.vk(), 2, w, 0, nullptr);

    r.Defer([self = &r, set]() mutable {
        vkFreeDescriptorSets(self->device.vk(), self->descriptorPool, 1, &set);
    });
    return set;
}

} // namespace

void PlayScopes(RendererImpl& r, ViewImpl& v, std::uint32_t slot,
                graph::RenderGraph& g, const PushCamera& world,
                const PushCamera& px, VkBuffer overlayBuffer,
                std::uint32_t overlayCount) {
    VkBuffer indirect = v.indirect[slot].buffer;
    const VkDescriptorSet sceneSet = v.sceneSet;

    for (const ScopeRun& run : v.scopeRuns) {
        IsoTarget& iso = v.iso[run.level];

        if (run.phase == ScopePhase::Content) {
            const bool isRoot = (run.scope == kRootScope);
            // ── Content of this scope → iso[level] (MSAA, resolved to Cur),
            //    with the clip-mask stencil attached (cleared per pass) ──
            graph::RenderGraph::ColorTarget content{};
            content.image         = &iso.msaa;
            content.clear         = true;
            // Root clears to the page/background colour; isolated scopes
            // clear to transparent so only their own content composites.
            content.clearColor[0] = isRoot ? v.background.r : 0.0f;
            content.clearColor[1] = isRoot ? v.background.g : 0.0f;
            content.clearColor[2] = isRoot ? v.background.b : 0.0f;
            content.clearColor[3] = isRoot ? v.background.a : 0.0f;
            content.resolveTo     = &iso.Cur();
            content.stencil       = &iso.stencil;
            content.clearStencil  = true;

            const PushCamera worldCam = world, pxCam = px;
            const CmdSegment* segs = v.segScratch.data() + run.segOffset;
            const std::uint32_t nSegs = run.segCount;
            g.AddRenderPass("scope.content", content, {},
                            [&r, worldCam, indirect, segs, nSegs, sceneSet,
                             isRoot, pxCam, overlayBuffer,
                             overlayCount](VkCommandBuffer cmd) {
                RecordContentPass(r, cmd, worldCam, indirect, segs, nSegs,
                                  sceneSet);
                // Editor overlays ride the root content pass (v1: a composite
                // scope's pixels cover overlays where they overlap — the
                // composite-segments follow-up lifts them above).
                if (isRoot)
                    RecordOverlayPass(r, cmd, pxCam, overlayBuffer, overlayCount);
            });
            continue;
        }

        // ── Composite this scope onto its parent (ping-pong) ────────────────
        IsoTarget& parent = v.iso[run.parentLevel];
        VkDescriptorSet compSet =
            MakeCompositeSet(r, iso.Cur().view, parent.Cur().view);
        rhi::Image& dst = parent.Other();

        graph::RenderGraph::ColorTarget comp{};
        comp.image = &dst;
        comp.clear = true;            // fullscreen overwrite of the ping target
        PushComposite pc{ run.opacity, run.blend };
        g.AddRenderPass("scope.composite", comp,
                        { &iso.Cur(), &parent.Cur() },
                        [&r, compSet, pc](VkCommandBuffer cmd) {
            RecordCompositePass(r, cmd, compSet, pc);
        });
        parent.cur ^= 1;              // Other() is now current
    }
}

} // namespace Ink::detail

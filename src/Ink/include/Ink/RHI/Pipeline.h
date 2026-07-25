#pragma once

#include "Ink/RHI/Device.h"
#include <string>
#include <vector>

namespace Ink::rhi {

// ─────────────────────────────────────────────────────────────────────────────
//  Pipeline helpers for dynamic rendering (no VkRenderPass anywhere): load a
//  SPIR-V module and build a graphics pipeline from a compact description.
//  Every pipeline uses dynamic viewport/scissor.
// ─────────────────────────────────────────────────────────────────────────────

// Load "<dir>/<name>.spv". VK_NULL_HANDLE on failure.
VkShaderModule LoadShaderModule(Device& dev, const std::string& path);

struct VertexAttribute {
    std::uint32_t location = 0;
    VkFormat      format   = VK_FORMAT_R32G32_SFLOAT;
    std::uint32_t offset   = 0;
};

// Stencil interaction of a pipeline (clip masks — docs/Ink/RENDER_GRAPH.md
// §ClipPass). None: stencil untouched (test off). WriteMask: colour-write
// off, sets stencil = `stencilRef` where the geometry covers (ref 0 erases a
// mask). TestEqual: draws only where stencil == `stencilRef`.
// TestNotEqualWrite: draws (colour ON) only where stencil != ref and REPLACES
// the stencil with ref where it drew — the self-overlap dedup for TRANSLUCENT
// strokes (join fans / segment quads overlap on the inner side of a turn;
// without the test each overlap blends twice and reads darker). The stencil
// REFERENCE is a DYNAMIC state for this mode, so one pipeline serves the
// per-draw tags.
// NOTE (dynamic rendering VUs): a pipeline used inside a pass that HAS a
// stencil attachment must declare that format even when its mode is None —
// set `stencilFormat` on every pipeline that renders into such a pass.
enum class StencilMode { None, WriteMask, TestEqual, TestNotEqualWrite };

// How a pipeline writes colour into its target:
//   PremultipliedOver — out = src + dst·(1−src.a): the engine's one "over"
//                       operator inside an isolation target.
//   Erase             — out = dst·(1−src.a): an absolute geometric erase
//                       (dst-out) — the covered area is CLEARED from an
//                       isolated layer regardless of what colour it was
//                       (a subtractive mark object cuts the stroke layer;
//                       docs/Ink/RENDER_GRAPH.md §Erase).
//   OpaqueOverwrite   — out = src: no blending (the present pass).
enum class BlendKind { PremultipliedOver, Erase, OpaqueOverwrite };

struct GraphicsPipelineDesc {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    // One vertex binding (stride 0 = no vertex input at all — fullscreen pass).
    std::uint32_t                vertexStride = 0;
    std::vector<VertexAttribute> attributes;
    VkFormat              colorFormat   = VK_FORMAT_UNDEFINED;
    VkFormat              stencilFormat = VK_FORMAT_UNDEFINED;  // pass has stencil
    VkSampleCountFlagBits samples       = VK_SAMPLE_COUNT_1_BIT;
    BlendKind     blend                = BlendKind::PremultipliedOver;
    StencilMode   stencil              = StencilMode::None;
    std::uint32_t stencilRef           = 1;   // WriteMask value / TestEqual ref
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

VkPipeline CreateGraphicsPipeline(Device& dev, const GraphicsPipelineDesc& desc);

} // namespace Ink::rhi

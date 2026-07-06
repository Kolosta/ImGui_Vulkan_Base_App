#pragma once

#include "Renderer/Tessellation/Tessellator.h"   // PageSeg (persistent per-view segs)
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Renderer {

// ─────────────────────────────────────────────────────────────────────────────
//  RenderTarget — one offscreen colour image the canvas renders into, exposed
//  to ImGui as an ImTextureID so a Viewport zone can blit it with ImGui::Image.
//
//  One target exists per Viewport leaf (keyed by the leaf's EditorState in the
//  CanvasRenderer). It is recreated when the zone's pixel size changes. The
//  image is COLOR_ATTACHMENT (rendered to) + SAMPLED (read by ImGui).
// ─────────────────────────────────────────────────────────────────────────────

struct RenderTarget {
    VkImage         image       = VK_NULL_HANDLE;
    VkDeviceMemory  memory      = VK_NULL_HANDLE;
    VkImageView     view        = VK_NULL_HANDLE;
    VkFramebuffer   framebuffer = VK_NULL_HANDLE;
    VkDescriptorSet descriptor  = VK_NULL_HANDLE;   // ImGui sampler binding
    ImTextureID     textureId   = ImTextureID(0);

    uint32_t        width  = 0;
    uint32_t        height = 0;
    // Frame index this target was last used; lets the renderer evict targets
    // for zones that no longer exist (joined/closed leaves).
    uint64_t        lastUsedFrame = 0;

    // ── Persistent per-view geometry (Phase 1) ───────────────────────────────
    //  The built document mesh lives in the target's OWN vertex buffer across
    //  frames and is rebuilt only when `buildSig` changes (an actual document /
    //  detail-bucket edit). Pan/zoom live in the vertex-shader push constant, so
    //  a static/pan/zoom frame re-records the SAME buffer with a new camera and
    //  per-page scissors — no tessellation, no copy, no upload. The offscreen pass
    //  signals a semaphore the main pass waits on (no CPU stall); `lastSubmitFence`
    //  (a NON-owned handle to the submit slot's fence) lets a REBUILD wait only for
    //  this view's previous read to finish before overwriting its host buffer.
    VkBuffer        vbo         = VK_NULL_HANDLE;   // host-visible, persistent
    VkDeviceMemory  vboMemory   = VK_NULL_HANDLE;
    VkDeviceSize    vboCapacity = 0;
    uint32_t        vertexCount = 0;                // valid vertices in `vbo`
    std::vector<Tessellator::PageSeg> segs;         // CPU copy, reused to re-record
    uint64_t        buildSig    = 0;                // content signature at last build
    bool            hasGeometry = false;
    VkFence         lastSubmitFence = VK_NULL_HANDLE;   // non-owned: slot fence of last submit
    // Lot 4 cadence throttle: the frame this view last attempted a rebuild. A
    // non-focused view only re-attempts a detail/cull-margin rebuild every few frames
    // (structural changes still pass immediately via the signature compare).
    uint64_t        lastRebuildFrame = 0;
    uint64_t        wantSig          = 0;   // latest signature seen (even if gated)

    // ── Per-view pattern + decorator buffers (Phase 3/4) ─────────────────────
    //  Persistent buffers rebuilt with `vbo` only when the build signature changes:
    //  `decorInstVbo` = curve-decorator PatternInstance stream (Phase 4),
    //  `maskVbo` = procedural-fill cover/cut-polygon triangles (Phase 3 stencil).
    VkBuffer        decorInstVbo = VK_NULL_HANDLE;   // PatternInstance stream (decor)
    VkDeviceMemory  decorInstMemory = VK_NULL_HANDLE;
    VkDeviceSize    decorInstCapacity = 0;
    uint32_t        decorInstCount = 0;
    VkBuffer        maskVbo     = VK_NULL_HANDLE;   // cut-polygon triangles (stencil)
    VkDeviceMemory  maskMemory  = VK_NULL_HANDLE;
    VkDeviceSize    maskCapacity= 0;
    uint32_t        maskVertexCount = 0;

    // Depth/stencil attachment (the pattern clip mask), matched to the colour size.
    VkImage         stencilImage = VK_NULL_HANDLE;
    VkDeviceMemory  stencilMemory= VK_NULL_HANDLE;
    VkImageView     stencilView  = VK_NULL_HANDLE;

    bool valid() const { return image != VK_NULL_HANDLE; }
};

} // namespace Renderer

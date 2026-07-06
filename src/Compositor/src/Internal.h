#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor — internal shared helpers
//
//  Small utilities and push-constant layouts shared across the Engine's
//  translation units (Engine.cpp, Pipelines.cpp, Targets.cpp, Signature.cpp,
//  Passes/RenderView.cpp). One Engine class split by concern across several .cpp;
//  anything used by more than one of them lives here and MUST be `inline` (a
//  non-inline definition in a header included by several TUs is a multiple-
//  definition link error — see CLAUDE.md "File Organisation").
// ─────────────────────────────────────────────────────────────────────────────

#include <vulkan/vulkan.h>
#include "Renderer/Document/Shape.h"   // Renderer::BlendMode (Erase routing)
#include <cstdint>
#include <cstdio>

namespace Comp {

// The blend-mode value that means knock-out (dst-out). Erase is a BLEND MODE (the
// last enum value), not a separate flag — the Compositor routes it to the dst-out
// composite pipeline. (Shared by RenderView's object + group paths.)
inline constexpr uint8_t kBlendErase = (uint8_t)Renderer::BlendMode::Erase;

// Log a non-success VkResult (the engine is best-effort: it reports and carries on
// rather than aborting, so a missing shader degrades gracefully).
inline void Check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) std::fprintf(stderr, "[compositor] %s failed: %d\n", what, (int)r);
}

// Aspect mask of a (possibly combined) depth-stencil format, for views/barriers.
inline VkImageAspectFlags StencilAspect(VkFormat f) {
    VkImageAspectFlags a = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT)
        a |= VK_IMAGE_ASPECT_DEPTH_BIT;
    return a;
}

// ── Push-constant layouts (must match the shaders byte-for-byte) ────────────────

// Camera push constant for shape.vert (32 bytes), byte-identical to the legacy.
struct ShapePush {
    float pan[2];
    float target[2];
    float zoom;
    float unitScale;
    float pad[2];
};

// Camera (0..31) + procedural-pattern block (32..95) = 96 bytes, byte-identical to
// pattern_fill.frag. The stencil-mask / stroke-fill draws only push the camera.
struct PatternPush {
    float pan[2];
    float target[2];
    float zoom;
    float unitScale;
    float pad[2];
    float pColor[4];   // 32
    float pKind;       // 48
    float pSpacing;    // 52
    float pSize;       // 56
    float pAngle;      // 60
    float pOffset[2];  // 64
    float pSeed;       // 72
    float pDash;       // 76
    float pDashGap;    // 80
    float pAltPhase;   // 84
    float pCenter[2];  // 88
};

// Push for the stencil-then-cover base fill (Lot 13-4a). The COVER pass uses the
// full 48 bytes (camera 0..31 like ShapePush + straight RGBA fill colour at 32);
// the STENCIL pass only reads the first 32 (camera), colour ignored. Byte-identical
// to fill_cover.vert's push block.
struct FillCoverPush {
    float pan[2];
    float target[2];
    float zoom;
    float unitScale;
    float pad[2];
    float color[4];   // 32
};

// Push for the isolation composite: NDC bbox + opacity (20 bytes).
struct IsoPushC { float ndc[4]; float opacity; };
// Push for the blend composite: NDC bbox + opacity + blend mode (24 bytes).
struct BlendPushC { float ndc[4]; float opacity; int32_t mode; };

// Push for the picking id-pass (Lot 8): the 32-byte camera (identical layout to
// ShapePush, so picking.vert can reuse the projection) + the 1-based object id at
// offset 32 — byte-identical to picking.vert's push block.
struct PickPush {
    float    pan[2];
    float    target[2];
    float    zoom;
    float    unitScale;
    float    pad[2];
    uint32_t objId;
};

} // namespace Comp

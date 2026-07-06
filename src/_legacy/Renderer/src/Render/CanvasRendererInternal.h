#pragma once
// Internal helpers shared by the CanvasRenderer translation units (split by
// concern: device/targets, pipelines, per-frame render). Were file-static in
// CanvasRenderer.cpp; `inline` here so all three .cpp can use them.
#include "Renderer/Render/CanvasRenderer.h"
#include <cstdint>
#include <cstdio>

namespace Renderer {


// Sentinel build signature meaning "the last build left shapes DEFERRED (budget ran
// out), re-attempt next frame". It never equals a real FNV-1a signature (which starts
// from the FNV offset basis and mixes data), so a deferred view always re-builds.
inline constexpr uint64_t kDeferredSig = 0ull;

inline void CheckVk(VkResult err, const char* what) {
    if (err != VK_SUCCESS)
        fprintf(stderr, "[Renderer] %s failed: VkResult %d\n", what, (int)err);
}

} // namespace Renderer

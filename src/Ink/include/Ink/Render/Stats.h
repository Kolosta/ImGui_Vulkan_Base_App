#pragma once

#include <cstdint>

namespace Ink {

// Per-frame engine statistics, exposed to the application (Dev panels) and to
// ink_bench — the same counters in both places (docs/Ink/PERF_TESTING.md §2).
struct Stats {
    float recordMs = 0.0f;   // CPU: command recording (all views)
    float gpuMs    = 0.0f;   // GPU: whole Ink submit (timestamp pair)
    std::uint32_t drawCalls     = 0;   // indirect draw commands executed
    std::uint32_t triangles     = 0;   // sum over drawn batches × instances
    std::uint32_t instances     = 0;   // instance records drawn
    std::uint32_t views         = 0;   // views alive this frame
    std::uint32_t viewsRendered = 0;   // views actually re-recorded (signature)
};

} // namespace Ink

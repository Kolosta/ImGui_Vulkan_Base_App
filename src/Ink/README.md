# Ink — the 2D vector engine

The from-scratch rendering stack. **Read `docs/Ink/README.md` first** — the
specification (architecture, document model, render graph, geometry, perf
testing) and the lot-by-lot roadmap live there, not here.

Current state: **Lot 1 (Viewport bootstrap)** — RHI + minimal render graph +
GpuScene pools with real instancing + content/overlay/present passes on a
hard-coded demo scene, rendered per Viewport zone through `Ink::View`.

Hard rule: nothing under `src/Ink/` includes or copies from `src/_legacy/**`.

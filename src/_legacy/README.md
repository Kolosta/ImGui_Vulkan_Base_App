# _legacy — quarantined code (NOT built, NOT to be included)

This tree holds the **disconnected** previous rendering stack, kept only as a
behavioural reference during the Ink rewrite (see `docs/Ink/`):

- `Renderer/` — the legacy Vulkan engine (`Renderer::CanvasRenderer`) and the
  old document model (`Renderer::Document/Artboard/Shape`, tessellator, .acu
  v1 codec consumers).
- `Compositor/` — the second (modern-Vulkan) engine experiment (`Comp::Engine`).
- `Application/` — every application-layer file that was coupled to those
  engines / the old document: the whole Viewport editor cluster (tools, edit
  mode, transforms, placement, overlays), the Outliner and Properties
  implementations, the old `Application.h`, actions, clipboard, project file
  (.acu v1), snapshot undo, per-viewport page layout, GPU overlay list.
- `Modules/IofMapping/` — the IOF module (entirely built on the old document);
  it returns rebuilt on Ink (ROADMAP Lot 11).

Rules:

1. **Nothing here is referenced by any CMakeLists** — it must never compile.
2. **No live code may include anything under `src/_legacy/`** (grep gate).
3. Ink re-implements features from scratch; this tree answers *"how did it
   behave?"*, never *"copy this code"*.
4. Delete pieces of this tree freely once their replacement lot has landed.

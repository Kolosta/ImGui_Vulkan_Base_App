# Renderer — the Vulkan-only vector engine

This subsystem owns the **vector document model** and renders it **exclusively
through Vulkan**. ImGui is reserved for the application interface (chrome,
rulers, tool palette, panels); it never draws the vector artwork. The renderer
produces an offscreen texture per Viewport zone, which the application then
blits with `ImGui::Image` — so ImGui only ever sees a finished image.

> Status: Document model + offscreen Vulkan pipeline render the full shape set —
> rectangle, ellipse, triangle, polyline and cubic Bézier curves, with
> independent fill and stroke. The drawing tools, integrated Bézier editing and
> selection/properties live in the Application layer (`Editors/Viewport/ViewportTools.cpp`,
> `Editors/Outliner/Outliner.cpp`); the document persists in the `.acu` file format (see
> `docs/acu-format.md`).

## Layers

```
include/Renderer/
├─ Document/        Pure data (no Vulkan, no ImGui) — what .acu serialises
│  ├─ Paint.h       Color, FillStyle, StrokeStyle (fill/stroke independent)
│  ├─ Path.h        Vec2, Segment (Line | CubicBezier), Path
│  ├─ Shape.h       Shape: kind + params + fill + stroke
│  └─ Document.h    Document → Artboards → Shapes; stable id allocator
├─ Tessellation/
│  └─ Tessellator.h CPU tessellation → triangle Mesh (camera-independent)
└─ Render/
   ├─ RenderTarget.h One offscreen colour image + ImGui descriptor (ImTextureID)
   └─ CanvasRenderer.h Pipeline + render pass + per-view targets
shaders/
├─ shape.vert       Applies the per-view camera (push constant) → clip space
└─ shape.frag       Straight-alpha colour out
```

## Coordinate model

Shapes and artboards are stored in **document units**. The vertex shader maps a
document point to the offscreen target exactly like the Viewport's `D2S`:

```
screen_px = (doc * unitScale - pan) * zoom
ndc       = screen_px / target * 2 - 1
```

- `unitScale` — document-unit → ruler-pixel factor (the active ruler unit's
  `pxPer`), so the Vulkan output lines up with the ImGui rulers/guides.
- `pan` (doc-units, in the unit-scaled space) and `zoom` (px per unit) come from
  the per-leaf `EditorState`; every Viewport zone has its own camera, so the
  triangle buffer is camera-independent and only rebuilds when the document
  changes.

## Frame integration

`CanvasRenderer` shares the application's Vulkan device/queue/command-pool and a
sampler the app owns. Per frame, during the **UI-build phase** (before
`ImGui::Render()` and the main swapchain pass):

```
canvasRenderer_.BeginFrame();           // Application::Update()
  // for each Viewport leaf, inside RenderViewport():
  ImTextureID tex = canvasRenderer_.RenderView(&editorState, document,
                                               camera, wPx, hPx, backdrop);
  drawList->AddImage(tex, canvasMin, canvasMax);
canvasRenderer_.EndFrame();             // evicts targets of closed/joined zones
```

`RenderView` begins its **own** offscreen render pass (so it must run outside the
main pass), tessellates the document, uploads vertices to a host-visible buffer,
draws, and leaves the image in `SHADER_READ_ONLY_OPTIMAL` for ImGui to sample.
Step 1 submits synchronously (`vkQueueWaitIdle`); a fence/semaphore pipeline is a
later optimisation if profiling calls for it.

## Shaders

`shaders/*.vert|frag` are compiled to SPIR-V by **glslc** (Vulkan SDK) via a
CMake `add_custom_command` (target `renderer_shaders`). The `.spv` files are
written to `${CMAKE_BINARY_DIR}/shaders` and copied next to the executable in a
POST_BUILD step. At runtime the app resolves the shader directory **absolutely**
from the executable location (`SDL_GetBasePath()` + `"shaders"`), not from the
current working directory — so it works when launched from an IDE (F5) whose CWD
is the project root. The `.spv` outputs are git-ignored; only the GLSL sources
are tracked.

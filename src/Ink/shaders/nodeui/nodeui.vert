#version 450
// Ink Node UI pass (docs/Ink/NODE_UI.md) — textured quads (glyphs, live
// preview vignettes) for a view with no document content. Same px -> NDC
// push-constant convention as the overlay pass.

layout(location = 0) in vec2 inPos;      // view px
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;    // linear premultiplied tint

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform PC { vec2 scale; vec2 offset; } pc;

void main() {
    gl_Position = vec4(inPos * pc.scale + pc.offset, 0.0, 1.0);
    vUv = inUv;
    vColor = inColor;
}

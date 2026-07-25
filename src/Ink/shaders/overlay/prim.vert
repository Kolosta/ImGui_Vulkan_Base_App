#version 450
// Ink overlay pass — editor primitives pre-triangulated in view pixels
// (OverlayList). Same push-constant block as the content pass, loaded with
// the px -> NDC mapping.

layout(location = 0) in vec2 inPos;      // view px
layout(location = 1) in vec4 inColor;    // linear premultiplied

layout(location = 0) out vec4 vColor;

layout(push_constant) uniform PC { vec2 scale; vec2 offset; } pc;

void main() {
    gl_Position = vec4(inPos * pc.scale + pc.offset, 0.0, 1.0);
    vColor = inColor;
}

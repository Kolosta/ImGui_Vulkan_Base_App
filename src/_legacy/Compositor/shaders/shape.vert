#version 450

// Document geometry: per-vertex coloured triangles in DOCUMENT units. The camera
// (pan/zoom + target size) is applied here, exactly like the legacy shape.vert, so
// the triangle buffer is camera-independent (pan/zoom never rebuild it).

layout(push_constant) uniform PC {
    vec2  pan;        // unit-scaled pan (doc-units × unitScale)
    vec2  target;     // offscreen target size, px
    float zoom;       // px per (unit-scaled) doc-unit
    float unitScale;  // doc-unit → ruler-px factor
    vec2  _pad;
} pc;

layout(location = 0) in vec2 inPos;     // document-space position
layout(location = 1) in vec4 inColor;   // straight RGBA

layout(location = 0) out vec4 vColor;

void main() {
    vec2 screen = (inPos * pc.unitScale - pc.pan) * pc.zoom;
    vec2 ndc    = (screen / pc.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = inColor;
}

#version 450

// Cover-quad vertex shader for the procedural fill pattern (P2 Mask/Coverage):
// applies the camera (→ clip space) and passes the DOCUMENT-space position to the
// fragment so the motif is computed in doc-units (stable under pan/zoom). The
// surface stencil (written first) clips the motif to the contour.

layout(push_constant) uniform PC {
    vec2  pan;        // unit-scaled pan
    vec2  target;     // offscreen target size, px
    float zoom;       // px per (unit-scaled) doc-unit
    float unitScale;  // doc-unit → ruler-px
    vec2  _pad;
} pc;

layout(location = 0) in vec2 inPos;     // document-space position
layout(location = 1) in vec4 inColor;   // unused (cover polygon)

layout(location = 0) out vec2 vDoc;

void main() {
    vec2 screen = (inPos * pc.unitScale - pc.pan) * pc.zoom;
    vec2 ndc    = (screen / pc.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vDoc = inPos;
}

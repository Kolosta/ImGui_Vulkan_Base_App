#version 450

// Picking id-pass vertex shader (Lot 8). Same projection as shape.vert — the
// object geometry in DOCUMENT units, camera applied here — but it carries a
// per-draw object id through to the fragment shader, which writes it to an R32UI
// id buffer. The vertex colour is irrelevant for picking and ignored.

layout(push_constant) uniform PC {
    vec2  pan;        // unit-scaled pan (doc-units × unitScale)
    vec2  target;     // offscreen target size, px
    float zoom;       // px per (unit-scaled) doc-unit
    float unitScale;  // doc-unit → ruler-px factor
    vec2  _pad;
    uint  objId;      // 1-based object id for this draw (0 = background)
} pc;

layout(location = 0) in vec2 inPos;     // document-space position
layout(location = 1) in vec4 inColor;   // ignored for picking

layout(location = 0) flat out uint vId;

void main() {
    vec2 screen = (inPos * pc.unitScale - pc.pan) * pc.zoom;
    vec2 ndc    = (screen / pc.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vId = pc.objId;
}

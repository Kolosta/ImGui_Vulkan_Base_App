#version 450

// Stencil-then-cover fill (Lot 13-4a) — COVER pass.
// After the contour is written to the stencil (non-zero winding), a bounding QUAD
// (6 world-space verts) is drawn stencil-tested NOT_EQUAL 0 so only the covered
// interior is painted, ONCE, with the object's uniform fill colour. The pass also
// resets the stencil to 0 on covered pixels (pipeline passOp = REPLACE ref 0), so
// the next object starts clean without a separate clear. Same camera as shape.vert.

layout(push_constant) uniform PC {
    vec2  pan;
    vec2  target;
    float zoom;
    float unitScale;
    vec2  _pad;
    vec4  color;      // straight RGBA fill colour (offset 32)
} pc;

layout(location = 0) in vec2 inPos;   // document-space quad corner (world doc-units)

layout(location = 0) out vec4 vColor;

void main() {
    vec2 screen = (inPos * pc.unitScale - pc.pan) * pc.zoom;
    vec2 ndc    = (screen / pc.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = pc.color;
}

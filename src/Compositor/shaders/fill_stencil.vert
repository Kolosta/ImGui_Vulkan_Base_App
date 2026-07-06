#version 450

// Stencil-then-cover fill (Lot 13-4a) — STENCIL pass.
// A solid fill's flattened contour is fanned trivially (pivot → each edge) and
// drawn here writing ONLY the stencil (no colour), under a non-zero winding rule
// (front faces INCR_WRAP, back faces DECR_WRAP — set in the pipeline). No interior
// triangulation: overlapping fan triangles resolve by winding, so interior pixels
// end non-zero and exterior zero. Position only; same camera as shape.vert.

layout(push_constant) uniform PC {
    vec2  pan;        // unit-scaled pan (doc-units × unitScale)
    vec2  target;     // offscreen target size, px
    float zoom;       // px per (unit-scaled) doc-unit
    float unitScale;  // doc-unit → ruler-px factor
    vec2  _pad;
} pc;

layout(location = 0) in vec2 inPos;   // document-space position (world doc-units)

void main() {
    vec2 screen = (inPos * pc.unitScale - pc.pan) * pc.zoom;
    vec2 ndc    = (screen / pc.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}

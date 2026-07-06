#version 450

// Stencil-then-cover fill (Lot 13-4a) — COVER fragment: the uniform fill colour,
// straight alpha (the pipeline blends SRC_ALPHA over, like the base shape pass).

layout(location = 0) in  vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}

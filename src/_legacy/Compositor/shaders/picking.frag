#version 450

// Picking id-pass fragment shader (Lot 8). Writes the per-draw object id (flat
// from the vertex shader) into a single-channel R32UI id buffer. No blending,
// no colour — just the identity of the topmost object covering each pixel (the
// id-pass is drawn in document order, so later objects overwrite earlier ones,
// matching the painter order of the colour pass).

layout(location = 0) flat in uint vId;
layout(location = 0) out uint outId;

void main() {
    outId = vId;
}

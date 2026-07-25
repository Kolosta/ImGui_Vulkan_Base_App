#version 450

// Cover vertex shader for procedural fill patterns. Draws the surface's cut-polygon
// triangles (the same geometry written to the stencil mask) and passes the DOCUMENT-
// space position to the fragment shader, which paints the motif procedurally. The
// camera maps doc → clip exactly like shape.vert.

layout(location = 0) in vec2 inPos;     // document-space position
layout(location = 1) in vec4 inColor;   // ignored (the fragment uses pColor)

layout(location = 0) out vec2 vDoc;     // document-space position for the motif

// Camera subset of the shared 96-byte push constant (bytes 0..31).
layout(push_constant) uniform PC {
    vec2 pan;
    vec2 target;
    float zoom;
    float unitScale;
    vec2 _pad;
} cam;

void main() {
    vec2 screen = (inPos * cam.unitScale - cam.pan) * cam.zoom;
    gl_Position = vec4((screen / cam.target) * 2.0 - 1.0, 0.0, 1.0);
    vDoc = inPos;
}

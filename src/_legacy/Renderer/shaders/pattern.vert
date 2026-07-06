#version 450

// Instanced pattern element (fill-layer dots / triangles / line dashes). Binding 0
// is a UNIT base mesh (a radius-0.5 disc, a unit triangle, a unit quad); binding 1
// carries one PER-INSTANCE transform + colour, so a whole pattern of N elements is
// one base mesh drawn N times. The vertex shader places each instance:
//   element = R(rot) * (basePos * scale)
//   doc     = center + element
// then maps doc → clip space with the SAME camera push constant as shape.vert.

// Binding 0 — unit base mesh (per-vertex).
layout(location = 0) in vec2 inBasePos;

// Binding 1 — per-instance (INPUT_RATE_INSTANCE).
layout(location = 2) in vec2 inCenter;   // instance centre, document units
layout(location = 3) in vec2 inScale;    // non-uniform scale (doc-units per unit mesh)
layout(location = 4) in float inRot;      // element rotation, radians
layout(location = 5) in vec4 inColor;    // straight RGBA (incl. layer opacity)

layout(location = 0) out vec4 fragColor;

// Must match Renderer::CanvasRenderer::PushConstants byte-for-byte (shared layout).
layout(push_constant) uniform Camera {
    vec2 pan;
    vec2 target;
    float zoom;
    float unitScale;
    vec2 _pad;
} cam;

void main() {
    float c = cos(inRot), s = sin(inRot);
    vec2 e   = inBasePos * inScale;
    vec2 rot = vec2(e.x * c - e.y * s, e.x * s + e.y * c);
    vec2 doc = inCenter + rot;
    vec2 screen = (doc * cam.unitScale - cam.pan) * cam.zoom;
    vec2 ndc    = (screen / cam.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor   = inColor;
}

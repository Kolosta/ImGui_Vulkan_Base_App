#version 450

// Vertex layout mirrors Renderer::Vertex (pos in document units, straight RGBA).
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// Camera + viewport packed as a push constant. We map a document-space point to
// Vulkan clip space in two steps:
//   screen_px = (doc - pan) * zoom              // doc-units -> target pixels
//   ndc       = screen_px / target * 2 - 1      // pixels    -> [-1, 1] clip
// pan/zoom come from the per-view EditorState; target is the offscreen size.
layout(push_constant) uniform Camera {
    vec2 pan;        // pan in unit-scaled space (matches the ImGui-side camera)
    vec2 target;     // offscreen target size, in pixels
    float zoom;      // pixels per (unit-scaled) document unit
    float unitScale; // document-unit -> ruler-pixel factor
    vec2 _pad;
} cam;

void main() {
    // screen_px = (doc * unitScale - pan) * zoom  — identical to the Viewport D2S.
    vec2 screen = (inPos * cam.unitScale - cam.pan) * cam.zoom;
    vec2 ndc    = (screen / cam.target) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor   = inColor;
}

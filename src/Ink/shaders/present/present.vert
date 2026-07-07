#version 450
// Ink present pass — fullscreen triangle (no vertex buffer).

layout(location = 0) out vec2 vUv;

void main() {
    // 3 vertices covering the screen: (0,0) (2,0) (0,2) in UV.
    vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}

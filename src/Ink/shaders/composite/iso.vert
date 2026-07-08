#version 450
// Ink composite pass — fullscreen triangle sampling an isolation target and
// its backdrop at the SAME pixel (no transform: the iso target and the parent
// share the view's pixel grid).

layout(location = 0) out vec2 vUv;

void main() {
    vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}

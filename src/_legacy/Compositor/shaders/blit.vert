#version 450

// Composite a rendered view onto the swapchain: a quad covering an NDC rect,
// textured with the view's offscreen image. Pure Vulkan — replaces the legacy
// ImGui blit of the canvas.

layout(push_constant) uniform PC {
    vec4 ndc;   // xmin, ymin, xmax, ymax (y down, ImGui-style NDC)
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    // Two triangles (6 verts) covering the unit square, expanded to the rect.
    const vec2 c[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 uv = c[gl_VertexIndex];
    vec2 p  = mix(pc.ndc.xy, pc.ndc.zw, uv);   // top-left → ndc.xy
    gl_Position = vec4(p, 0.0, 1.0);
    vUV = uv;                                   // (0,0) = image top-left
}

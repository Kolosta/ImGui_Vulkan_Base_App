#version 450

// P4 Composite: composite an isolated object layer onto the canvas. The object was
// rendered at FULL opacity into the (canvas-aligned) isolation image; here a quad
// over its NDC bbox samples it (screen-space UV, since canvas and isolation share
// the camera/size) and the fragment scales alpha by the object's opacity.

layout(push_constant) uniform PC {
    vec4  ndc;       // xmin, ymin, xmax, ymax (the object's bbox in NDC)
    float opacity;   // object opacity [0,1]
} pc;

layout(location = 0) out vec2 vUV;

void main() {
    const vec2 c[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
    vec2 corner = c[gl_VertexIndex];
    vec2 p = mix(pc.ndc.xy, pc.ndc.zw, corner);
    gl_Position = vec4(p, 0.0, 1.0);
    vUV = p * 0.5 + 0.5;   // canvas/isolation are aligned → screen-space UV
}

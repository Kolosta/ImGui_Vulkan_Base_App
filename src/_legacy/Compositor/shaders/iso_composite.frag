#version 450

layout(set = 0, binding = 0) uniform sampler2D uIso;

layout(push_constant) uniform PC {
    vec4  ndc;
    float opacity;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    // The isolation layer is PREMULTIPLIED (objects are drawn into a transparent
    // target with src-over, premultiplying rgb by a). UN-PREMULTIPLY to a straight
    // colour here and let the pipeline composite it straight (srcColour = SRC_ALPHA),
    // so the rgb is multiplied by alpha exactly ONCE total — no per-group darkening.
    vec4 s = texture(uIso, vUV);
    vec3 straight = s.a > 0.0001 ? s.rgb / s.a : vec3(0.0);
    outColor = vec4(straight, s.a * pc.opacity);
}

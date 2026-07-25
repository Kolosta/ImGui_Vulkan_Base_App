#version 450
// Ink present pass — sRGB-encode the resolved linear-premultiplied canvas
// into the display image the UI samples (encode-at-present rule,
// docs/Ink/ARCHITECTURE.md §6). The canvas is opaque by construction (the
// background clear has alpha 1), so alpha is pinned to 1.

layout(set = 0, binding = 0) uniform sampler2D uCanvas;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 EncodeSrgb(vec3 linear) {
    vec3 lo = linear * 12.92;
    vec3 hi = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), linear));
}

void main() {
    vec3 linear = texture(uCanvas, vUv).rgb;
    outColor = vec4(EncodeSrgb(clamp(linear, 0.0, 1.0)), 1.0);
}

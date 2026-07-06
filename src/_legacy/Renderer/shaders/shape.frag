#version 450

layout(location = 0) in  vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    // Straight-alpha colour. The offscreen target is later sampled by ImGui with
    // its standard alpha blend, so we keep premultiplication out of the shader.
    outColor = fragColor;
}

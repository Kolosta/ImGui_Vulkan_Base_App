#version 450
// Flat premultiplied fill; blending (premultiplied over) is fixed-function.

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}

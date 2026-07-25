#version 450

// Editor overlay (Lot 12): coloured triangles already in NORMALISED DEVICE COORDS
// (the editor tessellates selection outlines / handles / grid / etc. on the CPU and
// submits them; the GPU just rasterises). y is down, matching the main-window
// projection — positions are passed straight to gl_Position.

layout(location = 0) in vec2 inPos;     // NDC, y down
layout(location = 1) in vec4 inColor;   // straight RGBA (unpacked from 0xAABBGGRR)

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    vColor = inColor;
}

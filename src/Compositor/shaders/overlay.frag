#version 450

// Editor overlay fragment shader (Lot 12): straight-alpha colour, blended over the
// already-composited canvas by the pipeline (SRC_ALPHA / ONE_MINUS_SRC_ALPHA).

layout(location = 0) in  vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}

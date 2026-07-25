#version 450
// One shared textured-quad pipeline draws both Node UI uses (docs/Ink/
// NODE_UI.md): a glyph quad samples the font atlas (stored PREMULTIPLIED
// white — rgb=coverage, a=coverage — so multiplying by the vertex tint
// yields a correctly colored, anti-aliased, premultiplied glyph); a preview
// quad samples another view's own rendered image with the tint left at
// opaque white (pass-through) or dimmed for a muted node.

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(uTex, vUv) * vColor;
}

#version 450
// Ink composite pass (docs/Ink/RENDER_GRAPH.md §CompositePass): apply a
// group's opacity + blend mode when compositing its isolation target (source)
// onto the parent (backdrop). Both are LINEAR PREMULTIPLIED. The W3C
// compositing formula works on STRAIGHT colours, so we un-premultiply, blend,
// then re-premultiply — the rule that avoids the double-premultiply greying.
//
//   Cs, Cb : straight source / backdrop colour
//   B(Cb,Cs): the separable blend function (index uBlend)
//   Co = (1 - ab)·Cs + ab·B(Cb,Cs)          (W3C blended source colour)
//   result = Porter-Duff "over" of (Co, as·opacity) atop backdrop
//
// Erase (index 12) is dst-out: the source coverage removes the backdrop.

layout(set = 0, binding = 0) uniform sampler2D uSource;    // isolation target
layout(set = 0, binding = 1) uniform sampler2D uBackdrop;  // parent so far

layout(push_constant) uniform PC { float opacity; uint blend; } pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 unpremul(vec4 c) { return c.a > 0.0001 ? c.rgb / c.a : vec3(0.0); }

float blendChannel(uint mode, float b, float s) {
    if (mode == 1u)  return b * s;                                   // Multiply
    if (mode == 2u)  return b + s - b * s;                           // Screen
    if (mode == 3u)  return b <= 0.5 ? 2.0*b*s : 1.0-2.0*(1.0-b)*(1.0-s); // Overlay
    if (mode == 4u)  return min(b, s);                              // Darken
    if (mode == 5u)  return max(b, s);                              // Lighten
    if (mode == 6u)  return s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s)); // ColorDodge
    if (mode == 7u)  return s <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0-b)/s); // ColorBurn
    if (mode == 8u)  return s <= 0.5 ? 2.0*b*s : 1.0-2.0*(1.0-b)*(1.0-s); // HardLight
    if (mode == 9u) {                                               // SoftLight
        float d = (b <= 0.25) ? ((16.0*b - 12.0)*b + 4.0)*b : sqrt(b);
        return (s <= 0.5) ? b - (1.0-2.0*s)*b*(1.0-b)
                          : b + (2.0*s-1.0)*(d - b);
    }
    if (mode == 10u) return abs(b - s);                            // Difference
    if (mode == 11u) return b + s - 2.0*b*s;                       // Exclusion
    return s;                                                       // Normal
}

void main() {
    vec4 src = texture(uSource, vUv);
    vec4 bd  = texture(uBackdrop, vUv);

    // Erase: dst-out. The group's alpha (scaled by opacity) carves the backdrop.
    if (pc.blend == 12u) {
        outColor = bd * (1.0 - src.a * pc.opacity);
        return;
    }

    float as = src.a * pc.opacity;
    if (as <= 0.0) { outColor = bd; return; }

    vec3 Cs = unpremul(src);
    vec3 Cb = unpremul(bd);
    float ab = bd.a;

    vec3 blended = vec3(blendChannel(pc.blend, Cb.r, Cs.r),
                        blendChannel(pc.blend, Cb.g, Cs.g),
                        blendChannel(pc.blend, Cb.b, Cs.b));
    // W3C: the backdrop's alpha modulates how much blending vs plain source.
    vec3 Co = mix(Cs, blended, ab);

    // Porter-Duff over, in premultiplied output.
    vec3 outRgb = Co * as + bd.rgb * (1.0 - as);
    float outA  = as + ab * (1.0 - as);
    outColor = vec4(outRgb, outA);
}

#version 450
// Ink composite pass — composite a group's isolation target (source) onto its
// parent (backdrop) with the group's opacity and blend mode.
//
// This is the W3C "Compositing and Blending Level 1" model verbatim
// (https://www.w3.org/TR/compositing-1/), the current industry standard used
// by SVG / CSS / Canvas / PDF. Blend functions operate on NON-premultiplied
// colours by definition, so the pipeline (which is premultiplied for correct
// coverage math) un-premultiplies at the blend and re-premultiplies after —
// this is the standard, not a workaround.
//
//   Cs, Cb, αs, αb : source / backdrop non-premultiplied colour + alpha
//   B(Cb, Cs)      : the separable blend function
//   Cs' = (1 - αb)·Cs + αb·B(Cb, Cs)      blended source (W3C step 1)
//   Porter-Duff source-over with (Cs', αs):
//     Co = αs·Cs' + αb·(1 - αs)·Cb        (non-premultiplied composited colour)
//     αo = αs + αb·(1 - αs)
//   Output is premultiplied: (αo·Co, αo).
//
// Erase is dst-out: the source coverage removes the backdrop.

layout(set = 0, binding = 0) uniform sampler2D uSource;    // isolation target (premul)
layout(set = 0, binding = 1) uniform sampler2D uBackdrop;  // parent so far (premul)

layout(push_constant) uniform PC { float opacity; uint blend; } pc;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

// Separable blend functions (W3C §blending), per channel, on [0,1] colours.
float blendChannel(uint mode, float cb, float cs) {
    switch (mode) {
    case 1u:  return cb * cs;                                          // Multiply
    case 2u:  return cb + cs - cb * cs;                               // Screen
    case 3u:  return cb <= 0.5 ? 2.0*cb*cs                            // Overlay
                               : 1.0 - 2.0*(1.0-cb)*(1.0-cs);
    case 4u:  return min(cb, cs);                                     // Darken
    case 5u:  return max(cb, cs);                                     // Lighten
    case 6u:  return cb == 0.0 ? 0.0                                  // ColorDodge
                    : cs == 1.0 ? 1.0 : min(1.0, cb / (1.0 - cs));
    case 7u:  return cb == 1.0 ? 1.0                                  // ColorBurn
                    : cs == 0.0 ? 0.0 : 1.0 - min(1.0, (1.0-cb) / cs);
    case 8u:  return cs <= 0.5 ? 2.0*cb*cs                            // HardLight
                               : 1.0 - 2.0*(1.0-cb)*(1.0-cs);
    case 9u: {                                                        // SoftLight
        float d = cb <= 0.25 ? ((16.0*cb - 12.0)*cb + 4.0)*cb : sqrt(cb);
        return cs <= 0.5 ? cb - (1.0 - 2.0*cs) * cb * (1.0 - cb)
                         : cb + (2.0*cs - 1.0) * (d - cb);
    }
    case 10u: return abs(cb - cs);                                    // Difference
    case 11u: return cb + cs - 2.0*cb*cs;                             // Exclusion
    default:  return cs;                                             // Normal
    }
}

void main() {
    vec4 src = texture(uSource, vUv);
    vec4 bd  = texture(uBackdrop, vUv);

    // Erase (dst-out): the source's coverage carves the backdrop away.
    if (pc.blend == 12u) {
        outColor = bd * (1.0 - src.a * pc.opacity);
        return;
    }

    float alphaS = src.a * pc.opacity;
    float alphaB = bd.a;

    // Un-premultiply to the colours the blend functions are defined on.
    vec3 Cs = src.a > 0.0 ? src.rgb / src.a : vec3(0.0);
    vec3 Cb = bd.a  > 0.0 ? bd.rgb  / bd.a  : vec3(0.0);

    // W3C blended source colour: blending only applies where the backdrop is
    // opaque; over transparency it falls back to the plain source.
    vec3 blended = vec3(blendChannel(pc.blend, Cb.r, Cs.r),
                        blendChannel(pc.blend, Cb.g, Cs.g),
                        blendChannel(pc.blend, Cb.b, Cs.b));
    vec3 Csp = mix(Cs, blended, alphaB);

    // Porter-Duff source-over, then re-premultiply the output.
    float alphaO = alphaS + alphaB * (1.0 - alphaS);
    vec3  Co     = alphaS * Csp + alphaB * (1.0 - alphaS) * Cb;
    outColor = vec4(Co, alphaO);
}

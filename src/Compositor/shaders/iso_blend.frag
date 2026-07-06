#version 450

// P4 Composite blend (Lot 4b): composite an isolated object (src) over the canvas
// backdrop (dst) with a blend mode. The W3C/SVG separable + non-separable modes.
// The canvas is opaque (αb = 1), so out = mix(dst, blend(dst, src), src.a·opacity).

layout(set = 0, binding = 0) uniform sampler2D uIso;       // isolated object (src)
layout(set = 0, binding = 1) uniform sampler2D uBackdrop;  // canvas copy (dst)

layout(push_constant) uniform PC {
    vec4  ndc;
    float opacity;
    int   mode;     // BlendMode (1=Multiply … 15=Luminosity; 0=Normal not routed here)
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// ── Separable per-channel blend functions (cb = backdrop, cs = source) ──────────
float blendChannel(int m, float cb, float cs) {
    if (m == 1)  return cb * cs;                                  // Multiply
    if (m == 2)  return cb + cs - cb * cs;                        // Screen
    if (m == 3)  return cb <= 0.5 ? 2.0*cb*cs : 1.0 - 2.0*(1.0-cb)*(1.0-cs); // Overlay = HardLight(cs,cb)
    if (m == 4)  return min(cb, cs);                              // Darken
    if (m == 5)  return max(cb, cs);                              // Lighten
    if (m == 6)  return cs >= 1.0 ? 1.0 : min(1.0, cb / (1.0 - cs));        // ColorDodge
    if (m == 7)  return cs <= 0.0 ? 0.0 : 1.0 - min(1.0, (1.0 - cb) / cs);  // ColorBurn
    if (m == 8)  return cs <= 0.5 ? 2.0*cb*cs : 1.0 - 2.0*(1.0-cb)*(1.0-cs); // HardLight
    if (m == 9) {                                                 // SoftLight (W3C)
        float d = (cb <= 0.25) ? ((16.0*cb - 12.0)*cb + 4.0)*cb : sqrt(cb);
        return (cs <= 0.5) ? cb - (1.0 - 2.0*cs)*cb*(1.0 - cb)
                           : cb + (2.0*cs - 1.0)*(d - cb);
    }
    if (m == 10) return abs(cb - cs);                            // Difference
    if (m == 11) return cb + cs - 2.0*cb*cs;                     // Exclusion
    return cs;                                                   // Normal fallback
}

// ── Non-separable (HSL) helpers (W3C) ───────────────────────────────────────────
float lum(vec3 c) { return dot(c, vec3(0.3, 0.59, 0.11)); }
vec3 clipColor(vec3 c) {
    float l = lum(c);
    float n = min(min(c.r, c.g), c.b);
    float x = max(max(c.r, c.g), c.b);
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-6);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-6);
    return c;
}
vec3 setLum(vec3 c, float l) { return clipColor(c + (l - lum(c))); }
float satv(vec3 c) { return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b); }
vec3 setSat(vec3 c, float s) {
    float mn = min(min(c.r, c.g), c.b);
    float mx = max(max(c.r, c.g), c.b);
    vec3 r = vec3(0.0);
    if (mx > mn) r = (c - mn) / (mx - mn) * s;
    return r;
}

vec3 blendColor(vec3 cb, vec3 cs) {
    int m = pc.mode;
    if (m == 12) return setLum(setSat(cs, satv(cb)), lum(cb));   // Hue
    if (m == 13) return setLum(setSat(cb, satv(cs)), lum(cb));   // Saturation
    if (m == 14) return setLum(cs, lum(cb));                     // Color
    if (m == 15) return setLum(cb, lum(cs));                     // Luminosity
    return vec3(blendChannel(m, cb.r, cs.r),                     // separable
                blendChannel(m, cb.g, cs.g),
                blendChannel(m, cb.b, cs.b));
}

void main() {
    vec4 sp = texture(uIso, vUV);          // PREMULTIPLIED isolated source
    vec4 bk = texture(uBackdrop, vUV);     // backdrop (object stack below) — premultiplied
    // Un-premultiply both to straight colours for the W3C blend math.
    vec3 cs = sp.a > 0.0001 ? sp.rgb / sp.a : vec3(0.0);
    vec3 cb = bk.a > 0.0001 ? bk.rgb / bk.a : vec3(0.0);
    float a = clamp(sp.a * pc.opacity, 0.0, 1.0);
    // The page substrate is NOT part of the blend: the backdrop here is just the
    // object stack below, which may be partly transparent. W3C: where the backdrop
    // is transparent the blend reduces to the source colour, so lerp the blended
    // colour toward cs by the backdrop coverage.
    vec3 b = mix(cs, blendColor(cb, cs), clamp(bk.a, 0.0, 1.0));
    // Output STRAIGHT (the iso composite + the canvas are straight; blendEnable is
    // FALSE so this pixel overwrites the target, and the target/backdrop are straight).
    // Keep the backdrop's coverage so an erase hole below stays a hole.
    vec3  outRgb = mix(cb, b, a);
    float outA   = a + bk.a * (1.0 - a);
    outColor = vec4(outRgb, outA);
}

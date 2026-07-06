#version 450

// Procedural fill-pattern fragment shader (P2 Mask/Coverage stage). The motif
// (dots / lines / triangles / random-dots / grid / cross-hatch) is computed from
// the DOCUMENT-space fragment position + pattern parameters — no per-element CPU
// geometry. The surface stencil clips it to the contour, so editing spacing/size/
// angle/offset/contour costs zero CPU work. AA is automatic: fwidth() is in screen
// pixels (incl. zoom·unitScale·SSAA), so a 1px-soft edge falls out of smoothstep.

layout(location = 0) in  vec2 vDoc;     // document-space position
layout(location = 0) out vec4 outColor; // straight RGBA

layout(push_constant) uniform PC {
    vec2 pan; vec2 target; float zoom; float unitScale; vec2 _pad;   // camera (0..31)
    vec4  pColor;      // 32
    float pKind;       // 48  1 Dots 2 Lines 3 Triangles 4 RandomDots 5 Grid 6 CrossHatch
    float pSpacing;    // 52
    float pSize;       // 56
    float pAngle;      // 60
    vec2  pOffset;     // 64
    float pSeed;       // 72
    float pDash;       // 76
    float pDashGap;    // 80
    float pAltPhase;   // 84
    vec2  pCenter;     // 88
} pc;

// Murmurish hash + signed jitter (pixel-identical to the CPU generator scatter).
uint hash2(uint seed, int gx, int gy) {
    uint h = seed ^ uint(gx * 73856093) ^ uint(gy * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
float j16(uint x)  { return float(x & 0xFFFFu) / 65535.0 - 0.5; }
float u16(uint x)  { return float(x & 0xFFFFu) / 65535.0; }

// Signed distance from `p` to the inside of triangle (A,B,C) (>0 outside).
float triInside(vec2 p, vec2 A, vec2 B, vec2 C) {
    float s = sign((B.x-A.x)*(C.y-A.y) - (B.y-A.y)*(C.x-A.x));
    float d0 = s * ((B.x-A.x)*(p.y-A.y) - (B.y-A.y)*(p.x-A.x));
    float d1 = s * ((C.x-B.x)*(p.y-B.y) - (C.y-B.y)*(p.x-B.x));
    float d2 = s * ((A.x-C.x)*(p.y-C.y) - (A.y-C.y)*(p.x-C.x));
    float l0 = length(B-A), l1 = length(C-B), l2 = length(A-C);
    return max(max(-d0/max(l0,1e-6), -d1/max(l1,1e-6)), -d2/max(l2,1e-6));
}

void main() {
    float ca = cos(pc.pAngle), sa = sin(pc.pAngle);
    mat2 Rinv = mat2(ca, -sa, sa, ca);          // doc → local (inverse rotation)
    vec2 q = Rinv * (vDoc - pc.pCenter) - pc.pOffset;
    float aa = max(length(vec2(fwidth(q.x), fwidth(q.y))), 1e-6);  // ~1px in doc-units
    float sp = max(pc.pSpacing, 1e-4);
    float sz = max(pc.pSize, 1e-4);
    uint seed = floatBitsToUint(pc.pSeed);
    float cov = 0.0;
    int k = int(pc.pKind + 0.5);

    if (k == 1 || k == 4) {                      // Dots / RandomDots
        float r = sz * 0.5;
        vec2 cell = floor(q / sp + 0.5);
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 gc = cell + vec2(dx, dy);
            vec2 c = gc * sp;
            if (k == 4) {
                uint h = hash2(seed, int(gc.x), int(gc.y));
                c += vec2(j16(h) * sp * 0.8, j16(h >> 16) * sp * 0.8);
            }
            cov = max(cov, smoothstep(r + aa*0.5, r - aa*0.5, length(q - c)));
        }
    } else if (k == 3) {                         // Triangles (ISOM 8:6:5 scalene)
        float s8 = sz, s6 = sz * 0.75, s5 = sz * 0.625;
        float tcx = (s8*s8 + s6*s6 - s5*s5) / (2.0 * s8);
        float tcy = sqrt(max(0.0, s6*s6 - tcx*tcx));
        vec2 ce = vec2((s8 + tcx) / 3.0, tcy / 3.0);
        vec2 A = -ce, B = vec2(s8,0) - ce, C = vec2(tcx,tcy) - ce;
        int ring = int(ceil((0.5*sp + sz) / sp));
        vec2 cell = floor(q / sp + 0.5);
        for (int dy = -ring; dy <= ring; ++dy)
        for (int dx = -ring; dx <= ring; ++dx) {
            vec2 gc = cell + vec2(dx, dy);
            uint h = hash2(seed, int(gc.x), int(gc.y));
            vec2 c = gc * sp + vec2(j16(h) * sp * 0.5, j16(h >> 16) * sp * 0.5);
            float ta = u16(h >> 8) * 6.2831853;
            float c2 = cos(ta), s2 = sin(ta);
            mat2 Rt = mat2(c2, s2, -s2, c2);
            vec2 f = Rt * (q - c);
            float d = triInside(f, A, B, C);
            cov = max(cov, smoothstep(aa*0.5, -aa*0.5, d));
        }
    } else {                                     // Lines (2) / Grid (5) / CrossHatch (6)
        float halfw = sz * 0.5;
        float idV = floor(q.y / sp + 0.5);
        float covV = smoothstep(halfw + aa*0.5, halfw - aa*0.5, abs(q.y - idV*sp));
        if (pc.pDash > 1e-4) {
            float P = pc.pDash + pc.pDashGap;
            float phase = (pc.pAltPhase > 0.5 && (int(idV) & 1) != 0) ? P * 0.5 : 0.0;
            float t = mod(q.x - phase, P);
            covV *= 1.0 - smoothstep(pc.pDash - aa, pc.pDash + aa, t);
        }
        cov = covV;
        if (k == 5 || k == 6) {                  // second perpendicular set
            float idH = floor(q.x / sp + 0.5);
            float covH = smoothstep(halfw + aa*0.5, halfw - aa*0.5, abs(q.x - idH*sp));
            if (pc.pDash > 1e-4) {
                float P = pc.pDash + pc.pDashGap;
                float phase = (pc.pAltPhase > 0.5 && (int(idH) & 1) != 0) ? P * 0.5 : 0.0;
                float t = mod(q.y - phase, P);
                covH *= 1.0 - smoothstep(pc.pDash - aa, pc.pDash + aa, t);
            }
            cov = max(cov, covH);
        }
    }

    if (cov <= 0.0) discard;
    outColor = vec4(pc.pColor.rgb, pc.pColor.a * cov);
}

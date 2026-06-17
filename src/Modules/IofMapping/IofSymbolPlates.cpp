#include "IofGlyph.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Hand-authored precise-definition PLATES, reproducing the ISOM 2017-2 figures
//  (docs/Doc IOF .pdf / the extracted SVGs). Everything is in DOCUMENT MILLIMETRES
//  at the ISOM base scale (1:15 000). The contour curves are transcribed EXACTLY
//  from the reference SVG path data (parsed below) so the example matches the
//  official figure node-for-node; they are styled with the EXACT IOF symbols
//  (contour 101 / index contour 102 line styles) so the stroke width/colour is the
//  real thing. Red dimension callouts + the blue "min." frame complete the plate.
//
//  Currently authored: 101 Contour, 112 Pit. Other symbols fall back to the older
//  SymbolExamples()/SymbolDims() path (the viewer handles that).
// ─────────────────────────────────────────────────────────────────────────────

namespace App::Modules::IofMapping {

using Renderer::Shape;
using Renderer::Vec2;
using Renderer::Node;

namespace {

// ── Minimal SVG path parser ──────────────────────────────────────────────────
// Parses the subset of the SVG path grammar used by the reference figures —
// M/m (move), L/l (line), C/c (cubic), Z/z (close), with implicit repeats — into
// a list of cubic Bézier NODES (absolute SVG units). Numbers may be packed without
// separators ("1.2-.3" → 1.2, -0.3), as the exported SVGs do. Each output Node
// carries absolute in/out handles (a straight segment gets none on that side).
struct SvgPath { std::vector<Node> nodes; bool closed = false; };

// Read the next float from `p`, advancing past it and any leading separators.
double NextNum(const char*& p) {
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    p = (end > p) ? end : p + 1;
    return v;
}
bool MoreNums(const char* p) {
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return (*p == '-' || *p == '+' || *p == '.' || (*p >= '0' && *p <= '9'));
}

SvgPath ParseSvgPath(const char* d) {
    SvgPath out;
    Vec2 cur{0,0}, start{0,0};
    char cmd = 0;
    auto pushAnchor = [&](Vec2 pos) {
        Node n(pos); n.mode = Renderer::HandleMode::Free;
        n.hasIn = n.hasOut = false; out.nodes.push_back(n);
    };
    const char* p = d;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
        if (!*p) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) { cmd = *p++; }
        const bool rel = (cmd >= 'a');
        char c = (char)std::toupper((unsigned char)cmd);
        if (c == 'M') {
            float x = (float)NextNum(p), y = (float)NextNum(p);
            cur = rel ? Vec2{cur.x+x, cur.y+y} : Vec2{x,y};
            start = cur; pushAnchor(cur);
            cmd = rel ? 'l' : 'L';                 // subsequent pairs are line-tos
        } else if (c == 'L') {
            float x = (float)NextNum(p), y = (float)NextNum(p);
            cur = rel ? Vec2{cur.x+x, cur.y+y} : Vec2{x,y};
            pushAnchor(cur);
        } else if (c == 'C') {
            float x1=(float)NextNum(p), y1=(float)NextNum(p);
            float x2=(float)NextNum(p), y2=(float)NextNum(p);
            float x =(float)NextNum(p), y =(float)NextNum(p);
            Vec2 h1 = rel ? Vec2{cur.x+x1,cur.y+y1} : Vec2{x1,y1};
            Vec2 h2 = rel ? Vec2{cur.x+x2,cur.y+y2} : Vec2{x2,y2};
            Vec2 np = rel ? Vec2{cur.x+x, cur.y+y } : Vec2{x, y};
            if (!out.nodes.empty()) { out.nodes.back().hOut = h1; out.nodes.back().hasOut = true; }
            Node n(np); n.mode = Renderer::HandleMode::Free;
            n.hIn = h2; n.hasIn = true; n.hasOut = false;
            out.nodes.push_back(n);
            cur = np;
        } else if (c == 'Z') {
            out.closed = true; cur = start;
        } else {
            // Unsupported command (H/V/S/Q…) — skip its numbers defensively.
            while (MoreNums(p)) NextNum(p);
        }
        if (c != 'Z' && !MoreNums(p) && *p && !((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z'))) ++p;
    }
    return out;
}

// Build a contour-style line shape (using IOF symbol `code` for the stroke style)
// whose geometry is the parsed SVG path, converted SVG-units → plate mm by `k` and
// offset so the figure centres at (ox,oy). `flipY` keeps the SVG's y-down sense.
Shape FromSvg(int code, float scale, const char* d, float k, Vec2 origin, Vec2 off) {
    const IofElement* e = IofFindByCode(code);
    Shape s = e ? BuildSymbolShape(*e, scale) : Shape{};
    if (s.parts.empty()) return s;
    SvgPath sp = ParseSvgPath(d);
    auto& part = s.parts[0];
    part.path.nodes.clear();
    part.path.closed = sp.closed;
    part.path.subStart.clear();
    auto map = [&](Vec2 v){ return Vec2{ (v.x - origin.x) * k + off.x,
                                         (v.y - origin.y) * k + off.y }; };
    for (Node n : sp.nodes) {
        Node m(map(n.pos));
        m.mode = Renderer::HandleMode::Free;
        m.hasIn = n.hasIn; m.hasOut = n.hasOut;
        if (n.hasIn)  m.hIn  = map(n.hIn);
        if (n.hasOut) m.hOut = map(n.hOut);
        part.path.nodes.push_back(m);
    }
    return s;
}

}  // namespace

// ── 101 Contour plate — transcribed EXACTLY from "101 Contour.svg" ───────────
// The contour curves are the literal SVG path data (parsed above), converted from
// SVG units to plate mm by k = 0.14 mm / 0.8 px (the figure's contour stroke). The
// 0.8 px strokes are ordinary contours (101); the 1.42 px strokes are index
// contours (102). The figure origin (centring) is the bbox of the upper contour
// group. The lower blue "min." frame holds the knoll + depression sub-figure.
static SymbolPlate Plate101(float sc) {
    SymbolPlate pl;
    const float k = (0.14f / 0.8f) * sc;        // SVG px → plate mm (× the map scale)
    const Vec2 org{ 87.4f, 73.5f };             // centre of the upper contour group
    const Vec2 off{ 0.0f, 0.0f };
    auto add101 = [&](const char* d){ pl.shapes.push_back(FromSvg(1010, sc, d, k, org, off)); };
    auto add102 = [&](const char* d){ pl.shapes.push_back(FromSvg(1020, sc, d, k, org, off)); };

    // Ordinary contours (0.8 px in the SVG).
    add101("m48.31,127.93c0-4.18-.22-9.41,2.76-11.98,2.98-2.57,5.39-2.96,7.18-1.54,4.44,3.51,9.33,6.64,13.02,8.22,4.79,2.05,7.96,2.85,13.15,2.31,4.48-.47,12.21-1.13,11.08,3.23-1.4,5.37-5.44,14.44-9.46,10.61-4.76-4.54-9.34-5.35-15.91-5.53-6.67-.19-11.04,5.65-17.71,5.65-2.75,0-4.09-8.2-4.09-10.96Z");
    add101("m48.13,37.11c6.47.1,12.35,2.3,17.55-1.55,1.38-1.02,2.66-3.43,1.15-4.24-1.68-.9-3.1-1.85-3.1-3.75,0-1.54,1.89-1.9,3.43-1.96,3.31-.12,7.24-1.5,8.32,1.63,1.35,3.92,3.56,7.89,7.67,7.34,6.06-.81,9.38-2.46,15.5-2.28");
    add101("m48.3,99.26c3.49-2.77,10.35-6.18,12.48-2.27,2.79,5.13,8.51,9.33,13.54,6.36,4.51-2.67,9.36-4.51,13.38-1.14,3.66,3.07,7.25,5.91,11.58,3.91");
    add101("m48.2,30.31c4.59,0,6.72-3.15,10.73-5.38,3.69-2.05,6.47-3.03,10.6-2.12,10.56,2.32,17.06,4.95,27.57,2.45l1.71-.56");
    add101("m47.92,108.41c6.33-.97,11.64-.63,17.7,1.48,5.19,1.8,8.7.52,14.03-.81,2.8-.7,4.75.22,7.18,1.79,4.12,2.67,8.09,2.64,11.77-.61");
    add101("m48.13,44.52c9.01.09,25.01,3.84,23.59-5.06-.42-2.64-2.67-7.04,0-6.85,2.88.21,4.28,2.39,5.38,5.06,1.05,2.55,1.17,5.62,3.91,5.87,2.34.21,3.31-1.93,4.57-3.92,2.74-4.31,7.94-.77,13.05-.98");
    add101("m48.34,61.67c4.83.12,9.62-.18,13.62,2.72,3.96,2.88,6.03.26,8.02-2.12,2.45-2.92,5.25-4.59,6.79-2.09,1.92,3.11,2.38,5.33,4.89,7.99,1.72,1.81,5.62,1.73,6.36-.65,1.25-4,.27-9.01,4.4-8.32,2.59.43,2.04,3.69,1.9,6.33-.38,6.9-3.38,4.88-6.41,6.44-2.99,1.54-1.8,6.38-5.65,5.91-4.2-.51-7.76.75-11.76.9-1.8.07-3.58-.89-4.01-2.64-1.38-5.64-3.06-5.96-5.93-4.94-2.8,1-7.24,2.8-12.39,3.98");
    add101("m48.3,65.09c3.7.05,11.54-.37,11.11,2.51-.26,1.69-7.61,3.93-11.22,4.55");
    add101("m48.13,56.02c4.14-.07,10.01.06,12.34,2.08,4.18,3.63,6.13,2.11,8.97-1.25,1.6-1.89,4.18-5.17,5.57-3.12,2.12,3.13,3.98,6.1,6.22,9.15,1.77,2.4,4.87,2.68,5.75-1.64.87-4.23,2.17-8.64,6.35-8.17,2.03.23,4.31,5.35,3.7,12.65-1.53,13.78-5.11,9.84-7.61,12.68-2.33,2.65-3.26,3.09-6.77,2.81-4.39-.35-6.14,5.19-10.52,5.65-3.49.36-5.86-7.66-8.79-9.6-3.55-2.35-10.66.88-15.21,3.71");
    // Index contours (1.42 px in the SVG).
    add102("m48.13,16.7c8.42-3.19,7.21-2.61,13.36-7.11,6.01-4.78,8.45,1.51,14.23.84,5.78-.68,7.85,8.25,22.27,3.75");
    add102("m48.2,89.59c4.61-1.77,6.83-3.6,9.6-7.77,1.62-2.44,4.83-.74,4.41,2.87-.51,4.44-1.01,6.81.03,8.98,1.45,3.03,4.31,4.17,7.67,3.92,4.23-.32,7.84-.96,9.78-4.73,1.19-2.31,3.68-6.89,6.05-3.56,2.36,3.31,1.98,9.3,6.51,9.93,2.23.31,4.47-6.7,5.32-11.26,1.25-6.32,1.8-11.15,2.11-14.14,1.2-11.43-1.05-24.11-5.21-25.28-6.51-1.84-9.65,4.02-14.77-.41-1.81-1.56-1.85-4.4-4.24-4.4s-3.06,2.49-4.41,4.57c-4.59,7.08-14.51,2.21-22.94,1.71");

    // ── lower "min." sub-figure: the knoll + depression in their blue frame ──
    // The SVG draws it clipped to rect (49.32,161.7)+(52.16,72). Transcribe its
    // paths with the same k, but a separate origin so it sits below the contours.
    const Vec2 org2{ 75.4f, 197.5f };
    const Vec2 off2{ 0.0f, 30.0f * sc };
    auto addLo = [&](const char* d){ pl.shapes.push_back(FromSvg(1010, sc, d, k, org2, off2)); };
    addLo("m67.85,175.09c2.24.03,3.34.2,5.5.14,1.36-.04.1-1.12,1.75-1.19,1.57-.06.7,1.07,1.37,1.07,1.43,0,5.58-.11,5.72-.1");  // contour with a knoll bump
    addLo("m60.7,195.76c-.03-1,.61-1.49,2.14-1.46,1.39.02,2.23.5,2.23,1.41,0,.98-.8,1.42-2.29,1.43-1.41.01-2.05-.48-2.07-1.37Z");  // small knoll loop
    addLo("m60.37,217.37c-.04-1.4.76-2.08,2.66-2.05,1.73.03,2.78.7,2.78,1.97,0,1.37-1,1.98-2.85,2-1.76.02-2.56-.67-2.59-1.92Z");  // depression loop
    // Blue "min." frame (the SVG rect 49.89,162.27 + 51.02×70.87).
    pl.frames.push_back({ Vec2{ (49.89f - org2.x)*k + off2.x, (162.27f - org2.y)*k + off2.y },
                          Vec2{ 51.02f * k, 70.87f * k } });

    // ── red dimension callouts (from the SVG's red texts) ──
    auto rmm = [&](Vec2 svg){ return Vec2{ (svg.x - org.x)*k + off.x, (svg.y - org.y)*k + off.y }; };
    auto rmm2 = [&](Vec2 svg){ return Vec2{ (svg.x - org2.x)*k + off2.x, (svg.y - org2.y)*k + off2.y }; };
    PlateDim d;
    d.labelPos = rmm({108.3f, 38.98f}); d.mm = 0.14f; d.label = "0.14"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm({104.26f, 60.93f}); d.label = "0.4 (OM)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({73.35f, 170.5f}); d.label = "0.25 (CC)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({64.24f, 185.93f}); d.label = "0.5 (CC)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({51.55f, 207.43f}); d.label = "0.9 (OM)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({51.63f, 229.16f}); d.label = "1.1 (OM)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({76.04f, 197.81f}); d.label = "0.6 (OM)"; d.withSpan = false; pl.dims.push_back(d);
    d = {}; d.labelPos = rmm2({76.04f, 219.54f}); d.label = "0.7 (OM)"; d.withSpan = false; pl.dims.push_back(d);
    return pl;
}

// ── 112 Pit plate ────────────────────────────────────────────────────────────
// The pit glyph (a V opening up, oriented north) with the precise cotes: 0.7 mm
// width (OM), 0.8 mm height (OM), 0.18 mm line thickness — plus the north arrow.
static SymbolPlate Plate112(float s) {
    SymbolPlate pl;
    const IofElement* e = IofFindByCode(1120);
    if (e) pl.shapes.push_back(BuildSymbolShape(*e, s));

    // Red dimension callouts (mm): width 0.7, height 0.8, thickness 0.18.
    PlateDim d;
    d.a = { -0.35f * s, -0.42f * s }; d.b = { 0.35f * s, -0.42f * s };
    d.labelPos = { 0.9f * s, -0.5f * s }; d.mm = 0.7f; d.label = "0.7 (OM)";
    pl.dims.push_back(d);
    d = {}; d.a = { 0.45f * s, -0.40f * s }; d.b = { 0.45f * s, 0.40f * s };
    d.labelPos = { 1.2f * s, 0.45f * s }; d.mm = 0.8f; d.label = "0.8 (OM)";
    pl.dims.push_back(d);
    d = {}; d.labelPos = { 1.6f * s, 0.0f }; d.mm = 0.18f; d.withSpan = false;
    pl.dims.push_back(d);
    return pl;
}

SymbolPlate SymbolPlateFor(const IofElement& e, float scale) {
    const float s = (scale > 0.01f) ? scale : 1.0f;
    switch (e.code) {
        case 1010: case 1020: return Plate101(s);   // contour / index contour share it
        case 1120: return Plate112(s);              // pit
        default: break;
    }
    return {};   // no hand-authored plate → caller falls back to SymbolExamples
}

}  // namespace App::Modules::IofMapping

#include "AcuFile.h"

#include <algorithm>
#include <cstring>
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
//  .acu v2 wire format (docs/acu-format.md). Little-endian throughout.
//  Strings = u32 length + UTF-8 bytes. Doubles = IEEE-754 64-bit.
//
//  Container:  [MAGIC 'ACU1'][version u32 = 2][section]*
//  Section:    [tag u32][byteLength u32][payload]
//    META  — app name, project name, module id
//    DOC   — the Ink document (required; own version below)
//    LAY   — the ZoneLayout blob (opaque, self-versioned)
//    THMB  — [pngLen u32][PNG][pageId u64][x,y,w,h f32]  (written LAST so the
//            shell provider finds it by walking sections; it reads pngLen+PNG
//            only — the identical v1 frame)
//
//  DOC (docVersion 1 — the Ink model; the legacy v1 DOC died with container 1):
//    [docVersion u32][nextId u64]
//    [pageCount u32]   page*       (id, name, pos/size f64, background, children)
//    [nodeCount u32]   node*       (full Node: kind/name/tree links/parentId/
//                                   transform/flags/opacity/blend/children/
//                                   targetRef/path/style/modifiers)
//    [collCount u32]   collection* (id, name, colorTag, visible, members, kids)
//  Nodes are written PRE-ORDER per page, so every reachable node appears once.
//  Decoders must read every version ≤ current and default new fields.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {
namespace {

constexpr std::uint32_t kMagic   = 0x31554341;   // 'ACU1'
constexpr std::uint32_t kTagMeta = 0x4154454D;   // 'META'
constexpr std::uint32_t kTagDoc  = 0x00434F44;   // 'DOC\0'
constexpr std::uint32_t kTagLay  = 0x0059414C;   // 'LAY\0'
constexpr std::uint32_t kTagEdst = 0x54534445;   // 'EDST' (editing session)
constexpr std::uint32_t kTagThmb = 0x424D4854;   // 'THMB'

// v2: Array modifier modes (arrayMode/lineMode/circle*) appended to the
//     modifier record; instance transform-copy flags ride bits 5-7 of the
//     node flags byte (v1 wrote them as 0 = the new default).
constexpr std::uint32_t kDocVersion = 20;  // v3: subpath spline params +
                                           //     anchor weight (+ dead marks)
                                           // v4-v8: generic marks (dead)
                                           // v9: marks — per-object side/offset +
                                           //     gap start/end sub-object lists
                                           // v10: mark-object opacity ONLY (a
                                           //      shipped intermediate build
                                           //      wrote v10 files with just it)
                                           // v11: stroke REPEAT runs + dash
                                           //      fit, mark anchorSize /
                                           //      repeatAnchor, new shapes,
                                           //      along-path modifier options
                                           // v12: document display-unit system
                                           // v13: Instanced fill (shapes +
                                           //      line-sets, grid/scatter)
                                           // v14: instanced scatter mode
                                           //      (count vs distance)
                                           // v15: node previewOnly flag +
                                           //      per-property lock bitmask
                                           // v16: document colour mode (RGB /
                                           //      CMYK) + stroke taper length
                                           // v17: line-set dash stagger
                                           // v18: line-set spacing mode
                                           //      (centre vs border) + stroke
                                           //      butt-cap tilt angle
                                           // v19: stroke align offset (+ its
                                           //      unit) and repeat tangent
                                           //      model (perpendicular vs
                                           //      smoothed)
                                           // v20: repeat trim measure (to the
                                           //      group centre or its edge)

// ── Writer: append-only little-endian byte vector ────────────────────────────
struct Writer {
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t v)  { bytes.push_back(v); }
    void u32(std::uint32_t v) {
        bytes.push_back((std::uint8_t)v);
        bytes.push_back((std::uint8_t)(v >> 8));
        bytes.push_back((std::uint8_t)(v >> 16));
        bytes.push_back((std::uint8_t)(v >> 24));
    }
    void u64(std::uint64_t v) {
        u32((std::uint32_t)v);
        u32((std::uint32_t)(v >> 32));
    }
    void f32(float v) {
        std::uint32_t b;
        std::memcpy(&b, &v, 4);
        u32(b);
    }
    void f64(double v) {
        std::uint64_t b;
        std::memcpy(&b, &v, 8);
        u64(b);
    }
    void str(const std::string& s) {
        u32((std::uint32_t)s.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    void raw(const std::uint8_t* p, std::size_t n) {
        bytes.insert(bytes.end(), p, p + n);
    }
};

// ── Reader: bounds-checked cursor (any overrun poisons `ok`) ─────────────────
struct Reader {
    const std::uint8_t* p   = nullptr;
    std::size_t         n   = 0;
    std::size_t         pos = 0;
    bool                ok  = true;

    bool Has(std::size_t k) {
        if (pos + k > n) ok = false;
        return ok;
    }
    std::uint8_t u8() {
        if (!Has(1)) return 0;
        return p[pos++];
    }
    std::uint32_t u32() {
        if (!Has(4)) return 0;
        std::uint32_t v = (std::uint32_t)p[pos] | ((std::uint32_t)p[pos + 1] << 8) |
                          ((std::uint32_t)p[pos + 2] << 16) |
                          ((std::uint32_t)p[pos + 3] << 24);
        pos += 4;
        return v;
    }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        const std::uint64_t hi = u32();
        return lo | (hi << 32);
    }
    float f32() {
        const std::uint32_t b = u32();
        float v;
        std::memcpy(&v, &b, 4);
        return v;
    }
    double f64() {
        const std::uint64_t b = u64();
        double v;
        std::memcpy(&v, &b, 8);
        return v;
    }
    std::string str() {
        const std::uint32_t len = u32();
        if (!Has(len)) return {};
        std::string s((const char*)p + pos, len);
        pos += len;
        return s;
    }
};

// ── DOC encode ────────────────────────────────────────────────────────────────

void WriteTransform(Writer& w, const Ink::Transform2D& t) {
    w.f64(t.tx); w.f64(t.ty);
    w.f64(t.sx); w.f64(t.sy);
    w.f64(t.rotation);
}
Ink::Transform2D ReadTransform(Reader& r) {
    Ink::Transform2D t;
    t.tx = r.f64(); t.ty = r.f64();
    t.sx = r.f64(); t.sy = r.f64();
    t.rotation = r.f64();
    return t;
}

void WriteColor(Writer& w, const Ink::Color& c) {
    w.f32(c.r); w.f32(c.g); w.f32(c.b); w.f32(c.a);
}
Ink::Color ReadColor(Reader& r) {
    Ink::Color c;
    c.r = r.f32(); c.g = r.f32(); c.b = r.f32(); c.a = r.f32();
    return c;
}

void WritePath(Writer& w, const Ink::PathData& p) {
    w.u32((std::uint32_t)p.subpaths.size());
    for (const Ink::Subpath& sp : p.subpaths) {
        w.u8(sp.closed ? 1 : 0);
        // v3: the spline model (Bezier / Nurbs / Poly + NURBS params).
        w.u8((std::uint8_t)sp.spline);
        w.u8(sp.orderU);
        w.u8((sp.nurbsEndpoint ? 1 : 0) | (sp.nurbsBezier ? 2 : 0));
        w.u32((std::uint32_t)sp.anchors.size());
        for (const Ink::Anchor& a : sp.anchors) {
            w.f64(a.pos.x); w.f64(a.pos.y);
            w.f64(a.in.x);  w.f64(a.in.y);
            w.f64(a.out.x); w.f64(a.out.y);
            w.u8((std::uint8_t)((a.hasIn ? 1 : 0) | (a.hasOut ? 2 : 0) |
                                ((std::uint8_t)a.kind << 2)));
            w.f64(a.weight);   // v3
        }
    }
}
Ink::PathData ReadPath(Reader& r, std::uint32_t ver) {
    Ink::PathData p;
    const std::uint32_t nSub = r.u32();
    for (std::uint32_t i = 0; i < nSub && r.ok; ++i) {
        Ink::Subpath sp;
        sp.closed = r.u8() != 0;
        if (ver >= 3) {
            sp.spline = (Ink::SplineType)std::min<std::uint8_t>(r.u8(), 2);
            sp.orderU = r.u8();
            const std::uint8_t sf = r.u8();
            sp.nurbsEndpoint = (sf & 1) != 0;
            sp.nurbsBezier   = (sf & 2) != 0;
        }
        const std::uint32_t nA = r.u32();
        for (std::uint32_t j = 0; j < nA && r.ok; ++j) {
            Ink::Anchor a;
            a.pos.x = r.f64(); a.pos.y = r.f64();
            a.in.x  = r.f64(); a.in.y  = r.f64();
            a.out.x = r.f64(); a.out.y = r.f64();
            const std::uint8_t flags = r.u8();
            a.hasIn  = (flags & 1) != 0;
            a.hasOut = (flags & 2) != 0;
            a.kind   = (Ink::AnchorKind)std::min<std::uint8_t>((flags >> 2) & 3, 2);
            if (ver >= 3) a.weight = r.f64();
            sp.anchors.push_back(a);
        }
        p.subpaths.push_back(std::move(sp));
    }
    return p;
}

// A mark OBJECT (v9): recursive — a Gap object carries start/end marker object
// lists. `depth` guards against pathological nesting.
void WriteMarkObject(Writer& w, const Ink::MarkObject& o, int depth = 0) {
    w.u8((std::uint8_t)o.shape);
    w.u8((std::uint8_t)o.mode);
    w.u8((std::uint8_t)o.bend);
    w.u8((std::uint8_t)o.blend);
    w.f64(o.size);
    w.f64(o.width);
    w.u8(o.sizePercent ? 1 : 0);
    w.f64(o.rotation);
    w.f64(o.alongOffset);
    w.u8(o.sideInherit ? 1 : 0);
    w.u8((std::uint8_t)o.side);
    w.f64(o.sideOffset);
    w.u64(o.nodeRef);
    w.u8(o.front ? 1 : 0);
    w.u8((std::uint8_t)o.gapStart);
    w.u8((std::uint8_t)o.gapEnd);
    w.u8(o.gapCutsObjects ? 1 : 0);
    WriteColor(w, o.color);
    w.u8(o.useStrokeColor ? 1 : 0);
    w.f32(o.opacity);   // v10
    const bool nest = depth < 2;
    w.u32(nest ? (std::uint32_t)o.gapStartObjects.size() : 0);
    if (nest) for (const Ink::MarkObject& c : o.gapStartObjects) WriteMarkObject(w, c, depth + 1);
    w.u32(nest ? (std::uint32_t)o.gapEndObjects.size() : 0);
    if (nest) for (const Ink::MarkObject& c : o.gapEndObjects) WriteMarkObject(w, c, depth + 1);
}
Ink::MarkObject ReadMarkObject(Reader& r, std::uint32_t ver, int depth = 0) {
    Ink::MarkObject o;
    o.shape = (Ink::MarkShape)std::min<std::uint8_t>(r.u8(),
                                                     Ink::kMarkShapeMax);
    o.mode  = (Ink::MarkObjectMode)std::min<std::uint8_t>(r.u8(), 2);
    o.bend  = (Ink::MarkBend)std::min<std::uint8_t>(r.u8(), Ink::kMarkBendMax);
    o.blend = (Ink::BlendMode)std::min<std::uint8_t>(r.u8(),
                                                     (std::uint8_t)Ink::BlendMode::Erase);
    o.size = r.f64();
    o.width = r.f64();
    o.sizePercent = r.u8() != 0;
    o.rotation = r.f64();
    o.alongOffset = r.f64();
    o.sideInherit = r.u8() != 0;
    o.side = (Ink::MarkSide)std::min<std::uint8_t>(r.u8(), 2);
    o.sideOffset = r.f64();
    o.nodeRef = r.u64();
    o.front = r.u8() != 0;
    o.gapStart = (Ink::GapCap)std::min<std::uint8_t>(r.u8(), 2);
    o.gapEnd   = (Ink::GapCap)std::min<std::uint8_t>(r.u8(), 2);
    o.gapCutsObjects = r.u8() != 0;
    o.color = ReadColor(r);
    o.useStrokeColor = r.u8() != 0;
    if (ver >= 10) o.opacity = r.f32();
    const std::uint32_t ns = r.u32();
    for (std::uint32_t i = 0; i < ns && r.ok; ++i)
        o.gapStartObjects.push_back(ReadMarkObject(r, ver, depth + 1));
    const std::uint32_t ne = r.u32();
    for (std::uint32_t i = 0; i < ne && r.ok; ++i)
        o.gapEndObjects.push_back(ReadMarkObject(r, ver, depth + 1));
    return o;
}

// ── Stroke repeat run (shared by strokes and instanced-fill line-sets) ────────
void WriteStrokeRepeat(Writer& w, const Ink::StrokeRepeat& rp) {
    w.u8(rp.enabled ? 1 : 0);
    w.u8((std::uint8_t)rp.shape);
    w.u8((std::uint8_t)rp.mode);
    w.u8((std::uint8_t)rp.blend);
    w.f64(rp.size);
    w.f64(rp.width);
    w.u8(rp.sizePercent ? 1 : 0);
    w.f64(rp.rotation);
    w.u8((std::uint8_t)rp.side);
    w.f64(rp.sideOffset);
    w.u8(rp.offsetPercent ? 1 : 0);
    w.u8((std::uint8_t)rp.distribute);
    w.f64(rp.pitch);
    w.f64(rp.gap);
    w.u32((std::uint32_t)rp.count);
    w.f64(rp.density);
    w.f64(rp.phase);
    w.u32((std::uint32_t)rp.groupCount);
    w.f64(rp.groupPitch);
    w.f64(rp.startTrim);
    w.f64(rp.endTrim);
    w.u8((std::uint8_t)rp.fit);
    w.u8(rp.lineJoin ? 1 : 0);
    w.u8(rp.lineClip ? 1 : 0);
    WriteColor(w, rp.color);
    w.u8(rp.useStrokeColor ? 1 : 0);
    w.f32(rp.opacity);
    w.u8((std::uint8_t)rp.orient);        // v19
    w.u8((std::uint8_t)rp.trimMeasure);   // v20
}
Ink::StrokeRepeat ReadStrokeRepeat(Reader& r, std::uint32_t ver) {
    Ink::StrokeRepeat rp;
    rp.enabled = r.u8() != 0;
    rp.shape = (Ink::MarkShape)std::min<std::uint8_t>(r.u8(), Ink::kMarkShapeMax);
    rp.mode  = (Ink::MarkObjectMode)std::min<std::uint8_t>(r.u8(), 2);
    rp.blend = (Ink::BlendMode)std::min<std::uint8_t>(
        r.u8(), (std::uint8_t)Ink::BlendMode::Erase);
    rp.size = r.f64();
    rp.width = r.f64();
    rp.sizePercent = r.u8() != 0;
    rp.rotation = r.f64();
    rp.side = (Ink::RepeatSide)std::min<std::uint8_t>(r.u8(), 4);
    rp.sideOffset = r.f64();
    rp.offsetPercent = r.u8() != 0;
    rp.distribute = (Ink::RepeatDistribute)std::min<std::uint8_t>(r.u8(), 3);
    rp.pitch = r.f64();
    rp.gap = r.f64();
    rp.count = (int)r.u32();
    rp.density = r.f64();
    rp.phase = r.f64();
    rp.groupCount = (int)r.u32();
    rp.groupPitch = r.f64();
    rp.startTrim = r.f64();
    rp.endTrim = r.f64();
    rp.fit = (Ink::DashFit)std::min<std::uint8_t>(r.u8(), 2);
    rp.lineJoin = r.u8() != 0;
    rp.lineClip = r.u8() != 0;
    rp.color = ReadColor(r);
    rp.useStrokeColor = r.u8() != 0;
    rp.opacity = r.f32();
    // v19 introduced the tangent model. A file written before it was drawn with
    // the smoothed one, so that is what it must keep reading back as.
    rp.orient = ver >= 19
        ? (Ink::MarkOrient)std::min<std::uint8_t>(r.u8(), 1)
        : Ink::MarkOrient::Smoothed;
    if (ver >= 20)
        rp.trimMeasure =
            (Ink::RepeatTrimMeasure)std::min<std::uint8_t>(r.u8(), 1);
    return rp;
}

// ── Instanced fill (v13): elements, line-sets, layout ─────────────────────────
void WriteInstElement(Writer& w, const Ink::InstElement& e) {
    w.u8((std::uint8_t)e.shape);
    w.f64(e.sizeA); w.f64(e.sizeB); w.f64(e.sizeC);
    w.f64(e.rotation);
    w.u8((std::uint8_t)e.mode);
    w.u8(e.useFillColor ? 1 : 0);
    WriteColor(w, e.color);
    w.f32(e.opacity);
    w.u8(e.enabled ? 1 : 0);
}
Ink::InstElement ReadInstElement(Reader& r) {
    Ink::InstElement e;
    e.shape = (Ink::InstShape)std::min<std::uint8_t>(r.u8(), Ink::kInstShapeMax);
    e.sizeA = r.f64(); e.sizeB = r.f64(); e.sizeC = r.f64();
    e.rotation = r.f64();
    e.mode = (Ink::MarkObjectMode)std::min<std::uint8_t>(r.u8(), 2);
    e.useFillColor = r.u8() != 0;
    e.color = ReadColor(r);
    e.opacity = r.f32();
    e.enabled = r.u8() != 0;
    return e;
}
void WriteInstLineSet(Writer& w, const Ink::InstLineSet& l) {
    w.u8(l.enabled ? 1 : 0);
    w.f64(l.angle);
    w.f64(l.spacing);
    w.f64(l.phase);
    w.f64(l.stagger);            // v17
    w.u8((std::uint8_t)l.spacingMode);   // v18
    w.u8((std::uint8_t)l.mode);
    w.u8(l.useFillColor ? 1 : 0);
    WriteColor(w, l.color);
    // The line style: only the fields a straight line uses.
    w.f64(l.line.width);
    w.u8((std::uint8_t)l.line.cap);
    w.u32((std::uint32_t)l.line.dashPattern.size());
    for (double d : l.line.dashPattern) w.f64(d);
    w.f64(l.line.dashOffset);
    w.u8((std::uint8_t)l.line.dashFit);
    w.u32((std::uint32_t)l.line.repeats.size());
    for (const Ink::StrokeRepeat& rp : l.line.repeats) WriteStrokeRepeat(w, rp);
}
Ink::InstLineSet ReadInstLineSet(Reader& r, std::uint32_t ver) {
    Ink::InstLineSet l;
    l.enabled = r.u8() != 0;
    l.angle = r.f64();
    l.spacing = r.f64();
    l.phase = r.f64();
    if (ver >= 17) l.stagger = r.f64();
    if (ver >= 18)
        l.spacingMode = (Ink::InstLineSpacing)std::min<std::uint8_t>(r.u8(), 1);
    l.mode = (Ink::MarkObjectMode)std::min<std::uint8_t>(r.u8(), 2);
    l.useFillColor = r.u8() != 0;
    l.color = ReadColor(r);
    l.line.width = r.f64();
    l.line.cap = (Ink::CapStyle)std::min<std::uint8_t>(r.u8(), 3);
    const std::uint32_t nD = r.u32();
    for (std::uint32_t j = 0; j < nD && r.ok; ++j)
        l.line.dashPattern.push_back(r.f64());
    l.line.dashOffset = r.f64();
    l.line.dashFit = (Ink::DashFit)std::min<std::uint8_t>(r.u8(), 2);
    const std::uint32_t nR = r.u32();
    for (std::uint32_t j = 0; j < nR && r.ok; ++j)
        l.line.repeats.push_back(ReadStrokeRepeat(r, ver));
    return l;
}
void WriteInstancedFill(Writer& w, const Ink::InstancedFill& in) {
    w.u8((std::uint8_t)in.layout);
    w.u8((std::uint8_t)in.scatterMode);   // v14
    w.u32((std::uint32_t)in.gridAxes);
    for (int i = 0; i < 3; ++i) w.f64(in.spacing[i]);
    for (int i = 0; i < 3; ++i) w.f64(in.axisAngle[i]);
    w.u32((std::uint32_t)in.scatterCount);
    w.f64(in.scatterMinDist);
    w.f64(in.scatterMaxDist);
    w.u8(in.avoidCollisions ? 1 : 0);
    w.f64(in.posJitter);
    w.f64(in.rotJitter);
    w.u32(in.seed);
    w.f64(in.rotation);
    w.u8((std::uint8_t)in.clip);
    w.u8((std::uint8_t)in.anchor);
    w.u32((std::uint32_t)in.elements.size());
    for (const Ink::InstElement& e : in.elements) WriteInstElement(w, e);
    w.u32((std::uint32_t)in.lines.size());
    for (const Ink::InstLineSet& l : in.lines) WriteInstLineSet(w, l);
}
Ink::InstancedFill ReadInstancedFill(Reader& r, std::uint32_t ver) {
    Ink::InstancedFill in;
    in.layout = (Ink::InstLayout)std::min<std::uint8_t>(r.u8(), 1);
    if (ver >= 14)
        in.scatterMode = (Ink::InstScatterMode)std::min<std::uint8_t>(r.u8(), 1);
    in.gridAxes = (int)r.u32();
    for (int i = 0; i < 3; ++i) in.spacing[i] = r.f64();
    for (int i = 0; i < 3; ++i) in.axisAngle[i] = r.f64();
    in.scatterCount = (int)r.u32();
    in.scatterMinDist = r.f64();
    in.scatterMaxDist = r.f64();
    in.avoidCollisions = r.u8() != 0;
    in.posJitter = r.f64();
    in.rotJitter = r.f64();
    in.seed = r.u32();
    in.rotation = r.f64();
    in.clip   = (Ink::PatternClip)std::min<std::uint8_t>(r.u8(), 3);
    in.anchor = (Ink::PatternAnchor)std::min<std::uint8_t>(r.u8(), 1);
    const std::uint32_t nE = r.u32();
    for (std::uint32_t i = 0; i < nE && r.ok; ++i)
        in.elements.push_back(ReadInstElement(r));
    const std::uint32_t nL = r.u32();
    for (std::uint32_t i = 0; i < nL && r.ok; ++i)
        in.lines.push_back(ReadInstLineSet(r, ver));
    return in;
}

void WriteStyle(Writer& w, const Ink::Style& s) {
    w.u32((std::uint32_t)s.fills.size());
    for (const Ink::Fill& f : s.fills) {
        w.u8((std::uint8_t)f.kind);
        w.u8(f.enabled ? 1 : 0);
        w.u8((std::uint8_t)f.rule);
        w.f32(f.opacity);
        WriteColor(w, f.paint.color);
        w.u64(f.pattern.motifRef);
        w.f64(f.pattern.spacingX); w.f64(f.pattern.spacingY);
        w.f64(f.pattern.phaseX);   w.f64(f.pattern.phaseY);
        w.f64(f.pattern.rotation); w.f64(f.pattern.motifRotation);
        w.f64(f.pattern.scale);
        w.u8((std::uint8_t)f.pattern.clip);
        w.u8((std::uint8_t)f.pattern.anchor);
        WriteInstancedFill(w, f.instanced);   // v13
    }
    w.u32((std::uint32_t)s.strokes.size());
    for (const Ink::Stroke& st : s.strokes) {
        w.u8(st.enabled ? 1 : 0);
        WriteColor(w, st.paint.color);
        w.f64(st.width);
        w.u8((std::uint8_t)st.align);
        w.u8((std::uint8_t)st.cap);
        w.u8((std::uint8_t)st.join);
        w.f64(st.miterLimit);
        w.f64(st.taperLength);            // v16
        w.f64(st.capAngle);               // v18
        w.f64(st.alignOffset);            // v19
        w.u8(st.alignOffsetPercent ? 1 : 0);
        w.u8((std::uint8_t)st.widthSpace);
        w.u32((std::uint32_t)st.dashPattern.size());
        for (double d : st.dashPattern) w.f64(d);
        w.f64(st.dashOffset);
        w.u8((std::uint8_t)st.dashFit);   // v10
        // v5: the generic stroke marks (phase / side / offset + unit + a list
        // of objects — SVG-marker shapes or node instances; Fusion / Blend /
        // Cut mode, Hard / Bend, Rectangle length+width).
        w.u32((std::uint32_t)st.marks.size());
        for (const Ink::StrokeMark& m : st.marks) {
            w.u32((std::uint32_t)m.sub);
            w.f64(m.t);
            w.u8((std::uint8_t)m.phase);
            w.u8((std::uint8_t)m.side);
            w.f64(m.offset);
            w.u8(m.offsetPercent ? 1 : 0);
            w.u32((std::uint32_t)m.nodeAnchor);
            w.f64(m.anchorSize);              // v11
            w.u8((std::uint8_t)m.repeatAnchor);
            w.f64(m.repeatGap);               // v11
            w.u32((std::uint32_t)m.objects.size());
            for (const Ink::MarkObject& o : m.objects) WriteMarkObject(w, o);
        }
        // v11: the stroke REPEAT runs.
        w.u32((std::uint32_t)st.repeats.size());
        for (const Ink::StrokeRepeat& rp : st.repeats) WriteStrokeRepeat(w, rp);
    }
}
Ink::Style ReadStyle(Reader& r, std::uint32_t ver) {
    Ink::Style s;
    const std::uint32_t nF = r.u32();
    for (std::uint32_t i = 0; i < nF && r.ok; ++i) {
        Ink::Fill f;
        f.kind    = (Ink::FillKind)std::min<std::uint8_t>(r.u8(), 2);
        f.enabled = r.u8() != 0;
        f.rule    = (Ink::FillRule)std::min<std::uint8_t>(r.u8(), 1);
        f.opacity = r.f32();
        f.paint.color = ReadColor(r);
        f.pattern.motifRef = r.u64();
        f.pattern.spacingX = r.f64(); f.pattern.spacingY = r.f64();
        f.pattern.phaseX   = r.f64(); f.pattern.phaseY   = r.f64();
        f.pattern.rotation = r.f64(); f.pattern.motifRotation = r.f64();
        f.pattern.scale    = r.f64();
        f.pattern.clip   = (Ink::PatternClip)std::min<std::uint8_t>(r.u8(), 3);
        f.pattern.anchor = (Ink::PatternAnchor)std::min<std::uint8_t>(r.u8(), 1);
        if (ver >= 13) f.instanced = ReadInstancedFill(r, ver);
        s.fills.push_back(f);
    }
    const std::uint32_t nS = r.u32();
    for (std::uint32_t i = 0; i < nS && r.ok; ++i) {
        Ink::Stroke st;
        st.enabled     = r.u8() != 0;
        st.paint.color = ReadColor(r);
        st.width       = r.f64();
        st.align = (Ink::StrokeAlign)std::min<std::uint8_t>(
            r.u8(), Ink::kStrokeAlignMax);
        st.cap   = (Ink::CapStyle)std::min<std::uint8_t>(r.u8(), 3);
        st.join  = (Ink::JoinStyle)std::min<std::uint8_t>(r.u8(), 2);
        st.miterLimit = r.f64();
        if (ver >= 16) st.taperLength = r.f64();
        if (ver >= 18) st.capAngle    = r.f64();
        if (ver >= 19) {
            st.alignOffset = r.f64();
            st.alignOffsetPercent = r.u8() != 0;
        }
        st.widthSpace = (Ink::WidthSpace)std::min<std::uint8_t>(r.u8(), 1);
        const std::uint32_t nD = r.u32();
        for (std::uint32_t j = 0; j < nD && r.ok; ++j)
            st.dashPattern.push_back(r.f64());
        st.dashOffset = r.f64();
        if (ver >= 11)
            st.dashFit = (Ink::DashFit)std::min<std::uint8_t>(r.u8(), 2);
        if (ver == 3) {
            // v3 carried the OLD typed-mark layout (a dead pre-release format).
            // Drain its bytes so the stream stays aligned; not reconstructed.
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u8(); r.u32(); r.f64(); r.u32();       // kind/sub/t/side
                r.f64(); r.f64(); r.f64();               // gap/size/thickness
                r.u8(); r.u32();                          // flags/nodeAnchor
            }
        } else if (ver == 4) {
            // v4 was a first generic-mark layout (also pre-release) without the
            // offset unit / bend / rectangle width — drain it likewise.
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u32(); r.f64(); r.u8(); r.u8(); r.f64(); r.u32();
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k) {
                    r.u8(); r.u8(); r.u8(); r.f64(); r.f64(); r.u64();
                    ReadColor(r); r.u8();
                }
            }
        } else if (ver == 5) {
            // v5 was the first shipped generic-mark layout without the per-shape
            // size unit / front / third bend mode — drain it (also pre-release).
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u32(); r.f64(); r.u8(); r.u8(); r.f64(); r.u8(); r.u32();
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k) {
                    r.u8(); r.u8(); r.u8(); r.u8(); r.f64(); r.f64(); r.f64();
                    r.u64(); ReadColor(r); r.u8();
                }
            }
        } else if (ver == 6) {
            // v6 lacked the Gap shape + its caps — drain it (pre-release).
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u32(); r.f64(); r.u8(); r.u8(); r.f64(); r.u8(); r.u32();
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k) {
                    r.u8(); r.u8(); r.u8(); r.u8(); r.f64(); r.f64(); r.u8();
                    r.f64(); r.u64(); r.u8(); ReadColor(r); r.u8();
                }
            }
        } else if (ver == 7) {
            // v7 lacked the along-offset + gap end markers — drain it.
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u32(); r.f64(); r.u8(); r.u8(); r.f64(); r.u8(); r.u32();
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k) {
                    r.u8(); r.u8(); r.u8(); r.u8(); r.f64(); r.f64(); r.u8();
                    r.f64(); r.u64(); r.u8(); r.u8(); r.u8(); r.u8();
                    ReadColor(r); r.u8();
                }
            }
        } else if (ver == 8) {
            // v8 objects had no per-object side + gap sub-object lists (the gap
            // markers were a single shape) — drain it (pre-release).
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                r.u32(); r.f64(); r.u8(); r.u8(); r.f64(); r.u8(); r.u32();
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k) {
                    r.u8(); r.u8(); r.u8(); r.u8(); r.f64(); r.f64(); r.u8();
                    r.f64(); r.f64(); r.u64(); r.u8(); r.u8(); r.u8(); r.u8();
                    r.u8(); r.u8(); r.u64(); ReadColor(r); r.u8();
                }
            }
        } else if (ver >= 9) {
            const std::uint32_t nMk = r.u32();
            for (std::uint32_t j = 0; j < nMk && r.ok; ++j) {
                Ink::StrokeMark m;
                m.sub    = (std::int32_t)r.u32();
                m.t      = r.f64();
                m.phase  = (Ink::MarkPhase)std::min<std::uint8_t>(r.u8(), 2);
                m.side   = (Ink::MarkSide)std::min<std::uint8_t>(r.u8(), 2);
                m.offset = r.f64();
                m.offsetPercent = r.u8() != 0;
                m.nodeAnchor = (std::int32_t)r.u32();
                if (ver >= 11) {
                    m.anchorSize = r.f64();
                    m.repeatAnchor = (Ink::MarkRepeatAnchor)
                        std::min<std::uint8_t>(r.u8(), 2);
                    m.repeatGap = r.f64();
                }
                const std::uint32_t nObj = r.u32();
                for (std::uint32_t k = 0; k < nObj && r.ok; ++k)
                    m.objects.push_back(ReadMarkObject(r, ver));
                st.marks.push_back(m);
            }
        }
        if (ver >= 11) {   // stroke REPEAT runs
            const std::uint32_t nRp = r.u32();
            for (std::uint32_t j = 0; j < nRp && r.ok; ++j)
                st.repeats.push_back(ReadStrokeRepeat(r, ver));
        }
        s.strokes.push_back(std::move(st));
    }
    return s;
}

void WriteModifier(Writer& w, const Ink::Modifier& m) {
    // Fixed layout: every field of every kind (simple, forward-stable).
    w.u8((std::uint8_t)m.kind);
    w.u8(m.enabled ? 1 : 0);
    w.u32((std::uint32_t)m.count);
    WriteTransform(w, m.step);
    w.u8((std::uint8_t)m.stepSpace);
    w.u64(m.motifRef);
    w.u8((std::uint8_t)m.distribute);
    w.f64(m.spacing);
    w.u32((std::uint32_t)m.alongCount);
    w.u8((std::uint8_t)m.align);
    w.f64(m.startTrim);
    w.f64(m.endTrim);
    w.u8((std::uint8_t)m.op);
    w.u64(m.operandRef);
    // v2: the Array placement modes.
    w.u8((std::uint8_t)m.arrayMode);
    w.u8((std::uint8_t)m.lineMode);
    w.f64(m.circleRadius);
    w.u8((std::uint8_t)m.circleMethod);
    w.f64(m.circleAngleStep);
    w.u8(m.circleArc ? 1 : 0);
    w.f64(m.circleSweep);
    w.u8(m.circleAlign ? 1 : 0);
    // v11: the AlongPath content options (primitive shapes, groups, sides).
    w.u8((std::uint8_t)m.alongShape);
    w.f64(m.alongSize);
    w.f64(m.alongWidth);
    w.f64(m.alongRotation);
    w.u8((std::uint8_t)m.alongSide);
    w.f64(m.alongSideOffset);
    w.u32((std::uint32_t)m.alongGroupCount);
    w.f64(m.alongGroupPitch);
    w.f64(m.alongPhase);
    w.f64(m.alongGap);
    w.f64(m.alongDensity);
    w.u8((std::uint8_t)m.alongMode);
    w.u8((std::uint8_t)m.alongBlend);
    WriteColor(w, m.alongColor);
    w.f32(m.alongOpacity);
    w.u8(m.alongOffsetPercent ? 1 : 0);   // v11 (added late)
    w.f64(m.alongScale);
}
Ink::Modifier ReadModifier(Reader& r, std::uint32_t ver) {
    Ink::Modifier m;
    m.kind    = (Ink::ModifierKind)std::min<std::uint8_t>(r.u8(), 2);
    m.enabled = r.u8() != 0;
    m.count   = (int)r.u32();
    m.step    = ReadTransform(r);
    m.stepSpace = (Ink::ArrayStepSpace)std::min<std::uint8_t>(r.u8(), 1);
    m.motifRef  = r.u64();
    m.distribute = (Ink::AlongDistribute)std::min<std::uint8_t>(r.u8(), 2);
    m.spacing    = r.f64();
    m.alongCount = (int)r.u32();
    m.align      = (Ink::AlongAlign)std::min<std::uint8_t>(r.u8(), 1);
    m.startTrim  = r.f64();
    m.endTrim    = r.f64();
    m.op         = (Ink::BooleanOp)std::min<std::uint8_t>(r.u8(), 3);
    m.operandRef = r.u64();
    if (ver >= 2) {
        m.arrayMode = (Ink::ArrayMode)std::min<std::uint8_t>(r.u8(), 2);
        m.lineMode  = (Ink::ArrayLineMode)std::min<std::uint8_t>(r.u8(), 2);
        m.circleRadius    = r.f64();
        m.circleMethod =
            (Ink::ArrayCircleMethod)std::min<std::uint8_t>(r.u8(), 1);
        m.circleAngleStep = r.f64();
        m.circleArc       = r.u8() != 0;
        m.circleSweep     = r.f64();
        m.circleAlign     = r.u8() != 0;
    }
    if (ver >= 11) {
        m.alongShape = (Ink::MarkShape)std::min<std::uint8_t>(
            r.u8(), Ink::kMarkShapeMax);
        m.alongSize = r.f64();
        m.alongWidth = r.f64();
        m.alongRotation = r.f64();
        m.alongSide = (Ink::RepeatSide)std::min<std::uint8_t>(r.u8(), 4);
        m.alongSideOffset = r.f64();
        m.alongGroupCount = (int)r.u32();
        m.alongGroupPitch = r.f64();
        m.alongPhase = r.f64();
        m.alongGap = r.f64();
        m.alongDensity = r.f64();
        m.alongMode = (Ink::MarkObjectMode)std::min<std::uint8_t>(r.u8(), 2);
        m.alongBlend = (Ink::BlendMode)std::min<std::uint8_t>(
            r.u8(), (std::uint8_t)Ink::BlendMode::Erase);
        m.alongColor = ReadColor(r);
        m.alongOpacity = r.f32();
        m.alongOffsetPercent = r.u8() != 0;
        m.alongScale = r.f64();
    }
    return m;
}

void WriteNode(Writer& w, const Ink::Node& n) {
    w.u64(n.id);
    w.u8((std::uint8_t)n.kind);
    w.str(n.name);
    w.u64(n.parent);
    w.u64(n.page);
    w.u64(n.parentId);
    WriteTransform(w, n.transform);
    w.u8((std::uint8_t)((n.visible ? 1 : 0) | (n.locked ? 2 : 0) |
                        (n.isolate ? 4 : 0) | (n.clip ? 8 : 0) |
                        (n.isMask ? 16 : 0) |
                        (n.instCopyLoc ? 32 : 0) | (n.instCopyRot ? 64 : 0) |
                        (n.instCopyScale ? 128 : 0)));
    // v15: a second flags byte (previewOnly) + the per-property lock bitmask.
    w.u8((std::uint8_t)(n.previewOnly ? 1 : 0));
    w.u32(n.propLocks);
    w.f32(n.opacity);
    w.u8((std::uint8_t)n.blend);
    w.u32((std::uint32_t)n.children.size());
    for (Ink::NodeId c : n.children) w.u64(c);
    w.u64(n.targetRef);
    WritePath(w, n.path);
    WriteStyle(w, n.style);
    w.u32((std::uint32_t)n.modifiers.size());
    for (const Ink::Modifier& m : n.modifiers) WriteModifier(w, m);
}
Ink::Node ReadNode(Reader& r, std::uint32_t ver) {
    Ink::Node n;
    n.id   = r.u64();
    n.kind = (Ink::NodeKind)std::min<std::uint8_t>(r.u8(), 2);
    n.name = r.str();
    n.parent   = r.u64();
    n.page     = r.u64();
    n.parentId = r.u64();
    n.transform = ReadTransform(r);
    const std::uint8_t flags = r.u8();
    n.visible = (flags & 1) != 0;
    n.locked  = (flags & 2) != 0;
    n.isolate = (flags & 4) != 0;
    n.clip    = (flags & 8) != 0;
    n.isMask  = (flags & 16) != 0;
    n.instCopyLoc   = (flags & 32) != 0;   // v1 wrote 0s = the new default
    n.instCopyRot   = (flags & 64) != 0;
    n.instCopyScale = (flags & 128) != 0;
    if (ver >= 15) {
        const std::uint8_t flags2 = r.u8();
        n.previewOnly = (flags2 & 1) != 0;
        n.propLocks   = r.u32();
    }
    n.opacity = r.f32();
    n.blend   = (Ink::BlendMode)std::min<std::uint8_t>(
        r.u8(), (std::uint8_t)Ink::BlendMode::Erase);
    const std::uint32_t nC = r.u32();
    for (std::uint32_t i = 0; i < nC && r.ok; ++i) n.children.push_back(r.u64());
    n.targetRef = r.u64();
    n.path  = ReadPath(r, ver);
    n.style = ReadStyle(r, ver);
    const std::uint32_t nM = r.u32();
    for (std::uint32_t i = 0; i < nM && r.ok; ++i)
        n.modifiers.push_back(ReadModifier(r, ver));
    return n;
}

// Pre-order node walk of one page's layer tree.
void WriteSubtree(Writer& w, const Ink::Document& doc, Ink::NodeId id,
                  std::uint32_t& count) {
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    WriteNode(w, *n);
    ++count;
    for (Ink::NodeId c : n->children) WriteSubtree(w, doc, c, count);
}

std::vector<std::uint8_t> EncodeDoc(const Ink::Document& doc,
                                    std::uint8_t docUnitSystem,
                                    std::uint8_t colorMode) {
    Writer w;
    w.u32(kDocVersion);
    w.u64(doc.PeekNextId());

    const auto& pages = doc.Pages();
    w.u32((std::uint32_t)pages.size());
    for (const Ink::Page& p : pages) {
        w.u64(p.id);
        w.str(p.name);
        w.f64(p.pos.x);  w.f64(p.pos.y);
        w.f64(p.size.x); w.f64(p.size.y);
        WriteColor(w, p.background);
        w.u32((std::uint32_t)p.children.size());
        for (Ink::NodeId c : p.children) w.u64(c);
    }

    // Nodes: count first — write into a side buffer while counting.
    Writer nodes;
    std::uint32_t count = 0;
    for (const Ink::Page& p : pages)
        for (Ink::NodeId c : p.children) WriteSubtree(nodes, doc, c, count);
    w.u32(count);
    w.raw(nodes.bytes.data(), nodes.bytes.size());

    const auto& colls = doc.Collections();
    w.u32((std::uint32_t)colls.size());
    for (const Ink::Collection& c : colls) {
        w.u64(c.id);
        w.str(c.name);
        WriteColor(w, c.colorTag);
        w.u8(c.visible ? 1 : 0);
        w.u32((std::uint32_t)c.members.size());
        for (Ink::NodeId m : c.members) w.u64(m);
        w.u32((std::uint32_t)c.childCollections.size());
        for (Ink::NodeId k : c.childCollections) w.u64(k);
    }
    // v12: the document display-unit system (app-level, one byte).
    w.u8(docUnitSystem);
    // v16: the document colour mode (0 RGB · 1 CMYK).
    w.u8(colorMode);
    return std::move(w.bytes);
}

bool DecodeDoc(const std::uint8_t* p, std::size_t n, AcuData& out) {
    Reader r{ p, n };
    const std::uint32_t ver = r.u32();
    if (!r.ok || ver == 0 || ver > kDocVersion) return false;
    out.nextId = r.u64();

    const std::uint32_t nPages = r.u32();
    for (std::uint32_t i = 0; i < nPages && r.ok; ++i) {
        Ink::Page pg;
        pg.id   = r.u64();
        pg.name = r.str();
        pg.pos.x  = r.f64(); pg.pos.y  = r.f64();
        pg.size.x = r.f64(); pg.size.y = r.f64();
        pg.background = ReadColor(r);
        const std::uint32_t nC = r.u32();
        for (std::uint32_t j = 0; j < nC && r.ok; ++j)
            pg.children.push_back(r.u64());
        out.pages.push_back(std::move(pg));
    }

    const std::uint32_t nNodes = r.u32();
    for (std::uint32_t i = 0; i < nNodes && r.ok; ++i)
        out.nodes.push_back(ReadNode(r, ver));

    const std::uint32_t nColls = r.u32();
    for (std::uint32_t i = 0; i < nColls && r.ok; ++i) {
        Ink::Collection c;
        c.id   = r.u64();
        c.name = r.str();
        c.colorTag = ReadColor(r);
        c.visible  = r.u8() != 0;
        const std::uint32_t nM = r.u32();
        for (std::uint32_t j = 0; j < nM && r.ok; ++j)
            c.members.push_back(r.u64());
        const std::uint32_t nK = r.u32();
        for (std::uint32_t j = 0; j < nK && r.ok; ++j)
            c.childCollections.push_back(r.u64());
        out.collections.push_back(std::move(c));
    }
    // v12: the document display-unit system (defaults to Pixel for older files).
    out.docUnitSystem = ver >= 12 ? r.u8() : 3;
    // v16: the document colour mode (defaults to RGB for older files).
    out.colorMode = ver >= 16 ? r.u8() : 0;
    return r.ok;
}

void WriteSection(std::vector<std::uint8_t>& file, std::uint32_t tag,
                  const std::vector<std::uint8_t>& payload) {
    Writer w;
    w.u32(tag);
    w.u32((std::uint32_t)payload.size());
    file.insert(file.end(), w.bytes.begin(), w.bytes.end());
    file.insert(file.end(), payload.begin(), payload.end());
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

namespace AcuFile {

bool Save(const std::string& path, const std::string& projectName,
          const std::string& moduleId, const Ink::Document& doc,
          std::uint8_t docUnitSystem, std::uint8_t colorMode,
          const std::vector<std::uint8_t>& layoutBlob,
          const std::vector<std::uint8_t>& editorBlob, const AcuThumb& thumb,
          std::string* error) {
    std::vector<std::uint8_t> file;
    {
        Writer w;
        w.u32(kMagic);
        w.u32(kContainerVersion);
        file = std::move(w.bytes);
    }
    {
        Writer meta;
        meta.str("Carto");
        meta.str(projectName);
        meta.str(moduleId);
        WriteSection(file, kTagMeta, meta.bytes);
    }
    WriteSection(file, kTagDoc, EncodeDoc(doc, docUnitSystem, colorMode));
    if (!layoutBlob.empty()) WriteSection(file, kTagLay, layoutBlob);
    if (!editorBlob.empty()) WriteSection(file, kTagEdst, editorBlob);
    if (!thumb.png.empty()) {
        // THMB LAST (the shell provider stops at it; nothing follows).
        Writer t;
        t.u32((std::uint32_t)thumb.png.size());
        t.raw(thumb.png.data(), thumb.png.size());
        t.u64(thumb.page);
        t.f32((float)thumb.x); t.f32((float)thumb.y);
        t.f32((float)thumb.w); t.f32((float)thumb.h);
        WriteSection(file, kTagThmb, t.bytes);
    }

    std::ofstream outf(path, std::ios::binary | std::ios::trunc);
    if (!outf) {
        if (error) *error = "Cannot write file (path not writable?)";
        return false;
    }
    outf.write((const char*)file.data(), (std::streamsize)file.size());
    if (!outf.good()) {
        if (error) *error = "Write failed (disk full?)";
        return false;
    }
    return true;
}

bool Load(const std::string& path, AcuData& out, std::string* error) {
    std::ifstream inf(path, std::ios::binary | std::ios::ate);
    if (!inf) {
        if (error) *error = "Cannot open file";
        return false;
    }
    const std::streamsize size = inf.tellg();
    inf.seekg(0);
    std::vector<std::uint8_t> buf((std::size_t)std::max<std::streamsize>(size, 0));
    if (!buf.empty()) inf.read((char*)buf.data(), size);
    if (!inf.good() || buf.size() < 8) {
        if (error) *error = "Unreadable or truncated file";
        return false;
    }

    Reader r{ buf.data(), buf.size() };
    if (r.u32() != kMagic) {
        if (error) *error = "Not an .acu file";
        return false;
    }
    const std::uint32_t ver = r.u32();
    if (ver < kContainerVersion) {
        if (error)
            *error = "This is a v1 .acu from the previous engine — "
                     "the format restarted with the Ink engine and v1 files "
                     "cannot be opened by this build";
        return false;
    }
    if (ver > kContainerVersion) {
        if (error) *error = "This .acu was written by a NEWER version of Carto";
        return false;
    }

    bool haveDoc = false;
    while (r.ok && r.pos + 8 <= r.n) {
        const std::uint32_t tag = r.u32();
        const std::uint32_t len = r.u32();
        if (!r.Has(len)) break;
        const std::uint8_t* payload = r.p + r.pos;
        r.pos += len;

        if (tag == kTagMeta) {
            Reader m{ payload, len };
            (void)m.str();                     // app name (informational)
            out.projectName = m.str();
            out.moduleId    = m.str();
        } else if (tag == kTagDoc) {
            if (!DecodeDoc(payload, len, out)) {
                if (error) *error = "Corrupt document section";
                return false;
            }
            haveDoc = true;
        } else if (tag == kTagLay) {
            out.layoutBlob.assign(payload, payload + len);
        } else if (tag == kTagEdst) {
            out.editorBlob.assign(payload, payload + len);
        }
        // THMB and unknown sections: skipped (forward compatibility).
    }
    if (!haveDoc) {
        if (error) *error = "No document section in file";
        return false;
    }
    return true;
}

} // namespace AcuFile
} // namespace App

#include "ProjectFile.h"
#include "ZoneLayout.h"
#include <cstring>
#include <fstream>

namespace App {

// ── Little-endian byte buffer helpers ─────────────────────────────────────────
namespace {

struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v)   { b.push_back(v); }
    void u32(uint32_t v) { for (int i=0;i<4;++i) b.push_back((uint8_t)(v>>(i*8))); }
    void u64(uint64_t v) { for (int i=0;i<8;++i) b.push_back((uint8_t)(v>>(i*8))); }
    void f32(float v)    { uint32_t u; std::memcpy(&u,&v,4); u32(u); }
    void str(const std::string& s) {
        u32((uint32_t)s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
    void bytes(const std::vector<uint8_t>& v) {
        u32((uint32_t)v.size());
        b.insert(b.end(), v.begin(), v.end());
    }
};

struct Reader {
    const uint8_t* p; const uint8_t* end; bool ok = true;
    Reader(const uint8_t* d, size_t n) : p(d), end(d+n) {}
    uint8_t  u8()  { if (p+1>end){ok=false;return 0;} return *p++; }
    uint32_t u32() { if (p+4>end){ok=false;return 0;} uint32_t v=0;
                     for (int i=0;i<4;++i) v|=(uint32_t)(*p++)<<(i*8); return v; }
    uint64_t u64() { if (p+8>end){ok=false;return 0;} uint64_t v=0;
                     for (int i=0;i<8;++i) v|=(uint64_t)(*p++)<<(i*8); return v; }
    float    f32() { uint32_t u=u32(); float f; std::memcpy(&f,&u,4); return f; }
    std::string str() {
        uint32_t n=u32(); if (!ok || p+n>end){ok=false;return {};}
        std::string s((const char*)p, n); p+=n; return s;
    }
    std::vector<uint8_t> bytes() {
        uint32_t n=u32(); if (!ok || p+n>end){ok=false;return {};}
        std::vector<uint8_t> v(p, p+n); p+=n; return v;
    }
    void skip(uint32_t n) { if (p+n>end) {ok=false; p=end;} else p+=n; }
};

// Section tags.
constexpr uint32_t TAG_META = 0x4154454D; // 'META'
constexpr uint32_t TAG_DOC  = 0x00434F44; // 'DOC\0'
constexpr uint32_t TAG_LAY  = 0x0059414C; // 'LAY\0'
constexpr uint32_t TAG_THMB = 0x424D4854; // 'THMB' — page thumbnail (PNG)
constexpr uint32_t TAG_VSET = 0x54455356; // 'VSET' — editor UI settings

// Document section internal version (migrates independently of the container).
//   v1: parametric pos/size + legacy points[]/segments[] path.
//   v2: editable Node[] (hIn/hOut/mode) + Transform + origin + collectionId,
//       plus a collections table and the 2D cursor (single geometry per shape).
//   v3: a shape is an object = origin + transform + identity + a list of PARTS,
//       each part = kind + pos/size + Node[] path + fill + stroke (multi-subpath
//       Join). A v2 shape migrates to a single-part v3 object.
//   v4: each part gains an `isCurve` flag (curve vs mesh). v3 parts migrate with
//       isCurve derived from kind (Curve → true, else false).
//   v5: object geometry is PAGE-RELATIVE (Lot 2): origin + transform are relative
//       to the owning artboard's top-left, so moving a page (or an object to
//       another page) is just a page-origin offset. Files ≤ v4 stored WORLD
//       coordinates; on load each shape's transform.translate has the owning
//       page's pos subtracted (geometry/origin unchanged) → identical picture.
//   v6: each artboard gains a `clipContents` flag (clip objects to page bounds).
//       Older files default it to false.
//   v7: a part's `isCurve` bool becomes a strict `PartType` enum
//       (Mesh/Curve/NURBS/Path, Lot 6). v4–v6 files store a u8 isCurve at the
//       same offset → migrate (1 → Curve, 0 → Mesh); ≤v3 derive from the kind.
//   v8: PartType is reduced to {Mesh, Curve}; a curve carries a SplineType
//       (Bezier/NURBS/Poly) + orderU. A part now stores: u8 type, u8 spline,
//       u32 orderU. The transient v7 type enum {Mesh,Curve,Nurbs,Path} maps:
//       Mesh→Mesh/Bezier, Curve→Curve/Bezier, Nurbs→Curve/NURBS, Path→Curve/Poly.
//   v9: each collection stores an icon colour (i32 colorIndex + RGBA custom).
//       Older files default colorIndex 0 (theme) / a neutral custom colour.
//   v10: each artboard stores a document-wide pageVisible flag (Outliner page
//        eye). Older files default it to true.
//   v11: unified Outliner tree (8b). A reserved "Project" root collection
//        (id kProjectRootId) holds everything; Collection.children is now a
//        single ordered list of nested collections AND pages; each artboard
//        stores its parentId (owning collection). Pre-v11 files are migrated on
//        load: a root is created, top-level collections + all pages parented to
//        it. (Old `childCollections` decodes straight into `children`.)
//   v12: a page can nest collections (Artboard.children), and objects detached
//        from any page live in a document-level looseShapes list (raw doc space).
//        Pre-v12 files have no page children and no loose objects (defaults).
//   v15: a StrokeStyle gains line styling — cap (Butt/Round/Square), join
//        (Miter/Round/Bevel), align (Center/Inner/Outer), miterLimit, a dash[]
//        pattern, and a periodic decorator (LineDecor + spacing/size/angle/
//        thickness/centered). Older files default to Round cap/join, Center
//        align, no dash, no decorator (identical picture).
//   v16: a Path can hold several SUBPATHS (branches / disconnected strands) via a
//        sorted `subStart` index list into nodes[]; empty = one subpath. Older
//        files have one subpath (subStart empty), so they load unchanged.
//   v17: a Part can carry a stack of surface FILL LAYERS (pattern fills clipped to
//        its contour — dots/lines/triangles/screens, with colour, spacing, size,
//        angle, draggable offset, seed). Older files have no layers (empty list).
//   v18: a FillLayer (Lines) gains dash / dashGap / altPhase for dashed,
//        phase-alternating line screens (indistinct marsh, vineyard rows). Older
//        files read these as 0/false (solid lines). Also: Shape.allowCapEdit
//        (cliff cap opt-in), StrokeStyle.capTaper, and per-part manual LineMarks
//        (slope ticks / crossing 519 / bridge 512 / pinned pylons, with
//        outsideMeasure + square-pylon flags).
//   v19: Node.weight (rational NURBS). Older files read it as 1 (ordinary spline).
//   v20: LineMark.nodeAnchor (DashAnchor pinned to a curve node). −1 in old files.
//   v21: FillLayer.clip (per-layer fill-pattern cut mode: Construction / inner /
//        outer edge). Older files read Construction (today's behaviour, no clip).
//   v22: StrokeStyle.decorEdge / decorSide / decorSourceShapeId (Core curve
//        instancing: glyph lateral placement + array-along-curve source). Older
//        files read Construction edge + DefaultSideForDecor(decor) → identical look.
//   v23: FillLayer.anchor (pattern lattice anchored to object origin vs document).
//        Older files default to ObjectOrigin (motif unchanged, phase shifts slightly
//        from the former bbox-centre anchor).
//   v24: Part.openFillStraight (open-curve fill closes straight vs follows the curve).
//        Older files default false → follow-curve (today's behaviour).
//   v25: Shape.parentId (object parenting, Blender Ctrl+P). Older files default 0
//        (no parent), so they load with a flat object hierarchy — unchanged.
//   v26: Document.cursorRotation (the 2D cursor's orientation, rotated with R under
//        the 2D Cursor tool). Older files default 0 (axis-aligned cursor).
constexpr uint32_t DOC_VERSION = 34;   // v34: erase folded into BlendMode (no separate erase byte)

// ── Document encode ──────────────────────────────────────────────────────────
void EncodePaint(Writer& w, const Renderer::FillStyle& f) {
    w.u8(f.enabled ? 1 : 0);
    w.f32(f.color.r); w.f32(f.color.g); w.f32(f.color.b); w.f32(f.color.a);
}
void EncodeStroke(Writer& w, const Renderer::StrokeStyle& s) {
    w.u8(s.enabled ? 1 : 0);
    w.f32(s.color.r); w.f32(s.color.g); w.f32(s.color.b); w.f32(s.color.a);
    w.f32(s.width);
    // v15: line styling.
    w.u8((uint8_t)s.cap);
    w.u8((uint8_t)s.join);
    w.u8((uint8_t)s.align);
    w.f32(s.miterLimit);
    w.u32((uint32_t)s.dash.size());
    for (float d : s.dash) w.f32(d);
    w.u8((uint8_t)s.decor);
    w.f32(s.decorSpacing);
    w.f32(s.decorSize);
    w.f32(s.decorAngleDeg);
    w.f32(s.decorThickness);
    w.u8(s.decorCentered ? 1 : 0);
    w.f32(s.capTaper);                       // v18 taper-cap tip length
    w.u8((uint8_t)s.decorEdge);              // v22 curve-instancing placement
    w.u8((uint8_t)s.decorSide);
    w.u64(s.decorSourceShapeId);
}
void EncodePath(Writer& w, const Renderer::Path& p) {
    w.u32((uint32_t)p.nodes.size());
    for (const Renderer::Node& n : p.nodes) {
        w.f32(n.pos.x);  w.f32(n.pos.y);
        w.f32(n.hIn.x);  w.f32(n.hIn.y);
        w.f32(n.hOut.x); w.f32(n.hOut.y);
        uint8_t flags = (n.hasIn ? 1 : 0) | (n.hasOut ? 2 : 0);
        w.u8(flags);
        w.u8((uint8_t)n.mode);
        w.f32(n.weight);                          // v19 rational NURBS weight
        w.u32(n.junctionId);                      // v27 multi-path junction group
    }
    w.u8(p.closed ? 1 : 0);
    w.u32((uint32_t)p.subStart.size());          // v16 subpath starts
    for (int s : p.subStart) w.u32((uint32_t)(int32_t)s);
}
void EncodeFillLayers(Writer& w, const std::vector<Renderer::FillLayer>& layers) {
    w.u32((uint32_t)layers.size());          // v17 surface fill layers
    for (const Renderer::FillLayer& fl : layers) {
        w.u8(fl.enabled ? 1 : 0);
        w.u8((uint8_t)fl.pattern);
        w.f32(fl.color.r); w.f32(fl.color.g); w.f32(fl.color.b); w.f32(fl.color.a);
        w.f32(fl.opacity); w.f32(fl.spacing); w.f32(fl.size); w.f32(fl.angleDeg);
        w.f32(fl.offset.x); w.f32(fl.offset.y);
        w.u32(fl.seed);
        w.f32(fl.dash); w.f32(fl.dashGap); w.u8(fl.altPhase ? 1 : 0);   // v18
        w.u8((uint8_t)fl.clip);     // v21 per-layer fill-pattern clip mode
        w.u8((uint8_t)fl.anchor);   // v23 per-layer pattern anchor
    }
}
void EncodeMarks(Writer& w, const std::vector<Renderer::LineMark>& marks) {
    w.u32((uint32_t)marks.size());           // v18 manual line marks
    for (const Renderer::LineMark& m : marks) {
        w.u8((uint8_t)m.kind);
        w.u32((uint32_t)(int32_t)m.sub);
        w.f32(m.t);
        w.u32((uint32_t)(int32_t)m.side);
        w.f32(m.gap); w.f32(m.size); w.f32(m.thickness);
        w.u8(m.outsideMeasure ? 1 : 0);
        w.u8(m.square ? 1 : 0);
        w.u32((uint32_t)(int32_t)m.nodeAnchor);   // v20 DashAnchor node pin
    }
}
void EncodePart(Writer& w, const Renderer::Part& part) {
    w.u32((uint32_t)part.kind);
    w.u8((uint8_t)part.type);            // v7+ PartType {Mesh,Curve}
    w.u8((uint8_t)part.spline);          // v8 SplineType {Bezier,NURBS,Poly}
    w.u32((uint32_t)part.orderU);        // v8 NURBS order U
    w.f32(part.pos.x); w.f32(part.pos.y);
    w.f32(part.size.x); w.f32(part.size.y);
    EncodePath(w, part.path);
    EncodePaint(w, part.fill);
    EncodeStroke(w, part.stroke);
    EncodeFillLayers(w, part.fillLayers);    // v17
    EncodeMarks(w, part.marks);              // v18
    w.u8(part.nurbsEndpoint ? 1 : 0);        // v19 NURBS knot options
    w.u8(part.nurbsBezier ? 1 : 0);
    w.u8(part.openFillStraight ? 1 : 0);     // v24 open-curve fill close mode
}

// v3 shape (object) encode: identity + origin + transform + parts.
void EncodeShape(Writer& w, const Renderer::Shape& s) {
    w.u64(s.id);
    w.str(s.name);
    w.u8(s.visible ? 1 : 0);
    w.u64(s.collectionId);
    w.f32(s.origin.x); w.f32(s.origin.y);
    w.f32(s.transform.translate.x); w.f32(s.transform.translate.y);
    w.f32(s.transform.rotate);
    w.f32(s.transform.scale.x); w.f32(s.transform.scale.y);
    // v13..v27 wrote a single lockScale u8; v28 writes per-axis locks. The
    // legacy whole-scale slot is kept (= both scale axes) so a v28 file still
    // round-trips the old field's meaning for any downstream reader that stops at
    // v27, and the per-axis bytes follow.
    w.u8(s.LockScaleBoth() ? 1 : 0);    // v13 (legacy whole-scale)
    w.u8(s.lockRotation ? 1 : 0);       // v13
    w.u8(s.lockPosX ? 1 : 0);           // v28
    w.u8(s.lockPosY ? 1 : 0);           // v28
    w.u8(s.lockScaleX ? 1 : 0);         // v28
    w.u8(s.lockScaleY ? 1 : 0);         // v28
    w.u32((uint32_t)(int32_t)s.isomCode); // v14
    w.u8(s.allowCapEdit ? 1 : 0);       // v18 cap-editable opt-in
    w.u64(s.parentId);                  // v25 object parenting (0 = none)
    w.f32(s.opacity);                   // v29 object opacity (Compositor compositing)
    w.u8((uint8_t)s.blendMode);         // v30 object blend mode (v34: Erase is a blend mode)
    w.u64(s.groupId);                   // v33 owning layer group (0 = none)
    w.u32((uint32_t)s.parts.size());
    for (const Renderer::Part& part : s.parts) EncodePart(w, part);
}

std::vector<uint8_t> EncodeDocument(const Renderer::Document& doc) {
    Writer w;
    w.u32(DOC_VERSION);
    w.u64(doc.PeekNextId());
    // 2D cursor.
    w.f32(doc.cursor.x); w.f32(doc.cursor.y);
    w.f32(doc.cursorRotation);              // v26 cursor orientation
    // Collections table.
    w.u32((uint32_t)doc.collections.size());
    for (const Renderer::Collection& c : doc.collections) {
        w.u64(c.id);
        w.str(c.name);
        w.u64(c.parentId);
        // v11: `children` is the unified ordered list (collections AND pages).
        w.u32((uint32_t)c.children.size());
        for (uint64_t ch : c.children) w.u64(ch);
        // v9: collection icon colour.
        w.u32((uint32_t)(int32_t)c.colorIndex);
        w.f32(c.customColor.r); w.f32(c.customColor.g);
        w.f32(c.customColor.b); w.f32(c.customColor.a);
        // v32: layer-group compositing (Affinity-style groups). v34: Erase is a blend
        // mode (no separate erase byte).
        w.u8(c.isLayerGroup ? 1 : 0);
        w.f32(c.opacity);
        w.u8((uint8_t)c.blendMode);
    }
    // Artboards + shapes.
    w.u32((uint32_t)doc.artboards.size());
    for (const Renderer::Artboard& ab : doc.artboards) {
        w.u64(ab.id);
        w.str(ab.name);
        w.f32(ab.pos.x); w.f32(ab.pos.y);
        w.f32(ab.size.x); w.f32(ab.size.y);
        w.u8(ab.clipContents ? 1 : 0);   // v6
        w.u8(ab.pageVisible ? 1 : 0);    // v10 document-wide page visibility
        w.u64(ab.parentId);              // v11 owning collection in the unified tree
        // v12: collections nested under this page (a page is a full tree node).
        w.u32((uint32_t)ab.children.size());
        for (uint64_t ch : ab.children) w.u64(ch);
        w.u32((uint32_t)ab.shapes.size());
        for (const Renderer::Shape& s : ab.shapes) EncodeShape(w, s);
    }
    // v12: loose (page-less) objects, stored in raw document space.
    w.u32((uint32_t)doc.looseShapes.size());
    for (const Renderer::Shape& s : doc.looseShapes) EncodeShape(w, s);
    return std::move(w.b);
}

// ── Document decode (handles every DOC_VERSION ≤ current) ─────────────────────
void DecodePaint(Reader& r, Renderer::FillStyle& f) {
    f.enabled = r.u8() != 0;
    f.color.r = r.f32(); f.color.g = r.f32(); f.color.b = r.f32(); f.color.a = r.f32();
}
void DecodeStroke(Reader& r, Renderer::StrokeStyle& s, uint32_t ver) {
    s.enabled = r.u8() != 0;
    s.color.r = r.f32(); s.color.g = r.f32(); s.color.b = r.f32(); s.color.a = r.f32();
    s.width = r.f32();
    if (ver >= 15) {                    // line styling
        s.cap   = (Renderer::LineCap)r.u8();
        s.join  = (Renderer::LineJoin)r.u8();
        s.align = (Renderer::StrokeAlign)r.u8();
        s.miterLimit = r.f32();
        uint32_t nd = r.u32();
        s.dash.clear();
        for (uint32_t i = 0; i < nd && r.ok; ++i) s.dash.push_back(r.f32());
        s.decor         = (Renderer::LineDecor)r.u8();
        s.decorSpacing  = r.f32();
        s.decorSize     = r.f32();
        s.decorAngleDeg = r.f32();
        s.decorThickness = r.f32();
        s.decorCentered = r.u8() != 0;
        if (ver >= 18) s.capTaper = r.f32();     // taper-cap tip length
        if (ver >= 22) {                         // curve-instancing placement
            s.decorEdge = (Renderer::DecorEdge)r.u8();
            s.decorSide = (Renderer::DecorSide)r.u8();
            s.decorSourceShapeId = r.u64();
        } else {                                 // back-compat: reproduce old look
            s.decorEdge = Renderer::DecorEdge::Construction;
            s.decorSide = Renderer::DefaultSideForDecor(s.decor);
            s.decorSourceShapeId = 0;
        }
    }
}
// ── v1 → v2 migration: read the old parametric/segment shape, convert to the
//    editable Node[] model. ────────────────────────────────────────────────────
Renderer::Shape DecodeShapeV1(Reader& r) {
    Renderer::Shape s;
    Renderer::Part part;
    s.id   = r.u64();
    uint32_t kind = r.u32();
    s.name = r.str();
    s.visible = r.u8() != 0;
    part.pos.x = r.f32(); part.pos.y = r.f32();
    part.size.x = r.f32(); part.size.y = r.f32();

    std::vector<Renderer::Vec2> pts;
    uint32_t np = r.u32();
    for (uint32_t i = 0; i < np && r.ok; ++i) pts.push_back({ r.f32(), r.f32() });
    std::vector<Renderer::LegacySegment> segs;
    uint32_t ns = r.u32();
    for (uint32_t i = 0; i < ns && r.ok; ++i) {
        Renderer::LegacySegment sg;
        sg.kind = (Renderer::SegmentKind)r.u8();
        sg.c0.x = r.f32(); sg.c0.y = r.f32();
        sg.c1.x = r.f32(); sg.c1.y = r.f32();
        segs.push_back(sg);
    }
    bool closed = r.u8() != 0;
    DecodePaint(r, part.fill);
    DecodeStroke(r, part.stroke, 1);

    part.kind = (Renderer::ShapeKind)kind;
    if (part.kind == Renderer::ShapeKind::Triangle ||
        part.kind == Renderer::ShapeKind::Polyline ||
        part.kind == Renderer::ShapeKind::Curve) {
        Renderer::Path p;
        p.closed = closed;
        for (size_t i = 0; i < pts.size(); ++i) {
            Renderer::Node n(pts[i]);
            if (i < segs.size() && segs[i].kind == Renderer::SegmentKind::CubicBezier) {
                n.hasOut = true; n.hOut = segs[i].c0; n.mode = Renderer::HandleMode::Free;
            }
            if (i > 0 && (i - 1) < segs.size() &&
                segs[i - 1].kind == Renderer::SegmentKind::CubicBezier) {
                n.hasIn = true; n.hIn = segs[i - 1].c1; n.mode = Renderer::HandleMode::Free;
            }
            p.nodes.push_back(n);
        }
        part.path = std::move(p);
    }
    // Legacy files had no type flag: derive it from the kind (Curve → Curve).
    part.type = (part.kind == Renderer::ShapeKind::Curve) ? Renderer::PartType::Curve
                                                          : Renderer::PartType::Mesh;
    s.parts.push_back(std::move(part));
    return s;
}

void DecodePath(Reader& r, Renderer::Path& p, uint32_t ver) {
    uint32_t nn = r.u32();
    for (uint32_t i = 0; i < nn && r.ok; ++i) {
        Renderer::Node n;
        n.pos.x = r.f32();  n.pos.y = r.f32();
        n.hIn.x = r.f32();  n.hIn.y = r.f32();
        n.hOut.x = r.f32(); n.hOut.y = r.f32();
        uint8_t flags = r.u8();
        n.hasIn  = (flags & 1) != 0;
        n.hasOut = (flags & 2) != 0;
        n.mode   = (Renderer::HandleMode)r.u8();
        if (ver >= 19) n.weight = r.f32();        // rational NURBS weight (else 1)
        if (ver >= 27) n.junctionId = r.u32();    // multi-path junction group (else 0)
        p.nodes.push_back(n);
    }
    p.closed = r.u8() != 0;
    if (ver >= 16) {                              // multi-subpath starts
        uint32_t ns = r.u32();
        p.subStart.clear();
        for (uint32_t i = 0; i < ns && r.ok; ++i) p.subStart.push_back((int)(int32_t)r.u32());
        p.NormalizeSubpaths();
    }
}

// ── v2 shape decode → single-part v3 object ───────────────────────────────────
Renderer::Shape DecodeShapeV2(Reader& r) {
    Renderer::Shape s;
    Renderer::Part part;
    s.id   = r.u64();
    part.kind = (Renderer::ShapeKind)r.u32();
    s.name = r.str();
    s.visible = r.u8() != 0;
    s.collectionId = r.u64();
    part.pos.x = r.f32(); part.pos.y = r.f32();
    part.size.x = r.f32(); part.size.y = r.f32();
    s.origin.x = r.f32(); s.origin.y = r.f32();
    s.transform.translate.x = r.f32(); s.transform.translate.y = r.f32();
    s.transform.rotate = r.f32();
    s.transform.scale.x = r.f32(); s.transform.scale.y = r.f32();
    DecodePath(r, part.path, 2);
    DecodePaint(r, part.fill);
    DecodeStroke(r, part.stroke, 2);
    // Legacy files had no type flag: derive it from the kind (Curve → Curve).
    part.type = (part.kind == Renderer::ShapeKind::Curve) ? Renderer::PartType::Curve
                                                          : Renderer::PartType::Mesh;
    s.parts.push_back(std::move(part));
    return s;
}

// ── v3 shape (object) decode ──────────────────────────────────────────────────
Renderer::Shape DecodeShapeV3(Reader& r, uint32_t ver) {
    Renderer::Shape s;
    s.id   = r.u64();
    s.name = r.str();
    s.visible = r.u8() != 0;
    s.collectionId = r.u64();
    s.origin.x = r.f32(); s.origin.y = r.f32();
    s.transform.translate.x = r.f32(); s.transform.translate.y = r.f32();
    s.transform.rotate = r.f32();
    s.transform.scale.x = r.f32(); s.transform.scale.y = r.f32();
    if (ver >= 13) {                    // per-shape transform locks
        bool legacyScale = r.u8() != 0; // v13 whole-scale (both axes)
        s.lockRotation   = r.u8() != 0;
        s.SetLockScale(legacyScale);    // default per-axis from the legacy field
        if (ver >= 28) {                // v28 per-axis locks override the legacy bit
            s.lockPosX   = r.u8() != 0;
            s.lockPosY   = r.u8() != 0;
            s.lockScaleX = r.u8() != 0;
            s.lockScaleY = r.u8() != 0;
        }
    }
    if (ver >= 14) s.isomCode = (int)(int32_t)r.u32();   // ISOM symbol code
    if (ver >= 18) s.allowCapEdit = r.u8() != 0;         // cap-editable opt-in
    if (ver >= 25) s.parentId = r.u64();                 // object parenting (0=none)
    if (ver >= 29) s.opacity = r.f32();                  // object opacity (else 1.0)
    if (ver >= 30) s.blendMode = (Renderer::BlendMode)r.u8();   // blend mode (else Normal)
    if (ver >= 31 && ver < 34) {                         // v31..v33 had a separate erase byte;
        if (r.u8() != 0) s.blendMode = Renderer::BlendMode::Erase;   // v34 folds it into the blend
    }
    if (ver >= 33) s.groupId = r.u64();                  // owning layer group (else 0)
    uint32_t npart = r.u32();
    for (uint32_t i = 0; i < npart && r.ok; ++i) {
        Renderer::Part part;
        part.kind = (Renderer::ShapeKind)r.u32();
        // Part type + spline. v8+: u8 PartType{Mesh,Curve} + u8 SplineType + u32
        // orderU. v7: u8 type was a 4-value {Mesh,Curve,Nurbs,Path} → map to
        // family + spline. v4–v6: u8 isCurve (1→Curve,0→Mesh), Bézier. ≤v3:
        // derive from the kind.
        part.spline = Renderer::SplineType::Bezier;
        if (ver >= 8) {
            part.type   = (Renderer::PartType)r.u8();
            part.spline = (Renderer::SplineType)r.u8();
            part.orderU = (int)r.u32();
        } else if (ver == 7) {
            uint8_t t7 = r.u8();                 // 0 Mesh,1 Curve,2 Nurbs,3 Path
            part.type   = (t7 == 0) ? Renderer::PartType::Mesh
                                    : Renderer::PartType::Curve;
            part.spline = (t7 == 2) ? Renderer::SplineType::Nurbs
                        : (t7 == 3) ? Renderer::SplineType::Poly
                                    : Renderer::SplineType::Bezier;
        } else if (ver >= 4) {
            part.type = (r.u8() != 0) ? Renderer::PartType::Curve
                                      : Renderer::PartType::Mesh;
        } else {
            part.type = (part.kind == Renderer::ShapeKind::Curve)
                            ? Renderer::PartType::Curve : Renderer::PartType::Mesh;
        }
        part.pos.x = r.f32(); part.pos.y = r.f32();
        part.size.x = r.f32(); part.size.y = r.f32();
        DecodePath(r, part.path, ver);
        DecodePaint(r, part.fill);
        DecodeStroke(r, part.stroke, ver);
        if (ver >= 17) {                          // surface fill layers
            uint32_t nl = r.u32();
            for (uint32_t li = 0; li < nl && r.ok; ++li) {
                Renderer::FillLayer fl;
                fl.enabled = r.u8() != 0;
                fl.pattern = (Renderer::FillPattern)r.u8();
                fl.color.r = r.f32(); fl.color.g = r.f32(); fl.color.b = r.f32(); fl.color.a = r.f32();
                fl.opacity = r.f32(); fl.spacing = r.f32(); fl.size = r.f32(); fl.angleDeg = r.f32();
                fl.offset.x = r.f32(); fl.offset.y = r.f32();
                fl.seed = r.u32();
                if (ver >= 18) { fl.dash = r.f32(); fl.dashGap = r.f32(); fl.altPhase = r.u8() != 0; }
                if (ver >= 21) fl.clip = (Renderer::FillClip)r.u8();   // per-layer clip
                if (ver >= 23) fl.anchor = (Renderer::FillAnchor)r.u8();  // pattern anchor
                part.fillLayers.push_back(fl);
            }
        }
        if (ver >= 18) {                          // manual line marks
            uint32_t nm = r.u32();
            for (uint32_t mi = 0; mi < nm && r.ok; ++mi) {
                Renderer::LineMark m;
                m.kind = (Renderer::LineMarkKind)r.u8();
                m.sub  = (int)(int32_t)r.u32();
                m.t    = r.f32();
                m.side = (int)(int32_t)r.u32();
                m.gap = r.f32(); m.size = r.f32(); m.thickness = r.f32();
                m.outsideMeasure = r.u8() != 0;
                m.square = r.u8() != 0;
                if (ver >= 20) m.nodeAnchor = (int)(int32_t)r.u32();   // DashAnchor pin
                part.marks.push_back(m);
            }
        }
        if (ver >= 19) {                          // NURBS knot options
            part.nurbsEndpoint = r.u8() != 0;
            part.nurbsBezier   = r.u8() != 0;
        }
        if (ver >= 24) part.openFillStraight = r.u8() != 0;   // open-fill close mode
        s.parts.push_back(std::move(part));
    }
    return s;
}

bool DecodeDocument(const std::vector<uint8_t>& blob, Renderer::Document& doc) {
    Reader r(blob.data(), blob.size());
    uint32_t ver = r.u32();
    if (!r.ok || ver == 0 || ver > DOC_VERSION) return false;  // unknown future
    uint64_t nextId = r.u64();
    doc.Clear();
    // Clear() seeds the reserved Project root collection; remove it so a v11 file
    // (which stores its own root) doesn't end up duplicated. Pre-v11 files have no
    // root → we recreate + migrate below.
    doc.collections.clear();

    if (ver >= 2) {
        // 2D cursor.
        doc.cursor.x = r.f32(); doc.cursor.y = r.f32();
        if (ver >= 26) doc.cursorRotation = r.f32();   // cursor orientation
        // Collections.
        uint32_t nc = r.u32();
        for (uint32_t i = 0; i < nc && r.ok; ++i) {
            Renderer::Collection c;
            c.id = r.u64();
            c.name = r.str();
            c.parentId = r.u64();
            uint32_t nch = r.u32();
            for (uint32_t j = 0; j < nch && r.ok; ++j) c.children.push_back(r.u64());
            if (ver >= 9) {                  // collection icon colour
                c.colorIndex = (int)(int32_t)r.u32();
                c.customColor.r = r.f32(); c.customColor.g = r.f32();
                c.customColor.b = r.f32(); c.customColor.a = r.f32();
            }
            if (ver >= 32) {                 // layer-group compositing
                c.isLayerGroup = r.u8() != 0;
                c.opacity = r.f32();
                c.blendMode = (Renderer::BlendMode)r.u8();
                if (ver < 34 && r.u8() != 0)  // v32/v33 had a separate erase byte
                    c.blendMode = Renderer::BlendMode::Erase;
            }
            doc.collections.push_back(std::move(c));
        }
    }

    uint32_t nab = r.u32();
    for (uint32_t i = 0; i < nab && r.ok; ++i) {
        Renderer::Artboard ab;
        ab.id   = r.u64();
        ab.name = r.str();
        ab.pos.x = r.f32(); ab.pos.y = r.f32();
        ab.size.x = r.f32(); ab.size.y = r.f32();
        if (ver >= 6) ab.clipContents = (r.u8() != 0);   // v6 page-clip flag
        if (ver >= 10) ab.pageVisible = (r.u8() != 0);   // v10 page visibility
        if (ver >= 11) ab.parentId = r.u64();            // v11 owning collection
        if (ver >= 12) {                                 // v12 nested collections
            uint32_t nch = r.u32();
            for (uint32_t j = 0; j < nch && r.ok; ++j) ab.children.push_back(r.u64());
        }
        uint32_t nsh = r.u32();
        for (uint32_t j = 0; j < nsh && r.ok; ++j) {
            if      (ver >= 3) ab.shapes.push_back(DecodeShapeV3(r, ver));
            else if (ver == 2) ab.shapes.push_back(DecodeShapeV2(r));
            else               ab.shapes.push_back(DecodeShapeV1(r));
            // v≤4 stored WORLD coords; make them page-relative (Lot 2). Geometry
            // and origin are unchanged — only translate absorbs the page offset
            // so the object stays visually put: world = pageOrigin + translate' …
            if (ver < 5) {
                Renderer::Shape& s = ab.shapes.back();
                s.transform.translate.x -= ab.pos.x;
                s.transform.translate.y -= ab.pos.y;
            }
        }
        doc.artboards.push_back(std::move(ab));
    }
    // v12: loose (page-less) objects.
    if (ver >= 12) {
        uint32_t nl = r.u32();
        for (uint32_t j = 0; j < nl && r.ok; ++j)
            doc.looseShapes.push_back(DecodeShapeV3(r, ver));
    }
    if (!r.ok) return false;
    doc.SetNextId(nextId);

    // ── Unified tree (8b): ensure exactly one Project root, then migrate pre-v11
    //    files into it. Pre-v11 had no root, used `childCollections` (now
    //    `children`) for nested COLLECTIONS only, and didn't track a page parent.
    if (ver < 11) {
        // Make room for the reserved root id if a file happened to use it.
        // (Old ids started at 1, so id 1 may already be taken — bump everything
        // is overkill; instead just create the root and reference existing ids.)
        Renderer::Collection root;
        root.id = Renderer::kProjectRootId; root.name = "Project"; root.parentId = 0;
        // If id 1 is already used by a real object/collection/page, shift the root
        // to a fresh id is NOT possible (kProjectRootId is fixed); but pre-v11
        // ids 1.. were allocated before any root existed, so collisions are
        // possible. Guard: only adopt kProjectRootId if free; else remap.
        bool rootIdFree = !doc.FindCollection(Renderer::kProjectRootId) &&
                          !doc.FindArtboardById(Renderer::kProjectRootId);
        if (rootIdFree) {
            doc.collections.insert(doc.collections.begin(), std::move(root));
            // Top-level collections (parentId 0) + all pages become root children.
            Renderer::Collection* rp = doc.FindCollection(Renderer::kProjectRootId);
            for (Renderer::Collection& c : doc.collections) {
                if (c.id == Renderer::kProjectRootId) continue;
                if (c.parentId == 0) { c.parentId = Renderer::kProjectRootId;
                                       rp->children.push_back(c.id); }
            }
            for (Renderer::Artboard& a : doc.artboards) {
                a.parentId = Renderer::kProjectRootId;
                rp->children.push_back(a.id);
            }
        } else {
            // Rare collision: fall back to a fresh root via the API (allocates a
            // new id) and reparent everything to it.
            doc.EnsureProjectRoot();   // no-op if present; else makes one at kProjectRootId
        }
    } else {
        doc.EnsureProjectRoot();       // v11+ already has it; this is a safety net
    }
    return true;
}

} // namespace

// ── Container Save / Load ─────────────────────────────────────────────────────
bool ProjectFile::Save(const std::string& path,
                       const Project& project, const ZoneLayout& layout) {
    Writer file;
    file.u32(MAGIC);
    file.u32(CURRENT_VERSION);

    auto section = [&](uint32_t tag, const std::vector<uint8_t>& payload) {
        file.u32(tag);
        file.u32((uint32_t)payload.size());
        file.b.insert(file.b.end(), payload.begin(), payload.end());
    };

    // META.
    {
        Writer m;
        m.str(std::string("Carto"));        // app name
        m.str(project.name);                // display name
        m.str(project.moduleId);            // module the project belongs to ("" = Classic)
        section(TAG_META, m.b);
    }
    // DOCUMENT.
    section(TAG_DOC, EncodeDocument(project.document));
    // LAYOUT (opaque blob owned by ZoneLayout).
    section(TAG_LAY, layout.Serialize());
    // EDITOR SETTINGS (menu-bar toggles/choices restored on reopen). Versioned by a
    // leading u32 so new fields can be appended; old files load defaults for them.
    {
        const EditorSettings& e = project.editorSettings;
        Writer v;
        v.u32(3);                            // VSET version (3 → default fill/stroke)
        v.u32((uint32_t)(int32_t)e.pivotMode);
        v.u32((uint32_t)(int32_t)e.transformOrient);
        v.u8(e.show2DCursor ? 1 : 0);
        v.u8(e.showMetrics ? 1 : 0);
        v.u8(e.snapEnabled ? 1 : 0);
        v.u32((uint32_t)(int32_t)e.snapMode);
        v.u32((uint32_t)(int32_t)e.snapBase);
        v.u8(e.snapAffectMove ? 1 : 0);
        v.u8(e.snapAffectRotate ? 1 : 0);
        v.u8(e.snapAffectScale ? 1 : 0);
        v.f32(e.snapRotIncrement);
        v.f32(e.snapRotPrecision);
        // v2: per-mode tool memory (Object tool + per-object Edit tool).
        v.str(e.objectModeTool);
        v.u32((uint32_t)e.editToolByObject.size());
        for (const auto& kv : e.editToolByObject) { v.u64(kv.first); v.str(kv.second); }
        // v3: default fill/stroke colours (RGBA).
        for (int i = 0; i < 4; ++i) v.f32(e.defaultFill[i]);
        for (int i = 0; i < 4; ++i) v.f32(e.defaultStroke[i]);
        section(TAG_VSET, v.b);
    }
    // THUMBNAIL (PNG bytes + framing). Written last so a shell thumbnail
    // provider can find it by walking the tag/length sections. The payload is
    // [pngLen:u32][png bytes][artboard:u32][rmin.x,rmin.y][rsz.x,rsz.y : f32].
    if (!project.thumbnailPng.empty()) {
        Writer t;
        t.bytes(project.thumbnailPng);
        t.u32((uint32_t)project.thumbArtboard);
        t.f32(project.thumbRegionMin.x);  t.f32(project.thumbRegionMin.y);
        t.f32(project.thumbRegionSize.x); t.f32(project.thumbRegionSize.y);
        section(TAG_THMB, t.b);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(file.b.data()),
              (std::streamsize)file.b.size());
    return (bool)out;
}

bool ProjectFile::Load(const std::string& path,
                       Project& project, ZoneLayout& layout) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    std::streamsize n = in.tellg();
    if (n <= 0) return false;
    std::vector<uint8_t> buf((size_t)n);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(buf.data()), n);
    if (!in) return false;

    Reader r(buf.data(), buf.size());
    if (r.u32() != MAGIC) return false;
    uint32_t ver = r.u32();
    if (!r.ok || ver == 0 || ver > CURRENT_VERSION) return false;

    // Decode into temporaries, then commit only if the essential sections
    // (DOCUMENT) parsed — so a corrupt file never half-replaces the live state.
    Renderer::Document tmpDoc;
    std::vector<uint8_t> layoutBlob;
    std::string displayName = project.name;
    std::string loadedModuleId;            // META module id (empty = Classic / old file)
    bool haveDoc = false;
    std::vector<uint8_t> thumbPng;
    int   thumbAb = 0; Renderer::Vec2 thumbMin{0,0}, thumbSz{0,0};
    EditorSettings editorSettings;          // defaults unless a VSET section is read
    bool  haveVSet = false;

    while (r.ok && r.p < r.end) {
        uint32_t tag = r.u32();
        uint32_t len = r.u32();
        if (!r.ok) break;
        const uint8_t* sectionStart = r.p;
        if (r.p + len > r.end) break;          // truncated section
        std::vector<uint8_t> payload(sectionStart, sectionStart + len);
        r.skip(len);                           // advance past the section

        switch (tag) {
            case TAG_META: {
                Reader m(payload.data(), payload.size());
                m.str();                       // app name (ignored on load)
                displayName = m.str();
                std::string mid = m.str();     // module id (absent in old files)
                if (m.ok) loadedModuleId = mid;
                break;
            }
            case TAG_DOC:
                haveDoc = DecodeDocument(payload, tmpDoc);
                break;
            case TAG_LAY:
                layoutBlob = std::move(payload);
                break;
            case TAG_THMB: {
                Reader t(payload.data(), payload.size());
                thumbPng = t.bytes();
                thumbAb  = (int)t.u32();
                thumbMin.x = t.f32(); thumbMin.y = t.f32();
                thumbSz.x  = t.f32(); thumbSz.y  = t.f32();
                if (!t.ok) thumbPng.clear();
                break;
            }
            case TAG_VSET: {
                Reader v(payload.data(), payload.size());
                uint32_t vsetVer = v.u32();    // VSET version (1 or 2)
                EditorSettings e;
                e.pivotMode       = (int)(int32_t)v.u32();
                e.transformOrient = (int)(int32_t)v.u32();
                e.show2DCursor    = v.u8() != 0;
                e.showMetrics     = v.u8() != 0;
                e.snapEnabled     = v.u8() != 0;
                e.snapMode        = (int)(int32_t)v.u32();
                e.snapBase        = (int)(int32_t)v.u32();
                e.snapAffectMove   = v.u8() != 0;
                e.snapAffectRotate = v.u8() != 0;
                e.snapAffectScale  = v.u8() != 0;
                e.snapRotIncrement = v.f32();
                e.snapRotPrecision = v.f32();
                if (vsetVer >= 2) {            // per-mode tool memory
                    e.objectModeTool = v.str();
                    uint32_t cnt = v.u32();
                    for (uint32_t i = 0; i < cnt && v.ok; ++i) {
                        uint64_t id = v.u64(); std::string t = v.str();
                        if (v.ok) e.editToolByObject[id] = t;
                    }
                }
                if (vsetVer >= 3) {            // default fill/stroke colours
                    for (int i = 0; i < 4; ++i) e.defaultFill[i]   = v.f32();
                    for (int i = 0; i < 4; ++i) e.defaultStroke[i] = v.f32();
                }
                if (v.ok) { editorSettings = e; haveVSet = true; }
                break;
            }
            default:
                // Unknown section (newer file feature) — skipped already.
                break;
        }
    }

    if (!haveDoc) return false;

    project.document = std::move(tmpDoc);
    project.name     = displayName;
    project.moduleId = loadedModuleId;
    project.path     = path;
    project.dirty    = false;
    project.document.ClearSelection();
    project.thumbnailPng   = std::move(thumbPng);
    project.thumbArtboard  = thumbAb;
    project.thumbRegionMin = thumbMin;
    project.thumbRegionSize = thumbSz;
    // Editor settings: the file's if present, else defaults (old files).
    project.editorSettings = haveVSet ? editorSettings : EditorSettings{};
    if (!layoutBlob.empty())
        layout.Deserialize(layoutBlob);        // best-effort: keep tree if bad
    return true;
}

// ── Document blob (undo snapshots) — reuse the DOCUMENT section codec ──────────
std::vector<uint8_t> ProjectFile::EncodeDocumentBlob(const Renderer::Document& doc) {
    return EncodeDocument(doc);
}
bool ProjectFile::DecodeDocumentBlob(const std::vector<uint8_t>& blob,
                                     Renderer::Document& outDoc) {
    return DecodeDocument(blob, outDoc);
}

} // namespace App

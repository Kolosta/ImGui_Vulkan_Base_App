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
constexpr std::uint32_t kTagThmb = 0x424D4854;   // 'THMB'

constexpr std::uint32_t kDocVersion = 1;

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
        w.u32((std::uint32_t)sp.anchors.size());
        for (const Ink::Anchor& a : sp.anchors) {
            w.f64(a.pos.x); w.f64(a.pos.y);
            w.f64(a.in.x);  w.f64(a.in.y);
            w.f64(a.out.x); w.f64(a.out.y);
            w.u8((std::uint8_t)((a.hasIn ? 1 : 0) | (a.hasOut ? 2 : 0) |
                                ((std::uint8_t)a.kind << 2)));
        }
    }
}
Ink::PathData ReadPath(Reader& r) {
    Ink::PathData p;
    const std::uint32_t nSub = r.u32();
    for (std::uint32_t i = 0; i < nSub && r.ok; ++i) {
        Ink::Subpath sp;
        sp.closed = r.u8() != 0;
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
            sp.anchors.push_back(a);
        }
        p.subpaths.push_back(std::move(sp));
    }
    return p;
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
        w.u8((std::uint8_t)st.widthSpace);
        w.u32((std::uint32_t)st.dashPattern.size());
        for (double d : st.dashPattern) w.f64(d);
        w.f64(st.dashOffset);
    }
}
Ink::Style ReadStyle(Reader& r) {
    Ink::Style s;
    const std::uint32_t nF = r.u32();
    for (std::uint32_t i = 0; i < nF && r.ok; ++i) {
        Ink::Fill f;
        f.kind    = (Ink::FillKind)std::min<std::uint8_t>(r.u8(), 1);
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
        s.fills.push_back(f);
    }
    const std::uint32_t nS = r.u32();
    for (std::uint32_t i = 0; i < nS && r.ok; ++i) {
        Ink::Stroke st;
        st.enabled     = r.u8() != 0;
        st.paint.color = ReadColor(r);
        st.width       = r.f64();
        st.align = (Ink::StrokeAlign)std::min<std::uint8_t>(r.u8(), 2);
        st.cap   = (Ink::CapStyle)std::min<std::uint8_t>(r.u8(), 2);
        st.join  = (Ink::JoinStyle)std::min<std::uint8_t>(r.u8(), 2);
        st.miterLimit = r.f64();
        st.widthSpace = (Ink::WidthSpace)std::min<std::uint8_t>(r.u8(), 1);
        const std::uint32_t nD = r.u32();
        for (std::uint32_t j = 0; j < nD && r.ok; ++j)
            st.dashPattern.push_back(r.f64());
        st.dashOffset = r.f64();
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
}
Ink::Modifier ReadModifier(Reader& r) {
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
                        (n.isMask ? 16 : 0)));
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
Ink::Node ReadNode(Reader& r) {
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
    n.opacity = r.f32();
    n.blend   = (Ink::BlendMode)std::min<std::uint8_t>(
        r.u8(), (std::uint8_t)Ink::BlendMode::Erase);
    const std::uint32_t nC = r.u32();
    for (std::uint32_t i = 0; i < nC && r.ok; ++i) n.children.push_back(r.u64());
    n.targetRef = r.u64();
    n.path  = ReadPath(r);
    n.style = ReadStyle(r);
    const std::uint32_t nM = r.u32();
    for (std::uint32_t i = 0; i < nM && r.ok; ++i)
        n.modifiers.push_back(ReadModifier(r));
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

std::vector<std::uint8_t> EncodeDoc(const Ink::Document& doc) {
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
        out.nodes.push_back(ReadNode(r));

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
          const std::vector<std::uint8_t>& layoutBlob, const AcuThumb& thumb,
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
    WriteSection(file, kTagDoc, EncodeDoc(doc));
    if (!layoutBlob.empty()) WriteSection(file, kTagLay, layoutBlob);
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

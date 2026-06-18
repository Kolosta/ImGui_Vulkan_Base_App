#pragma once
// Internal helpers shared by the ViewportTools* translation units (the
// viewport drawing-tool state machine, split by tool for readability). These
// were file-static in the original ViewportTools.cpp; they are `inline` here
// so every ViewportTools* .cpp can use them without duplication. Not part of
// any public API.
#include <Renderer/Document/Document.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>

namespace App {

using Renderer::Vec2;

// A freshly created single-part shape with default paints (fill on, stroke off).
// Returns the shape; callers fill in its one Part's geometry. `type` is Mesh by
// default; curve seeds pass Curve plus the spline interpretation.
inline Renderer::Shape MakeShape(Renderer::ShapeKind kind,
                                 Renderer::PartType type = Renderer::PartType::Mesh,
                                 Renderer::SplineType spline = Renderer::SplineType::Bezier) {
    Renderer::Shape s;
    Renderer::Part part;
    part.kind = kind;
    part.type = type;
    part.spline = spline;
    part.fill.enabled   = true;
    part.fill.color     = { 0.20f, 0.55f, 0.90f, 1.0f };
    part.stroke.enabled = false;
    part.stroke.color   = { 0.05f, 0.05f, 0.06f, 1.0f };
    part.stroke.width   = 2.0f;
    s.parts.push_back(part);
    return s;
}

// Allocate a junction-group id unique WITHIN a part (max existing + 1). Junction
// ids only need to be unique inside the part that owns the branches, so this keeps
// the multi-path model fully self-contained (no document-wide counter to persist).
inline uint32_t AllocJunctionId(const Renderer::Part& part) {
    uint32_t mx = 0;
    for (const Renderer::Node& n : part.path.nodes) mx = std::max(mx, n.junctionId);
    return mx + 1;
}
// Set a shape's origin to the geometric centre of its outline (Blender places a
// new object's origin at its centre, not a corner). Called right after building
// a shape's geometry, before it is added to the document.
inline void CenterOrigin(Renderer::Shape& s) {
    Vec2 mn, mx;
    if (!Renderer::Tessellator::WorldBounds(s, 1.0f, mn, mx)) return;
    s.origin = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
}

// Pick the artboard whose bounds contain doc-point p (−1 if none). Used to bind
// a new gesture to a page so shapes belong somewhere sensible.
inline int ArtboardAt(const Renderer::Document& doc, Vec2 p) {
    for (int i = 0; i < (int)doc.artboards.size(); ++i) {
        const auto& ab = doc.artboards[(size_t)i];
        if (p.x >= ab.pos.x && p.x <= ab.pos.x + ab.size.x &&
            p.y >= ab.pos.y && p.y <= ab.pos.y + ab.size.y)
            return i;
    }
    return doc.artboards.empty() ? -1 : 0;   // fall back to the first page
}
// Add a shape authored in WORLD doc-units to artboard `abIndex`, converting it
// to PAGE-RELATIVE storage (Lot 2). The geometry/origin stay as authored; the
// page offset is absorbed into transform.translate so the shape stays visually
// put: world = ab.pos + (translate − ab.pos) + … = the authored world coords.
inline uint64_t AddShapeWorld(Renderer::Document& doc, int abIndex, Renderer::Shape s) {
    if (abIndex >= 0 && abIndex < (int)doc.artboards.size()) {
        const Vec2 po = doc.artboards[(size_t)abIndex].pos;
        s.transform.translate.x -= po.x;
        s.transform.translate.y -= po.y;
    }
    return doc.AddShape(abIndex, std::move(s));
}

// Add a shape whose transform.translate is ALREADY expressed in the target's
// page-relative (display) frame — used by Shift+A, where translate was set to
// (cursor − pageDisplayOrigin − origin). abIndex >= 0 → onto that page (stored
// as-is); abIndex < 0 → a page-less LOOSE object (raw document space). Returns
// the new id and makes it the sole selection.
inline uint64_t AddShapeWorldDisplay(Renderer::Document& doc, int abIndex, Renderer::Shape s) {
    if (abIndex >= 0 && abIndex < (int)doc.artboards.size())
        return doc.AddShape(abIndex, std::move(s));
    // Loose object: assign an id, push to looseShapes, select it.
    s.id = doc.AllocId();
    uint64_t id = s.id;
    doc.looseShapes.push_back(std::move(s));
    doc.SelectOnly(id);
    return id;
}

// Test whether world-point p hits a polyline (inside if closed, else near it).
inline bool HitPoly(const std::vector<Vec2>& poly, bool closed, Vec2 p, float tol) {
    if (poly.size() < 2) return false;
    if (closed) {
        bool in = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            bool cond = ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
                (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                           (poly[j].y - poly[i].y) + poly[i].x);
            if (cond) in = !in;
        }
        if (in) return true;
    }
    size_t segs = closed ? poly.size() : poly.size() - 1;
    for (size_t i = 0; i < segs; ++i) {
        Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        Vec2 ab{ b.x - a.x, b.y - a.y };
        float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = len2 > 1e-6f
            ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f)
            : 0.0f;
        Vec2 c{ a.x + ab.x * t, a.y + ab.y * t };
        if (std::hypot(p.x - c.x, p.y - c.y) <= tol) return true;
    }
    return false;
}

// Hit-test every shape (topmost first) for the Select tool. A shape is hit if
// ANY of its parts is hit. Returns the shape id or 0. `pageOriginOf(abIndex)`
// gives the page's DISPLAY origin (per-viewport layout), so picking matches what
// is shown; `pageVisibleAt(abIndex)` skips pages not shown in this viewport (a
// hidden page is not pickable). Pass them the viewport's CurPageOrigin/Visible.
inline uint64_t PickShape(Renderer::Document& doc, Vec2 p, float zoom,
                          const std::function<Vec2(int)>& pageOriginOf,
                          const std::function<bool(int)>& pageVisibleAt) {
    // Hit-test a shape against EVERY subpath (strand) of EVERY part, so a branched
    // object is pickable on any strand (not just the first).
    auto hitShape = [&](const Renderer::Shape& s, Vec2 po) -> bool {
        for (const Renderer::Part& part : s.parts) {
            float tol = std::max(part.stroke.enabled ? part.stroke.width : 0.0f, 6.0f / zoom);
            const int subs = Renderer::Tessellator::SubpathCount(part);
            for (int sp = 0; sp < subs; ++sp) {
                bool closed = false;
                std::vector<Vec2> poly =
                    Renderer::Tessellator::OutlinePartSub(s, part, sp, zoom, closed, po);
                if (HitPoly(poly, closed, p, tol)) return true;
            }
        }
        return false;
    };
    // Loose (page-less) objects first — they sit on top in raw document space.
    for (auto sit = doc.looseShapes.rbegin(); sit != doc.looseShapes.rend(); ++sit) {
        const Renderer::Shape& s = *sit;
        if (s.visible && hitShape(s, Vec2{0, 0})) return s.id;
    }
    for (int i = (int)doc.artboards.size() - 1; i >= 0; --i) {
        if (!pageVisibleAt(i)) continue;       // hidden page → nothing pickable
        auto& ab = doc.artboards[(size_t)i];
        const Vec2 po = pageOriginOf(i);   // display origin
        for (auto sit = ab.shapes.rbegin(); sit != ab.shapes.rend(); ++sit) {
            const Renderer::Shape& s = *sit;
            if (s.visible && hitShape(s, po)) return s.id;
        }
    }
    return 0;
}

// ── ISOM line-mark preset helpers (shared by the line-mark tool and the
//    symbol-placement path) ────────────────────────────────────────────────
// Which symbols accept which mark, and the spec mm preset for each (size /
// thickness / OM / gap). Sizes are in millimetres and scaled by the module's
// symbolScale at placement time.
// ISOM code ×10 (isomCode): 1010 Contour, 1020 Index contour, 1030 Form line;
// 5131/5132/5140/5150 walls; 5160/5170/5180 fences; 5280/5290 prominent lines;
// 5100 power line, 5110 major power line.

// Is a crossing point (519) allowed on this symbol? Used by the SYMBOL placement
// path (crossing is no longer a tool kind) — walls, fences, prominent lines.
inline bool CrossingAllowedOn(int isomCode) {
    return (isomCode >= 5131 && isomCode <= 5150) ||   // walls
           (isomCode >= 5160 && isomCode <= 5180) ||   // fences
           isomCode == 5280 || isomCode == 5290;       // prominent lines
}

// A symbol that takes a DASH ANCHOR (a phase pin): the dashed / patterned line
// features — form line, ruined walls/fences, paths, ditches, vegetation borders,
// and the regular-pattern walls/fences/prominent lines.
inline bool DashAnchorAllowedOn(int isomCode) {
    switch (isomCode) {
        case 1030:                       // 103 form line (dashed)
        case 1051: case 1052: case 1060: // earth wall / retaining / ruined earth wall
            return true;
        default: break;
    }
    // Walls / fences / prominent lines (regular pattern, incl. ruined + impassable).
    if ((isomCode >= 5131 && isomCode <= 5180) || isomCode == 5280 || isomCode == 5290)
        return true;
    // Vehicle track + footpaths + rides (dashed).
    if (isomCode >= 5040 && isomCode <= 5080)
        return true;
    return false;
}

// The mark KIND the Line-Mark tool drops on a given curve symbol — auto-chosen
// from the symbol: slope tick on contours, pylon on power lines, dash anchor on
// dashed/patterned features. Returns false if the symbol takes no tool-placeable
// mark.
inline bool AutoMarkKindFor(int isomCode, Renderer::LineMarkKind& outKind) {
    // Solid contours take slope ticks; the dashed form line takes a dash anchor.
    if (isomCode == 1010 || isomCode == 1020) {
        outKind = Renderer::LineMarkKind::SlopeTick; return true;
    }
    if (isomCode == 5100 || isomCode == 5110) {
        outKind = Renderer::LineMarkKind::Pylon; return true;
    }
    if (DashAnchorAllowedOn(isomCode)) {
        outKind = Renderer::LineMarkKind::DashAnchor; return true;
    }
    return false;
}

// Fill a mark's spec dimensions (in DOC-units) for the given symbol + scale.
inline void ApplyMarkPreset(Renderer::LineMark& m, int isomCode, float scale) {
    const float s = (scale > 0.01f) ? scale : 1.0f;
    switch (m.kind) {
        case Renderer::LineMarkKind::SlopeTick:
            m.outsideMeasure = true;
            m.size = 0.4f * s;                                  // 0.4 OM
            m.thickness = ((isomCode == 1030) ? 0.10f : 0.14f) * s;  // 103 thinner
            m.gap = 0.0f;
            break;
        case Renderer::LineMarkKind::Crossing:
            m.outsideMeasure = false;
            m.size = 0.5f * s;     // tick half-length (≈1.0 total)
            m.gap  = 1.0f * s;     // the cut in the line
            m.thickness = 0.18f * s;
            break;
        case Renderer::LineMarkKind::Bridge:
            m.outsideMeasure = false;
            m.size = 0.5f * s; m.gap = 2.0f * s; m.thickness = 0.18f * s;
            break;
        case Renderer::LineMarkKind::Pylon:
            // Bar half-length follows the host power line: 511 (twin rails 0.4 CC)
            // overhangs 0.3 OM each side → 0.2 rail + 0.3 = 0.5; 510 (single 0.14
            // line) → 0.07 + 0.3 ≈ 0.37. Thickness 0.2 either way.
            m.outsideMeasure = false;
            m.size = ((isomCode == 5110) ? 0.5f : 0.37f) * s;
            m.gap = 0.0f; m.thickness = 0.2f * s;
            break;
        case Renderer::LineMarkKind::DashAnchor:
            // No geometry; side +1 = centre a dash/element, −1 = centre a gap.
            m.side = +1; m.size = 0.0f; m.gap = 0.0f; m.thickness = 0.0f;
            break;
    }
}

} // namespace App

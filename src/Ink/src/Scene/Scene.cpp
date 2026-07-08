#include "Ink/Scene/Scene.h"

#include "Ink/Geometry/Geometry.h"

#include <algorithm>
#include <cmath>

namespace Ink {

void Scene::GrowBounds(DVec2 p) {
    const Vec2 f{ (float)p.x, (float)p.y };
    if (!boundsValid_) {
        bounds_.min = bounds_.max = f;
        boundsValid_ = true;
        return;
    }
    bounds_.min.x = std::min(bounds_.min.x, f.x);
    bounds_.min.y = std::min(bounds_.min.y, f.y);
    bounds_.max.x = std::max(bounds_.max.x, f.x);
    bounds_.max.y = std::max(bounds_.max.y, f.y);
}

// A group composites as a unit (opens an isolation scope) when it carries
// opacity < 1, a non-Normal blend, isolate, or a clip. Otherwise it is a
// plain pass-through layer (organisation + transform only) and its children
// stay in the parent scope.
ScopeId Scene::OpenScopeIfNeeded(const Document& doc, const Node& group,
                                 ScopeId parent, int depth) {
    NodeId clip = kNullNode;
    if (group.clip) {
        // Clip source = the group's first PATH child (Lot 4 rule).
        for (NodeId c : group.children) {
            if (const Node* ch = doc.Find(c))
                if (ch->kind == NodeKind::Path) { clip = c; break; }
        }
    }
    const bool composites = group.opacity < 0.999f ||
                            group.blend != BlendMode::Normal ||
                            group.isolate || clip != kNullNode;
    if (!composites) return parent;

    CompositeScope s;
    s.node    = group.id;
    s.parent  = parent;
    s.opacity = group.opacity;
    s.blend   = group.blend;
    s.isolate = group.isolate;
    s.clipNode = clip;
    s.depth   = depth;
    scopes_.push_back(s);
    if (depth > maxDepth_) maxDepth_ = depth;
    return (ScopeId)(scopes_.size() - 1);
}

namespace {

constexpr int kMaxInstanceDepth = 8;   // InstanceNode recursion clamp (§5)

// Invert an affine 2×3 (non-degenerate; identity fallback).
DMat23 InvertAffine(const DMat23& m) {
    const double det = m.m[0] * m.m[4] - m.m[1] * m.m[3];
    DMat23 r;
    if (std::abs(det) < 1e-18) return r;
    const double inv = 1.0 / det;
    r.m[0] =  m.m[4] * inv;
    r.m[1] = -m.m[1] * inv;
    r.m[3] = -m.m[3] * inv;
    r.m[4] =  m.m[0] * inv;
    r.m[2] = -(r.m[0] * m.m[2] + r.m[1] * m.m[5]);
    r.m[5] = -(r.m[3] * m.m[2] + r.m[4] * m.m[5]);
    return r;
}

// The instancing transforms a modifier stack produces (docs/Ink/
// DOCUMENT_MODEL.md §6): starting from the identity ("one copy"), each
// enabled modifier expands the current transform set. Local-space transforms
// (composed onto the node's world before emit).
std::vector<DMat23> ExpandModifiers(const Document& doc,
                                    const std::vector<Modifier>& mods) {
    std::vector<DMat23> xf{ DMat23{} };   // identity = the original copy
    for (const Modifier& m : mods) {
        if (!m.enabled) continue;

        if (m.kind == ModifierKind::Array) {
            const int count = m.count < 1 ? 1 : m.count;
            const DMat23 step = m.step.Matrix();
            std::vector<DMat23> out;
            out.reserve(xf.size() * (std::size_t)count);
            for (const DMat23& base : xf) {
                DMat23 acc = base;
                for (int i = 0; i < count; ++i) {
                    out.push_back(acc);
                    acc = acc.Compose(step);   // each copy offset from the last
                }
            }
            xf.swap(out);
        } else if (m.kind == ModifierKind::AlongPath) {
            const Node* path = doc.Find(m.pathRef);
            if (!path || path->kind != NodeKind::Path || path->path.Empty())
                continue;
            // Flatten the target path (coarse tolerance — placement, not
            // rendering) in the path node's LOCAL space, then map into this
            // node's space via (pathWorld relative to host). For Lot 5 the
            // path is assumed a sibling on the same page: sample in path-local
            // and offset by the path's transform translation.
            const auto polys = geom::Flatten(path->path, 1.0);
            if (polys.empty() || polys[0].points.size() < 2) continue;
            const auto& pts = polys[0].points;
            // Cumulative arc length.
            std::vector<double> arc(pts.size(), 0.0);
            for (std::size_t i = 1; i < pts.size(); ++i) {
                const double dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y;
                arc[i] = arc[i-1] + std::sqrt(dx*dx + dy*dy);
            }
            const double total = arc.back();
            const double from = std::min(m.startTrim, total);
            const double to   = std::max(from, total - m.endTrim);
            const double span = to - from;
            if (span <= 0.0) continue;
            const DMat23 pathXf = path->transform.Matrix();

            auto sampleAt = [&](double s, DVec2& pos, double& tanAngle) {
                s = std::clamp(s, 0.0, total);
                std::size_t i = 1;
                while (i < pts.size() && arc[i] < s) ++i;
                if (i >= pts.size()) i = pts.size() - 1;
                const double segLen = arc[i] - arc[i-1];
                const double t = segLen > 1e-9 ? (s - arc[i-1]) / segLen : 0.0;
                pos = { pts[i-1].x + (pts[i].x - pts[i-1].x) * t,
                        pts[i-1].y + (pts[i].y - pts[i-1].y) * t };
                tanAngle = std::atan2(pts[i].y - pts[i-1].y, pts[i].x - pts[i-1].x);
            };

            int n = m.useSpacing
                        ? (m.spacing > 1e-6 ? (int)(span / m.spacing) + 1 : 1)
                        : std::max(1, m.alongCount);
            std::vector<DMat23> out;
            out.reserve(xf.size() * (std::size_t)n);
            for (const DMat23& base : xf) {
                for (int i = 0; i < n; ++i) {
                    const double s = from + (n > 1 ? span * (double)i / (double)(n-1)
                                                   : 0.0);
                    DVec2 pos; double ang = 0.0;
                    sampleAt(s, pos, ang);
                    const DVec2 wp = pathXf.Apply(pos);
                    DMat23 place = DMat23::Translation(wp.x, wp.y);
                    if (m.align == AlongAlign::Tangent) {
                        const double c = std::cos(ang), sn = std::sin(ang);
                        DMat23 rot; rot.m[0]=c; rot.m[1]=-sn; rot.m[3]=sn; rot.m[4]=c;
                        place = place.Compose(rot);
                    }
                    out.push_back(place.Compose(base));
                }
            }
            xf.swap(out);
        }
    }
    return xf;
}

} // namespace

void Scene::EmitNode(const Document& doc, const Node& n,
                     const DMat23& parentWorld, ScopeId scope, int instDepth) {
    if (!n.visible) return;

    // Object PARENTING overrides the layer-tree origin (docs/Ink/
    // DOCUMENT_MODEL.md §2): a parented node's world comes from its parentId
    // chain, not from the group it was walked under. Unparented nodes use the
    // group's `parentWorld` as before.
    const DMat23 world =
        (n.parentId != kNullNode && doc.Find(n.parentId))
            ? doc.WorldTransform(n.parentId).Compose(n.transform.Matrix())
            : parentWorld.Compose(n.transform.Matrix());

    // Instancing modifiers expand the node into many copies at generated
    // transforms; with no modifiers this is a single identity copy. The
    // expansion is LOGICAL — same content, many transforms → grouped drawables
    // that merge into one instanced draw downstream (docs/Ink/DOCUMENT_MODEL §5).
    const bool hasMods = [&]{
        for (const Modifier& m : n.modifiers) if (m.enabled) return true;
        return false;
    }();
    if (!hasMods) {
        EmitContent(doc, n, world, scope, instDepth);
        return;
    }
    for (const DMat23& local : ExpandModifiers(doc, n.modifiers))
        EmitContent(doc, n, world.Compose(local), scope, instDepth);
}

void Scene::EmitContent(const Document& doc, const Node& n, const DMat23& world,
                        ScopeId scope, int instDepth) {
    if (n.kind == NodeKind::Instance) {
        if (instDepth >= kMaxInstanceDepth) return;
        const Node* target = doc.Find(n.targetRef);
        if (!target || target == &n) return;   // missing / self-reference
        // Render the target's OWN content at this instance's world (the
        // target's own transform is part of its identity — apply it).
        EmitNode(doc, *target, world, scope, instDepth + 1);
        return;
    }

    if (n.kind == NodeKind::Group) {
        const ScopeId childScope =
            OpenScopeIfNeeded(doc, n, scope, scopes_[scope].depth + 1);
        // The clip source path defines the scope's mask (SVG clip-path
        // semantics): emitted FIRST as a stencil-only drawable, and NOT
        // painted (skipped in the child walk below).
        const NodeId clipNode = scopes_[childScope].clipNode;
        if (childScope != scope && clipNode != kNullNode) {
            if (const Node* clip = doc.Find(clipNode); clip && !clip->path.Empty()) {
                const DMat23 cw = world.Compose(clip->transform.Matrix());
                Drawable d;
                d.node = clip->id;  d.world = cw;
                d.pathHash = clip->path.Hash();  d.path = &clip->path;
                d.rule = clip->style.fills.empty() ? FillRule::NonZero
                         : clip->style.fills.front().rule;
                d.scope = childScope;
                d.isClipSource = true;
                drawables_.push_back(std::move(d));
            }
        }
        for (NodeId c : n.children) {
            if (c == clipNode) continue;   // the mask never paints
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, world, childScope, instDepth);
        }
        return;
    }

    // Path.
    if (n.path.Empty()) return;
    EmitPath(doc, n, world, scope);
}

const PathData* Scene::ResolveGeometry(const Document& doc, const Node& n,
                                       std::uint64_t& hashOut) {
    // Cached derived path for this node this compile?
    if (auto it = derivedByNode_.find(n.id); it != derivedByNode_.end()) {
        hashOut = it->second->Hash();
        return it->second;
    }
    // Any enabled Boolean modifiers?
    bool hasBool = false;
    for (const Modifier& m : n.modifiers)
        if (m.enabled && m.kind == ModifierKind::Boolean) { hasBool = true; break; }
    if (!hasBool) { hashOut = n.path.Hash(); return &n.path; }

    // Flatten the host outline (node-local) into closed rings.
    auto toRings = [](const std::vector<geom::Polyline>& polys) {
        std::vector<std::vector<DVec2>> rings;
        for (const geom::Polyline& pl : polys)
            if (pl.closed && pl.points.size() >= 3) rings.push_back(pl.points);
        return rings;
    };
    std::vector<std::vector<DVec2>> acc =
        toRings(geom::Flatten(n.path, 0.5));

    for (const Modifier& m : n.modifiers) {
        if (!m.enabled || m.kind != ModifierKind::Boolean) continue;
        const Node* operand = doc.Find(m.operandRef);
        if (!operand || operand->kind != NodeKind::Path || operand->path.Empty())
            continue;
        // Operand geometry expressed in the HOST's local space:
        //   hostLocal = hostWorld⁻¹ ∘ operandWorld ∘ operandLocalPoint.
        // Both share the page; compose operand's transform relative to host's.
        const DMat23 rel = InvertAffine(n.transform.Matrix())
                               .Compose(operand->transform.Matrix());
        auto opPolys = geom::Flatten(operand->path, 0.5);
        std::vector<std::vector<DVec2>> clip;
        for (geom::Polyline& pl : opPolys) {
            if (!pl.closed || pl.points.size() < 3) continue;
            for (DVec2& p : pl.points) p = rel.Apply(p);
            clip.push_back(pl.points);
        }
        geom::BoolOp op = geom::BoolOp::Union;
        switch (m.op) {
            case BooleanOp::Union:     op = geom::BoolOp::Union; break;
            case BooleanOp::Subtract:  op = geom::BoolOp::Subtract; break;
            case BooleanOp::Intersect: op = geom::BoolOp::Intersect; break;
            case BooleanOp::Xor:       op = geom::BoolOp::Xor; break;
        }
        acc = geom::BooleanPolygons(acc, clip, op);
        if (acc.empty()) break;
    }

    // Build a polygonal PathData from the result rings.
    derivedPaths_.emplace_back();
    PathData& out = derivedPaths_.back();
    for (const auto& ring : acc) {
        Subpath sp; sp.closed = true;
        for (const DVec2& p : ring) { Anchor a; a.pos = p; sp.anchors.push_back(a); }
        if (sp.anchors.size() >= 3) out.subpaths.push_back(std::move(sp));
    }
    derivedByNode_[n.id] = &out;
    hashOut = out.Hash();
    return &out;
}

void Scene::EmitPath(const Document& doc, const Node& n, const DMat23& world,
                     ScopeId scope) {
    std::uint64_t pathHash = 0;
    const PathData* geo = ResolveGeometry(doc, n, pathHash);
    if (!geo || geo->Empty()) return;

    // Anchor-box bounds (cheap conservative fit-view input).
    for (const Subpath& sp : geo->subpaths)
        for (const Anchor& a : sp.anchors)
            GrowBounds(world.Apply(a.pos));

    // Fills bottom-up (a pattern fill expands into motif instances), then
    // strokes bottom-up — the unified paint order (docs/Ink/DOCUMENT_MODEL §4).
    for (std::size_t i = 0; i < n.style.fills.size(); ++i) {
        const Fill& f = n.style.fills[i];
        if (!f.enabled) continue;
        if (f.kind == FillKind::Pattern) {
            EmitPattern(doc, f, n, world, scope);
            continue;
        }
        Drawable d;
        d.node = n.id;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;
        d.isStroke = false;  d.pieceIndex = (std::uint8_t)i;
        d.rule = f.rule;  d.color = f.paint.color;
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < n.style.strokes.size(); ++i) {
        const Stroke& s = n.style.strokes[i];
        if (!s.enabled || s.width <= 0.0) continue;
        Drawable d;
        d.node = n.id;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;
        d.isStroke = true;  d.pieceIndex = (std::uint8_t)i;
        d.stroke = s;  d.color = s.paint.color;
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
}

void Scene::EmitPattern(const Document& doc, const Fill& fill, const Node& host,
                        const DMat23& world, ScopeId scope) {
    const Node* motif = doc.Find(fill.pattern.motifRef);
    if (!motif || motif->kind != NodeKind::Path || motif->path.Empty()) return;

    // Local bbox of the host path (the lattice extent). Lot 5 clips the
    // lattice to this bbox; exact clip-to-shape rides on the clip-mask
    // follow-up (docs/Ink/DOCUMENT_MODEL.md §Paints).
    DVec2 lo{ 1e300, 1e300 }, hi{ -1e300, -1e300 };
    for (const Subpath& sp : host.path.subpaths)
        for (const Anchor& a : sp.anchors) {
            lo.x = std::min(lo.x, a.pos.x); lo.y = std::min(lo.y, a.pos.y);
            hi.x = std::max(hi.x, a.pos.x); hi.y = std::max(hi.y, a.pos.y);
        }
    const double sx = fill.pattern.spacingX > 1e-6 ? fill.pattern.spacingX : 40.0;
    const double sy = fill.pattern.spacingY > 1e-6 ? fill.pattern.spacingY : 40.0;
    const std::uint64_t motifHash = motif->path.Hash();
    const Color motifColor = motif->style.fills.empty()
                                 ? Color{ 0, 0, 0, 1 }
                                 : motif->style.fills.front().paint.color;
    const double c = std::cos(fill.pattern.rotation),
                 s = std::sin(fill.pattern.rotation);
    const double sc = fill.pattern.scale;

    // Guard against runaway lattices (a huge shape with tiny spacing).
    const double cols = (hi.x - lo.x) / sx, rows = (hi.y - lo.y) / sy;
    if (cols * rows > 2.0e5) return;

    for (double gy = lo.y + fill.pattern.phaseY; gy <= hi.y; gy += sy)
        for (double gx = lo.x + fill.pattern.phaseX; gx <= hi.x; gx += sx) {
            // Motif placement: host world ∘ translate(cell) ∘ rot·scale.
            DMat23 place = DMat23::Translation(gx, gy);
            DMat23 rs; rs.m[0] = c * sc; rs.m[1] = -s * sc;
                       rs.m[3] = s * sc; rs.m[4] =  c * sc;
            const DMat23 mw = world.Compose(place.Compose(rs));
            Drawable d;
            d.node = host.id;  d.world = mw;
            d.pathHash = motifHash;  d.path = &motif->path;
            d.isStroke = false;
            d.rule = FillRule::NonZero;
            d.color = motifColor;
            d.scope = scope;
            drawables_.push_back(std::move(d));
        }
}

bool Scene::Compile(Document& doc, bool force) {
    if (!force && compiled_ && !doc.HasPendingChanges() &&
        version_ == doc.Version())
        return false;
    doc.DrainChanges();   // Lot 2: exact per-change diffing arrives with the
                          // perf lots; the walk is O(nodes) and change-gated.

    drawables_.clear();
    pageRects_.clear();
    derivedPaths_.clear();
    derivedByNode_.clear();
    scopes_.clear();
    maxDepth_ = 0;
    boundsValid_ = false;
    bounds_ = {};
    pageRects_.reserve(doc.Pages().size());   // stable addresses for borrows

    // Scope 0 is the page root (no isolation) — always present.
    scopes_.push_back(CompositeScope{});

    for (const Page& page : doc.Pages()) {
        // Page substrate: a display backdrop, not a layer
        // (docs/Ink/DOCUMENT_MODEL.md §8) — drawn first within the page.
        if (page.size.x > 0.0 && page.size.y > 0.0) {
            pageRects_.push_back(PathData::Rect(0, 0, page.size.x, page.size.y));
            Drawable d;
            d.node = page.id;
            d.world = DMat23::Translation(page.pos.x, page.pos.y);
            d.path = &pageRects_.back();
            d.pathHash = pageRects_.back().Hash();
            d.rule = FillRule::NonZero;
            d.color = page.background;
            drawables_.push_back(std::move(d));
            GrowBounds({ page.pos.x, page.pos.y });
            GrowBounds({ page.pos.x + page.size.x, page.pos.y + page.size.y });
        }
        const DMat23 pageWorld = DMat23::Translation(page.pos.x, page.pos.y);
        for (NodeId c : page.children)
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, pageWorld, kRootScope, 0);
    }

    version_  = doc.Version();
    compiled_ = true;
    return true;
}

} // namespace Ink

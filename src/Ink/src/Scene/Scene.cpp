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
// (composed onto the node's world before emit) — `hostWorld` is the node's
// resolved world so path-anchored placements can cancel it out.
std::vector<DMat23> ExpandModifiers(const Document& doc, const Node& host,
                                    const DMat23& hostWorld) {
    std::vector<DMat23> xf{ DMat23{} };   // identity = the original copy
    for (const Modifier& m : host.modifiers) {
        if (!m.enabled) continue;

        if (m.kind == ModifierKind::Array) {
            const int count = m.count < 1 ? 1 : m.count;
            DMat23 step = m.step.Matrix();
            if (m.stepSpace == ArrayStepSpace::Parent) {
                // The step is authored in the node's PARENT space (a 10-unit
                // step is 10 document units whatever the node's scale). As a
                // local factor that is L⁻¹ ∘ step ∘ L.
                const DMat23 L = host.transform.Matrix();
                step = InvertAffine(L).Compose(step).Compose(L);
            }
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
            // Copies sit ON the referenced path: the placement CANCELS the
            // node's world (its translation is irrelevant — Blender's
            // instancing rule) and re-anchors on the path's world transform;
            // the node's own rotation/scale still shape each copy. As a local
            // factor: world⁻¹ ∘ pathWorld ∘ T(sample) ∘ R(tangent) ∘ RS(node).
            const DMat23 toPath =
                InvertAffine(hostWorld).Compose(doc.WorldTransform(m.pathRef));
            Transform2D rsOnly = host.transform;
            rsOnly.tx = rsOnly.ty = 0.0;
            const DMat23 rs = rsOnly.Matrix();

            // Sample positions + tangent angles in the PATH's local space.
            std::vector<DVec2>  pos;
            std::vector<double> ang;
            if (m.distribute == AlongDistribute::AtAnchors) {
                // One copy per anchor point; tangent from the neighbours.
                for (const Subpath& sp : path->path.subpaths) {
                    const std::size_t nA = sp.anchors.size();
                    for (std::size_t i = 0; i < nA; ++i) {
                        const DVec2 p = sp.anchors[i].pos;
                        const DVec2 q = sp.anchors[(i + 1) % nA].pos;
                        const DVec2 r = sp.anchors[(i + nA - 1) % nA].pos;
                        DVec2 dir{ q.x - r.x, q.y - r.y };
                        if (nA < 2) dir = { 1.0, 0.0 };
                        else if (!sp.closed && i == 0)       dir = { q.x - p.x, q.y - p.y };
                        else if (!sp.closed && i + 1 == nA)  dir = { p.x - r.x, p.y - r.y };
                        pos.push_back(p);
                        ang.push_back(std::atan2(dir.y, dir.x));
                    }
                }
            } else {
                const auto polys = geom::Flatten(path->path, 1.0);
                if (polys.empty() || polys[0].points.size() < 2) continue;
                const auto& pts = polys[0].points;
                std::vector<double> arc(pts.size(), 0.0);
                for (std::size_t i = 1; i < pts.size(); ++i) {
                    const double dx = pts[i].x - pts[i-1].x,
                                 dy = pts[i].y - pts[i-1].y;
                    arc[i] = arc[i-1] + std::sqrt(dx*dx + dy*dy);
                }
                const double total = arc.back();
                const double from = std::min(m.startTrim, total);
                const double to   = std::max(from, total - m.endTrim);
                const double span = to - from;
                if (span <= 0.0) continue;

                auto sampleAt = [&](double s, DVec2& p, double& tanAngle) {
                    s = std::clamp(s, 0.0, total);
                    std::size_t i = 1;
                    while (i < pts.size() && arc[i] < s) ++i;
                    if (i >= pts.size()) i = pts.size() - 1;
                    const double segLen = arc[i] - arc[i-1];
                    const double t = segLen > 1e-9 ? (s - arc[i-1]) / segLen : 0.0;
                    p = { pts[i-1].x + (pts[i].x - pts[i-1].x) * t,
                          pts[i-1].y + (pts[i].y - pts[i-1].y) * t };
                    tanAngle = std::atan2(pts[i].y - pts[i-1].y,
                                          pts[i].x - pts[i-1].x);
                };
                const int n = m.distribute == AlongDistribute::BySpacing
                    ? (m.spacing > 1e-6 ? (int)(span / m.spacing) + 1 : 1)
                    : std::max(1, m.alongCount);
                for (int i = 0; i < n; ++i) {
                    const double s = from +
                        (n > 1 ? span * (double)i / (double)(n - 1) : 0.0);
                    DVec2 p; double a = 0.0;
                    sampleAt(s, p, a);
                    pos.push_back(p);
                    ang.push_back(a);
                }
            }
            if (pos.empty()) continue;

            std::vector<DMat23> out;
            out.reserve(xf.size() * pos.size());
            for (const DMat23& base : xf)
                for (std::size_t i = 0; i < pos.size(); ++i) {
                    DMat23 place = DMat23::Translation(pos[i].x, pos[i].y);
                    if (m.align == AlongAlign::Tangent) {
                        const double c = std::cos(ang[i]), sn = std::sin(ang[i]);
                        DMat23 rot;
                        rot.m[0] = c; rot.m[1] = -sn; rot.m[3] = sn; rot.m[4] = c;
                        place = place.Compose(rot);
                    }
                    out.push_back(toPath.Compose(place).Compose(rs).Compose(base));
                }
            xf.swap(out);
        }
    }
    return xf;
}

// ── Pattern-clip geometry helpers ────────────────────────────────────────────

// Non-zero winding point-in-rings test.
bool PointInRings(const std::vector<std::vector<DVec2>>& rings, DVec2 p) {
    int winding = 0;
    for (const auto& ring : rings) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const DVec2& a = ring[i];
            const DVec2& b = ring[(i + 1) % n];
            if (a.y <= p.y) {
                if (b.y > p.y &&
                    (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y) > 0)
                    ++winding;
            } else if (b.y <= p.y &&
                       (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y) < 0) {
                --winding;
            }
        }
    }
    return winding != 0;
}

// Distance from `p` to the nearest ring edge.
double DistToRings(const std::vector<std::vector<DVec2>>& rings, DVec2 p) {
    double best = 1e300;
    for (const auto& ring : rings) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const DVec2& a = ring[i];
            const DVec2& b = ring[(i + 1) % n];
            const double ex = b.x - a.x, ey = b.y - a.y;
            const double len2 = ex * ex + ey * ey;
            double t = len2 > 1e-18
                ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double dx = p.x - (a.x + ex * t), dy = p.y - (a.y + ey * t);
            best = std::min(best, dx * dx + dy * dy);
        }
    }
    return std::sqrt(best);
}

// Offset closed rings by `delta` (positive grows the shape) along averaged
// vertex normals with a clamped miter — the same spine-offset idea as the
// stroker's alignment (docs/Ink/GEOMETRY.md §2). v1: no self-intersection
// clean-up (extreme offsets on spiky shapes may fold; acceptable for pattern
// clipping at stroke-edge distances).
std::vector<std::vector<DVec2>>
OffsetRings(const std::vector<std::vector<DVec2>>& rings, double delta) {
    std::vector<std::vector<DVec2>> out;
    for (const auto& ring : rings) {
        const std::size_t n = ring.size();
        if (n < 3) continue;
        auto areaOf = [](const std::vector<DVec2>& r) {
            double a = 0.0;
            for (std::size_t i = 0; i < r.size(); ++i) {
                const DVec2& p = r[i];
                const DVec2& q = r[(i + 1) % r.size()];
                a += p.x * q.y - q.x * p.y;
            }
            return a * 0.5;
        };
        auto offsetBy = [&](double d) {
            std::vector<DVec2> r(n);
            for (std::size_t i = 0; i < n; ++i) {
                const DVec2& prev = ring[(i + n - 1) % n];
                const DVec2& cur  = ring[i];
                const DVec2& next = ring[(i + 1) % n];
                auto edgeN = [](DVec2 a, DVec2 b) {   // left normal of a→b
                    const double ex = b.x - a.x, ey = b.y - a.y;
                    const double l = std::sqrt(ex * ex + ey * ey);
                    return l > 1e-12 ? DVec2{ -ey / l, ex / l } : DVec2{ 0, 0 };
                };
                const DVec2 n1 = edgeN(prev, cur), n2 = edgeN(cur, next);
                DVec2 m{ n1.x + n2.x, n1.y + n2.y };
                const double ml = std::sqrt(m.x * m.x + m.y * m.y);
                if (ml > 1e-12) { m.x /= ml; m.y /= ml; }
                // Miter scale = 1/cos(half angle), clamped.
                const double cosHalf = std::max(0.25, (m.x * n1.x + m.y * n1.y));
                const double k = d / cosHalf;
                r[i] = { cur.x + m.x * k, cur.y + m.y * k };
            }
            return r;
        };
        std::vector<DVec2> r = offsetBy(delta);
        // The left-normal convention grows or shrinks depending on the ring's
        // winding; a positive delta must GROW — flip when it shrank.
        if ((std::abs(areaOf(r)) < std::abs(areaOf(ring))) == (delta > 0.0))
            r = offsetBy(-delta);
        if (r.size() >= 3) out.push_back(std::move(r));
    }
    return out;
}

} // namespace

void Scene::EmitNode(const Document& doc, const Node& n,
                     const DMat23& parentWorld, ScopeId scope, int instDepth,
                     NodeId owner) {
    // Hidden by ANY route — layer visibility or an invisible collection it
    // belongs to (docs/Ink/DOCUMENT_MODEL.md §7). Culling a hidden node never
    // affects the correctness of what IS drawn.
    if (!n.visible || doc.HiddenByCollection(n.id)) return;

    // Object PARENTING overrides the layer-tree origin (docs/Ink/
    // DOCUMENT_MODEL.md §2): a parented node's world comes from its parentId
    // chain, not from the group it was walked under. Unparented nodes use the
    // group's `parentWorld` as before.
    const DMat23 world =
        (n.parentId != kNullNode && doc.Find(n.parentId))
            ? doc.WorldTransform(n.parentId).Compose(n.transform.Matrix())
            : parentWorld.Compose(n.transform.Matrix());

    // ONE composite scope per NODE (never per modifier copy): any node —
    // group, path or instance — carrying opacity<1, a non-Normal blend or
    // isolate composites all its copies as a unit (Layers blend modes work on
    // plain objects too, not only groups).
    const ScopeId nodeScope =
        OpenScopeIfNeeded(doc, n, scope, scopes_[scope].depth + 1);

    // Instancing modifiers expand the node into many copies at generated
    // transforms; with no modifiers this is a single identity copy. The
    // expansion is LOGICAL — same content, many transforms → grouped drawables
    // that merge into one instanced draw downstream (docs/Ink/DOCUMENT_MODEL §5).
    const bool hasMods = [&]{
        for (const Modifier& m : n.modifiers) if (m.enabled) return true;
        return false;
    }();
    if (!hasMods) {
        EmitContent(doc, n, world, nodeScope, instDepth, owner);
        return;
    }
    for (const DMat23& local : ExpandModifiers(doc, n, world))
        EmitContent(doc, n, world.Compose(local), nodeScope, instDepth, owner);
}

void Scene::EmitContent(const Document& doc, const Node& n, const DMat23& world,
                        ScopeId scope, int instDepth, NodeId owner) {
    if (n.kind == NodeKind::Instance) {
        if (instDepth >= kMaxInstanceDepth) return;
        const Node* target = doc.Find(n.targetRef);
        if (!target || target == &n) return;   // missing / self-reference
        // Render the target's OWN content at this instance's world (the
        // target's own transform is part of its identity — apply it).
        // Selection-wise the whole subtree belongs to the OUTERMOST instance.
        EmitNode(doc, *target, world, scope, instDepth + 1,
                 owner != kNullNode ? owner : n.id);
        return;
    }

    if (n.kind == NodeKind::Group) {
        // The scope was opened by EmitNode (one per node, shared by every
        // modifier copy); it is OURS only when it records this node.
        const bool ownScope = scopes_[scope].node == n.id;
        const NodeId clipNode = ownScope ? scopes_[scope].clipNode : kNullNode;
        // The clip source path defines the scope's mask (SVG clip-path
        // semantics): emitted FIRST as a stencil-only drawable, and NOT
        // painted (skipped in the child walk below).
        if (clipNode != kNullNode) {
            if (const Node* clip = doc.Find(clipNode); clip && !clip->path.Empty()) {
                const DMat23 cw = world.Compose(clip->transform.Matrix());
                Drawable d;
                d.node = clip->id;  d.owner = owner != kNullNode ? owner : clip->id;
                d.world = cw;
                d.pathHash = clip->path.Hash();  d.path = &clip->path;
                d.rule = clip->style.fills.empty() ? FillRule::NonZero
                         : clip->style.fills.front().rule;
                d.scope = scope;
                d.isClipSource = true;
                drawables_.push_back(std::move(d));
            }
        }
        for (NodeId c : n.children) {
            if (c == clipNode) continue;   // the mask never paints
            if (const Node* child = doc.Find(c))
                EmitNode(doc, *child, world, scope, instDepth, owner);
        }
        return;
    }

    // Path.
    if (n.path.Empty()) return;
    EmitPath(doc, n, world, scope, owner != kNullNode ? owner : n.id);
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
                     ScopeId scope, NodeId owner) {
    std::uint64_t pathHash = 0;
    const PathData* geo = ResolveGeometry(doc, n, pathHash);
    if (!geo || geo->Empty()) return;

    // Bounds: the Bézier control-point hull (a cubic lies inside the convex
    // hull of its control points, so anchor+handle points give a CONSERVATIVE
    // box), grown into the global fit-view bounds and the per-owner box.
    DRect& nb = nodeBounds_[owner];
    for (const Subpath& sp : geo->subpaths)
        for (const Anchor& a : sp.anchors) {
            const DVec2 p = world.Apply(a.pos);
            GrowBounds(p); nb.Grow(p);
            if (a.hasIn) {
                const DVec2 q = world.Apply({ a.pos.x + a.in.x, a.pos.y + a.in.y });
                GrowBounds(q); nb.Grow(q);
            }
            if (a.hasOut) {
                const DVec2 q = world.Apply({ a.pos.x + a.out.x, a.pos.y + a.out.y });
                GrowBounds(q); nb.Grow(q);
            }
        }
    // Outward stroke extent (world units): Center = w/2, Outside = w,
    // Inside = 0. Conservative for the selection outline / box select.
    {
        double outward = 0.0;
        for (const Stroke& s : n.style.strokes) {
            if (!s.enabled || s.width <= 0.0 ||
                s.widthSpace != WidthSpace::Document) continue;
            const double e = s.align == StrokeAlign::Center ? s.width * 0.5
                           : s.align == StrokeAlign::Outside ? s.width : 0.0;
            outward = std::max(outward, e);
        }
        if (outward > 0.0) {
            const double sx = std::sqrt(world.m[0]*world.m[0] + world.m[3]*world.m[3]);
            const double sy = std::sqrt(world.m[1]*world.m[1] + world.m[4]*world.m[4]);
            nb.Inflate(outward * std::max(sx, sy));
        }
    }

    // Fills bottom-up (a pattern fill expands into motif instances), then
    // strokes bottom-up — the unified paint order (docs/Ink/DOCUMENT_MODEL §4).
    for (std::size_t i = 0; i < n.style.fills.size(); ++i) {
        const Fill& f = n.style.fills[i];
        if (!f.enabled) continue;
        if (f.kind == FillKind::Pattern) {
            EmitPattern(doc, f, n, world, scope, owner);
            continue;
        }
        Drawable d;
        d.node = n.id;  d.owner = owner;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;
        d.isStroke = false;  d.pieceIndex = (std::uint8_t)i;
        d.rule = f.rule;  d.color = f.paint.color;
        d.color.a *= f.opacity;             // layer opacity
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < n.style.strokes.size(); ++i) {
        const Stroke& s = n.style.strokes[i];
        if (!s.enabled || s.width <= 0.0) continue;
        Drawable d;
        d.node = n.id;  d.owner = owner;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;
        d.isStroke = true;  d.pieceIndex = (std::uint8_t)i;
        d.stroke = s;  d.color = s.paint.color;
        d.scope = scope;
        drawables_.push_back(std::move(d));
    }
}

void Scene::EmitPattern(const Document& doc, const Fill& fill, const Node& host,
                        const DMat23& world, ScopeId scope, NodeId owner) {
    const Node* motif = doc.Find(fill.pattern.motifRef);
    if (!motif || motif->kind != NodeKind::Path || motif->path.Empty()) return;
    const PatternFill& pat = fill.pattern;

    // Local bbox of the host path (the lattice extent).
    DVec2 lo{ 1e300, 1e300 }, hi{ -1e300, -1e300 };
    for (const Subpath& sp : host.path.subpaths)
        for (const Anchor& a : sp.anchors) {
            lo.x = std::min(lo.x, a.pos.x); lo.y = std::min(lo.y, a.pos.y);
            hi.x = std::max(hi.x, a.pos.x); hi.y = std::max(hi.y, a.pos.y);
        }
    if (lo.x > hi.x) return;
    const double sx = pat.spacingX > 1e-6 ? pat.spacingX : 40.0;
    const double sy = pat.spacingY > 1e-6 ? pat.spacingY : 40.0;
    const std::uint64_t motifHash = motif->path.Hash();
    Color motifColor = motif->style.fills.empty()
                           ? Color{ 0, 0, 0, 1 }
                           : motif->style.fills.front().paint.color;
    motifColor.a *= fill.opacity;
    const double c = std::cos(pat.rotation), s = std::sin(pat.rotation);
    const double sc = pat.scale;
    DMat23 rs; rs.m[0] = c * sc; rs.m[1] = -s * sc;
               rs.m[3] = s * sc; rs.m[4] =  c * sc;

    // Conservative motif radius (local units, rotation-safe): the anchor/handle
    // extent times the pattern scale. Classifies cells against the clip rings.
    double motifR = 0.0;
    for (const Subpath& sp : motif->path.subpaths)
        for (const Anchor& a : sp.anchors) {
            motifR = std::max(motifR, std::hypot(a.pos.x, a.pos.y));
            if (a.hasIn)
                motifR = std::max(motifR, std::hypot(a.pos.x + a.in.x,
                                                     a.pos.y + a.in.y));
            if (a.hasOut)
                motifR = std::max(motifR, std::hypot(a.pos.x + a.out.x,
                                                     a.pos.y + a.out.y));
        }
    motifR *= std::abs(sc);

    // The clip rings (HOST-LOCAL space): none for Bounds, the flattened host
    // outline for Contour, offset to the widest stroke's inner/outer edge for
    // the stroke-relative modes (the legacy Compositor "fill clip").
    std::vector<std::vector<DVec2>> clipRings;
    if (pat.clip != PatternClip::Bounds) {
        for (auto& pl : geom::Flatten(host.path, 0.5))
            if (pl.closed && pl.points.size() >= 3)
                clipRings.push_back(std::move(pl.points));
        if (clipRings.empty()) return;   // open path — nothing to fill against
        if (pat.clip == PatternClip::StrokeInner ||
            pat.clip == PatternClip::StrokeOuter) {
            double inner = 0.0, outer = 0.0;
            for (const Stroke& st : host.style.strokes) {
                if (!st.enabled || st.width <= 0.0 ||
                    st.widthSpace != WidthSpace::Document) continue;
                const double w = st.width;
                inner = std::min(inner,
                    st.align == StrokeAlign::Center ? -w * 0.5
                    : st.align == StrokeAlign::Inside ? -w : 0.0);
                outer = std::max(outer,
                    st.align == StrokeAlign::Center ? w * 0.5
                    : st.align == StrokeAlign::Outside ? w : 0.0);
            }
            const double off = pat.clip == PatternClip::StrokeInner ? inner : outer;
            if (off != 0.0) clipRings = OffsetRings(clipRings, off);
            if (clipRings.empty()) return;
        }
    }

    // Lattice space: Object pins the lattice to the host's local origin (the
    // pattern follows the shape); Document pins it to the document origin (a
    // moving shape slides over a static field). Everything below iterates in
    // LATTICE space and converts to host-local for the clip tests.
    const bool docAnchor = pat.anchor == PatternAnchor::Document;
    const DMat23 invWorld = InvertAffine(world);
    DVec2 llo = lo, lhi = hi;             // lattice-space bbox of the host
    if (docAnchor) {
        const DVec2 corners[4] = { { lo.x, lo.y }, { hi.x, lo.y },
                                   { hi.x, hi.y }, { lo.x, hi.y } };
        llo = { 1e300, 1e300 }; lhi = { -1e300, -1e300 };
        for (const DVec2& q : corners) {
            const DVec2 w = world.Apply(q);
            llo.x = std::min(llo.x, w.x); llo.y = std::min(llo.y, w.y);
            lhi.x = std::max(lhi.x, w.x); lhi.y = std::max(lhi.y, w.y);
        }
    }
    // Conservative lattice-units motif radius for cell classification: in
    // Document space the world scale applies to the local test radius.
    double invScale = 1.0;
    if (docAnchor) {
        const double r0 = std::hypot(invWorld.m[0], invWorld.m[3]);
        const double r1 = std::hypot(invWorld.m[1], invWorld.m[4]);
        invScale = std::max(r0, r1);
    }
    const double testR = motifR * (docAnchor ? invScale : 1.0);

    // Guard against runaway lattices (a huge shape with tiny spacing).
    const double cols = (lhi.x - llo.x) / sx, rows = (lhi.y - llo.y) / sy;
    if (cols * rows > 2.0e5) return;

    // Index-aligned lattice: cells at phase + k·spacing so the phase is a
    // stable offset, starting one cell before the bbox so boundary motifs
    // whose centre sits just outside still emit (they clip to the rim).
    const double gx0 = std::floor((llo.x - pat.phaseX - motifR) / sx) * sx + pat.phaseX;
    const double gy0 = std::floor((llo.y - pat.phaseY - motifR) / sy) * sy + pat.phaseY;

    for (double gy = gy0; gy <= lhi.y + motifR; gy += sy)
        for (double gx = gx0; gx <= lhi.x + motifR; gx += sx) {
            // Cell centre in host-local space (for the clip classification).
            const DVec2 cellLocal =
                docAnchor ? invWorld.Apply({ gx, gy }) : DVec2{ gx, gy };
            bool clipped = false;                      // boundary cell?
            if (!clipRings.empty()) {
                const bool inside = PointInRings(clipRings, cellLocal);
                const double dist = DistToRings(clipRings, cellLocal);
                if (!inside && dist > testR) continue; // fully outside
                clipped = (dist <= testR);             // straddles the rim
            } else if (cellLocal.x < lo.x || cellLocal.x > hi.x ||
                       cellLocal.y < lo.y || cellLocal.y > hi.y) {
                continue;                              // Bounds mode: bbox only
            }

            // Motif placement: lattice translate ∘ rot·scale, under the host
            // world for Object anchor, in document space for Document anchor.
            const DMat23 place = DMat23::Translation(gx, gy).Compose(rs);
            const DMat23 mw = docAnchor ? place : world.Compose(place);

            if (!clipped) {
                // Interior cell — shares the motif mesh (instanced draw), and
                // carries the motif's strokes too so any shape renders fully.
                Drawable d;
                d.node = host.id;  d.owner = owner;  d.world = mw;
                d.pathHash = motifHash;  d.path = &motif->path;
                d.isStroke = false;
                d.rule = FillRule::NonZero;
                d.color = motifColor;
                d.scope = scope;
                drawables_.push_back(std::move(d));
                for (std::size_t si = 0; si < motif->style.strokes.size(); ++si) {
                    const Stroke& st = motif->style.strokes[si];
                    if (!st.enabled || st.width <= 0.0) continue;
                    Drawable sd;
                    sd.node = host.id;  sd.owner = owner;  sd.world = mw;
                    sd.pathHash = motifHash;  sd.path = &motif->path;
                    sd.isStroke = true;  sd.pieceIndex = (std::uint8_t)si;
                    sd.stroke = st;  sd.color = st.paint.color;
                    sd.color.a *= fill.opacity;
                    sd.scope = scope;
                    drawables_.push_back(std::move(sd));
                }
                continue;
            }

            // Boundary cell — geometrically clipped against the rim (fills
            // only, v1): motif outline ∩ clip rings → a derived path drawn in
            // the CLIP space (host-local for Object, document for Document).
            std::vector<std::vector<DVec2>> motifRings;
            for (auto& pl : geom::Flatten(motif->path, 0.5)) {
                if (!pl.closed || pl.points.size() < 3) continue;
                for (DVec2& p : pl.points) p = place.Apply(p);
                motifRings.push_back(std::move(pl.points));
            }
            if (motifRings.empty()) continue;
            std::vector<std::vector<DVec2>> rim = clipRings;
            if (docAnchor)                        // clip rings → document space
                for (auto& ring : rim)
                    for (DVec2& p : ring) p = world.Apply(p);
            auto cut = geom::BooleanPolygons(motifRings, rim,
                                             geom::BoolOp::Intersect);
            if (cut.empty()) continue;
            derivedPaths_.emplace_back();
            PathData& dp = derivedPaths_.back();
            for (const auto& ring : cut) {
                Subpath sp; sp.closed = true;
                for (const DVec2& p : ring) {
                    Anchor a; a.pos = p; sp.anchors.push_back(a);
                }
                if (sp.anchors.size() >= 3) dp.subpaths.push_back(std::move(sp));
            }
            if (dp.Empty()) { derivedPaths_.pop_back(); continue; }
            Drawable d;
            d.node = host.id;  d.owner = owner;
            // The cut geometry is pre-placed in its lattice space: document
            // space for a Document anchor (identity world), host-local else.
            d.world = docAnchor ? DMat23{} : world;
            d.pathHash = dp.Hash();  d.path = &dp;
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
    nodeBounds_.clear();
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

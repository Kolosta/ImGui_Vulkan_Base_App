#include "Ink/Scene/Scene.h"

#include "Ink/Geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <functional>

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
    // A PATH with children clips them to its OWN fill (Affinity layer rule) —
    // it opens a scope even without opacity/blend so the child clip runs;
    // masks among the children make it composite too.
    const bool pathParent =
        group.kind == NodeKind::Path && !group.children.empty();
    const bool composites = group.opacity < 0.999f ||
                            group.blend != BlendMode::Normal ||
                            group.isolate || clip != kNullNode || pathParent;
    if (!composites) return parent;

    CompositeScope s;
    s.node    = group.id;
    s.parent  = parent;
    s.opacity = group.opacity;
    s.blend   = group.blend;
    s.isolate = group.isolate;
    s.clipNode = clip;
    // A group clip OR a path-parent both mask their contents through the
    // stencil (the mask geometry is emitted as an isClipSource drawable).
    s.hasClipMask = (clip != kNullNode) || pathParent;
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

// The self-copy transforms a modifier stack produces (docs/Ink/
// DOCUMENT_MODEL.md §6): starting from the identity ("one copy"), each
// enabled Array multiplies the current transform set with LOCAL factors
// (composed onto the node's world before emit). AlongPath does NOT expand
// the node itself — it instances a motif OBJECT along the node's spine.
//
// RESOLVED-OBJECT semantics: the whole layout lives in the node's LOCAL
// space, so an Object-mode rotate/scale transforms the RESOLVED ensemble —
// original and copies together, spacing included — exactly as if the
// modifier output were the object (Blender). Translation still never enters
// the factors, so moving the object moves the array as one rigid block.
std::vector<DMat23> ExpandModifiers(const Node& host) {
    std::vector<DMat23> xf{ DMat23{} };   // identity = the original copy

    // A local-space offset factor (the node's rotation/scale then carry the
    // whole layout with the object).
    auto localOffset = [](double ox, double oy) {
        return DMat23::Translation(ox, oy);
    };
    auto spin = [](double ang) {
        const double c = std::cos(ang), s = std::sin(ang);
        DMat23 r;
        r.m[0] = c; r.m[1] = -s; r.m[3] = s; r.m[4] = c;
        return r;
    };
    auto scaleOf = [](double sx, double sy) {
        DMat23 r;
        r.m[0] = sx; r.m[4] = sy;
        return r;
    };
    // Node-local bbox (Line/Relative reads its step in object sizes).
    double bw = 0.0, bh = 0.0;
    {
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        bool any = false;
        for (const Subpath& sp : host.path.subpaths)
            for (const Anchor& a : sp.anchors) {
                any = true;
                x0 = std::min(x0, a.pos.x); y0 = std::min(y0, a.pos.y);
                x1 = std::max(x1, a.pos.x); y1 = std::max(y1, a.pos.y);
            }
        if (any) { bw = x1 - x0; bh = y1 - y0; }
    }

    for (const Modifier& m : host.modifiers) {
        if (!m.enabled || m.kind != ModifierKind::Array) continue;
        std::vector<DMat23> copies;

        if (m.arrayMode == ArrayMode::Transform) {
            // Cumulative composition (spirals/orbits by design), in LOCAL
            // space — the node's rotate/scale carries the whole spiral.
            const int count = m.count < 1 ? 1 : m.count;
            const DMat23 step = m.step.Matrix();
            DMat23 acc;
            copies.reserve((std::size_t)count);
            for (int i = 0; i < count; ++i) {
                copies.push_back(acc);
                acc = acc.Compose(step);
            }
        } else if (m.arrayMode == ArrayMode::Line) {
            // A straight line of copies; rotation/scale spin each instance IN
            // PLACE (positions never couple to them).
            const int count = m.count < 1 ? 1 : m.count;
            double ox = m.step.tx, oy = m.step.ty;
            if (m.lineMode == ArrayLineMode::Relative) {
                ox *= bw;                   // factors of the object's own size
                oy *= bh;
            }
            if (m.lineMode == ArrayLineMode::Endpoint) {
                const double div = count > 1 ? (double)(count - 1) : 1.0;
                ox /= div;                  // translation IS the end point
                oy /= div;
            }
            copies.reserve((std::size_t)count);
            for (int k = 0; k < count; ++k) {
                DMat23 f = localOffset(ox * k, oy * k)
                               .Compose(spin(m.step.rotation * k))
                               .Compose(scaleOf(std::pow(m.step.sx, k),
                                                std::pow(m.step.sy, k)));
                copies.push_back(f);
            }
        } else {   // ArrayMode::Circle
            constexpr double kTau = 6.283185307179586;
            const double sweep = m.circleArc
                                     ? std::clamp(m.circleSweep, 0.0, kTau)
                                     : kTau;
            int count;
            double delta;
            if (m.circleMethod == ArrayCircleMethod::ByCount) {
                count = m.count < 1 ? 1 : m.count;
                delta = m.circleArc
                            ? (count > 1 ? sweep / (double)(count - 1) : 0.0)
                            : sweep / (double)count;
            } else {
                delta = std::max(1e-4, m.circleAngleStep);
                count = m.circleArc
                            ? (int)std::floor(sweep / delta + 1e-9) + 1
                            : (int)std::floor((kTau - 1e-9) / delta) + 1;
                count = std::clamp(count, 1, 10000);
            }
            copies.reserve((std::size_t)count);
            for (int k = 0; k < count; ++k) {
                const double th = delta * (double)k;
                DMat23 f = localOffset(m.circleRadius * std::cos(th),
                                       m.circleRadius * std::sin(th));
                if (m.circleAlign) f = f.Compose(spin(th));
                copies.push_back(f);
            }
        }

        std::vector<DMat23> out;
        out.reserve(xf.size() * copies.size());
        for (const DMat23& base : xf)
            for (const DMat23& c : copies)
                out.push_back(base.Compose(c));
        xf.swap(out);
    }
    return xf;
}

// Sample positions + tangent angles along a path's spine (path-local space)
// for an AlongPath modifier: by count, by arc-length spacing, or one per
// anchor point.
void SampleAlongSpine(const PathData& path, const Modifier& m,
                      std::vector<DVec2>& pos, std::vector<double>& ang) {
    if (m.distribute == AlongDistribute::AtAnchors) {
        for (const Subpath& sp : path.subpaths) {
            const std::size_t nA = sp.anchors.size();
            for (std::size_t i = 0; i < nA; ++i) {
                const DVec2 p = sp.anchors[i].pos;
                const DVec2 q = sp.anchors[(i + 1) % nA].pos;
                const DVec2 r = sp.anchors[(i + nA - 1) % nA].pos;
                DVec2 dir{ q.x - r.x, q.y - r.y };
                if (nA < 2) dir = { 1.0, 0.0 };
                else if (!sp.closed && i == 0)      dir = { q.x - p.x, q.y - p.y };
                else if (!sp.closed && i + 1 == nA) dir = { p.x - r.x, p.y - r.y };
                pos.push_back(p);
                ang.push_back(std::atan2(dir.y, dir.x));
            }
        }
        return;
    }
    const auto polys = geom::Flatten(path, 1.0);
    if (polys.empty() || polys[0].points.size() < 2) return;
    const auto& pts = polys[0].points;
    std::vector<double> arc(pts.size(), 0.0);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const double dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y;
        arc[i] = arc[i-1] + std::sqrt(dx*dx + dy*dy);
    }
    const double total = arc.back();
    const double from = std::min(m.startTrim, total);
    const double to   = std::max(from, total - m.endTrim);
    const double span = to - from;
    if (span <= 0.0) return;

    auto sampleAt = [&](double s, DVec2& p, double& tanAngle) {
        s = std::clamp(s, 0.0, total);
        std::size_t i = 1;
        while (i < pts.size() && arc[i] < s) ++i;
        if (i >= pts.size()) i = pts.size() - 1;
        const double segLen = arc[i] - arc[i-1];
        const double t = segLen > 1e-9 ? (s - arc[i-1]) / segLen : 0.0;
        p = { pts[i-1].x + (pts[i].x - pts[i-1].x) * t,
              pts[i-1].y + (pts[i].y - pts[i-1].y) * t };
        tanAngle = std::atan2(pts[i].y - pts[i-1].y, pts[i].x - pts[i-1].x);
    };
    const int n = m.distribute == AlongDistribute::BySpacing
        ? (m.spacing > 1e-6 ? (int)(span / m.spacing) + 1 : 1)
        : std::max(1, m.alongCount);
    for (int i = 0; i < n; ++i) {
        const double s = from + (n > 1 ? span * (double)i / (double)(n - 1) : 0.0);
        DVec2 p; double a = 0.0;
        sampleAt(s, p, a);
        pos.push_back(p);
        ang.push_back(a);
    }
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

} // namespace

void Scene::EmitNode(const Document& doc, const Node& n,
                     const DMat23& parentWorld, ScopeId scope, int instDepth,
                     NodeId owner, bool forceVisible) {
    // Hidden by ANY route — layer visibility or an invisible collection it
    // belongs to (docs/Ink/DOCUMENT_MODEL.md §7). Culling a hidden node never
    // affects the correctness of what IS drawn. `forceVisible` bypasses the
    // node's OWN flag: an INSTANCE renders its target even when the original
    // is hidden (Blender's linked-duplicate rule) — the target's children
    // still honour their own flags.
    if ((!n.visible && !forceVisible) || doc.HiddenByCollection(n.id)) return;

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

    // Array modifiers expand the node into many copies at generated
    // transforms; with no modifiers this is a single identity copy. The
    // expansion is LOGICAL — same content, many transforms → grouped drawables
    // that merge into one instanced draw downstream (docs/Ink/DOCUMENT_MODEL §5).
    const bool hasArray = [&]{
        for (const Modifier& m : n.modifiers)
            if (m.enabled && m.kind == ModifierKind::Array) return true;
        return false;
    }();
    if (!hasArray) {
        EmitContent(doc, n, world, nodeScope, instDepth, owner);
    } else {
        for (const DMat23& local : ExpandModifiers(n))
            EmitContent(doc, n, world.Compose(local), nodeScope, instDepth, owner);
    }

    // AlongPath modifiers ON A PATH instance a motif OBJECT along this node's
    // own spine (Blender's rule: the modifier lives on the path, the object
    // stays a plain, single object elsewhere). Copies render like pattern
    // motifs: the motif's own translation is ignored (rotation/scale kept),
    // its visibility is irrelevant, and the shared content hash keeps every
    // copy in one instanced draw. Selection/bounds map to THIS node.
    if (n.kind == NodeKind::Path && instDepth < kMaxInstanceDepth) {
        for (const Modifier& m : n.modifiers) {
            if (!m.enabled || m.kind != ModifierKind::AlongPath) continue;
            const Node* motif = doc.Find(m.motifRef);
            if (!motif || motif == &n || n.path.Empty()) continue;
            std::vector<DVec2>  pos;
            std::vector<double> ang;
            SampleAlongSpine(n.path, m, pos, ang);
            Transform2D rsOnly = motif->transform;
            rsOnly.tx = rsOnly.ty = 0.0;
            const DMat23 rs = rsOnly.Matrix();
            for (std::size_t i = 0; i < pos.size(); ++i) {
                DMat23 place = DMat23::Translation(pos[i].x, pos[i].y);
                if (m.align == AlongAlign::Tangent) {
                    const double c = std::cos(ang[i]), sn = std::sin(ang[i]);
                    DMat23 rot;
                    rot.m[0] = c; rot.m[1] = -sn; rot.m[3] = sn; rot.m[4] = c;
                    place = place.Compose(rot);
                }
                EmitContent(doc, *motif, world.Compose(place).Compose(rs),
                            nodeScope, instDepth + 1,
                            owner != kNullNode ? owner : n.id);
            }
        }
    }
}

void Scene::EmitContent(const Document& doc, const Node& n, const DMat23& world,
                        ScopeId scope, int instDepth, NodeId owner) {
    if (n.kind == NodeKind::Instance) {
        if (instDepth >= kMaxInstanceDepth) return;
        const Node* target = doc.Find(n.targetRef);
        if (!target || target == &n) return;   // missing / self-reference
        // Render the target's content at this instance's world — even when
        // the ORIGINAL is hidden (linked duplicates stay visible). The LINKED
        // data is the edit-mode geometry, NOT the original's object transform:
        // by default the target's location/rotation/scale are CANCELLED (they
        // were merely copied once at duplicate time), so moving the original
        // never drags its instances along. Each component can opt back in via
        // the instance's copy flags. The correction pre-composes the FILTERED
        // local against the inverse of the full one; EmitNode then re-applies
        // the full local, netting exactly the filtered components.
        // Selection-wise the whole subtree belongs to the OUTERMOST instance.
        Transform2D ft = target->transform;
        if (!n.instCopyLoc)   { ft.tx = 0.0; ft.ty = 0.0; }
        if (!n.instCopyRot)   { ft.rotation = 0.0; }
        if (!n.instCopyScale) { ft.sx = 1.0; ft.sy = 1.0; }
        const DMat23 corr =
            ft.Matrix().Compose(InvertAffine(target->transform.Matrix()));
        EmitNode(doc, *target, world.Compose(corr), scope, instDepth + 1,
                 owner != kNullNode ? owner : n.id, /*forceVisible=*/true);
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

    // Path. A path with children is an Affinity LAYER; the children are
    // CLIPPED or MASKED against it (docs/Ink/DOCUMENT_MODEL.md §Layers).
    const bool hasChildren = !n.children.empty();
    if (n.path.Empty() && !hasChildren) return;
    const bool layerParent = hasChildren && scopes_[scope].node == n.id;

    // The first MASK child, if any (a mask layer masks the parent's content).
    const Node* maskChild = nullptr;
    if (layerParent)
        for (NodeId c : n.children)
            if (const Node* ch = doc.Find(c); ch && ch->isMask &&
                ch->kind == NodeKind::Path && !ch->path.Empty()) {
                maskChild = ch; break;
            }

    // ── MASK layer ────────────────────────────────────────────────────────
    // The MASK child defines the coverage first (MaskWrite, no colour); the
    // host AND its non-mask children then paint CLIPPED to it, so everything
    // shows only through the mask shape.
    if (layerParent && maskChild) {
        const DMat23 cw = world.Compose(maskChild->transform.Matrix());
        std::uint64_t h = 0;
        const PathData* g = ResolveGeometry(doc, *maskChild, h);
        Drawable d;
        d.node = maskChild->id;
        d.owner = owner != kNullNode ? owner : maskChild->id;
        d.world = cw;  d.pathHash = h;  d.path = g;
        d.rule = maskChild->style.fills.empty() ? FillRule::NonZero
                 : maskChild->style.fills.front().rule;
        d.scope = scope;  d.isClipSource = true;  d.clip = ClipRole::MaskWrite;
        drawables_.push_back(std::move(d));

        if (!n.path.Empty())
            EmitPath(doc, n, world, scope, owner != kNullNode ? owner : n.id,
                     HostClip::Clipped);          // host shows through the mask
        for (NodeId c : n.children) {
            const Node* child = doc.Find(c);
            if (!child || child->isMask) continue;
            EmitNode(doc, *child, world, scope, instDepth, owner);
        }
        // The scope's non-mask drawables that this branch did NOT explicitly
        // tag are routed Clipped by the Compile post-pass.
        return;
    }

    // ── CLIP layer (or plain path) ────────────────────────────────────────
    // The host paints UNCLIPPED and its own fill also serves as the clip mask
    // for the children. Emit the host, then the mask (MaskWrite, no colour),
    // then the children (clipped).
    if (!n.path.Empty())
        EmitPath(doc, n, world, scope, owner != kNullNode ? owner : n.id,
                 layerParent ? HostClip::Unclipped : HostClip::AutoRoute);

    if (layerParent) {
        if (!n.path.Empty()) {
            std::uint64_t hostHash = 0;
            const PathData* hostGeo = ResolveGeometry(doc, n, hostHash);
            const geom::BoolProgram* hostProg = nullptr;
            if (auto it = progByNode_.find(n.id); it != progByNode_.end()) {
                hostProg = it->second; hostHash = hostProg->hash;
            }
            Drawable d;
            d.node = n.id;  d.owner = owner != kNullNode ? owner : n.id;
            d.world = world;  d.pathHash = hostHash;  d.path = hostGeo;
            d.boolProg = hostProg;
            d.rule = n.style.fills.empty() ? FillRule::NonZero
                                           : n.style.fills.front().rule;
            d.scope = scope;  d.isClipSource = true;  d.clip = ClipRole::MaskWrite;
            drawables_.push_back(std::move(d));
        }
        // An empty-geometry layer (a pure container) has no mask → children
        // clip to the page (nothing) — still emitted, just unclipped.
        for (NodeId c : n.children) {
            const Node* child = doc.Find(c);
            if (!child || child->isMask) continue;
            EmitNode(doc, *child, world, scope, instDepth, owner);
        }
    }
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

    // Build the boolean PROGRAM (the render path re-runs it per zoom tier so
    // the outline stays vector-smooth) while evaluating one COARSE result for
    // picking and bounds.
    boolPrograms_.emplace_back();
    geom::BoolProgram& prog = boolPrograms_.back();
    prog.host = &n.path;
    prog.hash = n.path.Hash();

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
        geom::BoolOp op = geom::BoolOp::Union;
        switch (m.op) {
            case BooleanOp::Union:     op = geom::BoolOp::Union; break;
            case BooleanOp::Subtract:  op = geom::BoolOp::Subtract; break;
            case BooleanOp::Intersect: op = geom::BoolOp::Intersect; break;
            case BooleanOp::Xor:       op = geom::BoolOp::Xor; break;
        }
        prog.steps.push_back({ op, &operand->path, rel });
        const std::uint64_t opHash = operand->path.Hash();
        prog.hash = HashBytes(&op, sizeof op, prog.hash);
        prog.hash = HashBytes(&opHash, sizeof opHash, prog.hash);
        prog.hash = HashBytes(rel.m, sizeof rel.m, prog.hash);

        auto opPolys = geom::Flatten(operand->path, 0.5);
        std::vector<std::vector<DVec2>> clip;
        for (geom::Polyline& pl : opPolys) {
            if (!pl.closed || pl.points.size() < 3) continue;
            for (DVec2& p : pl.points) p = rel.Apply(p);
            clip.push_back(pl.points);
        }
        acc = geom::BooleanPolygons(acc, clip, op);
        if (acc.empty()) break;
    }
    progByNode_[n.id] = &prog;

    // Build a polygonal PathData from the coarse result rings.
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
                     ScopeId scope, NodeId owner, HostClip forceClip) {
    std::uint64_t pathHash = 0;
    const PathData* geo = ResolveGeometry(doc, n, pathHash);
    if (!geo || geo->Empty()) return;
    // Affinity layer host: pin the clip role so the post-pass leaves it alone
    // (None = never clipped for a clip layer, Clipped for a mask layer).
    const bool pinClip = forceClip != HostClip::AutoRoute;
    const ClipRole pinnedRole = forceClip == HostClip::Clipped
                                    ? ClipRole::Clipped : ClipRole::None;
    // Boolean-modified nodes render through their PROGRAM (re-evaluated per
    // zoom tier); the drawables keep the coarse `geo` for picking but hash
    // and tessellate the program.
    const geom::BoolProgram* prog = nullptr;
    if (auto it = progByNode_.find(n.id); it != progByNode_.end()) {
        prog = it->second;
        pathHash = prog->hash;
    }

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
            EmitPattern(doc, f, n, geo, pathHash, prog, world, scope, owner, i);
            continue;
        }
        Drawable d;
        d.node = n.id;  d.owner = owner;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;  d.boolProg = prog;
        d.isStroke = false;  d.pieceIndex = (std::uint8_t)i;
        d.ownerPiece = (std::uint8_t)i;  d.ownerPieceStroke = false;
        d.rule = f.rule;  d.color = f.paint.color;
        d.color.a *= f.opacity;             // layer opacity
        d.scope = scope;
        if (pinClip) { d.clip = pinnedRole; d.clipPinned = true; }
        drawables_.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < n.style.strokes.size(); ++i) {
        const Stroke& s = n.style.strokes[i];
        if (!s.enabled || s.width <= 0.0) continue;
        // A stroke whose marks carry SEPARATE objects (Blend / Cut / Instance,
        // or a recoloured Fusion primitive) renders through EmitStrokeMarks.
        // Stroke-coloured Fusion primitives are already in the stroke mesh
        // (the stroker triangulated them), so a plain / stroke-coloured-Fusion
        // stroke takes the normal path below.
        bool needsMarkEmit = false;
        for (const StrokeMark& m : s.marks)
            for (const MarkObject& o : m.objects)
                if ((o.shape == MarkShape::Gap &&
                     (!o.gapStartObjects.empty() || !o.gapEndObjects.empty())) ||
                    (o.shape != MarkShape::Gap &&
                     (o.shape == MarkShape::Instance ||
                      o.mode != MarkObjectMode::Fusion || !o.useStrokeColor)))
                    needsMarkEmit = true;
        if (needsMarkEmit && !pinClip) {
            EmitStrokeMarks(doc, n, s, i, geo, world, scope, owner, 0);
            continue;
        }
        Drawable d;
        d.node = n.id;  d.owner = owner;  d.world = world;
        d.pathHash = pathHash;  d.path = geo;  d.boolProg = prog;
        d.isStroke = true;  d.pieceIndex = (std::uint8_t)i;
        d.ownerPiece = (std::uint8_t)i;  d.ownerPieceStroke = true;
        d.stroke = s;  d.color = s.paint.color;
        d.scope = scope;
        if (pinClip) { d.clip = pinnedRole; d.clipPinned = true; }
        drawables_.push_back(std::move(d));
    }
}

namespace {

// A closed PathData from a ring of node-local points (Blend / Subtract mark
// objects — a Fusion object is triangulated into the stroke mesh instead).
PathData RingToPath(const std::vector<DVec2>& ring) {
    PathData p;
    if (ring.size() < 3) return p;
    Subpath sp; sp.closed = true;
    sp.anchors.reserve(ring.size());
    for (const DVec2& q : ring) { Anchor a; a.pos = q; sp.anchors.push_back(a); }
    p.subpaths.push_back(std::move(sp));
    return p;
}

} // namespace

void Scene::EmitStrokeMarks(const Document& doc, const Node& n, const Stroke& s,
                            std::size_t strokeIndex, const PathData* geo,
                            const DMat23& world, ScopeId scope, NodeId owner,
                            int instDepth) {
    // An object needs a SEPARATE drawable when it is NOT a stroke-coloured
    // Fusion primitive: Blend / Cut / Instance, a RECOLOURED Fusion object, or
    // a Gap that stamps end markers. A stroke that only fuses stroke-coloured
    // primitives needs no isolation scope.
    auto separate = [](const MarkObject& o) {
        if (o.shape == MarkShape::Gap)
            return !o.gapStartObjects.empty() || !o.gapEndObjects.empty();
        return o.shape == MarkShape::Instance ||
               o.mode != MarkObjectMode::Fusion ||
               !o.useStrokeColor;
    };
    // `front` is a BLEND-ONLY choice (which side of the stroke a blend composites
    // on). A Cut MUST sit over the stroke to erase it, and an Instance/recoloured
    // object has no reverse order — so anything that is NOT a Blend is always
    // FRONT, regardless of a stale `front` flag left over from a former Blend.
    auto effFront = [](const MarkObject& o) {
        return o.mode == MarkObjectMode::Blend ? o.front : true;
    };
    // Blend/Cut objects need the stroke's isolation scope (a Cut erases it, a
    // Blend composites against it). This is true for a top-level object OR a
    // gap's start/end SUB-object.
    auto wantsScope = [](const MarkObject& o) {
        return o.shape != MarkShape::Gap && o.mode != MarkObjectMode::Fusion;
    };
    bool needsScope = false;
    for (const StrokeMark& m : s.marks)
        for (const MarkObject& o : m.objects) {
            if (wantsScope(o)) needsScope = true;
            if (o.shape == MarkShape::Gap)
                for (const auto* lst : { &o.gapStartObjects, &o.gapEndObjects })
                    for (const MarkObject& so : *lst)
                        if (wantsScope(so)) needsScope = true;
        }

    ScopeId strokeScope = scope;
    int isoDepth = scopes_[scope].depth;
    if (needsScope) {
        CompositeScope iso;
        iso.node = n.id;  iso.parent = scope;  iso.opacity = 1.0f;
        iso.blend = BlendMode::Normal;  iso.isolate = true;
        iso.depth = scopes_[scope].depth + 1;
        isoDepth = iso.depth;
        maxDepth_ = std::max(maxDepth_, iso.depth);
        strokeScope = (ScopeId)scopes_.size();
        scopes_.push_back(iso);
    }

    // Emit one separate object (Blend / Subtract / Instance). Placed with the
    // SAME helpers (and the SAME fine placement tolerance) the stroker uses for
    // Fusion, so all modes line up exactly. Hard/Bend primitives are PARAMETRIC
    // geometry placed by a transform, so the GeometryCache re-tessellates them
    // per zoom tier (vector-exact at any zoom).
    auto flat = geom::Flatten(*geo, geom::kMarkPlaceTolerance);
    // Total arc length of a flattened subpath (for gap-end marker placement).
    auto subTotal = [&](int sub) -> double {
        if (sub < 0 || sub >= (int)flat.size()) return 0.0;
        const auto& pts = flat[(std::size_t)sub].points;
        const bool cl = flat[(std::size_t)sub].closed;
        const std::size_t nn = pts.size(), sc = cl ? nn : nn - 1;
        double tot = 0.0;
        for (std::size_t i = 0; i < sc; ++i)
            tot += std::hypot(pts[(i+1)%nn].x - pts[i].x, pts[(i+1)%nn].y - pts[i].y);
        return tot;
    };
    std::function<void(const StrokeMark&, const MarkObject&)> emitObject;
    emitObject = [&](const StrokeMark& m, const MarkObject& obj) {
        if (m.sub < 0 || m.sub >= (int)flat.size()) return;
        const geom::Polyline& spine = flat[(std::size_t)m.sub];

        // A Gap cut the base line in the stroker. It draws no fill, but it can
        // stamp full MARKER SUB-OBJECTS at its START and END ends (each a
        // virtual mark there) — emitted through this same path.
        if (obj.shape == MarkShape::Gap) {
            const double tot = subTotal(m.sub);
            if (tot < 1e-9) return;
            const double half = std::max(1e-4, obj.SizeUnits(s.width)) * 0.5;
            const double tc = std::clamp(m.t, 0.0, 1.0) * tot;
            auto stampEnd = [&](double endArc,
                                const std::vector<MarkObject>& objs) {
                if (objs.empty()) return;
                StrokeMark vm = m;
                vm.t = std::clamp(endArc / tot, 0.0, 1.0);
                vm.objects.clear();
                for (const MarkObject& so : objs) {
                    if (so.shape == MarkShape::Gap) continue;  // no nested gaps
                    // A stroke-coloured Fusion sub-object is already baked INTO
                    // the stroke mesh by the stroker (like any Fusion object);
                    // emitting it here too would draw it twice (double alpha).
                    if (so.mode == MarkObjectMode::Fusion &&
                        so.shape != MarkShape::Instance && so.useStrokeColor)
                        continue;
                    emitObject(vm, so);
                }
            };
            stampEnd(tc - half, obj.gapStartObjects);
            stampEnd(tc + half, obj.gapEndObjects);
            return;
        }

        // A non-Normal Blend object composites through its OWN nested scope.
        ScopeId objScope = strokeScope;
        if (obj.mode == MarkObjectMode::Blend && obj.blend != BlendMode::Normal) {
            CompositeScope bs;
            bs.node = n.id;  bs.parent = strokeScope;  bs.opacity = 1.0f;
            bs.blend = obj.blend;  bs.isolate = true;
            bs.depth = isoDepth + 1;
            maxDepth_ = std::max(maxDepth_, bs.depth);
            objScope = (ScopeId)scopes_.size();
            scopes_.push_back(bs);
        }

        if (obj.shape == MarkShape::Instance) {
            const Node* target = doc.Find(obj.nodeRef);
            if (!target || target == &n || instDepth >= 6) return;
            // place = frame at the mark (translate + rotate to the tangent, plus
            // the Bend shear). A single node's arbitrary geometry can't be truly
            // Follow-bent, so an Instance set to Follow uses the Bend shear as its
            // curve-lean — an instance now always gets a real bend transform.
            MarkObject pobj = obj;
            if (pobj.bend == MarkBend::Follow) pobj.bend = MarkBend::Bend;
            // Uniform scale: `size` is the factor (100 % = ×1, or the raw
            // doc-unit value read as a multiplier).
            const double k = obj.sizePercent ? obj.size * 0.01
                                             : std::max(1e-6, obj.size);
            // Bend shear extent: the shear is the tangent turn measured over the
            // shape's half-length along the curve. An instance's meaningful
            // extent is its GEOMETRY's radius × the scale (obj.size is a scale
            // factor for instances, not a length — the default SizeUnits gave a
            // near-zero span, so Bend visually did nothing on instances).
            double instR = 0.0;
            for (const Subpath& tsp : target->path.subpaths)
                for (const Anchor& ta : tsp.anchors)
                    instR = std::max(instR, std::hypot(ta.pos.x, ta.pos.y));
            const double bendExtent = std::max(1e-3, instR * k);
            const DMat23 frame =
                geom::MarkPlaceMatrix(spine, m, pobj, s.width, bendExtent);
            DMat23 scaleM; scaleM.m[0] = k; scaleM.m[4] = k;
            // The instance is anchored at the target's ORIGIN (its transform
            // translation), decoupled like a linked instance: cancel the
            // target's own transform, then place = frame · scale.
            const DMat23 place = frame.Compose(scaleM);
            const DMat23 corr =
                place.Compose(InvertAffine(target->transform.Matrix()));
            EmitNode(doc, *target, world.Compose(corr), objScope,
                     instDepth + 1, owner, /*forceVisible=*/true);
            return;
        }

        const bool erase = obj.mode == MarkObjectMode::Subtract;
        Drawable d;
        d.node = n.id;  d.owner = owner;
        d.isStroke = false;  d.rule = FillRule::NonZero;
        d.pieceIndex = (std::uint8_t)strokeIndex;
        d.ownerPiece = (std::uint8_t)strokeIndex;  d.ownerPieceStroke = true;
        d.scope = objScope;
        if (geom::BendsAlongCurve(obj.bend)) {
            // Bend / Follow bend the outline along the line → a derived ring built
            // ONCE per scene compile at the fine ring tolerance (node-local, so
            // d.world = the node world). Smooth well past normal zoom; only an
            // extreme zoom shows facets (documented limit — Fusion rings, built
            // per tier by the stroker, don't have it).
            std::vector<DVec2> ring;
            if (!geom::MarkFollowContour(spine, m, obj, s.width,
                                         geom::kMarkRingTolerance, ring))
                return;
            markShapes_.push_back(RingToPath(ring));
            const PathData* g = &markShapes_.back();
            if (g->Empty()) { markShapes_.pop_back(); return; }
            d.world = world;  d.path = g;  d.pathHash = g->Hash();
        } else {
            // Hard: a PARAMETRIC primitive placed by an affine transform →
            // re-tessellated per tier, vector-smooth.
            markShapes_.push_back(geom::MarkPrimitiveShape(obj, s.width));
            const PathData* g = &markShapes_.back();
            if (g->Empty()) { markShapes_.pop_back(); return; }
            const DMat23 place = geom::MarkPlaceMatrix(spine, m, obj, s.width);
            d.world = world.Compose(place);  d.path = g;  d.pathHash = g->Hash();
        }
        if (erase) {
            // dst-out strength = the object's opacity: 1 cuts the stroke layer
            // fully, <1 is a PARTIAL erase (the stroke shows through dimmed —
            // the live mark-move preview).
            d.color = Color{ 0, 0, 0, std::clamp(obj.opacity, 0.0f, 1.0f) };
            d.clip = ClipRole::EraseWrite;
            d.clipPinned = true;
        } else {
            d.color = obj.useStrokeColor ? s.paint.color : obj.color;
            d.color.a *= std::clamp(obj.opacity, 0.0f, 1.0f);
        }
        drawables_.push_back(std::move(d));
    };

    // BEHIND objects (a Blend explicitly set Behind) paint before the stroke; the
    // stroke then composites over them (the reverse blend order). Stroke-coloured
    // Fusion primitives are in the stroke mesh already, so they are skipped here.
    for (const StrokeMark& m : s.marks)
        for (const MarkObject& obj : m.objects)
            if (separate(obj) && !effFront(obj))
                emitObject(m, obj);

    // The base stroke.
    {
        Drawable d;
        d.node = n.id;  d.owner = owner;  d.world = world;
        d.pathHash = geo->Hash();  d.path = geo;
        d.isStroke = true;  d.pieceIndex = (std::uint8_t)strokeIndex;
        d.ownerPiece = (std::uint8_t)strokeIndex;  d.ownerPieceStroke = true;
        d.stroke = s;  d.color = s.paint.color;
        d.scope = strokeScope;
        drawables_.push_back(std::move(d));
    }

    // FRONT objects (default; and anything non-Blend, e.g. Cut/Instance) paint
    // over the stroke.
    for (const StrokeMark& m : s.marks)
        for (const MarkObject& obj : m.objects)
            if (separate(obj) && effFront(obj))
                emitObject(m, obj);
}

void Scene::EmitPattern(const Document& doc, const Fill& fill, const Node& host,
                        const PathData* geo, std::uint64_t geoHash,
                        const geom::BoolProgram* geoProg,
                        const DMat23& world, ScopeId scope, NodeId owner,
                        std::size_t fillIndex) {
    const Node* motif = doc.Find(fill.pattern.motifRef);
    if (!motif || motif->kind != NodeKind::Path || motif->path.Empty()) return;
    if (!geo || geo->Empty()) return;
    const PatternFill& pat = fill.pattern;

    // Local bbox of the host geometry (the lattice extent).
    DVec2 lo{ 1e300, 1e300 }, hi{ -1e300, -1e300 };
    for (const Subpath& sp : geo->subpaths)
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
    // `rotation` rotates the whole LATTICE (motifs ride it — Affinity
    // semantics); `motifRotation` spins each motif in place on top of that;
    // `scale` sizes each motif.
    const double c = std::cos(pat.rotation), s = std::sin(pat.rotation);
    const double sc = pat.scale;
    DMat23 latt;  latt.m[0] = c;  latt.m[1] = -s;   // lattice → anchor space
                  latt.m[3] = s;  latt.m[4] =  c;
    DMat23 lattInv; lattInv.m[0] = c;  lattInv.m[1] = s;
                    lattInv.m[3] = -s; lattInv.m[4] = c;
    // Per-motif transform = rotate(motifRotation) ∘ scale.
    const double mc = std::cos(pat.motifRotation), ms = std::sin(pat.motifRotation);
    DMat23 sca;
    sca.m[0] = mc * sc; sca.m[1] = -ms * sc;
    sca.m[3] = ms * sc; sca.m[4] =  mc * sc;

    // Conservative motif radius (local units, rotation-safe): the anchor/handle
    // extent times the pattern scale — used only to CULL cells that cannot
    // touch the clip region (the exact cut is the stencil mask).
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

    // The clipped modes cut the cells with a STENCIL MASK rendered from the
    // host geometry at view tolerance — vector-exact at any zoom, and every
    // cell stays a shared-mesh instance (no per-cell geometry). The widest
    // Document-space stroke's mesh carves (StrokeInner) or extends
    // (StrokeOuter) the mask to that stroke's edge. Coarse rings are kept
    // CPU-side only to cull cells that cannot touch the region.
    const bool useMask = pat.clip != PatternClip::Bounds;
    const Stroke* edgeStroke = nullptr;
    double outward = 0.0;   // cull margin for StrokeOuter
    if (pat.clip == PatternClip::StrokeInner ||
        pat.clip == PatternClip::StrokeOuter) {
        double best = 0.0;
        for (const Stroke& st : host.style.strokes) {
            if (!st.enabled || st.width <= 0.0 ||
                st.widthSpace != WidthSpace::Document) continue;
            if (st.width > best) { best = st.width; edgeStroke = &st; }
        }
        if (edgeStroke)
            outward = edgeStroke->align == StrokeAlign::Center
                          ? edgeStroke->width * 0.5
                      : edgeStroke->align == StrokeAlign::Outside
                          ? edgeStroke->width : 0.0;
    }
    std::vector<std::vector<DVec2>> cullRings;
    if (useMask) {
        for (auto& pl : geom::Flatten(*geo, 1.0))
            if (pl.closed && pl.points.size() >= 3)
                cullRings.push_back(std::move(pl.points));
        if (cullRings.empty()) return;   // open path — nothing to fill against
    }

    // Lattice space: Object pins the lattice to the host's local origin (the
    // pattern follows the shape); Document pins it to the document origin (a
    // moving shape slides over a static field). Everything below iterates in
    // the ROTATED lattice frame and converts to host-local for the cull tests.
    const bool docAnchor = pat.anchor == PatternAnchor::Document;
    const DMat23 invWorld = InvertAffine(world);
    DVec2 llo{ 1e300, 1e300 }, lhi{ -1e300, -1e300 };
    {
        // Host bbox corners → anchor space (host local or document) → the
        // rotated lattice frame.
        const DVec2 corners[4] = { { lo.x, lo.y }, { hi.x, lo.y },
                                   { hi.x, hi.y }, { lo.x, hi.y } };
        for (const DVec2& q : corners) {
            const DVec2 a = docAnchor ? world.Apply(q) : q;
            const DVec2 l = lattInv.Apply(a);
            llo.x = std::min(llo.x, l.x); llo.y = std::min(llo.y, l.y);
            lhi.x = std::max(lhi.x, l.x); lhi.y = std::max(lhi.y, l.y);
        }
    }
    // Conservative lattice-units motif radius for the cull test: in Document
    // space the world scale applies to the local test radius.
    double invScale = 1.0;
    if (docAnchor) {
        const double r0 = std::hypot(invWorld.m[0], invWorld.m[3]);
        const double r1 = std::hypot(invWorld.m[1], invWorld.m[4]);
        invScale = std::max(r0, r1);
    }
    const double testR = (motifR + outward) * (docAnchor ? invScale : 1.0);

    // Guard against runaway lattices (a huge shape with tiny spacing).
    const double cols = (lhi.x - llo.x) / sx, rows = (lhi.y - llo.y) / sy;
    if (cols * rows > 2.0e5) return;

    // ── The mask: host fill (± the stroke band) → stencil, before the cells ──
    if (useMask) {
        Drawable m;
        m.node = host.id;  m.owner = owner;  m.world = world;
        m.pathHash = geoHash;  m.path = geo;  m.boolProg = geoProg;
        m.isStroke = false;
        m.ownerPiece = (std::uint8_t)fillIndex;  m.ownerPieceStroke = false;
        m.rule = fill.rule;
        m.scope = scope;
        m.clip = ClipRole::MaskWrite;
        m.isClipSource = true;
        drawables_.push_back(m);
        if (edgeStroke) {
            Drawable b = m;
            b.isStroke = true;
            b.stroke = *edgeStroke;
            // Inner: carve the stroke band out of the mask (the pattern stops
            // at the stroke's inner edge). Outer: extend the mask to its
            // outer edge.
            b.clip = pat.clip == PatternClip::StrokeInner ? ClipRole::MaskClear
                                                          : ClipRole::MaskWrite;
            drawables_.push_back(std::move(b));
        }
    }

    // Index-aligned lattice: cells at phase + k·spacing so the phase is a
    // stable offset, starting one cell before the bbox so boundary motifs
    // whose centre sits just outside still emit (the mask cuts them).
    const double margin = motifR + outward;
    const double gx0 = std::floor((llo.x - pat.phaseX - margin) / sx) * sx + pat.phaseX;
    const double gy0 = std::floor((llo.y - pat.phaseY - margin) / sy) * sy + pat.phaseY;

    for (double gy = gy0; gy <= lhi.y + margin; gy += sy)
        for (double gx = gx0; gx <= lhi.x + margin; gx += sx) {
            // Cell centre in host-local space (cull test only).
            const DVec2 cellAnchor = latt.Apply({ gx, gy });
            const DVec2 cellLocal =
                docAnchor ? invWorld.Apply(cellAnchor) : cellAnchor;
            if (useMask) {
                if (!PointInRings(cullRings, cellLocal) &&
                    DistToRings(cullRings, cellLocal) > testR)
                    continue;                          // cannot touch the mask
            } else if (cellLocal.x < lo.x - motifR || cellLocal.x > hi.x + motifR ||
                       cellLocal.y < lo.y - motifR || cellLocal.y > hi.y + motifR) {
                continue;                              // Bounds mode: bbox only
            }

            // Motif placement: rotated lattice ∘ cell translate ∘ scale,
            // under the host world for Object anchor, in document space for
            // Document anchor.
            const DMat23 place =
                latt.Compose(DMat23::Translation(gx, gy)).Compose(sca);
            const DMat23 mw = docAnchor ? place : world.Compose(place);

            // Every cell shares the motif mesh (one instanced draw) and
            // carries the motif's strokes too, cut by the mask when clipped.
            const ClipRole role = useMask ? ClipRole::Clipped : ClipRole::None;
            Drawable d;
            d.node = host.id;  d.owner = owner;  d.world = mw;
            d.pathHash = motifHash;  d.path = &motif->path;
            d.isStroke = false;
            d.ownerPiece = (std::uint8_t)fillIndex;  d.ownerPieceStroke = false;
            d.rule = FillRule::NonZero;
            d.color = motifColor;
            d.scope = scope;
            d.clip = role;
            drawables_.push_back(std::move(d));
            for (std::size_t si = 0; si < motif->style.strokes.size(); ++si) {
                const Stroke& st = motif->style.strokes[si];
                if (!st.enabled || st.width <= 0.0) continue;
                Drawable sd;
                sd.node = host.id;  sd.owner = owner;  sd.world = mw;
                sd.pathHash = motifHash;  sd.path = &motif->path;
                sd.isStroke = true;  sd.pieceIndex = (std::uint8_t)si;
                sd.ownerPiece = (std::uint8_t)fillIndex;
                sd.ownerPieceStroke = false;   // the HOST fill owns the cells
                sd.stroke = st;  sd.color = st.paint.color;
                sd.color.a *= fill.opacity;
                sd.scope = scope;
                sd.clip = role;
                drawables_.push_back(std::move(sd));
            }
        }

    // Erase the mask so the next clipped region in this pass starts clean.
    if (useMask) {
        Drawable m;
        m.node = host.id;  m.owner = owner;  m.world = world;
        m.pathHash = geoHash;  m.path = geo;  m.boolProg = geoProg;
        m.isStroke = false;
        m.ownerPiece = (std::uint8_t)fillIndex;  m.ownerPieceStroke = false;
        m.rule = fill.rule;
        m.scope = scope;
        m.clip = ClipRole::MaskClear;
        m.isClipSource = true;
        drawables_.push_back(m);
        if (edgeStroke && pat.clip == PatternClip::StrokeOuter) {
            Drawable b = m;
            b.isStroke = true;
            b.stroke = *edgeStroke;
            drawables_.push_back(std::move(b));
        }
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
    boolPrograms_.clear();
    progByNode_.clear();
    nodeBounds_.clear();
    markShapes_.clear();
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

    // Scope clip masks (SVG clip-path semantics): route each clip scope's
    // content through the stencil — the clip source (emitted FIRST in its
    // scope) writes the mask, the scope's own drawables draw only inside it,
    // and the composite blends the masked result onto the parent. Pattern
    // cells keep their own mask roles (a pattern inside a clip scope tests
    // its pattern mask — the scope clip on those cells is a known v1 limit).
    for (Drawable& d : drawables_) {
        if (!scopes_[d.scope].hasClipMask) continue;
        if (d.clipPinned) continue;              // Affinity layer host — decided
        if (d.clip != ClipRole::None) continue;  // pattern cells, mask writers
        d.clip = d.isClipSource ? ClipRole::MaskWrite : ClipRole::Clipped;
    }

    version_  = doc.Version();
    compiled_ = true;
    return true;
}

} // namespace Ink

#include "Ink/Scene/Scene.h"

#include "Ink/Geometry/Geometry.h"
#include "Ink/Scene/CompGraph.h"

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
//
// This decision is now the Compositing Graph's auto-generator
// (docs/Ink/NODE_GRAPH.md, Ink/Scene/CompGraph.h): ComputeAutoMergeParams
// evaluates the SAME predicate that used to live inline here (clip source,
// Affinity path-parent, Subtract AlongPath cut, Erase-blend pieces), so this
// function's observable output is unchanged — only its mechanism moved,
// which is exactly what makes it safe to swap: TestCompositeScopes and the
// blend_groups bench exercise this same code path and are the regression
// gate. Nothing pins a layer's graph yet (Lot 13+), so the result is always
// what the auto-generator computes.
ScopeId Scene::OpenScopeIfNeeded(const Document& doc, const Node& group,
                                 ScopeId parent, int depth) {
    const CompAutoMergeParams p = ComputeAutoMergeParams(doc, group);
    // Node Graph "Mute" (M) on this layer's BLEND node only (Clip/Mask are
    // separate nodes now, docs/Ink/NODE_GRAPH.md §7, and are never muted):
    // bypasses opacity/blend/isolate specifically, while a clip/mask/cut-
    // modifier/erase-piece trigger still opens the scope on its own — the
    // fields Blend reads are untouched in the Document, so unmuting restores
    // them exactly. Applied HERE (not inside ComputeAutoMergeParams) because
    // BuildAutoGraph needs the UN-muted predicate to still show/edit the
    // Blend node while it's muted.
    const bool blendActive = p.blendTrigger && !group.compBlendMuted;
    if (!blendActive && !p.otherTrigger) return parent;

    CompositeScope s;
    s.node    = group.id;
    s.parent  = parent;
    s.opacity = blendActive ? p.opacity : 1.0f;
    s.blend   = blendActive ? p.blend   : BlendMode::Normal;
    s.isolate = blendActive ? p.isolate : false;
    // A group clip OR a path-parent both mask their contents through the
    // stencil (the mask geometry is emitted as an isClipSource drawable).
    s.clipNode    = p.clipNode;
    s.hasClipMask = p.hasClipMask;
    s.depth   = depth;
    scopes_.push_back(s);
    if (depth > maxDepth_) maxDepth_ = depth;
    return (ScopeId)(scopes_.size() - 1);
}

// A single style piece that composites on its own. It isolates, so whatever it
// paints (or erases) resolves as ONE layer before meeting the fills below —
// which is what lets a pattern's white bands cut through a sibling fill instead
// of merely covering it.
ScopeId Scene::OpenPieceScope(NodeId node, ScopeId parent, BlendMode blend) {
    CompositeScope s;
    s.node    = node;
    s.parent  = parent;
    s.opacity = 1.0f;
    s.blend   = blend;
    s.isolate = true;
    s.depth   = (parent < scopes_.size() ? scopes_[parent].depth : 0) + 1;
    scopes_.push_back(s);
    if (s.depth > maxDepth_) maxDepth_ = s.depth;
    return (ScopeId)(scopes_.size() - 1);
}

// Turn every drawable a style piece just produced into a CUT: it removes what
// is already beneath it in the node's isolation layer instead of painting.
//
// This is why an erasing piece is NOT given a composite scope of its own: a
// child scope resolves at the END of the node, so it would cut the pieces drawn
// ABOVE it as well. ClipRole::EraseWrite is drawn at the piece's own rank with
// the dst-out pipeline, which is exactly "erase what is under me".
//
// The piece's stencil masks are dropped: inside an isolated node an unclipped
// cut reaches nothing outside the shape, because there is no paint there. And
// the paint becomes plain coverage with no colour and no swatch — a mask is not
// an ink and must never turn up on a separation.
void Scene::MakePieceErase(std::size_t begin) {
    for (std::size_t k = begin; k < drawables_.size(); ++k) {
        Drawable& d = drawables_[k];
        // The stencil masks STAY: a pattern or instanced fill that erases still
        // has to stop at its fill-clip edge, so its cells cut through the same
        // mask they would have painted through (ClipRole::EraseClipped).
        if (d.isClipSource) continue;
        // The cut is as strong as the paint was opaque: a half-transparent
        // erase takes half the backdrop away, like every other editor. The
        // alpha already carries the paint colour AND the piece opacity.
        d.clip = (d.clip == ClipRole::Clipped) ? ClipRole::EraseClipped
                                               : ClipRole::EraseWrite;
        d.clipPinned = true;
        d.color      = Color{ 0, 0, 0, std::clamp(d.color.a, 0.0f, 1.0f) };
        d.swatch     = kNullSwatch;
    }
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
    // A preview-only LIBRARY root is deferred out of the main walk (no canvas
    // drawables, no bounds, no picking) and compiled by the dedicated pass at
    // the end of Compile — its drawables render only under a preview filter.
    // An INSTANCE whose target lives in the library still renders (instDepth):
    // the placed copy is normal content, only the library originals are hidden.
    if (!pvPass_ && n.previewOnly && instDepth == 0) {
        pvPending_.push_back({ n.id, parentWorld });
        return;
    }
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

    // AlongPath modifiers ON A PATH distribute content along this node's own
    // spine (Blender's rule: the modifier lives on the path): motif-node
    // INSTANCES (alongShape == Instance) or PRIMITIVE shapes, in GROUPS, on a
    // chosen SIDE, rigidly inclined — the IOF fence-tick family as a
    // modifier. Copies render technically instanced (shared mesh / parametric
    // shape, merged draws). Selection/bounds map to THIS node. Marks are
    // IGNORED here (the stroke-style repeats are the mark-aware variant).
    if (n.kind == NodeKind::Path && instDepth < kMaxInstanceDepth) {
        for (const Modifier& m : n.modifiers) {
            if (!m.enabled || m.kind != ModifierKind::AlongPath) continue;
            if (n.path.Empty()) continue;
            const bool primitive = m.alongShape != MarkShape::Instance;
            const Node* motif = primitive ? nullptr : doc.Find(m.motifRef);
            if (!primitive && (!motif || motif == &n)) continue;

            // Placements: AtAnchors keeps the legacy per-anchor sampling;
            // every other distribution routes through the shared repeat
            // engine (groups, gap/density modes, Inside/Outside sides).
            struct Sample { DVec2 p; double ang; double offset; };
            std::vector<Sample> samples;
            if (m.distribute == AlongDistribute::AtAnchors) {
                std::vector<DVec2>  pos;
                std::vector<double> ang;
                SampleAlongSpine(n.path, m, pos, ang);
                for (std::size_t i = 0; i < pos.size(); ++i) {
                    double off = m.alongSideOffset;
                    if (m.alongSide == RepeatSide::Center) off = 0.0;
                    else if (m.alongSide == RepeatSide::Right ||
                             m.alongSide == RepeatSide::Outside) off = -off;
                    samples.push_back({ pos[i], ang[i], off });
                }
            } else {
                Stroke synth;                    // resolves nothing (% unused)
                synth.width = 1.0;
                StrokeRepeat rep;
                rep.enabled = true;
                rep.shape = primitive ? m.alongShape : MarkShape::Rectangle;
                rep.sizePercent = false;
                rep.size  = std::max(1e-3, m.alongSize);
                rep.width = std::max(1e-3, m.alongWidth);
                rep.rotation = 0.0;              // applied on the frame below
                rep.side = m.alongSide;
                rep.offsetPercent = false;
                // Offset in % resolves against the shape size (there is no
                // stroke width for a modifier).
                rep.sideOffset = m.alongOffsetPercent
                    ? m.alongSideOffset * 0.01 * std::max(1e-3, m.alongSize)
                    : m.alongSideOffset;
                switch (m.distribute) {
                case AlongDistribute::BySpacing:
                    rep.distribute = RepeatDistribute::Pitch;
                    rep.pitch = std::max(1e-3, m.spacing);
                    break;
                case AlongDistribute::ByGap:
                    rep.distribute = RepeatDistribute::Gap;
                    rep.gap = std::max(0.0, m.alongGap);
                    break;
                case AlongDistribute::ByDensity:
                    rep.distribute = RepeatDistribute::Density;
                    rep.density = std::max(1e-3, m.alongDensity);
                    break;
                case AlongDistribute::ByCount:
                default:
                    rep.distribute = RepeatDistribute::Count;
                    rep.count = std::max(1, m.alongCount);
                    break;
                }
                rep.phase = m.alongPhase + m.startTrim;
                rep.groupCount = std::max(1, m.alongGroupCount);
                rep.groupPitch = m.alongGroupPitch;
                const auto flatSp = geom::Flatten(n.path, 0.05);
                for (int sub = 0; sub < (int)flatSp.size(); ++sub) {
                    const auto& poly = flatSp[(std::size_t)sub];
                    if (poly.points.size() < 2) continue;
                    // Arc table (linear tangents suffice for HARD placement).
                    const std::size_t nn = poly.points.size();
                    const std::size_t sc2 = poly.closed ? nn : nn - 1;
                    std::vector<double> arc(sc2 + 1, 0.0);
                    for (std::size_t i2 = 0; i2 < sc2; ++i2) {
                        const DVec2 a2 = poly.points[i2];
                        const DVec2 b2 = poly.points[(i2 + 1) % nn];
                        arc[i2 + 1] = arc[i2] + std::hypot(b2.x - a2.x,
                                                           b2.y - a2.y);
                    }
                    const double subTotal2 = arc[sc2];
                    if (subTotal2 < 1e-9) continue;
                    auto sampleAt = [&](double s2, DVec2& p, double& a3) {
                        s2 = std::clamp(s2, 0.0, subTotal2);
                        std::size_t i2 = 1;
                        while (i2 < arc.size() && arc[i2] < s2) ++i2;
                        if (i2 >= arc.size()) i2 = arc.size() - 1;
                        const double L = arc[i2] - arc[i2 - 1];
                        const double t2 =
                            L > 1e-9 ? (s2 - arc[i2 - 1]) / L : 0.0;
                        const DVec2 a2 = poly.points[i2 - 1];
                        const DVec2 b2 = poly.points[i2 % nn];
                        p = { a2.x + (b2.x - a2.x) * t2,
                              a2.y + (b2.y - a2.y) * t2 };
                        a3 = std::atan2(b2.y - a2.y, b2.x - a2.x);
                    };
                    const double hiTrim = subTotal2 - m.endTrim;
                    for (const geom::RepeatPlacement& rp :
                         geom::RepeatObjectPlacements(poly, synth, rep, sub)) {
                        if (rp.at < m.startTrim - 1e-9 ||
                            rp.at > hiTrim + 1e-9) continue;
                        Sample smp;
                        sampleAt(rp.at, smp.p, smp.ang);
                        smp.offset = rp.offset;
                        samples.push_back(smp);
                    }
                }
            }

            // One shared parametric primitive per modifier (instanced draws).
            const PathData* primShape = nullptr;
            std::uint64_t primHash = 0;
            if (primitive) {
                MarkObject po;
                po.shape = m.alongShape;
                po.sizePercent = false;
                po.size = std::max(1e-3, m.alongSize);
                po.width = std::max(1e-3, m.alongWidth);
                markShapes_.push_back(geom::MarkPrimitiveShape(po, 1.0));
                if (markShapes_.back().Empty()) { markShapes_.pop_back(); continue; }
                primShape = &markShapes_.back();
                primHash = primShape->Hash();
            }
            Transform2D rsOnly;
            if (motif) {
                rsOnly = motif->transform;
                rsOnly.tx = rsOnly.ty = 0.0;
            }
            // The motif's rotation/scale, then the modifier's extra uniform
            // scale of every copy.
            DMat23 rs = rsOnly.Matrix();
            {
                const double ms = std::max(1e-6, m.alongScale);
                DMat23 sc; sc.m[0] = ms; sc.m[4] = ms;
                rs = sc.Compose(rs);
            }
            for (const Sample& smp : samples) {
                const double baseAng =
                    m.align == AlongAlign::Tangent ? smp.ang : 0.0;
                const double a2 = baseAng + m.alongRotation;
                const double c = std::cos(a2), sn = std::sin(a2);
                // Across-the-line offset along the local normal.
                const double na = baseAng + 1.5707963267948966;
                const DVec2 at{ smp.p.x + std::cos(na) * smp.offset,
                                smp.p.y + std::sin(na) * smp.offset };
                DMat23 place = DMat23::Translation(at.x, at.y);
                DMat23 rot;
                rot.m[0] = c; rot.m[1] = -sn; rot.m[3] = sn; rot.m[4] = c;
                place = place.Compose(rot);
                if (primitive) {
                    Drawable d;
                    d.node = n.id;
                    d.owner = owner != kNullNode ? owner : n.id;
                    d.world = world.Compose(place);
                    d.path = primShape;
                    d.pathHash = primHash;
                    d.isStroke = false;
                    d.rule = FillRule::NonZero;
                    d.scope = nodeScope;
                    if (m.alongMode == MarkObjectMode::Subtract) {
                        d.color = Color{ 0, 0, 0,
                            std::clamp(m.alongOpacity, 0.0f, 1.0f) };
                        d.clip = ClipRole::EraseWrite;
                        d.clipPinned = true;
                    } else {
                        d.color = m.alongColor;
                        d.color.a *= std::clamp(m.alongOpacity, 0.0f, 1.0f);
                    }
                    drawables_.push_back(std::move(d));
                } else {
                    EmitContent(doc, *motif, world.Compose(place).Compose(rs),
                                nodeScope, instDepth + 1,
                                owner != kNullNode ? owner : n.id);
                }
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
        // Compositing Graph manual override (docs/Ink/NODE_GRAPH.md, ROADMAP
        // Lot 13): a non-empty `compInputs` replaces the child walk below
        // with a hand-authored reorder/filter of the SAME children —
        // Document::SetCompInputs already dropped anything that wasn't a
        // current child, and the `child->parent != n.id` guard here catches
        // one that moved away SINCE (a structural edit elsewhere never
        // touches compInputs — the stale entry just stops rendering, the
        // same "missing reference" convention every other Document ref
        // follows). Empty (the common case) is untouched Lot-12 behavior.
        if (!n.compInputs.empty()) {
            for (const CompInputOverride& ov : n.compInputs) {
                // Muted (M): skip emission but keep the entry/order intact —
                // "as if the node weren't there", not removed.
                if (ov.muted || ov.node == clipNode) continue;
                const Node* child = doc.Find(ov.node);
                if (!child || child->parent != n.id) continue;
                EmitNode(doc, *child, world, scope, instDepth, owner);
            }
        } else {
            for (NodeId c : n.children) {
                if (c == clipNode) continue;   // the mask never paints
                if (const Node* child = doc.Find(c))
                    EmitNode(doc, *child, world, scope, instDepth, owner);
            }
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
            outward = std::max(outward, s.OuterExtent());
        }
        if (outward > 0.0) {
            const double sx = std::sqrt(world.m[0]*world.m[0] + world.m[3]*world.m[3]);
            const double sy = std::sqrt(world.m[1]*world.m[1] + world.m[4]*world.m[4]);
            nb.Inflate(outward * std::max(sx, sy));
        }
    }

    // ONE paint stack: fills and strokes share a rank, so a stroke can sit under
    // a fill (Style::PaintOrder). Left untouched every piece ranks 0 and the
    // stable order is the historical one — every fill, then every stroke.
    for (const Style::PaintRef& ref : n.style.PaintOrder()) {
      if (!ref.isStroke) {
        const std::size_t i = ref.index;
        const Fill& f = n.style.fills[i];
        if (!f.enabled) continue;
        // An ERASE piece cuts in place (MakePieceErase); any OTHER non-Normal
        // blend needs a layer of its own so it meets the paint below it as a
        // finished thing rather than piece by piece.
        const bool erasePiece = f.blend == BlendMode::Erase;
        const ScopeId fscope =
            (f.blend == BlendMode::Normal || erasePiece)
                ? scope : OpenPieceScope(n.id, scope, f.blend);
        const std::size_t pieceBegin = drawables_.size();
        if (f.kind == FillKind::Pattern) {
            EmitPattern(doc, f, n, geo, pathHash, prog, world, fscope, owner, i);
        } else if (f.kind == FillKind::Instanced) {
            EmitInstancedFill(doc, f, n, geo, pathHash, prog, world, fscope,
                              owner, i);
        } else {
            Drawable d;
            d.node = n.id;  d.owner = owner;  d.world = world;
            d.pathHash = pathHash;  d.path = geo;  d.boolProg = prog;
            d.isStroke = false;  d.pieceIndex = (std::uint8_t)i;
            d.ownerPiece = (std::uint8_t)i;  d.ownerPieceStroke = false;
            d.rule = f.rule;  d.color = f.paint.color;  d.swatch = f.paint.swatch;
            d.color.a *= f.opacity;             // layer opacity
            d.scope = fscope;
            if (pinClip) { d.clip = pinnedRole; d.clipPinned = true; }
            drawables_.push_back(std::move(d));
        }
        if (erasePiece) MakePieceErase(pieceBegin);
        continue;
      }
      {
        const std::size_t i = ref.index;
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
        // A NON-Fusion repeat run emits its own drawables (Blend / Cut objects
        // along the line); Fusion runs are baked into the stroke mesh.
        for (const StrokeRepeat& rr : s.repeats)
            if (rr.enabled && rr.mode != MarkObjectMode::Fusion)
                needsMarkEmit = true;
        // Same rule as the fills: an Erase stroke cuts in place, so a centred
        // one opens a real hole down the middle of the paint beneath it and
        // leaves the pieces above it alone.
        const bool eraseStroke = s.blend == BlendMode::Erase;
        const ScopeId sscope =
            (s.blend == BlendMode::Normal || eraseStroke)
                ? scope : OpenPieceScope(n.id, scope, s.blend);
        const std::size_t pieceBegin = drawables_.size();
        if (needsMarkEmit && !pinClip) {
            EmitStrokeMarks(doc, n, s, i, geo, world, sscope, owner, 0);
        } else {
            Drawable d;
            d.node = n.id;  d.owner = owner;  d.world = world;
            d.pathHash = pathHash;  d.path = geo;  d.boolProg = prog;
            d.isStroke = true;  d.pieceIndex = (std::uint8_t)i;
            d.ownerPiece = (std::uint8_t)i;  d.ownerPieceStroke = true;
            d.stroke = s;  d.color = s.paint.color;  d.swatch = s.paint.swatch;
            d.scope = sscope;
            if (pinClip) { d.clip = pinnedRole; d.clipPinned = true; }
            drawables_.push_back(std::move(d));
        }
        if (eraseStroke) MakePieceErase(pieceBegin);
      }
    }
}

namespace {

// (Spine sampling now goes through geom::SampleSpineFrame, so both the baked
// and the Blend/Cut repeats orient by the same MarkOrient rule.)

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
    for (const StrokeRepeat& rr : s.repeats)
        if (rr.enabled && rr.mode != MarkObjectMode::Fusion) needsScope = true;

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
            // Uniform scale: `size` is the factor (100 % = ×1, or the raw
            // doc-unit value read as a multiplier).
            const double k = obj.sizePercent ? obj.size * 0.01
                                             : std::max(1e-6, obj.size);
            // Bend / Follow: an affine can't truly bend arbitrary geometry, so
            // the instance's PATH is DERIVED through the same curve frame the
            // primitive rings use (perpendicular bounding sides on Bend, fully
            // curved edges on Follow) and its fills/strokes emit as their own
            // drawables — the exact transformation circles/diamonds/rectangles
            // get. Rebuilt per scene compile (documented cost; Hard instances
            // keep the shared-geometry fast path below).
            const bool instBend = geom::BendsAlongCurve(obj.bend);
            const bool instGeom =
                target->kind == NodeKind::Path && !target->path.Empty();
            // BLEND + HARD/Chord: keep the true-instance fast path (shared
            // geometry, own colours, merged draws) — a Blend composites the
            // real instance. Everything else (Fusion silhouette, Subtract
            // erase, any BENT instance) needs the PLACED geometry below.
            if (obj.mode == MarkObjectMode::Blend && !instBend) {
                const DMat23 frame =
                    geom::MarkPlaceMatrix(spine, m, obj, s.width);
                DMat23 scaleM; scaleM.m[0] = k; scaleM.m[4] = k;
                const DMat23 place = frame.Compose(scaleM);
                const DMat23 corr =
                    place.Compose(InvertAffine(target->transform.Matrix()));
                EmitNode(doc, *target, world.Compose(corr), objScope,
                         instDepth + 1, owner, /*forceVisible=*/true);
                return;
            }
            if (instGeom) {
                // Build the PLACED instance geometry (node-local; world = the
                // node world): Bend/Follow derive it through the curve frame,
                // Hard/Chord transform the target path by the rigid (or chord)
                // frame · scale.
                PathData placed;
                if (instBend) {
                    placed = geom::MarkBendPath(spine, m, obj, s.width,
                                                target->path, k,
                                                geom::kMarkRingTolerance);
                } else {
                    const DMat23 frame =
                        geom::MarkPlaceMatrix(spine, m, obj, s.width);
                    DMat23 scaleM; scaleM.m[0] = k; scaleM.m[4] = k;
                    const DMat23 place = frame.Compose(scaleM);
                    placed = target->path;
                    for (Subpath& sp : placed.subpaths)
                        for (Anchor& a : sp.anchors) {
                            const DVec2 p = place.Apply(a.pos);
                            // Handles are RELATIVE → linear part only.
                            const DVec2 hin{ a.pos.x + a.in.x, a.pos.y + a.in.y };
                            const DVec2 hout{ a.pos.x + a.out.x,
                                              a.pos.y + a.out.y };
                            const DVec2 pin = place.Apply(hin);
                            const DVec2 pout = place.Apply(hout);
                            a.pos = p;
                            a.in  = { pin.x - p.x, pin.y - p.y };
                            a.out = { pout.x - p.x, pout.y - p.y };
                        }
                }
                if (placed.Empty()) return;
                markShapes_.push_back(std::move(placed));
                const PathData* g = &markShapes_.back();
                const std::uint64_t gh = g->Hash();
                auto baseDrawable = [&]() {
                    Drawable d;
                    d.node = n.id;  d.owner = owner;  d.world = world;
                    d.path = g;     d.pathHash = gh;
                    d.pieceIndex = (std::uint8_t)strokeIndex;
                    d.ownerPiece = (std::uint8_t)strokeIndex;
                    d.ownerPieceStroke = true;
                    d.scope = objScope;
                    return d;
                };
                const float op = std::clamp(obj.opacity, 0.0f, 1.0f);
                if (obj.mode == MarkObjectMode::Subtract) {
                    // Erase the instance's SILHOUETTE (its fill area).
                    Drawable d = baseDrawable();
                    d.isStroke = false;  d.rule = FillRule::NonZero;
                    d.color = Color{ 0, 0, 0, op };
                    d.clip = ClipRole::EraseWrite;
                    d.clipPinned = true;
                    drawables_.push_back(std::move(d));
                } else if (obj.mode == MarkObjectMode::Fusion) {
                    // FUSION: the instance's SHAPE in the STROKE colour (its own
                    // colours are ignored) — a silhouette fused onto the line.
                    Color sc = s.paint.color;  sc.a *= op;
                    const SwatchId scSw = s.paint.swatch;
                    Drawable d = baseDrawable();
                    d.isStroke = false;  d.rule = FillRule::NonZero;
                    d.color = sc;  d.swatch = scSw;
                    drawables_.push_back(std::move(d));
                    for (const Stroke& ts : target->style.strokes) {
                        if (!ts.enabled || ts.width <= 0.0) continue;
                        Drawable ds = baseDrawable();
                        ds.isStroke = true;
                        ds.stroke = ts;
                        ds.stroke.width = ts.width * k;
                        ds.stroke.marks.clear();
                        ds.stroke.repeats.clear();
                        ds.color = sc;  ds.swatch = scSw;   // stroke colour too
                        drawables_.push_back(std::move(ds));
                    }
                } else {   // Blend + Bent: the instance's OWN render, bent.
                    for (const Fill& f : target->style.fills) {
                        if (!f.enabled || f.kind != FillKind::Solid) continue;
                        Drawable d = baseDrawable();
                        d.isStroke = false;  d.rule = f.rule;
                        d.color = f.paint.color;  d.swatch = f.paint.swatch;
                        d.color.a *= f.opacity * op;
                        drawables_.push_back(std::move(d));
                    }
                    for (const Stroke& ts : target->style.strokes) {
                        if (!ts.enabled || ts.width <= 0.0) continue;
                        Drawable d = baseDrawable();
                        d.isStroke = true;
                        d.stroke = ts;
                        d.stroke.width = ts.width * k;
                        d.stroke.marks.clear();
                        d.stroke.repeats.clear();
                        d.color = ts.paint.color;  d.swatch = ts.paint.swatch;
                        d.color.a *= op;
                        drawables_.push_back(std::move(d));
                    }
                }
            }
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
            d.color  = obj.useStrokeColor ? s.paint.color  : obj.color;
            d.swatch = obj.useStrokeColor ? s.paint.swatch : obj.swatch;
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
        d.stroke = s;  d.color = s.paint.color;  d.swatch = s.paint.swatch;
        d.scope = strokeScope;
        drawables_.push_back(std::move(d));
    }

    // FRONT objects (default; and anything non-Blend, e.g. Cut/Instance) paint
    // over the stroke.
    for (const StrokeMark& m : s.marks)
        for (const MarkObject& obj : m.objects)
            if (separate(obj) && effFront(obj))
                emitObject(m, obj);

    // ── REPEAT runs that are NOT stroke-coloured Fusion ──────────────────────
    // (Blend / Cut / recoloured objects along the line — Fusion runs are baked
    // into the stroke mesh by the stroker.) Every placement routes through
    // emitObject: the Hard parametric shape is identical for the whole run, so
    // the GeometryCache dedups it and the GPU merges the run into instanced
    // draws.
    for (const StrokeRepeat& rep : s.repeats) {
        if (!rep.enabled) continue;
        if (rep.mode == MarkObjectMode::Fusion) continue;   // baked by stroker
        MarkObject obj;
        obj.shape = rep.shape;
        obj.mode = rep.mode;
        obj.blend = rep.blend;
        obj.bend = MarkBend::Hard;
        obj.orient = rep.orient;
        obj.size = rep.size;
        obj.width = rep.width;
        obj.sizePercent = rep.sizePercent;
        obj.rotation = rep.rotation;
        obj.color = rep.color;
        obj.swatch = rep.swatch;
        obj.useStrokeColor = rep.useStrokeColor;
        obj.opacity = rep.opacity;
        obj.sideInherit = true;   // the synthetic mark carries the offset
        const bool isLine = rep.shape == MarkShape::Line;
        const bool erase = rep.mode == MarkObjectMode::Subtract;
        const double lhu = std::max(1e-6, rep.SizeUnits(s.width));
        const double lhv = std::max(1e-6, rep.WidthUnits(s.width));
        const bool centred = rep.side == RepeatSide::Center;
        const double rc = std::cos(rep.rotation), rs = std::sin(rep.rotation);
        for (int sub = 0; sub < (int)flat.size(); ++sub) {
            const double subTot = subTotal(sub);
            if (subTot < 1e-9) continue;
            const geom::Polyline& poly = flat[(std::size_t)sub];
            const auto places = geom::RepeatObjectPlacements(poly, s, rep, sub);
            for (const geom::RepeatPlacement& rp : places) {
                if (isLine) {
                    // A Line's geometry depends on its offset (offset-start) —
                    // build its NODE-space corners, clip the overflow past the
                    // path if asked, and emit as a Blend/Cut/recoloured drawable
                    // (Fusion-stroke-coloured Lines are baked by the stroker).
                    DVec2 p0, t0;
                    geom::SampleSpineFrame(
                        poly, std::clamp(rp.at, 0.0, subTot),
                        rep.orient == MarkOrient::Smoothed, p0, t0);
                    const DVec2 n0{ -t0.y, t0.x };
                    const DVec2 at{ p0.x + n0.x * rp.offset,
                                    p0.y + n0.y * rp.offset };
                    const double ux = t0.x * rc - n0.x * rs;
                    const double uy = t0.y * rc - n0.y * rs;
                    const double vx = t0.x * rs + n0.x * rc;
                    const double vy = t0.y * rs + n0.y * rc;
                    const PathData lp = geom::MarkLineShape(
                        lhu, lhv, rp.offset, rp.dir, centred, rep.lineJoin);
                    if (lp.subpaths.empty()) continue;
                    std::vector<DVec2> corners;
                    for (const Anchor& a : lp.subpaths.front().anchors)
                        corners.push_back({ at.x + ux * a.pos.x + vx * a.pos.y,
                                            at.y + uy * a.pos.x + vy * a.pos.y });
                    if (corners.size() < 3) continue;
                    PathData linePath;
                    if (rep.lineClip && !centred) {
                        // Cut the far-side overflow along the real path curve.
                        const double ext =
                            2.0 * lhu + lhv + std::abs(rp.offset);
                        for (auto& r : geom::ClipPolygonToPathSide(
                                 corners, poly, rp.at, -rp.dir, ext)) {
                            if (r.size() < 3) continue;
                            Subpath sp; sp.closed = true;
                            for (const DVec2& q : r) {
                                Anchor a2; a2.pos = q; sp.anchors.push_back(a2);
                            }
                            linePath.subpaths.push_back(std::move(sp));
                        }
                    } else {
                        linePath = RingToPath(corners);
                    }
                    if (linePath.Empty()) continue;
                    markShapes_.push_back(std::move(linePath));
                    const PathData* g = &markShapes_.back();
                    if (g->Empty()) { markShapes_.pop_back(); continue; }
                    Drawable d;
                    d.node = n.id;  d.owner = owner;
                    d.world = world;                 // corners already node-local
                    d.path = g;  d.pathHash = g->Hash();
                    d.isStroke = false;  d.rule = FillRule::NonZero;
                    d.pieceIndex = (std::uint8_t)strokeIndex;
                    d.ownerPiece = (std::uint8_t)strokeIndex;
                    d.ownerPieceStroke = true;
                    d.scope = strokeScope;
                    if (erase) {
                        d.color = Color{ 0, 0, 0,
                            std::clamp(rep.opacity, 0.0f, 1.0f) };
                        d.clip = ClipRole::EraseWrite;
                        d.clipPinned = true;
                    } else {
                        d.color  = rep.useStrokeColor ? s.paint.color  : rep.color;
                        d.swatch = rep.useStrokeColor ? s.paint.swatch : rep.swatch;
                        d.color.a *= std::clamp(rep.opacity, 0.0f, 1.0f);
                    }
                    drawables_.push_back(std::move(d));
                    continue;
                }
                StrokeMark vm;
                vm.sub = sub;
                vm.t = rp.at / subTot;
                vm.side = rp.offset > 1e-12 ? MarkSide::Left
                        : rp.offset < -1e-12 ? MarkSide::Right
                                             : MarkSide::Center;
                vm.offset = std::abs(rp.offset);
                vm.offsetPercent = false;
                emitObject(vm, obj);
            }
        }
    }
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

    // Local bbox of the host geometry (the lattice extent). The Bézier/NURBS
    // curve can bulge PAST the anchor points, so the box must include the
    // control HANDLES (a cubic lies inside its control-point hull) — else the
    // lattice stops short and the pattern misses the shape's rounded edges.
    DVec2 lo{ 1e300, 1e300 }, hi{ -1e300, -1e300 };
    for (const Subpath& sp : geo->subpaths)
        for (const Anchor& a : sp.anchors) {
            auto grow = [&](double x, double y) {
                lo.x = std::min(lo.x, x); lo.y = std::min(lo.y, y);
                hi.x = std::max(hi.x, x); hi.y = std::max(hi.y, y);
            };
            grow(a.pos.x, a.pos.y);
            if (a.hasIn)  grow(a.pos.x + a.in.x,  a.pos.y + a.in.y);
            if (a.hasOut) grow(a.pos.x + a.out.x, a.pos.y + a.out.y);
        }
    if (lo.x > hi.x) return;
    const double sx = pat.spacingX > 1e-6 ? pat.spacingX : 40.0;
    const double sy = pat.spacingY > 1e-6 ? pat.spacingY : 40.0;
    const std::uint64_t motifHash = motif->path.Hash();
    Color motifColor = motif->style.fills.empty()
                           ? Color{ 0, 0, 0, 1 }
                           : motif->style.fills.front().paint.color;
    const SwatchId motifSwatch = motif->style.fills.empty()
                           ? kNullSwatch
                           : motif->style.fills.front().paint.swatch;
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
            outward = edgeStroke->OuterExtent();
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
            d.color = motifColor;  d.swatch = motifSwatch;
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
                sd.stroke = st;  sd.color = st.paint.color;  sd.swatch = st.paint.swatch;
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

namespace {

// Deterministic per-instance RNG: splitmix64. Mix a seed with an index to draw
// an independent stream — reproducible across platforms (no <random> state).
inline std::uint64_t Mix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
// A uniform double in [0,1) from a 64-bit state.
inline double U01(std::uint64_t h) {
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);   // 2^53
}

// The primitive ring for an instanced-fill element — node-local, centred at the
// origin, unrotated. Béziers are preserved (re-tessellated per tier). Returns an
// empty path for a degenerate element (a bad triangle), which the caller skips.
PathData InstElementPath(const InstElement& e) {
    const double a = std::max(1e-6, e.sizeA);
    switch (e.shape) {
        case InstShape::Circle:
            return PathData::Ellipse(0, 0, a, a);
        case InstShape::Rectangle: {
            const double b = std::max(1e-6, e.sizeB);
            return PathData::Rect(-a, -b, a * 2.0, b * 2.0);
        }
        case InstShape::Diamond: {
            const double b = std::max(1e-6, e.sizeB);
            return PathData::Polygon(
                { { a, 0 }, { 0, b }, { -a, 0 }, { 0, -b } }, true);
        }
        case InstShape::HalfCircle: {
            PathData p; Subpath sp; sp.closed = true;
            const double k = 0.5522847498307936 * a;   // circle kappa
            Anchor a0; a0.pos = { -a, 0 }; a0.hasOut = true; a0.out = { 0, k };
            Anchor a1; a1.pos = { 0, a };
            a1.hasIn = true;  a1.in  = { -k, 0 };
            a1.hasOut = true; a1.out = { k, 0 };
            Anchor a2; a2.pos = { a, 0 }; a2.hasIn = true; a2.in = { 0, k };
            sp.anchors = { a0, a1, a2 };
            p.subpaths.push_back(std::move(sp));
            return p;
        }
        case InstShape::Triangle: default: break;
    }
    // Triangle by three side lengths (SSS). Sides: P0P1 = sizeA, P1P2 = sizeB,
    // P2P0 = sizeC. Solve P2, then centre on the centroid.
    const double sA = std::max(1e-6, e.sizeA);   // base P0→P1
    const double sB = std::max(1e-6, e.sizeB);   // P1→P2
    const double sC = std::max(1e-6, e.sizeC);   // P2→P0
    const double x = (sC * sC - sB * sB + sA * sA) / (2.0 * sA);
    const double y2 = sC * sC - x * x;
    if (y2 <= 1e-9) return PathData{};           // degenerate (violates SSS)
    const double y = std::sqrt(y2);
    const double cx = (0.0 + sA + x) / 3.0;
    const double cy = (0.0 + 0.0 + y) / 3.0;
    return PathData::Polygon({ { -cx, -cy }, { sA - cx, -cy }, { x - cx, y - cy } },
                             true);
}

// A conservative half-extent of an element (node-local units) for cull margins.
double InstElementRadius(const InstElement& e) {
    switch (e.shape) {
        case InstShape::Circle:     return std::max(1e-6, e.sizeA);
        case InstShape::Rectangle:  return std::hypot(e.sizeA, e.sizeB);
        case InstShape::Diamond:    return std::max(e.sizeA, e.sizeB);
        case InstShape::HalfCircle: return std::max(1e-6, e.sizeA);
        default: break;
    }
    return std::max({ e.sizeA, e.sizeB, e.sizeC });   // Triangle (over-estimate)
}

} // namespace

void Scene::EmitInstancedFill(const Document& /*doc*/, const Fill& fill,
                              const Node& host, const PathData* geo,
                              std::uint64_t geoHash,
                              const geom::BoolProgram* geoProg,
                              const DMat23& world, ScopeId scope, NodeId owner,
                              std::size_t fillIndex) {
    if (!geo || geo->Empty()) return;
    const InstancedFill& in = fill.instanced;

    // Enabled elements (shapes) and line-sets — nothing enabled → done.
    std::vector<std::size_t> enabledIdx, lineIdx;
    for (std::size_t i = 0; i < in.elements.size(); ++i)
        if (in.elements[i].enabled) enabledIdx.push_back(i);
    for (std::size_t i = 0; i < in.lines.size(); ++i)
        if (in.lines[i].enabled) lineIdx.push_back(i);
    if (enabledIdx.empty() && lineIdx.empty()) return;

    // Host local bbox (the layout extent) — include the Bézier/NURBS control
    // HANDLES so the box covers the curve where it bulges past the anchors
    // (else the field stops short of the shape's rounded edges).
    DVec2 lo{ 1e300, 1e300 }, hi{ -1e300, -1e300 };
    for (const Subpath& sp : geo->subpaths)
        for (const Anchor& a : sp.anchors) {
            auto grow = [&](double x, double y) {
                lo.x = std::min(lo.x, x); lo.y = std::min(lo.y, y);
                hi.x = std::max(hi.x, x); hi.y = std::max(hi.y, y);
            };
            grow(a.pos.x, a.pos.y);
            if (a.hasIn)  grow(a.pos.x + a.in.x,  a.pos.y + a.in.y);
            if (a.hasOut) grow(a.pos.x + a.out.x, a.pos.y + a.out.y);
        }
    if (lo.x > hi.x) return;

    const bool docAnchor = in.anchor == PatternAnchor::Document;
    const DMat23 invWorld = InvertAffine(world);
    double invScale = 1.0;
    if (docAnchor) {
        const double r0 = std::hypot(invWorld.m[0], invWorld.m[3]);
        const double r1 = std::hypot(invWorld.m[1], invWorld.m[4]);
        invScale = std::max(r0, r1);
    }

    // Conservative per-element extent (node-local), for cull margins, the mask
    // edge band, and shape-radius collision avoidance (borders never overlap
    // when centres are ≥ the two radii apart, for ANY rotation).
    std::vector<double> elemR(in.elements.size(), 0.0);
    double maxR = 0.0;
    for (std::size_t k : enabledIdx) {
        elemR[k] = InstElementRadius(in.elements[k]);
        maxR = std::max(maxR, elemR[k]);
    }
    const double margin = maxR + std::max(0.0, in.posJitter);
    const double testR = margin * (docAnchor ? invScale : 1.0);

    // Region bbox in ANCHOR space (node-local for Object, document for Document)
    // — the box scatter samples in and line-sets span.
    DVec2 rlo{ 1e300, 1e300 }, rhi{ -1e300, -1e300 };
    {
        const DVec2 nc[4] = { { lo.x, lo.y }, { hi.x, lo.y },
                              { hi.x, hi.y }, { lo.x, hi.y } };
        for (const DVec2& q : nc) {
            const DVec2 c = docAnchor ? world.Apply(q) : q;
            rlo.x = std::min(rlo.x, c.x); rlo.y = std::min(rlo.y, c.y);
            rhi.x = std::max(rhi.x, c.x); rhi.y = std::max(rhi.y, c.y);
        }
    }

    // ── The clip mask (identical to EmitPattern): host fill ± the widest
    // Document-space stroke band → stencil; each stamp draws Clipped against it.
    const bool useMask = in.clip != PatternClip::Bounds;
    const Stroke* edgeStroke = nullptr;
    if (in.clip == PatternClip::StrokeInner || in.clip == PatternClip::StrokeOuter) {
        double best = 0.0;
        for (const Stroke& st : host.style.strokes) {
            if (!st.enabled || st.width <= 0.0 ||
                st.widthSpace != WidthSpace::Document) continue;
            if (st.width > best) { best = st.width; edgeStroke = &st; }
        }
    }
    std::vector<std::vector<DVec2>> cullRings;
    if (useMask) {
        for (auto& pl : geom::Flatten(*geo, 1.0))
            if (pl.closed && pl.points.size() >= 3)
                cullRings.push_back(std::move(pl.points));
        if (cullRings.empty()) return;   // open path — nothing to fill against
    }

    // Is an anchor-space point inside the host region? `interiorOnly` keeps only
    // points whose CENTRE is inside the contour (scatter); otherwise a testR
    // margin admits boundary cells (grid — the mask trims the overhang).
    auto inRegion = [&](DVec2 pA, bool interiorOnly) -> bool {
        const DVec2 pL = docAnchor ? invWorld.Apply(pA) : pA;
        if (useMask) {
            if (PointInRings(cullRings, pL)) return true;
            if (interiorOnly) return false;
            return DistToRings(cullRings, pL) <= testR;
        }
        return pL.x >= lo.x - maxR && pL.x <= hi.x + maxR &&
               pL.y >= lo.y - maxR && pL.y <= hi.y + maxR;
    };
    // The host silhouette (± the widest Document-space stroke band) as a stencil
    // in the given scope, then erased — so a scope's clipped stamps are cut at
    // the contour and the next region starts clean.
    auto emitMaskWrite = [&](ScopeId sc) {
        Drawable m;
        m.node = host.id;  m.owner = owner;  m.world = world;
        m.pathHash = geoHash;  m.path = geo;  m.boolProg = geoProg;
        m.isStroke = false;
        m.ownerPiece = (std::uint8_t)fillIndex;  m.ownerPieceStroke = false;
        m.rule = fill.rule;  m.scope = sc;
        m.clip = ClipRole::MaskWrite;  m.isClipSource = true;
        drawables_.push_back(m);
        if (edgeStroke) {
            Drawable b = m;
            b.isStroke = true;  b.stroke = *edgeStroke;
            b.clip = in.clip == PatternClip::StrokeInner ? ClipRole::MaskClear
                                                         : ClipRole::MaskWrite;
            drawables_.push_back(std::move(b));
        }
    };
    auto emitMaskClear = [&](ScopeId sc) {
        Drawable m;
        m.node = host.id;  m.owner = owner;  m.world = world;
        m.pathHash = geoHash;  m.path = geo;  m.boolProg = geoProg;
        m.isStroke = false;
        m.ownerPiece = (std::uint8_t)fillIndex;  m.ownerPieceStroke = false;
        m.rule = fill.rule;  m.scope = sc;
        m.clip = ClipRole::MaskClear;  m.isClipSource = true;
        drawables_.push_back(m);
        if (edgeStroke && in.clip == PatternClip::StrokeOuter) {
            Drawable b = m;
            b.isStroke = true;  b.stroke = *edgeStroke;
            drawables_.push_back(std::move(b));
        }
    };

    // ── Placements → POSES (anchor space; jitter + element choice baked in),
    // CACHED by a hash of everything that affects them. So a pure move / pan /
    // zoom (Object-anchor poses are node-local → world-independent) or an
    // unrelated edit reuses the poses — tens of thousands never re-scatter.
    std::vector<InstPose> poses;
    if (!enabledIdx.empty()) {
        std::uint64_t key = 0x9E3779B9ULL;
        key = HashBytes(&geoHash, sizeof geoHash, key);
        auto mixd = [&](double v) { key = HashDouble(v, key); };
        auto mixu = [&](std::uint64_t v) { key = HashBytes(&v, sizeof v, key); };
        mixu((std::uint64_t)in.layout);
        mixu((std::uint64_t)in.gridAxes);
        for (double v : in.spacing)   mixd(v);
        for (double v : in.axisAngle) mixd(v);
        mixu((std::uint64_t)in.scatterMode);
        mixu((std::uint64_t)(std::uint32_t)in.scatterCount);
        mixd(in.scatterMinDist); mixd(in.scatterMaxDist);
        mixu(in.avoidCollisions ? 1u : 0u);
        mixd(in.posJitter); mixd(in.rotJitter);
        mixu(in.seed); mixd(in.rotation);
        for (std::size_t k : enabledIdx) {
            const InstElement& e = in.elements[k];
            mixu((std::uint64_t)k);
            mixu((std::uint64_t)e.shape);
            mixd(e.sizeA); mixd(e.sizeB); mixd(e.sizeC); mixd(e.rotation);
        }
        mixu(docAnchor ? 1u : 0u);
        if (docAnchor) for (double v : world.m) mixd(v);

        bool hit = false;
        for (auto& ent : instPoseCache_)
            if (ent.first == key) { poses = ent.second; hit = true; break; }

        if (!hit) {
            std::uint64_t rs = Mix64(((std::uint64_t)in.seed << 5) ^ 0xB16B00B5ULL);
            auto rnd = [&]() { rs = Mix64(rs); return U01(rs); };
            auto pickElem = [&]() -> std::size_t {
                if (enabledIdx.size() == 1) return enabledIdx[0];
                std::size_t k = (std::size_t)(rnd() * (double)enabledIdx.size());
                if (k >= enabledIdx.size()) k = enabledIdx.size() - 1;
                return enabledIdx[k];
            };
            if (in.layout == InstLayout::Grid) {
                const double a0 = in.axisAngle[0] + in.rotation;
                const double s0 = in.spacing[0] > 1e-6 ? in.spacing[0] : 24.0;
                DVec2 b0{ std::cos(a0) * s0, std::sin(a0) * s0 };
                DVec2 b1;
                if (in.gridAxes >= 3) {   // triangular: second axis at +60°
                    const double a1 = a0 + 1.0471975511965976;  // π/3
                    b1 = { std::cos(a1) * s0, std::sin(a1) * s0 };
                } else {
                    const double a1 = in.axisAngle[1] + in.rotation;
                    const double s1 = in.spacing[1] > 1e-6 ? in.spacing[1] : 24.0;
                    b1 = { std::cos(a1) * s1, std::sin(a1) * s1 };
                }
                const double det = b0.x * b1.y - b1.x * b0.y;
                if (std::abs(det) >= 1e-9) {
                    const DVec2 nc[4] = { { lo.x, lo.y }, { hi.x, lo.y },
                                          { hi.x, hi.y }, { lo.x, hi.y } };
                    double iMin = 1e300, iMax = -1e300, jMin = 1e300, jMax = -1e300;
                    for (const DVec2& q : nc) {
                        const DVec2 cc = docAnchor ? world.Apply(q) : q;
                        const double ii = ( b1.y * cc.x - b1.x * cc.y) / det;
                        const double jj = (-b0.y * cc.x + b0.x * cc.y) / det;
                        iMin = std::min(iMin, ii); iMax = std::max(iMax, ii);
                        jMin = std::min(jMin, jj); jMax = std::max(jMax, jj);
                    }
                    const double bl = std::min(std::hypot(b0.x, b0.y),
                                               std::hypot(b1.x, b1.y));
                    const int mrg = (int)std::ceil(margin / std::max(bl, 1e-6)) + 1;
                    const long i0 = (long)std::floor(iMin) - mrg;
                    const long i1 = (long)std::ceil(iMax) + mrg;
                    const long j0 = (long)std::floor(jMin) - mrg;
                    const long j1 = (long)std::ceil(jMax) + mrg;
                    if ((double)(i1 - i0 + 1) * (double)(j1 - j0 + 1) <= 5.0e5)
                        for (long j = j0; j <= j1 && poses.size() < 300000u; ++j)
                            for (long i = i0; i <= i1; ++i) {
                                const DVec2 pA{ i * b0.x + j * b1.x,
                                                i * b0.y + j * b1.y };
                                if (!inRegion(pA, false)) continue;
                                const std::size_t el = pickElem();
                                double dx = 0, dy = 0, dr = 0;
                                if (in.posJitter > 0.0) {
                                    dx = (rnd() * 2.0 - 1.0) * in.posJitter;
                                    dy = (rnd() * 2.0 - 1.0) * in.posJitter;
                                }
                                if (in.rotJitter > 0.0)
                                    dr = (rnd() * 2.0 - 1.0) * in.rotJitter;
                                poses.push_back({ { pA.x + dx, pA.y + dy },
                                    in.rotation + in.elements[el].rotation + dr,
                                    (std::int32_t)el });
                            }
                }
            } else {
                // ── Scatter: seeded blue-noise Bridson filling the WHOLE region.
                // Count mode: ~N poses (radius from area/N). Distance mode: fill
                // at centre spacing in [min, max]. Collision keeps whole SHAPES
                // apart (centres ≥ the two circumscribed radii). All O(N).
                const double diag = std::hypot(rhi.x - rlo.x, rhi.y - rlo.y);
                const double collFloor = in.avoidCollisions ? 2.0 * maxR : 0.0;
                double minEff, maxEff; int target;
                if (in.scatterMode == InstScatterMode::Count) {
                    target = std::clamp(in.scatterCount, 0, 200000);
                    const double area =
                        std::max(1.0, (rhi.x - rlo.x) * (rhi.y - rlo.y));
                    const double r = target > 0
                        ? std::sqrt(area / (double)target) * 0.75 : diag * 0.05;
                    minEff = std::max({ r, collFloor, 1e-3 });
                    maxEff = minEff * 1.8;
                } else {   // Distance — fill the whole region
                    const double minU = std::max(0.0, in.scatterMinDist);
                    const double maxU = in.scatterMaxDist;
                    double base = std::max(minU, collFloor);
                    if (base <= 1e-6)
                        base = maxU > 1e-6 ? maxU * 0.35
                                           : std::max(1e-3, diag * 0.02);
                    minEff = base;
                    maxEff = maxU > minEff ? maxU : minEff * 1.8;
                    target = 200000;
                }
                const double cell = std::max(1e-3, std::max(minEff, collFloor));
                auto ckey = [](long cx, long cy) {
                    return ((long long)cx << 32) ^ (long long)(unsigned long)cy;
                };
                std::unordered_map<long long, std::vector<int>> grid;
                std::vector<double> poseR;
                auto cellX = [&](double x){ return (long)std::floor((x-rlo.x)/cell); };
                auto cellY = [&](double y){ return (long)std::floor((y-rlo.y)/cell); };
                auto add = [&](DVec2 p, double rot, std::size_t el, double rr) {
                    const int idx = (int)poses.size();
                    poses.push_back({ p, rot, (std::int32_t)el });
                    poseR.push_back(rr);
                    grid[ckey(cellX(p.x), cellY(p.y))].push_back(idx);
                };
                auto blocked = [&](DVec2 p, double rr) -> bool {
                    const long cx = cellX(p.x), cy = cellY(p.y);
                    for (long dy = -1; dy <= 1; ++dy)
                        for (long dx = -1; dx <= 1; ++dx) {
                            auto it = grid.find(ckey(cx + dx, cy + dy));
                            if (it == grid.end()) continue;
                            for (int idx : it->second) {
                                const double ex = poses[idx].pos.x - p.x;
                                const double ey = poses[idx].pos.y - p.y;
                                double rej = minEff;
                                if (in.avoidCollisions)
                                    rej = std::max(rej, rr + poseR[idx]);
                                if (ex * ex + ey * ey < rej * rej) return true;
                            }
                        }
                    return false;
                };
                const double kTwoPi = 6.283185307179586;
                std::vector<int> active;
                for (int t = 0; t < 4000 && poses.empty(); ++t) {
                    const DVec2 c{ rlo.x + rnd() * (rhi.x - rlo.x),
                                   rlo.y + rnd() * (rhi.y - rlo.y) };
                    if (!inRegion(c, true)) continue;
                    const std::size_t el = pickElem();
                    const double dr = in.rotJitter > 0.0
                        ? (rnd() * 2.0 - 1.0) * in.rotJitter : 0.0;
                    add(c, in.rotation + in.elements[el].rotation + dr, el, elemR[el]);
                    active.push_back(0);
                }
                while (!active.empty() && (int)poses.size() < target) {
                    int ai = (int)(rnd() * (double)active.size());
                    if (ai >= (int)active.size()) ai = (int)active.size() - 1;
                    const DVec2 baseP = poses[active[ai]].pos;
                    bool placed = false;
                    for (int k = 0; k < 24; ++k) {
                        const double ang = rnd() * kTwoPi;
                        const double rr = minEff + rnd() * (maxEff - minEff);
                        const DVec2 c{ baseP.x + std::cos(ang) * rr,
                                       baseP.y + std::sin(ang) * rr };
                        if (!inRegion(c, true)) continue;
                        const std::size_t el = pickElem();
                        if (blocked(c, elemR[el])) continue;
                        const double dr = in.rotJitter > 0.0
                            ? (rnd() * 2.0 - 1.0) * in.rotJitter : 0.0;
                        add(c, in.rotation + in.elements[el].rotation + dr, el,
                            elemR[el]);
                        active.push_back((int)poses.size() - 1);
                        placed = true;  break;
                    }
                    if (!placed) { active[ai] = active.back(); active.pop_back(); }
                }
            }
            if (instPoseCache_.size() >= 16)
                instPoseCache_.erase(instPoseCache_.begin());
            instPoseCache_.push_back({ key, poses });
        }
    }

    if (poses.empty() && lineIdx.empty()) return;

    // ── Isolation ONLY when the fill actually needs to composite as a unit:
    //   • the fill is TRANSLUCENT (Add overlaps must blend once at the fill
    //     opacity — shapes with shapes AND with lines), or a translucent Add
    //     colour would double-darken at crossings;
    //   • a CUT element/line erases (dst-out) — it must cut THIS fill's
    //     content, never the backdrop under it.
    // A fully OPAQUE Add/Blend fill needs none of that: its instances draw
    // straight into the parent scope (no extra render passes — a page full of
    // area symbols stays flat). Shapes are shared-mesh INSTANCED drawables, so
    // tens of thousands stay light either way.
    bool needScope = fill.opacity < 0.999f;
    for (std::size_t k : enabledIdx) {
        const InstElement& el = in.elements[k];
        if (el.mode == MarkObjectMode::Subtract) needScope = true;
        const float a = (el.useFillColor ? fill.paint.color.a : el.color.a) *
                        std::clamp(el.opacity, 0.0f, 1.0f);
        if (el.mode == MarkObjectMode::Fusion && a < 0.999f) needScope = true;
    }
    for (std::size_t li : lineIdx) {
        const InstLineSet& l = in.lines[li];
        if (l.mode == MarkObjectMode::Subtract) needScope = true;
        const float a = l.useFillColor ? fill.paint.color.a : l.color.a;
        if (l.mode == MarkObjectMode::Fusion && a < 0.999f) needScope = true;
    }

    ScopeId fs = scope;
    if (needScope) {
        CompositeScope fscope;
        fscope.node = host.id;  fscope.parent = scope;
        fscope.opacity = std::clamp(fill.opacity, 0.0f, 1.0f);
        fscope.blend = BlendMode::Normal;  fscope.isolate = true;
        fscope.depth = scopes_[scope].depth + 1;
        maxDepth_ = std::max(maxDepth_, fscope.depth);
        fs = (ScopeId)scopes_.size();
        scopes_.push_back(fscope);
    }

    if (useMask) emitMaskWrite(fs);
    const ClipRole role = useMask ? ClipRole::Clipped : ClipRole::None;

    // Base ring per enabled element (ONE shared mesh, GPU-instanced). Indexed by
    // absolute element index (poses store that). markShapes_ is a deque — the
    // pointers stay valid as more paths are pushed.
    std::vector<const PathData*> baseRing(in.elements.size(), nullptr);
    std::vector<std::uint64_t>   baseHash(in.elements.size(), 0);
    for (std::size_t k : enabledIdx) {
        markShapes_.push_back(InstElementPath(in.elements[k]));
        if (markShapes_.back().Empty()) { markShapes_.pop_back(); continue; }
        baseRing[k] = &markShapes_.back();
        baseHash[k] = markShapes_.back().Hash();
    }

    // A shape instance → one shared-mesh drawable. `cutPass` splits the two
    // rounds (positive content first, then the erasing Cut content).
    auto emitShape = [&](const InstPose& p, bool cutPass) {
        const InstElement& e = in.elements[(std::size_t)p.elem];
        const bool cut = e.mode == MarkObjectMode::Subtract;
        if (cut != cutPass) return;
        const PathData* g = baseRing[(std::size_t)p.elem];
        if (!g) return;
        const double c = std::cos(p.rot), s = std::sin(p.rot);
        DMat23 rot; rot.m[0] = c; rot.m[1] = -s; rot.m[3] = s; rot.m[4] = c;
        const DMat23 place = DMat23::Translation(p.pos.x, p.pos.y).Compose(rot);
        Drawable d;
        d.node = host.id;  d.owner = owner;
        d.world = docAnchor ? place : world.Compose(place);
        d.path = g;  d.pathHash = baseHash[(std::size_t)p.elem];
        d.isStroke = false;  d.rule = FillRule::NonZero;
        d.ownerPiece = (std::uint8_t)fillIndex;  d.ownerPieceStroke = false;
        d.scope = fs;
        const float op = std::clamp(e.opacity, 0.0f, 1.0f);
        if (cut) {
            d.color = Color{ 0, 0, 0, op };
            d.clip = ClipRole::EraseWrite;  d.clipPinned = true;
        } else {
            d.color  = e.useFillColor ? fill.paint.color  : e.color;
            d.swatch = e.useFillColor ? fill.paint.swatch : e.swatch;
            if (e.mode == MarkObjectMode::Fusion) d.color.a = 1.0f;  // paint once
            else d.color.a *= op;                                    // Blend stacks
            d.clip = role;
        }
        drawables_.push_back(std::move(d));
    };

    // ── Line-sets: parallel lines across the region (cap / dash / repeats).
    // `genLines` yields each spine's endpoints; `emitLine` strokes it in the
    // fill scope with the set's mode.
    auto genLines = [&](const InstLineSet& l,
                        const std::function<void(DVec2, DVec2)>& fn) {
        const double la = l.angle + in.rotation;
        const DVec2 dir{ std::cos(la), std::sin(la) };
        const DVec2 nrm{ -dir.y, dir.x };
        const DVec2 rc[4] = { { rlo.x, rlo.y }, { rhi.x, rlo.y },
                              { rhi.x, rhi.y }, { rlo.x, rhi.y } };
        double tMin = 1e300, tMax = -1e300, dMin = 1e300, dMax = -1e300;
        for (const DVec2& q : rc) {
            const double t = dir.x * q.x + dir.y * q.y;
            const double d = nrm.x * q.x + nrm.y * q.y;
            tMin = std::min(tMin, t); tMax = std::max(tMax, t);
            dMin = std::min(dMin, d); dMax = std::max(dMax, d);
        }
        // SPACING MODE: Center measures axis to axis (the pitch IS the spacing);
        // Border measures the visible gap between the facing edges, so the real
        // pitch has to carry the line's own width on top of it.
        double sp = l.spacing > 1e-6 ? l.spacing : 20.0;
        if (l.spacingMode == InstLineSpacing::Border)
            sp += std::max(0.0, l.line.width);
        if (sp < 1e-6) sp = 20.0;
        tMin -= sp; tMax += sp;                       // overshoot; the mask trims
        // STAGGER: shift each successive line ALONG its own direction by a
        // fraction of the dash PERIOD, so consecutive lines fall out of step
        // (the dash pattern itself is untouched — the whole line slides, so
        // anything riding it stays consistent).
        double period = 0.0;
        for (double dsh : l.line.dashPattern) period += dsh;
        const double step = (l.stagger != 0.0 && period > 1e-9)
                                ? l.stagger * period : 0.0;
        const long k0 = (long)std::floor((dMin - l.phase) / sp);
        const long k1 = (long)std::ceil((dMax - l.phase) / sp);
        if ((double)(k1 - k0 + 1) > 2.0e4) return;    // runaway guard
        for (long k = k0; k <= k1; ++k) {
            const double d = l.phase + (double)k * sp;
            // Keep the shift bounded to one period (a k-proportional slide
            // would run away across a large region).
            double slide = 0.0;
            if (step != 0.0) {
                slide = std::fmod((double)k * step, period);
                if (slide < 0.0) slide += period;
            }
            const DVec2 cC{ d * nrm.x + slide * dir.x,
                            d * nrm.y + slide * dir.y };
            fn({ cC.x + tMin * dir.x, cC.y + tMin * dir.y },
               { cC.x + tMax * dir.x, cC.y + tMax * dir.y });
        }
    };
    auto emitLine = [&](DVec2 p0, DVec2 p1, const InstLineSet& l) {
        const bool cut = l.mode == MarkObjectMode::Subtract;
        PathData lp; Subpath spn; spn.closed = false;
        Anchor a0; a0.pos = p0;  Anchor a1; a1.pos = p1;
        spn.anchors = { a0, a1 };
        lp.subpaths.push_back(std::move(spn));
        markShapes_.push_back(std::move(lp));
        const PathData* g = &markShapes_.back();
        Drawable d;
        d.node = host.id;  d.owner = owner;
        d.world = docAnchor ? DMat23{} : world;
        d.path = g;  d.pathHash = g->Hash();
        d.isStroke = true;
        d.stroke = l.line;                       // width / cap / dash / repeats
        d.stroke.align = StrokeAlign::Center;
        d.stroke.marks.clear();                  // marks unused on line-sets
        d.ownerPiece = (std::uint8_t)fillIndex;  d.ownerPieceStroke = false;
        d.scope = fs;
        if (cut) {
            d.color = Color{ 0, 0, 0, 1.0f };
            d.clip = ClipRole::EraseWrite;  d.clipPinned = true;
        } else {
            d.color  = l.useFillColor ? fill.paint.color  : l.color;
            d.swatch = l.useFillColor ? fill.paint.swatch : l.swatch;
            if (l.mode == MarkObjectMode::Fusion) d.color.a = 1.0f;  // paint once
            d.clip = role;                        // Blend keeps its own alpha
        }
        drawables_.push_back(std::move(d));
    };

    // POSITIVE content first (Add + Blend), then CUT content — so a cut erases
    // everything the fill drew before it, all clipped to the contour.
    for (const InstPose& p : poses) emitShape(p, /*cutPass=*/false);
    for (std::size_t li : lineIdx) {
        const InstLineSet& l = in.lines[li];
        if (l.mode == MarkObjectMode::Subtract) continue;
        genLines(l, [&](DVec2 a, DVec2 b) { emitLine(a, b, l); });
    }
    for (const InstPose& p : poses) emitShape(p, /*cutPass=*/true);
    for (std::size_t li : lineIdx) {
        const InstLineSet& l = in.lines[li];
        if (l.mode != MarkObjectMode::Subtract) continue;
        genLines(l, [&](DVec2 a, DVec2 b) { emitLine(a, b, l); });
    }

    if (useMask) emitMaskClear(fs);
}

bool Scene::Compile(Document& doc, bool force) {
    if (!force && compiled_ && !doc.HasPendingChanges() &&
        version_ == doc.Version() && !flattenDirty_)
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
    pvPending_.clear();
    pvPass_ = false;
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

    // ── Preview-only (library) pass: compile the deferred subtrees, tag their
    // drawables + scopes, and revert their bounds/pick contribution — vignette-
    // only content (Node::previewOnly).
    if (!pvPending_.empty()) {
        const Rect savedBounds  = bounds_;
        const bool savedValid   = boundsValid_;
        const std::size_t firstPvDraw  = drawables_.size();
        const std::size_t firstPvScope = scopes_.size();
        pvPass_ = true;
        auto pending = std::move(pvPending_);
        pvPending_.clear();
        for (const auto& [id, world] : pending)
            if (const Node* n = doc.Find(id))
                EmitNode(doc, *n, world, kRootScope, 0);
        pvPass_ = false;
        // Tag drawables/scopes; nodeBounds_ entries are KEPT (the vignette
        // camera frames a specimen through NodeBounds) — picking skips the
        // tagged drawables instead.
        for (std::size_t i = firstPvDraw; i < drawables_.size(); ++i)
            drawables_[i].previewOnly = true;
        for (std::size_t i = firstPvScope; i < scopes_.size(); ++i)
            scopes_[i].previewOnly = true;
        bounds_      = savedBounds;
        boundsValid_ = savedValid;
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

    // Resolve document SWATCHES. Every paint source in the model funnels into
    // Drawable::color, so one pass here restyles the whole document from the
    // palette. Only the HUE is taken from the swatch; the alpha accumulated
    // above (the paint's own alpha times every opacity on the way down) is
    // multiplied by the swatch's, so a translucent swatch works for screen work
    // while an object keeps its own fading.
    //
    // The PRINT transform is deliberately NOT applied here. It is a per-VIEW
    // display choice — a proofing viewport and a symbol vignette must be able to
    // disagree — so each drawable only carries what the transform needs and the
    // GPU style tables build one variant per configuration in use.
    if (!doc.Swatches().empty()) {
        const PrintTechnique tech = doc.PrintTech();
        for (Drawable& d : drawables_) {
            const Swatch* sw = doc.FindSwatch(d.swatch);
            if (!sw) continue;
            d.color.r = sw->display.r;
            d.color.g = sw->display.g;
            d.color.b = sw->display.b;
            d.color.a *= sw->display.a;
            if (sw->hasPrintOrder) {
                d.plate    = sw->printOrder;
                d.plateInk = sw->ink;
                d.hasSpot  = SwatchPrintsSpot(*sw, tech);
                d.spotColor = sw->spotDisplay;
            }
        }
    }

    // FLATTENER — the artwork that could not go to a separation as it stands:
    // anything translucent, blended or cutting. Nothing is flattened (the canvas
    // already shows the flattened result); the regions are reported so the app
    // can mark them up. Only computed while a view actually asks, since it
    // re-runs the stroker over every translucent stroke.
    flattenRings_.clear();
    flattenDirty_ = false;
    if (wantFlatten_) {
        for (const Drawable& d : drawables_) {
            if (d.isClipSource || d.previewOnly || !d.path) continue;
            const bool soft = d.color.a < 0.999f ||
                              d.clip == ClipRole::EraseWrite ||
                              d.clip == ClipRole::EraseClipped ||
                              scopes_[d.scope].blend != BlendMode::Normal ||
                              scopes_[d.scope].opacity < 0.999f;
            if (!soft) continue;
            const std::vector<geom::Polyline> flat = geom::Flatten(*d.path, 0.4);
            if (d.isStroke) {
                // Run the real stroker: the painted band is what matters, and
                // it depends on the alignment, the dash pattern and the caps —
                // none of which can be recovered from the spine.
                const geom::Mesh m =
                    geom::TessellateStroke(flat, d.stroke, 0.4, d.path);
                if (m.Empty()) continue;
                FlattenRegion fr;
                fr.isStroke = true;
                fr.tris.reserve(m.indices.size());
                for (std::uint32_t idx : m.indices)
                    fr.tris.push_back(d.world.Apply(
                        { m.positions[idx * 2], m.positions[idx * 2 + 1] }));
                flattenRings_.push_back(std::move(fr));
            } else {
                for (const geom::Polyline& pl : flat) {
                    if (pl.points.size() < 2) continue;
                    FlattenRegion fr;
                    fr.ring.reserve(pl.points.size());
                    for (const DVec2& p : pl.points)
                        fr.ring.push_back(d.world.Apply(p));
                    flattenRings_.push_back(std::move(fr));
                }
            }
        }
    }

    version_  = doc.Version();
    compiled_ = true;
    return true;
}

} // namespace Ink

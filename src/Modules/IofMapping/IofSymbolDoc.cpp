#include "IofGlyph.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Symbol documentation data for the Symbol Viewer: worked EXAMPLES and DYNAMIC
//  DIMENSION annotations. Dimensions carry the spec mm value (verifiable against
//  ISOM 2017-2 §3.8) and are re-labelled by the viewer as the map scale changes.
// ─────────────────────────────────────────────────────────────────────────────

namespace App::Modules::IofMapping {

using Renderer::Shape;
using Renderer::Vec2;

namespace {

// Rotate a whole shape about the local origin {0,0} by `deg` (used for the
// "rotated" / oriented examples). Sets the transform rotation.
Shape Rotated(Shape s, float deg) {
    s.transform.rotate = deg * 3.14159265f / 180.0f;
    return s;
}
// Translate a shape (local mm).
Shape MovedTo(Shape s, float dx, float dy) {
    s.transform.translate.x += dx; s.transform.translate.y += dy;
    return s;
}
// Build any symbol by code at `scale` (for companion shapes in examples).
Shape ByCode(int code, float scale) {
    const IofElement* e = IofFindByCode(code);
    return e ? BuildSymbolShape(*e, scale) : Shape{};
}
// A simple straight blue river segment of length `len` (mm@scale) along +x.
Shape River(float scale, float len) {
    Shape s = ByCode(3040, scale);          // crossable watercourse (blue 0.30)
    // Replace its default-length line with a longer straight segment.
    if (!s.parts.empty() && !s.parts[0].path.nodes.empty()) {
        auto& n = s.parts[0].path.nodes;
        if (n.size() >= 2) { n.front().pos = { -len * 0.5f, 0 }; n.back().pos = { len * 0.5f, 0 }; }
    }
    return s;
}

// Reshape a line symbol's FIRST part into a gentle S-curve of horizontal span
// `span` (mm@scale) so the decorator/dash pattern follows the bend — the way the
// spec plates show a worked curved example. Leaves the styling intact.
Shape Curved(Shape s, float span) {
    if (s.parts.empty()) return s;
    auto& nd = s.parts[0].path.nodes;
    if (nd.size() < 2) return s;
    nd.clear();
    const int N = 8;
    for (int i = 0; i <= N; ++i) {
        float t = (float)i / (float)N;
        float x = (-0.5f + t) * span;
        float y = std::sin(t * 6.2831853f) * span * 0.10f;
        Renderer::Node n({ x, y }); n.mode = Renderer::HandleMode::Vector;
        nd.push_back(n);
    }
    return s;
}
// Reshape a line symbol's first part to a straight segment of length `len`.
Shape Straight(Shape s, float len) {
    if (s.parts.empty()) return s;
    auto& nd = s.parts[0].path.nodes;
    if (nd.size() < 2) return s;
    nd.clear();
    Renderer::Node a({ -len * 0.5f, 0 }), b({ len * 0.5f, 0 });
    a.mode = b.mode = Renderer::HandleMode::Vector;
    nd = { a, b };
    return s;
}

}  // namespace

std::vector<SymbolExample> SymbolExamples(const IofElement& e, float scale) {
    std::vector<SymbolExample> out;
    const float s = scale;

    switch (e.glyph) {
        case IofGlyphKind::EarthBank: {
            // Curved example + a straight minimum-length example.
            Shape curved = BuildSymbolShape(e, s);
            // Bend the base line into an arc and let the tags follow.
            if (!curved.parts.empty()) {
                auto& nd = curved.parts[0].path.nodes;
                nd.clear();
                for (int i = 0; i <= 6; ++i) {
                    float t = (float)i / 6.0f;
                    float x = (-8.0f + 16.0f * t) * s;
                    float y = -std::sin(t * 3.14159f) * 3.0f * s;
                    Renderer::Node n({ x, y }); n.mode = Renderer::HandleMode::Vector;
                    nd.push_back(n);
                }
            }
            out.push_back({ "Curved earth bank", { curved } });
            Shape mn = BuildSymbolShape(e, s);
            if (!mn.parts.empty()) {
                auto& nd = mn.parts[0].path.nodes;
                if (nd.size() >= 2) { nd.front().pos = { -3.0f * s, 0 }; nd.back().pos = { 3.0f * s, 0 }; }
            }
            out.push_back({ "Minimum length 0.6 mm", { mn } });
            return out;
        }
        case IofGlyphKind::Spring: {
            out.push_back({ "Oriented north", { BuildSymbolShape(e, s) } });
            out.push_back({ "Rotated", { Rotated(BuildSymbolShape(e, s), 35.0f) } });
            // Spring + a river flowing downstream from it.
            Shape spring = Rotated(BuildSymbolShape(e, s), 0.0f);
            Shape river = River(s, 16.0f);
            river = MovedTo(river, 0.0f, 9.0f * s);   // flowing below the source
            out.push_back({ "Source feeding a stream", { spring, river } });
            return out;
        }
        case IofGlyphKind::BridgeTunnel: {
            // Path + river crossing under a road.
            { Shape road = River(s, 22.0f);                 // reuse a long line as a road
              road.parts[0].stroke.width = 0.35f * s;
              road.parts[0].stroke.color = { 0,0,0,1 };
              Shape river = MovedTo(River(s, 10.0f), 0, 0);
              // orient river vertically through the crossing
              for (auto& n : river.parts[0].path.nodes) { float y = n.pos.x; n.pos = { 0, y }; }
              Shape bridge = BuildSymbolShape(e, s);
              out.push_back({ "Road over a path + river", { road, river, bridge } }); }
            { Shape river = River(s, 10.0f);
              for (auto& n : river.parts[0].path.nodes) { float y = n.pos.x; n.pos = { 0, y }; }
              Shape road = River(s, 22.0f);
              road.parts[0].stroke.width = 0.35f * s; road.parts[0].stroke.color = { 0,0,0,1 };
              Shape bridge = BuildSymbolShape(e, s);
              out.push_back({ "River under a road", { road, river, bridge } }); }
            { Shape rail = ByCode(5090, s);                  // railway crossing
              Shape bridge = BuildSymbolShape(e, s);
              out.push_back({ "Over a railway", { rail, bridge } }); }
            return out;
        }
        case IofGlyphKind::CrossingPointFence: {
            Shape fence = ByCode(5160, s);                   // a fence with a crossing point
            Shape cp = BuildSymbolShape(e, s);
            out.push_back({ "A crossing through a fence", { fence, cp } });
            return out;
        }
        // ── Combined-screen area examples (ISOM §2.11.4) ──
        case IofGlyphKind::MarshArea: {                      // 308 marsh over rough open
            out.push_back({ "Marsh", { BuildSymbolShape(e, s) } });
            Shape rough = ByCode(4030, s);                   // rough open land (yellow 50%)
            out.push_back({ "Over rough open land", { rough, BuildSymbolShape(e, s) } });
            return out;
        }
        case IofGlyphKind::StonyGround: {                   // 210 stony + slow vegetation
            out.push_back({ "Stony ground", { BuildSymbolShape(e, s) } });
            Shape veg = ByCode(4060, s);                      // vegetation: slow (green 30%)
            out.push_back({ "Combined with slow vegetation", { veg, BuildSymbolShape(e, s) } });
            return out;
        }
        case IofGlyphKind::OpenLandDots: {                  // 402/404 open land w/ scattered trees
            out.push_back({ "Scattered trees", { BuildSymbolShape(e, s) } });
            return out;
        }
        default: break;
    }

    // Type-aware default worked examples (mirrors how the spec plates show a
    // symbol): a clean reference + a curved variant for lines, an oriented +
    // rotated pair for north-locked points, etc.
    if (e.type == IofType::Line) {
        out.push_back({ "Curved",          { Curved(BuildSymbolShape(e, s), 18.0f) } });
        out.push_back({ "Straight",        { Straight(BuildSymbolShape(e, s), 16.0f) } });
        return out;
    }
    if (e.type == IofType::Point) {
        if (e.northLocked) {
            out.push_back({ "Oriented north", { BuildSymbolShape(e, s) } });
            out.push_back({ "Rotated",        { Rotated(BuildSymbolShape(e, s), 30.0f) } });
        } else {
            out.push_back({ e.name, { BuildSymbolShape(e, s) } });
        }
        return out;
    }
    // Area / Text → the plain glyph.
    out.push_back({ e.name, { BuildSymbolShape(e, s) } });
    return out;
}

std::vector<DimAnnotation> SymbolDims(const IofElement& e) {
    using K = DimAnnotation::Kind;
    std::vector<DimAnnotation> d;
    auto dim = [&](K k, Vec2 a, Vec2 b, float mm, const char* lbl = "") {
        DimAnnotation x; x.kind = k; x.a = a; x.b = b; x.mm = mm; if (lbl[0]) x.label = lbl;
        d.push_back(x);
    };

    switch (e.glyph) {
        case IofGlyphKind::Contour:        dim(K::Thickness, {-8,0}, {8,0}, 0.14f); break;
        case IofGlyphKind::IndexContour:   dim(K::Thickness, {-8,0}, {8,0}, 0.25f); break;
        case IofGlyphKind::FormLine:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Length, {-8,0}, {-6,0}, 2.0f, "dash 2.0");
            dim(K::Gap, {-6,0}, {-5.8f,0}, 0.2f, "gap 0.2"); break;
        case IofGlyphKind::EarthBank:
            dim(K::Thickness, {-8,0}, {8,0}, 0.25f);
            dim(K::Length, {0,0}, {0,0.4f}, 0.4f, "tag 0.4 (OM)");
            dim(K::Gap, {0,0}, {0.5f,0}, 0.5f, "0.5 (CC)"); break;
        case IofGlyphKind::EarthWall:
            dim(K::Thickness, {-8,0}, {8,0}, 0.18f);
            dim(K::Diameter, {-0.225f,0}, {0.225f,0}, 0.45f, "ø0.45");
            dim(K::Gap, {0,0}, {2.0f,0}, 2.0f, "2.0 (CC)"); break;
        case IofGlyphKind::RetainingEarthWall:
        case IofGlyphKind::RetainingWall:
            dim(K::Thickness, {-8,0}, {8,0}, 0.18f);
            dim(K::Diameter, {-0.225f,0}, {0.225f,0}, 0.45f, "ø0.45");
            dim(K::Gap, {0,0}, {1.0f,0}, 1.0f, "1.0 (CC)"); break;
        case IofGlyphKind::ErosionGully:
            dim(K::Thickness, {-8,0}, {0,0}, 0.18f);
            dim(K::Length, {8,0}, {8.75f,0}, 0.75f, "taper 0.75"); break;
        case IofGlyphKind::ImpassableCliff:
            dim(K::Thickness, {-8,0}, {8,0}, 0.35f);
            dim(K::Length, {0,0}, {0,0.4f}, 0.4f, "tag 0.4 (OM)"); break;
        case IofGlyphKind::Cliff:
            dim(K::Thickness, {-8,0}, {8,0}, 0.25f);
            dim(K::Length, {0,0}, {0,0.25f}, 0.25f, "tag 0.25"); break;
        case IofGlyphKind::Trench:
            dim(K::Thickness, {-8,-0.1f}, {8,-0.1f}, 0.10f);
            dim(K::Gap, {0,-0.1f}, {0,0.1f}, 0.10f, "gap 0.10"); break;
        case IofGlyphKind::Wall:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Diameter, {-0.2f,0}, {0.2f,0}, 0.4f, "ø0.4");
            dim(K::Gap, {0,0}, {2.0f,0}, 2.0f, "2.0 (CC)"); break;
        case IofGlyphKind::ImpassableWall:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Diameter, {-0.3f,0}, {0.3f,0}, 0.6f, "ø0.6");
            dim(K::Gap, {0,0}, {3.0f,0}, 3.0f, "3.0 (CC)"); break;
        case IofGlyphKind::Fence:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Angle, {0,0}, {0.25f,-0.43f}, 60.0f, "60°");
            dim(K::Gap, {0,0}, {2.0f,0}, 2.0f, "2.0 (CC)"); break;
        case IofGlyphKind::ImpassableFence:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Width, {0,-0.3f}, {0,0.3f}, 0.6f, "0.6");
            dim(K::Angle, {0,0}, {0.25f,-0.43f}, 60.0f, "60°"); break;
        case IofGlyphKind::Railway:
            dim(K::Thickness, {-8,0}, {8,0}, 0.10f);
            dim(K::Width, {0,-0.75f}, {0,0.75f}, 1.5f, "1.5");
            dim(K::Gap, {0,0}, {1.0f,0}, 1.0f, "1.0 (CC)"); break;
        case IofGlyphKind::PowerLine:
            dim(K::Thickness, {-8,0}, {8,0}, 0.14f);
            dim(K::Gap, {0,0}, {6.0f,0}, 6.0f, "6.0 (default)"); break;
        case IofGlyphKind::Footpath:
            dim(K::Thickness, {-8,0}, {8,0}, 0.25f);
            dim(K::Length, {-8,0}, {-6,0}, 2.0f, "dash 2.0");
            dim(K::Gap, {-6,0}, {-5.75f,0}, 0.25f, "gap 0.25"); break;
        case IofGlyphKind::SmallPath:
            dim(K::Thickness, {-8,0}, {8,0}, 0.18f);
            dim(K::Length, {-8,0}, {-7,0}, 1.0f, "dash 1.0"); break;
        case IofGlyphKind::Control:
            dim(K::Diameter, {-2.5f,0}, {2.5f,0}, 5.0f, "ø5.0 (CC)");
            dim(K::Thickness, {2.5f,0}, {2.85f,0}, 0.35f, "0.35"); break;
        case IofGlyphKind::Start:
            dim(K::Diameter, {-2.6f,1.5f}, {2.6f,1.5f}, 6.0f, "ø6.0");
            dim(K::Thickness, {0,-3.0f}, {0,-2.65f}, 0.35f, "0.35"); break;
        case IofGlyphKind::Finish:
            dim(K::Diameter, {-3.0f,0}, {3.0f,0}, 6.0f, "ø6.0");
            dim(K::Diameter, {-2.0f,0}, {2.0f,0}, 4.0f, "ø4.0"); break;
        case IofGlyphKind::SmallDepression:
        case IofGlyphKind::Spring:
            dim(K::Diameter, {-0.4f,0}, {0.4f,0}, 0.8f, "ø0.8 (OM)");
            dim(K::Thickness, {0,0.31f}, {0.18f,0.31f}, 0.18f, "0.18"); break;
        case IofGlyphKind::Pit:
        case IofGlyphKind::RockyPit:
        case IofGlyphKind::Waterhole:
            dim(K::Width,  {-0.35f,-0.40f}, {0.35f,-0.40f}, 0.7f, "0.7 (OM)");
            dim(K::Length, {0.40f,-0.40f},  {0.40f,0.40f},  0.8f, "0.8");
            dim(K::Thickness, {0,0.31f}, {0.18f,0.31f}, 0.18f, "0.18"); break;
        case IofGlyphKind::KnollDot:
            dim(K::Diameter, {-0.25f,0}, {0.25f,0}, 0.5f, "ø0.5"); break;
        case IofGlyphKind::Tower:
            dim(K::Diameter, {-0.4f,0}, {0.4f,0}, 0.8f, "ø0.8"); break;
        case IofGlyphKind::Cairn:
            dim(K::Diameter, {-0.33f,0}, {0.33f,0}, 0.8f, "ø0.8");
            dim(K::Thickness, {0.33f,0}, {0.47f,0}, 0.14f, "0.14"); break;
        case IofGlyphKind::ProminentWater:
            dim(K::Angle, {0,0}, {0.45f,0}, 72.0f, "72°");
            dim(K::Diameter, {-0.45f,0}, {0.45f,0}, 0.9f, "0.9"); break;
        case IofGlyphKind::ElongatedKnoll:
            dim(K::Length, {-0.4f,-0.3f}, {0.4f,-0.3f}, 0.8f, "0.8");
            dim(K::Width,  {0.5f,-0.2f},  {0.5f,0.2f},  0.4f, "0.4"); break;
        case IofGlyphKind::BoulderDot:
            dim(K::Diameter, {-0.2f,0}, {0.2f,0}, 0.4f, "ø0.4"); break;
        case IofGlyphKind::LargeBoulderDot:
            dim(K::Diameter, {-0.3f,0}, {0.3f,0}, 0.6f, "ø0.6"); break;
        case IofGlyphKind::DangerousPit:
            dim(K::Diameter, {-0.45f,0}, {0.45f,0}, 0.9f, "ø0.9 (OM)");
            dim(K::Thickness, {0.1f,0}, {0.45f,0}, 0.35f, "0.35"); break;
        case IofGlyphKind::ProminentLandform:
            dim(K::Length, {-0.45f,0.26f}, {0.45f,0.26f}, 0.9f, "0.9");
            dim(K::Thickness, {0,-0.52f}, {0.18f,-0.52f}, 0.18f, "0.18"); break;
        case IofGlyphKind::BoulderTriangle:
            dim(K::Length, {-0.4f,0.3f}, {0.4f,0.3f}, 0.8f, "0.8"); break;
        case IofGlyphKind::Well:
            dim(K::Length, {-0.31f,-0.45f}, {0.31f,-0.45f}, 0.8f, "0.8 (OM)");
            dim(K::Thickness, {0.31f,0}, {0.49f,0}, 0.18f, "0.18"); break;
        case IofGlyphKind::FeatureRing:
            dim(K::Diameter, {-0.4f,0}, {0.4f,0}, 0.8f, "ø0.8");
            dim(K::Thickness, {0.4f,0}, {0.56f,0}, 0.16f, "0.16"); break;
        case IofGlyphKind::FeatureX:
            dim(K::Length, {-0.4f,-0.55f}, {0.4f,-0.55f}, 0.8f, "0.8");
            dim(K::Thickness, {0,0.55f}, {0.16f,0.55f}, 0.16f, "0.16"); break;
        case IofGlyphKind::LargeTree:
            dim(K::Diameter, {-0.45f,0}, {0.45f,0}, 0.9f, "ø0.9 (OM)");
            dim(K::Thickness, {0.36f,0}, {0.54f,0}, 0.18f, "0.18"); break;
        case IofGlyphKind::KnollGreen:
            dim(K::Diameter, {-0.3f,0}, {0.3f,0}, 0.6f, "ø0.6"); break;
        case IofGlyphKind::SmallTower:
            dim(K::Length, {-0.5f,-0.5f}, {0.5f,-0.5f}, 1.0f, "1.0 (OM)"); break;
        case IofGlyphKind::FodderRack:
            dim(K::Angle, {0,-0.45f}, {0.45f,0}, 60.0f, "60°"); break;
        case IofGlyphKind::RuinedEarthWall:
        case IofGlyphKind::RuinedWall:
            dim(K::Thickness, {-8,0}, {8,0}, 0.18f);
            dim(K::Length, {-8,0}, {-7.2f,0}, 0.8f, "dash 0.8");
            dim(K::Gap, {-7.2f,0}, {-6.6f,0}, 0.6f, "gap 0.6"); break;
        case IofGlyphKind::MagneticNorth:
            dim(K::Thickness, {-0.05f,4}, {0.05f,4}, 0.10f, "0.10"); break;
        case IofGlyphKind::RegistrationMark:
            dim(K::Length, {-2,2.4f}, {2,2.4f}, 4.0f, "4.0");
            dim(K::Thickness, {0,0}, {0.1f,0}, 0.10f, "0.10"); break;
        case IofGlyphKind::VegBoundaryDots:
            dim(K::Diameter, {-0.11f,0}, {0.11f,0}, 0.22f, "ø0.22");
            dim(K::Gap, {0,0}, {0.45f,0}, 0.45f, "0.45 (CC)"); break;
        case IofGlyphKind::NarrowMarsh:
        case IofGlyphKind::SmallErosionGully:
            dim(K::Diameter, {-0.125f,0}, {0.125f,0}, 0.25f, "ø0.25");
            dim(K::Gap, {0,0}, {0.45f,0}, 0.45f, "0.45 (CC)"); break;
        case IofGlyphKind::WideRoad:
            dim(K::Thickness, {-8,-0.22f}, {8,-0.22f}, 0.14f, "0.14");
            dim(K::Gap, {0,-0.22f}, {0,0.22f}, 0.30f, "0.30 (IM)"); break;
        case IofGlyphKind::Road:
            dim(K::Thickness, {-8,0}, {8,0}, 0.35f); break;
        case IofGlyphKind::VehicleTrack:
            dim(K::Thickness, {-8,0}, {8,0}, 0.35f);
            dim(K::Length, {-8,0}, {-5,0}, 3.0f, "dash 3.0");
            dim(K::Gap, {-5,0}, {-4.75f,0}, 0.25f, "gap 0.25"); break;
        case IofGlyphKind::Watercourse:
            dim(K::Thickness, {-8,0}, {8,0}, 0.30f); break;
        case IofGlyphKind::SmallWatercourse:
            dim(K::Thickness, {-8,0}, {8,0}, 0.18f); break;
        default: break;
    }
    return d;
}

}  // namespace App::Modules::IofMapping

#pragma once
// Internal helpers shared by the EditMode translation units (the interactive
// vertex/edge/handle editor and the edit-mode actions). These were file-static
// in EditMode.cpp; `inline`/template here so both EditMode*.cpp can use them.
#include <Renderer/Document/Document.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <vector>

namespace App {

using Renderer::Vec2;
using Renderer::Node;

namespace editmode_detail {

// World position of a node's anchor / handles, through the shape transform AND
// the owning page origin (geometry is page-relative).
inline Vec2 NodeWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.pos, po);
}
inline Vec2 HandleInWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.hIn, po);
}
inline Vec2 HandleOutWorld(const Renderer::Shape& s, const Node& n, Vec2 po = {0, 0}) {
    return Renderer::Tessellator::WorldTransform(s, n.hOut, po);
}

inline float Dist2(ImVec2 a, ImVec2 b) {
    float dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy;
}

// Even-odd point-in-polygon (world-space), for Face-mode picking.
inline bool PointInPoly(const std::vector<Vec2>& poly, Vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                       (poly[j].y - poly[i].y) + poly[i].x))
            in = !in;
    return in;
}

// Iterate every editable node of the selection (the selected OBJECTS' parts).
template <class F>
void ForEachEditableNode(Renderer::Document& doc, F&& fn) {
    for (uint64_t sid : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(sid);
        if (!s) continue;
        s->EnsurePath();   // editing requires baked nodes (primitive → path)
        for (int pi = 0; pi < (int)s->parts.size(); ++pi) {
            Renderer::Part& part = s->parts[(size_t)pi];
            for (int ni = 0; ni < (int)part.path.nodes.size(); ++ni)
                fn(*s, sid, pi, ni, part, part.path.nodes[(size_t)ni]);
        }
    }
}

} // namespace editmode_detail

// Bring the helpers into App scope so existing unqualified calls resolve.
using editmode_detail::NodeWorld;
using editmode_detail::HandleInWorld;
using editmode_detail::HandleOutWorld;
using editmode_detail::Dist2;
using editmode_detail::PointInPoly;
using editmode_detail::ForEachEditableNode;

} // namespace App

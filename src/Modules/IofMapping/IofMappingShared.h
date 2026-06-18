#pragma once
// Course-object helpers shared by the IOF Mapping module logic and its UI
// panels: the "is this a placed Control/Start/Finish?" predicates and the
// document-wide ForEachCourseObject walk. These were file-static in
// IofMappingModule.cpp; `inline`/template here so both .cpp can use them.
#include <Renderer/Document/Document.h>

namespace App::Modules::IofMapping {

// Is this shape a placed course object, and of which kind?
inline bool IsControl(const Renderer::Shape& s) { return s.name.rfind("Control", 0) == 0; }
inline bool IsStart  (const Renderer::Shape& s) { return s.name == "Start"; }
inline bool IsFinish (const Renderer::Shape& s) { return s.name == "Finish"; }
inline bool IsCourseObject(const Renderer::Shape& s) { return IsControl(s) || IsStart(s) || IsFinish(s); }

// A course object's raw DOCUMENT-space centre (world coords). Object geometry is
// page-relative, so world = pageOrigin + translate + origin. Course objects now
// live ON the page, so the page offset must be added (loose objects → {0,0}).
inline ImVec2 DocCentre(Renderer::Document& doc, const Renderer::Shape& s) {
    Renderer::Vec2 po = doc.PageOriginOfShape(s.id);
    return ImVec2(po.x + s.transform.translate.x + s.origin.x,
                  po.y + s.transform.translate.y + s.origin.y);
}

// Apply `fn` to every placed course OBJECT in the document (on a page or loose),
// so course tooling works whether symbols live on the page or as legacy orphans.
template <class Fn>
void ForEachCourseObject(Renderer::Document& doc, Fn&& fn) {
    for (Renderer::Artboard& ab : doc.artboards)
        for (Renderer::Shape& s : ab.shapes) if (IsCourseObject(s)) fn(s);
    for (Renderer::Shape& s : doc.looseShapes) if (IsCourseObject(s)) fn(s);
}

}  // namespace App::Modules::IofMapping

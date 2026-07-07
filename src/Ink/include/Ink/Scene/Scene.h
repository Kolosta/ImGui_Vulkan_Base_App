#pragma once

#include "Ink/Document/Document.h"

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Scene — the compiled runtime scene (docs/Ink/ARCHITECTURE.md §2): the only
//  consumer of the Document and the only producer of render items. A compile
//  walks pages → layer trees in painter order and emits one Drawable per
//  enabled style piece (fills bottom-up, then strokes) of every visible path
//  node, with the world transform resolved in double.
//
//  Lot 2 granularity: the walk itself is O(nodes) and runs only when the
//  ChangeLog is non-empty; the expensive work stays incremental behind it —
//  geometry by cache key, GPU tables by scene version. Finer per-change
//  diffing (dirty ranges instead of a re-walk) layers on in the perf lots.
// ─────────────────────────────────────────────────────────────────────────────

struct Drawable {
    NodeId          node = kNullNode;
    DMat23          world;              // node-local → document (double)
    std::uint64_t   pathHash = 0;
    const PathData* path = nullptr;     // borrowed; valid until the next compile
    bool            isStroke = false;
    std::uint8_t    pieceIndex = 0;     // index into style.fills / style.strokes
    FillRule        rule = FillRule::NonZero;   // fill pieces
    Stroke          stroke;                     // stroke pieces (geometry params)
    Color           color;              // linear straight (premultiplied later)
};

class Scene {
public:
    // Recompile if the document changed since the last compile (or `force`).
    // Drains the document's ChangeLog. Returns true when the drawable list
    // was rebuilt (GPU tables must resync).
    bool Compile(Document& doc, bool force = false);

    const std::vector<Drawable>& Drawables() const { return drawables_; }
    // Document version this scene reflects (mixed into view signatures).
    std::uint64_t Version() const { return version_; }
    // Document-space bounds (pages ∪ node anchor boxes) — drives fit-view.
    Rect Bounds() const { return bounds_; }

private:
    void EmitNode(const Document& doc, const Node& n, const DMat23& parentWorld);
    void GrowBounds(DVec2 p);

    std::vector<Drawable> drawables_;
    std::vector<PathData> pageRects_;   // stable storage for page substrates
    std::uint64_t version_  = 0;
    bool          compiled_ = false;
    Rect          bounds_{};
    bool          boundsValid_ = false;
};

} // namespace Ink

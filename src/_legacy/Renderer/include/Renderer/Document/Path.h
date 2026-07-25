#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Renderer {

// A 2D point in OBJECT-LOCAL space (doc-units before the shape's transform).
struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Path — an editable sequence of nodes (Blender-style, adapted to 2D).
//
//  Every shape's geometry is ultimately a Path of `Node`s. A node has a position
//  plus two OPTIONAL Bézier handles (in / out). When a node has no active handle
//  on a side, that side is a straight segment; when it does, the segment uses the
//  handle as the cubic control point. This single representation covers straight
//  polylines, smooth curves, and everything in between — so any point of any
//  shape (even a converted primitive) can be turned into a Bézier point.
//
//  Handles are stored as ABSOLUTE object-local positions (not offsets), which
//  keeps flattening and hit-testing simple. `HandleMode` constrains how the two
//  handles relate when one is edited (enforced by the editor, Lot E).
// ─────────────────────────────────────────────────────────────────────────────

enum class HandleMode : uint8_t {
    Free     = 0,  // the two handles are independent
    Aligned  = 1,  // handles stay COLLINEAR (opposite directions), lengths free
    Mirrored = 2,  // handles keep EQUAL LENGTH, directions free
    Vector   = 3,  // no curvature: handles point at the neighbour anchors
    AlignedMirrored = 4,  // collinear AND equal length (fully symmetric)
};

struct Node {
    Vec2       pos;                 // anchor position (object-local doc-units)
    Vec2       hIn;                 // incoming handle (absolute), if hasIn
    Vec2       hOut;                // outgoing handle (absolute), if hasOut
    bool       hasIn  = false;
    bool       hasOut = false;
    HandleMode mode   = HandleMode::Free;
    // RATIONAL NURBS weight of this control point (1 = ordinary). >1 pulls the
    // curve toward the point; the classic exact-circle control polygon uses
    // weight cos(half-segment-angle) on the "corner" controls. Ignored unless the
    // part is a NURBS curve.
    float      weight = 1.0f;
    // JUNCTION group id (0 = ordinary node). A real multi-path branch shares ONE
    // vertex: the branch's first node and the point it grew from carry the SAME
    // non-zero junctionId and the SAME position. Edit mode draws them as a SINGLE
    // vertex (with all their handles, ≥3) and moves them together; the tessellator
    // still strokes each subpath, so the branches render as one continuous curve.
    uint32_t   junctionId = 0;

    Node() = default;
    explicit Node(Vec2 p) : pos(p), hIn(p), hOut(p) {}
};

// A Path is one or more SUBPATHS sharing a single flat `nodes[]` array. Each
// subpath is a contiguous range [subStart[i], subStart[i+1]) of nodes; the last
// runs to nodes.size(). `subStart` is sorted, starts with 0, and is empty/{0}
// for the common single-subpath case (so all existing `nodes` access is
// unchanged). Subpaths let one Part hold disconnected strands or a 3-way BRANCH
// (a new strand starting at a junction position) — needed by the reworked
// extrude / curve tools and by network line features (a stream that forks).
//
// `closed` applies to the WHOLE path's subpaths uniformly for now (every subpath
// is open, or all closed) — the common case; per-subpath closure can be added
// later if needed. Vertex addressing (VertRef.node) is unchanged: it's a flat
// index into `nodes`, so edit-mode, hit-testing and serialization of vertices
// stay identical; only flattening/joining is taught not to bridge subpaths.
struct Path {
    std::vector<Node> nodes;
    bool              closed = false;
    std::vector<int>  subStart;   // sorted node indices where each subpath begins

    bool empty() const { return nodes.empty(); }
    size_t size() const { return nodes.size(); }

    // Number of subpaths (≥1 when there are nodes).
    int subCount() const {
        if (nodes.empty()) return 0;
        int n = (int)subStart.size();
        return (n <= 1) ? 1 : n;        // empty/{0} → one subpath
    }
    // [begin,end) flat node range of subpath i (0-based).
    void subRange(int i, int& begin, int& end) const {
        const int N = (int)nodes.size();
        if (subStart.size() <= 1) { begin = 0; end = N; return; }
        begin = subStart[(size_t)i];
        end   = (i + 1 < (int)subStart.size()) ? subStart[(size_t)(i + 1)] : N;
    }
    // The subpath index that owns flat node `k`.
    int subOf(int k) const {
        if (subStart.size() <= 1) return 0;
        int s = 0;
        for (int i = 0; i < (int)subStart.size(); ++i)
            if (k >= subStart[(size_t)i]) s = i; else break;
        return s;
    }
    // Shift subpath boundaries after a node was INSERTED at flat index `at`. The
    // boundary AT `at` (if any) STAYS — so the new node becomes the FIRST of the
    // subpath that began there (used when prepending to a subpath's start).
    void OnNodeInserted(int at) {
        if (subStart.empty()) return;
        for (int& s : subStart) if (s > at) ++s;
        NormalizeSubpaths();
    }
    // Like OnNodeInserted, but the boundary AT `at` ALSO moves up — so the new
    // node joins the PREVIOUS subpath (used when appending past a subpath's end,
    // where `at` equals the next subpath's start).
    void OnNodeInsertedInclusive(int at) {
        if (subStart.empty()) return;
        for (int& s : subStart) if (s >= at) ++s;
        NormalizeSubpaths();
    }
    // Shift subpath boundaries after the node at flat index `at` was ERASED.
    void OnNodeErased(int at) {
        if (subStart.empty()) return;
        for (int& s : subStart) if (s > at) --s;
        NormalizeSubpaths();
    }
    // Start a new subpath at flat index `at` (i.e. the node at `at` begins a new
    // strand). No-op if `at` is 0 or already a boundary.
    void SplitAt(int at) {
        if (at <= 0 || at >= (int)nodes.size()) return;
        if (subStart.empty()) subStart.push_back(0);
        subStart.push_back(at);
        NormalizeSubpaths();
    }

    // Keep subStart well-formed (sorted, unique, leading 0, in range) after edits.
    void NormalizeSubpaths() {
        const int N = (int)nodes.size();
        if (subStart.empty()) return;
        std::vector<int> v;
        for (int s : subStart) if (s > 0 && s < N) v.push_back(s);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        subStart.clear();
        if (N > 0) { subStart.push_back(0); for (int s : v) subStart.push_back(s); }
        if (subStart.size() <= 1) subStart.clear();   // canonical single-subpath
    }
};

// ── Legacy (pre-v2) types, kept ONLY for .acu v1 migration ───────────────────
// The v1 document stored a path as parallel points[] + segments[]. ProjectFile's
// v1 decoder reads these and converts them into the Node[] model above. Nothing
// else in the codebase should use them.
enum class SegmentKind : uint8_t { Line = 0, CubicBezier = 1 };
struct LegacySegment {
    SegmentKind kind = SegmentKind::Line;
    Vec2        c0;
    Vec2        c1;
};

} // namespace Renderer

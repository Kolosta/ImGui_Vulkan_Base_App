#pragma once

#include "Ink/Scene/Scene.h"

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Picking — CPU-exact hit-testing on the compiled Scene (docs/Ink/ROADMAP.md
//  Lot 8). Deterministic and windowless (unit-tested; the pick_storm bench
//  measures it). The async GPU picking path is a later perf lot; this exact
//  test is its permanent fallback and its correctness reference.
//
//  Coordinates are DOCUMENT space (double — unbounded canvas): the caller
//  converts the mouse position through its camera and passes a tolerance in
//  document units (typically a few pixels divided by the zoom).
//
//  Semantics:
//   • Topmost first: drawables are stored in painter order, so the walk runs
//     back→front and the first hit wins.
//   • A hit reports the drawable's OWNER (an instance's subtree picks the
//     instance; a pattern motif copy picks its host shape).
//   • Fills hit by their fill rule (NonZero / EvenOdd) on the flattened
//     outline; strokes hit within half their width (+ tolerance) of the
//     flattened spine. Stroke align v1: Center extents both sides;
//     Inside/Outside are tested as Center (documented approximation).
//   • Clip-source drawables never pick (they never paint).
// ─────────────────────────────────────────────────────────────────────────────

struct PickOptions {
    double tolerance = 3.0;      // document units added around edges/strokes
    // View pixels per document unit (converts Viewport-space stroke widths;
    // <= 0 → viewport-space strokes are tested with width 0 + tolerance).
    double zoom = 0.0;
};

// The topmost node at `point`, or kNullNode.
NodeId PickTop(const Scene& scene, DVec2 point, const PickOptions& opt = {});

// All distinct owners whose rendered bounds intersect the box (box select:
// conservative per-node bounds, not exact geometry — documented v1).
std::vector<NodeId> PickBox(const Scene& scene, DVec2 boxMin, DVec2 boxMax);

} // namespace Ink

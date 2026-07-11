#pragma once

#include "Ink/Document/Types.h"
#include <vector>

namespace Ink {

// ─────────────────────────────────────────────────────────────────────────────
//  Modifiers (docs/Ink/DOCUMENT_MODEL.md §6) — non-destructive operators on a
//  node, evaluated at Scene compile. Lot 5 ships the two INSTANCING modifiers:
//  they turn a node's geometry into MANY copies at generated transforms,
//  expanded logically by the Scene (one cached mesh, many instance records,
//  merged into one indirect draw — never geometry duplication).
//
//  A modifier references its inputs by id (paths/collections) so the Scene
//  knows the dependency edges; Boolean/Mask and the rest arrive in later lots
//  behind this same struct.
// ─────────────────────────────────────────────────────────────────────────────

enum class ModifierKind : std::uint8_t {
    Array = 0,     // count copies, each offset from the previous by `step`
    AlongPath = 1, // copies distributed along a target path by arc length
    Boolean = 2,   // combine this node's geometry with an operand's (Lot 7)
};

// How instances orient along a path.
enum class AlongAlign : std::uint8_t {
    None = 0,      // keep the source orientation
    Tangent = 1,   // rotate to the path tangent at each sample
};

// How copies distribute along the target path.
enum class AlongDistribute : std::uint8_t {
    ByCount = 0,   // `alongCount` copies, evenly spaced by arc length
    BySpacing = 1, // one copy every `spacing` arc-length units
    AtAnchors = 2, // one copy on every anchor point (Blender's "on points")
};

// The space the Array step is expressed in. Local composes after the node's
// own transform (the step inherits the node's rotation/scale); Parent applies
// the step in the node's parent space (a 10-unit step is 10 document units
// regardless of the node's scale — the predictable default for UI editing).
enum class ArrayStepSpace : std::uint8_t { Local = 0, Parent = 1 };

// Boolean geometry operation (docs/Ink/DOCUMENT_MODEL.md §6). Operates on the
// flattened outlines of the host and the operand (v1: polygon-level, the
// pragmatic standard; exact Bézier booleans are a later quality lot).
enum class BooleanOp : std::uint8_t {
    Union = 0,      // host ∪ operand
    Subtract = 1,   // host − operand
    Intersect = 2,  // host ∩ operand
    Xor = 3,        // symmetric difference
};

struct Modifier {
    ModifierKind kind = ModifierKind::Array;
    bool         enabled = true;

    // ── Array ────────────────────────────────────────────────────────────────
    int            count = 2;           // total copies (incl. the original)
    Transform2D    step;                // per-copy incremental transform
                                        // (translate/rotate/scale composed)
    ArrayStepSpace stepSpace = ArrayStepSpace::Local;

    // ── AlongPath ────────────────────────────────────────────────────────────
    // Copies sit ON the referenced path (the node's own translation is
    // ignored; its rotation/scale still apply to each copy).
    NodeId          pathRef = kNullNode;   // path whose spine is sampled
    AlongDistribute distribute = AlongDistribute::ByCount;
    double          spacing = 50.0;        // arc-length step (BySpacing)
    int             alongCount = 10;       // number of copies (ByCount)
    AlongAlign      align = AlongAlign::Tangent;
    double          startTrim = 0.0;       // skip this arc-length at the start
    double          endTrim   = 0.0;       // and at the end

    // ── Boolean ──────────────────────────────────────────────────────────────
    BooleanOp    op = BooleanOp::Union;
    NodeId       operandRef = kNullNode;   // the other operand's path node
};

} // namespace Ink

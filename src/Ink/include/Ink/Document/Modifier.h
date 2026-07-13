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
    AlongPath = 1, // instance a MOTIF object along THIS path's spine
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

// Array placement mode (Blender's array, 2D):
//   Transform — cumulative per-copy transform (translate∘rotate∘scale
//               composed k times: spirals/orbits possible).
//   Line      — copies on a straight line; rotation/scale spin each INSTANCE
//               in place (never the positions).
//   Circle    — copies on a circle around the object.
enum class ArrayMode : std::uint8_t { Transform = 0, Line = 1, Circle = 2 };

// How the Line mode reads its translation: Relative = factors of the object's
// own size (1 = one object width/height per copy); Offset = absolute parent
// units per copy; Endpoint = the translation is the END POINT and the copies
// distribute evenly from the original to it.
enum class ArrayLineMode : std::uint8_t { Relative = 0, Offset = 1, Endpoint = 2 };

// How the Circle mode derives its copy count: an explicit count, or one copy
// every `circleAngleStep` radians of the (arc or full) sweep.
enum class ArrayCircleMethod : std::uint8_t { ByCount = 0, ByAngle = 1 };

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
    Transform2D    step;                // per-copy transform: Transform mode
                                        // composes it cumulatively; Line mode
                                        // reads translation as the line step /
                                        // endpoint and rotation/scale as the
                                        // per-instance in-place spin.
    ArrayStepSpace stepSpace = ArrayStepSpace::Local;   // Transform mode only
    ArrayMode      arrayMode = ArrayMode::Transform;
    ArrayLineMode  lineMode  = ArrayLineMode::Offset;
    // Circle mode.
    double            circleRadius    = 100.0;
    ArrayCircleMethod circleMethod    = ArrayCircleMethod::ByCount;
    double            circleAngleStep = 0.5235987755982988;  // 30° in radians
    bool              circleArc       = false;  // false = full circle
    double            circleSweep     = 3.141592653589793;   // arc sweep (rad)
    bool              circleAlign     = true;   // rotate instances along it

    // ── AlongPath ────────────────────────────────────────────────────────────
    // The modifier lives on the PATH being followed (Blender's rule): it
    // instances the MOTIF object along this node's own spine. Copies sit ON
    // the path — the motif's translation is ignored, its rotation/scale still
    // shape each copy — and render like pattern motifs: technically instanced
    // (shared mesh, merged draws) and independent of the motif's visibility.
    NodeId          motifRef = kNullNode;  // node instanced along the spine
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

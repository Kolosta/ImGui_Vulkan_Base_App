#pragma once

#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Generic node/port/cable canvas widget (docs/Ink/NODE_GRAPH.md §5) — the
//  "Core Node UI" shared by every node-graph editor in the app (first user:
//  the Node Graph Editor, ROADMAP Lot 13). Kind-agnostic: it draws boxes,
//  sockets and cables and reports gestures (move/select/connect); it has no
//  idea what a "layer" or a "CompNode" is — the caller maps its own node
//  kinds onto NodeGraphNode/NodeGraphPort and interprets the result.
//
//  Camera, node positions, selection and connections are ALL caller-owned
//  (this widget keeps no persistent state of its own beyond the current
//  in-flight drag/box-select gesture — Panel.h's convention). Call
//  DrawNodeGraph every frame with the current node/edge list, apply the
//  returned gesture to your own data, redraw next frame.
//
//  Usage:
//      UI::NodeGraphConfig cfg;
//      cfg.nodes = &myNodes; cfg.edges = &myEdges;
//      cfg.panX = &myPanX; cfg.panY = &myPanY; cfg.zoom = &myZoom;
//      UI::NodeGraphResult r = UI::DrawNodeGraph(cfg);
//      if (r.connected) myApp.Commit(r.newEdge);   // typed op, one undo entry
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

enum class NodePortDir : std::uint8_t { In = 0, Out = 1 };

// One port slot, as the generic widget sees it. `type` is a caller-defined
// tag (a plain int, not an enum, so this header never needs to know the
// caller's port-type enum) — two ports connect only when an Out port's type
// equals an In port's type; DrawNodeGraph never connects across types.
struct NodeGraphPort {
    std::string  label;
    NodePortDir  dir  = NodePortDir::In;
    int          type = 0;
    // Colour override for this port's dot AND any cable landing on it
    // (w < 0 = unset — falls back to the design-token default). Lets a
    // caller colour-code ports/cables by type (docs/Ink/NODE_GRAPH.md
    // feedback: "des couleurs aux liens en fonction de leur type").
    ImVec4       color{ -1.0f, -1.0f, -1.0f, -1.0f };
};

struct NodeGraphNode {
    std::uint64_t  id = 0;              // caller's node id, echoed back
    std::string    title;
    ImVec2         pos{ 0, 0 };         // canvas-space top-left (caller-owned/persisted)
    std::vector<NodeGraphPort> ports;   // any order; `dir` picks which edge they sit on
    bool           selected = false;    // caller-owned selection highlight (multi-select ok)
    bool           muted    = false;    // dims the whole box + its ports/cables
    // Collapsed (H): header only, ports draw ON the header row (no labels,
    // all at header-centre height) instead of in the body.
    bool           collapsed = false;
    // Header band colour override (w < 0 = unset — falls back to the
    // design-token default). Lets a caller colour-code node headers by kind
    // (docs/Ink/NODE_GRAPH.md feedback: "comme dans Blender").
    ImVec4         headerColor{ -1.0f, -1.0f, -1.0f, -1.0f };
    // Optional body content BELOW the ports — drawn via normal ImGui calls
    // (SetCursorScreenPos + any widget) at the given screen rect. Skipped
    // when `collapsed`. `bodyHeight` (canvas units, scales with zoom like
    // everything else) reserves the space; 0 = no body row.
    float          bodyHeight = 0.0f;
    std::function<void(ImVec2 bodyMin, ImVec2 bodyMax)> bodyDraw;
    // Optional content drawn ABOVE the header (docs/Ink/NODE_UI.md task #3 —
    // e.g. a live preview thumbnail): always reserved as a SQUARE exactly as
    // wide as the node itself (not a fixed pixel size), so it scales with
    // the node under zoom instead of capping out. nullptr = no top content,
    // no space reserved, node layout unaffected.
    std::function<void(ImVec2 topMin, ImVec2 topMax)> topDraw;
};

struct NodeGraphEdge {
    std::uint64_t fromNode = 0; int fromPort = 0;
    std::uint64_t toNode   = 0; int toPort   = 0;
};

struct NodeGraphConfig {
    const char* id = "##nodegraph";
    ImVec2      canvasSize{ 0, 0 };   // {0,0} = fill the available content region
    // Caller-owned camera, persisted across frames by the CALLER — a fresh
    // camera per opened graph (NODE_GRAPH.md §5: "own camera, unrelated to
    // the Viewport's"). Required (never null).
    double* panX = nullptr;
    double* panY = nullptr;
    double* zoom = nullptr;
    // Node positions are read AND written here (dragging moves them in
    // place) — the caller persists whatever it wants from `nodes` after the
    // call returns (NodeGraphNode::pos is the only field this widget mutates
    // besides reporting selection in the result).
    std::vector<NodeGraphNode>* nodes = nullptr;
    const std::vector<NodeGraphEdge>* edges = nullptr;
};

struct NodeGraphResult {
    bool          nodeMoved       = false;   // true every frame a node is being dragged
    std::uint64_t movedNode       = 0;
    // The move drag just finished (mouse released) — the moved node's FINAL
    // position is settled this frame.
    bool          nodeMoveEnded   = false;
    std::uint64_t nodeMoveEndedId = 0;
    // The authoritative selection set as of THIS frame, whenever it changed
    // (click/ctrl-click/box-select/clear) — the caller should store this
    // onto its own persisted selection and feed it back via
    // NodeGraphNode::selected next frame. Empty + selectionChanged=true is a
    // valid "cleared" result, distinct from "nothing happened".
    bool                        selectionChanged = false;
    std::vector<std::uint64_t> selectedNodes;
    // A drag-to-connect gesture completed over a type-compatible port this
    // frame — the caller commits it through its own typed op (undo, pin
    // bookkeeping, etc.); the widget never mutates `edges` itself.
    bool          connected     = false;
    NodeGraphEdge newEdge{};
    // A drag-to-connect gesture ended without completing — over empty
    // canvas, an incompatible port, or cancelled (right-click/Escape).
    bool          dragEnded     = false;
    // Set whenever a cable drag CONCLUDES this frame (whether or not it
    // connected) — the source port it started from, so a caller can treat
    // "dropped in empty space" as "delete/detach the source's existing
    // connection" (Blender: dragging a link off into the void removes it).
    // Distinguishes a cable gesture ending from a MoveNode release, which
    // also sets `dragEnded` but leaves this false.
    bool          cableDragEnded     = false;
    std::uint64_t cableDragSourceNode = 0;
    int           cableDragSourcePort = -1;
};

// Draws the canvas (grid, nodes, cables) at the current cursor position,
// filling `canvasSize` (or the available content region), and handles pan
// (middle-mouse drag), zoom (mouse wheel, centred on the cursor), node
// dragging (left-drag a header), multi-select (click/ctrl-click/box-select
// on empty canvas) and drag-to-connect (left-drag from an Out port, snaps to
// a type-compatible In port; Escape/right-click cancels).
NodeGraphResult DrawNodeGraph(const NodeGraphConfig& cfg);

} // namespace UI

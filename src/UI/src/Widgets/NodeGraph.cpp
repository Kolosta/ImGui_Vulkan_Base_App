#include <UI/Widgets/NodeGraph.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t)   { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t)   { return DS::DesignSystem::Instance().GetFloat(t); }
ImVec2 V2(Tok t)    { return DS::DesignSystem::Instance().GetVec2(t); }
ImU32  ColU32(Tok t) { return ImGui::ColorConvertFloat4ToU32(Col(t)); }

// Muted (M): dims any resolved colour uniformly, header/body/border/ports/
// cables alike, rather than hiding the node — it stays fully legible as
// "present but bypassed".
ImU32 Dim(ImU32 c, bool muted) {
    if (!muted) return c;
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= 0.35f;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 PortColor(const NodeGraphPort& p, Tok fallback) {
    return p.color.w >= 0.0f ? ImGui::ColorConvertFloat4ToU32(p.color) : ColU32(fallback);
}

float PortRowH(float gs) { return 20.0f * gs; }

ImVec2 NodeBodySize(const NodeGraphNode& n, float gs, float headerH, ImVec2 pad) {
    if (n.collapsed) {
        const float w = std::max(120.0f * gs,
            ImGui::CalcTextSize(n.title.c_str()).x + pad.x * 2.0f + 40.0f * gs);
        return ImVec2(w, headerH);
    }
    int inCount = 0, outCount = 0;
    for (const NodeGraphPort& p : n.ports)
        (p.dir == NodePortDir::In ? inCount : outCount)++;
    const int rows = std::max(inCount, outCount);
    float w = std::max(160.0f * gs, ImGui::CalcTextSize(n.title.c_str()).x + pad.x * 2.0f);
    for (const NodeGraphPort& p : n.ports)
        w = std::max(w, ImGui::CalcTextSize(p.label.c_str()).x + pad.x * 2.0f + 28.0f * gs);
    const float h = headerH + pad.y * 2.0f + (float)rows * PortRowH(gs) + n.bodyHeight;
    return ImVec2(w, h);
}

// Canvas-space position of one port: In ports on the left edge, Out ports on
// the right edge, stacked top-to-bottom within their own column (this is
// what lets a node declare SEVERAL In ports — e.g. Merge, one per current
// input — and have each land at its own row with no extra widget support).
// Collapsed: every port sits at header-centre height regardless of count
// (Blender's collapsed-node behaviour — a multi-input socket collapses to
// one point too).
ImVec2 PortPosCanvas(const NodeGraphNode& n, int portIdx, float gs, float headerH,
                     ImVec2 pad, ImVec2 size) {
    const NodeGraphPort& p = n.ports[portIdx];
    const float x = (p.dir == NodePortDir::In) ? n.pos.x : n.pos.x + size.x;
    if (n.collapsed) return ImVec2(x, n.pos.y + headerH * 0.5f);
    int row = 0;
    for (int i = 0; i < portIdx; ++i)
        if (n.ports[i].dir == p.dir) ++row;
    const float y = n.pos.y + headerH + pad.y + (float)row * PortRowH(gs) + PortRowH(gs) * 0.5f;
    return ImVec2(x, y);
}

// Only one node-graph canvas is ever interactively dragged at a time — the
// same simplifying assumption UI::Panel's list-drag makes (a single file-
// static context rather than an ImGuiStorage entry per widget instance).
struct DragState {
    enum class Kind { None, MoveNode, ConnectCable, BoxSelect };
    Kind          kind = Kind::None;
    std::uint64_t nodeId = 0;     // node being moved, or the cable's source node
    int           portIndex = -1; // source port index (ConnectCable only)
    ImVec2        grabOffset{ 0, 0 }; // MoveNode: cursor-to-node-topleft, doc space
    ImVec2        boxStart{ 0, 0 };   // BoxSelect: screen-space press position
};
DragState& Drag() { static DragState s; return s; }

} // namespace

NodeGraphResult DrawNodeGraph(const NodeGraphConfig& cfg) {
    NodeGraphResult result;
    if (!cfg.nodes || !cfg.panX || !cfg.panY || !cfg.zoom) return result;

    ImGui::PushID(cfg.id);
    DS::DesignSystem& ds = DS::DesignSystem::Instance();
    DS::DesignSystem::ComponentScope _cs("NodeGraph");
    const float gs = ds.GetGlobalScale();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size = (cfg.canvasSize.x > 0 && cfg.canvasSize.y > 0) ? cfg.canvasSize : avail;
    const ImVec2 cMin = ImGui::GetCursorScreenPos();
    const ImVec2 cMax(cMin.x + size.x, cMin.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(cMin, cMax, true);
    dl->AddRectFilled(cMin, cMax, ColU32(Tok::C_NodeGraph_Background));

    double& panX = *cfg.panX; double& panY = *cfg.panY; double& zoom = *cfg.zoom;
    auto D2S = [&](ImVec2 d) -> ImVec2 {
        return ImVec2(cMin.x + (float)((d.x - panX) * zoom),
                      cMin.y + (float)((d.y - panY) * zoom));
    };
    auto S2D = [&](ImVec2 s) -> ImVec2 {
        return ImVec2((float)((s.x - cMin.x) / zoom + panX),
                      (float)((s.y - cMin.y) / zoom + panY));
    };

    ImGui::SetCursorScreenPos(cMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##ngCanvas", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    // LEFT press only — IsItemActivated() alone fires for the MIDDLE button
    // too (both are registered on this InvisibleButton for panning), which
    // was starting a box-select that then never got a matching release
    // (BoxSelect only ever checks the LEFT button) and stuck around forever.
    const bool canvasClicked = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        panX -= io.MouseDelta.x / zoom;
        panY -= io.MouseDelta.y / zoom;
    }
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const ImVec2 before = S2D(io.MousePos);
        const float fz = io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f;
        zoom = std::clamp(zoom * (double)fz, 0.15, 4.0);
        const ImVec2 after = S2D(io.MousePos);
        panX += before.x - after.x;
        panY += before.y - after.y;
    }

    // Grid — subtle: a faint reference, not a dominant pattern.
    {
        const float step = 32.0f * (float)zoom;
        if (step > 6.0f) {
            const ImVec4 gridC = Col(Tok::C_NodeGraph_GridLine);
            const ImU32 grid = ImGui::ColorConvertFloat4ToU32(
                ImVec4(gridC.x, gridC.y, gridC.z, gridC.w * 0.12f));
            const float ox = cMin.x - std::fmod((float)(panX * zoom), step);
            for (float gx = ox; gx < cMax.x; gx += step)
                dl->AddLine(ImVec2(gx, cMin.y), ImVec2(gx, cMax.y), grid, 1.0f);
            const float oy = cMin.y - std::fmod((float)(panY * zoom), step);
            for (float gy = oy; gy < cMax.y; gy += step)
                dl->AddLine(ImVec2(cMin.x, gy), ImVec2(cMax.x, gy), grid, 1.0f);
        }
    }

    std::vector<NodeGraphNode>& nodes = *cfg.nodes;
    const float headerH = Flt(Tok::C_NodeBox_HeaderHeight) * gs;
    const ImVec2 padTok = V2(Tok::C_NodeBox_Padding);
    const ImVec2 pad(padTok.x * gs, padTok.y * gs);
    const float radius = Flt(Tok::C_NodeBox_CornerRadius) * gs;
    const float portR = Flt(Tok::C_NodePort_Size) * 0.5f * gs;

    std::vector<ImVec2> nodeSize(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
        nodeSize[i] = NodeBodySize(nodes[i], gs, headerH, pad);

    DragState& drag = Drag();

    auto findNode = [&](std::uint64_t id) -> int {
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].id == id) return (int)i;
        return -1;
    };

    // Port hit-test (screen space, nearest within a small radius, any node).
    int hovNode = -1, hovPort = -1;
    {
        float best = (portR * 2.2f) * (portR * 2.2f);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            for (int p = 0; p < (int)nodes[i].ports.size(); ++p) {
                const ImVec2 ps = D2S(PortPosCanvas(nodes[i], p, gs, headerH, pad, nodeSize[i]));
                const float dx = ps.x - io.MousePos.x, dy = ps.y - io.MousePos.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hovNode = (int)i; hovPort = p; }
            }
        }
    }

    // Start a cable drag from an Out port — takes priority over box-select.
    if (drag.kind == DragState::Kind::None && hovNode >= 0 && hovPort >= 0 &&
        nodes[hovNode].ports[hovPort].dir == NodePortDir::Out &&
        canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag.kind = DragState::Kind::ConnectCable;
        drag.nodeId = nodes[hovNode].id;
        drag.portIndex = hovPort;
    }

    // A click that lands on the canvas background itself (not a port, not a
    // node header — those are separate, later-submitted items that win the
    // click over this one) starts a box-select. Handled here, before nodes
    // are drawn, so BoxSelect and MoveNode/ConnectCable can't both start on
    // the same press.
    if (drag.kind == DragState::Kind::None && canvasClicked && hovPort < 0) {
        drag.kind = DragState::Kind::BoxSelect;
        drag.boxStart = io.MousePos;
    }

    // Which (node, port) slots a committed edge already touches — drives the
    // "connected" port colour (default colour otherwise, hover/incompatible
    // still win while a cable is actively being dragged over them).
    auto isConnected = [&](std::uint64_t nodeId, int portIdx, NodePortDir dir) {
        if (!cfg.edges) return false;
        for (const NodeGraphEdge& e : *cfg.edges) {
            if (dir == NodePortDir::Out && e.fromNode == nodeId && e.fromPort == portIdx)
                return true;
            if (dir == NodePortDir::In && e.toNode == nodeId && e.toPort == portIdx)
                return true;
        }
        return false;
    };

    // Committed edges.
    if (cfg.edges) {
        for (const NodeGraphEdge& e : *cfg.edges) {
            const int a = findNode(e.fromNode), b = findNode(e.toNode);
            if (a < 0 || b < 0) continue;
            if (e.fromPort < 0 || e.fromPort >= (int)nodes[a].ports.size()) continue;
            if (e.toPort   < 0 || e.toPort   >= (int)nodes[b].ports.size()) continue;
            const ImVec2 aP = D2S(PortPosCanvas(nodes[a], e.fromPort, gs, headerH, pad, nodeSize[a]));
            const ImVec2 bP = D2S(PortPosCanvas(nodes[b], e.toPort,   gs, headerH, pad, nodeSize[b]));
            const float dx = std::max(30.0f * gs, std::fabs(bP.x - aP.x) * 0.5f);
            const bool muted = nodes[a].muted || nodes[b].muted;
            const ImU32 col = Dim(PortColor(nodes[a].ports[e.fromPort], Tok::C_NodeCable_Color), muted);
            dl->AddBezierCubic(aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y), bP,
                               col, Flt(Tok::C_NodeCable_Width) * gs);
        }
    }

    // Nodes: box + header drag/select + ports + optional body content.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        NodeGraphNode& n = nodes[i];
        const ImVec2 tl = D2S(n.pos);
        const ImVec2 br = D2S(ImVec2(n.pos.x + nodeSize[i].x, n.pos.y + nodeSize[i].y));
        const ImVec2 headerBr = D2S(ImVec2(n.pos.x + nodeSize[i].x, n.pos.y + headerH));

        if (n.topDraw) {
            const ImVec2 topMin = D2S(ImVec2(n.pos.x, n.pos.y - nodeSize[i].x));
            const ImVec2 topMax = D2S(ImVec2(n.pos.x + nodeSize[i].x, n.pos.y));
            n.topDraw(topMin, topMax);
        }

        const ImU32 headerCol = Dim(n.headerColor.w >= 0.0f
            ? ImGui::ColorConvertFloat4ToU32(n.headerColor)
            : ColU32(Tok::C_NodeBox_HeaderBackground), n.muted);
        dl->AddRectFilled(tl, br, Dim(ColU32(Tok::C_NodeBox_BodyBackground), n.muted), radius);
        dl->AddRectFilled(tl, headerBr, headerCol, radius, ImDrawFlags_RoundCornersTop);
        const ImU32 borderCol = Dim(n.selected ? ColU32(Tok::C_NodeBox_BorderSelected)
                                                : ColU32(Tok::C_NodeBox_Border), n.muted);
        dl->AddRect(tl, br, borderCol, radius, 0, (n.selected ? 2.0f : 1.0f) * gs);
        dl->AddText(ImVec2(tl.x + pad.x, tl.y + (headerBr.y - tl.y - ImGui::GetFontSize()) * 0.5f),
                   Dim(ColU32(Tok::C_NodeBox_Text), n.muted), n.title.c_str());

        // Header drag hit-rect (select + move). AllowOverlap so a port dot
        // sitting right at the header's edge still wins its own hover.
        ImGui::PushID((int)i);
        ImGui::SetCursorScreenPos(tl);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##ngHeader", ImVec2(br.x - tl.x, headerBr.y - tl.y));
        if (ImGui::IsItemActivated()) {
            // Selection: plain press = select only this one; Ctrl+press =
            // toggle it in the current set (Blender-style multi-select).
            std::vector<std::uint64_t> sel;
            if (io.KeyCtrl) {
                for (const NodeGraphNode& other : nodes) if (other.selected) sel.push_back(other.id);
                auto it = std::find(sel.begin(), sel.end(), n.id);
                if (it != sel.end()) sel.erase(it); else sel.push_back(n.id);
            } else if (!n.selected) {
                sel.push_back(n.id);
            } else {
                for (const NodeGraphNode& other : nodes) if (other.selected) sel.push_back(other.id);
            }
            result.selectionChanged = true;
            result.selectedNodes = sel;

            const ImVec2 mouseDoc = S2D(io.MousePos);
            drag.kind = DragState::Kind::MoveNode;
            drag.nodeId = n.id;
            drag.grabOffset = ImVec2(mouseDoc.x - n.pos.x, mouseDoc.y - n.pos.y);
        }
        ImGui::PopID();   // closes the header's PushID((int)i) above

        if (n.collapsed) {
            for (int p = 0; p < (int)n.ports.size(); ++p) {
                const ImVec2 pc = D2S(PortPosCanvas(n, p, gs, headerH, pad, nodeSize[i]));
                const bool isHover = (hovNode == (int)i && hovPort == p);
                ImU32 portCol = Dim(isConnected(n.id, p, n.ports[p].dir)
                                  ? PortColor(n.ports[p], Tok::C_NodePort_BackgroundConnected)
                                  : PortColor(n.ports[p], Tok::C_NodePort_Background), n.muted);
                if (isHover) portCol = ColU32(Tok::C_NodePort_BackgroundHover);
                dl->AddCircleFilled(pc, portR, portCol);
                dl->AddCircle(pc, portR, Dim(ColU32(Tok::C_NodePort_Border), n.muted), 0, 1.0f * gs);
            }
            continue;   // no body/port labels while collapsed
        }

        for (int p = 0; p < (int)n.ports.size(); ++p) {
            const ImVec2 pc = D2S(PortPosCanvas(n, p, gs, headerH, pad, nodeSize[i]));
            const bool isHover = (hovNode == (int)i && hovPort == p);
            bool compatible = true;
            if (drag.kind == DragState::Kind::ConnectCable && isHover) {
                const int srcIdx = findNode(drag.nodeId);
                compatible = srcIdx >= 0 && srcIdx != (int)i &&
                             n.ports[p].dir == NodePortDir::In &&
                             nodes[srcIdx].ports[drag.portIndex].type == n.ports[p].type;
            }
            ImU32 portCol = Dim(isConnected(n.id, p, n.ports[p].dir)
                          ? PortColor(n.ports[p], Tok::C_NodePort_BackgroundConnected)
                          : PortColor(n.ports[p], Tok::C_NodePort_Background), n.muted);
            if (isHover)
                portCol = compatible ? ColU32(Tok::C_NodePort_BackgroundHover)
                                     : ColU32(Tok::C_NodePort_BackgroundIncompatible);
            dl->AddCircleFilled(pc, portR, portCol);
            dl->AddCircle(pc, portR, Dim(ColU32(Tok::C_NodePort_Border), n.muted), 0, 1.0f * gs);

            if (!n.ports[p].label.empty()) {
                const ImVec2 labelSize = ImGui::CalcTextSize(n.ports[p].label.c_str());
                const float lx = (n.ports[p].dir == NodePortDir::In)
                                ? pc.x + portR * 2.0f
                                : pc.x - portR * 2.0f - labelSize.x;
                dl->AddText(ImVec2(lx, pc.y - labelSize.y * 0.5f), Dim(ColU32(Tok::C_NodeBox_Text), n.muted),
                           n.ports[p].label.c_str());
            }
        }

        if (n.bodyDraw && n.bodyHeight > 0.0f) {
            int inCount = 0, outCount = 0;
            for (const NodeGraphPort& pp : n.ports) (pp.dir == NodePortDir::In ? inCount : outCount)++;
            const int rows = std::max(inCount, outCount);
            const ImVec2 bodyTl = D2S(ImVec2(n.pos.x + pad.x,
                                             n.pos.y + headerH + pad.y + (float)rows * PortRowH(gs)));
            const ImVec2 bodyBr = D2S(ImVec2(n.pos.x + nodeSize[i].x - pad.x,
                                             n.pos.y + headerH + pad.y + (float)rows * PortRowH(gs) + n.bodyHeight));
            n.bodyDraw(bodyTl, bodyBr);
        }
    }

    // Apply the in-flight drag (after drawing every node, so the cable/ghost
    // /marquee overlays everything).
    if (drag.kind == DragState::Kind::MoveNode) {
        const int idx = findNode(drag.nodeId);
        if (idx >= 0) {
            const ImVec2 mouseDoc = S2D(io.MousePos);
            nodes[idx].pos = ImVec2(mouseDoc.x - drag.grabOffset.x, mouseDoc.y - drag.grabOffset.y);
            result.nodeMoved = true; result.movedNode = nodes[idx].id;
        }
        if (idx < 0 || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (idx >= 0) {
                result.nodeMoveEnded = true;
                result.nodeMoveEndedId = nodes[idx].id;
            }
            drag.kind = DragState::Kind::None;
            result.dragEnded = true;
        }
    } else if (drag.kind == DragState::Kind::ConnectCable) {
        const int srcIdx = findNode(drag.nodeId);
        if (srcIdx < 0) {
            drag.kind = DragState::Kind::None;
        } else {
            const ImVec2 aP = D2S(PortPosCanvas(nodes[srcIdx], drag.portIndex, gs, headerH, pad,
                                                nodeSize[srcIdx]));
            ImVec2 bP = io.MousePos;
            bool dropValid = false;
            if (hovNode >= 0 && hovNode != srcIdx && hovPort >= 0 &&
                nodes[hovNode].ports[hovPort].dir == NodePortDir::In &&
                nodes[hovNode].ports[hovPort].type == nodes[srcIdx].ports[drag.portIndex].type) {
                bP = D2S(PortPosCanvas(nodes[hovNode], hovPort, gs, headerH, pad, nodeSize[hovNode]));
                dropValid = true;
            }
            const float dx = std::max(30.0f * gs, std::fabs(bP.x - aP.x) * 0.5f);
            const ImU32 col = dropValid ? ColU32(Tok::C_NodeCable_ColorActive)
                                        : ColU32(Tok::C_NodeCable_ColorIncompatible);
            dl->AddBezierCubic(aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y), bP,
                               col, Flt(Tok::C_NodeCable_Width) * gs * 1.2f);
            dl->AddCircleFilled(bP, portR, col);

            const bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (cancel) {
                drag.kind = DragState::Kind::None;
                result.dragEnded = true;
                result.cableDragEnded = true;
                result.cableDragSourceNode = drag.nodeId;
                result.cableDragSourcePort = drag.portIndex;
            } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (dropValid) {
                    result.connected = true;
                    result.newEdge = { drag.nodeId, drag.portIndex, nodes[hovNode].id, hovPort };
                } else {
                    result.dragEnded = true;
                }
                result.cableDragEnded = true;
                result.cableDragSourceNode = drag.nodeId;
                result.cableDragSourcePort = drag.portIndex;
                drag.kind = DragState::Kind::None;
            }
        }
    } else if (drag.kind == DragState::Kind::BoxSelect) {
        const ImVec2 a = drag.boxStart, b = io.MousePos;
        const ImVec2 boxMin(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 boxMax(std::max(a.x, b.x), std::max(a.y, b.y));
        const float dragDist = std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y));
        if (dragDist > ImGui::GetIO().MouseDragThreshold) {
            ImVec4 fillV = Col(Tok::C_NodeBox_BorderSelected);
            fillV.w = 0.15f;
            dl->AddRectFilled(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(fillV));
            dl->AddRect(boxMin, boxMax, ColU32(Tok::C_NodeBox_BorderSelected), 0.0f, 0, 1.0f * gs);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            std::vector<std::uint64_t> sel;
            if (io.KeyCtrl)
                for (const NodeGraphNode& n : nodes) if (n.selected) sel.push_back(n.id);
            if (dragDist > ImGui::GetIO().MouseDragThreshold) {
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    const ImVec2 tl = D2S(nodes[i].pos);
                    const ImVec2 br = D2S(ImVec2(nodes[i].pos.x + nodeSize[i].x,
                                                 nodes[i].pos.y + nodeSize[i].y));
                    const bool overlap = tl.x <= boxMax.x && br.x >= boxMin.x &&
                                        tl.y <= boxMax.y && br.y >= boxMin.y;
                    if (overlap &&
                        std::find(sel.begin(), sel.end(), nodes[i].id) == sel.end())
                        sel.push_back(nodes[i].id);
                }
            }
            // A plain click (no drag) with no Ctrl clears the selection —
            // `sel` is already empty in that case.
            result.selectionChanged = true;
            result.selectedNodes = sel;
            drag.kind = DragState::Kind::None;
        }
    }

    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(cMin.x, cMin.y + size.y));
    ImGui::Dummy(ImVec2(size.x, 1.0f));
    ImGui::PopID();
    return result;
}

} // namespace UI

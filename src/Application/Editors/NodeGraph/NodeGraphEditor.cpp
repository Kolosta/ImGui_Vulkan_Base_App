#include "Application.h"

#include <Ink/Scene/CompGraph.h>
#include <Shortcuts/ShortcutManager.h>
#include <DesignSystem/DesignSystem.h>

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Node Graph Editor (docs/Ink/NODE_GRAPH.md §5/§7, docs/Ink/NODE_UI.md,
//  ROADMAP Lot 13). Named after the GRAPH, not "layer": the Layers view is
//  only one way of looking at the underlying Compositing Graph (the
//  Outliner's), so the editor that shows the graph itself must not be named
//  after that one view.
//
//  RENDERING (docs/Ink/NODE_UI.md, 2026-07-24): this canvas is 100% Vulkan —
//  no ImGui widget draws so much as a pixel inside it. Its own `Ink::View`
//  (SetContentVisible(false) — it has no relationship to the Document) is
//  filled every frame through `Overlay()` (node boxes, borders, ports,
//  cables — the SAME untextured pipeline the Viewport's own gizmos use) and
//  `NodeUI()` (glyph text + live preview vignettes — a NEW textured pipeline,
//  see NODE_UI.md §3 for why a second, separate pipeline is the right split),
//  then blitted with one `ImGui::AddImage`. Mouse/keyboard input over the
//  canvas is captured the exact way Viewport already does (raw
//  IsWindowHovered + io.MousePos, no InvisibleButton) and routed through a
//  hand-rolled hit-test/drag state machine below — there is no ImGui item on
//  this canvas to report Hovered/Active/Clicked. The breadcrumb header above
//  the canvas and the Shift+A/picker/blend-mode POPUPS still draw through
//  this same Vulkan canvas too (see NgDrawPopupList) — only the breadcrumb
//  ROW itself (outside the canvas rect) stays ImGui, per ARCHITECTURE.md's
//  "ImGui draws only the surrounding interface" rule.
//
//  Node chain, mirroring `Ink::CompNodeKind` (docs/Ink/NODE_GRAPH.md §3/§7 —
//  each node is exactly ONE real operation, not a bundle):
//      Object (this node's own paint stack, Path/Instance only) + ordinary
//      child Inputs → Merge (present whenever there's more than just the
//      Object alone) → Clip/Mask (if any, fed by its own dedicated mask-
//      source Input) → Blend (if any) → Output. A plain leaf shape with
//      neither children nor clip/blend is just Object → Output directly.
//      Reordering/excluding an ordinary child Input works TWO ways: drag its
//      BOX up/down (re-sorted by Y on drop) OR drag its cable off Merge and
//      re-drop it on a different Merge port / into empty space (Blender-
//      style link dragging) — both funnel into `Action_SetCompInputs`. The
//      Object input is never reorderable/excludable this way (it isn't a
//      compInputs entry — it's unconditional by construction).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
using DesignSystem::Tok;
namespace DS = DesignSystem;

constexpr std::uint64_t kOutputKey      = ~std::uint64_t(0);
constexpr std::uint64_t kMergeKey       = ~std::uint64_t(0) - 1;
constexpr std::uint64_t kBlendKey       = ~std::uint64_t(0) - 2;
constexpr std::uint64_t kClipMaskKey    = ~std::uint64_t(0) - 3;
constexpr std::uint64_t kPendingAddKey  = ~std::uint64_t(0) - 4;

std::uint64_t NgKey(const Ink::CompNode& n) {
    switch (n.kind) {
        case Ink::CompNodeKind::Output: return kOutputKey;
        case Ink::CompNodeKind::Merge:  return kMergeKey;
        case Ink::CompNodeKind::Blend:  return kBlendKey;
        case Ink::CompNodeKind::Clip:
        case Ink::CompNodeKind::Mask:   return kClipMaskKey;
        default:                        return n.target.node;   // Input (incl. Object)
    }
}

bool IsSentinelKey(std::uint64_t k) {
    return k == kOutputKey || k == kMergeKey || k == kBlendKey || k == kClipMaskKey;
}

std::vector<Ink::CompInputOverride> MaterializeCompInputs(const Ink::Document& doc,
                                                          const Ink::Node& layer) {
    if (!layer.compInputs.empty()) return layer.compInputs;
    const Ink::CompAutoMergeParams p = Ink::ComputeAutoMergeParams(doc, layer);
    const Ink::NodeId maskSource = p.hasClipMask ? p.clipNode : Ink::kNullNode;
    std::vector<Ink::CompInputOverride> list;
    list.reserve(layer.children.size());
    for (Ink::NodeId c : layer.children)
        if (c != maskSource) list.push_back({ c });
    return list;
}

// ── Node UI drawing model (docs/Ink/NODE_UI.md) — a from-scratch, ImGui-free
// replacement for the deleted UI::NodeGraph widget's node/port/edge structs. ──

struct NgPort { std::string label; bool isOut = false; };
enum class NgBody : std::uint8_t { None, Picker, BlendMode };
struct NgNode {
    std::uint64_t id = 0;
    std::string   title;
    ImVec2        pos{ 0.0f, 0.0f };    // canvas space, header top-left
    ImVec2        size{ 0.0f, 0.0f };   // computed this frame (header+ports+body only)
    std::vector<NgPort> ports;
    bool selected = false, muted = false, collapsed = false;
    Ink::Color headerColor{ 0.3f, 0.3f, 0.3f, 1.0f };
    bool hasPreview = false;
    Ink::NodeId previewTarget = Ink::kNullNode;
    NgBody body = NgBody::None;
    int   childSlot = -1;   // Picker: index into compInputs
};
struct NgEdge { std::uint64_t fromNode = 0; int fromPort = 0; std::uint64_t toNode = 0; int toPort = 0; };

Ink::Color TokCol(Tok t) {
    const ImVec4 v = DS::DesignSystem::Instance().GetColor(t);
    return Ink::SrgbToLinearPremultiplied(v.x, v.y, v.z, v.w);
}
float TokFlt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
ImVec2 TokVec2(Tok t) { return DS::DesignSystem::Instance().GetVec2(t); }

Ink::Color NgDim(Ink::Color c, bool muted) {
    if (!muted) return c;
    c.r *= 0.35f; c.g *= 0.35f; c.b *= 0.35f; c.a *= 0.35f;
    return c;
}

constexpr float kHeaderH  = 28.0f;
constexpr float kPortRowH = 20.0f;
constexpr float kMinNodeW = 160.0f;

float NgTextWidth(const Ink::Renderer& ink, const std::string& s, float px) {
    if (!ink.FontReady()) return (float)s.size() * px * 0.55f;
    const float scale = px / ink.FontReferenceSize();
    float w = 0.0f;
    for (unsigned char c : s) {
        const Ink::Renderer::GlyphMetrics g = ink.Glyph(c);
        if (g.found) w += g.advance * scale;
    }
    return w;
}

// Baseline-anchored (FreeType convention: bearingY is the distance from the
// baseline to the glyph bitmap's TOP), CANVAS-space camera-agnostic — the
// caller passes an already view-pixel-space `baselinePx`.
void NgDrawText(Ink::View& view, const Ink::Renderer& ink, ImVec2 baselinePx,
                const std::string& s, float px, const Ink::Color& col) {
    if (!ink.FontReady()) return;
    const float scale = px / ink.FontReferenceSize();
    float x = baselinePx.x;
    for (unsigned char c : s) {
        const Ink::Renderer::GlyphMetrics g = ink.Glyph(c);
        if (g.found && g.width > 0.0f && g.height > 0.0f) {
            const Ink::Vec2 qmin{ x + g.bearingX * scale, baselinePx.y - g.bearingY * scale };
            const Ink::Vec2 qmax{ qmin.x + g.width * scale, qmin.y + g.height * scale };
            view.NodeUI().AddAtlasQuad(qmin, qmax, { g.u0, g.v0 }, { g.u1, g.v1 }, col);
        }
        if (g.found) x += g.advance * scale;
    }
}

ImVec2 NgC2P(ImVec2 c, double panX, double panY, double zoom) {
    return ImVec2((float)((c.x - panX) * zoom), (float)((c.y - panY) * zoom));
}
ImVec2 NgP2C(ImVec2 p, double panX, double panY, double zoom) {
    return ImVec2((float)(p.x / zoom + panX), (float)(p.y / zoom + panY));
}

ImVec2 NgNodeSize(const NgNode& n, const Ink::Renderer& ink, float gs) {
    const float headerH = kHeaderH * gs;
    const ImVec2 pad = TokVec2(Tok::C_NodeBox_Padding);
    const ImVec2 padS(pad.x * gs, pad.y * gs);
    if (n.collapsed) {
        const float w = std::max(120.0f * gs,
            NgTextWidth(ink, n.title, 14.0f * gs) + padS.x * 2.0f + 40.0f * gs);
        return ImVec2(w, headerH);
    }
    int inCount = 0, outCount = 0;
    for (const NgPort& p : n.ports) (p.isOut ? outCount : inCount)++;
    const int rows = std::max(inCount, outCount);
    float w = std::max(kMinNodeW * gs, NgTextWidth(ink, n.title, 14.0f * gs) + padS.x * 2.0f);
    for (const NgPort& p : n.ports)
        w = std::max(w, NgTextWidth(ink, p.label, 13.0f * gs) + padS.x * 2.0f + 28.0f * gs);
    float bodyH = 0.0f;
    if (n.body != NgBody::None) bodyH += 26.0f * gs;
    const float h = headerH + padS.y * 2.0f + (float)rows * kPortRowH * gs + bodyH;
    return ImVec2(w, h);
}

ImVec2 NgPortPosCanvas(const NgNode& n, int idx, float gs) {
    const NgPort& p = n.ports[idx];
    const float x = p.isOut ? n.pos.x + n.size.x : n.pos.x;
    const float headerH = kHeaderH * gs;
    if (n.collapsed) return ImVec2(x, n.pos.y + headerH * 0.5f);
    const ImVec2 pad = TokVec2(Tok::C_NodeBox_Padding);
    int row = 0;
    for (int i = 0; i < idx; ++i) if (n.ports[i].isOut == p.isOut) ++row;
    const float y = n.pos.y + headerH + pad.y * gs + (float)row * kPortRowH * gs +
                   kPortRowH * gs * 0.5f;
    return ImVec2(x, y);
}

// A node's BODY rect (Picker / BlendMode control row), CANVAS space —
// directly below its ports, full width, one row tall. NgBody::None nodes
// have no body rect (caller must check `n.body` first).
void NgBodyRectCanvas(const NgNode& n, float gs, ImVec2& outMin, ImVec2& outMax) {
    int inCount = 0, outCount = 0;
    for (const NgPort& p : n.ports) (p.isOut ? outCount : inCount)++;
    const int rows = std::max(inCount, outCount);
    const ImVec2 pad = TokVec2(Tok::C_NodeBox_Padding);
    const float top = n.pos.y + kHeaderH * gs + pad.y * gs + (float)rows * kPortRowH * gs;
    outMin = ImVec2(n.pos.x + pad.x * gs, top);
    outMax = ImVec2(n.pos.x + n.size.x - pad.x * gs, top + 26.0f * gs);
}

void NgDrawCable(Ink::View& view, ImVec2 p0, ImVec2 c0, ImVec2 c1, ImVec2 p1,
                 const Ink::Color& col, float thickness) {
    constexpr int kSeg = 20;
    ImVec2 prev = p0;
    for (int i = 1; i <= kSeg; ++i) {
        const float t = (float)i / (float)kSeg, u = 1.0f - t;
        const ImVec2 p((u * u * u) * p0.x + 3.0f * u * u * t * c0.x +
                       3.0f * u * t * t * c1.x + (t * t * t) * p1.x,
                       (u * u * u) * p0.y + 3.0f * u * u * t * c0.y +
                       3.0f * u * t * t * c1.y + (t * t * t) * p1.y);
        view.Overlay().AddLine({ prev.x, prev.y }, { p.x, p.y }, col, thickness);
        prev = p;
    }
}

// Every document node, by name, walking Pages -> recursive children — the
// same listing `pr::NodePickerRow` used, inlined here (that ImGui widget no
// longer belongs on this canvas, docs/Ink/NODE_UI.md).
void NgListAllNodes(const Ink::Document& doc, Ink::NodeId self,
                    std::vector<Ink::NodeId>& outIds, std::vector<std::string>& outNames) {
    for (const Ink::Page& page : doc.Pages()) {
        std::vector<Ink::NodeId> stack(page.children.begin(), page.children.end());
        while (!stack.empty()) {
            const Ink::NodeId id = stack.back(); stack.pop_back();
            const Ink::Node* n = doc.Find(id);
            if (!n) continue;
            for (Ink::NodeId ch : n->children) stack.push_back(ch);
            if (id == self) continue;
            outIds.push_back(id);
            outNames.push_back(n->name.empty() ? "(unnamed)" : n->name);
        }
    }
}

} // namespace

void Application::Action_SetCompInputs(Ink::NodeId layer,
                                       std::vector<Ink::CompInputOverride> inputs) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(layer);
    if (!n) return;
    const std::vector<Ink::CompInputOverride> before = n->compInputs;
    doc.SetCompInputs(layer, std::move(inputs));
    const std::vector<Ink::CompInputOverride> after = doc.Find(layer)->compInputs;
    PushDocCommand("Node Graph",
        [layer, before](Ink::Document& d) { d.SetCompInputs(layer, before); },
        [layer, after](Ink::Document& d) { d.SetCompInputs(layer, after); });
    LogInfoAction("Node Graph");
}

void Application::Action_ResetCompInputs(Ink::NodeId layer) {
    Action_SetCompInputs(layer, {});
}

void Application::Action_SetCompMergeMuted(Ink::NodeId layer, bool muted) {
    Action_SetCompBlendMuted(layer, muted);
}

void Application::Action_SetCompBlendMuted(Ink::NodeId layer, bool muted) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(layer);
    if (!n || n->compBlendMuted == muted) return;
    doc.SetCompBlendMuted(layer, muted);
    PushDocCommand(muted ? "Mute Blend" : "Unmute Blend",
        [layer, muted](Ink::Document& d) { d.SetCompBlendMuted(layer, !muted); },
        [layer, muted](Ink::Document& d) { d.SetCompBlendMuted(layer, muted); });
    LogInfoAction(muted ? "Mute Blend" : "Unmute Blend");
}

void Application::Action_NodeGraphDeleteSelected() {
    if (!project_.document || ngSelected_.empty()) return;
    const Ink::NodeId activeId = edit_.active;
    Ink::Document& doc = *project_.document;
    const Ink::Node* layer = doc.Find(activeId);
    if (!layer || layer->kind != Ink::NodeKind::Group) return;
    std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Ink::CompInputOverride& ov) {
                                  return ngSelected_.count(ov.node) > 0;
                              }),
              list.end());
    Action_SetCompInputs(activeId, std::move(list));
    ngSelected_.clear();
}

void Application::Action_NodeGraphMuteToggleSelected() {
    if (!project_.document || ngSelected_.empty()) return;
    const Ink::NodeId activeId = edit_.active;
    Ink::Document& doc = *project_.document;
    const Ink::Node* layer = doc.Find(activeId);
    if (!layer || layer->kind != Ink::NodeKind::Group) return;
    if (ngSelected_.count(kBlendKey))
        Action_SetCompBlendMuted(activeId, !layer->compBlendMuted);
    const bool anyInputSelected = std::any_of(ngSelected_.begin(), ngSelected_.end(),
        [](std::uint64_t k) { return !IsSentinelKey(k); });
    if (anyInputSelected) {
        std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
        for (Ink::CompInputOverride& ov : list)
            if (ngSelected_.count(ov.node)) ov.muted = !ov.muted;
        Action_SetCompInputs(activeId, std::move(list));
    }
}

void Application::Action_NodeGraphCollapseToggleSelected() {
    for (std::uint64_t key : ngSelected_) {
        auto it = ngCollapsed_.find(key);
        if (it != ngCollapsed_.end()) ngCollapsed_.erase(it); else ngCollapsed_.insert(key);
    }
}

void Application::Action_NodeGraphOpenAddMenu() { ngAddMenuRequested_ = true; }

void Application::RenderNodeGraphEditor(EditorState&) {
    Shortcuts::ShortcutManager::Instance()
        .RegisterRegionContext("##zone", "nodegraph", "content");

    if (!project_.document) {
        ImGui::TextDisabled("No document.");
        return;
    }
    Ink::Document& doc = *project_.document;
    const Ink::NodeId activeId = edit_.active;
    const Ink::Node* layer = activeId != Ink::kNullNode ? doc.Find(activeId) : nullptr;

    if (activeId != ngLastActive_) {
        ngSelected_.clear();
        ngLastActive_ = activeId;
        ngPendingPlacement_ = false;
        ngPendingPlaced_ = false;
        ngPopupKind_ = NgPopupKind::None;
        ngDragKind_ = NgDragKind::None;
    }

    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    // ── Breadcrumb header: SURROUNDING interface, stays ImGui (ARCHITECTURE.md
    // §"ImGui draws only the surrounding interface" — this row is chrome
    // above the canvas, not canvas content). ──
    ImGui::Dummy(ImVec2(1.0f, 4.0f * gs));
    ImGui::Indent(8.0f * gs);
    if (layer) {
        std::vector<std::pair<Ink::NodeId, std::string>> chain;
        std::vector<std::pair<Ink::NodeId, std::string>> rev;
        Ink::NodeId p = layer->parent;
        while (p != Ink::kNullNode) {
            const Ink::Node* pn = doc.Find(p);
            if (!pn) break;
            rev.push_back({ p, pn->name });
            p = pn->parent;
        }
        chain.assign(rev.rbegin(), rev.rend());
        auto crumb = [&](const std::string& text, bool last) {
            ImGui::TextUnformatted(text.c_str());
            if (!last) { ImGui::SameLine(0.0f, 4.0f * gs); ImGui::TextDisabled(">"); ImGui::SameLine(0.0f, 4.0f * gs); }
        };
        if (chain.size() <= 4) {
            for (const auto& entry : chain) crumb(entry.second, false);
        } else {
            crumb(chain.front().second, false);
            crumb("...", false);
            for (std::size_t i = chain.size() - 3; i < chain.size(); ++i)
                crumb(chain[i].second, false);
        }
        crumb(layer->name, true);
    } else {
        ImGui::TextDisabled("No active object");
    }
    ImGui::Unindent(8.0f * gs);
    const bool canEdit = layer && layer->kind == Ink::NodeKind::Group;
    if (canEdit && !layer->compInputs.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(customized)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to Automatic"))
            Action_ResetCompInputs(activeId);
    }
    ImGui::Dummy(ImVec2(1.0f, 4.0f * gs));
    ImGui::Separator();

    if (!layer) {
        ImGui::TextDisabled("(select an object in the Viewport or Outliner)");
        return;
    }
    if (!ink_) {
        ImGui::TextDisabled("Ink engine unavailable.");
        return;
    }

    const Ink::CompGraph graph = Ink::BuildAutoGraph(doc, *layer);

    // ── Build the Node UI node/edge lists (unchanged CompGraph consumption —
    // only the RENDER TARGET type changed from UI::NodeGraphNode to NgNode). ──
    std::vector<NgNode> nodes;
    std::vector<NgEdge> edges;
    std::vector<std::uint64_t> inputKeys;

    auto addOrdinaryInput = [&](const Ink::CompNode& n, int posSlot, int childSlot) {
        NgNode un;
        un.id = NgKey(n);
        un.title = n.isObjectInput ? "Object" : "Layer Input";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderInput);
        un.selected = ngSelected_.count(un.id) > 0;
        un.muted = n.muted;
        un.collapsed = ngCollapsed_.count(un.id) > 0;
        un.ports.push_back({ "", true });
        un.hasPreview = ngShowPreviews_;
        un.previewTarget = n.target.node;
        if (canEdit && !n.isObjectInput) { un.body = NgBody::Picker; un.childSlot = childSlot; }
        auto it = ngNodePos_.find(un.id);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(0.0f, (float)posSlot * 160.0f * gs); ngNodePos_[un.id] = un.pos; }
        inputKeys.push_back(un.id);
        nodes.push_back(std::move(un));
    };

    int posSlot = 0, childSlot = 0;
    for (const Ink::CompNode& n : graph.nodes) {
        if (n.kind == Ink::CompNodeKind::Input && !n.isMaskSourceInput) {
            addOrdinaryInput(n, posSlot++, n.isObjectInput ? -1 : childSlot);
            if (!n.isObjectInput) ++childSlot;
        }
    }

    std::uint64_t maskSourceKey = 0;
    bool hasMaskSourceInput = false;
    for (const Ink::CompNode& n : graph.nodes) {
        if (n.kind != Ink::CompNodeKind::Input || !n.isMaskSourceInput) continue;
        NgNode un;
        un.id = NgKey(n);
        const Ink::Node* t = doc.Find(n.target.node);
        un.title = t ? (t->name.empty() ? "(unnamed)" : t->name) : "(missing)";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderInput);
        un.selected = ngSelected_.count(un.id) > 0;
        un.collapsed = ngCollapsed_.count(un.id) > 0;
        un.ports.push_back({ "Mask", true });
        un.hasPreview = ngShowPreviews_;
        un.previewTarget = n.target.node;
        auto it = ngNodePos_.find(un.id);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(0.0f, -180.0f * gs); ngNodePos_[un.id] = un.pos; }
        maskSourceKey = un.id;
        hasMaskSourceInput = true;
        nodes.push_back(std::move(un));
    }

    const Ink::CompNode* merge = graph.FindMerge();
    std::uint64_t sinkKey = inputKeys.empty() ? kOutputKey : inputKeys.front();
    if (merge) {
        NgNode un;
        un.id = kMergeKey;
        un.title = "Merge";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderMerge);
        un.selected = ngSelected_.count(kMergeKey) > 0;
        un.collapsed = ngCollapsed_.count(kMergeKey) > 0;
        for (std::size_t k = 0; k < inputKeys.size(); ++k) un.ports.push_back({ "", false });
        un.ports.push_back({ "Result", true });
        auto it = ngNodePos_.find(kMergeKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(280.0f * gs, 0.0f); ngNodePos_[kMergeKey] = un.pos; }
        nodes.push_back(std::move(un));
        sinkKey = kMergeKey;
    }

    const Ink::CompNode* clipOrMask = graph.FindClipOrMask();
    if (clipOrMask) {
        const bool isMask = clipOrMask->kind == Ink::CompNodeKind::Mask;
        NgNode un;
        un.id = kClipMaskKey;
        un.title = isMask ? "Mask" : "Clip";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderClipMask);
        un.selected = ngSelected_.count(kClipMaskKey) > 0;
        un.collapsed = ngCollapsed_.count(kClipMaskKey) > 0;
        un.ports.push_back({ "Content", false });
        un.ports.push_back({ "Mask", false });
        un.ports.push_back({ "Result", true });
        auto it = ngNodePos_.find(kClipMaskKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(420.0f * gs, 0.0f); ngNodePos_[kClipMaskKey] = un.pos; }
        edges.push_back({ sinkKey, merge ? (int)inputKeys.size() : 0, kClipMaskKey, 0 });
        if (hasMaskSourceInput) edges.push_back({ maskSourceKey, 0, kClipMaskKey, 1 });
        nodes.push_back(std::move(un));
        sinkKey = kClipMaskKey;
    }

    const Ink::CompNode* blend = graph.FindBlend();
    if (blend) {
        NgNode un;
        un.id = kBlendKey;
        un.title = "Blend";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderBlend);
        un.selected = ngSelected_.count(kBlendKey) > 0;
        un.muted = blend->muted;
        un.collapsed = ngCollapsed_.count(kBlendKey) > 0;
        un.ports.push_back({ "Content", false });
        un.ports.push_back({ "Result", true });
        un.body = NgBody::BlendMode;
        auto it = ngNodePos_.find(kBlendKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(clipOrMask ? 560.0f * gs : 420.0f * gs, 0.0f); ngNodePos_[kBlendKey] = un.pos; }
        const int fromPort = (sinkKey == kMergeKey && merge) ? (int)inputKeys.size()
                            : (sinkKey == kClipMaskKey ? 2 : 0);
        edges.push_back({ sinkKey, fromPort, kBlendKey, 0 });
        nodes.push_back(std::move(un));
        sinkKey = kBlendKey;
    }

    {
        NgNode un;
        un.id = kOutputKey;
        un.title = "Output";
        un.headerColor = TokCol(Tok::C_NodeBox_HeaderOutput);
        un.selected = ngSelected_.count(kOutputKey) > 0;
        un.collapsed = ngCollapsed_.count(kOutputKey) > 0;
        un.ports.push_back({ "Layer", false });
        auto it = ngNodePos_.find(kOutputKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(720.0f * gs, 0.0f); ngNodePos_[kOutputKey] = un.pos; }
        const int fromPort =
            sinkKey == kMergeKey && merge ? (int)inputKeys.size()
          : sinkKey == kClipMaskKey       ? 2
          : sinkKey == kBlendKey          ? 1
          : 0;
        edges.push_back({ sinkKey, fromPort, kOutputKey, 0 });
        nodes.push_back(std::move(un));
    }

    for (std::size_t k = 0; k < inputKeys.size(); ++k)
        if (merge) edges.push_back({ inputKeys[k], 0, kMergeKey, (int)k });

    // ── Acquire the canvas View (docs/Ink/NODE_UI.md) ──
    static int kNgViewKey = 0;
    Ink::View* view = ink_->AcquireView(&kNgViewKey);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const std::uint32_t vw = (std::uint32_t)std::max(1.0f, avail.x);
    const std::uint32_t vh = (std::uint32_t)std::max(1.0f, avail.y);
    view->SetViewport(vw, vh);
    view->SetContentVisible(false);
    view->SetBackground(TokCol(Tok::C_NodeGraph_Background));

    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImGuiIO& io = ImGui::GetIO();
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        io.MousePos.x >= canvasOrigin.x && io.MousePos.x <= canvasOrigin.x + avail.x &&
        io.MousePos.y >= canvasOrigin.y && io.MousePos.y <= canvasOrigin.y + avail.y;
    const ImVec2 mousePx(io.MousePos.x - canvasOrigin.x, io.MousePos.y - canvasOrigin.y);

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        ngPanX_ -= io.MouseDelta.x / ngZoom_;
        ngPanY_ -= io.MouseDelta.y / ngZoom_;
    }
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const ImVec2 before = NgP2C(mousePx, ngPanX_, ngPanY_, ngZoom_);
        const float fz = io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f;
        ngZoom_ = std::clamp(ngZoom_ * (double)fz, 0.15, 4.0);
        const ImVec2 after = NgP2C(mousePx, ngPanX_, ngPanY_, ngZoom_);
        ngPanX_ += before.x - after.x;
        ngPanY_ += before.y - after.y;
    }

    // ── Pending "Layer Input" placement ghost (Shift+A flow) ──
    if (canEdit && ngPendingPlacement_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ngPendingPlacement_ = false;
    if (canEdit && ngPendingPlaced_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ngPendingPlaced_ = false;
    if (canEdit && ngPendingPlacement_) {
        NgNode ghost;
        ghost.id = kPendingAddKey;
        ghost.title = "Layer Input";
        ghost.headerColor = TokCol(Tok::C_NodeBox_HeaderInput);
        ghost.ports.push_back({ "", true });
        ghost.pos = NgP2C(mousePx, ngPanX_, ngPanY_, ngZoom_);
        nodes.push_back(std::move(ghost));
    } else if (canEdit && ngPendingPlaced_) {
        NgNode ghost;
        ghost.id = kPendingAddKey;
        ghost.title = "Layer Input";
        ghost.headerColor = TokCol(Tok::C_NodeBox_HeaderInput);
        ghost.ports.push_back({ "", true });
        ghost.pos = ngPendingPos_;
        ghost.body = NgBody::Picker;
        ghost.childSlot = -2;   // sentinel: "not yet a real compInputs entry"
        nodes.push_back(std::move(ghost));
    }

    // ── Layout ──
    std::vector<ImVec2> sizes(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) sizes[i] = NgNodeSize(nodes[i], *ink_, gs);
    for (std::size_t i = 0; i < nodes.size(); ++i) nodes[i].size = sizes[i];

    auto findNode = [&](std::uint64_t id) -> int {
        for (std::size_t i = 0; i < nodes.size(); ++i) if (nodes[i].id == id) return (int)i;
        return -1;
    };

    // ── Hit-testing (this frame's node/port/body rects vs the mouse) ──
    const float portR = TokFlt(Tok::C_NodePort_Size) * 0.5f * gs;
    int hovNode = -1, hovPort = -1;
    {
        float best = (portR * 2.2f) * (portR * 2.2f);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].collapsed) {
                for (int p = 0; p < (int)nodes[i].ports.size(); ++p) {
                    const ImVec2 ps = NgC2P(NgPortPosCanvas(nodes[i], p, gs), ngPanX_, ngPanY_, ngZoom_);
                    const float dx = ps.x - mousePx.x, dy = ps.y - mousePx.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < best) { best = d2; hovNode = (int)i; hovPort = p; }
                }
                continue;
            }
            for (int p = 0; p < (int)nodes[i].ports.size(); ++p) {
                const ImVec2 ps = NgC2P(NgPortPosCanvas(nodes[i], p, gs), ngPanX_, ngPanY_, ngZoom_);
                const float dx = ps.x - mousePx.x, dy = ps.y - mousePx.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hovNode = (int)i; hovPort = p; }
            }
        }
    }
    int hovHeaderNode = -1, hovBodyNode = -1;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const NgNode& n = nodes[i];
        const ImVec2 tl = NgC2P(n.pos, ngPanX_, ngPanY_, ngZoom_);
        const ImVec2 headerBr = NgC2P(ImVec2(n.pos.x + n.size.x,
                                            n.pos.y + (n.collapsed ? n.size.y : kHeaderH * gs)),
                                      ngPanX_, ngPanY_, ngZoom_);
        if (mousePx.x >= tl.x && mousePx.x <= headerBr.x &&
            mousePx.y >= tl.y && mousePx.y <= headerBr.y)
            hovHeaderNode = (int)i;
        if (!n.collapsed && n.body != NgBody::None) {
            ImVec2 bMin, bMax;
            NgBodyRectCanvas(n, gs, bMin, bMax);
            const ImVec2 btl = NgC2P(bMin, ngPanX_, ngPanY_, ngZoom_);
            const ImVec2 bbr = NgC2P(bMax, ngPanX_, ngPanY_, ngZoom_);
            if (mousePx.x >= btl.x && mousePx.x <= bbr.x &&
                mousePx.y >= btl.y && mousePx.y <= bbr.y)
                hovBodyNode = (int)i;
        }
    }

    const bool mouseClickedL = canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // Body row click (Picker / BlendMode) opens the shared popup — checked
    // BEFORE header/box-select so it wins the click (body rects never
    // overlap header rects, so this is unambiguous, not a priority hack).
    if (mouseClickedL && ngDragKind_ == NgDragKind::None && ngPopupKind_ == NgPopupKind::None &&
        hovBodyNode >= 0 && hovPort < 0) {
        const NgNode& n = nodes[(std::size_t)hovBodyNode];
        ImVec2 bMin, bMax;
        NgBodyRectCanvas(n, gs, bMin, bMax);
        ngPopupPos_ = NgC2P(ImVec2(bMin.x, bMax.y), ngPanX_, ngPanY_, ngZoom_);
        ngPopupOwner_ = activeId;
        if (n.body == NgBody::Picker) {
            ngPopupKind_ = NgPopupKind::Picker;
            ngPopupChildSlot_ = n.childSlot;
        } else if (n.body == NgBody::BlendMode) {
            ngPopupKind_ = NgPopupKind::BlendMode;
        }
    }

    // Start a cable drag from an Out port.
    if (ngDragKind_ == NgDragKind::None && ngPopupKind_ == NgPopupKind::None &&
        hovNode >= 0 && hovPort >= 0 && nodes[(std::size_t)hovNode].ports[(std::size_t)hovPort].isOut &&
        mouseClickedL) {
        ngDragKind_ = NgDragKind::ConnectCable;
        ngDragNode_ = nodes[(std::size_t)hovNode].id;
        ngDragPort_ = hovPort;
    }
    // Header press: select (+ start move).
    else if (ngDragKind_ == NgDragKind::None && ngPopupKind_ == NgPopupKind::None &&
             hovHeaderNode >= 0 && hovPort < 0 && mouseClickedL) {
        NgNode& n = nodes[(std::size_t)hovHeaderNode];
        if (io.KeyCtrl) {
            if (n.selected) ngSelected_.erase(n.id); else ngSelected_.insert(n.id);
        } else if (!n.selected) {
            ngSelected_.clear();
            ngSelected_.insert(n.id);
        }
        for (NgNode& other : nodes) other.selected = ngSelected_.count(other.id) > 0;
        ngDragKind_ = NgDragKind::MoveNode;
        ngDragNode_ = n.id;
        const ImVec2 mouseCanvas = NgP2C(mousePx, ngPanX_, ngPanY_, ngZoom_);
        ngDragGrabOffset_ = ImVec2(mouseCanvas.x - n.pos.x, mouseCanvas.y - n.pos.y);
    }
    // Empty canvas press: box-select.
    else if (ngDragKind_ == NgDragKind::None && ngPopupKind_ == NgPopupKind::None &&
             canvasHovered && mouseClickedL && hovPort < 0 && hovHeaderNode < 0 && hovBodyNode < 0) {
        ngDragKind_ = NgDragKind::BoxSelect;
        ngDragBoxStart_ = mousePx;
    }

    // ── Draw: committed cables first, then nodes on top ──
    for (const NgEdge& e : edges) {
        const int a = findNode(e.fromNode), b = findNode(e.toNode);
        if (a < 0 || b < 0) continue;
        if (e.fromPort < 0 || e.fromPort >= (int)nodes[(std::size_t)a].ports.size()) continue;
        if (e.toPort < 0 || e.toPort >= (int)nodes[(std::size_t)b].ports.size()) continue;
        const ImVec2 aP = NgC2P(NgPortPosCanvas(nodes[(std::size_t)a], e.fromPort, gs), ngPanX_, ngPanY_, ngZoom_);
        const ImVec2 bP = NgC2P(NgPortPosCanvas(nodes[(std::size_t)b], e.toPort, gs), ngPanX_, ngPanY_, ngZoom_);
        const float dx = std::max(30.0f * gs * (float)ngZoom_, std::fabs(bP.x - aP.x) * 0.5f);
        const bool muted = nodes[(std::size_t)a].muted || nodes[(std::size_t)b].muted;
        const Ink::Color col = NgDim(TokCol(Tok::C_NodeCable_Color), muted);
        NgDrawCable(*view, aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y), bP,
                   col, TokFlt(Tok::C_NodeCable_Width) * gs * (float)ngZoom_);
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const NgNode& n = nodes[i];
        const ImVec2 tl = NgC2P(n.pos, ngPanX_, ngPanY_, ngZoom_);
        const ImVec2 br = NgC2P(ImVec2(n.pos.x + n.size.x, n.pos.y + n.size.y), ngPanX_, ngPanY_, ngZoom_);
        const ImVec2 headerBr = NgC2P(ImVec2(n.pos.x + n.size.x, n.pos.y + kHeaderH * gs), ngPanX_, ngPanY_, ngZoom_);

        // Live preview vignette, ABOVE the header — a square exactly as wide
        // as the node, so it scales WITH the node under zoom (docs/Ink/
        // NODE_UI.md task #3).
        if (n.hasPreview && n.previewTarget != Ink::kNullNode) {
            const float dispW = br.x - tl.x;
            if (dispW > 1.0f) {
                const int fetchPx = (int)std::clamp(dispW, 16.0f, 256.0f);
                const std::uint64_t set = NodePreviewDescriptorSet((std::uint64_t)n.previewTarget, fetchPx);
                if (set != 0) {
                    const ImVec2 pTl(tl.x, tl.y - dispW);
                    view->NodeUI().AddPreviewQuad({ pTl.x, pTl.y }, { br.x, tl.y }, set);
                }
            }
        }

        const Ink::Color headerCol = NgDim(n.headerColor, n.muted);
        view->Overlay().AddRectFilled({ tl.x, tl.y }, { br.x, br.y },
                                      NgDim(TokCol(Tok::C_NodeBox_BodyBackground), n.muted));
        view->Overlay().AddRectFilled({ tl.x, tl.y }, { headerBr.x, headerBr.y }, headerCol);
        const Ink::Color borderCol = NgDim(n.selected ? TokCol(Tok::C_NodeBox_BorderSelected)
                                                      : TokCol(Tok::C_NodeBox_Border), n.muted);
        view->Overlay().AddRect({ tl.x, tl.y }, { br.x, br.y }, borderCol, (n.selected ? 2.0f : 1.0f) * gs);

        const float titlePx = 14.0f * gs * (float)ngZoom_;
        NgDrawText(*view, *ink_, ImVec2(tl.x + 8.0f * gs * (float)ngZoom_,
                                       tl.y + (headerBr.y - tl.y) * 0.5f + titlePx * 0.32f),
                  n.title, titlePx, NgDim(TokCol(Tok::C_NodeBox_Text), n.muted));

        if (n.collapsed) {
            for (int p = 0; p < (int)n.ports.size(); ++p) {
                const ImVec2 pc = NgC2P(NgPortPosCanvas(n, p, gs), ngPanX_, ngPanY_, ngZoom_);
                const bool isHover = (hovNode == (int)i && hovPort == p);
                Ink::Color portCol = NgDim(TokCol(Tok::C_NodePort_Background), n.muted);
                if (isHover) portCol = TokCol(Tok::C_NodePort_BackgroundHover);
                view->Overlay().AddCircleFilled({ pc.x, pc.y }, portR, portCol);
                view->Overlay().AddCircle({ pc.x, pc.y }, portR, NgDim(TokCol(Tok::C_NodePort_Border), n.muted));
            }
            continue;
        }

        for (int p = 0; p < (int)n.ports.size(); ++p) {
            const ImVec2 pc = NgC2P(NgPortPosCanvas(n, p, gs), ngPanX_, ngPanY_, ngZoom_);
            const bool isHover = (hovNode == (int)i && hovPort == p);
            bool compatible = true;
            if (ngDragKind_ == NgDragKind::ConnectCable && isHover) {
                const int srcIdx = findNode(ngDragNode_);
                compatible = srcIdx >= 0 && srcIdx != (int)i && !n.ports[(std::size_t)p].isOut;
            }
            Ink::Color portCol = NgDim(TokCol(Tok::C_NodePort_Background), n.muted);
            if (isHover) portCol = compatible ? TokCol(Tok::C_NodePort_BackgroundHover)
                                              : TokCol(Tok::C_NodePort_BackgroundIncompatible);
            view->Overlay().AddCircleFilled({ pc.x, pc.y }, portR, portCol);
            view->Overlay().AddCircle({ pc.x, pc.y }, portR, NgDim(TokCol(Tok::C_NodePort_Border), n.muted));
            if (!n.ports[(std::size_t)p].label.empty()) {
                const float labelPx = 12.0f * gs * (float)ngZoom_;
                const float lw = NgTextWidth(*ink_, n.ports[(std::size_t)p].label, labelPx);
                const float lx = n.ports[(std::size_t)p].isOut ? pc.x - portR * 2.0f - lw : pc.x + portR * 2.0f;
                NgDrawText(*view, *ink_, ImVec2(lx, pc.y + labelPx * 0.32f),
                          n.ports[(std::size_t)p].label, labelPx, NgDim(TokCol(Tok::C_NodeBox_Text), n.muted));
            }
        }

        if (n.body != NgBody::None) {
            ImVec2 bMin, bMax;
            NgBodyRectCanvas(n, gs, bMin, bMax);
            const ImVec2 btl = NgC2P(bMin, ngPanX_, ngPanY_, ngZoom_);
            const ImVec2 bbr = NgC2P(bMax, ngPanX_, ngPanY_, ngZoom_);
            const bool hov = hovBodyNode == (int)i;
            view->Overlay().AddRectFilled({ btl.x, btl.y }, { bbr.x, bbr.y },
                TokCol(hov ? Tok::C_NodePort_BackgroundHover : Tok::C_NodeBox_HeaderBackground));
            view->Overlay().AddRect({ btl.x, btl.y }, { bbr.x, bbr.y }, TokCol(Tok::C_NodeBox_Border));
            std::string label = "(none)";
            if (n.body == NgBody::Picker) {
                Ink::NodeId cur = Ink::kNullNode;
                if (n.childSlot == -2) {
                    label = "(choose a layer)";
                } else {
                    const std::vector<Ink::CompInputOverride> mat = MaterializeCompInputs(doc, *layer);
                    if (n.childSlot >= 0 && n.childSlot < (int)mat.size())
                        cur = mat[(std::size_t)n.childSlot].node;
                    if (cur != Ink::kNullNode) {
                        const Ink::Node* t = doc.Find(cur);
                        label = t ? (t->name.empty() ? "(unnamed)" : t->name) : "(missing)";
                    } else {
                        label = "(choose a layer)";
                    }
                }
            } else if (n.body == NgBody::BlendMode) {
                static const char* kBlendNames[] = {
                    "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
                    "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
                    "Difference", "Exclusion", "Erase" };
                const int mode = std::clamp((int)layer->blend, 0, 12);
                label = kBlendNames[mode];
            }
            const float bodyPx = 12.0f * gs * (float)ngZoom_;
            NgDrawText(*view, *ink_, ImVec2(btl.x + 6.0f * gs * (float)ngZoom_, btl.y + (bbr.y - btl.y) * 0.5f + bodyPx * 0.32f),
                      label, bodyPx, TokCol(Tok::C_NodeBox_Text));
        }
    }

    // ── Apply the in-flight drag (drawn last so the ghost/marquee overlays
    // everything, mirroring the deleted widget's own ordering). ──
    if (ngDragKind_ == NgDragKind::MoveNode) {
        const int idx = findNode(ngDragNode_);
        if (idx >= 0) {
            const ImVec2 mouseCanvas = NgP2C(mousePx, ngPanX_, ngPanY_, ngZoom_);
            const ImVec2 newPos(mouseCanvas.x - ngDragGrabOffset_.x, mouseCanvas.y - ngDragGrabOffset_.y);
            ngNodePos_[ngDragNode_] = newPos;
        }
        if (idx < 0 || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (idx >= 0 && ngPendingPlacement_ && ngDragNode_ == kPendingAddKey) {
                ngPendingPos_ = ngNodePos_[ngDragNode_];
                ngPendingPlacement_ = false;
                ngPendingPlaced_ = true;
            } else if (idx >= 0 && canEdit && ngDragNode_ != activeId &&
                      std::find(inputKeys.begin(), inputKeys.end(), ngDragNode_) != inputKeys.end()) {
                std::vector<std::pair<float, std::uint64_t>> ordered;
                for (std::uint64_t k : inputKeys) {
                    const auto pit = ngNodePos_.find(k);
                    ordered.push_back({ pit != ngNodePos_.end() ? pit->second.y : 0.0f, k });
                }
                std::sort(ordered.begin(), ordered.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
                const std::vector<Ink::CompInputOverride> current = MaterializeCompInputs(doc, *layer);
                std::vector<Ink::CompInputOverride> newList;
                newList.reserve(ordered.size());
                for (const auto& entry : ordered) {
                    auto found = std::find_if(current.begin(), current.end(),
                                              [&](const Ink::CompInputOverride& ov) { return ov.node == entry.second; });
                    if (found != current.end()) newList.push_back(*found);
                }
                Action_SetCompInputs(activeId, std::move(newList));
            }
            ngDragKind_ = NgDragKind::None;
        }
    } else if (ngDragKind_ == NgDragKind::ConnectCable) {
        const int srcIdx = findNode(ngDragNode_);
        if (srcIdx < 0) {
            ngDragKind_ = NgDragKind::None;
        } else {
            const ImVec2 aP = NgC2P(NgPortPosCanvas(nodes[(std::size_t)srcIdx], ngDragPort_, gs), ngPanX_, ngPanY_, ngZoom_);
            ImVec2 bP = mousePx;
            bool dropValid = false;
            if (hovNode >= 0 && hovNode != srcIdx && hovPort >= 0 &&
                !nodes[(std::size_t)hovNode].ports[(std::size_t)hovPort].isOut) {
                bP = NgC2P(NgPortPosCanvas(nodes[(std::size_t)hovNode], hovPort, gs), ngPanX_, ngPanY_, ngZoom_);
                dropValid = true;
            }
            const float dx = std::max(30.0f * gs, std::fabs(bP.x - aP.x) * 0.5f);
            const Ink::Color col = TokCol(dropValid ? Tok::C_NodeCable_ColorActive : Tok::C_NodeCable_ColorIncompatible);
            NgDrawCable(*view, aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y), bP, col,
                       TokFlt(Tok::C_NodeCable_Width) * gs * 1.2f);
            view->Overlay().AddCircleFilled({ bP.x, bP.y }, portR, col);

            const bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (cancel) {
                ngDragKind_ = NgDragKind::None;
            } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                const std::uint64_t fromNode = ngDragNode_;
                const int fromPort = ngDragPort_;
                if (dropValid && canEdit) {
                    const std::uint64_t toNode = nodes[(std::size_t)hovNode].id;
                    const int toPort = hovPort;
                    if (toNode == kMergeKey && fromNode != activeId) {
                        const Ink::Node* moved = doc.Find(fromNode);
                        if (moved && moved->parent == activeId) {
                            std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
                            list.erase(std::remove_if(list.begin(), list.end(),
                                                      [&](const Ink::CompInputOverride& ov) { return ov.node == fromNode; }),
                                      list.end());
                            const int pos = std::clamp(toPort, 0, (int)list.size());
                            list.insert(list.begin() + pos, Ink::CompInputOverride{ fromNode });
                            Action_SetCompInputs(activeId, std::move(list));
                        }
                    }
                } else if (canEdit && fromNode != activeId &&
                          std::find(inputKeys.begin(), inputKeys.end(), fromNode) != inputKeys.end()) {
                    std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
                    list.erase(std::remove_if(list.begin(), list.end(),
                                              [&](const Ink::CompInputOverride& ov) { return ov.node == fromNode; }),
                              list.end());
                    Action_SetCompInputs(activeId, std::move(list));
                }
                ngDragKind_ = NgDragKind::None;
            }
        }
    } else if (ngDragKind_ == NgDragKind::BoxSelect) {
        const ImVec2 a = ngDragBoxStart_, b = mousePx;
        const ImVec2 boxMin(std::min(a.x, b.x), std::min(a.y, b.y));
        const ImVec2 boxMax(std::max(a.x, b.x), std::max(a.y, b.y));
        const float dragDist = std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y));
        if (dragDist > io.MouseDragThreshold) {
            Ink::Color fillC = TokCol(Tok::C_NodeBox_BorderSelected);
            fillC.a *= 0.15f; fillC.r *= 0.15f; fillC.g *= 0.15f; fillC.b *= 0.15f;
            view->Overlay().AddRectFilled({ boxMin.x, boxMin.y }, { boxMax.x, boxMax.y }, fillC);
            view->Overlay().AddRect({ boxMin.x, boxMin.y }, { boxMax.x, boxMax.y },
                                    TokCol(Tok::C_NodeBox_BorderSelected), 1.0f * gs);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!io.KeyCtrl) ngSelected_.clear();
            if (dragDist > io.MouseDragThreshold) {
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    if (nodes[i].id == kPendingAddKey) continue;
                    const ImVec2 tl = NgC2P(nodes[i].pos, ngPanX_, ngPanY_, ngZoom_);
                    const ImVec2 br = NgC2P(ImVec2(nodes[i].pos.x + nodes[i].size.x,
                                                   nodes[i].pos.y + nodes[i].size.y), ngPanX_, ngPanY_, ngZoom_);
                    const bool overlap = tl.x <= boxMax.x && br.x >= boxMin.x &&
                                        tl.y <= boxMax.y && br.y >= boxMin.y;
                    if (overlap) ngSelected_.insert(nodes[i].id);
                }
            }
            ngDragKind_ = NgDragKind::None;
        }
    }

    // ── Shift+A: arms the pending-placement flow (a real "Add" list, even
    // though "Layer Input" is the only user-placeable kind today). ──
    if (canEdit && ngAddMenuRequested_) {
        ngPopupKind_ = NgPopupKind::AddMenu;
        ngPopupPos_ = mousePx;
        ngAddMenuRequested_ = false;
    }

    // ── The one shared floating-list popup (Add menu / layer picker /
    // blend-mode selector) — docs/Ink/NODE_UI.md: no ImGui popup exists on
    // this canvas, so this is hand-rolled, drawn and hit-tested in PIXEL
    // space (chrome, not canvas content — it does not scale with zoom). ──
    if (ngPopupKind_ != NgPopupKind::None) {
        std::vector<std::string> items;
        std::vector<Ink::NodeId> itemIds;
        if (ngPopupKind_ == NgPopupKind::AddMenu) {
            items = { "Layer Input" };
        } else if (ngPopupKind_ == NgPopupKind::Picker) {
            NgListAllNodes(doc, ngPopupOwner_, itemIds, items);
        } else {
            items = { "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
                     "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
                     "Difference", "Exclusion", "Erase" };
        }
        const float rowH = 22.0f * gs, w = 200.0f * gs;
        const ImVec2 tl = ngPopupPos_;
        const ImVec2 br(tl.x + w, tl.y + rowH * (float)items.size());
        const bool insidePopup = mousePx.x >= tl.x && mousePx.x <= br.x &&
                                 mousePx.y >= tl.y && mousePx.y <= br.y;
        const int rowHit = insidePopup ? (int)((mousePx.y - tl.y) / rowH) : -1;
        const bool clickedNow = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (clickedNow) {
            if (rowHit >= 0 && rowHit < (int)items.size()) {
                if (ngPopupKind_ == NgPopupKind::AddMenu) {
                    ngPendingPlacement_ = true;
                    ngPendingPlaced_ = false;
                } else if (ngPopupKind_ == NgPopupKind::Picker && canEdit) {
                    const Ink::NodeId picked = itemIds[(std::size_t)rowHit];
                    std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
                    if (ngPopupChildSlot_ == -2) {
                        const bool already = std::any_of(list.begin(), list.end(),
                            [&](const Ink::CompInputOverride& ov) { return ov.node == picked; });
                        if (!already) list.push_back({ picked });
                        ngPendingPlaced_ = false;
                    } else if (ngPopupChildSlot_ >= 0 && ngPopupChildSlot_ < (int)list.size()) {
                        list[(std::size_t)ngPopupChildSlot_].node = picked;
                    }
                    Action_SetCompInputs(ngPopupOwner_, std::move(list));
                } else if (ngPopupKind_ == NgPopupKind::BlendMode && canEdit) {
                    Action_SetBlendMode({ ngPopupOwner_ }, (Ink::BlendMode)rowHit);
                }
            }
            ngPopupKind_ = NgPopupKind::None;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ngPopupKind_ = NgPopupKind::None;
        }
        if (ngPopupKind_ != NgPopupKind::None) {
            view->Overlay().AddRectFilled({ tl.x, tl.y }, { br.x, br.y }, TokCol(Tok::C_NodeBox_HeaderBackground));
            view->Overlay().AddRect({ tl.x, tl.y }, { br.x, br.y }, TokCol(Tok::C_NodeBox_Border), 1.0f * gs);
            for (std::size_t i = 0; i < items.size(); ++i) {
                const ImVec2 rtl(tl.x, tl.y + (float)i * rowH), rbr(br.x, tl.y + (float)(i + 1) * rowH);
                if ((int)i == rowHit)
                    view->Overlay().AddRectFilled({ rtl.x, rtl.y }, { rbr.x, rbr.y },
                                                  TokCol(Tok::C_NodePort_BackgroundHover));
                const float rowPx = 13.0f * gs;
                NgDrawText(*view, *ink_, ImVec2(rtl.x + 8.0f * gs, rtl.y + rowH * 0.5f + rowPx * 0.32f),
                          items[i], rowPx, TokCol(Tok::C_NodeBox_Text));
            }
        }
    }

    // ── Blit + claim the layout rect ──
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)view->Texture(), canvasOrigin,
                                         ImVec2(canvasOrigin.x + avail.x, canvasOrigin.y + avail.y));
    ImGui::Dummy(avail);
}

} // namespace App

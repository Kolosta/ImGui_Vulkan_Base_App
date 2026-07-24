#include "Application.h"
#include "PropertiesRows.h"

#include <Ink/Scene/CompGraph.h>
#include <UI/Widgets/NodeGraph.h>
#include <VectorGraphics/IconManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <DesignSystem/DesignSystem.h>

#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  Node Graph Editor (docs/Ink/NODE_GRAPH.md §5/§7, docs/Ink/NODE_UI.md,
//  ROADMAP Lot 13) — the first concrete consumer of the generic UI::NodeGraph
//  widget. Named after the GRAPH, not "layer": the Layers view is only one
//  way of looking at the underlying Compositing Graph (the Outliner's), so
//  the editor that shows the graph itself must not be named after that one
//  view.
//
//  Follows `edit_.active` automatically, Blender-style (no manual "open"
//  step, same pattern as Properties.cpp). Reorder/exclude/mute editing only
//  commits for a Group (the only kind `Document::SetCompInputs`/
//  `SetCompBlendMuted` accept) — a non-Group active node still shows its
//  graph, read-only.
//
//  Node chain, mirroring `Ink::CompNodeKind` (docs/Ink/NODE_GRAPH.md §3/§7 —
//  each node is exactly ONE real operation, not a bundle):
//      Object (this node's own paint stack, Path/Instance only) + ordinary
//      child Inputs → Merge (present whenever there's more than just the
//      Object alone) → Clip/Mask (if any, fed by its own dedicated mask-
//      source Input) → Blend (if any) → Output. A plain leaf shape with
//      neither children nor clip/blend is just Object → Output directly —
//      never "Output connected to nothing" (the bug this round fixed).
//      Reordering/excluding an ordinary child Input works TWO ways: drag its
//      BOX up/down (re-sorted by Y on drop) OR drag its cable off Merge and
//      re-drop it on a different Merge port / into empty space (Blender-
//      style link dragging) — both funnel into the same
//      `Action_SetCompInputs` commit. The Object input is never reorderable/
//      excludable this way (it isn't a compInputs entry — it's unconditional
//      by construction), so those two gestures no-op on it rather than
//      corrupt anything. Lot 13 scope (docs/Ink/NODE_GRAPH.md §2/§7): this is
//      still all WITHIN one layer — cross-layer piece routing (a fill/stroke
//      feeding a FOREIGN layer) needs real per-edge Document storage, which
//      is the next lot (see NODE_GRAPH.md §7), not yet built.
//
//  The node RENDERING itself (boxes/ports/cables/labels) is still the
//  ImGui-based `UI::NodeGraph` widget — a from-scratch Vulkan-native
//  replacement is planned (docs/Ink/NODE_UI.md) but is large, separate
//  scope, not part of this file's own history.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
using DesignSystem::Tok;

// Merge/Blend/Clip-or-Mask/Output are singletons per graph; an Input (incl.
// the Object self-input and the Clip/Mask mask-source Input) is keyed by its
// target NodeId (globally unique, so one shared position/selection/collapse
// map is safe across every active object the editor ever follows — see
// Application.h). CompGraph is rebuilt fresh every compile (Ink::Scene owns
// it transiently), so CompNode::id (a small per-build counter) cannot anchor
// state across frames — this stable key can. NOTE: retargeting an ordinary
// Input (the body picker field) changes its key, since the key IS the
// target — position/collapse state for that slot resets after a retarget (a
// known, minor v1 quirk).
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

// The compInputs list a Node Graph edit will commit against: the CURRENT
// override list if the layer is already customized, else the auto list
// materialised from `children` (every entry starts unmuted, in tree order),
// EXCLUDING the clip/mask source — mirrors BuildAutoGraph's own exclusion of
// that child from ordinary Merge inputs (Ink::ComputeAutoMergeParams), so a
// picker's slot index always lines up with the graph's own ordinary-input
// order even for a Group that also clips its children.
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
    // Kept as a thin, correctly-named forward per the Merge/Clip/Mask/Blend
    // split (docs/Ink/NODE_GRAPH.md §7): Merge itself is never muted — mute
    // targets Blend now. See Action_SetCompBlendMuted.
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

    // Selection/collapse state is local to whichever object is being viewed —
    // clear the selection the moment the active object changes (Blender
    // resets the Shader Editor's node selection when the active material
    // changes too).
    if (activeId != ngLastActive_) {
        ngSelected_.clear();
        ngLastActive_ = activeId;
        ngPendingPlacement_ = false;
        ngPendingPlaced_ = false;
    }

    if (!layer) {
        ImGui::TextDisabled("No active object.");
        ImGui::TextDisabled("(select an object in the Viewport or Outliner)");
        return;
    }
    const bool canEdit = layer->kind == Ink::NodeKind::Group;
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    // ── Breadcrumb header: margins around IT specifically (the canvas below
    // stays edge-to-edge — d.contentInset = false at registration). Root/
    // Document itself is implicit (never shown); beyond 4 ancestors, only the
    // first-after-root and the 3 closest parents are kept, "..." in between.
    ImGui::Dummy(ImVec2(1.0f, 4.0f * gs));
    ImGui::Indent(8.0f * gs);
    {
        std::vector<std::pair<Ink::NodeId, std::string>> chain;   // root-most first, excludes `layer`
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
            if (last) return;
            ImGui::SameLine(0.0f, 4.0f * gs);
            auto& im = VectorGraphics::IconManager::Instance();
            const float isz = ImGui::GetTextLineHeight();
            auto md = im.GetDefaultMetadata("chevron-right");
            if (!md.colorZones.empty())
                md.colorZones[0].customColor = ds.GetColor(Tok::S_Color_Text_Subtle);
            const ImVec2 pScr = ImGui::GetCursorScreenPos();
            im.RenderIcon(ImGui::GetWindowDrawList(), "chevron-right", pScr, isz, md);
            ImGui::Dummy(ImVec2(isz, isz));
            ImGui::SameLine(0.0f, 4.0f * gs);
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
    }
    ImGui::Unindent(8.0f * gs);
    if (canEdit && !layer->compInputs.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(customized)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to Automatic"))
            Action_ResetCompInputs(activeId);
    }
    ImGui::Dummy(ImVec2(1.0f, 4.0f * gs));
    ImGui::Separator();

    const Ink::CompGraph graph = Ink::BuildAutoGraph(doc, *layer);

    // ── Build the widget's node/edge lists ──
    std::vector<UI::NodeGraphNode> uiNodes;
    std::vector<UI::NodeGraphEdge> uiEdges;
    std::vector<std::uint64_t> inputKeys;   // ordinary Input keys (Object + children), in build order

    // `posSlot` numbers EVERY ordinary Input (Object included) purely for
    // canvas Y-stacking of new nodes; `childSlot` numbers only the REAL
    // children (excluding the Object self-input and the mask source) and is
    // what indexes into MaterializeCompInputs — the two diverge whenever an
    // Object input exists (always, for a non-Group layer), which is why they
    // are tracked separately rather than reusing one counter for both.
    auto addOrdinaryInput = [&](const Ink::CompNode& n, int posSlot, int childSlot) {
        const std::uint64_t key = NgKey(n);
        UI::NodeGraphNode un;
        un.id = key;
        un.title = n.isObjectInput ? "Object" : "Layer Input";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderInput);
        un.selected  = ngSelected_.count(key) > 0;
        un.muted     = n.muted;
        un.collapsed = ngCollapsed_.count(key) > 0;

        UI::NodeGraphPort outp;
        // No label: which layer this feeds is already obvious from the cable
        // landing opposite it (product-owner feedback).
        outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(outp);

        // Live preview (docs/Ink/NODE_UI.md task #3): ABOVE the header, not
        // in the body, and always reserved as a square exactly as wide as
        // the node (UI::NodeGraphNode::topDraw's own contract) so it scales
        // WITH the node under zoom instead of a fixed pixel size. Shown for
        // every ordinary input regardless of `canEdit` — a leaf Path's
        // read-only graph is just as worth previewing as a Group's.
        if (ngShowPreviews_) {
            const Ink::NodeId previewTarget = n.target.node;
            un.topDraw = [this, previewTarget](ImVec2 topMin, ImVec2 topMax) {
                if (previewTarget == Ink::kNullNode) return;
                const float dispW = topMax.x - topMin.x;
                if (dispW < 1.0f) return;
                const int fetchPx = (int)std::clamp(dispW, 16.0f, 256.0f);
                if (auto tex = NodePreviewTexture((std::uint64_t)previewTarget, fetchPx))
                    ImGui::GetWindowDrawList()->AddImage((ImTextureID)tex, topMin, topMax);
            };
        }

        if (canEdit && !n.isObjectInput) {
            un.bodyHeight = 26.0f * gs;
            un.bodyDraw = [this, activeId, childSlot](ImVec2 bMin, ImVec2 bMax) {
                if (!project_.document) return;
                if (ngZoom_ < 0.55) {
                    ImGui::SetCursorScreenPos(bMin);
                    ImGui::TextDisabled("(zoom in to edit)");
                    return;
                }
                Ink::Document& d = *project_.document;
                const Ink::Node* l = d.Find(activeId);
                if (!l) return;
                const std::vector<Ink::CompInputOverride> mat = MaterializeCompInputs(d, *l);
                Ink::NodeId cur = (childSlot >= 0 && childSlot < (int)mat.size())
                                 ? mat[(std::size_t)childSlot].node : Ink::kNullNode;
                auto commit = [this, activeId, childSlot](Ink::NodeId picked) {
                    if (!project_.document) return;
                    const Ink::Node* ll = project_.document->Find(activeId);
                    if (!ll) return;
                    std::vector<Ink::CompInputOverride> list =
                        MaterializeCompInputs(*project_.document, *ll);
                    if (childSlot < 0 || childSlot >= (int)list.size()) return;
                    if (list[(std::size_t)childSlot].node == picked) return;
                    list[(std::size_t)childSlot].node = picked;
                    Action_SetCompInputs(activeId, std::move(list));
                };
                // A real ImGui row (pr::NodePickerRow) is built for a NORMAL
                // properties panel: it sizes itself off GetContentRegionAvail()
                // of the CURRENT WINDOW, which — called directly on the canvas
                // — is the whole Node Graph editor's width, not this node's
                // tiny body rect. A borderless child window keyed uniquely
                // per slot fixes that AND the "conflicting ID" (NodePickerRow's
                // own ImGui::PushID(label) collided across every Input node
                // sharing the same literal label) — the child's own id scopes
                // everything inside it.
                char childId[24];
                std::snprintf(childId, sizeof childId, "##ngInBody%d", childSlot);
                ImGui::SetCursorScreenPos(bMin);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                if (ImGui::BeginChild(childId, ImVec2(bMax.x - bMin.x, bMax.y - bMin.y),
                                      false,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                    Ink::NodeId target = cur;
                    bool pickReq = false;
                    if (pr::NodePickerRow("", d, &target, activeId,
                                          /*allowNone=*/false, /*pathsOnly=*/false, &pickReq) &&
                        target != cur)
                        commit(target);
                    if (pickReq) BeginObjectPick(nullptr, commit);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            };
        }

        auto it = ngNodePos_.find(key);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(0.0f, (float)posSlot * 140.0f * gs); ngNodePos_[key] = un.pos; }

        inputKeys.push_back(key);
        uiNodes.push_back(std::move(un));
    };

    int posSlot = 0;
    int childSlot = 0;
    for (const Ink::CompNode& n : graph.nodes) {
        if (n.kind == Ink::CompNodeKind::Input && !n.isMaskSourceInput) {
            addOrdinaryInput(n, posSlot++, n.isObjectInput ? -1 : childSlot);
            if (!n.isObjectInput) ++childSlot;
        }
    }

    // The Clip/Mask mask-source Input, if any — a plain read-only reference
    // node for v1 (retargeting it isn't wired to a picker; the mask/clip
    // source is normally set via "Clip to First Child" / the Affinity mask
    // flag, not this editor).
    std::uint64_t maskSourceKey = 0;
    bool hasMaskSourceInput = false;
    for (const Ink::CompNode& n : graph.nodes) {
        if (n.kind != Ink::CompNodeKind::Input || !n.isMaskSourceInput) continue;
        UI::NodeGraphNode un;
        un.id = NgKey(n);
        const Ink::Node* t = doc.Find(n.target.node);
        un.title = t ? (t->name.empty() ? "(unnamed)" : t->name) : "(missing)";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderInput);
        un.selected  = ngSelected_.count(un.id) > 0;
        un.collapsed = ngCollapsed_.count(un.id) > 0;
        UI::NodeGraphPort outp;
        outp.label = "Mask"; outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(outp);
        if (ngShowPreviews_) {
            const Ink::NodeId src = n.target.node;
            un.topDraw = [this, src](ImVec2 topMin, ImVec2 topMax) {
                if (src == Ink::kNullNode) return;
                const float dispW = topMax.x - topMin.x;
                if (dispW < 1.0f) return;
                const int fetchPx = (int)std::clamp(dispW, 16.0f, 256.0f);
                if (auto tex = NodePreviewTexture((std::uint64_t)src, fetchPx))
                    ImGui::GetWindowDrawList()->AddImage((ImTextureID)tex, topMin, topMax);
            };
        }
        auto it = ngNodePos_.find(un.id);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(0.0f, -160.0f * gs); ngNodePos_[un.id] = un.pos; }
        maskSourceKey = un.id;
        hasMaskSourceInput = true;
        uiNodes.push_back(std::move(un));
    }

    const Ink::CompNode* merge = graph.FindMerge();
    std::uint64_t sinkKey = inputKeys.empty() ? kOutputKey : inputKeys.front();
    if (merge) {
        UI::NodeGraphNode un;
        un.id = kMergeKey;
        un.title = "Merge";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderMerge);
        un.selected  = ngSelected_.count(kMergeKey) > 0;
        un.collapsed = ngCollapsed_.count(kMergeKey) > 0;
        // One In port PER CURRENT ORDINARY INPUT (Object included — it is
        // always the first/bottom port) — each incoming cable lands at its
        // own row, in order. This needed ZERO widget changes: the per-
        // direction row-stacking already handles the vertical fan-out.
        for (std::size_t k = 0; k < inputKeys.size(); ++k) {
            UI::NodeGraphPort inp;
            inp.dir = UI::NodePortDir::In; inp.type = 0;
            inp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
            un.ports.push_back(inp);
        }
        UI::NodeGraphPort outp;
        outp.label = "Result"; outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(outp);

        auto it = ngNodePos_.find(kMergeKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(260.0f * gs, 0.0f); ngNodePos_[kMergeKey] = un.pos; }

        uiNodes.push_back(std::move(un));
        sinkKey = kMergeKey;
    }

    const Ink::CompNode* clipOrMask = graph.FindClipOrMask();
    if (clipOrMask) {
        const bool isMask = clipOrMask->kind == Ink::CompNodeKind::Mask;
        UI::NodeGraphNode un;
        un.id = kClipMaskKey;
        un.title = isMask ? "Mask" : "Clip";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderClipMask);
        un.selected  = ngSelected_.count(kClipMaskKey) > 0;
        un.collapsed = ngCollapsed_.count(kClipMaskKey) > 0;
        UI::NodeGraphPort contentIn;
        contentIn.label = "Content"; contentIn.dir = UI::NodePortDir::In; contentIn.type = 0;
        contentIn.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(contentIn);
        UI::NodeGraphPort maskIn;
        maskIn.label = "Mask"; maskIn.dir = UI::NodePortDir::In; maskIn.type = 0;
        maskIn.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(maskIn);
        UI::NodeGraphPort outp;
        outp.label = "Result"; outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(outp);

        auto it = ngNodePos_.find(kClipMaskKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(380.0f * gs, 0.0f); ngNodePos_[kClipMaskKey] = un.pos; }

        uiEdges.push_back({ sinkKey, merge ? (int)inputKeys.size() : 0, kClipMaskKey, 0 });
        if (hasMaskSourceInput)
            uiEdges.push_back({ maskSourceKey, 0, kClipMaskKey, 1 });
        uiNodes.push_back(std::move(un));
        sinkKey = kClipMaskKey;
    }

    const Ink::CompNode* blend = graph.FindBlend();
    if (blend) {
        UI::NodeGraphNode un;
        un.id = kBlendKey;
        un.title = "Blend";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderBlend);
        un.selected  = ngSelected_.count(kBlendKey) > 0;
        un.muted     = blend->muted;
        un.collapsed = ngCollapsed_.count(kBlendKey) > 0;
        UI::NodeGraphPort inp;
        inp.label = "Content"; inp.dir = UI::NodePortDir::In; inp.type = 0;
        inp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(inp);
        UI::NodeGraphPort outp;
        outp.label = "Result"; outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(outp);

        // In-node blend-mode control (docs/Ink/NODE_UI.md task #4) — the
        // SAME list PropCompositingSection uses, wired to the SAME action.
        un.bodyHeight = 26.0f * gs;
        un.bodyDraw = [this, activeId](ImVec2 bMin, ImVec2 bMax) {
            if (!project_.document || ngZoom_ < 0.55) {
                if (ngZoom_ < 0.55) {
                    ImGui::SetCursorScreenPos(bMin);
                    ImGui::TextDisabled("(zoom in to edit)");
                }
                return;
            }
            const Ink::Node* l = project_.document->Find(activeId);
            if (!l) return;
            static const char* kBlendNames[] = {
                "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
                "Color Dodge", "Color Burn", "Hard Light", "Soft Light",
                "Difference", "Exclusion", "Erase" };
            constexpr int kBlendCount = (int)(sizeof kBlendNames / sizeof kBlendNames[0]);
            int mode = std::clamp((int)l->blend, 0, kBlendCount - 1);
            ImGui::SetCursorScreenPos(bMin);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            if (ImGui::BeginChild("##ngBlendBody", ImVec2(bMax.x - bMin.x, bMax.y - bMin.y),
                                  false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                if (pr::DropdownRow("", kBlendNames, kBlendCount, &mode))
                    Action_SetBlendMode({ activeId }, (Ink::BlendMode)mode);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        auto it = ngNodePos_.find(kBlendKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(clipOrMask ? 500.0f * gs : 380.0f * gs, 0.0f); ngNodePos_[kBlendKey] = un.pos; }

        const int fromPort = (sinkKey == kMergeKey && merge) ? (int)inputKeys.size()
                            : (sinkKey == kClipMaskKey ? 2 : 0);
        uiEdges.push_back({ sinkKey, fromPort, kBlendKey, 0 });
        uiNodes.push_back(std::move(un));
        sinkKey = kBlendKey;
    }

    {
        UI::NodeGraphNode un;
        un.id = kOutputKey;
        un.title = "Output";
        un.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderOutput);
        un.selected  = ngSelected_.count(kOutputKey) > 0;
        un.collapsed = ngCollapsed_.count(kOutputKey) > 0;
        UI::NodeGraphPort inp;
        inp.label = "Layer"; inp.dir = UI::NodePortDir::In; inp.type = 0;
        inp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        un.ports.push_back(inp);

        auto it = ngNodePos_.find(kOutputKey);
        if (it != ngNodePos_.end()) un.pos = it->second;
        else { un.pos = ImVec2(660.0f * gs, 0.0f); ngNodePos_[kOutputKey] = un.pos; }

        const int fromPort =
            sinkKey == kMergeKey && merge ? (int)inputKeys.size()
          : sinkKey == kClipMaskKey       ? 2
          : sinkKey == kBlendKey          ? 1
          : 0;
        uiEdges.push_back({ sinkKey, fromPort, kOutputKey, 0 });
        uiNodes.push_back(std::move(un));
    }

    // Ordinary Inputs (Object + children) → Merge (or, absent a Merge,
    // straight to whatever the sink already is — a single-Object leaf skips
    // Merge entirely, per sinkKey's own initial value above).
    for (std::size_t k = 0; k < inputKeys.size(); ++k)
        if (merge)
            uiEdges.push_back({ inputKeys[k], 0, kMergeKey, (int)k });

    // ── Pending "Layer Input" placement (Shift+A → picked from the Add menu
    // → follows the mouse until a click drops it; the node's own picker then
    // starts EMPTY — Blender's exact add-node flow, docs/Ink/NODE_UI.md
    // task #2). Nothing commits to the Document until a real target is
    // picked in the second phase. ──
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    auto screenToCanvas = [&](ImVec2 s) {
        return ImVec2((float)((s.x - canvasOrigin.x) / ngZoom_ + ngPanX_),
                      (float)((s.y - canvasOrigin.y) / ngZoom_ + ngPanY_));
    };
    if (canEdit && ngPendingPlacement_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ngPendingPlacement_ = false;
    if (canEdit && ngPendingPlaced_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ngPendingPlaced_ = false;
    if (canEdit && ngPendingPlacement_) {
        UI::NodeGraphNode ghost;
        ghost.id = kPendingAddKey;
        ghost.title = "Layer Input";
        ghost.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderInput);
        UI::NodeGraphPort outp;
        outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        ghost.ports.push_back(outp);
        ghost.pos = screenToCanvas(ImGui::GetIO().MousePos);
        uiNodes.push_back(std::move(ghost));
    } else if (canEdit && ngPendingPlaced_) {
        UI::NodeGraphNode ghost;
        ghost.id = kPendingAddKey;
        ghost.title = "Layer Input";
        ghost.headerColor = ds.GetColor(Tok::C_NodeBox_HeaderInput);
        UI::NodeGraphPort outp;
        outp.dir = UI::NodePortDir::Out; outp.type = 0;
        outp.color = ds.GetColor(Tok::C_NodeCable_TypeRenderOutput);
        ghost.ports.push_back(outp);
        ghost.pos = ngPendingPos_;
        ghost.bodyHeight = 26.0f * gs;
        ghost.bodyDraw = [this, activeId](ImVec2 bMin, ImVec2 bMax) {
            if (!project_.document) return;
            auto commit = [this, activeId](Ink::NodeId picked) {
                if (picked == Ink::kNullNode || !project_.document) return;
                const Ink::Node* l = project_.document->Find(activeId);
                if (!l) return;
                std::vector<Ink::CompInputOverride> list =
                    MaterializeCompInputs(*project_.document, *l);
                const bool already = std::any_of(list.begin(), list.end(),
                    [&](const Ink::CompInputOverride& ov) { return ov.node == picked; });
                if (!already) { list.push_back({ picked }); Action_SetCompInputs(activeId, std::move(list)); }
                ngPendingPlaced_ = false;
            };
            ImGui::SetCursorScreenPos(bMin);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            if (ImGui::BeginChild("##ngPendingBody", ImVec2(bMax.x - bMin.x, bMax.y - bMin.y),
                                  false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                Ink::NodeId target = Ink::kNullNode;
                bool pickReq = false;
                if (pr::NodePickerRow("", *project_.document, &target, activeId,
                                      /*allowNone=*/true, /*pathsOnly=*/false, &pickReq) &&
                    target != Ink::kNullNode)
                    commit(target);
                if (pickReq) BeginObjectPick(nullptr, commit);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };
        uiNodes.push_back(std::move(ghost));
    }

    UI::NodeGraphConfig cfg;
    cfg.id = "##nodeGraph";
    cfg.nodes = &uiNodes;
    cfg.edges = &uiEdges;
    cfg.panX = &ngPanX_; cfg.panY = &ngPanY_; cfg.zoom = &ngZoom_;
    const UI::NodeGraphResult r = UI::DrawNodeGraph(cfg);

    // The placement click lands on the ghost's own header (it is glued to
    // the cursor every frame while pending, so the press always hits it) —
    // reusing the widget's EXISTING MoveNode press/release gesture as the
    // "drop here" signal needs no widget changes at all.
    if (ngPendingPlacement_ && r.nodeMoveEnded && r.nodeMoveEndedId == kPendingAddKey) {
        for (const UI::NodeGraphNode& un : uiNodes)
            if (un.id == kPendingAddKey) { ngPendingPos_ = un.pos; break; }
        ngPendingPlacement_ = false;
        ngPendingPlaced_ = true;
    }

    // Persist a move EVERY frame it happens (not only when it ends) — this
    // is what makes the drag actually follow the cursor instead of
    // snapping back to the last-committed position (uiNodes is rebuilt from
    // ngNodePos_ fresh every frame).
    if (r.nodeMoved && r.movedNode != kPendingAddKey) {
        for (const UI::NodeGraphNode& un : uiNodes)
            if (un.id == r.movedNode) { ngNodePos_[un.id] = un.pos; break; }
    }
    if (r.selectionChanged) {
        ngSelected_.clear();
        for (std::uint64_t k : r.selectedNodes) ngSelected_.insert(k);
    }

    // Reorder by dragging an Input's BOX up/down (Y-sort on release). The
    // Object input can be dragged around visually like any node (position is
    // still persisted above) but never re-enters this commit — it isn't a
    // compInputs entry, so a resulting no-match is silently skipped anyway;
    // the explicit guard just avoids a pointless identical-order commit.
    if (r.nodeMoveEnded && canEdit && r.nodeMoveEndedId != activeId &&
        std::find(inputKeys.begin(), inputKeys.end(), r.nodeMoveEndedId) != inputKeys.end()) {
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
            const std::uint64_t id = entry.second;
            auto found = std::find_if(current.begin(), current.end(),
                                      [&](const Ink::CompInputOverride& ov) { return ov.node == id; });
            if (found != current.end()) newList.push_back(*found);
        }
        Action_SetCompInputs(activeId, std::move(newList));
    }

    // Reorder/attach by dragging an Input's CABLE onto a different Merge
    // slot (Blender-style link dragging) — drop a valid connection to
    // Merge, and the source (an ordinary child input) takes that slot's
    // position among the OTHER current merge inputs. The Object input is
    // excluded (its own id equals `activeId`, never a real child) — dragging
    // its cable onto Merge has no meaningful destination in this data model
    // yet (it isn't a compInputs entry), so it is deliberately a no-op.
    if (canEdit && r.connected && r.newEdge.toNode == kMergeKey &&
        r.newEdge.fromNode != activeId) {
        const Ink::Node* moved = doc.Find(r.newEdge.fromNode);
        if (moved && moved->parent == activeId) {
            std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const Ink::CompInputOverride& ov) {
                                          return ov.node == r.newEdge.fromNode;
                                      }),
                      list.end());
            const int pos = std::clamp(r.newEdge.toPort, 0, (int)list.size());
            list.insert(list.begin() + pos, Ink::CompInputOverride{ r.newEdge.fromNode });
            Action_SetCompInputs(activeId, std::move(list));
        }
    }
    // Dropping an Input's cable in empty space (or on an incompatible port)
    // deletes it — Blender: dragging a link off into the void removes it.
    // For an ordinary child Input's sole outgoing edge, that means excluding
    // it (the Object input is excluded from this too, same reason as above).
    if (canEdit && r.cableDragEnded && !r.connected && r.cableDragSourceNode != activeId &&
        std::find(inputKeys.begin(), inputKeys.end(), r.cableDragSourceNode) != inputKeys.end()) {
        std::vector<Ink::CompInputOverride> list = MaterializeCompInputs(doc, *layer);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const Ink::CompInputOverride& ov) {
                                      return ov.node == r.cableDragSourceNode;
                                  }),
                  list.end());
        Action_SetCompInputs(activeId, std::move(list));
    }

    // Shift+A: a real Add menu (Blender-style — a list of node types, even
    // though "Layer Input" is the only user-placeable kind today; the auto-
    // managed kinds — Object/Merge/Clip/Mask/Blend/Output — are never in
    // this list, they are never manually added). Picking it arms the
    // pending-placement ghost above instead of adding anything immediately.
    if (ngAddMenuRequested_) {
        ImGui::OpenPopup("##ngAddMenu");
        ngAddMenuRequested_ = false;
    }
    if (canEdit && ImGui::BeginPopup("##ngAddMenu")) {
        ImGui::TextDisabled("Add");
        ImGui::Separator();
        if (ImGui::Selectable("Layer Input")) {
            ngPendingPlacement_ = true;
            ngPendingPlaced_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace App

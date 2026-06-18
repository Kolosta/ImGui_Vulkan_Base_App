#include "Application.h"
#include "ProjectFile.h"
#include "ModuleRegistry.h"   // restore a file's module on Load
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Text/FontManager.h>

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif
#include <imgui_impl_sdl3.h>

namespace App {

void Application::Action_Zone1()    { std::cout << "[ACTION] Zone 1"    << std::endl; }
void Application::Action_Zone2()    { std::cout << "[ACTION] Zone 2"    << std::endl; }
void Application::Action_ThemePreviewCycle() {
    std::cout << "[ACTION] Theme Preview Cycle" << std::endl;
}

void Application::Action_Quit() {
    std::cout << "[ACTION] Quit" << std::endl;
    running_ = false;
}
void Application::Action_ToggleSettings() {
    // If Settings is already open but sits BEHIND (not keyboard-focused), the
    // shortcut should bring it to the front + focus rather than close it — only
    // toggle when it is closed or already the active window. The SDL raise is
    // deferred to ProcessEvents (outside the ImGui frame) via the focus request.
    if (showSettings_ && settingsHost_.IsOpen() && !settingsHost_.HasInputFocus()) {
        settingsHost_.RequestFocus();
        return;
    }
    // Only flip the desired state here (this runs inside the main ImGui frame).
    // The actual Show()/Hide() — which creates the OS window and fires SDL
    // window events that re-enter RenderFrame via the event watch — is deferred
    // to ProcessEvents(), OUTSIDE the NewFrame/Render span.
    showSettings_ = !showSettings_;
}
void Application::Action_ToggleImGuiDemo() {
    showImGuiDemo_ = !showImGuiDemo_;
}
void Application::Action_ActivateNamedTool(const std::string& toolId) {
    // Switching tools cancels any half-finished gesture so the new tool starts
    // clean (e.g. abandoning a polyline mid-placement).
    toolState_.Reset();
    // Extrude is an Edit-Mode tool: it authors geometry into the active object, so
    // it enters Edit Mode even with no selection (unlike Tab, which needs a
    // selected obj). Curve works in BOTH modes now, so it stays in the current one.
    const EditorMode prevMode = editorMode_;
    if (toolId == "tool.extrude" && editorMode_ != EditorMode::Edit) {
        // Save the Object-mode tool before forcing Edit (so leaving Edit restores it).
        objectModeTool_ = Shortcuts::Tools::ToolManager::Instance().GetActiveTool();
        editorMode_ = EditorMode::Edit;
        project_.document.ClearVertSelection();
        editDrag_.Reset();
    }
    // Mark selection only makes sense under the Line-Mark tool; leaving it clears
    // the selection (and any in-progress mark op) so other tools' Properties show.
    if (toolId != "tool.linemark") {
        project_.document.ClearMarkSelection();
        markGrab_.Reset(); markBox_ = {}; markDrag_ = {};
    }
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool(toolId);
    // Record this as the chosen tool for the CURRENT mode so a later mode switch
    // restores it (Object Mode → objectModeTool_; Edit Mode → per active object).
    if (editorMode_ == EditorMode::Object) {
        objectModeTool_ = toolId;
    } else {
        if (uint64_t id = EditToolObject()) editToolByObject_[id] = toolId;
    }
    (void)prevMode;
}
void Application::Action_NewDocument() {
    // Open the New Artboard popup in the Viewport leaf the mouse is over
    // (no-op if none). The popup adds an artboard to the shared project.
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->openNewDoc = true;
}
void Application::Action_NewProject() {
    // Reset to a brand-new empty project (no artboard). Shared by every
    // Viewport and the Outliner. File save/open comes later.
    project_.Reset();
    objectModeTool_ = "tool.select"; editToolByObject_.clear();  // fresh tool memory
    ResetUndoHistory();
    std::cout << "[ACTION] New Project" << std::endl;
}
void Application::Action_ViewFitDocument() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqFitDoc = true;
}
void Application::Action_ViewFitSelection() {
    // Numpad . — frame the selected/active object(s) in the hovered viewport, OR,
    // if the Outliner is hovered, scroll/recentre it on the active object.
    if (EditorState* st = zoneLayout_.HoveredEditorState()) {
        if (project_.document.HasSelection() || project_.document.ActiveId())
            st->reqFitSelection = true;
        st->outliner.reqScrollToActive = true;   // Outliner leaves act on this
    }
}
void Application::Action_ViewResetOrigin() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqResetOrigin = true;
}
uint64_t Application::EditToolObject() const {
    uint64_t id = project_.document.ActiveId();
    if (!id && !project_.document.Selection().empty())
        id = project_.document.Selection().front();
    return id;
}

// After a mode switch, save the tool of the mode we LEFT (`prevMode`) into its
// per-mode memory, then restore the tool the NEW mode should show. Object Mode has
// one remembered tool; Edit Mode remembers per edited object (defaulting to Select).
void Application::SyncToolForMode(EditorMode prevMode) {
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    const std::string cur = tm.GetActiveTool();
    if (prevMode == editorMode_) return;     // nothing changed

    if (prevMode == EditorMode::Object) {
        objectModeTool_ = cur;               // remember what Object Mode was using
    } else {
        if (uint64_t id = EditToolObject()) editToolByObject_[id] = cur;
    }

    std::string want;
    if (editorMode_ == EditorMode::Object) {
        want = objectModeTool_.empty() ? "tool.select" : objectModeTool_;
    } else {
        uint64_t id = EditToolObject();
        auto it = id ? editToolByObject_.find(id) : editToolByObject_.end();
        want = (it != editToolByObject_.end()) ? it->second : "tool.select";
    }
    // Apply directly (NOT Action_ActivateNamedTool — that would re-enter the mode
    // logic and re-save/clobber; here we only set the ToolManager + clear gestures).
    if (!want.empty() && want != cur) {
        toolState_.Reset();
        if (want != "tool.linemark") {
            project_.document.ClearMarkSelection();
            markGrab_.Reset(); markBox_ = {}; markDrag_ = {};
        }
        tm.SetActiveTool(want);
    }
}

void Application::Action_ToggleEditMode() {
    // Tab toggles Object ⇄ Edit. Entering Edit needs at least one selected
    // object; bake its parametric parts so vertices show immediately.
    const EditorMode prevMode = editorMode_;
    if (editorMode_ == EditorMode::Object) {
        if (project_.document.HasSelection()) {
            editorMode_ = EditorMode::Edit;
            for (uint64_t sid : project_.document.Selection())
                if (Renderer::Shape* s = project_.document.FindShape(sid)) s->EnsurePath();
            project_.document.ClearVertSelection();
        }
    } else {
        editorMode_ = EditorMode::Object;
    }
    SyncToolForMode(prevMode);   // save/restore the per-mode tool
    toolState_.Reset();
    editDrag_.Reset();
    // The editor mode is in the undo snapshot → make the toggle its own step.
    MarkUndoLabel(editorMode_ == EditorMode::Edit ? "Enter Edit Mode"
                                                  : "Enter Object Mode");
    LogInfoActionRich(editorMode_ == EditorMode::Edit ? "Enter Edit Mode"
                                                      : "Enter Object Mode",
                  std::string("mode=") +
                  (editorMode_ == EditorMode::Edit ? "EDIT" : "OBJECT"));
}

void Application::Action_DeleteSelection() {
    // During a modal G/R/S, X/Y are the AXIS CONSTRAINT (handled in
    // UpdateTransformOp), not Delete — so X must not also delete the selection.
    if (transformOp_.Active()) return;
    // Line-mark tool with marks selected → delete those marks (quasi-objects).
    if (Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.linemark" &&
        project_.document.HasMarkSelection()) {
        DeleteSelectedMarks();
        return;
    }
    // X deletes the right thing for the current mode: whole objects in Object
    // mode, selected vertices/edges/faces in Edit mode.
    if (editorMode_ == EditorMode::Edit) { Action_DeleteElements(); return; }
    auto ids = project_.document.Selection();   // copy: EraseShape mutates it
    if (ids.empty()) return;
    MarkUndoLabel("Delete");
    for (uint64_t id : ids) project_.document.EraseShape(id);
    project_.document.ClearSelection();
    project_.dirty = true;
}

void Application::Action_HideSelection() {
    // H (Object mode): hide the selected objects. Hidden objects don't render and
    // aren't pickable IN THE VIEWPORT, but stay SELECTABLE/active from the
    // Outliner (Blender-style) — so the selection (and the active element) is
    // kept. Transforms skip hidden objects; the Outliner shows a closed eye.
    if (editorMode_ != EditorMode::Object) return;
    auto ids = project_.document.Selection();
    if (ids.empty()) return;
    MarkUndoLabel("Hide");
    for (uint64_t id : ids)
        if (Renderer::Shape* s = project_.document.FindShape(id)) s->visible = false;
    project_.dirty = true;
}

void Application::Action_RevealAll() {
    // Alt+H: reveal every hidden object in the document (Blender-style).
    MarkUndoLabel("Reveal Hidden");
    project_.document.RevealAllShapes();
    project_.dirty = true;
}

void Application::Action_ParentSelection() {
    // Ctrl+P: parent every OTHER selected object to the ACTIVE one. The active
    // object is the parent; the others become its children (cycles refused by
    // Document::SetParent). Objects stay visually put — parenting only affects
    // FUTURE motion. Needs ≥2 selected and an active object.
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    uint64_t parent = doc.ActiveId();
    if (!parent || doc.Selection().size() < 2) return;
    bool any = false;
    for (uint64_t id : doc.Selection()) {
        if (id == parent) continue;
        if (doc.SetParent(id, parent)) any = true;
    }
    if (any) { MarkUndoLabel("Parent"); project_.dirty = true; }
}

void Application::Action_ClearParent() {
    // Alt+P: clear the parent of every selected object (keeps them visually put;
    // they simply stop following their former parent).
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    bool any = false;
    for (uint64_t id : doc.Selection())
        if (doc.ClearParent(id)) any = true;
    if (any) { MarkUndoLabel("Clear Parent"); project_.dirty = true; }
}

// ── Selection families ────────────────────────────────────────────────────────
namespace {
// Approximate colour equality (per channel) for "Select Color".
bool ColorNear(const Renderer::Color& a, const Renderer::Color& b) {
    const float e = 0.02f;
    return std::fabs(a.r - b.r) < e && std::fabs(a.g - b.g) < e &&
           std::fabs(a.b - b.b) < e && std::fabs(a.a - b.a) < e;
}
}

void Application::Action_SelectGrouped(GroupedMode mode) {
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    Renderer::Shape* act = doc.FindShape(doc.ActiveId());
    if (!act) return;
    const uint64_t activeId = act->id;

    // Snapshot the selection BEFORE applying, so the operator panel can re-run with
    // a different mode without compounding (restore base → re-apply). Captured once
    // per fresh invocation (a panel re-run reuses the same base via the closure).
    std::vector<uint64_t> base(doc.Selection().begin(), doc.Selection().end());

    // Visit every object in the document.
    auto forEach = [&](const std::function<void(Renderer::Shape&)>& fn) {
        for (Renderer::Artboard& ab : doc.artboards)
            for (Renderer::Shape& s : ab.shapes) fn(s);
        for (Renderer::Shape& s : doc.looseShapes) fn(s);
    };
    std::vector<uint64_t> add;
    auto pick = [&](uint64_t id){ if (id) add.push_back(id); };

    switch (mode) {
        case GroupedMode::Children:
            for (uint64_t d : doc.DescendantsOf(activeId)) pick(d);
            break;
        case GroupedMode::ImmediateChildren:
            for (uint64_t c : doc.ChildrenOf(activeId)) pick(c);
            break;
        case GroupedMode::Parent:
            pick(act->parentId);
            break;
        case GroupedMode::Siblings: {
            uint64_t par = act->parentId;     // 0 → root-level objects (no parent)
            forEach([&](Renderer::Shape& s){ if (s.parentId == par) pick(s.id); });
            break;
        }
        case GroupedMode::Type: {
            Renderer::PartType fam = act->Family();
            forEach([&](Renderer::Shape& s){ if (s.Family() == fam) pick(s.id); });
            break;
        }
        case GroupedMode::Collection: {
            uint64_t coll = act->collectionId;
            forEach([&](Renderer::Shape& s){ if (s.collectionId == coll) pick(s.id); });
            break;
        }
        case GroupedMode::Color: {
            if (act->Empty()) break;
            Renderer::Color fc = act->MainPart().fill.color;
            Renderer::Color sc = act->MainPart().stroke.color;
            forEach([&](Renderer::Shape& s){
                if (s.Empty()) return;
                if (ColorNear(s.MainPart().fill.color, fc) &&
                    ColorNear(s.MainPart().stroke.color, sc)) pick(s.id);
            });
            break;
        }
    }
    for (uint64_t id : add) if (Renderer::Shape* s = doc.FindShape(id); s && s->visible)
        doc.SelectAdd(id);
    doc.SetActive(activeId);                 // keep the original active object
    MarkUndoLabel("Select Grouped");

    // Publish the operator to the redo panel with the mode as an adjustable enum,
    // so the user can switch Children/Parent/Type/Color… after the fact (Blender).
    OperatorRecord op;
    op.active = true;
    op.title  = "Select Grouped";
    OperatorParam p;
    p.label = "Type"; p.kind = OperatorParam::Kind::Enum; p.value = (int)mode;
    p.options = { "Children","Immediate Children","Parent","Siblings",
                  "Type","Collection","Color" };
    op.params.push_back(std::move(p));
    uint64_t keepActive = activeId;
    op.rerun = [this, base, keepActive]() {
        auto& d = project_.document;
        // Restore the base selection, then re-apply with the panel's current mode.
        d.ClearSelection();
        for (uint64_t id : base) d.SelectAdd(id);
        d.SetActive(keepActive);
        int mi = lastOperator_.params.empty() ? 0 : lastOperator_.params[0].value;
        Action_SelectGrouped((GroupedMode)mi);
    };
    SetLastOperator(std::move(op));
}

void Application::Action_SelectLinked() {
    // No shared data-blocks in this model → "linked" = same geometry family + kind
    // as the active object (closest analogue to Blender's Object Data link).
    if (editorMode_ != EditorMode::Object) return;
    auto& doc = project_.document;
    Renderer::Shape* act = doc.FindShape(doc.ActiveId());
    if (!act || act->Empty()) return;
    Renderer::PartType fam = act->Family();
    Renderer::ShapeKind kind = act->MainPart().kind;
    auto consider = [&](Renderer::Shape& s){
        if (s.Empty() || !s.visible) return;
        if (s.Family() == fam && s.MainPart().kind == kind) doc.SelectAdd(s.id);
    };
    for (Renderer::Artboard& ab : doc.artboards) for (Renderer::Shape& s : ab.shapes) consider(s);
    for (Renderer::Shape& s : doc.looseShapes) consider(s);
    doc.SetActive(act->id);
    MarkUndoLabel("Select Linked");
}

void Application::Action_SelectMoreLess(bool grow) {
    auto& doc = project_.document;
    if (editorMode_ == EditorMode::Edit) {
        // Edit mode is handled by the element-selection grow/shrink in EditMode.cpp.
        Action_SelectMoreLessElements(grow);
        return;
    }
    // Object mode: More = add immediate parents + children of the selection; Less =
    // deselect objects that sit at a parent/child boundary of the selection.
    std::vector<uint64_t> sel(doc.Selection().begin(), doc.Selection().end());
    if (sel.empty()) return;
    auto inSel = [&](uint64_t id){ return std::find(sel.begin(), sel.end(), id) != sel.end(); };
    if (grow) {
        std::vector<uint64_t> add;
        for (uint64_t id : sel) {
            Renderer::Shape* s = doc.FindShape(id);
            if (s && s->parentId) add.push_back(s->parentId);   // parent
            for (uint64_t c : doc.ChildrenOf(id)) add.push_back(c);  // children
        }
        for (uint64_t id : add)
            if (Renderer::Shape* s = doc.FindShape(id); s && s->visible) doc.SelectAdd(id);
    } else {
        // Deselect any selected object that has a neighbour (parent or child) NOT in
        // the selection — i.e. it sits on the boundary.
        std::vector<uint64_t> remove;
        for (uint64_t id : sel) {
            Renderer::Shape* s = doc.FindShape(id);
            bool boundary = false;
            if (s && s->parentId && !inSel(s->parentId)) boundary = true;
            for (uint64_t c : doc.ChildrenOf(id)) if (!inSel(c)) { boundary = true; break; }
            if (boundary) remove.push_back(id);
        }
        // Never shrink to nothing: keep at least the active object.
        if ((int)remove.size() >= (int)sel.size()) {
            uint64_t keep = doc.ActiveId() ? doc.ActiveId() : sel.front();
            remove.erase(std::remove(remove.begin(), remove.end(), keep), remove.end());
        }
        for (uint64_t id : remove) doc.Deselect(id);
    }
    MarkUndoLabel(grow ? "Select More" : "Select Less");
}

void Application::Action_JoinSelection() {
    // Merge all selected objects into the ACTIVE one (Blender's Ctrl+J): the
    // result is ONE object holding every source object's PARTS, each keeping its
    // own geometry, fill and stroke. So a rectangle, a line and an ellipse can
    // coexist (different colours) inside a single selectable item with one
    // origin. Absorbed parts are rebased into the host's local space so the
    // picture is unchanged.
    if (editorMode_ != EditorMode::Object) return;
    auto ids = project_.document.Selection();
    if (ids.size() < 2) return;

    // Typed Join (Lot 6): you may only merge objects of the SAME family
    // (Mesh↔Mesh, or any curve-like↔curve-like). Reject a mixed selection — the
    // context menu greys the entry and offers "Convert & Join"; this guards the
    // Ctrl+J shortcut path too. (Action_ConvertAllAndJoin pre-converts, so by
    // the time it calls us the selection is single-family.)
    {
        bool haveFamily = false; Renderer::PartType fam{};
        for (uint64_t id : ids) {
            Renderer::Shape* s = project_.document.FindShape(id);
            if (!s) continue;
            Renderer::PartType f = s->Family();
            if (!haveFamily) { fam = f; haveFamily = true; }
            else if (f != fam) {
                LogInfoAction("Join cancelled: selection mixes Mesh and Curve types");
                return;
            }
        }
    }

    MarkUndoLabel("Join");
    uint64_t hostId = project_.document.ActiveId();
    Renderer::Shape* host = project_.document.FindShape(hostId);
    if (!host) return;

    // Re-express a source part's nodes from its object's WORLD space into the
    // host's local space, baking parametric parts first so we can move them.
    auto rebasePart = [&](Renderer::Shape& src, Renderer::Part part,
                          Renderer::Shape& dstHost) {
        part.EnsurePath();
        auto toHostLocal = [&](Renderer::Vec2 p) {
            return Renderer::Tessellator::InverseTransform(
                dstHost, Renderer::Tessellator::WorldTransform(src, p));
        };
        for (Renderer::Node& n : part.path.nodes) {
            n.pos = toHostLocal(n.pos);
            if (n.hasIn)  n.hIn  = toHostLocal(n.hIn);
            if (n.hasOut) n.hOut = toHostLocal(n.hOut);
        }
        return part;
    };

    // Append every other selected object's parts to the host (each keeps its
    // own geometry + fill + stroke). One object, one origin, multiple parts.
    for (uint64_t id : ids) {
        if (id == hostId) continue;
        Renderer::Shape* o = project_.document.FindShape(id);
        if (!o) continue;
        for (const Renderer::Part& part : o->parts)
            host->parts.push_back(rebasePart(*o, part, *host));
    }
    for (uint64_t id : ids) if (id != hostId) project_.document.EraseShape(id);
    project_.document.SelectOnly(hostId);
    project_.dirty = true;
}

Renderer::Vec2 Application::ComputePivot() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    const auto& sel = doc.Selection();
    if (sel.empty()) return doc.cursor;

    // Pivots are in WORLD/display space. Each object's geometry is page-relative,
    // so we must offset it by the page's DISPLAY origin for THIS viewport
    // (CurPageOriginOfShape) — the same origin UpdateTransformOp uses — otherwise
    // the pivot lands as if every object were on page 1 at (0,0): wrong as soon
    // as a page is moved, not page 1, or under an auto layout. Objects on
    // different pages each contribute at their own display position, so a
    // multi-page selection's pivot is the true visual centre/median.
    auto originWorld = [&](uint64_t id) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(id);
        return s ? Renderer::Tessellator::WorldTransform(*s, s->origin,
                                                         CurPageOriginOfShape(id))
                 : Renderer::Vec2{0, 0};
    };
    switch (pivotMode_) {
        case PivotMode::Cursor2D:
            return doc.cursor;
        case PivotMode::ActiveElement:
            return originWorld(doc.ActiveId());
        case PivotMode::IndividualOrigins:
            // Per-object pivot is applied in UpdateTransformOp; the median is a
            // sensible scalar fallback here.
        case PivotMode::MedianPoint: {
            Renderer::Vec2 sum{0, 0}; int n = 0;
            for (uint64_t id : sel) { Renderer::Vec2 o = originWorld(id); sum.x += o.x; sum.y += o.y; ++n; }
            return n ? Renderer::Vec2{ sum.x / n, sum.y / n } : doc.cursor;
        }
        case PivotMode::BoundingBoxCenter: {
            Renderer::Vec2 mn{ 1e30f, 1e30f }, mx{ -1e30f, -1e30f };
            for (uint64_t id : sel)
                if (Renderer::Shape* s = doc.FindShape(id)) {
                    bool cl = false;
                    std::vector<Renderer::Vec2> poly =
                        Renderer::Tessellator::Outline(*s, 1.0f, cl, CurPageOriginOfShape(id));
                    for (auto& p : poly) {
                        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y);
                        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y);
                    }
                }
            return { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
        }
    }
    return doc.cursor;
}

// World-space pivot for the edit-mode element selection (VERTICES + selected
// HANDLES) under pivotMode_. With a handle-only selection the pivot follows the
// selected handle endpoints (not the 2D cursor).
Renderer::Vec2 Application::ComputeVertPivot() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    auto vw = [&](const Renderer::VertRef& v) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(v.shape);
        if (!s || v.part >= (int)s->parts.size()) return {0,0};
        auto& ns = s->parts[(size_t)v.part].path.nodes;
        if (v.node >= (int)ns.size()) return {0,0};
        return Renderer::Tessellator::WorldTransform(*s, ns[(size_t)v.node].pos,
                                                     CurPageOriginOfShape(v.shape));
    };
    auto hw = [&](const Renderer::HandleRef& h) -> Renderer::Vec2 {
        Renderer::Shape* s = doc.FindShape(h.shape);
        if (!s || h.part >= (int)s->parts.size()) return {0,0};
        auto& ns = s->parts[(size_t)h.part].path.nodes;
        if (h.node >= (int)ns.size()) return {0,0};
        const Renderer::Node& n = ns[(size_t)h.node];
        return Renderer::Tessellator::WorldTransform(*s, h.outSide ? n.hOut : n.hIn,
                                                     CurPageOriginOfShape(h.shape));
    };
    // The full element point set: vertex positions + selected handle endpoints whose
    // node isn't itself a selected vertex (those move with the node).
    std::vector<Renderer::Vec2> pts;
    for (const Renderer::VertRef& v : doc.VertSelection()) pts.push_back(vw(v));
    for (const Renderer::HandleRef& h : doc.HandleSelection())
        if (!doc.IsVertSelected({ h.shape, h.part, h.node })) pts.push_back(hw(h));
    if (pts.empty()) return doc.cursor;

    if (pivotMode_ == PivotMode::Cursor2D) return doc.cursor;
    if (pivotMode_ == PivotMode::ActiveElement) {
        // Prefer the active vertex; else the active handle; else the first point.
        if (doc.ActiveVert().shape) return vw(doc.ActiveVert());
        if (doc.ActiveHandle().valid()) return hw(doc.ActiveHandle());
        return pts.front();
    }
    if (pivotMode_ == PivotMode::BoundingBoxCenter) {
        Renderer::Vec2 mn{1e30f,1e30f}, mx{-1e30f,-1e30f};
        for (const Renderer::Vec2& w : pts) {
            mn.x=std::min(mn.x,w.x); mn.y=std::min(mn.y,w.y);
            mx.x=std::max(mx.x,w.x); mx.y=std::max(mx.y,w.y); }
        return { (mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f };
    }
    // Median (default) / IndividualOrigins → centroid of the element points.
    Renderer::Vec2 sum{0,0};
    for (const Renderer::Vec2& w : pts) { sum.x+=w.x; sum.y+=w.y; }
    return { sum.x/(float)pts.size(), sum.y/(float)pts.size() };
}

// Snapping applies when the magnet is on OR Ctrl is held, gated by the per-kind
// Affect toggle. Ctrl is the transient "snap just for this drag" Blender modifier.
bool Application::SnapActiveFor(TransformKind kind) const {
    const bool on = snap_.enabled || ImGui::GetIO().KeyCtrl;
    if (!on) return false;
    switch (kind) {
        case TransformKind::Move:   return snap_.affectMove;
        case TransformKind::Rotate: return snap_.affectRotate;
        case TransformKind::Scale:  return snap_.affectScale;
        default: return false;
    }
}

// Find the snap target for `world` under the current snap mode. Geometry modes
// (Vertex/Edge/Face/EdgeCenter) search every visible shape NOT in `exclude` and
// snap to the nearest candidate within a screen-pixel radius. Increment/Grid snap
// to the view grid (always). Returns the snapped world point + whether to draw the
// indicator (Increment never shows a mark).
Application::SnapResult Application::ComputeSnap(
        Renderer::Vec2 cursorWorld, float effZoom,
        const std::vector<uint64_t>& exclude,
        const std::vector<Renderer::Vec2>& rejectPts,
        const std::vector<Renderer::Vec2>& rejectSegs) const {
    // Snap is anchored to the CURSOR (cursorWorld), NOT the relative move amount —
    // so the target is stable per frame (no flicker) and only engages when the
    // cursor is within a screen-pixel radius of a candidate (Blender: too far → no
    // snap → normal move). Grid/Increment also gate on that radius.
    SnapResult out; out.pos = cursorWorld;
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    const float zoom = std::max(1e-4f, effZoom);
    const float kRadiusPx = 16.0f;               // snap pickup radius (screen px)
    const float kRadiusDoc = kRadiusPx / zoom;
    auto excluded = [&](uint64_t id){
        return std::find(exclude.begin(), exclude.end(), id) != exclude.end();
    };

    // Grid / Increment: round the CURSOR to the nearest grid crossing — ALWAYS snaps
    // (no radius; the grid is everywhere). Increment shows no mark.
    if (snap_.mode == SnapSettings::Mode::Grid ||
        snap_.mode == SnapSettings::Mode::Increment) {
        const float g = SnapGridStep(effZoom);
        if (g > 1e-6f) {
            out.pos = { std::round(cursorWorld.x / g) * g,
                        std::round(cursorWorld.y / g) * g };
            out.snapped = true;
            out.showMark = (snap_.mode == SnapSettings::Mode::Grid);
        }
        return out;
    }

    // Geometry modes: keep the candidate closest to the CURSOR within the radius,
    // skipping any candidate that coincides with a rejected point (the moving
    // selection's current positions) so the selection never snaps onto itself.
    const float kRejectDoc = 1.0f / zoom;        // ~1px coincidence tolerance
    auto isRejected = [&](Renderer::Vec2 p) {
        for (const Renderer::Vec2& r : rejectPts)
            if (std::hypot(p.x - r.x, p.y - r.y) < kRejectDoc) return true;
        return false;
    };
    // A flattened segment belongs to the MOVING selection when both its endpoints are
    // rejected points (a moving edge) → its projections/midpoint must be skipped, so
    // Edge/EdgeCenter never snap onto the selection itself (the edit-mode self-snap bug).
    auto segRejected = [&](Renderer::Vec2 a, Renderer::Vec2 b) {
        return isRejected(a) && isRejected(b);
    };
    // Distance from p to the segment [a,b].
    auto distToSeg = [](Renderer::Vec2 p, Renderer::Vec2 a, Renderer::Vec2 b) {
        Renderer::Vec2 ab{ b.x-a.x, b.y-a.y };
        float L2 = ab.x*ab.x + ab.y*ab.y;
        float t = L2 > 1e-9f ? std::clamp(((p.x-a.x)*ab.x + (p.y-a.y)*ab.y)/L2, 0.0f, 1.0f) : 0.0f;
        return std::hypot(p.x - (a.x+ab.x*t), p.y - (a.y+ab.y*t));
    };
    // A candidate is "on the moving selection" when it's a reject POINT, or lies on a
    // moving EDGE (rejectSegs is an explicit a,b,a,b,… list of the selection's actual
    // edges in world space, current positions). Using REAL edges — not consecutive
    // rejectPts (which form phantom segments between non-adjacent points).
    auto onSelection = [&](Renderer::Vec2 p) {
        if (isRejected(p)) return true;
        for (size_t i = 0; i + 1 < rejectSegs.size(); i += 2)
            if (distToSeg(p, rejectSegs[i], rejectSegs[i+1]) < kRejectDoc) return true;
        return false;
    };
    float bestD = kRadiusDoc; bool found = false; Renderer::Vec2 best{};
    auto consider = [&](Renderer::Vec2 p) {
        if (onSelection(p)) return;
        float d = std::hypot(p.x - cursorWorld.x, p.y - cursorWorld.y);
        if (d < bestD) { bestD = d; best = p; found = true; }
    };
    auto scanShape = [&](const Renderer::Shape& s) {
        if (!s.visible || excluded(s.id)) return;
        Renderer::Vec2 po = CurPageOriginOfShape(s.id);
        for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
            const Renderer::Part& part = s.parts[(size_t)pi];
            // VERTEX mode: only the CONTROL POINTS (path nodes for curves/paths; the
            // rectangle/ellipse corners via the baked nodes), NOT every flattened
            // outline point. We bake a copy so a parametric part still yields nodes.
            if (snap_.mode == SnapSettings::Mode::Vertex) {
                Renderer::Part baked = part; baked.EnsurePath();
                for (const Renderer::Node& nd : baked.path.nodes)
                    consider(Renderer::Tessellator::WorldTransform(s, nd.pos, po));
                continue;
            }
            // EDGE CENTER: the arc-length midpoint of each CONTROL-NODE segment (the
            // span between two consecutive nodes), NOT the midpoint of every tiny
            // flattened sub-segment (which scattered candidates along a curve). We
            // flatten per node-segment and take its 50%-arc-length point.
            if (snap_.mode == SnapSettings::Mode::EdgeCenter) {
                Renderer::Part baked = part; baked.EnsurePath();
                const int sc = std::max(1, baked.path.subCount());
                for (int spi = 0; spi < sc; ++spi) {
                    int b0 = 0, e0 = (int)baked.path.nodes.size();
                    baked.path.subRange(spi, b0, e0);
                    const bool cyc = baked.path.closed;
                    int segCount = cyc ? (e0 - b0) : (e0 - b0 - 1);
                    for (int k = 0; k < segCount; ++k) {
                        int ia = b0 + k, ib = b0 + ((k + 1) % (e0 - b0));
                        // World endpoints (node positions) — skip a moving edge.
                        Renderer::Vec2 wa = Renderer::Tessellator::WorldTransform(
                            s, baked.path.nodes[(size_t)ia].pos, po);
                        Renderer::Vec2 wb = Renderer::Tessellator::WorldTransform(
                            s, baked.path.nodes[(size_t)ib].pos, po);
                        if (segRejected(wa, wb)) continue;
                        // Mid arc-length point of this node-segment via a fine flatten.
                        const Renderer::Node& na = baked.path.nodes[(size_t)ia];
                        const Renderer::Node& nb = baked.path.nodes[(size_t)ib];
                        std::vector<Renderer::Vec2> pts{ na.pos };
                        Renderer::Tessellator::FlattenCubic(
                            na.pos, na.hasOut ? na.hOut : na.pos,
                            nb.hasIn ? nb.hIn : nb.pos, nb.pos, 24, pts);
                        float total = 0.0f;
                        for (size_t j = 1; j < pts.size(); ++j)
                            total += std::hypot(pts[j].x - pts[j-1].x, pts[j].y - pts[j-1].y);
                        float half = total * 0.5f, acc = 0.0f;
                        Renderer::Vec2 mid = na.pos;
                        for (size_t j = 1; j < pts.size(); ++j) {
                            float l = std::hypot(pts[j].x - pts[j-1].x, pts[j].y - pts[j-1].y);
                            if (acc + l >= half) { float u = l > 1e-6f ? (half - acc) / l : 0.0f;
                                mid = { pts[j-1].x + (pts[j].x - pts[j-1].x) * u,
                                        pts[j-1].y + (pts[j].y - pts[j-1].y) * u }; break; }
                            acc += l;
                        }
                        consider(Renderer::Tessellator::WorldTransform(s, mid, po));
                    }
                }
                continue;
            }
            // Edge / Face use the flattened outline (any point on the line for Edge;
            // centroid for Face).
            int subs = std::max(1, Renderer::Tessellator::SubpathCount(part));
            for (int sub = 0; sub < subs; ++sub) {
                bool closed = false;
                std::vector<Renderer::Vec2> poly =
                    Renderer::Tessellator::OutlinePartSub(s, part, sub, zoom, closed, po);
                if (poly.empty()) continue;
                const size_t n = poly.size();
                if (snap_.mode == SnapSettings::Mode::Edge) {
                    size_t sc = closed ? n : n - 1;
                    for (size_t i = 0; i < sc; ++i) {
                        Renderer::Vec2 a = poly[i], b = poly[(i+1)%n];
                        if (segRejected(a, b)) continue;   // moving edge → skip
                        Renderer::Vec2 ab{ b.x-a.x, b.y-a.y };
                        float L2 = ab.x*ab.x + ab.y*ab.y; if (L2 < 1e-9f) continue;
                        float t = std::clamp(((cursorWorld.x-a.x)*ab.x +
                                              (cursorWorld.y-a.y)*ab.y)/L2, 0.0f, 1.0f);
                        consider({ a.x + ab.x*t, a.y + ab.y*t });
                    }
                } else if (snap_.mode == SnapSettings::Mode::Face) {
                    if (closed && n >= 3) {     // face centroid
                        // Skip a MOVING face (all its outline points are on the moving
                        // selection) — else its centroid moves with the snap → flicker.
                        bool allMoving = true;
                        for (const Renderer::Vec2& v : poly)
                            if (!onSelection(v)) { allMoving = false; break; }
                        if (!allMoving) {
                            Renderer::Vec2 c{0,0};
                            for (const Renderer::Vec2& v : poly) { c.x += v.x; c.y += v.y; }
                            consider({ c.x / (float)n, c.y / (float)n });
                        }
                    }
                }
            }
        }
    };
    for (const Renderer::Artboard& ab : doc.artboards)
        for (const Renderer::Shape& s : ab.shapes) scanShape(s);
    for (const Renderer::Shape& s : doc.looseShapes) scanShape(s);
    if (found) { out.pos = best; out.snapped = true; out.showMark = true; }
    return out;
}

void Application::Action_BeginTransform(TransformKind kind) {
    // 2D Cursor tool: R rotates the 2D CURSOR (so the "Cursor" transform
    // orientation's axes can be aimed). G/S fall through to the normal ops.
    if (kind == TransformKind::Rotate &&
        Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.cursor") {
        cursorRotate_.Reset();
        cursorRotate_.active   = true;
        cursorRotate_.startRot = project_.document.cursorRotation;
        // startAngle is seeded by the first UpdateCursorRotate from the owning leaf.
        return;
    }
    // Line-mark tool: G/R/S act on the SELECTED MARKS (move along the curve / flip
    // side / scale a crossing's interval), not on objects. Intercept first.
    if (Shortcuts::Tools::ToolManager::Instance().GetActiveTool() == "tool.linemark" &&
        project_.document.HasMarkSelection()) {
        BeginMarkTransform(kind);
        return;
    }
    // Label the resulting undo step (and the Info feed) by the transform kind.
    MarkUndoLabel(kind == TransformKind::Move   ? "Move"
                : kind == TransformKind::Rotate ? "Rotate"
                : kind == TransformKind::Scale  ? "Scale" : "Transform");
    transformOp_.Reset();
    if (editorMode_ == EditorMode::Edit) {
        // Element transform: the WHOLE edit-mode selection — VERTICES and HANDLES —
        // undergoes the SAME G/R/S about the common pivot. Vertices move as whole
        // nodes; individually-selected handles move only their endpoint (per the
        // node's HandleMode). A handle whose node is also a selected vertex is skipped
        // (it moves with the node).
        auto& doc = project_.document;
        if (!doc.HasVertSelection() && !doc.HasHandleSelection()) return;
        transformOp_.kind    = kind;
        transformOp_.element = true;
        transformOp_.pivot   = ComputeVertPivot();
        transformOp_.vrefs.assign(doc.VertSelection().begin(), doc.VertSelection().end());
        // JUNCTION welding: if a selected vertex belongs to a junction group, pull in
        // every coincident node sharing its junctionId (same part) so the whole vertex
        // — all its branches' anchors — moves as one (a single multi-path vertex).
        {
            auto contains = [&](const Renderer::VertRef& r){
                for (const Renderer::VertRef& e : transformOp_.vrefs)
                    if (e.shape==r.shape && e.part==r.part && e.node==r.node) return true;
                return false;
            };
            std::vector<Renderer::VertRef> extra;
            for (const Renderer::VertRef& v : transformOp_.vrefs) {
                Renderer::Shape* s = doc.FindShape(v.shape);
                if (!s || v.part < 0 || v.part >= (int)s->parts.size()) continue;
                auto& nds = s->parts[(size_t)v.part].path.nodes;
                if (v.node < 0 || v.node >= (int)nds.size()) continue;
                uint32_t jid = nds[(size_t)v.node].junctionId;
                if (!jid) continue;
                for (int ni = 0; ni < (int)nds.size(); ++ni) {
                    if (nds[(size_t)ni].junctionId != jid) continue;
                    Renderer::VertRef r{ v.shape, v.part, ni };
                    if (!contains(r)) extra.push_back(r);
                }
            }
            for (const Renderer::VertRef& r : extra) transformOp_.vrefs.push_back(r);
        }
        transformOp_.vsnap.clear();
        auto nodeOf = [&](const Renderer::VertRef& v) -> Renderer::Node {
            Renderer::Shape* s = doc.FindShape(v.shape);
            if (s && v.part < (int)s->parts.size() &&
                v.node < (int)s->parts[(size_t)v.part].path.nodes.size())
                return s->parts[(size_t)v.part].path.nodes[(size_t)v.node];
            return Renderer::Node{};
        };
        for (const Renderer::VertRef& v : transformOp_.vrefs)
            transformOp_.vsnap.push_back(nodeOf(v));
        // Selected handles whose NODE isn't already a selected vertex.
        transformOp_.hrefs.clear(); transformOp_.hsnap.clear();
        for (const Renderer::HandleRef& h : doc.HandleSelection()) {
            Renderer::VertRef hv{ h.shape, h.part, h.node };
            if (doc.IsVertSelected(hv)) continue;     // node moves whole → skip handle
            transformOp_.hrefs.push_back(h);
            transformOp_.hsnap.push_back(nodeOf(hv));
        }
        return;
    }
    if (!project_.document.HasSelection()) return;
    transformOp_.kind  = kind;
    // The pivot is computed from the FULL selection (so "Active Element" can pivot
    // about a hidden active object), but only VISIBLE objects are actually
    // transformed — a hidden object stays put even when selected/active.
    transformOp_.pivot = ComputePivot();
    transformOp_.ids.clear();
    transformOp_.snapshot.clear();
    // PARENTING: the op targets the selection's transitive closure over children, so
    // a parent drags its descendants rigidly — they're transformed by the SAME op
    // about the SAME pivot, no separate propagation pass (the clean way).
    for (uint64_t id : SelectionWithDescendants())
        if (Renderer::Shape* s = project_.document.FindShape(id); s && s->visible) {
            transformOp_.ids.push_back(id);
            transformOp_.snapshot.push_back(s->transform);
        }
    if (transformOp_.ids.empty()) { transformOp_.Reset(); return; }  // all hidden
    // Capture the orientation basis ONCE at op start (axes stay fixed during the op
    // even if a Local/Parent reference rotates). axis starts free; X/Y toggle it.
    transformOp_.axis = TransformAxis::None;
    ComputeOrientationBasis(transformOp_.axisX, transformOp_.axisY);
    // startMouse + owner are set by the first UpdateTransformOp from the leaf.
}

// Selection ∪ all object descendants (parenting closure). Stable, de-duplicated,
// selection order first then descendants.
std::vector<uint64_t> Application::SelectionWithDescendants() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    std::vector<uint64_t> out;
    auto pushUnique = [&](uint64_t id) {
        if (id && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };
    for (uint64_t id : doc.Selection()) {
        pushUnique(id);
        for (uint64_t d : doc.DescendantsOf(id)) pushUnique(d);
    }
    return out;
}

// Orthonormal basis of the current Transform Orientation for the active selection.
void Application::ComputeOrientationBasis(Renderer::Vec2& outX,
                                          Renderer::Vec2& outY,
                                          TransformOrientation orient) const {
    outX = {1, 0}; outY = {0, 1};                 // Global / View / Cursor (today)
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    auto axesFromRotation = [&](float rot) {
        float c = std::cos(rot), s = std::sin(rot);
        outX = { c, s };                          // rotated X
        outY = { -s, c };                         // rotated Y (90° CCW from X)
    };
    if (orient == TransformOrientation::Local) {
        if (Renderer::Shape* a = doc.FindShape(doc.ActiveId()))
            axesFromRotation(a->transform.rotate);
    } else if (orient == TransformOrientation::Parent) {
        Renderer::Shape* a = doc.FindShape(doc.ActiveId());
        Renderer::Shape* p = a ? doc.FindShape(a->parentId) : nullptr;
        if (p) axesFromRotation(p->transform.rotate);
        else if (a) axesFromRotation(a->transform.rotate);   // no parent → Local
    } else if (orient == TransformOrientation::Cursor) {
        axesFromRotation(doc.cursorRotation);                // 2D cursor's own angle
    }
    // Global / View keep the document axes until the canvas can rotate (View).
}

void Application::Action_CycleTool() {
    Shortcuts::Tools::ToolManager::Instance().CycleNext();
    std::cout << "[ACTION] Cycle Tool → "
              << Shortcuts::Tools::ToolManager::Instance().GetActiveTool()
              << std::endl;
}

} // namespace App

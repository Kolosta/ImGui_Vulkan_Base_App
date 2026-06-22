#include "Application.h"
#include "ViewportToolsShared.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {

using Renderer::Vec2;

void Application::ApplyDefaultColors(Renderer::Shape& s) const {
    for (Renderer::Part& p : s.parts) {
        p.fill.color   = defaultFill_;
        p.stroke.color = defaultStroke_;
    }
}


void Application::HandleViewportTools(
    EditorState& st,
    const std::function<Vec2(ImVec2)>& s2d,
    const std::function<ImVec2(Vec2)>& d2s,
    float effZoom, bool hovered, ImDrawList* dl) {

    auto& ds   = DesignSystem::DesignSystem::Instance();
    auto& tm   = Shortcuts::Tools::ToolManager::Instance();
    const std::string tool = tm.GetActiveTool();
    ImGuiIO& io = ImGui::GetIO();
    Vec2 m = s2d(io.MousePos);
    const float zoom = std::max(0.0001f, effZoom);  // px per RAW doc-unit
    const void* self = &st;                          // this leaf's identity
    auto& doc = project_.document;

    // A modal G/R/S transform is driven directly by RenderViewport (top
    // priority), so HandleViewportTools is not called while one is active.

    // Hand tool + camera gestures are handled in RenderViewport; here we only
    // drive content tools. The Hand tool draws nothing.
    if (tool == "tool.hand") return;

    // ── Gesture ownership ────────────────────────────────────────────────────
    // With several Viewport zones open, this runs once per leaf. A gesture is
    // owned by the leaf that started it; OTHER leaves must not advance it (that
    // caused the "moves in both viewports / wrong mapping" bug). A leaf may only
    // start a new gesture, or drive the one it already owns.
    const bool owns   = toolState_.Active() && toolState_.owner == self;
    const bool foreign = toolState_.Active() && toolState_.owner != self;
    if (foreign) return;   // another zone is mid-gesture — stay out of its way

    // Overlay colours (chrome → design-system tokens, not document colours).
    ImU32 cAccent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    ImU32 cHandle = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_CursorRing));
    ImU32 cHandleEdge = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Viewport_OriginOutline));

    const bool lpressed  = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool lreleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool ldouble   = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool enter     = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    const bool escape    = ImGui::IsKeyPressed(ImGuiKey_Escape);
    // Right-click PRESS cancels an active gesture (handled centrally in
    // RenderViewport so it also gates the context menu); Escape does too.
    if (escape && owns) { CancelViewportGesture(); return; }
    (void)owns;

    // ── Select tool (Object Mode): click-pick, grab-move, box-select ─────────
    // Edit-mode selection/editing is handled in Lot E; until then the select
    // tool behaves the same (object picking) so the build stays functional.
    if (tool == "tool.select") {
        // Start a gesture only if this leaf is hovered and none is active.
        if (lpressed && !toolState_.Active()) {
            uint64_t hit = PickShape(doc, m, zoom,
                [this](int ab){ return CurPageOrigin(ab); },
                [this](int ab){ return CurPageVisible(ab); });
            if (hit) {
                // Did the selection/active actually change? Re-clicking the SAME sole
                // active object is a no-op → don't log or label a redundant "Select".
                const std::vector<uint64_t> prevSel = doc.Selection();
                const uint64_t prevActive = doc.ActiveId();
                if (io.KeyShift)              doc.SelectToggle(hit);
                else if (!doc.IsSelected(hit)) doc.SelectOnly(hit);
                else                           doc.SetActive(hit);
                doc.SyncActivePageToSelection();   // active page = selected obj's page
                const bool changed = (doc.Selection() != prevSel) ||
                                     (doc.ActiveId() != prevActive);
                if (changed) {
                    // Selection is in the undo snapshot → label the step. (A drag
                    // overrides this with "Move" before it commits.)
                    MarkUndoLabel(io.KeyShift ? "Extend Selection" : "Select");
                    Renderer::Shape* sh = doc.FindShape(hit);
                    const std::string nm = sh ? (sh->name.empty() ? "Object" : sh->name) : "";
                    char d[160];
                    std::snprintf(d, sizeof d, "object=%s  id=%llu  selected=%d",
                                  nm.c_str(), (unsigned long long)hit,
                                  (int)doc.Selection().size());
                    LogInfoActionRich(io.KeyShift ? "Extend Selection" : "Select Object", d);
                }
                // Arm a move of the whole selection (commits past threshold).
                // dragStart = the VIRTUAL mouse at press (doc-units); moveAccum
                // accumulates warp compensation so the displacement is computed
                // from a continuous virtual mouse (no jump across edge wraps).
                toolState_.Reset();
                toolState_.gesture = ToolGesture::MoveObjects;
                toolState_.owner   = self;
                toolState_.dragStart = m;          // virtual anchor at press
                toolState_.moveAccum = m;          // virtual mouse, integrated below
                BeginGestureMouseTracking();        // seed real-motion reference
                // Move only VISIBLE selected objects; a hidden object stays put
                // even if selected/active (Blender-style).
                toolState_.moveIds.clear();
                toolState_.moveOrigTranslate.clear();
                for (uint64_t id : doc.Selection())
                    if (Renderer::Shape* s = doc.FindShape(id); s && s->visible) {
                        toolState_.moveIds.push_back(id);
                        toolState_.moveOrigTranslate.push_back(s->transform.translate);
                    }
            } else {
                // Press on empty canvas → box-select (Shift = add).
                toolState_.Reset();
                toolState_.gesture = ToolGesture::BoxSelect;
                toolState_.owner   = self;
                toolState_.dragStart = toolState_.dragNow = m;
                toolState_.boxAdditive = io.KeyShift;
                // Clicking empty canvas deselects all but KEEPS the active object
                // active (Blender-style: its origin stays visible). A box-select
                // that ends up selecting nothing therefore leaves the active put.
                if (!io.KeyShift) doc.DeselectAll();
                // Active page: clicking ON a page's white sets it active; clicking
                // outside any page clears the active page (→ Shift+A spawns loose).
                int pageHit = -1;
                for (int k = (int)doc.artboards.size() - 1; k >= 0; --k) {
                    if (!CurPageVisible(k)) continue;
                    Renderer::Vec2 po = CurPageOrigin(k);
                    const auto& ab = doc.artboards[(size_t)k];
                    if (m.x >= po.x && m.x <= po.x + ab.size.x &&
                        m.y >= po.y && m.y <= po.y + ab.size.y) { pageHit = k; break; }
                }
                if (pageHit >= 0) doc.SetActivePage(doc.artboards[(size_t)pageHit].id);
                else              doc.ClearActivePage();
            }
        }

        // ── Drive an armed move (owner only) ────────────────────────────────
        if (toolState_.gesture == ToolGesture::MoveObjects && toolState_.owner == self) {
            // moveAccum is the VIRTUAL mouse (doc-units), integrated from the
            // REAL pointer motion (GestureMouseDelta excludes warp jumps but
            // keeps fast motion) → continuous + drift-free across edge wraps.
            // Eased by the global Shift precision-drag factor (finer move).
            ImVec2 d = GestureMouseDelta();
            const float pf = PrecisionDragFactor();
            toolState_.moveAccum.x += d.x * pf / zoom;
            toolState_.moveAccum.y += d.y * pf / zoom;
            Vec2 disp{ toolState_.moveAccum.x - toolState_.dragStart.x,
                       toolState_.moveAccum.y - toolState_.dragStart.y };
            if (std::hypot(disp.x, disp.y) * zoom > 3.0f)
                toolState_.movedPastThreshold = true;
            if (toolState_.movedPastThreshold) {
                for (size_t i = 0; i < toolState_.moveIds.size(); ++i)
                    if (Renderer::Shape* s = doc.FindShape(toolState_.moveIds[i]))
                        s->transform.translate = {
                            toolState_.moveOrigTranslate[i].x + disp.x,
                            toolState_.moveOrigTranslate[i].y + disp.y };
                // Move cursor + wrap at THIS zone's edges (infinite drag); the
                // wrap drops the next delta frame so the object never teleports.
                ShowMoveCursor();
                WrapMouseInRect(gestureCanvasMin_, gestureCanvasMax_);
            }
            if (lreleased) {
                if (toolState_.movedPastThreshold) {
                    MaybeTransferMovedObjects(toolState_.moveIds);  // page transfer
                    project_.dirty = true;
                    // Log the click-drag move (the G op logs itself; this is the
                    // Select-tool drag path, which previously logged nothing).
                    Vec2 fdisp{ toolState_.moveAccum.x - toolState_.dragStart.x,
                                toolState_.moveAccum.y - toolState_.dragStart.y };
                    char dd[128];
                    std::snprintf(dd, sizeof dd, "value=(%.4g, %.4g)  objects=%d",
                                  fdisp.x, fdisp.y, (int)toolState_.moveIds.size());
                    MarkUndoLabel("Move");
                    LogInfoActionRich("Move", dd);
                }
                toolState_.Reset();
            }
            return;
        }

        // ── Drive a box-select (owner only) ─────────────────────────────────
        if (toolState_.gesture == ToolGesture::BoxSelect && toolState_.owner == self) {
            toolState_.dragNow = m;
            ImVec2 a = d2s(toolState_.dragStart), b = d2s(toolState_.dragNow);
            dl->AddRectFilled(a, b, (cAccent & 0x00FFFFFF) | 0x22000000);
            dl->AddRect(a, b, cAccent, 0.0f, 0, 1.0f);
            if (lreleased) {
                float x0 = std::min(toolState_.dragStart.x, toolState_.dragNow.x);
                float y0 = std::min(toolState_.dragStart.y, toolState_.dragNow.y);
                float x1 = std::max(toolState_.dragStart.x, toolState_.dragNow.x);
                float y1 = std::max(toolState_.dragStart.y, toolState_.dragNow.y);
                for (int ai = 0; ai < (int)doc.artboards.size(); ++ai) {
                    if (!CurPageVisible(ai)) continue;   // hidden page → not box-selectable
                    for (const Renderer::Shape& s : doc.artboards[(size_t)ai].shapes) {
                        if (!s.visible) continue;
                        bool cl = false;
                        std::vector<Vec2> poly =
                            Renderer::Tessellator::Outline(s, zoom, cl, CurPageOrigin(ai));
                        bool inside = false;
                        for (const Vec2& p : poly)
                            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) { inside = true; break; }
                        if (inside) doc.SelectAdd(s.id);
                    }
                }
                toolState_.Reset();
            }
            return;
        }
        return;
    }

    // ── 2D Cursor tool: a plain click or drag places the 2D cursor (Lot 5).
    // (Shift+RMB still moves it under any tool — handled in RenderViewport.)
    if (tool == "tool.cursor") {
        auto& doc = project_.document;
        // R (Action_BeginTransform) armed a cursor rotation: turn doc.cursorRotation
        // by the change in the cursor→mouse angle. LMB/Enter confirm, Esc/RMB cancel.
        if (cursorRotate_.Active()) {
            if (cursorRotate_.owner == nullptr && hovered) cursorRotate_.owner = self;
            if (cursorRotate_.owner == self) {
                ImVec2 cs = d2s(doc.cursor);
                float ang = std::atan2(io.MousePos.y - cs.y, io.MousePos.x - cs.x);
                if (!cursorRotate_.seeded) {
                    cursorRotate_.startAngle = ang; cursorRotate_.seeded = true;
                }
                float delta = (ang - cursorRotate_.startAngle) * PrecisionDragFactor();
                // Ctrl → 5° snapping, mirroring the object rotate.
                float newRot = cursorRotate_.startRot + delta;
                if (io.KeyCtrl) {
                    const float inc = 5.0f * 3.14159265358979f / 180.0f;
                    newRot = std::round(newRot / inc) * inc;
                }
                doc.cursorRotation = newRot;
                // Guide line cursor→mouse + the cursor's current X axis.
                ImU32 acc = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
                dl->AddLine(cs, io.MousePos, acc, 1.4f);
                ShowOrientedCursor("move-up-down-cur", ang);
                bool confirm = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                               ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                               ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
                bool cancel  = ImGui::IsKeyPressed(ImGuiKey_Escape) ||
                               ImGui::IsMouseClicked(ImGuiMouseButton_Right);
                if (cancel) { doc.cursorRotation = cursorRotate_.startRot;
                              cursorRotate_.Reset();
                              rmbConsumedByTransform_ = true; }
                else if (confirm) {
                    MarkUndoLabel("Rotate 2D Cursor");
                    char d[96];
                    std::snprintf(d, sizeof d, "angle=%.3g\xC2\xB0",
                                  doc.cursorRotation * 180.0f / 3.14159265358979f);
                    LogInfoActionRich("Rotate 2D Cursor", d);
                    project_.dirty = true; cursorRotate_.Reset();
                }
            }
            return;
        }
        if (hovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))) {
            doc.cursor = m;
        }
        return;
    }
    // No other tools exist: object creation is done through the Shift+A menu.
}

// ── Shift+A "Add" menu (Lot 5): spawn an object at the 2D cursor ─────────────
// Build a default geometry for `what` (object-local, centred near origin), then
// place it so its centre lands on the 2D cursor, on the page under the cursor.
void Application::Action_AddShape(const std::string& what) {
    // Preview placement: arm a cursor-following preview instead of dropping the
    // object at the 2D cursor right now (the click commits it). The guard avoids
    // re-arming when UpdatePlacement re-enters this path to actually create it.
    if (!placementCommitting_ && PreviewPlacementEnabled()) {
        RequestPlacementCore(what);
        return;
    }
    using K = Renderer::ShapeKind;
    auto& doc = project_.document;
    const float R = 100.0f;                 // default half-extent (doc-units)

    // A regular N-gon path (mesh) around the local origin, flat-top.
    auto ngon = [&](Renderer::Shape& s, int n) {
        for (int i = 0; i < n; ++i) {
            float a = (float)i / (float)n * 6.2831853f - 1.5707963f;
            Renderer::Node nd({ std::cos(a) * R, std::sin(a) * R });
            nd.mode = Renderer::HandleMode::Vector;
            s.MainPart().path.nodes.push_back(nd);
        }
        s.MainPart().path.closed = true;
    };
    // A closed cubic circle (4 quadrants) as an editable Bézier CURVE around
    // origin (anchors on the curve, aligned in/out handles).
    auto bezierCircle = [&](Renderer::Shape& s) {
        const float k = 0.5522847498f * R;
        Renderer::Vec2 p[4] = { {R,0}, {0,R}, {-R,0}, {0,-R} };
        Renderer::Vec2 t[4] = { {0,k}, {-k,0}, {0,-k}, {k,0} };  // tangents (CCW)
        for (int i = 0; i < 4; ++i) {
            Renderer::Node nd(p[i]);
            nd.hasIn = nd.hasOut = true;
            nd.hIn  = { p[i].x - t[i].x, p[i].y - t[i].y };
            nd.hOut = { p[i].x + t[i].x, p[i].y + t[i].y };
            nd.mode = Renderer::HandleMode::Aligned;
            s.MainPart().path.nodes.push_back(nd);
        }
        s.MainPart().path.closed = true;
    };
    Renderer::Shape s;
    if      (what == "rectangle") { s = MakeShape(K::Rectangle); s.name = "Rectangle";
                                    s.MainPart().pos = {-R,-R}; s.MainPart().size = {2*R,2*R}; }
    else if (what == "ellipse")   { s = MakeShape(K::Ellipse);   s.name = "Ellipse";
                                    s.MainPart().pos = {-R,-R}; s.MainPart().size = {2*R,2*R}; }
    else if (what == "circle")    { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
                                    s.name = "Circle"; bezierCircle(s); }
    else if (what == "triangle")  { s = MakeShape(K::Triangle);  s.name = "Triangle";  ngon(s, 3); CenterOrigin(s); }
    else if (what == "hexagon")   { s = MakeShape(K::Path);      s.name = "Hexagon";   ngon(s, 6); }
    else if (what == "bezier")    { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
                                    s.name = "Bezier";
                                    // a simple open 2-point bezier
                                    Renderer::Node a({-R,0}); a.hasOut=true; a.hOut={-R*0.3f,-R}; a.mode=Renderer::HandleMode::Aligned;
                                    Renderer::Node b({ R,0}); b.hasIn =true; b.hIn ={ R*0.3f, R}; b.mode=Renderer::HandleMode::Aligned;
                                    s.MainPart().path.nodes = { a, b }; s.MainPart().path.closed = false;
                                    s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
    else if (what == "nurbs_circle") { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Nurbs);
                                    s.name = "NURBS Circle";
                                    // EXACT rational circle: 8 control points on a SQUARE —
                                    // edge-midpoints (on the circle, radius R) weight 1, corners
                                    // (the square corners, at R·√2) weight √2/2. Closed periodic
                                    // quadratic. This is Blender's NURBS-circle control hull.
                                    {
                                        const float w = 0.70710678f;          // √2/2
                                        auto& nodes = s.MainPart().path.nodes;
                                        nodes.clear();
                                        // Order: right-mid, corner, top-mid, corner, left-mid,
                                        // corner, bottom-mid, corner (CCW, Y-down).
                                        struct CP { float x, y, w; };
                                        const CP cps[8] = {
                                            { R, 0, 1}, { R, R, w}, { 0, R, 1}, {-R, R, w},
                                            {-R, 0, 1}, {-R,-R, w}, { 0,-R, 1}, { R,-R, w} };
                                        for (const CP& c : cps) {
                                            Renderer::Node nd({ c.x, c.y });
                                            nd.hasIn = nd.hasOut = false; nd.weight = c.w;
                                            nodes.push_back(nd);
                                        }
                                    }
                                    // Bezier U → the periodic loop is 4 rational-Bézier
                                    // quarter-arcs = an EXACT circle (not a rounded square).
                                    s.MainPart().path.closed = true; s.MainPart().orderU = 3;
                                    s.MainPart().nurbsBezier = true; }
    else if (what == "nurbs")     { s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Nurbs);
                                    s.name = "NURBS Curve";
                                    // open 4-control-point arc (control points OFF the curve),
                                    // endpoint-clamped so it meets the first/last control point.
                                    Renderer::Node a({-R,0}), b({-R*0.33f,-R}), c({R*0.33f,-R}), d({R,0});
                                    a.hasIn=a.hasOut=b.hasIn=b.hasOut=false;
                                    c.hasIn=c.hasOut=d.hasIn=d.hasOut=false;
                                    s.MainPart().path.nodes = { a, b, c, d }; s.MainPart().path.closed = false;
                                    s.MainPart().orderU = 3;
                                    s.MainPart().nurbsEndpoint = true; s.MainPart().nurbsBezier = false;
                                    s.MainPart().fill.enabled = false; s.MainPart().stroke.enabled = true; }
    else                          { return; }            // unknown id

    ApplyDefaultColors(s);   // new primitive uses the menu-bar default fill/stroke
    if (what != "triangle") CenterOrigin(s);             // triangle already done

    // The new object belongs to the ACTIVE PAGE (Shift+A target), not the page
    // under the cursor. If no page is active it's added page-less (loose, under
    // the root collection). It must appear exactly AT the 2D cursor — which is in
    // document/display space — so we place its translate relative to the active
    // page's DISPLAY origin (this viewport's layout), then convert to the stored
    // page-relative (ab.pos) frame. For a loose object the reference is {0,0}.
    int ab = -1;
    if (doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());

    if (editorMode_ == EditorMode::Edit) {
        // In Edit Mode, fold the new geometry into the active object (like Join).
        uint64_t hostId = doc.ActiveId();
        if (!hostId && !doc.Selection().empty()) hostId = doc.Selection().front();
        if (hostId) {
            // Place at the cursor relative to the host's page display origin.
            Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{0, 0};
            s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                                      doc.cursor.y - dispPo.y - s.origin.y };
            AddShapeWorldDisplay(doc, ab, std::move(s));
            FoldNewShapeIntoObject(hostId);
            MarkUndoLabel("Add " + std::string(what));
            project_.dirty = true;
            return;
        }
    }

    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{0, 0};
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
}

// The Shift+A Add menu. Contextual: Object Mode offers everything; Edit Mode
// offers only shapes compatible with the active object's type (curve→curves,
// mesh→meshes), Blender-style.

void Application::RenderAddMenu() {
    // A module may REPLACE the Add menu entirely (e.g. IOF Mapping → ISOM
    // catalogue). If it does, use those entries verbatim.
    if (activeModule_) {
        std::vector<UI::MenuEntry> modEntries;
        if (activeModule_->BuildAddMenu(modEntries)) {
            if (modEntries.empty()) {
                UI::MenuEntry e; e.label = "(no objects)"; e.enabled = false;
                modEntries.push_back(std::move(e));
            }
            UI::ContextMenu("##addMenu", addMenuPos_, modEntries, "Add");
            return;
        }
    }
    // Module disabled the core primitives → nothing to add from here.
    if (!activeCapabilities_.corePrimitivesAddMenu) {
        std::vector<UI::MenuEntry> none;
        UI::MenuEntry e; e.label = "(no objects in this module)"; e.enabled = false;
        none.push_back(std::move(e));
        UI::ContextMenu("##addMenu", addMenuPos_, none, "Add");
        return;
    }
    const bool edit = (editorMode_ == EditorMode::Edit);
    // In Edit Mode, what can be added depends on the active object's family:
    // a Mesh object accepts only meshes, a curve-like object only curve-likes
    // (you can't fold incompatible geometry into the edited object).
    bool activeIsCurve = false; bool haveActive = false;
    if (edit) {
        if (Renderer::Shape* s = project_.document.ActiveShape();
            s && !s->parts.empty()) {
            activeIsCurve = (s->Family() == Renderer::PartType::Curve);
            haveActive = true;
        }
    }
    auto leaf = [&](const char* label, const char* id, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        std::string what = id;
        e.onClick = [this, what]{ Action_AddShape(what); };
        return e;
    };

    std::vector<UI::MenuEntry> entries;
    const bool allowMesh  = !edit || (haveActive && !activeIsCurve);
    const bool allowCurve = !edit || (haveActive && activeIsCurve);

    if (allowMesh) {
        UI::MenuEntry shapes; shapes.label = "Shape";
        shapes.submenu.push_back(leaf("Rectangle", "rectangle", "Add a rectangle"));
        shapes.submenu.push_back(leaf("Ellipse",   "ellipse",   "Add an ellipse"));
        shapes.submenu.push_back(leaf("Triangle",  "triangle",  "Add a triangle"));
        shapes.submenu.push_back(leaf("Hexagon",   "hexagon",   "Add a hexagon"));
        entries.push_back(std::move(shapes));
    }
    if (allowCurve) {
        UI::MenuEntry curve; curve.label = "Curve";
        curve.submenu.push_back(leaf("Bezier",       "bezier",       "Add a Bézier curve"));
        curve.submenu.push_back(leaf("Circle",       "circle",       "Add a Bézier circle"));
        curve.submenu.push_back(leaf("Nurbs Curve",  "nurbs",        "Add a NURBS curve"));
        curve.submenu.push_back(leaf("Nurbs Circle", "nurbs_circle", "Add a NURBS circle"));
        entries.push_back(std::move(curve));
    }
    if (entries.empty()) {   // edit mode with no active object → nothing addable
        UI::MenuEntry e; e.label = "(select an object first)"; e.enabled = false;
        entries.push_back(std::move(e));
    }
    // New document — moved here from the viewport top bar. Only in Object mode
    // (adding a whole document while editing one object's geometry makes no sense).
    if (!edit) {
        UI::MenuEntry nd; nd.label = "New Document"; nd.icon = "new";
        nd.tooltip = "Create a new, empty document";
        nd.onClick = [this]{
            // Open the New Artboard popup in the leaf the Add menu belongs to (the
            // mouse is over the popup by now, so HoveredEditorState() won't do).
            if (addMenuState_) addMenuState_->openNewDoc = true;
        };
        entries.push_back(std::move(nd));
    }
    UI::ContextMenu("##addMenu", addMenuPos_, entries, "Add");
}

// Shift+G "Select Grouped" picker: a flat menu of the relationship modes. Each
// entry runs Action_SelectGrouped, which also publishes the operator panel so the
// mode can be changed afterwards.
void Application::RenderSelectGroupedMenu() {
    auto group = [&](const char* label, GroupedMode mode, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        e.enabled = project_.document.ActiveId() != 0;
        e.onClick = [this, mode]{ Action_SelectGrouped(mode); };
        return e;
    };
    std::vector<UI::MenuEntry> entries;
    entries.push_back(group("Children", GroupedMode::Children,
        "Select all hierarchical descendants of the active object"));
    entries.push_back(group("Immediate Children", GroupedMode::ImmediateChildren,
        "Select the direct children of the active object"));
    entries.push_back(group("Parent", GroupedMode::Parent,
        "Select the parent of the active object"));
    entries.push_back(group("Siblings", GroupedMode::Siblings,
        "Select objects sharing the active object's parent"));
    entries.push_back(group("Type", GroupedMode::Type,
        "Select objects of the same geometry type"));
    entries.push_back(group("Collection", GroupedMode::Collection,
        "Select objects in the active object's collection"));
    entries.push_back(group("Color", GroupedMode::Color,
        "Select objects with the same fill and stroke colour"));
    UI::ContextMenu("##selectGroupedMenu", selectGroupedMenuPos_, entries, "Select Grouped");
}


} // namespace App

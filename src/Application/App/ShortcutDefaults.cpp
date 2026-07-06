#include "Application.h"
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>

namespace App {

void Application::RegisterDefaultShortcuts() {
    using namespace Shortcuts;
    auto& sm = ShortcutManager::Instance();
    auto& tm = Tools::ToolManager::Instance();

    auto sigKey = [](ImGuiKey key, bool ctrl=false, bool shift=false, bool alt=false) {
        EventSignature s;
        s.type = EventType::KeyPress;
        s.key  = key;
        s.modifiers.ctrl = ctrl;
        s.modifiers.shift = shift;
        s.modifiers.alt  = alt;
        return s;
    };

    // ── Application ──────────────────────────────────────────────────────────
    {
        Action a;
        a.id = "app.quit";
        a.name = "Quit";
        a.description = "Close the application";
        a.category = ActionCategory::Application;
        a.callback = [this]{ Action_Quit(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Q, true) });
    }
    {
        Action a;
        a.id = "app.toggleSettings";
        a.name = "Toggle Settings";
        a.description = "Show or hide the Settings window";
        a.category = ActionCategory::Application;
        a.callback = [this]{ Action_ToggleSettings(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F1),
                               sigKey(ImGuiKey_Comma, /*ctrl=*/true) });
    }
    {
        Action a;
        a.id = "app.toggleTokenGraph";
        a.name = "Toggle Token Graph";
        a.description = "Show or hide the Token Graph editor window";
        a.category = ActionCategory::Application;
        a.callback = [this]{ Action_ToggleTokenGraph(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F2) });
    }

    // ── File ─────────────────────────────────────────────────────────────────
    {
        Action a; a.id = "file.new"; a.name = "New Project";
        a.description = "Start a new empty project (one default page)";
        a.category = ActionCategory::File; a.callback = [this]{ Action_NewFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_N, true) });
    }
    {
        Action a; a.id = "file.open"; a.name = "Open Project";
        a.description = "Open an existing .acu project";
        a.category = ActionCategory::File; a.callback = [this]{ Action_OpenFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_O, true) });
    }
    {
        Action a; a.id = "file.save"; a.name = "Save Project";
        a.description = "Save the current project (.acu)";
        a.category = ActionCategory::File; a.callback = [this]{ Action_SaveFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, true) });
    }
    {
        Action a; a.id = "file.saveAs"; a.name = "Save Project As";
        a.description = "Save the current project to a new .acu file";
        a.category = ActionCategory::File; a.callback = [this]{ Action_SaveFileAs(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, /*ctrl=*/true, /*shift=*/true) });
    }

    // ── Edit: undo / redo (main window history; Preferences keeps its own) ─────
    {
        Action a; a.id = "edit.undo"; a.name = "Undo";
        a.description = "Undo the last change in this window";
        a.category = ActionCategory::Edit; a.callback = [this]{ Action_Undo(); };
        a.allowRepeat = true;   // holding Ctrl+Z keeps undoing
        sm.RegisterAction(a, { sigKey(ImGuiKey_Z, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.redo"; a.name = "Redo";
        a.description = "Redo the change undone last in this window";
        a.category = ActionCategory::Edit; a.callback = [this]{ Action_Redo(); };
        a.allowRepeat = true;
        // Ctrl+Shift+Z and Ctrl+Y both redo (two default bindings).
        sm.RegisterAction(a, { sigKey(ImGuiKey_Z, /*ctrl=*/true, /*shift=*/true),
                               sigKey(ImGuiKey_Y, /*ctrl=*/true) });
    }

    // ── View ─────────────────────────────────────────────────────────────────
    {
        Action a; a.id = "view.toggleDemo"; a.name = "ImGui Demo";
        a.description = "Show the ImGui demo window";
        a.category = ActionCategory::View;
        a.callback = [this]{ Action_ToggleImGuiDemo(); };
        // F12 conflicte parfois avec des hotkeys système (devtools, etc.) :
        // par défaut on prend Ctrl+Shift+D, plus sûr.
        sm.RegisterAction(a, { sigKey(ImGuiKey_D, /*ctrl=*/true, /*shift=*/true) });
    }
    {
        Action a; a.id = "view.toggleFullscreen"; a.name = "Toggle Fullscreen";
        a.description = "Toggle borderless fullscreen-desktop (F11)";
        a.category = ActionCategory::View;
        // Deferred: changing fullscreen mid-frame re-enters RenderFrame via the
        // resize event watch and trips an ImGui assert (same as min/maximize).
        a.callback = [this]{ pendingWindowOp_ = WindowOp::ToggleFullscreen; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F11) });
    }

    // ── Tools (Lot 5 refactor) ────────────────────────────────────────────────
    // Blender-style: there are NO creation tools any more. Objects are spawned
    // through the Shift+A "Add" menu. Only two tools remain:
    //   • Select  — pick / box-select / grab-move (default).
    //   • Cursor  — click or drag places the 2D cursor (Shift+RMB still works
    //               under any tool, so the cursor tool is just a convenience).
    tm.RegisterTool({"tool.select", "Select",     "select",    {"tool.select.activate"}});
    tm.RegisterTool({"tool.cursor", "2D Cursor",  "crop-free", {"tool.cursor.activate"}});
    // Curve tool — Edit Mode only (the palette hides it elsewhere). Click = a
    // straight (Vector) point, click-drag = a Bézier point (pull the tangent);
    // double-click / Enter finishes, Esc / right-click cancels.
    tm.RegisterTool({"tool.curve",  "Curve",      "pen",       {"tool.curve.activate"}});
    // Extrude tool — Edit Mode only. Grab inside the ring around the active vertex
    // to extend the path with a new point that drops on release.
    tm.RegisterTool({"tool.extrude","Extrude",    "polyline",  {"tool.extrude.activate"}});
    // Line-mark tool — click ON a stroked line to drop a manual mark (slope tick,
    // crossing point 519, bridge 512, pinned pylon) at the nearest point. The mark
    // KIND is chosen in the Viewport top bar; the line is cut for crossing/bridge.
    tm.RegisterTool({"tool.linemark","Line Mark", "line-mark", {"tool.linemark.activate"}});

    // Select tool: W (Blender's select tool key) and Shift+Space.
    {
        Action a; a.id = "tool.select.activate"; a.name = "Activate Select";
        a.description = "Select, box-select and grab-move objects";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.select"); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_W),
                               sigKey(ImGuiKey_Space, /*ctrl=*/false, /*shift=*/true) });
    }
    // Cursor tool: places the 2D cursor with a plain click/drag.
    {
        Action a; a.id = "tool.cursor.activate"; a.name = "Activate 2D Cursor";
        a.description = "Place the 2D cursor by clicking or dragging";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.cursor"); };
        sm.RegisterAction(a, {});   // no default key
    }
    // Curve tool: C (Edit Mode). Activating it also enters Edit Mode if needed.
    {
        Action a; a.id = "tool.curve.activate"; a.name = "Activate Curve";
        a.description = "Draw a Bézier curve: click for points, drag for handles";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.curve"); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_C) });
    }
    // Extrude tool (Edit Mode). No default key (the E key does the inline extrude).
    {
        Action a; a.id = "tool.extrude.activate"; a.name = "Activate Extrude";
        a.description = "Extend a path: grab the active vertex's ring and drag";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.extrude"); };
        sm.RegisterAction(a, {});
    }
    // Line-mark tool: no default key. Activating it lets the user click on a line
    // to place a slope tick / crossing point / bridge / pylon.
    {
        Action a; a.id = "tool.linemark.activate"; a.name = "Activate Line Mark";
        a.description = "Click a line to place a slope tick, crossing, bridge or pylon";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.linemark"); };
        sm.RegisterAction(a, {});
    }
    // E: extrude the active vertex inline (Edit Mode), then move (click to drop).
    {
        Action a; a.id = "edit.extrude"; a.name = "Extrude Vertex";
        a.description = "Extend the path from the active vertex";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ExtrudeActiveVertex(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_E) });
    }
    // ── Shift+A: the Add (create object) menu (Blender-style spawn) ───────────
    {
        Action a; a.id = "viewport.addMenu"; a.name = "Add…";
        a.description = "Open the Add menu to create a shape or curve";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ addMenuRequest_ = true; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_A, /*ctrl=*/false, /*shift=*/true) });
    }
    // Start on the Select tool.
    tm.SetActiveTool("tool.select");
    {
        Action a; a.id = "view.fitDocument"; a.name = "Fit Document in View";
        a.description = "Zoom/pan so the whole document is visible";
        a.category = ActionCategory::View;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ViewFitDocument(); };
        // Shift+C and Ctrl+Numpad0.
        sm.RegisterAction(a, { sigKey(ImGuiKey_C, false, true) });
    }
    {
        Action a; a.id = "view.resetOrigin"; a.name = "Reset View to Origin";
        a.description = "Recenter near the document origin at 100% zoom";
        a.category = ActionCategory::View;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ViewResetOrigin(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Keypad0, true) });
    }
    {
        // Numpad . — frame the selected/active object(s). Works in BOTH the
        // Viewport (zoom/pan onto them) and the Outliner (scroll to the active
        // row), like Blender — so no editor context is required; it acts on the
        // hovered zone.
        Action a; a.id = "view.fitSelection"; a.name = "Frame Selected";
        a.description = "Zoom the view onto the selected/active object(s)";
        a.category = ActionCategory::View;
        a.callback = [this]{ Action_ViewFitSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_KeypadDecimal) });
    }
    {
        Action a; a.id = "file.newDocument"; a.name = "New Document";
        a.description = "Create a new blank document / artboard";
        a.category = ActionCategory::File;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_NewDocument(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_N, true, true) });
    }
    {
        Action a; a.id = "edit.toggleMode"; a.name = "Toggle Edit Mode";
        a.description = "Switch between Object and Edit mode (Tab)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ToggleEditMode(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab) });
    }
    {
        Action a; a.id = "edit.deleteSelection"; a.name = "Delete Selection";
        a.description = "Delete the selected objects (or elements in Edit mode)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_DeleteSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_X) });
    }
    // ── Internal clipboard: copy / cut / paste objects & pages (Ctrl+C/X/V). ──
    // Global (no editor scope) so they work from the Viewport or the Outliner.
    {
        Action a; a.id = "edit.copy"; a.name = "Copy";
        a.description = "Copy the selected objects / pages to the internal clipboard";
        a.category = ActionCategory::Edit;
        a.callback = [this]{ Action_Copy(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_C, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.cut"; a.name = "Cut";
        a.description = "Cut the selected objects / pages to the internal clipboard";
        a.category = ActionCategory::Edit;
        a.callback = [this]{ Action_Cut(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_X, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.paste"; a.name = "Paste";
        a.description = "Paste the internal clipboard (objects onto the active page; pages as new artboards)";
        a.category = ActionCategory::Edit;
        a.callback = [this]{ Action_Paste(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_V, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.joinSelection"; a.name = "Join";
        a.description = "Merge the selected objects into the active one";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_JoinSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_J, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.hideSelection"; a.name = "Hide Selected";
        a.description = "Hide the selected objects (H); reveal with Alt+H";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_HideSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_H) });
    }
    {
        Action a; a.id = "edit.parent"; a.name = "Parent";
        a.description = "Parent the selected objects to the active one (Ctrl+P)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ParentSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_P, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.clearParent"; a.name = "Clear Parent";
        a.description = "Clear the parent of the selected objects (Alt+P)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ClearParent(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_P, /*ctrl=*/false, /*shift=*/false, /*alt=*/true) });
    }
    {
        Action a; a.id = "edit.group"; a.name = "Group";
        a.description = "Group the selected objects into a layer group (Ctrl+G)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_GroupSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_G, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.ungroup"; a.name = "Ungroup";
        a.description = "Dissolve the layer group of the selected objects (Ctrl+Shift+G)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_UngroupSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_G, /*ctrl=*/true, /*shift=*/true) });
    }
    // ── Selection families (Blender Shift+G / Shift+L / Shift+Numpad±) ─────────
    {
        Action a; a.id = "select.grouped"; a.name = "Select Grouped";
        a.description = "Select objects grouped with the active one (Shift+G)";
        a.category = ActionCategory::Selection;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ selectGroupedMenuRequest_ = true; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_G, /*ctrl=*/false, /*shift=*/true) });
    }
    {
        Action a; a.id = "select.linked"; a.name = "Select Linked";
        a.description = "Select objects sharing the active object's data (Shift+L)";
        a.category = ActionCategory::Selection;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_SelectLinked(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_L, /*ctrl=*/false, /*shift=*/true) });
    }
    {
        Action a; a.id = "select.more"; a.name = "Select More";
        a.description = "Grow the selection by one parent/child or vertex ring";
        a.category = ActionCategory::Selection;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_SelectMoreLess(true); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_KeypadAdd, /*ctrl=*/false, /*shift=*/true) });
    }
    {
        Action a; a.id = "select.less"; a.name = "Select Less";
        a.description = "Shrink the selection by one parent/child or vertex ring";
        a.category = ActionCategory::Selection;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_SelectMoreLess(false); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_KeypadSubtract, /*ctrl=*/false, /*shift=*/true) });
    }
    {
        Action a; a.id = "edit.revealHidden"; a.name = "Reveal Hidden";
        a.description = "Show every hidden object (Alt+H)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_RevealAll(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_H, /*ctrl=*/false, /*shift=*/false, /*alt=*/true) });
    }
    // ── Modal transforms (Blender G/R/S) ──────────────────────────────────────
    {
        Action a; a.id = "transform.move"; a.name = "Move (Grab)";
        a.description = "Move the selection (G); LMB/Enter confirm, Esc/RMB cancel";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_BeginTransform(TransformKind::Move); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_G) });
    }
    {
        Action a; a.id = "transform.rotate"; a.name = "Rotate";
        a.description = "Rotate the selection around the pivot (R)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_BeginTransform(TransformKind::Rotate); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_R) });
    }
    {
        Action a; a.id = "transform.scale"; a.name = "Scale";
        a.description = "Scale the selection around the pivot (S)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_BeginTransform(TransformKind::Scale); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S) });
    }
    {
        Action a; a.id = "edit.snapPie"; a.name = "Snap Pie Menu";
        a.description = "Open the snap pie menu (Shift+S): cursor / selection snapping";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ pieMenuRequest_ = true; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, /*ctrl=*/false, /*shift=*/true) });
    }
    // ── Edit-mode element sub-modes (Blender 1/2/3) + merge (M) ───────────────
    {
        Action a; a.id = "edit.vertexMode"; a.name = "Vertex Select Mode";
        a.description = "Edit mode: select vertices (1)";
        a.category = ActionCategory::Edit; a.requiredContext.editor = "viewport";
        a.callback = [this]{ project_.document.elementMode = Renderer::SelectElementMode::Vertex; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_1) });
    }
    {
        Action a; a.id = "edit.edgeMode"; a.name = "Edge Select Mode";
        a.description = "Edit mode: select edges (2)";
        a.category = ActionCategory::Edit; a.requiredContext.editor = "viewport";
        a.callback = [this]{ project_.document.elementMode = Renderer::SelectElementMode::Edge; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_2) });
    }
    {
        Action a; a.id = "edit.faceMode"; a.name = "Face Select Mode";
        a.description = "Edit mode: select faces (3)";
        a.category = ActionCategory::Edit; a.requiredContext.editor = "viewport";
        a.callback = [this]{ project_.document.elementMode = Renderer::SelectElementMode::Face; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_3) });
    }
    {
        Action a; a.id = "edit.merge"; a.name = "Merge";
        a.description = "Edit mode: merge selected vertices (M)";
        a.category = ActionCategory::Edit; a.requiredContext.editor = "viewport";
        a.callback = [this]{ mergeMenuRequest_ = true; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_M) });
    }
    {
        Action a; a.id = "edit.handleMenu"; a.name = "Set Handle Type";
        a.description = "Edit mode: choose the Bézier handle type (V)";
        a.category = ActionCategory::Edit; a.requiredContext.editor = "viewport";
        a.callback = [this]{ handleMenuRequest_ = true; };
        sm.RegisterAction(a, { sigKey(ImGuiKey_V) });
    }

    // ── Editor switch shortcuts (Blender-style) ──────────────────────────────
    // Switch the editor kind of the zone under the mouse. No requiredContext:
    // they fire over any zone, targeting the hovered leaf (ZoneLayout resolves
    // it each frame). The editor-selector dropdown shows these bindings.
    {
        Action a; a.id = "editor.viewport"; a.name = "Viewport Editor";
        a.description = "Show the Viewport editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::Viewport); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F5, false, true) });
    }
    {
        Action a; a.id = "editor.outliner"; a.name = "Outliner Editor";
        a.description = "Show the Outliner editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::Outliner); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F9, false, true) });
    }
    {
        Action a; a.id = "editor.properties"; a.name = "Properties Editor";
        a.description = "Show the Properties editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::Properties); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F10, false, true) });
    }
    {
        Action a; a.id = "editor.timeline"; a.name = "Timeline Editor";
        a.description = "Show the Timeline editor in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::Timeline); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F12, false, true) });
    }
    {
        Action a; a.id = "editor.devPanels"; a.name = "Dev Panel";
        a.description = "Show the Dev Panel (live undo/redo lists + debug data)";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::DevPanels); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_F11, false, true) });
    }
    {
        Action a; a.id = "editor.info"; a.name = "Info Editor";
        a.description = "Show the Info editor (live action feed) in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.SetHoveredEditor(CoreEditor::Info); };
        sm.RegisterAction(a, {});   // no default key
    }

    // ── Tab navigation (multi-tab zones) ─────────────────────────────────────
    // Next/Previous cycle the tabs of the HOVERED zone; First/Last jump within
    // the ACTIVE zone (the last one clicked).
    {
        Action a; a.id = "editor.tabNext"; a.name = "Next Tab";
        a.description = "Activate the next tab in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.HoveredTabCycle(+1); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab, true) });
    }
    {
        Action a; a.id = "editor.tabPrev"; a.name = "Previous Tab";
        a.description = "Activate the previous tab in the hovered zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.HoveredTabCycle(-1); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Tab, true, true) });
    }
    {
        Action a; a.id = "editor.tabFirst"; a.name = "First Tab";
        a.description = "Activate the first tab in the active zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.ActiveTabSelectEdge(false); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_Home, true) });
    }
    {
        Action a; a.id = "editor.tabLast"; a.name = "Last Tab";
        a.description = "Activate the last tab in the active zone";
        a.category = ActionCategory::Window;
        a.callback = [this]{ zoneLayout_.ActiveTabSelectEdge(true); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_End, true) });
    }

    // ── Zone-specific actions (same key, different editors) ──────────────────
    {
        Action a; a.id = "edit.themePreview.cycle"; a.name = "Cycle Theme";
        a.description = "Cycle through themes (active in the Theme Preview area)";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "themePreview";
        a.callback = &Application::Action_ThemePreviewCycle;
        sm.RegisterAction(a, { sigKey(ImGuiKey_T) });
    }
    {
        Action a; a.id = "edit.testZone1.action"; a.name = "Zone 1 Action";
        a.description = "Action scoped to test zone 1";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "testZone1";
        a.callback = &Application::Action_Zone1;
        sm.RegisterAction(a, { sigKey(ImGuiKey_A) });
    }
    {
        Action a; a.id = "edit.testZone2.action"; a.name = "Zone 2 Action";
        a.description = "Action scoped to test zone 2";
        a.category = ActionCategory::Edit;
        a.requiredContext.editor = "testZone2";
        a.callback = &Application::Action_Zone2;
        sm.RegisterAction(a, { sigKey(ImGuiKey_A) });
    }

    // re-save once after registering everything so freshly added defaults
    // are persisted (Load happened before Register, so defaults are missing
    // from disk on first run).
    sm.Save();
}

} // namespace App

#include "Application.h"
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>

namespace App {

// NOTE (Ink rework, Lot 0): the document-editing shortcuts of the old stack
// (transforms G/R/S, edit mode, clipboard, selection families, add menu, …)
// were removed together with their actions; they return, re-designed, with the
// Ink editing loop (docs/Ink/ROADMAP.md Lot 8). What remains is the app shell:
// application/file/undo/view toggles, the two base tools, the editor-switch
// and tab-navigation shortcuts, and the dev-zone demo actions.
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
        a.description = "Start a new empty project";
        a.category = ActionCategory::File; a.callback = [this]{ Action_NewFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_N, true) });
    }
    {
        Action a; a.id = "file.open"; a.name = "Open Project";
        a.description = "Open an existing .acu project (disabled during the engine rework)";
        a.category = ActionCategory::File; a.callback = [this]{ Action_OpenFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_O, true) });
    }
    {
        Action a; a.id = "file.save"; a.name = "Save Project";
        a.description = "Save the current project (disabled during the engine rework)";
        a.category = ActionCategory::File; a.callback = [this]{ Action_SaveFile(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, true) });
    }
    {
        Action a; a.id = "file.saveAs"; a.name = "Save Project As";
        a.description = "Save the project to a new file (disabled during the engine rework)";
        a.category = ActionCategory::File; a.callback = [this]{ Action_SaveFileAs(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_S, /*ctrl=*/true, /*shift=*/true) });
    }

    // ── Edit: undo / redo (Preferences history; document history → Ink Lot 8) ─
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
    {
        Action a; a.id = "view.fitDocument"; a.name = "Fit Document in View";
        a.description = "Zoom/pan so the whole document is visible";
        a.category = ActionCategory::View;
        a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ViewFitDocument(); };
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
        Action a; a.id = "view.fitSelection"; a.name = "Frame Selected";
        a.description = "Zoom the view onto the selected/active object(s)";
        a.category = ActionCategory::View;
        a.callback = [this]{ Action_ViewFitSelection(); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_KeypadDecimal) });
    }

    // ── Tools (Lot 8: the editing loop) ───────────────────────────────────────
    // Shape and Curve are MULTI-TOOLS: one palette button hosting several
    // related creation tools (rectangle/ellipse/triangle/free — bézier/circles/
    // nurbs/poly). The active variant lives on Application (toolShapeKind_ /
    // toolCurveKind_); right-clicking the palette button picks the variant.
    tm.RegisterTool({"tool.select", "Select",    "select",         {"tool.select.activate"}});
    tm.RegisterTool({"tool.cursor", "2D Cursor", "crop-free",      {"tool.cursor.activate"}});
    tm.RegisterTool({"tool.shape",  "Shape",     "crop-landscape", {"tool.shape.activate"}});
    tm.RegisterTool({"tool.curve",  "Curve",     "bezier-curve",   {"tool.curve.activate"}});
    {
        Action a; a.id = "tool.select.activate"; a.name = "Activate Select";
        a.description = "Select, box-select and grab-move objects";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.select"); };
        sm.RegisterAction(a, { sigKey(ImGuiKey_W) });
    }
    {
        Action a; a.id = "tool.shape.activate"; a.name = "Activate Shape";
        a.description = "Draw shapes (rectangle / ellipse / triangle / free) "
                        "by dragging on the canvas";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.shape"); };
        sm.RegisterAction(a, {});
    }
    {
        Action a; a.id = "tool.curve.activate"; a.name = "Activate Curve";
        a.description = "Draw curves (Bézier / NURBS / poly and their circle "
                        "forms) on the canvas";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.curve"); };
        sm.RegisterAction(a, {});
    }
    {
        Action a; a.id = "tool.cursor.activate"; a.name = "Activate 2D Cursor";
        a.description = "Place the 2D cursor by clicking (2D cursor — later)";
        a.category = ActionCategory::Tool; a.requiredContext.editor = "viewport";
        a.callback = [this]{ Action_ActivateNamedTool("tool.cursor"); };
        sm.RegisterAction(a, {});
    }
    tm.SetActiveTool("tool.select");

    // ── Editing (Lot 8): selection, transforms, mode, add ────────────────────
    // Canvas-scoped so they never steal keys while a text field is focused.
    auto viewportKey = [&](const char* id, const char* name, const char* desc,
                           std::function<void()> cb,
                           std::vector<EventSignature> keys,
                           bool modal = false, bool repeat = false,
                           std::function<bool()> poll = {}) {
        Action a; a.id = id; a.name = name; a.description = desc;
        a.category = ActionCategory::Transform;
        a.requiredContext.editor = "viewport";
        a.callback = std::move(cb);
        a.pollFn = std::move(poll);
        a.isModal = modal; a.allowRepeat = repeat;
        sm.RegisterAction(a, std::move(keys));
    };
    // X and Y are shared: outside a modal op they delete / (unused);
    // during a modal op they constrain the axis. pollFn disambiguates so the
    // two never both fire on one keypress.
    // A modal op (object transform OR a mark G/R/S) captures X/Y as the axis
    // constraint; otherwise they are the idle Delete / (unused) actions.
    auto idle  = [this] { return !transformOp_.Active() && !markGrab_.Active(); };
    auto modal = [this] { return transformOp_.Active() || markGrab_.Active(); };

    viewportKey("edit.selectAll", "Select All", "Select every object",
                [this]{ Action_SelectAll(); }, { sigKey(ImGuiKey_A) }, false, false, idle);
    viewportKey("edit.deselectAll", "Deselect All", "Clear the selection",
                [this]{ Action_DeselectAll(); }, { sigKey(ImGuiKey_A, false, false, true) });
    viewportKey("edit.delete", "Delete", "Delete the selected objects",
                [this]{ Action_DeleteSelection(); },
                { sigKey(ImGuiKey_X), sigKey(ImGuiKey_Delete) }, false, false, idle);
    viewportKey("edit.duplicate", "Duplicate", "Duplicate the selected objects",
                [this]{ Action_DuplicateSelection(); }, { sigKey(ImGuiKey_D, true) },
                false, false, idle);
    viewportKey("edit.duplicateGrab", "Duplicate & Move",
                "Duplicate the selection and grab the copies",
                [this]{ Action_DuplicateGrab(); },
                { sigKey(ImGuiKey_D, false, true) }, false, false, idle);
    viewportKey("edit.duplicateLinked", "Duplicate Linked",
                "Instance the selected objects (shared data) and grab the copies",
                [this]{ Action_DuplicateLinked(); },
                { sigKey(ImGuiKey_D, false, false, true) }, false, false, idle);
    viewportKey("edit.grab", "Move", "Grab-move the selection",
                [this]{ Action_BeginMove(); }, { sigKey(ImGuiKey_G) }, true, false, idle);
    viewportKey("edit.rotate", "Rotate", "Rotate the selection",
                [this]{ Action_BeginRotate(); }, { sigKey(ImGuiKey_R) }, true, false, idle);
    viewportKey("edit.scale", "Scale", "Scale the selection",
                [this]{ Action_BeginScale(); }, { sigKey(ImGuiKey_S) }, true, false, idle);
    viewportKey("edit.axisX", "Constrain X", "Constrain the transform to X",
                [this]{ Action_ConstrainAxisX(); }, { sigKey(ImGuiKey_X) },
                false, false, modal);
    viewportKey("edit.axisY", "Constrain Y", "Constrain the transform to Y",
                [this]{ Action_ConstrainAxisY(); }, { sigKey(ImGuiKey_Y) },
                false, false, modal);
    // Line-Mark mode: V cycles the mark side; Ctrl+C / Ctrl+V copy/paste marks.
    // Gated on the mode being active AND idle (the actions no-op otherwise).
    auto markIdle = [this] {
        return MarkModeActive() && !transformOp_.Active() && !markGrab_.Active();
    };
    viewportKey("mark.cycleSide", "Cycle Mark Side",
                "Cycle the selected marks Center / Left / Right",
                [this]{ Action_CycleMarkSide(); }, { sigKey(ImGuiKey_V) },
                false, false, markIdle);
    viewportKey("mark.cyclePhase", "Cycle Mark Phase",
                "Cycle the selected marks Neutral / Dash / Gap",
                [this]{ Action_CycleMarkPhase(); }, { sigKey(ImGuiKey_P) },
                false, false, markIdle);
    viewportKey("mark.copy", "Copy Marks", "Copy the selected line marks",
                [this]{ Action_CopyMarks(); },
                { sigKey(ImGuiKey_C, /*ctrl=*/true) }, false, false, markIdle);
    viewportKey("mark.paste", "Paste Marks",
                "Paste the copied marks (a translucent preview follows the "
                "cursor; click to drop, right-click / Esc to cancel)",
                [this]{ Action_PasteMarks(); },
                { sigKey(ImGuiKey_V, /*ctrl=*/true) }, false, false, markIdle);
    viewportKey("edit.toggleMode", "Toggle Edit Mode",
                "Switch between Object and Edit mode",
                [this]{ Action_ToggleEditMode(); }, { sigKey(ImGuiKey_Tab) },
                false, false, idle);
    viewportKey("edit.toggleLineMark", "Toggle Line Mark Mode",
                "Switch between Object and Line Mark mode",
                [this]{ Action_ToggleLineMarkMode(); },
                { sigKey(ImGuiKey_Tab, false, true) }, false, false, idle);
    viewportKey("edit.applyScale", "Apply Scale",
                "Bake the selection's scale into its geometry",
                [this]{ Action_ApplyScale(); }, {});
    // V opens the handle menu in EDIT mode; the SAME key cycles the mark side
    // in Line-Mark mode (disambiguated by the mode poll so they never collide).
    viewportKey("edit.handleMenu", "Set Handle Type",
                "Open the vertex handle-type menu (Edit mode)",
                [this]{ Action_OpenHandleMenu(); }, { sigKey(ImGuiKey_V) },
                false, false, [this]{
                    return edit_.mode == EditorMode::Edit &&
                           !transformOp_.Active() && !markGrab_.Active();
                });
    viewportKey("edit.addMenu", "Add", "Open the Add menu at the cursor",
                [this]{ Action_OpenAddMenu(); }, { sigKey(ImGuiKey_A, false, true) },
                false, false, idle);
    viewportKey("view.snapMenu", "Snap",
                "Open the snap pie (move the selection / 2D cursor to targets)",
                [this]{ Action_OpenSnapMenu(); },
                { sigKey(ImGuiKey_S, false, true) }, false, false, idle);
    viewportKey("edit.parentToActive", "Parent to Active",
                "Parent the other selected objects to the active one",
                [this]{ Action_ParentToActive(); }, { sigKey(ImGuiKey_P, true) },
                false, false, idle);
    viewportKey("edit.clearParent", "Clear Parent",
                "Detach the selection from its object parent (keeps world position)",
                [this]{
                    OutlinerUnparent(std::vector<Ink::NodeId>(
                        edit_.selection.begin(), edit_.selection.end()));
                }, { sigKey(ImGuiKey_P, false, false, true) }, false, false, idle);

    // Group / ungroup — global (fire from the Viewport OR the Outliner), gated
    // to the idle state so they never fire mid modal transform.
    {
        Action a; a.id = "edit.group"; a.name = "Group";
        a.description = "Wrap the selected objects in a new group";
        a.category = ActionCategory::Edit;
        a.callback = [this]{ Action_GroupSelection(); };
        a.pollFn = idle;
        sm.RegisterAction(a, { sigKey(ImGuiKey_G, /*ctrl=*/true) });
    }
    {
        Action a; a.id = "edit.ungroup"; a.name = "Ungroup";
        a.description = "Dissolve the selected group(s)";
        a.category = ActionCategory::Edit;
        a.callback = [this]{ Action_UngroupSelection(); };
        a.pollFn = idle;
        sm.RegisterAction(a, { sigKey(ImGuiKey_G, /*ctrl=*/true, /*shift=*/false, /*alt=*/true) });
    }

    // ── Editor switch shortcuts (Blender-style) ──────────────────────────────
    // Switch the editor kind of the zone under the mouse. No requiredContext:
    // they fire over any zone, targeting the hovered leaf.
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

    // Re-save once after registering everything so freshly added defaults are
    // persisted (Load happened before Register, so defaults are missing from
    // disk on first run).
    sm.Save();
}

} // namespace App

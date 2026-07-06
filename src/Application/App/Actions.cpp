#include "Application.h"
#include <Shortcuts/ToolManager.h>
#include <iostream>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Generic (engine-independent) actions. The document/editing actions of the
//  old stack live in src/_legacy/Application/App/ApplicationActions.cpp and
//  are re-designed with the Ink engine (docs/Ink/ROADMAP.md Lot 8).
// ─────────────────────────────────────────────────────────────────────────────

void Application::Action_Zone1()    { std::cout << "[ACTION] Zone 1"    << std::endl; }
void Application::Action_Zone2()    { std::cout << "[ACTION] Zone 2"    << std::endl; }
void Application::Action_ThemePreviewCycle() {
    std::cout << "[ACTION] Theme Preview Cycle" << std::endl;
}

void Application::Action_Quit() {
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
    // Only flip the desired state here (this runs inside the main ImGui frame);
    // the actual Show()/Hide() is reconciled in ProcessEvents().
    showSettings_ = !showSettings_;
}

void Application::Action_ToggleTokenGraph() {
    // Same focus-vs-toggle dance as Settings.
    if (showTokenGraph_ && tokenGraphHost_.IsOpen() && !tokenGraphHost_.HasInputFocus()) {
        tokenGraphHost_.RequestFocus();
        return;
    }
    showTokenGraph_ = !showTokenGraph_;
}

void Application::Action_ToggleImGuiDemo() {
    showImGuiDemo_ = !showImGuiDemo_;
}

void Application::Action_ActivateNamedTool(const std::string& toolId) {
    // Transitional: just switch the ToolManager's active tool. Gesture
    // cancellation / mode handling returns with the Ink editing loop (Lot 8).
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool(toolId);
}

// Per-leaf view requests. The placeholder Viewport ignores them; the Ink
// Viewport (Lot 1) consumes them to drive its camera.
void Application::Action_ViewFitDocument() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqFitDoc = true;
}

void Application::Action_ViewFitSelection() {
    if (EditorState* st = zoneLayout_.HoveredEditorState()) {
        st->reqFitSelection = true;
        st->outliner.reqScrollToActive = true;   // Outliner leaves act on this
    }
}

void Application::Action_ViewResetOrigin() {
    if (EditorState* st = zoneLayout_.HoveredEditorState())
        st->reqResetOrigin = true;
}

} // namespace App

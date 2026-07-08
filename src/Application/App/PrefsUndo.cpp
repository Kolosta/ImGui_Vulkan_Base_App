#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <sstream>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Undo / Redo.
//
//  The MAIN (document) history of the old stack was snapshot-based on the old
//  Renderer::Document and is quarantined under src/_legacy/. Ink brings
//  command-based document undo with ROADMAP Lot 8. Until then, undo in the
//  main window is a no-op.
//
//  The PREFERENCES window keeps its own fully working history: each step is
//  the design-system OVERRIDES (Customisation / Theme / Accessibility edits)
//  serialised via the OverrideManager. Blender-style: the two histories never
//  cross — Action_Undo/Redo route on activeUndoTarget_, set by whichever
//  window is dispatching shortcuts.
// ─────────────────────────────────────────────────────────────────────────────

void Application::Action_Undo() {
    if (activeUndoTarget_ == UndoTarget::Preferences) {
        if (!prefsUndo_.CanUndo()) return;
        std::string lbl = prefsUndo_.Undo();
        prefsUndoLast_ = CapturePrefsOverrides();   // resync the change detector
        LogInfoAction("Undo (" + lbl + ")");
        return;
    }
    // Main window: command-based document undo (Ink engine, Lot 8).
    if (!project_.document || !docUndo_.CanUndo()) return;
    if (transformOp_.Active()) CancelTransform();
    const std::string lbl = docUndo_.Undo(*project_.document);
    edit_.Prune(*project_.document);
    project_.dirty = true;
    LogInfoAction("Undo (" + lbl + ")");
}

void Application::Action_Redo() {
    if (activeUndoTarget_ == UndoTarget::Preferences) {
        if (!prefsUndo_.CanRedo()) return;
        std::string lbl = prefsUndo_.Redo();
        prefsUndoLast_ = CapturePrefsOverrides();
        LogInfoAction("Redo (" + lbl + ")");
        return;
    }
    // Main window: command-based document redo (Ink engine, Lot 8).
    if (!project_.document || !docUndo_.CanRedo()) return;
    const std::string lbl = docUndo_.Redo(*project_.document);
    edit_.Prune(*project_.document);
    project_.dirty = true;
    LogInfoAction("Redo (" + lbl + ")");
}

std::string Application::CapturePrefsOverrides() const {
    std::ostringstream os(std::ios::binary);
    DesignSystem::DesignSystem::Instance().GetOverrideManager().WriteToBinary(os);
    return os.str();
}

void Application::RestorePrefsOverrides(const std::string& blob) {
    auto& mgr = DesignSystem::DesignSystem::Instance().GetOverrideManager();
    mgr.Clear();
    std::istringstream is(blob, std::ios::binary);
    mgr.ReadFromBinary(is);
    // Persist + re-apply the global style so the restored overrides take effect.
    DesignSystem::DesignSystem::Instance().NotifyOverrideChange();
}

void Application::InitPrefsUndo() {
    prefsUndo_.Configure(
        /*capture*/ [this] { return CapturePrefsOverrides(); },
        /*restore*/ [this](const std::string& blob) { RestorePrefsOverrides(blob); });
    prefsUndo_.SetCapacity(undoBufferSteps_);
    prefsUndo_.Reset();
    prefsUndoLast_ = CapturePrefsOverrides();
    prefsUndoInited_ = true;
}

// Called at the end of the Preferences window frame: if the overrides changed
// and no mouse button is held (not mid slider-drag), push one undo step.
void Application::CommitPrefsUndoIfChanged() {
    if (!prefsUndoInited_) InitPrefsUndo();
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2]) return;
    std::string now = CapturePrefsOverrides();
    if (now != prefsUndoLast_) {
        prefsUndo_.Commit("Preferences edit");
        prefsUndoLast_ = std::move(now);
    }
}

} // namespace App

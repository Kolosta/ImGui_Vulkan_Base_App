#pragma once

#include <Shortcuts/ShortcutManager.h>
#include <imgui.h>
#include <string>
#include <map>

namespace UI {

class ShortcutEditor {
public:
    ShortcutEditor();

    /** Standalone window. */
    void Render(bool* p_open = nullptr);

    /** Embedded inside another window (e.g. Settings -> Shortcuts). */
    void RenderContent();

    /** No-op kept for API compatibility (was the popup renderer). */
    void RenderCapturePopup() {}

private:
    void RenderToolbar();
    void RenderTreePane();
    void RenderDetailPane();
    void RenderConflictsList();
    void RenderAdvancedEditorFor(const std::string& actionId,
                                 Shortcuts::EventSignature& sig,
                                 int bindingIndex);

    void DrawTreeForCategory(Shortcuts::ActionCategory cat);
    void DrawActionLeaf(const Shortcuts::Action* action);

    // selection
    std::string selectedActionId_;

    // filters
    char  searchBuf_[256];
    bool  showConflicts_       = false;
    bool  showOnlyOverridden_  = false;

    // advanced editor target binding (-1 = none / -2 = pending new binding)
    int   advancedBindingIndex_ = -1;
    Shortcuts::EventSignature advancedDraft_;

    // ── Pending edits & rejection state ─────────────────────────────────
    // For each (actionId|index) we may keep:
    //   pendingDraft_ : the user's in-progress signature, even when the
    //                   resolver refuses to commit it (dangerous binding).
    //   pendingError_ : the human-readable reason from IsDangerousBinding()
    //                   so the row keeps showing the error until the user
    //                   makes the binding safe again.
    struct PendingEdit {
        Shortcuts::EventSignature draft;
        std::string error;
        bool active = false;
    };
    std::map<std::string, PendingEdit> pendingEdits_;
    std::string PendingKey(const std::string& actionId, int index) const {
        return actionId + "|" + std::to_string(index);
    }

    // tree expansion state for ImGui (key by category)
    bool categoryOpen_[10] = { true, true, true, true, true, true, true, true, true, true };
};

} // namespace UI

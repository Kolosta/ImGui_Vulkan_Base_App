#pragma once

#include <Shortcuts/ShortcutManager.h>
#include <imgui.h>
#include <string>

namespace UI {

class ShortcutEditor {
public:
    ShortcutEditor();

    // Fenêtre autonome (conservée pour usage standalone éventuel).
    void Render(bool* p_open = nullptr);

    // Contenu seul — utilisé à l'intérieur de la fenêtre Paramètres.
    void RenderContent();

    // Popup de capture — doit être appelé après le End() de la fenêtre parente.
    void RenderCapturePopup();

private:
    void RenderActionList();
    void RenderActionDetails();
    void RenderConflictDetection();
    void RenderShortcutCapture();

    void StartCapture(const std::string& actionId, int bindingIndex);
    void StopCapture();
    bool CaptureInput(Shortcuts::KeyCombination& outKey);

    void RenderShortcutButton(const std::string& label,
                              const Shortcuts::KeyCombination& key,
                              const std::string& actionId,
                              int bindingIndex);

    std::string selectedActionId_;
    Shortcuts::ShortcutZone filterZone_;
    bool showConflicts_;
    char searchBuffer_[256];

    bool capturing_;
    std::string capturingActionId_;
    int capturingBindingIndex_;
    Shortcuts::KeyCombination capturedKey_;
    bool keyCaptured_;
};

} // namespace UI
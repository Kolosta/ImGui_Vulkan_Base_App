#pragma once

#include <Shortcuts/ShortcutManager.h>
#include <imgui.h>
#include <string>

namespace UI {

/**
 * Éditeur de shortcuts avec capture correcte et gestion des conflits
 */
class ShortcutEditor {
public:
    ShortcutEditor();
    
    void Render(bool* p_open = nullptr);
    
private:
    void RenderActionList();
    void RenderActionDetails();
    void RenderConflictDetection();
    void RenderShortcutCapture();
    
    void StartCapture(const std::string& actionId, int bindingIndex);
    void StopCapture();
    bool CaptureInput(Shortcuts::KeyCombination& outKey);
    
    void RenderShortcutButton(const std::string& label, const Shortcuts::KeyCombination& key, 
                             const std::string& actionId, int bindingIndex);
    
    // UI State
    std::string selectedActionId_;
    Shortcuts::ShortcutZone filterZone_;
    bool showConflicts_;
    char searchBuffer_[256];
    
    // Capture state
    bool capturing_;
    std::string capturingActionId_;
    int capturingBindingIndex_;
    Shortcuts::KeyCombination capturedKey_;
    bool keyCaptured_;
};

} // namespace UI
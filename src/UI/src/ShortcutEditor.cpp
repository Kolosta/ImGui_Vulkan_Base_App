// #include "ShortcutEditor.h"
// #include <algorithm>
// #include <cstring>

// namespace DesignSystem {

// ShortcutEditor::ShortcutEditor()
//     : filterZone_(ShortcutZone::Global),
//       showConflicts_(false),
//       capturing_(false),
//       capturingBindingIndex_(-1) {
//     searchBuffer_[0] = '\0';
// }

// void ShortcutEditor::Render(bool* p_open) {
//     ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
//     if (!ImGui::Begin("Shortcut Editor", p_open)) {
//         ImGui::End();
//         return;
//     }
    
//     // Toolbar
//     if (ImGui::BeginMenuBar()) {
//         if (ImGui::BeginMenu("View")) {
//             ImGui::MenuItem("Show Conflicts", nullptr, &showConflicts_);
//             ImGui::EndMenu();
//         }
//         ImGui::EndMenuBar();
//     }
    
//     // Filter
//     ImGui::Text("Filter by Zone:");
//     ImGui::SameLine();
//     if (ImGui::BeginCombo("##zone", ShortcutZoneToString(filterZone_).c_str())) {
//         for (int i = 0; i <= (int)ShortcutZone::ShortcutEditor; i++) {
//             ShortcutZone zone = (ShortcutZone)i;
//             bool selected = (filterZone_ == zone);
//             if (ImGui::Selectable(ShortcutZoneToString(zone).c_str(), selected)) {
//                 filterZone_ = zone;
//             }
//         }
//         ImGui::EndCombo();
//     }
    
//     ImGui::SameLine();
//     ImGui::InputTextWithHint("##search", "Search actions...", searchBuffer_, sizeof(searchBuffer_));
    
//     ImGui::Separator();
    
//     // Split view
//     ImGui::BeginChild("ActionListPane", ImVec2(300, 0), true);
//     RenderActionList();
//     ImGui::EndChild();
    
//     ImGui::SameLine();
    
//     ImGui::BeginChild("ActionDetailsPane", ImVec2(0, 0), true);
//     if (!selectedActionId_.empty()) {
//         RenderActionDetails();
//     } else {
//         ImGui::TextWrapped("Select an action to view and edit shortcuts");
//     }
//     ImGui::EndChild();
    
//     // Conflict detection panel
//     if (showConflicts_) {
//         ImGui::Separator();
//         RenderConflictDetection();
//     }
    
//     // Shortcut capture popup
//     if (capturing_) {
//         RenderShortcutCapture();
//     }
    
//     ImGui::End();
// }

// void ShortcutEditor::RenderActionList() {
//     auto& manager = ShortcutManager::Instance();
    
//     ImGui::Text("Actions");
//     ImGui::Separator();
    
//     // Filter actions par zone
//     auto actions = manager.GetActionsForZone(filterZone_);
    
//     // Ajouter aussi les actions globales si on n'est pas sur Global
//     if (filterZone_ != ShortcutZone::Global) {
//         auto globalActions = manager.GetActionsForZone(ShortcutZone::Global);
//         actions.insert(actions.end(), globalActions.begin(), globalActions.end());
//     }
    
//     for (const Action* action : actions) {
//         // Search filter
//         if (searchBuffer_[0] != '\0') {
//             std::string searchLower = searchBuffer_;
//             std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            
//             std::string nameLower = action->name;
//             std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
//             if (nameLower.find(searchLower) == std::string::npos) {
//                 continue;
//             }
//         }
        
//         bool selected = (selectedActionId_ == action->id);
        
//         std::string label = action->name;
//         if (action->zone == ShortcutZone::Global) {
//             label += " [Global]";
//         }
        
//         if (ImGui::Selectable(label.c_str(), selected)) {
//             selectedActionId_ = action->id;
//         }
//     }
// }

// void ShortcutEditor::RenderActionDetails() {
//     auto& manager = ShortcutManager::Instance();
    
//     const Action* action = manager.GetAction(selectedActionId_);
//     if (!action) {
//         ImGui::Text("Action not found");
//         return;
//     }
    
//     ImGui::Text("Action: %s", action->name.c_str());
//     ImGui::Text("Zone: %s", ShortcutZoneToString(action->zone).c_str());
//     ImGui::Text("Priority Override: %s", action->allowPriorityOverride ? "Yes" : "No");
    
//     ImGui::Separator();
    
//     const ShortcutBinding* binding = manager.GetBinding(selectedActionId_);
//     if (!binding) {
//         ImGui::Text("No binding found");
//         return;
//     }
    
//     ImGui::Text("Shortcuts:");
    
//     // Afficher shortcuts existants
//     for (size_t i = 0; i < binding->keys.size(); i++) {
//         ImGui::PushID(i);
        
//         const KeyCombination& key = binding->keys[i];
//         std::string keyStr = key.ToString();
        
//         RenderShortcutButton(keyStr.c_str(), key, selectedActionId_, i);
        
//         ImGui::SameLine();
//         if (ImGui::SmallButton("Remove")) {
//             manager.RemoveBinding(selectedActionId_, key);
//         }
        
//         // Vérifier conflit
//         if (manager.HasConflict(selectedActionId_, key)) {
//             ImGui::SameLine();
//             ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONFLICT!");
//         }
        
//         ImGui::PopID();
//     }
    
//     // Ajouter nouveau shortcut
//     if (ImGui::Button("Add New Shortcut")) {
//         StartCapture(selectedActionId_, -1);
//     }
    
//     ImGui::SameLine();
//     if (ImGui::Button("Restore Defaults")) {
//         manager.RestoreDefaultBindings(selectedActionId_);
//     }
// }

// void ShortcutEditor::RenderConflictDetection() {
//     auto& manager = ShortcutManager::Instance();
//     auto conflicts = manager.DetectConflicts();
    
//     ImGui::Text("Detected Conflicts: %zu", conflicts.size());
//     ImGui::Separator();
    
//     if (conflicts.empty()) {
//         ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No conflicts detected!");
//         return;
//     }
    
//     for (const auto& conflict : conflicts) {
//         ImGui::PushID(&conflict);
        
//         ImVec4 color = conflict.isResolvable ? 
//             ImVec4(1.0f, 0.65f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        
//         ImGui::TextColored(color, "%s", conflict.combination.ToString().c_str());
//         ImGui::SameLine();
//         ImGui::Text("- %s [%s] vs %s [%s]", 
//             conflict.actionId1.c_str(),
//             ShortcutZoneToString(conflict.zone1).c_str(),
//             conflict.actionId2.c_str(),
//             ShortcutZoneToString(conflict.zone2).c_str());
        
//         if (conflict.isResolvable) {
//             ImGui::SameLine();
//             ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Resolvable)");
//         }
        
//         ImGui::PopID();
//     }
// }

// void ShortcutEditor::RenderShortcutCapture() {
//     ImGui::OpenPopup("Capture Shortcut");
    
//     ImVec2 center = ImGui::GetMainViewport()->GetCenter();
//     ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
//     if (ImGui::BeginPopupModal("Capture Shortcut", nullptr, 
//         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        
//         ImGui::Text("Press a key combination...");
//         ImGui::Separator();
        
//         KeyCombination captured = CaptureInput();
        
//         if (captured.key != ImGuiKey_None) {
//             ImGui::Text("Captured: %s", captured.ToString().c_str());
            
//             auto& manager = ShortcutManager::Instance();
            
//             // Vérifier conflit
//             bool hasConflict = manager.HasConflict(capturingActionId_, captured);
//             if (hasConflict) {
//                 ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
//                     "Warning: This shortcut conflicts with another action!");
//             }
            
//             if (ImGui::Button("Accept")) {
//                 if (capturingBindingIndex_ == -1) {
//                     // Nouveau shortcut
//                     manager.AddBinding(capturingActionId_, captured);
//                 } else {
//                     // Modifier shortcut existant
//                     auto binding = manager.GetBinding(capturingActionId_);
//                     if (binding && capturingBindingIndex_ < (int)binding->keys.size()) {
//                         auto keys = binding->keys;
//                         keys[capturingBindingIndex_] = captured;
//                         manager.SetBinding(capturingActionId_, keys);
//                     }
//                 }
//                 StopCapture();
//                 ImGui::CloseCurrentPopup();
//             }
            
//             ImGui::SameLine();
//         }
        
//         if (ImGui::Button("Cancel")) {
//             StopCapture();
//             ImGui::CloseCurrentPopup();
//         }
        
//         ImGui::EndPopup();
//     }
// }

// void ShortcutEditor::StartCapture(const std::string& actionId, int bindingIndex) {
//     capturing_ = true;
//     capturingActionId_ = actionId;
//     capturingBindingIndex_ = bindingIndex;
//     capturedKey_ = KeyCombination();
// }

// void ShortcutEditor::StopCapture() {
//     capturing_ = false;
//     capturingActionId_ = "";
//     capturingBindingIndex_ = -1;
//     capturedKey_ = KeyCombination();
// }

// KeyCombination ShortcutEditor::CaptureInput() {
//     ImGuiIO& io = ImGui::GetIO();
    
//     // Capturer modifiers
//     bool ctrl = io.KeyCtrl;
//     bool shift = io.KeyShift;
//     bool alt = io.KeyAlt;
    
//     // Capturer key
//     for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key = (ImGuiKey)(key + 1)) {
//         // Ignorer les modifiers eux-mêmes
//         if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
//             key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
//             key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) {
//             continue;
//         }
        
//         if (ImGui::IsKeyPressed(key)) {
//             return KeyCombination(key, ctrl, shift, alt);
//         }
//     }
    
//     return KeyCombination();
// }

// void ShortcutEditor::RenderShortcutButton(const std::string& label, const KeyCombination& key,
//                                          const std::string& actionId, int bindingIndex) {
//     if (ImGui::Button(label.c_str())) {
//         StartCapture(actionId, bindingIndex);
//     }
// }

// } // namespace DesignSystem




#include <UI/ShortcutEditor.h>
#include <algorithm>
#include <cstring>

namespace UI {

ShortcutEditor::ShortcutEditor()
    : filterZone_(Shortcuts::ShortcutZone::Global),
      showConflicts_(false),
      capturing_(false),
      capturingBindingIndex_(-1),
      keyCaptured_(false) {
    searchBuffer_[0] = '\0';
}

void ShortcutEditor::Render(bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Shortcut Editor", p_open)) {
        ImGui::End();
        return;
    }
    
    // Enregistrer la zone de cette fenêtre
    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Shortcut Editor", Shortcuts::ShortcutZone::ShortcutEditor);
    
    // Toolbar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Conflicts", nullptr, &showConflicts_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    
    // Filter
    ImGui::Text("Filter by Zone:");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##zone", Shortcuts::ShortcutZoneToString(filterZone_).c_str())) {
        for (int i = 0; i <= static_cast<int>(Shortcuts::ShortcutZone::ShortcutEditor); i++) {
            Shortcuts::ShortcutZone zone = static_cast<Shortcuts::ShortcutZone>(i);
            bool selected = (filterZone_ == zone);
            if (ImGui::Selectable(Shortcuts::ShortcutZoneToString(zone).c_str(), selected)) {
                filterZone_ = zone;
            }
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    ImGui::InputTextWithHint("##search", "Search actions...", searchBuffer_, sizeof(searchBuffer_));
    
    ImGui::Separator();
    
    // Split view
    ImGui::BeginChild("ActionListPane", ImVec2(300, 0), true);
    RenderActionList();
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    ImGui::BeginChild("ActionDetailsPane", ImVec2(0, 0), true);
    if (!selectedActionId_.empty()) {
        RenderActionDetails();
    } else {
        ImGui::TextWrapped("Select an action to view and edit shortcuts");
    }
    ImGui::EndChild();
    
    // Conflict detection panel
    if (showConflicts_) {
        ImGui::Separator();
        RenderConflictDetection();
    }
    
    ImGui::End();
    
    // Shortcut capture popup (rendu en dehors de la fenêtre principale)
    if (capturing_) {
        RenderShortcutCapture();
    }
}

void ShortcutEditor::RenderActionList() {
    auto& manager = Shortcuts::ShortcutManager::Instance();
    
    ImGui::Text("Actions");
    ImGui::Separator();
    
    auto actions = manager.GetActionsForZone(filterZone_);
    
    if (filterZone_ != Shortcuts::ShortcutZone::Global) {
        auto globalActions = manager.GetActionsForZone(Shortcuts::ShortcutZone::Global);
        actions.insert(actions.end(), globalActions.begin(), globalActions.end());
    }
    
    for (const Shortcuts::Action* action : actions) {
        if (searchBuffer_[0] != '\0') {
            std::string searchLower = searchBuffer_;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            
            std::string nameLower = action->name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        
        bool selected = (selectedActionId_ == action->id);
        
        std::string label = action->name;
        if (action->zone == Shortcuts::ShortcutZone::Global) {
            label += " [Global]";
        }
        
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectedActionId_ = action->id;
        }
    }
}

void ShortcutEditor::RenderActionDetails() {
    auto& manager = Shortcuts::ShortcutManager::Instance();
    
    const Shortcuts::Action* action = manager.GetAction(selectedActionId_);
    if (!action) {
        ImGui::Text("Action not found");
        return;
    }
    
    ImGui::Text("Action: %s", action->name.c_str());
    ImGui::Text("Zone: %s", Shortcuts::ShortcutZoneToString(action->zone).c_str());
    ImGui::Text("Priority Override: %s", action->allowPriorityOverride ? "Yes" : "No");
    
    ImGui::Separator();
    
    const Shortcuts::ShortcutBinding* binding = manager.GetBinding(selectedActionId_);
    if (!binding) {
        ImGui::Text("No binding found");
        return;
    }
    
    ImGui::Text("Shortcuts:");
    
    for (size_t i = 0; i < binding->keys.size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        
        const Shortcuts::KeyCombination& key = binding->keys[i];
        std::string keyStr = key.ToString();
        
        RenderShortcutButton(keyStr.c_str(), key, selectedActionId_, static_cast<int>(i));
        
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            manager.RemoveBinding(selectedActionId_, key);
        }
        
        if (manager.HasConflict(selectedActionId_, key)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONFLICT!");
        }
        
        ImGui::PopID();
    }
    
    if (ImGui::Button("Add New Shortcut")) {
        StartCapture(selectedActionId_, -1);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Restore Defaults")) {
        manager.RestoreDefaultBindings(selectedActionId_);
    }
}

void ShortcutEditor::RenderConflictDetection() {
    auto& manager = Shortcuts::ShortcutManager::Instance();
    auto conflicts = manager.DetectConflicts();
    
    ImGui::Text("Detected Conflicts: %zu", conflicts.size());
    ImGui::Separator();
    
    if (conflicts.empty()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "No conflicts detected!");
        return;
    }
    
    for (const auto& conflict : conflicts) {
        ImGui::PushID(&conflict);
        
        ImVec4 color = conflict.isResolvable ? 
            ImVec4(1.0f, 0.65f, 0.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        
        ImGui::TextColored(color, "%s", conflict.combination.ToString().c_str());
        ImGui::SameLine();
        ImGui::Text("- %s [%s] vs %s [%s]", 
            conflict.actionId1.c_str(),
            Shortcuts::ShortcutZoneToString(conflict.zone1).c_str(),
            conflict.actionId2.c_str(),
            Shortcuts::ShortcutZoneToString(conflict.zone2).c_str());
        
        if (conflict.isResolvable) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "(Resolvable)");
        }
        
        ImGui::PopID();
    }
}

void ShortcutEditor::RenderShortcutCapture() {
    ImGui::OpenPopup("Capture Shortcut");
    
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Capture Shortcut", nullptr, 
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        
        ImGui::Text("Press a key combination...");
        ImGui::Text("(Press ESC to cancel)");
        ImGui::Separator();
        
        Shortcuts::KeyCombination newKey;
        bool captured = CaptureInput(newKey);
        
        if (keyCaptured_) {
            ImGui::Text("Captured: %s", capturedKey_.ToString().c_str());
            
            auto& manager = Shortcuts::ShortcutManager::Instance();
            bool hasConflict = manager.HasConflict(capturingActionId_, capturedKey_);
            
            if (hasConflict) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                    "Warning: This shortcut conflicts with another action!");
            }
            
            if (ImGui::Button("Accept", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                if (capturingBindingIndex_ == -1) {
                    manager.AddBinding(capturingActionId_, capturedKey_);
                } else {
                    auto binding = manager.GetBinding(capturingActionId_);
                    if (binding && capturingBindingIndex_ < static_cast<int>(binding->keys.size())) {
                        auto keys = binding->keys;
                        keys[capturingBindingIndex_] = capturedKey_;
                        manager.SetBinding(capturingActionId_, keys);
                    }
                }
                StopCapture();
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
        }
        
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            StopCapture();
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    } else if (!ImGui::IsPopupOpen("Capture Shortcut")) {
        StopCapture();
    }
}

void ShortcutEditor::StartCapture(const std::string& actionId, int bindingIndex) {
    capturing_ = true;
    capturingActionId_ = actionId;
    capturingBindingIndex_ = bindingIndex;
    capturedKey_ = Shortcuts::KeyCombination();
    keyCaptured_ = false;
}

void ShortcutEditor::StopCapture() {
    capturing_ = false;
    capturingActionId_ = "";
    capturingBindingIndex_ = -1;
    capturedKey_ = Shortcuts::KeyCombination();
    keyCaptured_ = false;
}

// bool ShortcutEditor::CaptureInput(Shortcuts::KeyCombination& outKey) {
//     ImGuiIO& io = ImGui::GetIO();
    
//     // Si déjà capturé, ne pas recapturer
//     if (keyCaptured_) {
//         outKey = capturedKey_;
//         return false;
//     }
    
//     // Capturer modifiers
//     bool ctrl = io.KeyCtrl;
//     bool shift = io.KeyShift;
//     bool alt = io.KeyAlt;
    
//     // Chercher une touche pressée
//     for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key = static_cast<ImGuiKey>(key + 1)) {
//         // Ignorer les modifiers
//         if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
//             key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
//             key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
//             key == ImGuiKey_ModCtrl || key == ImGuiKey_ModShift || key == ImGuiKey_ModAlt) {
//             continue;
//         }
        
//         // Ignorer Escape (utilisé pour annuler)
//         if (key == ImGuiKey_Escape) {
//             continue;
//         }
        
//         if (ImGui::IsKeyPressed(key, false)) {
//             Shortcuts::KeyCombination combo(key, ctrl, shift, alt);
            
//             if (combo.IsValid()) {
//                 capturedKey_ = combo;
//                 keyCaptured_ = true;
//                 outKey = combo;
//                 return true;
//             }
//         }
//     }
    
//     return false;
// }
bool ShortcutEditor::CaptureInput(Shortcuts::KeyCombination& outKey)
{
    ImGuiIO& io = ImGui::GetIO();

    // Si déjà capturé, ne pas recapturer
    if (keyCaptured_) {
        outKey = capturedKey_;
        return false;
    }

    // États des modifiers (API officielle)
    bool ctrl  = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt   = io.KeyAlt;

    // Parcourir toutes les touches nommées
    for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN;
         key < ImGuiKey_NamedKey_END;
         key = static_cast<ImGuiKey>(key + 1))
    {
        // Ignorer les modifiers physiques
        if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
            key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
            key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
        {
            continue;
        }

        // Escape réservé à l'annulation
        if (key == ImGuiKey_Escape)
            continue;

        if (ImGui::IsKeyPressed(key, false))
        {
            Shortcuts::KeyCombination combo(key, ctrl, shift, alt);

            if (combo.IsValid())
            {
                capturedKey_ = combo;
                keyCaptured_ = true;
                outKey = combo;
                return true;
            }
        }
    }

    return false;
}


void ShortcutEditor::RenderShortcutButton(const std::string& label, const Shortcuts::KeyCombination& key,
                                         const std::string& actionId, int bindingIndex) {
    if (ImGui::Button(label.c_str())) {
        StartCapture(actionId, bindingIndex);
    }
}

} // namespace UI
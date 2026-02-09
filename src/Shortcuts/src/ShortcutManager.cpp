// #include "ShortcutManager.h"
// #include <fstream>
// #include <iostream>
// #include <sstream>
// #include <algorithm>

// namespace DesignSystem {

// std::string KeyCombination::ToString() const {
//     if (key == ImGuiKey_None) return "";
    
//     std::string result;
//     if (ctrl) result += "Ctrl+";
//     if (shift) result += "Shift+";
//     if (alt) result += "Alt+";
    
//     // Conversion ImGuiKey vers string
//     const char* keyName = ImGui::GetKeyName(key);
//     if (keyName) {
//         result += keyName;
//     }
    
//     return result;
// }

// bool KeyCombination::IsPressed() const {
//     if (key == ImGuiKey_None) return false;
    
//     ImGuiIO& io = ImGui::GetIO();
    
//     bool ctrlMatch = ctrl == (io.KeyCtrl);
//     bool shiftMatch = shift == (io.KeyShift);
//     bool altMatch = alt == (io.KeyAlt);
    
//     return ctrlMatch && shiftMatch && altMatch && ImGui::IsKeyPressed(key);
// }

// ShortcutManager& ShortcutManager::Instance() {
//     static ShortcutManager* instance = new ShortcutManager();
//     return *instance;
// }

// void ShortcutManager::RegisterAction(const Action& action) {
//     // actions_[action.id] = action;
//     actions_.emplace(action.id, action);
    
//     // Créer binding vide si n'existe pas
//     if (bindings_.find(action.id) == bindings_.end()) {
//         bindings_[action.id] = ShortcutBinding(action.id, {});
//     }
// }

// void ShortcutManager::UnregisterAction(const std::string& actionId) {
//     actions_.erase(actionId);
//     bindings_.erase(actionId);
// }

// void ShortcutManager::SetBinding(const std::string& actionId, const std::vector<KeyCombination>& keys) {
//     if (bindings_.find(actionId) != bindings_.end()) {
//         bindings_[actionId].keys = keys;
//     } else {
//         bindings_[actionId] = ShortcutBinding(actionId, keys);
//     }
// }

// void ShortcutManager::AddBinding(const std::string& actionId, const KeyCombination& key) {
//     auto& binding = bindings_[actionId];
    
//     // Vérifier que la combinaison n'existe pas déjà
//     if (std::find(binding.keys.begin(), binding.keys.end(), key) == binding.keys.end()) {
//         binding.keys.push_back(key);
//     }
// }

// void ShortcutManager::RemoveBinding(const std::string& actionId, const KeyCombination& key) {
//     auto it = bindings_.find(actionId);
//     if (it != bindings_.end()) {
//         auto& keys = it->second.keys;
//         keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
//     }
// }

// void ShortcutManager::RestoreDefaultBindings(const std::string& actionId) {
//     auto it = bindings_.find(actionId);
//     if (it != bindings_.end()) {
//         it->second.RestoreDefaults();
//     }
// }

// bool ShortcutManager::ZoneConflicts(ShortcutZone z1, ShortcutZone z2) const {
//     // Global conflicte avec tout
//     if (z1 == ShortcutZone::Global || z2 == ShortcutZone::Global) {
//         return true;
//     }
    
//     // Même zone = conflit
//     if (z1 == z2) return true;
    
//     // Zones différentes = pas de conflit (sauf si parent/enfant)
//     return IsParentZone(z1, z2) || IsParentZone(z2, z1);
// }

// bool ShortcutManager::IsParentZone(ShortcutZone parent, ShortcutZone child) const {
//     // Global est parent de tout
//     if (parent == ShortcutZone::Global) return true;
    
//     // Pas d'autre hiérarchie pour l'instant
//     return false;
// }

// std::vector<ShortcutConflict> ShortcutManager::DetectConflicts() const {
//     std::vector<ShortcutConflict> conflicts;
    
//     // Pour chaque binding
//     for (const auto& [actionId1, binding1] : bindings_) {
//         auto action1It = actions_.find(actionId1);
//         if (action1It == actions_.end()) continue;
//         const Action& action1 = action1It->second;
        
//         for (const auto& key1 : binding1.keys) {
//             // Comparer avec tous les autres bindings
//             for (const auto& [actionId2, binding2] : bindings_) {
//                 if (actionId1 >= actionId2) continue; // Éviter doublons
                
//                 auto action2It = actions_.find(actionId2);
//                 if (action2It == actions_.end()) continue;
//                 const Action& action2 = action2It->second;
                
//                 for (const auto& key2 : binding2.keys) {
//                     if (key1 == key2) {
//                         // Même combinaison de touches
//                         // bool conflicts = ZoneConflicts(action1.zone, action2.zone);
                        
//                         // // Vérifier si résolvable par priorité
//                         // bool resolvable = !conflicts || 
//                         //                 action1.allowPriorityOverride || 
//                         //                 action2.allowPriorityOverride;

//                         bool hasConflict = ZoneConflicts(action1.zone, action2.zone);

//                         bool resolvable = !hasConflict ||
//                                         action1.allowPriorityOverride ||
//                                         action2.allowPriorityOverride;

//                         conflicts.push_back(ShortcutConflict{
//                             key1,
//                             actionId1,
//                             actionId2,
//                             action1.zone,
//                             action2.zone,
//                             resolvable
//                         });

//                     }
//                 }
//             }
//         }
//     }
    
//     return conflicts;
// }

// bool ShortcutManager::HasConflict(const std::string& actionId, const KeyCombination& key) const {
//     auto actionIt = actions_.find(actionId);
//     if (actionIt == actions_.end()) return false;
//     const Action& action = actionIt->second;
    
//     // Vérifier tous les autres bindings
//     for (const auto& [otherId, binding] : bindings_) {
//         if (otherId == actionId) continue;
        
//         auto otherActionIt = actions_.find(otherId);
//         if (otherActionIt == actions_.end()) continue;
//         const Action& otherAction = otherActionIt->second;
        
//         for (const auto& otherKey : binding.keys) {
//             if (otherKey == key) {
//                 // Conflit si zones se chevauchent
//                 bool conflicts = ZoneConflicts(action.zone, otherAction.zone);
                
//                 // Résolvable si override autorisé
//                 if (conflicts && !action.allowPriorityOverride && !otherAction.allowPriorityOverride) {
//                     return true;
//                 }
//             }
//         }
//     }
    
//     return false;
// }

// void ShortcutManager::ProcessInput(ShortcutZone currentZone) {
//     // Collecter toutes les actions activables dans cette zone
//     std::vector<const Action*> activeActions;
    
//     for (const auto& [actionId, action] : actions_) {
//         // Global toujours actif
//         if (action.zone == ShortcutZone::Global) {
//             activeActions.push_back(&action);
//         }
//         // Zone spécifique
//         else if (action.zone == currentZone) {
//             activeActions.push_back(&action);
//         }
//     }
    
//     // Trier par priorité (zone spécifique > global)
//     std::sort(activeActions.begin(), activeActions.end(), 
//         [](const Action* a, const Action* b) {
//             if (a->zone == ShortcutZone::Global && b->zone != ShortcutZone::Global) return false;
//             if (a->zone != ShortcutZone::Global && b->zone == ShortcutZone::Global) return true;
//             return false;
//         });
    
//     // Tester les shortcuts
//     for (const Action* action : activeActions) {
//         auto bindingIt = bindings_.find(action->id);
//         if (bindingIt == bindings_.end()) continue;
        
//         for (const auto& key : bindingIt->second.keys) {
//             if (key.IsPressed()) {
//                 // Exécuter callback
//                 if (action->callback) {
//                     action->callback();
//                 }
//                 return; // Une seule action par frame
//             }
//         }
//     }
// }

// const Action* ShortcutManager::GetAction(const std::string& actionId) const {
//     auto it = actions_.find(actionId);
//     return (it != actions_.end()) ? &it->second : nullptr;
// }

// const ShortcutBinding* ShortcutManager::GetBinding(const std::string& actionId) const {
//     auto it = bindings_.find(actionId);
//     return (it != bindings_.end()) ? &it->second : nullptr;
// }

// std::vector<const Action*> ShortcutManager::GetActionsForZone(ShortcutZone zone) const {
//     std::vector<const Action*> result;
//     for (const auto& [id, action] : actions_) {
//         if (action.zone == zone) {
//             result.push_back(&action);
//         }
//     }
//     return result;
// }

// std::string ShortcutManager::GetShortcutString(const std::string& actionId) const {
//     auto binding = GetBinding(actionId);
//     if (binding && !binding->keys.empty()) {
//         return binding->keys[0].ToString();
//     }
//     return "";
// }

// void ShortcutManager::SaveBindings() {
//     // TODO: Implémenter sauvegarde binaire
//     std::cout << "Saving shortcuts..." << std::endl;
// }

// void ShortcutManager::LoadBindings() {
//     // TODO: Implémenter chargement binaire
//     std::cout << "Loading shortcuts..." << std::endl;
// }

// } // namespace DesignSystem



#include <Shortcuts/ShortcutManager.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace Shortcuts {

// ========== KeyCombination ==========

std::string KeyCombination::ToString() const {
    if (key == ImGuiKey_None) return "";
    
    std::string result;
    if (ctrl) result += "Ctrl+";
    if (shift) result += "Shift+";
    if (alt) result += "Alt+";
    
    const char* keyName = ImGui::GetKeyName(key);
    if (keyName) {
        result += keyName;
    }
    
    return result;
}

bool KeyCombination::IsPressed() const {
    if (key == ImGuiKey_None) return false;
    
    ImGuiIO& io = ImGui::GetIO();
    
    bool ctrlMatch = ctrl == io.KeyCtrl;
    bool shiftMatch = shift == io.KeyShift;
    bool altMatch = alt == io.KeyAlt;
    
    return ctrlMatch && shiftMatch && altMatch && ImGui::IsKeyPressed(key, false);
}

bool KeyCombination::IsValid() const {
    if (key == ImGuiKey_None) return false;
    
    // Touches de modification seules ne sont pas valides
    if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
        key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
        key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) {
        return false;
    }
    
    return true;
}

// ========== ShortcutManager ==========

ShortcutManager& ShortcutManager::Instance() {
    static ShortcutManager* instance = new ShortcutManager();
    return *instance;
}

void ShortcutManager::Initialize() {
    LoadBindings();
}

void ShortcutManager::Shutdown() {
    SaveBindings();
}

void ShortcutManager::RegisterAction(const Action& action) {
    actions_.emplace(action.id, action);
    
    if (bindings_.find(action.id) == bindings_.end()) {
        bindings_[action.id] = ShortcutBinding(action.id, {});
    }
}

void ShortcutManager::UnregisterAction(const std::string& actionId) {
    actions_.erase(actionId);
    bindings_.erase(actionId);
}

void ShortcutManager::SetBinding(const std::string& actionId, const std::vector<KeyCombination>& keys) {
    if (bindings_.find(actionId) != bindings_.end()) {
        bindings_[actionId].keys = keys;
    } else {
        bindings_[actionId] = ShortcutBinding(actionId, keys);
    }
    SaveBindings();
}

void ShortcutManager::AddBinding(const std::string& actionId, const KeyCombination& key) {
    auto& binding = bindings_[actionId];
    
    if (std::find(binding.keys.begin(), binding.keys.end(), key) == binding.keys.end()) {
        binding.keys.push_back(key);
        SaveBindings();
    }
}

void ShortcutManager::RemoveBinding(const std::string& actionId, const KeyCombination& key) {
    auto it = bindings_.find(actionId);
    if (it != bindings_.end()) {
        auto& keys = it->second.keys;
        keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
        SaveBindings();
    }
}

void ShortcutManager::RestoreDefaultBindings(const std::string& actionId) {
    auto it = bindings_.find(actionId);
    if (it != bindings_.end()) {
        it->second.RestoreDefaults();
        SaveBindings();
    }
}

bool ShortcutManager::ZoneConflicts(ShortcutZone z1, ShortcutZone z2) const {
    if (z1 == ShortcutZone::Global || z2 == ShortcutZone::Global) {
        return true;
    }
    
    if (z1 == z2) return true;
    
    return IsParentZone(z1, z2) || IsParentZone(z2, z1);
}

bool ShortcutManager::IsParentZone(ShortcutZone parent, ShortcutZone child) const {
    if (parent == ShortcutZone::Global) return true;
    return false;
}

std::vector<ShortcutConflict> ShortcutManager::DetectConflicts() const {
    std::vector<ShortcutConflict> conflicts;
    
    for (const auto& [actionId1, binding1] : bindings_) {
        auto action1It = actions_.find(actionId1);
        if (action1It == actions_.end()) continue;
        const Action& action1 = action1It->second;
        
        for (const auto& key1 : binding1.keys) {
            for (const auto& [actionId2, binding2] : bindings_) {
                if (actionId1 >= actionId2) continue;
                
                auto action2It = actions_.find(actionId2);
                if (action2It == actions_.end()) continue;
                const Action& action2 = action2It->second;
                
                for (const auto& key2 : binding2.keys) {
                    if (key1 == key2) {
                        bool hasConflict = ZoneConflicts(action1.zone, action2.zone);
                        bool resolvable = !hasConflict ||
                                        action1.allowPriorityOverride ||
                                        action2.allowPriorityOverride;
                        
                        conflicts.push_back(ShortcutConflict{
                            key1,
                            actionId1,
                            actionId2,
                            action1.zone,
                            action2.zone,
                            resolvable
                        });
                    }
                }
            }
        }
    }
    
    return conflicts;
}

bool ShortcutManager::HasConflict(const std::string& actionId, const KeyCombination& key) const {
    auto actionIt = actions_.find(actionId);
    if (actionIt == actions_.end()) return false;
    const Action& action = actionIt->second;
    
    for (const auto& [otherId, binding] : bindings_) {
        if (otherId == actionId) continue;
        
        auto otherActionIt = actions_.find(otherId);
        if (otherActionIt == actions_.end()) continue;
        const Action& otherAction = otherActionIt->second;
        
        for (const auto& otherKey : binding.keys) {
            if (otherKey == key) {
                bool conflicts = ZoneConflicts(action.zone, otherAction.zone);
                
                if (conflicts && !action.allowPriorityOverride && !otherAction.allowPriorityOverride) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

// void ShortcutManager::RegisterWindowZone(const char* windowName, ShortcutZone zone) {
//     windowZones_[windowName] = zone;
// }

// ShortcutZone ShortcutManager::DetectCurrentZone() const {
//     ImGuiContext* ctx = ImGui::GetCurrentContext();
//     if (!ctx) return ShortcutZone::Global;
    
//     // Obtenir la fenêtre sous la souris
//     ImGuiWindow* window = ImGui::FindHoveredWindow();
//     if (!window) return ShortcutZone::Global;
    
//     // Vérifier si la fenêtre a une zone enregistrée
//     auto it = windowZones_.find(window->Name);
//     if (it != windowZones_.end()) {
//         return it->second;
//     }
    
//     return ShortcutZone::Global;
// }

void ShortcutManager::RegisterWindowZone(const char* windowName, ShortcutZone zone)
{
    windowZones_[windowName] = zone;

    // Si cette fenêtre est hover, elle devient la zone active
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        currentZone_ = zone;
    }
}


// void ShortcutManager::ProcessInput() {
//     // ShortcutZone currentZone = DetectCurrentZone();
//     ShortcutZone currentZone = currentZone_;

    
//     // Collecter les actions activables
//     std::vector<const Action*> activeActions;
    
//     for (const auto& [actionId, action] : actions_) {
//         if (action.zone == ShortcutZone::Global) {
//             activeActions.push_back(&action);
//         } else if (action.zone == currentZone) {
//             activeActions.push_back(&action);
//         }
//     }
    
//     // Trier par priorité (zone spécifique > global)
//     std::sort(activeActions.begin(), activeActions.end(), 
//         [](const Action* a, const Action* b) {
//             if (a->zone == ShortcutZone::Global && b->zone != ShortcutZone::Global) return false;
//             if (a->zone != ShortcutZone::Global && b->zone == ShortcutZone::Global) return true;
//             return false;
//         });
    
//     // Tester les shortcuts
//     for (const Action* action : activeActions) {
//         auto bindingIt = bindings_.find(action->id);
//         if (bindingIt == bindings_.end()) continue;
        
//         for (const auto& key : bindingIt->second.keys) {
//             if (key.IsPressed()) {
//                 if (action->callback) {
//                     action->callback();
//                 }
//                 return;
//             }
//         }
//     }
// }
void ShortcutManager::ProcessInput()
{
    ShortcutZone currentZone = currentZone_;

    std::vector<const Action*> activeActions;

    for (const auto& [actionId, action] : actions_)
    {
        if (action.zone == ShortcutZone::Global || action.zone == currentZone)
        {
            activeActions.push_back(&action);
        }
    }

    std::sort(activeActions.begin(), activeActions.end(),
        [](const Action* a, const Action* b)
        {
            if (a->zone == ShortcutZone::Global && b->zone != ShortcutZone::Global) return false;
            if (a->zone != ShortcutZone::Global && b->zone == ShortcutZone::Global) return true;
            return false;
        });

    for (const Action* action : activeActions)
    {
        auto it = bindings_.find(action->id);
        if (it == bindings_.end()) continue;

        for (const auto& key : it->second.keys)
        {
            if (key.IsPressed())
            {
                if (action->callback)
                    action->callback();
                return;
            }
        }
    }
}


const Action* ShortcutManager::GetAction(const std::string& actionId) const {
    auto it = actions_.find(actionId);
    return (it != actions_.end()) ? &it->second : nullptr;
}

const ShortcutBinding* ShortcutManager::GetBinding(const std::string& actionId) const {
    auto it = bindings_.find(actionId);
    return (it != bindings_.end()) ? &it->second : nullptr;
}

std::vector<const Action*> ShortcutManager::GetActionsForZone(ShortcutZone zone) const {
    std::vector<const Action*> result;
    for (const auto& [id, action] : actions_) {
        if (action.zone == zone) {
            result.push_back(&action);
        }
    }
    return result;
}

std::string ShortcutManager::GetShortcutString(const std::string& actionId) const {
    auto binding = GetBinding(actionId);
    if (binding && !binding->keys.empty()) {
        return binding->keys[0].ToString();
    }
    return "";
}

// ========== Persistance ==========

void ShortcutManager::SaveBindings() {
    std::ofstream out("shortcuts.dat", std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to save shortcuts" << std::endl;
        return;
    }
    
    // Magic number
    uint32_t magic = 0x53484354; // "SHCT"
    out.write(reinterpret_cast<const char*>(&magic), sizeof(uint32_t));
    
    // Nombre de bindings
    uint32_t count = static_cast<uint32_t>(bindings_.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
    
    // Écrire chaque binding
    for (const auto& [actionId, binding] : bindings_) {
        // Action ID
        uint32_t idLen = static_cast<uint32_t>(actionId.length());
        out.write(reinterpret_cast<const char*>(&idLen), sizeof(uint32_t));
        out.write(actionId.data(), idLen);
        
        // Nombre de touches
        uint32_t keyCount = static_cast<uint32_t>(binding.keys.size());
        out.write(reinterpret_cast<const char*>(&keyCount), sizeof(uint32_t));
        
        // Chaque touche
        for (const auto& key : binding.keys) {
            int32_t keyVal = static_cast<int32_t>(key.key);
            out.write(reinterpret_cast<const char*>(&keyVal), sizeof(int32_t));
            out.write(reinterpret_cast<const char*>(&key.ctrl), sizeof(bool));
            out.write(reinterpret_cast<const char*>(&key.shift), sizeof(bool));
            out.write(reinterpret_cast<const char*>(&key.alt), sizeof(bool));
        }
    }
    
    out.close();
}

void ShortcutManager::LoadBindings() {
    std::ifstream in("shortcuts.dat", std::ios::binary);
    if (!in.is_open()) {
        return; // Fichier n'existe pas encore
    }
    
    // Vérifier magic number
    uint32_t magic;
    in.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    if (magic != 0x53484354) {
        in.close();
        return;
    }
    
    // Nombre de bindings
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    
    for (uint32_t i = 0; i < count; i++) {
        // Action ID
        uint32_t idLen;
        in.read(reinterpret_cast<char*>(&idLen), sizeof(uint32_t));
        std::string actionId(idLen, '\0');
        in.read(&actionId[0], idLen);
        
        // Nombre de touches
        uint32_t keyCount;
        in.read(reinterpret_cast<char*>(&keyCount), sizeof(uint32_t));
        
        std::vector<KeyCombination> keys;
        for (uint32_t j = 0; j < keyCount; j++) {
            int32_t keyVal;
            bool ctrl, shift, alt;
            in.read(reinterpret_cast<char*>(&keyVal), sizeof(int32_t));
            in.read(reinterpret_cast<char*>(&ctrl), sizeof(bool));
            in.read(reinterpret_cast<char*>(&shift), sizeof(bool));
            in.read(reinterpret_cast<char*>(&alt), sizeof(bool));
            
            keys.emplace_back(static_cast<ImGuiKey>(keyVal), ctrl, shift, alt);
        }
        
        // Mettre à jour seulement si l'action existe
        if (bindings_.find(actionId) != bindings_.end()) {
            bindings_[actionId].keys = keys;
        }
    }
    
    in.close();
}

} // namespace Shortcuts
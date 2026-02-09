// #pragma once

// #include <string>
// #include <vector>
// #include <map>
// #include <set>
// #include <functional>
// #include <optional>
// #include <imgui.h>

// namespace DesignSystem {

// /**
//  * Zone d'application pour les shortcuts
//  * Hiérarchie: Global > Zone spécifique
//  */
// enum class ShortcutZone {
//     Global,           // Partout dans l'application
//     Toolbar,          // Barre d'outils
//     DesignExample,    // Zone Design System Example
//     ThemePreview,     // Zone Theme Preview
//     TestZone1,        // Zone de test 1
//     TestZone2,        // Zone de test 2
//     TokenEditor,      // Token Editor window
//     ShortcutEditor    // Shortcut Editor window
// };

// inline std::string ShortcutZoneToString(ShortcutZone zone) {
//     switch (zone) {
//         case ShortcutZone::Global: return "Global";
//         case ShortcutZone::Toolbar: return "Toolbar";
//         case ShortcutZone::DesignExample: return "Design Example";
//         case ShortcutZone::ThemePreview: return "Theme Preview";
//         case ShortcutZone::TestZone1: return "Test Zone 1";
//         case ShortcutZone::TestZone2: return "Test Zone 2";
//         case ShortcutZone::TokenEditor: return "Token Editor";
//         case ShortcutZone::ShortcutEditor: return "Shortcut Editor";
//         default: return "Unknown";
//     }
// }

// /**
//  * Représente une combinaison de touches
//  */
// struct KeyCombination {
//     ImGuiKey key = ImGuiKey_None;
//     bool ctrl = false;
//     bool shift = false;
//     bool alt = false;
    
//     KeyCombination() = default;
//     KeyCombination(ImGuiKey k, bool c = false, bool s = false, bool a = false)
//         : key(k), ctrl(c), shift(s), alt(a) {}
    
//     bool operator==(const KeyCombination& other) const {
//         return key == other.key && ctrl == other.ctrl && 
//                shift == other.shift && alt == other.alt;
//     }
    
//     bool operator<(const KeyCombination& other) const {
//         if (key != other.key) return key < other.key;
//         if (ctrl != other.ctrl) return ctrl < other.ctrl;
//         if (shift != other.shift) return shift < other.shift;
//         return alt < other.alt;
//     }
    
//     std::string ToString() const;
//     bool IsPressed() const;
// };

// /**
//  * Action exécutable par un shortcut
//  */
// struct Action {
//     std::string id;                         // Identifiant unique
//     std::string name;                       // Nom affiché
//     ShortcutZone zone;                      // Zone d'application
//     bool allowPriorityOverride = false;     // Permet override prioritaire
//     std::function<void()> callback;         // Fonction à exécuter
    
//     Action(const std::string& id, const std::string& name, ShortcutZone zone,
//            std::function<void()> cb, bool allowOverride = false)
//         : id(id), name(name), zone(zone), callback(cb), allowPriorityOverride(allowOverride) {}
// };

// /**
//  * Shortcut bindings pour une action
//  */
// struct ShortcutBinding {
//     std::string actionId;
//     std::vector<KeyCombination> keys;        // Plusieurs shortcuts possibles
//     std::vector<KeyCombination> defaultKeys; // Shortcuts par défaut
    
//     ShortcutBinding() = default;
//     ShortcutBinding(const std::string& id, const std::vector<KeyCombination>& k)
//         : actionId(id), keys(k), defaultKeys(k) {}
        
//     void RestoreDefaults() { keys = defaultKeys; }
// };

// /**
//  * Conflit de shortcut détecté
//  */
// struct ShortcutConflict {
//     KeyCombination combination;
//     std::string actionId1;
//     std::string actionId2;
//     ShortcutZone zone1;
//     ShortcutZone zone2;
//     bool isResolvable;  // True si zones différentes et non-parentes
// };

// /**
//  * Gestionnaire centralisé des shortcuts
//  */
// class ShortcutManager {
// public:
//     static ShortcutManager& Instance();
    
//     /**
//      * Enregistrement d'actions
//      */
//     void RegisterAction(const Action& action);
//     void UnregisterAction(const std::string& actionId);
    
//     /**
//      * Gestion des bindings
//      */
//     void SetBinding(const std::string& actionId, const std::vector<KeyCombination>& keys);
//     void AddBinding(const std::string& actionId, const KeyCombination& key);
//     void RemoveBinding(const std::string& actionId, const KeyCombination& key);
//     void RestoreDefaultBindings(const std::string& actionId);
    
//     /**
//      * Détection de conflits
//      */
//     std::vector<ShortcutConflict> DetectConflicts() const;
//     bool HasConflict(const std::string& actionId, const KeyCombination& key) const;
    
//     /**
//      * Traitement des inputs (à appeler chaque frame)
//      */
//     void ProcessInput(ShortcutZone currentZone);
    
//     /**
//      * Récupération d'informations
//      */
//     const Action* GetAction(const std::string& actionId) const;
//     const ShortcutBinding* GetBinding(const std::string& actionId) const;
//     std::vector<const Action*> GetActionsForZone(ShortcutZone zone) const;
//     std::string GetShortcutString(const std::string& actionId) const; // Premier shortcut
    
//     /**
//      * Sauvegarde/Chargement
//      */
//     void SaveBindings();
//     void LoadBindings();
    
// private:
//     ShortcutManager() = default;
    
//     bool ZoneConflicts(ShortcutZone z1, ShortcutZone z2) const;
//     bool IsParentZone(ShortcutZone parent, ShortcutZone child) const;
    
//     std::map<std::string, Action> actions_;
//     std::map<std::string, ShortcutBinding> bindings_;
// };

// } // namespace DesignSystem



#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <optional>
#include <imgui.h>

namespace Shortcuts {

/**
 * Zone d'application pour les shortcuts
 * Déterminée par la position de la souris, pas par la fenêtre active
 */
enum class ShortcutZone {
    Global,           // Partout dans l'application
    Toolbar,          // Barre d'outils
    DesignExample,    // Zone Design System Example
    ThemePreview,     // Zone Theme Preview
    TestZone1,        // Zone de test 1
    TestZone2,        // Zone de test 2
    TokenEditor,      // Token Editor window
    ShortcutEditor    // Shortcut Editor window
};

inline std::string ShortcutZoneToString(ShortcutZone zone) {
    switch (zone) {
        case ShortcutZone::Global: return "Global";
        case ShortcutZone::Toolbar: return "Toolbar";
        case ShortcutZone::DesignExample: return "Design Example";
        case ShortcutZone::ThemePreview: return "Theme Preview";
        case ShortcutZone::TestZone1: return "Test Zone 1";
        case ShortcutZone::TestZone2: return "Test Zone 2";
        case ShortcutZone::TokenEditor: return "Token Editor";
        case ShortcutZone::ShortcutEditor: return "Shortcut Editor";
        default: return "Unknown";
    }
}

/**
 * Représente une combinaison de touches
 */
struct KeyCombination {
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    
    KeyCombination() = default;
    KeyCombination(ImGuiKey k, bool c = false, bool s = false, bool a = false)
        : key(k), ctrl(c), shift(s), alt(a) {}
    
    bool operator==(const KeyCombination& other) const {
        return key == other.key && ctrl == other.ctrl && 
               shift == other.shift && alt == other.alt;
    }
    
    bool operator<(const KeyCombination& other) const {
        if (key != other.key) return key < other.key;
        if (ctrl != other.ctrl) return ctrl < other.ctrl;
        if (shift != other.shift) return shift < other.shift;
        return alt < other.alt;
    }
    
    std::string ToString() const;
    bool IsPressed() const;
    bool IsValid() const;
};

/**
 * Action exécutable par un shortcut
 */
struct Action {
    std::string id;
    std::string name;
    ShortcutZone zone;
    bool allowPriorityOverride = false;
    std::function<void()> callback;
    
    Action(const std::string& id, const std::string& name, ShortcutZone zone,
           std::function<void()> cb, bool allowOverride = false)
        : id(id), name(name), zone(zone), callback(cb), allowPriorityOverride(allowOverride) {}
};

/**
 * Shortcut bindings pour une action
 */
struct ShortcutBinding {
    std::string actionId;
    std::vector<KeyCombination> keys;
    std::vector<KeyCombination> defaultKeys;
    
    ShortcutBinding() = default;
    ShortcutBinding(const std::string& id, const std::vector<KeyCombination>& k)
        : actionId(id), keys(k), defaultKeys(k) {}
        
    void RestoreDefaults() { keys = defaultKeys; }
};

/**
 * Conflit de shortcut détecté
 */
struct ShortcutConflict {
    KeyCombination combination;
    std::string actionId1;
    std::string actionId2;
    ShortcutZone zone1;
    ShortcutZone zone2;
    bool isResolvable;
};

/**
 * Gestionnaire centralisé des shortcuts avec persistance
 */
class ShortcutManager {
public:
    static ShortcutManager& Instance();
    
    /**
     * Initialisation et persistence
     */
    void Initialize();
    void Shutdown();
    
    /**
     * Enregistrement d'actions
     */
    void RegisterAction(const Action& action);
    void UnregisterAction(const std::string& actionId);
    
    /**
     * Gestion des bindings
     */
    void SetBinding(const std::string& actionId, const std::vector<KeyCombination>& keys);
    void AddBinding(const std::string& actionId, const KeyCombination& key);
    void RemoveBinding(const std::string& actionId, const KeyCombination& key);
    void RestoreDefaultBindings(const std::string& actionId);
    
    /**
     * Détection de conflits
     */
    std::vector<ShortcutConflict> DetectConflicts() const;
    bool HasConflict(const std::string& actionId, const KeyCombination& key) const;
    
    /**
     * Traitement des inputs (à appeler chaque frame)
     * Détecte automatiquement la zone basée sur la position de la souris
     */
    void ProcessInput();
    
    /**
     * Enregistrer la zone d'une fenêtre ImGui
     * Appeler dans ImGui::Begin() de chaque fenêtre
     */
    void RegisterWindowZone(const char* windowName, ShortcutZone zone);
    
    /**
     * Récupération d'informations
     */
    const Action* GetAction(const std::string& actionId) const;
    const ShortcutBinding* GetBinding(const std::string& actionId) const;
    std::vector<const Action*> GetActionsForZone(ShortcutZone zone) const;
    std::string GetShortcutString(const std::string& actionId) const;
    
private:
    ShortcutManager() = default;
    ShortcutZone currentZone_ = ShortcutZone::Global;
    
    bool ZoneConflicts(ShortcutZone z1, ShortcutZone z2) const;
    bool IsParentZone(ShortcutZone parent, ShortcutZone child) const;
    
    /**
     * Détection de zone basée sur la souris
     */
    ShortcutZone DetectCurrentZone() const;
    
    /**
     * Persistance
     */
    void SaveBindings();
    void LoadBindings();
    
    std::map<std::string, Action> actions_;
    std::map<std::string, ShortcutBinding> bindings_;
    std::map<std::string, ShortcutZone> windowZones_;
};

} // namespace Shortcuts
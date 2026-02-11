#pragma once

#include <imgui.h>
#include <string>
#include <map>

namespace UI {

/**
 * Gestionnaire de polices pour ImGui
 * Intégré avec le Design System
 */
class FontManager {
public:
    static FontManager& Instance();
    
    /**
     * Initialiser avec les polices par défaut
     * Doit être appelé avant ImGui::NewFrame()
     */
    void Initialize();
    
    /**
     * Charger une police depuis un fichier
     * @param id Identifiant unique
     * @param filepath Chemin vers le fichier .ttf/.otf
     * @param size Taille de base de la police
     */
    bool LoadFont(const std::string& id, const std::string& filepath, float size = 14.0f);
    
    /**
     * Définir la police par défaut
     */
    void SetDefaultFont(const std::string& id);
    
    /**
     * Obtenir une police par son ID
     */
    ImFont* GetFont(const std::string& id) const;
    
    /**
     * Obtenir la police par défaut
     */
    ImFont* GetDefaultFont() const;
    
    /**
     * Push/Pop de police
     */
    void PushFont(const std::string& id);
    void PopFont();

private:
    FontManager() = default;
    
    std::map<std::string, ImFont*> fonts_;
    std::string defaultFontId_ = "default";
};

} // namespace UI
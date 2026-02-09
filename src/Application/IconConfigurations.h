#pragma once

#include <UI/IconManager.h>

/**
 * Configuration centralisée de toutes les icônes
 * Organisé par catégorie pour faciliter la maintenance
 * 
 * Structure :
 * - Chaque fonction retourne une IconMetadata configurée
 * - ApplyAllConfigs() applique toutes les configurations
 * 
 * Pour ajouter une nouvelle icône :
 * 1. Créer une fonction Get[NomIcone]Config()
 * 2. Configurer scheme et colorZones
 * 3. Ajouter l'appel dans ApplyAllConfigs()
 */
namespace IconConfigurations {

// ========================================
// TOOLS
// ========================================

inline UI::IconMetadata GetTool1Config() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Bicolor;
    
    // tool1.svg a 2 zones de couleur
    // Zone 0: Orange (#ffa814) → Secondary
    // Zone 1: Noir (#000000) → Primary
    
    // Note : Les zones sont auto-détectées lors du chargement
    // On configure juste usePrimary pour chaque zone
    // Les colorZones seront remplies automatiquement
    
    return meta;
}

inline UI::IconMetadata GetTool2Config() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Bicolor;
    return meta;
}

// ========================================
// UI ELEMENTS
// ========================================

inline UI::IconMetadata GetSettingsConfig() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Bicolor;
    return meta;
}

// ========================================
// BRAND / LOGOS
// ========================================

inline UI::IconMetadata GetLogoCartoConfig() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Original;  // Garde les couleurs d'origine
    return meta;
}

inline UI::IconMetadata GetThreeBallsConfig() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Original;  // Garde les couleurs d'origine
    return meta;
}

inline UI::IconMetadata GetMangaIconConfig() {
    UI::IconMetadata meta;
    meta.scheme = UI::IconColorScheme::Original;  // Contient un dégradé, non éditable
    return meta;
}

// ========================================
// APPLICATION DE TOUTES LES CONFIGS
// ========================================

/**
 * Applique toutes les configurations aux icônes chargées
 * À appeler après LoadIconsFromDirectory()
 */
inline void ApplyAllConfigs(UI::IconManager& iconManager) {
    // ===== TOOLS =====
    {
        auto meta = GetTool1Config();
        auto* actualMeta = iconManager.GetIconMetadata("tool1");
        if (actualMeta && actualMeta->colorZones.size() >= 2) {
            meta.colorZones = actualMeta->colorZones;
            // Zone 0 (orange) = Secondary
            if (meta.colorZones.size() > 0) meta.colorZones[0].usePrimary = false;
            // Zone 1 (noir) = Primary
            if (meta.colorZones.size() > 1) meta.colorZones[1].usePrimary = true;
        }
        iconManager.SetIconMetadata("tool1", meta);
    }
    
    {
        auto meta = GetTool2Config();
        auto* actualMeta = iconManager.GetIconMetadata("tool2");
        if (actualMeta) {
            meta.colorZones = actualMeta->colorZones;
            // Première couleur = Primary, reste = Secondary
            bool first = true;
            for (auto& zone : meta.colorZones) {
                zone.usePrimary = first;
                first = false;
            }
        }
        iconManager.SetIconMetadata("tool2", meta);
    }
    
    // ===== UI ELEMENTS =====
    {
        auto meta = GetSettingsConfig();
        auto* actualMeta = iconManager.GetIconMetadata("settings");
        if (actualMeta) {
            meta.colorZones = actualMeta->colorZones;
            // Première couleur = Primary, reste = Secondary
            bool first = true;
            for (auto& zone : meta.colorZones) {
                zone.usePrimary = first;
                first = false;
            }
        }
        iconManager.SetIconMetadata("settings", meta);
    }
    
    // ===== BRAND / LOGOS =====
    // Ces icônes gardent leurs couleurs originales
    iconManager.SetIconMetadata("logo_carto", GetLogoCartoConfig());
    iconManager.SetIconMetadata("three_balls", GetThreeBallsConfig());
    iconManager.SetIconMetadata("manga_icon", GetMangaIconConfig());
}

} // namespace IconConfigurations
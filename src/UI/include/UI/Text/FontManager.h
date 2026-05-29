#pragma once

#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>

namespace UI {

// ─────────────────────────────────────────────────────────────────────────────
//  FontManager — font discovery, loading and runtime control (ImGui 1.92 +
//  FreeType backend).
//
//  Design goals driven by the design system:
//   • DISCOVERY: scan resources/fonts recursively, group files into families
//     (e.g. "NotoSans", "Qanelas") with their per-weight static faces and any
//     variable face (…VariableFont_wght…). No hardcoded font list.
//   • FALLBACK: a symbol font (NotoSansSymbols2) is merged onto every loaded
//     face so glyphs missing from the primary face (arrows, symbols…) still
//     render instead of showing the “missing glyph” box. Works for any font.
//   • VARIABLE FONTS: faces exposing an `fvar` table can be rasterized at an
//     arbitrary weight/width via FreeType; non-variable families fall back to
//     the nearest static weight face.
//   • TOKENS: the application no longer hardcodes the default font; the design
//     system supplies family/size/weight/line-height through tokens, and this
//     manager resolves a concrete ImFont for a (family, weight) request.
// ─────────────────────────────────────────────────────────────────────────────

// One discovered face file on disk.
struct FontFace {
    std::string filepath;
    int         weight = 400;   // parsed from the style name (Regular=400…)
    bool        italic = false;
    bool        variable = false;  // has an fvar table (variable font)
    // Variable-axis ranges (valid when variable). Min/Default/Max design coords.
    float       wghtMin = 400, wghtDef = 400, wghtMax = 400;
    bool        hasWghtAxis = false;
};

// A family groups all faces sharing a typeface name (e.g. "NotoSans").
struct FontFamily {
    std::string name;
    std::vector<FontFace> faces;             // all on-disk faces
    bool HasVariable() const;
    // Pick the file whose static weight is closest to `weight` (upright unless
    // italic requested). Returns empty if the family has no face.
    const FontFace* PickFace(int weight, bool italic) const;
    // First variable face matching `italic` (relaxes to any variable face if
    // no italic-matching one exists). Null if the family has no variable face.
    const FontFace* VariableFace(bool italic) const;
};

class FontManager {
public:
    static FontManager& Instance();

    // dpiScale: SDL_GetDisplayContentScale() — initial atlas size hint.
    void Initialize(float dpiScale = 1.0f);

    // Recursively scan a directory for .ttf/.otf and build the family registry.
    // Does NOT load any glyphs yet. Returns the number of families found.
    int  DiscoverFonts(const std::string& rootDir);

    // Designate which discovered family supplies the glyph-fallback face
    // (merged onto every loaded font). Pass the family name and the substring
    // that identifies the fallback file (e.g. "Symbols2"). Safe if absent.
    void SetFallbackFamily(const std::string& familyName);

    // Resolve (or lazily create) an ImFont for (family, weight, italic) at the
    // current base size. For variable families the weight is applied as a
    // design-coordinate; for static families the nearest weight face is used.
    // The glyph-fallback face is merged automatically.
    ImFont* GetFont(const std::string& family, int weight = 400, bool italic = false);

    // The font ImGui uses when none is pushed. Set from a design-system token.
    void    SetDefaultFont(const std::string& family, int weight = 400);
    ImFont* GetDefaultFont() const;

    // Map a font-family *role index* (matching the design system's
    // primitive.font.family.* order: 0=sans, 1=serif, 2=mono, 3=cjk,
    // 4=cjk-serif) to a discovered family name. The app assigns these from
    // discovery (auto-heuristic) and the user can override them; tokens then
    // select a role and this resolves it to a concrete family.
    void        SetRoleFamily(int roleIndex, const std::string& familyName);
    std::string RoleFamily(int roleIndex) const;
    // Auto-assign roles from the discovered families using name heuristics
    // (mono/code → mono, cjk → cjk, the rest → sans/serif). Existing explicit
    // assignments are preserved.
    void AutoAssignRoles();

    // Push/pop a resolved font for a scope of widgets.
    void PushFont(const std::string& family, int weight = 400, bool italic = false);
    void PopFont();

    // Introspection for the editor UI (list families, weights, variable ranges).
    std::vector<std::string> FamilyNames() const;
    const FontFamily* Family(const std::string& name) const;

    // Compatibility shims for older call-sites.
    bool LoadFont(const std::string& id, const std::string& filepath, float logicalSize = 14.0f);
    ImFont* GetFont(const std::string& id) const;  // legacy id lookup
    void RequestRebuild(float) {}
    void ExecuteRebuildIfNeeded(VkDevice) {}

private:
    FontManager() = default;

    // Build an ImFont from a face file, merging the fallback face. `wght` < 0
    // means "use the face's default"; otherwise apply as variable coordinate.
    ImFont* LoadFace(const std::string& filepath, float wght);

    std::unordered_map<std::string, FontFamily> families_;
    std::string fallbackFamily_;

    // Cache of materialized fonts keyed by "family|weight|italic".
    std::unordered_map<std::string, ImFont*> cache_;
    // Legacy id → ImFont (for LoadFont/GetFont(id) compatibility).
    std::unordered_map<std::string, ImFont*> legacyFonts_;

    std::string defaultFamily_;
    int         defaultWeight_ = 400;
    float       dpiScale_ = 1.0f;

    // Role index → discovered family name (0=sans,1=serif,2=mono,3=cjk,4=cjk-serif).
    std::array<std::string, 5> roleFamilies_{};
};

} // namespace UI

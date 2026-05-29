#include <UI/Text/FontManager.h>

#include <imgui_freetype.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace UI {

// ── Style-name → weight parsing ──────────────────────────────────────────────
// Faces are named "Family-Style" (e.g. NotoSans-SemiBold, Qanelas-Heavy). We
// map the style token to a numeric weight so families can be picked by weight.
namespace {

struct WeightName { const char* token; int weight; };
// Longer tokens first so "ExtraBold" matches before "Bold", etc.
constexpr WeightName kWeights[] = {
    {"extrablackitalic", 950}, {"extrablack", 950},
    {"blackitalic", 900},      {"black", 900},
    {"heavyitalic", 900},      {"heavy", 900},
    {"extrabolditalic", 800},  {"extrabold", 800},
    {"semibolditalic", 600},   {"semibold", 600},
    {"bolditalic", 700},       {"bold", 700},
    {"mediumitalic", 500},     {"medium", 500},
    {"extralightitalic", 200}, {"extralight", 200},
    {"ultralightitalic", 200}, {"ultralight", 200},
    {"lightitalic", 300},      {"light", 300},
    {"thinitalic", 100},       {"thin", 100},
    {"regularitalic", 400},    {"regular", 400},
    {"italic", 400},
};

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

int ParseWeight(const std::string& stem) {
    std::string low = ToLower(stem);
    for (const auto& w : kWeights)
        if (low.find(w.token) != std::string::npos) return w.weight;
    return 400;  // default to Regular
}

bool ParseItalic(const std::string& stem) {
    return ToLower(stem).find("italic") != std::string::npos;
}

// Family name = the part of the stem before the first '-' (NotoSans-Bold →
// "NotoSans"). For "…VariableFont…" files we strip that suffix too.
std::string ParseFamily(const std::string& stem) {
    auto dash = stem.find('-');
    std::string fam = (dash == std::string::npos) ? stem : stem.substr(0, dash);
    return fam;
}

// Probe a face with FreeType for an `fvar` weight axis (variable font).
void ProbeVariable(FontFace& face) {
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) return;
    FT_Face ft;
    if (FT_New_Face(lib, face.filepath.c_str(), 0, &ft) == 0) {
        if (ft->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
            FT_MM_Var* mm = nullptr;
            if (FT_Get_MM_Var(ft, &mm) == 0 && mm) {
                face.variable = true;
                for (FT_UInt a = 0; a < mm->num_axis; ++a) {
                    // Axis tag 'wght' == 0x77676874.
                    if (mm->axis[a].tag == 0x77676874) {
                        face.hasWghtAxis = true;
                        face.wghtMin = mm->axis[a].minimum / 65536.0f;
                        face.wghtDef = mm->axis[a].def     / 65536.0f;
                        face.wghtMax = mm->axis[a].maximum / 65536.0f;
                    }
                }
                FT_Done_MM_Var(lib, mm);
            }
        }
        FT_Done_Face(ft);
    }
    FT_Done_FreeType(lib);
}

} // namespace

bool FontFamily::HasVariable() const {
    for (const auto& f : faces) if (f.variable) return true;
    return false;
}

const FontFace* FontFamily::VariableFace(bool italic) const {
    // Prefer a variable face matching the requested slant; relax otherwise.
    for (const auto& f : faces)
        if (f.variable && f.hasWghtAxis && f.italic == italic) return &f;
    for (const auto& f : faces)
        if (f.variable && f.hasWghtAxis) return &f;
    return nullptr;
}

const FontFace* FontFamily::PickFace(int weight, bool italic) const {
    const FontFace* best = nullptr;
    int bestDist = 1 << 30;
    for (const auto& f : faces) {
        if (f.variable) continue;            // statics only for nearest-weight
        if (f.italic != italic) continue;
        int d = std::abs(f.weight - weight);
        if (d < bestDist) { bestDist = d; best = &f; }
    }
    if (best) return best;
    // No upright/italic match — relax the italic requirement.
    for (const auto& f : faces) {
        if (f.variable) continue;
        int d = std::abs(f.weight - weight);
        if (d < bestDist) { bestDist = d; best = &f; }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────

FontManager& FontManager::Instance() {
    static FontManager instance;
    return instance;
}

void FontManager::Initialize(float dpiScale) {
    dpiScale_ = dpiScale;
}

int FontManager::DiscoverFonts(const std::string& rootDir) {
    families_.clear();
    if (!fs::exists(rootDir)) {
        std::cerr << "[FontManager] font dir not found: " << rootDir << "\n";
        return 0;
    }
    for (const auto& entry : fs::recursive_directory_iterator(rootDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = ToLower(entry.path().extension().string());
        if (ext != ".ttf" && ext != ".otf") continue;

        std::string stem = entry.path().stem().string();
        FontFace face;
        face.filepath = entry.path().string();
        face.weight   = ParseWeight(stem);
        face.italic   = ParseItalic(stem);
        ProbeVariable(face);

        std::string fam = ParseFamily(stem);
        families_[fam].name = fam;
        families_[fam].faces.push_back(face);
    }
    return static_cast<int>(families_.size());
}

void FontManager::SetFallbackFamily(const std::string& familyName) {
    fallbackFamily_ = familyName;
}

// Read a font file and, when `wght` > 0 and the face is variable, bake the
// requested weight into the in-memory copy via FreeType's MM API. ImGui's
// FreeType loader does not expose variation coordinates, so we instantiate the
// variation ourselves and hand ImGui a plain memory buffer. The returned
// vector owns the (possibly re-serialized) font bytes for the atlas lifetime.
//
// NOTE: FreeType cannot re-serialize an arbitrary interpolated instance to a
// new TTF blob without the (optional) `FT_Face_Properties`/subsetting tools.
// What we CAN do portably is pick the nearest *named instance* exposed by the
// font's fvar table; most variable fonts ship named instances at standard
// weights (Thin…Black). We select the named instance closest to `wght` and
// record it so the loader applies it. For fonts without suitable named
// instances we fall back to the default instance.
static std::vector<unsigned char> ReadFile(const std::string& path) {
    std::vector<unsigned char> buf;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return buf;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) { buf.resize((size_t)n); fread(buf.data(), 1, (size_t)n, f); }
    fclose(f);
    return buf;
}

ImFont* FontManager::LoadFace(const std::string& filepath, float wght) {
    ImGuiIO& io = ImGui::GetIO();
    float hintSize = 14.0f * dpiScale_;

    // Persist the file bytes for the atlas lifetime (ImGui 1.92 requires the
    // data to outlive the atlas). We keep them in a static store.
    static std::vector<std::vector<unsigned char>> s_fontData;
    s_fontData.push_back(ReadFile(filepath));
    std::vector<unsigned char>& data = s_fontData.back();
    if (data.empty()) {
        std::cerr << "[FontManager] failed to read: " << filepath << "\n";
        return nullptr;
    }

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;
    cfg.FontDataOwnedByAtlas = false;   // we own s_fontData

    // For a variable face, choose the named instance closest to the requested
    // weight (FontNo selects a named instance: index 0 = default, 1.. = fvar
    // named instances in order).
    if (wght > 0.0f) {
        FT_Library lib;
        if (FT_Init_FreeType(&lib) == 0) {
            FT_Face ft;
            if (FT_New_Memory_Face(lib, data.data(), (FT_Long)data.size(), 0, &ft) == 0) {
                FT_MM_Var* mm = nullptr;
                if (FT_Get_MM_Var(ft, &mm) == 0 && mm) {
                    int wghtAxis = -1;
                    for (FT_UInt a = 0; a < mm->num_axis; ++a)
                        if (mm->axis[a].tag == 0x77676874) wghtAxis = (int)a;
                    if (wghtAxis >= 0) {
                        int bestInst = -1; float bestDist = 1e9f;
                        for (FT_UInt n = 0; n < mm->num_namedstyles; ++n) {
                            float w = mm->namedstyle[n].coords[wghtAxis] / 65536.0f;
                            float d = std::abs(w - wght);
                            if (d < bestDist) { bestDist = d; bestInst = (int)n; }
                        }
                        // FontNo high 16 bits select the named instance in
                        // FreeType (face_index | (instance_index << 16)).
                        if (bestInst >= 0)
                            cfg.FontNo = (unsigned)((bestInst + 1) << 16);
                    }
                    FT_Done_MM_Var(lib, mm);
                }
                FT_Done_Face(ft);
            }
            FT_Done_FreeType(lib);
        }
    }

    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        data.data(), (int)data.size(), hintSize, &cfg);
    if (!font) {
        std::cerr << "[FontManager] failed to load: " << filepath << "\n";
        return nullptr;
    }

    // NOTE: no glyph-fallback merge. A complete primary face (NotoSans, Qanelas)
    // already covers the glyphs the app uses (arrows, symbols), and merging a
    // symbol font here was masking/competing with that coverage. With imgui
    // 1.92's dynamic atlas, the primary face's glyphs rasterize lazily on demand.
    return font;
}

ImFont* FontManager::GetFont(const std::string& family, int weight, bool italic) {
    std::string key = family + "|" + std::to_string(weight) + "|" + (italic ? "i" : "u");
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;

    auto fam = families_.find(family);
    if (fam == families_.end()) return GetDefaultFont();

    // STATIC-FIRST strategy. NotoSans/Qanelas ship complete per-weight static
    // faces, so we resolve weight by picking the nearest static file and load
    // it with FontNo=0 (full glyph coverage). The variable named-instance path
    // (FontNo = instance<<16) is only a last resort for families with no
    // static faces: in imgui 1.92.9 the FreeType backend exposes no variation-
    // axis API, so a named instance is the only variable lever and it can drop
    // glyph coverage (e.g. "→" vanished). Avoid it for the default font.
    ImFont* font = nullptr;
    if (const FontFace* sf = fam->second.PickFace(weight, italic)) {
        font = LoadFace(sf->filepath, -1.0f);
    } else if (const FontFace* vf = fam->second.VariableFace(italic)) {
        float w = std::clamp(static_cast<float>(weight), vf->wghtMin, vf->wghtMax);
        font = LoadFace(vf->filepath, w);
    }
    if (!font) font = GetDefaultFont();
    cache_[key] = font;
    return font;
}

void FontManager::SetDefaultFont(const std::string& family, int weight) {
    defaultFamily_ = family;
    defaultWeight_ = weight;
    if (ImFont* f = GetFont(family, weight, false))
        ImGui::GetIO().FontDefault = f;
}

ImFont* FontManager::GetDefaultFont() const {
    if (defaultFamily_.empty()) return ImGui::GetIO().FontDefault;
    std::string key = defaultFamily_ + "|" + std::to_string(defaultWeight_) + "|u";
    auto it = cache_.find(key);
    return it != cache_.end() ? it->second : ImGui::GetIO().FontDefault;
}

void FontManager::PushFont(const std::string& family, int weight, bool italic) {
    if (ImFont* f = GetFont(family, weight, italic))
        ImGui::PushFont(f, 0.0f);
}

void FontManager::PopFont() { ImGui::PopFont(); }

void FontManager::SetRoleFamily(int roleIndex, const std::string& familyName) {
    if (roleIndex >= 0 && roleIndex < (int)roleFamilies_.size())
        roleFamilies_[roleIndex] = familyName;
}

std::string FontManager::RoleFamily(int roleIndex) const {
    if (roleIndex >= 0 && roleIndex < (int)roleFamilies_.size())
        return roleFamilies_[roleIndex];
    return {};
}

void FontManager::AutoAssignRoles() {
    auto contains = [](const std::string& s, const char* sub) {
        std::string a = ToLower(s), b = ToLower(sub);
        return a.find(b) != std::string::npos;
    };
    std::string sans, serif, mono, cjk;
    for (const auto& [name, fam] : families_) {
        if (contains(name, "symbol")) continue;       // fallback face, not a role
        if (contains(name, "mono") || contains(name, "code")) { if (mono.empty()) mono = name; }
        else if (contains(name, "cjk") || contains(name, "noto sans sc") ||
                 contains(name, "jp") || contains(name, "kr")) { if (cjk.empty()) cjk = name; }
        else if (contains(name, "serif")) { if (serif.empty()) serif = name; }
        else { if (sans.empty()) sans = name; }
    }
    // Sensible fallbacks: a role with no dedicated family borrows sans.
    if (sans.empty()) {
        for (const auto& [name, fam] : families_) {
            if (contains(name, "symbol")) continue;
            sans = name; break;
        }
        if (sans.empty() && !families_.empty()) sans = families_.begin()->first;
    }
    auto setIfEmpty = [&](int idx, const std::string& v, const std::string& fb) {
        if (roleFamilies_[idx].empty()) roleFamilies_[idx] = v.empty() ? fb : v;
    };
    setIfEmpty(0, sans,  sans);
    setIfEmpty(1, serif, sans);
    setIfEmpty(2, mono,  sans);
    setIfEmpty(3, cjk,   sans);
    setIfEmpty(4, cjk,   sans);
}

std::vector<std::string> FontManager::FamilyNames() const {
    std::vector<std::string> names;
    names.reserve(families_.size());
    for (const auto& [name, fam] : families_) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

const FontFamily* FontManager::Family(const std::string& name) const {
    auto it = families_.find(name);
    return it != families_.end() ? &it->second : nullptr;
}

// ── Legacy compatibility ─────────────────────────────────────────────────────
bool FontManager::LoadFont(const std::string& id, const std::string& filepath,
                           float /*logicalSize*/) {
    ImFont* f = LoadFace(filepath, -1.0f);
    if (!f) return false;
    legacyFonts_[id] = f;
    return true;
}

ImFont* FontManager::GetFont(const std::string& id) const {
    auto it = legacyFonts_.find(id);
    return it != legacyFonts_.end() ? it->second : nullptr;
}

} // namespace UI

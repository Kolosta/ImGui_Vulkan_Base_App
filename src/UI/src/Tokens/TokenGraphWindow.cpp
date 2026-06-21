#include <UI/Tokens/TokenGraphWindow.h>
#include <UI/Tokens/TokenValueWidgets.h>
#include <UI/Tokens/TokenJsonExport.h>
#include <UI/Chrome/BorderlessWindow.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <VectorGraphics/IconManager.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cfloat>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
ImU32  ColU32(Tok t) { return ImGui::ColorConvertFloat4ToU32(Col(t)); }

const char* ThemeName(DS::ThemeType t) {
    switch (t) {
        case DS::ThemeType::Dark:         return "Dark";
        case DS::ThemeType::Light:        return "Light";
        case DS::ThemeType::MutedGreen:   return "Muted Green";
        case DS::ThemeType::HighContrast: return "High Contrast";
    }
    return "?";
}

const char* AccessibilityName(DS::AccessibilityType a) {
    switch (a) {
        case DS::AccessibilityType::None:         return "None";
        case DS::AccessibilityType::Protanopia:   return "Protanopia";
        case DS::AccessibilityType::Deuteranopia: return "Deuteranopia";
        case DS::AccessibilityType::Tritanopia:   return "Tritanopia";
    }
    return "?";
}

// The four themes, in ThemeType order (matches kThemeCount / refByTheme index).
constexpr DS::ThemeType kThemes[kThemeCount] = {
    DS::ThemeType::Dark, DS::ThemeType::Light,
    DS::ThemeType::MutedGreen, DS::ThemeType::HighContrast,
};

const char* ShortThemeName(int i) {
    switch (i) { case 0: return "Dark"; case 1: return "Light";
                 case 2: return "Muted"; case 3: return "HiCon"; }
    return "?";
}

// Map a token's value type to its display TypeGroup (Float/Int/Ratio → Number).
TypeGroup GroupOf(DS::ValueType t) {
    switch (t) {
        case DS::ValueType::Color:      return TypeGroup::Color;
        case DS::ValueType::Float:
        case DS::ValueType::Int:
        case DS::ValueType::Ratio:      return TypeGroup::Number;
        case DS::ValueType::Vec2:       return TypeGroup::Vec2;
        case DS::ValueType::Bezier:     return TypeGroup::Bezier;
        case DS::ValueType::FontFamily: return TypeGroup::FontFamily;
        case DS::ValueType::TextStyle:  return TypeGroup::TextStyle;
        // A pure Reference token's group follows its terminal type (resolved by
        // the caller via TerminalType); default Color as a safe fallback.
        case DS::ValueType::Reference:  return TypeGroup::Color;
    }
    return TypeGroup::Color;
}

const char* GroupLabel(TypeGroup g) {
    switch (g) {
        case TypeGroup::Color:      return "Color";
        case TypeGroup::Number:     return "Number";
        case TypeGroup::Vec2:       return "Vec2";
        case TypeGroup::Bezier:     return "Bezier";
        case TypeGroup::FontFamily: return "Font Family";
        case TypeGroup::TextStyle:  return "Text Style";
        default: return "?";
    }
}

// The pure schema default of a token (ignores ALL overrides): follow the token's
// own default; if it is a reference, recurse into the target's default until a
// concrete value.
DS::TokenValue PureDefault(const std::string& id, DS::ThemeType theme) {
    auto& reg = DS::DesignSystem::Instance().GetRegistry();
    std::string cur = id;
    for (int hops = 0; hops < 64; ++hops) {
        auto tok = reg.GetToken(cur);
        if (!tok) break;
        const DS::TokenValue& dv = tok->GetDefaultValue();
        if (dv.GetType() != DS::ValueType::Reference) return dv;
        cur = dv.AsReference();
    }
    return DS::DesignSystem::Instance().ResolveTokenValue(id, theme);
}

// Read the override value for a layer if present; else the resolved value.
DS::TokenValue LayerValueOrResolved(const std::string& id, bool global,
                                    DS::ThemeType theme, bool& hasOverride) {
    auto& mgr = DS::DesignSystem::Instance().GetOverrideManager();
    DS::Override* o = mgr.GetOverride(id, global, theme);
    hasOverride = (o != nullptr);
    if (o) return o->GetValue();
    return DS::DesignSystem::Instance().ResolveTokenValue(id, theme);
}

void WriteLayer(const std::string& id, bool global, DS::ThemeType theme,
                const DS::TokenValue& v) {
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    if (global) mgr.AddOverride(DS::Override(id, v));
    else        mgr.AddOverride(DS::Override(id, v, theme));
    ds.NotifyOverrideChange();
    ds.ApplyGlobalStyle();
}

void ClearLayer(const std::string& id, bool global, DS::ThemeType theme) {
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    if (global) mgr.RemoveGlobalOverride(id);
    else        mgr.RemoveThemeOverride(id, theme);
    ds.NotifyOverrideChange();
    ds.ApplyGlobalStyle();
}

// Rank of a tier: references may only go to a strictly-lower-or-equal rank.
int LevelRank(DS::TokenLevel l) {
    return l == DS::TokenLevel::Primitive ? 0
         : l == DS::TokenLevel::Semantic  ? 1 : 2;
}

// The terminal (non-reference) value type a token resolves to via its default
// reference chain.
DS::ValueType TerminalType(const std::string& id) {
    auto& reg = DS::DesignSystem::Instance().GetRegistry();
    std::string cur = id;
    for (int hops = 0; hops < 64; ++hops) {
        auto tok = reg.GetToken(cur);
        if (!tok) break;
        if (tok->GetValueType() != DS::ValueType::Reference)
            return tok->GetValueType();
        const DS::TokenValue& dv = tok->GetDefaultValue();
        if (dv.GetType() != DS::ValueType::Reference) return dv.GetType();
        cur = dv.AsReference();
    }
    return DS::ValueType::Color;
}

} // namespace

bool TokenGraphWindow::CanReference(const std::string& sourceId,
                                    const std::string& targetId, int themeIdx,
                                    std::string* reason) const {
    auto& ds = DS::DesignSystem::Instance();
    auto setReason = [&](const char* r) { if (reason) *reason = r; };

    if (sourceId == targetId) { setReason("a token cannot reference itself"); return false; }
    auto src = ds.GetRegistry().GetToken(sourceId);
    auto tgt = ds.GetRegistry().GetToken(targetId);
    if (!src || !tgt) { setReason("unknown token"); return false; }

    if (src->GetLevel() == DS::TokenLevel::Primitive) {
        setReason("primitives cannot reference"); return false;
    }
    if (LevelRank(tgt->GetLevel()) > LevelRank(src->GetLevel())) {
        setReason("would reference a higher tier"); return false;
    }
    if (TerminalType(sourceId) != TerminalType(targetId)) {
        setReason("incompatible value type"); return false;
    }
    // Acyclicity: from target, can we reach source via effective edges?
    {
        std::vector<std::string> stack{ targetId };
        std::vector<std::string> seen;
        int guard = 0;
        while (!stack.empty() && guard++ < 100000) {
            std::string cur = stack.back(); stack.pop_back();
            if (cur == sourceId) { setReason("would create a reference cycle"); return false; }
            if (std::find(seen.begin(), seen.end(), cur) != seen.end()) continue;
            seen.push_back(cur);
            for (int t = 0; t < kThemeCount; ++t) {
                std::string r = EffectiveRefTarget(cur, t);
                if (!r.empty()) stack.push_back(r);
            }
        }
    }
    (void)themeIdx;
    return true;
}

std::unordered_map<std::string, bool>
TokenGraphWindow::RelatedClosure(const std::vector<std::string>& seeds) const {
    std::unordered_map<std::string, bool> in;
    if (seeds.empty()) return in;

    std::vector<std::string> stack(seeds.begin(), seeds.end());
    for (const auto& s : seeds) in[s] = true;
    int guard = 0;
    while (!stack.empty() && guard++ < 200000) {
        std::string cur = stack.back(); stack.pop_back();
        for (int t = 0; t < kThemeCount; ++t) {
            std::string r = EffectiveRefTarget(cur, t);
            if (!r.empty() && !in.count(r)) { in[r] = true; stack.push_back(r); }
        }
    }
    bool changed = true; guard = 0;
    while (changed && guard++ < 1000) {
        changed = false;
        for (const GraphNode& n : nodes_) {
            if (in.count(n.id)) continue;
            for (int t = 0; t < kThemeCount; ++t) {
                const std::string& r = n.refByTheme[t];
                if (!r.empty() && in.count(r)) { in[n.id] = true; changed = true; break; }
            }
        }
    }
    return in;
}

std::string TokenGraphWindow::EffectiveRefTarget(const std::string& tokenId,
                                                 int themeIdx) const {
    using DS::ValueType;
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    const DS::ThemeType theme = kThemes[themeIdx];

    if (const DS::Override* o = mgr.GetBestOverride(tokenId, theme)) {
        if (o->GetValue().GetType() == ValueType::Reference)
            return o->GetValue().AsReference();
        return {};
    }
    auto tok = ds.GetRegistry().GetToken(tokenId);
    if (!tok) return {};
    if (const DS::TokenValue* tv =
            tok->GetContextValue(DS::Context(theme, DS::AccessibilityType::None))) {
        if (tv->GetType() == ValueType::Reference) return tv->AsReference();
        return {};
    }
    const DS::TokenValue& dv = tok->GetDefaultValue();
    if (dv.GetType() == ValueType::Reference) return dv.AsReference();
    return {};
}

void TokenGraphWindow::RebuildGraph() {
    auto& ds = DS::DesignSystem::Instance();

    // Preserve per-id opened/visible state across rebuilds.
    std::unordered_map<std::string, bool> wasOpened;
    for (const GraphNode& n : nodes_) if (n.opened) wasOpened[n.id] = true;
    std::unordered_map<TypeGroup, bool> typeOpened;
    for (const TypeCard& tc : typeCards_) if (tc.opened) typeOpened[tc.group] = true;

    nodes_.clear();
    index_.clear();

    auto all = ds.GetRegistry().GetAllTokens();
    nodes_.reserve(all.size());
    for (const auto& tok : all) {
        GraphNode n;
        n.id        = tok->GetId();
        n.fullName  = n.id;
        n.level     = tok->GetLevel();
        n.type      = tok->GetValueType();
        n.group     = GroupOf(n.type == DS::ValueType::Reference
                                  ? TerminalType(n.id) : n.type);
        n.zone      = LevelRank(n.level);
        nodes_.push_back(std::move(n));
    }
    std::sort(nodes_.begin(), nodes_.end(), [](const GraphNode& a, const GraphNode& b) {
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.id < b.id;
    });
    for (int i = 0; i < (int)nodes_.size(); ++i)
        index_[nodes_[i].id] = i;

    // Reference targets per theme, and the reverse "children" edges.
    for (GraphNode& n : nodes_)
        for (int t = 0; t < kThemeCount; ++t)
            n.refByTheme[t] = EffectiveRefTarget(n.id, t);
    for (int i = 0; i < (int)nodes_.size(); ++i) {
        std::unordered_set<int> tgts;
        for (int t = 0; t < kThemeCount; ++t) {
            const std::string& r = nodes_[i].refByTheme[t];
            if (r.empty()) continue;
            auto it = index_.find(r);
            if (it != index_.end()) tgts.insert(it->second);
        }
        for (int tg : tgts) nodes_[tg].children.push_back(i);
    }

    // Descendant counts (direct + indirect) via memoised DFS over children.
    std::vector<int> memo(nodes_.size(), -1);
    std::function<int(int)> countDesc = [&](int i) -> int {
        if (memo[i] >= 0) return memo[i];
        memo[i] = 0;  // guard against runtime cycles
        std::unordered_set<int> seen;
        std::vector<int> stack(nodes_[i].children.begin(), nodes_[i].children.end());
        while (!stack.empty()) {
            int c = stack.back(); stack.pop_back();
            if (seen.count(c)) continue;
            seen.insert(c);
            for (int cc : nodes_[c].children) stack.push_back(cc);
        }
        memo[i] = (int)seen.size();
        return memo[i];
    };
    for (int i = 0; i < (int)nodes_.size(); ++i)
        nodes_[i].descendantCount = countDesc(i);

    // Restore opened flags.
    for (GraphNode& n : nodes_) n.opened = wasOpened.count(n.id) > 0;

    // (Re)build the type cards.
    typeCards_.clear();
    for (int g = 0; g < (int)TypeGroup::Count; ++g) {
        TypeCard tc;
        tc.group = (TypeGroup)g;
        tc.label = GroupLabel(tc.group);
        tc.opened = typeOpened.count(tc.group) > 0;
        for (const GraphNode& n : nodes_)
            if (n.zone == 0 && n.group == tc.group) tc.tokenCount++;
        typeCards_.push_back(tc);
    }

    dirty_ = false;
    layoutDirty_ = true;
}

void TokenGraphWindow::RecomputeVisibility() {
    // Reversed dataflow: we read RIGHT-to-LEFT (leaves on the left, parents they
    // reference revealed to the right). A node is visible if:
    //   • it is a LEAF (nothing references it: children.empty()) — these are the
    //     default starting points, mixing every tier; OR
    //   • it is OPENED itself; OR
    //   • it is an ancestor reachable from an opened node by following references
    //     (refByTheme, all themes) — i.e. opening a card reveals its WHOLE parent
    //     chain up to the primitives.
    for (GraphNode& n : nodes_) n.visible = false;

    // 1) All leaves (no children) are shown by default.
    for (GraphNode& n : nodes_)
        if (n.children.empty()) n.visible = true;

    // 1b) An opened TYPE card reveals all primitives of that type (the right-most
    //     roots), so the user can also explore top-down from a type.
    std::unordered_set<TypeGroup> openTypes;
    for (const TypeCard& tc : typeCards_) if (tc.opened) openTypes.insert(tc.group);
    if (!openTypes.empty())
        for (GraphNode& n : nodes_)
            if (n.zone == 0 && openTypes.count(n.group)) n.visible = true;

    // 2) Opened nodes reveal their entire reference (parent) chain.
    std::vector<int> stack;
    for (int i = 0; i < (int)nodes_.size(); ++i)
        if (nodes_[i].opened) { nodes_[i].visible = true; stack.push_back(i); }
    std::unordered_set<int> seen;
    while (!stack.empty()) {
        int i = stack.back(); stack.pop_back();
        if (seen.count(i)) continue;
        seen.insert(i);
        for (int t = 0; t < kThemeCount; ++t) {
            const std::string& r = nodes_[i].refByTheme[t];
            if (r.empty()) continue;
            auto it = index_.find(r);
            if (it == index_.end()) continue;
            nodes_[it->second].visible = true;
            stack.push_back(it->second);
        }
    }
    layoutDirty_ = true;
}

// ── Card metrics (all in graph-units = pixels at zoom 1) ─────────────────────
// Base frame height matches what BeginChild + FramePadding(4,3) produce at
// scale 1: fontSize + 2*3. We read the live font size so it tracks the theme.
namespace metrics {
    inline float FontGU()  { return ImGui::GetFontSize(); }
    inline float FrameGU() { return FontGU() + 6.0f; }      // +2*FramePadding.y(3)
    inline float LineGU()  { return FontGU() + 4.0f; }      // text line + small gap
    inline float ItemGapGU() { return 4.0f; }
}

// Height (graph-units) of the value editor for `vt` at a given editor width.
// Mirrors TokenValueEditor: bezier draws a (width × 0.6) preview box stacked
// above a DragFloat4 row; everything else is a single frame-height widget.
static float EditorHeightGU(DS::ValueType vt, float editorW) {
    using namespace metrics;
    switch (vt) {
        case DS::ValueType::Bezier:
            return editorW * 0.6f + ItemGapGU() + FrameGU();   // box + 4 inputs
        default:
            return FrameGU();
    }
}

// Height of one labelled value row = max(label line, editor) + a little gap.
static float RowHeightGU(DS::ValueType vt, float editorW) {
    using namespace metrics;
    return std::max(LineGU(), EditorHeightGU(vt, editorW)) + ItemGapGU() * 2.0f;
}

// Compose the token's info text (description + constraint) for the card footer.
static std::string ComposeInfo(const std::shared_ptr<DS::Token>& tok,
                               DS::ValueType exactType) {
    std::string s = "type: ";
    s += DS::ValueTypeToString(exactType);
    if (tok) {
        if (!tok->GetDescription().empty()) {
            s += "\n"; s += tok->GetDescription();
        }
        const DS::ValueConstraint& c = tok->GetConstraint();
        if (!c.IsEmpty()) {
            char buf[96];
            if (!c.description.empty())
                std::snprintf(buf, sizeof(buf), "\nconstraint: %s", c.description.c_str());
            else {
                auto mn = c.Min(); auto mx = c.Max();
                std::snprintf(buf, sizeof(buf), "\nrange: %.3g .. %.3g",
                              mn ? *mn : 0.0, mx ? *mx : 0.0);
            }
            s += buf;
        }
    }
    return s;
}

void TokenGraphWindow::LayoutGraph() {
    // Reversed bands, left → right: Component, Semantic, Primitive, then the type
    // column. In this REVERSED view a parent (a token that is referenced) must
    // sit to the RIGHT of every child that references it. So a node's sub-column
    // = 1 + max(sub-column of its same-tier VISIBLE children); a leaf (no visible
    // same-tier child) is column 0. Following `children` (not refs) guarantees a
    // parent is always at least one column right of each of its children — and
    // several columns right when it is the parent of a deeper chain.
    const int N = (int)nodes_.size();
    std::vector<int> localCol(N, -1), visiting(N, 0);
    std::function<int(int)> depth = [&](int i) -> int {
        if (i < 0 || i >= N) return 0;
        if (localCol[i] >= 0) return localCol[i];
        if (visiting[i]) return 0;
        visiting[i] = 1;
        int best = 0;
        for (int c : nodes_[i].children) {
            if (!nodes_[c].visible) continue;
            if (nodes_[c].zone == nodes_[i].zone)            // same tier only
                best = std::max(best, depth(c) + 1);
        }
        visiting[i] = 0;
        localCol[i] = best;
        return best;
    };
    for (int i = 0; i < N; ++i)
        if (nodes_[i].visible) nodes_[i].column = depth(i);

    // ── Geometry (graph-units; the renderer multiplies by zoom). Heights are
    //    derived from the LIVE metrics so the card reserves exactly what it
    //    draws (no clipped bezier, no overflowing info lines).
    const float titleH = metrics::FrameGU() + 4.0f;
    const float colGap  = 80.0f;     // gap between sub-columns of a band
    const float zoneGap = 240.0f;    // wide gap between bands
    const float gapY    = 18.0f;
    const float minW    = 240.0f;
    const float padX    = 8.0f;
    const float labelW  = 52.0f;     // label column inside the card
    const float resetW  = 20.0f;     // room for the reset button at row end
    auto& reg = DS::DesignSystem::Instance().GetRegistry();

    for (GraphNode& n : nodes_) {
        if (!n.visible) continue;
        auto tok = reg.GetToken(n.id);
        // Width fits the full id (title) with room for child-count + eye.
        ImVec2 ts = ImGui::CalcTextSize(n.fullName.c_str());
        float w = std::max(minW, ts.x + 80.0f);
        n.size.x = w;
        n.editorW = std::max(40.0f, w - padX * 2.0f - labelW - resetW);
        n.bodyTop = titleH;

        // Per-row height = height of the editor actually shown on that row, which
        // depends on the TYPE OF THE VALUE on that layer (an override may pin a
        // Reference → small input, while the resolved value could be a bezier).
        // Rows for hidden themes collapse to zero height (Global + Dark always).
        float y = titleH;
        for (int r = 0; r < 1 + kThemeCount; ++r) {
            bool global = (r == 0);
            int themeIdx = global ? 0 : (r - 1);   // row 1 = Dark (theme 0)
            n.rowTop[r] = y;
            if (!global && !themeVisible_[themeIdx]) { n.rowH[r] = 0.0f; continue; }
            DS::ThemeType th = kThemes[themeIdx];
            bool hasOvr = false;
            DS::TokenValue v = LayerValueOrResolved(n.id, global, th, hasOvr);
            n.rowH[r] = RowHeightGU(v.GetType(), n.editorW);
            y += n.rowH[r];
        }
        // Info block: terminal type + description + constraint, wrapped.
        n.infoText = ComposeInfo(tok, TerminalType(n.id));
        float wrap = w - padX * 2.0f;
        ImVec2 its = ImGui::CalcTextSize(n.infoText.c_str(), nullptr, false, wrap);
        n.infoTop = y;
        n.infoH = its.y + 8.0f;
        n.size.y = y + n.infoH + 8.0f;
    }

    // Visual band order (left → right): zone 2 (component), 1 (semantic),
    // 0 (primitive). bandOf maps a model zone to its visual band index 0..2.
    auto bandOf = [](int zone) { return 2 - zone; };   // comp→0, sem→1, prim→2

    int maxColInBand[3] = {0,0,0};
    for (const GraphNode& n : nodes_)
        if (n.visible) {
            int b = bandOf(n.zone);
            maxColInBand[b] = std::max(maxColInBand[b], n.column);
        }
    // Width of each (band,column) = widest visible card there.
    std::vector<std::vector<float>> colW(3);
    for (int b = 0; b < 3; ++b) colW[b].assign(maxColInBand[b] + 1, minW);
    for (const GraphNode& n : nodes_)
        if (n.visible) {
            int b = bandOf(n.zone);
            colW[b][n.column] = std::max(colW[b][n.column], n.size.x);
        }

    // Cumulative X per (band,column). Inside a band, a deeper sub-column is to
    // the RIGHT, matching the reversed dataflow.
    std::vector<std::vector<float>> colX(3);
    float cursorX = 0.0f;
    for (int b = 0; b < 3; ++b) {
        colX[b].assign(maxColInBand[b] + 1, 0.0f);
        bool any = false;
        for (int c = 0; c <= maxColInBand[b]; ++c) {
            colX[b][c] = cursorX;
            cursorX += colW[b][c] + colGap;
            any = true;
        }
        if (any) cursorX += zoneGap;   // wide gap before the next band
    }

    // Y stacking per (band,column), stable order from the sorted nodes_.
    std::vector<std::vector<float>> nextY(3);
    for (int b = 0; b < 3; ++b) nextY[b].assign(maxColInBand[b] + 1, 0.0f);
    for (GraphNode& n : nodes_) {
        if (!n.visible) continue;
        int b = bandOf(n.zone);
        n.pos.x = colX[b][n.column];
        n.pos.y = nextY[b][n.column];
        nextY[b][n.column] += n.size.y + gapY;
    }

    // Type cards: a column to the RIGHT of everything (after the primitive band).
    float typeW = minW;
    for (TypeCard& tc : typeCards_) {
        ImVec2 ts = ImGui::CalcTextSize(tc.label.c_str());
        typeW = std::max(typeW, ts.x + 100.0f);
    }
    const float typeCardH = titleH + metrics::LineGU() + 12.0f;  // title + count line
    float typeX = cursorX;   // cursorX already past the last band + its zoneGap
    float ty = 0.0f;
    for (TypeCard& tc : typeCards_) {
        tc.size = ImVec2(typeW, typeCardH);
        tc.pos = ImVec2(typeX, ty);
        ty += typeCardH + gapY;
    }
    (void)padX;
    layoutDirty_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────

void TokenGraphWindow::DrawTypeCard(TypeCard& tc, ImVec2 screenPos, float scale,
                                    bool dimmed, int& toggleReq) {
    auto& ds = DS::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float titleH = (metrics::FrameGU() + 4.0f) * scale;
    const ImVec2 sz(tc.size.x * scale, tc.size.y * scale);
    ImVec2 p0 = screenPos, p1(p0.x + sz.x, p0.y + sz.y);
    const float a = dimmed ? 0.28f : 1.0f;
    auto fade = [&](Tok tk) { ImVec4 c = ds.GetColor(tk); c.w *= a;
                              return ImGui::ColorConvertFloat4ToU32(c); };

    const float radius = Flt(Tok::C_Frame_CornerRadius) * scale;
    // Slightly translucent like token cards, so links behind show through.
    ImVec4 bgc = ds.GetColor(Tok::S_Surface_Raised); bgc.w *= a * 0.85f;
    dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(bgc), radius);
    ImU32 outline = tc.opened ? fade(Tok::S_Color_Accent_Default)
                              : fade(Tok::S_Color_Border_Default);
    dl->AddRect(p0, p1, outline, radius, 0, (tc.opened ? 2.0f : 1.0f) * scale);

    const float padX = 8.0f * scale;
    const float eye = titleH * 0.7f;
    ImVec2 eyeMin(p0.x + padX, p0.y + (titleH - eye) * 0.5f);

    // One full-card button: a single click OR a double click toggles the type
    // (these cards have no other controls, so either gesture opens/closes it).
    bool hovEye = false;
    if (!dimmed) {
        ImGui::PushID(("type" + tc.label).c_str());
        ImGui::SetNextItemAllowOverlap();
        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton("##tc", sz))
            toggleReq = (int)tc.group;            // single click toggles
        bool hov = ImGui::IsItemHovered();
        ImVec2 m = ImGui::GetIO().MousePos;
        hovEye = hov &&
                 m.x >= eyeMin.x && m.x <= eyeMin.x + eye &&
                 m.y >= eyeMin.y && m.y <= eyeMin.y + eye;
        ImGui::PopID();
    }

    dl->AddCircle(ImVec2(eyeMin.x + eye * 0.5f, eyeMin.y + eye * 0.5f), eye * 0.35f,
                  hovEye ? fade(Tok::S_Color_Accent_Default) : fade(Tok::S_Color_Text_Subtle),
                  0, std::max(1.0f, scale));
    ImFont* font = ImGui::GetFont();
    const float fsz = ImGui::GetFontSize() * scale;
    if (titleH > 8.0f) {
        dl->AddText(font, fsz, ImVec2(eyeMin.x + eye + padX,
                           p0.y + (titleH - fsz) * 0.5f),
                    fade(Tok::S_Color_Text_Default), tc.label.c_str());
    }
    // Info row: token count.
    char info[48];
    std::snprintf(info, sizeof(info), "%d primitives", tc.tokenCount);
    dl->AddText(font, fsz, ImVec2(p0.x + padX, p0.y + titleH + 4.0f * scale),
                fade(Tok::S_Color_Text_Subtle), info);
}

void TokenGraphWindow::DrawCard(GraphNode& n, ImVec2 screenPos, float scale,
                                const CardFlags& f, std::string& toggleReq,
                                std::string& selectReq) {
    auto& ds = DS::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const DS::ThemeType curTheme = ds.GetCurrentContext().GetTheme();
    const int curThemeIdx = (int)curTheme;

    // Title height matches LayoutGraph (metrics::FrameGU()+4) × scale.
    const float titleH = (metrics::FrameGU() + 4.0f) * scale;
    const ImVec2 sz(n.size.x * scale, n.size.y * scale);
    ImVec2 p0 = screenPos, p1(p0.x + sz.x, p0.y + sz.y);

    const float a = f.dimmed ? 0.28f : 1.0f;
    auto fade = [&](Tok tk) { ImVec4 c = ds.GetColor(tk); c.w *= a;
                              return ImGui::ColorConvertFloat4ToU32(c); };
    // Slightly translucent so links passing behind a card remain visible.
    auto fadeBg = [&](Tok tk, float alpha) { ImVec4 c = ds.GetColor(tk);
                              c.w *= a * alpha; return ImGui::ColorConvertFloat4ToU32(c); };

    const float padX = 8.0f * scale;
    const float btnSz = titleH * 0.7f;
    const float radius = Flt(Tok::C_Frame_CornerRadius) * scale;
    const ImU32 bg     = fadeBg(Tok::C_Frame_Background, 0.72f);   // see-through body
    const ImU32 raised = fadeBg(Tok::S_Surface_Raised, 0.85f);     // see-through title
    const ImU32 border = fade(Tok::S_Color_Border_Default);
    const ImU32 txt    = fade(Tok::S_Color_Text_Default);
    const ImU32 txtSub = fade(Tok::S_Color_Text_Subtle);
    const ImU32 accent = fade(Tok::S_Color_Accent_Default);

    // Card background, title band, border. The body content sits in a child
    // window (transparent) so links behind the card show through.
    dl->AddRectFilled(p0, p1, bg, radius);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + titleH), raised, radius,
                      ImDrawFlags_RoundCornersTop);
    ImU32 outline = border; float outlineW = std::max(1.0f, scale);
    if (n.opened) { outline = accent; outlineW = 1.5f * scale; }
    if (f.filterHit) { outline = ColU32(Tok::S_Color_Positive_Default); outlineW = 2.0f * scale; }
    if (f.selected)  { outline = ColU32(Tok::S_State_Selected_OnPage);   outlineW = 2.0f * scale; }
    if (f.active)    { outline = ColU32(Tok::S_State_Active_OnPage);     outlineW = 2.5f * scale; }
    dl->AddRect(p0, p1, outline, radius, 0, outlineW);

    // ── Title bar interaction + glyphs.
    ImVec2 eyeMin(p1.x - padX - btnSz, p0.y + (titleH - btnSz) * 0.5f);
    bool eyeHov = false;
    if (!f.dimmed) {
        ImGui::PushID(n.id.c_str());
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##title", ImVec2(sz.x, titleH));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            toggleReq = n.id;
        else if (ImGui::IsItemDeactivated() &&
                 !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
            selectReq = n.id;
        ImGui::SetCursorScreenPos(eyeMin);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##eye", ImVec2(btnSz, btnSz))) toggleReq = n.id;
        eyeHov = ImGui::IsItemHovered();
        if (eyeHov) ImGui::SetTooltip("Show/hide the parents this token references");
        ImGui::PopID();
    }
    // Title text drawn at an explicitly scaled font size so it tracks the zoom
    // (dl->AddText with a size arg, independent of any window font scale).
    ImFont* font = ImGui::GetFont();
    const float fsz = ImGui::GetFontSize() * scale;
    if (titleH > 8.0f) {
        float titleRight = eyeMin.x - 6.0f * scale;
        dl->PushClipRect(ImVec2(p0.x + padX, p0.y),
                         ImVec2(titleRight, p0.y + titleH), true);
        dl->AddText(font, fsz, ImVec2(p0.x + padX, p0.y + (titleH - fsz) * 0.5f),
                    txt, n.fullName.c_str());
        dl->PopClipRect();
        ImU32 eyeCol = eyeHov ? accent : (n.opened ? accent : txtSub);
        dl->AddCircle(ImVec2(eyeMin.x + btnSz * 0.5f, eyeMin.y + btnSz * 0.5f),
                      btnSz * 0.35f, eyeCol, 0, std::max(1.0f, scale));
        if (n.descendantCount > 0) {
            char cc[24]; std::snprintf(cc, sizeof(cc), "%d", n.descendantCount);
            ImVec2 cts = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, cc);
            dl->AddText(font, fsz, ImVec2(eyeMin.x - 6.0f * scale - cts.x,
                               p0.y + (titleH - cts.y) * 0.5f), txtSub, cc);
        }
    }

    // ── Theme ports on the card edges (drawn over the border). Left edge = the
    //    "in" port (children entering); right edge = the "ref" port (toward the
    //    parent on the right). Y uses the per-row cached offsets (scaled).
    for (int t = 0; t < kThemeCount; ++t) {
        int r = 1 + t;
        if (n.rowH[r] <= 0.0f) continue;       // hidden theme → no port
        float ry = p0.y + (n.rowTop[r] + n.rowH[r] * 0.5f) * scale;
        ImU32 pc = (t == curThemeIdx) ? accent : txtSub;
        dl->AddCircleFilled(ImVec2(p0.x, ry), 4.0f * scale, pc);   // in (left)
        dl->AddCircleFilled(ImVec2(p1.x, ry), 4.0f * scale, pc);   // ref (right)
    }

    // ── Body content inside a real child window so the widget layout (incl. the
    //    multi-line bezier: preview stacked above its 4 inputs) stays LOCAL to
    //    the card and never spills out to the host window's left margin. Font +
    //    style metrics scale with the zoom.
    ImVec2 bodyPos(p0.x, p0.y + titleH);
    ImVec2 bodySize(sz.x, sz.y - titleH);
    ImGui::SetCursorScreenPos(bodyPos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, 4.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * scale, 3.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(4.0f * scale, 4.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,  std::max(6.0f, 8.0f * scale));
    ImGui::SetNextItemAllowOverlap();
    ImGui::BeginChild((n.id + "##body").c_str(), bodySize, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetWindowFontScale(scale);

    const ImVec2 childOrigin = ImGui::GetWindowPos();   // == bodyPos
    const float labelW = 52.0f * scale;
    const float editorW = std::max(20.0f, bodySize.x - padX * 2.0f - labelW - 20.0f * scale);

    // Each row sits at its cached per-row offset and is as tall as its content
    // (a bezier row reserves room for the preview + 4 inputs). Local Y in the
    // child = (rowTop − bodyTop) × scale.
    auto rowEditor = [&](int rowIdx, const char* label, bool global, int themeIdx,
                         bool tinted) {
        float localTop = (n.rowTop[rowIdx] - n.bodyTop) * scale;
        ImVec2 labelScreen(childOrigin.x + padX, childOrigin.y + localTop + 3.0f * scale);
        dl->AddText(font, fsz, labelScreen, tinted ? txt : txtSub, label);
        if (f.dimmed) return;
        DS::ThemeType th = (themeIdx >= 0) ? kThemes[themeIdx] : curTheme;
        bool hasOvr = false;
        DS::TokenValue val = LayerValueOrResolved(n.id, global, th, hasOvr);
        DS::TokenValue dflt = PureDefault(n.id, th);
        ImGui::PushID((std::string(label) + (global ? "G" : "T")).c_str());
        ImGui::SetCursorPos(ImVec2(padX + labelW, localTop + 2.0f * scale));
        ImGui::BeginGroup();
        if (UI::TokenValueEditor("##e", val, n.id, editorW, &dflt)) {
            WriteLayer(n.id, global, th, val);
            layoutDirty_ = true; dirty_ = true;
        }
        if (hasOvr) {
            ImGui::SameLine(0.0f, 3.0f * scale);
            if (ImGui::SmallButton("\xE2\x86\xBA")) { ClearLayer(n.id, global, th); dirty_ = true; }
        }
        ImGui::EndGroup();
        ImGui::PopID();
    };

    rowEditor(0, "Global", true, -1, false);
    for (int t = 0; t < kThemeCount; ++t) {
        if (n.rowH[1 + t] <= 0.0f) continue;   // hidden theme row
        rowEditor(1 + t, ShortThemeName(t), false, t, t == curThemeIdx);
    }

    // ── Info block under the rows: terminal type + description + constraints,
    //    wrapped to the inner width (cached in LayoutGraph as infoTop/infoText).
    {
        float localTop = (n.infoTop - n.bodyTop) * scale;
        ImVec2 ip(childOrigin.x + padX, childOrigin.y + localTop + 2.0f * scale);
        float wrap = bodySize.x - padX * 2.0f;
        dl->AddText(font, fsz, ip, txtSub, n.infoText.c_str(), nullptr, wrap);
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
}

// ─────────────────────────────────────────────────────────────────────────────

void TokenGraphWindow::Render(bool* open) {
    if (!open || !*open) return;

    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Col(Tok::S_Surface_Canvas));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

    bool stayOpen = true;
    sysClose_ = false;
    if (ImGui::Begin("Token Graph", &stayOpen, kFlags)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float controlH = Flt(Tok::S_Size_ControlHeight) * gs;
        const float barH = controlH + 8.0f * gs;

        RenderTitleBar(avail.x);

        ImGui::SetCursorPos(ImVec2(0.0f, barH));
        RenderMenuBar(avail.x);

        const float menuH = controlH + 6.0f * gs;
        ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize(avail.x, avail.y - barH - menuH);
        if (canvasSize.y < 0.0f) canvasSize.y = 0.0f;
        RenderCanvas(canvasOrigin, canvasSize);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    if (!stayOpen || sysClose_) *open = false;
}

void TokenGraphWindow::RenderTitleBar(float width) {
    auto& ds = DS::DesignSystem::Instance();
    auto& im = VectorGraphics::IconManager::Instance();
    const float gs = ds.GetGlobalScale();
    const float controlH = Flt(Tok::S_Size_ControlHeight) * gs;
    const float pad = 8.0f * gs;
    const float barH = controlH + pad;

    const ImVec4 barBgV = Col(Tok::C_PrefBar_Background);
    const ImVec4 textV  = Col(Tok::C_PrefBar_Text);
    const ImVec4 iconV  = Col(Tok::C_PrefBar_Icon);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImVec2 mx(mn.x + width, mn.y + barH);
    dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(barBgV));

    const float iconSz = Flt(Tok::C_Dropdown_IconSize) * gs;
    float x = mn.x + pad;
    {
        ImVec2 ip(x, mn.y + (barH - iconSz) * 0.5f);
        auto md = im.GetDefaultMetadata("logo_carto");
        for (auto& z : md.colorZones) z.customColor = iconV;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, "logo_carto", ip, iconSz, md);
        x += iconSz + pad;
    }
    {
        const char* title = "Token Graph";
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(x, mn.y + (barH - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(textV), title);
    }

    ImGui::SetCursorScreenPos(mn);
    ImGui::Dummy(ImVec2(width, barH));

    ImGuiViewport* vp = ImGui::GetMainViewport();
    SDL_Window* sdlWin = vp
        ? SDL_GetWindowFromID((SDL_WindowID)(intptr_t)vp->PlatformHandle)
        : nullptr;
    // Borderless behaviour is owned by the window's controller (shared with the
    // main window); the bar only picks glyphs + forwards the button hits.
    BorderlessWindowController* chrome =
        BorderlessWindowController::FromWindow(sdlWin);

    const float btnW = barH;
    const ImU32 glyph   = ImGui::ColorConvertFloat4ToU32(iconV);
    const ImU32 hovBg   = ColU32(Tok::C_PrefBar_ButtonHover);
    const ImU32 closeBg = ColU32(Tok::C_PrefBar_CloseHover);

    // Show the restore glyph (two stacked squares) while maximized, like the
    // main title bar.
    const bool maximized = chrome && chrome->IsMaximized();

    struct SysBtn { const char* id; int kind; };
    const SysBtn order[3] = { {"##tgMin",0}, {"##tgMax",1}, {"##tgClose",2} };
    for (int i = 0; i < 3; ++i) {
        float bx = mx.x - btnW * (3 - i);
        ImGui::SetCursorScreenPos(ImVec2(bx, mn.y));
        ImGui::InvisibleButton(order[i].id, ImVec2(btnW, barH));
        bool hov = ImGui::IsItemHovered();
        // Fire on RELEASE over the button, not on press (native window-button
        // behaviour, matching the main title bar).
        bool clk = ImGui::IsItemDeactivated() && hov;
        ImVec2 bmn = ImGui::GetItemRectMin(), bmx = ImGui::GetItemRectMax();
        if (hov)
            dl->AddRectFilled(bmn, bmx, order[i].kind == 2 ? closeBg : hovBg);
        ImVec2 c((bmn.x + bmx.x) * 0.5f, (bmn.y + bmx.y) * 0.5f);
        const float s = 5.0f * gs;
        const float t = std::max(1.0f, std::floor(gs));
        if (order[i].kind == 0) {
            dl->AddRectFilled(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y + t), glyph);
        } else if (order[i].kind == 1) {
            if (maximized) {
                const float o = 2.0f * gs;        // back-square offset
                dl->AddRect(ImVec2(c.x - s, c.y - s + o),
                            ImVec2(c.x + s - o, c.y + s), glyph, 0, 0, t);
                dl->AddLine(ImVec2(c.x - s + o, c.y - s),
                            ImVec2(c.x + s, c.y - s), glyph, t);
                dl->AddLine(ImVec2(c.x + s, c.y - s),
                            ImVec2(c.x + s, c.y + s - o), glyph, t);
            } else {
                dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
                            glyph, 0, 0, t);
            }
        } else {
            // Snap the four ends to pixel centres so both diagonals are the
            // same length (un-snapped ends get eaten unevenly by AA, making the
            // right branches look shorter) — same as the main bar.
            auto snap = [](float v){ return std::floor(v) + 0.5f; };
            float fl = snap(c.x - s), fr = snap(c.x + s);
            float ft = snap(c.y - s), fb = snap(c.y + s);
            dl->AddLine(ImVec2(fl, ft), ImVec2(fr, fb), glyph, t);
            dl->AddLine(ImVec2(fl, fb), ImVec2(fr, ft), glyph, t);
        }
        if (clk) {
            if (order[i].kind == 0) { if (chrome) chrome->Minimize(); }
            else if (order[i].kind == 1) {
                if (chrome) chrome->ToggleMaximizeOrFullscreen();
            } else sysClose_ = true;
        }
    }
}

void TokenGraphWindow::RenderMenuBar(float width) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float controlH = Flt(Tok::S_Size_ControlHeight) * gs;
    const float pad = 8.0f * gs;
    const float barH = controlH + 6.0f * gs;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImVec2 mx(mn.x + width, mn.y + barH);
    dl->AddRectFilled(mn, mx, ColU32(Tok::S_Surface_Raised));
    dl->AddLine(ImVec2(mn.x, mx.y), ImVec2(mx.x, mx.y),
                ColU32(Tok::S_Color_Border_Default), std::max(1.0f, std::floor(gs)));

    const DS::Context& ctx = ds.GetCurrentContext();
    char ctxStr[96];
    std::snprintf(ctxStr, sizeof(ctxStr), "Theme: %s  |  A11y: %s",
                  ThemeName(ctx.GetTheme()), AccessibilityName(ctx.GetAccessibility()));
    ImVec2 ts = ImGui::CalcTextSize(ctxStr);
    dl->AddText(ImVec2(mn.x + pad, mn.y + (barH - ts.y) * 0.5f),
                ColU32(Tok::S_Color_Text_Default), ctxStr);

    const float ctrlY = mn.y + (barH - ImGui::GetFrameHeight()) * 0.5f;

    // ── Theme visibility toggles (Dark is the always-on base layer). At least
    //    one theme stays checked. These drive which rows the cards show.
    float tx = mn.x + pad + ts.x + 16.0f * gs;
    ImGui::SetCursorScreenPos(ImVec2(tx, ctrlY));
    ImGui::TextUnformatted("Show:");
    for (int t = 0; t < kThemeCount; ++t) {
        ImGui::SameLine(0.0f, 6.0f * gs);
        ImGui::PushID(t);
        bool v = themeVisible_[t];
        if (t == 0) ImGui::BeginDisabled(true);   // Dark always shown
        if (ImGui::Checkbox(ShortThemeName(t), &v)) {
            // Keep at least one theme visible.
            int count = 0; for (int k = 0; k < kThemeCount; ++k) count += themeVisible_[k] ? 1 : 0;
            if (v || count > 1) { themeVisible_[t] = v; layoutDirty_ = true; }
        }
        if (t == 0) ImGui::EndDisabled();
        ImGui::PopID();
    }

    // ── Right side: theme picker + Export to JSON.
    const float exportW = 110.0f * gs;
    const float pickW   = 120.0f * gs;
    ImGui::SetCursorScreenPos(ImVec2(mx.x - pad - exportW, ctrlY));
    if (ImGui::Button("Export to JSON", ImVec2(exportW, 0.0f))) {
        // Generate the chosen theme layer NOW (main thread); an async Save dialog
        // picks the destination and the callback writes the heap-owned JSON.
        std::string* doc = new std::string(UI::ExportTokensJson(exportTheme_));
        const SDL_DialogFileFilter filters[] = {
            { "Carto tokens (*.tokens.json)", "json" },
            { "All files", "*" },
        };
        SDL_ShowSaveFileDialog(
            [](void* ud, const char* const* files, int /*filter*/) {
                std::unique_ptr<std::string> json(static_cast<std::string*>(ud));
                if (files && files[0]) {
                    std::string path = files[0];
                    if (path.find('.') == std::string::npos) path += ".tokens.json";
                    std::ofstream f(path, std::ios::binary | std::ios::trunc);
                    if (f) f << *json;
                }
            },
            doc, nullptr, filters, 2, "tokens.json");
    }
    // Export-theme picker (which layer to export: Dark/base or a specific theme).
    static const char* kThemeItems[kThemeCount] = { "Dark / base", "Light", "Muted Green", "High Contrast" };
    ImGui::SetCursorScreenPos(ImVec2(mx.x - pad - exportW - 8.0f * gs - pickW, ctrlY));
    ImGui::SetNextItemWidth(pickW);
    if (ImGui::BeginCombo("##tgExportTheme", kThemeItems[exportTheme_])) {
        for (int t = 0; t < kThemeCount; ++t)
            if (ImGui::Selectable(kThemeItems[t], exportTheme_ == t)) exportTheme_ = t;
        ImGui::EndCombo();
    }

    // Name filter, left of the export picker.
    const float filterW = 180.0f * gs;
    ImGui::SetCursorScreenPos(ImVec2(mx.x - pad - exportW - 8.0f * gs - pickW - 8.0f * gs - filterW, ctrlY));
    ImGui::SetNextItemWidth(filterW);
    ImGui::InputTextWithHint("##tgFilter", "Filter by name\xE2\x80\xA6",
                             filter_, sizeof(filter_));

    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y));
}

void TokenGraphWindow::RenderCanvas(ImVec2 origin, ImVec2 size) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 cMin = origin, cMax(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(cMin, cMax, true);
    dl->AddRectFilled(cMin, cMax, ColU32(Tok::S_Surface_Canvas));

    // Background canvas input (pan/zoom). Cards/ports are drawn AFTER and must be
    // able to capture clicks over this full-rect button — AllowOverlap lets a
    // later item (a card) win the hover/active even though the canvas covers it.
    ImGui::SetCursorScreenPos(cMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##tgCanvas", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    auto D2S = [&](ImVec2 d) {
        return ImVec2(cMin.x + (d.x - pan_.x) * zoom_, cMin.y + (d.y - pan_.y) * zoom_);
    };
    auto S2D = [&](ImVec2 s) {
        return ImVec2((s.x - cMin.x) / zoom_ + pan_.x, (s.y - cMin.y) / zoom_ + pan_.y);
    };

    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        pan_.x -= io.MouseDelta.x / zoom_;
        pan_.y -= io.MouseDelta.y / zoom_;
    }
    if (canvasHovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            // Ctrl + wheel = vertical scroll (like the Viewport).
            pan_.y -= io.MouseWheel * 60.0f / zoom_;
        } else if (io.KeyShift) {
            // Shift + wheel = horizontal scroll.
            pan_.x -= io.MouseWheel * 60.0f / zoom_;
        } else {
            // Plain wheel = zoom centred on the cursor.
            ImVec2 before = S2D(io.MousePos);
            float fz = io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f;
            zoom_ = std::clamp(zoom_ * fz, 0.15f, 4.0f);
            ImVec2 after = S2D(io.MousePos);
            pan_.x += before.x - after.x;
            pan_.y += before.y - after.y;
        }
    }
    // Horizontal wheel (trackpads / tilt wheels) also pans horizontally.
    if (canvasHovered && io.MouseWheelH != 0.0f)
        pan_.x -= io.MouseWheelH * 60.0f / zoom_;

    // Grid.
    {
        const float step = 64.0f * zoom_;
        const ImU32 grid = ColU32(Tok::S_Color_Border_Default);
        if (step > 6.0f) {
            float ox = cMin.x - std::fmod((pan_.x * zoom_), step);
            for (float gx = ox; gx < cMax.x; gx += step)
                dl->AddLine(ImVec2(gx, cMin.y), ImVec2(gx, cMax.y), grid, 1.0f);
            float oy = cMin.y - std::fmod((pan_.y * zoom_), step);
            for (float gy = oy; gy < cMax.y; gy += step)
                dl->AddLine(ImVec2(cMin.x, gy), ImVec2(cMax.x, gy), grid, 1.0f);
        }
    }

    // Build / theme-switch / layout.
    const int themeNow = (int)ds.GetCurrentContext().GetTheme();
    if (themeNow != lastThemeIdx_) { dirty_ = true; lastThemeIdx_ = themeNow; }
    if (dirty_) RebuildGraph();
    if (layoutDirty_) { RecomputeVisibility(); LayoutGraph(); }

    // Filtering + isolation (over the VISIBLE nodes).
    std::string filterText = filter_;
    std::vector<std::string> filterHits;
    if (!filterText.empty()) {
        std::string q = filterText;
        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
        for (const GraphNode& n : nodes_) {
            if (!n.visible) continue;
            std::string id = n.id;
            std::transform(id.begin(), id.end(), id.begin(), ::tolower);
            if (id.find(q) != std::string::npos) filterHits.push_back(n.id);
        }
    }
    const bool filterActive  = !filterText.empty();
    const bool isolateActive = !isolateSeeds_.empty();
    auto filterKept  = RelatedClosure(filterHits);
    auto isolateKept = RelatedClosure(isolateSeeds_);
    std::unordered_map<std::string, bool> hitSet;
    for (const auto& h : filterHits) hitSet[h] = true;
    auto isKept = [&](const std::string& id) -> bool {
        bool ok = true;
        if (filterActive)  ok = ok && (filterKept.count(id) > 0);
        if (isolateActive) ok = ok && (isolateKept.count(id) > 0);
        return ok;
    };
    auto nodeDimmed = [&](const GraphNode& n) -> bool {
        return (filterActive || isolateActive) && !isKept(n.id);
    };

    const int curThemeIdx = (int)ds.GetCurrentContext().GetTheme();
    // Port Y = centre of theme t's row, using the per-row cached offsets so it
    // lines up with the (possibly tall) editor on that row.
    auto portY = [&](const GraphNode& n, int t) {
        int r = 1 + t;   // row 0 = Global; themes start at 1
        return n.pos.y + n.rowTop[r] + n.rowH[r] * 0.5f;
    };
    // Reversed dataflow: parents (the references) sit to the RIGHT. So a token
    // emits its reference from its RIGHT edge (refPort) into the parent's LEFT
    // edge (inPort). This keeps every link a clean left→right curve.
    auto refPort = [&](const GraphNode& n, int t) {        // child's right edge
        return ImVec2(n.pos.x + n.size.x, portY(n, t)); };
    auto inPort  = [&](const GraphNode& n, int t) {        // parent's left edge
        return ImVec2(n.pos.x, portY(n, t)); };
    // A theme row is shown only when its cached height is non-zero.
    auto themeShown = [&](const GraphNode& n, int t) { return n.rowH[1 + t] > 0.0f; };

    // Port hit-testing (visible, non-dimmed nodes only). A re-wire drag STARTS on
    // a child's reference port (right edge) and DROPS on a candidate parent's in
    // port (left edge) of the same theme.
    const float portHitR = 7.0f * gs;
    int hovRefNode = -1, hovRefTheme = -1;   // right-edge port (drag source)
    int hovInNode  = -1, hovInTheme  = -1;   // left-edge port (drop target)
    {
        ImVec2 m = io.MousePos;
        float bestRef = portHitR * portHitR, bestIn = portHitR * portHitR;
        for (int i = 0; i < (int)nodes_.size(); ++i) {
            if (!nodes_[i].visible || nodeDimmed(nodes_[i])) continue;
            for (int t = 0; t < kThemeCount; ++t) {
                if (!themeShown(nodes_[i], t)) continue;
                ImVec2 rp = D2S(refPort(nodes_[i], t));
                float dr = (rp.x - m.x) * (rp.x - m.x) + (rp.y - m.y) * (rp.y - m.y);
                if (dr < bestRef) { bestRef = dr; hovRefNode = i; hovRefTheme = t; }
                ImVec2 ip = D2S(inPort(nodes_[i], t));
                float di = (ip.x - m.x) * (ip.x - m.x) + (ip.y - m.y) * (ip.y - m.y);
                if (di < bestIn) { bestIn = di; hovInNode = i; hovInTheme = t; }
            }
        }
    }
    if (!dragging_ && hovRefNode >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_ = true; dragSourceId_ = nodes_[hovRefNode].id; dragTheme_ = hovRefTheme;
    }

    // Edges (visible child + visible parent only). The link leaves the child's
    // RIGHT edge and enters the parent's LEFT edge → clean left→right curve.
    const ImU32 edgeCur   = ColU32(Tok::S_Color_Accent_Default);
    const ImU32 edgeOther = ColU32(Tok::S_Color_Text_Subtle);
    const ImU32 edgeDim   = ImGui::ColorConvertFloat4ToU32(
        ImVec4(ds.GetColor(Tok::S_Color_Text_Subtle).x,
               ds.GetColor(Tok::S_Color_Text_Subtle).y,
               ds.GetColor(Tok::S_Color_Text_Subtle).z, 0.15f));
    for (const GraphNode& n : nodes_) {
        if (!n.visible) continue;
        for (int t = 0; t < kThemeCount; ++t) {
            if (!themeShown(n, t)) continue;       // hidden theme → no edge
            const std::string& ref = n.refByTheme[t];
            if (ref.empty()) continue;
            auto it = index_.find(ref);
            if (it == index_.end() || !nodes_[it->second].visible) continue;
            const GraphNode& tgt = nodes_[it->second];
            if (!themeShown(tgt, t)) continue;     // parent row hidden too
            ImVec2 aP = D2S(refPort(n, t));     // child's right edge
            ImVec2 bP = D2S(inPort(tgt, t));    // parent's left edge
            float dx = std::max(30.0f, std::fabs(bP.x - aP.x) * 0.5f);
            const bool cur = (t == curThemeIdx);
            ImU32 col = cur ? edgeCur : edgeOther;
            if (dragging_ && t != dragTheme_) col = edgeDim;
            if (nodeDimmed(n) || nodeDimmed(tgt)) col = edgeDim;
            dl->AddBezierCubic(aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y),
                               bP, col, (cur ? 2.5f : 1.5f) * gs);
        }
    }

    // In-flight dragged edge: from the child's ref port to the cursor; snaps to a
    // compatible parent's in-port of the same theme.
    bool dropValid = false;
    bool cancelledDrag = false;
    if (dragging_) {
        // Cancel the re-wire with a right click or Escape (no override written).
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            dragging_ = false; dragSourceId_.clear(); dragTheme_ = -1;
            cancelledDrag = true;
        }
    }
    if (dragging_) {
        auto sit = index_.find(dragSourceId_);
        if (sit != index_.end() && nodes_[sit->second].visible) {
            ImVec2 aP = D2S(refPort(nodes_[sit->second], dragTheme_));
            ImVec2 bP = io.MousePos;
            std::string reason, targetId;
            if (hovInNode >= 0 && hovInTheme == dragTheme_)
                targetId = nodes_[hovInNode].id;
            if (!targetId.empty() &&
                CanReference(dragSourceId_, targetId, dragTheme_, &reason)) {
                bP = D2S(inPort(nodes_[hovInNode], dragTheme_));
                dropValid = true;
            }
            float dx = std::max(30.0f, std::fabs(bP.x - aP.x) * 0.5f);
            ImU32 col = dropValid ? edgeCur
                                  : ImGui::ColorConvertFloat4ToU32(
                                        ds.GetColor(Tok::S_Color_Accent_Hover));
            dl->AddBezierCubic(aP, ImVec2(aP.x + dx, aP.y), ImVec2(bP.x - dx, bP.y),
                               bP, col, 2.5f * gs);
            dl->AddCircleFilled(bP, 5.0f * gs, col);
            if (!targetId.empty() && !dropValid && !reason.empty())
                ImGui::SetTooltip("%s", reason.c_str());
        } else dragging_ = false;

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (dropValid && hovInNode >= 0) {
                WriteLayer(dragSourceId_, false, kThemes[dragTheme_],
                           DS::TokenValue(nodes_[hovInNode].id));
                dirty_ = true;
            }
            dragging_ = false; dragSourceId_.clear(); dragTheme_ = -1;
        }
    }

    // ── Type cards (left column).
    int typeToggle = -1;
    for (TypeCard& tc : typeCards_) {
        ImVec2 sp = D2S(tc.pos);
        ImVec2 se(sp.x + tc.size.x * zoom_, sp.y + tc.size.y * zoom_);
        if (se.x < cMin.x || sp.x > cMax.x || se.y < cMin.y || sp.y > cMax.y)
            continue;
        DrawTypeCard(tc, sp, zoom_, /*dimmed=*/false, typeToggle);
    }
    if (typeToggle >= 0 && !dragging_) {
        for (TypeCard& tc : typeCards_)
            if ((int)tc.group == typeToggle) { tc.opened = !tc.opened; }
        layoutDirty_ = true;
    }

    // ── Token cards.
    std::string toggleReq, selectReq;
    for (GraphNode& n : nodes_) {
        if (!n.visible) continue;
        ImVec2 sp = D2S(n.pos);
        ImVec2 se(sp.x + n.size.x * zoom_, sp.y + n.size.y * zoom_);
        if (se.x < cMin.x || sp.x > cMax.x || se.y < cMin.y || sp.y > cMax.y)
            continue;
        CardFlags cf;
        cf.dimmed    = nodeDimmed(n);
        cf.filterHit = hitSet.count(n.id) > 0;
        cf.selected  = std::find(sel_.begin(), sel_.end(), n.id) != sel_.end();
        cf.active    = (n.id == active_);
        DrawCard(n, sp, zoom_, cf, toggleReq, selectReq);
    }

    // Toggle open (double-click / eye): keep open + reveal children.
    if (!dragging_ && !toggleReq.empty()) {
        auto it = index_.find(toggleReq);
        if (it != index_.end()) { nodes_[it->second].opened = !nodes_[it->second].opened; }
        layoutDirty_ = true;
    }

    // Selection (single = only; shift = toggle).
    if (!dragging_ && !selectReq.empty()) {
        if (io.KeyShift) {
            auto it = std::find(sel_.begin(), sel_.end(), selectReq);
            if (it != sel_.end()) sel_.erase(it); else sel_.push_back(selectReq);
            active_ = selectReq;
        } else { sel_.clear(); sel_.push_back(selectReq); active_ = selectReq; }
    }

    // Keyboard: Tab isolates the selection (+ parents/children); Esc resets.
    const bool winFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (winFocused && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        if (!sel_.empty()) isolateSeeds_ = sel_;
        else               isolateSeeds_.clear();
    }
    // Esc resets isolation+selection — unless this same Esc just cancelled a drag.
    if (winFocused && !cancelledDrag && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        isolateSeeds_.clear(); sel_.clear(); active_.clear();
    }
    (void)S2D;

    dl->PopClipRect();
}

} // namespace UI

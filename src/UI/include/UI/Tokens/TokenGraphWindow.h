#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// ─────────────────────────────────────────────────────────────────────────────
//  Token Graph window — a Blender "Geometry Nodes"-style view of every design
//  token.
//
//  A real, separate OS window (hosted by App::SecondaryWindow): when open it
//  tears off the main window and draws its OWN title bar + a menu bar (current
//  context layers on the left, a name filter, an "Export to JSON" button on the
//  right) followed by a full-canvas node graph.
//
//  Layout, left → right:
//    • a column of TYPE cards (Color, Number, Vec2, …) — always visible;
//    • Zone 1 = primitives, Zone 2 = semantics, Zone 3 = components, each a band
//      of sub-columns by reference depth, separated by a wide inter-zone gap.
//  Cards are linked by reference curves, one port row per theme.
//
//  Visibility is progressive: by default only the TYPE cards are on the graph.
//  Double-clicking (or the eye button on) a type card reveals that type's
//  primitives; double-clicking a token reveals its descendants (the tokens that
//  reference it, directly and transitively). Each opened card stays open on its
//  own, independent of its parent. Pan/zoom navigation mirrors the Viewport.
//
//  No persistence: the model is rebuilt from the live DesignSystem when dirty_.
//  All value/reference edits go through the OverrideManager (live), exactly like
//  the Settings Customisation page.
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

// The four themes the graph exposes one port-row per. Mirrors ThemeType order.
inline constexpr int kThemeCount = 4;

// Type groups shown as the left-most "type" cards. Float/Int/Ratio collapse into
// one "Number" group; the rest map one-to-one. `Count` is the sentinel.
enum class TypeGroup { Color, Number, Vec2, Bezier, FontFamily, TextStyle, Count };

class TokenGraphWindow {
public:
    // Render the window when *open is true. Sets *open to false when the user
    // closes it (title-bar close button). No-op when *open is false.
    void Render(bool* open);

private:
    // ── Graph model (rebuilt from the live DesignSystem when dirty_) ─────────
    struct GraphNode {
        std::string id;                       // full token id ("primitive.color…")
        std::string fullName;                 // == id (shown in full on the card)
        DesignSystem::TokenLevel level{};
        DesignSystem::ValueType  type{};
        TypeGroup  group{};                   // which type card it belongs under
        // First-hop reference target per theme ("" = literal value, no edge).
        std::string refByTheme[kThemeCount];
        std::vector<int> children;            // node indices that reference this one
        int    descendantCount = 0;           // direct + indirect children
        int    zone = 0;                       // 0=primitive 1=semantic 2=component
        int    column = 0;                    // computed layout column (global)
        ImVec2 pos{0, 0};                     // graph-space top-left (computed)
        ImVec2 size{0, 0};                     // card size in graph-units (computed)
        bool   visible = false;               // currently shown on the graph
        bool   opened  = false;               // user double-clicked → keep open + reveal kids
        // Cached layout metrics (graph-units, scale 1), filled by LayoutGraph so
        // DrawCard renders exactly the height that was reserved. Each value row
        // (index 0 = Global, 1..kThemeCount = themes) is sized by the editor it
        // actually shows on that row (a bezier row is far taller than a colour
        // row), so rows and the whole card grow to fit their content.
        float  rowH[1 + kThemeCount] = {0};   // per-row heights
        float  rowTop[1 + kThemeCount] = {0}; // per-row top offset from body start
        float  bodyTop = 0.0f;                // = title height (cached)
        float  editorW = 0.0f;                // editor column width inside the card
        float  infoTop = 0.0f;                // info block top offset (from card top)
        float  infoH = 0.0f;                  // height of the info block
        std::string infoText;                 // precomposed description/constraints text
    };

    // A type card: the left-most grouping nodes. Always visible.
    struct TypeCard {
        TypeGroup group{};
        std::string label;
        int    tokenCount = 0;                // primitives of this group
        bool   opened = false;
        ImVec2 pos{0, 0};
        ImVec2 size{0, 0};
    };

    void RenderTitleBar(float width);
    void RenderMenuBar(float width);
    void RenderCanvas(ImVec2 origin, ImVec2 size);

    // Rebuild nodes_/index_ from the registry + overrides (edges, children,
    // descendant counts, groups). Visibility/opened state is preserved by id.
    void RebuildGraph();
    // Recompute visibility from opened-state, then lay everything out by zone +
    // reference depth. Run whenever the visible set changes.
    void RecomputeVisibility();
    void LayoutGraph();

    // First-hop reference target of `tokenId` in `theme` (override > theme value
    // > default), or "" when the effective value is a literal.
    std::string EffectiveRefTarget(const std::string& tokenId, int themeIdx) const;

    // Per-card render state for one frame.
    struct CardFlags {
        bool dimmed     = false;  // outside the active filter/isolation (no input)
        bool filterHit  = false;  // matches the name filter (green outline)
        bool selected   = false;  // in the selection (orange outline)
        bool active     = false;  // the active node (bright orange outline)
    };
    // Draw one token card. Reports interactions back via the out-params (only set
    // when not dimmed). `scale` = zoom (widgets scale with it).
    void DrawCard(GraphNode& n, ImVec2 screenPos, float scale, const CardFlags& f,
                  std::string& toggleReq, std::string& selectReq);
    // Draw a type card; reports a toggle request (double-click or eye button).
    void DrawTypeCard(TypeCard& tc, ImVec2 screenPos, float scale, bool dimmed,
                      int& toggleReq);

    // (value editing is inline in DrawCard; no separate popup)

    std::vector<GraphNode> nodes_;
    std::vector<TypeCard>  typeCards_;
    std::unordered_map<std::string, int> index_;   // id → nodes_ index
    bool dirty_ = true;                            // model needs a rebuild
    bool layoutDirty_ = true;                      // visibility/layout needs a pass
    int  lastThemeIdx_ = -1;                       // detect theme switches

    // ── Navigation (mirrors the Viewport pan/zoom) ───────────────────────────
    ImVec2 pan_{0.0f, 0.0f};   // graph-space coords at the canvas top-left
    float  zoom_ = 1.0f;       // pixels per graph-unit

    bool   sysClose_ = false;  // set by the title-bar close button this frame
    char   filter_[128] = {0}; // name filter text (menu-bar dropdown)

    // Which theme rows are shown on the cards. Index 0 = Dark (the base layer,
    // always on); 1..3 = Light / MutedGreen / HighContrast, toggled in the menu
    // bar. At least Dark stays visible. Drives card height + the export picker.
    bool   themeVisible_[kThemeCount] = { true, false, false, false };

    // Theme chosen for the next "Export to JSON" (index into the theme list).
    int    exportTheme_ = 0;   // 0 = Dark / base

    // ── Reference re-wiring drag ─────────────────────────────────────────────
    // While dragging, the source node's IN port for dragTheme_ is detached and
    // follows the cursor; dropping on another node's OUT port of the SAME theme
    // re-points the source's reference (as an override) if the link is valid.
    bool        dragging_ = false;
    std::string dragSourceId_;   // the node whose reference is being re-wired
    int         dragTheme_ = -1; // theme index of the dragged edge

    // Validate that `sourceId` may reference `targetId` (hierarchy + terminal
    // type + acyclicity). `reason` (optional) receives a short failure note.
    bool CanReference(const std::string& sourceId, const std::string& targetId,
                      int themeIdx, std::string* reason = nullptr) const;

    // Transitive closure of seed ids over the reference edges, BOTH directions
    // (ancestors = what they reference, descendants = what references them). Used
    // by the name filter, the per-card isolate, and Tab-isolate.
    std::unordered_map<std::string, bool>
    RelatedClosure(const std::vector<std::string>& seeds) const;

    // ── Selection (mirrors the Viewport: selected set + one active) ──────────
    std::vector<std::string> sel_;     // selected node ids
    std::string              active_;  // active node id (last clicked)

    // ── Isolation ────────────────────────────────────────────────────────────
    // When non-empty, only these seeds and their parents/children are shown.
    std::vector<std::string> isolateSeeds_;
};

} // namespace UI

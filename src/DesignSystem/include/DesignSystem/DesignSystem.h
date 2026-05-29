#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <DesignSystem/Core/ValueConstraint.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <DesignSystem/Tokens/ThemeDefinition.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <imgui.h>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace DesignSystem {

/**
 * Main design system facade - singleton.
 * Provides high-level API for token resolution and style management.
 */
class DesignSystem {
public:
    static DesignSystem& Instance();
    
    /**
     * Initialization.
     * dpiScale: SDL_GetDisplayContentScale() — used to convert logical to physical pixels.
     */
    void Initialize(float dpiScale = 1.0f);
    void Shutdown();
    
    /**
     * Context management.
     */
    Context& GetCurrentContext() { return currentContext_; }
    const Context& GetCurrentContext() const { return currentContext_; }
    void SetContext(const Context& context);
    
    /**
     * Notify that an override was changed (triggers save).
     */
    void NotifyOverrideChange();
    
    /**
     * Value resolution (with current context).
     */
    ImVec4 GetColor(const std::string& tokenId, bool applyAccessibility = true);
    float GetFloat(const std::string& tokenId);
    int GetInt(const std::string& tokenId);
    ImVec2 GetVec2(const std::string& tokenId);

    /**
     * Strongly-typed accessors. Preferred in application/UI code: a typo is a
     * compile error (no such enumerator) rather than a runtime exception.
     * These just forward to the string overloads via TokName().
     */
    ImVec4 GetColor(Tok t, bool applyAccessibility = true) { return GetColor(TokIdStr(t), applyAccessibility); }
    float  GetFloat(Tok t) { return GetFloat(TokIdStr(t)); }
    int    GetInt(Tok t)   { return GetInt(TokIdStr(t)); }
    ImVec2 GetVec2(Tok t)  { return GetVec2(TokIdStr(t)); }
    
    /**
     * Value resolution (with specific context).
     */
    ImVec4 GetColorValue(const std::string& tokenId, const Context& context,
                        bool applyAccessibility = true);
    float GetFloatValue(const std::string& tokenId, const Context& context);
    int GetIntValue(const std::string& tokenId, const Context& context);
    ImVec2 GetVec2Value(const std::string& tokenId, const Context& context);

    // ─────────────────────────────────────────────────────────────────────────
    //  Scoped cascade resolution (zone → sub-zone → component → element).
    //
    //  A *scope* is a "/"-separated path, e.g. "designExample/toolbar/apply".
    //  Resolution tries the most specific scoped override first and walks up
    //  to the bare (global) token:
    //
    //      token@designExample/toolbar/apply
    //   →  token@designExample/toolbar
    //   →  token@designExample
    //   →  token                              (global, the typed default)
    //
    //  Scoped overrides exist ONLY as OverrideManager entries keyed by
    //  "tokenId@scope" — no extra enum/token is declared, so the strongly
    //  typed token set never grows and uncustomised zones automatically fall
    //  through to the global value (unified styling). This is the single
    //  mechanism for infinitely deep per-zone/per-component theming.
    // ─────────────────────────────────────────────────────────────────────────

    /// Separator used to attach a scope to a token id in the override store.
    static constexpr char kScopeSep = '@';

    /// Compose "tokenId@scope" (or just tokenId when scope is empty).
    static std::string ScopedId(const std::string& tokenId,
                                const std::string& scope);

    /// Master switch for the per-scope resolution system (scoped overrides +
    /// theme definitions + ZoneStyle pushes). TEMPORARILY false while we debug
    /// scoped resolution not propagating token edits to combos / Settings /
    /// DevTest. When false, every token resolves purely through the global
    /// graph + theme/global user overrides, and ZoneStyle pushes are no-ops.
    static constexpr bool kScopesEnabled = false;

    /// Resolve through the scoped cascade. scope = "" behaves exactly like the
    /// unscoped ResolveTokenValue (full backward compatibility).
    TokenValue ResolveScoped(const std::string& tokenId,
                             const std::string& scope, ThemeType theme);

    /// Active-scope stack. EVERY GetColor/GetFloat/GetInt/GetVec2 resolves
    /// through ResolveScoped(token, ActiveScope(), theme), so a scoped
    /// override automatically affects ALL tokens read inside a scoped region
    /// — no per-token wiring. PushZoneStyles/ZoneStyle push the scope here;
    /// PopZoneStyles pops it. Empty stack ("") == old global behaviour.
    void PushActiveScope(const std::string& scope) {
        scopeStack_.push_back(scope);
    }
    void PopActiveScope() {
        if (!scopeStack_.empty()) scopeStack_.pop_back();
    }
    const std::string& ActiveScope() const {
        static const std::string kEmpty;
        return scopeStack_.empty() ? kEmpty : scopeStack_.back();
    }

    // ─────────────────────────────────────────────────────────────────────
    //  COMPONENT USAGE TRACKING
    //
    //  Each rendered widget declares its component identity via a
    //  ComponentScope RAII. While that scope is active, every token read
    //  records a usage entry — and ResolveScoped also records every token it
    //  traverses in the reference chain (component → semantic → primitive),
    //  so a primitive correctly reports the COMPONENTS that ultimately
    //  resolve through it.
    //
    //  Data model: tokenId → componentName → occurrence count.
    //  The Tokens-viewer tab reads GetUsage() and renders the "impact"
    //  column (count of components per token) + the per-component popup.
    //  ResetUsage() should be called once per frame so counts reflect the
    //  most recent frame instead of growing unbounded.
    // ─────────────────────────────────────────────────────────────────────
    using ComponentUsageMap =
        std::unordered_map<std::string,
                           std::unordered_map<std::string, int>>;

    void PushComponent(const std::string& name) {
        componentStack_.push_back(name);
    }
    void PopComponent() {
        if (!componentStack_.empty()) componentStack_.pop_back();
    }
    const std::string& ActiveComponent() const {
        static const std::string kEmpty;
        return componentStack_.empty() ? kEmpty : componentStack_.back();
    }
    const ComponentUsageMap& GetUsage() const { return usage_; }
    void ResetUsage() { usage_.clear(); }

    /// Record one usage of `tokenId` by the currently active component (if
    /// any). Called by ResolveScoped for every token it visits — so the
    /// reference chain is captured end-to-end without per-token wiring.
    void RecordUsage(const std::string& tokenId) {
        if (componentStack_.empty()) return;
        ++usage_[tokenId][componentStack_.back()];
    }

    /// RAII sugar: declare at the top of a widget's render to identify it.
    struct ComponentScope {
        explicit ComponentScope(const std::string& name) {
            Instance().PushComponent(name);
        }
        ~ComponentScope() { Instance().PopComponent(); }
        ComponentScope(const ComponentScope&) = delete;
        ComponentScope& operator=(const ComponentScope&) = delete;
    };

    /**
     * How many ImGui colors/vars a PushZoneStyles call pushed. Returned so
     * the matching PopZoneStyles pops exactly that many — this makes the
     * push/pop pair *re-entrant*: nested zones (zone → sub-component scope)
     * each track their own count instead of sharing one member, which would
     * desync and trigger ImGui's "Missing PopStyleColor()" assert.
     */
    struct ZonePushCounts { int colors = 0; int vars = 0; };

    /**
     * Push every component style/color for a zone, resolved through `scope`.
     * Mirrors PushAllStyles() but per-zone and re-entrant: pass the returned
     * counts back to PopZoneStyles() to restore exactly this push.
     */
    ZonePushCounts PushZoneStyles(const std::string& scope);
    void PopZoneStyles(const ZonePushCounts& counts);

    // ═══════════════════════════════════════════════════════════════════════
    //  SCOPE SEMANTICS  (Lot 6 — authoritative documentation)
    //
    //  A *scope* lets a token be themed differently inside a specific area
    //  of the UI without forking the whole theme. It is the mechanism the
    //  user-facing "Theme editor" exposes; the dev "Token tree" tab exposes
    //  per-scope overrides too.
    //
    //  IDENTITY
    //    Each scope has a slash-separated PATH like
    //        designExample           — a top-level zone (depth 1)
    //        designExample/print     — a sub-zone / sub-component (depth 2)
    //        editors/viewport        — same idea, two segments deep
    //    The empty string ""  is the GLOBAL scope — the implicit parent of
    //    every other scope. A token resolves through GLOBAL when no scoped
    //    override / theme-def is found anywhere deeper.
    //
    //  CASCADE (most-specific level wins)
    //    Reading a token inside scope "a/b/c" consults, in order:
    //      1. user override @ "a/b/c"   (the most specific edit)
    //      2. theme-def     @ "a/b/c"
    //      3. user override @ "a/b"
    //      4. theme-def     @ "a/b"
    //      5. user override @ "a"
    //      6. theme-def     @ "a"
    //      7. theme-def     @ ""        (global theme default)
    //      8. global token  default     (typed value / reference chain)
    //    "Scope specificity wins over layer kind": a theme-def on "a/b/c"
    //    beats a user override on the parent "a" — exactly like CSS.
    //
    //  PROPAGATION
    //    Push the scope with PushZoneStyles(scope) / ZoneStyle RAII (see
    //    above). EVERY GetColor/GetFloat/GetInt/GetVec2 read inside that
    //    region automatically resolves through the active scope — there is
    //    NO per-token wiring. A nested ZoneStyle pushes a deeper scope on
    //    top of the stack; popping returns to the parent scope.
    //
    //  COMPONENT-AS-SCOPE (shared instances)
    //    A scope is *typed* by where it is registered, not where it is
    //    instantiated. If multiple widget instances all run under the same
    //    scope path (e.g. every Viewport leaf inside "editors/viewport"),
    //    editing the token at that scope affects ALL of them at once —
    //    that's what makes "all viewports" a meaningful theme target.
    //    A per-instance override would require a uniquely-pathed scope.
    //
    //  REGISTRATION (explicit, manual)
    //    Scopes are dynamic and arbitrarily deep so the theme editors can't
    //    know them statically. ZoneStyle's ctor (and any code applying a
    //    scope) calls RegisterScope(path, label) once — the registry is
    //    sticky (a scope seen once stays listed for the session) and
    //    idempotent. Parent paths are auto-registered so the tree is always
    //    complete. The editors iterate GetScopes() to build the picker.
    //
    //  AUTHORING A NEW SCOPE
    //    1. Pick a unique path matching the UI hierarchy ("zoneA/subB").
    //    2. Wrap the rendering region in `ZoneStyle z("zoneA/subB", "Sub B");`
    //       (push/pop are RAII; the destructor MUST run before any
    //       surrounding EndChild — ImGui validates the style stack there).
    //    3. Editors pick the scope up automatically; theme-defs and
    //       overrides at that path now take effect for that region.
    // ═══════════════════════════════════════════════════════════════════════

    /// One registered scope: full path ("designExample/print") + a friendly
    /// label for its last segment ("Print button"). depth = number of
    /// '/'-separated segments (1 = top-level zone).
    struct ScopeInfo {
        std::string path;
        std::string label;
        int         depth = 1;
    };

    /// Register (or relabel) a scope path. Parent paths are auto-registered
    /// so the tree is always complete. Empty path is ignored (= global).
    void RegisterScope(const std::string& path, const std::string& label = {});

    /// All registered scopes, sorted by path (stable tree order).
    std::vector<ScopeInfo> GetScopes() const;

    /**
     * RAII sugar around PushZoneStyles/PopZoneStyles. Declare one right after
     * a zone's BeginChild(); every widget until end of scope is themed by the
     * zone's scoped cascade, and styles are popped automatically. Safe to
     * nest (each instance remembers its own push counts).
     *
     * IMPORTANT: ImGui validates the style stack at EndChild()/End(). A
     * ZoneStyle must therefore be destroyed BEFORE the matching EndChild(),
     * so always wrap it in an explicit block that closes before EndChild():
     *
     *   ImGui::BeginChild("Zone");
     *   { DesignSystem::DesignSystem::ZoneStyle z("designExample");
     *     ... widgets ... }            // ZoneStyle pops here
     *   ImGui::EndChild();             // stack already balanced
     */
    struct ZoneStyle {
        // `label` is the friendly name shown in the theme editors for this
        // scope's last segment; defaults to the path if omitted. Registering
        // here is what makes every scope discoverable/editable in the UI.
        explicit ZoneStyle(const std::string& scope,
                           const std::string& label = {}) {
            DesignSystem::Instance().RegisterScope(scope, label);
            counts_ = DesignSystem::Instance().PushZoneStyles(scope);
        }
        ~ZoneStyle() { DesignSystem::Instance().PopZoneStyles(counts_); }
        ZoneStyle(const ZoneStyle&) = delete;
        ZoneStyle& operator=(const ZoneStyle&) = delete;
    private:
        ZonePushCounts counts_;
    };
    
    /**
     * Apply global ImGui style.
     * Called on context change and by TokenEditor to ensure UI updates.
     */
    void ApplyGlobalStyle();
    
    /**
     * Scoped style management.
     */
    void PushAllStyles();
    void PopAllStyles();
    
    /**
     * Access to subsystems.
     */
    TokenRegistry& GetRegistry() { return TokenRegistry::Instance(); }
    OverrideManager& GetOverrideManager() { return overrideManager_; }
    ThemeDefinitionStore& GetThemeDefinitions() { return themeDefs_; }

    /// True if the THEME (not a user override) defines `tokenId` at exactly
    /// `scope` for the current theme. The editors use this to render a row as
    /// a theme base value (no override badge, Reset returns here) instead of
    /// a user override.
    bool HasThemeDefinition(const std::string& tokenId,
                            const std::string& scope) const {
        return themeDefs_.HasExact(tokenId, scope,
                                   currentContext_.GetTheme());
    }
    
    /**
     * Persistence.
     */
    void SaveState();
    bool LoadState();
    
    /**
     * Recursively resolve token value (public for TokenEditor).
     */
    TokenValue ResolveTokenValue(const std::string& tokenId, ThemeType theme);

    /**
     * Effective numeric constraint of a token: its own constraint, or — if it
     * is a Reference with none — the constraint inherited from the token it
     * resolves to. This is what UI sliders must use so a Reference field
     * (e.g. component.style.disabledAlpha) is still bounded by the range it
     * ultimately drives. Returns an empty constraint when there is none.
     */
    ValueConstraint GetEffectiveConstraint(const std::string& tokenId);

    /**
     * Trace the chain of references for `tokenId` in `theme`, from the token
     * itself down to the first non-reference value (or the first token not
     * found). Each entry is the id of one token visited; the last entry's
     * resolved value is what you'd get from `ResolveTokenValue`.
     *
     * Used by the TokenEditor to display "A → B → C → #1A73E8" trails. Also
     * a future cycle-detection point: today the function bounds itself to 64
     * hops and silently stops to avoid hangs.
     */
    struct ReferenceChainEntry {
        std::string tokenId;       // token visited
        bool found;                // false if this id has no registered token
        bool overridden;           // true if an override (global or theme) supplied the value
        TokenValue value;          // the value held at this step (reference or terminal)
    };
    std::vector<ReferenceChainEntry> GetReferenceChain(const std::string& tokenId,
                                                       ThemeType theme);

    /**
     * Scale accessors — for code that needs to scale raw pixel values
     * (e.g. hardcoded toolbar widths) so they track the UI scale + DPI.
     *   GetUiScale()       = current semantic.scale.default token value
     *   GetGlobalScale()   = uiScale * dpiScale  (full physical-pixel multiplier)
     *   GetDpiScale()      = monitor DPI scale captured at init
     */
    float GetUiScale() const;
    float GetGlobalScale() const;
    float GetDpiScale() const { return dpiScale_; }

private:
    DesignSystem();
    ~DesignSystem();
    DesignSystem(const DesignSystem&) = delete;
    DesignSystem& operator=(const DesignSystem&) = delete;

    ImVec4 ApplyAccessibility(const ImVec4& color, AccessibilityType type);

    Context              currentContext_;
    OverrideManager      overrideManager_;
    ThemeDefinitionStore themeDefs_;          // theme base layer (not overrides)
    int                  stylesPushedCount_;
    float                dpiScale_ = 1.0f;
    // Sticky scope registry (path → label), populated by RegisterScope.
    std::map<std::string, std::string> scopeRegistry_;
    // Active-scope stack: top = the scope every Get* currently resolves
    // through. Pushed/popped by PushZoneStyles/PopZoneStyles (ZoneStyle).
    std::vector<std::string> scopeStack_;
    // Active-component stack + per-frame usage map (tokenId → component → n).
    // Populated by ResolveScoped at every chain step while a ComponentScope
    // is active. ResetUsage() should be called once per frame.
    std::vector<std::string> componentStack_;
    ComponentUsageMap        usage_;
};

} // namespace DesignSystem
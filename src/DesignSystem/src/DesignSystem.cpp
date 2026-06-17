#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Persistence/Serialization.h>
#include <DesignSystem/Accessibility/ColorBlindness.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <stdexcept>
#include <iostream>

namespace DesignSystem {

DesignSystem& DesignSystem::Instance() {
    static DesignSystem* instance = new DesignSystem();
    return *instance;
}

DesignSystem::DesignSystem() : stylesPushedCount_(0) {}

DesignSystem::~DesignSystem() {
    Shutdown();
}

void DesignSystem::Initialize(float dpiScale) {
    dpiScale_ = dpiScale;
    Serialization::Initialize();
    TokenRegistry::Instance().InitializeDefaultTokens();

    // Theme base layer (per-zone look that belongs to the theme, not to user
    // overrides). Authored in code; resolved before overrides fall through.
    // TEMPORARILY DISABLED: the per-scope theme-definition system is turned off
    // (see kScopesEnabled in DesignSystem.h) while we debug scoped-resolution
    // not propagating token edits to combos / Settings / DevTest. With scopes
    // off, every token resolves purely through global + theme/global overrides.
    themeDefs_.Clear();
    if (kScopesEnabled)
        InstallThemeDefinitions(themeDefs_);

    if (!LoadState()) {
        currentContext_ = Context(ThemeType::Dark, AccessibilityType::None);
    }

    ApplyGlobalStyle();
}

void DesignSystem::Shutdown() {
    SaveState();
    Serialization::Shutdown();
}

void DesignSystem::SetContext(const Context& context) {
    currentContext_ = context;
    SaveState();
    ApplyGlobalStyle();
}

void DesignSystem::NotifyOverrideChange() {
    SaveState();
    ApplyGlobalStyle();
}

ImVec4 DesignSystem::GetColor(const std::string& tokenId, bool applyAccessibility) {
    return GetColorValue(tokenId, currentContext_, applyAccessibility);
}

float DesignSystem::GetFloat(const std::string& tokenId) {
    return GetFloatValue(tokenId, currentContext_);
}

int DesignSystem::GetInt(const std::string& tokenId) {
    return GetIntValue(tokenId, currentContext_);
}

ImVec2 DesignSystem::GetVec2(const std::string& tokenId) {
    return GetVec2Value(tokenId, currentContext_);
}

// Every typed getter resolves through the ACTIVE SCOPE stack. With an empty
// stack ResolveScoped(token, "", theme) == ResolveTokenValue(token, theme),
// so behaviour outside any ZoneStyle is unchanged. Inside a scoped region
// EVERY token automatically honours that scope's theme-defs and overrides —
// no per-token wiring, no hardcoded list (fixes "scoped override only works
// for a handful of tokens"). The reference chain inside ResolveScoped stays
// in-scope, so semantic→primitive cascades resolve per scope too.
ImVec4 DesignSystem::GetColorValue(const std::string& tokenId, const Context& context,
                                  bool applyAccessibility) {
    TokenValue value = ResolveScoped(tokenId, ActiveScope(), context.GetTheme());
    if (value.GetType() != ValueType::Color)
        throw std::runtime_error("Token is not a color: " + tokenId);
    ImVec4 color = value.AsColor();
    if (applyAccessibility && context.GetAccessibility() != AccessibilityType::None)
        color = ApplyAccessibility(color, context.GetAccessibility());
    return color;
}

float DesignSystem::GetFloatValue(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveScoped(tokenId, ActiveScope(), context.GetTheme());
    if (value.GetType() != ValueType::Float)
        throw std::runtime_error("Token is not a float: " + tokenId);
    return value.AsFloat();
}

int DesignSystem::GetIntValue(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveScoped(tokenId, ActiveScope(), context.GetTheme());
    if (value.GetType() != ValueType::Int)
        throw std::runtime_error("Token is not an int: " + tokenId);
    return value.AsInt();
}

ImVec2 DesignSystem::GetVec2Value(const std::string& tokenId, const Context& context) {
    TokenValue value = ResolveScoped(tokenId, ActiveScope(), context.GetTheme());
    if (value.GetType() != ValueType::Vec2)
        throw std::runtime_error("Token is not a Vec2: " + tokenId);
    return value.AsVec2();
}

// Scale a "thin line" value so it never collapses below 1 physical pixel when
// the user picks a small uiScale. 0 stays 0 (intentional no-border).
static inline float ScaleThinLine(float base, float scale) {
    if (base <= 0.0f) return 0.0f;
    float v = base * scale;
    return v < 1.0f ? 1.0f : v;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scope binding table — the single source of truth for "what a zone restyles".
//
//  Each entry maps an ImGui style slot (color or var) to the design-system
//  token that drives it. ApplyGlobalStyle and PushZoneStyles BOTH iterate this
//  same table, so:
//   • Adding a new component-bound ImGui slot here covers BOTH the global
//     style AND every scope automatically — no per-zone hardcoded list.
//   • A zone-scoped override on the token now propagates to the corresponding
//     ImGui slot inside that zone without any extra code path.
//
//  Bool flag `scopable` keeps a few global-only slots (e.g. anti-aliasing,
//  tessellation, cursor scale) out of per-scope pushes — they are app-global
//  by nature and don't make sense to override per-zone.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
struct ColBind { ImGuiCol slot; Tok tok; bool scopable; };
struct VarFBind { ImGuiStyleVar slot; Tok tok; bool scaled; bool scopable; };

constexpr ColBind kColorBindings[] = {
    // Per-component colours (all scopable: redefining the token in a scope
    // automatically rethemes that ImGui slot inside the scope).
    { ImGuiCol_Text,                       Tok::S_Color_Text_Default,            true },
    { ImGuiCol_TextDisabled,               Tok::S_Color_Text_Disabled,           true },
    { ImGuiCol_WindowBg,                   Tok::C_Window_Background,             true },
    { ImGuiCol_ChildBg,                    Tok::C_Child_Background,              true },
    { ImGuiCol_PopupBg,                    Tok::C_Popup_Background,              true },
    { ImGuiCol_Border,                     Tok::C_Window_BorderColor,            true },
    { ImGuiCol_BorderShadow,               Tok::S_Color_Border_Shadow,           true },
    { ImGuiCol_FrameBg,                    Tok::C_Frame_Background,              true },
    { ImGuiCol_FrameBgHovered,             Tok::C_Frame_BackgroundHover,         true },
    { ImGuiCol_FrameBgActive,              Tok::C_Frame_BackgroundDown,          true },
    { ImGuiCol_TitleBg,                    Tok::S_Color_Background_Title,        true },
    { ImGuiCol_TitleBgActive,              Tok::S_Color_Background_TitleActive,  true },
    { ImGuiCol_TitleBgCollapsed,           Tok::S_Color_Background_TitleCollapsed,true },
    { ImGuiCol_MenuBarBg,                  Tok::C_Popup_MenuBarBackground,       true },
    { ImGuiCol_ScrollbarBg,                Tok::C_Scrollbar_Background,          true },
    { ImGuiCol_ScrollbarGrab,              Tok::C_Scrollbar_Grab,                true },
    { ImGuiCol_ScrollbarGrabHovered,       Tok::C_Scrollbar_GrabHover,           true },
    { ImGuiCol_ScrollbarGrabActive,        Tok::C_Scrollbar_GrabDown,            true },
    { ImGuiCol_CheckMark,                  Tok::C_Checkbox_Mark,                 true },
    { ImGuiCol_CheckboxSelectedBg,         Tok::C_Checkbox_BackgroundSelected,   true },
    { ImGuiCol_SliderGrab,                 Tok::C_Slider_Grab,                   true },
    { ImGuiCol_SliderGrabActive,           Tok::C_Slider_GrabDown,               true },
    { ImGuiCol_Button,                     Tok::C_Button_Background,             true },
    { ImGuiCol_ButtonHovered,              Tok::C_Button_BackgroundHover,        true },
    { ImGuiCol_ButtonActive,               Tok::C_Button_BackgroundDown,         true },
    { ImGuiCol_Header,                     Tok::C_Header_Background,             true },
    { ImGuiCol_HeaderHovered,              Tok::C_Header_BackgroundHover,        true },
    { ImGuiCol_HeaderActive,               Tok::C_Header_BackgroundDown,         true },
    { ImGuiCol_Separator,                  Tok::C_Separator_Color,               true },
    { ImGuiCol_SeparatorHovered,           Tok::C_Separator_Hover,               true },
    { ImGuiCol_SeparatorActive,            Tok::C_Separator_Down,                true },
    { ImGuiCol_ResizeGrip,                 Tok::C_ResizeGrip_Color,              true },
    { ImGuiCol_ResizeGripHovered,          Tok::C_ResizeGrip_Hover,              true },
    { ImGuiCol_ResizeGripActive,           Tok::C_ResizeGrip_Down,               true },
    { ImGuiCol_InputTextCursor,            Tok::C_Frame_InputTextCursor,         true },
    { ImGuiCol_TabHovered,                 Tok::C_Tab_BackgroundHover,           true },
    { ImGuiCol_Tab,                        Tok::C_Tab_Background,                true },
    { ImGuiCol_TabSelected,                Tok::C_Tab_BackgroundSelected,        true },
    { ImGuiCol_TabSelectedOverline,        Tok::C_Tab_OverlineSelected,          true },
    { ImGuiCol_TabDimmed,                  Tok::C_Tab_BackgroundDimmed,          true },
    { ImGuiCol_TabDimmedSelected,          Tok::C_Tab_BackgroundDimmedSelected,  true },
    { ImGuiCol_TabDimmedSelectedOverline,  Tok::C_Tab_OverlineDimmed,            true },
    { ImGuiCol_DockingPreview,             Tok::S_Color_Accent_DockingPreview,   true },
    { ImGuiCol_DockingEmptyBg,             Tok::S_Color_Background_DockingEmpty, true },
    { ImGuiCol_PlotLines,                  Tok::S_Color_DataViz_Line,            true },
    { ImGuiCol_PlotLinesHovered,           Tok::S_Color_DataViz_LineHover,       true },
    { ImGuiCol_PlotHistogram,              Tok::S_Color_DataViz_Histogram,       true },
    { ImGuiCol_PlotHistogramHovered,       Tok::S_Color_DataViz_HistogramHover,  true },
    { ImGuiCol_TableHeaderBg,              Tok::C_Table_HeaderBackground,        true },
    { ImGuiCol_TableBorderStrong,          Tok::C_Table_BorderStrong,            true },
    { ImGuiCol_TableBorderLight,           Tok::C_Table_BorderLight,             true },
    { ImGuiCol_TableRowBg,                 Tok::C_Table_RowBackground,           true },
    { ImGuiCol_TableRowBgAlt,              Tok::C_Table_RowBackgroundAlt,        true },
    { ImGuiCol_TextLink,                   Tok::S_Color_Text_Link,               true },
    { ImGuiCol_TextSelectedBg,             Tok::S_Color_Background_TextSelection,true },
    { ImGuiCol_TreeLines,                  Tok::S_Color_Border_TreeLine,         true },
    { ImGuiCol_DragDropTarget,             Tok::S_Color_Accent_DropTarget,       true },
    { ImGuiCol_DragDropTargetBg,           Tok::S_Color_Background_DropTarget,   true },
    { ImGuiCol_UnsavedMarker,              Tok::C_Tab_UnsavedMarker,             true },
    { ImGuiCol_NavCursor,                  Tok::S_Color_Focus_Default,           true },
    { ImGuiCol_NavWindowingHighlight,      Tok::S_Color_Focus_Windowing,         true },
    { ImGuiCol_NavWindowingDimBg,          Tok::S_Color_Background_DimWindowing, true },
    { ImGuiCol_ModalWindowDimBg,           Tok::S_Color_Background_DimModal,     true },
};

// Float ImGuiStyleVar bindings used by PushZoneStyles for scopable per-zone
// look. `scaled` = true means the token value is in logical px and must be
// multiplied by globalScale at push time.
constexpr VarFBind kVarBindings[] = {
    { ImGuiStyleVar_FrameRounding,    Tok::C_Frame_CornerRadius,      true,  true },
    { ImGuiStyleVar_WindowRounding,   Tok::C_Window_CornerRadius,     true,  true },
    { ImGuiStyleVar_ChildRounding,    Tok::C_Child_CornerRadius,      true,  true },
    { ImGuiStyleVar_PopupRounding,    Tok::C_Popup_CornerRadius,      true,  true },
    { ImGuiStyleVar_GrabRounding,     Tok::C_Slider_CornerRadius,     true,  true },
    { ImGuiStyleVar_TabRounding,      Tok::C_Tab_CornerRadius,        true,  true },
    { ImGuiStyleVar_ScrollbarRounding,Tok::C_Scrollbar_CornerRadius,  true,  true },
};
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  ApplyGlobalStyle
//
//  Every ImGuiStyle field and every ImGuiCol_ is now driven by a token. The
//  defaults in the schema equal ImGui's own defaults, so this produces a
//  pixel-identical look until the user overrides something. Scaling/DPI/font
//  logic is preserved from the previous implementation.
// ─────────────────────────────────────────────────────────────────────────────
void DesignSystem::ApplyGlobalStyle() {
    // Always target the MAIN context's style. An override may be committed while
    // a secondary window's ImGui context is current (e.g. editing in the
    // Preferences window); applying the style there would be lost (the secondary
    // window copies the main style every frame). Switch to the main context for
    // the duration of this function, restoring the caller's context on exit.
    struct ContextGuard {
        ImGuiContext* saved;
        bool          switched;
        explicit ContextGuard(ImGuiContext* target)
            : saved(ImGui::GetCurrentContext()), switched(false) {
            if (target && target != saved) {
                ImGui::SetCurrentContext(target);
                switched = true;
            }
        }
        ~ContextGuard() { if (switched) ImGui::SetCurrentContext(saved); }
    } _ctxGuard(mainImGuiContext_);

    ImGuiStyle& style = ImGui::GetStyle();

    // Capture the DPI-scaled dark theme as the immutable baseline (once, after
    // StyleColorsDark() + ScaleAllSizes(dpiScale)).
    static ImGuiStyle baseStyle;
    static bool       baseStyleCaptured = false;
    if (!baseStyleCaptured) {
        baseStyle         = style;
        baseStyleCaptured = true;
    }

    try {
        float uiScale = GetFloat(Tok::S_Scale_Default);
        if (uiScale < 0.1f) uiScale = 0.1f;

        // Reset to baseline (DPI-baked) then apply UI scale absolutely.
        style = baseStyle;
        style.ScaleAllSizes(uiScale);

        // ── Fonts (imgui 1.92+ lazy rasterisation) ──────────────────────────
        float baseFontSize = 14.0f;
        try { baseFontSize = GetFloat(Tok::S_FontSize_Default); } catch (...) {}
        if (baseFontSize < 1.0f) baseFontSize = 14.0f;

        float fontScale = 1.0f;
        try { fontScale = GetFloat(Tok::S_FontScale_Default); } catch (...) {}
        if (fontScale < 0.1f) fontScale = 0.1f;

        style.FontSizeBase           = baseFontSize;
        style._NextFrameFontSizeBase = baseFontSize;
        style.FontScaleMain          = uiScale * fontScale;
        style.FontScaleDpi           = dpiScale_;

        // Token metrics are in logical px; convert to physical pixels.
        const float es = uiScale * dpiScale_;
        auto S  = [&](float v) { return v * es; };               // scale a length
        auto SV = [&](ImVec2 v) { return ImVec2(v.x * es, v.y * es); };

        // Helper: token → ImVec4 with accessibility applied (mirrors GetColor).
        // ── Global (semantic base) ──────────────────────────────────────────
        style.Alpha         = GetFloat(Tok::S_Opacity_Default);
        style.DisabledAlpha = GetFloat(Tok::S_Opacity_Disabled);

        // ── Window component ────────────────────────────────────────────────
        style.WindowPadding            = SV(GetVec2(Tok::C_Window_Padding));
        style.WindowRounding           = S(GetFloat(Tok::C_Window_CornerRadius));
        style.WindowBorderSize         = ScaleThinLine(GetBorderWidth(Tok::C_Window_BorderWidth), uiScale);
        style.WindowBorderHoverPadding = S(GetFloat(Tok::C_Window_BorderHoverPadding));
        style.WindowMinSize            = SV(GetVec2(Tok::C_Window_MinSize));
        style.WindowTitleAlign         = GetVec2(Tok::C_Window_TitleAlign);
        style.WindowMenuButtonPosition = (ImGuiDir)GetInt(Tok::C_Window_MenuButtonPosition);
        // ── Child component ─────────────────────────────────────────────────
        style.ChildRounding            = S(GetFloat(Tok::C_Child_CornerRadius));
        style.ChildBorderSize          = ScaleThinLine(GetBorderWidth(Tok::C_Child_BorderWidth), uiScale);
        // ── Popup component ─────────────────────────────────────────────────
        style.PopupRounding            = S(GetFloat(Tok::C_Popup_CornerRadius));
        style.PopupBorderSize          = ScaleThinLine(GetBorderWidth(Tok::C_Popup_BorderWidth), uiScale);

        // ── Frame component ─────────────────────────────────────────────────
        style.FramePadding      = SV(GetVec2(Tok::C_Frame_Padding));
        style.FrameRounding     = S(GetFloat(Tok::C_Frame_CornerRadius));
        style.FrameBorderSize   = ScaleThinLine(GetBorderWidth(Tok::C_Frame_BorderWidth), uiScale);

        // ── Global layout (semantic.spacing.*: not widgets) ─────────────────
        style.ItemSpacing       = SV(GetVec2(Tok::S_Config_ItemSpacing));
        style.ItemInnerSpacing  = SV(GetVec2(Tok::S_Config_ItemInnerSpacing));
        style.CellPadding       = SV(GetVec2(Tok::S_Config_CellPadding));
        style.TouchExtraPadding = SV(GetVec2(Tok::S_Config_TouchExtraPadding));
        style.IndentSpacing     = S(GetFloat(Tok::S_Config_IndentSpacing));
        style.ColumnsMinSpacing = S(GetFloat(Tok::S_Config_ColumnsMinSpacing));
        style.GrabMinSize       = S(GetFloat(Tok::S_Config_GrabMinSize));
        style.LogSliderDeadzone = S(GetFloat(Tok::S_Config_LogSliderDeadzone));

        // ── Scrollbar component ─────────────────────────────────────────────
        style.ScrollbarSize     = S(GetFloat(Tok::C_Scrollbar_Size));
        style.ScrollbarRounding = S(GetFloat(Tok::C_Scrollbar_CornerRadius));
        style.ScrollbarPadding  = S(GetFloat(Tok::C_Scrollbar_Padding));
        // ── Slider component ────────────────────────────────────────────────
        style.GrabRounding      = S(GetFloat(Tok::C_Slider_CornerRadius));

        // ── Image component ─────────────────────────────────────────────────
        style.ImageRounding   = S(GetFloat(Tok::C_Image_CornerRadius));
        style.ImageBorderSize = ScaleThinLine(GetBorderWidth(Tok::C_Image_BorderWidth), uiScale);

        // ── Tab component ───────────────────────────────────────────────────
        style.TabRounding                      = S(GetFloat(Tok::C_Tab_CornerRadius));
        style.TabBorderSize                    = ScaleThinLine(GetBorderWidth(Tok::C_Tab_BorderWidth), uiScale);
        style.TabMinWidthBase                  = S(GetFloat(Tok::C_Tab_MinWidthBase));
        style.TabMinWidthShrink                = S(GetFloat(Tok::C_Tab_MinWidthShrink));
        style.TabCloseButtonMinWidthSelected   = GetFloat(Tok::C_Tab_CloseButtonMinWidthSelected);
        style.TabCloseButtonMinWidthUnselected = GetFloat(Tok::C_Tab_CloseButtonMinWidthUnselected);
        style.TabBarBorderSize                 = ScaleThinLine(GetBorderWidth(Tok::C_Tab_BarBorderWidth), uiScale);
        style.TabBarOverlineSize               = S(GetFloat(Tok::C_Tab_BarOverlineWidth));

        // ── Table component / tree lines (semantic) ─────────────────────────
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        style.TableAngledHeadersAngle     = GetFloat(Tok::C_Table_AngledHeadersAngle) * kDegToRad;
        style.TableAngledHeadersTextAlign = GetVec2(Tok::C_Table_AngledHeadersTextAlign);
        switch (GetInt(Tok::S_Config_TreeLinesFlags)) {
            case 1:  style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull;    break;
            case 2:  style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes; break;
            default: style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesNone;    break;
        }
        style.TreeLinesSize     = ScaleThinLine(GetFloat(Tok::S_Config_TreeLinesSize), uiScale);
        style.TreeLinesRounding = S(GetFloat(Tok::C_Window_CornerRadius));

        // ── Drag & drop component ───────────────────────────────────────────
        style.DragDropTargetRounding   = S(GetFloat(Tok::C_DragDropTarget_CornerRadius));
        style.DragDropTargetBorderSize = ScaleThinLine(GetBorderWidth(Tok::C_DragDropTarget_BorderWidth), uiScale);
        style.DragDropTargetPadding    = S(GetFloat(Tok::C_DragDropTarget_Padding));

        // ── Separator component + global widget config (semantic) ───────────
        style.SeparatorSize           = S(GetFloat(Tok::C_Separator_Size));
        style.SeparatorTextBorderSize = S(GetFloat(Tok::C_Separator_TextBorderWidth));
        style.ColorMarkerSize         = S(GetFloat(Tok::S_Config_ColorMarkerSize));
        style.ColorButtonPosition     = (ImGuiDir)GetInt(Tok::S_Config_ColorButtonPosition);
        style.ButtonTextAlign         = GetVec2(Tok::S_Config_ButtonTextAlign);
        style.SelectableTextAlign     = GetVec2(Tok::S_Config_SelectableTextAlign);
        style.SeparatorTextAlign      = GetVec2(Tok::S_Config_SeparatorTextAlign);
        style.SeparatorTextPadding    = SV(GetVec2(Tok::S_Config_SeparatorTextPadding));
        style.DisplayWindowPadding    = SV(GetVec2(Tok::S_Config_DisplayWindowPadding));
        style.DisplaySafeAreaPadding  = SV(GetVec2(Tok::S_Config_DisplaySafeAreaPadding));
        style.DockingNodeHasCloseButton = GetInt(Tok::C_Docking_NodeHasCloseButton) != 0;
        style.DockingSeparatorSize    = S(GetFloat(Tok::C_Docking_SeparatorSize));
        style.MouseCursorScale        = GetFloat(Tok::S_Config_MouseCursorScale);
        style.AntiAliasedLines        = GetInt(Tok::S_Config_AntiAliasedLines) != 0;
        style.AntiAliasedLinesUseTex  = GetInt(Tok::S_Config_AntiAliasedLinesUseTex) != 0;
        style.AntiAliasedFill         = GetInt(Tok::S_Config_AntiAliasedFill) != 0;
        style.CurveTessellationTol    = GetFloat(Tok::S_Config_CurveTessellationTol);
        style.CircleTessellationMaxError = GetFloat(Tok::S_Config_CircleTessellationMaxError);

        // ── Hover behaviour (semantic.interaction.*) ────────────────────────
        style.HoverStationaryDelay = GetFloat(Tok::S_Config_HoverDelayStationary);
        style.HoverDelayShort      = GetFloat(Tok::S_Config_HoverDelayShort);
        style.HoverDelayNormal     = GetFloat(Tok::S_Config_HoverDelayNormal);

        // ── Colors — iterate the binding table (single source of truth) ─────
        //   The same table is consumed by PushZoneStyles for scoped per-zone
        //   re-skinning, so adding/removing a binding above covers BOTH paths.
        ImVec4* c = style.Colors;
        for (const auto& b : kColorBindings) {
            try { c[b.slot] = GetColor(b.tok); } catch (...) {}
        }

    } catch (...) {
        // Tokens may not exist yet during the very first initialization pass.
    }
}

float DesignSystem::GetUiScale() const {
    try {
        auto& self = const_cast<DesignSystem&>(*this);
        float s = self.GetFloat(Tok::S_Scale_Default);
        return s < 0.1f ? 0.1f : s;
    } catch (...) {
        return 1.0f;
    }
}

float DesignSystem::GetGlobalScale() const {
    return GetUiScale() * dpiScale_;
}

bool DesignSystem::BordersEnabled() {
    try { return GetInt(Tok::S_Border_Enabled) != 0; }
    catch (...) { return true; }   // missing token → borders on (safe default)
}

float DesignSystem::GetBorderWidth(Tok widthToken) {
    if (!BordersEnabled()) return 0.0f;
    try { return GetFloat(widthToken); } catch (...) { return 0.0f; }
}

void DesignSystem::PushAllStyles() {
    stylesPushedCount_ = 0;
    try {
        float uiScale = 1.0f;
        try { uiScale = GetFloat(Tok::S_Scale_Default); } catch (...) {}
        if (uiScale < 0.1f) uiScale = 0.1f;
        float es = uiScale * dpiScale_;

        ImVec2 fp = GetVec2(Tok::C_Frame_Padding);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  GetFloat(Tok::C_Frame_CornerRadius)  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(fp.x * es, fp.y * es));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, GetFloat(Tok::C_Window_CornerRadius) * es);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,  GetFloat(Tok::C_Child_CornerRadius)  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  GetFloat(Tok::C_Popup_CornerRadius)  * es);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,   GetFloat(Tok::C_Slider_CornerRadius) * es);
        stylesPushedCount_ += 6;

        ImGui::PushStyleColor(ImGuiCol_FrameBg,        GetColor(Tok::C_Frame_Background));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, GetColor(Tok::C_Frame_BackgroundHover));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  GetColor(Tok::C_Frame_BackgroundDown));
        ImGui::PushStyleColor(ImGuiCol_Button,         GetColor(Tok::C_Button_Background));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  GetColor(Tok::C_Button_BackgroundHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   GetColor(Tok::C_Button_BackgroundDown));
        stylesPushedCount_ += 6;
    } catch (...) {}
}

void DesignSystem::PopAllStyles() {
    if (stylesPushedCount_ > 0) {
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(6);
        stylesPushedCount_ = 0;
    }
}

DesignSystem::ZonePushCounts
DesignSystem::PushZoneStyles(const std::string& scope) {
    ZonePushCounts counts;  // local → re-entrant; nested zones don't clobber

    // Scopes temporarily disabled: do nothing so zone bodies render with the
    // global style. PopZoneStyles sees zero counts and also no-ops.
    if (!kScopesEnabled)
        return counts;

    // Make `scope` the ACTIVE scope for the whole zone body: every
    // ds.GetColor/GetFloat/GetVec2 read inside now resolves through it
    // automatically. The explicit ImGui Push* calls below mirror the same
    // table that ApplyGlobalStyle uses, so ImGui-internal widgets that read
    // style.Colors[]/style.* directly (most of them) also see the scoped
    // values without per-token wiring.
    PushActiveScope(scope);

    ThemeType theme = currentContext_.GetTheme();
    AccessibilityType acc = currentContext_.GetAccessibility();

    auto pushCol = [&](ImGuiCol slot, Tok t) {
        try {
            TokenValue v = ResolveScoped(TokIdStr(t), scope, theme);
            if (v.GetType() != ValueType::Color) return;
            ImVec4 c = v.AsColor();
            if (acc != AccessibilityType::None) c = ApplyAccessibility(c, acc);
            ImGui::PushStyleColor(slot, c);
            ++counts.colors;
        } catch (...) {}
    };
    auto pushVarF = [&](ImGuiStyleVar sv, Tok t, bool scaled) {
        try {
            TokenValue v = ResolveScoped(TokIdStr(t), scope, theme);
            if (v.GetType() != ValueType::Float) return;
            float fv = v.AsFloat();
            if (scaled) fv *= GetGlobalScale();
            ImGui::PushStyleVar(sv, fv);
            ++counts.vars;
        } catch (...) {}
    };

    // Iterate the SAME table ApplyGlobalStyle uses, so every scopable slot is
    // covered automatically; adding a binding above covers every scope.
    for (const auto& b : kColorBindings)
        if (b.scopable) pushCol(b.slot, b.tok);
    for (const auto& b : kVarBindings)
        if (b.scopable) pushVarF(b.slot, b.tok, b.scaled);

    return counts;
}

void DesignSystem::PopZoneStyles(const ZonePushCounts& counts) {
    if (counts.colors > 0) ImGui::PopStyleColor(counts.colors);
    if (counts.vars   > 0) ImGui::PopStyleVar(counts.vars);
    // Pop the active scope pushed by the matching PushZoneStyles. ZoneStyle
    // is strict RAII so the stack stays balanced and correctly nested.
    PopActiveScope();
}

void DesignSystem::SaveState() {
    Serialization::SaveState(currentContext_, overrideManager_);
}

bool DesignSystem::LoadState() {
    return Serialization::LoadState(currentContext_, overrideManager_);
}

TokenValue DesignSystem::ResolveTokenValue(const std::string& tokenId, ThemeType theme) {
    // Usage tracking is centralised in ResolveScoped (which is the public
    // entry from Get*). Internal reference recursion goes BACK through
    // ResolveScoped(ref, "", theme) so every chain step is recorded exactly
    // once — no duplicate counting of the initial tokenId.
    auto& registry = TokenRegistry::Instance();

    const Override* override = overrideManager_.GetBestOverride(tokenId, theme);
    if (override) {
        TokenValue value = override->GetValue();
        if (value.IsReference())
            return ResolveScoped(value.AsReference(), "", theme);
        return value;
    }

    auto token = registry.GetToken(tokenId);
    if (!token)
        throw std::runtime_error("Token not found: " + tokenId);

    Context themeContext(theme, AccessibilityType::None);
    const TokenValue* themeValue = token->GetContextValue(themeContext);
    if (themeValue) {
        if (themeValue->IsReference())
            return ResolveScoped(themeValue->AsReference(), "", theme);
        return *themeValue;
    }

    TokenValue defaultValue = token->GetDefaultValue();
    if (defaultValue.IsReference())
        return ResolveScoped(defaultValue.AsReference(), "", theme);
    return defaultValue;
}

std::string DesignSystem::ScopedId(const std::string& tokenId,
                                   const std::string& scope) {
    if (scope.empty()) return tokenId;
    return tokenId + kScopeSep + scope;
}

void DesignSystem::RegisterScope(const std::string& path,
                                 const std::string& label) {
    if (path.empty()) return;
    // Auto-register every parent so the tree is always complete even if a
    // child scope is seen before its parent. Only set a label if given or
    // none stored yet (don't overwrite a good label with an empty one).
    std::size_t pos = 0;
    while (true) {
        std::size_t slash = path.find('/', pos);
        std::string prefix = (slash == std::string::npos)
                                 ? path : path.substr(0, slash);
        auto it = scopeRegistry_.find(prefix);
        if (it == scopeRegistry_.end()) {
            // Default label = the prefix's own last segment.
            std::size_t s = prefix.find_last_of('/');
            scopeRegistry_[prefix] =
                (s == std::string::npos) ? prefix : prefix.substr(s + 1);
        }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    if (!label.empty()) scopeRegistry_[path] = label;
}

std::vector<DesignSystem::ScopeInfo> DesignSystem::GetScopes() const {
    std::vector<ScopeInfo> out;
    out.reserve(scopeRegistry_.size());
    for (const auto& [path, label] : scopeRegistry_) {  // std::map = sorted
        int depth = 1;
        for (char c : path) if (c == '/') ++depth;
        out.push_back({path, label, depth});
    }
    return out;
}

TokenValue DesignSystem::ResolveScoped(const std::string& tokenId,
                                       const std::string& scope,
                                       ThemeType theme) {
    // TEMPORARILY DISABLED scope handling: resolve purely against the global
    // token graph + theme/global user overrides. This keeps usage recording
    // but ignores scope-specific overrides and theme-definitions, so a token
    // edit propagates everywhere uniformly (debugging combos/Settings/DevTest).
    if (!kScopesEnabled) {
        RecordUsage(tokenId);
        return ResolveTokenValue(tokenId, theme);
    }
    // Record EVERY token visited in the chain for the active component. As
    // ResolveScoped recurses on references (component → semantic →
    // primitive), each level enters here and is recorded — so a primitive
    // correctly accumulates the components that ultimately depend on it.
    RecordUsage(tokenId);
    // Scope specificity wins over layer kind. We walk the scope from the
    // MOST specific level to the parent and, at *each* level, look first for
    // a user override then for a theme definition AT THAT EXACT LEVEL. The
    // first level that has either one wins — so a theme-def on
    // `zone/element` still beats a user override on the parent `zone`,
    // exactly like nested token references. Only if no level (down to "")
    // supplies anything do we fall back to the global typed token.
    //
    // Order within a level: user override > theme definition (an explicit
    // user edit at a level still trumps the theme's base look at that same
    // level). A found value may be a reference; it resolves within the same
    // scope so chains like button@zone → accent cascade correctly.
    std::string s = scope;
    while (true) {
        if (!s.empty()) {
            if (const Override* ov =
                    overrideManager_.GetBestOverride(ScopedId(tokenId, s), theme)) {
                TokenValue v = ov->GetValue();
                if (v.IsReference())
                    return ResolveScoped(v.AsReference(), scope, theme);
                return v;
            }
            if (const TokenValue* td =
                    themeDefs_.FindExact(tokenId, s, theme)) {
                if (td->IsReference())
                    return ResolveScoped(td->AsReference(), scope, theme);
                return *td;
            }
        }
        if (s.empty()) break;
        std::size_t slash = s.find_last_of('/');
        s = (slash == std::string::npos) ? std::string() : s.substr(0, slash);
        if (slash == std::string::npos) {
            // Drop to the "" (global-scope) level for one final theme-def
            // check before falling through to the global token.
            if (const TokenValue* td = themeDefs_.FindExact(tokenId, "", theme)) {
                if (td->IsReference())
                    return ResolveScoped(td->AsReference(), "", theme);
                return *td;
            }
            break;
        }
    }

    // Nothing scoped at any level → the global typed token (which itself
    // honours global/theme user overrides + the reference chain).
    return ResolveTokenValue(tokenId, theme);
}

ValueConstraint DesignSystem::GetEffectiveConstraint(const std::string& tokenId) {
    auto& reg = TokenRegistry::Instance();
    std::string cur = tokenId;
    // Walk the default-reference chain until a token carries a constraint or
    // the value becomes concrete. Bounded against runtime-introduced cycles.
    for (int hop = 0; hop < 16; ++hop) {
        auto token = reg.GetToken(cur);
        if (!token) break;
        if (token->HasConstraint()) return token->GetConstraint();
        const TokenValue& dv = token->GetDefaultValue();
        if (!dv.IsReference()) break;
        cur = dv.AsReference();
    }
    return ValueConstraint{};
}

ImVec4 DesignSystem::ApplyAccessibility(const ImVec4& color, AccessibilityType type) {
    return ColorBlindness::ApplyColorBlindness(color, type);
}

std::vector<DesignSystem::ReferenceChainEntry>
DesignSystem::GetReferenceChain(const std::string& tokenId, ThemeType theme) {
    std::vector<ReferenceChainEntry> chain;
    auto& registry = TokenRegistry::Instance();

    // Bound the walk defensively. The schema is proven acyclic at compile
    // time, but a *runtime* override could introduce a reference cycle, so we
    // still cap the traversal at 64 hops (primitive→semantic→component is 3).
    std::string current = tokenId;
    for (int hop = 0; hop < 64; ++hop) {
        ReferenceChainEntry entry;
        entry.tokenId = current;
        entry.found = false;
        entry.overridden = false;

        const Override* override = overrideManager_.GetBestOverride(current, theme);
        if (override) {
            entry.overridden = true;
            entry.value = override->GetValue();
            entry.found = true;
            chain.push_back(entry);
            if (!entry.value.IsReference()) return chain;
            current = entry.value.AsReference();
            continue;
        }

        auto token = registry.GetToken(current);
        if (!token) {
            chain.push_back(entry);
            return chain;
        }
        entry.found = true;

        Context themeContext(theme, AccessibilityType::None);
        const TokenValue* themeValue = token->GetContextValue(themeContext);
        const TokenValue& src = themeValue ? *themeValue : token->GetDefaultValue();
        entry.value = src;
        chain.push_back(entry);

        if (!src.IsReference()) return chain;
        current = src.AsReference();
    }
    return chain;
}

} // namespace DesignSystem

#pragma once

#include <DesignSystem/Core/TokenType.h>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
//  Strongly-typed token identifiers.
//
//  Every design token has exactly ONE entry in `enum class Tok`. C++ code refers
//  to tokens through this enum (a typo / missing token is a *compile error*).
//  The enum spelling is an internal handle; the design-system IDENTITY is the
//  string key returned by TokName(), which follows the taxonomy in
//  TOKEN_SYSTEM.md strictly:
//
//      tier.{property|family}.{...}.{state?}
//
//  • tier         — primitive / semantic / component (token hierarchy level).
//  • PRIMITIVE    — raw palette / unitless scales. NEVER a reference. The full
//                   colour palette (gray + 17 hues × 16 steps + transparent
//                   black/white + static colours) lives here, plus opacity /
//                   spacing / radius / border-width / size / scale ladders.
//  • SEMANTIC     — design roles following {property}.{role}.{modifier?}.{state}
//                   (background.*, text.color.*, border.color.*, icon.color.*),
//                   role colour scales semantic.{role}.color.{100..1600},
//                   data-viz roles, plus grouped ImGuiStyle config
//                   (semantic.spacing/alignment/interaction/rendering.*).
//  • COMPONENT    — a real, nameable widget grouping coherent properties.
//                   Components reference semantic / primitive tokens.
//
//  CRITICAL NAMING RULE: a token NAME never encodes its VALUE. Use a positional
//  scale index (100/200/…/1600) or a t-shirt size, never the literal value.
//
//  This file (and TokenSchema.cpp) are generated/maintained together; a
//  compile-time check proves row i has id Tok(i) and references are valid.
// ─────────────────────────────────────────────────────────────────────────────

namespace DesignSystem {

enum class Tok : std::uint32_t {
    P_Color_Gray_25,
    P_Color_Gray_50,
    P_Color_Gray_75,
    P_Color_Gray_100,
    P_Color_Gray_200,
    P_Color_Gray_300,
    P_Color_Gray_400,
    P_Color_Gray_500,
    P_Color_Gray_600,
    P_Color_Gray_700,
    P_Color_Gray_800,
    P_Color_Gray_900,
    P_Color_Gray_1000,
    P_Color_Blue_100,
    P_Color_Blue_200,
    P_Color_Blue_300,
    P_Color_Blue_400,
    P_Color_Blue_500,
    P_Color_Blue_600,
    P_Color_Blue_700,
    P_Color_Blue_800,
    P_Color_Blue_900,
    P_Color_Blue_1000,
    P_Color_Blue_1100,
    P_Color_Blue_1200,
    P_Color_Blue_1300,
    P_Color_Blue_1400,
    P_Color_Blue_1500,
    P_Color_Blue_1600,
    P_Color_Brown_100,
    P_Color_Brown_200,
    P_Color_Brown_300,
    P_Color_Brown_400,
    P_Color_Brown_500,
    P_Color_Brown_600,
    P_Color_Brown_700,
    P_Color_Brown_800,
    P_Color_Brown_900,
    P_Color_Brown_1000,
    P_Color_Brown_1100,
    P_Color_Brown_1200,
    P_Color_Brown_1300,
    P_Color_Brown_1400,
    P_Color_Brown_1500,
    P_Color_Brown_1600,
    P_Color_Celery_100,
    P_Color_Celery_200,
    P_Color_Celery_300,
    P_Color_Celery_400,
    P_Color_Celery_500,
    P_Color_Celery_600,
    P_Color_Celery_700,
    P_Color_Celery_800,
    P_Color_Celery_900,
    P_Color_Celery_1000,
    P_Color_Celery_1100,
    P_Color_Celery_1200,
    P_Color_Celery_1300,
    P_Color_Celery_1400,
    P_Color_Celery_1500,
    P_Color_Celery_1600,
    P_Color_Chartreuse_100,
    P_Color_Chartreuse_200,
    P_Color_Chartreuse_300,
    P_Color_Chartreuse_400,
    P_Color_Chartreuse_500,
    P_Color_Chartreuse_600,
    P_Color_Chartreuse_700,
    P_Color_Chartreuse_800,
    P_Color_Chartreuse_900,
    P_Color_Chartreuse_1000,
    P_Color_Chartreuse_1100,
    P_Color_Chartreuse_1200,
    P_Color_Chartreuse_1300,
    P_Color_Chartreuse_1400,
    P_Color_Chartreuse_1500,
    P_Color_Chartreuse_1600,
    P_Color_Cinnamon_100,
    P_Color_Cinnamon_200,
    P_Color_Cinnamon_300,
    P_Color_Cinnamon_400,
    P_Color_Cinnamon_500,
    P_Color_Cinnamon_600,
    P_Color_Cinnamon_700,
    P_Color_Cinnamon_800,
    P_Color_Cinnamon_900,
    P_Color_Cinnamon_1000,
    P_Color_Cinnamon_1100,
    P_Color_Cinnamon_1200,
    P_Color_Cinnamon_1300,
    P_Color_Cinnamon_1400,
    P_Color_Cinnamon_1500,
    P_Color_Cinnamon_1600,
    P_Color_Cyan_100,
    P_Color_Cyan_200,
    P_Color_Cyan_300,
    P_Color_Cyan_400,
    P_Color_Cyan_500,
    P_Color_Cyan_600,
    P_Color_Cyan_700,
    P_Color_Cyan_800,
    P_Color_Cyan_900,
    P_Color_Cyan_1000,
    P_Color_Cyan_1100,
    P_Color_Cyan_1200,
    P_Color_Cyan_1300,
    P_Color_Cyan_1400,
    P_Color_Cyan_1500,
    P_Color_Cyan_1600,
    P_Color_Fuchsia_100,
    P_Color_Fuchsia_200,
    P_Color_Fuchsia_300,
    P_Color_Fuchsia_400,
    P_Color_Fuchsia_500,
    P_Color_Fuchsia_600,
    P_Color_Fuchsia_700,
    P_Color_Fuchsia_800,
    P_Color_Fuchsia_900,
    P_Color_Fuchsia_1000,
    P_Color_Fuchsia_1100,
    P_Color_Fuchsia_1200,
    P_Color_Fuchsia_1300,
    P_Color_Fuchsia_1400,
    P_Color_Fuchsia_1500,
    P_Color_Fuchsia_1600,
    P_Color_Green_100,
    P_Color_Green_200,
    P_Color_Green_300,
    P_Color_Green_400,
    P_Color_Green_500,
    P_Color_Green_600,
    P_Color_Green_700,
    P_Color_Green_800,
    P_Color_Green_900,
    P_Color_Green_1000,
    P_Color_Green_1100,
    P_Color_Green_1200,
    P_Color_Green_1300,
    P_Color_Green_1400,
    P_Color_Green_1500,
    P_Color_Green_1600,
    P_Color_Indigo_100,
    P_Color_Indigo_200,
    P_Color_Indigo_300,
    P_Color_Indigo_400,
    P_Color_Indigo_500,
    P_Color_Indigo_600,
    P_Color_Indigo_700,
    P_Color_Indigo_800,
    P_Color_Indigo_900,
    P_Color_Indigo_1000,
    P_Color_Indigo_1100,
    P_Color_Indigo_1200,
    P_Color_Indigo_1300,
    P_Color_Indigo_1400,
    P_Color_Indigo_1500,
    P_Color_Indigo_1600,
    P_Color_Magenta_100,
    P_Color_Magenta_200,
    P_Color_Magenta_300,
    P_Color_Magenta_400,
    P_Color_Magenta_500,
    P_Color_Magenta_600,
    P_Color_Magenta_700,
    P_Color_Magenta_800,
    P_Color_Magenta_900,
    P_Color_Magenta_1000,
    P_Color_Magenta_1100,
    P_Color_Magenta_1200,
    P_Color_Magenta_1300,
    P_Color_Magenta_1400,
    P_Color_Magenta_1500,
    P_Color_Magenta_1600,
    P_Color_Orange_100,
    P_Color_Orange_200,
    P_Color_Orange_300,
    P_Color_Orange_400,
    P_Color_Orange_500,
    P_Color_Orange_600,
    P_Color_Orange_700,
    P_Color_Orange_800,
    P_Color_Orange_900,
    P_Color_Orange_1000,
    P_Color_Orange_1100,
    P_Color_Orange_1200,
    P_Color_Orange_1300,
    P_Color_Orange_1400,
    P_Color_Orange_1500,
    P_Color_Orange_1600,
    P_Color_Pink_100,
    P_Color_Pink_200,
    P_Color_Pink_300,
    P_Color_Pink_400,
    P_Color_Pink_500,
    P_Color_Pink_600,
    P_Color_Pink_700,
    P_Color_Pink_800,
    P_Color_Pink_900,
    P_Color_Pink_1000,
    P_Color_Pink_1100,
    P_Color_Pink_1200,
    P_Color_Pink_1300,
    P_Color_Pink_1400,
    P_Color_Pink_1500,
    P_Color_Pink_1600,
    P_Color_Purple_100,
    P_Color_Purple_200,
    P_Color_Purple_300,
    P_Color_Purple_400,
    P_Color_Purple_500,
    P_Color_Purple_600,
    P_Color_Purple_700,
    P_Color_Purple_800,
    P_Color_Purple_900,
    P_Color_Purple_1000,
    P_Color_Purple_1100,
    P_Color_Purple_1200,
    P_Color_Purple_1300,
    P_Color_Purple_1400,
    P_Color_Purple_1500,
    P_Color_Purple_1600,
    P_Color_Red_100,
    P_Color_Red_200,
    P_Color_Red_300,
    P_Color_Red_400,
    P_Color_Red_500,
    P_Color_Red_600,
    P_Color_Red_700,
    P_Color_Red_800,
    P_Color_Red_900,
    P_Color_Red_1000,
    P_Color_Red_1100,
    P_Color_Red_1200,
    P_Color_Red_1300,
    P_Color_Red_1400,
    P_Color_Red_1500,
    P_Color_Red_1600,
    P_Color_Seafoam_100,
    P_Color_Seafoam_200,
    P_Color_Seafoam_300,
    P_Color_Seafoam_400,
    P_Color_Seafoam_500,
    P_Color_Seafoam_600,
    P_Color_Seafoam_700,
    P_Color_Seafoam_800,
    P_Color_Seafoam_900,
    P_Color_Seafoam_1000,
    P_Color_Seafoam_1100,
    P_Color_Seafoam_1200,
    P_Color_Seafoam_1300,
    P_Color_Seafoam_1400,
    P_Color_Seafoam_1500,
    P_Color_Seafoam_1600,
    P_Color_Silver_100,
    P_Color_Silver_200,
    P_Color_Silver_300,
    P_Color_Silver_400,
    P_Color_Silver_500,
    P_Color_Silver_600,
    P_Color_Silver_700,
    P_Color_Silver_800,
    P_Color_Silver_900,
    P_Color_Silver_1000,
    P_Color_Silver_1100,
    P_Color_Silver_1200,
    P_Color_Silver_1300,
    P_Color_Silver_1400,
    P_Color_Silver_1500,
    P_Color_Silver_1600,
    P_Color_Turquoise_100,
    P_Color_Turquoise_200,
    P_Color_Turquoise_300,
    P_Color_Turquoise_400,
    P_Color_Turquoise_500,
    P_Color_Turquoise_600,
    P_Color_Turquoise_700,
    P_Color_Turquoise_800,
    P_Color_Turquoise_900,
    P_Color_Turquoise_1000,
    P_Color_Turquoise_1100,
    P_Color_Turquoise_1200,
    P_Color_Turquoise_1300,
    P_Color_Turquoise_1400,
    P_Color_Turquoise_1500,
    P_Color_Turquoise_1600,
    P_Color_Yellow_100,
    P_Color_Yellow_200,
    P_Color_Yellow_300,
    P_Color_Yellow_400,
    P_Color_Yellow_500,
    P_Color_Yellow_600,
    P_Color_Yellow_700,
    P_Color_Yellow_800,
    P_Color_Yellow_900,
    P_Color_Yellow_1000,
    P_Color_Yellow_1100,
    P_Color_Yellow_1200,
    P_Color_Yellow_1300,
    P_Color_Yellow_1400,
    P_Color_Yellow_1500,
    P_Color_Yellow_1600,
    P_Color_TransparentBlack_25,
    P_Color_TransparentBlack_50,
    P_Color_TransparentBlack_75,
    P_Color_TransparentBlack_100,
    P_Color_TransparentBlack_200,
    P_Color_TransparentBlack_300,
    P_Color_TransparentBlack_400,
    P_Color_TransparentBlack_500,
    P_Color_TransparentBlack_600,
    P_Color_TransparentBlack_700,
    P_Color_TransparentBlack_800,
    P_Color_TransparentBlack_900,
    P_Color_TransparentBlack_1000,
    P_Color_TransparentWhite_25,
    P_Color_TransparentWhite_50,
    P_Color_TransparentWhite_75,
    P_Color_TransparentWhite_100,
    P_Color_TransparentWhite_200,
    P_Color_TransparentWhite_300,
    P_Color_TransparentWhite_400,
    P_Color_TransparentWhite_500,
    P_Color_TransparentWhite_600,
    P_Color_TransparentWhite_700,
    P_Color_TransparentWhite_800,
    P_Color_TransparentWhite_900,
    P_Color_TransparentWhite_1000,
    P_Color_Static_Blue_900,
    P_Color_Static_Blue_1000,
    P_Color_Static_Red_400,
    P_Color_Static_Red_600,
    P_Color_Static_Red_800,
    P_Color_Static_Red_900,
    P_Color_Static_Red_1000,
    P_Color_Static_Green_400,
    P_Color_Static_Green_600,
    P_Color_Static_Green_800,
    P_Color_Static_Orange_400,
    P_Color_Static_Orange_600,
    P_Color_Static_Orange_800,
    P_Opacity_0,
    P_Opacity_100,
    P_Opacity_200,
    P_Opacity_300,
    P_Opacity_400,
    P_Opacity_500,
    P_Opacity_600,
    P_Opacity_700,
    P_Opacity_800,
    P_Opacity_900,
    P_Opacity_1000,
    P_Spacing_0,
    P_Spacing_50,
    P_Spacing_100,
    P_Spacing_200,
    P_Spacing_300,
    P_Spacing_400,
    P_Spacing_500,
    P_Spacing_600,
    P_Spacing_700,
    P_Spacing_800,
    P_Spacing_900,
    P_Spacing_1000,
    P_Radius_none,
    P_Radius_100,
    P_Radius_200,
    P_Radius_300,
    P_Radius_400,
    P_Radius_500,
    P_Radius_600,
    P_Radius_700,
    P_Radius_800,
    P_Radius_900,
    P_Radius_Full,
    P_StrokeWidth_100,
    P_StrokeWidth_150,
    P_StrokeWidth_200,
    P_StrokeWidth_300,
    P_StrokeWidth_400,
    P_StrokeWidth_0,
    P_Size_75,
    P_Size_100,
    P_Size_200,
    P_Size_400,
    P_Size_800,
    P_FontSize_10,
    P_FontSize_20,
    P_FontSize_30,
    P_FontSize_40,
    P_FontSize_50,
    P_FontSize_60,
    P_FontSize_70,
    P_FontSize_80,
    P_FontSize_90,
    P_FontSize_100,
    P_FontSize_110,
    P_FontSize_120,
    P_FontSize_130,
    P_FontSize_140,
    P_FontSize_150,
    P_FontSize_160,
    P_FontSize_170,
    P_FontSize_180,
    P_FontSize_190,
    P_FontWeight_Thin,
    P_FontWeight_ExtraLight,
    P_FontWeight_Light,
    P_FontWeight_Regular,
    P_FontWeight_Medium,
    P_FontWeight_SemiBold,
    P_FontWeight_Bold,
    P_FontWeight_ExtraBold,
    P_FontWeight_Black,
    P_FontWeight_ExtraBlack,
    P_FontScale_Base,
    P_FontScale_Ratio,
    P_FontScale_75,
    P_FontScale_100,
    P_FontScale_125,
    P_FontScale_150,
    P_Scale_75,
    P_Scale_100,
    P_Scale_125,
    P_Scale_150,
    P_LineHeight_None,
    P_LineHeight_Tight,
    P_LineHeight_Snug,
    P_LineHeight_Normal,
    P_LineHeight_Relaxed,
    P_LineHeight_Loose,
    P_LineHeightCjk_Compact,
    P_LineHeightCjk_Normal,
    P_LineHeightCjk_Relaxed,
    P_Tracking_Tighter,
    P_Tracking_Tight,
    P_Tracking_Normal,
    P_Tracking_Wide,
    P_Tracking_Wider,
    P_Tracking_Widest,
    P_FontStyle_Normal,
    P_FontStyle_Italic,
    P_FontFamily_Sans,
    P_FontFamily_Serif,
    P_FontFamily_Mono,
    P_FontFamily_Cjk,
    P_FontFamily_CjkSerif,
    P_Layer_Ground,
    P_Layer_Low,
    P_Layer_Mid,
    P_Layer_High,
    P_Layer_Veil,
    P_Layer_Raised,
    P_Layer_Peak,
    P_Layer_Critical,
    P_Layer_Absolute,
    P_Duration_Instant,
    P_Duration_Fast,
    P_Duration_Normal,
    P_Duration_Slow,
    P_Duration_Slower,
    P_Duration_Slowest,
    P_Easing_Linear,
    P_Easing_Ease,
    P_Easing_EaseIn,
    P_Easing_EaseOut,
    P_Easing_EaseInOut,
    P_Easing_Spring,
    P_Easing_Decelerate,
    P_Easing_Accelerate,
    P_Easing_Overshoot,
    P_GradientAngle_0,
    P_GradientAngle_45,
    P_GradientAngle_90,
    P_GradientAngle_135,
    P_GradientAngle_180,
    P_GradientAngle_225,
    P_GradientAngle_270,
    P_GradientAngle_315,
    P_GradientStop_0,
    P_GradientStop_25,
    P_GradientStop_50,
    P_GradientStop_75,
    P_GradientStop_100,
    P_Align_Start,
    P_Align_Center,
    P_Align_End,
    P_Config_ItemSpacing,
    P_Config_ItemInnerSpacing,
    P_Config_CellPadding,
    P_Config_TouchExtraPadding,
    P_Config_IndentSpacing,
    P_Config_ColumnsMinSpacing,
    P_Config_UndoSteps,
    P_Config_DisplayWindowPadding,
    P_Config_DisplaySafeAreaPadding,
    P_Config_SeparatorTextPadding,
    P_Config_ButtonTextAlign,
    P_Config_SelectableTextAlign,
    P_Config_SeparatorTextAlign,
    P_Config_ColorButtonPosition,
    P_Config_GrabMinSize,
    P_Config_LogSliderDeadzone,
    P_Config_ColorMarkerSize,
    P_Config_MouseCursorScale,
    P_Config_HoverDelayStationary,
    P_Config_HoverDelayShort,
    P_Config_HoverDelayNormal,
    P_Config_DragThreshold,
    P_Border_Enabled,           // literal 1/0 backing S_Border_Enabled (global borders toggle)
    P_Config_AntiAliasedLines,
    P_Config_AntiAliasedLinesUseTex,
    P_Config_AntiAliasedFill,
    P_Config_CurveTessellationTol,
    P_Config_CircleTessellationMaxError,
    P_Config_TreeLinesFlags,
    P_Config_TreeLinesSize,
    S_Accent_Color_100,
    S_Accent_Color_200,
    S_Accent_Color_300,
    S_Accent_Color_400,
    S_Accent_Color_500,
    S_Accent_Color_600,
    S_Accent_Color_700,
    S_Accent_Color_800,
    S_Accent_Color_900,
    S_Accent_Color_1000,
    S_Accent_Color_1100,
    S_Accent_Color_1200,
    S_Accent_Color_1300,
    S_Accent_Color_1400,
    S_Accent_Color_1500,
    S_Accent_Color_1600,
    S_Info_Color_100,
    S_Info_Color_200,
    S_Info_Color_300,
    S_Info_Color_400,
    S_Info_Color_500,
    S_Info_Color_600,
    S_Info_Color_700,
    S_Info_Color_800,
    S_Info_Color_900,
    S_Info_Color_1000,
    S_Info_Color_1100,
    S_Info_Color_1200,
    S_Info_Color_1300,
    S_Info_Color_1400,
    S_Info_Color_1500,
    S_Info_Color_1600,
    S_Negative_Color_100,
    S_Negative_Color_200,
    S_Negative_Color_300,
    S_Negative_Color_400,
    S_Negative_Color_500,
    S_Negative_Color_600,
    S_Negative_Color_700,
    S_Negative_Color_800,
    S_Negative_Color_900,
    S_Negative_Color_1000,
    S_Negative_Color_1100,
    S_Negative_Color_1200,
    S_Negative_Color_1300,
    S_Negative_Color_1400,
    S_Negative_Color_1500,
    S_Negative_Color_1600,
    S_Positive_Color_100,
    S_Positive_Color_200,
    S_Positive_Color_300,
    S_Positive_Color_400,
    S_Positive_Color_500,
    S_Positive_Color_600,
    S_Positive_Color_700,
    S_Positive_Color_800,
    S_Positive_Color_900,
    S_Positive_Color_1000,
    S_Positive_Color_1100,
    S_Positive_Color_1200,
    S_Positive_Color_1300,
    S_Positive_Color_1400,
    S_Positive_Color_1500,
    S_Positive_Color_1600,
    S_Notice_Color_100,
    S_Notice_Color_200,
    S_Notice_Color_300,
    S_Notice_Color_400,
    S_Notice_Color_500,
    S_Notice_Color_600,
    S_Notice_Color_700,
    S_Notice_Color_800,
    S_Notice_Color_900,
    S_Notice_Color_1000,
    S_Notice_Color_1100,
    S_Notice_Color_1200,
    S_Notice_Color_1300,
    S_Notice_Color_1400,
    S_Notice_Color_1500,
    S_Notice_Color_1600,
    S_Neutral_Color_25,
    S_Neutral_Color_50,
    S_Neutral_Color_75,
    S_Neutral_Color_100,
    S_Neutral_Color_200,
    S_Neutral_Color_300,
    S_Neutral_Color_400,
    S_Neutral_Color_500,
    S_Neutral_Color_600,
    S_Neutral_Color_700,
    S_Neutral_Color_800,
    S_Neutral_Color_900,
    S_Neutral_Color_1000,
    S_Color_Accent_Default,
    S_Color_Accent_Hover,
    S_Color_Accent_Down,
    // ── Interaction-STATE colour matrix (semantic.accent.color.<role>.<status>)
    // Reusable everywhere an editor/list/row needs coherent state cues. Six roles
    // (visual / hover / text / selected / hover-selected / active) × seven status
    // families (default, positive, negative, info, neutral, notice, brand). The
    // `default` status is grey for the passive roles (visual/hover/text) and blue
    // (accent) for the selection roles (selected/active/hover-selected) — Blender-
    // like (grey at rest, blue selected). Component tokens reference THESE.
    S_Accent_Visual_Default,
    S_Accent_Visual_Positive,
    S_Accent_Visual_Negative,
    S_Accent_Visual_Info,
    S_Accent_Visual_Neutral,
    S_Accent_Visual_Notice,
    S_Accent_Visual_Brand,
    S_Accent_Hover_Default,
    S_Accent_Hover_Positive,
    S_Accent_Hover_Negative,
    S_Accent_Hover_Info,
    S_Accent_Hover_Neutral,
    S_Accent_Hover_Notice,
    S_Accent_Hover_Brand,
    S_Accent_Text_Default,
    S_Accent_Text_Positive,
    S_Accent_Text_Negative,
    S_Accent_Text_Info,
    S_Accent_Text_Neutral,
    S_Accent_Text_Notice,
    S_Accent_Text_Brand,
    S_Accent_Selected_Default,
    S_Accent_Selected_Positive,
    S_Accent_Selected_Negative,
    S_Accent_Selected_Info,
    S_Accent_Selected_Neutral,
    S_Accent_Selected_Notice,
    S_Accent_Selected_Brand,
    S_Accent_HoverSelected_Default,
    S_Accent_HoverSelected_Positive,
    S_Accent_HoverSelected_Negative,
    S_Accent_HoverSelected_Info,
    S_Accent_HoverSelected_Neutral,
    S_Accent_HoverSelected_Notice,
    S_Accent_HoverSelected_Brand,
    S_Accent_Active_Default,
    S_Accent_Active_Positive,
    S_Accent_Active_Negative,
    S_Accent_Active_Info,
    S_Accent_Active_Neutral,
    S_Accent_Active_Notice,
    S_Accent_Active_Brand,
    S_Background_Accent_KbdFocus,
    S_Background_Info_Default,
    S_Background_Info_Hover,
    S_Background_Info_Pressed,
    S_Background_Info_KbdFocus,
    S_Color_Negative_Default,
    S_Background_Negative_Hover,
    S_Background_Negative_Pressed,
    S_Background_Negative_KbdFocus,
    S_Color_Positive_Default,
    S_Background_Positive_Hover,
    S_Background_Positive_Pressed,
    S_Background_Positive_KbdFocus,
    S_Color_Notice_Default,
    S_Background_Neutral_Default,
    S_Background_Neutral_Hover,
    S_Background_Neutral_Pressed,
    S_Background_Neutral_KbdFocus,
    S_Background_Accent_Subtle,
    S_Background_Info_Subtle,
    S_Background_Negative_Subtle,
    S_Background_Positive_Subtle,
    S_Background_Notice_Subtle,
    S_Background_Neutral_Subtle,
    S_Background_Blue_Default,
    S_Background_Blue_Subtle,
    S_Background_Blue_Visual,
    S_Background_Brown_Default,
    S_Background_Brown_Subtle,
    S_Background_Brown_Visual,
    S_Background_Celery_Default,
    S_Background_Celery_Subtle,
    S_Background_Celery_Visual,
    S_Background_Chartreuse_Default,
    S_Background_Chartreuse_Subtle,
    S_Background_Chartreuse_Visual,
    S_Background_Cinnamon_Default,
    S_Background_Cinnamon_Subtle,
    S_Background_Cinnamon_Visual,
    S_Background_Cyan_Default,
    S_Background_Cyan_Subtle,
    S_Background_Cyan_Visual,
    S_Background_Fuchsia_Default,
    S_Background_Fuchsia_Subtle,
    S_Background_Fuchsia_Visual,
    S_Background_Green_Default,
    S_Background_Green_Subtle,
    S_Background_Green_Visual,
    S_Background_Indigo_Default,
    S_Background_Indigo_Subtle,
    S_Background_Indigo_Visual,
    S_Background_Magenta_Default,
    S_Background_Magenta_Subtle,
    S_Background_Magenta_Visual,
    S_Background_Orange_Default,
    S_Background_Orange_Subtle,
    S_Background_Orange_Visual,
    S_Background_Pink_Default,
    S_Background_Pink_Subtle,
    S_Background_Pink_Visual,
    S_Background_Purple_Default,
    S_Background_Purple_Subtle,
    S_Background_Purple_Visual,
    S_Background_Red_Default,
    S_Background_Red_Subtle,
    S_Background_Red_Visual,
    S_Background_Seafoam_Default,
    S_Background_Seafoam_Subtle,
    S_Background_Seafoam_Visual,
    S_Background_Silver_Default,
    S_Background_Silver_Subtle,
    S_Background_Silver_Visual,
    S_Background_Turquoise_Default,
    S_Background_Turquoise_Subtle,
    S_Background_Turquoise_Visual,
    S_Background_Yellow_Default,
    S_Background_Yellow_Subtle,
    S_Background_Yellow_Visual,
    S_Background_Gray_Default,
    S_Background_Gray_Subtle,
    S_Background_Gray_Visual,
    S_Color_Background_Default,
    S_Color_Background_Layer1,
    S_Color_Background_Layer2,
    S_Color_Background_Popup,
    S_Background_Dim,
    S_Content_Accent_Default,
    S_Content_Accent_Hover,
    S_Content_Accent_Pressed,
    S_Content_Accent_KbdFocus,
    S_Content_Accent_Selected,
    S_Content_Negative_Default,
    S_Content_Negative_Hover,
    S_Content_Negative_Pressed,
    S_Content_Negative_KbdFocus,
    S_Content_Neutral_Default,
    S_Content_Neutral_Hover,
    S_Content_Neutral_Pressed,
    S_Content_Neutral_KbdFocus,
    S_Color_Text_Default,
    S_Color_Text_Subtle,
    S_Text_Tertiary,
    S_Color_Text_Disabled,
    S_Color_Text_Link,
    S_Text_Placeholder,
    S_Text_Blue,
    S_Text_Magenta,
    S_Text_Green,
    S_Text_Red,
    S_Text_Yellow,
    S_Text_Purple,
    S_Text_Orange,
    S_Text_Cyan,
    S_Color_Icon_Primary,
    S_Color_Icon_Secondary,
    S_Color_Icon_Tertiary,
    S_Icon_Disabled,
    S_Icon_Accent,
    S_Icon_Negative,
    S_Icon_Positive,
    S_Icon_Notice,
    S_Icon_Info,
    S_Icon_Blue,
    S_Icon_Magenta,
    S_Icon_Green,
    S_Icon_Red,
    S_Icon_Yellow,
    S_Icon_Purple,
    S_Icon_Orange,
    S_Icon_Cyan,
    S_Color_Border_Default,
    S_Border_Subtle,
    S_Color_Border_Strong,
    S_Border_Disabled,
    S_Border_Enabled,           // global on/off (1/0) for ALL borders at once

    S_Border_Negative_Default,
    S_Border_Negative_Hover,
    S_Border_Negative_Pressed,
    S_Border_Negative_Focus,
    S_Border_Negative_KbdFocus,
    S_Border_Accent_Default,
    S_Border_Focus,
    S_Border_Blue,
    S_Border_Magenta,
    S_Border_Green,
    S_Border_Red,
    S_Border_Yellow,
    S_Border_Purple,
    S_Border_Orange,
    S_Border_Cyan,
    S_Focus_Indicator,
    S_Accent_Visual,
    S_Info_Visual,
    S_Negative_Visual,
    S_Positive_Visual,
    S_Notice_Visual,
    S_Neutral_Visual,
    S_Disabled_Background,
    S_Disabled_Border,
    S_Disabled_Content,
    S_Disabled_StaticBlackBackground,
    S_Disabled_StaticBlackBorder,
    S_Disabled_StaticBlackContent,
    S_Disabled_StaticWhiteBackground,
    S_Disabled_StaticWhiteBorder,
    S_Disabled_StaticWhiteContent,
    S_Static_BlackText,
    S_Static_WhiteText,
    S_Static_BlackTrack,
    S_Static_WhiteTrack,
    S_Static_BlackTrackIndicator,
    S_Static_WhiteTrackIndicator,
    S_Static_BlackFocus,
    S_Static_WhiteFocus,
    S_Track_Color,
    S_Title_Color,
    S_Color_Background_TextSelection,
    S_Shadow_100_Color,
    S_Shadow_200_Color,
    S_Shadow_300_Color,
    S_Shadow_400_Color,
    S_Shadow_500_Color,
    S_Shadow_600_Color,
    S_Shadow_Xs,
    S_Shadow_S,
    S_Shadow_M,
    S_Shadow_L,
    S_Shadow_Xl,
    S_Size_Xxs,
    S_Size_Xs,
    S_Size_S,
    S_Size_M,
    S_Size_L,
    S_Size_Xl,
    S_Size_2xl,
    S_Size_3xl,
    S_CornerRadius_None,
    S_Radius_Xs,
    S_CornerRadius_Small,
    S_CornerRadius_Default,
    S_Radius_L,
    S_Radius_Xl,
    S_Radius_Full,
    S_BorderWidth_Thin,
    S_BorderWidth_Medium,
    S_BorderWidth_Thick,
    S_BorderWidth_Focus,
    S_BorderWidth_None,
    S_Opacity_Ghost,
    S_Opacity_Faint,
    S_Opacity_Reduced,
    S_Opacity_Moderate,
    S_Opacity_Strong,
    S_Opacity_Full,
    S_Opacity_Transparent,
    S_Opacity_Default,
    S_Opacity_Disabled,
    S_Opacity_DimLight,
    S_Opacity_DimHeavy,
    S_Opacity_ContentDisabled,
    S_FontSize_Default,
    S_FontScale_Default,
    S_Scale_Default,
    S_FontSize_DisplayL,
    S_FontSize_DisplayM,
    S_FontSize_DisplayS,
    S_FontSize_HeadingXl,
    S_FontSize_HeadingL,
    S_FontSize_HeadingM,
    S_FontSize_HeadingS,
    S_FontSize_HeadingXs,
    S_FontSize_BodyL,
    S_FontSize_BodyM,
    S_FontSize_BodyS,
    S_FontSize_DetailL,
    S_FontSize_DetailM,
    S_FontSize_DetailS,
    S_FontSize_LabelL,
    S_FontSize_LabelM,
    S_FontSize_LabelS,
    S_FontSize_MonoL,
    S_FontSize_MonoM,
    S_FontSize_MonoS,
    S_FontSize_CodeM,
    S_FontWeight_DisplayL,
    S_FontWeight_HeadingXl,
    S_FontWeight_HeadingL,
    S_FontWeight_HeadingM,
    S_FontWeight_HeadingS,
    S_FontWeight_BodyM,
    S_FontWeight_LabelM,
    S_FontWeight_MonoM,
    S_LineHeight_DisplayL,
    S_LineHeight_HeadingXl,
    S_LineHeight_HeadingM,
    S_LineHeight_BodyM,
    S_LineHeight_DetailS,
    S_LineHeight_LabelM,
    S_LineHeightCjk_HeadingM,
    S_LineHeightCjk_BodyM,
    S_Tracking_HeadingXl,
    S_Tracking_BodyM,
    S_Tracking_LabelM,
    S_FontFamily_Heading,
    S_FontFamily_Body,
    S_FontFamily_Mono,
    S_FontFamily_Cjk,
    S_TextMarginTop_HeadingXl,
    S_TextMarginTop_HeadingL,
    S_TextMarginTop_HeadingM,
    S_TextMarginTop_BodyM,
    S_TextMarginBottom_HeadingXl,
    S_TextMarginBottom_HeadingM,
    S_TextMarginBottom_BodyM,
    S_Text_DisplayL,
    S_Text_DisplayM,
    S_Text_DisplayS,
    S_Text_HeadingXl,
    S_Text_HeadingL,
    S_Text_HeadingM,
    S_Text_HeadingS,
    S_Text_HeadingXs,
    S_Text_BodyL,
    S_Text_BodyM,
    S_Text_BodyS,
    S_Text_DetailL,
    S_Text_DetailM,
    S_Text_DetailS,
    S_Text_LabelL,
    S_Text_LabelM,
    S_Text_LabelS,
    S_Text_MonoL,
    S_Text_MonoM,
    S_Text_MonoS,
    S_Text_CodeM,
    S_AnimDuration_Enter,
    S_AnimDuration_Exit,
    S_AnimDuration_Expand,
    S_AnimDuration_Collapse,
    S_AnimDuration_Page,
    S_AnimDuration_Pulse,
    S_AnimEasing_Enter,
    S_AnimEasing_Exit,
    S_AnimEasing_Interact,
    S_AnimEasing_Move,
    S_GradientAngle_Upward,
    S_GradientAngle_Downward,
    S_Layer_Dropdown,
    S_Layer_Sticky,
    S_Layer_Overlay,
    S_Layer_Modal,
    S_Layer_Popover,
    S_Layer_Toast,
    S_Layer_Tooltip,
    S_Config_ItemSpacing,
    S_Config_ItemInnerSpacing,
    S_Config_CellPadding,
    S_Config_TouchExtraPadding,
    S_Config_IndentSpacing,
    S_Config_ColumnsMinSpacing,
    S_Config_UndoSteps,
    S_Config_DisplayWindowPadding,
    S_Config_DisplaySafeAreaPadding,
    S_Config_SeparatorTextPadding,
    S_Config_ButtonTextAlign,
    S_Config_SelectableTextAlign,
    S_Config_SeparatorTextAlign,
    S_Config_ColorButtonPosition,
    S_Config_GrabMinSize,
    S_Config_LogSliderDeadzone,
    S_Config_ColorMarkerSize,
    S_Config_MouseCursorScale,
    S_Config_HoverDelayStationary,
    S_Config_HoverDelayShort,
    S_Config_HoverDelayNormal,
    S_Config_DragThreshold,
    S_Config_AntiAliasedLines,
    S_Config_AntiAliasedLinesUseTex,
    S_Config_AntiAliasedFill,
    S_Config_CurveTessellationTol,
    S_Config_CircleTessellationMaxError,
    S_Config_TreeLinesFlags,
    S_Config_TreeLinesSize,
    S_DataViz_Cat_1,
    S_DataViz_Cat_2,
    S_DataViz_Cat_3,
    S_DataViz_Cat_4,
    S_DataViz_Cat_5,
    S_DataViz_Cat_6,
    S_DataViz_Cat_7,
    S_DataViz_Cat_8,
    S_DataViz_Cat_9,
    S_DataViz_Cat_10,
    S_DataViz_Cat_11,
    S_DataViz_Cat_12,
    S_DataViz_Ord_1,
    S_DataViz_Ord_2,
    S_DataViz_Ord_3,
    S_DataViz_Ord_4,
    S_DataViz_Ord_5,
    S_DataViz_Ord_6,
    S_DataViz_Ord_7,
    S_DataViz_Ord_8,
    S_DataViz_Ord_9,
    S_DataViz_Seq_Viridis_100,
    S_DataViz_Seq_Viridis_200,
    S_DataViz_Seq_Viridis_300,
    S_DataViz_Seq_Viridis_400,
    S_DataViz_Seq_Viridis_500,
    S_DataViz_Seq_Viridis_600,
    S_DataViz_Seq_Viridis_700,
    S_DataViz_Seq_Viridis_800,
    S_DataViz_Seq_Viridis_900,
    S_DataViz_Seq_Viridis_1000,
    S_DataViz_Seq_Viridis_1100,
    S_DataViz_Seq_Viridis_1200,
    S_DataViz_Seq_Viridis_1300,
    S_DataViz_Seq_Viridis_1400,
    S_DataViz_Seq_Viridis_1500,
    S_DataViz_Seq_Viridis_1600,
    S_DataViz_Seq_Magma_100,
    S_DataViz_Seq_Magma_200,
    S_DataViz_Seq_Magma_300,
    S_DataViz_Seq_Magma_400,
    S_DataViz_Seq_Magma_500,
    S_DataViz_Seq_Magma_600,
    S_DataViz_Seq_Magma_700,
    S_DataViz_Seq_Magma_800,
    S_DataViz_Seq_Magma_900,
    S_DataViz_Seq_Magma_1000,
    S_DataViz_Seq_Magma_1100,
    S_DataViz_Seq_Magma_1200,
    S_DataViz_Seq_Magma_1300,
    S_DataViz_Seq_Magma_1400,
    S_DataViz_Seq_Magma_1500,
    S_DataViz_Seq_Magma_1600,
    S_DataViz_Seq_Plasma_100,
    S_DataViz_Seq_Plasma_200,
    S_DataViz_Seq_Plasma_300,
    S_DataViz_Seq_Plasma_400,
    S_DataViz_Seq_Plasma_500,
    S_DataViz_Seq_Plasma_600,
    S_DataViz_Seq_Plasma_700,
    S_DataViz_Seq_Plasma_800,
    S_DataViz_Seq_Plasma_900,
    S_DataViz_Seq_Plasma_1000,
    S_DataViz_Seq_Plasma_1100,
    S_DataViz_Seq_Plasma_1200,
    S_DataViz_Seq_Plasma_1300,
    S_DataViz_Seq_Plasma_1400,
    S_DataViz_Seq_Plasma_1500,
    S_DataViz_Seq_Plasma_1600,
    S_DataViz_Seq_Inferno_100,
    S_DataViz_Seq_Inferno_200,
    S_DataViz_Seq_Inferno_300,
    S_DataViz_Seq_Inferno_400,
    S_DataViz_Seq_Inferno_500,
    S_DataViz_Seq_Inferno_600,
    S_DataViz_Seq_Inferno_700,
    S_DataViz_Seq_Inferno_800,
    S_DataViz_Seq_Inferno_900,
    S_DataViz_Seq_Inferno_1000,
    S_DataViz_Seq_Inferno_1100,
    S_DataViz_Seq_Inferno_1200,
    S_DataViz_Seq_Inferno_1300,
    S_DataViz_Seq_Inferno_1400,
    S_DataViz_Seq_Inferno_1500,
    S_DataViz_Seq_Inferno_1600,
    S_DataViz_Seq_Cividis_100,
    S_DataViz_Seq_Cividis_200,
    S_DataViz_Seq_Cividis_300,
    S_DataViz_Seq_Cividis_400,
    S_DataViz_Seq_Cividis_500,
    S_DataViz_Seq_Cividis_600,
    S_DataViz_Seq_Cividis_700,
    S_DataViz_Seq_Cividis_800,
    S_DataViz_Seq_Cividis_900,
    S_DataViz_Seq_Cividis_1000,
    S_DataViz_Seq_Cividis_1100,
    S_DataViz_Seq_Cividis_1200,
    S_DataViz_Seq_Cividis_1300,
    S_DataViz_Seq_Cividis_1400,
    S_DataViz_Seq_Cividis_1500,
    S_DataViz_Seq_Cividis_1600,
    S_DataViz_Div_RdBu_100,
    S_DataViz_Div_RdBu_200,
    S_DataViz_Div_RdBu_300,
    S_DataViz_Div_RdBu_400,
    S_DataViz_Div_RdBu_500,
    S_DataViz_Div_RdBu_600,
    S_DataViz_Div_RdBu_700,
    S_DataViz_Div_RdBu_800,
    S_DataViz_Div_RdBu_900,
    S_DataViz_Div_RdBu_1000,
    S_DataViz_Div_RdBu_1100,
    S_DataViz_Div_RdBu_1200,
    S_DataViz_Div_RdBu_1300,
    S_DataViz_Div_RdBu_1400,
    S_DataViz_Div_RdBu_1500,
    S_DataViz_Div_RdBu_1600,
    S_DataViz_Div_PuGn_100,
    S_DataViz_Div_PuGn_200,
    S_DataViz_Div_PuGn_300,
    S_DataViz_Div_PuGn_400,
    S_DataViz_Div_PuGn_500,
    S_DataViz_Div_PuGn_600,
    S_DataViz_Div_PuGn_700,
    S_DataViz_Div_PuGn_800,
    S_DataViz_Div_PuGn_900,
    S_DataViz_Div_PuGn_1000,
    S_DataViz_Div_PuGn_1100,
    S_DataViz_Div_PuGn_1200,
    S_DataViz_Div_PuGn_1300,
    S_DataViz_Div_PuGn_1400,
    S_DataViz_Div_PuGn_1500,
    S_DataViz_Div_PuGn_1600,
    S_DataViz_Div_BrTeal_100,
    S_DataViz_Div_BrTeal_200,
    S_DataViz_Div_BrTeal_300,
    S_DataViz_Div_BrTeal_400,
    S_DataViz_Div_BrTeal_500,
    S_DataViz_Div_BrTeal_600,
    S_DataViz_Div_BrTeal_700,
    S_DataViz_Div_BrTeal_800,
    S_DataViz_Div_BrTeal_900,
    S_DataViz_Div_BrTeal_1000,
    S_DataViz_Div_BrTeal_1100,
    S_DataViz_Div_BrTeal_1200,
    S_DataViz_Div_BrTeal_1300,
    S_DataViz_Div_BrTeal_1400,
    S_DataViz_Div_BrTeal_1500,
    S_DataViz_Div_BrTeal_1600,
    S_DataViz_Axis,
    S_DataViz_Grid,
    S_DataViz_Label,
    S_DataViz_Highlight,
    C_Window_Background,
    C_Window_CornerRadius,
    C_Window_BorderWidth,
    C_Window_BorderColor,
    C_Window_BorderHoverPadding,
    C_Window_Padding,
    C_Window_MinSize,
    C_Window_TitleAlign,
    C_Window_MenuButtonPosition,
    C_Window_ShadowColor,       // detached-window drop shadow tint (incl. alpha)
    C_Window_ShadowSize,        // shadow spread in px
    C_Child_Background,
    C_Child_BackgroundOpacity,
    C_Child_CornerRadius,
    C_Child_BorderWidth,
    C_Popup_Background,
    C_Popup_CornerRadius,
    C_Popup_BorderWidth,
    C_Popup_MenuBarBackground,
    C_Frame_Background,
    C_Frame_BackgroundHover,
    C_Frame_BackgroundDown,
    C_Frame_CornerRadius,
    C_Frame_BorderWidth,
    C_Frame_Padding,
    C_Frame_InputTextCursor,
    // Blender-style numeric drag field (UI::DragValue).
    C_DragValue_Background,        // fill at rest
    C_DragValue_BackgroundHover,   // fill, hovered (lighter)
    C_DragValue_BackgroundPressed, // fill, pressed (held, before a drag begins)
    C_DragValue_BackgroundDrag,    // fill, while dragging
    C_DragValue_Text,              // value text
    C_DragValue_Unit,              // unit suffix text (subtle)
    C_DragValue_StepButton,        // +/- step glyph tint at rest
    C_DragValue_StepButtonHover,   // +/- step button hover fill
    C_DragValue_Border,            // border
    C_DragValue_CornerRadius,      // rounding
    C_DragValue_BorderWidth,       // border width
    C_Button_Background,
    C_Button_BackgroundHover,
    C_Button_BackgroundDown,
    C_Button_Label,
    C_Combo_Background,
    C_Combo_BackgroundHover,
    C_Combo_BackgroundDown,
    C_Combo_PreviewText,
    C_Combo_ArrowIcon,
    C_Combo_PopupBackground,
    C_Combo_ItemBackgroundHover,
    C_Combo_ItemBackgroundSelected,
    C_Combo_Border,
    C_Combo_CornerRadius,
    C_Combo_BorderWidth,
    C_Combo_Padding,
    C_Tab_Background,
    C_Tab_BackgroundHover,
    C_Tab_BackgroundSelected,
    C_Tab_OverlineSelected,
    C_Tab_BackgroundDimmed,
    C_Tab_BackgroundDimmedSelected,
    C_Tab_OverlineDimmed,
    C_Tab_CornerRadius,
    C_Tab_BorderWidth,
    C_Tab_BarBorderWidth,
    C_Tab_BarOverlineWidth,
    C_Tab_MinWidthBase,
    C_Tab_MinWidthShrink,
    C_Tab_CloseButtonMinWidthSelected,
    C_Tab_CloseButtonMinWidthUnselected,
    C_Tab_UnsavedMarker,
    C_Header_Background,
    C_Header_BackgroundHover,
    C_Header_BackgroundDown,
    C_Scrollbar_Background,
    C_Scrollbar_Grab,
    C_Scrollbar_GrabHover,
    C_Scrollbar_GrabDown,
    C_Scrollbar_Size,
    C_Scrollbar_CornerRadius,
    C_Scrollbar_Padding,
    // Custom Blender-style overlay scrollbar (UI::BeginScroll/EndScroll): a thin
    // grab floating in the component's right margin — zero reserved space — that
    // grows and brightens as the cursor nears it.
    C_Scrollbar_OverlayMargin,      // right inset the grab lives in (= the 5px margin)
    C_Scrollbar_OverlayWidthRest,   // grab thickness at rest (thin, e.g. 3px)
    C_Scrollbar_OverlayWidthHover,  // grab thickness when near/hovered (~ margin)
    C_Scrollbar_OverlayProximity,   // cursor distance at which the grab starts reacting
    C_Scrollbar_OverlayPadding,     // top/bottom inset so the grab clears the container edges/radius
    C_Slider_Grab,
    C_Slider_GrabDown,
    C_Slider_CornerRadius,
    C_Checkbox_Mark,
    C_Checkbox_BackgroundSelected,
    // Full per-state set for the UI::Checkbox widget (square box, ui-unit row).
    C_Checkbox_Background,             // box fill, unchecked, at rest
    C_Checkbox_BackgroundHover,        // box fill, unchecked, hovered
    C_Checkbox_BackgroundDown,         // box fill, unchecked, pressed
    C_Checkbox_BackgroundSelectedHover,// box fill, checked, hovered
    C_Checkbox_BackgroundSelectedDown, // box fill, checked, pressed
    C_Checkbox_Border,                 // box border, unchecked
    C_Checkbox_BorderSelected,         // box border, checked
    C_Checkbox_BoxSize,                // drawn box side length (px, scaled)
    C_Checkbox_CornerRadius,           // box rounding
    C_Checkbox_BorderWidth,            // box border width
    C_Separator_Color,
    C_Separator_Hover,
    C_Separator_Down,
    C_Separator_Size,
    C_Separator_TextBorderWidth,
    C_ResizeGrip_Color,
    C_ResizeGrip_Hover,
    C_ResizeGrip_Down,
    C_Table_HeaderBackground,
    C_Table_BorderStrong,
    C_Table_BorderLight,
    C_Table_RowBackground,
    C_Table_RowBackgroundAlt,
    C_Table_AngledHeadersAngle,
    C_Table_AngledHeadersTextAlign,
    C_Image_CornerRadius,
    C_Image_BorderWidth,
    C_Docking_NodeHasCloseButton,
    C_Docking_SeparatorSize,
    C_Zone_SeparatorSize,
    C_Zone_SeparatorColor,
    C_Zone_SeparatorColorContinuation,
    C_Zone_SeparatorContinuationOpacity,
    C_DragDropTarget_CornerRadius,
    C_DragDropTarget_BorderWidth,
    C_DragDropTarget_Padding,
    C_KeyCap_Background,
    C_KeyCap_Border,
    C_KeyCap_Label,
    C_KeyCap_CornerRadius,
    C_KeyCap_Padding,
    C_KeyCap_FontScale,
    C_StatusBar_Background,
    C_StatusBar_Label,
    C_StatusBar_Height,
    C_StatusBar_Padding,
    C_ShortcutRow_BackgroundHover,
    C_ShortcutRow_BackgroundSelected,
    C_Shortcut_ConflictSoft,
    C_Shortcut_ConflictHard,
    C_Shortcut_Recording,
    C_Shortcut_CaptureBackground,
    C_SectionHeader_Label,
    C_SectionHeader_FontScale,
    C_CaptureField_Background,
    C_CaptureField_BackgroundRecording,
    C_CaptureField_BackgroundHover,
    C_CaptureField_BackgroundDown,
    C_CaptureField_Border,
    C_CaptureField_BorderRecording,
    C_CaptureField_Label,
    C_CaptureField_LabelHint,
    C_CaptureField_CornerRadius,
    C_CaptureField_Padding,
    C_CaptureField_MinWidth,
    C_CaptureField_Height,
    C_Toggle_Background,
    C_Toggle_BackgroundHover,
    C_Toggle_BackgroundSelected,
    C_Toggle_Border,
    C_Toggle_BorderSelected,
    C_Toggle_Label,
    C_Toggle_LabelSelected,
    C_Toggle_CornerRadius,
    C_Toggle_BorderWidth,
    C_IconButton_Background,
    C_IconButton_BackgroundHover,
    C_IconButton_BackgroundDown,
    C_IconButton_Border,
    C_IconButton_Icon,
    C_IconButton_IconNegative,
    C_IconButton_CornerRadius,
    S_Color_DataViz_Line,
    S_Color_DataViz_LineHover,
    S_Color_DataViz_Histogram,
    S_Color_DataViz_HistogramHover,
    S_Color_Negative_Recording,
    S_Color_Background_Child,
    S_Color_Background_MenuBar,
    S_Color_Background_Title,
    S_Color_Background_TitleActive,
    S_Color_Background_TitleCollapsed,
    S_Color_Background_ScrollbarTrack,
    S_Color_Background_DimWindowing,
    S_Color_Background_DimModal,
    S_Color_Background_DockingEmpty,
    S_Color_Background_DropTarget,
    S_Color_Foreground_ScrollbarGrab,
    S_Color_Border_Shadow,
    S_Color_Border_TreeLine,
    S_Color_Focus_Default,
    S_Color_Focus_Windowing,
    S_Color_Accent_DropTarget,
    S_Color_Accent_DockingPreview,
    C_EditorTopBar_Padding,
    C_StatusBar_Gap,
    P_UiUnit,
    S_Size_ControlHeight,
    C_Dropdown_Height,
    C_Dropdown_Padding,
    C_Dropdown_ChevronSize,
    C_Dropdown_IconSize,
    C_Dropdown_Background,
    C_Dropdown_BackgroundHover,
    C_Dropdown_Text,
    C_Dropdown_Icon,
    C_Dropdown_CornerRadius,
    C_Menu_Background,
    C_Menu_CornerRadius,
    C_Menu_ItemHoverBg,
    C_Menu_ItemSelectedBg,
    C_Menu_ColumnHeaderText,
    C_Menu_Padding,
    C_Menu_ColumnGap,
    C_Menu_ItemGap,
    P_Radius_150,
    S_CornerRadius_Control,
    C_Dropdown_Border,
    C_Dropdown_BorderHover,
    C_Dropdown_BackgroundDown,
    C_Dropdown_BackgroundOpen,   // trigger fill while the menu is open (lighter than rest)
    C_Dropdown_BorderWidth,
    C_Menu_Border,
    C_Menu_BorderWidth,
    P_Color_Gray_850,
    C_Editor_Background,
    C_Editor_TopBarBackground,
    S_Spacing_EditorInset,
    C_Editor_ContentInset,
    C_Cursor_Color,
    C_Menu_ItemPaddingX,
    C_Menu_TitleText,
    C_Tooltip_Background,
    C_Tooltip_Text,
    C_Tooltip_Border,
    C_Tooltip_BorderWidth,
    C_Tooltip_CornerRadius,
    C_Tooltip_Padding,
    P_Color_WhiteTransparent,
    P_Color_Gray_875,
    C_ZoneTab_Gap,
    C_ZoneTab_Padding,
    C_ZoneTab_Background,
    C_ZoneTab_BackgroundActive,
    C_ZoneTab_BackgroundHover,
    C_ZoneTab_Text,
    C_ZoneTab_TextActive,
    C_ZoneTab_BarBackground,
    C_ZoneTab_InsertLineColor,
    C_ZoneTab_InsertLineWidth,
    C_ZoneTab_DropPreviewFill,
    C_ZoneTab_DropCenterInset,
    C_ZoneTab_PreviewAnimDuration,
    C_ZoneTab_DragThreshold,
    C_ZoneTab_ShowSolo,
    C_TitleBar_Background,
    C_TitleBar_Icon,
    C_TitleBar_Text,
    C_TitleBar_ButtonHover,
    C_TitleBar_CloseHover,
    C_Splash_Background,
    C_Splash_Link,
    C_Splash_VersionText,       // version label over the (light) splash image — dark

    // ── Application surface hierarchy (darkest → lightest) ──
    // One ordered semantic ladder for the app chrome, so every clickable /
    // container surface derives from a single, coherent scale instead of
    // borrowing the generic recessed/raised layers. Order, dark theme:
    //   base < control < editor < menu-bar < child < frame.
    C_Dropdown_BackgroundHoverMinimal,
    P_Color_Gray_780,
    P_Color_Gray_760,
    P_Color_Gray_740,
    // Generic surface ROLES (named by function/elevation, never by component).
    // Darkest → lightest. Component tokens below reference these. The darkest
    // "base" role already exists as S_Color_Background_Default
    // (semantic.background.base.default) — reused here, not duplicated.
    S_Background_App_Control,   // clickable component chips
    S_Surface_Canvas,           // large content surface (e.g. an editor canvas)
    S_Surface_Raised,           // a bar/toolbar sitting above a canvas
    S_Background_App_Child,     // inner sub-surface inside a content surface
    S_Background_App_Frame,     // text input / frame field (lightest)
    S_Background_App_FrameHover,// input/frame field, hovered (a step LIGHTER than frame)

    // ── Preferences window title bar (independent of the main title bar) ──
    // Each window's title bar controls its own text/icon colours, so they can
    // be themed separately. The main window uses C_TitleBar_*; the Preferences
    // window uses these.
    C_PrefBar_Background,
    C_PrefBar_Text,
    C_PrefBar_Icon,
    C_PrefBar_ButtonHover,
    C_PrefBar_CloseHover,

    // ── Nested "panel" widget (Blender-style collapsible) ──
    // Header is flat (no accent/colour change on hover/select). Body colour
    // darkens with nesting depth so deeper sub-panels recede.
    C_Panel_HeaderBackground,   // header band (all depths)
    C_Panel_BodyL1,             // level-1 body + its direct children
    C_Panel_BodyL2,             // an opened child's body (a bit darker)
    C_Panel_BodyL3,             // deeper child body (darker still)
    C_Panel_Border,             // level-1 outer border
    C_Panel_Text,               // header label
    C_Panel_OverrideBadge,      // "has override" marker tint
    C_Panel_Gap,                // vertical gap before each level-1 panel
    C_PropertyGroup_Gap,        // extra vertical gap between property groups (> item gap)
    C_Panel_CornerRadius,       // panel rounding (small control radius, not editor radius)

    // ── Outliner rows: a tree list whose rows carry interaction state. Each row
    // background + the row text reference the semantic state matrix above. There
    // are NORMAL tokens (default status → grey/blue) and SEARCH tokens (positive
    // status → green) that the Outliner swaps to while a search is active.
    C_Outliner_Row_Hover,           // hovered (unselected) row bg
    C_Outliner_Row_Selected,        // selected (inactive) row bg
    C_Outliner_Row_SelectedHover,   // selected + hovered row bg
    C_Outliner_Row_Active,          // selected + active row bg
    C_Outliner_Row_ActiveHover,     // selected + active + hovered row bg
    C_Outliner_Text,                // default row label colour
    C_Outliner_TreeLineInset,       // top/bottom inset of the vertical tree guide line (px)
    C_Outliner_Search_Visual,       // search: a matched-but-idle row bg (faint)
    C_Outliner_Search_Hover,        // search: hovered matched row bg
    C_Outliner_Search_Selected,     // search: selected matched row bg
    C_Outliner_Search_SelectedHover,
    C_Outliner_Search_Active,
    C_Outliner_Search_ActiveHover,
    C_Outliner_Search_Text,         // search: matched row label colour (green text)

    // ── Shared object-STATE colours (Viewport + Outliner + Edit mode) ──
    // The "active = orange, loose = violet" cues every editor agrees on. Semantic
    // so a theme can retune them once; they reference the palette, never a literal.
    // `loose` = the object belongs to no page (page-less) → violet instead of orange.
    S_State_Active_OnPage,          // active object marker, on a page (orange)
    S_State_Active_Loose,           // active object marker, page-less (violet)
    S_State_Selected_OnPage,        // selected (NOT active) on-page object (darker orange)
    S_State_Selected_Loose,         // selected (not active) page-less object (violet)

    // ── Viewport canvas overlays (chrome drawn over the Vulkan canvas) ──
    // Cursor/axes/thumbnail cues use the theme-INVARIANT static palette so they
    // stay legible over any page colour regardless of the active theme.
    C_Viewport_CanvasArea,          // ruler/canvas backdrop band
    C_Viewport_Guide,               // alignment guide line (blue)
    C_Viewport_PageBorder,          // page rectangle edge
    C_Viewport_PageNameHover,       // hovered page-name label background
    C_Viewport_OriginOutline,       // object origin dot outline
    C_Viewport_CursorRing,          // 2D cursor outer ring (white)
    C_Viewport_CursorRingAccent,    // 2D cursor inner ring (red)
    C_Viewport_CursorTick,          // −X/−Y crosshair ticks (black)
    C_Viewport_CursorAxisX,         // +X axis tick (red)
    C_Viewport_CursorAxisY,         // +Y axis tick (green)
    C_Viewport_ThumbnailBackground, // thumbnail strip slot fill
    C_Viewport_ThumbnailBorder,     // thumbnail strip slot border

    // ── Edit-mode overlay: edges, vertices and Bézier handles ──
    C_EditHandle_Edge,              // unselected edge line
    C_EditHandle_Vertex,           // unselected vertex dot
    C_EditHandle_VertexRing,        // vertex highlight ring (white)
    C_EditHandle_NurbsHull,         // NURBS control polygon (dim)
    C_EditHandle_Free,              // Free handle (blue)
    C_EditHandle_Aligned,           // Aligned handle (amber)
    C_EditHandle_Mirrored,          // Mirrored handle (green)
    C_EditHandle_Vector,            // Vector handle (purple)
    C_EditHandle_Default,           // fallback handle colour

    // ── Zone-layout overlays (split corners, join preview, transform dim) ──
    C_ZoneOverlay_CornerTopLeft,     // split corner tint — top-left (blue)
    C_ZoneOverlay_CornerTopRight,    // split corner tint — top-right (green)
    C_ZoneOverlay_CornerBottomLeft,  // split corner tint — bottom-left (amber)
    C_ZoneOverlay_CornerBottomRight, // split corner tint — bottom-right (pink)
    C_ZoneOverlay_SplitLine,         // split arm preview line (white)
    C_ZoneOverlay_JoinKeep,          // join: kept-zone faint fill (white)
    C_ZoneOverlay_JoinRemove,        // join: removed-zone dim (black)
    C_ZoneOverlay_JoinResidual,      // join: residual-zone strong dim (black)
    C_ZoneOverlay_JoinFrame,         // join: final-rect frame (blue)
    C_ZoneOverlay_TransformDim,      // crop/transform scrim over the canvas

    // ── Object-placement preview (core preference + IOF default) ──
    P_Config_PreviewPlacement,       // raw 0/1 backing the semantic below
    S_Config_PreviewPlacement,       // 0/1: place new objects as a cursor-following preview
    C_Viewport_Crosshair,            // thin crosshair cursor used in preview placement
    S_Config_PlacementPreviewAlpha,  // 0..1: opacity of the placement ghost preview

    // ── Dev / debug toggles (Preferences ▸ Dev) ──
    P_Config_ShowCornerZones,        // raw 0/1 backing the semantic below
    S_Config_ShowCornerZones,        // 0/1: draw the colour-coded editor-corner hit-zone previews

    // Sentinel — must stay last. Never used as a real token.
    _Count
};

/// Number of real tokens (excludes the `_Count` sentinel).
inline constexpr std::size_t kTokenCount = static_cast<std::size_t>(Tok::_Count);

/**
 * Design-system string id for a token — the persisted key and the name shown
 * in the token editors. Exhaustive switch: a missing case triggers
 * -Werror=switch / the schema consteval checks, so enum and strings can't drift.
 */
constexpr std::string_view TokName(Tok t) {
    switch (t) {
        case Tok::P_Color_Gray_25: return "primitive.color.gray.25";
        case Tok::P_Color_Gray_50: return "primitive.color.gray.50";
        case Tok::P_Color_Gray_75: return "primitive.color.gray.75";
        case Tok::P_Color_Gray_100: return "primitive.color.gray.100";
        case Tok::P_Color_Gray_200: return "primitive.color.gray.200";
        case Tok::P_Color_Gray_300: return "primitive.color.gray.300";
        case Tok::P_Color_Gray_400: return "primitive.color.gray.400";
        case Tok::P_Color_Gray_500: return "primitive.color.gray.500";
        case Tok::P_Color_Gray_600: return "primitive.color.gray.600";
        case Tok::P_Color_Gray_700: return "primitive.color.gray.700";
        case Tok::P_Color_Gray_800: return "primitive.color.gray.800";
        case Tok::P_Color_Gray_900: return "primitive.color.gray.900";
        case Tok::P_Color_Gray_1000: return "primitive.color.gray.1000";
        case Tok::P_Color_Blue_100: return "primitive.color.blue.100";
        case Tok::P_Color_Blue_200: return "primitive.color.blue.200";
        case Tok::P_Color_Blue_300: return "primitive.color.blue.300";
        case Tok::P_Color_Blue_400: return "primitive.color.blue.400";
        case Tok::P_Color_Blue_500: return "primitive.color.blue.500";
        case Tok::P_Color_Blue_600: return "primitive.color.blue.600";
        case Tok::P_Color_Blue_700: return "primitive.color.blue.700";
        case Tok::P_Color_Blue_800: return "primitive.color.blue.800";
        case Tok::P_Color_Blue_900: return "primitive.color.blue.900";
        case Tok::P_Color_Blue_1000: return "primitive.color.blue.1000";
        case Tok::P_Color_Blue_1100: return "primitive.color.blue.1100";
        case Tok::P_Color_Blue_1200: return "primitive.color.blue.1200";
        case Tok::P_Color_Blue_1300: return "primitive.color.blue.1300";
        case Tok::P_Color_Blue_1400: return "primitive.color.blue.1400";
        case Tok::P_Color_Blue_1500: return "primitive.color.blue.1500";
        case Tok::P_Color_Blue_1600: return "primitive.color.blue.1600";
        case Tok::P_Color_Brown_100: return "primitive.color.brown.100";
        case Tok::P_Color_Brown_200: return "primitive.color.brown.200";
        case Tok::P_Color_Brown_300: return "primitive.color.brown.300";
        case Tok::P_Color_Brown_400: return "primitive.color.brown.400";
        case Tok::P_Color_Brown_500: return "primitive.color.brown.500";
        case Tok::P_Color_Brown_600: return "primitive.color.brown.600";
        case Tok::P_Color_Brown_700: return "primitive.color.brown.700";
        case Tok::P_Color_Brown_800: return "primitive.color.brown.800";
        case Tok::P_Color_Brown_900: return "primitive.color.brown.900";
        case Tok::P_Color_Brown_1000: return "primitive.color.brown.1000";
        case Tok::P_Color_Brown_1100: return "primitive.color.brown.1100";
        case Tok::P_Color_Brown_1200: return "primitive.color.brown.1200";
        case Tok::P_Color_Brown_1300: return "primitive.color.brown.1300";
        case Tok::P_Color_Brown_1400: return "primitive.color.brown.1400";
        case Tok::P_Color_Brown_1500: return "primitive.color.brown.1500";
        case Tok::P_Color_Brown_1600: return "primitive.color.brown.1600";
        case Tok::P_Color_Celery_100: return "primitive.color.celery.100";
        case Tok::P_Color_Celery_200: return "primitive.color.celery.200";
        case Tok::P_Color_Celery_300: return "primitive.color.celery.300";
        case Tok::P_Color_Celery_400: return "primitive.color.celery.400";
        case Tok::P_Color_Celery_500: return "primitive.color.celery.500";
        case Tok::P_Color_Celery_600: return "primitive.color.celery.600";
        case Tok::P_Color_Celery_700: return "primitive.color.celery.700";
        case Tok::P_Color_Celery_800: return "primitive.color.celery.800";
        case Tok::P_Color_Celery_900: return "primitive.color.celery.900";
        case Tok::P_Color_Celery_1000: return "primitive.color.celery.1000";
        case Tok::P_Color_Celery_1100: return "primitive.color.celery.1100";
        case Tok::P_Color_Celery_1200: return "primitive.color.celery.1200";
        case Tok::P_Color_Celery_1300: return "primitive.color.celery.1300";
        case Tok::P_Color_Celery_1400: return "primitive.color.celery.1400";
        case Tok::P_Color_Celery_1500: return "primitive.color.celery.1500";
        case Tok::P_Color_Celery_1600: return "primitive.color.celery.1600";
        case Tok::P_Color_Chartreuse_100: return "primitive.color.chartreuse.100";
        case Tok::P_Color_Chartreuse_200: return "primitive.color.chartreuse.200";
        case Tok::P_Color_Chartreuse_300: return "primitive.color.chartreuse.300";
        case Tok::P_Color_Chartreuse_400: return "primitive.color.chartreuse.400";
        case Tok::P_Color_Chartreuse_500: return "primitive.color.chartreuse.500";
        case Tok::P_Color_Chartreuse_600: return "primitive.color.chartreuse.600";
        case Tok::P_Color_Chartreuse_700: return "primitive.color.chartreuse.700";
        case Tok::P_Color_Chartreuse_800: return "primitive.color.chartreuse.800";
        case Tok::P_Color_Chartreuse_900: return "primitive.color.chartreuse.900";
        case Tok::P_Color_Chartreuse_1000: return "primitive.color.chartreuse.1000";
        case Tok::P_Color_Chartreuse_1100: return "primitive.color.chartreuse.1100";
        case Tok::P_Color_Chartreuse_1200: return "primitive.color.chartreuse.1200";
        case Tok::P_Color_Chartreuse_1300: return "primitive.color.chartreuse.1300";
        case Tok::P_Color_Chartreuse_1400: return "primitive.color.chartreuse.1400";
        case Tok::P_Color_Chartreuse_1500: return "primitive.color.chartreuse.1500";
        case Tok::P_Color_Chartreuse_1600: return "primitive.color.chartreuse.1600";
        case Tok::P_Color_Cinnamon_100: return "primitive.color.cinnamon.100";
        case Tok::P_Color_Cinnamon_200: return "primitive.color.cinnamon.200";
        case Tok::P_Color_Cinnamon_300: return "primitive.color.cinnamon.300";
        case Tok::P_Color_Cinnamon_400: return "primitive.color.cinnamon.400";
        case Tok::P_Color_Cinnamon_500: return "primitive.color.cinnamon.500";
        case Tok::P_Color_Cinnamon_600: return "primitive.color.cinnamon.600";
        case Tok::P_Color_Cinnamon_700: return "primitive.color.cinnamon.700";
        case Tok::P_Color_Cinnamon_800: return "primitive.color.cinnamon.800";
        case Tok::P_Color_Cinnamon_900: return "primitive.color.cinnamon.900";
        case Tok::P_Color_Cinnamon_1000: return "primitive.color.cinnamon.1000";
        case Tok::P_Color_Cinnamon_1100: return "primitive.color.cinnamon.1100";
        case Tok::P_Color_Cinnamon_1200: return "primitive.color.cinnamon.1200";
        case Tok::P_Color_Cinnamon_1300: return "primitive.color.cinnamon.1300";
        case Tok::P_Color_Cinnamon_1400: return "primitive.color.cinnamon.1400";
        case Tok::P_Color_Cinnamon_1500: return "primitive.color.cinnamon.1500";
        case Tok::P_Color_Cinnamon_1600: return "primitive.color.cinnamon.1600";
        case Tok::P_Color_Cyan_100: return "primitive.color.cyan.100";
        case Tok::P_Color_Cyan_200: return "primitive.color.cyan.200";
        case Tok::P_Color_Cyan_300: return "primitive.color.cyan.300";
        case Tok::P_Color_Cyan_400: return "primitive.color.cyan.400";
        case Tok::P_Color_Cyan_500: return "primitive.color.cyan.500";
        case Tok::P_Color_Cyan_600: return "primitive.color.cyan.600";
        case Tok::P_Color_Cyan_700: return "primitive.color.cyan.700";
        case Tok::P_Color_Cyan_800: return "primitive.color.cyan.800";
        case Tok::P_Color_Cyan_900: return "primitive.color.cyan.900";
        case Tok::P_Color_Cyan_1000: return "primitive.color.cyan.1000";
        case Tok::P_Color_Cyan_1100: return "primitive.color.cyan.1100";
        case Tok::P_Color_Cyan_1200: return "primitive.color.cyan.1200";
        case Tok::P_Color_Cyan_1300: return "primitive.color.cyan.1300";
        case Tok::P_Color_Cyan_1400: return "primitive.color.cyan.1400";
        case Tok::P_Color_Cyan_1500: return "primitive.color.cyan.1500";
        case Tok::P_Color_Cyan_1600: return "primitive.color.cyan.1600";
        case Tok::P_Color_Fuchsia_100: return "primitive.color.fuchsia.100";
        case Tok::P_Color_Fuchsia_200: return "primitive.color.fuchsia.200";
        case Tok::P_Color_Fuchsia_300: return "primitive.color.fuchsia.300";
        case Tok::P_Color_Fuchsia_400: return "primitive.color.fuchsia.400";
        case Tok::P_Color_Fuchsia_500: return "primitive.color.fuchsia.500";
        case Tok::P_Color_Fuchsia_600: return "primitive.color.fuchsia.600";
        case Tok::P_Color_Fuchsia_700: return "primitive.color.fuchsia.700";
        case Tok::P_Color_Fuchsia_800: return "primitive.color.fuchsia.800";
        case Tok::P_Color_Fuchsia_900: return "primitive.color.fuchsia.900";
        case Tok::P_Color_Fuchsia_1000: return "primitive.color.fuchsia.1000";
        case Tok::P_Color_Fuchsia_1100: return "primitive.color.fuchsia.1100";
        case Tok::P_Color_Fuchsia_1200: return "primitive.color.fuchsia.1200";
        case Tok::P_Color_Fuchsia_1300: return "primitive.color.fuchsia.1300";
        case Tok::P_Color_Fuchsia_1400: return "primitive.color.fuchsia.1400";
        case Tok::P_Color_Fuchsia_1500: return "primitive.color.fuchsia.1500";
        case Tok::P_Color_Fuchsia_1600: return "primitive.color.fuchsia.1600";
        case Tok::P_Color_Green_100: return "primitive.color.green.100";
        case Tok::P_Color_Green_200: return "primitive.color.green.200";
        case Tok::P_Color_Green_300: return "primitive.color.green.300";
        case Tok::P_Color_Green_400: return "primitive.color.green.400";
        case Tok::P_Color_Green_500: return "primitive.color.green.500";
        case Tok::P_Color_Green_600: return "primitive.color.green.600";
        case Tok::P_Color_Green_700: return "primitive.color.green.700";
        case Tok::P_Color_Green_800: return "primitive.color.green.800";
        case Tok::P_Color_Green_900: return "primitive.color.green.900";
        case Tok::P_Color_Green_1000: return "primitive.color.green.1000";
        case Tok::P_Color_Green_1100: return "primitive.color.green.1100";
        case Tok::P_Color_Green_1200: return "primitive.color.green.1200";
        case Tok::P_Color_Green_1300: return "primitive.color.green.1300";
        case Tok::P_Color_Green_1400: return "primitive.color.green.1400";
        case Tok::P_Color_Green_1500: return "primitive.color.green.1500";
        case Tok::P_Color_Green_1600: return "primitive.color.green.1600";
        case Tok::P_Color_Indigo_100: return "primitive.color.indigo.100";
        case Tok::P_Color_Indigo_200: return "primitive.color.indigo.200";
        case Tok::P_Color_Indigo_300: return "primitive.color.indigo.300";
        case Tok::P_Color_Indigo_400: return "primitive.color.indigo.400";
        case Tok::P_Color_Indigo_500: return "primitive.color.indigo.500";
        case Tok::P_Color_Indigo_600: return "primitive.color.indigo.600";
        case Tok::P_Color_Indigo_700: return "primitive.color.indigo.700";
        case Tok::P_Color_Indigo_800: return "primitive.color.indigo.800";
        case Tok::P_Color_Indigo_900: return "primitive.color.indigo.900";
        case Tok::P_Color_Indigo_1000: return "primitive.color.indigo.1000";
        case Tok::P_Color_Indigo_1100: return "primitive.color.indigo.1100";
        case Tok::P_Color_Indigo_1200: return "primitive.color.indigo.1200";
        case Tok::P_Color_Indigo_1300: return "primitive.color.indigo.1300";
        case Tok::P_Color_Indigo_1400: return "primitive.color.indigo.1400";
        case Tok::P_Color_Indigo_1500: return "primitive.color.indigo.1500";
        case Tok::P_Color_Indigo_1600: return "primitive.color.indigo.1600";
        case Tok::P_Color_Magenta_100: return "primitive.color.magenta.100";
        case Tok::P_Color_Magenta_200: return "primitive.color.magenta.200";
        case Tok::P_Color_Magenta_300: return "primitive.color.magenta.300";
        case Tok::P_Color_Magenta_400: return "primitive.color.magenta.400";
        case Tok::P_Color_Magenta_500: return "primitive.color.magenta.500";
        case Tok::P_Color_Magenta_600: return "primitive.color.magenta.600";
        case Tok::P_Color_Magenta_700: return "primitive.color.magenta.700";
        case Tok::P_Color_Magenta_800: return "primitive.color.magenta.800";
        case Tok::P_Color_Magenta_900: return "primitive.color.magenta.900";
        case Tok::P_Color_Magenta_1000: return "primitive.color.magenta.1000";
        case Tok::P_Color_Magenta_1100: return "primitive.color.magenta.1100";
        case Tok::P_Color_Magenta_1200: return "primitive.color.magenta.1200";
        case Tok::P_Color_Magenta_1300: return "primitive.color.magenta.1300";
        case Tok::P_Color_Magenta_1400: return "primitive.color.magenta.1400";
        case Tok::P_Color_Magenta_1500: return "primitive.color.magenta.1500";
        case Tok::P_Color_Magenta_1600: return "primitive.color.magenta.1600";
        case Tok::P_Color_Orange_100: return "primitive.color.orange.100";
        case Tok::P_Color_Orange_200: return "primitive.color.orange.200";
        case Tok::P_Color_Orange_300: return "primitive.color.orange.300";
        case Tok::P_Color_Orange_400: return "primitive.color.orange.400";
        case Tok::P_Color_Orange_500: return "primitive.color.orange.500";
        case Tok::P_Color_Orange_600: return "primitive.color.orange.600";
        case Tok::P_Color_Orange_700: return "primitive.color.orange.700";
        case Tok::P_Color_Orange_800: return "primitive.color.orange.800";
        case Tok::P_Color_Orange_900: return "primitive.color.orange.900";
        case Tok::P_Color_Orange_1000: return "primitive.color.orange.1000";
        case Tok::P_Color_Orange_1100: return "primitive.color.orange.1100";
        case Tok::P_Color_Orange_1200: return "primitive.color.orange.1200";
        case Tok::P_Color_Orange_1300: return "primitive.color.orange.1300";
        case Tok::P_Color_Orange_1400: return "primitive.color.orange.1400";
        case Tok::P_Color_Orange_1500: return "primitive.color.orange.1500";
        case Tok::P_Color_Orange_1600: return "primitive.color.orange.1600";
        case Tok::P_Color_Pink_100: return "primitive.color.pink.100";
        case Tok::P_Color_Pink_200: return "primitive.color.pink.200";
        case Tok::P_Color_Pink_300: return "primitive.color.pink.300";
        case Tok::P_Color_Pink_400: return "primitive.color.pink.400";
        case Tok::P_Color_Pink_500: return "primitive.color.pink.500";
        case Tok::P_Color_Pink_600: return "primitive.color.pink.600";
        case Tok::P_Color_Pink_700: return "primitive.color.pink.700";
        case Tok::P_Color_Pink_800: return "primitive.color.pink.800";
        case Tok::P_Color_Pink_900: return "primitive.color.pink.900";
        case Tok::P_Color_Pink_1000: return "primitive.color.pink.1000";
        case Tok::P_Color_Pink_1100: return "primitive.color.pink.1100";
        case Tok::P_Color_Pink_1200: return "primitive.color.pink.1200";
        case Tok::P_Color_Pink_1300: return "primitive.color.pink.1300";
        case Tok::P_Color_Pink_1400: return "primitive.color.pink.1400";
        case Tok::P_Color_Pink_1500: return "primitive.color.pink.1500";
        case Tok::P_Color_Pink_1600: return "primitive.color.pink.1600";
        case Tok::P_Color_Purple_100: return "primitive.color.purple.100";
        case Tok::P_Color_Purple_200: return "primitive.color.purple.200";
        case Tok::P_Color_Purple_300: return "primitive.color.purple.300";
        case Tok::P_Color_Purple_400: return "primitive.color.purple.400";
        case Tok::P_Color_Purple_500: return "primitive.color.purple.500";
        case Tok::P_Color_Purple_600: return "primitive.color.purple.600";
        case Tok::P_Color_Purple_700: return "primitive.color.purple.700";
        case Tok::P_Color_Purple_800: return "primitive.color.purple.800";
        case Tok::P_Color_Purple_900: return "primitive.color.purple.900";
        case Tok::P_Color_Purple_1000: return "primitive.color.purple.1000";
        case Tok::P_Color_Purple_1100: return "primitive.color.purple.1100";
        case Tok::P_Color_Purple_1200: return "primitive.color.purple.1200";
        case Tok::P_Color_Purple_1300: return "primitive.color.purple.1300";
        case Tok::P_Color_Purple_1400: return "primitive.color.purple.1400";
        case Tok::P_Color_Purple_1500: return "primitive.color.purple.1500";
        case Tok::P_Color_Purple_1600: return "primitive.color.purple.1600";
        case Tok::P_Color_Red_100: return "primitive.color.red.100";
        case Tok::P_Color_Red_200: return "primitive.color.red.200";
        case Tok::P_Color_Red_300: return "primitive.color.red.300";
        case Tok::P_Color_Red_400: return "primitive.color.red.400";
        case Tok::P_Color_Red_500: return "primitive.color.red.500";
        case Tok::P_Color_Red_600: return "primitive.color.red.600";
        case Tok::P_Color_Red_700: return "primitive.color.red.700";
        case Tok::P_Color_Red_800: return "primitive.color.red.800";
        case Tok::P_Color_Red_900: return "primitive.color.red.900";
        case Tok::P_Color_Red_1000: return "primitive.color.red.1000";
        case Tok::P_Color_Red_1100: return "primitive.color.red.1100";
        case Tok::P_Color_Red_1200: return "primitive.color.red.1200";
        case Tok::P_Color_Red_1300: return "primitive.color.red.1300";
        case Tok::P_Color_Red_1400: return "primitive.color.red.1400";
        case Tok::P_Color_Red_1500: return "primitive.color.red.1500";
        case Tok::P_Color_Red_1600: return "primitive.color.red.1600";
        case Tok::P_Color_Seafoam_100: return "primitive.color.seafoam.100";
        case Tok::P_Color_Seafoam_200: return "primitive.color.seafoam.200";
        case Tok::P_Color_Seafoam_300: return "primitive.color.seafoam.300";
        case Tok::P_Color_Seafoam_400: return "primitive.color.seafoam.400";
        case Tok::P_Color_Seafoam_500: return "primitive.color.seafoam.500";
        case Tok::P_Color_Seafoam_600: return "primitive.color.seafoam.600";
        case Tok::P_Color_Seafoam_700: return "primitive.color.seafoam.700";
        case Tok::P_Color_Seafoam_800: return "primitive.color.seafoam.800";
        case Tok::P_Color_Seafoam_900: return "primitive.color.seafoam.900";
        case Tok::P_Color_Seafoam_1000: return "primitive.color.seafoam.1000";
        case Tok::P_Color_Seafoam_1100: return "primitive.color.seafoam.1100";
        case Tok::P_Color_Seafoam_1200: return "primitive.color.seafoam.1200";
        case Tok::P_Color_Seafoam_1300: return "primitive.color.seafoam.1300";
        case Tok::P_Color_Seafoam_1400: return "primitive.color.seafoam.1400";
        case Tok::P_Color_Seafoam_1500: return "primitive.color.seafoam.1500";
        case Tok::P_Color_Seafoam_1600: return "primitive.color.seafoam.1600";
        case Tok::P_Color_Silver_100: return "primitive.color.silver.100";
        case Tok::P_Color_Silver_200: return "primitive.color.silver.200";
        case Tok::P_Color_Silver_300: return "primitive.color.silver.300";
        case Tok::P_Color_Silver_400: return "primitive.color.silver.400";
        case Tok::P_Color_Silver_500: return "primitive.color.silver.500";
        case Tok::P_Color_Silver_600: return "primitive.color.silver.600";
        case Tok::P_Color_Silver_700: return "primitive.color.silver.700";
        case Tok::P_Color_Silver_800: return "primitive.color.silver.800";
        case Tok::P_Color_Silver_900: return "primitive.color.silver.900";
        case Tok::P_Color_Silver_1000: return "primitive.color.silver.1000";
        case Tok::P_Color_Silver_1100: return "primitive.color.silver.1100";
        case Tok::P_Color_Silver_1200: return "primitive.color.silver.1200";
        case Tok::P_Color_Silver_1300: return "primitive.color.silver.1300";
        case Tok::P_Color_Silver_1400: return "primitive.color.silver.1400";
        case Tok::P_Color_Silver_1500: return "primitive.color.silver.1500";
        case Tok::P_Color_Silver_1600: return "primitive.color.silver.1600";
        case Tok::P_Color_Turquoise_100: return "primitive.color.turquoise.100";
        case Tok::P_Color_Turquoise_200: return "primitive.color.turquoise.200";
        case Tok::P_Color_Turquoise_300: return "primitive.color.turquoise.300";
        case Tok::P_Color_Turquoise_400: return "primitive.color.turquoise.400";
        case Tok::P_Color_Turquoise_500: return "primitive.color.turquoise.500";
        case Tok::P_Color_Turquoise_600: return "primitive.color.turquoise.600";
        case Tok::P_Color_Turquoise_700: return "primitive.color.turquoise.700";
        case Tok::P_Color_Turquoise_800: return "primitive.color.turquoise.800";
        case Tok::P_Color_Turquoise_900: return "primitive.color.turquoise.900";
        case Tok::P_Color_Turquoise_1000: return "primitive.color.turquoise.1000";
        case Tok::P_Color_Turquoise_1100: return "primitive.color.turquoise.1100";
        case Tok::P_Color_Turquoise_1200: return "primitive.color.turquoise.1200";
        case Tok::P_Color_Turquoise_1300: return "primitive.color.turquoise.1300";
        case Tok::P_Color_Turquoise_1400: return "primitive.color.turquoise.1400";
        case Tok::P_Color_Turquoise_1500: return "primitive.color.turquoise.1500";
        case Tok::P_Color_Turquoise_1600: return "primitive.color.turquoise.1600";
        case Tok::P_Color_Yellow_100: return "primitive.color.yellow.100";
        case Tok::P_Color_Yellow_200: return "primitive.color.yellow.200";
        case Tok::P_Color_Yellow_300: return "primitive.color.yellow.300";
        case Tok::P_Color_Yellow_400: return "primitive.color.yellow.400";
        case Tok::P_Color_Yellow_500: return "primitive.color.yellow.500";
        case Tok::P_Color_Yellow_600: return "primitive.color.yellow.600";
        case Tok::P_Color_Yellow_700: return "primitive.color.yellow.700";
        case Tok::P_Color_Yellow_800: return "primitive.color.yellow.800";
        case Tok::P_Color_Yellow_900: return "primitive.color.yellow.900";
        case Tok::P_Color_Yellow_1000: return "primitive.color.yellow.1000";
        case Tok::P_Color_Yellow_1100: return "primitive.color.yellow.1100";
        case Tok::P_Color_Yellow_1200: return "primitive.color.yellow.1200";
        case Tok::P_Color_Yellow_1300: return "primitive.color.yellow.1300";
        case Tok::P_Color_Yellow_1400: return "primitive.color.yellow.1400";
        case Tok::P_Color_Yellow_1500: return "primitive.color.yellow.1500";
        case Tok::P_Color_Yellow_1600: return "primitive.color.yellow.1600";
        case Tok::P_Color_TransparentBlack_25: return "primitive.color.transparent-black.25";
        case Tok::P_Color_TransparentBlack_50: return "primitive.color.transparent-black.50";
        case Tok::P_Color_TransparentBlack_75: return "primitive.color.transparent-black.75";
        case Tok::P_Color_TransparentBlack_100: return "primitive.color.transparent-black.100";
        case Tok::P_Color_TransparentBlack_200: return "primitive.color.transparent-black.200";
        case Tok::P_Color_TransparentBlack_300: return "primitive.color.transparent-black.300";
        case Tok::P_Color_TransparentBlack_400: return "primitive.color.transparent-black.400";
        case Tok::P_Color_TransparentBlack_500: return "primitive.color.transparent-black.500";
        case Tok::P_Color_TransparentBlack_600: return "primitive.color.transparent-black.600";
        case Tok::P_Color_TransparentBlack_700: return "primitive.color.transparent-black.700";
        case Tok::P_Color_TransparentBlack_800: return "primitive.color.transparent-black.800";
        case Tok::P_Color_TransparentBlack_900: return "primitive.color.transparent-black.900";
        case Tok::P_Color_TransparentBlack_1000: return "primitive.color.transparent-black.1000";
        case Tok::P_Color_TransparentWhite_25: return "primitive.color.transparent-white.25";
        case Tok::P_Color_TransparentWhite_50: return "primitive.color.transparent-white.50";
        case Tok::P_Color_TransparentWhite_75: return "primitive.color.transparent-white.75";
        case Tok::P_Color_TransparentWhite_100: return "primitive.color.transparent-white.100";
        case Tok::P_Color_TransparentWhite_200: return "primitive.color.transparent-white.200";
        case Tok::P_Color_TransparentWhite_300: return "primitive.color.transparent-white.300";
        case Tok::P_Color_TransparentWhite_400: return "primitive.color.transparent-white.400";
        case Tok::P_Color_TransparentWhite_500: return "primitive.color.transparent-white.500";
        case Tok::P_Color_TransparentWhite_600: return "primitive.color.transparent-white.600";
        case Tok::P_Color_TransparentWhite_700: return "primitive.color.transparent-white.700";
        case Tok::P_Color_TransparentWhite_800: return "primitive.color.transparent-white.800";
        case Tok::P_Color_TransparentWhite_900: return "primitive.color.transparent-white.900";
        case Tok::P_Color_TransparentWhite_1000: return "primitive.color.transparent-white.1000";
        case Tok::P_Color_Static_Blue_900: return "primitive.color.static.blue.900";
        case Tok::P_Color_Static_Blue_1000: return "primitive.color.static.blue.1000";
        case Tok::P_Color_Static_Red_400: return "primitive.color.static.red.400";
        case Tok::P_Color_Static_Red_600: return "primitive.color.static.red.600";
        case Tok::P_Color_Static_Red_800: return "primitive.color.static.red.800";
        case Tok::P_Color_Static_Red_900: return "primitive.color.static.red.900";
        case Tok::P_Color_Static_Red_1000: return "primitive.color.static.red.1000";
        case Tok::P_Color_Static_Green_400: return "primitive.color.static.green.400";
        case Tok::P_Color_Static_Green_600: return "primitive.color.static.green.600";
        case Tok::P_Color_Static_Green_800: return "primitive.color.static.green.800";
        case Tok::P_Color_Static_Orange_400: return "primitive.color.static.orange.400";
        case Tok::P_Color_Static_Orange_600: return "primitive.color.static.orange.600";
        case Tok::P_Color_Static_Orange_800: return "primitive.color.static.orange.800";
        case Tok::P_Opacity_0: return "primitive.opacity.0";
        case Tok::P_Opacity_100: return "primitive.opacity.100";
        case Tok::P_Opacity_200: return "primitive.opacity.200";
        case Tok::P_Opacity_300: return "primitive.opacity.300";
        case Tok::P_Opacity_400: return "primitive.opacity.400";
        case Tok::P_Opacity_500: return "primitive.opacity.500";
        case Tok::P_Opacity_600: return "primitive.opacity.600";
        case Tok::P_Opacity_700: return "primitive.opacity.700";
        case Tok::P_Opacity_800: return "primitive.opacity.800";
        case Tok::P_Opacity_900: return "primitive.opacity.900";
        case Tok::P_Opacity_1000: return "primitive.opacity.1000";
        case Tok::P_Spacing_0: return "primitive.spacing.0";
        case Tok::P_Spacing_50: return "primitive.spacing.50";
        case Tok::P_Spacing_100: return "primitive.spacing.100";
        case Tok::P_Spacing_200: return "primitive.spacing.200";
        case Tok::P_Spacing_300: return "primitive.spacing.300";
        case Tok::P_Spacing_400: return "primitive.spacing.400";
        case Tok::P_Spacing_500: return "primitive.spacing.500";
        case Tok::P_Spacing_600: return "primitive.spacing.600";
        case Tok::P_Spacing_700: return "primitive.spacing.700";
        case Tok::P_Spacing_800: return "primitive.spacing.800";
        case Tok::P_Spacing_900: return "primitive.spacing.900";
        case Tok::P_Spacing_1000: return "primitive.spacing.1000";
        case Tok::P_Radius_none: return "primitive.radius.none";
        case Tok::P_Radius_100: return "primitive.radius.100";
        case Tok::P_Radius_200: return "primitive.radius.200";
        case Tok::P_Radius_300: return "primitive.radius.300";
        case Tok::P_Radius_400: return "primitive.radius.400";
        case Tok::P_Radius_500: return "primitive.radius.500";
        case Tok::P_Radius_600: return "primitive.radius.600";
        case Tok::P_Radius_700: return "primitive.radius.700";
        case Tok::P_Radius_800: return "primitive.radius.800";
        case Tok::P_Radius_900: return "primitive.radius.900";
        case Tok::P_Radius_Full: return "primitive.radius.full";
        case Tok::P_StrokeWidth_100: return "primitive.stroke.width.100";
        case Tok::P_StrokeWidth_150: return "primitive.stroke.width.150";
        case Tok::P_StrokeWidth_200: return "primitive.stroke.width.200";
        case Tok::P_StrokeWidth_300: return "primitive.stroke.width.300";
        case Tok::P_StrokeWidth_400: return "primitive.stroke.width.400";
        case Tok::P_StrokeWidth_0: return "primitive.stroke.width.0";
        case Tok::P_Size_75: return "primitive.size.75";
        case Tok::P_Size_100: return "primitive.size.100";
        case Tok::P_Size_200: return "primitive.size.200";
        case Tok::P_Size_400: return "primitive.size.400";
        case Tok::P_Size_800: return "primitive.size.800";
        case Tok::P_FontSize_10: return "primitive.font-size.10";
        case Tok::P_FontSize_20: return "primitive.font-size.20";
        case Tok::P_FontSize_30: return "primitive.font-size.30";
        case Tok::P_FontSize_40: return "primitive.font-size.40";
        case Tok::P_FontSize_50: return "primitive.font-size.50";
        case Tok::P_FontSize_60: return "primitive.font-size.60";
        case Tok::P_FontSize_70: return "primitive.font-size.70";
        case Tok::P_FontSize_80: return "primitive.font-size.80";
        case Tok::P_FontSize_90: return "primitive.font-size.90";
        case Tok::P_FontSize_100: return "primitive.font-size.100";
        case Tok::P_FontSize_110: return "primitive.font-size.110";
        case Tok::P_FontSize_120: return "primitive.font-size.120";
        case Tok::P_FontSize_130: return "primitive.font-size.130";
        case Tok::P_FontSize_140: return "primitive.font-size.140";
        case Tok::P_FontSize_150: return "primitive.font-size.150";
        case Tok::P_FontSize_160: return "primitive.font-size.160";
        case Tok::P_FontSize_170: return "primitive.font-size.170";
        case Tok::P_FontSize_180: return "primitive.font-size.180";
        case Tok::P_FontSize_190: return "primitive.font-size.190";
        case Tok::P_FontWeight_Thin: return "primitive.font.weight.thin";
        case Tok::P_FontWeight_ExtraLight: return "primitive.font.weight.extra-light";
        case Tok::P_FontWeight_Light: return "primitive.font.weight.light";
        case Tok::P_FontWeight_Regular: return "primitive.font.weight.regular";
        case Tok::P_FontWeight_Medium: return "primitive.font.weight.medium";
        case Tok::P_FontWeight_SemiBold: return "primitive.font.weight.semi-bold";
        case Tok::P_FontWeight_Bold: return "primitive.font.weight.bold";
        case Tok::P_FontWeight_ExtraBold: return "primitive.font.weight.extra-bold";
        case Tok::P_FontWeight_Black: return "primitive.font.weight.black";
        case Tok::P_FontWeight_ExtraBlack: return "primitive.font.weight.extra-black";
        case Tok::P_FontScale_Base: return "primitive.font.scale.base";
        case Tok::P_FontScale_Ratio: return "primitive.font.scale.ratio";
        case Tok::P_FontScale_75: return "primitive.font-scale.75";
        case Tok::P_FontScale_100: return "primitive.font-scale.100";
        case Tok::P_FontScale_125: return "primitive.font-scale.125";
        case Tok::P_FontScale_150: return "primitive.font-scale.150";
        case Tok::P_Scale_75: return "primitive.scale.75";
        case Tok::P_Scale_100: return "primitive.scale.100";
        case Tok::P_Scale_125: return "primitive.scale.125";
        case Tok::P_Scale_150: return "primitive.scale.150";
        case Tok::P_LineHeight_None: return "primitive.font.line-height.none";
        case Tok::P_LineHeight_Tight: return "primitive.font.line-height.tight";
        case Tok::P_LineHeight_Snug: return "primitive.font.line-height.snug";
        case Tok::P_LineHeight_Normal: return "primitive.font.line-height.normal";
        case Tok::P_LineHeight_Relaxed: return "primitive.font.line-height.relaxed";
        case Tok::P_LineHeight_Loose: return "primitive.font.line-height.loose";
        case Tok::P_LineHeightCjk_Compact: return "primitive.font.line-height.cjk.compact";
        case Tok::P_LineHeightCjk_Normal: return "primitive.font.line-height.cjk.normal";
        case Tok::P_LineHeightCjk_Relaxed: return "primitive.font.line-height.cjk.relaxed";
        case Tok::P_Tracking_Tighter: return "primitive.font.tracking.tighter";
        case Tok::P_Tracking_Tight: return "primitive.font.tracking.tight";
        case Tok::P_Tracking_Normal: return "primitive.font.tracking.normal";
        case Tok::P_Tracking_Wide: return "primitive.font.tracking.wide";
        case Tok::P_Tracking_Wider: return "primitive.font.tracking.wider";
        case Tok::P_Tracking_Widest: return "primitive.font.tracking.widest";
        case Tok::P_FontStyle_Normal: return "primitive.font.style.normal";
        case Tok::P_FontStyle_Italic: return "primitive.font.style.italic";
        case Tok::P_FontFamily_Sans: return "primitive.font.family.sans";
        case Tok::P_FontFamily_Serif: return "primitive.font.family.serif";
        case Tok::P_FontFamily_Mono: return "primitive.font.family.mono";
        case Tok::P_FontFamily_Cjk: return "primitive.font.family.cjk";
        case Tok::P_FontFamily_CjkSerif: return "primitive.font.family.cjk-serif";
        case Tok::P_Layer_Ground: return "primitive.layer.ground";
        case Tok::P_Layer_Low: return "primitive.layer.low";
        case Tok::P_Layer_Mid: return "primitive.layer.mid";
        case Tok::P_Layer_High: return "primitive.layer.high";
        case Tok::P_Layer_Veil: return "primitive.layer.veil";
        case Tok::P_Layer_Raised: return "primitive.layer.raised";
        case Tok::P_Layer_Peak: return "primitive.layer.peak";
        case Tok::P_Layer_Critical: return "primitive.layer.critical";
        case Tok::P_Layer_Absolute: return "primitive.layer.absolute";
        case Tok::P_Duration_Instant: return "primitive.duration.instant";
        case Tok::P_Duration_Fast: return "primitive.duration.fast";
        case Tok::P_Duration_Normal: return "primitive.duration.normal";
        case Tok::P_Duration_Slow: return "primitive.duration.slow";
        case Tok::P_Duration_Slower: return "primitive.duration.slower";
        case Tok::P_Duration_Slowest: return "primitive.duration.slowest";
        case Tok::P_Easing_Linear: return "primitive.easing.linear";
        case Tok::P_Easing_Ease: return "primitive.easing.ease";
        case Tok::P_Easing_EaseIn: return "primitive.easing.ease-in";
        case Tok::P_Easing_EaseOut: return "primitive.easing.ease-out";
        case Tok::P_Easing_EaseInOut: return "primitive.easing.ease-in-out";
        case Tok::P_Easing_Spring: return "primitive.easing.spring";
        case Tok::P_Easing_Decelerate: return "primitive.easing.decelerate";
        case Tok::P_Easing_Accelerate: return "primitive.easing.accelerate";
        case Tok::P_Easing_Overshoot: return "primitive.easing.overshoot";
        case Tok::P_GradientAngle_0: return "primitive.gradient.angle.0";
        case Tok::P_GradientAngle_45: return "primitive.gradient.angle.45";
        case Tok::P_GradientAngle_90: return "primitive.gradient.angle.90";
        case Tok::P_GradientAngle_135: return "primitive.gradient.angle.135";
        case Tok::P_GradientAngle_180: return "primitive.gradient.angle.180";
        case Tok::P_GradientAngle_225: return "primitive.gradient.angle.225";
        case Tok::P_GradientAngle_270: return "primitive.gradient.angle.270";
        case Tok::P_GradientAngle_315: return "primitive.gradient.angle.315";
        case Tok::P_GradientStop_0: return "primitive.gradient.stop.0";
        case Tok::P_GradientStop_25: return "primitive.gradient.stop.25";
        case Tok::P_GradientStop_50: return "primitive.gradient.stop.50";
        case Tok::P_GradientStop_75: return "primitive.gradient.stop.75";
        case Tok::P_GradientStop_100: return "primitive.gradient.stop.100";
        case Tok::P_Align_Start: return "primitive.align.start";
        case Tok::P_Align_Center: return "primitive.align.center";
        case Tok::P_Align_End: return "primitive.align.end";
        case Tok::P_Config_ItemSpacing: return "primitive.config.item-spacing";
        case Tok::P_Config_ItemInnerSpacing: return "primitive.config.item-inner-spacing";
        case Tok::P_Config_CellPadding: return "primitive.config.cell-padding";
        case Tok::P_Config_TouchExtraPadding: return "primitive.config.touch-extra-padding";
        case Tok::P_Config_IndentSpacing: return "primitive.config.indent-spacing";
        case Tok::P_Config_ColumnsMinSpacing: return "primitive.config.columns-min-spacing";
        case Tok::P_Config_UndoSteps: return "primitive.config.undo-steps";
        case Tok::P_Config_DisplayWindowPadding: return "primitive.config.display-window-padding";
        case Tok::P_Config_DisplaySafeAreaPadding: return "primitive.config.display-safe-area-padding";
        case Tok::P_Config_SeparatorTextPadding: return "primitive.config.separator-text-padding";
        case Tok::P_Config_ButtonTextAlign: return "primitive.config.button-text-align";
        case Tok::P_Config_SelectableTextAlign: return "primitive.config.selectable-text-align";
        case Tok::P_Config_SeparatorTextAlign: return "primitive.config.separator-text-align";
        case Tok::P_Config_ColorButtonPosition: return "primitive.config.color-button-position";
        case Tok::P_Config_GrabMinSize: return "primitive.config.grab-min-size";
        case Tok::P_Config_LogSliderDeadzone: return "primitive.config.log-slider-deadzone";
        case Tok::P_Config_ColorMarkerSize: return "primitive.config.color-marker-size";
        case Tok::P_Config_MouseCursorScale: return "primitive.config.mouse-cursor-scale";
        case Tok::P_Config_HoverDelayStationary: return "primitive.config.hover-delay-stationary";
        case Tok::P_Config_HoverDelayShort: return "primitive.config.hover-delay-short";
        case Tok::P_Config_HoverDelayNormal: return "primitive.config.hover-delay-normal";
        case Tok::P_Config_DragThreshold: return "primitive.config.drag-threshold";
        case Tok::P_Border_Enabled: return "primitive.border.enabled";
        case Tok::P_Config_AntiAliasedLines: return "primitive.config.anti-aliased-lines";
        case Tok::P_Config_AntiAliasedLinesUseTex: return "primitive.config.anti-aliased-lines-use-tex";
        case Tok::P_Config_AntiAliasedFill: return "primitive.config.anti-aliased-fill";
        case Tok::P_Config_CurveTessellationTol: return "primitive.config.curve-tessellation-tol";
        case Tok::P_Config_CircleTessellationMaxError: return "primitive.config.circle-tessellation-max-error";
        case Tok::P_Config_TreeLinesFlags: return "primitive.config.tree-lines-flags";
        case Tok::P_Config_TreeLinesSize: return "primitive.config.tree-lines-size";
        case Tok::S_Accent_Color_100: return "semantic.accent.color.100";
        case Tok::S_Accent_Color_200: return "semantic.accent.color.200";
        case Tok::S_Accent_Color_300: return "semantic.accent.color.300";
        case Tok::S_Accent_Color_400: return "semantic.accent.color.400";
        case Tok::S_Accent_Color_500: return "semantic.accent.color.500";
        case Tok::S_Accent_Color_600: return "semantic.accent.color.600";
        case Tok::S_Accent_Color_700: return "semantic.accent.color.700";
        case Tok::S_Accent_Color_800: return "semantic.accent.color.800";
        case Tok::S_Accent_Color_900: return "semantic.accent.color.900";
        case Tok::S_Accent_Color_1000: return "semantic.accent.color.1000";
        case Tok::S_Accent_Color_1100: return "semantic.accent.color.1100";
        case Tok::S_Accent_Color_1200: return "semantic.accent.color.1200";
        case Tok::S_Accent_Color_1300: return "semantic.accent.color.1300";
        case Tok::S_Accent_Color_1400: return "semantic.accent.color.1400";
        case Tok::S_Accent_Color_1500: return "semantic.accent.color.1500";
        case Tok::S_Accent_Color_1600: return "semantic.accent.color.1600";
        case Tok::S_Info_Color_100: return "semantic.info.color.100";
        case Tok::S_Info_Color_200: return "semantic.info.color.200";
        case Tok::S_Info_Color_300: return "semantic.info.color.300";
        case Tok::S_Info_Color_400: return "semantic.info.color.400";
        case Tok::S_Info_Color_500: return "semantic.info.color.500";
        case Tok::S_Info_Color_600: return "semantic.info.color.600";
        case Tok::S_Info_Color_700: return "semantic.info.color.700";
        case Tok::S_Info_Color_800: return "semantic.info.color.800";
        case Tok::S_Info_Color_900: return "semantic.info.color.900";
        case Tok::S_Info_Color_1000: return "semantic.info.color.1000";
        case Tok::S_Info_Color_1100: return "semantic.info.color.1100";
        case Tok::S_Info_Color_1200: return "semantic.info.color.1200";
        case Tok::S_Info_Color_1300: return "semantic.info.color.1300";
        case Tok::S_Info_Color_1400: return "semantic.info.color.1400";
        case Tok::S_Info_Color_1500: return "semantic.info.color.1500";
        case Tok::S_Info_Color_1600: return "semantic.info.color.1600";
        case Tok::S_Negative_Color_100: return "semantic.negative.color.100";
        case Tok::S_Negative_Color_200: return "semantic.negative.color.200";
        case Tok::S_Negative_Color_300: return "semantic.negative.color.300";
        case Tok::S_Negative_Color_400: return "semantic.negative.color.400";
        case Tok::S_Negative_Color_500: return "semantic.negative.color.500";
        case Tok::S_Negative_Color_600: return "semantic.negative.color.600";
        case Tok::S_Negative_Color_700: return "semantic.negative.color.700";
        case Tok::S_Negative_Color_800: return "semantic.negative.color.800";
        case Tok::S_Negative_Color_900: return "semantic.negative.color.900";
        case Tok::S_Negative_Color_1000: return "semantic.negative.color.1000";
        case Tok::S_Negative_Color_1100: return "semantic.negative.color.1100";
        case Tok::S_Negative_Color_1200: return "semantic.negative.color.1200";
        case Tok::S_Negative_Color_1300: return "semantic.negative.color.1300";
        case Tok::S_Negative_Color_1400: return "semantic.negative.color.1400";
        case Tok::S_Negative_Color_1500: return "semantic.negative.color.1500";
        case Tok::S_Negative_Color_1600: return "semantic.negative.color.1600";
        case Tok::S_Positive_Color_100: return "semantic.positive.color.100";
        case Tok::S_Positive_Color_200: return "semantic.positive.color.200";
        case Tok::S_Positive_Color_300: return "semantic.positive.color.300";
        case Tok::S_Positive_Color_400: return "semantic.positive.color.400";
        case Tok::S_Positive_Color_500: return "semantic.positive.color.500";
        case Tok::S_Positive_Color_600: return "semantic.positive.color.600";
        case Tok::S_Positive_Color_700: return "semantic.positive.color.700";
        case Tok::S_Positive_Color_800: return "semantic.positive.color.800";
        case Tok::S_Positive_Color_900: return "semantic.positive.color.900";
        case Tok::S_Positive_Color_1000: return "semantic.positive.color.1000";
        case Tok::S_Positive_Color_1100: return "semantic.positive.color.1100";
        case Tok::S_Positive_Color_1200: return "semantic.positive.color.1200";
        case Tok::S_Positive_Color_1300: return "semantic.positive.color.1300";
        case Tok::S_Positive_Color_1400: return "semantic.positive.color.1400";
        case Tok::S_Positive_Color_1500: return "semantic.positive.color.1500";
        case Tok::S_Positive_Color_1600: return "semantic.positive.color.1600";
        case Tok::S_Notice_Color_100: return "semantic.notice.color.100";
        case Tok::S_Notice_Color_200: return "semantic.notice.color.200";
        case Tok::S_Notice_Color_300: return "semantic.notice.color.300";
        case Tok::S_Notice_Color_400: return "semantic.notice.color.400";
        case Tok::S_Notice_Color_500: return "semantic.notice.color.500";
        case Tok::S_Notice_Color_600: return "semantic.notice.color.600";
        case Tok::S_Notice_Color_700: return "semantic.notice.color.700";
        case Tok::S_Notice_Color_800: return "semantic.notice.color.800";
        case Tok::S_Notice_Color_900: return "semantic.notice.color.900";
        case Tok::S_Notice_Color_1000: return "semantic.notice.color.1000";
        case Tok::S_Notice_Color_1100: return "semantic.notice.color.1100";
        case Tok::S_Notice_Color_1200: return "semantic.notice.color.1200";
        case Tok::S_Notice_Color_1300: return "semantic.notice.color.1300";
        case Tok::S_Notice_Color_1400: return "semantic.notice.color.1400";
        case Tok::S_Notice_Color_1500: return "semantic.notice.color.1500";
        case Tok::S_Notice_Color_1600: return "semantic.notice.color.1600";
        case Tok::S_Neutral_Color_25: return "semantic.neutral.color.25";
        case Tok::S_Neutral_Color_50: return "semantic.neutral.color.50";
        case Tok::S_Neutral_Color_75: return "semantic.neutral.color.75";
        case Tok::S_Neutral_Color_100: return "semantic.neutral.color.100";
        case Tok::S_Neutral_Color_200: return "semantic.neutral.color.200";
        case Tok::S_Neutral_Color_300: return "semantic.neutral.color.300";
        case Tok::S_Neutral_Color_400: return "semantic.neutral.color.400";
        case Tok::S_Neutral_Color_500: return "semantic.neutral.color.500";
        case Tok::S_Neutral_Color_600: return "semantic.neutral.color.600";
        case Tok::S_Neutral_Color_700: return "semantic.neutral.color.700";
        case Tok::S_Neutral_Color_800: return "semantic.neutral.color.800";
        case Tok::S_Neutral_Color_900: return "semantic.neutral.color.900";
        case Tok::S_Neutral_Color_1000: return "semantic.neutral.color.1000";
        case Tok::S_Color_Accent_Default: return "semantic.background.accent.default";
        case Tok::S_Color_Accent_Hover: return "semantic.background.accent.hover";
        case Tok::S_Color_Accent_Down: return "semantic.background.accent.pressed";
        // ── State matrix: semantic.accent.color.<role>.<status> ──
        case Tok::S_Accent_Visual_Default:         return "semantic.accent.color.visual.default";
        case Tok::S_Accent_Visual_Positive:        return "semantic.accent.color.visual.positive";
        case Tok::S_Accent_Visual_Negative:        return "semantic.accent.color.visual.negative";
        case Tok::S_Accent_Visual_Info:            return "semantic.accent.color.visual.info";
        case Tok::S_Accent_Visual_Neutral:         return "semantic.accent.color.visual.neutral";
        case Tok::S_Accent_Visual_Notice:          return "semantic.accent.color.visual.notice";
        case Tok::S_Accent_Visual_Brand:           return "semantic.accent.color.visual.brand";
        case Tok::S_Accent_Hover_Default:          return "semantic.accent.color.hover.default";
        case Tok::S_Accent_Hover_Positive:         return "semantic.accent.color.hover.positive";
        case Tok::S_Accent_Hover_Negative:         return "semantic.accent.color.hover.negative";
        case Tok::S_Accent_Hover_Info:             return "semantic.accent.color.hover.info";
        case Tok::S_Accent_Hover_Neutral:          return "semantic.accent.color.hover.neutral";
        case Tok::S_Accent_Hover_Notice:           return "semantic.accent.color.hover.notice";
        case Tok::S_Accent_Hover_Brand:            return "semantic.accent.color.hover.brand";
        case Tok::S_Accent_Text_Default:           return "semantic.accent.color.text.default";
        case Tok::S_Accent_Text_Positive:          return "semantic.accent.color.text.positive";
        case Tok::S_Accent_Text_Negative:          return "semantic.accent.color.text.negative";
        case Tok::S_Accent_Text_Info:              return "semantic.accent.color.text.info";
        case Tok::S_Accent_Text_Neutral:           return "semantic.accent.color.text.neutral";
        case Tok::S_Accent_Text_Notice:            return "semantic.accent.color.text.notice";
        case Tok::S_Accent_Text_Brand:             return "semantic.accent.color.text.brand";
        case Tok::S_Accent_Selected_Default:       return "semantic.accent.color.selected.default";
        case Tok::S_Accent_Selected_Positive:      return "semantic.accent.color.selected.positive";
        case Tok::S_Accent_Selected_Negative:      return "semantic.accent.color.selected.negative";
        case Tok::S_Accent_Selected_Info:          return "semantic.accent.color.selected.info";
        case Tok::S_Accent_Selected_Neutral:       return "semantic.accent.color.selected.neutral";
        case Tok::S_Accent_Selected_Notice:        return "semantic.accent.color.selected.notice";
        case Tok::S_Accent_Selected_Brand:         return "semantic.accent.color.selected.brand";
        case Tok::S_Accent_HoverSelected_Default:  return "semantic.accent.color.hover-selected.default";
        case Tok::S_Accent_HoverSelected_Positive: return "semantic.accent.color.hover-selected.positive";
        case Tok::S_Accent_HoverSelected_Negative: return "semantic.accent.color.hover-selected.negative";
        case Tok::S_Accent_HoverSelected_Info:     return "semantic.accent.color.hover-selected.info";
        case Tok::S_Accent_HoverSelected_Neutral:  return "semantic.accent.color.hover-selected.neutral";
        case Tok::S_Accent_HoverSelected_Notice:   return "semantic.accent.color.hover-selected.notice";
        case Tok::S_Accent_HoverSelected_Brand:    return "semantic.accent.color.hover-selected.brand";
        case Tok::S_Accent_Active_Default:         return "semantic.accent.color.active.default";
        case Tok::S_Accent_Active_Positive:        return "semantic.accent.color.active.positive";
        case Tok::S_Accent_Active_Negative:        return "semantic.accent.color.active.negative";
        case Tok::S_Accent_Active_Info:            return "semantic.accent.color.active.info";
        case Tok::S_Accent_Active_Neutral:         return "semantic.accent.color.active.neutral";
        case Tok::S_Accent_Active_Notice:          return "semantic.accent.color.active.notice";
        case Tok::S_Accent_Active_Brand:           return "semantic.accent.color.active.brand";
        case Tok::S_Background_Accent_KbdFocus: return "semantic.background.accent.kbd-focus";
        case Tok::S_Background_Info_Default: return "semantic.background.info.default";
        case Tok::S_Background_Info_Hover: return "semantic.background.info.hover";
        case Tok::S_Background_Info_Pressed: return "semantic.background.info.pressed";
        case Tok::S_Background_Info_KbdFocus: return "semantic.background.info.kbd-focus";
        case Tok::S_Color_Negative_Default: return "semantic.background.negative.default";
        case Tok::S_Background_Negative_Hover: return "semantic.background.negative.hover";
        case Tok::S_Background_Negative_Pressed: return "semantic.background.negative.pressed";
        case Tok::S_Background_Negative_KbdFocus: return "semantic.background.negative.kbd-focus";
        case Tok::S_Color_Positive_Default: return "semantic.background.positive.default";
        case Tok::S_Background_Positive_Hover: return "semantic.background.positive.hover";
        case Tok::S_Background_Positive_Pressed: return "semantic.background.positive.pressed";
        case Tok::S_Background_Positive_KbdFocus: return "semantic.background.positive.kbd-focus";
        case Tok::S_Color_Notice_Default: return "semantic.background.notice.default";
        case Tok::S_Background_Neutral_Default: return "semantic.background.neutral.default";
        case Tok::S_Background_Neutral_Hover: return "semantic.background.neutral.hover";
        case Tok::S_Background_Neutral_Pressed: return "semantic.background.neutral.pressed";
        case Tok::S_Background_Neutral_KbdFocus: return "semantic.background.neutral.kbd-focus";
        case Tok::S_Background_Accent_Subtle: return "semantic.background.accent.subtle.default";
        case Tok::S_Background_Info_Subtle: return "semantic.background.info.subtle.default";
        case Tok::S_Background_Negative_Subtle: return "semantic.background.negative.subtle.default";
        case Tok::S_Background_Positive_Subtle: return "semantic.background.positive.subtle.default";
        case Tok::S_Background_Notice_Subtle: return "semantic.background.notice.subtle.default";
        case Tok::S_Background_Neutral_Subtle: return "semantic.background.neutral.subtle.default";
        case Tok::S_Background_Blue_Default: return "semantic.background.blue.default";
        case Tok::S_Background_Blue_Subtle: return "semantic.background.blue.subtle.default";
        case Tok::S_Background_Blue_Visual: return "semantic.background.blue.visual.default";
        case Tok::S_Background_Brown_Default: return "semantic.background.brown.default";
        case Tok::S_Background_Brown_Subtle: return "semantic.background.brown.subtle.default";
        case Tok::S_Background_Brown_Visual: return "semantic.background.brown.visual.default";
        case Tok::S_Background_Celery_Default: return "semantic.background.celery.default";
        case Tok::S_Background_Celery_Subtle: return "semantic.background.celery.subtle.default";
        case Tok::S_Background_Celery_Visual: return "semantic.background.celery.visual.default";
        case Tok::S_Background_Chartreuse_Default: return "semantic.background.chartreuse.default";
        case Tok::S_Background_Chartreuse_Subtle: return "semantic.background.chartreuse.subtle.default";
        case Tok::S_Background_Chartreuse_Visual: return "semantic.background.chartreuse.visual.default";
        case Tok::S_Background_Cinnamon_Default: return "semantic.background.cinnamon.default";
        case Tok::S_Background_Cinnamon_Subtle: return "semantic.background.cinnamon.subtle.default";
        case Tok::S_Background_Cinnamon_Visual: return "semantic.background.cinnamon.visual.default";
        case Tok::S_Background_Cyan_Default: return "semantic.background.cyan.default";
        case Tok::S_Background_Cyan_Subtle: return "semantic.background.cyan.subtle.default";
        case Tok::S_Background_Cyan_Visual: return "semantic.background.cyan.visual.default";
        case Tok::S_Background_Fuchsia_Default: return "semantic.background.fuchsia.default";
        case Tok::S_Background_Fuchsia_Subtle: return "semantic.background.fuchsia.subtle.default";
        case Tok::S_Background_Fuchsia_Visual: return "semantic.background.fuchsia.visual.default";
        case Tok::S_Background_Green_Default: return "semantic.background.green.default";
        case Tok::S_Background_Green_Subtle: return "semantic.background.green.subtle.default";
        case Tok::S_Background_Green_Visual: return "semantic.background.green.visual.default";
        case Tok::S_Background_Indigo_Default: return "semantic.background.indigo.default";
        case Tok::S_Background_Indigo_Subtle: return "semantic.background.indigo.subtle.default";
        case Tok::S_Background_Indigo_Visual: return "semantic.background.indigo.visual.default";
        case Tok::S_Background_Magenta_Default: return "semantic.background.magenta.default";
        case Tok::S_Background_Magenta_Subtle: return "semantic.background.magenta.subtle.default";
        case Tok::S_Background_Magenta_Visual: return "semantic.background.magenta.visual.default";
        case Tok::S_Background_Orange_Default: return "semantic.background.orange.default";
        case Tok::S_Background_Orange_Subtle: return "semantic.background.orange.subtle.default";
        case Tok::S_Background_Orange_Visual: return "semantic.background.orange.visual.default";
        case Tok::S_Background_Pink_Default: return "semantic.background.pink.default";
        case Tok::S_Background_Pink_Subtle: return "semantic.background.pink.subtle.default";
        case Tok::S_Background_Pink_Visual: return "semantic.background.pink.visual.default";
        case Tok::S_Background_Purple_Default: return "semantic.background.purple.default";
        case Tok::S_Background_Purple_Subtle: return "semantic.background.purple.subtle.default";
        case Tok::S_Background_Purple_Visual: return "semantic.background.purple.visual.default";
        case Tok::S_Background_Red_Default: return "semantic.background.red.default";
        case Tok::S_Background_Red_Subtle: return "semantic.background.red.subtle.default";
        case Tok::S_Background_Red_Visual: return "semantic.background.red.visual.default";
        case Tok::S_Background_Seafoam_Default: return "semantic.background.seafoam.default";
        case Tok::S_Background_Seafoam_Subtle: return "semantic.background.seafoam.subtle.default";
        case Tok::S_Background_Seafoam_Visual: return "semantic.background.seafoam.visual.default";
        case Tok::S_Background_Silver_Default: return "semantic.background.silver.default";
        case Tok::S_Background_Silver_Subtle: return "semantic.background.silver.subtle.default";
        case Tok::S_Background_Silver_Visual: return "semantic.background.silver.visual.default";
        case Tok::S_Background_Turquoise_Default: return "semantic.background.turquoise.default";
        case Tok::S_Background_Turquoise_Subtle: return "semantic.background.turquoise.subtle.default";
        case Tok::S_Background_Turquoise_Visual: return "semantic.background.turquoise.visual.default";
        case Tok::S_Background_Yellow_Default: return "semantic.background.yellow.default";
        case Tok::S_Background_Yellow_Subtle: return "semantic.background.yellow.subtle.default";
        case Tok::S_Background_Yellow_Visual: return "semantic.background.yellow.visual.default";
        case Tok::S_Background_Gray_Default: return "semantic.background.gray.default";
        case Tok::S_Background_Gray_Subtle: return "semantic.background.gray.subtle.default";
        case Tok::S_Background_Gray_Visual: return "semantic.background.gray.visual.default";
        case Tok::S_Color_Background_Default: return "semantic.background.base.default";
        case Tok::S_Color_Background_Layer1: return "semantic.background.sunken.default";
        case Tok::S_Color_Background_Layer2: return "semantic.background.raised.default";
        case Tok::S_Color_Background_Popup: return "semantic.background.floating.default";
        case Tok::S_Background_Dim: return "semantic.background.dim.default";
        case Tok::S_Content_Accent_Default: return "semantic.text.color.accent.default";
        case Tok::S_Content_Accent_Hover: return "semantic.text.color.accent.hover";
        case Tok::S_Content_Accent_Pressed: return "semantic.text.color.accent.pressed";
        case Tok::S_Content_Accent_KbdFocus: return "semantic.text.color.accent.kbd-focus";
        case Tok::S_Content_Accent_Selected: return "semantic.text.color.accent.selected";
        case Tok::S_Content_Negative_Default: return "semantic.text.color.negative.default";
        case Tok::S_Content_Negative_Hover: return "semantic.text.color.negative.hover";
        case Tok::S_Content_Negative_Pressed: return "semantic.text.color.negative.pressed";
        case Tok::S_Content_Negative_KbdFocus: return "semantic.text.color.negative.kbd-focus";
        case Tok::S_Content_Neutral_Default: return "semantic.text.color.neutral.default";
        case Tok::S_Content_Neutral_Hover: return "semantic.text.color.neutral.hover";
        case Tok::S_Content_Neutral_Pressed: return "semantic.text.color.neutral.pressed";
        case Tok::S_Content_Neutral_KbdFocus: return "semantic.text.color.neutral.kbd-focus";
        case Tok::S_Color_Text_Default: return "semantic.text.color.primary.default";
        case Tok::S_Color_Text_Subtle: return "semantic.text.color.secondary.default";
        case Tok::S_Text_Tertiary: return "semantic.text.color.tertiary.default";
        case Tok::S_Color_Text_Disabled: return "semantic.text.color.disabled.default";
        case Tok::S_Color_Text_Link: return "semantic.text.color.link.default";
        case Tok::S_Text_Placeholder: return "semantic.text.color.placeholder.default";
        case Tok::S_Text_Blue: return "semantic.text.color.blue.default";
        case Tok::S_Text_Magenta: return "semantic.text.color.magenta.default";
        case Tok::S_Text_Green: return "semantic.text.color.green.default";
        case Tok::S_Text_Red: return "semantic.text.color.red.default";
        case Tok::S_Text_Yellow: return "semantic.text.color.yellow.default";
        case Tok::S_Text_Purple: return "semantic.text.color.purple.default";
        case Tok::S_Text_Orange: return "semantic.text.color.orange.default";
        case Tok::S_Text_Cyan: return "semantic.text.color.cyan.default";
        case Tok::S_Color_Icon_Primary: return "semantic.icon.color.primary.default";
        case Tok::S_Color_Icon_Secondary: return "semantic.icon.color.secondary.default";
        case Tok::S_Color_Icon_Tertiary: return "semantic.icon.color.tertiary.default";
        case Tok::S_Icon_Disabled: return "semantic.icon.color.disabled.default";
        case Tok::S_Icon_Accent: return "semantic.icon.color.accent.default";
        case Tok::S_Icon_Negative: return "semantic.icon.color.negative.default";
        case Tok::S_Icon_Positive: return "semantic.icon.color.positive.default";
        case Tok::S_Icon_Notice: return "semantic.icon.color.notice.default";
        case Tok::S_Icon_Info: return "semantic.icon.color.info.default";
        case Tok::S_Icon_Blue: return "semantic.icon.color.blue.default";
        case Tok::S_Icon_Magenta: return "semantic.icon.color.magenta.default";
        case Tok::S_Icon_Green: return "semantic.icon.color.green.default";
        case Tok::S_Icon_Red: return "semantic.icon.color.red.default";
        case Tok::S_Icon_Yellow: return "semantic.icon.color.yellow.default";
        case Tok::S_Icon_Purple: return "semantic.icon.color.purple.default";
        case Tok::S_Icon_Orange: return "semantic.icon.color.orange.default";
        case Tok::S_Icon_Cyan: return "semantic.icon.color.cyan.default";
        case Tok::S_Color_Border_Default: return "semantic.border.color.default";
        case Tok::S_Border_Subtle: return "semantic.border.color.subtle.default";
        case Tok::S_Color_Border_Strong: return "semantic.border.color.strong.default";
        case Tok::S_Border_Disabled: return "semantic.border.color.disabled.default";
        case Tok::S_Border_Enabled: return "semantic.border.enabled.default";
        case Tok::S_Border_Negative_Default: return "semantic.border.color.negative.default";
        case Tok::S_Border_Negative_Hover: return "semantic.border.color.negative.hover";
        case Tok::S_Border_Negative_Pressed: return "semantic.border.color.negative.pressed";
        case Tok::S_Border_Negative_Focus: return "semantic.border.color.negative.focus";
        case Tok::S_Border_Negative_KbdFocus: return "semantic.border.color.negative.kbd-focus";
        case Tok::S_Border_Accent_Default: return "semantic.border.color.accent.default";
        case Tok::S_Border_Focus: return "semantic.border.color.focus.default";
        case Tok::S_Border_Blue: return "semantic.border.color.blue.default";
        case Tok::S_Border_Magenta: return "semantic.border.color.magenta.default";
        case Tok::S_Border_Green: return "semantic.border.color.green.default";
        case Tok::S_Border_Red: return "semantic.border.color.red.default";
        case Tok::S_Border_Yellow: return "semantic.border.color.yellow.default";
        case Tok::S_Border_Purple: return "semantic.border.color.purple.default";
        case Tok::S_Border_Orange: return "semantic.border.color.orange.default";
        case Tok::S_Border_Cyan: return "semantic.border.color.cyan.default";
        case Tok::S_Focus_Indicator: return "semantic.focus.color.indicator.default";
        case Tok::S_Accent_Visual: return "semantic.accent.color.visual.default";
        case Tok::S_Info_Visual: return "semantic.info.color.visual.default";
        case Tok::S_Negative_Visual: return "semantic.negative.color.visual.default";
        case Tok::S_Positive_Visual: return "semantic.positive.color.visual.default";
        case Tok::S_Notice_Visual: return "semantic.notice.color.visual.default";
        case Tok::S_Neutral_Visual: return "semantic.neutral.color.visual.default";
        case Tok::S_Disabled_Background: return "semantic.disabled.background.default";
        case Tok::S_Disabled_Border: return "semantic.disabled.border.default";
        case Tok::S_Disabled_Content: return "semantic.disabled.content.default";
        case Tok::S_Disabled_StaticBlackBackground: return "semantic.disabled.static-black.background.default";
        case Tok::S_Disabled_StaticBlackBorder: return "semantic.disabled.static-black.border.default";
        case Tok::S_Disabled_StaticBlackContent: return "semantic.disabled.static-black.content.default";
        case Tok::S_Disabled_StaticWhiteBackground: return "semantic.disabled.static-white.background.default";
        case Tok::S_Disabled_StaticWhiteBorder: return "semantic.disabled.static-white.border.default";
        case Tok::S_Disabled_StaticWhiteContent: return "semantic.disabled.static-white.content.default";
        case Tok::S_Static_BlackText: return "semantic.static.text.color.black";
        case Tok::S_Static_WhiteText: return "semantic.static.text.color.white";
        case Tok::S_Static_BlackTrack: return "semantic.static.track.color.black";
        case Tok::S_Static_WhiteTrack: return "semantic.static.track.color.white";
        case Tok::S_Static_BlackTrackIndicator: return "semantic.static.track-indicator.color.black";
        case Tok::S_Static_WhiteTrackIndicator: return "semantic.static.track-indicator.color.white";
        case Tok::S_Static_BlackFocus: return "semantic.static.focus.color.black";
        case Tok::S_Static_WhiteFocus: return "semantic.static.focus.color.white";
        case Tok::S_Track_Color: return "semantic.track.color.default";
        case Tok::S_Title_Color: return "semantic.title.color.default";
        case Tok::S_Color_Background_TextSelection: return "semantic.background.text-selection.default";
        case Tok::S_Shadow_100_Color: return "semantic.shadow.100.color";
        case Tok::S_Shadow_200_Color: return "semantic.shadow.200.color";
        case Tok::S_Shadow_300_Color: return "semantic.shadow.300.color";
        case Tok::S_Shadow_400_Color: return "semantic.shadow.400.color";
        case Tok::S_Shadow_500_Color: return "semantic.shadow.500.color";
        case Tok::S_Shadow_600_Color: return "semantic.shadow.600.color";
        case Tok::S_Shadow_Xs: return "semantic.shadow.xs";
        case Tok::S_Shadow_S: return "semantic.shadow.s";
        case Tok::S_Shadow_M: return "semantic.shadow.m";
        case Tok::S_Shadow_L: return "semantic.shadow.l";
        case Tok::S_Shadow_Xl: return "semantic.shadow.xl";
        case Tok::S_Size_Xxs: return "semantic.size.xxs";
        case Tok::S_Size_Xs: return "semantic.size.xs";
        case Tok::S_Size_S: return "semantic.size.s";
        case Tok::S_Size_M: return "semantic.size.m";
        case Tok::S_Size_L: return "semantic.size.l";
        case Tok::S_Size_Xl: return "semantic.size.xl";
        case Tok::S_Size_2xl: return "semantic.size.2xl";
        case Tok::S_Size_3xl: return "semantic.size.3xl";
        case Tok::S_CornerRadius_None: return "semantic.radius.none";
        case Tok::S_Radius_Xs: return "semantic.radius.xs";
        case Tok::S_CornerRadius_Small: return "semantic.radius.s";
        case Tok::S_CornerRadius_Default: return "semantic.radius.m";
        case Tok::S_Radius_L: return "semantic.radius.l";
        case Tok::S_Radius_Xl: return "semantic.radius.xl";
        case Tok::S_Radius_Full: return "semantic.radius.full";
        case Tok::S_BorderWidth_Thin: return "semantic.border-width.thin";
        case Tok::S_BorderWidth_Medium: return "semantic.border-width.medium";
        case Tok::S_BorderWidth_Thick: return "semantic.border-width.thick";
        case Tok::S_BorderWidth_Focus: return "semantic.border-width.focus";
        case Tok::S_BorderWidth_None: return "semantic.border-width.none";
        case Tok::S_Opacity_Ghost: return "semantic.opacity.ghost";
        case Tok::S_Opacity_Faint: return "semantic.opacity.faint";
        case Tok::S_Opacity_Reduced: return "semantic.opacity.reduced";
        case Tok::S_Opacity_Moderate: return "semantic.opacity.moderate";
        case Tok::S_Opacity_Strong: return "semantic.opacity.strong";
        case Tok::S_Opacity_Full: return "semantic.opacity.full";
        case Tok::S_Opacity_Transparent: return "semantic.opacity.transparent";
        case Tok::S_Opacity_Default: return "semantic.opacity.default";
        case Tok::S_Opacity_Disabled: return "semantic.opacity.disabled";
        case Tok::S_Opacity_DimLight: return "semantic.opacity.dim.light";
        case Tok::S_Opacity_DimHeavy: return "semantic.opacity.dim.heavy";
        case Tok::S_Opacity_ContentDisabled: return "semantic.opacity.content.disabled";
        case Tok::S_FontSize_Default: return "semantic.font-size.default";
        case Tok::S_FontScale_Default: return "semantic.font-scale.default";
        case Tok::S_Scale_Default: return "semantic.scale.default";
        case Tok::S_FontSize_DisplayL: return "semantic.font.size.display.l";
        case Tok::S_FontSize_DisplayM: return "semantic.font.size.display.m";
        case Tok::S_FontSize_DisplayS: return "semantic.font.size.display.s";
        case Tok::S_FontSize_HeadingXl: return "semantic.font.size.heading.xl";
        case Tok::S_FontSize_HeadingL: return "semantic.font.size.heading.l";
        case Tok::S_FontSize_HeadingM: return "semantic.font.size.heading.m";
        case Tok::S_FontSize_HeadingS: return "semantic.font.size.heading.s";
        case Tok::S_FontSize_HeadingXs: return "semantic.font.size.heading.xs";
        case Tok::S_FontSize_BodyL: return "semantic.font.size.body.l";
        case Tok::S_FontSize_BodyM: return "semantic.font.size.body.m";
        case Tok::S_FontSize_BodyS: return "semantic.font.size.body.s";
        case Tok::S_FontSize_DetailL: return "semantic.font.size.detail.l";
        case Tok::S_FontSize_DetailM: return "semantic.font.size.detail.m";
        case Tok::S_FontSize_DetailS: return "semantic.font.size.detail.s";
        case Tok::S_FontSize_LabelL: return "semantic.font.size.label.l";
        case Tok::S_FontSize_LabelM: return "semantic.font.size.label.m";
        case Tok::S_FontSize_LabelS: return "semantic.font.size.label.s";
        case Tok::S_FontSize_MonoL: return "semantic.font.size.mono.l";
        case Tok::S_FontSize_MonoM: return "semantic.font.size.mono.m";
        case Tok::S_FontSize_MonoS: return "semantic.font.size.mono.s";
        case Tok::S_FontSize_CodeM: return "semantic.font.size.code.m";
        case Tok::S_FontWeight_DisplayL: return "semantic.font.weight.display.l";
        case Tok::S_FontWeight_HeadingXl: return "semantic.font.weight.heading.xl";
        case Tok::S_FontWeight_HeadingL: return "semantic.font.weight.heading.l";
        case Tok::S_FontWeight_HeadingM: return "semantic.font.weight.heading.m";
        case Tok::S_FontWeight_HeadingS: return "semantic.font.weight.heading.s";
        case Tok::S_FontWeight_BodyM: return "semantic.font.weight.body.m";
        case Tok::S_FontWeight_LabelM: return "semantic.font.weight.label.m";
        case Tok::S_FontWeight_MonoM: return "semantic.font.weight.mono.m";
        case Tok::S_LineHeight_DisplayL: return "semantic.font.line-height.display.l";
        case Tok::S_LineHeight_HeadingXl: return "semantic.font.line-height.heading.xl";
        case Tok::S_LineHeight_HeadingM: return "semantic.font.line-height.heading.m";
        case Tok::S_LineHeight_BodyM: return "semantic.font.line-height.body.m";
        case Tok::S_LineHeight_DetailS: return "semantic.font.line-height.detail.s";
        case Tok::S_LineHeight_LabelM: return "semantic.font.line-height.label.m";
        case Tok::S_LineHeightCjk_HeadingM: return "semantic.font.line-height.cjk.heading.m";
        case Tok::S_LineHeightCjk_BodyM: return "semantic.font.line-height.cjk.body.m";
        case Tok::S_Tracking_HeadingXl: return "semantic.font.tracking.heading.xl";
        case Tok::S_Tracking_BodyM: return "semantic.font.tracking.body.m";
        case Tok::S_Tracking_LabelM: return "semantic.font.tracking.label.m";
        case Tok::S_FontFamily_Heading: return "semantic.font.family.heading";
        case Tok::S_FontFamily_Body: return "semantic.font.family.body";
        case Tok::S_FontFamily_Mono: return "semantic.font.family.mono";
        case Tok::S_FontFamily_Cjk: return "semantic.font.family.cjk";
        case Tok::S_TextMarginTop_HeadingXl: return "semantic.text.margin.top.heading.xl";
        case Tok::S_TextMarginTop_HeadingL: return "semantic.text.margin.top.heading.l";
        case Tok::S_TextMarginTop_HeadingM: return "semantic.text.margin.top.heading.m";
        case Tok::S_TextMarginTop_BodyM: return "semantic.text.margin.top.body.m";
        case Tok::S_TextMarginBottom_HeadingXl: return "semantic.text.margin.bottom.heading.xl";
        case Tok::S_TextMarginBottom_HeadingM: return "semantic.text.margin.bottom.heading.m";
        case Tok::S_TextMarginBottom_BodyM: return "semantic.text.margin.bottom.body.m";
        case Tok::S_Text_DisplayL: return "semantic.text.display.l";
        case Tok::S_Text_DisplayM: return "semantic.text.display.m";
        case Tok::S_Text_DisplayS: return "semantic.text.display.s";
        case Tok::S_Text_HeadingXl: return "semantic.text.heading.xl";
        case Tok::S_Text_HeadingL: return "semantic.text.heading.l";
        case Tok::S_Text_HeadingM: return "semantic.text.heading.m";
        case Tok::S_Text_HeadingS: return "semantic.text.heading.s";
        case Tok::S_Text_HeadingXs: return "semantic.text.heading.xs";
        case Tok::S_Text_BodyL: return "semantic.text.body.l";
        case Tok::S_Text_BodyM: return "semantic.text.body.m";
        case Tok::S_Text_BodyS: return "semantic.text.body.s";
        case Tok::S_Text_DetailL: return "semantic.text.detail.l";
        case Tok::S_Text_DetailM: return "semantic.text.detail.m";
        case Tok::S_Text_DetailS: return "semantic.text.detail.s";
        case Tok::S_Text_LabelL: return "semantic.text.label.l";
        case Tok::S_Text_LabelM: return "semantic.text.label.m";
        case Tok::S_Text_LabelS: return "semantic.text.label.s";
        case Tok::S_Text_MonoL: return "semantic.text.mono.l";
        case Tok::S_Text_MonoM: return "semantic.text.mono.m";
        case Tok::S_Text_MonoS: return "semantic.text.mono.s";
        case Tok::S_Text_CodeM: return "semantic.text.code.m";
        case Tok::S_AnimDuration_Enter: return "semantic.animation.duration.enter";
        case Tok::S_AnimDuration_Exit: return "semantic.animation.duration.exit";
        case Tok::S_AnimDuration_Expand: return "semantic.animation.duration.expand";
        case Tok::S_AnimDuration_Collapse: return "semantic.animation.duration.collapse";
        case Tok::S_AnimDuration_Page: return "semantic.animation.duration.page";
        case Tok::S_AnimDuration_Pulse: return "semantic.animation.duration.pulse";
        case Tok::S_AnimEasing_Enter: return "semantic.animation.easing.enter";
        case Tok::S_AnimEasing_Exit: return "semantic.animation.easing.exit";
        case Tok::S_AnimEasing_Interact: return "semantic.animation.easing.interact";
        case Tok::S_AnimEasing_Move: return "semantic.animation.easing.move";
        case Tok::S_GradientAngle_Upward: return "semantic.gradient.upward.angle";
        case Tok::S_GradientAngle_Downward: return "semantic.gradient.downward.angle";
        case Tok::S_Layer_Dropdown: return "semantic.layer.dropdown";
        case Tok::S_Layer_Sticky: return "semantic.layer.sticky";
        case Tok::S_Layer_Overlay: return "semantic.layer.overlay";
        case Tok::S_Layer_Modal: return "semantic.layer.modal";
        case Tok::S_Layer_Popover: return "semantic.layer.popover";
        case Tok::S_Layer_Toast: return "semantic.layer.toast";
        case Tok::S_Layer_Tooltip: return "semantic.layer.tooltip";
        case Tok::S_Config_ItemSpacing: return "semantic.config.item-spacing";
        case Tok::S_Config_ItemInnerSpacing: return "semantic.config.item-inner-spacing";
        case Tok::S_Config_CellPadding: return "semantic.config.cell-padding";
        case Tok::S_Config_TouchExtraPadding: return "semantic.config.touch-extra-padding";
        case Tok::S_Config_IndentSpacing: return "semantic.config.indent-spacing";
        case Tok::S_Config_ColumnsMinSpacing: return "semantic.config.columns-min-spacing";
        case Tok::S_Config_UndoSteps: return "semantic.config.undo-steps";
        case Tok::S_Config_DisplayWindowPadding: return "semantic.config.display-window-padding";
        case Tok::S_Config_DisplaySafeAreaPadding: return "semantic.config.display-safe-area-padding";
        case Tok::S_Config_SeparatorTextPadding: return "semantic.config.separator-text-padding";
        case Tok::S_Config_ButtonTextAlign: return "semantic.config.button-text-align";
        case Tok::S_Config_SelectableTextAlign: return "semantic.config.selectable-text-align";
        case Tok::S_Config_SeparatorTextAlign: return "semantic.config.separator-text-align";
        case Tok::S_Config_ColorButtonPosition: return "semantic.config.color-button-position";
        case Tok::S_Config_GrabMinSize: return "semantic.config.grab-min-size";
        case Tok::S_Config_LogSliderDeadzone: return "semantic.config.log-slider-deadzone";
        case Tok::S_Config_ColorMarkerSize: return "semantic.config.color-marker-size";
        case Tok::S_Config_MouseCursorScale: return "semantic.config.mouse-cursor-scale";
        case Tok::S_Config_HoverDelayStationary: return "semantic.config.hover-delay-stationary";
        case Tok::S_Config_HoverDelayShort: return "semantic.config.hover-delay-short";
        case Tok::S_Config_HoverDelayNormal: return "semantic.config.hover-delay-normal";
        case Tok::S_Config_DragThreshold: return "semantic.config.drag-threshold";
        case Tok::S_Config_AntiAliasedLines: return "semantic.config.anti-aliased-lines";
        case Tok::S_Config_AntiAliasedLinesUseTex: return "semantic.config.anti-aliased-lines-use-tex";
        case Tok::S_Config_AntiAliasedFill: return "semantic.config.anti-aliased-fill";
        case Tok::S_Config_CurveTessellationTol: return "semantic.config.curve-tessellation-tol";
        case Tok::S_Config_CircleTessellationMaxError: return "semantic.config.circle-tessellation-max-error";
        case Tok::S_Config_TreeLinesFlags: return "semantic.config.tree-lines-flags";
        case Tok::S_Config_TreeLinesSize: return "semantic.config.tree-lines-size";
        case Tok::S_DataViz_Cat_1: return "semantic.data-viz.categorical.short.1";
        case Tok::S_DataViz_Cat_2: return "semantic.data-viz.categorical.short.2";
        case Tok::S_DataViz_Cat_3: return "semantic.data-viz.categorical.short.3";
        case Tok::S_DataViz_Cat_4: return "semantic.data-viz.categorical.short.4";
        case Tok::S_DataViz_Cat_5: return "semantic.data-viz.categorical.short.5";
        case Tok::S_DataViz_Cat_6: return "semantic.data-viz.categorical.short.6";
        case Tok::S_DataViz_Cat_7: return "semantic.data-viz.categorical.extended.7";
        case Tok::S_DataViz_Cat_8: return "semantic.data-viz.categorical.extended.8";
        case Tok::S_DataViz_Cat_9: return "semantic.data-viz.categorical.extended.9";
        case Tok::S_DataViz_Cat_10: return "semantic.data-viz.categorical.extended.10";
        case Tok::S_DataViz_Cat_11: return "semantic.data-viz.categorical.extended.11";
        case Tok::S_DataViz_Cat_12: return "semantic.data-viz.categorical.extended.12";
        case Tok::S_DataViz_Ord_1: return "semantic.data-viz.ordinal.1";
        case Tok::S_DataViz_Ord_2: return "semantic.data-viz.ordinal.2";
        case Tok::S_DataViz_Ord_3: return "semantic.data-viz.ordinal.3";
        case Tok::S_DataViz_Ord_4: return "semantic.data-viz.ordinal.4";
        case Tok::S_DataViz_Ord_5: return "semantic.data-viz.ordinal.5";
        case Tok::S_DataViz_Ord_6: return "semantic.data-viz.ordinal.6";
        case Tok::S_DataViz_Ord_7: return "semantic.data-viz.ordinal.7";
        case Tok::S_DataViz_Ord_8: return "semantic.data-viz.ordinal.8";
        case Tok::S_DataViz_Ord_9: return "semantic.data-viz.ordinal.9";
        case Tok::S_DataViz_Seq_Viridis_100: return "semantic.data-viz.sequential.viridis.100";
        case Tok::S_DataViz_Seq_Viridis_200: return "semantic.data-viz.sequential.viridis.200";
        case Tok::S_DataViz_Seq_Viridis_300: return "semantic.data-viz.sequential.viridis.300";
        case Tok::S_DataViz_Seq_Viridis_400: return "semantic.data-viz.sequential.viridis.400";
        case Tok::S_DataViz_Seq_Viridis_500: return "semantic.data-viz.sequential.viridis.500";
        case Tok::S_DataViz_Seq_Viridis_600: return "semantic.data-viz.sequential.viridis.600";
        case Tok::S_DataViz_Seq_Viridis_700: return "semantic.data-viz.sequential.viridis.700";
        case Tok::S_DataViz_Seq_Viridis_800: return "semantic.data-viz.sequential.viridis.800";
        case Tok::S_DataViz_Seq_Viridis_900: return "semantic.data-viz.sequential.viridis.900";
        case Tok::S_DataViz_Seq_Viridis_1000: return "semantic.data-viz.sequential.viridis.1000";
        case Tok::S_DataViz_Seq_Viridis_1100: return "semantic.data-viz.sequential.viridis.1100";
        case Tok::S_DataViz_Seq_Viridis_1200: return "semantic.data-viz.sequential.viridis.1200";
        case Tok::S_DataViz_Seq_Viridis_1300: return "semantic.data-viz.sequential.viridis.1300";
        case Tok::S_DataViz_Seq_Viridis_1400: return "semantic.data-viz.sequential.viridis.1400";
        case Tok::S_DataViz_Seq_Viridis_1500: return "semantic.data-viz.sequential.viridis.1500";
        case Tok::S_DataViz_Seq_Viridis_1600: return "semantic.data-viz.sequential.viridis.1600";
        case Tok::S_DataViz_Seq_Magma_100: return "semantic.data-viz.sequential.magma.100";
        case Tok::S_DataViz_Seq_Magma_200: return "semantic.data-viz.sequential.magma.200";
        case Tok::S_DataViz_Seq_Magma_300: return "semantic.data-viz.sequential.magma.300";
        case Tok::S_DataViz_Seq_Magma_400: return "semantic.data-viz.sequential.magma.400";
        case Tok::S_DataViz_Seq_Magma_500: return "semantic.data-viz.sequential.magma.500";
        case Tok::S_DataViz_Seq_Magma_600: return "semantic.data-viz.sequential.magma.600";
        case Tok::S_DataViz_Seq_Magma_700: return "semantic.data-viz.sequential.magma.700";
        case Tok::S_DataViz_Seq_Magma_800: return "semantic.data-viz.sequential.magma.800";
        case Tok::S_DataViz_Seq_Magma_900: return "semantic.data-viz.sequential.magma.900";
        case Tok::S_DataViz_Seq_Magma_1000: return "semantic.data-viz.sequential.magma.1000";
        case Tok::S_DataViz_Seq_Magma_1100: return "semantic.data-viz.sequential.magma.1100";
        case Tok::S_DataViz_Seq_Magma_1200: return "semantic.data-viz.sequential.magma.1200";
        case Tok::S_DataViz_Seq_Magma_1300: return "semantic.data-viz.sequential.magma.1300";
        case Tok::S_DataViz_Seq_Magma_1400: return "semantic.data-viz.sequential.magma.1400";
        case Tok::S_DataViz_Seq_Magma_1500: return "semantic.data-viz.sequential.magma.1500";
        case Tok::S_DataViz_Seq_Magma_1600: return "semantic.data-viz.sequential.magma.1600";
        case Tok::S_DataViz_Seq_Plasma_100: return "semantic.data-viz.sequential.plasma.100";
        case Tok::S_DataViz_Seq_Plasma_200: return "semantic.data-viz.sequential.plasma.200";
        case Tok::S_DataViz_Seq_Plasma_300: return "semantic.data-viz.sequential.plasma.300";
        case Tok::S_DataViz_Seq_Plasma_400: return "semantic.data-viz.sequential.plasma.400";
        case Tok::S_DataViz_Seq_Plasma_500: return "semantic.data-viz.sequential.plasma.500";
        case Tok::S_DataViz_Seq_Plasma_600: return "semantic.data-viz.sequential.plasma.600";
        case Tok::S_DataViz_Seq_Plasma_700: return "semantic.data-viz.sequential.plasma.700";
        case Tok::S_DataViz_Seq_Plasma_800: return "semantic.data-viz.sequential.plasma.800";
        case Tok::S_DataViz_Seq_Plasma_900: return "semantic.data-viz.sequential.plasma.900";
        case Tok::S_DataViz_Seq_Plasma_1000: return "semantic.data-viz.sequential.plasma.1000";
        case Tok::S_DataViz_Seq_Plasma_1100: return "semantic.data-viz.sequential.plasma.1100";
        case Tok::S_DataViz_Seq_Plasma_1200: return "semantic.data-viz.sequential.plasma.1200";
        case Tok::S_DataViz_Seq_Plasma_1300: return "semantic.data-viz.sequential.plasma.1300";
        case Tok::S_DataViz_Seq_Plasma_1400: return "semantic.data-viz.sequential.plasma.1400";
        case Tok::S_DataViz_Seq_Plasma_1500: return "semantic.data-viz.sequential.plasma.1500";
        case Tok::S_DataViz_Seq_Plasma_1600: return "semantic.data-viz.sequential.plasma.1600";
        case Tok::S_DataViz_Seq_Inferno_100: return "semantic.data-viz.sequential.inferno.100";
        case Tok::S_DataViz_Seq_Inferno_200: return "semantic.data-viz.sequential.inferno.200";
        case Tok::S_DataViz_Seq_Inferno_300: return "semantic.data-viz.sequential.inferno.300";
        case Tok::S_DataViz_Seq_Inferno_400: return "semantic.data-viz.sequential.inferno.400";
        case Tok::S_DataViz_Seq_Inferno_500: return "semantic.data-viz.sequential.inferno.500";
        case Tok::S_DataViz_Seq_Inferno_600: return "semantic.data-viz.sequential.inferno.600";
        case Tok::S_DataViz_Seq_Inferno_700: return "semantic.data-viz.sequential.inferno.700";
        case Tok::S_DataViz_Seq_Inferno_800: return "semantic.data-viz.sequential.inferno.800";
        case Tok::S_DataViz_Seq_Inferno_900: return "semantic.data-viz.sequential.inferno.900";
        case Tok::S_DataViz_Seq_Inferno_1000: return "semantic.data-viz.sequential.inferno.1000";
        case Tok::S_DataViz_Seq_Inferno_1100: return "semantic.data-viz.sequential.inferno.1100";
        case Tok::S_DataViz_Seq_Inferno_1200: return "semantic.data-viz.sequential.inferno.1200";
        case Tok::S_DataViz_Seq_Inferno_1300: return "semantic.data-viz.sequential.inferno.1300";
        case Tok::S_DataViz_Seq_Inferno_1400: return "semantic.data-viz.sequential.inferno.1400";
        case Tok::S_DataViz_Seq_Inferno_1500: return "semantic.data-viz.sequential.inferno.1500";
        case Tok::S_DataViz_Seq_Inferno_1600: return "semantic.data-viz.sequential.inferno.1600";
        case Tok::S_DataViz_Seq_Cividis_100: return "semantic.data-viz.sequential.cividis.100";
        case Tok::S_DataViz_Seq_Cividis_200: return "semantic.data-viz.sequential.cividis.200";
        case Tok::S_DataViz_Seq_Cividis_300: return "semantic.data-viz.sequential.cividis.300";
        case Tok::S_DataViz_Seq_Cividis_400: return "semantic.data-viz.sequential.cividis.400";
        case Tok::S_DataViz_Seq_Cividis_500: return "semantic.data-viz.sequential.cividis.500";
        case Tok::S_DataViz_Seq_Cividis_600: return "semantic.data-viz.sequential.cividis.600";
        case Tok::S_DataViz_Seq_Cividis_700: return "semantic.data-viz.sequential.cividis.700";
        case Tok::S_DataViz_Seq_Cividis_800: return "semantic.data-viz.sequential.cividis.800";
        case Tok::S_DataViz_Seq_Cividis_900: return "semantic.data-viz.sequential.cividis.900";
        case Tok::S_DataViz_Seq_Cividis_1000: return "semantic.data-viz.sequential.cividis.1000";
        case Tok::S_DataViz_Seq_Cividis_1100: return "semantic.data-viz.sequential.cividis.1100";
        case Tok::S_DataViz_Seq_Cividis_1200: return "semantic.data-viz.sequential.cividis.1200";
        case Tok::S_DataViz_Seq_Cividis_1300: return "semantic.data-viz.sequential.cividis.1300";
        case Tok::S_DataViz_Seq_Cividis_1400: return "semantic.data-viz.sequential.cividis.1400";
        case Tok::S_DataViz_Seq_Cividis_1500: return "semantic.data-viz.sequential.cividis.1500";
        case Tok::S_DataViz_Seq_Cividis_1600: return "semantic.data-viz.sequential.cividis.1600";
        case Tok::S_DataViz_Div_RdBu_100: return "semantic.data-viz.diverging.rd-bu.100";
        case Tok::S_DataViz_Div_RdBu_200: return "semantic.data-viz.diverging.rd-bu.200";
        case Tok::S_DataViz_Div_RdBu_300: return "semantic.data-viz.diverging.rd-bu.300";
        case Tok::S_DataViz_Div_RdBu_400: return "semantic.data-viz.diverging.rd-bu.400";
        case Tok::S_DataViz_Div_RdBu_500: return "semantic.data-viz.diverging.rd-bu.500";
        case Tok::S_DataViz_Div_RdBu_600: return "semantic.data-viz.diverging.rd-bu.600";
        case Tok::S_DataViz_Div_RdBu_700: return "semantic.data-viz.diverging.rd-bu.700";
        case Tok::S_DataViz_Div_RdBu_800: return "semantic.data-viz.diverging.rd-bu.800";
        case Tok::S_DataViz_Div_RdBu_900: return "semantic.data-viz.diverging.rd-bu.900";
        case Tok::S_DataViz_Div_RdBu_1000: return "semantic.data-viz.diverging.rd-bu.1000";
        case Tok::S_DataViz_Div_RdBu_1100: return "semantic.data-viz.diverging.rd-bu.1100";
        case Tok::S_DataViz_Div_RdBu_1200: return "semantic.data-viz.diverging.rd-bu.1200";
        case Tok::S_DataViz_Div_RdBu_1300: return "semantic.data-viz.diverging.rd-bu.1300";
        case Tok::S_DataViz_Div_RdBu_1400: return "semantic.data-viz.diverging.rd-bu.1400";
        case Tok::S_DataViz_Div_RdBu_1500: return "semantic.data-viz.diverging.rd-bu.1500";
        case Tok::S_DataViz_Div_RdBu_1600: return "semantic.data-viz.diverging.rd-bu.1600";
        case Tok::S_DataViz_Div_PuGn_100: return "semantic.data-viz.diverging.pu-gn.100";
        case Tok::S_DataViz_Div_PuGn_200: return "semantic.data-viz.diverging.pu-gn.200";
        case Tok::S_DataViz_Div_PuGn_300: return "semantic.data-viz.diverging.pu-gn.300";
        case Tok::S_DataViz_Div_PuGn_400: return "semantic.data-viz.diverging.pu-gn.400";
        case Tok::S_DataViz_Div_PuGn_500: return "semantic.data-viz.diverging.pu-gn.500";
        case Tok::S_DataViz_Div_PuGn_600: return "semantic.data-viz.diverging.pu-gn.600";
        case Tok::S_DataViz_Div_PuGn_700: return "semantic.data-viz.diverging.pu-gn.700";
        case Tok::S_DataViz_Div_PuGn_800: return "semantic.data-viz.diverging.pu-gn.800";
        case Tok::S_DataViz_Div_PuGn_900: return "semantic.data-viz.diverging.pu-gn.900";
        case Tok::S_DataViz_Div_PuGn_1000: return "semantic.data-viz.diverging.pu-gn.1000";
        case Tok::S_DataViz_Div_PuGn_1100: return "semantic.data-viz.diverging.pu-gn.1100";
        case Tok::S_DataViz_Div_PuGn_1200: return "semantic.data-viz.diverging.pu-gn.1200";
        case Tok::S_DataViz_Div_PuGn_1300: return "semantic.data-viz.diverging.pu-gn.1300";
        case Tok::S_DataViz_Div_PuGn_1400: return "semantic.data-viz.diverging.pu-gn.1400";
        case Tok::S_DataViz_Div_PuGn_1500: return "semantic.data-viz.diverging.pu-gn.1500";
        case Tok::S_DataViz_Div_PuGn_1600: return "semantic.data-viz.diverging.pu-gn.1600";
        case Tok::S_DataViz_Div_BrTeal_100: return "semantic.data-viz.diverging.br-teal.100";
        case Tok::S_DataViz_Div_BrTeal_200: return "semantic.data-viz.diverging.br-teal.200";
        case Tok::S_DataViz_Div_BrTeal_300: return "semantic.data-viz.diverging.br-teal.300";
        case Tok::S_DataViz_Div_BrTeal_400: return "semantic.data-viz.diverging.br-teal.400";
        case Tok::S_DataViz_Div_BrTeal_500: return "semantic.data-viz.diverging.br-teal.500";
        case Tok::S_DataViz_Div_BrTeal_600: return "semantic.data-viz.diverging.br-teal.600";
        case Tok::S_DataViz_Div_BrTeal_700: return "semantic.data-viz.diverging.br-teal.700";
        case Tok::S_DataViz_Div_BrTeal_800: return "semantic.data-viz.diverging.br-teal.800";
        case Tok::S_DataViz_Div_BrTeal_900: return "semantic.data-viz.diverging.br-teal.900";
        case Tok::S_DataViz_Div_BrTeal_1000: return "semantic.data-viz.diverging.br-teal.1000";
        case Tok::S_DataViz_Div_BrTeal_1100: return "semantic.data-viz.diverging.br-teal.1100";
        case Tok::S_DataViz_Div_BrTeal_1200: return "semantic.data-viz.diverging.br-teal.1200";
        case Tok::S_DataViz_Div_BrTeal_1300: return "semantic.data-viz.diverging.br-teal.1300";
        case Tok::S_DataViz_Div_BrTeal_1400: return "semantic.data-viz.diverging.br-teal.1400";
        case Tok::S_DataViz_Div_BrTeal_1500: return "semantic.data-viz.diverging.br-teal.1500";
        case Tok::S_DataViz_Div_BrTeal_1600: return "semantic.data-viz.diverging.br-teal.1600";
        case Tok::S_DataViz_Axis: return "semantic.data-viz.axis.default";
        case Tok::S_DataViz_Grid: return "semantic.data-viz.grid.default";
        case Tok::S_DataViz_Label: return "semantic.data-viz.label.default";
        case Tok::S_DataViz_Highlight: return "semantic.data-viz.highlight.default";
        case Tok::C_Window_Background: return "component.window.background.color.default";
        case Tok::C_Window_CornerRadius: return "component.window.corner-radius.default";
        case Tok::C_Window_BorderWidth: return "component.window.border.width.default";
        case Tok::C_Window_BorderColor: return "component.window.border.color.default";
        case Tok::C_Window_BorderHoverPadding: return "component.window.border.hover-padding";
        case Tok::C_Window_Padding: return "component.window.padding.default";
        case Tok::C_Window_MinSize: return "component.window.size.min";
        case Tok::C_Window_TitleAlign: return "component.window.title.text-align";
        case Tok::C_Window_MenuButtonPosition: return "component.window.menu-button.position";
        case Tok::C_Window_ShadowColor: return "component.window.shadow.color.default";
        case Tok::C_Window_ShadowSize: return "component.window.shadow.size.default";
        case Tok::C_Child_Background: return "component.child.background.color.default";
        case Tok::C_Child_BackgroundOpacity: return "component.child.background.opacity.default";
        case Tok::C_Child_CornerRadius: return "component.child.corner-radius.default";
        case Tok::C_Child_BorderWidth: return "component.child.border.width.default";
        case Tok::C_Popup_Background: return "component.popup.background.color.default";
        case Tok::C_Popup_CornerRadius: return "component.popup.corner-radius.default";
        case Tok::C_Popup_BorderWidth: return "component.popup.border.width.default";
        case Tok::C_Popup_MenuBarBackground: return "component.popup.menu-bar.background.color.default";
        case Tok::C_Frame_Background: return "component.frame.background.color.default";
        case Tok::C_Frame_BackgroundHover: return "component.frame.background.color.hover";
        case Tok::C_Frame_BackgroundDown: return "component.frame.background.color.down";
        case Tok::C_Frame_CornerRadius: return "component.frame.corner-radius.default";
        case Tok::C_Frame_BorderWidth: return "component.frame.border.width.default";
        case Tok::C_Frame_Padding: return "component.frame.padding.default";
        case Tok::C_Frame_InputTextCursor: return "component.frame.input-text.cursor.color.default";
        case Tok::C_DragValue_Background: return "component.drag-value.background.color.default";
        case Tok::C_DragValue_BackgroundHover: return "component.drag-value.background.color.hover";
        case Tok::C_DragValue_BackgroundPressed: return "component.drag-value.background.color.pressed";
        case Tok::C_DragValue_BackgroundDrag: return "component.drag-value.background.color.drag";
        case Tok::C_DragValue_Text: return "component.drag-value.text.color.default";
        case Tok::C_DragValue_Unit: return "component.drag-value.unit.color.default";
        case Tok::C_DragValue_StepButton: return "component.drag-value.step-button.color.default";
        case Tok::C_DragValue_StepButtonHover: return "component.drag-value.step-button.background.color.hover";
        case Tok::C_DragValue_Border: return "component.drag-value.border.color.default";
        case Tok::C_DragValue_CornerRadius: return "component.drag-value.corner-radius.default";
        case Tok::C_DragValue_BorderWidth: return "component.drag-value.border.width.default";
        case Tok::C_Button_Background: return "component.button.background.color.default";
        case Tok::C_Button_BackgroundHover: return "component.button.background.color.hover";
        case Tok::C_Button_BackgroundDown: return "component.button.background.color.down";
        case Tok::C_Button_Label: return "component.button.label.color.default";
        case Tok::C_Combo_Background: return "component.combo.background.color.default";
        case Tok::C_Combo_BackgroundHover: return "component.combo.background.color.hover";
        case Tok::C_Combo_BackgroundDown: return "component.combo.background.color.down";
        case Tok::C_Combo_PreviewText: return "component.combo.preview.label.color.default";
        case Tok::C_Combo_ArrowIcon: return "component.combo.arrow.icon.color.default";
        case Tok::C_Combo_PopupBackground: return "component.combo.popup.background.color.default";
        case Tok::C_Combo_ItemBackgroundHover: return "component.combo.item.background.color.hover";
        case Tok::C_Combo_ItemBackgroundSelected: return "component.combo.item.background.color.selected";
        case Tok::C_Combo_Border: return "component.combo.border.color.default";
        case Tok::C_Combo_CornerRadius: return "component.combo.corner-radius.default";
        case Tok::C_Combo_BorderWidth: return "component.combo.border.width.default";
        case Tok::C_Combo_Padding: return "component.combo.padding.default";
        case Tok::C_Tab_Background: return "component.tab.background.color.default";
        case Tok::C_Tab_BackgroundHover: return "component.tab.background.color.hover";
        case Tok::C_Tab_BackgroundSelected: return "component.tab.background.color.selected";
        case Tok::C_Tab_OverlineSelected: return "component.tab.overline.color.selected";
        case Tok::C_Tab_BackgroundDimmed: return "component.tab.background.color.dimmed";
        case Tok::C_Tab_BackgroundDimmedSelected: return "component.tab.background.color.dimmed-selected";
        case Tok::C_Tab_OverlineDimmed: return "component.tab.overline.color.dimmed";
        case Tok::C_Tab_CornerRadius: return "component.tab.corner-radius.default";
        case Tok::C_Tab_BorderWidth: return "component.tab.border.width.default";
        case Tok::C_Tab_BarBorderWidth: return "component.tab.bar.border.width.default";
        case Tok::C_Tab_BarOverlineWidth: return "component.tab.bar.overline.width.default";
        case Tok::C_Tab_MinWidthBase: return "component.tab.size.min-width-base";
        case Tok::C_Tab_MinWidthShrink: return "component.tab.size.min-width-shrink";
        case Tok::C_Tab_CloseButtonMinWidthSelected: return "component.tab.close-button.min-width-selected";
        case Tok::C_Tab_CloseButtonMinWidthUnselected: return "component.tab.close-button.min-width-unselected";
        case Tok::C_Tab_UnsavedMarker: return "component.tab.unsaved-marker.color.default";
        case Tok::C_Header_Background: return "component.header.background.color.default";
        case Tok::C_Header_BackgroundHover: return "component.header.background.color.hover";
        case Tok::C_Header_BackgroundDown: return "component.header.background.color.down";
        case Tok::C_Scrollbar_Background: return "component.scrollbar.background.color.default";
        case Tok::C_Scrollbar_Grab: return "component.scrollbar.grab.color.default";
        case Tok::C_Scrollbar_GrabHover: return "component.scrollbar.grab.color.hover";
        case Tok::C_Scrollbar_GrabDown: return "component.scrollbar.grab.color.down";
        case Tok::C_Scrollbar_Size: return "component.scrollbar.size.default";
        case Tok::C_Scrollbar_CornerRadius: return "component.scrollbar.corner-radius.default";
        case Tok::C_Scrollbar_Padding: return "component.scrollbar.padding.default";
        case Tok::C_Scrollbar_OverlayMargin: return "component.scrollbar.overlay.margin.default";
        case Tok::C_Scrollbar_OverlayWidthRest: return "component.scrollbar.overlay.width.rest";
        case Tok::C_Scrollbar_OverlayWidthHover: return "component.scrollbar.overlay.width.hover";
        case Tok::C_Scrollbar_OverlayProximity: return "component.scrollbar.overlay.proximity.default";
        case Tok::C_Scrollbar_OverlayPadding: return "component.scrollbar.overlay.padding.default";
        case Tok::C_Slider_Grab: return "component.slider.grab.color.default";
        case Tok::C_Slider_GrabDown: return "component.slider.grab.color.down";
        case Tok::C_Slider_CornerRadius: return "component.slider.corner-radius.default";
        case Tok::C_Checkbox_Mark: return "component.checkbox.mark.color.default";
        case Tok::C_Checkbox_BackgroundSelected: return "component.checkbox.background.color.selected";
        case Tok::C_Checkbox_Background: return "component.checkbox.background.color.default";
        case Tok::C_Checkbox_BackgroundHover: return "component.checkbox.background.color.hover";
        case Tok::C_Checkbox_BackgroundDown: return "component.checkbox.background.color.down";
        case Tok::C_Checkbox_BackgroundSelectedHover: return "component.checkbox.background.color.selected-hover";
        case Tok::C_Checkbox_BackgroundSelectedDown: return "component.checkbox.background.color.selected-down";
        case Tok::C_Checkbox_Border: return "component.checkbox.border.color.default";
        case Tok::C_Checkbox_BorderSelected: return "component.checkbox.border.color.selected";
        case Tok::C_Checkbox_BoxSize: return "component.checkbox.box-size.default";
        case Tok::C_Checkbox_CornerRadius: return "component.checkbox.corner-radius.default";
        case Tok::C_Checkbox_BorderWidth: return "component.checkbox.border.width.default";
        case Tok::C_Separator_Color: return "component.separator.color.default";
        case Tok::C_Separator_Hover: return "component.separator.color.hover";
        case Tok::C_Separator_Down: return "component.separator.color.down";
        case Tok::C_Separator_Size: return "component.separator.size.default";
        case Tok::C_Separator_TextBorderWidth: return "component.separator.text.border.width.default";
        case Tok::C_ResizeGrip_Color: return "component.resize-grip.color.default";
        case Tok::C_ResizeGrip_Hover: return "component.resize-grip.color.hover";
        case Tok::C_ResizeGrip_Down: return "component.resize-grip.color.down";
        case Tok::C_Table_HeaderBackground: return "component.table.header.background.color.default";
        case Tok::C_Table_BorderStrong: return "component.table.border.color.strong";
        case Tok::C_Table_BorderLight: return "component.table.border.color.light";
        case Tok::C_Table_RowBackground: return "component.table.row.background.color.default";
        case Tok::C_Table_RowBackgroundAlt: return "component.table.row.background.color.alt";
        case Tok::C_Table_AngledHeadersAngle: return "component.table.angled-headers.angle";
        case Tok::C_Table_AngledHeadersTextAlign: return "component.table.angled-headers.text-align";
        case Tok::C_Image_CornerRadius: return "component.image.corner-radius.default";
        case Tok::C_Image_BorderWidth: return "component.image.border.width.default";
        case Tok::C_Docking_NodeHasCloseButton: return "component.docking.node.has-close-button";
        case Tok::C_Docking_SeparatorSize: return "component.docking.separator.size.default";
        case Tok::C_Zone_SeparatorSize: return "component.zone.separator.size.default";
        case Tok::C_Zone_SeparatorColor: return "component.zone.separator.color.default";
        case Tok::C_Zone_SeparatorColorContinuation: return "component.zone.separator.color.continuation";
        case Tok::C_Zone_SeparatorContinuationOpacity: return "component.zone.separator.continuation.opacity.default";
        case Tok::C_DragDropTarget_CornerRadius: return "component.drop-target.corner-radius.default";
        case Tok::C_DragDropTarget_BorderWidth: return "component.drop-target.border.width.default";
        case Tok::C_DragDropTarget_Padding: return "component.drop-target.padding.default";
        case Tok::C_KeyCap_Background: return "component.key-cap.background.color.default";
        case Tok::C_KeyCap_Border: return "component.key-cap.border.color.default";
        case Tok::C_KeyCap_Label: return "component.key-cap.label.color.default";
        case Tok::C_KeyCap_CornerRadius: return "component.key-cap.corner-radius.default";
        case Tok::C_KeyCap_Padding: return "component.key-cap.padding.default";
        case Tok::C_KeyCap_FontScale: return "component.key-cap.font-scale.default";
        case Tok::C_StatusBar_Background: return "component.status-bar.background.color.default";
        case Tok::C_StatusBar_Label: return "component.status-bar.label.color.default";
        case Tok::C_StatusBar_Height: return "component.status-bar.size.height";
        case Tok::C_StatusBar_Padding: return "component.status-bar.padding.default";
        case Tok::C_ShortcutRow_BackgroundHover: return "component.shortcut-row.background.color.hover";
        case Tok::C_ShortcutRow_BackgroundSelected: return "component.shortcut-row.background.color.selected";
        case Tok::C_Shortcut_ConflictSoft: return "component.shortcut.conflict.color.soft";
        case Tok::C_Shortcut_ConflictHard: return "component.shortcut.conflict.color.hard";
        case Tok::C_Shortcut_Recording: return "component.shortcut.recording.color.default";
        case Tok::C_Shortcut_CaptureBackground: return "component.shortcut.capture.background.color.default";
        case Tok::C_SectionHeader_Label: return "component.section-header.label.color.default";
        case Tok::C_SectionHeader_FontScale: return "component.section-header.font-scale.default";
        case Tok::C_CaptureField_Background: return "component.capture-field.background.color.default";
        case Tok::C_CaptureField_BackgroundRecording: return "component.capture-field.background.color.recording";
        case Tok::C_CaptureField_BackgroundHover: return "component.capture-field.background.color.hover";
        case Tok::C_CaptureField_BackgroundDown: return "component.capture-field.background.color.down";
        case Tok::C_CaptureField_Border: return "component.capture-field.border.color.default";
        case Tok::C_CaptureField_BorderRecording: return "component.capture-field.border.color.recording";
        case Tok::C_CaptureField_Label: return "component.capture-field.label.color.default";
        case Tok::C_CaptureField_LabelHint: return "component.capture-field.label.color.hint";
        case Tok::C_CaptureField_CornerRadius: return "component.capture-field.corner-radius.default";
        case Tok::C_CaptureField_Padding: return "component.capture-field.padding.default";
        case Tok::C_CaptureField_MinWidth: return "component.capture-field.size.min-width";
        case Tok::C_CaptureField_Height: return "component.capture-field.size.height";
        case Tok::C_Toggle_Background: return "component.toggle.background.color.default";
        case Tok::C_Toggle_BackgroundHover: return "component.toggle.background.color.hover";
        case Tok::C_Toggle_BackgroundSelected: return "component.toggle.background.color.selected";
        case Tok::C_Toggle_Border: return "component.toggle.border.color.default";
        case Tok::C_Toggle_BorderSelected: return "component.toggle.border.color.selected";
        case Tok::C_Toggle_Label: return "component.toggle.label.color.default";
        case Tok::C_Toggle_LabelSelected: return "component.toggle.label.color.selected";
        case Tok::C_Toggle_CornerRadius: return "component.toggle.corner-radius.default";
        case Tok::C_Toggle_BorderWidth: return "component.toggle.border.width.default";
        case Tok::C_IconButton_Background: return "component.icon-button.background.color.default";
        case Tok::C_IconButton_BackgroundHover: return "component.icon-button.background.color.hover";
        case Tok::C_IconButton_BackgroundDown: return "component.icon-button.background.color.down";
        case Tok::C_IconButton_Border: return "component.icon-button.border.color.default";
        case Tok::C_IconButton_Icon: return "component.icon-button.icon.color.default";
        case Tok::C_IconButton_IconNegative: return "component.icon-button.icon.color.negative";
        case Tok::C_IconButton_CornerRadius: return "component.icon-button.corner-radius.default";
        case Tok::S_Color_DataViz_Line: return "component.data-viz.line.color.default";
        case Tok::S_Color_DataViz_LineHover: return "component.data-viz.line.color.hover";
        case Tok::S_Color_DataViz_Histogram: return "component.data-viz.histogram.color.default";
        case Tok::S_Color_DataViz_HistogramHover: return "component.data-viz.histogram.color.hover";
        case Tok::S_Color_Negative_Recording: return "semantic.background.negative.recording";
        case Tok::S_Color_Background_Child: return "semantic.background.child.default";
        case Tok::S_Color_Background_MenuBar: return "semantic.background.menu-bar.default";
        case Tok::S_Color_Background_Title: return "semantic.background.title.default";
        case Tok::S_Color_Background_TitleActive: return "semantic.background.title.active";
        case Tok::S_Color_Background_TitleCollapsed: return "semantic.background.title.collapsed";
        case Tok::S_Color_Background_ScrollbarTrack: return "semantic.background.scrollbar-track.default";
        case Tok::S_Color_Background_DimWindowing: return "semantic.background.dim.windowing";
        case Tok::S_Color_Background_DimModal: return "semantic.background.dim.modal";
        case Tok::S_Color_Background_DockingEmpty: return "semantic.background.docking-empty.default";
        case Tok::S_Color_Background_DropTarget: return "semantic.background.drop-target.default";
        case Tok::S_Color_Foreground_ScrollbarGrab: return "semantic.foreground.scrollbar-grab.default";
        case Tok::S_Color_Border_Shadow: return "semantic.border.color.shadow.default";
        case Tok::S_Color_Border_TreeLine: return "semantic.border.color.tree-line.default";
        case Tok::S_Color_Focus_Default: return "semantic.focus.color.default";
        case Tok::S_Color_Focus_Windowing: return "semantic.focus.color.windowing";
        case Tok::S_Color_Accent_DropTarget: return "semantic.background.accent.drop-target";
        case Tok::S_Color_Accent_DockingPreview: return "semantic.background.accent.docking-preview";
        case Tok::C_EditorTopBar_Padding: return "component.editor-top-bar.padding.default";
        case Tok::C_StatusBar_Gap: return "component.status-bar.gap.default";
        case Tok::P_UiUnit: return "primitive.ui-unit";
        case Tok::S_Size_ControlHeight: return "semantic.size.control-height";
        case Tok::C_Dropdown_Height: return "component.dropdown.size.height";
        case Tok::C_Dropdown_Padding: return "component.dropdown.padding.default";
        case Tok::C_Dropdown_ChevronSize: return "component.dropdown.chevron.size";
        case Tok::C_Dropdown_IconSize: return "component.dropdown.icon.size";
        case Tok::C_Dropdown_Background: return "component.dropdown.background.color.default";
        case Tok::C_Dropdown_BackgroundHover: return "component.dropdown.background.color.hover";
        case Tok::C_Dropdown_Text: return "component.dropdown.label.color.default";
        case Tok::C_Dropdown_Icon: return "component.dropdown.icon.color.default";
        case Tok::C_Dropdown_CornerRadius: return "component.dropdown.corner-radius.default";
        case Tok::C_Menu_Background: return "component.menu.background.color.default";
        case Tok::C_Menu_CornerRadius: return "component.menu.corner-radius.default";
        case Tok::C_Menu_ItemHoverBg: return "component.menu.item.background.color.hover";
        case Tok::C_Menu_ItemSelectedBg: return "component.menu.item.background.color.selected";
        case Tok::C_Menu_ColumnHeaderText: return "component.menu.column-header.label.color.default";
        case Tok::C_Menu_Padding: return "component.menu.padding.default";
        case Tok::C_Menu_ColumnGap: return "component.menu.column-gap.default";
        case Tok::C_Menu_ItemGap: return "component.menu.item-gap.default";
        case Tok::P_Radius_150: return "primitive.radius.150";
        case Tok::S_CornerRadius_Control: return "semantic.radius.control";
        case Tok::C_Dropdown_Border: return "component.dropdown.border.color.default";
        case Tok::C_Dropdown_BorderHover: return "component.dropdown.border.color.hover";
        case Tok::C_Dropdown_BackgroundDown: return "component.dropdown.background.color.down";
        case Tok::C_Dropdown_BackgroundOpen: return "component.dropdown.background.color.open";
        case Tok::C_Dropdown_BorderWidth: return "component.dropdown.border.width.default";
        case Tok::C_Menu_Border: return "component.menu.border.color.default";
        case Tok::C_Menu_BorderWidth: return "component.menu.border.width.default";
        case Tok::P_Color_Gray_850: return "primitive.color.gray.850";
        case Tok::C_Editor_Background: return "component.editor.background.color.default";
        case Tok::C_Editor_TopBarBackground: return "component.editor.top-bar.background.color.default";
        case Tok::S_Spacing_EditorInset: return "semantic.spacing.editor-inset";
        case Tok::C_Editor_ContentInset: return "component.editor.content.inset.default";
        case Tok::C_Cursor_Color: return "component.cursor.icon.color.default";
        case Tok::C_Menu_ItemPaddingX: return "component.menu.item.padding.horizontal";
        case Tok::C_Menu_TitleText: return "component.menu.title.label.color.default";
        case Tok::C_Tooltip_Background: return "component.tooltip.background.color.default";
        case Tok::C_Tooltip_Text: return "component.tooltip.label.color.default";
        case Tok::C_Tooltip_Border: return "component.tooltip.border.color.default";
        case Tok::C_Tooltip_BorderWidth: return "component.tooltip.border.width.default";
        case Tok::C_Tooltip_CornerRadius: return "component.tooltip.corner-radius.default";
        case Tok::C_Tooltip_Padding: return "component.tooltip.padding.default";
        case Tok::P_Color_WhiteTransparent: return "primitive.color.white-transparent";
        case Tok::P_Color_Gray_875: return "primitive.color.gray.875";
        case Tok::C_ZoneTab_Gap: return "component.zone-tab.gap.default";
        case Tok::C_ZoneTab_Padding: return "component.zone-tab.padding.default";
        case Tok::C_ZoneTab_Background: return "component.zone-tab.background.color.default";
        case Tok::C_ZoneTab_BackgroundActive: return "component.zone-tab.background.color.active";
        case Tok::C_ZoneTab_BackgroundHover: return "component.zone-tab.background.color.hover";
        case Tok::C_ZoneTab_Text: return "component.zone-tab.label.color.default";
        case Tok::C_ZoneTab_TextActive: return "component.zone-tab.label.color.active";
        case Tok::C_ZoneTab_BarBackground: return "component.zone-tab.bar.background.color.default";
        case Tok::C_ZoneTab_InsertLineColor: return "component.zone-tab.insert-line.color.default";
        case Tok::C_ZoneTab_InsertLineWidth: return "component.zone-tab.insert-line.width.default";
        case Tok::C_ZoneTab_DropPreviewFill: return "component.zone-tab.drop-preview.color.default";
        case Tok::C_ZoneTab_DropCenterInset: return "component.zone-tab.drop-preview.center-inset";
        case Tok::C_ZoneTab_PreviewAnimDuration: return "component.zone-tab.drop-preview.anim-duration";
        case Tok::C_ZoneTab_DragThreshold: return "component.zone-tab.drag.threshold";
        case Tok::C_ZoneTab_ShowSolo: return "component.zone-tab.show-solo";
        case Tok::C_TitleBar_Background: return "component.title-bar.background.color.default";
        case Tok::C_TitleBar_Icon: return "component.title-bar.icon.color.default";
        case Tok::C_TitleBar_Text: return "component.title-bar.label.color.default";
        case Tok::C_TitleBar_ButtonHover: return "component.title-bar.button.background.color.hover";
        case Tok::C_TitleBar_CloseHover: return "component.title-bar.close.background.color.hover";
        case Tok::C_Splash_Background: return "component.splash.background.color.default";
        case Tok::C_Splash_Link: return "component.splash.link.color.default";
        case Tok::C_Splash_VersionText: return "component.splash.version-text.color.default";
        case Tok::C_Dropdown_BackgroundHoverMinimal: return "component.dropdown.background.color.hover-minimal";
        case Tok::P_Color_Gray_780: return "primitive.color.gray.780";
        case Tok::P_Color_Gray_760: return "primitive.color.gray.760";
        case Tok::P_Color_Gray_740: return "primitive.color.gray.740";
        case Tok::S_Background_App_Control: return "semantic.background.app.control";
        case Tok::S_Surface_Canvas: return "semantic.background.surface.canvas";
        case Tok::S_Surface_Raised: return "semantic.background.surface.raised";
        case Tok::S_Background_App_Child: return "semantic.background.app.child";
        case Tok::S_Background_App_Frame: return "semantic.background.app.frame";
        case Tok::S_Background_App_FrameHover: return "semantic.background.app.frame-hover";
        case Tok::C_PrefBar_Background: return "component.preferences-title-bar.background.color.default";
        case Tok::C_PrefBar_Text: return "component.preferences-title-bar.label.color.default";
        case Tok::C_PrefBar_Icon: return "component.preferences-title-bar.icon.color.default";
        case Tok::C_PrefBar_ButtonHover: return "component.preferences-title-bar.button.background.color.hover";
        case Tok::C_PrefBar_CloseHover: return "component.preferences-title-bar.close.background.color.hover";
        case Tok::C_Panel_HeaderBackground: return "component.panel.header.background.color.default";
        case Tok::C_Panel_BodyL1: return "component.panel.body.background.color.level-1";
        case Tok::C_Panel_BodyL2: return "component.panel.body.background.color.level-2";
        case Tok::C_Panel_BodyL3: return "component.panel.body.background.color.level-3";
        case Tok::C_Panel_Border: return "component.panel.border.color.default";
        case Tok::C_Panel_Text: return "component.panel.label.color.default";
        case Tok::C_Panel_OverrideBadge: return "component.panel.override-badge.color.default";
        case Tok::C_Panel_Gap: return "component.panel.gap.size.default";
        case Tok::C_PropertyGroup_Gap: return "component.property-group.gap.size.default";
        case Tok::C_Panel_CornerRadius: return "component.panel.corner-radius.default";
        case Tok::C_Outliner_Row_Hover:           return "component.outliner.row.background.hover";
        case Tok::C_Outliner_Row_Selected:        return "component.outliner.row.background.selected";
        case Tok::C_Outliner_Row_SelectedHover:   return "component.outliner.row.background.selected-hover";
        case Tok::C_Outliner_Row_Active:          return "component.outliner.row.background.active";
        case Tok::C_Outliner_Row_ActiveHover:     return "component.outliner.row.background.active-hover";
        case Tok::C_Outliner_Text:                return "component.outliner.row.text.default";
        case Tok::C_Outliner_TreeLineInset:       return "component.outliner.tree-line.inset.default";
        case Tok::C_Outliner_Search_Visual:       return "component.outliner.row.search.background.visual";
        case Tok::C_Outliner_Search_Hover:        return "component.outliner.row.search.background.hover";
        case Tok::C_Outliner_Search_Selected:     return "component.outliner.row.search.background.selected";
        case Tok::C_Outliner_Search_SelectedHover:return "component.outliner.row.search.background.selected-hover";
        case Tok::C_Outliner_Search_Active:       return "component.outliner.row.search.background.active";
        case Tok::C_Outliner_Search_ActiveHover:  return "component.outliner.row.search.background.active-hover";
        case Tok::C_Outliner_Search_Text:         return "component.outliner.row.search.text.default";

        case Tok::S_State_Active_OnPage:    return "semantic.state.active.on-page";
        case Tok::S_State_Active_Loose:     return "semantic.state.active.loose";
        case Tok::S_State_Selected_OnPage:  return "semantic.state.selected.on-page";
        case Tok::S_State_Selected_Loose:   return "semantic.state.selected.loose";

        case Tok::C_Viewport_CanvasArea:        return "component.viewport.canvas.background";
        case Tok::C_Viewport_Guide:             return "component.viewport.guide.color";
        case Tok::C_Viewport_PageBorder:        return "component.viewport.page.border";
        case Tok::C_Viewport_PageNameHover:     return "component.viewport.page-name.background.hover";
        case Tok::C_Viewport_OriginOutline:     return "component.viewport.origin.outline";
        case Tok::C_Viewport_CursorRing:        return "component.viewport.cursor.ring";
        case Tok::C_Viewport_CursorRingAccent:  return "component.viewport.cursor.ring-accent";
        case Tok::C_Viewport_CursorTick:        return "component.viewport.cursor.tick";
        case Tok::C_Viewport_CursorAxisX:       return "component.viewport.cursor.axis-x";
        case Tok::C_Viewport_CursorAxisY:       return "component.viewport.cursor.axis-y";
        case Tok::C_Viewport_ThumbnailBackground: return "component.viewport.thumbnail.background";
        case Tok::C_Viewport_ThumbnailBorder:   return "component.viewport.thumbnail.border";

        case Tok::C_EditHandle_Edge:        return "component.edit-handle.edge";
        case Tok::C_EditHandle_Vertex:      return "component.edit-handle.vertex";
        case Tok::C_EditHandle_VertexRing:  return "component.edit-handle.vertex-ring";
        case Tok::C_EditHandle_NurbsHull:   return "component.edit-handle.nurbs-hull";
        case Tok::C_EditHandle_Free:        return "component.edit-handle.free";
        case Tok::C_EditHandle_Aligned:     return "component.edit-handle.aligned";
        case Tok::C_EditHandle_Mirrored:    return "component.edit-handle.mirrored";
        case Tok::C_EditHandle_Vector:      return "component.edit-handle.vector";
        case Tok::C_EditHandle_Default:     return "component.edit-handle.default";

        case Tok::C_ZoneOverlay_CornerTopLeft:     return "component.zone-overlay.corner.top-left";
        case Tok::C_ZoneOverlay_CornerTopRight:    return "component.zone-overlay.corner.top-right";
        case Tok::C_ZoneOverlay_CornerBottomLeft:  return "component.zone-overlay.corner.bottom-left";
        case Tok::C_ZoneOverlay_CornerBottomRight: return "component.zone-overlay.corner.bottom-right";
        case Tok::C_ZoneOverlay_SplitLine:         return "component.zone-overlay.split-line";
        case Tok::C_ZoneOverlay_JoinKeep:          return "component.zone-overlay.join.keep";
        case Tok::C_ZoneOverlay_JoinRemove:        return "component.zone-overlay.join.remove";
        case Tok::C_ZoneOverlay_JoinResidual:      return "component.zone-overlay.join.residual";
        case Tok::C_ZoneOverlay_JoinFrame:         return "component.zone-overlay.join.frame";
        case Tok::C_ZoneOverlay_TransformDim:      return "component.zone-overlay.transform-dim";

        case Tok::P_Config_PreviewPlacement: return "primitive.config.preview-placement";
        case Tok::S_Config_PreviewPlacement: return "semantic.config.preview-placement";
        case Tok::C_Viewport_Crosshair:      return "component.viewport.crosshair";
        case Tok::S_Config_PlacementPreviewAlpha: return "semantic.config.placement-preview-alpha";
        case Tok::P_Config_ShowCornerZones: return "primitive.config.show-corner-zones";
        case Tok::S_Config_ShowCornerZones: return "semantic.config.show-corner-zones";

        case Tok::_Count: return "";
    }
    return "";
}

/// Convenience: the std::string id used by every string-keyed API.
inline std::string TokIdStr(Tok t) { return std::string(TokName(t)); }

} // namespace DesignSystem

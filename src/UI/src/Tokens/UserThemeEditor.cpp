#include <UI/Tokens/UserThemeEditor.h>
#include <UI/Widgets/IconWidgets.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace DesignSystem {

namespace {

// One editable control inside a sub-section: a friendly label + the token it
// drives. Using `Tok` (not a string) means a typo here is a *compile error*.
struct ZoneEntry {
    Tok         token;
    const char* label;
};

// A sub-section (a collapsible group, e.g. "Buttons").
struct ZoneSection {
    const char*                  name;
    std::vector<ZoneEntry>       entries;
};

// A top-level area (e.g. "User Interface"), holding sub-sections.
struct ZoneArea {
    const char*                  name;
    std::vector<ZoneSection>     sections;
};

// ─────────────────────────────────────────────────────────────────────────────
//  The Blender-style zone map. This is the single place that decides how the
//  user-facing editor is organised. Reordering / regrouping is purely a
//  data edit here; no rendering code changes.
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ZoneArea>& ZoneMap() {
    static const std::vector<ZoneArea> kMap = {
        { "Base Colors", {
            { "Surfaces", {
                { Tok::S_Color_Background_Default,        "App background" },
                { Tok::S_Color_Background_Layer1,          "Surface" },
                { Tok::S_Color_Background_Layer2,  "Elevated surface" },
                { Tok::S_Color_Background_Popup,  "Popup / menu bg" },
                { Tok::S_Color_Background_Child,  "Child background" },
                { Tok::S_Color_Background_MenuBar,"Menu bar bg" },
            }},
            { "Text", {
                { Tok::S_Color_Text_Default,             "Primary text" },
                { Tok::S_Color_Text_Subtle,        "Muted text" },
                { Tok::S_Color_Text_Disabled,     "Disabled text" },
                { Tok::S_Color_Text_Link,         "Hyperlink" },
                { Tok::S_Color_Background_TextSelection, "Selection bg" },
            }},
            { "Accent & Status", {
                { Tok::S_Color_Accent_Default,           "Accent" },
                { Tok::S_Color_Accent_Hover,      "Accent (hover)" },
                { Tok::S_Color_Accent_Down,     "Accent (active)" },
                { Tok::S_Color_Negative_Default,           "Danger" },
                { Tok::S_Color_Notice_Default,          "Warning" },
                { Tok::S_Color_Positive_Default,          "Success" },
            }},
            { "Borders & Title", {
                { Tok::S_Color_Border_Default,           "Border" },
                { Tok::S_Color_Border_Strong,     "Strong border" },
                { Tok::S_Color_Background_Title,  "Title bar" },
                { Tok::S_Color_Background_TitleActive, "Title bar (focused)" },
            }},
        }},
        { "Window", {
            { "Appearance", {
                { Tok::C_Window_Background,       "Background" },
                { Tok::C_Window_BorderColor,     "Border color" },
                { Tok::C_Window_BorderWidth,      "Border thickness" },
                { Tok::C_Window_CornerRadius,        "Corner rounding" },
                { Tok::C_Window_Padding,         "Inner padding" },
                { Tok::C_Window_TitleAlign,      "Title text align" },
            }},
            { "Child", {
                { Tok::C_Child_Background,        "Background" },
                { Tok::C_Child_CornerRadius,         "Rounding" },
                { Tok::C_Child_BorderWidth,       "Border thickness" },
            }},
        }},
        { "Button", {
            { "Colors", {
                { Tok::C_Button_Background,       "Background" },
                { Tok::C_Button_BackgroundHover, "Background (hover)" },
                { Tok::C_Button_BackgroundDown,"Background (active)" },
                { Tok::C_Button_Label,            "Text" },
            }},
        }},
        { "Frame & Inputs", {
            { "Colors", {
                { Tok::C_Frame_Background,        "Background" },
                { Tok::C_Frame_BackgroundHover,  "Background (hover)" },
                { Tok::C_Frame_BackgroundDown, "Background (active)" },
                { Tok::C_Frame_InputTextCursor,  "Text caret" },
                { Tok::C_Checkbox_Mark,          "Check mark" },
                { Tok::C_Checkbox_BackgroundSelected, "Checkbox selected bg" },
            }},
            { "Shape", {
                { Tok::C_Frame_CornerRadius,         "Rounding" },
                { Tok::C_Frame_BorderWidth,       "Border thickness" },
                { Tok::C_Frame_Padding,          "Inner padding" },
            }},
        }},
        { "Slider", {
            { "Grab", {
                { Tok::C_Slider_Grab,            "Grab" },
                { Tok::C_Slider_GrabDown,      "Grab (active)" },
                { Tok::C_Slider_CornerRadius,        "Grab rounding" },
                { Tok::S_Config_GrabMinSize,      "Grab min size" },
                { Tok::S_Config_LogSliderDeadzone,"Log slider deadzone" },
            }},
        }},
        { "Header & Tree", {
            { "Colors", {
                { Tok::C_Header_Background,       "Header" },
                { Tok::C_Header_BackgroundHover, "Header (hover)" },
                { Tok::C_Header_BackgroundDown,"Header (active)" },
                { Tok::S_Color_Border_TreeLine,        "Tree lines" },
            }},
            { "Layout", {
                { Tok::S_Config_TreeLinesSize,    "Tree line size" },
                { Tok::S_Config_IndentSpacing,    "Indent width" },
            }},
        }},
        { "Tab", {
            { "Colors", {
                { Tok::C_Tab_Background,          "Tab" },
                { Tok::C_Tab_BackgroundHover,    "Tab (hover)" },
                { Tok::C_Tab_BackgroundSelected, "Tab (selected)" },
                { Tok::C_Tab_OverlineSelected,   "Selected overline" },
                { Tok::C_Tab_BackgroundDimmed,   "Tab (unfocused)" },
                { Tok::C_Tab_BackgroundDimmedSelected, "Tab (unfocused selected)" },
            }},
            { "Shape", {
                { Tok::C_Tab_CornerRadius,           "Rounding" },
                { Tok::C_Tab_BarBorderWidth,      "Tab-bar border" },
                { Tok::C_Tab_BarOverlineWidth,    "Tab-bar overline" },
            }},
        }},
        { "Scrollbar", {
            { "Appearance", {
                { Tok::C_Scrollbar_Background,    "Track" },
                { Tok::C_Scrollbar_Grab,         "Grab" },
                { Tok::C_Scrollbar_GrabHover,    "Grab (hover)" },
                { Tok::C_Scrollbar_GrabDown,   "Grab (active)" },
                { Tok::C_Scrollbar_Size,         "Thickness" },
                { Tok::C_Scrollbar_CornerRadius,     "Rounding" },
            }},
        }},
        { "Popup & Menu", {
            { "Appearance", {
                { Tok::C_Popup_Background,        "Popup bg" },
                { Tok::C_Popup_MenuBarBackground,"Menu bar bg" },
                { Tok::C_Popup_CornerRadius,         "Popup rounding" },
                { Tok::C_Popup_BorderWidth,       "Popup border" },
            }},
        }},
        { "Separator & Resize", {
            { "Separator", {
                { Tok::C_Separator_Color,        "Separator" },
                { Tok::C_Separator_Hover,        "Separator (hover)" },
                { Tok::C_Separator_Down,       "Separator (active)" },
                { Tok::C_Separator_Size,         "Separator size" },
                { Tok::C_Separator_TextBorderWidth, "Separator text border" },
            }},
            { "Resize Grip", {
                { Tok::C_ResizeGrip_Color,       "Resize grip" },
                { Tok::C_ResizeGrip_Hover,       "Resize grip (hover)" },
                { Tok::C_ResizeGrip_Down,      "Resize grip (active)" },
            }},
        }},
        { "Status Bar", {
            { "Appearance", {
                { Tok::C_StatusBar_Background,    "Background" },
                { Tok::C_StatusBar_Label,         "Text" },
                { Tok::C_StatusBar_Height,       "Height" },
                { Tok::C_StatusBar_Padding,      "Padding" },
            }},
        }},
        { "Table", {
            { "Colors", {
                { Tok::C_Table_HeaderBackground,  "Header bg" },
                { Tok::C_Table_BorderStrong,      "Strong border" },
                { Tok::C_Table_BorderLight,       "Light border" },
                { Tok::C_Table_RowBackground,     "Row bg (even)" },
                { Tok::C_Table_RowBackgroundAlt,  "Row bg (odd)" },
            }},
            { "Layout", {
                { Tok::S_Config_CellPadding,             "Cell padding" },
                { Tok::C_Table_AngledHeadersAngle,      "Angled header angle" },
                { Tok::C_Table_AngledHeadersTextAlign,  "Angled header align" },
            }},
        }},
        { "Docking", {
            { "Appearance", {
                { Tok::S_Color_Accent_DockingPreview,         "Preview overlay" },
                { Tok::S_Color_Background_DockingEmpty, "Empty node bg" },
                { Tok::C_Docking_SeparatorSize,        "Separator size" },
                { Tok::C_Docking_NodeHasCloseButton,   "Node close button" },
            }},
        }},
        { "Plots", {
            { "Lines & Histogram", {
                { Tok::S_Color_DataViz_Line,            "Lines" },
                { Tok::S_Color_DataViz_LineHover,     "Lines (hover)" },
                { Tok::S_Color_DataViz_Histogram,        "Histogram" },
                { Tok::S_Color_DataViz_HistogramHover, "Histogram (hover)" },
            }},
        }},
        { "Navigation & Drag-Drop", {
            { "Navigation", {
                { Tok::S_Color_Focus_Default,                 "Nav cursor" },
                { Tok::S_Color_Focus_Windowing,     "Ctrl+Tab highlight" },
                { Tok::S_Color_Background_DimWindowing, "Ctrl+Tab dim" },
                { Tok::S_Color_Background_DimModal,  "Modal dim" },
            }},
            { "Drag & Drop", {
                { Tok::S_Color_Accent_DropTarget,           "Target border" },
                { Tok::S_Color_Background_DropTarget, "Target background" },
                { Tok::C_DragDropTarget_BorderWidth,      "Target border size" },
                { Tok::C_DragDropTarget_Padding,         "Target padding" },
            }},
        }},
        { "Spacing & Sizing", {
            { "Item Spacing", {
                { Tok::S_Config_ItemSpacing,       "Item spacing" },
                { Tok::S_Config_ItemInnerSpacing,  "Inner spacing" },
                { Tok::S_Config_TouchExtraPadding, "Touch padding" },
                { Tok::S_Config_ColumnsMinSpacing, "Columns min spacing" },
            }},
            { "Display", {
                { Tok::S_Config_DisplayWindowPadding,   "Window keep-visible" },
                { Tok::S_Config_DisplaySafeAreaPadding, "Safe area padding" },
                { Tok::S_Config_MouseCursorScale,       "Cursor scale" },
            }},
        }},
        { "Global", {
            { "Appearance", {
                { Tok::S_Scale_Default,          "UI scale (everything)" },
                { Tok::S_Opacity_Default,          "Global alpha" },
                { Tok::S_Opacity_Disabled,         "Disabled alpha" },
                { Tok::S_FontSize_Default,       "Base font size" },
                { Tok::S_FontScale_Default,      "Font scale (text only)" },
            }},
            { "Rendering", {
                { Tok::S_Config_AntiAliasedLines,        "Anti-aliased lines" },
                { Tok::S_Config_AntiAliasedLinesUseTex,  "AA lines via texture" },
                { Tok::S_Config_AntiAliasedFill,         "Anti-aliased fill" },
                { Tok::S_Config_CurveTessellationTol,    "Curve tolerance" },
                { Tok::S_Config_CircleTessellationMaxError, "Circle max error" },
            }},
            { "Hover Timing", {
                { Tok::S_Config_HoverDelayStationary, "Stationary delay" },
                { Tok::S_Config_HoverDelayShort,      "Short delay" },
                { Tok::S_Config_HoverDelayNormal,     "Normal delay" },
            }},
        }},
    };
    return kMap;
}

bool RowMatches(const ZoneEntry& e, const char* filter) {
    if (!filter || filter[0] == '\0') return true;
    std::string hay = std::string(e.label) + " " + TokIdStr(e.token);
    // Case-insensitive contains.
    auto lower = [](std::string s) {
        for (char& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    return lower(hay).find(lower(filter)) != std::string::npos;
}

} // namespace

UserThemeEditor::UserThemeEditor() {
    searchBuffer_[0] = '\0';
}

void UserThemeEditor::Render(Context& ctx, OverrideManager& mgr) {
    auto& ds = DesignSystem::Instance();

    ImGui::TextWrapped(
        "Customise the look by area, like a theme editor. Pick a target scope "
        "(the whole app, or a specific zone / sub-component); each property "
        "row then lets you choose, per property, whether the edit applies to "
        "this theme only or globally. Hover the '(?)' on a row to compare "
        "inherited vs actual.");

    // ── Theme / accessibility selector (which theme you are editing) ─────────
    const char* themes[] = { "Dark", "Light", "Muted Green", "High Contrast" };
    int ti = (int)ctx.GetTheme();
    if (ImGui::Combo("Theme", &ti, themes, 4)) {
        ctx.SetTheme((ThemeType)ti);
        ds.SetContext(ctx);
    }
    ImGui::SameLine();
    const char* acc[] = { "None", "Protanopia", "Deuteranopia", "Tritanopia" };
    int ai = (int)ctx.GetAccessibility();
    if (ImGui::Combo("Accessibility", &ai, acc, 4)) {
        ctx.SetAccessibility((AccessibilityType)ai);
        ds.SetContext(ctx);
    }

    ImGui::InputTextWithHint("##userSearch", "Filter controls...",
                             searchBuffer_, sizeof(searchBuffer_));
    ImGui::Separator();

    // Blender-style: ONE section per zone/scope (the whole app first, then
    // every registered zone & sub-component). Inside each zone section, the
    // property areas (Window, Button, …) drill down; each property is one
    // row in the shared 3-column [Property | Global | Theme] table, written
    // at THAT zone's scope. No target-scope selector.
    struct ScopeEntry { std::string path, label; int depth; };
    std::vector<ScopeEntry> zones;
    zones.push_back({ "", "Whole application (global)", 0 });
    for (const auto& s : ds.GetScopes())
        zones.push_back({ s.path, s.label, s.depth });

    ImGui::BeginChild("UserThemeScroll", ImVec2(0, 0), false);

    int zoneIdx = 0;
    for (const ScopeEntry& z : zones) {
        ++zoneIdx;

        // Count rows visible under the filter for this zone (all areas).
        int zoneVisible = 0;
        for (const auto& area : ZoneMap())
            for (const auto& sec : area.sections)
                for (const auto& e : sec.entries)
                    if (RowMatches(e, searchBuffer_)) ++zoneVisible;
        if (zoneVisible == 0) break;  // filter hides everything

        ImGui::PushID(zoneIdx);
        std::string indent(z.depth * 2, ' ');
        std::string header = indent + z.label;
        if (!z.path.empty()) header += "   [" + z.path + "]";

        // Only the global section is open by default; per-zone sections are
        // collapsed so the list stays scannable (Blender-like).
        bool zOpen = z.path.empty() || searchBuffer_[0] != '\0';
        if (!UI::IconCollapsingHeader("zhdr", header.c_str(), "", zOpen)) {
            ImGui::PopID();
            continue;
        }

        ImGui::Indent();
        int areaIdx = 0;
        for (const ZoneArea& area : ZoneMap()) {
            ++areaIdx;
            int visible = 0;
            for (const auto& sec : area.sections)
                for (const auto& e : sec.entries)
                    if (RowMatches(e, searchBuffer_)) ++visible;
            if (visible == 0) continue;

            ImGui::PushID(areaIdx);
            bool aOpen = (searchBuffer_[0] != '\0');
            if (!UI::IconTreeNode("ahdr", area.name, aOpen)) {
                ImGui::PopID();
                continue;
            }

            int secIdx = 0;
            for (const ZoneSection& sec : area.sections) {
                ++secIdx;
                int secVisible = 0;
                for (const auto& e : sec.entries)
                    if (RowMatches(e, searchBuffer_)) ++secVisible;
                if (secVisible == 0) continue;

                ImGui::PushID(secIdx);
                ImGui::SeparatorText(sec.name);
                if (inspector_.BeginPropertyTable("##t")) {
                    for (const ZoneEntry& e : sec.entries) {
                        if (!RowMatches(e, searchBuffer_)) continue;
                        inspector_.RenderScopedRow(TokIdStr(e.token), e.label,
                                                   z.path, ctx, mgr);
                    }
                    inspector_.EndPropertyTable();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
            ImGui::PopID();
        }
        ImGui::Unindent();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

} // namespace DesignSystem

#include "Application.h"

#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/DragValue.h>
#include <UI/Widgets/PopupMenu.h>
#include <UI/Widgets/ToolPalette.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport top bar + floating tool palette + Snap widget + Add menu
//  (docs/Ink/ROADMAP.md Lot 8). This restores the legacy Viewport bar on the
//  Ink model: the Object/Edit-mode, transform-orientation, pivot-point and snap
//  controls plus the overlay dropdown. Controls whose backing feature has NOT
//  landed yet on Ink are shown but GREYED (disabled) — they light up in later
//  passes. Every colour/size comes from design-system tokens.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// ── Floating tool palette (left column of the canvas) ─────────────────────────
// A plain column of buttons (no container chrome), in LOGICAL GROUPS separated
// by a larger gap: [Select · 2D Cursor] then [Shape · Curve]. Shape and Curve
// are MULTI-TOOLS: their button shows the current variant's icon, carries the
// corner triangle, and a RIGHT-CLICK opens the variant menu (icon + name per
// option).

namespace {
struct ToolVariant { const char* kind; const char* name; const char* icon; };
constexpr ToolVariant kShapeVariants[] = {
    { "rect",     "Rectangle", "crop-landscape" },
    { "ellipse",  "Ellipse",   "format-shapes" },
    { "triangle", "Triangle",  "shape-category" },
    { "free",     "Free",      "draw" },
};
constexpr ToolVariant kCurveVariants[] = {
    { "curve",        "B\xC3\xA9zier",        "bezier-curve" },
    { "beziercircle", "B\xC3\xA9zier Circle", "rounded-corner" },
    { "nurbs",        "NURBS Path",           "nurbs-curve" },
    { "nurbscircle",  "NURBS Circle",         "contrast-square" },
    { "poly",         "Poly Line",            "polyline" },
};
const ToolVariant& FindVariant(const std::string& kind, const ToolVariant* v,
                               int n) {
    for (int i = 0; i < n; ++i)
        if (kind == v[i].kind) return v[i];
    return v[0];
}
} // namespace

void Application::RenderToolPalette(ImVec2 origin, EditorState& st) {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    const std::string activeTool = tm.GetActiveTool();

    struct Entry {
        const char* toolId; int group; bool multi;
        std::string icon, name;
    };
    const ToolVariant& sv = FindVariant(toolShapeKind_, kShapeVariants, 4);
    const ToolVariant& cv = FindVariant(toolCurveKind_, kCurveVariants, 5);
    // The palette is CONTEXTUAL: it lists the current MODE's tools only
    // (ToolsForMode — Select + 2D Cursor everywhere, the creation multi-tools
    // in Object mode). Grouping: [Select · Cursor] then [Shape · Curve].
    std::vector<Entry> entries;
    for (const std::string& id : ToolsForMode(edit_.mode)) {
        if (id == "tool.select")
            entries.push_back({ "tool.select", 0, false, "select", "Select" });
        else if (id == "tool.cursor")
            entries.push_back({ "tool.cursor", 0, false, "crop-free",
                                "2D Cursor" });
        else if (id == "tool.shape")
            entries.push_back({ "tool.shape", 1, true, sv.icon,
                                std::string("Shape \xC2\xB7 ") + sv.name });
        else if (id == "tool.curve")
            entries.push_back({ "tool.curve", 1, true, cv.icon,
                                std::string("Curve \xC2\xB7 ") + cv.name });
        // Module Object tools (id prefix "iof."/other) are rendered by the
        // module via DrawToolButtons, not this core icon strip.
    }
    if (entries.empty()) return;

    std::vector<UI::ToolPaletteItem> items;
    items.reserve(entries.size());
    for (const Entry& e : entries) {
        UI::ToolPaletteItem it;
        it.icon = e.icon;
        it.selected = (activeTool == e.toolId);
        it.group = e.group;
        it.hasMenu = e.multi;
        it.tooltip = e.name;
        // Append the bound shortcut when there is one.
        if (const auto* t = tm.GetTool(e.toolId); t && !t->actionIds.empty()) {
            const std::string s = sm.GetShortcutString(t->actionIds.front());
            if (!s.empty()) it.tooltip += "   (" + s + ")";
        }
        if (e.multi) it.tooltip += "\nRight-click: pick the tool variant";
        items.push_back(std::move(it));
    }

    DS::DesignSystem::ZoneStyle zone("viewport/tools", "Viewport tools");
    const UI::ToolPaletteResult r = UI::ToolPalette("##InkTools", origin, items);
    // Publish the rect so canvas hit-testing excludes the palette.
    st.overlayRects.push_back(ImVec4(r.rectMin.x, r.rectMin.y, r.rectMax.x, r.rectMax.y));
    // Module tool buttons (IOF theme vignettes) below the core strip — ONLY in
    // Object mode (they are Object-mode tools, gone in Edit / Line-Mark). They
    // share the button side + a larger separating gap; the module publishes
    // their rects into st.overlayRects.
    if (activeModule_ && edit_.mode == EditorMode::Object) {
        const float gs = DS::DesignSystem::Instance().GetGlobalScale();
        const float side = r.rectMax.x - r.rectMin.x;   // match the core button
        const ImVec2 mOrigin(r.rectMin.x, r.rectMax.y + 10.0f * gs);
        activeModule_->DrawToolButtons(mOrigin, side, st.overlayRects);
    }
    if (r.clicked >= 0)
        Action_ActivateNamedTool(entries[(size_t)r.clicked].toolId);
    if (r.rightClicked >= 0 && entries[(size_t)r.rightClicked].multi) {
        toolMenuFor_  = entries[(size_t)r.rightClicked].toolId;
        toolMenuPos_  = ImGui::GetIO().MousePos;
        toolMenuLeaf_ = &st;
        toolMenuOpen_ = true;
        ImGui::OpenPopup("##toolVariants");
    }

    // Variant menu — rendered EVERY frame while open (popup rule), only by the
    // leaf that opened it (popup ids are scoped to the zone child window).
    if (toolMenuOpen_ && toolMenuLeaf_ == &st && !toolMenuFor_.empty()) {
        const bool shape = toolMenuFor_ == "tool.shape";
        const ToolVariant* vars = shape ? kShapeVariants : kCurveVariants;
        const int nVars = shape ? 4 : 5;
        const std::string& cur = shape ? toolShapeKind_ : toolCurveKind_;
        std::vector<UI::MenuEntry> menu;
        for (int i = 0; i < nVars; ++i) {
            UI::MenuEntry e;
            e.label = vars[i].name;
            if (cur == vars[i].kind) e.label += "  \xE2\x9C\x93";   // current
            e.icon  = vars[i].icon;
            const char* kind = vars[i].kind;
            const bool isShape = shape;
            e.onClick = [this, kind, isShape]() {
                if (isShape) toolShapeKind_ = kind; else toolCurveKind_ = kind;
                Action_ActivateNamedTool(isShape ? "tool.shape" : "tool.curve");
                toolMenuOpen_ = false;
                toolMenuFor_.clear();
            };
            menu.push_back(std::move(e));
        }
        const bool open = UI::ContextMenu("##toolVariants", toolMenuPos_, menu,
                                          shape ? "Shape tools" : "Curve tools");
        if (!open) { toolMenuOpen_ = false; toolMenuFor_.clear(); }
    }
}

// ── Default fill / stroke swatches (new-shape style) ──────────────────────────
// A mini-view of the SAME state the Fill / Stroke editors show: the FIRST
// fill/stroke of the current stacks (the active object's when one is selected,
// the persisted default otherwise). An EMPTY stack reads as "none": a white
// plate crossed by a red diagonal. The picker edits the first entry (applied to
// the whole selection when there is one) and offers the "none" button.

namespace {
// linear straight ↔ sRGB (UI pickers are sRGB; document colours are linear).
float LinToSrgb1(float u) {
    return u <= 0.0031308f ? u * 12.92f
                           : 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
}
float SrgbToLin1(float u) {
    return u <= 0.04045f ? u / 12.92f
                         : std::pow((u + 0.055f) / 1.055f, 2.4f);
}
ImVec4 LinToSrgb(const Ink::Color& c) {
    return { LinToSrgb1(c.r), LinToSrgb1(c.g), LinToSrgb1(c.b), c.a };
}
Ink::Color SrgbToLin(const ImVec4& c) {
    return { SrgbToLin1(c.x), SrgbToLin1(c.y), SrgbToLin1(c.z), c.w };
}
} // namespace

void Application::DrawDefaultColorSwatches(float barHeight) {
    (void)barHeight;
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    SyncDefaultStyleFromActive();

    // ui-unit tall, wider than tall (rectangular) — the same control height as
    // the bar's dropdowns, so it sits vertically centred like they do.
    const float uiU = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    const ImVec2 swSize(uiU * 1.6f, uiU);
    const float rnd = ds.GetFloat(Tok::S_CornerRadius_Control) * gs;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(
        ds.GetColor(Tok::S_Color_Border_Default));

    auto swatch = [&](const char* id, bool isFill) {
        ImGui::PushID(id);
        const ImVec2 mn = ImGui::GetCursorScreenPos();
        const ImVec2 mx(mn.x + swSize.x, mn.y + swSize.y);
        const bool none = isFill ? edit_.defaultFills.empty()
                                 : edit_.defaultStrokes.empty();
        if (ImGui::InvisibleButton("##sw", swSize))
            ImGui::OpenPopup("##pick");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip(isFill
                ? "Fill for new shapes / the selection (first fill of the stack)"
                : "Stroke for new shapes / the selection (first stroke of the stack)",
                ImGui::GetIO().MousePos);
        // Visual: the first entry's colour, or the "none" plate (white + red
        // diagonal).
        if (none) {
            dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), rnd);
            dl->AddLine(ImVec2(mn.x + 1.5f * gs, mx.y - 1.5f * gs),
                        ImVec2(mx.x - 1.5f * gs, mn.y + 1.5f * gs),
                        IM_COL32(220, 40, 40, 255), 2.0f * gs);
        } else {
            const Ink::Color c = isFill
                ? edit_.defaultFills.front().paint.color
                : edit_.defaultStrokes.front().paint.color;
            dl->AddRectFilled(mn, mx,
                ImGui::ColorConvertFloat4ToU32(LinToSrgb(c)), rnd);
        }
        dl->AddRect(mn, mx, border, rnd);

        if (ImGui::BeginPopup("##pick")) {
            const char* lbl = isFill ? "Fill Colour" : "Stroke Colour";
            ImVec4 cur(1, 1, 1, 1);
            if (!none)
                cur = LinToSrgb(isFill ? edit_.defaultFills.front().paint.color
                                       : edit_.defaultStrokes.front().paint.color);
            if (ImGui::ColorPicker4("##p", &cur.x,
                                    ImGuiColorEditFlags_NoSidePreview |
                                    ImGuiColorEditFlags_AlphaBar)) {
                if (isFill) {
                    if (edit_.defaultFills.empty())
                        edit_.defaultFills.push_back(Ink::Fill{});
                    edit_.defaultFills.front().paint.color = SrgbToLin(cur);
                    ApplyDefaultFillsEdit(lbl, false);
                } else {
                    if (edit_.defaultStrokes.empty()) {
                        Ink::Stroke s; s.width = 2.0;
                        edit_.defaultStrokes.push_back(s);
                    }
                    edit_.defaultStrokes.front().paint.color = SrgbToLin(cur);
                    ApplyDefaultStrokesEdit(lbl, false);
                }
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (isFill) ApplyDefaultFillsEdit(lbl, true);
                else        ApplyDefaultStrokesEdit(lbl, true);
            }
            if (!isFill && !edit_.defaultStrokes.empty()) {
                float w = (float)edit_.defaultStrokes.front().width;
                if (ImGui::DragFloat("Width", &w, 0.1f, 0.0f, 100.0f, "%.1f")) {
                    edit_.defaultStrokes.front().width = w;
                    ApplyDefaultStrokesEdit("Stroke Width", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    ApplyDefaultStrokesEdit("Stroke Width", true);
            }
            ImGui::Separator();
            if (ImGui::Button(isFill ? "No fill" : "No stroke")) {
                if (isFill) { edit_.defaultFills.clear();
                              ApplyDefaultFillsEdit("Remove Fills", true); }
                else        { edit_.defaultStrokes.clear();
                              ApplyDefaultStrokesEdit("Remove Strokes", true); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };

    swatch("##fillSw", true);
    ImGui::SameLine(0.0f, 4.0f * gs);
    swatch("##strokeSw", false);
}

// ── Snap widget: magnet toggle fused to a Snap dropdown ───────────────────────

static const char* kSnapModeNames[] = { "Increment", "Grid", "Vertex", "Edge",
                                         "Face", "Edge Center" };
static const char* kSnapBaseNames[] = { "Closest", "Pivot", "Median", "Active" };

void Application::DrawSnapWidget(ImVec2 pos, float widthPx) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    ImGui::SetCursorPos(pos);
    UI::DropdownConfig cfg;
    cfg.id = "##snap";
    cfg.triggerIcon = "background-dot-small";
    cfg.triggerLabel = kSnapModeNames[(int)edit_.snap.mode];
    UI::DropdownButton mag;
    mag.id = "magnet"; mag.icon = "background-dot-small"; mag.active = edit_.snap.enabled;
    mag.side = UI::DropdownButton::Side::Left;
    mag.tooltip = edit_.snap.enabled
        ? "Snapping ON (click to disable; Ctrl snaps one drag)"
        : "Snapping OFF (click to enable; hold Ctrl to snap a drag)";
    cfg.buttons.push_back(mag);

    const float bodyW = 230.0f * gs;
    const float bodyH = 300.0f * gs;
    cfg.menuSize = ImVec2(bodyW, bodyH);
    cfg.bodyDraw = [this, &ds, gs, bodyW]() {
        auto subtle = [&](const char* s) {
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted(s); ImGui::PopStyleColor();
        };
        // Snap To — every mode is live: Increment/Grid (grid) and Vertex/Edge/
        // Face/EdgeCenter (document geometry). See ViewportSnap.cpp.
        subtle("Snap To");
        {
            static const char* kSnapIcons[6] = {
                "background-dot-small", "grid-on", "line-start-square",
                "diagonal-line", "crop-free", "line-end-diamond" };
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapto");
            g.SetGrid({ bodyW }, std::vector<float>(6, cellH));
            for (int i = 0; i < 6; ++i) {
                UI::ButtonGroup::Cell c{};
                c.label = kSnapModeNames[i];
                c.icon  = kSnapIcons[i];
                c.col = 0; c.row = i;
                c.selected = ((int)edit_.snap.mode == i);
                c.enabled  = true;
                c.align = UI::ButtonGroup::Align::Left;
                g.AddCell(c);
            }
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex >= 0)
                edit_.snap.mode = (SnapSettings::Mode)r.clickedIndex;
        }
        ImGui::Separator();
        // Snap Base — a 4-cell group (functional: affects incremental rounding
        // base later; kept enabled to preserve the control surface).
        subtle("Snap Base");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 3.0f) / 4.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapbase");
            g.SetGrid({ cellW, cellW, cellW, cellW }, { cellH });
            for (int i = 0; i < 4; ++i)
                g.AddCell(kSnapBaseNames[i], i, 0, 1, 1, (int)edit_.snap.base == i);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex >= 0) edit_.snap.base = (SnapSettings::Base)r.clickedIndex;
        }
        ImGui::Separator();
        subtle("Affect");
        {
            const float cellW = (bodyW - ds.GetFloat(Tok::P_Spacing_100) * 2.0f) / 3.0f;
            const float cellH = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
            UI::ButtonGroup g("##snapaffect");
            g.SetGrid({ cellW, cellW, cellW }, { cellH });
            g.AddCell("Move",   0, 0, 1, 1, edit_.snap.affectMove);
            g.AddCell("Rotate", 1, 0, 1, 1, edit_.snap.affectRotate);
            g.AddCell("Scale",  2, 0, 1, 1, edit_.snap.affectScale);
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex == 0) edit_.snap.affectMove   = !edit_.snap.affectMove;
            if (r.clickedIndex == 1) edit_.snap.affectRotate = !edit_.snap.affectRotate;
            if (r.clickedIndex == 2) edit_.snap.affectScale  = !edit_.snap.affectScale;
        }
        ImGui::Separator();
        subtle("Increment");
        {
            float mv = (float)edit_.snap.moveIncrement;
            if (ImGui::DragFloat("Move##inc", &mv, 0.5f, 0.0f, 1000.0f, "%.1f"))
                edit_.snap.moveIncrement = mv;
            if (ImGui::DragFloat("Rotate##inc", &edit_.snap.rotIncrement, 0.5f,
                                 0.0f, 180.0f, "%.1f\xC2\xB0"))
                ;
        }
    };
    UI::DropdownResult r = UI::Dropdown(cfg);
    if (r.buttonClicked == 0) edit_.snap.enabled = !edit_.snap.enabled;
    (void)widthPx;
}

// ── The Viewport top bar ──────────────────────────────────────────────────────

void Application::BuildViewportTopBar(EditorState& st, EditorBar& bar) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    EditorState* pst = &st;

    // LEFT: Object / Edit / Line Mark mode + ruler space (greyed) + fill/stroke
    // swatches (+ the mark-shape picker while in Line Mark mode).
    const bool markMode = edit_.mode == EditorMode::LineMark;
    bar.left.width =
        (110.0f + 6.0f + 130.0f + 6.0f + 60.0f + (markMode ? 136.0f : 0.0f)) * gs;
    bar.left.draw = [this, gs, pst, markMode](ImVec2 pos, float bh) {
        ImGui::SetCursorPos(pos);
        const bool canEdit = edit_.active != Ink::kNullNode && project_.document &&
            project_.document->Find(edit_.active) &&
            project_.document->Find(edit_.active)->kind == Ink::NodeKind::Path;
        { UI::DropdownConfig cfg; cfg.id = "##editormode";
          cfg.triggerLabel = edit_.mode == EditorMode::Edit ? "Edit Mode"
                           : edit_.mode == EditorMode::LineMark ? "Line Mark Mode"
                                                                : "Object Mode";
          { UI::DropdownItem it; it.label = "Object Mode";
            it.tooltip = "Select and transform whole objects"; cfg.items.push_back(it); }
          { UI::DropdownItem it; it.label = "Edit Mode";
            it.tooltip = "Edit an object's points"; it.enabled = canEdit;
            cfg.items.push_back(it); }
          { UI::DropdownItem it; it.label = "Line Mark Mode";
            it.tooltip = "Place and edit marks along strokes (Shift+Tab)";
            cfg.items.push_back(it); }
          cfg.selectedIndex = edit_.mode == EditorMode::Edit ? 1
                            : edit_.mode == EditorMode::LineMark ? 2 : 0;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed) {
              if (r.selected == 0) SetEditorMode(EditorMode::Object);
              else if (r.selected == 1 && canEdit) SetEditorMode(EditorMode::Edit);
              else if (r.selected == 2) SetEditorMode(EditorMode::LineMark);
          }
        }
        // Ruler-space dropdown — the rulers/ruler-space feature is not on Ink
        // yet, so the control is present but GREYED (returns in a later pass).
        ImGui::SameLine(0.0f, 6.0f * gs);
        { UI::DropdownConfig cfg; cfg.id = "##rulerspace";
          cfg.triggerLabel = "Viewport rulers";
          { UI::DropdownItem it; it.label = "Viewport rulers"; it.enabled = false;
            cfg.items.push_back(it); }
          { UI::DropdownItem it; it.label = "Page rulers"; it.enabled = false;
            cfg.items.push_back(it); }
          cfg.selectedIndex = 0;
          UI::Dropdown(cfg);
        }
        ImGui::SameLine(0.0f, 8.0f * gs);
        DrawDefaultColorSwatches(bh);
        // The default OBJECT a click drops on the line: an SVG-marker shape
        // and whether it ADDS to or SUBTRACTS from the stroke. Phase, side,
        // offset and the full object list are edited in Properties.
        if (markMode) {
            ImGui::SameLine(0.0f, 8.0f * gs);
            // The placeable shapes + GAP (an opening cut in the line). While
            // Gap is picked the Subtract toggle disappears — a gap always
            // cuts. The picker's trailing CROSS clears the selection ("Empty
            // Mark" placeholder): clicks then drop an objectless mark.
            struct ShapeOpt { const char* name; Ink::MarkShape shape; };
            static const ShapeOpt kShapes[] = {
                { "Circle",      Ink::MarkShape::Circle },
                { "Rectangle",   Ink::MarkShape::Rectangle },
                { "Diamond",     Ink::MarkShape::Diamond },
                { "Triangle",    Ink::MarkShape::Triangle },
                { "Half Circle", Ink::MarkShape::HalfCircle },
                { "Gap",         Ink::MarkShape::Gap },
            };
            constexpr int kNShapes = 6;
            int shp = -1;
            for (int i = 0; i < kNShapes; ++i)
                if (!markPlaceEmpty_ && markPlaceShape_ == kShapes[i].shape)
                    shp = i;
            const bool gapPicked =
                !markPlaceEmpty_ && markPlaceShape_ == Ink::MarkShape::Gap;
            UI::DropdownConfig cfg; cfg.id = "##markshape";
            cfg.triggerIcon = "line-end-diamond";
            cfg.objectPicker = true;
            cfg.objectPickerHasValue = !markPlaceEmpty_;
            cfg.objectPickerNoEyedropper = true;   // clearable, but no pick
            cfg.placeholder = "Empty Mark";
            cfg.triggerLabel = shp >= 0 ? kShapes[shp].name : "";
            for (int i = 0; i < kNShapes; ++i) {
                UI::DropdownItem it; it.label = kShapes[i].name;
                cfg.items.push_back(it);
            }
            cfg.selectedIndex = shp;
            UI::DropdownResult r = UI::Dropdown(cfg);
            if (r.cleared) markPlaceEmpty_ = true;
            if (r.changed && r.selected >= 0 && r.selected < kNShapes) {
                markPlaceShape_ = kShapes[r.selected].shape;
                markPlaceEmpty_ = false;
            }
            if (!gapPicked) {
                // Subtract as an ICON TOGGLE (eraser): on = the object cuts
                // the stroke (dst-out) instead of fusing into it.
                ImGui::SameLine(0.0f, 6.0f * gs);
                auto& ds2 = DS::DesignSystem::Instance();
                const float ui = ds2.GetFloat(Tok::S_Size_ControlHeight) * gs;
                const float rnd2 = ds2.GetFloat(Tok::S_CornerRadius_Control) * gs;
                const ImVec2 mn = ImGui::GetCursorScreenPos();
                const ImVec2 mx(mn.x + ui, mn.y + ui);
                if (ImGui::InvisibleButton("##marksub", ImVec2(ui, ui)))
                    markPlaceSubtract_ = !markPlaceSubtract_;
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl2 = ImGui::GetWindowDrawList();
                const ImVec4 fill = markPlaceSubtract_
                    ? ds2.GetColor(Tok::S_Color_Accent_Default)
                    : ds2.GetColor(hov ? Tok::C_IconButton_BackgroundHover
                                       : Tok::C_IconButton_Background);
                dl2->AddRectFilled(mn, mx,
                                   ImGui::ColorConvertFloat4ToU32(fill), rnd2);
                auto& im = VectorGraphics::IconManager::Instance();
                if (im.HasIcon("ink-eraser")) {
                    const float isz = ui * 0.7f;
                    auto md = im.GetDefaultMetadata("ink-eraser");
                    if (!md.colorZones.empty())
                        md.colorZones[0].customColor =
                            ds2.GetColor(Tok::C_IconButton_Icon);
                    im.RenderIcon(dl2, "ink-eraser",
                                  ImVec2(mn.x + (ui - isz) * 0.5f,
                                         mn.y + (ui - isz) * 0.5f), isz, md);
                }
                if (hov)
                    UI::DrawTooltip("Subtract: the object cuts the stroke "
                                    "instead of fusing into it (Ctrl-click "
                                    "places an objectless dash tick)",
                                    ImGui::GetIO().MousePos);
            }
        }
        (void)pst;
    };

    // MIDDLE: Transform Orientation + Pivot Point + Snap widget.
    const float kOrientW = 130.0f * gs;
    const float kPivotW  = 190.0f * gs;
    const float kSnapW   = 150.0f * gs;
    const float kMidGap  = 6.0f * gs;
    bar.middle.width = kOrientW + kMidGap + kPivotW + kMidGap + kSnapW;
    bar.middle.draw = [this, kMidGap](ImVec2 pos, float) {
        ImGui::SetCursorPos(pos);
        static const char* kOrients[] = { "Global", "Local", "View", "Cursor", "Parent" };
        static const char* kOrientTips[] = {
            "Transform along the document axes",
            "Transform along the active object's own axes",
            "Transform along the view axes",
            "Transform along the 2D cursor's axes",
            "Transform along the active object's parent axes" };
        { UI::DropdownConfig cfg; cfg.id = "##orient"; cfg.triggerIcon = "crop-free";
          cfg.triggerLabel = kOrients[(int)edit_.orientation];
          for (int i = 0; i < 5; ++i) {
              UI::DropdownItem it; it.label = kOrients[i]; it.tooltip = kOrientTips[i];
              cfg.items.push_back(it);
          }
          cfg.selectedIndex = (int)edit_.orientation;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed && r.selected >= 0 && r.selected < 5)
              edit_.orientation = (TransformOrientation)r.selected;
        }
        ImGui::SameLine(0.0f, kMidGap);
        static const char* kPivots[] = { "Bounding Box Center", "2D Cursor",
            "Individual Origins", "Median Point", "Active Element" };
        { UI::DropdownConfig cfg; cfg.id = "##pivot"; cfg.triggerIcon = "crop-free";
          cfg.triggerLabel = kPivots[(int)edit_.pivot];
          for (int i = 0; i < 5; ++i) {
              UI::DropdownItem it; it.label = kPivots[i];
              cfg.items.push_back(it);
          }
          cfg.selectedIndex = (int)edit_.pivot;
          UI::DropdownResult r = UI::Dropdown(cfg);
          if (r.changed && r.selected >= 0 && r.selected < 5)
              edit_.pivot = (PivotMode)r.selected;
        }
        ImGui::SameLine(0.0f, kMidGap);
        DrawSnapWidget(ImGui::GetCursorPos(), 150.0f);
    };

    // RIGHT: overlay dropdown (icon-only). Page-layout / per-page / 2D-cursor /
    // metrics are not on Ink yet → shown greyed.
    const float h = ds.GetFloat(Tok::S_Size_ControlHeight) * gs;
    bar.right.width = h + 6.0f * gs;
    bar.right.draw = [this, gs, pst](ImVec2 pos, float) {
        auto& ds2 = DS::DesignSystem::Instance();
        ImGui::SetCursorPos(pos);
        const float bodyW = 232.0f * gs, bodyH = 400.0f * gs;
        const float cellH = ds2.GetFloat(Tok::S_Size_ControlHeight) * gs;
        UI::DropdownConfig ov;
        ov.id = "##viewportOverlay";
        ov.triggerIcon = "image-aspect-ratio";
        ov.triggerLabel = "";
        ov.menuSize = ImVec2(bodyW, bodyH);
        // Custom widgets throughout (token-styled checkboxes / button groups) —
        // no base ImGui controls.
        ov.bodyDraw = [this, &ds2, gs, bodyW, cellH, pst]() {
            auto subtle = [&](const char* s) {
                ImGui::PushStyleColor(ImGuiCol_Text, ds2.GetColor(Tok::S_Color_Text_Subtle));
                ImGui::TextUnformatted(s); ImGui::PopStyleColor();
            };
            // ── Rulers (any combination; default = top + left) ───────────────
            subtle("Rulers");
            UI::Checkbox("##rTop",    "Top",    &pst->rulerTop);
            UI::Checkbox("##rBottom", "Bottom", &pst->rulerBottom);
            UI::Checkbox("##rLeft",   "Left",   &pst->rulerLeft);
            UI::Checkbox("##rRight",  "Right",  &pst->rulerRight);
            ImGui::Dummy(ImVec2(0, 4.0f * gs));

            // ── Viewport unit (also here for when the rulers have no corner) ──
            subtle("Viewport unit");
            {
                static const char* kU[5] = { "Follow document", "Metric",
                                             "Imperial", "Typographic", "Pixel" };
                UI::ButtonGroup g("##vpunit");
                g.SetGrid({ bodyW }, std::vector<float>(5, cellH));
                for (int i = 0; i < 5; ++i)
                    g.AddCell(kU[i], 0, i, 1, 1, pst->docUnit == i);
                const UI::ButtonGroup::Result r = g.Render();
                if (r.clickedIndex >= 0) pst->docUnit = r.clickedIndex;
            }
            ImGui::Dummy(ImVec2(0, 4.0f * gs));

            // ── 2D cursor ────────────────────────────────────────────────────
            subtle("2D Cursor");
            UI::Checkbox("##show2d", "Show 2D cursor", &show2DCursor_);
            {
                const float sp = ds2.GetFloat(Tok::P_Spacing_100) * gs;
                const float cw = (bodyW - sp) * 0.5f;
                UI::ButtonGroup g("##cursorActs");
                g.SetGrid({ cw, cw }, { cellH });
                g.AddCell("Reset to origin",   0, 0, 1, 1, false);
                g.AddCell("Move to selection", 1, 0, 1, 1, false);
                const UI::ButtonGroup::Result r = g.Render();
                if (r.clickedIndex == 0) Action_Cursor2DToOrigin();
                if (r.clickedIndex == 1) Action_Cursor2DToSelection();
            }
            ImGui::Dummy(ImVec2(0, 4.0f * gs));

            // ── Print ────────────────────────────────────────────────────────
            // Both settings live on the DOCUMENT: the resolved colours sit in
            // one shared GPU paint table, so a per-viewport proof would mean
            // duplicating it. Proofing the whole app at once is what you want
            // anyway.
            if (project_.document) {
                (void)project_.document;
                // Per VIEWPORT: proofing one canvas leaves the others — and
                // the symbol vignettes — on the plain screen render. The
                // printing TECHNIQUE is not here: it is an output choice, and
                // it lives in Properties ▸ Document settings.
                subtle("Print (this viewport)");
                {
                    static const char* kPv[4] = { "Normal", "Overprint",
                                                  "Separations",
                                                  "Flattener preview" };
                    UI::ButtonGroup g("##printPreview");
                    g.SetGrid({ bodyW }, std::vector<float>(4, cellH));
                    const int cur = pst->printPreview;
                    for (int i = 0; i < 4; ++i)
                        g.AddCell(kPv[i], 0, i, 1, 1, cur == i);
                    const UI::ButtonGroup::Result r = g.Render();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        UI::DrawTooltip(
                            "What the canvas simulates. These are CHECKS, not "
                            "alternative renderings — on a well-set-up document "
                            "Normal and Overprint should agree.\n"
                            "Normal: the calibrated screen colours.\n"
                            "Overprint: the colours rebuilt from the inks each "
                            "plate actually lays.\n"
                            "Separations: the same, filtered to the channels "
                            "you keep — spot inks drop out, having no CMYK "
                            "plate of their own.\n"
                            "Flattener preview: MARKS the artwork that could "
                            "not go to a separation as it stands (translucent, "
                            "blended or "
                            "cutting).",
                            ImGui::GetIO().MousePos);
                    if (r.clickedIndex >= 0) pst->printPreview = r.clickedIndex;
                }
                // Which separations stay visible.
                if (pst->printPreview == (int)Ink::PrintPreview::Separations) {
                    static const char* kCh[4] = { "C", "M", "Y", "K" };
                    const std::uint8_t bits = (std::uint8_t)pst->printChannels;
                    const float cw = (bodyW - 3.0f * 2.0f * gs) * 0.25f;
                    UI::ButtonGroup g("##sepChans");
                    g.SetGrid(std::vector<float>(4, cw), { cellH });
                    for (int i = 0; i < 4; ++i)
                        g.AddCell(kCh[i], i, 0, 1, 1, (bits >> i) & 1);
                    const UI::ButtonGroup::Result r = g.Render();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        UI::DrawTooltip(
                            "Which separations stay visible. Switching one off "
                            "answers \"what is actually on that plate\" — the "
                            "way to catch artwork carrying an ink it should not.",
                            ImGui::GetIO().MousePos);
                    if (r.clickedIndex >= 0)
                        pst->printChannels = (int)(bits ^ (1u << r.clickedIndex));
                }
                ImGui::Dummy(ImVec2(0, 4.0f * gs));
            }

            // ── Overlays (not on Ink yet) ────────────────────────────────────
            subtle("Overlays");
            ImGui::BeginDisabled(true);
            static bool page = true, pages = true, metrics = false;
            UI::Checkbox("##ovPage",    "Page layout (later)",         &page);
            UI::Checkbox("##ovPages",   "Show pages (later)",          &pages);
            UI::Checkbox("##ovMetrics", "Performance metrics (later)", &metrics);
            ImGui::EndDisabled();
        };
        UI::Dropdown(ov);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip("Viewport overlays — rulers, unit, 2D cursor",
                            ImGui::GetIO().MousePos);
    };
}

// ── Shift+A Add menu ──────────────────────────────────────────────────────────

// The Add menu (Shift+A). CRITICAL: this must be called every frame from a
// STABLE, unconditional place (RenderViewport calls it after the canvas blit,
// not gated by hover) — a popup whose BeginPopup stops being called freezes.
// Action_OpenAddMenu opens the ImGui popup exactly once; here we only render it.
void Application::RenderAddMenu() {
    if (addMenuRequested_) {           // arm → open ONCE, in THIS window scope
        addMenuRequested_ = false;
        addMenuOpen_ = true;
        ImGui::OpenPopup("##addMenu");
    }
    if (!addMenuOpen_) return;

    // The legacy two-column split: SHAPES (filled primitives) and CURVES
    // (Bézier / NURBS / Poly, with their circle forms) as submenus. Every
    // entry SPAWNS a preset at the 2D cursor — interactive drawing lives on
    // the palette's Shape / Curve tools now (draw-on-create is the tools'
    // behaviour, no menu toggle). Free is the exception: a custom shape only
    // exists drawn, so it starts the pen.
    std::vector<UI::MenuEntry> entries;
    // The active module may REPLACE the menu wholesale (IOF: the ISOM symbol
    // catalogue instead of core primitives) or leave it to the core (subject
    // to the corePrimitivesAddMenu capability).
    const bool moduleMenu =
        activeModule_ && activeModule_->BuildAddMenu(entries);
    if (moduleMenu) {
        const bool open = UI::ContextMenu("##addMenu", addMenuPos_, entries, "Add");
        if (!open) addMenuOpen_ = false;
        return;
    }
    if (!activeCapabilities_.corePrimitivesAddMenu) { addMenuOpen_ = false; return; }
    auto leaf = [&](std::vector<UI::MenuEntry>& dst, const char* label,
                    const char* kind, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        e.onClick = [this, kind]() {
            SpawnShape(kind);
            addMenuOpen_ = false;
        };
        dst.push_back(std::move(e));
    };
    {
        UI::MenuEntry shapes; shapes.label = "Shapes"; shapes.icon = "shape-category";
        leaf(shapes.submenu, "Rectangle", "rect",     "Rectangle preset");
        leaf(shapes.submenu, "Ellipse",   "ellipse",  "Ellipse preset");
        leaf(shapes.submenu, "Triangle",  "triangle", "Triangle preset");
        {
            UI::MenuEntry e; e.label = "Free";
            e.tooltip = "Draw a free custom shape point by point (the pen)";
            e.onClick = [this]() { BeginPenDraw("free"); addMenuOpen_ = false; };
            shapes.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(shapes));
    }
    {
        UI::MenuEntry curves; curves.label = "Curves"; curves.icon = "bezier-curve";
        leaf(curves.submenu, "Bézier",        "curve",        "Open Bézier curve preset");
        leaf(curves.submenu, "Bézier Circle", "beziercircle", "Four-arc Bézier circle");
        leaf(curves.submenu, "NURBS Path",    "nurbs",        "Open NURBS path preset");
        leaf(curves.submenu, "NURBS Circle",  "nurbscircle",  "Exact rational-NURBS circle");
        leaf(curves.submenu, "Poly Line",     "poly",         "Straight polyline preset");
        entries.push_back(std::move(curves));
    }

    const bool open = UI::ContextMenu("##addMenu", addMenuPos_, entries, "Add");
    if (!open) addMenuOpen_ = false;
}

// The Shift+S snap pie — the legacy 2D adaptation of Blender's: the SELECTION
// snaps by each object's ORIGIN; the grid is the snap move increment.
// Rendered unconditionally like the Add menu.
void Application::RenderSnapPieMenu() {
    if (snapMenuRequested_) {
        snapMenuRequested_ = false;
        snapMenuOpen_ = true;
        ImGui::OpenPopup("##snapPie");
    }
    if (!snapMenuOpen_) return;
    const bool hasSel = !edit_.selection.empty();
    std::vector<UI::MenuEntry> entries;
    auto add = [&](const char* lbl, const char* tip, bool enabled, auto fn) {
        UI::MenuEntry e; e.label = lbl; e.tooltip = tip; e.enabled = enabled;
        e.onClick = [this, fn]{ fn(this); snapMenuOpen_ = false; };
        entries.push_back(std::move(e));
    };
    add("Selection to Cursor", "Move each selected object's origin to the 2D cursor",
        hasSel, [](Application* a) { a->Action_SelectionToCursor(); });
    add("Selection to Active", "Move the other selected origins onto the active one",
        edit_.selection.size() >= 2,
        [](Application* a) { a->Action_SelectionToActive(); });
    add("Selection to Grid", "Snap each selected origin to the nearest grid crossing",
        hasSel, [](Application* a) { a->Action_SelectionToGrid(); });
    add("Cursor to Active", "Place the 2D cursor on the active object's origin",
        edit_.active != Ink::kNullNode,
        [](Application* a) { a->Action_Cursor2DToActive(); });
    add("Cursor to Selected", "Place the 2D cursor at the selection's centre",
        hasSel, [](Application* a) { a->Action_Cursor2DToSelection(); });
    add("Cursor to World Origin", "Place the 2D cursor at (0, 0)",
        true, [](Application* a) { a->Action_Cursor2DToWorldOrigin(); });
    add("Cursor to Page Origin", "Place the 2D cursor at the page's top-left",
        true, [](Application* a) { a->Action_Cursor2DToOrigin(); });
    add("Cursor to Grid", "Snap the 2D cursor to the nearest grid crossing",
        true, [](Application* a) { a->Action_Cursor2DToGrid(); });
    const bool open = UI::ContextMenu("##snapPie", snapMenuPos_, entries, "Snap");
    if (!open) snapMenuOpen_ = false;
}

// The Edit-mode "Set Handle Type" menu (V) — restores the legacy vertex-type
// chooser. Rendered unconditionally like the Add menu.
void Application::RenderHandleTypeMenu() {
    if (handleMenuRequested_) {
        handleMenuRequested_ = false;
        handleMenuOpen_ = true;
        ImGui::OpenPopup("##handleMenu");
    }
    if (!handleMenuOpen_) return;
    std::vector<UI::MenuEntry> entries;
    auto add = [&](const char* lbl, const char* tip, int mode) {
        UI::MenuEntry e; e.label = lbl; e.tooltip = tip;
        e.onClick = [this, mode]{ Action_SetHandleType(mode); handleMenuOpen_ = false; };
        entries.push_back(std::move(e));
    };
    add("Free",     "Both handles move independently",                    0);
    add("Aligned",  "Handles stay collinear (lengths independent)",       1);
    add("Mirrored", "Handles keep equal length (directions independent)", 2);
    add("Aligned + Mirrored", "Handles stay collinear AND equal length",  3);
    add("Vector",   "Handles point at the neighbouring points (straight)",4);
    { UI::MenuEntry e; e.label = "Remove Handles";
      e.tooltip = "Delete both handles — a straight corner point";
      e.onClick = [this]{ Action_RemoveHandles(); handleMenuOpen_ = false; };
      entries.push_back(std::move(e)); }
    const bool open = UI::ContextMenu("##handleMenu", handleMenuPos_, entries, "Set Handle Type");
    if (!open) handleMenuOpen_ = false;
}

// The viewport right-click context menu. Same unconditional-render rule.
// viewportCtxNode_ is the object under the cursor (or null for empty space).
void Application::RenderViewportContextMenu() {
    if (viewportCtxRequested_) {        // arm → open ONCE, in THIS window scope
        viewportCtxRequested_ = false;
        viewportCtxOpen_ = true;
        ImGui::OpenPopup("##viewportCtx");
    }
    if (!viewportCtxOpen_) return;
    Ink::Document* doc = project_.document.get();
    if (!doc) { viewportCtxOpen_ = false; return; }

    std::vector<UI::MenuEntry> entries;
    // The menu is driven by the SELECTION (Blender): an object menu whenever
    // something is selected, the empty-canvas menu otherwise.
    const bool hasSel = !edit_.selection.empty();
    const bool onObject = hasSel && edit_.active != Ink::kNullNode &&
                          doc->Find(edit_.active);
    const bool editing = (edit_.mode == EditorMode::Edit);
    auto close = [this]{ viewportCtxOpen_ = false; };
    const char* title = editing ? "Vertex" : (onObject ? "Object" : "Viewport");

    if (editing) {
        // ── Edit-mode menu (acts on the selected vertices/handles) ──
        const bool hasElems = !edit_.elemSel.empty();
        { UI::MenuEntry e; e.label = "Set Handle Type"; e.enabled = hasElems;
          auto add = [&](const char* l, int m) {
              UI::MenuEntry s; s.label = l;
              s.onClick = [this, m, close]{ Action_SetHandleType(m); close(); };
              e.submenu.push_back(std::move(s)); };
          add("Free", 0); add("Aligned", 1); add("Mirrored", 2);
          add("Aligned + Mirrored", 3); add("Vector", 4);
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Remove Handles"; e.enabled = hasElems;
          e.onClick = [this, close]{ Action_RemoveHandles(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete Vertices"; e.shortcut = "X"; e.icon = "ink-eraser";
          e.enabled = hasElems;
          e.onClick = [this, close]{ Action_DeleteVertices(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Exit Edit Mode"; e.shortcut = "Tab";
          e.onClick = [this, close]{ Action_ExitEditMode(); close(); };
          entries.push_back(std::move(e)); }
    } else if (onObject) {
        // Acts on the SELECTION; `id` = the active object (its kind gates the
        // path-only entries).
        const Ink::NodeId id = edit_.active;
        const Ink::Node* n = doc->Find(id);
        const bool isPath = n->kind == Ink::NodeKind::Path;
        const bool inGroup = n->parent != Ink::kNullNode;
        { UI::MenuEntry e; e.label = "Enter Edit Mode"; e.shortcut = "Tab"; e.enabled = isPath;
          e.onClick = [this, close]{ Action_EnterEditMode(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate"; e.shortcut = "Ctrl D"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_DuplicateSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Duplicate Linked"; e.shortcut = "Alt D";
          e.tooltip = "Instance the selection (shared data, independent transform) "
                      "and grab the copies";
          e.enabled = hasSel;
          e.onClick = [this, close]{ Action_DuplicateLinked(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Group"; e.shortcut = "Ctrl G"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_GroupSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Ungroup"; e.shortcut = "Ctrl Alt G"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_UngroupSelection(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Select Group"; e.enabled = inGroup;
          e.tooltip = "Select the group this object belongs to";
          e.onClick = [this, close]{ Action_SelectGroup(); close(); };
          entries.push_back(std::move(e)); }
        // Parent ▸ — object parenting lives HERE (and on shortcuts), never on
        // the Outliner drag & drop (that is collection organisation only).
        { UI::MenuEntry pa; pa.label = "Parent";
          { UI::MenuEntry e; e.label = "Parent to Active"; e.shortcut = "Ctrl P";
            e.tooltip = "Parent the other selected objects to the active one "
                        "(world positions preserved)";
            e.enabled = edit_.selection.size() >= 2;
            e.onClick = [this, close]{ Action_ParentToActive(); close(); };
            pa.submenu.push_back(std::move(e)); }
          { UI::MenuEntry e; e.label = "Clear Parent"; e.shortcut = "Alt P";
            e.tooltip = "Detach from the object parent (keeps the world position)";
            bool anyParented = false;
            for (Ink::NodeId sid : edit_.selection)
                if (const Ink::Node* sn = doc->Find(sid);
                    sn && sn->parentId != Ink::kNullNode) { anyParented = true; break; }
            e.enabled = anyParented;
            e.onClick = [this, close]{
                OutlinerUnparent(std::vector<Ink::NodeId>(
                    edit_.selection.begin(), edit_.selection.end()));
                close();
            };
            pa.submenu.push_back(std::move(e)); }
          entries.push_back(std::move(pa)); }
        // Set Origin ▸ (legacy parity)
        { UI::MenuEntry so; so.label = "Set Origin"; so.enabled = hasSel;
          { UI::MenuEntry e; e.label = "Origin to Geometry";
            e.tooltip = "Move the origin to the geometry centre (geometry stays put)";
            e.onClick = [this, close]{ Action_OriginToGeometry(); close(); };
            so.submenu.push_back(std::move(e)); }
          { UI::MenuEntry e; e.label = "Geometry to Origin";
            e.tooltip = "Re-centre the geometry on the object's origin";
            e.onClick = [this, close]{ Action_GeometryToOrigin(); close(); };
            so.submenu.push_back(std::move(e)); }
          { UI::MenuEntry e; e.label = "Origin to 2D Cursor";
            e.tooltip = "Move the origin onto the 2D cursor (geometry stays put)";
            e.enabled = edit_.cursor2DValid;
            e.onClick = [this, close]{ Action_OriginTo2DCursor(); close(); };
            so.submenu.push_back(std::move(e)); }
          entries.push_back(std::move(so)); }
        { UI::MenuEntry e; e.label = "Apply Scale"; e.enabled = isPath;
          e.tooltip = "Bake the scale into the geometry";
          e.onClick = [this, close]{ Action_ApplyScale(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Delete"; e.shortcut = "X"; e.icon = "ink-eraser";
          e.enabled = hasSel;
          e.onClick = [this, close]{ Action_DeleteSelection(); close(); };
          entries.push_back(std::move(e)); }
    } else {
        // Empty space: the Add submenu + selection / cursor helpers.
        UI::MenuEntry add; add.label = "Add";
        auto leaf = [&](const char* label, const char* kind) {
            UI::MenuEntry e; e.label = label;
            e.onClick = [this, kind, close]{ SpawnShape(kind); close(); };
            add.submenu.push_back(std::move(e));
        };
        leaf("Rectangle", "rect"); leaf("Ellipse", "ellipse");
        leaf("Triangle", "triangle"); leaf("Bézier Curve", "curve");
        entries.push_back(std::move(add));
        { UI::MenuEntry e; e.label = "Select All"; e.shortcut = "A";
          e.onClick = [this, close]{ Action_SelectAll(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "Deselect All"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_DeselectAll(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "2D Cursor to Origin";
          e.onClick = [this, close]{ Action_Cursor2DToOrigin(); close(); };
          entries.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "2D Cursor to Selection"; e.enabled = hasSel;
          e.onClick = [this, close]{ Action_Cursor2DToSelection(); close(); };
          entries.push_back(std::move(e)); }
    }

    const bool open = UI::ContextMenu("##viewportCtx", viewportCtxPos_, entries, title);
    if (!open) viewportCtxOpen_ = false;
}

} // namespace App

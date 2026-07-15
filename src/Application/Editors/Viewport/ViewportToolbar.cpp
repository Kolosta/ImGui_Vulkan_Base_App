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

void Application::RenderToolPalette(ImVec2 origin, EditorState& st) {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    auto& tm = Shortcuts::Tools::ToolManager::Instance();

    // Build the item list from the ToolManager (data-driven — the palette
    // never drifts from the shortcut system). Edit-only tools are hidden in
    // Object Mode (legacy skip-logic).
    const std::string activeTool = tm.GetActiveTool();
    const bool editMode = (edit_.mode == EditorMode::Edit);
    std::vector<const Shortcuts::Tools::ToolDef*> defs;
    for (const auto* t : tm.GetAllTools()) {
        if (t->id == "tool.extrude" && !editMode) continue;
        defs.push_back(t);
    }
    if (defs.empty()) return;

    std::vector<UI::ToolPaletteItem> items;
    items.reserve(defs.size());
    for (const auto* t : defs) {
        UI::ToolPaletteItem it;
        it.icon = t->iconId;
        it.selected = (activeTool == t->id);
        it.tooltip = t->name;
        if (!t->actionIds.empty()) {
            const std::string s = sm.GetShortcutString(t->actionIds.front());
            if (!s.empty()) it.tooltip += "   (" + s + ")";
        }
        items.push_back(std::move(it));
    }

    DS::DesignSystem::ZoneStyle zone("viewport/tools", "Viewport tools");
    const UI::ToolPaletteResult r = UI::ToolPalette("##InkTools", origin, items);
    // Publish the rect so canvas hit-testing excludes the palette.
    st.overlayRects.push_back(ImVec4(r.rectMin.x, r.rectMin.y, r.rectMax.x, r.rectMax.y));
    if (r.clicked >= 0) Action_ActivateNamedTool(defs[(size_t)r.clicked]->id);
}

// ── Default fill / stroke swatches (new-shape style) ──────────────────────────

void Application::DrawDefaultColorSwatches(float barHeight) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float sw = barHeight * 0.62f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Fill swatch (filled disc).
    ImGui::PushID("##fillSw");
    if (ImGui::ColorButton("##fill", edit_.defaultFill,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(sw, sw)))
        ImGui::OpenPopup("##fillPick");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        UI::DrawTooltip("Default fill for new shapes", ImGui::GetIO().MousePos);
    if (ImGui::BeginPopup("##fillPick")) {
        ImGui::ColorPicker4("##fillp", &edit_.defaultFill.x,
                            ImGuiColorEditFlags_NoSidePreview);
        ImGui::Checkbox("Fill enabled", &edit_.defaultFillEnabled);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    (void)dl;

    ImGui::SameLine(0.0f, 4.0f * gs);
    // Stroke swatch.
    ImGui::PushID("##strokeSw");
    if (ImGui::ColorButton("##stroke", edit_.defaultStroke,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(sw, sw)))
        ImGui::OpenPopup("##strokePick");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        UI::DrawTooltip("Default stroke for new shapes", ImGui::GetIO().MousePos);
    if (ImGui::BeginPopup("##strokePick")) {
        ImGui::ColorPicker4("##strokep", &edit_.defaultStroke.x,
                            ImGuiColorEditFlags_NoSidePreview);
        ImGui::Checkbox("Stroke enabled", &edit_.defaultStrokeEnabled);
        float w = (float)edit_.defaultStrokeWidth;
        if (ImGui::DragFloat("Width", &w, 0.1f, 0.0f, 100.0f, "%.1f"))
            edit_.defaultStrokeWidth = w;
        ImGui::EndPopup();
    }
    ImGui::PopID();
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
        // Snap To — only Increment is implemented on Ink; the rest are greyed.
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
                c.enabled  = (i == 0);   // Increment only (Lot 8); others later
                c.align = UI::ButtonGroup::Align::Left;
                g.AddCell(c);
            }
            UI::ButtonGroup::Result r = g.Render();
            if (r.clickedIndex == 0) edit_.snap.mode = SnapSettings::Mode::Increment;
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
            static const char* kShapes[] = { "Circle", "Rectangle", "Diamond" };
            UI::DropdownConfig cfg; cfg.id = "##markshape";
            cfg.triggerIcon = "line-end-diamond";
            const int shp = std::min((int)markPlaceShape_, 2);
            cfg.triggerLabel = kShapes[shp];
            for (int i = 0; i < 3; ++i) {
                UI::DropdownItem it; it.label = kShapes[i];
                cfg.items.push_back(it);
            }
            cfg.selectedIndex = shp;
            UI::DropdownResult r = UI::Dropdown(cfg);
            if (r.changed && r.selected >= 0 && r.selected < 3)
                markPlaceShape_ = (Ink::MarkShape)r.selected;
            ImGui::SameLine(0.0f, 6.0f * gs);
            bool sub = markPlaceSubtract_;
            if (UI::CheckboxBox("##marksub", &sub)) markPlaceSubtract_ = sub;
            if (ImGui::IsItemHovered())
                UI::DrawTooltip("Subtract: the object cuts the stroke instead "
                                "of fusing into it (Ctrl-click places an "
                                "objectless dash tick)",
                                ImGui::GetIO().MousePos);
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
    bar.right.draw = [this, gs](ImVec2 pos, float) {
        auto& ds2 = DS::DesignSystem::Instance();
        ImGui::SetCursorPos(pos);
        const float bodyW = 220.0f * gs, bodyH = 200.0f * gs;
        UI::DropdownConfig ov;
        ov.id = "##viewportOverlay";
        ov.triggerIcon = "image-aspect-ratio";
        ov.triggerLabel = "";
        ov.menuSize = ImVec2(bodyW, bodyH);
        ov.bodyDraw = [this, &ds2]() {
            ImGui::PushStyleColor(ImGuiCol_Text, ds2.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted("2D Cursor"); ImGui::PopStyleColor();
            ImGui::Checkbox("Show 2D cursor", &show2DCursor_);
            if (ImGui::Button("Reset to origin"))     Action_Cursor2DToOrigin();
            if (ImGui::Button("Move to selection"))   Action_Cursor2DToSelection();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ds2.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextUnformatted("Overlays"); ImGui::PopStyleColor();
            ImGui::BeginDisabled(true);
            bool page = true, pages = true, metrics = false;
            ImGui::Checkbox("Page layout (later)", &page);
            ImGui::Checkbox("Show pages (later)", &pages);
            ImGui::Checkbox("Performance metrics (later)", &metrics);
            ImGui::EndDisabled();
        };
        UI::Dropdown(ov);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            UI::DrawTooltip("Viewport overlays (2D cursor, page layout, metrics)",
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
    // (Bézier / NURBS / Poly, with their circle forms) as submenus.
    std::vector<UI::MenuEntry> entries;
    // A SHAPE / circle leaf: when Draw on Create is on it arms a drag-to-place
    // (the canvas builds the shape in the dragged box); otherwise it spawns a
    // preset at the 2D cursor.
    auto leaf = [&](std::vector<UI::MenuEntry>& dst, const char* label,
                    const char* kind, const char* tip) {
        UI::MenuEntry e; e.label = label; e.tooltip = tip;
        e.onClick = [this, kind]() {
            if (addDrawOnCreate_) pendingDrawKind_ = kind;
            else                  SpawnShape(kind);
            addMenuOpen_ = false;
        };
        dst.push_back(std::move(e));
    };
    {
        UI::MenuEntry shapes; shapes.label = "Shapes"; shapes.icon = "shape-category";
        leaf(shapes.submenu, "Rectangle", "rect",
             "Rectangle (drag to place when Draw on Create is on)");
        leaf(shapes.submenu, "Ellipse",   "ellipse",
             "Ellipse (drag to place when Draw on Create is on)");
        leaf(shapes.submenu, "Triangle",  "triangle",
             "Triangle (drag to place when Draw on Create is on)");
        // FREE: a custom-shaped path drawn point by point with the pen — ALWAYS
        // starts the pen (as if Draw on Create were on, even when it is off).
        {
            UI::MenuEntry e; e.label = "Free";
            e.tooltip = "Draw a free custom shape point by point (the pen); "
                        "always uses draw-on-create";
            e.onClick = [this]() { BeginPenDraw("free"); addMenuOpen_ = false; };
            shapes.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(shapes));
    }
    {
        // Open curve kinds honour "Draw on Create" via the PEN (per-point
        // construction); the circle forms use the drag-to-place box like the
        // shapes.
        auto curveLeaf = [&](std::vector<UI::MenuEntry>& dst, const char* label,
                             const char* kind, const char* tip) {
            UI::MenuEntry e; e.label = label; e.tooltip = tip;
            e.onClick = [this, kind]() {
                if (addDrawOnCreate_) BeginPenDraw(kind);
                else                  SpawnShape(kind);
                addMenuOpen_ = false;
            };
            dst.push_back(std::move(e));
        };
        UI::MenuEntry curves; curves.label = "Curves"; curves.icon = "bezier-curve";
        curveLeaf(curves.submenu, "Bézier",     "curve",
                  "Add an open Bézier curve (pen when Draw on Create is on)");
        leaf(curves.submenu, "Bézier Circle",   "beziercircle",
             "Four-arc Bézier circle (drag to place when Draw on Create is on)");
        curveLeaf(curves.submenu, "NURBS Path", "nurbs",
                  "Add an open NURBS path (pen when Draw on Create is on)");
        leaf(curves.submenu, "NURBS Circle",    "nurbscircle",
             "Exact rational-NURBS circle (drag to place when Draw on Create "
             "is on)");
        curveLeaf(curves.submenu, "Poly Line",  "poly",
                  "Add a straight polyline (pen when Draw on Create is on)");
        entries.push_back(std::move(curves));
    }
    // Draw-on-create: when ON, picking a CURVE arms the PEN instead of
    // spawning a ready-made object — click places corner anchors, click-drag
    // pulls symmetric handles, Enter/double-click finishes, first-anchor
    // click closes, Esc cancels (the legacy pen workflow).
    {
        UI::MenuEntry t;
        t.label = addDrawOnCreate_ ? "Draw on Create: On" : "Draw on Create: Off";
        t.tooltip = "When on, curve entries start the pen tool at the cursor "
                    "instead of spawning a preset object";
        t.onClick = [this] { addDrawOnCreate_ = !addDrawOnCreate_; addMenuOpen_ = false; };
        entries.push_back(std::move(t));
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

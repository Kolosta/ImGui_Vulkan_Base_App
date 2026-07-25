#include "IofMappingModule.h"

#include "IofStyles.h"
#include "PropertiesRows.h"          // pr:: row helpers (core styling)
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/PopupMenu.h>
#include <UI/Widgets/ScrollArea.h>
#include <DesignSystem/DesignSystem.h>
#include <Ink/Document/Document.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  IOF module UI — the Shift+A catalogue menu, the Viewport "Symbols" side-
//  panel tab, the Symbol Viewer editor and the Map Settings editor. All the
//  symbol imagery is the REAL Vulkan pipeline (ModuleHost::NodePreviewTexture /
//  CanvasPreviewTexture on the library specimens); all widgets/styling follow
//  the core (pr:: rows, UI panels, design tokens).
// ─────────────────────────────────────────────────────────────────────────────

namespace App::Modules::IofMapping {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

float Gs() { return DS::DesignSystem::Instance().GetGlobalScale(); }
ImVec4 Col(Tok t, ImVec4 fb) {
    try { return DS::DesignSystem::Instance().GetColor(t); } catch (...) { return fb; }
}
float Flt(Tok t, float fb) {
    try { return DS::DesignSystem::Instance().GetFloat(t); } catch (...) { return fb; }
}
ImVec2 V2(Tok t, ImVec2 fb) {
    try { return DS::DesignSystem::Instance().GetVec2(t); } catch (...) { return fb; }
}

// A square vignette tile: white card + the real-pipeline symbol texture +
// hover/selected outline. Returns true on click.
bool VignetteTile(const char* id, ImTextureID tex, float side, bool selected,
                  const char* tooltip) {
    const ImVec2 mn = ImGui::GetCursorScreenPos();
    const ImVec2 mx(mn.x + side, mn.y + side);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = Flt(Tok::S_CornerRadius_Control, 4.0f) * Gs();
    dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), rnd);
    if (tex)
        dl->AddImageRounded(tex, mn, mx, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, rnd);
    const ImVec4 border =
        selected ? Col(Tok::S_Color_Accent_Default, ImVec4(1, 0.6f, 0.2f, 1))
        : hov    ? Col(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))
                 : Col(Tok::S_Color_Border_Default, ImVec4(0.4f, 0.4f, 0.4f, 1));
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(border), rnd, 0,
                (selected || hov) ? 2.0f : 1.0f);
    if (hov && tooltip && *tooltip)
        UI::DrawTooltip(tooltip, ImGui::GetIO().MousePos);
    return clicked;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Shift+A — the ISOM catalogue replaces the core primitives.
// ─────────────────────────────────────────────────────────────────────────────

bool IofMappingModule::BuildAddMenu(std::vector<UI::MenuEntry>& out) {
    for (const IofGroup& g : IofCatalogue()) {
        UI::MenuEntry grp;
        grp.label = g.name;
        for (const IofElement& e : g.elements) {
            UI::MenuEntry item;
            item.label   = IofElementLabel(e);
            item.tooltip = e.desc;
            item.onClick = [this, &e]() { SelectSymbol(e); };
            grp.submenu.push_back(std::move(item));
        }
        out.push_back(std::move(grp));
    }
    // Non-cartographic LAYOUT objects (the frame, sponsor boxes, titles…) —
    // plain editable shapes routed to the Extras layer.
    {
        UI::MenuEntry lay;
        lay.label = "Map layout";
        auto add = [&](const char* label, const char* kind, const char* tip) {
            UI::MenuEntry e; e.label = label; e.tooltip = tip;
            e.onClick = [this, kind]() { SelectLayoutTool(kind); };
            lay.submenu.push_back(std::move(e));
        };
        add("Frame / outline", "line", "Draw an editable outline (map frame)");
        add("Filled area",     "area", "Draw an editable filled area");
        out.push_back(std::move(lay));
    }
    return true;   // override the core menu
}

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport side panel — the "Symbols" tab (catalogue browser).
// ─────────────────────────────────────────────────────────────────────────────

void IofMappingModule::ViewportSidePanelTabs(std::vector<UI::SidePanelTab>& out) {
    UI::SidePanelTab tab;
    tab.name = "Symbols";
    tab.draw = [this](ImVec2 cMin, ImVec2 cMax) { DrawSymbolsTab(cMin, cMax); };
    out.push_back(std::move(tab));
}

// A symbol vignette in the CORE preview style: an AREA swatch fills the tile
// (small pad), a POINT glyph is fitted, a LINE sample is short + zoomed (the
// same reading as the core fill/stroke vignettes). Draws it + click select.
void IofMappingModule::DrawSymbolCell(const IofElement& e, float side) {
    ImGui::PushID(e.code);
    const std::uint64_t lib = LibNode(e.code);
    // Fills (areas) reach the tile edges; LINES zoom in tight (a thin ISOM
    // stroke must read); points keep a small centred margin.
    const float pad = e.type == IofType::Area ? 0.02f
                    : e.type == IofType::Line ? 0.04f : 0.14f;
    ImTextureID tex = 0;
    if (lib && Host())
        tex = (ImTextureID)Host()->NodePreviewTexture(lib, (int)side, pad);
    char tip[256];
    std::snprintf(tip, sizeof tip, "%s\n%s", IofElementLabel(e).c_str(), e.desc);
    // Highlights what is ARMED, not what the Symbol Viewer happens to show —
    // the viewer is an independent browser (see DrawSymbolViewer).
    if (VignetteTile("##sym", tex, side, armedSymbol_ == e.code, tip))
        SelectSymbol(e);
    ImGui::PopID();
}

void IofMappingModule::DrawSymbolsTab(ImVec2 cMin, ImVec2 cMax) {
    // The N side panel ALREADY wraps this callback in a scroll area and
    // measures the cursor advance to auto-fit its height — so we draw straight
    // into the current window (no nested scroll: that pins the height and
    // shows a permanent scrollbar).
    (void)cMax;
    const float gs = Gs();
    ImGui::SetCursorScreenPos(cMin);
    const float side = 44.0f * gs;
    const float gap  = 5.0f * gs;
    for (const IofGroup& g : IofCatalogue()) {
        UI::PanelConfig pc;
        pc.id = g.name; pc.label = g.name; pc.defaultOpen = true;
        if (UI::BeginPanel(pc).open) {
            const float availW = ImGui::GetContentRegionAvail().x;
            const int perRow = std::max(1, (int)((availW + gap) / (side + gap)));
            int i = 0;
            for (const IofElement& e : g.elements) {
                if (i % perRow) ImGui::SameLine(0.0f, gap);
                ++i;
                DrawSymbolCell(e, side);
            }
        }
        UI::EndPanel();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport tool palette — six ISOM THEME buttons (each a symbol vignette; a
//  right-click opens a grid to pick the theme's symbol).
// ─────────────────────────────────────────────────────────────────────────────

namespace {
// The six theme buttons, each covering one or more catalogue groups.
struct ThemeButton { const char* label; std::vector<const char*> groups; };
const std::vector<ThemeButton>& ThemeButtons() {
    static const std::vector<ThemeButton> kThemes = {
        { "Landforms",              { "Landforms" } },
        { "Rock and boulders",      { "Rock and boulders" } },
        { "Water and marsh",        { "Water and marsh" } },
        { "Vegetation",             { "Vegetation" } },
        { "Man-made features",      { "Man-made features" } },
        { "Course & technical",     { "Course overprint", "Technical symbols" } },
    };
    return kThemes;
}
// The elements of a theme button, in catalogue order.
std::vector<const IofElement*> ThemeElements(const ThemeButton& t) {
    std::vector<const IofElement*> out;
    for (const IofGroup& g : IofCatalogue())
        for (const char* gn : t.groups)
            if (std::strcmp(g.name, gn) == 0)
                for (const IofElement& e : g.elements) out.push_back(&e);
    return out;
}
}  // namespace

void IofMappingModule::DrawToolButtons(ImVec2 origin, float size,
                                       std::vector<ImVec4>& outRects) {
    const float gs = Gs();
    const float gap = 4.0f * gs;
    const auto& themes = ThemeButtons();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rnd = Flt(Tok::S_CornerRadius_Control, 4.0f) * gs;

    for (int i = 0; i < (int)themes.size(); ++i) {
        const ThemeButton& t = themes[(std::size_t)i];
        const ImVec2 mn(origin.x, origin.y + (float)i * (size + gap));
        const ImVec2 mx(mn.x + size, mn.y + size);
        ImGui::SetCursorScreenPos(mn);
        ImGui::PushID(1000 + i);
        const bool clicked = ImGui::InvisibleButton("##themeBtn",
                                                    ImVec2(size, size));
        const bool hov = ImGui::IsItemHovered();
        const bool rclick = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        outRects.push_back(ImVec4(mn.x, mn.y, mx.x, mx.y));

        const IofElement* e = IofFindByCode(themeSel_[i]);
        // Fall back to the theme's first symbol if the stored code drifted.
        if (!e) {
            auto els = ThemeElements(t);
            if (!els.empty()) { e = els.front(); themeSel_[i] = e->code; }
        }
        // Selected = THIS theme tool is the ACTIVE viewport tool (a real tool).
        char toolId[16];
        std::snprintf(toolId, sizeof toolId, "iof.theme%d", i);
        const bool sel = Host() && Host()->ActiveTool() == toolId;
        // White card + real-pipeline vignette + selection/hover outline.
        dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255), rnd);
        if (e && Host()) {
            const float pad = e->type == IofType::Area ? 0.02f
                            : e->type == IofType::Line ? 0.04f : 0.14f;
            if (auto tex = Host()->NodePreviewTexture(LibNode(e->code),
                                                      (int)size, pad))
                dl->AddImageRounded((ImTextureID)tex, mn, mx, ImVec2(0, 0),
                                    ImVec2(1, 1), IM_COL32_WHITE, rnd);
        }
        const ImVec4 border =
            sel   ? Col(Tok::S_Color_Accent_Default, ImVec4(1, 0.6f, 0.2f, 1))
            : hov ? Col(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1))
                  : Col(Tok::S_Color_Border_Default, ImVec4(0.4f, 0.4f, 0.4f, 1));
        dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(border), rnd, 0,
                    (sel || hov) ? 2.0f : 1.0f);
        // Corner triangle — EXACT core multi-tool geometry (t=6, inset=2.5),
        // but coloured with the tool-button BACKGROUND so it reads on the white
        // vignette (inverse of the core's light-on-dark triangle).
        {
            const float tt = 6.0f * gs, inset = 2.5f * gs;
            const ImVec2 c(mx.x - inset, mx.y - inset);
            dl->AddTriangleFilled(
                ImVec2(c.x - tt, c.y), ImVec2(c.x, c.y - tt), c,
                ImGui::ColorConvertFloat4ToU32(
                    Col(Tok::C_IconButton_Background,
                        ImVec4(0.2f, 0.2f, 0.22f, 1))));
        }
        if (hov && e) {
            char tip[128];
            std::snprintf(tip, sizeof tip, "%s\n%s\nRight-click: pick a symbol",
                          t.label, IofElementLabel(*e).c_str());
            UI::DrawTooltip(tip, ImGui::GetIO().MousePos);
        }
        if (clicked && e) ActivateThemeTool(i);
        if (rclick) ImGui::OpenPopup("##themeMenu");

        // The theme's symbol grid (vignette + code + name). The popup carries the
        // window padding token: without it the grid sits flush against the frame.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            V2(Tok::C_Window_Padding, ImVec2(8, 8)));
        const bool themeMenuOpen = ImGui::BeginPopup("##themeMenu");
        ImGui::PopStyleVar();
        if (themeMenuOpen) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                Col(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1)));
            ImGui::TextUnformatted(t.label);
            ImGui::PopStyleColor();
            ImGui::Separator();
            const float cell = 34.0f * gs;
            int col = 0;
            for (const IofElement* se : ThemeElements(t)) {
                if (col++ % 6) ImGui::SameLine(0.0f, 3.0f * gs);
                ImGui::PushID(se->code);
                ImTextureID tex = 0;
                if (Host()) {
                    const float pad = se->type == IofType::Area ? 0.02f
                                    : se->type == IofType::Line ? 0.04f : 0.14f;
                    tex = (ImTextureID)Host()->NodePreviewTexture(
                        LibNode(se->code), (int)cell, pad);
                }
                if (VignetteTile("##pick", tex, cell, themeSel_[i] == se->code,
                                 IofElementLabel(*se).c_str())) {
                    themeSel_[i] = se->code;
                    ActivateThemeTool(i);   // set the tool + (re)arm the symbol
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Editors — Symbol Viewer + Map Settings.
// ─────────────────────────────────────────────────────────────────────────────

void IofMappingModule::OnRegister(ModuleContext& ctx) {
    {
        EditorDescriptor d;
        d.id = "iof.symbolviewer";
        d.name = "Symbol Viewer";
        d.icon = "shape-category";
        d.column = 0;
        d.themeScope = "editors";
        d.wrapInScroll = false;      // it manages its own panes
        d.contentInset = false;
        d.draw = [this](ImVec2 size, EditorState&) { DrawSymbolViewer(size); };
        ctx.editors.Register(std::move(d));
    }
    {
        EditorDescriptor d;
        d.id = "iof.mapsettings";
        d.name = "Map Settings";
        d.icon = "settings";
        d.column = 0;
        d.themeScope = "editors";
        d.draw = [this](ImVec2, EditorState&) { DrawMapSettings(); };
        ctx.editors.Register(std::move(d));
    }
}

void IofMappingModule::DrawSymbolViewer(ImVec2 size) {
    const float gs = Gs();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float splitW = 6.0f * gs;
    viewerGridW_ = std::clamp(viewerGridW_, 140.0f * gs,
                              std::max(160.0f * gs, size.x * 0.5f));

    // ── LEFT: the vignette grid (all symbols, grouped) ───────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    if (UI::BeginScroll("##iofViewerGrid", ImVec2(viewerGridW_, size.y))) {
        const float side = 52.0f * gs, gap = 5.0f * gs;
        for (const IofGroup& g : IofCatalogue()) {
            UI::PanelConfig pc;
            pc.id = g.name; pc.label = g.name; pc.defaultOpen = true;
            if (UI::BeginPanel(pc).open) {
                const float availW = ImGui::GetContentRegionAvail().x;
                const int perRow =
                    std::max(1, (int)((availW + gap) / (side + gap)));
                int i = 0;
                for (const IofElement& e : g.elements) {
                    if (i % perRow) ImGui::SameLine(0.0f, gap);
                    ++i;
                    ImGui::PushID(e.code);
                    const std::uint64_t lib = LibNode(e.code);
                    const float pad = e.type == IofType::Area ? 0.02f
                                    : e.type == IofType::Line ? 0.04f : 0.14f;
                    ImTextureID tex = 0;
                    if (lib && Host())
                        tex = (ImTextureID)Host()->NodePreviewTexture(
                            lib, (int)side, pad);
                    if (VignetteTile("##v", tex, side, viewerSel_ == e.code,
                                     IofElementLabel(e).c_str())) {
                        viewerSel_ = e.code;
                        viewerZoom_ = -1.0;   // re-fit the canvas
                    }
                    ImGui::PopID();
                }
            }
            UI::EndPanel();
        }
    }
    UI::EndScroll();

    // ── Splitter (drag to resize the grid pane) ──────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(origin.x + viewerGridW_, origin.y));
    ImGui::InvisibleButton("##iofViewerSplit", ImVec2(splitW, size.y));
    if (ImGui::IsItemActive())
        viewerGridW_ += ImGui::GetIO().MouseDelta.x;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // ── RIGHT: the example canvas (zoom/pan, real pipeline) + description ────
    const IofElement* e = IofFindByCode(viewerSel_);
    const float rightX = origin.x + viewerGridW_ + splitW;
    const float rightW = std::max(60.0f, size.x - viewerGridW_ - splitW);
    const float infoH  = std::min(size.y * 0.35f, 150.0f * gs);
    const float canvasH = std::max(40.0f, size.y - infoH);
    ImGui::SetCursorScreenPos(ImVec2(rightX, origin.y));
    if (e && Host()) {
        const std::uint64_t lib = LibNode(e->code);
        double bb[4];
        if (lib && Host()->NodeDocBounds(lib, bb)) {
            // Fit on selection change / first draw.
            if (viewerZoom_ <= 0.0) {
                const double w = std::max(1e-3, bb[2] - bb[0]);
                const double h = std::max(1e-3, bb[3] - bb[1]);
                viewerZoom_ = std::min((double)rightW * 0.8 / w,
                                       (double)canvasH * 0.8 / h);
                viewerPanX_ = (bb[0] + bb[2]) * 0.5 - rightW * 0.5 / viewerZoom_;
                viewerPanY_ = (bb[1] + bb[3]) * 0.5 - canvasH * 0.5 / viewerZoom_;
            }
            const ImVec2 cMin(rightX, origin.y);
            const ImVec2 cMax(rightX + rightW, origin.y + canvasH);
            ImGui::InvisibleButton("##iofViewerCanvas",
                                   ImVec2(rightW, canvasH));
            const bool hovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();
            // Drag to pan (LMB or MMB); wheel to zoom about the cursor.
            if (ImGui::IsItemActive() &&
                (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
                viewerPanX_ -= io.MouseDelta.x / viewerZoom_;
                viewerPanY_ -= io.MouseDelta.y / viewerZoom_;
            }
            if (hovered && io.MouseWheel != 0.0f) {
                const double mx = io.MousePos.x - cMin.x;
                const double my = io.MousePos.y - cMin.y;
                const double docX = viewerPanX_ + mx / viewerZoom_;
                const double docY = viewerPanY_ + my / viewerZoom_;
                const double f = io.MouseWheel > 0 ? 1.2 : 1.0 / 1.2;
                viewerZoom_ = std::clamp(viewerZoom_ * f, 0.05, 4000.0);
                viewerPanX_ = docX - mx / viewerZoom_;
                viewerPanY_ = docY - my / viewerZoom_;
            }
            const ImTextureID tex = (ImTextureID)Host()->CanvasPreviewTexture(
                lib, 1u, (int)rightW, (int)canvasH, viewerPanX_, viewerPanY_,
                viewerZoom_);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float rnd = Flt(Tok::S_CornerRadius_Control, 4.0f) * gs;
            dl->AddRectFilled(cMin, cMax, IM_COL32(255, 255, 255, 255), rnd);
            if (tex)
                dl->AddImageRounded(tex, cMin, cMax, ImVec2(0, 0), ImVec2(1, 1),
                                    IM_COL32_WHITE, rnd);
            dl->AddRect(cMin, cMax,
                        ImGui::ColorConvertFloat4ToU32(
                            Col(Tok::S_Color_Border_Default,
                                ImVec4(0.4f, 0.4f, 0.4f, 1))), rnd);
        }
        // ── Info block ──
        ImGui::SetCursorScreenPos(ImVec2(rightX, origin.y + canvasH + 4.0f * gs));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        if (ImGui::BeginChild("##iofViewerInfo", ImVec2(rightW, infoH - 8.0f * gs),
                              0, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                Col(Tok::S_Color_Text_Default, ImVec4(0.9f, 0.9f, 0.9f, 1)));
            ImGui::TextUnformatted(IofElementLabel(*e).c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text,
                Col(Tok::S_Color_Text_Subtle, ImVec4(0.65f, 0.65f, 0.65f, 1)));
            const char* typeName =
                e->type == IofType::Point ? "Point symbol"
              : e->type == IofType::Line  ? "Line symbol"
              : e->type == IofType::Area  ? "Area symbol" : "Text symbol";
            ImGui::Text("%s  ·  %s%s", typeName, LayerName(e->layer),
                        e->northLocked ? "  ·  north-locked" : "");
            ImGui::Spacing();
            ImGui::TextWrapped("%s", e->desc);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();   // the grid's ChildBg push
}

void IofMappingModule::DrawMapSettings() {
    static const char* kScaleNames[] = { "1:15 000", "1:10 000", "1:7 500",
                                         "1:5 000", "1:4 000" };
    UI::PanelConfig pc;
    pc.id = "##iofMap"; pc.label = "Map"; pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        int idx = std::clamp(scaleIndex_, 0, kIofScaleCount - 1);
        if (pr::DropdownRow("Scale", kScaleNames, kIofScaleCount, &idx) &&
            idx != scaleIndex_) {
            scaleIndex_ = idx;
            // Re-skin the library at the new scale: placed instances follow
            // live (they reference the specimen groups).
            SeedLibrary(/*rebuildExisting=*/true);
            if (Host()) {
                Host()->MarkDirty();
                Host()->LogInfoAction(std::string("Map scale ") +
                                      kScaleNames[idx]);
            }
        }
        int ci = contourInterval_;
        if (pr::DragInt("Contour interval", &ci, 0.1f, 1, 25))
            contourInterval_ = ci;
    }
    UI::EndPanel();
}

}  // namespace App::Modules::IofMapping

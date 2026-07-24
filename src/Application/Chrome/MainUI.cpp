#include "Application.h"
#include "PngWrite.h"
#include <Shell/ShellIntegration.h>
#include <SDL3/SDL.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Chrome/StatusBar.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/Checkbox.h>
#include <UI/Widgets/PopupMenu.h>     // UI::DrawTooltip
#include <UI/Widgets/ScrollArea.h>    // UI::BeginScroll / EndScroll
#include <UI/Widgets/SidePanel.h>     // UI::EditorSidePanel
#include <imgui_internal.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

namespace App {
// ── Full-screen layout container ──────────────────────────────────────────────

void Application::RenderMainLayout() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    // The custom title bar occupies the top `titleBarHeightPx_` of the window.
    // It is a plain ImGui window pinned at vp->Pos, so the layout must use the
    // FULL window rect (Pos/Size), not the work-area, and start just below the
    // bar — otherwise it overlaps the editors underneath.
    const float topInset = titleBarHeightPx_;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topInset));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - topInset));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus            |
        ImGuiWindowFlags_NoScrollbar           |
        ImGuiWindowFlags_NoScrollWithMouse     |
        ImGuiWindowFlags_NoDocking;

    ImGuiWindowFlags layoutFlags = kFlags;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::Begin("##MainLayout", nullptr, layoutFlags);
    ImGui::PopStyleVar(3);

    // Reserve space for the bottom status bar. ImGui inserts ItemSpacing.y
    // between ##LayoutBody and the status-bar child, so subtract it too —
    // otherwise the status bar is pushed ItemSpacing.y past the window bottom
    // and its content clips below the screen edge.
    const float statusBarHeight = UI::StatusBar::Height();
    const float itemSpacingY    = ImGui::GetStyle().ItemSpacing.y;
    const float layoutHeight =
        ImGui::GetWindowHeight() - statusBarHeight - itemSpacingY;

    // NoBackground: this container is the app canvas BEHIND the editor zones —
    // the gap between zones must show the app base surface (the host
    // ##MainLayout window bg = component.window.background → app base), not the
    // generic child surface. Painting a ChildBg here would tint the inter-zone
    // gaps with the child colour.
    if (ImGui::BeginChild("##LayoutBody",
                          ImVec2(0.0f, layoutHeight),
                          false,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoBackground)) {
        // Blender-style fixed 3-zone layout (no native docking UX). The tool
        // palette is no longer a global left strip — it floats inside the
        // Viewport editor. Each zone renders the editor it was assigned.
        zoneLayout_.Render();
    }
    ImGui::EndChild();

    RenderStatusBar();

    ImGui::End();
}

void Application::RenderStatusBar() {
    UI::StatusBar::Render(kVersion);
}

// ── Core editor registration ─────────────────────────────────────────────────
// Register the built-in editors into the EditorRegistry once at startup. Each
// descriptor's draw/topBar call the existing Render* methods. Modules add their
// own editors the same way (see ModuleAPI). This replaces the old per-kind
// switch in the layout: a zone now draws whatever id it holds.
void Application::RegisterCoreEditors() {
    auto& reg = EditorRegistry::Instance();

    // Viewport — the Ink vector canvas + the editing loop (docs/Ink/ROADMAP.md
    // Lot 8). Draws its own chrome edge-to-edge (no scroll wrap, no content
    // inset) and carries the editor top bar (mode/orientation/pivot/snap +
    // overlay). Controls whose Ink feature has not landed yet are greyed.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Viewport; d.name = "Viewport"; d.icon = "image";
        d.column = 0; d.themeScope = "editors/viewport";
        d.switchAction = "editor.viewport";
        d.wrapInScroll = false; d.contentInset = false;
        d.draw = [this](ImVec2 sz, EditorState& st) { RenderViewport(sz, st); };
        d.topBar = [this](EditorState& st, EditorBar& bar) {
            BuildViewportTopBar(st, bar);
        };
        reg.Register(std::move(d));
    }

    // Outliner — object organisation trees on the Ink model (Lot 9): a Layers
    // view (pages → layer trees, z-order/visibility) and a Collections view.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Outliner; d.name = "Outliner"; d.icon = "checklist";
        d.column = 2; d.themeScope = "editors/outliner";
        d.switchAction = "editor.outliner";
        // No content inset: the selection bands run flush to the editor's left
        // edge (Blender-style). The editor owns its OWN overlay scrollbar
        // (BeginScroll, needed by the row culling) — do NOT let the zone wrap it
        // in a second one, which would reserve an empty right gutter.
        d.contentInset = false;
        d.wrapInScroll = false;
        d.draw   = [this](ImVec2, EditorState& st) { RenderOutliner(st); };
        d.topBar = [this](EditorState& st, EditorBar& bar) { BuildOutlinerTopBar(st, bar); };
        reg.Register(std::move(d));
    }

    // Properties — the active object's transform + unified style (Lot 9).
    {
        EditorDescriptor d;
        d.id = CoreEditor::Properties; d.name = "Properties"; d.icon = "settings";
        d.column = 2; d.themeScope = "editors/properties";
        d.switchAction = "editor.properties";
        // Owns its own overlay scrollbar (BeginScroll inside RenderProperties) —
        // don't double-wrap (that reserved the spurious empty right gutter).
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderProperties(st); };
        d.topBar = [this](EditorState& st, EditorBar& bar) {
            BuildPropertiesTopBar(st, bar);
        };
        reg.Register(std::move(d));
    }

    // Stroke editor — the stroke stack of the WHOLE selection (or, with no
    // selection, the default strokes used for new objects). Fill editor: same
    // for fills. Both reuse the Properties Paint stack UI (PaintEditors.cpp).
    {
        EditorDescriptor d;
        d.id = CoreEditor::StrokeEd; d.name = "Stroke"; d.icon = "draw";
        d.column = 2; d.themeScope = "editors/properties";
        d.draw = [this](ImVec2, EditorState& st) { RenderStrokeEditor(st); };
        reg.Register(std::move(d));
    }
    {
        EditorDescriptor d;
        d.id = CoreEditor::FillEd; d.name = "Fill"; d.icon = "colorize";
        d.column = 2; d.themeScope = "editors/properties";
        d.draw = [this](ImVec2, EditorState& st) { RenderFillEditor(st); };
        reg.Register(std::move(d));
    }
    // Strokes & Fills — the active shape's paint pieces as the ONE stack they
    // are painted in, so a stroke can be dragged under a fill.
    {
        EditorDescriptor d;
        d.id = CoreEditor::PaintStack; d.name = "Strokes & Fills";
        d.icon = "align-justify-center";
        d.column = 2; d.themeScope = "editors/properties";
        d.switchAction = "editor.paintstack";
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderPaintStackEditor(st); };
        reg.Register(std::move(d));
    }

    // Timeline — placeholder strip with the reusable side panel demo.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Timeline; d.name = "Timeline"; d.icon = "find-replace";
        d.column = 1; d.themeScope = "editors/timeline";
        d.switchAction = "editor.timeline";
        d.wrapInScroll = false;
        d.draw = [this](ImVec2 sz, EditorState& st) {
            ImGui::TextDisabled("Timeline");
            ImGui::TextDisabled("(animation timeline — later)");
            ImVec2 cMn = ImGui::GetWindowPos();
            ImVec2 cMx(cMn.x + sz.x, cMn.y + sz.y);
            std::vector<UI::SidePanelTab> tabs;
            const char* names[] = { "Tool", "Curves", "Modifiers" };
            for (const char* nm : names) {
                UI::SidePanelTab t; t.name = nm;
                std::string label = nm;
                t.draw = [label](ImVec2 a, ImVec2) {
                    ImGui::SetCursorScreenPos(ImVec2(a.x + 10, a.y + 10));
                    ImGui::TextDisabled("%s panel (demo)", label.c_str());
                };
                tabs.push_back(std::move(t));
            }
            if (ImGui::IsWindowHovered() && ImGui::IsKeyPressed(ImGuiKey_N, false) &&
                !ImGui::GetIO().WantTextInput)
                st.sidePanel.stage = (st.sidePanel.stage == 2) ? 0 : 2;
            UI::EditorSidePanel("##timelineSide", cMn, cMx, st.sidePanel, tabs);
        };
        reg.Register(std::move(d));
    }

    // Dev Panel — live debug data (undo/redo lists, document/selection state).
    {
        EditorDescriptor d;
        d.id = CoreEditor::DevPanels; d.name = "Dev Panel"; d.icon = "draw";
        d.column = 2; d.themeScope = "editors/devPanels";
        d.switchAction = "editor.devPanels";
        d.draw = [this](ImVec2, EditorState&) { RenderDevDataEditor(); };
        reg.Register(std::move(d));
    }

    // Info — live action feed (Blender info-log style). Manages its own scroll.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Info; d.name = "Info"; d.icon = "format-align-left";
        d.column = 2; d.themeScope = "editors/info";
        d.switchAction = "editor.info";
        // The feed draws the shared zebra rows, which run edge to edge: an
        // editor inset would leave a strip of panel down the left of every
        // stripe.
        d.wrapInScroll = false; d.contentInset = false;
        d.draw = [this](ImVec2, EditorState&) { RenderInfoEditor(); };
        reg.Register(std::move(d));
    }

    // Node Graph — the ACTIVE object's Compositing Graph, shown/edited
    // through the generic UI::NodeGraph widget (docs/Ink/NODE_GRAPH.md §5,
    // ROADMAP Lot 13). Follows edit_.active automatically (Blender-style —
    // no manual "open" step, same pattern as Properties). Own pan/zoom
    // canvas, no scroll wrap/inset (mirrors Viewport).
    {
        EditorDescriptor d;
        d.id = CoreEditor::NodeGraph; d.name = "Node Graph"; d.icon = "polyline";
        d.column = 0; d.themeScope = "editors/nodegraph";
        d.switchAction = "editor.nodegraph";
        d.wrapInScroll = false; d.contentInset = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderNodeGraphEditor(st); };
        reg.Register(std::move(d));
    }

    // Palette — the document colour table: named colours any paint may
    // follow, each optionally carrying its CMYK, its place in the plate
    // stack and whether it overprints (Editors/Palette/Palette.cpp).
    {
        EditorDescriptor d;
        d.id = CoreEditor::Palette; d.name = "Palette"; d.icon = "format-color-text";
        d.column = 2; d.themeScope = "editors/outliner";
        // Flush bands and its OWN overlay scrollbar, like the Outliner: the
        // zone must add neither an inset nor a second scroll area, or the rows
        // end up with a margin on every side.
        d.contentInset = false;
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderPalette(st); };
        reg.Register(std::move(d));
    }

    // Colour Usage — every colour the document paints with, in print
    // order, expandable to the objects and the exact pieces using it.
    {
        EditorDescriptor d;
        d.id = CoreEditor::ColorUsage; d.name = "Colour Usage";
        d.icon = "checklist";
        d.column = 2; d.themeScope = "editors/outliner";
        // Same deal as the Outliner: flush bands and its OWN overlay scrollbar,
        // so the zone must not wrap it in a second one (that reserved gutter is
        // the stray right margin).
        d.contentInset = false;
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderColorUsage(st); };
        reg.Register(std::move(d));
    }
}

// (The .acu thumbnail generation — offscreen render + PNG encode into the
// project — was part of the old engine stack; it returns with Ink's headless
// render path in ROADMAP Lot 10.)

namespace {
// Pack several RGBA sizes into a single PNG-embedded .ico (ICONDIR + entries).
// Windows (Vista+) accepts PNG image data inside .ico for any size.
void WriteIco(const std::string& path,
              const std::vector<std::pair<int, std::vector<uint8_t>>>& pngs) {
    std::vector<uint8_t> out;
    auto u16 = [&](uint16_t v){ out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v){ for (int i=0;i<4;++i) out.push_back((uint8_t)(v >> (i*8))); };
    // ICONDIR.
    u16(0); u16(1); u16((uint16_t)pngs.size());
    // Entries (image data starts after the dir).
    uint32_t offset = 6 + 16 * (uint32_t)pngs.size();
    for (const auto& [sz, png] : pngs) {
        out.push_back(sz >= 256 ? 0 : (uint8_t)sz);   // width (0 = 256)
        out.push_back(sz >= 256 ? 0 : (uint8_t)sz);   // height
        out.push_back(0);  // palette
        out.push_back(0);  // reserved
        u16(1);            // colour planes
        u16(32);           // bpp
        u32((uint32_t)png.size());
        u32(offset);
        offset += (uint32_t)png.size();
    }
    for (const auto& [sz, png] : pngs) { (void)sz; out.insert(out.end(), png.begin(), png.end()); }
    std::ofstream f(path, std::ios::binary);
    if (f) f.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
}
} // namespace

void Application::RegisterShellIntegration() {
#ifdef _WIN32
    const char* base = SDL_GetBasePath();
    if (!base) return;
    std::string exeDir = base;
    std::string icoPath = exeDir + "carto_acu.ico";
    std::string dllPath = exeDir + "acu_thumbs.dll";

    // Build the .ico from the logo (original colours) at a few sizes — only if
    // it isn't already there (cheap idempotency; delete the file to refresh).
    {
        std::ifstream exists(icoPath, std::ios::binary);
        if (!exists.good()) {
            auto& im = VectorGraphics::IconManager::Instance();
            std::string svg = exeDir + "resources/icons/logo_carto.svg";
            const int sizes[] = { 16, 32, 48, 256 };
            std::vector<std::pair<int, std::vector<uint8_t>>> pngs;
            for (int s : sizes) {
                std::vector<uint8_t> rgba;
                if (!im.RasterizeSvgFile(svg, s, s, rgba)) continue;
                std::vector<uint8_t> png;
                if (App::png::EncodeRGBA(rgba.data(), s, s, png))
                    pngs.emplace_back(s, std::move(png));
            }
            if (!pngs.empty()) WriteIco(icoPath, pngs);
        }
    }

    ShellReg::EnsureRegistered(icoPath, dllPath);
#endif
}

// Build an SDL_Surface (RGBA32) from the logo SVG at `px`, owning a copy of the
// pixels (SDL_CreateSurfaceFrom does NOT copy, so we duplicate into the surface).
namespace {
SDL_Surface* LogoSurface(const std::string& svgPath, int px) {
    std::vector<uint8_t> rgba;
    if (!VectorGraphics::IconManager::Instance().RasterizeSvgFile(svgPath, px, px, rgba))
        return nullptr;
    if ((int)rgba.size() < px * px * 4) return nullptr;
    // Make a surface that owns its pixels (so the temporary `rgba` can die).
    SDL_Surface* s = SDL_CreateSurface(px, px, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);
    for (int y = 0; y < px; ++y)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + (size_t)y * s->pitch,
                    rgba.data() + (size_t)y * px * 4, (size_t)px * 4);
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    return s;
}
} // namespace

void Application::SetWindowIconFromLogo() {
    if (!window_) return;
    const char* base = SDL_GetBasePath();
    if (!base) return;
    const std::string svg = std::string(base) + "resources/icons/logo_carto.svg";

    // Primary icon at a large size; Windows scales it for the taskbar / Alt-Tab.
    SDL_Surface* icon = LogoSurface(svg, 256);
    if (!icon) return;
    // Crisper small sizes for the taskbar/title (SDL picks the best alternate).
    for (int px : { 16, 24, 32, 48, 64 })
        if (SDL_Surface* alt = LogoSurface(svg, px))
            SDL_AddSurfaceAlternateImage(icon, alt);   // icon keeps a reference

    SDL_SetWindowIcon(window_, icon);
    // SDL copies what it needs into the platform icon on SetWindowIcon, so the
    // surfaces (and their alternates) can be freed now.
    SDL_DestroySurface(icon);
}

void Application::RenderMainContent() {
    // The demo/test panels moved to the floating "Dev Test Window". This
    // central area is the viewport host; the full 3-zone layout + rulers +
    // floating tools are built in BLOC 2. Placeholder until then.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::BeginChild("##MainContent", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    ImGui::TextDisabled("Viewport");
    ImGui::TextDisabled("(3-zone layout, rulers and floating tools: next step)");
    ImGui::EndChild(); // ##MainContent
}

// All former main-area panels, gathered into one unique, non-dockable
// floating window — same organisation as before (chevron sections).

} // namespace App

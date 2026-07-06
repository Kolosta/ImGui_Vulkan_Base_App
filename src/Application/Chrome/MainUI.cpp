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

    // Viewport — the vector canvas (placeholder until the Ink engine lands —
    // docs/Ink/ROADMAP.md Lot 1). Draws its own chrome edge-to-edge: no scroll
    // wrap, no content inset. The editor top bar (mode/pivot/snap/pages) was
    // part of the old editing stack and returns with the Ink editing loop.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Viewport; d.name = "Viewport"; d.icon = "image";
        d.column = 0; d.themeScope = "editors/viewport";
        d.switchAction = "editor.viewport";
        d.wrapInScroll = false; d.contentInset = false;
        d.draw = [this](ImVec2 sz, EditorState& st) { RenderViewport(sz, st); };
        reg.Register(std::move(d));
    }

    // Outliner — object organisation trees (placeholder until Ink Lot 9).
    {
        EditorDescriptor d;
        d.id = CoreEditor::Outliner; d.name = "Outliner"; d.icon = "checklist";
        d.column = 2; d.themeScope = "editors/outliner";
        d.switchAction = "editor.outliner";
        // No content inset: the zebra stripes / selection bands run flush to the
        // editor's left edge (Blender-style).
        d.contentInset = false;
        d.draw = [this](ImVec2, EditorState& st) { RenderOutliner(st); };
        reg.Register(std::move(d));
    }

    // Properties — selected object's properties.
    {
        EditorDescriptor d;
        d.id = CoreEditor::Properties; d.name = "Properties"; d.icon = "settings";
        d.column = 2; d.themeScope = "editors/properties";
        d.switchAction = "editor.properties";
        d.draw = [this](ImVec2, EditorState&) { RenderProperties(); };
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
        d.wrapInScroll = false;
        d.draw = [this](ImVec2, EditorState&) { RenderInfoEditor(); };
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

#include "Application.h"
#include "ModuleRegistry.h"
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <cstdio>
#include <string>

// stb_image lives here as the single translation unit that defines the
// implementation. It decodes ANY raster format (JPG/PNG/BMP/…) into RGBA,
// which we then upload to a Vulkan texture via IconManager.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace App {

namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

static const char* kGitHubUrl =
    "https://github.com/Kolosta/ImGui_Vulkan_Base_App";

namespace {

// One splash "menu" row, styled like a Dropdown popup item (see UI/Dropdown.cpp):
// a rounded hover fill, an icon inset by the row padding, then the label. Drawn on
// the window draw list with a manual hit-test, so it matches the dropdown exactly.
// `width` is the row's clickable width. Returns true on RELEASE over the row (the
// activation only fires if the press ALSO started on the row — a press that drags
// off and releases elsewhere, or a drag that ends on the row, never triggers it).
bool SplashMenuRow(ImDrawList* dl, const char* icon, const char* label,
                   float width, const char* tooltip = nullptr) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    const float rowH    = ImGui::GetTextLineHeightWithSpacing();
    const float radius  = ds.GetFloat(Tok::C_Menu_CornerRadius) * gs;
    ImVec2      pad     = ds.GetVec2(Tok::C_Dropdown_Padding);  pad.x *= gs;
    const float iconSz  = ImGui::GetTextLineHeight();
    const float gap     = 6.0f * gs;
    const ImVec4 fgV    = ds.GetColor(Tok::C_Dropdown_Text);
    const ImU32  fg     = ImGui::ColorConvertFloat4ToU32(fgV);
    const ImU32  hovBg  = ImGui::ColorConvertFloat4ToU32(ds.GetColor(Tok::C_Menu_ItemHoverBg));

    const ImVec2 r0 = ImGui::GetCursorScreenPos();
    const ImVec2 r1(r0.x + width, r0.y + rowH);
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                         io.MousePos.y >= r0.y && io.MousePos.y <= r1.y;

    if (hovered) {
        dl->AddRectFilled(r0, r1, hovBg, radius);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    }

    float ix = r0.x + pad.x;
    if (icon && *icon) {
        auto& im = VectorGraphics::IconManager::Instance();
        auto md = im.GetDefaultMetadata(icon);
        for (auto& z : md.colorZones) z.customColor = fgV;
        im.RenderIcon(dl, icon, ImVec2(ix, r0.y + (rowH - iconSz) * 0.5f), iconSz, md);
        ix += iconSz + gap;
    }
    ImVec2 lts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(ix, r0.y + (rowH - lts.y) * 0.5f), fg, label);

    // Reserve the row so ImGui advances the cursor (rows stack vertically).
    ImGui::Dummy(ImVec2(width, rowH));

    // Activate on RELEASE, and only if the PRESS started on this row too — so a
    // click-drag that began elsewhere never triggers it. io.MouseClickedPos[0]
    // holds where the current left-button press began.
    const ImVec2 pp = io.MouseClickedPos[0];
    const bool pressedHere = pp.x >= r0.x && pp.x <= r1.x &&
                             pp.y >= r0.y && pp.y <= r1.y;
    return hovered && pressedHere && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
}

// File-name stem (no directory, no extension) for a recent-file path.
std::string PathStem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    size_t dot   = path.find_last_of('.');
    size_t end   = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}

} // namespace

// Decode resources/images/splashscreen.jpg (relative to the exe CWD, like the
// fonts) and upload it once as a Vulkan texture reused by the splash screen.
void Application::LoadSplashTexture() {
    if (splashTex_) return;                 // already loaded
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load("resources/images/splashscreen.jpg",
                                  &w, &h, &ch, 4);   // force RGBA
    if (!px) {
        std::printf("[splash] could not load splashscreen image: %s\n",
                    stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return;
    }
    auto tex = VectorGraphics::IconManager::Instance()
                   .CreateVulkanTextureFromRGBA(px, w, h);
    stbi_image_free(px);
    splashTex_  = tex.textureId;
    splashTexW_ = w;
    splashTexH_ = h;
}

// ── Blender-style start screen ────────────────────────────────────────────────
void Application::RenderSplash() {
    if (!showSplash_) return;
    if (!splashTex_) LoadSplashTexture();

    DS::DesignSystem::ComponentScope _cs("Splash");
    auto& ds = DS::DesignSystem::Instance();
    auto& im = VectorGraphics::IconManager::Instance();
    const float gs = ds.GetGlobalScale();

    // Splash size: image width (capped), image keeps its aspect; body below.
    const float splashW = (splashTexW_ > 0)
        ? std::min((float)splashTexW_, 720.0f * gs) : 640.0f * gs;
    const float imgH = (splashTexW_ > 0)
        ? splashW * (float)splashTexH_ / (float)splashTexW_ : 200.0f * gs;
    const float bodyH = 220.0f * gs;
    const float splashH = imgH + bodyH;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(splashW, splashH));

    const float rnd = ds.GetFloat(Tok::C_Window_CornerRadius) * gs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rnd);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ds.GetColor(Tok::C_Splash_Background));

    ImGui::Begin("##Splash", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoSavedSettings);

    ImVec2 win0 = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── Top image ─────────────────────────────────────────────────────────────
    // Rounded TOP corners only (matching the window radius) so the image never
    // overflows the rounded container; the bottom stays square so it meets the
    // body flush. ImGui::Image can't round, so we blit via AddImageRounded.
    const ImVec2 imgMin = win0;
    const ImVec2 imgMax(win0.x + splashW, win0.y + imgH);
    if (splashTex_) {
        dl->AddImageRounded(splashTex_, imgMin, imgMax, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, rnd, ImDrawFlags_RoundCornersTop);
        // Reserve the image area in layout (we drew it manually on the draw list).
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Dummy(ImVec2(splashW, imgH));
    } else {
        dl->AddRectFilled(imgMin, imgMax,
                          ImGui::ColorConvertFloat4ToU32(
                              ds.GetColor(Tok::S_Color_Background_Layer2)),
                          rnd, ImDrawFlags_RoundCornersTop);
    }
    // App logo top-left (large) + version top-right, painted over the image.
    {
        const float logoSz = 48.0f * gs;
        const float m = 12.0f * gs;
        // ORIGINAL SVG colours (like the title bar): don't force a scheme or
        // overwrite the per-zone colours — that is what broke the rendering.
        auto md = im.GetDefaultMetadata("logo_carto");
        md.scheme = VectorGraphics::IconColorScheme::Original;
        im.RenderIcon(dl, "logo_carto", ImVec2(win0.x + m, win0.y + m), logoSz, md);

        // Version sits over the LIGHT image → dark, token-driven colour.
        ImVec2 vts = ImGui::CalcTextSize(kVersion);
        dl->AddText(ImVec2(win0.x + splashW - vts.x - m, win0.y + m),
                    ImGui::ColorConvertFloat4ToU32(ds.GetColor(Tok::C_Splash_VersionText)),
                    kVersion);
    }

    // ── Body: New File presets · Recent files · Modules (3 columns) ───────────
    ImGui::SetCursorPos(ImVec2(16.0f * gs, imgH + 14.0f * gs));
    if (ImGui::BeginTable("##splashCols", 3, ImGuiTableFlags_None,
                          ImVec2(splashW - 32.0f * gs, bodyH - 60.0f * gs))) {
        const float colW = (splashW - 32.0f * gs) / 3.0f - 8.0f * gs;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("New File");
        ImGui::PopStyleColor();
        // New File presets, each opening a different zone layout (after the
        // unsaved-changes guard). Styled like Dropdown popup items.
        struct Preset { const char* icon; const char* label; LayoutPreset preset; };
        const Preset presets[] = {
            { "new",       "General", LayoutPreset::General },
            { "image",     "Layout",  LayoutPreset::Layout  },
            { "checklist", "Data",    LayoutPreset::Data    },
        };
        for (const Preset& p : presets) {
            if (SplashMenuRow(dl, p.icon, p.label, colW)) {
                showSplash_ = false;
                RequestNewFile(p.preset);
            }
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Recent Files");
        ImGui::PopStyleColor();
        if (recentFiles_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextDisabled("No recent files");
            ImGui::PopStyleColor();
        } else {
            for (const std::string& path : recentFiles_) {
                std::string stem = PathStem(path);
                if (SplashMenuRow(dl, "open", stem.c_str(), colW, path.c_str())) {
                    // Open via the same async path as Action_OpenFile: stash the
                    // path; ProcessPendingFileOp loads it on the main thread.
                    {
                        std::lock_guard<std::mutex> lk(pendingFile_.mtx);
                        pendingFile_.kind = 1;
                        pendingFile_.path = path;
                    }
                    showSplash_ = false;
                }
            }
        }

        // ── Modules column: open a module specialisation of the app. ──
        ImGui::TableSetColumnIndex(2);
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("Modules");
        ImGui::PopStyleColor();
        const auto& mods = Modules::ModuleRegistry::Instance().All();
        if (mods.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
            ImGui::TextDisabled("No modules");
            ImGui::PopStyleColor();
        } else {
            for (const auto& mod : mods) {
                const Modules::ModuleInfo info = mod->Info();
                const char* icon = info.icon.empty() ? "checklist" : info.icon.c_str();
                if (SplashMenuRow(dl, icon, info.name.c_str(), colW,
                                  info.description.c_str())) {
                    showSplash_ = false;
                    RequestOpenModule(info.id);
                }
            }
        }
        ImGui::EndTable();
    }

    // ── Bottom: GitHub link ────────────────────────────────────────────────────
    ImGui::SetCursorPos(ImVec2(16.0f * gs, splashH - 26.0f * gs));
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::C_Splash_Link));
    ImGui::TextUnformatted("GitHub: Kolosta/ImGui_Vulkan_Base_App");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        // Open on RELEASE over the label, and only if the press began on it too —
        // a plain text isn't an active ImGui item, so test the rect manually.
        ImVec2 lr0 = ImGui::GetItemRectMin(), lr1 = ImGui::GetItemRectMax();
        ImVec2 pp  = ImGui::GetIO().MouseClickedPos[0];
        bool pressedHere = pp.x >= lr0.x && pp.x <= lr1.x &&
                           pp.y >= lr0.y && pp.y <= lr1.y;
        if (pressedHere && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            SDL_OpenURL(kGitHubUrl);
    }

    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                                          ImGuiHoveredFlags_ChildWindows);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Click anywhere outside the splash (or Esc) dismisses it — EXCEPT on the
    // very frame it (re)appeared: the click that chose "Show splash screen" in
    // the logo menu is still registering this frame and would dismiss it
    // instantly (the bug where re-opening flashed for one frame).
    if (splashJustOpened_) {
        splashJustOpened_ = false;            // consume; dismissing resumes next frame
    } else if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) ||
               ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showSplash_ = false;
    }
}

// ── "About Carto" popup ───────────────────────────────────────────────────────
void Application::RenderAbout() {
    if (showAbout_) { ImGui::OpenPopup("About Carto"); showAbout_ = false; }

    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ds.GetColor(Tok::S_Color_Background_Layer1));
    ImGui::PushStyleColor(ImGuiCol_Text,    ds.GetColor(Tok::S_Color_Text_Default));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ds.GetVec2(Tok::C_Window_Padding));

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About Carto", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Carto");
        ImGui::Separator();
        ImGui::Text("Version %s", kVersion);
        ImGui::Spacing();
        ImGui::TextWrapped("A design-system demonstrator built with Dear ImGui, "
                           "SDL3 and Vulkan.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::C_Splash_Link));
        ImGui::TextUnformatted("GitHub: Kolosta/ImGui_Vulkan_Base_App");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            // Open on release over the label (press must have started on it too).
            ImVec2 lr0 = ImGui::GetItemRectMin(), lr1 = ImGui::GetItemRectMax();
            ImVec2 pp  = ImGui::GetIO().MouseClickedPos[0];
            bool pressedHere = pp.x >= lr0.x && pp.x <= lr1.x &&
                               pp.y >= lr0.y && pp.y <= lr1.y;
            if (pressedHere && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                SDL_OpenURL(kGitHubUrl);
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// ── "Unsaved changes" guard for splash New File presets ───────────────────────
// Save / Don't Save / Cancel before discarding a dirty project. Save uses the
// synchronous Action_SaveFile when a path exists; otherwise it opens Save-As
// (async) and arms newFileAfterSave_ so DoNewFile runs once the save commits.
void Application::RenderUnsavedDialog() {
    if (unsavedDialogOpen_) {
        ImGui::OpenPopup("Unsaved changes");
        unsavedDialogOpen_ = false;
    }

    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ds.GetColor(Tok::S_Color_Background_Layer1));
    ImGui::PushStyleColor(ImGuiCol_Text,    ds.GetColor(Tok::S_Color_Text_Default));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ds.GetVec2(Tok::C_Window_Padding));

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current project has unsaved changes.");
        ImGui::TextUnformatted("Save before creating a new file?");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            if (project_.path.empty()) {
                // No path yet → Save-As is async; finish the pending new/open
                // after it commits (handled in ProcessPendingFileOp).
                newFileAfterSave_ = true;
                Action_SaveFileAs();
            } else {
                Action_SaveFile();
                CommitPendingNew();   // preset OR module open
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
            CommitPendingNew();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            pendingModuleId_.clear();       // drop the pending intent
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace App

#include "ZoneLayout.h"
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Dropdown.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace App {
static const char* EditorKindAction(EditorKind k) {
    switch (k) {
        case EditorKind::Viewport:  return "editor.viewport";
        case EditorKind::Outliner:  return "editor.outliner";
        case EditorKind::Timeline:  return "editor.timeline";
        case EditorKind::DevPanels: return "editor.devPanels";
        default:                    return "";
    }
}
// Column group in the editor selector menu: 0 General, 1 Animation, 2 Data.
static int EditorKindColumn(EditorKind k) {
    switch (k) {
        case EditorKind::Viewport:  return 0;  // General
        case EditorKind::Timeline:  return 1;  // Animation
        case EditorKind::Outliner:  return 2;  // Data
        case EditorKind::DevPanels: return 2;  // Data
        default:                    return 0;
    }
}
// Theme scope for an editor zone: "editors/<kind>" (sub-scope of the
// "editors" root, where window padding is forced to 0).
static const char* EditorKindScope(EditorKind k) {
    switch (k) {
        case EditorKind::Viewport:  return "editors/viewport";
        case EditorKind::Outliner:  return "editors/outliner";
        case EditorKind::Timeline:  return "editors/timeline";
        case EditorKind::DevPanels: return "editors/devPanels";
        default:                    return "editors";
    }
}

void ZoneLayout::DrawLeaf(
    Node* n, float gap,
    const DrawEditorFn& drawEditor, const TopBarExtraFn& topBarExtras) {
    // Track the whole zone as a component "Editor" (the inner editor body
    // pushes its own kind-specific ComponentScope; both contribute to the
    // usage map so the Tokens viewer can attribute usage to BOTH layers).
    DesignSystem::DesignSystem::ComponentScope _cs("Editor");
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs   = ds.GetGlobalScale();
    // The top bar holds control-height widgets with a vertical inset above and
    // below: barH = control-height + 2·padding.y. The dropdown padding token is
    // the shared "bar inset" (x=side margin, y=vertical inset).
    const float controlH = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gs;
    ImVec2 barPad = ds.GetVec2(DesignSystem::Tok::C_Dropdown_Padding);
    barPad.x *= gs; barPad.y *= gs;
    const float barH = controlH + barPad.y * 2.0f;
    if (n->size.x < 8.0f || n->size.y < 8.0f) return;

    char zid[40];
    std::snprintf(zid, sizeof(zid), "##zone_%p", (void*)n);

    // An ImGui CHILD per zone, in-flow inside ##LayoutBody (a Begin() here
    // would be a detached root window hidden behind ##MainLayout's opaque
    // background — that was the all-black bug). NATIVE ImGui border/rounding/
    // padding, all token-driven: this is the base look the user wants. The
    // separator only adds a hover highlight on top of these native borders.
    //
    // Window tokens are resolved through the EDITOR SCOPE ("editors/<kind>",
    // sub-scope of "editors" where padding is forced to 0), so editor
    // content is flush while a single editor kind can still be re-themed.
    const std::string scope = EditorKindScope(LeafKind(n));
    const auto theme = ds.GetCurrentContext().GetTheme();
    ImVec2 wpad(0, 0);
    try {
        wpad = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_Padding), scope, theme).AsVec2();
    } catch (...) {}

    // The zone child is SQUARE with NO native border. ImGui draws a child's
    // bg/border in Begin() (before content), and our nested ##tb/##c children
    // have their OWN draw lists rendered ON TOP — so a native border would be
    // overdrawn by the editor content at the corners (the bug the user saw).
    // Instead the rounded border + the rounded-corner CLIP are painted on the
    // overlay (top-most) by DrawZoneFrames(), strictly above all content.
    ImGui::SetCursorScreenPos(n->pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ds.GetColor(DesignSystem::Tok::C_Editor_Background));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   wpad);
    ImGui::BeginChild(zid, n->size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    // Inner content rect (ImGui already inset by WindowPadding).
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // ── Tab bar ──────────────────────────────────────────────────────────────
    // Shown when the zone has >1 tab, when the always-show preference is on, OR
    // transiently while a tab drag hovers a SOLO zone's menu-bar band (the bar
    // slides in, animated, as a drop target — revealFrac eases that height).
    const bool permanentBar = n->tabs.size() > 1 ||
        ds.GetInt(DesignSystem::Tok::C_ZoneTab_ShowSolo) != 0;
    const float revealFrac = (n == revealLeaf_) ? revealAnim_ : 0.0f;
    const bool showTabBar = permanentBar || revealFrac > 0.01f;
    ImVec2 tabPad = ds.GetVec2(DesignSystem::Tok::C_ZoneTab_Padding);
    tabPad.x *= gs; tabPad.y *= gs;
    const float tabBarFull = controlH + tabPad.y * 2.0f;
    // Permanent bars are full height; a pure reveal animates its height.
    const float tabBarH = !showTabBar ? 0.0f
        : (permanentBar ? tabBarFull : tabBarFull * std::clamp(revealFrac, 0.0f, 1.0f));
    if (showTabBar)
        DrawTabBar(n, tabBarFull, n->pos, ImVec2(avail.x, tabBarH));

    // Top bar: a SINGLE menu-style dropdown showing the current editor (icon +
    // name), opening a menu listing every EditorKind — same UX as the main
    // menu bar's File/Edit tabs. The extras (e.g. the Viewport "+" button) are
    // pinned to the right.
    ImVec4 tbBg   = ds.GetColor(DesignSystem::Tok::C_Editor_TopBarBackground);

    ImGui::SetCursorPos(ImVec2(0.0f, tabBarH));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tbBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##tb", ImVec2(avail.x, barH), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Reserve room on the right for the extras (Viewport "+" button): a
    // control-height square plus its own side margins.
    float extrasW = 0.0f;
    if (topBarExtras && LeafKind(n) == EditorKind::Viewport)
        extrasW = controlH + barPad.x * 2.0f;

    // When there is NO tab bar, the empty menu-bar background is a grab handle
    // for the zone's solitary tab. SetNextItemAllowOverlap lets the dropdown /
    // extras buttons (submitted afterwards, same area) still receive their
    // clicks; the grab only arms where no other item is hovered. The four zone
    // CORNERS are excluded so the corner-drag (merge / split) still wins there.
    if (!showTabBar) {
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##barGrab", ImVec2(avail.x, barH));
        const float cornerSz = 14.0f * gs;
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool inCorner =
            (mp.x < n->pos.x + cornerSz || mp.x > n->pos.x + n->size.x - cornerSz);
        // (top edge only — the bar is at the top of the zone)
        inCorner = inCorner && (mp.y < n->pos.y + cornerSz);
        if (ImGui::IsItemActive() && !ImGui::IsAnyItemHovered() && !inCorner &&
            !tabDrag_.armed && !tabDrag_.active &&
            !addArm_.armed && !sepDragging_ && !join_.active && !splitArm_.active) {
            tabDrag_.armed   = true;
            tabDrag_.srcLeaf = n;
            tabDrag_.srcTab  = n->activeTab;
            tabDrag_.grabPos = ImGui::GetIO().MousePos;
        }
    }

    // ── The editor selector: a reusable Blender-style dropdown ───────────
    // Placed at the bar inset so the control-height trigger is centred in the
    // taller bar. Menu = three columns (General / Animation / Data).
    {
        auto& sm = Shortcuts::ShortcutManager::Instance();
        UI::DropdownConfig cfg;
        cfg.id          = "##editorsel";
        cfg.triggerIcon = EditorKindIcon(LeafKind(n));
        cfg.triggerLabel = EditorKindName(LeafKind(n));
        cfg.columnHeaders = { "General", "Animation", "Data" };

        int selected = -1;
        for (int i = 0; i < (int)EditorKind::Count; ++i) {
            EditorKind k = (EditorKind)i;
            UI::DropdownItem it;
            it.icon        = EditorKindIcon(k);
            it.label       = EditorKindName(k);
            it.shortcut    = sm.GetShortcutString(EditorKindAction(k));
            it.columnGroup = EditorKindColumn(k);
            cfg.items.push_back(it);
            if (k == LeafKind(n)) selected = i;
        }
        cfg.selectedIndex = selected;

        ImGui::SetCursorPos(ImVec2(barPad.x, barPad.y));
        UI::DropdownResult r = UI::Dropdown(cfg);
        if (r.changed && r.selected >= 0 &&
            r.selected < (int)EditorKind::Count)
            ActiveTab(n).kind = (EditorKind)r.selected;
    }

    // Extras (e.g. "+" button for Viewport), right-aligned and centred.
    if (topBarExtras && extrasW > 0.0f) {
        ImGui::SetCursorPos(ImVec2(avail.x - extrasW, barPad.y));
        topBarExtras(LeafKind(n), LeafState(n));
    }
    // Anchor the child's full height (we used SetCursorPos for manual layout).
    // A zero-size Dummy after the manual cursor move grows the child to barH so
    // ImGui doesn't assert about SetCursorPos extending the boundary.
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::Dummy(ImVec2(avail.x, barH));
    ImGui::EndChild(); // ##tb

    const float chromeH = tabBarH + barH;
    ImVec2 cs(avail.x, std::max(0.0f, avail.y - chromeH));
    ImGui::SetCursorPos(ImVec2(0.0f, chromeH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##c", cs, false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    {
        // Apply the editor's theme SCOPE ("editors/<kind>", under "editors")
        // around the content. PushZoneStyles resolves every zone token
        // through the scoped cascade, so a Theme-Editor override on
        // "editors" or "editors/<kind>" actually affects the editor — that
        // was the missing piece (no widget consulted those scopes before).
        // The ZoneStyle MUST be destroyed before EndChild() (ImGui validates
        // the style stack there) — hence this explicit block.
        DesignSystem::DesignSystem::ZoneStyle zsc(scope,
                                                  EditorKindName(LeafKind(n)));
        // Inner editor body is attributed to its OWN kind ("Viewport",
        // "Outliner", …) on top of the wrapping "Editor" scope, so usage
        // stats distinguish chrome from per-kind content.
        DesignSystem::DesignSystem::ComponentScope _kindCs(
            EditorKindName(LeafKind(n)));
        drawEditor(LeafKind(n), cs, LeafState(n));
    }
    ImGui::EndChild(); // ##c
    ImGui::PopStyleVar();

    ImGui::EndChild(); // zone
    ImGui::PopStyleVar(2);     // ChildRounding + WindowPadding
    ImGui::PopStyleColor(1);   // ChildBg
    (void)gap;
}

// ── Separator: MANUAL hit-test (no InvisibleButton, no overlay window) so it
//    never conflicts with the per-zone real ImGui windows' hover. The gap
//    region is free of any window, so geometric testing on io.MousePos is
//    reliable. The zones meet at the separator MIDLINE → no dead zone. ──────

void ZoneLayout::DrawZoneFrames(Node* n) {
    if (!n) return;
    if (!n->isLeaf()) { DrawZoneFrames(n->a.get());
                        DrawZoneFrames(n->b.get()); return; }
    if (n->size.x < 8.0f || n->size.y < 8.0f) return;

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gs   = ds.GetGlobalScale();
    const std::string scope = EditorKindScope(LeafKind(n));
    const auto theme = ds.GetCurrentContext().GetTheme();
    float rnd = 0.0f, bord = 1.0f;
    try { rnd  = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_CornerRadius), scope, theme).AsFloat(); }
    catch (...) {}
    try { bord = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_BorderWidth), scope, theme).AsFloat(); }
    catch (...) {}
    rnd  *= gs;
    // Honour borderSize == 0 exactly (no clamp to 1px): setting the token to
    // 0 must remove the zone border like everywhere else. Negative guarded.
    bord  = std::max(0.0f, bord * gs);

    ImDrawList* dl = ImGui::GetWindowDrawList();   // overlay → above content
    // Snap the zone rect to whole pixels. Layout() produces fractional
    // pos/size (ratio splits, DPI scaling), and a 1px AddRect on a fractional
    // edge gets anti-aliased across two pixels — the "blurry / bleeding"
    // border the user saw. ImGui's own window borders look sharp precisely
    // because window rects are pixel-aligned; we do the same here.
    ImVec2 mn(std::roundf(n->pos.x), std::roundf(n->pos.y));
    ImVec2 mx(std::roundf(n->pos.x + n->size.x), std::roundf(n->pos.y + n->size.y));
    ImU32 bg     = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_Editor_Background));
    ImU32 border = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Border_Default));

    if (rnd > 0.5f) {
        const float PI = 3.14159265358979f;
        // Each corner: fill the square-minus-quarter-disc nib with the bg
        // colour so editor content can't show outside the rounded border.
        auto nib = [&](ImVec2 corner, ImVec2 center,
                       float a0, float a1) {
            dl->PathClear();
            dl->PathLineTo(corner);
            dl->PathArcTo(center, rnd, a0, a1, 16);
            dl->PathFillConvex(bg);
        };
        // TL: corner (mn.x,mn.y), center (mn.x+rnd,mn.y+rnd), arc 180°→270°
        nib(ImVec2(mn.x, mn.y), ImVec2(mn.x + rnd, mn.y + rnd),
            PI, 1.5f * PI);
        // TR: corner (mx.x,mn.y), center (mx.x-rnd,mn.y+rnd), arc 270°→360°
        nib(ImVec2(mx.x, mn.y), ImVec2(mx.x - rnd, mn.y + rnd),
            1.5f * PI, 2.0f * PI);
        // BR: corner (mx.x,mx.y), center (mx.x-rnd,mx.y-rnd), arc 0°→90°
        nib(ImVec2(mx.x, mx.y), ImVec2(mx.x - rnd, mx.y - rnd),
            0.0f, 0.5f * PI);
        // BL: corner (mn.x,mx.y), center (mn.x+rnd,mx.y-rnd), arc 90°→180°
        nib(ImVec2(mn.x, mx.y), ImVec2(mn.x + rnd, mx.y - rnd),
            0.5f * PI, PI);
    }
    // Rounded border, ON TOP of everything — the visual limit of the zone.
    // Skip entirely when the border token is 0 (borders removed everywhere).
    if (bord > 0.01f)
        dl->AddRect(mn, mx, border, rnd, 0, bord);
}

// ── Per-leaf translucent corner drag hot-zones, colour-coded by corner ───────
void ZoneLayout::DrawCornerZones(Node* n) {
    if (!n) return;
    if (!n->isLeaf()) { DrawCornerZones(n->a.get());
                        DrawCornerZones(n->b.get()); return; }
    if (n->size.x < 40.0f || n->size.y < 40.0f) return;

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float c = 14.0f * ds.GetGlobalScale();   // corner hit radius
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lL = n->pos.x, lR = n->pos.x + n->size.x;
    float lT = n->pos.y, lB = n->pos.y + n->size.y;

    // Distinct, low-alpha tint per corner so the user sees which corner does
    // what (TL/TR/BL/BR). Bright accents at ~12% alpha.
    struct CZ { ImVec2 a, b; ImU32 col; };
    const CZ zones[4] = {
        { ImVec2(lL, lT),       ImVec2(lL + c, lT + c), IM_COL32( 80,170,255, 32) }, // TL blue
        { ImVec2(lR - c, lT),   ImVec2(lR,     lT + c), IM_COL32(120,230,140, 32) }, // TR green
        { ImVec2(lL, lB - c),   ImVec2(lL + c, lB),     IM_COL32(255,200, 90, 32) }, // BL amber
        { ImVec2(lR - c, lB - c), ImVec2(lR,   lB),     IM_COL32(255,120,160, 32) }, // BR pink
    };
    for (const CZ& z : zones)
        dl->AddRectFilled(z.a, z.b, z.col, 3.0f);
}

// ── Pre-pass: pick the split whose separator hit-rect the mouse is over ───────

void ZoneLayout::DrawLeaves(
    Node* n, float gap,
    const DrawEditorFn& drawEditor, const TopBarExtraFn& topBarExtras) {
    if (n->isLeaf()) { DrawLeaf(n, gap, drawEditor, topBarExtras); return; }
    DrawLeaves(n->a.get(), gap, drawEditor, topBarExtras);
    DrawLeaves(n->b.get(), gap, drawEditor, topBarExtras);
}

// ── Pass 2: separators + join preview + add-area (foreground draw list,
//    manual geometric hit-test in the window-free gaps) ─────────────────────

} // namespace App

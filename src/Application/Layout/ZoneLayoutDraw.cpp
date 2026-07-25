#include "ZoneLayout.h"
#include <UI/Widgets/IconWidgets.h>
#include <UI/Widgets/Dropdown.h>
#include <UI/Widgets/ScrollArea.h>
#include <VectorGraphics/IconManager.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <unordered_map>

namespace App {
// Editor metadata (name/icon/column/scope/switch-action) now lives in each
// EditorDescriptor (EditorRegistry). The per-zone picker and theming read those
// fields directly — no per-kind switch here anymore.

// Theme scope of an editor id (falls back to the "editors" root).
static std::string ScopeOf(const std::string& id) {
    if (const EditorDescriptor* d = EditorRegistry::Instance().Get(id);
        d && !d->themeScope.empty())
        return d->themeScope;
    return "editors";
}

void ZoneLayout::DrawLeaf(Node* n, float gap) {
    const std::string& editorId = LeafEditorId(n);
    const EditorDescriptor* desc = EditorRegistry::Instance().Get(editorId);
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
    const std::string scope = (desc && !desc->themeScope.empty())
                                  ? desc->themeScope : std::string("editors");
    const auto theme = ds.GetCurrentContext().GetTheme();
    ImVec2 wpad(0, 0);
    try {
        wpad = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_Padding), scope, theme).AsVec2();
    } catch (...) {}
    // The zone's corner radius, resolved through the editor scope (same source
    // DrawZoneFrames reads, so the rounded clip and the overlay border agree).
    float zoneRnd = 0.0f;
    try { zoneRnd  = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_CornerRadius), scope, theme).AsFloat(); }
    catch (...) {}
    zoneRnd  *= gs;

    // The zone child carries ImGui's NATIVE rounded background. ImGui paints
    // the rounded fill in Begin() (clipped to the rounded rect on the window
    // draw list), so the editor canvas never bleeds past the radius and we no
    // longer re-paint corner "nibs". The inner bars (tab bar / menu bar) round
    // their OWN top corners to match (see below); content children inherit the
    // editor bg, so the bottom corners are already the right colour under the
    // rounded clip. The 1px border stays on the overlay (DrawZoneFrames), above
    // all content, so it is the crisp visual limit of the zone.
    ImGui::SetCursorScreenPos(n->pos);
    // The Viewport zone goes FULLY transparent when the active engine composites
    // its canvas onto the swapchain itself (the Compositor): the zone + content
    // children must not paint an opaque bg, or it would hide the canvas drawn
    // under ImGui. The Viewport then repaints only its ruler strips (it leaves the
    // canvas rect transparent). The legacy engine keeps the opaque editor bg.
    const bool transpCanvas =
        canvasZoneTransparent_ && editorId == CoreEditor::Viewport;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          transpCanvas ? ImVec4(0, 0, 0, 0)
                                       : ds.GetColor(DesignSystem::Tok::C_Editor_Background));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   zoneRnd);
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
    if (showTabBar) {
        // The tab bar is flush with the zone top, so its background must follow
        // the zone radius on its TOP corners. DrawTabBar no longer paints a
        // square ChildBg; we lay the bar fill here so the corners read
        // correctly under the rounded clip.
        ImDrawList* zdl = ImGui::GetWindowDrawList();
        ImVec2 bMin = ImGui::GetCursorScreenPos();
        ImVec2 bMax(bMin.x + avail.x, bMin.y + tabBarH);
        const float r = zoneRnd;
        zdl->AddRectFilled(bMin, bMax,
            ImGui::ColorConvertFloat4ToU32(
                ds.GetColor(DesignSystem::Tok::C_ZoneTab_BarBackground)),
            r, r > 0.5f ? ImDrawFlags_RoundCornersTop
                        : ImDrawFlags_RoundCornersNone);
        DrawTabBar(n, tabBarFull, n->pos, ImVec2(avail.x, tabBarH));
    }

    // Top bar: a SINGLE menu-style dropdown showing the current editor (icon +
    // name), opening a menu listing every EditorKind — same UX as the main
    // menu bar's File/Edit tabs. The extras (e.g. the Viewport "+" button) are
    // pinned to the right.
    ImVec4 tbBg   = ds.GetColor(DesignSystem::Tok::C_Editor_TopBarBackground);

    ImGui::SetCursorPos(ImVec2(0.0f, tabBarH));
    // Paint the menu-bar background ourselves (no ChildBg): when there is no
    // tab bar above it, the menu bar is flush with the zone top, so its TOP
    // corners must follow the zone radius — otherwise its square fill bleeds
    // past the rounded clip into the corners. With a tab bar, the menu bar is
    // an interior band and stays square.
    {
        ImDrawList* zdl = ImGui::GetWindowDrawList();
        ImVec2 tbMin = ImGui::GetCursorScreenPos();
        ImVec2 tbMax(tbMin.x + avail.x, tbMin.y + barH);
        const bool flushTop = !showTabBar;
        const float r = flushTop ? zoneRnd : 0.0f;
        zdl->AddRectFilled(tbMin, tbMax,
                           ImGui::ColorConvertFloat4ToU32(tbBg), r,
                           r > 0.5f ? ImDrawFlags_RoundCornersTop
                                    : ImDrawFlags_RoundCornersNone);
    }
    // NoBackground: the menu-bar fill is the AddRectFilled above. Letting the
    // child paint its own ChildBg would stack a second (child-coloured)
    // rectangle on top of it — the spurious overlay the user saw.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##tb", ImVec2(avail.x, barH), false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

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
    // ICON-ONLY trigger (label hidden); the menu still shows icon + label.
    // Placed at the bar inset so the control-height trigger is centred.
    float selectorRight = barPad.x;   // x where the extras' LEFT group may start
    {
        auto& sm = Shortcuts::ShortcutManager::Instance();
        UI::DropdownConfig cfg;
        cfg.id          = "##editorsel";
        cfg.triggerIcon = desc ? desc->icon.c_str() : "";
        cfg.triggerLabel = "";        // icon-only trigger (chevron still shown)
        cfg.columnHeaders = { "General", "Animation", "Data" };

        // List every registered editor (core + any active module's), in
        // registration order, grouped by descriptor.column.
        const auto& all = EditorRegistry::Instance().All();
        // Restrict to the active filter (Classic = core ids; a module = its
        // AllowedEditors). The zone's CURRENT editor is always offered so it never
        // becomes unselectable. `pick` maps menu rows back to registry indices.
        auto allowed = [&](const EditorDescriptor& d) {
            if (editorFilter_.empty() || d.id == editorId) return true;
            for (const std::string& f : editorFilter_) if (f == d.id) return true;
            return false;
        };
        std::vector<int> pick;
        int selected = -1;
        for (size_t i = 0; i < all.size(); ++i) {
            const EditorDescriptor& d = all[i];
            if (!allowed(d)) continue;
            UI::DropdownItem it;
            it.icon        = d.icon.c_str();   // DropdownItem::icon is const char*
            it.label       = d.name;
            if (!d.switchAction.empty()) {
                it.shortcut = sm.GetShortcutString(d.switchAction.c_str());
                if (const Shortcuts::Action* a = sm.GetAction(d.switchAction.c_str()))
                    it.tooltip = a->description;
            }
            it.columnGroup = d.column;
            if (d.id == editorId) selected = (int)pick.size();
            pick.push_back((int)i);
            cfg.items.push_back(it);
        }
        cfg.selectedIndex = selected;

        ImGui::SetCursorPos(ImVec2(barPad.x, barPad.y));
        UI::DropdownResult r = UI::Dropdown(cfg);
        selectorRight = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        if (r.changed && r.selected >= 0 && r.selected < (int)pick.size())
            ActiveTab(n).editorId = all[(size_t)pick[(size_t)r.selected]].id;
    }

    // ── Editor top bar: three groups (left / middle / right) ─────────────
    // The hook declares each group's draw fn + natural width. Layout rules:
    //   • LEFT sits just after the editor-kind picker;
    //   • MIDDLE is centred, but never left of (left end + gap) nor right of
    //     (right start − gap);
    //   • RIGHT is anchored to the right inset, growing leftward (right-aligned);
    //   • when there isn't enough room, the whole {left, middle, right} block is
    //     shifted RIGHT together (so left never gets covered) and the overflow on
    //     the right is CLIPPED off the editor edge — no group ever overlaps.
    if (desc && desc->topBar) {
        EditorBar bar;
        desc->topBar(LeafState(n), bar);
        const float gap  = barPad.x;                 // ONE uniform gap everywhere
        const float availX = avail.x;
        const float groupY = barPad.y;               // vertical-centre like the picker
        const float xMin   = selectorRight + gap;    // left boundary (after picker)

        // Group widths are MEASURED automatically (each group is wrapped in a
        // BeginGroup/EndGroup; its real width is cached and reused next frame for
        // positioning). This makes placement independent of any hand-declared width
        // (`bar.*.width` is now only a first-frame hint). One-frame lag is
        // imperceptible. Cache key = this zone's window id + a group index.
        static std::unordered_map<ImU32, float> s_groupW;
        const ImU32 base = ImGui::GetID("##barGroupW");
        auto cachedW = [&](int idx, float hint) -> float {
            auto it = s_groupW.find(base + (ImU32)idx);
            return it != s_groupW.end() ? it->second : hint;
        };
        const float lW = bar.left.draw   ? cachedW(0, bar.left.width)   : 0.0f;
        const float mW = bar.middle.draw ? cachedW(1, bar.middle.width) : 0.0f;
        const float rW = bar.right.draw  ? cachedW(2, bar.right.width)  : 0.0f;

        // Ideal positions: left after the picker; middle centred on the WHOLE bar;
        // right anchored to the right inset (same margin as the left, = barPad.x).
        float leftX  = xMin;
        float rightX = availX - barPad.x - rW;
        float midX   = (availX - mW) * 0.5f;
        // Clamp the middle so it never overlaps a neighbour (keep exactly one gap).
        if (lW > 0) midX = std::max(midX, leftX + lW + gap);
        if (rW > 0) midX = std::min(midX, rightX - gap - mW);
        // If they still don't fit, push the block right (overflow clipped at the
        // editor edge): pack left→middle→right with a single gap between each.
        if (mW > 0 && midX < leftX + lW + gap) midX = leftX + lW + gap;
        if (rW > 0) {
            float minRight = (mW > 0 ? midX + mW : leftX + lW) + gap;
            if (rightX < minRight) rightX = minRight;   // right overflows → clipped
        }

        // Clip the whole bar to the editor (right group can overflow → hidden).
        ImVec2 clipMin = ImGui::GetWindowPos();
        ImVec2 clipMax(clipMin.x + availX, clipMin.y + barH);
        ImGui::PushClipRect(clipMin, clipMax, true);
        // Draw a group wrapped in BeginGroup so we can measure its real width and
        // cache it for next frame's positioning.
        auto drawGroup = [&](const EditorBarGroup& g, int idx, float x) {
            if (!g.draw) return;
            ImGui::SetCursorPos(ImVec2(x, groupY));
            ImGui::BeginGroup();
            g.draw(ImVec2(x, groupY), barH);
            ImGui::EndGroup();
            s_groupW[base + (ImU32)idx] = ImGui::GetItemRectSize().x;
        };
        drawGroup(bar.left,   0, leftX);
        drawGroup(bar.middle, 1, midX);
        drawGroup(bar.right,  2, rightX);
        ImGui::PopClipRect();
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
    // The content child is SQUARE: its top edge sits flush under the menu bar
    // (a rounded clip here notched the canvas's top corners mid-zone). The
    // zone's rounded BOTTOM corners are restored on the overlay by
    // DrawZoneFrames' corner masks, above any content (canvas included).
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
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
                                                  desc ? desc->name.c_str() : "Editor");
        // Inner editor body is attributed to its OWN kind ("Viewport",
        // "Outliner", …) on top of the wrapping "Editor" scope, so usage
        // stats distinguish chrome from per-kind content.
        DesignSystem::DesignSystem::ComponentScope _kindCs(
            desc ? desc->name.c_str() : "Editor");

        // Draw the editor body via its registry descriptor. Editors that opt into
        // wrapInScroll get the Blender-style overlay scrollbar (in-margin); those
        // that don't (Viewport, Timeline, Info) draw their content directly.
        auto body = [&](ImVec2 size) {
            if (!desc || !desc->draw) { ImGui::TextDisabled("Unknown editor"); return; }
            if (desc->wrapInScroll) {
                if (UI::BeginScroll("##editorScroll")) desc->draw(size, LeafState(n));
                UI::EndScroll();
            } else {
                desc->draw(size, LeafState(n));
            }
        };

        // Content inset: panel editors (Outliner/Properties/Timeline/Dev) get a
        // coherent padding on all four sides, via an inner child placed at
        // (inset, inset) and shrunk by 2·inset — explicit and unaffected by any
        // sub-child the editor opens. Resolved through the editor scope so a
        // theme (Preferences ▸ Customisation ▸ Editor ▸ Editor frame) can tune
        // it. The Viewport stays flush (inset disabled): it draws its canvas/
        // rulers edge-to-edge and lays out its own chrome absolutely.
        float inset = 0.0f;
        if (desc && desc->contentInset) {
            try { inset = ds.ResolveScoped(DesignSystem::TokIdStr(
                    DesignSystem::Tok::C_Editor_ContentInset), scope, theme).AsFloat(); }
            catch (...) {}
            inset = std::max(0.0f, inset) * gs;
        }
        if (inset > 0.0f) {
            // Inset on the left/top/bottom; the RIGHT side is left flush so the
            // editor's BeginScroll overlay scrollbar lives in that right margin
            // (its gutter) — i.e. the scrollbar sits in the editor's inset band,
            // not inside the content. Content is narrowed by BeginScroll's gutter
            // so it never touches the right edge. (Editors that don't scroll just
            // see a flush-right inner area, which is fine.)
            ImVec2 inner(std::max(0.0f, cs.x - inset),       // left inset only
                         std::max(0.0f, cs.y - inset * 2.0f));
            ImGui::SetCursorPos(ImVec2(inset, inset));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("##cin", inner, false,
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
            body(inner);
            ImGui::EndChild(); // ##cin
            ImGui::PopStyleVar();
        } else {
            body(cs);
        }
    }
    ImGui::EndChild(); // ##c
    ImGui::PopStyleVar(2);     // ChildRounding(0) + WindowPadding

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
    const std::string scope = ScopeOf(LeafEditorId(n));
    const auto theme = ds.GetCurrentContext().GetTheme();
    float rnd = 0.0f, bord = 1.0f;
    try { rnd  = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_CornerRadius), scope, theme).AsFloat(); }
    catch (...) {}
    try { bord = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_BorderWidth), scope, theme).AsFloat(); }
    catch (...) {}
    rnd  *= gs;
    // Honour the global borders toggle, then borderSize == 0 exactly (no clamp
    // to 1px): disabling borders or setting the token to 0 removes the zone
    // border like everywhere else. Negative guarded.
    if (!ds.BordersEnabled()) bord = 0.0f;
    bord  = std::max(0.0f, bord * gs);

    ImDrawList* dl = ImGui::GetWindowDrawList();   // overlay → above content
    // Snap the zone rect to whole pixels. Layout() produces fractional
    // pos/size (ratio splits, DPI scaling), and a 1px AddRect on a fractional
    // edge gets anti-aliased across two pixels — the "blurry / bleeding"
    // border the user saw. ImGui's own window borders look sharp precisely
    // because window rects are pixel-aligned; we do the same here.
    ImVec2 mn(std::roundf(n->pos.x), std::roundf(n->pos.y));
    ImVec2 mx(std::roundf(n->pos.x + n->size.x), std::roundf(n->pos.y + n->size.y));
    ImU32 border = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Border_Default));

    // BOTTOM corner masks: the content child is square (its rounded clip used
    // to notch the canvas's TOP corners mid-zone), so the zone's rounded
    // bottom corners are restored here by painting the area between the square
    // corner and the arc with the HOST background (the colour between zones),
    // above any content — canvas included. Top corners need no mask (the
    // tab/menu bars round their own tops under the zone clip).
    if (rnd > 0.5f) {
        const ImU32 hostBg = ImGui::GetColorU32(
            ds.GetColor(DesignSystem::Tok::S_Color_Background_Default));
        constexpr float kPi = 3.14159265358979f;
        // Each mask = the square corner minus the quarter disc. The path fans
        // from the CORNER point (the notch is star-shaped from there): corner →
        // arc points → close.
        dl->PathLineTo(ImVec2(mn.x, mx.y));                    // bottom-left
        dl->PathArcTo(ImVec2(mn.x + rnd, mx.y - rnd), rnd,
                      kPi, kPi * 0.5f);                        // 180° → 90°
        dl->PathFillConvex(hostBg);
        dl->PathLineTo(ImVec2(mx.x, mx.y));                    // bottom-right
        dl->PathArcTo(ImVec2(mx.x - rnd, mx.y - rnd), rnd,
                      kPi * 0.5f, 0.0f);                       // 90° → 0°
        dl->PathFillConvex(hostBg);
    }
    // The crisp rounded border, ON TOP of all content, as the strict visual
    // limit of the zone. Skipped when the border token is 0.
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
    const float gs = ds.GetGlobalScale();
    const float c = 14.0f * gs;   // corner hit radius
    // Zone corner radius (editor scope) so the previews follow the rounded
    // border at the outer corner instead of poking past it as square nibs.
    const std::string scope = ScopeOf(LeafEditorId(n));
    const auto theme = ds.GetCurrentContext().GetTheme();
    float rnd = 0.0f;
    try { rnd = ds.ResolveScoped(DesignSystem::TokIdStr(
            DesignSystem::Tok::C_Window_CornerRadius), scope, theme).AsFloat(); }
    catch (...) {}
    rnd *= gs;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lL = n->pos.x, lR = n->pos.x + n->size.x;
    float lT = n->pos.y, lB = n->pos.y + n->size.y;

    // Distinct, low-alpha tint per corner so the user sees which corner does what
    // (TL/TR/BL/BR). The hue comes from the per-corner zone-overlay tokens; the
    // ~12% overlay alpha is a functional preview strength applied here. The OUTER
    // corner of each preview is rounded to the zone radius so it stays inside the
    // rounded border; the inner corner keeps the small 3px rounding.
    auto tint = [&](DesignSystem::Tok t) {
        ImVec4 c4 = ds.GetColor(t); c4.w = 0.125f;   // ~32/255 overlay alpha
        return ImGui::ColorConvertFloat4ToU32(c4);
    };
    struct CZ { ImVec2 a, b; ImU32 col; ImDrawFlags outer; };
    const CZ zones[4] = {
        { ImVec2(lL, lT),         ImVec2(lL + c, lT + c), tint(DesignSystem::Tok::C_ZoneOverlay_CornerTopLeft),     ImDrawFlags_RoundCornersTopLeft },     // TL blue
        { ImVec2(lR - c, lT),     ImVec2(lR,     lT + c), tint(DesignSystem::Tok::C_ZoneOverlay_CornerTopRight),    ImDrawFlags_RoundCornersTopRight },    // TR green
        { ImVec2(lL, lB - c),     ImVec2(lL + c, lB),     tint(DesignSystem::Tok::C_ZoneOverlay_CornerBottomLeft),  ImDrawFlags_RoundCornersBottomLeft },  // BL amber
        { ImVec2(lR - c, lB - c), ImVec2(lR,     lB),     tint(DesignSystem::Tok::C_ZoneOverlay_CornerBottomRight), ImDrawFlags_RoundCornersBottomRight }, // BR pink
    };
    for (const CZ& z : zones) {
        if (rnd > 0.5f)
            dl->AddRectFilled(z.a, z.b, z.col, rnd, z.outer);
        else
            dl->AddRectFilled(z.a, z.b, z.col, 3.0f);
    }
}

// ── Pre-pass: pick the split whose separator hit-rect the mouse is over ───────

void ZoneLayout::DrawLeaves(Node* n, float gap) {
    if (n->isLeaf()) { DrawLeaf(n, gap); return; }
    DrawLeaves(n->a.get(), gap);
    DrawLeaves(n->b.get(), gap);
}

// ── Pass 2: separators + join preview + add-area (foreground draw list,
//    manual geometric hit-test in the window-free gaps) ─────────────────────

} // namespace App

#include <UI/Widgets/Panel.h>
#include <UI/Widgets/IconWidgets.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <vector>
#include <cmath>
#include <cstdio>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// Each panel = ONE AutoResizeY child window holding header (+content when open).
// The child exists open OR collapsed (header-only), so a header inline editor
// drawn by the caller always sits inside it and is never covered. Level-1 panels
// get native rounded ChildBg + Borders enclosing everything.
struct PanelFrame {
    int    depth   = 0;
    bool   level1  = false;
    bool   open    = false;
    ImVec2 topLeft{};     // panel top-left (for the manual level-1 border)
    float  width   = 0.0f;
    float  radius  = 0.0f;
    float  borderW = 0.0f;
    // True when this (level-1) panel pushed ItemSpacing twice: once at depth-1
    // to zero the parent's inter-panel spacing (so the gap is exactly the gap
    // token, not gap + 2×ItemSpacing.y), and once inside the child to restore
    // the normal spacing for the panel's own content. EndPanel pops both.
    bool   spacingPushed = false;
};

std::vector<PanelFrame>& Stack() {
    static std::vector<PanelFrame> s;
    return s;
}

// ── Reorderable-list context ─────────────────────────────────────────────────
// Set for the duration of one BeginPanelListItem/EndPanelListItem pair; the
// header logic inside BeginPanel consults it (press capture, release-toggle
// gating). List state (drag index, press, per-item rects) lives in the LIST
// window's ImGuiStorage so it survives frames; the keys are computed at list
// scope (outside the per-item PushID) so every item reads the same slots.
struct ListCtx {
    bool           active   = false;
    int            index    = 0;
    int            count    = 0;
    const char*    itemId   = "";        // cfg.id — identical for every item
    ImGuiStorage*  st       = nullptr;   // the list window's storage
    ImGuiID        kDrag    = 0;         // int: index being dragged (-1 none)
    ImGuiID        kPress   = 0;         // int: index pressed, pre-threshold
    ImGuiID        kPressY  = 0;         // float: mouse y at press
    ImGuiID        kDragged = 0;         // bool: this hold entered a drag
    PanelListEdit* edit     = nullptr;
};
ListCtx g_list;

// Per-index storage key at LIST scope (call outside the item's PushID).
ImGuiID ListRectKey(int index, bool top) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "##plR%d%c", index, top ? 't' : 'b');
    return ImGui::GetID(buf);
}

// Swap the two items' persisted panel state (the "##open" flag) so expansion
// follows the ITEM when the list reorders it, not the slot it sat in.
void ListSwapItemState(ImGuiStorage* st, const char* itemId, int a, int b) {
    auto key = [&](int i) {
        ImGui::PushID(i);
        ImGui::PushID(itemId);
        const ImGuiID k = ImGui::GetID("##open");
        ImGui::PopID();
        ImGui::PopID();
        return k;
    };
    const ImGuiID ka = key(a), kb = key(b);
    const bool va = st->GetBool(ka, true), vb = st->GetBool(kb, true);
    st->SetBool(ka, vb);
    st->SetBool(kb, va);
}

// Body background by nesting level (per the design spec):
//   L1 category body      = raised
//   L2 section body        = canvas   (a bit darker)
//   L3 item/property body  = canvas   (same as section content)
//   L4+ (e.g. chain) body  = base     (darkest)
Tok BodyTokForDepth(int depth) {
    return (depth <= 1) ? Tok::C_Panel_BodyL1   // raised
         : (depth <= 3) ? Tok::C_Panel_BodyL2   // canvas
                        : Tok::C_Panel_BodyL3;   // base
}
// A header always matches the body colour of its PARENT, so it blends with the
// surface it sits on. Level-1 has no panel parent → the dedicated header token
// (which equals the raised body), keeping its top corners seamless.
Tok HeaderTokForDepth(int depth) {
    return (depth <= 1) ? Tok::C_Panel_HeaderBackground
                        : BodyTokForDepth(depth - 1);
}

} // namespace

int PanelDepth() { return (int)Stack().size(); }

float PanelHeaderTextIndent(int depth) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float pad  = 8.0f * gs;
    const float chev = Flt(Tok::C_Dropdown_ChevronSize) * gs;
    if (depth < 1) depth = 1;
    return pad + (float)(depth - 1) * (pad + chev);   // matches BeginPanel's cx
}

PanelResult BeginPanel(const PanelConfig& cfg) {
    PanelResult res;
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();

    const int   depth   = (int)Stack().size() + 1;
    const bool  level1  = (depth == 1);
    ImVec2 barPad = ds.GetVec2(Tok::C_Dropdown_Padding);
    barPad.x *= gs; barPad.y *= gs;
    const float headerH = Flt(Tok::S_Size_ControlHeight) * gs + barPad.y * 2.0f;
    const float pad     = 8.0f * gs;
    const float chev    = Flt(Tok::C_Dropdown_ChevronSize) * gs;
    const float iconSz  = Flt(Tok::C_Dropdown_IconSize) * gs;
    const float gap     = 4.0f * gs;
    const float radius  = Flt(Tok::C_Panel_CornerRadius) * gs;
    const float borderW = level1 ? ds.GetBorderWidth(Tok::C_Window_BorderWidth) * gs : 0.0f;
    const float depthIndent = (depth - 1) * (pad + chev);

    // Token-driven vertical gap before each level-1 panel. ImGui adds
    // ItemSpacing.y BEFORE and AFTER the gap Dummy (it sits between the previous
    // panel's child and this one's), which would inflate the gap to
    // `gap + 2×ItemSpacing.y`. Zero the parent's item spacing for the duration
    // of this level-1 panel so the visible inter-panel gap is exactly the token;
    // the normal spacing is restored INSIDE the child (below) for the content.
    const ImVec2 normalSpacing = ImGui::GetStyle().ItemSpacing;
    bool spacingPushed = false;
    if (level1) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(normalSpacing.x, 0.0f));
        spacingPushed = true;
        ImGui::Dummy(ImVec2(0.0f, Flt(Tok::C_Panel_Gap) * gs));
    }

    ImGui::PushID(cfg.id);

    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID openKey = ImGui::GetID("##open");
    bool open = st->GetBool(openKey, cfg.defaultOpen);

    const float fullW = ImGui::GetContentRegionAvail().x;

    // Body child: ImGui's NATIVE rounded ChildBg + Border (level-1). The border
    // is drawn & clipped by ImGui inside the child window's own draw list, so it
    // is always crisp and never covered by nested children (the manual-border
    // approach was z-ordered below the children's backgrounds). The native
    // border insets content by border width — that ~1px is the border itself,
    // not a stray margin. The header background is a FLAT rectangle whose colour
    // equals the body colour at the corners, so the rounded ChildBg shows
    // through the corners with no seam (no need to round the header rect).
    // A flat sub-panel keeps its PARENT's body colour (Properties: every
    // section shares the level-1 surface); otherwise the body darkens with
    // depth. Level-1 always uses its own body token.
    const Tok bodyTok = (cfg.flatBody && depth > 1) ? BodyTokForDepth(depth - 1)
                                                    : BodyTokForDepth(depth);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col(bodyTok));
    ImGui::PushStyleColor(ImGuiCol_Border, Col(Tok::C_Panel_Border));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, level1 ? radius : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, borderW);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiChildFlags childFlags = ImGuiChildFlags_AutoResizeY;
    if (level1) childFlags |= ImGuiChildFlags_Borders;
    ImGui::BeginChild("##panel", ImVec2(fullW, 0.0f), childFlags,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    // Inside the child, restore the normal item spacing so the panel's OWN
    // content lays out as usual (the zeroed spacing pushed above only governs
    // the parent's inter-panel gap). Popped in EndPanel, before the parent pop.
    if (spacingPushed)
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, normalSpacing);

    const float innerW = ImGui::GetContentRegionAvail().x;
    ImVec2 headerMin = ImGui::GetCursorScreenPos();
    ImVec2 headerMax(headerMin.x + innerW, headerMin.y + headerH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Header rectangle — ONLY when the header colour differs from the body
    // colour. When they are equal (e.g. level-1: header & body are both
    // "raised"), the rounded ChildBg already paints the header area perfectly,
    // so drawing nothing avoids any radius/border mismatch at the corners.
    // NOTE: compare the RESOLVED colours, not the token enums: a level-1 header
    // and body are distinct tokens (C_Panel_HeaderBackground vs C_Panel_BodyL1)
    // that both resolve to "raised". Comparing enums would always draw the flat
    // rect, whose square top corners overflow the rounded ChildBg + border.
    const ImVec4 headerCol = Col(HeaderTokForDepth(depth));
    const ImVec4 bodyCol   = Col(bodyTok);
    if (headerCol.x != bodyCol.x || headerCol.y != bodyCol.y ||
        headerCol.z != bodyCol.z || headerCol.w != bodyCol.w) {
        // A level-1 panel is rounded, so its header must round its TOP corners
        // with the same radius (a flat rect would overflow the corners). Deeper
        // panels are square, so no rounding there.
        const ImDrawFlags rf = level1 ? ImDrawFlags_RoundCornersTop
                                       : ImDrawFlags_RoundCornersNone;
        dl->AddRectFilled(headerMin, headerMax,
                          ImGui::ColorConvertFloat4ToU32(headerCol),
                          level1 ? radius : 0.0f, rf);
    }

    // The reset-badge / close-cross slot is ALWAYS reserved (drawn only when
    // overridden / closable), so the header inline editor never shifts.
    const float badgeW  = headerH;
    const float inlineW = cfg.headerInlineWidth;
    const float toggleW = innerW - badgeW - inlineW;
    ImGui::SetCursorScreenPos(headerMin);
    ImGui::InvisibleButton("##hdr", ImVec2(std::max(1.0f, toggleW), headerH));
    if (g_list.active) {
        // List item: the header is a click-OR-drag handle. The press is only
        // RECORDED here; the list step (BeginPanelListItem, index 0) promotes
        // it to a drag past the threshold and performs the live reordering. A
        // press released WITHOUT ever dragging toggles open/closed — release,
        // not click, is what lets drag and expand share the header.
        if (ImGui::IsItemActivated()) {
            g_list.st->SetInt(g_list.kPress, g_list.index);
            g_list.st->SetFloat(g_list.kPressY, ImGui::GetIO().MousePos.y);
        }
        if (ImGui::IsItemDeactivated() &&
            !g_list.st->GetBool(g_list.kDragged, false) &&
            ImGui::IsItemHovered()) {
            open = !open; st->SetBool(openKey, open);
        }
    } else if (ImGui::IsItemClicked()) {
        open = !open; st->SetBool(openKey, open);
    }

    auto& im = VectorGraphics::IconManager::Instance();
    const ImVec4 textV = Col(Tok::C_Panel_Text);
    float cx = headerMin.x + pad + depthIndent;
    {
        const char* gl = open ? "chevron-down" : "chevron-right";
        auto md = im.GetDefaultMetadata(gl);
        for (auto& z : md.colorZones) z.customColor = textV;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, gl, ImVec2(cx, headerMin.y + (headerH - chev) * 0.5f),
                          chev, md);
        cx += chev + gap;
    }
    if (cfg.icon && *cfg.icon) {
        auto md = im.GetDefaultMetadata(cfg.icon);
        for (auto& z : md.colorZones) z.customColor = textV;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, cfg.icon, ImVec2(cx, headerMin.y + (headerH - iconSz) * 0.5f),
                          iconSz, md);
        cx += iconSz + gap;
    }
    {
        ImVec2 ts = ImGui::CalcTextSize(cfg.label);
        dl->AddText(ImVec2(cx, headerMin.y + (headerH - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(textV), cfg.label);
    }

    if (inlineW > 0.0f) {
        res.inlineMin = ImVec2(headerMax.x - badgeW - inlineW, headerMin.y);
        res.inlineMax = ImVec2(headerMax.x - badgeW, headerMax.y);
    }

    // Override → a reset icon button on the right (reset-settings icon). Shown
    // on this panel AND on every ancestor (the caller computes hasOverride for
    // the whole subtree), so a modified descendant is visible at any level.
    if (cfg.hasOverride) {
        ImVec2 bMin(headerMax.x - badgeW, headerMin.y);
        ImGui::SetCursorScreenPos(bMin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##ovr", ImVec2(badgeW, headerH));
        bool bHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) res.resetClicked = true;
        const float bi = iconSz;
        ImVec2 ip(bMin.x + (badgeW - bi) * 0.5f, headerMin.y + (headerH - bi) * 0.5f);
        ImVec4 tint = Col(Tok::C_Panel_OverrideBadge);
        if (bHov) tint = Col(Tok::S_Color_Text_Default);
        auto md = im.GetDefaultMetadata("reset-settings");
        for (auto& z : md.colorZones) z.customColor = tint;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, "reset-settings", ip, bi, md);
        if (bHov) ImGui::SetTooltip("Reset all overrides under this panel");
    }

    // Close → a small cross in the right-hand slot (reorderable paint / modifier
    // panels). Drawn on TOP of the header handle (AllowOverlap) so clicking the
    // cross removes the item instead of dragging / toggling the panel.
    if (cfg.closable) {
        ImVec2 bMin(headerMax.x - badgeW, headerMin.y);
        ImGui::SetCursorScreenPos(bMin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##close", ImVec2(badgeW, headerH));
        bool bHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) res.closeClicked = true;
        const float bi = iconSz;
        ImVec2 ip(bMin.x + (badgeW - bi) * 0.5f, headerMin.y + (headerH - bi) * 0.5f);
        ImVec4 tint = bHov ? Col(Tok::S_Color_Text_Default) : Col(Tok::C_Panel_Text);
        auto md = im.GetDefaultMetadata("close");
        for (auto& z : md.colorZones) z.customColor = tint;
        if (!md.colorZones.empty())
            im.RenderIcon(dl, "close", ip, bi, md);
    }

    // Cursor below the header for body content.
    ImGui::SetCursorScreenPos(ImVec2(headerMin.x, headerMax.y));
    // Always submit an item right after the SetCursorScreenPos so it never sits
    // as the LAST call before EndChild: the header is drawn via InvisibleButtons
    // and the cursor is then moved to headerMax.y, but sub-pixel rounding of the
    // button's item rect can leave CursorPos.y a hair above CursorMaxPos.y. A
    // trailing SetCursorScreenPos with no following item then trips ImGui's
    // "uses SetCursorPos() to extend parent boundaries" assert (it fired on the
    // Keymap page where every level-1 category panel starts collapsed). The
    // open inset (4px) doubles as that item; a collapsed panel still needs a
    // zero-height Dummy to validate the cursor.
    ImGui::Dummy(ImVec2(0.0f, open ? 4.0f * gs : 0.0f));   // inset before content

    PanelFrame f;
    f.depth   = depth;
    f.level1  = level1;
    f.open    = open;
    f.topLeft = headerMin;
    f.width   = innerW;
    f.radius  = radius;
    f.borderW = borderW;   // already gated by the global borders toggle above
    f.spacingPushed = spacingPushed;
    Stack().push_back(f);

    res.open = open;
    return res;
}

void EndPanel() {
    if (Stack().empty()) return;
    PanelFrame f = Stack().back();
    Stack().pop_back();

    // Bottom inset (open level-1 panels) so the last content clears the rounded
    // bottom corners — added here, not as window padding, so the header stays
    // flush at the top.
    if (f.open && f.level1) {
        const float gs = DS::DesignSystem::Instance().GetGlobalScale();
        ImGui::Dummy(ImVec2(0.0f, Flt(Tok::C_Panel_CornerRadius) * gs));
    }

    // Pop the in-child "restore normal spacing" var BEFORE EndChild (it was
    // pushed while the child was current).
    if (f.spacingPushed) ImGui::PopStyleVar();   // inner ItemSpacing

    ImGui::EndChild();              // ##panel
    ImGui::PopStyleVar(3);          // ChildRounding + ChildBorderSize + WindowPadding
    ImGui::PopStyleColor(2);        // ChildBg + Border
    ImGui::PopID();

    // Pop the parent "zeroed inter-panel spacing" var AFTER EndChild + PopID,
    // matching the order it was pushed in BeginPanel (before PushID/BeginChild).
    if (f.spacingPushed) ImGui::PopStyleVar();   // parent ItemSpacing
    (void)f;
}

// ── Reorderable panel list ────────────────────────────────────────────────────

PanelResult BeginPanelListItem(const PanelConfig& cfg, int index, int count,
                               PanelListEdit& edit) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    g_list.active   = true;
    g_list.index    = index;
    g_list.count    = count;
    g_list.itemId   = cfg.id;
    g_list.st       = st;
    g_list.kDrag    = ImGui::GetID("##plistDrag");
    g_list.kPress   = ImGui::GetID("##plistPress");
    g_list.kPressY  = ImGui::GetID("##plistPressY");
    g_list.kDragged = ImGui::GetID("##plistDragged");
    g_list.edit     = &edit;

    // The list step runs ONCE per frame, on the first item: end/promote the
    // press, and while a drag is live, reorder when the cursor crosses a
    // neighbour's midline (last frame's item rects — one-frame lag is fine).
    if (index == 0) {
        int drag = st->GetInt(g_list.kDrag, -1);
        if (drag >= count) { drag = -1; st->SetInt(g_list.kDrag, -1); }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Release: end the drag but keep the "dragged" flag for THIS frame —
            // the headers' IsItemDeactivated runs later this frame and must not
            // treat a drag release as a collapse click.
            if (drag >= 0) st->SetInt(g_list.kDrag, -1);
            else           st->SetBool(g_list.kDragged, false);
            st->SetInt(g_list.kPress, -1);
        } else {
            const float my = ImGui::GetIO().MousePos.y;
            const int press = st->GetInt(g_list.kPress, -1);
            if (drag < 0 && press >= 0 && press < count) {
                const float py = st->GetFloat(g_list.kPressY, my);
                if (std::fabs(my - py) > ImGui::GetIO().MouseDragThreshold) {
                    drag = press;
                    st->SetInt(g_list.kDrag, drag);
                    st->SetBool(g_list.kDragged, true);
                }
            }
            if (drag >= 0) {
                auto mid = [&](int i) {
                    const float y0 = st->GetFloat(ListRectKey(i, true), 0.0f);
                    const float y1 = st->GetFloat(ListRectKey(i, false), 0.0f);
                    return (y0 + y1) * 0.5f;
                };
                if (drag > 0 && my < mid(drag - 1)) {
                    edit.moveFrom = drag; edit.moveTo = drag - 1;
                } else if (drag + 1 < count && my > mid(drag + 1)) {
                    edit.moveFrom = drag; edit.moveTo = drag + 1;
                }
                // The persisted-state swap + drag-index update are applied at
                // the END of the list (last EndPanelListItem), so this frame
                // still draws consistently in the old order.
            }
        }
    }

    ImGui::PushID(index);
    PanelResult r = BeginPanel(cfg);
    if (r.closeClicked) edit.removeAt = index;
    return r;
}

void EndPanelListItem() {
    EndPanel();
    ImGui::PopID();
    ImGuiStorage* st = g_list.st;

    // Record this item's rect (list scope) for next frame's midline tests; the
    // panel child window is the last submitted item after EndPanel.
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    st->SetFloat(ListRectKey(g_list.index, true),  mn.y);
    st->SetFloat(ListRectKey(g_list.index, false), mx.y);

    // The dragged item carries an accent outline (Blender's lifted panel).
    if (st->GetInt(g_list.kDrag, -1) == g_list.index) {
        const float gs = DS::DesignSystem::Instance().GetGlobalScale();
        ImGui::GetWindowDrawList()->AddRect(
            mn, mx,
            ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Accent_Default)),
            Flt(Tok::C_Panel_CornerRadius) * gs, 0, 1.0f * gs);
    }

    // Last item and a move is pending: swap the persisted per-item state (open
    // flag) and the cached rects NOW — after every panel drew with the old
    // order — so next frame the caller-applied data order and the panel states
    // agree. The drag index follows the item to its new slot.
    if (g_list.index + 1 == g_list.count && g_list.edit &&
        g_list.edit->moveFrom >= 0) {
        const int a = g_list.edit->moveFrom, b = g_list.edit->moveTo;
        ListSwapItemState(st, g_list.itemId, a, b);
        for (int t = 0; t < 2; ++t) {
            const ImGuiID ka = ListRectKey(a, t == 0);
            const ImGuiID kb = ListRectKey(b, t == 0);
            const float va = st->GetFloat(ka, 0.0f), vb = st->GetFloat(kb, 0.0f);
            st->SetFloat(ka, vb);
            st->SetFloat(kb, va);
        }
        if (st->GetInt(g_list.kDrag, -1) == a) st->SetInt(g_list.kDrag, b);
    }

    g_list.active = false;
    g_list.edit   = nullptr;
}

} // namespace UI

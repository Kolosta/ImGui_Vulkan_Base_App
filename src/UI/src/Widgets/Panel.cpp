#include <UI/Widgets/Panel.h>
#include <UI/Widgets/IconWidgets.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>
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
// gating). List state (drag index, press, per-item rects/offsets) lives in the
// LIST window's ImGuiStorage so it survives frames; the keys are computed at
// list scope (outside the per-item PushID) so every item reads the same slots.
struct ListCtx {
    bool           active   = false;
    int            index    = 0;
    int            count    = 0;
    const char*    itemId   = "";        // cfg.id — identical for every item
    ImGuiStorage*  st       = nullptr;   // the list window's storage
    ImGuiID        kDrag    = 0;         // int: index being dragged (-1 none)
    ImGuiID        kPress   = 0;         // int: index pressed, pre-threshold
    ImGuiID        kPressY  = 0;         // float: mouse y at press
    ImGuiID        kGrabDY  = 0;         // float: press y − item top (grab point)
    ImGuiID        kDragged = 0;         // bool: this hold entered a drag
    ImGuiID        kFloat   = 0;         // float: dragged item's floating top
    ImGuiID        kTarget  = 0;         // int: current insertion slot
    ImGuiID        kDropSlot  = 0;       // int: slot just dropped into (-1)
    ImGuiID        kDropFloat = 0;       // float: float pos at the drop
    PanelListEdit* edit     = nullptr;
    // Per-item bookkeeping between Begin and End of the same item.
    float          naturalY = 0.0f;      // layout position before any offset
    float          placedY  = 0.0f;      // where the panel was actually placed
    float          x        = 0.0f;
};
ListCtx g_list;

// Per-index storage keys at LIST scope (call outside the item's PushID).
ImGuiID ListKey(const char* tag, int index) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "##pl%s%d", tag, index);
    return ImGui::GetID(buf);
}
ImGuiID ListRectKey(int index, bool top) { return ListKey(top ? "Rt" : "Rb", index); }

// MOVE the persisted per-item panel state (the "##open" flag) with the item:
// item `from` goes to slot `to`, the ones in between shift by one — the exact
// rotation the caller applies to its data.
void ListRotateItemState(ImGuiStorage* st, const char* itemId, int from, int to) {
    auto key = [&](int i) {
        ImGui::PushID(i);
        ImGui::PushID(itemId);
        const ImGuiID k = ImGui::GetID("##open");
        ImGui::PopID();
        ImGui::PopID();
        return k;
    };
    const bool moved = st->GetBool(key(from), true);
    if (from < to) {
        for (int i = from; i < to; ++i)
            st->SetBool(key(i), st->GetBool(key(i + 1), true));
    } else {
        for (int i = from; i > to; --i)
            st->SetBool(key(i), st->GetBool(key(i - 1), true));
    }
    st->SetBool(key(to), moved);
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
    // The GRABBED list panel is raised above its siblings via BeginOrderWithin
    // Parent. But ImGui paints a child's ChildBg into the PARENT draw list (in
    // SUBMISSION order), so when it is dragged DOWNWARD its background renders
    // under the next panel even though its content (child draw list) is raised.
    // Fix: for the grabbed panel, suppress the native ChildBg and repaint the
    // body INSIDE the child draw list, so it rises together with the content.
    const bool isGrabbed = g_list.active &&
        g_list.st && g_list.st->GetInt(g_list.kDrag, -1) == g_list.index;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          isGrabbed ? ImVec4(0, 0, 0, 0) : Col(bodyTok));
    ImGui::PushStyleColor(ImGuiCol_Border, Col(Tok::C_Panel_Border));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, level1 ? radius : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, borderW);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiChildFlags childFlags = ImGuiChildFlags_AutoResizeY;
    if (level1) childFlags |= ImGuiChildFlags_Borders;
    ImGui::BeginChild("##panel", ImVec2(fullW, 0.0f), childFlags,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    // Repaint the suppressed body into the CHILD draw list (behind all content),
    // so the grabbed panel's background travels with it above the siblings. The
    // child auto-resizes in Y, so its rect here is last frame's size — identical
    // during a drag (the panel doesn't resize while grabbed), so it is exact.
    if (isGrabbed) {
        const ImRect cr = ImGui::GetCurrentWindow()->Rect();
        if (cr.GetHeight() > 1.0f)
            ImGui::GetWindowDrawList()->AddRectFilled(
                cr.Min, cr.Max,
                ImGui::ColorConvertFloat4ToU32(Col(bodyTok)),
                level1 ? radius : 0.0f);
    }

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
            // Grab point inside the item, so the floating panel tracks the
            // cursor from where it was picked up (not from its top edge).
            g_list.st->SetFloat(g_list.kGrabDY,
                                ImGui::GetIO().MousePos.y - headerMin.y);
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
    g_list.kGrabDY  = ImGui::GetID("##plistGrabDY");
    g_list.kDragged = ImGui::GetID("##plistDragged");
    g_list.kFloat     = ImGui::GetID("##plistFloat");
    g_list.kTarget    = ImGui::GetID("##plistTarget");
    g_list.kDropSlot  = ImGui::GetID("##plistDropSlot");
    g_list.kDropFloat = ImGui::GetID("##plistDropFloat");
    g_list.edit       = &edit;

    // ── The list step: once per frame, on the first item ─────────────────────
    // Promote a press to a drag past the threshold; while dragging, follow the
    // mouse with the grabbed item (kFloat) and derive the INSERTION slot
    // (kTarget) from the other items' last-frame midlines; on release, emit
    // the move (applied by the caller + the last EndPanelListItem).
    if (index == 0) {
        int drag = st->GetInt(g_list.kDrag, -1);
        if (drag >= count) { drag = -1; st->SetInt(g_list.kDrag, -1); }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (drag >= 0) {
                // Release: commit the move. Keep the "dragged" flag for THIS
                // frame — the headers' IsItemDeactivated runs later and must
                // not treat a drag release as a collapse click. The dropped
                // slot + float position are remembered so NEXT frame (once
                // the caller applied the move and the natural layout is the
                // new one) the panel glides from where it was released.
                const int t = st->GetInt(g_list.kTarget, drag);
                if (t >= 0 && t < count) {
                    if (t != drag) {
                        edit.moveFrom = drag;
                        edit.moveTo   = t;
                    }
                    st->SetInt(g_list.kDropSlot, t);
                    st->SetFloat(g_list.kDropFloat,
                                 st->GetFloat(g_list.kFloat, 0.0f));
                }
                st->SetInt(g_list.kDrag, -1);
                st->SetInt(g_list.kTarget, -1);
            } else {
                st->SetBool(g_list.kDragged, false);
            }
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
                    st->SetInt(g_list.kTarget, drag);
                }
            }
            if (drag >= 0) {
                // Auto-scroll: dragging near (or past) the top/bottom edge of
                // the scroll window shifts the view, so long stacks can be
                // traversed without dropping (Blender). Proportional to the
                // overshoot, framerate-independent.
                {
                    const float margin = 28.0f;
                    const float wTop = ImGui::GetWindowPos().y;
                    const float wBot = wTop + ImGui::GetWindowHeight();
                    float over = 0.0f;
                    if (my < wTop + margin)      over = my - (wTop + margin);
                    else if (my > wBot - margin) over = my - (wBot - margin);
                    if (over != 0.0f) {
                        const float k = std::clamp(
                            ImGui::GetIO().DeltaTime * 10.0f, 0.0f, 1.0f);
                        ImGui::SetScrollY(ImGui::GetScrollY() + over * k);
                    }
                }
                // Floating position (child-top space), clamped to the LIST's
                // natural top and the WINDOW's bottom — the panel can be
                // dragged through any empty space below the stack, never only
                // to the last item's edge. Natural (offset-free) positions
                // only: clamping against the visual rects feeds back (the
                // pinned panel crept downward forever).
                const float dragH =
                    st->GetFloat(ListRectKey(drag, false), 0.0f) -
                    st->GetFloat(ListRectKey(drag, true), 0.0f);
                const float gapD  = st->GetFloat(ListKey("Gap", drag), 0.0f);
                const float natTop =
                    st->GetFloat(ListKey("Nat", 0), 0.0f) + gapD;
                const float lastH =
                    st->GetFloat(ListRectKey(count - 1, false), 0.0f) -
                    st->GetFloat(ListRectKey(count - 1, true), 0.0f);
                const float natBottom =
                    st->GetFloat(ListKey("Nat", count - 1), natTop) +
                    st->GetFloat(ListKey("Gap", count - 1), 0.0f) + lastH;
                const float winBottom =
                    ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
                const float bottom = std::max(natBottom, winBottom);
                float ft = my - st->GetFloat(g_list.kGrabDY, dragH * 0.5f);
                if (bottom > natTop)
                    ft = std::clamp(ft, natTop,
                                    std::max(natTop, bottom - dragH));
                st->SetFloat(g_list.kFloat, ft);
                // Insertion slot = how many OTHER items sit above the floating
                // centre (their shifted, visual midlines from last frame).
                const float fc = ft + dragH * 0.5f;
                int t = 0;
                for (int i = 0; i < count; ++i) {
                    if (i == drag) continue;
                    const float m0 = st->GetFloat(ListRectKey(i, true), 0.0f);
                    const float m1 = st->GetFloat(ListRectKey(i, false), 0.0f);
                    if ((m0 + m1) * 0.5f < fc) ++t;
                }
                st->SetInt(g_list.kTarget, std::clamp(t, 0, count - 1));
            }
        }
    }

    // ── Per-item placement: natural flow + animated offset ───────────────────
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    g_list.naturalY = cur.y;
    g_list.x        = cur.x;
    st->SetFloat(ListKey("Nat", index), cur.y);
    const int   drag = st->GetInt(g_list.kDrag, -1);
    const int   tgt  = st->GetInt(g_list.kTarget, -1);
    const float adv  = st->GetFloat(ListKey("Adv", drag < 0 ? 0 : drag), 0.0f);
    float target = 0.0f;
    float off    = st->GetFloat(ListKey("Off", index), 0.0f);
    if (drag >= 0 && index == drag) {
        // The grabbed panel tracks the cursor directly (no smoothing). kFloat
        // is in CHILD-TOP space; the cursor sits one panel gap above it.
        const float gap = st->GetFloat(ListKey("Gap", index), 0.0f);
        off = (st->GetFloat(g_list.kFloat, cur.y + gap) - gap) - cur.y;
    } else if (st->GetInt(g_list.kDropSlot, -1) == index) {
        // First frame after a drop: the caller applied the move, this slot now
        // holds the dropped item and its NATURAL position is known — start the
        // settle exactly where the panel was released and glide to rest (the
        // smoothing takes over next frame). Computing this against the real
        // new layout is what kills the release "teleport".
        st->SetInt(g_list.kDropSlot, -1);
        const float gap = st->GetFloat(ListKey("Gap", index), 0.0f);
        off = (st->GetFloat(g_list.kDropFloat, cur.y + gap) - gap) - cur.y;
    } else {
        if (drag >= 0 && tgt >= 0) {
            // Neighbours slide out of / into the vacated slot (Blender): the
            // items between the origin and the insertion point shift by the
            // dragged item's advance.
            if (drag < tgt && index > drag && index <= tgt)      target = -adv;
            else if (tgt < drag && index >= tgt && index < drag) target = +adv;
        }
        const float k = std::clamp(ImGui::GetIO().DeltaTime * 14.0f, 0.0f, 1.0f);
        off += (target - off) * k;
        if (std::fabs(off) < 0.25f && target == 0.0f) off = 0.0f;
    }
    st->SetFloat(ListKey("Off", index), off);
    g_list.placedY = g_list.naturalY + off;
    if (off != 0.0f)
        ImGui::SetCursorScreenPos(ImVec2(g_list.x, g_list.placedY));

    ImGui::PushID(index);
    PanelResult r = BeginPanel(cfg);
    // The grabbed panel draws IN FRONT of every sibling — purely visual: the
    // renderer sorts sibling child windows by BeginOrderWithinParent, so
    // giving the dragged one the highest order makes it render last (on top)
    // without touching the submission or data order.
    if (drag >= 0 && index == drag)
        ImGui::GetCurrentWindow()->BeginOrderWithinParent = 0x7FFF;
    if (r.closeClicked) edit.removeAt = index;
    return r;
}

void EndPanelListItem() {
    EndPanel();
    ImGui::PopID();
    ImGuiStorage* st = g_list.st;
    const int index = g_list.index;

    // Record this item's VISUAL rect (list scope) for next frame's insertion
    // math; the panel child window is the last submitted item after EndPanel.
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    st->SetFloat(ListRectKey(index, true),  mn.y);
    st->SetFloat(ListRectKey(index, false), mx.y);
    // The panel's leading gap (a level-1 panel opens with a gap Dummy before
    // its child) — converts between cursor space and child-top space.
    st->SetFloat(ListKey("Gap", index), mn.y - g_list.placedY);

    // Natural advance (gap + panel + spacing) and flow restore: the next item
    // lays out from the NATURAL position, not the offset one.
    const float afterY  = ImGui::GetCursorScreenPos().y;
    const float advance = afterY - g_list.placedY;
    st->SetFloat(ListKey("Adv", index), advance);
    ImGui::SetCursorScreenPos(ImVec2(g_list.x, g_list.naturalY + advance));

    // The grabbed panel carries an accent outline — on the FOREGROUND list,
    // since the panel itself is raised above its siblings while dragged.
    if (st->GetInt(g_list.kDrag, -1) == index) {
        const float gs = DS::DesignSystem::Instance().GetGlobalScale();
        ImGui::GetForegroundDrawList()->AddRect(
            mn, mx,
            ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Accent_Default)),
            Flt(Tok::C_Panel_CornerRadius) * gs, 0, 1.0f * gs);
    }

    if (index + 1 == g_list.count) {
        // A move committed this frame (mouse released): move the persisted
        // open flags WITH the item and zero the offsets — the caller applies
        // the same rotation to its data after the loop; the drop marker
        // (kDropSlot) re-seeds the dropped panel's offset NEXT frame against
        // the real new layout, so it glides from where it was released.
        if (g_list.edit && g_list.edit->moveFrom >= 0) {
            const int from = g_list.edit->moveFrom, to = g_list.edit->moveTo;
            ListRotateItemState(st, g_list.itemId, from, to);
            for (int i = 0; i < g_list.count; ++i)
                st->SetFloat(ListKey("Off", i), 0.0f);
        }

        // The flow-restore SetCursorScreenPos above may EXTEND the window
        // (an item drawn above its natural slot restores DOWNWARD); ImGui
        // asserts on a trailing cursor move that grows the parent without a
        // following item. Validate with a zero-size, zero-spacing item.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::PopStyleVar();
    }

    g_list.active = false;
    g_list.edit   = nullptr;
}

} // namespace UI

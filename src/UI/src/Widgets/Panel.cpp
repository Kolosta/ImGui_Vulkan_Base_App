#include <UI/Widgets/Panel.h>
#include <UI/Widgets/IconWidgets.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <vector>
#include <cmath>

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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col(BodyTokForDepth(depth)));
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
    const ImVec4 bodyCol   = Col(BodyTokForDepth(depth));
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

    // The reset-badge slot is ALWAYS reserved (drawn only when overridden), so
    // the header inline editor never shifts when the badge appears/disappears.
    const float badgeW  = headerH;
    const float inlineW = cfg.headerInlineWidth;
    const float toggleW = innerW - badgeW - inlineW;
    ImGui::SetCursorScreenPos(headerMin);
    ImGui::InvisibleButton("##hdr", ImVec2(std::max(1.0f, toggleW), headerH));
    if (ImGui::IsItemClicked()) { open = !open; st->SetBool(openKey, open); }

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

} // namespace UI

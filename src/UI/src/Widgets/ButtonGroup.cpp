#include <UI/Widgets/ButtonGroup.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {

// Token-typed Safe* (Tok auto-follows TokName(): Spectrum-2 rename safe).
ImVec4 SafeColor(DesignSystem::Tok t, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(t); }
    catch (...) { return fallback; }
}
float SafeFloat(DesignSystem::Tok t, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(t); }
    catch (...) { return fallback; }
}

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// State priority: which cell "owns" a shared edge / draws on top.
//   3 = active (mouse down)   2 = hovered   1 = selected   0 = normal
int StatePriority(bool selected, bool hovered, bool active) {
    if (active)   return 3;
    if (hovered)  return 2;
    if (selected) return 1;
    return 0;
}

} // namespace

ImVec2 ButtonGroup::CalcSize() const {
    float w = 0.0f, h = 0.0f;
    for (float c : colW_) w += c;
    for (float r : rowH_) h += r;
    return ImVec2(w, h);
}

ButtonGroup::Result ButtonGroup::Render() {
    Result result;
    if (cells_.empty() || colW_.empty() || rowH_.empty()) return result;

    DesignSystem::DesignSystem::ComponentScope _cs("ButtonGroup");
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();

    // ── Tokens ──────────────────────────────────────────────────────────
    ImVec4 baseBg   = SafeColor(DesignSystem::Tok::C_Toggle_Background,
                                 ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImVec4 hoverBg  = SafeColor(DesignSystem::Tok::C_Toggle_BackgroundHover,
                                 ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
    ImVec4 activeBg = SafeColor(DesignSystem::Tok::C_Toggle_BackgroundSelected,
                                 ImVec4(0.22f, 0.45f, 0.85f, 1.0f));
    ImVec4 border   = SafeColor(DesignSystem::Tok::C_Toggle_Border,
                                 ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
    ImVec4 text     = SafeColor(DesignSystem::Tok::C_Toggle_Label,
                                 ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    ImVec4 activeTx = SafeColor(DesignSystem::Tok::C_Toggle_LabelSelected,
                                 ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImVec4 disabledTx = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
                                  ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    // Radius from the frame token (matches native inputs). Border colour +
    // width come from the TOGGLE tokens — a single neutral outline, never an
    // accent-coloured perimeter (the selected cell reads by its FILL alone,
    // like the tab-bar DropdownButtonRow).
    float radius      = SafeFloat(DesignSystem::Tok::C_Frame_CornerRadius, 4.0f) * scale;
    float borderSize  = SafeFloat(DesignSystem::Tok::C_Toggle_BorderWidth, 1.0f) * scale;

    // ── Column / row pixel offsets ──────────────────────────────────────
    std::vector<float> colX(colW_.size() + 1, 0.0f);
    for (size_t i = 0; i < colW_.size(); ++i) colX[i + 1] = colX[i] + colW_[i];
    std::vector<float> rowY(rowH_.size() + 1, 0.0f);
    for (size_t i = 0; i < rowH_.size(); ++i) rowY[i + 1] = rowY[i] + rowH_[i];

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 total  = CalcSize();

    ImGui::PushID(id_.c_str());
    // Wrap everything in a group so ImGui treats the whole control as a
    // single item with a correct bounding box.  Without this, the manual
    // SetCursorScreenPos() per cell desyncs ImGui's line-height tracking
    // and the next widget (SameLine neighbour, or the control below)
    // overlaps the group.
    ImGui::BeginGroup();

    struct Resolved {
        ImVec2 mn, mx;
        bool   selected, enabled, hovered, active;
        int    priority;
    };
    std::vector<Resolved> rc(cells_.size());

    // ── Pass 1: place an invisible/real button per cell ─────────────────
    // We use a real ImGui::Button (transparent colours, no rounding, no
    // native border) so keyboard nav / focus / activation all work, then
    // we paint fills + borders ourselves on top for pixel-perfect fused
    // edges with token colours.
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   0.0f);

    for (size_t i = 0; i < cells_.size(); ++i) {
        const Cell& c = cells_[i];
        int c0 = std::clamp(c.col, 0, (int)colW_.size() - 1);
        int r0 = std::clamp(c.row, 0, (int)rowH_.size() - 1);
        int c1 = std::clamp(c.col + c.colSpan, 1, (int)colW_.size());
        int r1 = std::clamp(c.row + c.rowSpan, 1, (int)rowH_.size());

        ImVec2 mn(origin.x + colX[c0], origin.y + rowY[r0]);
        ImVec2 mx(origin.x + colX[c1], origin.y + rowY[r1]);

        ImGui::SetCursorScreenPos(mn);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!c.enabled);
        bool clicked = ImGui::Button("##cell",
                                     ImVec2(mx.x - mn.x, mx.y - mn.y));
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();
        ImGui::EndDisabled();
        if (clicked && c.enabled) result.clickedIndex = static_cast<int>(i);
        if (hovered && c.enabled && !c.tooltip.empty())
            ImGui::SetTooltip("%s", c.tooltip.c_str());
        ImGui::PopID();

        rc[i] = { mn, mx, c.selected, c.enabled,
                  c.enabled && hovered, c.enabled && active,
                  StatePriority(c.selected, c.enabled && hovered,
                                c.enabled && active) };
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // A disabled cell is clearly dimmed (darker, desaturated) so the user
    // understands it is not clickable.
    ImVec4 disabledBg = Lerp(baseBg, ImVec4(0,0,0,baseBg.w), 0.35f);
    auto cellFill = [&](const Resolved& r) -> ImVec4 {
        if (!r.enabled) return disabledBg;
        if (r.selected) {
            if (r.active)  return Lerp(activeBg, ImVec4(0,0,0,1), 0.10f);
            if (r.hovered) return Lerp(activeBg, ImVec4(1,1,1,1), 0.08f);
            return activeBg;
        }
        if (r.active)  return Lerp(hoverBg, activeBg, 0.25f);
        if (r.hovered) return hoverBg;
        return baseBg;
    };
    auto cellBorder = [&](const Resolved& r) -> ImVec4 {
        if (!r.enabled) return Lerp(border, disabledBg, 0.55f);
        return border;   // always the neutral border — no accent perimeter
    };

    // Outer bounding box for corner rounding decisions.
    ImVec2 groupMn = origin;
    ImVec2 groupMx(origin.x + total.x, origin.y + total.y);

    auto cornerFlagsFor = [&](const ImVec2& mn, const ImVec2& mx) -> ImDrawFlags {
        ImDrawFlags f = ImDrawFlags_RoundCornersNone;
        const float eps = 0.5f;
        bool L = std::fabs(mn.x - groupMn.x) < eps;
        bool R = std::fabs(mx.x - groupMx.x) < eps;
        bool T = std::fabs(mn.y - groupMn.y) < eps;
        bool B = std::fabs(mx.y - groupMx.y) < eps;
        if (L && T) f |= ImDrawFlags_RoundCornersTopLeft;
        if (R && T) f |= ImDrawFlags_RoundCornersTopRight;
        if (R && B) f |= ImDrawFlags_RoundCornersBottomRight;
        if (L && B) f |= ImDrawFlags_RoundCornersBottomLeft;
        if (f == ImDrawFlags_RoundCornersNone)
            f = ImDrawFlags_RoundCornersNone;
        return f;
    };

    // ── Pass 2: fills (rounded only where the cell touches a group corner)
    for (const auto& r : rc) {
        dl->AddRectFilled(r.mn, r.mx,
                          ImGui::ColorConvertFloat4ToU32(cellFill(r)),
                          radius, cornerFlagsFor(r.mn, r.mx));
    }

    // ── Borders ─────────────────────────────────────────────────────────
    // Kept deliberately simple so it stays pixel-aligned with ImGui's own
    // frame rendering (which uses the same FrameRounding):
    //   • internal separators = single straight bord-to-bord lines
    //   • whole-group perimeter = ONE AddRect with the 4 corners rounded
    //     (ImGui handles the arc geometry, so no 1px drift)
    // The perimeter takes the highest-priority cell's border colour so a
    // hovered / active / selected cell still visually "owns" the outline.
    auto eq = [](float a, float b) { return std::fabs(a - b) < 0.5f; };
    auto borderU = [&](const Resolved& r) {
        return ImGui::ColorConvertFloat4ToU32(cellBorder(r));
    };

    // (1) Internal vertical separators.
    for (size_t a = 0; a < rc.size(); ++a) {
        for (size_t b = a + 1; b < rc.size(); ++b) {
            float x;
            if      (eq(rc[a].mx.x, rc[b].mn.x)) x = rc[a].mx.x;
            else if (eq(rc[b].mx.x, rc[a].mn.x)) x = rc[b].mx.x;
            else continue;
            float y0 = std::max(rc[a].mn.y, rc[b].mn.y);
            float y1 = std::min(rc[a].mx.y, rc[b].mx.y);
            if (y1 <= y0) continue;
            const Resolved& owner =
                rc[a].priority >= rc[b].priority ? rc[a] : rc[b];
            dl->AddLine(ImVec2(x, y0), ImVec2(x, y1),
                        borderU(owner), borderSize);
        }
    }
    // (2) Internal horizontal separators.
    for (size_t a = 0; a < rc.size(); ++a) {
        for (size_t b = a + 1; b < rc.size(); ++b) {
            float y;
            if      (eq(rc[a].mx.y, rc[b].mn.y)) y = rc[a].mx.y;
            else if (eq(rc[b].mx.y, rc[a].mn.y)) y = rc[b].mx.y;
            else continue;
            float x0 = std::max(rc[a].mn.x, rc[b].mn.x);
            float x1 = std::min(rc[a].mx.x, rc[b].mx.x);
            if (x1 <= x0) continue;
            const Resolved& owner =
                rc[a].priority >= rc[b].priority ? rc[a] : rc[b];
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y),
                        borderU(owner), borderSize);
        }
    }

    // (3) Whole-group rounded perimeter — single AddRect, ImGui-aligned. Always
    // the neutral border colour (the selection reads by its fill, not an accent
    // outline), so nothing frames the group in blue permanently.
    if (borderSize > 0.01f)
        dl->AddRect(groupMn, groupMx, ImGui::ColorConvertFloat4ToU32(border),
                    radius, ImDrawFlags_RoundCornersAll, borderSize);

    // ── Pass 6: icon (optional) + label (stripped of "##id") ─────────────
    // Center alignment keeps the classic look; Left alignment makes a
    // full-width "nav list" row: icon + label start at a left inset, vertically
    // centred — used by the Preferences left column.
    const float iconSz   = SafeFloat(DesignSystem::Tok::C_Dropdown_IconSize, 16.0f) * scale;
    const float leftPad  = SafeFloat(DesignSystem::Tok::C_Menu_ItemPaddingX, 6.0f) * scale;
    const float iconGap  = 6.0f * scale;
    auto& im = VectorGraphics::IconManager::Instance();
    for (size_t i = 0; i < cells_.size(); ++i) {
        const Cell& c = cells_[i];
        const Resolved& r = rc[i];
        const char* lbl = c.label.c_str();
        const char* lblEnd = lbl;
        while (*lblEnd && !(lblEnd[0] == '#' && lblEnd[1] == '#')) ++lblEnd;
        ImVec2 ts = ImGui::CalcTextSize(lbl, lblEnd);
        const bool hasIcon = !c.icon.empty();
        ImVec4 tc = !c.enabled ? disabledTx
                   : (c.selected ? activeTx : text);

        float cellH = r.mx.y - r.mn.y;
        float textY = r.mn.y + (cellH - ts.y) * 0.5f;
        float x;
        if (c.align == Align::Left) {
            x = r.mn.x + leftPad;
            if (hasIcon) {
                ImVec2 ip(x, r.mn.y + (cellH - iconSz) * 0.5f);
                auto md = im.GetDefaultMetadata(c.icon.c_str());
                for (auto& z : md.colorZones) z.customColor = tc;
                if (!md.colorZones.empty())
                    im.RenderIcon(dl, c.icon.c_str(), ip, iconSz, md);
                x += iconSz + iconGap;
            }
        } else {
            float contentW = ts.x + (hasIcon ? iconSz + iconGap : 0.0f);
            x = r.mn.x + (r.mx.x - r.mn.x - contentW) * 0.5f;
            if (hasIcon) {
                ImVec2 ip(x, r.mn.y + (cellH - iconSz) * 0.5f);
                auto md = im.GetDefaultMetadata(c.icon.c_str());
                for (auto& z : md.colorZones) z.customColor = tc;
                if (!md.colorZones.empty())
                    im.RenderIcon(dl, c.icon.c_str(), ip, iconSz, md);
                x += iconSz + iconGap;
            }
        }
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, textY),
                    ImGui::ColorConvertFloat4ToU32(tc), lbl, lblEnd);
    }

    // Reserve the exact group footprint, then close the group so ImGui's
    // line-height / cursor tracking is correct for the next widget.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(total);
    ImGui::EndGroup();

    ImGui::PopID();
    return result;
}

} // namespace UI

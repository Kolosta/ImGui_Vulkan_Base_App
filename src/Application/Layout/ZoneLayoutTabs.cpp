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
// Tab display name / icon resolved from the editor registry (falls back safely).
static const char* TabName(const Tab& t) {
    if (const EditorDescriptor* d = EditorRegistry::Instance().Get(t.editorId))
        return d->name.c_str();
    return t.editorId.c_str();
}
static const char* TabIcon(const Tab& t) {
    if (const EditorDescriptor* d = EditorRegistry::Instance().Get(t.editorId))
        return d->icon.c_str();
    return "";
}

void ZoneLayout::SplitLeafWithTab(Node* leaf, const Tab& moved,
                                  bool vertical, bool freshFirst) {
    auto oldContent = std::make_unique<Node>();
    oldContent->tabs      = std::move(leaf->tabs);
    oldContent->activeTab = leaf->activeTab;

    auto fresh = std::make_unique<Node>();
    fresh->tabs.push_back(moved);
    fresh->activeTab = 0;

    float ext = vertical ? leaf->size.x : leaf->size.y;
    float half = ext * 0.5f;

    leaf->tabs.clear();
    leaf->activeTab = 0;
    leaf->vertical = vertical;
    if (freshFirst) {
        leaf->a = std::move(fresh);
        leaf->b = std::move(oldContent);
    } else {
        leaf->a = std::move(oldContent);
        leaf->b = std::move(fresh);
    }
    leaf->firstPx = std::clamp(half, kMinZonePx_,
                               std::max(kMinZonePx_, ext - kMinZonePx_));
}

// Split `leaf` in two at the mouse, DUPLICATING its tabs into both halves
// (same editors, same camera/zoom snapshot, then independent — not synced).

void ZoneLayout::DrawTabBar(Node* n, float barH, ImVec2 origin, ImVec2 size) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    auto& iconMgr = VectorGraphics::IconManager::Instance();
    const float gs = ds.GetGlobalScale();
    ImVec2 tabPad = ds.GetVec2(DesignSystem::Tok::C_ZoneTab_Padding);
    tabPad.x *= gs; tabPad.y *= gs;
    const float iconSz = ds.GetFloat(DesignSystem::Tok::C_Dropdown_IconSize) * gs;
    const float gap    = 4.0f * gs;     // icon → title
    const float radius = ds.GetFloat(DesignSystem::Tok::S_CornerRadius_Control) * gs;

    const ImVec4 barBg   = ds.GetColor(DesignSystem::Tok::C_ZoneTab_BarBackground);
    const ImVec4 bgDef   = ds.GetColor(DesignSystem::Tok::C_ZoneTab_Background);
    const ImVec4 bgAct   = ds.GetColor(DesignSystem::Tok::C_ZoneTab_BackgroundActive);
    const ImVec4 bgHov   = ds.GetColor(DesignSystem::Tok::C_ZoneTab_BackgroundHover);
    const ImVec4 txtDef  = ds.GetColor(DesignSystem::Tok::C_ZoneTab_Text);
    const ImVec4 txtAct  = ds.GetColor(DesignSystem::Tok::C_ZoneTab_TextActive);

    // NoBackground: the rounded bar background is painted by DrawLeaf underneath
    // (so its top corners follow the zone radius). A ChildBg here would stack a
    // square rectangle over those rounded corners.
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##tabs", size, false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetWindowPos();   // screen-space top-left
    ImGuiIO& io = ImGui::GetIO();

    // Capture the tab rects into the shared scratch ONLY for the leaf the mouse
    // is over, so the drag's insertion line/hit-test always reads the bar under
    // the cursor (not whichever leaf happened to draw its bar last).
    const bool captureRects = (n == hoveredLeaf_);
    if (captureRects) { tabRects_.clear(); tabRectsLeaf_ = n; }

    const float tabGap = ds.GetFloat(DesignSystem::Tok::C_ZoneTab_Gap) * gs;
    const float tabH = barH - tabPad.y * 2.0f;   // == control-height
    const float barBottom = base.y + barH;       // where the menu bar begins

    // Draws a tab's icon + label, vertically centred in its box.
    auto drawTabContent = [&](const Tab& t, ImVec2 tabMin, const ImVec4& fg) {
        const char* name = TabName(t);
        const char* icon = TabIcon(t);
        ImVec2 ts = ImGui::CalcTextSize(name);
        auto md = iconMgr.GetDefaultMetadata(icon);
        md.scheme = VectorGraphics::IconColorScheme::Multicolor;
        for (auto& z : md.colorZones) z.customColor = fg;
        iconMgr.RenderIcon(dl, icon,
                           ImVec2(tabMin.x + tabPad.x,
                                  tabMin.y + (tabH - iconSz) * 0.5f),
                           iconSz, md);
        dl->AddText(ImVec2(tabMin.x + tabPad.x + iconSz + gap,
                           tabMin.y + (tabH - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(fg), name);
    };

    // Geometry pass: each tab's box (rects don't depend on draw order).
    std::vector<ImVec2> tabMins, tabMaxs;
    tabMins.reserve(n->tabs.size());
    tabMaxs.reserve(n->tabs.size());
    {
        float x = tabPad.x;
        for (int i = 0; i < (int)n->tabs.size(); ++i) {
            ImVec2 ts = ImGui::CalcTextSize(TabName(n->tabs[(size_t)i]));
            float tabW = tabPad.x + iconSz + gap + ts.x + tabPad.x;
            ImVec2 tabMin(base.x + x, base.y + tabPad.y);
            ImVec2 tabMax(tabMin.x + tabW, tabMin.y + tabH);
            tabMins.push_back(tabMin);
            tabMaxs.push_back(tabMax);
            if (captureRects) tabRects_.push_back({ tabMin, tabMax });
            x += tabW + tabGap;
        }
    }

    // Hit-test pass: an InvisibleButton per tab (selection + drag arm).
    std::vector<bool> tabHov(n->tabs.size(), false);
    for (int i = 0; i < (int)n->tabs.size(); ++i) {
        char bid[32];
        std::snprintf(bid, sizeof(bid), "##tab_%d", i);
        ImGui::SetCursorPos(ImVec2(tabMins[(size_t)i].x - base.x, tabPad.y));
        ImGui::InvisibleButton(bid, ImVec2(tabMaxs[(size_t)i].x - tabMins[(size_t)i].x, tabH));
        tabHov[(size_t)i] = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) {
            n->activeTab = i;
            activeLeaf_  = n;
            if (!tabDrag_.armed && !tabDrag_.active &&
                !addArm_.armed && !sepDragging_ &&
                !join_.active && !splitArm_.active) {
                tabDrag_.armed   = true;
                tabDrag_.srcLeaf = n;
                tabDrag_.srcTab  = i;
                tabDrag_.grabPos = io.MousePos;
            }
        }
    }
    const int activeIdx =
        (n->activeTab >= 0 && n->activeTab < (int)n->tabs.size()) ? n->activeTab : -1;

    ImU32 actCol = ImGui::ColorConvertFloat4ToU32(bgAct);
    ImU32 barCol = ImGui::ColorConvertFloat4ToU32(barBg);
    const float r = radius;
    const float PI = 3.14159265358979f;

    // Draw the active tab body FIRST (lowest), so a hovered neighbour painted
    // afterwards covers any overhang. The active tab is the menu-bar colour and
    // reaches the bar; its bottom corners flare OUT with concave fillets that
    // merge into the menu bar below.
    //
    // The whole shape is ONE filled path in actCol only — no re-paint of the
    // concave area with the bar colour. The bar background is already painted
    // underneath (by DrawLeaf), so the concave cut-outs simply expose whatever
    // is there, adapting automatically to any bar/editor colour (and to
    // transparency). Path, clockwise from the top-left rounded corner:
    //   TL convex → TR convex → down right side → right concave fillet flaring
    //   out to barBottom → across the bottom → left concave fillet back up.
    (void)barCol;
    if (activeIdx >= 0) {
        ImVec2 aMin = tabMins[(size_t)activeIdx], aMax = tabMaxs[(size_t)activeIdx];
        if (r > 0.5f) {
            dl->PathClear();
            // Top-left convex corner (center inside the tab).
            dl->PathArcTo(ImVec2(aMin.x + r, aMin.y + r), r, PI, 1.5f * PI, 12);
            // Top-right convex corner.
            dl->PathArcTo(ImVec2(aMax.x - r, aMin.y + r), r, 1.5f * PI, 2.0f * PI, 12);
            // Right concave fillet: arc centred OUTSIDE the tab (at aMax.x + r),
            // sweeping from the tab's right edge down-and-out to barBottom.
            dl->PathArcTo(ImVec2(aMax.x + r, barBottom - r), r, PI, 0.5f * PI, 12);
            // Left concave fillet: arc centred outside at aMin.x - r, sweeping
            // from the bottom back up to the tab's left edge.
            dl->PathArcTo(ImVec2(aMin.x - r, barBottom - r), r, 0.5f * PI, 0.0f, 12);
            // The shape is non-convex (the bottom fillets curve inward), so it
            // must be filled with the concave-aware polygon fill.
            dl->PathFillConcave(actCol);
        } else {
            dl->AddRectFilled(aMin, ImVec2(aMax.x, barBottom), actCol);
        }
    }

    // Inactive / hovered tabs (above the active nibs) + their content.
    for (int i = 0; i < (int)n->tabs.size(); ++i) {
        if (i == activeIdx) continue;
        ImVec2 tabMin = tabMins[(size_t)i], tabMax = tabMaxs[(size_t)i];
        if (tabHov[(size_t)i])
            dl->AddRectFilled(tabMin, ImVec2(tabMax.x, barBottom),
                              ImGui::ColorConvertFloat4ToU32(bgHov), radius,
                              ImDrawFlags_RoundCornersTop);
        drawTabContent(n->tabs[(size_t)i], tabMin, txtDef);
    }

    // Active tab content LAST, so its label/icon stay on top.
    if (activeIdx >= 0)
        drawTabContent(n->tabs[(size_t)activeIdx], tabMins[(size_t)activeIdx], txtAct);

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::Dummy(size);
    ImGui::EndChild(); // ##tabs
    (void)origin;
}

// Drop region of the mouse over leaf `n`: a central deadzone → Center, else the
// dominant edge (Left/Right/Top/Bottom) by nearest-side comparison.
ZoneLayout::DropRegion ZoneLayout::DropRegionAt(Node* n, ImVec2 m) const {
    if (!n) return DropRegion::None;
    float l = n->pos.x, t = n->pos.y;
    float r = n->pos.x + n->size.x, b = n->pos.y + n->size.y;
    if (m.x < l || m.x > r || m.y < t || m.y > b) return DropRegion::None;
    float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
    float halfW = n->size.x * 0.5f, halfH = n->size.y * 0.5f;
    // Center deadzone = inner 40% box (0.30 inset each side leaves 40%).
    float insetX = n->size.x * 0.30f, insetY = n->size.y * 0.30f;
    if (m.x > l + insetX && m.x < r - insetX &&
        m.y > t + insetY && m.y < b - insetY)
        return DropRegion::Center;
    // Normalised distance from center along each axis; the larger wins.
    float nx = (halfW > 0.0f) ? std::abs(m.x - cx) / halfW : 0.0f;
    float ny = (halfH > 0.0f) ? std::abs(m.y - cy) / halfH : 0.0f;
    if (nx >= ny) return (m.x < cx) ? DropRegion::Left : DropRegion::Right;
    return (m.y < cy) ? DropRegion::Top : DropRegion::Bottom;
}

// The rect a drop in `region` of `n` would occupy. Center = whole zone inset by
// `inset`; sides = the corresponding half.
ImVec4 ZoneLayout::DropTargetRect(Node* n, DropRegion region, float inset) const {
    float l = n->pos.x, t = n->pos.y;
    float r = n->pos.x + n->size.x, b = n->pos.y + n->size.y;
    float cx = (l + r) * 0.5f, cy = (t + b) * 0.5f;
    switch (region) {
        case DropRegion::Center:
            return ImVec4(l + inset, t + inset, r - inset, b - inset);
        case DropRegion::Left:   return ImVec4(l, t, cx, b);
        case DropRegion::Right:  return ImVec4(cx, t, r, b);
        case DropRegion::Top:    return ImVec4(l, t, r, cy);
        case DropRegion::Bottom: return ImVec4(l, cy, r, b);
        default:                 return ImVec4(l, t, r, b);
    }
}

// ── Once-per-frame tab-drag driver ───────────────────────────────────────────
// Promotes an armed drag past the threshold, draws the animated white drop
// preview + the in-bar insertion line, and on release reorders / moves /
// detaches the dragged tab. Drawn on the overlay draw list (called from the
// overlay child in Render, above the zones).
void ZoneLayout::UpdateTabDrag(float gap) {
    (void)gap;
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float gsc = ds.GetGlobalScale();
    ImGuiIO& io = ImGui::GetIO();

    if (!tabDrag_.armed && !tabDrag_.active) return;

    // The source leaf may have been destroyed by another gesture — bail safely.
    if (!NodeAlive(tabDrag_.srcLeaf)) {
        tabDrag_ = TabDrag{}; previewLeaf_ = nullptr; revealTarget_ = 0.0f; return;
    }

    // Promote armed → active past the move threshold.
    if (tabDrag_.armed && !tabDrag_.active) {
        const float thr = ds.GetFloat(DesignSystem::Tok::C_ZoneTab_DragThreshold) * gsc;
        ImVec2 d(io.MousePos.x - tabDrag_.grabPos.x,
                 io.MousePos.y - tabDrag_.grabPos.y);
        if (d.x * d.x + d.y * d.y >= thr * thr) {
            int st = std::clamp(tabDrag_.srcTab, 0,
                                (int)tabDrag_.srcLeaf->tabs.size() - 1);
            tabDrag_.payload = tabDrag_.srcLeaf->tabs[(size_t)st];
            tabDrag_.srcTab  = st;
            tabDrag_.active  = true;
            previewLeaf_     = nullptr;        // force an instant snap
        }
        // Released before moving enough → it was a plain click.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { tabDrag_ = TabDrag{}; return; }
    }
    if (!tabDrag_.active) return;

    // Cancel the whole drag with Esc or a right-click (nothing is moved). The
    // solo reveal collapses immediately (no hover-out delay).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        tabDrag_ = TabDrag{};
        previewLeaf_ = nullptr;
        revealTarget_ = 0.0f;
        return;
    }

    RequestCursor(Cursor::None);

    // Resolve the hovered target leaf + drop region.
    Node* target = overlayHov_ ? LeafAt(io.MousePos) : nullptr;
    DropRegion region = DropRegionAt(target, io.MousePos);

    // ── Solo-zone tab-bar reveal ──────────────────────────────────────────────
    // Hovering the menu-bar BAND (top control-height strip) of a SOLITARY zone
    // for a moment slides its 1-tab bar in, so the drag can be dropped into the
    // bar. Leaving the band collapses it again after a short delay.
    {
        const float controlH = ds.GetFloat(DesignSystem::Tok::S_Size_ControlHeight) * gsc;
        ImVec2 tpad = ds.GetVec2(DesignSystem::Tok::C_ZoneTab_Padding);
        const float bandH = controlH + tpad.y * 2.0f * gsc;
        bool soloTarget = target && target->isLeaf() &&
                          target->tabs.size() == 1 &&
                          ds.GetInt(DesignSystem::Tok::C_ZoneTab_ShowSolo) == 0;
        bool inBand = soloTarget &&
                      io.MousePos.y <= target->pos.y + bandH;
        const float kRevealIn  = 0.35f;   // dwell before it opens
        const float kRevealOut = 0.25f;   // grace before it closes
        if (inBand) {
            if (revealLeaf_ != target) { revealLeaf_ = target; revealDwell_ = 0.0f; revealTarget_ = 0.0f; }
            revealDwell_ += io.DeltaTime;
            if (revealDwell_ >= kRevealIn) revealTarget_ = 1.0f;
        } else if (revealLeaf_) {
            // Mouse left the band: after a grace period, collapse.
            revealDwell_ += io.DeltaTime;
            if (revealDwell_ >= kRevealOut) revealTarget_ = 0.0f;
            // Re-arm the in-timer if the cursor comes back.
            if (revealTarget_ == 1.0f) revealDwell_ = 0.0f;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();   // overlay draw list

    // ── In-bar reorder: if hovering the bar of the leaf under the cursor, show
    //    a vertical insertion line at the slot where the tab would land (it
    //    always lands to the RIGHT of the line). Only valid when the tab rects
    //    we captured belong to the leaf actually under the mouse. ──
    bool overBar = false;
    int  insertSlot = -1;
    // The bar must be STABLE before its insertion line shows: a permanent bar
    // (>1 tab or always-show), or a solo reveal whose slide-in has finished —
    // otherwise the line would pop over the menu bar mid-animation.
    bool barStable = false;
    if (target) {
        bool permanent = target->tabs.size() > 1 ||
            ds.GetInt(DesignSystem::Tok::C_ZoneTab_ShowSolo) != 0;
        bool revealedFull = (target == revealLeaf_) && revealAnim_ >= 0.95f;
        barStable = permanent || revealedFull;
    }
    const bool barIsHovered = barStable && tabRectsLeaf_ &&
                              tabRectsLeaf_ == target && !tabRects_.empty();
    if (barIsHovered) {
        ImVec2 b0 = tabRects_.front().mn, b1 = tabRects_.back().mx;
        if (io.MousePos.y >= b0.y && io.MousePos.y <= b1.y) {
            overBar = true;
            // Slot = number of tabs whose CENTRE is left of the cursor → the
            // tab lands just right of the line. Default past the last tab = end.
            insertSlot = (int)tabRects_.size();
            for (int i = 0; i < (int)tabRects_.size(); ++i) {
                float mid = (tabRects_[(size_t)i].mn.x + tabRects_[(size_t)i].mx.x) * 0.5f;
                if (io.MousePos.x < mid) { insertSlot = i; break; }
            }
            // Reordering within the SOURCE bar: inserting at srcTab or srcTab+1
            // is a no-op (the tab doesn't move), so clamp the shown line to the
            // tab's own current boundary instead of drifting one slot past the
            // end when the last tab is dragged further right.
            if (tabRectsLeaf_ == tabDrag_.srcLeaf) {
                if (insertSlot == tabDrag_.srcTab + 1) insertSlot = tabDrag_.srcTab + 1;
            }
        }
    }

    if (overBar) {
        // Line X: between tab[slot-1] and tab[slot]; centred in the inter-tab
        // gap. Slot 0 → left edge of the first tab; slot N → right edge of last.
        float lineX;
        int N = (int)tabRects_.size();
        if (insertSlot <= 0)        lineX = tabRects_.front().mn.x;
        else if (insertSlot >= N)   lineX = tabRects_.back().mx.x;
        else lineX = (tabRects_[(size_t)insertSlot - 1].mx.x +
                      tabRects_[(size_t)insertSlot].mn.x) * 0.5f;
        float y0 = tabRects_.front().mn.y, y1 = tabRects_.front().mx.y;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(
            ds.GetColor(DesignSystem::Tok::C_ZoneTab_InsertLineColor));
        float w = ds.GetFloat(DesignSystem::Tok::C_ZoneTab_InsertLineWidth) * gsc;
        dl->AddLine(ImVec2(lineX, y0), ImVec2(lineX, y1), col, w);
        previewLeaf_ = nullptr;   // no big preview while over a bar
    } else if (target) {
        // ── Big white drop preview, animated. ──
        const float inset = ds.GetFloat(DesignSystem::Tok::C_ZoneTab_DropCenterInset) * gsc;
        ImVec4 tgt = DropTargetRect(target, region, inset);
        if (previewLeaf_ != target || !previewInit_) {
            previewRect_ = tgt;            // instant snap on zone change
            previewLeaf_ = target;
            previewInit_ = true;
        } else {
            float tau = std::max(0.0001f,
                ds.GetFloat(DesignSystem::Tok::C_ZoneTab_PreviewAnimDuration));
            float a = 1.0f - std::exp(-io.DeltaTime / tau);
            previewRect_.x += (tgt.x - previewRect_.x) * a;
            previewRect_.y += (tgt.y - previewRect_.y) * a;
            previewRect_.z += (tgt.z - previewRect_.z) * a;
            previewRect_.w += (tgt.w - previewRect_.w) * a;
        }
        float rnd = ds.GetFloat(DesignSystem::Tok::C_Window_CornerRadius) * gsc;
        ImU32 fill = ImGui::ColorConvertFloat4ToU32(
            ds.GetColor(DesignSystem::Tok::C_ZoneTab_DropPreviewFill));
        dl->AddRectFilled(ImVec2(previewRect_.x, previewRect_.y),
                          ImVec2(previewRect_.z, previewRect_.w), fill, rnd);
    } else {
        previewLeaf_ = nullptr;
    }

    // ── Release: dispatch the drop. ──
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Node* src = tabDrag_.srcLeaf;
        int   srcTab = std::clamp(tabDrag_.srcTab, 0, (int)src->tabs.size() - 1);
        Tab   payload = tabDrag_.payload;
        bool  soleTab = src->tabs.size() == 1;

        auto eraseSrc = [&]() {
            if (!NodeAlive(src)) return;
            if (srcTab >= 0 && srcTab < (int)src->tabs.size())
                src->tabs.erase(src->tabs.begin() + srcTab);
            if (src->tabs.empty()) RemoveLeaf(src);
            else src->activeTab = std::clamp(src->activeTab, 0,
                                             (int)src->tabs.size() - 1);
        };

        // A drop onto the revealed bar of a solo zone (even if the cursor is in
        // its menu-bar band rather than precisely on a tab) inserts into it.
        bool intoReveal = target && target == revealLeaf_ && target != src &&
                          revealAnim_ > 0.5f && !overBar;

        if (overBar && tabRectsLeaf_) {
            Node* barLeaf = tabRectsLeaf_;
            if (barLeaf == src) {
                // Reorder within the same bar.
                int slot = std::clamp(insertSlot, 0, (int)src->tabs.size());
                if (slot > srcTab) slot -= 1;   // account for the erase
                Tab moved = src->tabs[(size_t)srcTab];
                src->tabs.erase(src->tabs.begin() + srcTab);
                src->tabs.insert(src->tabs.begin() + slot, moved);
                src->activeTab = slot;
            } else {
                // Insert into another zone's bar at the slot.
                int slot = std::clamp(insertSlot, 0, (int)barLeaf->tabs.size());
                barLeaf->tabs.insert(barLeaf->tabs.begin() + slot, payload);
                barLeaf->activeTab = slot;
                eraseSrc();
            }
        } else if (intoReveal) {
            target->tabs.push_back(payload);
            target->activeTab = (int)target->tabs.size() - 1;
            eraseSrc();
        } else if (target) {
            if (region == DropRegion::Center) {
                if (target == src) {
                    // Re-drop into its own zone → no structural change.
                } else {
                    target->tabs.push_back(payload);
                    target->activeTab = (int)target->tabs.size() - 1;
                    eraseSrc();
                }
            } else {
                // A side → create a new 50/50 zone.
                bool vertical   = (region == DropRegion::Left || region == DropRegion::Right);
                bool freshFirst = (region == DropRegion::Left || region == DropRegion::Top);
                if (target == src) {
                    if (!soleTab) {
                        // Detach: remove from src first, then split src.
                        src->tabs.erase(src->tabs.begin() + srcTab);
                        src->activeTab = std::clamp(src->activeTab, 0,
                                                    (int)src->tabs.size() - 1);
                        SplitLeafWithTab(src, payload, vertical, freshFirst);
                    }
                    // soleTab + side on its own zone → no-op.
                } else {
                    // Split the TARGET, then erase from src (src pointer stays
                    // valid: SplitLeafWithTab mutates target in place).
                    SplitLeafWithTab(target, payload, vertical, freshFirst);
                    eraseSrc();
                }
            }
        }
        tabDrag_ = TabDrag{};
        previewLeaf_ = nullptr;
        // Collapse the solo reveal after the drop. If the zone gained a 2nd tab
        // it now has a PERMANENT bar, so the animated reveal is no longer used.
        revealTarget_ = 0.0f;
    }
}

} // namespace App

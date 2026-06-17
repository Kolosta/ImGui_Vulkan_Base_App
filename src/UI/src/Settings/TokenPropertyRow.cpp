#include <UI/Settings/TokenPropertyRow.h>
#include <UI/Widgets/Panel.h>
#include <UI/Text/FontManager.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Override/OverrideManager.h>
#include <DesignSystem/Tokens/TokenRegistry.h>
#include <DesignSystem/Tokens/Token.h>
#include <VectorGraphics/IconManager.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
using DS::TokenValue;
using DS::ValueType;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// Resolve the value a token would have in `theme` if a given layer's override
// were ignored — i.e. the inherited/default value. We approximate "default" as
// the resolved value with no override considered: re-resolve the reference
// chain's terminal. Simpler: resolve in theme (overrides applied) for "actual",
// and use the schema default for "default" via the registry.
TokenValue Resolved(const std::string& id, DS::ThemeType theme) {
    return DS::DesignSystem::Instance().ResolveTokenValue(id, theme);
}

// Read the override value for a layer if present; else fall back to the resolved
// value (so the editor starts from the current effective value).
TokenValue LayerValueOrResolved(const std::string& id, bool global,
                                DS::ThemeType theme, bool& hasOverride) {
    auto& mgr = DS::DesignSystem::Instance().GetOverrideManager();
    DS::Override* o = mgr.GetOverride(id, global, theme);
    hasOverride = (o != nullptr);
    if (o) return o->GetValue();
    return Resolved(id, theme);
}

void WriteLayer(const std::string& id, bool global, DS::ThemeType theme,
                const TokenValue& v) {
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    if (global) mgr.AddOverride(DS::Override(id, v));
    else        mgr.AddOverride(DS::Override(id, v, theme));
    ds.NotifyOverrideChange();
    ds.ApplyGlobalStyle();   // make the edit visible live
}

void ClearLayer(const std::string& id, bool global, DS::ThemeType theme) {
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    if (global) mgr.RemoveGlobalOverride(id);
    else        mgr.RemoveThemeOverride(id, theme);
    ds.NotifyOverrideChange();
    ds.ApplyGlobalStyle();
}

// Draw a cubic-bezier easing curve (control points cp = {x1,y1,x2,y2}) inside
// a box at the cursor, of the given size. A flat, token-coloured preview: the
// unit square is mapped into the box (y flipped so the curve reads bottom-left
// → top-right like a standard easing plot). Endpoints are fixed at (0,0)/(1,1).
void DrawBezierBox(const ImVec4& cp, ImVec2 size) {
    auto& ds = DS::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(Col(Tok::C_Frame_Background)),
                      Flt(Tok::C_Frame_CornerRadius) * ds.GetGlobalScale());
    dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Border_Default)),
                Flt(Tok::C_Frame_CornerRadius) * ds.GetGlobalScale());
    // Map unit (u,v) → box pixels (v flipped).
    auto map = [&](float u, float w) {
        return ImVec2(p0.x + u * size.x, p1.y - w * size.y);
    };
    const ImU32 curveCol = ImGui::ColorConvertFloat4ToU32(Col(Tok::S_Color_Accent_Default));
    const int kSeg = 32;
    ImVec2 prev = map(0.0f, 0.0f);
    for (int i = 1; i <= kSeg; ++i) {
        float t = (float)i / (float)kSeg;
        float mt = 1.0f - t;
        // Cubic bezier with P0=(0,0), P1=(x1,y1), P2=(x2,y2), P3=(1,1).
        float x = 3*mt*mt*t*cp.x + 3*mt*t*t*cp.z + t*t*t;
        float y = 3*mt*mt*t*cp.y + 3*mt*t*t*cp.w + t*t*t;
        ImVec2 cur = map(x, y);
        dl->AddLine(prev, cur, curveCol, 1.5f * ds.GetGlobalScale());
        prev = cur;
    }
    // Reserve the box in layout.
    ImGui::Dummy(size);
}

// A typed value editor of a FIXED width (so every editor — colour, slider,
// vec — lines up at the same size). Returns true (and writes `v`) when edited.
// `defaultColor` (for colour types) is shown as a "Default" swatch inside the
// picker popup, below Current/Original. Numeric editors are bounded by the
// token's effective constraint.
bool ValueEditor(const char* id, TokenValue& v, const std::string& tokenId,
                 float width, const TokenValue* defaultVal) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    DS::ValueConstraint cn = ds.GetEffectiveConstraint(tokenId);
    const float h = ImGui::GetFrameHeight();
    bool changed = false;
    switch (v.GetType()) {
        case ValueType::Color: {
            ImVec4 c = v.AsColor();
            // A fixed-size swatch opens a colour picker popup on click. The
            // popup shows Current vs Original (ref_col) natively, plus a Default
            // swatch. Each control has a UNIQUE id (no ImGui id clash).
            ImGui::PushID(id);
            if (ImGui::ColorButton("##sw", c,
                    ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                    ImVec2(width, h)))
                ImGui::OpenPopup("##pickpop");
            // A small, token-driven popup padding so the picker isn't glued to
            // the edges (the menu padding token is the natural small inset).
            ImVec2 popPad = ds.GetVec2(Tok::C_Menu_Padding);
            popPad.x *= gs; popPad.y *= gs;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, popPad);
            if (ImGui::BeginPopup("##pickpop")) {
                // Remember the value when the popup opened → "Original".
                ImGuiStorage* ss = ImGui::GetStateStorage();
                ImGuiID rk = ImGui::GetID("##ref");
                if (ImGui::IsWindowAppearing())
                    ss->SetInt(rk, (int)ImGui::ColorConvertFloat4ToU32(c));
                ImVec4 ref = ImGui::ColorConvertU32ToFloat4((ImU32)ss->GetInt(rk,
                    (int)ImGui::ColorConvertFloat4ToU32(c)));

                // Picker WITHOUT the native side preview, so we can stack our own
                // Current / Original / Default swatches with labels on the right.
                if (ImGui::ColorPicker4("##pick", &c.x,
                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB |
                        ImGuiColorEditFlags_DisplayHex |
                        ImGuiColorEditFlags_NoSidePreview)) {
                    v.SetColor(c); changed = true;
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                // Big preview swatches (Current / Original / Default), stacked.
                const ImVec2 swSz(72.0f * gs, 40.0f * gs);
                const ImGuiColorEditFlags swFlags =
                    ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip;
                ImGui::TextUnformatted("Current");
                ImGui::ColorButton("##cur", c, swFlags, swSz);
                ImGui::TextUnformatted("Original");
                if (ImGui::ColorButton("##orig", ref, swFlags, swSz))
                { v.SetColor(ref); changed = true; }
                if (defaultVal && defaultVal->GetType() == ValueType::Color) {
                    ImGui::TextUnformatted("Default");
                    ImVec4 dc = defaultVal->AsColor();
                    if (ImGui::ColorButton("##defsw", dc, swFlags, swSz))
                    { v.SetColor(dc); changed = true; }
                }
                ImGui::EndGroup();
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();   // popup WindowPadding (pushed before BeginPopup)
            ImGui::PopID();
            break;
        }
        case ValueType::Float: {
            float f = v.AsFloat();
            float lo = cn.Min().has_value() ? (float)*cn.Min() : 0.0f;
            float hi = cn.Max().has_value() ? (float)*cn.Max() : 100.0f;
            ImGui::SetNextItemWidth(width);
            if (ImGui::DragFloat(id, &f, (hi - lo) * 0.005f + 0.001f, lo, hi, "%.2f")) {
                v.SetFloat(f); changed = true;
            }
            break;
        }
        case ValueType::Int: {
            int n = v.AsInt();
            int lo = cn.Min().has_value() ? (int)*cn.Min() : 0;
            int hi = cn.Max().has_value() ? (int)*cn.Max() : 100;
            // A 0/1 constraint is a boolean → show a checkbox (clearer than a
            // drag from 0 to 1), right-aligned to keep the column width.
            if (lo == 0 && hi == 1) {
                bool b = (n != 0);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     std::max(0.0f, width - ImGui::GetFrameHeight()));
                if (ImGui::Checkbox(id, &b)) { v.SetInt(b ? 1 : 0); changed = true; }
            } else {
                ImGui::SetNextItemWidth(width);
                if (ImGui::DragInt(id, &n, 1.0f, lo, hi)) { v.SetInt(n); changed = true; }
            }
            break;
        }
        case ValueType::Vec2: {
            ImVec2 vv = v.AsVec2();
            float a[2] = { vv.x, vv.y };
            float hi = cn.Max().has_value() ? (float)*cn.Max() : 100.0f;
            ImGui::SetNextItemWidth(width);
            if (ImGui::DragFloat2(id, a, hi * 0.005f + 0.001f, 0.0f, hi, "%.1f")) {
                v.SetVec2(ImVec2(a[0], a[1])); changed = true;
            }
            break;
        }
        case ValueType::Bezier: {
            // Easing curve: a curve preview stacked above the 4 control points
            // (x1,y1,x2,y2). The whole editor fits the fixed `width`, so the
            // global/theme columns still line up.
            ImVec4 cp = v.AsBezier();
            ImGui::PushID(id);
            DrawBezierBox(cp, ImVec2(width, width * 0.6f));
            float a[4] = { cp.x, cp.y, cp.z, cp.w };
            ImGui::SetNextItemWidth(width);
            // X stays in [0,1]; Y may overshoot (spring/overshoot) so allow a
            // wider range. DragFloat4 edits all four at once.
            if (ImGui::DragFloat4("##cp", a, 0.005f, -1.0f, 2.0f, "%.2f")) {
                if (a[0] < 0.0f) a[0] = 0.0f; if (a[0] > 1.0f) a[0] = 1.0f;
                if (a[2] < 0.0f) a[2] = 0.0f; if (a[2] > 1.0f) a[2] = 1.0f;
                v.SetBezier(ImVec4(a[0], a[1], a[2], a[3])); changed = true;
            }
            ImGui::PopID();
            break;
        }
        case ValueType::FontFamily: {
            // A combo of the font families discovered in the project (same
            // source as the classic Design System editor). Selecting one writes
            // the family name; ApplyFontTokens() re-reads it every frame, so the
            // default UI font changes live.
            std::string fam = v.AsFontFamily();
            ImGui::SetNextItemWidth(width);
            if (ImGui::BeginCombo(id, fam.empty() ? "(none)" : fam.c_str())) {
                for (const std::string& name : UI::FontManager::Instance().FamilyNames()) {
                    bool sel = (name == fam);
                    if (ImGui::Selectable(name.c_str(), sel)) {
                        v.SetFontFamily(name); changed = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        default:
            ImGui::TextDisabled("(not editable)");
            break;
    }
    return changed;
}

// The TRUE default value of a token (schema, ignoring ALL overrides): follow
// the token's own default value; if it is a reference, recurse into the target's
// default, until a concrete (non-reference) value. This is the pure default,
// distinct from the resolved value (which applies overrides).
TokenValue DefaultValue(const std::string& id, DS::ThemeType theme) {
    auto& reg = DS::DesignSystem::Instance().GetRegistry();
    std::string cur = id;
    for (int hops = 0; hops < 64; ++hops) {
        auto tok = reg.GetToken(cur);
        if (!tok) break;
        const TokenValue& dv = tok->GetDefaultValue();
        if (dv.GetType() != ValueType::Reference) return dv;
        cur = dv.AsReference();
    }
    // Fallback: resolved value if the default chain couldn't terminate.
    return DS::DesignSystem::Instance().ResolveTokenValue(id, theme);
}

// Small read-only preview of a value (swatch / number / vec). `pvId` must be
// unique within the window (ColorButton needs a distinct id, else ImGui warns).
void ValuePreview(const char* pvId, const TokenValue& v) {
    switch (v.GetType()) {
        case ValueType::Color: {
            ImVec4 c = v.AsColor();
            ImGui::ColorButton(pvId, c,
                ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                ImVec2(40, ImGui::GetFrameHeight()));
            break;
        }
        case ValueType::Float: ImGui::Text("%.3f", v.AsFloat()); break;
        case ValueType::Int:   ImGui::Text("%d", v.AsInt()); break;
        case ValueType::Vec2:  ImGui::Text("%.1f, %.1f", v.AsVec2().x, v.AsVec2().y); break;
        case ValueType::Bezier: {
            const float gs = DS::DesignSystem::Instance().GetGlobalScale();
            DrawBezierBox(v.AsBezier(), ImVec2(28.0f * gs, 18.0f * gs));
            break;
        }
        case ValueType::FontFamily: ImGui::TextUnformatted(v.AsFontFamily().c_str()); break;
        default:               ImGui::TextDisabled("—"); break;
    }
}

} // namespace

// One layer editor column: a left-aligned heading, a fixed-width editor, and a
// reset icon button whose slot is ALWAYS reserved (so the layout never shifts
// when an override appears). `global` selects the override layer.
// Returns nothing; writes overrides directly. `editorW` is the input width.
void LayerColumn(const char* colId, const char* heading, const std::string& id,
                 bool global, DS::ThemeType theme, float editorW,
                 const TokenValue& defaultVal) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float resetW = ImGui::GetFrameHeight();   // square reset slot

    ImGui::PushID(colId);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(heading);

    bool hasOvr = false;
    TokenValue val = LayerValueOrResolved(id, global, theme, hasOvr);
    if (ValueEditor("##e", val, id, editorW, &defaultVal))
        WriteLayer(id, global, theme, val);

    // Reserved reset slot to the right of the editor (icon-only; tooltip).
    ImGui::SameLine(0.0f, 6.0f * gs);
    if (hasOvr) {
        auto& im = VectorGraphics::IconManager::Instance();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##rst", ImVec2(resetW, resetW)))
            ClearLayer(id, global, theme);
        bool hov = ImGui::IsItemHovered();
        auto md = im.GetDefaultMetadata("reset-settings");
        ImVec4 tint = ds.GetColor(hov ? Tok::S_Color_Text_Default
                                      : Tok::S_Color_Text_Subtle);
        for (auto& z : md.colorZones) z.customColor = tint;
        const float ic = resetW * 0.8f;
        if (!md.colorZones.empty())
            im.RenderIcon(ImGui::GetWindowDrawList(), "reset-settings",
                          ImVec2(p.x + (resetW - ic) * 0.5f, p.y + (resetW - ic) * 0.5f),
                          ic, md);
        if (hov) ImGui::SetTooltip("Reset this override");
    } else {
        ImGui::Dummy(ImVec2(resetW, resetW));   // keep the slot, no shift
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

void TokenPropertyRow(const char* idPrefix, const char* label,
                      Tok tok, DS::Context& ctx, bool editGlobal) {
    auto& ds  = DS::DesignSystem::Instance();
    auto& mgr = ds.GetOverrideManager();
    const float gs = ds.GetGlobalScale();
    const std::string id = DS::TokIdStr(tok);
    const DS::ThemeType theme = ctx.GetTheme();

    const bool hasG = mgr.HasGlobalOverride(id);
    const bool hasT = mgr.HasThemeOverride(id, theme);
    const bool overridden = hasG || hasT;

    char pid[160];
    std::snprintf(pid, sizeof(pid), "%s_%s", idPrefix, id.c_str());

    // Reserve a header inline editor (a quick edit of the ACTIVE layer without
    // expanding). Width ≈ a third of a typical row.
    const float inlineW = 150.0f * gs;

    PanelConfig cfg;
    cfg.id = pid;
    cfg.label = label;
    cfg.hasOverride = overridden;   // header shows the reset icon button
    cfg.headerInlineWidth = inlineW;
    PanelResult pr = BeginPanel(cfg);

    if (pr.resetClicked) {
        if (hasG) ClearLayer(id, true, theme);
        if (hasT) ClearLayer(id, false, theme);
    }

    // ── Header inline editor (edits the active layer per editGlobal) ─────────
    // Drawn INSIDE the panel child (which exists whether open or collapsed), so
    // it is never covered by a following panel. Vertically centred in the bar.
    if (pr.inlineMax.x > pr.inlineMin.x) {
        const float h = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(pr.inlineMin.x,
            pr.inlineMin.y + (pr.inlineMax.y - pr.inlineMin.y - h) * 0.5f));
        bool layerHas = false;
        TokenValue val = LayerValueOrResolved(id, editGlobal, theme, layerHas);
        TokenValue dflt = DefaultValue(id, theme);
        ImGui::PushID("inlineEd");
        if (ValueEditor("##ie", val, id, inlineW - 6.0f * gs, &dflt))
            WriteLayer(id, editGlobal, theme, val);
        ImGui::PopID();
    }

    if (pr.open) {
        // Indent the leaf content (the body child has no horizontal padding so
        // nested headers stay full width; only the editable content is inset).
        const float pad = 8.0f * gs;
        ImGui::Indent(pad);

        // Two editor columns, each ~1/3 wide, RIGHT-aligned with a right margin.
        // Headings sit left-aligned above each editor. We compute X positions so
        // both columns start at the same Y (perfectly aligned).
        const float rightMargin = 16.0f * gs;
        const float colGap      = 16.0f * gs;
        const float avail       = ImGui::GetContentRegionAvail().x;
        const float resetW      = ImGui::GetFrameHeight() + 6.0f * gs;
        const float editorW     = (avail) * (1.0f / 3.0f);
        const float colW        = editorW + resetW;            // editor + reset slot
        const float originX     = ImGui::GetCursorPosX();
        const float col2X       = originX + avail - rightMargin - colW;
        const float col1X       = col2X - colGap - colW;
        const TokenValue dflt   = DefaultValue(id, theme);

        const char* tnames[] = { "Dark", "Light", "Muted Green", "High Contrast" };
        char themeHdr[48];
        std::snprintf(themeHdr, sizeof(themeHdr), "Current theme: %s",
                      tnames[(int)theme]);

        float colY = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(col1X, colY));
        LayerColumn("g", "Application", id, true, theme, editorW, dflt);
        ImGui::SetCursorPos(ImVec2(col2X, colY));
        LayerColumn("t", themeHdr, id, false, theme, editorW, dflt);

        ImGui::Dummy(ImVec2(0.0f, 8.0f * gs));   // margin below the editors

        // The resolution chain is a deepest-level sub-panel. It must sit at the
        // SAME left edge as the other sub-panels (flush with the body), so drop
        // the leaf-content indent around it — otherwise its rounded body and
        // header start `pad` to the right (the gap the user saw) and the
        // PanelHeaderTextIndent alignment below is thrown off.
        ImGui::Unindent(pad);

        // Token id, indented to line up with the header label of the sibling
        // "Resolution chain" sub-panel (begun at PanelDepth()+1): its text sits
        // at PanelHeaderTextIndent(depthSub) inside the panel body, whereas
        // plain text would otherwise hug the left edge.
        const int depthSub = PanelDepth() + 1;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + PanelHeaderTextIndent(depthSub));
        ImGui::TextDisabled("%s", id.c_str());
        ImGui::Dummy(ImVec2(0.0f, 4.0f * gs));   // margin below the token name

        char cpid[176];
        std::snprintf(cpid, sizeof(cpid), "%s_chain", id.c_str());
        PanelConfig chainCfg;
        chainCfg.id = cpid;
        chainCfg.label = "Resolution chain";
        PanelResult chainR = BeginPanel(chainCfg);
        if (chainR.open) {
            // Indent the chain content so it lines up UNDER the chain panel's
            // header label (chevron + depth), not flush at the body's left edge.
            const float chainIndent = PanelHeaderTextIndent(PanelDepth());
            ImGui::Indent(chainIndent);

            auto chain = ds.GetReferenceChain(id, theme);
            // Cascade view: each link sits one step deeper than the previous one,
            // prefixed with a "↳" connector, so the A → B → C → value chain reads
            // top-to-bottom. The tier is colour-coded (primitive / semantic /
            // component) and the link's value is shown to the right; overridden
            // links are dimmed and annotated with their underlying default.
            const float step = 14.0f * gs;   // per-level cascade indent
            for (size_t k = 0; k < chain.size(); ++k) {
                const auto& e = chain[k];
                ImGui::PushID((int)k);
                if (k > 0) ImGui::Indent(step * (float)k);

                if (k > 0) {
                    // U+21B3 "↳" as an explicit UTF-8 byte string (a u8"" literal
                    // is const char8_t* in C++20 and won't bind to const char*).
                    ImGui::TextDisabled("\xE2\x86\xB3");
                    ImGui::SameLine(0.0f, 4.0f * gs);
                }
                // Neutral label colour (primary text); only an overridden link
                // is dimmed. The tier is already conveyed by the id prefix and
                // the cascade indentation, so no per-tier accent colour.
                ImVec4 nameCol = e.overridden ? Col(Tok::S_Color_Text_Subtle)
                                              : Col(Tok::S_Color_Text_Default);
                ImGui::TextColored(nameCol, "%s", e.tokenId.c_str());

                ImGui::SameLine(0.0f, 8.0f * gs);
                ValuePreview("##cur", e.value);
                if (e.overridden) {
                    ImGui::SameLine(0.0f, 8.0f * gs);
                    ImGui::TextDisabled("(default:");
                    ImGui::SameLine(0.0f, 4.0f * gs);
                    ValuePreview("##def", DefaultValue(e.tokenId, theme));
                    ImGui::SameLine(0.0f, 2.0f * gs);
                    ImGui::TextDisabled(")");
                }

                if (k > 0) ImGui::Unindent(step * (float)k);
                ImGui::PopID();
            }
            ImGui::Unindent(chainIndent);
        }
        EndPanel();
        // NB: the leaf-content Indent(pad) was already undone by the
        // Unindent(pad) before the chain panel, so nothing to unwind here.
    }

    EndPanel();
}

} // namespace UI

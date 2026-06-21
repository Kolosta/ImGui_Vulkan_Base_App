#include <UI/Tokens/TokenValueWidgets.h>
#include <UI/Text/FontManager.h>
#include <UI/Widgets/Checkbox.h>
#include <DesignSystem/DesignSystem.h>
#include <DesignSystem/Tokens/TokenIds.h>
#include <imgui.h>
#include <algorithm>
#include <string>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
using DS::TokenValue;
using DS::ValueType;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }
} // namespace

// Draw a cubic-bezier easing curve (control points cp = {x1,y1,x2,y2}) inside a
// box at the cursor, of the given size. A flat, token-coloured preview: the unit
// square is mapped into the box (y flipped so the curve reads bottom-left →
// top-right like a standard easing plot). Endpoints are fixed at (0,0)/(1,1).
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

void TokenValuePreview(const char* pvId, const TokenValue& v) {
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
        default:               ImGui::TextDisabled("\xE2\x80\x94"); break;  // em dash
    }
}

bool TokenValueEditor(const char* id, TokenValue& v, const std::string& tokenId,
                      float width, const TokenValue* defaultVal) {
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    DS::ValueConstraint cn = ds.GetEffectiveConstraint(tokenId);
    const float h = ImGui::GetFrameHeight();
    bool changed = false;
    switch (v.GetType()) {
        case ValueType::Color: {
            ImVec4 c = v.AsColor();
            // A fixed-size swatch opens a colour picker popup on click. The popup
            // shows Current vs Original (ref_col) natively, plus a Default swatch.
            // Each control has a UNIQUE id (no ImGui id clash).
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
                // Right-align the box in the value column (box side = box-size token).
                const float gs = DS::DesignSystem::Instance().GetGlobalScale();
                const float boxSz = Flt(Tok::C_Checkbox_BoxSize) * gs;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     std::max(0.0f, width - boxSz));
                if (UI::CheckboxBox(id, &b)) { v.SetInt(b ? 1 : 0); changed = true; }
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
            // A combo of the font families discovered in the project (same source
            // as the classic Design System editor). Selecting one writes the
            // family name; ApplyFontTokens() re-reads it every frame, so the
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

} // namespace UI

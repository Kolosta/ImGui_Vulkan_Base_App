#include <UI/Settings/TokenPropertyRow.h>
#include <UI/Tokens/TokenValueWidgets.h>
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

// A typed value editor of a FIXED width. Delegates to the shared widget helper
// (UI::TokenValueEditor) so the Settings rows and the Token Graph cards use the
// exact same editors. Kept as a thin local alias to avoid churn at call sites.
bool ValueEditor(const char* id, TokenValue& v, const std::string& tokenId,
                 float width, const TokenValue* defaultVal) {
    return UI::TokenValueEditor(id, v, tokenId, width, defaultVal);
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

// Small read-only preview of a value. Thin alias over the shared widget helper.
void ValuePreview(const char* pvId, const TokenValue& v) {
    UI::TokenValuePreview(pvId, v);
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

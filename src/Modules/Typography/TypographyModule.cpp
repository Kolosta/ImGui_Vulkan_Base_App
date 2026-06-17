#include "TypographyModule.h"

#include <imgui.h>
#include <DesignSystem/DesignSystem.h>

namespace App::Modules::Typography {

namespace {
// Register a template editor: a titled placeholder body. Real content comes
// later — this module is intentionally a scaffold for now.
void RegisterStub(EditorRegistry& reg, const char* id, const char* name,
                  const char* icon, const char* blurb) {
    EditorDescriptor d;
    d.id = id; d.name = name; d.icon = icon;
    d.column = 0;                       // all in one picker group for the module
    d.themeScope = "editors";          // reuse the generic editor scope for now
    d.draw = [name, blurb](ImVec2, EditorState&) {
        auto& ds = DesignSystem::DesignSystem::Instance();
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
        ImGui::TextUnformatted(name);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DesignSystem::Tok::S_Color_Text_Subtle));
        ImGui::TextWrapped("%s", blurb);
        ImGui::TextDisabled("(Typography module — not implemented yet)");
        ImGui::PopStyleColor();
    };
    reg.Register(std::move(d));
}
}  // namespace

ModuleInfo TypographyModule::Info() const {
    return { "typography", "Typography",
             "Font design workspace (template)", "format-align-left", "0.1.0" };
}

void TypographyModule::OnRegister(ModuleContext& ctx) {
    auto& reg = ctx.editors;
    RegisterStub(reg, "typo.fontatlas",   "Font Atlas",
                 "All available glyphs of the current font.",          "checklist");
    RegisterStub(reg, "typo.fonteditor",  "Font Editor",
                 "Edit a glyph on an infinite canvas (no pages). TODO: infinite-canvas viewport.",
                 "image");
    RegisterStub(reg, "typo.fontinfo",    "Font Info",
                 "Metadata of the current font (family, metrics, tables).", "settings");
    RegisterStub(reg, "typo.fontpreview", "Font Preview",
                 "Type a sentence to preview the font at several sizes.", "format-align-left");
    RegisterStub(reg, "typo.variations",  "Variation Panel",
                 "Variable-font axes (weight, width, slant…).",         "find-replace");
    RegisterStub(reg, "typo.fontoutliner","Font Outliner",
                 "Layers of the current glyph (instead of objects).",   "checklist");
}

LayoutSpec TypographyModule::BuildLayout() const {
    using L = LayoutSpec;
    // [ Atlas | [ FontEditor(big) | rightColumn ] ] with a right stack of
    // Info / Preview / Variations / Outliner.
    L rightStack =
        L::Split(false, 0.25f, L::Leaf("typo.fontinfo"),
        L::Split(false, 0.34f, L::Leaf("typo.fontpreview"),
        L::Split(false, 0.5f,  L::Leaf("typo.variations"),
                               L::Leaf("typo.fontoutliner"))));
    L centreRight = L::Split(true, 0.70f, L::Leaf("typo.fonteditor"), std::move(rightStack));
    return L::Split(true, 0.18f, L::Leaf("typo.fontatlas"), std::move(centreRight));
}

void TypographyModule::ConfigureCapabilities(Capabilities& caps) const {
    caps.corePrimitivesAddMenu = false;   // no rectangles/curves in a font module
    caps.pages                 = false;    // glyphs, not pages
    caps.editMode              = true;     // (point editing makes sense later)
}

std::vector<std::string> TypographyModule::AllowedEditors() const {
    // Only the module's editors are selectable — no core Viewport/Timeline/etc.
    return { "typo.fontatlas", "typo.fonteditor", "typo.fontinfo",
             "typo.fontpreview", "typo.variations", "typo.fontoutliner" };
}

}  // namespace App::Modules::Typography

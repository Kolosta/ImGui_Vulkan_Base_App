#include "TypographyModule.h"

#include <imgui.h>
#include <DesignSystem/DesignSystem.h>
#include <Ink/Document/Document.h>

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

// ── Ink document hooks (Lot 11) ──────────────────────────────────────────────
// A square 1000-unit em canvas (the classic font-unit grid) seeded with the
// glyph working guides. Built through the document's typed ops — the same path
// core tools use, so the seed is undo-consistent and engine-visible.

std::pair<float, float> TypographyModule::DefaultPageSize() const {
    return { 1000.0f, 1000.0f };   // one em at 1000 units/em
}

void TypographyModule::OnDocumentCreated(Ink::Document& doc) {
    if (doc.Pages().empty()) return;
    const Ink::Page& pg = doc.Pages().front();
    const double x = pg.pos.x, y = pg.pos.y;
    const double w = pg.size.x, h = pg.size.y;

    // Guides are viewport-px hairlines (constant on-screen width at any zoom),
    // in a muted grey — glyph outlines will sit on top of them.
    const Ink::Color guideCol{ 0.35f, 0.38f, 0.45f, 1.0f };
    auto hairline = [&](double width) {
        Ink::Style s = Ink::Style::Stroked(guideCol, width);
        s.strokes[0].widthSpace = Ink::WidthSpace::Viewport;
        return s;
    };

    // Em square (the full page frame) + the classic horizontal metrics:
    // baseline at 20 % up, x-height at 50 %, cap height at 70 %.
    const Ink::NodeId em = doc.AddPath(
        pg.id, Ink::PathData::Rect(x, y, w, h), hairline(1.5), "Em square");
    auto metricLine = [&](double frac, const char* name) {
        const double ly = y + h * (1.0 - frac);
        Ink::PathData p =
            Ink::PathData::Polygon({ { x, ly }, { x + w, ly } }, false);
        return doc.AddPath(pg.id, std::move(p), hairline(1.0), name);
    };
    const Ink::NodeId baseline  = metricLine(0.20, "Baseline");
    const Ink::NodeId xheight   = metricLine(0.50, "x-height");
    const Ink::NodeId capheight = metricLine(0.70, "Cap height");

    // Organised into a locked-purpose "Guides" collection so the outliner
    // groups them; the glyph itself is drawn as ordinary paths beside them.
    const Ink::NodeId guides = doc.AddCollection("Guides");
    doc.SetCollectionColor(guides, { 0.35f, 0.38f, 0.45f, 1.0f });
    for (Ink::NodeId n : { em, baseline, xheight, capheight })
        doc.AddToCollection(guides, n);
}

}  // namespace App::Modules::Typography

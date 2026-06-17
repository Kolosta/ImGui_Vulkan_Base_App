#pragma once

#include "ModuleAPI.h"

namespace App::Modules::Typography {

// ─────────────────────────────────────────────────────────────────────────────
//  Typography module — a template specialisation for font work.
//
//  First pass: it contributes its own LAYOUT and a set of TEMPLATE editors (Font
//  Atlas, Font Editor, Font Info, Font Preview, Variation Panel, Font Outliner)
//  whose bodies are placeholders for now — no new functionality yet. It does not
//  offer the core Shift+A primitives (a font workspace adds glyph data, not
//  rectangles). Demonstrates a module that only adds editors + a layout.
// ─────────────────────────────────────────────────────────────────────────────
class TypographyModule final : public IModule {
public:
    ModuleInfo Info() const override;
    void       OnRegister(ModuleContext& ctx) override;
    LayoutSpec BuildLayout() const override;
    void       ConfigureCapabilities(Capabilities& caps) const override;
    std::vector<std::string> AllowedEditors() const override;
};

}  // namespace App::Modules::Typography

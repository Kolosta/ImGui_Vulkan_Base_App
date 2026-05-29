#pragma once

#include <DesignSystem/Core/Context.h>
#include <UI/Tokens/TokenInspector.h>
#include <string>
#include <vector>

namespace DesignSystem {

class OverrideManager;

/**
 * User-facing theme editor — organised the way Blender's Theme editor is:
 * by *zone / usage* rather than by raw token id. The user picks a top-level
 * area (User Interface, Text, Status Bar, …) and drills into sub-areas
 * (Buttons, Frames & Inputs, Tabs, …) through collapsible sections. Each row
 * shows a friendly label, an inline editor, the original-vs-actual value and
 * a reset, and writes a global and/or per-theme override.
 *
 * The zone → token mapping is a static, declarative table (see the .cpp);
 * adding a control to a zone is a one-line edit and is compile-checked
 * against the strongly-typed `Tok` enum.
 */
class UserThemeEditor {
public:
    UserThemeEditor();
    void Render(Context& ctx, OverrideManager& mgr);

private:
    TokenInspector inspector_;
    char           searchBuffer_[256];
};

} // namespace DesignSystem

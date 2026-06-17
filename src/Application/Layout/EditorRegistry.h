#pragma once

#include <functional>
#include <string>
#include <vector>
#include <imgui.h>

namespace App {

// EditorState / EditorBar are defined in ZoneLayout.h. Only referenced here by
// reference inside std::function signatures, so forward declarations suffice and
// the registry stays decoupled from the ZoneLayout class (and usable by modules).
struct EditorState;
struct EditorBar;

// ─────────────────────────────────────────────────────────────────────────────
//  EditorRegistry — the single source of truth for what editors exist.
//
//  Editors are identified by a STRING id (e.g. "core.viewport", "iof.mapsettings")
//  instead of a fixed enum, so modules (and, later, external plugins) can add
//  their own editors without touching core code. The core registers its editors
//  at startup; a module registers its own in IModule::OnRegister.
//
//  Every zone in the layout stores an editor id; the layout draws a zone by
//  looking up its descriptor here and calling `draw` / `topBar`. The picker, the
//  per-zone theme scope and the picker grouping all read these descriptor fields.
// ─────────────────────────────────────────────────────────────────────────────
struct EditorDescriptor {
    std::string id;             // unique handle, e.g. "core.viewport"
    std::string name;           // display name shown in the picker / tab
    std::string icon;           // icon id (resources/icons), may be empty
    int         column = 0;     // picker column group (General/Animation/Data…)
    std::string themeScope;     // design-system scope, e.g. "editors/viewport"
    std::string switchAction;   // optional shortcut action id (e.g. "editor.viewport")
    bool        wrapInScroll = true;  // wrap content in the overlay scroll area
    bool        contentInset = true;  // apply the editor content inset padding

    // Draw the editor body into the current child window (size = content rect).
    std::function<void(ImVec2 /*size*/, EditorState&)> draw;
    // Optional: fill the editor's three top-bar groups (left/middle/right).
    std::function<void(EditorState&, EditorBar&)>      topBar;
};

class EditorRegistry {
public:
    static EditorRegistry& Instance();

    // Register (or replace, by id) an editor descriptor. Core registers first;
    // a module may register additional editors or override a core one by id.
    void Register(EditorDescriptor desc);
    // Remove every editor whose id starts with `prefix` (e.g. a module's "typo.")
    // — used when a module is deactivated so its editors leave the picker.
    void UnregisterByPrefix(const std::string& prefix);

    // Descriptor for `id`, or nullptr if unknown (e.g. a plugin not loaded).
    const EditorDescriptor* Get(const std::string& id) const;
    bool Has(const std::string& id) const { return Get(id) != nullptr; }
    const std::vector<EditorDescriptor>& All() const { return editors_; }

private:
    EditorRegistry() = default;
    std::vector<EditorDescriptor> editors_;
};

// Core editor ids — symbolic constants so core code never hard-codes the strings.
namespace CoreEditor {
inline constexpr const char* Viewport   = "core.viewport";
inline constexpr const char* Outliner   = "core.outliner";
inline constexpr const char* Properties = "core.properties";
inline constexpr const char* Timeline   = "core.timeline";
inline constexpr const char* DevPanels  = "core.devpanels";
inline constexpr const char* Info       = "core.info";
// Every core editor id — the editor set offered in Classic mode (no module).
inline std::vector<std::string> Ids() {
    return { Viewport, Outliner, Properties, Timeline, DevPanels, Info };
}
}  // namespace CoreEditor

}  // namespace App

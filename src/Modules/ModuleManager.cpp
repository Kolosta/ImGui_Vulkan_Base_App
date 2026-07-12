#include "Application.h"
#include "ModuleRegistry.h"
#include <Shortcuts/ShortcutManager.h>

namespace App {

// Build the module catalogue and let each module register its editors. Called
// once at init AFTER RegisterCoreEditors() (modules may reference core editor
// ids and add their own on top).
void Application::RegisterModules() {
    Modules::ModuleContext ctx{ EditorRegistry::Instance(),
                                Shortcuts::ShortcutManager::Instance() };
    Modules::ModuleRegistry::Instance().RegisterInternal(ctx);
}

// Splash → open a module. Same unsaved-changes guard as the New File presets:
// if the project is dirty, arm the dialog; otherwise create the fresh project +
// activate the module immediately.
void Application::RequestOpenModule(const std::string& moduleId) {
    pendingModuleId_ = moduleId;
    pendingOpenPath_.clear();               // a module open, not a held file open
    if (project_.dirty) {
        unsavedDialogOpen_ = true;
    } else {
        CommitPendingNew();
    }
}

// Resolve the pending intent: an .acu open (pendingOpenPath_ set — held while
// the unsaved dialog ran), a module open (pendingModuleId_), or a plain new-file
// preset. Shared by the direct path, the unsaved dialog, and the post-save
// continuation (newFileAfterSave_).
void Application::CommitPendingNew() {
    if (!pendingOpenPath_.empty()) {
        const std::string path = pendingOpenPath_;
        pendingOpenPath_.clear();
        LoadProjectFromFile(path);
    } else if (!pendingModuleId_.empty()) {
        std::string id = pendingModuleId_;
        pendingModuleId_.clear();
        DoOpenModule(id);
    } else {
        DoNewFile(pendingNewPreset_, /*applyLayout=*/true);
    }
}

void Application::DoOpenModule(const std::string& moduleId) {
    Modules::IModule* mod = Modules::ModuleRegistry::Instance().Get(moduleId);
    // Fresh blank project sized to the module's default page; the Classic demo
    // seed stays OUT of module projects — the module builds its own starter
    // content through the typed document ops (OnDocumentCreated, Lot 11).
    double pageW = 1920.0, pageH = 1080.0;
    if (mod) {
        const auto [w, h] = mod->DefaultPageSize();
        if (w > 0.0f && h > 0.0f) { pageW = w; pageH = h; }
    }
    ResetDocument(/*seedDemo=*/mod == nullptr, pageW, pageH);
    if (mod && project_.document) {
        mod->BindHost(this);            // host services live before OnActivate
        mod->OnDocumentCreated(*project_.document);
    }
    ActivateModule(mod);
}

// Make `mod` the active module (nullptr = Classic). Applies its capabilities,
// editor filter and lifecycle hooks. `rebuildLayout` rebuilds the zone tree from
// the module/preset — true when OPENING a module (fresh project), but false when
// LOADING a file (whose own saved layout is authoritative and must be kept).
void Application::ActivateModule(Modules::IModule* mod, bool rebuildLayout) {
    if (activeModule_ == mod) {
        // Re-activating the same module still rebuilds its layout (the open flow
        // already reset the document).
    } else if (activeModule_) {
        activeModule_->OnDeactivate();
    }
    activeModule_       = mod;
    activeCapabilities_ = Modules::Capabilities{};   // back to full Classic defaults
    project_.moduleId   = mod ? mod->Info().id : std::string();  // persisted in META
    if (mod) {
        mod->BindHost(this);                          // app services for its hooks
        mod->ConfigureCapabilities(activeCapabilities_);
        // Restrict the editor picker to the module's editors (its own + reused
        // core ones); fall back to core-only if the module declares none.
        std::vector<std::string> allowed = mod->AllowedEditors();
        zoneLayout_.SetEditorFilter(allowed.empty() ? CoreEditor::Ids() : allowed);
        if (rebuildLayout) zoneLayout_.BuildFromSpec(mod->BuildLayout());
        // Apply the module's default document unit to every zone (IOF → mm).
        zoneLayout_.ApplyDocUnitToAll(activeCapabilities_.documentUnit);
        mod->OnActivate();
    } else {
        zoneLayout_.SetEditorFilter(CoreEditor::Ids());     // Classic: core only
        if (rebuildLayout) zoneLayout_.ApplyPreset(LayoutPreset::General);
        zoneLayout_.ApplyDocUnitToAll(0);                   // Classic: px
    }
}

// ── Modules::ModuleHost — app services exposed to the active module ───────────
// The document services (object creation, baked-shape placement, cached glyph
// rendering) return re-designed on the Ink document — docs/Ink/ROADMAP.md Lot 11.
void Application::MarkDirty() { project_.dirty = true; }

}  // namespace App

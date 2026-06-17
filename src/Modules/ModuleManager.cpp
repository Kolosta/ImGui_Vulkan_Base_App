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
    if (project_.dirty) {
        unsavedDialogOpen_ = true;
    } else {
        CommitPendingNew();
    }
}

// Resolve the pending new-file intent: a module open (pendingModuleId_ set) or a
// plain preset. Shared by the direct path, the unsaved dialog, and the post-save
// continuation (newFileAfterSave_).
void Application::CommitPendingNew() {
    if (!pendingModuleId_.empty()) {
        std::string id = pendingModuleId_;
        pendingModuleId_.clear();
        DoOpenModule(id);
    } else {
        DoNewFile(pendingNewPreset_, /*applyLayout=*/true);
    }
}

void Application::DoOpenModule(const std::string& moduleId) {
    Modules::IModule* mod = Modules::ModuleRegistry::Instance().Get(moduleId);
    // Fresh blank project, no preset layout yet — the module supplies its own
    // arrangement via ActivateModule. Use the module's default page size if it
    // declares one (e.g. IOF → A4 landscape), else the core default.
    project_.Reset();
    auto [pw, ph] = mod ? mod->DefaultPageSize() : std::pair<float, float>{0.0f, 0.0f};
    if (pw > 0.0f && ph > 0.0f) project_.AddArtboard("Page 1", ImVec2(0, 0), ImVec2(pw, ph));
    else                        project_.AddArtboard("Page 1", ImVec2(0, 0), ImVec2(1920, 1080));
    project_.dirty = false;
    ResetUndoHistory();
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
Renderer::Document& Application::Document() { return project_.document; }

void Application::CreateObject(const std::string& presetKind, const std::string& name) {
    Action_AddShape(presetKind.empty() ? "rectangle" : presetKind);
    if (Renderer::Shape* s = project_.document.ActiveShape(); s && !name.empty())
        s->name = name;
    project_.dirty = true;
}

void Application::MarkDirty() { project_.dirty = true; }

ImTextureID Application::RenderGlyphTexture(uint64_t key, uint64_t contentHash,
                                           const std::vector<Renderer::Shape>& shapes,
                                           int widthPx, int heightPx, float padFrac,
                                           bool transparent, bool exactFit,
                                           const Renderer::Vec2* frameMin,
                                           const Renderer::Vec2* frameMax) {
    // White "map paper" card behind a thumbnail; transparent for the placement
    // ghost (so it overlays the canvas). SSAA-smoothed via the Vulkan pipeline.
    ImVec4 clear = transparent ? ImVec4(0, 0, 0, 0) : ImVec4(1, 1, 1, 1);
    return canvasRenderer_.RenderGlyphCached(key, contentHash, shapes,
                                             widthPx, heightPx, padFrac, clear, exactFit,
                                             frameMin, frameMax);
}

}  // namespace App

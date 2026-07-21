#include "Application.h"
#include "ModuleRegistry.h"
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/ToolManager.h>
#include <Ink/Render/Renderer.h>
#include <Ink/View/View.h>
#include <algorithm>

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
        // A FRESH module project takes the module's DOCUMENT unit system and
        // colour mode (persisted with the file); a loaded file keeps its own.
        if (rebuildLayout) {
            if (activeCapabilities_.documentUnit >= 0)
                project_.docUnitSystem = (UI::Units::UnitSystem)std::clamp(
                    activeCapabilities_.documentUnit, 0,
                    UI::Units::kUnitSystemCount - 1);
            if (activeCapabilities_.colorMode >= 0)
                project_.colorMode = activeCapabilities_.colorMode
                                         ? Project::ColorModeKind::Cmyk
                                         : Project::ColorModeKind::Rgb;
        }
        zoneLayout_.ApplyDocUnitToAll(0);   // zones follow the document unit
        mod->OnActivate();
    } else {
        zoneLayout_.SetEditorFilter(CoreEditor::Ids());     // Classic: core only
        if (rebuildLayout) zoneLayout_.ApplyPreset(LayoutPreset::General);
        zoneLayout_.ApplyDocUnitToAll(0);                   // zones follow doc
    }
}

// ── Modules::ModuleHost — app services exposed to the active module ───────────
// The document services (object creation, baked-shape placement, cached glyph
// rendering) return re-designed on the Ink document — docs/Ink/ROADMAP.md Lot 11.
void Application::MarkDirty() { project_.dirty = true; }

// Real-pipeline vignette of a node subtree: a cached off-screen Ink view with
// the preview filter on the subtree, camera FITTED on the node's bounds with a
// `padFrac` margin — identical rendering (strokes / instanced fills / MSAA) to
// the canvas. Works for preview-only library nodes (their bounds are kept).
std::uint64_t Application::NodePreviewTexture(std::uint64_t node, int px,
                                              float padFrac) {
    if (!ink_ || !project_.document || px <= 0) return 0;
    // Owners = the layer subtree; the frame is the UNION of their bounds
    // (NodeBounds is per-owner — a multi-part GROUP has no entry of its own).
    std::vector<std::uint64_t> owners;
    Ink::DRect bb;
    {
        std::vector<Ink::NodeId> stack{ node };
        while (!stack.empty()) {
            const Ink::NodeId c = stack.back();
            stack.pop_back();
            owners.push_back(c);
            Ink::DRect nb;
            if (ink_->NodeBounds(c, nb) && nb.valid) {
                bb.Grow(nb.min);
                bb.Grow(nb.max);
            }
            if (const Ink::Node* n = project_.document->Find(c))
                for (Ink::NodeId k : n->children) stack.push_back(k);
        }
    }
    if (!bb.valid) return 0;
    // Key space: low bits 0b11 mark the module vignettes (Outliner thumbnails
    // are odd; paint previews use 0b10; viewport keys are aligned pointers).
    const void* key = (const void*)(std::uintptr_t)(
        ((node * 131u + (std::uint64_t)px) << 2) | 3u);
    Ink::View* view = ink_->AcquireView(key);
    view->SetViewport((std::uint32_t)px, (std::uint32_t)px);
    const double w  = std::max(1e-6, bb.max.x - bb.min.x);
    const double h  = std::max(1e-6, bb.max.y - bb.min.y);
    const double cx = (bb.min.x + bb.max.x) * 0.5;
    const double cy = (bb.min.y + bb.max.y) * 0.5;
    const double pad = std::clamp((double)padFrac, 0.0, 0.45);
    const double zoom = (double)px * (1.0 - 2.0 * pad) / std::max(w, h);
    view->SetCamera(cx - (double)px * 0.5 / zoom,
                    cy - (double)px * 0.5 / zoom, zoom);
    view->SetBackground(Ink::SrgbToLinearPremultiplied(1, 1, 1, 1));
    view->SetPreviewFilter(owners);
    return view->Texture();
}

// A module-driven zoom/pan preview canvas: same isolation render as the
// vignettes but with an explicit camera and a non-square target (the IOF
// Symbol Viewer example canvas).
std::uint64_t Application::CanvasPreviewTexture(std::uint64_t node,
                                                std::uint32_t viewKey, int w,
                                                int h, double panX, double panY,
                                                double zoom) {
    if (!ink_ || !project_.document || w <= 0 || h <= 0 || zoom <= 0.0) return 0;
    std::vector<std::uint64_t> owners;
    std::vector<Ink::NodeId> stack{ node };
    while (!stack.empty()) {
        const Ink::NodeId c = stack.back();
        stack.pop_back();
        owners.push_back(c);
        if (const Ink::Node* n = project_.document->Find(c))
            for (Ink::NodeId k : n->children) stack.push_back(k);
    }
    // Key space: low bits 0b11 (module vignettes), a distinct salt per canvas.
    const void* key = (const void*)(std::uintptr_t)(
        ((node * 977u + (std::uint64_t)viewKey * 131u + 7u) << 2) | 3u);
    Ink::View* view = ink_->AcquireView(key);
    view->SetViewport((std::uint32_t)w, (std::uint32_t)h);
    view->SetCamera(panX, panY, zoom);
    view->SetBackground(Ink::SrgbToLinearPremultiplied(1, 1, 1, 1));
    view->SetPreviewFilter(owners);
    return view->Texture();
}

bool Application::NodeDocBounds(std::uint64_t node, double out[4]) {
    if (!ink_ || !project_.document) return false;
    Ink::DRect bb;
    std::vector<Ink::NodeId> stack{ node };
    while (!stack.empty()) {
        const Ink::NodeId c = stack.back();
        stack.pop_back();
        Ink::DRect nb;
        if (ink_->NodeBounds(c, nb) && nb.valid) {
            bb.Grow(nb.min);
            bb.Grow(nb.max);
        }
        if (const Ink::Node* n = project_.document->Find(c))
            for (Ink::NodeId k : n->children) stack.push_back(k);
    }
    if (!bb.valid) return false;
    out[0] = bb.min.x; out[1] = bb.min.y;
    out[2] = bb.max.x; out[3] = bb.max.y;
    return true;
}

// ── Module viewport tools: place-symbol arming + symbol-draw pen override ────
void Application::ArmPlacement(const Modules::PlacementRequest& req) {
    EndSymbolDraw();                      // the two modes are exclusive
    modulePlace_.armed = true;
    modulePlace_.req   = req;
}

void Application::CancelPlacement() {
    modulePlace_.armed = false;
    modulePlace_.req = {};
}

void Application::BeginSymbolDraw(const Modules::SymbolDrawRequest& req) {
    CancelPlacement();                    // the two modes are exclusive
    moduleDraw_.active   = true;
    moduleDraw_.style    = req.style ? *req.style : Ink::Style{};
    moduleDraw_.iconNode = req.iconNode;
    moduleDraw_.onCommit = req.onCommit;
    moduleDraw_.penKind  = req.penKind.empty() ? std::string("curve")
                                               : req.penKind;
    BeginPenDraw(moduleDraw_.penKind.c_str());
}

void Application::EndSymbolDraw() {
    // Cancel any in-progress module pen draw (switching away from the tool
    // mid-path must not leave the pen live with a stale style).
    if (moduleDraw_.active && penActive_) CommitPenDraw(/*keep=*/false);
    moduleDraw_ = {};
}

std::string Application::ActiveTool() const {
    return Shortcuts::Tools::ToolManager::Instance().GetActiveTool();
}

void Application::RegisterTool(const std::string& id, const std::string& name,
                               const std::string& icon) {
    Shortcuts::Tools::ToolDef def;
    def.id = id; def.name = name; def.iconId = icon;
    Shortcuts::Tools::ToolManager::Instance().RegisterTool(def);
}

}  // namespace App

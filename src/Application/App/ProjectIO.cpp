#include "Application.h"

#include "AcuFile.h"
#include "PngWrite.h"
#include "ModuleRegistry.h"

#include <Shortcuts/ToolManager.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────────────
//  Project persistence — the .acu v2 lifecycle (docs/Ink/ROADMAP.md Lot 10,
//  docs/acu-format.md).
//
//  OPEN: the async SDL dialog (or a splash recent-file click) stashes the path
//  into pendingFile_; ProcessPendingFileOp — on the main thread, before the
//  ImGui frame — loads it (or routes through the "Unsaved changes" dialog when
//  the current project is dirty).
//
//  SAVE: two-phase, inside ONE frame. ProcessPendingFileOp only ARMS the path
//  (pendingSavePath_); PrepareSavePass — right before ink_->EndFrame() — sets
//  up an off-screen view fit to page 1, which EndFrame renders through the
//  normal pipeline; FinishSavePass — right after — reads the canvas back
//  (Renderer::ReadViewPixels), PNG-encodes it and writes the file. So the
//  THMB thumbnail is a REAL Ink render (strokes, patterns, blends, MSAA), and
//  a save never blocks longer than one extra readback.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {

// Display name from a file path: the stem ("D:/a/b/Map.acu" → "Map").
std::string PathDisplayName(const std::string& path) {
    std::error_code ec;
    const std::string stem = std::filesystem::path(path).stem().string();
    (void)ec;
    return stem.empty() ? path : stem;
}

// The save dialog may return a bare name — normalise to a .acu extension.
std::string EnsureAcuExtension(std::string path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (ext != ".acu") path += ".acu";
    return path;
}

const SDL_DialogFileFilter kAcuFilters[] = { { "Carto project", "acu" } };

} // namespace

// ── EDST blob: the editing-session state saved with the project ──────────────
// v1: [version u32][toolByMode ×3 str][shape variant str][curve variant str].
// Self-versioned and skipped gracefully — an unknown/short blob leaves the
// session defaults in place.

std::vector<std::uint8_t> Application::BuildEditorStateBlob() const {
    std::vector<std::uint8_t> b;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back((std::uint8_t)(v >> (i * 8)));
    };
    auto str = [&](const std::string& s) {
        u32((std::uint32_t)s.size());
        b.insert(b.end(), s.begin(), s.end());
    };
    u32(1);
    for (int i = 0; i < 3; ++i) str(edit_.toolByMode[i]);
    str(toolShapeKind_);
    str(toolCurveKind_);
    return b;
}

void Application::ApplyEditorStateBlob(const std::vector<std::uint8_t>& blob) {
    std::size_t pos = 0;
    bool ok = true;
    auto u32 = [&]() -> std::uint32_t {
        if (pos + 4 > blob.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (std::uint32_t)blob[pos + i] << (i * 8);
        pos += 4;
        return v;
    };
    auto str = [&]() -> std::string {
        const std::uint32_t n = u32();
        if (!ok || pos + n > blob.size()) { ok = false; return {}; }
        std::string s((const char*)blob.data() + pos, n);
        pos += n;
        return s;
    };
    const std::uint32_t ver = u32();
    if (!ok || ver == 0) return;
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    std::string tools[3];
    for (int i = 0; i < 3; ++i) tools[i] = str();
    const std::string shape = str();
    const std::string curve = str();
    if (!ok) return;
    // Validate every remembered tool against its mode's palette (a file from a
    // newer build may name tools this build doesn't have).
    for (int i = 0; i < 3; ++i) {
        bool allowed = false;
        for (const std::string& id : ToolsForMode((EditorMode)i))
            allowed = allowed || tools[i] == id;
        if (allowed && tm.GetTool(tools[i])) edit_.toolByMode[i] = tools[i];
    }
    auto validVariant = [](const std::string& k,
                           std::initializer_list<const char*> set) {
        for (const char* s : set)
            if (k == s) return true;
        return false;
    };
    if (validVariant(shape, { "rect", "ellipse", "triangle", "free" }))
        toolShapeKind_ = shape;
    if (validVariant(curve, { "curve", "beziercircle", "nurbs", "nurbscircle",
                              "poly" }))
        toolCurveKind_ = curve;
    // The document opens in Object mode — arm its remembered tool.
    tm.SetActiveTool(edit_.toolByMode[(int)EditorMode::Object]);
}

// ── Async dialog callbacks (may fire on another thread: stash only) ──────────

void Application::DialogOpenChosen(void* user, const char* const* files, int) {
    auto* app = static_cast<Application*>(user);
    std::lock_guard<std::mutex> lk(app->pendingFile_.mtx);
    if (files && files[0] && files[0][0]) {
        app->pendingFile_.kind = 1;
        app->pendingFile_.path = files[0];
    }
    // Cancel / error: nothing stashed — no pending intent to resolve on open.
}

void Application::DialogSaveChosen(void* user, const char* const* files, int) {
    auto* app = static_cast<Application*>(user);
    std::lock_guard<std::mutex> lk(app->pendingFile_.mtx);
    if (files && files[0] && files[0][0]) {
        app->pendingFile_.kind = 2;
        app->pendingFile_.path = files[0];
    } else {
        // A CANCELLED save must also drop any chained intent (the unsaved
        // dialog's "Save then new/open") — kind 3 resolves that on the main
        // thread.
        app->pendingFile_.kind = 3;
        app->pendingFile_.path.clear();
    }
}

// ── Actions ───────────────────────────────────────────────────────────────────

void Application::Action_OpenFile() {
    SDL_ShowOpenFileDialog(&Application::DialogOpenChosen, this, window_,
                           kAcuFilters, 1, nullptr, /*allow_many=*/false);
}

void Application::Action_SaveFile() {
    if (project_.path.empty()) {
        Action_SaveFileAs();
        return;
    }
    std::lock_guard<std::mutex> lk(pendingFile_.mtx);
    pendingFile_.kind = 2;
    pendingFile_.path = project_.path;
}

void Application::Action_SaveFileAs() {
    SDL_ShowSaveFileDialog(&Application::DialogSaveChosen, this, window_,
                           kAcuFilters, 1, nullptr);
}

// ── Pending-op resolution (main thread, before the ImGui frame) ──────────────

void Application::ProcessPendingFileOp() {
    int kind = 0;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(pendingFile_.mtx);
        kind = pendingFile_.kind;
        path = pendingFile_.path;
        pendingFile_.kind = 0;
        pendingFile_.path.clear();
    }
    if (kind == 0) return;

    if (kind == 1) {                       // open
        if (project_.dirty) {
            // Route through the "Unsaved changes" dialog; CommitPendingNew
            // performs the open once the user resolves it.
            pendingOpenPath_ = path;
            pendingModuleId_.clear();
            unsavedDialogOpen_ = true;
        } else {
            LoadProjectFromFile(path);
        }
    } else if (kind == 2) {                // save: arm for this frame's end
        pendingSavePath_ = EnsureAcuExtension(path);
    } else if (kind == 3) {                // save dialog cancelled
        newFileAfterSave_ = false;
        pendingModuleId_.clear();
        pendingOpenPath_.clear();
    }
}

// ── Open ──────────────────────────────────────────────────────────────────────

void Application::LoadProjectFromFile(const std::string& path) {
    AcuData data;
    std::string err;
    if (!AcuFile::Load(path, data, &err)) {
        LogInfoAction("Open File", err);
        return;
    }
    auto doc = std::make_unique<Ink::Document>();
    if (!doc->Restore(std::move(data.pages), std::move(data.nodes),
                      std::move(data.collections), data.nextId)) {
        LogInfoAction("Open File", "Corrupt document structure");
        return;
    }
    // The colour table the paints above reference (v21; empty for older files).
    doc->RestoreSwatches(std::move(data.swatches));
    doc->SetPrintTech((Ink::PrintTechnique)data.printTech);

    // Commit: document + bookkeeping (decode succeeded — nothing can fail now).
    project_.document = std::move(doc);
    project_.name  = data.projectName.empty() ? PathDisplayName(path)
                                              : data.projectName;
    project_.path  = path;
    project_.dirty = false;
    project_.docUnitSystem = (UI::Units::UnitSystem)std::min<std::uint8_t>(
        data.docUnitSystem, (std::uint8_t)(UI::Units::kUnitSystemCount - 1));
    project_.colorMode = data.colorMode
                             ? Project::ColorModeKind::Cmyk
                             : Project::ColorModeKind::Rgb;

    // Fresh editing state for the restored document (mirrors ResetDocument,
    // minus the demo seed).
    edit_.Clear();
    edit_.mode = EditorMode::Object;
    if (!project_.document->Pages().empty()) {
        const Ink::Page& pg = project_.document->Pages().front();
        edit_.cursor2D = { pg.pos.x + pg.size.x * 0.5,
                           pg.pos.y + pg.size.y * 0.5 };
        edit_.cursor2DValid = true;
    }
    docUndo_.Clear();
    transformOp_ = TransformOp{};
    canvasDrag_  = CanvasDrag{};
    addMenuOpen_ = false;
    viewportCtxOpen_ = false;
    osCursorHidden_  = false;
    markPreviewSaved_.clear();   // saved styles referenced the OLD document
    penActive_ = false;          // any pen construction referenced it too
    penHasPending_ = false;
    penNode_ = penFillNode_ = penTailNode_ = Ink::kNullNode;
    penFollowTail_.clear(); penTailJoinHasOut_ = false;
    followLocked_ = false;
    if (ink_) ink_->SetDocument(project_.document.get());

    // Restore the module the file was made with (Lot 11): activate it WITHOUT
    // rebuilding the layout (the file's LAY blob is authoritative, applied
    // below) and WITHOUT reseeding (the loaded content is the document).
    // A moduleId whose module is not installed falls back to Classic but is
    // KEPT on the project so a later save preserves it.
    {
        Modules::IModule* mod =
            data.moduleId.empty()
                ? nullptr
                : Modules::ModuleRegistry::Instance().Get(data.moduleId);
        if (mod || activeModule_) ActivateModule(mod, /*rebuildLayout=*/false);
        project_.moduleId = data.moduleId;
    }

    // The stored zone arrangement (tabs, cameras) — best-effort: a malformed
    // blob leaves the current layout in place.
    if (!data.layoutBlob.empty()) zoneLayout_.Deserialize(data.layoutBlob);
    // The editing-session state (per-mode tools + tool variants) — best-effort.
    if (!data.editorBlob.empty()) ApplyEditorStateBlob(data.editorBlob);

    showSplash_ = false;
    AddRecentFile(path);
    LogInfoAction("Open File", project_.name);
}

// ── Save (two-phase around ink_->EndFrame) ────────────────────────────────────

void Application::PrepareSavePass() {
    if (pendingSavePath_.empty()) return;
    // A live mark preview is a TEMPORARY style — it must appear neither in the
    // saved document nor in the thumbnail this frame renders.
    ClearMarkPreviewStyle();
    if (!ink_ || !project_.document) return;
    const auto& pages = project_.document->Pages();
    if (pages.empty()) return;             // no page → file saves without THMB
    const Ink::Page& pg = pages.front();
    if (pg.size.x <= 0.0 || pg.size.y <= 0.0) return;

    // Thumbnail size: page aspect, longest side 256 px.
    const double aspect = pg.size.x / pg.size.y;
    std::uint32_t w = 256, h = 256;
    if (aspect >= 1.0) h = (std::uint32_t)std::max(1.0, 256.0 / aspect);
    else               w = (std::uint32_t)std::max(1.0, 256.0 * aspect);

    // Fit the full page, centred.
    const double zoom = std::min((double)w / pg.size.x, (double)h / pg.size.y);
    const double panX = pg.pos.x - ((double)w / zoom - pg.size.x) * 0.5;
    const double panY = pg.pos.y - ((double)h / zoom - pg.size.y) * 0.5;

    Ink::View* view = ink_->AcquireView(&thumbViewKey_);
    view->SetViewport(w, h);
    view->SetCamera(panX, panY, zoom);
    view->SetBackground(Ink::SrgbToLinearPremultiplied(1, 1, 1, 1));
}

void Application::FinishSavePass() {
    if (pendingSavePath_.empty()) return;
    const std::string path = pendingSavePath_;
    pendingSavePath_.clear();
    if (!project_.document) return;

    // Read the just-rendered thumbnail view back and PNG-encode it.
    AcuThumb thumb;
    const auto& pages = project_.document->Pages();
    if (ink_ && !pages.empty()) {
        const Ink::Page& pg = pages.front();
        Ink::View* view = ink_->AcquireView(&thumbViewKey_);
        std::vector<std::uint8_t> rgba;
        std::uint32_t w = 0, h = 0;
        if (ink_->ReadViewPixels(view, rgba, w, h)) {
            png::EncodeRGBA(rgba.data(), (int)w, (int)h, thumb.png);
            thumb.page = pg.id;
            thumb.x = pg.pos.x;  thumb.y = pg.pos.y;
            thumb.w = pg.size.x; thumb.h = pg.size.y;
        }
    }

    const std::string name = PathDisplayName(path);
    std::string err;
    if (!AcuFile::Save(path, name, project_.moduleId, *project_.document,
                       (std::uint8_t)project_.docUnitSystem,
                       (std::uint8_t)project_.colorMode,
                       zoneLayout_.Serialize(), BuildEditorStateBlob(), thumb,
                       &err)) {
        LogInfoAction("Save File", err);
        // Drop any chained new/open intent — it must not run on a failed save.
        newFileAfterSave_ = false;
        pendingModuleId_.clear();
        pendingOpenPath_.clear();
        return;
    }

    project_.name  = name;
    project_.path  = path;
    project_.dirty = false;
    AddRecentFile(path);
    LogInfoAction("Save File", name);

    // "Unsaved changes" → Save → the held new-file / open-module / open-file
    // intent commits now that the write is on disk.
    if (newFileAfterSave_) {
        newFileAfterSave_ = false;
        CommitPendingNew();
    }
}

} // namespace App

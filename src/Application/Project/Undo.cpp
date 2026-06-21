#include "Application.h"
#include "ProjectFile.h"
#include <DesignSystem/DesignSystem.h>
#include <imgui.h>
#include <sstream>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Undo / Redo wiring for the MAIN (viewport) window.
//
//  Snapshot = the vector Document SERIALISED to a byte blob (the .acu DOCUMENT
//  codec). At end of Update(), while no gesture is live, the live document is
//  serialised and compared to the last committed blob; if it DIFFERS, one step
//  is pushed. This makes "one finished operation = one step" robust (it does not
//  rely on project_.dirty, which never falls back to false between edits — the
//  bug that made undo jump straight to the baseline). Selection is not in the
//  blob, so merely picking objects never creates a step.
//
//  Undo/Redo restore a blob into the live document. After any commit/undo/redo
//  we resync undoLast_ so the just-applied state is the new comparison baseline
//  and is not immediately re-committed.
// ─────────────────────────────────────────────────────────────────────────────

// The undo snapshot = the document blob (same codec as .acu) PLUS a small
// selection trailer (selection, active, vertex selection, active vertex, editor
// mode). Selection is NOT in the .acu format (it's runtime state), but Blender-
// style undo restores it, so each select / mode-toggle is its own step. The
// trailer is appended after the doc blob with a magic marker so RestoreDocBlob can
// find it; a blob without the marker (old/foreign) simply keeps the live selection.
namespace {
constexpr char kSelMagic[8] = { 'S','E','L','T','R','A','I','L' }; //TODO : why is it hardcoded letters ?
void WriteU64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back((char)((v >> (i * 8)) & 0xFF));
}
uint64_t ReadU64(const std::string& s, size_t& off) {
    uint64_t v = 0;
    for (int i = 0; i < 8 && off < s.size(); ++i, ++off)
        v |= (uint64_t)(uint8_t)s[off] << (i * 8);
    return v;
}
} // namespace

std::string Application::CaptureDocBlob() const {
    std::vector<uint8_t> b = ProjectFile::EncodeDocumentBlob(project_.document);
    std::string out(b.begin(), b.end());
    // Selection trailer.
    out.append(kSelMagic, sizeof kSelMagic);
    const auto& sel = project_.document.Selection();
    WriteU64(out, sel.size());
    for (uint64_t id : sel) WriteU64(out, id);
    WriteU64(out, project_.document.ActiveId());
    const auto& vsel = project_.document.VertSelection();
    WriteU64(out, vsel.size());
    for (const Renderer::VertRef& v : vsel) {
        WriteU64(out, v.shape); WriteU64(out, (uint64_t)(int64_t)v.part);
        WriteU64(out, (uint64_t)(int64_t)v.node);
    }
    const Renderer::VertRef av = project_.document.ActiveVert();
    WriteU64(out, av.shape); WriteU64(out, (uint64_t)(int64_t)av.part);
    WriteU64(out, (uint64_t)(int64_t)av.node);
    WriteU64(out, editorMode_ == EditorMode::Edit ? 1 : 0);
    return out;
}

void Application::RestoreDocBlob(const std::string& blob) {
    // Split the document blob from the selection trailer (appended by CaptureDocBlob
    // after kSelMagic). Without the trailer (foreign blob) we keep the live selection.
    size_t trailerPos = blob.rfind(std::string(kSelMagic, sizeof kSelMagic));
    std::string docPart = (trailerPos != std::string::npos) ? blob.substr(0, trailerPos)
                                                            : blob;
    std::vector<uint8_t> b(docPart.begin(), docPart.end());
    Renderer::Document tmp;
    if (!ProjectFile::DecodeDocumentBlob(b, tmp)) return;

    // Selection to restore: the trailer's (Blender-style undo restores selection +
    // mode), falling back to the live selection when there's no trailer.
    std::vector<uint64_t> keepSel  = project_.document.Selection();
    uint64_t              keepAct  = project_.document.ActiveId();
    std::vector<Renderer::VertRef> keepVerts = project_.document.VertSelection();
    Renderer::VertRef     keepActV = project_.document.ActiveVert();
    bool                  restoreMode = false; bool wantEdit = false;
    if (trailerPos != std::string::npos) {
        size_t off = trailerPos + sizeof kSelMagic;
        keepSel.clear(); keepVerts.clear();
        uint64_t n = ReadU64(blob, off);
        for (uint64_t i = 0; i < n; ++i) keepSel.push_back(ReadU64(blob, off));
        keepAct = ReadU64(blob, off);
        uint64_t nv = ReadU64(blob, off);
        for (uint64_t i = 0; i < nv; ++i) {
            Renderer::VertRef v;
            v.shape = ReadU64(blob, off);
            v.part  = (int)(int64_t)ReadU64(blob, off);
            v.node  = (int)(int64_t)ReadU64(blob, off);
            keepVerts.push_back(v);
        }
        keepActV.shape = ReadU64(blob, off);
        keepActV.part  = (int)(int64_t)ReadU64(blob, off);
        keepActV.node  = (int)(int64_t)ReadU64(blob, off);
        wantEdit = ReadU64(blob, off) != 0;
        restoreMode = true;
    }

    project_.document = std::move(tmp);
    project_.dirty = true;                 // restored state ≠ on-disk
    if (restoreMode) editorMode_ = wantEdit ? EditorMode::Edit : EditorMode::Object;

    // Re-apply the shape selection (ids that still exist).
    project_.document.ClearSelection();
    for (uint64_t id : keepSel)
        if (project_.document.FindShape(id)) project_.document.SelectAdd(id);
    if (keepAct && project_.document.FindShape(keepAct))
        project_.document.SetActive(keepAct);

    // In Edit Mode the selected objects must keep their baked path so the restored
    // vertex selection resolves (a parametric part has no nodes until baked, and the
    // editor bakes lazily each frame — but we validate the refs right now).
    if (editorMode_ == EditorMode::Edit)
        for (uint64_t id : project_.document.Selection())
            if (Renderer::Shape* s = project_.document.FindShape(id)) s->EnsurePath();

    // Re-apply the vertex selection (refs whose (shape,part,node) still resolve).
    auto vertValid = [&](const Renderer::VertRef& v) {
        const Renderer::Shape* s = project_.document.FindShape(v.shape);
        if (!s || v.part < 0 || v.part >= (int)s->parts.size()) return false;
        const Renderer::Part& p = s->parts[(size_t)v.part];
        return v.node >= 0 && v.node < (int)p.path.nodes.size();
    };
    project_.document.ClearVertSelection();
    for (const Renderer::VertRef& v : keepVerts)
        if (vertValid(v)) project_.document.VertSelectAdd(v);
    if (vertValid(keepActV)) project_.document.VertSelectAdd(keepActV);

    // If the undone step removed the object(s) we were editing (e.g. undoing the
    // very creation of a freshly drawn shape), there is nothing left to edit — drop
    // back to Object Mode rather than stranding the user in an empty Edit Mode.
    if (editorMode_ == EditorMode::Edit && !project_.document.HasSelection())
        editorMode_ = EditorMode::Object;
}

void Application::InitUndo() {
    undo_.Configure(
        /*capture*/ [this] { return CaptureDocBlob(); },
        /*restore*/ [this](const std::string& blob) { RestoreDocBlob(blob); });
    undo_.SetCapacity(undoBufferSteps_);
    undo_.Reset();
    undoLast_ = CaptureDocBlob();
}

void Application::ResetUndoHistory() {
    undo_.SetCapacity(undoBufferSteps_);
    undo_.Reset();                        // baseline = current document
    undoLast_ = CaptureDocBlob();
}

// A gesture is "live" while the user holds a drag/transform/edit/crop, or any
// mouse button is down: committing then would snapshot a half-finished state.
bool Application::AnyViewportGestureActive() const {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2]) return true;
    if (transformOp_.Active()) return true;
    if (toolState_.Active()) return true;
    if (editDrag_.Active()) return true;
    if (cropArtboard_ >= 0) return true;
    if (pageDrag_ >= 0) return true;
    // Modal cursor rotation (R under the 2D Cursor tool) writes doc.cursorRotation
    // every frame — defer the commit until it confirms, so the whole rotation is ONE
    // undo step (not hundreds). Same for the line-mark modal G/R/S.
    if (cursorRotate_.Active()) return true;
    if (markGrab_.Active()) return true;
    if (handleOp_.Active()) return true;         // modal single-handle G/R/S
    return false;
}

// End of Update(): if the document actually changed since the last step and no
// gesture is in flight, push exactly one undo step for the finished change.
void Application::CommitUndoIfPending() {
    if (AnyViewportGestureActive()) return;
    std::string now = CaptureDocBlob();
    if (now == undoLast_) return;                  // nothing actually changed
    undo_.Commit(pendingUndoLabel_);
    // Feed: skip the bare label if the action already logged a rich entry.
    if (!richLoggedPendingCommit_) LogInfoAction(pendingUndoLabel_);
    richLoggedPendingCommit_ = false;
    undoLast_ = std::move(now);
    pendingUndoLabel_ = "Edit";                    // reset the generic label
}

void Application::Action_Undo() {
    // Route to the window the user is interacting with (Blender-style: the two
    // histories never cross).
    if (activeUndoTarget_ == UndoTarget::Preferences) {
        if (!prefsUndo_.CanUndo()) return;
        std::string lbl = prefsUndo_.Undo();
        prefsUndoLast_ = CapturePrefsOverrides();   // resync the change detector
        LogInfoAction("Undo (" + lbl + ")");
        return;
    }
    if (!undo_.CanUndo()) return;
    DismissOperatorPanel();              // undo is "another action" → close the panel
    std::string lbl = undo_.Undo();
    undoLast_ = CaptureDocBlob();        // the restored state is the new baseline
    LogInfoAction("Undo (" + lbl + ")");
}

void Application::Action_Redo() {
    if (activeUndoTarget_ == UndoTarget::Preferences) {
        if (!prefsUndo_.CanRedo()) return;
        std::string lbl = prefsUndo_.Redo();
        prefsUndoLast_ = CapturePrefsOverrides();
        LogInfoAction("Redo (" + lbl + ")");
        return;
    }
    if (!undo_.CanRedo()) return;
    DismissOperatorPanel();
    std::string lbl = undo_.Redo();
    undoLast_ = CaptureDocBlob();
    LogInfoAction("Redo (" + lbl + ")");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preferences window history — snapshots the design-system OVERRIDES (the
//  Customisation / Theme / Accessibility edits). Snapshot = the OverrideManager
//  serialised to a string; restore deserialises it and re-applies the style.
// ─────────────────────────────────────────────────────────────────────────────

std::string Application::CapturePrefsOverrides() const {
    std::ostringstream os(std::ios::binary);
    DesignSystem::DesignSystem::Instance().GetOverrideManager().WriteToBinary(os);
    return os.str();
}

void Application::RestorePrefsOverrides(const std::string& blob) {
    auto& mgr = DesignSystem::DesignSystem::Instance().GetOverrideManager();
    mgr.Clear();
    std::istringstream is(blob, std::ios::binary);
    mgr.ReadFromBinary(is);
    // Persist + re-apply the global style so the restored overrides take effect.
    DesignSystem::DesignSystem::Instance().NotifyOverrideChange();
}

void Application::InitPrefsUndo() {
    prefsUndo_.Configure(
        /*capture*/ [this] { return CapturePrefsOverrides(); },
        /*restore*/ [this](const std::string& blob) { RestorePrefsOverrides(blob); });
    prefsUndo_.SetCapacity(undoBufferSteps_);
    prefsUndo_.Reset();
    prefsUndoLast_ = CapturePrefsOverrides();
    prefsUndoInited_ = true;
}

// Called at the end of the Preferences window frame: if the overrides changed
// and no mouse button is held (not mid slider-drag), push one undo step.
void Application::CommitPrefsUndoIfChanged() {
    if (!prefsUndoInited_) InitPrefsUndo();
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2]) return;
    std::string now = CapturePrefsOverrides();
    if (now != prefsUndoLast_) {
        prefsUndo_.Commit("Preferences edit");
        prefsUndoLast_ = std::move(now);
    }
}

} // namespace App

#include "Application.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  Internal copy / paste / cut (NOT the OS clipboard). Works on OBJECTS and
//  PAGES, from either the Viewport (document object selection) or the Outliner
//  (its node+object selection set). Deep copies are stored in `clipboard_`; paste
//  clones them with fresh ids. One undo step per paste/cut.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

// Fill `clipboard_` from a set of ids (objects and/or pages). When `ids` is empty
// the document's object selection is used (the Viewport context). Collections are
// skipped (a grouping node — copy its objects/pages explicitly).
void Application::ClipboardGather(const std::vector<uint64_t>& ids) {
    auto& doc = project_.document;
    clipboard_.clear();
    for (uint64_t id : ids) {
        if (doc.IsPageId(id)) {
            if (Renderer::Artboard* ab = doc.FindArtboardById(id))
                clipboard_.pages.push_back(*ab);          // deep copy (incl. shapes)
        } else if (Renderer::Shape* s = doc.FindShape(id)) {
            ClipObject o;
            o.shape = *s;                                  // deep copy
            o.pageOrigin = doc.PageOriginOfShape(id);
            clipboard_.objects.push_back(std::move(o));
        }
    }
}

void Application::ClipboardCopy(const std::vector<uint64_t>& ids) {
    auto& doc = project_.document;
    // No explicit ids → use the document object selection (Viewport context).
    std::vector<uint64_t> src = ids;
    if (src.empty()) src = doc.Selection();
    if (src.empty()) return;
    ClipboardGather(src);
}

void Application::ClipboardCut(const std::vector<uint64_t>& ids) {
    auto& doc = project_.document;
    std::vector<uint64_t> src = ids;
    if (src.empty()) src = doc.Selection();
    if (src.empty()) return;
    ClipboardGather(src);
    if (clipboard_.empty()) return;
    MarkUndoLabel("Cut");
    // Delete the cut items: pages first (removes their shapes), then loose objects
    // not already gone with a deleted page.
    for (const Renderer::Artboard& ab : clipboard_.pages) doc.RemoveArtboard(ab.id);
    for (const ClipObject& o : clipboard_.objects) doc.EraseShape(o.shape.id);
    doc.ClearSelection();
    project_.dirty = true;
}

void Application::ClipboardPaste() {
    if (clipboard_.empty()) return;
    auto& doc = project_.document;
    MarkUndoLabel("Paste");

    // ── Pages: each becomes a NEW artboard, offset so it doesn't sit exactly on
    //    the original; its shapes are re-added with fresh ids. ──
    uint64_t lastSel = 0;
    bool selectedAnything = false;
    for (const Renderer::Artboard& src : clipboard_.pages) {
        const Renderer::Vec2 off{ src.size.x * 0.0f + 40.0f, 40.0f };
        int idx = doc.AddArtboard(src.name.empty() ? "Page copy" : src.name + " copy",
                                  { src.pos.x + off.x, src.pos.y + off.y }, src.size);
        if (idx < 0) continue;
        doc.artboards[(size_t)idx].pageVisible  = src.pageVisible;
        doc.artboards[(size_t)idx].clipContents = src.clipContents;
        // Re-add each shape (page-relative geometry is unchanged → same layout in
        // the new page). Fresh ids via AddShape. collectionId is reset to the page
        // root (the copied collection structure isn't recreated here).
        for (const Renderer::Shape& s : src.shapes) {
            Renderer::Shape copy = s;
            copy.collectionId = 0;
            copy.parentId = 0;                 // parenting across the copy isn't remapped
            doc.AddShape(idx, std::move(copy));
        }
    }

    // ── Objects: pasted onto the active page (or the first page), nudged. ──
    if (!clipboard_.objects.empty()) {
        int ab = doc.artboards.empty() ? -1 : 0;
        if (uint64_t ap = doc.ActivePage(); ap) {
            int a = doc.ArtboardIndexById(ap);
            if (a >= 0) ab = a;
        } else if (doc.ActiveShape()) {
            int a = doc.ArtboardOfShape(doc.ActiveId());
            if (a >= 0) ab = a;
        }
        if (ab >= 0) {
            const Renderer::Vec2 dstOrigin = doc.artboards[(size_t)ab].pos;
            doc.ClearSelection();
            for (const ClipObject& o : clipboard_.objects) {
                Renderer::Shape copy = o.shape;
                copy.name = o.shape.name.empty() ? "Object copy" : o.shape.name + " copy";
                copy.collectionId = 0;
                copy.parentId = 0;
                // Keep the WORLD position: page-relative coords += (srcOrigin −
                // dstOrigin), then a small nudge so the paste is visible.
                copy.transform.translate.x += (o.pageOrigin.x - dstOrigin.x) + 12.0f;
                copy.transform.translate.y += (o.pageOrigin.y - dstOrigin.y) + 12.0f;
                uint64_t id = doc.AddShape(ab, std::move(copy));  // assigns id + SelectOnly
                if (id) { doc.SelectAdd(id); lastSel = id; selectedAnything = true; }
            }
            if (lastSel) doc.SetActive(lastSel);
        }
    }

    doc.SyncActivePageToSelection();
    (void)selectedAnything;
    project_.dirty = true;
}

// ── Context-aware entry points (bound to Ctrl+C / X / V) ─────────────────────
// Objects are mirrored between the document selection and the Outliner set, so
// copying objects works the same from the Viewport or the Outliner. PAGES only
// appear in the Outliner selection — so when the Outliner selection holds nodes
// (pages/collections) that the document selection doesn't, use the Outliner set.
std::vector<uint64_t> Application::ClipboardSourceIds() const {
    auto& doc = const_cast<Renderer::Document&>(project_.document);
    if (outlinerCur_) {
        for (uint64_t id : outlinerCur_->sel)
            if (doc.IsPageId(id) || doc.IsCollectionId(id))
                return outlinerCur_->sel;   // node selection → use the Outliner set
    }
    return {};   // → ClipboardCopy/Cut fall back to the document object selection
}
void Application::Action_Copy()  { ClipboardCopy(ClipboardSourceIds()); }
void Application::Action_Cut()   { ClipboardCut(ClipboardSourceIds()); }
void Application::Action_Paste() { ClipboardPaste(); }

} // namespace App

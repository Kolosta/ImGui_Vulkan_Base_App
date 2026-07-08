#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  OutlinerState — per-Outliner-editor state (docs/Ink/ROADMAP.md Lot 9),
//  rebuilt on the Ink document. Lives inside EditorState so two Outliner zones
//  are independent (display mode, search, expansion) and a tab carries its
//  state when it moves between zones.
//
//  The object SELECTION is NOT stored here — it lives in the shared
//  App::EditContext (one selection per document, mirrored by every Outliner and
//  Viewport, like the Viewport top bar and Blender). The Outliner reads/writes
//  that context directly.
// ─────────────────────────────────────────────────────────────────────────────

// How the Outliner organises the document:
//   • Layers      → pages → layer trees, TOP-of-stack first (a "layers" panel:
//     z-order, visibility, lock, blend). This is the primary view.
//   • Collections → the organisational collection sets (membership, per-set
//     visibility) — DOCUMENT_MODEL.md §7.
enum class OutlinerDisplayMode : uint8_t { Layers = 0, Collections };

struct OutlinerState {
    OutlinerDisplayMode display = OutlinerDisplayMode::Layers;

    // Collapsed groups / collections (ids). Absent id = expanded (default).
    std::unordered_set<uint64_t> collapsed;

    // Filters.
    char search[128] = { 0 };
    bool showObjects = true;
    bool showGroups  = true;

    // Inline rename in flight: the node/collection id being renamed, plus its
    // edit buffer. 0 = not renaming.
    uint64_t renaming = 0;
    char     renameBuf[128] = { 0 };

    // Numpad . / double-click — recentre on the active row next frame.
    bool reqScrollToActive = false;

    bool IsCollapsed(uint64_t id) const { return collapsed.count(id) != 0; }
    void ToggleCollapsed(uint64_t id) {
        if (!collapsed.erase(id)) collapsed.insert(id);
    }
};

} // namespace App

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace App {

struct EditorState;   // forward decl (the viewport-sync target is a leaf)

// ─────────────────────────────────────────────────────────────────────────────
//  OutlinerState — per-Outliner-editor state (docs/Ink/ROADMAP.md Lot 9),
//  rebuilt on the Ink document but restoring the legacy Outliner's design and
//  feature set. Lives inside EditorState so two Outliner zones are independent
//  (display mode, filters, search, expansion, viewport-sync) and a tab carries
//  its state when it moves between zones.
//
//  The object SELECTION lives in the shared App::EditContext (one selection per
//  document, mirrored by every Outliner and Viewport). Collections and pages —
//  which the document selection cannot hold — are selected here, in `sel`.
// ─────────────────────────────────────────────────────────────────────────────

// Order matches the legacy dropdown (Collections first, then Layers).
enum class OutlinerDisplayMode : uint8_t { Collections = 0, Layers };

// Object-state filter (legacy parity). Applied on top of the kind toggles.
enum class ObjStateFilter : uint8_t { All, Visible, Selected, Active, Selectable };

struct OutlinerState {
    OutlinerDisplayMode display = OutlinerDisplayMode::Collections;

    // ── Selection of NON-object rows (collections / pages). Objects are the
    // shared EditContext; these rows have no document-selection home. ──
    std::vector<uint64_t> sel;         // selected collection / page ids
    uint64_t              active = 0;  // last-clicked row (any kind)

    // ── Filters (which kinds + which object states are shown). ──
    bool showObjects = true, showPages = true, showCollections = true;
    bool showGroups  = true;   // layer groups (Ink Node groups)
    ObjStateFilter objState = ObjStateFilter::All;
    bool invertFilter = false;
    char search[128] = { 0 };

    // Collapsed groups / collections / pages (ids). Absent = expanded.
    std::unordered_set<uint64_t> collapsed;

    // Inline rename in flight: the id being renamed + its edit buffer (0 = none).
    // renameTakeFocus is true for the FIRST frame only (grabs the keyboard once).
    uint64_t renaming = 0;
    bool     renameTakeFocus = false;
    char     renameBuf[128] = { 0 };

    // ── Per-frame scratch (rebuilt each frame at the top of RenderOutliner). ──
    std::vector<uint64_t> rowOrder;      // visible rows in draw order (Shift range)
    bool reqScrollToActive = false;      // Numpad . / double-click → recentre

    // ── Viewport sync (top-bar button). Two phases (legacy parity):
    //   • syncPicking: armed, waiting for the user to click a Viewport zone;
    //   • syncTarget:  once chosen, the Outliner scopes to that viewport's page
    //     (Ink: the page the viewport's camera is centred on — the page-layout
    //     visibility of the legacy stack returns with the page-layout lot). ──
    bool         syncPicking = false;
    EditorState* syncTarget  = nullptr;

    bool IsCollapsed(uint64_t id) const { return collapsed.count(id) != 0; }
    void ToggleCollapsed(uint64_t id) {
        if (!collapsed.erase(id)) collapsed.insert(id);
    }
    bool RowSelected(uint64_t id) const {
        for (uint64_t s : sel) if (s == id) return true;
        return false;
    }
};

} // namespace App

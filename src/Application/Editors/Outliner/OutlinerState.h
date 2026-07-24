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

// Pseudo-id of the Collections-view "Project" root row — used for collapse
// tracking only (never a document id; document ids are monotonic from 1).
inline constexpr uint64_t kProjectRootRowId = ~0ull;

// Object-state filter (legacy parity). Applied on top of the kind toggles.
enum class ObjStateFilter : uint8_t { All, Visible, Selected, Active, Selectable };

// Which kind of CHILD row selChildObj/selChildMod refer to (see below) — a
// real discriminator rather than inferring it from selChildMod's sign, since
// a Fill/Stroke row's piece index is a real ≥0 value just like a modifier
// row's stack index, and the two must never be confused for one another.
enum class OutlinerChildKind : uint8_t { None, Modifier, LinkedData, Fill, Stroke };

struct OutlinerState {
    OutlinerDisplayMode display = OutlinerDisplayMode::Collections;

    // ── Selection of NON-object rows (collections / pages). Objects are the
    // shared EditContext; these rows have no document-selection home. ──
    std::vector<uint64_t> sel;         // selected collection / page ids
    uint64_t              active = 0;  // last-clicked row (any kind)
    // Objects and collections MAY be selected together, but only when this
    // Outliner built both. The document selection is shared and can change from
    // anywhere - a viewport click, a box select - and such a change still drops
    // the collection rows, which would otherwise linger as a phantom selection
    // nobody asked for. The signature is how the two are told apart.
    uint64_t objSig = 0;               // last seen document-selection signature
    bool     objSelfEdit = false;      // this Outliner changed it this frame
    int  activeModifier = -1;          // modifier row picked (Properties focus)

    // ── Selected CHILD row (both Collections AND Layers view, the latter for
    // Fill/Stroke — docs/Ink/NODE_GRAPH.md §4): a modifier, linked-data, fill
    // or stroke row reads as SELECTED here even though the document selection
    // is its owning object. selChildObj = the owning object id; selChildKind
    // says which kind of child it is; selChildMod = an auxiliary index whose
    // meaning depends on selChildKind (modifier stack index for Modifier,
    // unused for LinkedData, style.fills/strokes index for Fill/Stroke). ──
    uint64_t          selChildObj  = 0;
    OutlinerChildKind selChildKind = OutlinerChildKind::None;
    int               selChildMod  = -1;
    void ClearChildSel() {
        selChildObj = 0; selChildKind = OutlinerChildKind::None; selChildMod = -1;
    }

    // ── Filters (which kinds + which object states are shown). ──
    bool showObjects = true, showPages = true, showCollections = true;
    bool showGroups  = true;   // layer groups (Ink Node groups)
    ObjStateFilter objState = ObjStateFilter::All;
    bool invertFilter = false;
    char search[128] = { 0 };

    // Collapsed groups / collections / pages (ids). Absent = expanded.
    std::unordered_set<uint64_t> collapsed;
    // Collections view OBJECT rows expand the other way round (Blender):
    // collapsed by default, EXPANDED only when present here — the object's
    // children (modifier stack, linked data, parented objects) unfold on
    // demand and the collapsed row summarises them with icons + counts.
    std::unordered_set<uint64_t> expandedObjects;

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
    void SelAdd(uint64_t id)    { if (!RowSelected(id)) sel.push_back(id); }
    void SelRemove(uint64_t id) {
        for (std::size_t i = 0; i < sel.size(); ++i)
            if (sel[i] == id) { sel.erase(sel.begin() + (long)i); return; }
    }
    bool ObjExpanded(uint64_t id) const { return expandedObjects.count(id) != 0; }
    void ToggleObjExpanded(uint64_t id) {
        if (!expandedObjects.erase(id)) expandedObjects.insert(id);
    }
};

} // namespace App

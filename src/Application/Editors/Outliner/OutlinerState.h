#pragma once

#include <cstdint>
#include <vector>

namespace App {

struct EditorState;   // forward decl (the sync target is a leaf's EditorState)

// Per-Outliner-editor state. Lives INSIDE EditorState (like a Viewport's camera
// or a leaf's page layout) so two Outliner zones are fully independent — their
// display mode, filters, search, selection and viewport-sync never bleed into
// one another. Moving a tab between zones carries this state with it.
// How the Outliner organises objects:
//   • Collections → the unified collection/page tree (objects grouped by their
//     collection; the default editing organisation).
//   • Layers      → objects listed per PAGE in DRAW order (z-order, bottom-most
//     first), ignoring collections — a "layers" view. In the IOF module the page
//     shapes are kept in print-layer order, so this reads as the print stack.
enum class OutlinerDisplayMode { Collections, Layers };
enum class ObjStateFilter { All, Visible, Selected, Active, Selectable };

struct OutlinerState {
    // ── Selection (objects + collections + pages). For OBJECTS it is kept in
    // sync with the document/viewport selection. ──
    std::vector<uint64_t> sel;            // selected node ids (objects/colls/pages)
    uint64_t              active = 0;     // last-clicked node (the "outliner active")
    OutlinerDisplayMode   display = OutlinerDisplayMode::Collections;

    // ── Filters (which kinds + which object states are shown). ──
    bool showObjects = true, showPages = true, showCollections = true;
    bool showMeshes = true, showCurves = true;
    ObjStateFilter objState = ObjStateFilter::All;
    bool invertFilter = false;
    char  search[128] = {0};

    // ── Per-frame scratch (rebuilt each frame at the top of RenderOutliner). ──
    std::vector<uint64_t> rowOrder;       // visible rows in draw order (Shift range)
    std::vector<uint64_t> searchMatches;  // ids whose own name matches the query
    bool searchActive = false;
    // Numpad . — scroll/recentre this Outliner on the active object's row next
    // frame (Blender's "Frame Selected" equivalent). Consumed in RenderOutliner.
    bool reqScrollToActive = false;

    // ── Viewport sync (the top-bar "synchronise" button). Two phases:
    //   • syncPicking: armed by the button, waiting for the user to click a
    //     Viewport (a follow-mouse tooltip prompts; each hovered Viewport paints
    //     an orange full-zone preview and consumes the click). RMB/Escape cancels.
    //   • syncTarget: once chosen, this Outliner shows ONLY the objects AND pages
    //     visible in that Viewport (its page-layout visibility + orphan setting).
    //     Clicking the button again clears it.
    // syncTarget points at the chosen leaf's EditorState; it is validated against
    // the live layout each frame and dropped if that zone closes. nullptr = no
    // sync (the normal, full Outliner). ──
    bool         syncPicking = false;
    EditorState* syncTarget  = nullptr;
    // Per-frame cache (rebuilt while syncTarget is set): page-visibility for the
    // synced viewport, indexed by artboard, plus whether that viewport shows
    // orphan (page-less) objects. Makes the per-object filter O(1).
    std::vector<bool> syncPageVisible;
    bool              syncOrphans = true;
};

} // namespace App

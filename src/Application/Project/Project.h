#pragma once

#include <Renderer/Document/Document.h>
#include <imgui.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Shared project model.
//
//  Layering:
//   - Project   : the top-level document (like a .blend / .acu file). Created
//                 empty on launch. Wraps a Renderer::Document (the vector data)
//                 plus app-level bookkeeping (file path, dirty flag).
//   - Artboard  : a work surface (the white page) inside the document. A project
//                 can hold several, laid out in the same coordinate space.
//   - Shape     : drawable vector content living on an artboard.
//
//  ONE Project instance is owned by Application. Every Viewport zone and the
//  Outliner read/write THIS shared model — creating an artboard or a shape in
//  one Viewport makes it appear in every Viewport and in the Outliner. What is
//  NOT shared lives per-leaf in ZoneLayout::EditorState (camera/zoom/tool).
//
//  The vector document is rendered EXCLUSIVELY by the Vulkan CanvasRenderer; the
//  Project itself holds no rendering state.
// ─────────────────────────────────────────────────────────────────────────────

// Re-export the document types into the App namespace so existing call sites
// (`App::Artboard`, `project_.artboards`) keep compiling unchanged.
using Artboard = Renderer::Artboard;
using Shape    = Renderer::Shape;

// Editor UI settings persisted PER PROJECT (saved in the .acu, restored on reopen
// — not reset by a new document). These are the menu-bar toggles/choices that are
// otherwise app-global; the per-leaf view (pan/zoom/page layout/outliner filter)
// lives in the LAYOUT blob. Application syncs its live members to/from here around
// save/load.
struct EditorSettings {
    int   pivotMode          = 3;     // PivotMode (MedianPoint)
    int   transformOrient    = 0;     // TransformOrientation (Global)
    bool  show2DCursor       = true;
    bool  showMetrics        = false;
    // Snap settings.
    bool  snapEnabled        = false;
    int   snapMode           = 0;     // SnapSettings::Mode (Increment)
    int   snapBase           = 0;     // SnapSettings::Base (Closest)
    bool  snapAffectMove     = true;
    bool  snapAffectRotate   = true;
    bool  snapAffectScale    = true;
    float snapRotIncrement   = 45.0f;
    float snapRotPrecision   = 5.0f;
    // Per-mode active tool memory (Blender keeps a distinct tool per mode).
    // `objectModeTool` is restored when leaving Edit; `editToolByObject` remembers,
    // per object id, the tool it last used in Edit Mode. Persisted in TAG_VSET.
    std::string objectModeTool = "tool.select";
    std::map<uint64_t, std::string> editToolByObject;
    // Default fill / stroke colours for new shapes (menu-bar swatches). RGBA 0..1.
    float defaultFill[4]   = { 0.80f, 0.80f, 0.82f, 1.0f };
    float defaultStroke[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
};

struct Project {
    std::string        name;          // empty until saved (display name)
    std::string        path;          // .acu file path; empty until saved
    bool               dirty = false; // unsaved changes pending
    EditorSettings     editorSettings; // persisted menu-bar settings (see above)
    // Module the project was created with ("" = Classic). Persisted in META so
    // reopening a file restores its module (a file can't switch modules later).
    std::string        moduleId;
    Renderer::Document document;       // the vector model

    // Saved thumbnail (PNG bytes) written to the .acu THMB section and read by
    // the Windows shell thumbnail provider. Empty = no thumbnail (the .acu shows
    // the extension icon instead). Regenerated on demand from a chosen page/zone.
    std::vector<uint8_t> thumbnailPng;
    // Which artboard the thumbnail is generated from (index), and an optional
    // sub-region of it in doc-units (w/h <= 0 → whole page). Persisted so
    // "Update thumbnail" repeats the user's framing.
    int   thumbArtboard = 0;
    Renderer::Vec2 thumbRegionMin{0, 0};
    Renderer::Vec2 thumbRegionSize{0, 0};   // (0,0) = whole artboard

    // Convenience: the document's artboards (every consumer iterates these).
    std::vector<Renderer::Artboard>&       artboards()       { return document.artboards; }
    const std::vector<Renderer::Artboard>& artboards() const { return document.artboards; }

    // Reset to a brand-new empty project (no artboard, default name).
    void Reset() {
        name.clear();
        path.clear();
        moduleId.clear();
        dirty = false;
        document.Clear();
        thumbnailPng.clear();
        thumbArtboard = 0;
        thumbRegionMin = thumbRegionSize = {0, 0};
        editorSettings = EditorSettings{};
    }

    // Add an artboard and mark the project dirty. Returns its index.
    // Accepts ImVec2 for the legacy call sites; converts to Renderer::Vec2.
    int AddArtboard(const std::string& nm, ImVec2 pos, ImVec2 size) {
        int idx = document.AddArtboard(nm, {pos.x, pos.y}, {size.x, size.y});
        dirty = true;
        return idx;
    }

    // Title shown in the menu-bar project tab.
    //   unsaved & new   → "* (unsaved)"
    //   saved, modified  → "name *"
    //   saved, clean     → "name"
    std::string TabTitle() const {
        if (name.empty()) return "* (unsaved)";
        return dirty ? name + " *" : name;
    }
};

} // namespace App

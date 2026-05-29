#pragma once

#include <imgui.h>
#include <string>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Shared project model.
//
//  Layering:
//   - Project   : the top-level document (like a .blend / .psd / .ai file).
//                 Created empty on launch. Save-to-file comes later.
//   - Artboard  : a work surface (the white page) inside a project. A project
//                 can hold several, laid out in the same coordinate space.
//   - Object    : (later) drawable content living on an artboard.
//
//  ONE Project instance is owned by Application. Every Viewport zone and the
//  Outliner read/write THIS shared model — creating an artboard in one
//  Viewport makes it appear in every Viewport and in the Outliner. What is
//  NOT shared lives per-leaf in ZoneLayout::EditorState (camera/zoom/tool).
// ─────────────────────────────────────────────────────────────────────────────

struct Artboard {
    std::string name;
    ImVec2      pos{0, 0};        // top-left in project doc-units
    ImVec2      size{1920, 1080}; // in project doc-units
};

struct Project {
    std::string           name;          // empty until saved
    bool                  dirty = false; // unsaved changes pending
    std::vector<Artboard> artboards;

    // Reset to a brand-new empty project (no artboard, default name).
    void Reset() {
        name.clear();
        dirty = false;
        artboards.clear();
    }

    // Add an artboard and mark the project dirty. Returns its index.
    int AddArtboard(const std::string& nm, ImVec2 pos, ImVec2 size) {
        artboards.push_back(Artboard{nm, pos, size});
        dirty = true;
        return (int)artboards.size() - 1;
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

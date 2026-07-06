#pragma once

#include <string>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Shared project model — Ink-rework transitional version (Lot 0).
//
//  The old Project wrapped a Renderer::Document (quarantined under
//  src/_legacy/). During the Ink engine rewrite the project keeps only the
//  app-level bookkeeping (display name, file path, dirty flag, module id);
//  the new document model arrives with Ink Lot 2 (docs/Ink/DOCUMENT_MODEL.md)
//  and the .acu v2 persistence with Lot 10.
//
//  ONE Project instance is owned by Application.
// ─────────────────────────────────────────────────────────────────────────────
struct Project {
    std::string name;           // empty until saved (display name)
    std::string path;           // .acu file path; empty until saved
    bool        dirty = false;  // unsaved changes pending
    // Module the project was created with ("" = Classic). Will be persisted in
    // the .acu v2 META so reopening a file restores its module.
    std::string moduleId;

    // Reset to a brand-new empty project.
    void Reset() {
        name.clear();
        path.clear();
        moduleId.clear();
        dirty = false;
    }

    // Title shown in the title bar.
    //   unsaved & new    → "* (unsaved)"
    //   saved, modified  → "name *"
    //   saved, clean     → "name"
    std::string TabTitle() const {
        if (name.empty()) return "* (unsaved)";
        return dirty ? name + " *" : name;
    }
};

} // namespace App

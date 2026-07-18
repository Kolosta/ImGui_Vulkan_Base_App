#pragma once

#include <Ink/Document/Document.h>
#include <UI/Units.h>
#include <memory>
#include <string>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Shared project model.
//
//  The project owns THE document — an Ink::Document (docs/Ink/
//  DOCUMENT_MODEL.md) — plus the app-level bookkeeping (display name, file
//  path, dirty flag, module id). ONE Project instance is owned by
//  Application; every Viewport zone renders this same document through the
//  Ink engine, and editors mutate it through its typed operations (which feed
//  the engine's change-driven recompiles).
//
//  Persistence: .acu v2 (docs/acu-format.md) — AcuFile.h is the codec,
//  App/ProjectIO.cpp the save/open flow.
// ─────────────────────────────────────────────────────────────────────────────
struct Project {
    std::string name;           // empty until saved (display name)
    std::string path;           // .acu file path; empty until saved
    bool        dirty = false;  // unsaved changes pending
    // Module the project was created with ("" = Classic). Persisted in the
    // .acu META; applying it on open returns with module re-entry (Lot 11).
    std::string moduleId;

    // The document. Always valid after Reset(); recreated (never reused) for
    // a new project so engine-side pointers are refreshed explicitly.
    std::unique_ptr<Ink::Document> document;

    // The DOCUMENT display-unit system (Metric / Imperial / Typographic /
    // Pixel), persisted in the .acu. Followed by every app input except a
    // viewport's rulers + N-panel Item tab (those use the per-viewport unit).
    // Geometry is stored once in the base unit (px); this only changes display.
    UI::Units::UnitSystem docUnitSystem = UI::Units::UnitSystem::Pixel;

    // Reset to a brand-new empty project: fresh document with one default
    // page (size overridable — a module supplies its own via DefaultPageSize).
    // The caller re-hands the new document to the engine.
    void Reset(double pageW = 1920.0, double pageH = 1080.0) {
        name.clear();
        path.clear();
        moduleId.clear();
        dirty = false;
        docUnitSystem = UI::Units::UnitSystem::Pixel;
        document = std::make_unique<Ink::Document>();
        document->AddPage("Page 1", { 0, 0 }, { pageW, pageH });
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

#pragma once

#include <Ink/Document/Document.h>
#include <cstdint>
#include <string>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  AcuFile — the `.acu` v2 project codec (docs/acu-format.md, docs/Ink/
//  ROADMAP.md Lot 10). One binary file = the Ink document + the zone layout +
//  a page thumbnail.
//
//  The CONTAINER frame is unchanged from v1 (magic 'ACU1', u32 version, then
//  [tag u32][len u32][payload] sections) so the Windows shell thumbnail
//  provider keeps locating THMB without an update. The container version is
//  bumped to 2 — a CLEAN BREAK: the v1 document model is quarantined with the
//  old engine, so v1 files are refused with a clear message (never
//  half-loaded).
//
//  Serialisation is APP-SIDE by design (the Ink model does no file I/O):
//  Load parses into the plain containers below; the caller commits them via
//  Ink::Document::Restore, so a corrupt file never touches the live project.
// ─────────────────────────────────────────────────────────────────────────────

// Parsed .acu payload, decoded into temporaries.
struct AcuData {
    std::string projectName;
    std::string moduleId;                     // module the project was made with
    std::vector<Ink::Page>       pages;
    std::vector<Ink::Node>       nodes;
    std::vector<Ink::Collection> collections;
    // The document colour table (v21). Paints reference these by id; empty for
    // older files, whose paints all carry literal colours.
    std::vector<Ink::Swatch>     swatches;
    // Render in plate order rather than layer order (v22; off pre-v22).
    bool                         printOrderRender = false;
    // Printing technique (0 CMYK · 1 CMYK+B · 2 PMS), v23.
    std::uint8_t                 printTech = 0;
    Ink::NodeId                  nextId = 1;  // id-allocator high-water mark
    // The document display-unit system index (0 Metric · 1 Imperial ·
    // 2 Typographic · 3 Pixel). Defaults to Pixel for pre-v12 documents.
    std::uint8_t                 docUnitSystem = 3;
    // Document colour mode (0 RGB · 1 CMYK). Defaults to RGB pre-v16.
    std::uint8_t                 colorMode = 0;
    std::vector<std::uint8_t>    layoutBlob;  // ZoneLayout blob (empty = none)
    // EDST section: opaque, self-versioned editing-session blob written by the
    // Application (per-mode active tools + tool variants). Empty = none.
    std::vector<std::uint8_t>    editorBlob;
};

// The THMB section content: a PNG preview of one page (empty png = skip the
// section) plus the captured doc-space rect (informational).
struct AcuThumb {
    std::vector<std::uint8_t> png;
    Ink::NodeId page = Ink::kNullNode;
    double x = 0, y = 0, w = 0, h = 0;
};

namespace AcuFile {

inline constexpr std::uint32_t kContainerVersion = 2;

// Write the whole project to `path`. False on I/O failure (`error` gets a
// user-displayable reason).
bool Save(const std::string& path, const std::string& projectName,
          const std::string& moduleId, const Ink::Document& doc,
          std::uint8_t docUnitSystem, std::uint8_t colorMode,
          const std::vector<std::uint8_t>& layoutBlob,
          const std::vector<std::uint8_t>& editorBlob, const AcuThumb& thumb,
          std::string* error = nullptr);

// Parse `path` into `out`. False on I/O failure / corrupt data / a v1 file /
// a newer container than this build understands.
bool Load(const std::string& path, AcuData& out, std::string* error = nullptr);

} // namespace AcuFile
} // namespace App

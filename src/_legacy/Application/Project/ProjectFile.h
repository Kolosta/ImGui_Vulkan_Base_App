#pragma once

#include "Project.h"
#include <cstdint>
#include <string>
#include <vector>

namespace App {

class ZoneLayout;   // forward decl: layout serialised opaquely via its API

// ─────────────────────────────────────────────────────────────────────────────
//  ProjectFile — the proprietary ".acu" document format.
//
//  Design goals (per the brief): a SINGLE binary file, compact and fast, and
//  VERSIONED with MIGRATION — opening an older .acu must succeed (like Blender
//  opening an old .blend), not be rejected.
//
//  Layout:
//    [ MAGIC  : 4 bytes = 'A','C','U','1' ]
//    [ version: u32 (file format version, = CURRENT_VERSION when written) ]
//    [ sections... ] each: [ tag:u32 ][ byteLength:u32 ][ payload ]
//
//  Sections (tags), order not significant, unknown tags skipped (forward-compat):
//    META     'M' — app version string, timestamps
//    DOCUMENT 'D' — the vector model (artboards → shapes → paths/paints)
//    LAYOUT   'L' — the zone tree (splits, tabs, per-leaf cameras), opaque blob
//                   produced/consumed by ZoneLayout::Serialize/Deserialize
//
//  Migration: Load() reads `version`, then each section decoder is written to
//  understand its OWN evolution. Older field sets are upgraded to the current
//  model in-place. CURRENT_VERSION is bumped whenever the on-disk layout of any
//  section changes; the decoders keep handling every prior version.
// ─────────────────────────────────────────────────────────────────────────────

class ProjectFile {
public:
    static constexpr uint32_t MAGIC           = 0x31554341; // 'A''C''U''1' (LE)
    static constexpr uint32_t CURRENT_VERSION = 1;

    // Save `project` (vector document) + `layout` (zones/tabs/cameras) to `path`.
    // Returns false on I/O error. On success the project's name/path/dirty are
    // the caller's responsibility to update.
    static bool Save(const std::string& path,
                     const Project& project, const ZoneLayout& layout);

    // Load `path` into `project` + `layout`, migrating older versions. Returns
    // false if the file is missing, not an .acu, or unrecoverably corrupt.
    static bool Load(const std::string& path,
                     Project& project, ZoneLayout& layout);

    // Encode / decode JUST the vector Document to/from a byte blob, reusing the
    // exact same versioned serialisation as the DOCUMENT section. Used by the
    // undo system (a snapshot = an encoded document; comparing blobs detects a
    // change). Decode returns false on a malformed/empty blob.
    static std::vector<uint8_t> EncodeDocumentBlob(const Renderer::Document& doc);
    static bool DecodeDocumentBlob(const std::vector<uint8_t>& blob,
                                   Renderer::Document& outDoc);
};

} // namespace App

# The `.acu` project format (v2)

`.acu` is Carto's proprietary project file: a **single binary file** that
encapsulates an entire project — the Ink vector document **and** the
application state (zone layout, open tabs, per-view cameras) — so reopening it
restores the editor exactly as it was left.

Design goals: **compact and fast** (binary, length-prefixed, no text parsing)
and **versioned** — unknown *sections* are skipped (forward compatibility) and
the document payload carries its own version so fields can be added without
touching the container.

Implemented by [`AcuFile`](../src/Application/Project/AcuFile.h)
(`Save`/`Load`, app-side — the Ink model does no file I/O; a parsed file is
committed through `Ink::Document::Restore`). The save/open flow, the async
dialogs and the thumbnail render live in
[`ProjectIO.cpp`](../src/Application/App/ProjectIO.cpp).

> **v2 is a clean break.** Container version 1 carried the previous engine's
> document model, which is quarantined under `src/_legacy/` with that engine.
> v1 files are refused with a clear message — never half-loaded. The container
> FRAME, however, is unchanged, so the Windows shell thumbnail provider
> (`src/Shell/thumbprovider/`) reads v1 and v2 files alike.

## Encoding primitives

All integers are **little-endian**. Strings are `u32 length` + raw UTF-8 bytes
(no terminator). `f32`/`f64` are IEEE-754. No padding/alignment. Everything
spatial is `f64` (the document is double-precision end-to-end — unbounded
canvas, docs/Ink/README req. 9).

## Container layout

```
[ MAGIC   : u32 = 'A''C''U''1' (0x31554341 LE) ]
[ version : u32 = 2 ]
[ section ]*                       // until EOF
```

Each **section** is tag + length + payload, so unknown sections (written by a
newer app) are skipped cleanly:

```
[ tag        : u32 ]
[ byteLength : u32 ]
[ payload    : byteLength bytes ]
```

| Tag    | u32          | Contents |
|--------|--------------|----------|
| `META` | `0x4154454D` | App name (string), project display name (string), module id (string; "" = Classic) |
| `DOC`  | `0x00434F44` | The Ink document (see below) |
| `LAY`  | `0x0059414C` | The zone-tree blob from `ZoneLayout::Serialize` (opaque, self-versioned) |
| `THMB` | `0x424D4854` | Page thumbnail: `[pngLen:u32][PNG bytes][pageId:u64][x,y,w,h : f32]` |

`DOC` is required to load; `META`/`LAY`/`THMB` are best-effort. `THMB` is
written **LAST** so the shell thumbnail provider locates it by walking the
tag/length sections without parsing the document; its leading
`[pngLen][PNG]` frame is identical to v1 (the provider reads only that). The
PNG is a **real Ink render** of page 1 (strokes, patterns, blends, MSAA),
captured through an off-screen view + `Renderer::ReadViewPixels` at save time.
Decoding happens into temporaries and is committed only once `DOC` parses and
`Document::Restore` validates it, so a corrupt file never half-replaces the
live project.

## DOC section

Carries its own version (`kDocVersion`, current = **1** — a fresh counter for
the Ink model). Decoders must read every version ≤ current and default new
fields. Enums are stored as `u8` and clamped to their valid range on read.

```
[ docVersion : u32 = 1 ]
[ nextId     : u64 ]                 // id-allocator high-water mark
[ pageCount : u32 ]
page* {
    [ id : u64 ][ name : string ]
    [ pos.x, pos.y : f64 ][ size.x, size.y : f64 ]
    [ background : r,g,b,a f32 ]     // display substrate, not a layer
    [ childCount : u32 ] ( [ childNodeId : u64 ] )*   // painter order
}
[ nodeCount : u32 ]
node* {                              // written PRE-ORDER per page
    [ id : u64 ][ kind : u8 ]        // 0 Group, 1 Path, 2 Instance
    [ name : string ]
    [ parent : u64 ][ page : u64 ]   // layer-tree owner + owning page
    [ parentId : u64 ]               // object parenting (transform relation)
    transform { tx,ty,sx,sy,rotation : f64 }
    [ flags : u8 ]    // bit0 visible, bit1 locked, bit2 isolate,
                      // bit3 clip (group), bit4 isMask
    [ opacity : f32 ][ blend : u8 ]
    [ childCount : u32 ] ( [ childNodeId : u64 ] )*
    [ targetRef : u64 ]              // Instance target (0 = none)
    path {
        [ subpathCount : u32 ]
        subpath* {
            [ closed : u8 ][ anchorCount : u32 ]
            anchor* { pos.x,y : f64; in.x,y : f64; out.x,y : f64
                      [ flags : u8 ] }   // bit0 hasIn, bit1 hasOut,
                                         // bits2-3 kind (Corner/Smooth/Symmetric)
        }
    }
    style {
        [ fillCount : u32 ]
        fill* {
            [ kind : u8 ][ enabled : u8 ][ rule : u8 ][ opacity : f32 ]
            paint { r,g,b,a : f32 }              // linear-light, straight alpha
            pattern { [ motifRef : u64 ]
                      [ spacingX, spacingY, phaseX, phaseY : f64 ]
                      [ rotation, motifRotation, scale : f64 ]
                      [ clip : u8 ][ anchor : u8 ] }
        }
        [ strokeCount : u32 ]
        stroke* {
            [ enabled : u8 ] paint { r,g,b,a : f32 }
            [ width : f64 ][ align : u8 ][ cap : u8 ][ join : u8 ]
            [ miterLimit : f64 ][ widthSpace : u8 ]
            [ dashCount : u32 ] ( [ dash : f64 ] )* [ dashOffset : f64 ]
        }
    }
    [ modifierCount : u32 ]
    modifier* {                      // fixed layout: every field, every kind
        [ kind : u8 ][ enabled : u8 ]
        [ count : u32 ] step { tx,ty,sx,sy,rotation : f64 } [ stepSpace : u8 ]
        [ motifRef : u64 ][ distribute : u8 ][ spacing : f64 ]
        [ alongCount : u32 ][ align : u8 ][ startTrim : f64 ][ endTrim : f64 ]
        [ op : u8 ][ operandRef : u64 ]
    }
}
[ collectionCount : u32 ]
collection* {
    [ id : u64 ][ name : string ][ colorTag : r,g,b,a f32 ][ visible : u8 ]
    [ memberCount : u32 ] ( [ nodeId : u64 ] )*
    [ childCount : u32 ]  ( [ collectionId : u64 ] )*
}
```

Colors are **user data in linear light** (straight alpha), exactly as the
model stores them — not design-system tokens.

## LAYOUT section

An opaque blob owned by `ZoneLayout` (its own internal version — currently
v7). It encodes the binary split tree: splits (orientation + absolute sizes)
and leaves (tabs = editor ids + per-view double cameras + per-editor state).
See `ZoneLayout::Serialize/Deserialize`.

## Load/validate pipeline

1. `AcuFile::Load` parses the container into `AcuData` **temporaries**
   (bounds-checked reader — any overrun fails the load).
2. `Ink::Document::Restore` validates STRUCTURE (id uniqueness, bidirectional
   page/parent ↔ children consistency) — a malformed file is refused whole.
   Non-structural references (parentId, instance targets, modifier refs,
   collection members) pointing at missing nodes are sanitised to null /
   dropped, so a file that lost a referenced node still opens.
3. Only then does the app commit: document swap, editing-state reset,
   `SetDocument` to the engine, layout blob applied, recent-files updated.

## Save pipeline (two-phase, one frame)

A save request only **arms** the path. Right before `ink_->EndFrame()` the app
sets up an off-screen view fit to page 1; EndFrame renders it through the
normal pipeline; right after, `Renderer::ReadViewPixels` copies it back,
`PngWrite.h` encodes it, and the file is written. The chained intent from the
"Unsaved changes" dialog (new file / open module / open file) commits only
after the write succeeds.

## Versioning rules

- **Container**: `AcuFile::kContainerVersion` (= 2). `Load` refuses
  `version != 2`: older is the dead v1 model (clean break), newer is a file
  from a future build.
- **Section-local**: `DOC` carries `kDocVersion`; a decoder reads every prior
  version and defaults new fields. Adding a field = bump `kDocVersion`, gate
  the read on `version >= N`.
- Unknown whole *sections* need no version bump — the tag/length frame skips
  them.

## Not stored here

Runtime/UI preferences that are global (not per-project) live in their own
files: `design_system.bin`, `shortcuts.dat`, `imgui.ini`. Recent files live in
the OS user-prefs folder (`SDL_GetPrefPath` → `recent.txt`). The `.acu` file
is purely the project document + its window/zone arrangement.

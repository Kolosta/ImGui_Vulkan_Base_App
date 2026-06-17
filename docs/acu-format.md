# The `.acu` project format

`.acu` is Carto's proprietary project file: a **single binary file** that
encapsulates an entire project — the vector document **and** the application
state (zone layout, open tabs, per-view cameras) — so reopening it restores the
editor exactly as it was left.

Design goals: **compact and fast** (binary, length-prefixed, no text parsing),
and **versioned with migration** — opening an older `.acu` succeeds and is
upgraded to the current model (like Blender opening an old `.blend`), never
rejected for being old.

Implemented by [`ProjectFile`](../src/Application/Project/ProjectFile.h)
(`Save`/`Load`) with the layout blob produced/consumed by
[`ZoneLayout::Serialize/Deserialize`](../src/Application/Layout/ZoneLayout.h).

## Encoding primitives

All integers are **little-endian**. Strings are `u32 length` + raw UTF-8 bytes
(no terminator). Floats are IEEE-754 32-bit. There is no padding/alignment.

## Container layout

```
[ MAGIC   : u32 = 'A''C''U''1' (0x31554341 LE) ]
[ version : u32 = container version (CURRENT_VERSION) ]
[ section ]*                       // until EOF
```

Each **section** is tag + length + payload, so unknown sections (written by a
newer app) are skipped cleanly — forward compatibility:

```
[ tag        : u32 ]
[ byteLength : u32 ]
[ payload    : byteLength bytes ]
```

| Tag    | u32          | Contents |
|--------|--------------|----------|
| `META` | `0x4154454D` | App name (string), project display name (string) |
| `DOC`  | `0x00434F44` | The vector document (see below) |
| `LAY`  | `0x0059414C` | The zone-tree blob from `ZoneLayout::Serialize` |
| `THMB` | `0x424D4854` | Page thumbnail: `[pngLen:u32][PNG bytes][artboard:u32][rmin.x,rmin.y][rsz.x,rsz.y : f32]` |

`DOC` is required to load; `META`/`LAY`/`THMB` are best-effort. `THMB` holds a
PNG preview of a chosen page/region (default Page 1), written LAST so a Windows
shell thumbnail provider can locate it by walking the tag/length sections without
parsing the document. Decoding happens into temporaries and is committed only
once `DOC` parses, so a corrupt file never half-replaces the live project.

## DOCUMENT section

Carries its own internal version (`DOC_VERSION`, current = **11**) so the document
model can evolve independently of the container; the decoder understands every
version ≤ current and migrates older ones. The authoritative, per-version field
list lives in the `DOC_VERSION` comment block at the top of
`src/Application/Project/ProjectFile.cpp` (v3 multi-part objects, v4–v8 part
type/spline, v9 collection colour, v10 page visibility, v11 unified Project-root
tree with page↔collection nesting). The layout below describes the v2 baseline;
later fields are appended per that comment block.

**v2 layout:**
```
[ docVersion : u32 = 2 ]
[ nextId     : u64 ]                // id allocator high-water mark
[ cursor.x, cursor.y : f32 ]        // the 2D cursor (doc-units)
[ collectionCount : u32 ]
collection* {
    [ id : u64 ][ name : string ][ parentId : u64 ]
    [ childCount : u32 ] ( [ childCollectionId : u64 ] )*
}
[ artboardCount : u32 ]
artboard* {
    [ id : u64 ][ name : string ]
    [ pos.x, pos.y : f32 ][ size.x, size.y : f32 ]
    [ shapeCount : u32 ]
    shape* {
        [ id : u64 ][ kind : u32 ][ name : string ][ visible : u8 ]
        [ collectionId : u64 ]
        [ pos.x, pos.y : f32 ][ size.x, size.y : f32 ]   // parametric params
        [ origin.x, origin.y : f32 ]
        transform { [ tx,ty : f32 ][ rotate : f32 ][ sx,sy : f32 ] }
        [ nodeCount : u32 ]
        node* {
            [ pos.x,pos.y : f32 ][ hIn.x,hIn.y : f32 ][ hOut.x,hOut.y : f32 ]
            [ flags : u8 ]   // bit0 = hasIn, bit1 = hasOut
            [ mode  : u8 ]   // 0 Free, 1 Aligned, 2 Mirrored, 3 Vector
        }
        [ closed : u8 ]
        fill   { [enabled:u8][r,g,b,a : f32] }
        stroke { [enabled:u8][r,g,b,a : f32][width : f32] }
    }
}
```

`kind` ∈ { 0 Rectangle, 1 Ellipse, 2 Triangle, 3 Polyline, 4 Curve, 5 Path }.
Rectangle/Ellipse stay parametric (pos/size) until edited; other kinds carry the
editable `node[]`. Shape colours are **user data** (not design-system tokens).

**v1 → v2 migration:** the v1 shape stored parallel `points[]` + `segments[]`
(Line|CubicBezier) and no transform/origin/collection. The v1 decoder reads that
form and converts the path into `node[]` (cubic controls → node handles, mode
Free), defaulting transform to identity, origin to (0,0), collection to root.
Rectangle/Ellipse keep their parametric pos/size.

## LAYOUT section

An opaque blob owned by `ZoneLayout` (its own internal version). It encodes the
binary split tree:

```
[ blobVersion : u32 ]
node:
    [ nodeKind : u8 ]                       // 0 leaf, 1 split, 2 null
    leaf  → [ activeTab : u32 ][ tabCount : u32 ]
            tab* { [ editorKind : u32 ][ pan.x, pan.y : f32 ][ zoom : f32 ][ docUnit : u32 ] }
    split → [ vertical : u8 ][ firstPx : f32 ][ initRatio : f32 ][ lastUsable : f32 ]
            node (child a)  node (child b)
```

## Versioning & migration

- **Container**: `ProjectFile::CURRENT_VERSION`. `Load` rejects only a *newer*
  container it cannot understand (`version > CURRENT_VERSION`); older versions
  are read and upgraded.
- **Section-local**: `DOC` and `LAY` each carry their own version, so a change
  to one section bumps only that version. Each decoder keeps reading every prior
  version and fills new fields with sensible defaults.
- **Adding a field**: bump the relevant section version; in the decoder, read
  the new field only when `version >= N`, defaulting it otherwise. Unknown whole
  *sections* need no version bump — they are skipped via the tag/length frame.

## Not stored here

Runtime/UI preferences that are global (not per-project) live in their own files:
`design_system.bin`, `shortcuts.dat`, `imgui.ini`. The `.acu` file is purely the
project document + its window/zone arrangement.

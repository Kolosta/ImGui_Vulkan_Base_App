# Shell — Windows shell integration for `.acu`

Makes `.acu` files show, in Windows Explorer, **a page preview** (medium/large/
extra-large icons) and **the app logo** (small icons / no preview) — the same
behaviour as `.blend` files.

Windows does **not** do this automatically for an unknown format: an aperçu needs
a registered **IThumbnailProvider** COM handler, and the icon needs a registry
association. This module provides both, per-user (no admin).

## Pieces

- **`Shell` (static lib)** — `ShellIntegration.{h,cpp}`: registry-only. Registers,
  under `HKCU\Software\Classes`:
  - ProgID `Carto.Project` + `DefaultIcon` → a generated `.ico`;
  - `.acu` → that ProgID;
  - the thumbnail handler under the ProgID's `ShellEx\{IThumbnailProvider}` →
    our CLSID, and the CLSID's `InprocServer32` → `acu_thumbs.dll`.
  Idempotent; call `ShellReg::EnsureRegistered(icoPath, dllPath)` every launch.

- **`acu_thumbs.dll`** — `thumbprovider/AcuThumbProvider.cpp`: the COM server
  Explorer loads in-proc. Implements `IInitializeWithStream` + `IThumbnailProvider`:
  reads the `.acu` stream, walks its sections to the `THMB` payload, decodes the
  embedded PNG (stb_image) and returns a 32-bit premultiplied `HBITMAP`.

## Flow

1. App startup → `Application::RegisterShellIntegration()` rasterises
   `logo_carto.svg` (resvg, original colours) at 16/32/48/256 into
   `carto_acu.ico` next to the exe, then calls `ShellReg::EnsureRegistered`.
2. The build copies `acu_thumbs.dll` next to the exe; the registry points there.
3. Saving a `.acu` writes a `THMB` section (a PNG of Page 1 by default; see
   `docs/acu-format.md`). Explorer asks `acu_thumbs.dll` for the preview.

The CLSID in `ShellIntegration.cpp` (`kThumbProviderCLSID`) and in
`AcuThumbProvider.cpp` (`CLSID_AcuThumb`) **must stay identical**.

> Per-user registration writes only to `HKCU`; no elevation. `ShellReg::Unregister()`
> removes it. Explorer caches thumbnails — a `SHChangeNotify(SHCNE_ASSOCCHANGED)`
> nudges it after (re)registration.

#pragma once

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Windows shell integration for the .acu project format (per-user, no admin).
//
//  Registers, under HKEY_CURRENT_USER\Software\Classes:
//    • a ProgID "Carto.Project" whose DefaultIcon is `icoPath` (the app logo, in
//      original colours) — shown for .acu in Explorer with no thumbnail / small;
//    • the .acu extension → that ProgID;
//    • the IThumbnailProvider COM handler (`dllPath` = acu_thumbs.dll) under the
//      ProgID's ShellEx, so Explorer shows the page preview at medium+ sizes.
//
//  This module is REGISTRY-ONLY (pure Win32): the caller supplies the already
//  written .ico and the DLL path. Registration is idempotent and refreshes the
//  stored paths when the exe/DLL/ico move. All functions no-op on non-Windows.
// ─────────────────────────────────────────────────────────────────────────────

// NB: the namespace is `ShellReg`, not `Shell` — the Windows SDK (shldisp.h, via
// shlobj.h) declares a global COM type literally named `Shell`, which collides
// with a `namespace Shell`. The folder/include prefix stays "Shell".
namespace ShellReg {

// CLSID of our thumbnail provider, as a registry GUID string ("{...}"). Shared
// by the app (registration) and the DLL (self-identification).
extern const char* kThumbProviderCLSID;

// Register the .acu icon + thumbnail handler for the current user. Safe to call
// every launch (only writes when a value changed). Returns true on success.
bool EnsureRegistered(const std::string& icoPath, const std::string& dllPath);

// Remove all per-user .acu registrations.
void Unregister();

} // namespace ShellReg

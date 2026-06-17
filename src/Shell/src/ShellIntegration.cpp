#include "Shell/ShellIntegration.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>     // SHChangeNotify
#include <string>

namespace ShellReg {

// A fixed, app-unique CLSID for the .acu thumbnail provider. (Generated once;
// must match the GUID the DLL exposes — see acu_thumbs.cpp.)
const char* kThumbProviderCLSID = "{B7A1E4C2-9D3F-4A6B-8E21-7C4F0A9D5E13}";

namespace {

const char* kProgId      = "Carto.Project";
const wchar_t* kExt      = L".acu";
const wchar_t* kIThumbIID = L"{e357fccd-a995-4576-b01f-234630154e96}"; // IThumbnailProvider

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// Create/open a key under HKCU and set its (default or named) string value.
// Returns true if the value was written (or already equal).
bool SetKeyValue(const std::wstring& subkey, const wchar_t* valueName,
                 const std::wstring& data) {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                        &h, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(h, valueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(data.c_str()),
                            (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

void DeleteTree(const std::wstring& subkey) {
    RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
}

} // namespace

bool EnsureRegistered(const std::string& icoPath, const std::string& dllPath) {
    const std::wstring ico   = Widen(icoPath);
    const std::wstring dll   = Widen(dllPath);
    const std::wstring clsid = Widen(kThumbProviderCLSID);
    const std::wstring progId = Widen(kProgId);
    const std::wstring classes = L"Software\\Classes\\";

    bool ok = true;
    // .acu → ProgID.
    ok &= SetKeyValue(classes + kExt, nullptr, progId);
    // ProgID friendly name + DefaultIcon.
    ok &= SetKeyValue(classes + progId, nullptr, L"Carto Project");
    ok &= SetKeyValue(classes + progId + L"\\DefaultIcon", nullptr, ico);

    // Thumbnail provider association on the ProgID:
    //   HKCU\Software\Classes\Carto.Project\ShellEx\{IThumbnailProvider} = CLSID
    ok &= SetKeyValue(classes + progId + L"\\ShellEx\\" + kIThumbIID, nullptr, clsid);
    // Also associate on the extension directly (some shells look there).
    ok &= SetKeyValue(classes + std::wstring(kExt) + L"\\ShellEx\\" + kIThumbIID,
                      nullptr, clsid);

    // The COM server: CLSID → InprocServer32 = our DLL, apartment-threaded.
    const std::wstring clsKey = classes + L"CLSID\\" + clsid;
    ok &= SetKeyValue(clsKey, nullptr, L"Carto ACU Thumbnail Provider");
    ok &= SetKeyValue(clsKey + L"\\InprocServer32", nullptr, dll);
    ok &= SetKeyValue(clsKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    // Nudge the shell to drop cached (empty) thumbnails for .acu.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

void Unregister() {
    const std::wstring classes = L"Software\\Classes\\";
    const std::wstring progId  = Widen(kProgId);
    DeleteTree(classes + progId);
    DeleteTree(classes + L"CLSID\\" + Widen(kThumbProviderCLSID));
    // Leave the ".acu → ProgID" pointer; harmless if the ProgID is gone.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace ShellReg

#else  // non-Windows: no-op
namespace ShellReg {
const char* kThumbProviderCLSID = "";
bool EnsureRegistered(const std::string&, const std::string&) { return false; }
void Unregister() {}
} // namespace ShellReg
#endif

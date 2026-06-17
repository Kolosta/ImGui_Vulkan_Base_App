// ─────────────────────────────────────────────────────────────────────────────
//  acu_thumbs.dll — a Windows IThumbnailProvider for .acu project files.
//
//  Explorer loads this in-proc, hands us the file as an IStream
//  (IInitializeWithStream), and asks for a thumbnail (IThumbnailProvider).
//  We walk the .acu container to find the THMB section, decode its PNG (via
//  stb_image), and return a 32-bit premultiplied HBITMAP.
//
//  .acu container (see docs/acu-format.md):
//    [MAGIC 'ACU1' u32][version u32] then sections [tag u32][len u32][payload].
//    THMB payload = [pngLen u32][PNG bytes][artboard u32][rmin.x,y][rsz.x,y].
//
//  Registered (per-user) by the app via ShellReg::EnsureRegistered. CLSID below
//  MUST match ShellReg::kThumbProviderCLSID.
// ─────────────────────────────────────────────────────────────────────────────

#include <windows.h>
#include <shlwapi.h>
#include <objidl.h>
#include <thumbcache.h>     // IThumbnailProvider, IInitializeWithStream
#include <new>
#include <vector>
#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

// {B7A1E4C2-9D3F-4A6B-8E21-7C4F0A9D5E13}
static const GUID CLSID_AcuThumb =
    { 0xB7A1E4C2, 0x9D3F, 0x4A6B, { 0x8E, 0x21, 0x7C, 0x4F, 0x0A, 0x9D, 0x5E, 0x13 } };

// Define the shell IIDs locally so the DLL doesn't depend on a particular
// MinGW uuid lib exporting them (values per the Windows SDK).
//   IThumbnailProvider  {E357FCCD-A995-4576-B01F-234630154E96}
//   IInitializeWithStream {B824B49D-22AC-4161-AC8A-9916E8FA3F7F}
static const GUID kIID_IThumbnailProvider =
    { 0xE357FCCD, 0xA995, 0x4576, { 0xB0, 0x1F, 0x23, 0x46, 0x30, 0x15, 0x4E, 0x96 } };
static const GUID kIID_IInitializeWithStream =
    { 0xB824B49D, 0x22AC, 0x4161, { 0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F } };

static LONG g_dllRef = 0;
static HINSTANCE g_hInst = nullptr;

// ── Read the whole IStream into a byte buffer ────────────────────────────────
static bool ReadStream(IStream* s, std::vector<uint8_t>& out) {
    if (!s) return false;
    STATSTG st{};
    if (FAILED(s->Stat(&st, STATFLAG_NONAME))) return false;
    ULONGLONG size = st.cbSize.QuadPart;
    if (size == 0 || size > (64ull << 20)) return false;   // sanity cap 64 MB
    out.resize((size_t)size);
    LARGE_INTEGER zero{}; s->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG done = 0; ULONG total = 0;
    while (total < out.size()) {
        if (FAILED(s->Read(out.data() + total, (ULONG)(out.size() - total), &done)) || done == 0)
            break;
        total += done;
    }
    out.resize(total);
    return total > 0;
}

// Little-endian readers.
static uint32_t RdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Find the PNG bytes inside the .acu THMB section. Returns false if absent.
static bool FindThumbPng(const std::vector<uint8_t>& buf, const uint8_t** png, uint32_t* pngLen) {
    const uint32_t MAGIC = 0x31554341;   // 'ACU1'
    const uint32_t TAG_THMB = 0x424D4854; // 'THMB'
    if (buf.size() < 8) return false;
    if (RdU32(buf.data()) != MAGIC) return false;
    size_t pos = 8;  // skip magic + version
    while (pos + 8 <= buf.size()) {
        uint32_t tag = RdU32(buf.data() + pos);
        uint32_t len = RdU32(buf.data() + pos + 4);
        size_t payload = pos + 8;
        if (payload + len > buf.size()) break;
        if (tag == TAG_THMB) {
            if (len < 4) return false;
            uint32_t pl = RdU32(buf.data() + payload);   // pngLen
            if (4 + (size_t)pl > len) return false;
            *png = buf.data() + payload + 4;
            *pngLen = pl;
            return pl > 0;
        }
        pos = payload + len;
    }
    return false;
}

// ── The COM object: IInitializeWithStream + IThumbnailProvider ───────────────
class AcuThumb : public IInitializeWithStream, public IThumbnailProvider {
public:
    AcuThumb() : ref_(1) { InterlockedIncrement(&g_dllRef); }
    ~AcuThumb() { if (stream_) stream_->Release(); InterlockedDecrement(&g_dllRef); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == kIID_IInitializeWithStream)
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (riid == kIID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD) override {
        if (stream_) return E_UNEXPECTED;
        stream_ = stream;
        stream_->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr; *pdwAlpha = WTSAT_ARGB;

        std::vector<uint8_t> buf;
        if (!ReadStream(stream_, buf)) return E_FAIL;
        const uint8_t* png = nullptr; uint32_t pngLen = 0;
        if (!FindThumbPng(buf, &png, &pngLen)) return E_FAIL;

        int w = 0, h = 0, comp = 0;
        unsigned char* rgba = stbi_load_from_memory(png, (int)pngLen, &w, &h, &comp, 4);
        if (!rgba || w <= 0 || h <= 0) { if (rgba) stbi_image_free(rgba); return E_FAIL; }

        // Scale to fit `cx` box, preserving aspect, into a 32-bit DIB section.
        int tw = w, th = h;
        if (w > (int)cx || h > (int)cx) {
            float s = (w >= h) ? (float)cx / w : (float)cx / h;
            tw = (int)(w * s + 0.5f); th = (int)(h * s + 0.5f);
            if (tw < 1) tw = 1; if (th < 1) th = 1;
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = tw; bi.bmiHeader.biHeight = -th;  // top-down
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm) { stbi_image_free(rgba); return E_FAIL; }

        // Nearest-neighbour resample RGBA → BGRA premultiplied (what the shell wants).
        uint8_t* dst = static_cast<uint8_t*>(bits);
        for (int y = 0; y < th; ++y) {
            int sy = (th > 1) ? y * (h - 1) / (th - 1) : 0;
            for (int x = 0; x < tw; ++x) {
                int sx = (tw > 1) ? x * (w - 1) / (tw - 1) : 0;
                const uint8_t* sp = rgba + ((size_t)sy * w + sx) * 4;
                uint8_t a = sp[3];
                uint8_t* dp = dst + ((size_t)y * tw + x) * 4;
                dp[0] = (uint8_t)(sp[2] * a / 255);  // B premultiplied
                dp[1] = (uint8_t)(sp[1] * a / 255);  // G
                dp[2] = (uint8_t)(sp[0] * a / 255);  // R
                dp[3] = a;                           // A
            }
        }
        stbi_image_free(rgba);
        *phbmp = hbm;
        *pdwAlpha = WTSAT_ARGB;
        return S_OK;
    }

private:
    LONG     ref_;
    IStream* stream_ = nullptr;
};

// ── Class factory ────────────────────────────────────────────────────────────
class AcuFactory : public IClassFactory {
public:
    AcuFactory() : ref_(1) { InterlockedIncrement(&g_dllRef); }
    ~AcuFactory() { InterlockedDecrement(&g_dllRef); }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref_); if (r == 0) delete this; return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        AcuThumb* obj = new (std::nothrow) AcuThumb();
        if (!obj) return E_OUTOFMEMORY;
        HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_dllRef); else InterlockedDecrement(&g_dllRef);
        return S_OK;
    }
private:
    LONG ref_;
};

// ── DLL exports ──────────────────────────────────────────────────────────────
extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = hInst; DisableThreadLibraryCalls(hInst); }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_AcuThumb) return CLASS_E_CLASSNOTAVAILABLE;
    AcuFactory* f = new (std::nothrow) AcuFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return g_dllRef == 0 ? S_OK : S_FALSE;
}

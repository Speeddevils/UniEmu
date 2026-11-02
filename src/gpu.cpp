#include "gpu.h"
#include "util.h"
#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#endif

std::vector<GpuInfo> gpuList() {
    static std::vector<GpuInfo> cache;
    static bool done = false;
    if (done) return cache;
    done = true;
#ifdef _WIN32
    HMODULE dx = LoadLibraryW(L"dxgi.dll");
    if (!dx) return cache;
    typedef HRESULT(WINAPI * PFN)(REFIID, void**);
    PFN create = (PFN)GetProcAddress(dx, "CreateDXGIFactory1");
    IDXGIFactory1* f = nullptr;
    if (create && SUCCEEDED(create(__uuidof(IDXGIFactory1), (void**)&f)) && f) {
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                GpuInfo g;
                char name[256];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, nullptr, nullptr);
                g.name = name;
                g.vramMB = d.DedicatedVideoMemory / (1024 * 1024);
                g.vendor = d.VendorId;
                // integrada: pouca memória própria (usa a RAM do sistema) — Intel, AMD APU
                g.integrated = g.vramMB < 1024 || (d.VendorId == 0x8086 && g.vramMB < 4096 &&
                                                  g.name.find("Arc") == std::string::npos);
                bool dup = false;
                for (auto& x : cache) if (x.name == g.name) dup = true;
                if (!dup) cache.push_back(g);
            }
            a->Release();
        }
        f->Release();
    }
#endif
    for (auto& g : cache)
        util::log("GPU: %s (%llu MB, %s)", g.name.c_str(), (unsigned long long)g.vramMB,
                  g.integrated ? "integrada" : "dedicada");
    return cache;
}

const GpuInfo* gpuForPref(int pref) {
    static std::vector<GpuInfo> l;
    l = gpuList();
    for (auto& g : l)
        if ((pref == 1 && g.integrated) || (pref == 2 && !g.integrated)) return &g;
    return nullptr;
}

bool gpuApplyPreference(const std::string& exePath, int pref) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(), -1, &w[0], n);
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r;
    if (pref == 0) r = RegDeleteValueW(k, w.c_str());
    else {
        std::wstring v = pref == 1 ? L"GpuPreference=1;" : L"GpuPreference=2;";
        r = RegSetValueExW(k, w.c_str(), 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS || (pref == 0 && r == ERROR_FILE_NOT_FOUND);
#else
    (void)exePath;
    (void)pref;
    return false;
#endif
}

/**
 * EtherMount Shell Namespace Extension
 * Adds a custom folder under Network that launches the WinSCP-style browser when opened.
 * Display name configurable via CredentialManager (displayName field).
 */

#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <comdef.h>
#include <objbase.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")

#include "EtherMount/CredentialManager.hpp"
#include "EtherMount/SftpClient.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

static HMODULE g_hModule = nullptr;

// {7A8E9F4B-2C1D-4E5A-9B3F-6D7E8C9A0B1C}
static const CLSID CLSID_EtherMountFolder = {
    0x7A8E9F4B, 0x2C1D, 0x4E5A, {0x9B, 0x3F, 0x6D, 0x7E, 0x8C, 0x9A, 0x0B, 0x1C}
};

namespace {

// PIDL structure: cb (2) + is_dir (1) + name (variable, null-terminated)
// DWORD-aligned
#pragma pack(push, 1)
struct EtherMountPidl {
    USHORT cb;
    BYTE is_dir;
    char name[1];
};
#pragma pack(pop)

LPITEMIDLIST CreatePidl(bool isDir, const char* name) {
    size_t nameLen = strlen(name) + 1;
    size_t total = sizeof(USHORT) + 1 + nameLen;
    total = (total + 3) & ~3;  // DWORD align
    LPITEMIDLIST pidl = static_cast<LPITEMIDLIST>(CoTaskMemAlloc(total + sizeof(USHORT)));
    if (!pidl) return nullptr;
    EtherMountPidl* p = reinterpret_cast<EtherMountPidl*>(pidl);
    p->cb = static_cast<USHORT>(total);
    p->is_dir = isDir ? 1 : 0;
    memcpy(p->name, name, nameLen);
    *reinterpret_cast<USHORT*>(reinterpret_cast<BYTE*>(pidl) + total) = 0;  // terminator
    return pidl;
}

const EtherMountPidl* GetPidlData(LPCITEMIDLIST pidl) {
    if (!pidl || !reinterpret_cast<const EtherMountPidl*>(pidl)->cb)
        return nullptr;
    return reinterpret_cast<const EtherMountPidl*>(pidl);
}

std::string GetPidlName(LPCITEMIDLIST pidl) {
    const EtherMountPidl* p = GetPidlData(pidl);
    if (!p) return "";
    return std::string(p->name);
}

bool GetPidlIsDir(LPCITEMIDLIST pidl) {
    const EtherMountPidl* p = GetPidlData(pidl);
    return p && p->is_dir;
}

// Convert Windows path (backslashes) to Unix path
std::string toUnixPath(const std::string& path) {
    std::string r = path;
    for (char& c : r) if (c == '\\') c = '/';
    if (!r.empty() && r[0] != '/') r = "/" + r;
    return r;
}

// Get full remote path from folder path + item name
std::string makeRemotePath(const std::string& folderPath, const std::string& name) {
    std::string base = folderPath;
    if (base.empty() || base == "/") base = "";
    else if (base.size() > 1 && base.back() == '/') base.pop_back();
    if (base.empty()) return "/" + name;
    return base + "/" + name;
}

// Global SFTP context for the extension (Explorer loads us, we connect on demand)
struct ShellSftpContext {
    std::mutex mutex;
    EtherMount::SftpClient client;
    EtherMount::VpsCredentials creds;
    bool connected = false;

    bool ensureConnected() {
        std::lock_guard<std::mutex> lock(mutex);
        if (connected) return true;
        EtherMount::CredentialManager mgr;
        auto loaded = mgr.load();
        if (!loaded) return false;
        creds = *loaded;
        if (client.initialize() != EtherMount::Result::Success) return false;
        if (client.connect(creds.host, creds.port, creds.username, creds.password) != EtherMount::Result::Success)
            return false;
        if (client.initSftp() != EtherMount::Result::Success) {
            client.disconnect();
            return false;
        }
        connected = true;
        return true;
    }

    std::string getBasePath() {
        std::lock_guard<std::mutex> lock(mutex);
        std::string p = creds.remotePath;
        if (p.empty() || p == "/") return "/";
        if (p.back() == '/') p.pop_back();
        return p;
    }
};
ShellSftpContext g_sftp;

}  // namespace

// --- EtherMountEnumIDList ---
class EtherMountEnumIDList : public IEnumIDList {
public:
    EtherMountEnumIDList(const std::string& folderPath, DWORD grfFlags);
    ~EtherMountEnumIDList();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Next(ULONG celt, LPITEMIDLIST* rgelt, ULONG* pceltFetched) override;
    STDMETHODIMP Skip(ULONG celt) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumIDList** ppenum) override;

private:
    ULONG refCount_;
    std::string folderPath_;
    DWORD grfFlags_;
    std::vector<std::pair<std::string, bool>> items_;
    size_t index_;
};

EtherMountEnumIDList::EtherMountEnumIDList(const std::string& folderPath, DWORD grfFlags)
    : refCount_(1), folderPath_(folderPath), grfFlags_(grfFlags), index_(0) {
    if (!g_sftp.ensureConnected()) return;
    std::string remotePath = folderPath.empty() || folderPath == "/" ? g_sftp.getBasePath() : folderPath;
    if (remotePath.empty()) remotePath = "/";
    std::vector<EtherMount::DirEntry> entries;
    {
        std::lock_guard<std::mutex> lock(g_sftp.mutex);
        if (g_sftp.client.listDirectory(remotePath, entries) != EtherMount::Result::Success)
            return;
    }
    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;
        bool wantFolders = (grfFlags & SHCONTF_FOLDERS) != 0;
        bool wantNonFolders = (grfFlags & SHCONTF_NONFOLDERS) != 0;
        if (e.is_directory && !wantFolders) continue;
        if (!e.is_directory && !wantNonFolders) continue;
        items_.emplace_back(e.name, e.is_directory);
    }
}

EtherMountEnumIDList::~EtherMountEnumIDList() = default;

STDMETHODIMP EtherMountEnumIDList::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IEnumIDList) {
        *ppv = static_cast<IEnumIDList*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EtherMountEnumIDList::AddRef() { return ++refCount_; }
STDMETHODIMP_(ULONG) EtherMountEnumIDList::Release() {
    if (--refCount_ == 0) { delete this; return 0; }
    return refCount_;
}

STDMETHODIMP EtherMountEnumIDList::Next(ULONG celt, LPITEMIDLIST* rgelt, ULONG* pceltFetched) {
    if (!rgelt) return E_POINTER;
    ULONG fetched = 0;
    while (fetched < celt && index_ < items_.size()) {
        const auto& p = items_[index_++];
        LPITEMIDLIST pidl = CreatePidl(p.second, p.first.c_str());
        if (!pidl) return E_OUTOFMEMORY;
        rgelt[fetched++] = pidl;
    }
    if (pceltFetched) *pceltFetched = fetched;
    return fetched == celt ? S_OK : S_FALSE;
}

STDMETHODIMP EtherMountEnumIDList::Skip(ULONG celt) {
    index_ = (std::min)(index_ + celt, items_.size());
    return S_OK;
}

STDMETHODIMP EtherMountEnumIDList::Reset() {
    index_ = 0;
    return S_OK;
}

STDMETHODIMP EtherMountEnumIDList::Clone(IEnumIDList** ppenum) {
    if (!ppenum) return E_POINTER;
    *ppenum = new EtherMountEnumIDList(folderPath_, grfFlags_);
    (*ppenum)->AddRef();
    return S_OK;
}

// --- EtherMountFolder ---
class EtherMountFolder : public IShellFolder, public IPersistFolder2 {
public:
    EtherMountFolder();
    explicit EtherMountFolder(const std::string& path);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID) override;

    // IPersistFolder
    STDMETHODIMP Initialize(LPCITEMIDLIST pidl) override;

    // IPersistFolder2
    STDMETHODIMP GetCurFolder(LPITEMIDLIST* ppidl) override;

    // IShellFolder
    STDMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx* pbc, LPOLESTR pszDisplayName,
        ULONG* pchEaten, LPITEMIDLIST* ppidl, ULONG* pdwAttributes) override;
    STDMETHODIMP EnumObjects(HWND hwnd, DWORD grfFlags, IEnumIDList** ppenumIDList) override;
    STDMETHODIMP BindToObject(LPCITEMIDLIST pidl, IBindCtx* pbc, REFIID riid, void** ppv) override;
    STDMETHODIMP BindToStorage(LPCITEMIDLIST pidl, IBindCtx* pbc, REFIID riid, void** ppv) override;
    STDMETHODIMP CompareIDs(LPARAM lParam, LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2) override;
    STDMETHODIMP CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) override;
    STDMETHODIMP GetAttributesOf(UINT cidl, LPCITEMIDLIST* apidl, SFGAOF* rgfInOut) override;
    STDMETHODIMP GetUIObjectOf(HWND hwndOwner, UINT cidl, LPCITEMIDLIST* apidl,
        REFIID riid, UINT* prgfInOut, void** ppv) override;
    STDMETHODIMP GetDisplayNameOf(LPCITEMIDLIST pidl, DWORD uFlags, STRRET* pName) override;
    STDMETHODIMP SetNameOf(HWND hwnd, LPCITEMIDLIST pidl, LPCOLESTR pszName,
        DWORD uFlags, LPITEMIDLIST* ppidlOut) override;

private:
    ULONG refCount_;
    LPITEMIDLIST pidlRoot_;
    std::string path_;  // Full remote path for this folder
};

EtherMountFolder::EtherMountFolder() : refCount_(1), pidlRoot_(nullptr), path_("") {}

EtherMountFolder::EtherMountFolder(const std::string& path) : refCount_(1), pidlRoot_(nullptr), path_(path) {}

STDMETHODIMP EtherMountFolder::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IShellFolder) {
        *ppv = static_cast<IShellFolder*>(this);
    } else if (riid == IID_IPersistFolder || riid == IID_IPersistFolder2) {
        *ppv = static_cast<IPersistFolder2*>(this);
    } else if (riid == IID_IPersist) {
        *ppv = static_cast<IPersist*>(this);
    }
    if (*ppv) { AddRef(); return S_OK; }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EtherMountFolder::AddRef() { return ++refCount_; }
STDMETHODIMP_(ULONG) EtherMountFolder::Release() {
    if (--refCount_ == 0) {
        if (pidlRoot_) CoTaskMemFree(pidlRoot_);
        delete this;
        return 0;
    }
    return refCount_;
}

STDMETHODIMP EtherMountFolder::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_EtherMountFolder;
    return S_OK;
}

STDMETHODIMP EtherMountFolder::Initialize(LPCITEMIDLIST pidl) {
    if (pidlRoot_) { CoTaskMemFree(pidlRoot_); pidlRoot_ = nullptr; }
    if (pidl) {
        size_t len = ILGetSize(pidl);
        pidlRoot_ = static_cast<LPITEMIDLIST>(CoTaskMemAlloc(len));
        if (pidlRoot_) memcpy(pidlRoot_, pidl, len);
    }
    return S_OK;
}

STDMETHODIMP EtherMountFolder::GetCurFolder(LPITEMIDLIST* ppidl) {
    if (!ppidl) return E_POINTER;
    *ppidl = pidlRoot_ ? ILClone(pidlRoot_) : nullptr;
    return S_OK;
}

STDMETHODIMP EtherMountFolder::ParseDisplayName(HWND, IBindCtx*, LPOLESTR, ULONG*, LPITEMIDLIST*, ULONG*) {
    return E_NOTIMPL;
}

STDMETHODIMP EtherMountFolder::EnumObjects(HWND, DWORD grfFlags, IEnumIDList** ppenumIDList) {
    if (!ppenumIDList) return E_POINTER;
    *ppenumIDList = new EtherMountEnumIDList(path_, grfFlags);
    (*ppenumIDList)->AddRef();
    return S_OK;
}

STDMETHODIMP EtherMountFolder::BindToObject(LPCITEMIDLIST pidl, IBindCtx*, REFIID riid, void** ppv) {
    if (!pidl || !ppv) return E_POINTER;
    *ppv = nullptr;
    if (!GetPidlIsDir(pidl)) return E_INVALIDARG;
    std::string name = GetPidlName(pidl);
    std::string basePath = path_.empty() ? g_sftp.getBasePath() : path_;
    if (basePath.empty() || basePath == "/") basePath = "";
    std::string childPath = basePath.empty() ? ("/" + name) : (basePath + "/" + name);
    EtherMountFolder* child = new EtherMountFolder(childPath);
    HRESULT hr = child->QueryInterface(riid, ppv);
    child->Release();
    return hr;
}

STDMETHODIMP EtherMountFolder::BindToStorage(LPCITEMIDLIST, IBindCtx*, REFIID, void** ppv) {
    if (ppv) *ppv = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP EtherMountFolder::CompareIDs(LPARAM lParam, LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2) {
    std::string n1 = GetPidlName(pidl1);
    std::string n2 = GetPidlName(pidl2);
    int cmp = _stricmp(n1.c_str(), n2.c_str());
    if (cmp < 0) return S_OK;
    if (cmp > 0) return 1;
    return S_OK;
}

STDMETHODIMP EtherMountFolder::CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IShellView) {
        // Launch EtherMount.exe --browser when user opens this folder (Option A)
        if (g_hModule) {
            wchar_t dllPath[MAX_PATH];
            if (GetModuleFileNameW(g_hModule, dllPath, MAX_PATH) > 0) {
                std::wstring path(dllPath);
                size_t slash = path.find_last_of(L"\\/");
                if (slash != std::wstring::npos) {
                    std::wstring exePath = path.substr(0, slash + 1) + L"EtherMount.exe";
                    ShellExecuteW(nullptr, L"open", exePath.c_str(), L"--browser",
                        nullptr, SW_SHOWNORMAL);
                }
            }
        }
        SFV_CREATE csfv = {};
        csfv.cbSize = sizeof(SFV_CREATE);
        csfv.pshf = this;
        return SHCreateShellFolderView(&csfv, reinterpret_cast<IShellView**>(ppv));
    }
    return E_NOINTERFACE;
}

STDMETHODIMP EtherMountFolder::GetAttributesOf(UINT cidl, LPCITEMIDLIST* apidl, SFGAOF* rgfInOut) {
    if (!rgfInOut) return E_POINTER;
    SFGAOF mask = *rgfInOut;
    *rgfInOut = 0;
    for (UINT i = 0; i < cidl; i++) {
        if (apidl && apidl[i]) {
            if (GetPidlIsDir(apidl[i])) {
                *rgfInOut |= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE;
            } else {
                *rgfInOut |= SFGAO_FILESYSTEM;
            }
        }
    }
    *rgfInOut &= mask;
    return S_OK;
}

STDMETHODIMP EtherMountFolder::GetUIObjectOf(HWND, UINT, LPCITEMIDLIST*, REFIID, UINT*, void** ppv) {
    if (ppv) *ppv = nullptr;
    return E_NOTIMPL;
}

static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    result.resize(len - 1);
    return result;
}

STDMETHODIMP EtherMountFolder::GetDisplayNameOf(LPCITEMIDLIST pidl, DWORD uFlags, STRRET* pName) {
    if (!pidl || !pName) return E_POINTER;
    std::string name = GetPidlName(pidl);
    if (name.empty()) return E_INVALIDARG;
    std::wstring wname = utf8ToWide(name);
    if (uFlags & SHGDN_FORPARSING) {
        pName->uType = STRRET_WSTR;
        pName->pOleStr = static_cast<LPWSTR>(CoTaskMemAlloc((wname.size() + 1) * sizeof(WCHAR)));
        if (pName->pOleStr) {
            wcscpy_s(pName->pOleStr, wname.size() + 1, wname.c_str());
            return S_OK;
        }
        return E_OUTOFMEMORY;
    }
    pName->uType = STRRET_WSTR;
    pName->pOleStr = static_cast<LPWSTR>(CoTaskMemAlloc((wname.size() + 1) * sizeof(WCHAR)));
    if (pName->pOleStr) {
        wcscpy_s(pName->pOleStr, wname.size() + 1, wname.c_str());
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

STDMETHODIMP EtherMountFolder::SetNameOf(HWND, LPCITEMIDLIST, LPCOLESTR, DWORD, LPITEMIDLIST*) {
    return E_ACCESSDENIED;  // Read-only
}

// --- ClassFactory ---
class EtherMountClassFactory : public IClassFactory {
public:
    EtherMountClassFactory() : refCount_(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    STDMETHODIMP_(ULONG) Release() override {
        if (--refCount_ == 0) { delete this; return 0; }
        return refCount_;
    }
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        EtherMountFolder* folder = new EtherMountFolder();
        HRESULT hr = folder->QueryInterface(riid, ppv);
        folder->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }

private:
    ULONG refCount_;
};

extern "C" {

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void) {
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_EtherMountFolder)) return CLASS_E_CLASSNOTAVAILABLE;
    EtherMountClassFactory* cf = new EtherMountClassFactory();
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

STDAPI DllRegisterServer(void) {
    wchar_t dllPath[MAX_PATH];
    if (!g_hModule || GetModuleFileNameW(g_hModule, dllPath, MAX_PATH) == 0)
        return E_FAIL;

    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_EtherMountFolder, clsidStr, 64);

    // Register CLSID with InProcServer32
    std::wstring clsidKey = std::wstring(L"CLSID\\") + clsidStr;
    HKEY hClsid;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hClsid, nullptr) != ERROR_SUCCESS)
        return E_FAIL;
    RegSetValueExW(hClsid, nullptr, 0, REG_SZ, (BYTE*)L"EtherMount VPS", 30);
    HKEY hInproc;
    if (RegCreateKeyExW(hClsid, L"InProcServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hInproc, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hInproc, nullptr, 0, REG_SZ, (BYTE*)dllPath, static_cast<DWORD>((wcslen(dllPath) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hInproc, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment", 20);
        RegCloseKey(hInproc);
    }
    RegCloseKey(hClsid);

    // Add to Network (NetworkNeighborhood)
    std::wstring nsKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NetworkNeighborhood\\NameSpace\\" + std::wstring(clsidStr);
    HKEY hNs;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, nsKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hNs, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hNs, nullptr, 0, REG_SZ, (BYTE*)L"EtherMount VPS", 30);
        RegCloseKey(hNs);
    }

    return S_OK;
}

STDAPI DllUnregisterServer(void) {
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_EtherMountFolder, clsidStr, 64);

    RegDeleteKeyW(HKEY_CURRENT_USER,
        (L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NetworkNeighborhood\\NameSpace\\" + std::wstring(clsidStr)).c_str());
    RegDeleteKeyW(HKEY_CLASSES_ROOT, (L"CLSID\\" + std::wstring(clsidStr) + L"\\InProcServer32").c_str());
    RegDeleteKeyW(HKEY_CLASSES_ROOT, (L"CLSID\\" + std::wstring(clsidStr)).c_str());

    return S_OK;
}

}  // extern "C"

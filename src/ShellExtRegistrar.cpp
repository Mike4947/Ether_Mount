#include "EtherMount/ShellExtRegistrar.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "Shell32.lib")
#endif

#include <string>

namespace EtherMount {

namespace {

constexpr wchar_t CLSID_STR[] = L"{7A8E9F4B-2C1D-4E5A-9B3F-6D7E8C9A0B1C}";

std::wstring getExeDir() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return L"";
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.resize(slash + 1);
    return p;
#else
    return L"";
#endif
}

std::wstring utf8ToWide(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    result.resize(len - 1);
    return result;
#else
    return L"";
#endif
}

}  // namespace

bool ShellExtRegistrar::registerShellExt(const std::string& displayName) {
#ifdef _WIN32
    std::wstring exeDir = getExeDir();
    if (exeDir.empty()) return false;

    std::wstring dllPath = exeDir + L"EtherMountShellExt.dll";
    std::wstring displayW = displayName.empty() ? L"EtherMount VPS" : utf8ToWide(displayName);

    // Use HKCU\Software\Classes so non-admin users can register
    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_STR;
    HKEY hClsid;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, clsidKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hClsid, nullptr) != ERROR_SUCCESS)
        return false;

    RegSetValueExW(hClsid, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(displayW.c_str()),
        static_cast<DWORD>((displayW.size() + 1) * sizeof(wchar_t)));

    HKEY hInproc;
    if (RegCreateKeyExW(hClsid, L"InProcServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hInproc, nullptr) != ERROR_SUCCESS) {
        RegCloseKey(hClsid);
        return false;
    }
    RegSetValueExW(hInproc, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dllPath.c_str()),
        static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(hInproc, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(L"Apartment"), 20);
    RegCloseKey(hInproc);
    RegCloseKey(hClsid);

    std::wstring nsKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NetworkNeighborhood\\NameSpace\\" + std::wstring(CLSID_STR);
    HKEY hNs;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, nsKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hNs, nullptr) != ERROR_SUCCESS)
        return false;
    RegSetValueExW(hNs, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(displayW.c_str()),
        static_cast<DWORD>((displayW.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hNs);

    return true;
#else
    (void)displayName;
    return false;
#endif
}

bool ShellExtRegistrar::unregisterShellExt() {
#ifdef _WIN32
    RegDeleteKeyW(HKEY_CURRENT_USER,
        (L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NetworkNeighborhood\\NameSpace\\" + std::wstring(CLSID_STR)).c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_STR + L"\\InProcServer32").c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_STR).c_str());
    return true;
#else
    return false;
#endif
}

bool ShellExtRegistrar::updateDisplayName(const std::string& displayName) {
#ifdef _WIN32
    std::wstring displayW = displayName.empty() ? L"EtherMount VPS" : utf8ToWide(displayName);

    std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + CLSID_STR;
    HKEY hClsid;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, clsidKey.c_str(), 0, KEY_WRITE, &hClsid) != ERROR_SUCCESS)
        return false;
    RegSetValueExW(hClsid, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(displayW.c_str()),
        static_cast<DWORD>((displayW.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hClsid);

    std::wstring nsKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\NetworkNeighborhood\\NameSpace\\" + std::wstring(CLSID_STR);
    HKEY hNs;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, nsKey.c_str(), 0, KEY_WRITE, &hNs) == ERROR_SUCCESS) {
        RegSetValueExW(hNs, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(displayW.c_str()),
            static_cast<DWORD>((displayW.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hNs);
    }

    return true;
#else
    (void)displayName;
    return false;
#endif
}

}  // namespace EtherMount

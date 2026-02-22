/**
 * EtherMountFS - WinFSP + SFTP bridge (v0.03 Read-Only).
 * Maps WinFSP callbacks to SftpClient. Strictly read-only.
 * v0.03.1: Root path handling, GetDirInfoByName, explicit Security Descriptor, debug logging.
 */

#include "EtherMount/EtherMountFS.hpp"
#include "EtherMount/SftpClient.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")
#endif
#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
#include <winfsp/winfsp.h>
#include <aclapi.h>
#include <sddl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string getLogPath() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return "";
    return std::string(path) + "\\EtherMount\\ethermount_fs.log";
#else
    return "";
#endif
}
bool checkDokanBeforeWinFsp() {
#ifdef _WIN32
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order",
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    char buf[512] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    bool conflict = false;
    if (RegQueryValueExA(hKey, "ProviderOrder", nullptr, &type,
            reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS && buf[0]) {
        std::string order(buf);
        size_t winfsp = order.find("WinFsp");
        size_t dokan = std::string::npos;
        for (const char* name : {"Dokan", "Dokan2", "dokany"}) {
            size_t p = order.find(name);
            if (p != std::string::npos && (dokan == std::string::npos || p < dokan))
                dokan = p;
        }
        if (winfsp != std::string::npos && dokan != std::string::npos && dokan < winfsp)
            conflict = true;
    }
    RegCloseKey(hKey);
    return conflict;
#else
    return false;
#endif
}

void emfsLog(const std::string& msg) {
#ifdef _WIN32
    std::string p = getLogPath();
    if (p.empty()) return;
    size_t slash = p.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::string dir = p.substr(0, slash);
        CreateDirectoryA(dir.c_str(), nullptr);
    }
    std::ofstream f(p, std::ios::app);
    if (f) {
        f << "[EtherMountFS] " << msg << "\n";
        f.flush();
    }
#endif
}
}
#define EMFS_LOG(msg) do { std::ostringstream _oss; _oss << msg; emfsLog(_oss.str()); } while(0)

namespace EtherMount {

#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
namespace {

// --- POSIX to Windows timestamp conversion ---
// Windows FILETIME: 100-nanosecond intervals since 1601-01-01 00:00:00 UTC.
// POSIX: seconds since 1970-01-01 00:00:00 UTC.
// Conversion: WinTime = (UnixTime * 10000000) + 116444736000000000ULL
constexpr UINT64 WINDOWS_TICK_OFFSET = 116444736000000000ULL;

static inline UINT64 posixToWindowsTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return 0;
    return static_cast<UINT64>(unixSeconds) * 10000000ULL + WINDOWS_TICK_OFFSET;
}

/// Convert WinFSP path (backslashes) to Unix relative path.
/// Root: "\", "" -> "". "\path\to\file" -> "/path/to/file"
static std::string windowsPathToUnix(PWSTR FileName) {
    if (!FileName) return "";
    std::string result;
    for (const wchar_t* p = FileName; *p; ++p) {
        if (*p == L'\\')
            result += '/';
        else if (*p < 128)
            result += static_cast<char>(*p);
    }
    if (result.empty() || result[0] != '/')
        result.insert(0, "/");
    while (result.size() > 1 && result[0] == '/' && result[1] == '/')
        result.erase(0, 1);
    if (result == "/") return "";
    return result;
}

/// Convert Windows path to full SFTP path using base path from credentials.
/// Z:\ -> basePath (e.g. /var/www/flowdesk), Z:\subdir -> basePath/subdir
static std::string toRemotePath(PWSTR FileName, const std::string& basePath) {
    std::string rel = windowsPathToUnix(FileName);
    if (basePath.empty() || basePath == "/") {
        return rel.empty() ? "/" : rel;
    }
    std::string base = basePath;
    if (base.back() == '/') base.pop_back();
    if (rel.empty()) return base;
    return base + (rel[0] == '/' ? rel : "/" + rel);
}

/// Fill FSP_FSCTL_FILE_INFO from SFTP FileInfo.
/// Maps: is_directory -> FILE_ATTRIBUTE_DIRECTORY, size, POSIX times -> Windows times.
static void fillFileInfo(const FileInfo& info, FSP_FSCTL_FILE_INFO* FileInfo) {
    FileInfo->FileAttributes = info.is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    FileInfo->ReparseTag = 0;
    FileInfo->AllocationSize = info.is_directory ? 0 : ((info.size + 4095) & ~4095ULL);
    FileInfo->FileSize = info.size;
    FileInfo->CreationTime = posixToWindowsTime(info.mtime);
    FileInfo->LastAccessTime = posixToWindowsTime(info.atime);
    FileInfo->LastWriteTime = posixToWindowsTime(info.mtime);
    FileInfo->ChangeTime = posixToWindowsTime(info.mtime);
    FileInfo->IndexNumber = 0;
    FileInfo->HardLinks = 0;
    FileInfo->EaSize = 0;
}

/// Synthetic root directory info when SFTP stat("/") fails (some servers).
static void fillRootFileInfo(FSP_FSCTL_FILE_INFO* FileInfo) {
    FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    FileInfo->ReparseTag = 0;
    FileInfo->AllocationSize = 0;
    FileInfo->FileSize = 0;
    FileInfo->CreationTime = 0;
    FileInfo->LastAccessTime = 0;
    FileInfo->LastWriteTime = 0;
    FileInfo->ChangeTime = 0;
    FileInfo->IndexNumber = 0;
    FileInfo->HardLinks = 0;
    FileInfo->EaSize = 0;
}

struct OpenContext {
    std::unique_ptr<SftpHandle> handle;
    std::string remotePath;
};

struct MountContext {
    SftpClient sftp;
    VpsCredentials creds;
    bool connected = false;
};
static MountContext* g_ctx = nullptr;
static std::mutex g_ctx_mutex;

static MountContext* getContext() {
    std::lock_guard<std::mutex> lock(g_ctx_mutex);
    return g_ctx;
}

// Forward declarations
static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* FileSystem,
                              FSP_FSCTL_VOLUME_INFO* VolumeInfo);
static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                                  PUINT32 PFileAttributes,
                                  PSECURITY_DESCRIPTOR SecurityDescriptor,
                                  SIZE_T* PSecurityDescriptorSize);
static NTSTATUS Create(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                      UINT32 CreateOptions, UINT32 GrantedAccess,
                      UINT32 FileAttributes,
                      PSECURITY_DESCRIPTOR SecurityDescriptor,
                      UINT64 AllocationSize, PVOID* PFileContext,
                      FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Open(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName,
                    UINT32 CreateOptions, UINT32 GrantedAccess,
                    PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo);
static VOID Cleanup(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                   PWSTR FileName, ULONG Flags);
static VOID Close(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext);
static NTSTATUS Read(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                    PVOID Buffer, UINT64 Offset, ULONG Length,
                    PULONG PBytesTransferred);
static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                              PWSTR Pattern, PWSTR Marker, PVOID Buffer,
                              ULONG Length, PULONG PBytesTransferred);
static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                           FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Write(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                     PVOID Buffer, UINT64 Offset, ULONG Length,
                     BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
                     PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                            UINT32 FileAttributes, UINT64 CreationTime,
                            UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime,
                            FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                           UINT64 NewSize, BOOLEAN SetAllocationSize,
                           FSP_FSCTL_FILE_INFO* FileInfo);
static NTSTATUS Rename(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                      PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
static NTSTATUS GetDirInfoByName(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                                PWSTR FileName, FSP_FSCTL_DIR_INFO* DirInfo);
static NTSTATUS CanDelete(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName);
static NTSTATUS Flush(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext,
                     FSP_FSCTL_FILE_INFO* FileInfo);

static const FSP_FILE_SYSTEM_INTERFACE* getInterface() {
    static FSP_FILE_SYSTEM_INTERFACE iface = {};
    static bool initialized = false;
    if (!initialized) {
        iface.GetVolumeInfo = GetVolumeInfo;
        iface.GetSecurityByName = GetSecurityByName;
        iface.Create = Create;
        iface.Open = Open;
        iface.Cleanup = Cleanup;
        iface.Close = Close;
        iface.Read = Read;
        iface.Write = Write;
        iface.ReadDirectory = ReadDirectory;
        iface.GetFileInfo = GetFileInfo;
        iface.SetBasicInfo = SetBasicInfo;
        iface.SetFileSize = SetFileSize;
        iface.Rename = Rename;
        iface.GetDirInfoByName = GetDirInfoByName;
        iface.CanDelete = CanDelete;
        iface.Flush = Flush;
        initialized = true;
    }
    return &iface;
}

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* /*FileSystem*/,
                              FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
    EMFS_LOG("Called GetVolumeInfo");
    VolumeInfo->TotalSize = 1024ULL * 1024 * 1024 * 1024;  // 1 TB placeholder
    VolumeInfo->FreeSize = 1024ULL * 1024 * 1024 * 1024;
    const wchar_t* label = L"EtherMount VPS";
    VolumeInfo->VolumeLabelLength = static_cast<UINT16>(
        (wcslen(label) + 1) * sizeof(WCHAR));
    wcscpy_s(VolumeInfo->VolumeLabel, 32, label);
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* /*FileSystem*/,
                                  PWSTR FileName, PUINT32 PFileAttributes,
                                  PSECURITY_DESCRIPTOR SecurityDescriptor,
                                  SIZE_T* PSecurityDescriptorSize) {
    EMFS_LOG("Called GetSecurityByName");

    MountContext* ctx = getContext();
    if (!ctx || !ctx->connected) {
        EMFS_LOG("  -> FAIL: no context or not connected");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    std::string path = toRemotePath(FileName, ctx->creds.remotePath);
    EMFS_LOG("  path='" << path << "'");
    auto info = ctx->sftp.getFileInfo(path);
    if (!info) {
        std::string base = ctx->creds.remotePath;
        if (base.empty() || base == "/") base = "/";
        else if (base.size() > 1 && base.back() == '/') base.pop_back();
        if (path == base || path == "/" || path.empty()) {
            EMFS_LOG("  -> Root path, using synthetic directory (SFTP stat failed)");
            if (PFileAttributes) *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        } else {
            EMFS_LOG("  -> FAIL: path not found");
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    } else {
        if (PFileAttributes) {
            *PFileAttributes = info->is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        }
    }

    if (SecurityDescriptor && PSecurityDescriptorSize) {
        static BYTE sdSelfRelative[512];
        static SIZE_T sdLen = 0;
        if (sdLen == 0) {
            PSECURITY_DESCRIPTOR pSD = nullptr;
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &pSD, nullptr)) {
                DWORD len = sizeof(sdSelfRelative);
                if (MakeSelfRelativeSD(pSD, sdSelfRelative, &len)) {
                    sdLen = len;
                }
                LocalFree(pSD);
            }
        }
        if (sdLen > 0 && *PSecurityDescriptorSize < sdLen) {
            *PSecurityDescriptorSize = sdLen;
            return STATUS_BUFFER_OVERFLOW;
        }
        if (sdLen > 0) {
            memcpy(SecurityDescriptor, sdSelfRelative, sdLen);
            *PSecurityDescriptorSize = sdLen;
        }
    }

    EMFS_LOG("  -> SUCCESS");
    return STATUS_SUCCESS;
}

static NTSTATUS Create(FSP_FILE_SYSTEM* /*FileSystem*/, PWSTR /*FileName*/,
                      UINT32 /*CreateOptions*/, UINT32 /*GrantedAccess*/,
                      UINT32 /*FileAttributes*/,
                      PSECURITY_DESCRIPTOR /*SecurityDescriptor*/,
                      UINT64 /*AllocationSize*/, PVOID* /*PFileContext*/,
                      FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS Open(FSP_FILE_SYSTEM* /*FileSystem*/, PWSTR FileName,
                    UINT32 /*CreateOptions*/, UINT32 /*GrantedAccess*/,
                    PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
    EMFS_LOG("Called Open");
    MountContext* ctx = getContext();
    if (!ctx || !ctx->connected) {
        EMFS_LOG("  -> FAIL: no context or not connected");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    std::string path = toRemotePath(FileName, ctx->creds.remotePath);
    EMFS_LOG("  path='" << path << "'");
    auto infoOpt = ctx->sftp.getFileInfo(path);
    EtherMount::FileInfo sftpInfo;
    if (!infoOpt) {
        std::string base = ctx->creds.remotePath;
        if (base.empty() || base == "/") base = "/";
        else if (base.size() > 1 && base.back() == '/') base.pop_back();
        if (path == base || path == "/" || path.empty()) {
            EMFS_LOG("  -> Root path, treating as directory (SFTP stat failed)");
            sftpInfo.is_directory = true;
            sftpInfo.size = 0;
            sftpInfo.mtime = 0;
            sftpInfo.atime = 0;
        } else {
            EMFS_LOG("  -> FAIL: path not found");
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    } else {
        sftpInfo = *infoOpt;
    }

    auto openCtx = std::make_unique<OpenContext>();
    if (sftpInfo.is_directory) {
        openCtx->handle = ctx->sftp.openDirectory(path);
    } else {
        openCtx->handle = ctx->sftp.openFile(path);
    }
    if (!openCtx->handle) {
        EMFS_LOG("  -> FAIL: openDirectory/openFile failed");
        return STATUS_ACCESS_DENIED;
    }

    openCtx->remotePath = path;
    fillFileInfo(sftpInfo, FileInfo);
    *PFileContext = openCtx.release();
    EMFS_LOG("  -> SUCCESS");
    return STATUS_SUCCESS;
}

static VOID Cleanup(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                   PWSTR /*FileName*/, ULONG Flags) {
    EMFS_LOG("Called Cleanup");
    if (Flags & FspCleanupDelete) {
        return;  // Read-only: delete is a no-op
    }
}

static VOID Close(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext) {
    EMFS_LOG("Called Close");
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx) return;

    MountContext* mctx = getContext();
    if (mctx && ctx->handle) {
        mctx->sftp.closeHandle(std::move(ctx->handle));
    }
    delete ctx;
}

static NTSTATUS Read(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                    PVOID Buffer, UINT64 Offset, ULONG Length,
                    PULONG PBytesTransferred) {
    EMFS_LOG("Called Read");
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx || !ctx->handle || ctx->handle->is_directory) {
        *PBytesTransferred = 0;
        return STATUS_INVALID_PARAMETER;
    }

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) {
        *PBytesTransferred = 0;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    int64_t n = mctx->sftp.readFile(ctx->handle.get(), Buffer, Offset, Length);
    if (n < 0) {
        *PBytesTransferred = 0;
        return STATUS_ACCESS_DENIED;
    }
    *PBytesTransferred = static_cast<ULONG>(n);
    return STATUS_SUCCESS;
}

static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                              PWSTR /*Pattern*/, PWSTR /*Marker*/,
                              PVOID Buffer, ULONG Length,
                              PULONG PBytesTransferred) {
    EMFS_LOG("Called ReadDirectory");
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx || !ctx->handle || !ctx->handle->is_directory) {
        *PBytesTransferred = 0;
        return STATUS_INVALID_PARAMETER;
    }

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) {
        *PBytesTransferred = 0;
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    std::vector<DirEntry> entries;
    if (mctx->sftp.listDirectory(ctx->remotePath, entries) != Result::Success) {
        *PBytesTransferred = 0;
        return STATUS_ACCESS_DENIED;
    }

    PVOID buffer = Buffer;
    ULONG remaining = Length;
    ULONG total = 0;

    auto addEntry = [&](const wchar_t* name, bool isDir, UINT64 size, UINT64 mtime) {
        FSP_FSCTL_FILE_INFO fi = {};
        fi.FileAttributes = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        fi.ReparseTag = 0;
        fi.AllocationSize = isDir ? 0 : ((size + 4095) & ~4095ULL);
        fi.FileSize = size;
        fi.CreationTime = posixToWindowsTime(static_cast<int64_t>(mtime));
        fi.LastAccessTime = fi.CreationTime;
        fi.LastWriteTime = fi.CreationTime;
        fi.ChangeTime = fi.CreationTime;
        fi.IndexNumber = 0;
        fi.HardLinks = 0;
        fi.EaSize = 0;

        SIZE_T nameLen = (wcslen(name) + 1) * sizeof(WCHAR);
        SIZE_T dirInfoSize = FSP_FSCTL_DEFAULT_ALIGN_UP(
            sizeof(FSP_FSCTL_DIR_INFO) + nameLen);
        if (remaining < dirInfoSize) return false;

        FSP_FSCTL_DIR_INFO* di = static_cast<FSP_FSCTL_DIR_INFO*>(buffer);
        di->Size = static_cast<UINT16>(dirInfoSize);
        di->FileInfo = fi;
        di->NextOffset = 0;
        memcpy(di->FileNameBuf, name, nameLen);

        buffer = static_cast<PUINT8>(buffer) + dirInfoSize;
        remaining -= static_cast<ULONG>(dirInfoSize);
        total += static_cast<ULONG>(dirInfoSize);
        return true;
    };

    if (!addEntry(L".", true, 0, 0)) { *PBytesTransferred = total; return STATUS_SUCCESS; }
    if (!addEntry(L"..", true, 0, 0)) { *PBytesTransferred = total; return STATUS_SUCCESS; }

    for (const auto& e : entries) {
        std::wstring wname;
        for (char c : e.name) {
            wname += static_cast<wchar_t>(static_cast<unsigned char>(c));
        }
        UINT64 mtime = e.mtime.value_or(0);
        UINT64 size = e.size.value_or(0);
        if (!addEntry(wname.c_str(), e.is_directory, size, mtime)) break;
    }

    *PBytesTransferred = total;
    return STATUS_SUCCESS;
}

static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                           FSP_FSCTL_FILE_INFO* FileInfo) {
    EMFS_LOG("Called GetFileInfo");
    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    if (!ctx) return STATUS_INVALID_PARAMETER;

    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) return STATUS_OBJECT_NAME_NOT_FOUND;

    auto info = mctx->sftp.getFileInfo(ctx->remotePath);
    if (!info) {
        if (ctx->remotePath == "/" || ctx->remotePath.empty()) {
            fillRootFileInfo(FileInfo);
            return STATUS_SUCCESS;
        }
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    fillFileInfo(*info, FileInfo);
    return STATUS_SUCCESS;
}

static NTSTATUS Write(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                     PVOID /*Buffer*/, UINT64 /*Offset*/, ULONG /*Length*/,
                     BOOLEAN /*WriteToEndOfFile*/, BOOLEAN /*ConstrainedIo*/,
                     PULONG /*PBytesTransferred*/, FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                            UINT32 /*FileAttributes*/, UINT64 /*CreationTime*/,
                            UINT64 /*LastAccessTime*/, UINT64 /*LastWriteTime*/,
                            UINT64 /*ChangeTime*/, FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                           UINT64 /*NewSize*/, BOOLEAN /*SetAllocationSize*/,
                           FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS Rename(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                      PWSTR /*FileName*/, PWSTR /*NewFileName*/, BOOLEAN /*ReplaceIfExists*/) {
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS CanDelete(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                         PWSTR /*FileName*/) {
    EMFS_LOG("Called CanDelete");
    return STATUS_MEDIA_WRITE_PROTECTED;
}

static NTSTATUS Flush(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID /*FileContext*/,
                     FSP_FSCTL_FILE_INFO* /*FileInfo*/) {
    EMFS_LOG("Called Flush");
    return STATUS_SUCCESS;
}

static NTSTATUS GetDirInfoByName(FSP_FILE_SYSTEM* /*FileSystem*/, PVOID FileContext,
                                PWSTR FileName, FSP_FSCTL_DIR_INFO* DirInfo) {
    EMFS_LOG("Called GetDirInfoByName");
    if (!FileContext || !FileName || !DirInfo) {
        EMFS_LOG("  -> FAIL: null parameter");
        return STATUS_INVALID_PARAMETER;
    }

    OpenContext* ctx = static_cast<OpenContext*>(FileContext);
    MountContext* mctx = getContext();
    if (!mctx || !mctx->connected) {
        EMFS_LOG("  -> FAIL: no context or not connected");
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    std::wstring wname(FileName);
    std::string name;
    for (wchar_t c : wname) name += (c < 128 ? static_cast<char>(c) : '?');

    if (name == "." || name == "..") {
        fillRootFileInfo(&DirInfo->FileInfo);
        DirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        DirInfo->NextOffset = 0;
        SIZE_T nameLen = (wname.size() + 1) * sizeof(WCHAR);
        memcpy(DirInfo->FileNameBuf, FileName, nameLen);
        DirInfo->Size = static_cast<UINT16>(FSP_FSCTL_DEFAULT_ALIGN_UP(
            sizeof(FSP_FSCTL_DIR_INFO) + nameLen));
        EMFS_LOG("  -> SUCCESS (. or ..)");
        return STATUS_SUCCESS;
    }

    std::vector<DirEntry> entries;
    if (mctx->sftp.listDirectory(ctx->remotePath, entries) != Result::Success) {
        EMFS_LOG("  -> FAIL: listDirectory failed");
        return STATUS_ACCESS_DENIED;
    }

    for (const auto& e : entries) {
        if (e.name == name) {
            FSP_FSCTL_FILE_INFO fi = {};
            fi.FileAttributes = e.is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            fi.ReparseTag = 0;
            fi.AllocationSize = e.is_directory ? 0 : (((e.size.value_or(0)) + 4095) & ~4095ULL);
            fi.FileSize = e.size.value_or(0);
            fi.CreationTime = posixToWindowsTime(e.mtime.value_or(0));
            fi.LastAccessTime = fi.CreationTime;
            fi.LastWriteTime = fi.CreationTime;
            fi.ChangeTime = fi.CreationTime;
            fi.IndexNumber = 0;
            fi.HardLinks = 0;
            fi.EaSize = 0;

            std::wstring wentry;
            for (char c : e.name) wentry += static_cast<wchar_t>(static_cast<unsigned char>(c));
            SIZE_T nameLen = (wentry.size() + 1) * sizeof(WCHAR);
            DirInfo->FileInfo = fi;
            DirInfo->NextOffset = 0;
            memcpy(DirInfo->FileNameBuf, wentry.c_str(), nameLen);
            DirInfo->Size = static_cast<UINT16>(FSP_FSCTL_DEFAULT_ALIGN_UP(
                sizeof(FSP_FSCTL_DIR_INFO) + nameLen));
            EMFS_LOG("  -> SUCCESS (found '" << name << "')");
            return STATUS_SUCCESS;
        }
    }

    EMFS_LOG("  -> FAIL: not found '" << name << "'");
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

}  // namespace
#endif

EtherMountFS::EtherMountFS() {
    wcscpy_s(mountPoint_, MOUNT_POINT);
}

EtherMountFS::~EtherMountFS() {
    unmount();
}

bool EtherMountFS::mount(const VpsCredentials& creds) {
#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
    EMFS_LOG("mount() called, drive=" << (creds.driveLetter.empty() ? "Z:" : (creds.driveLetter + ":")));
    if (mounted_) return false;

    credentials_ = creds;

    char dl = (creds.driveLetter.empty() ? 'Z' : static_cast<char>(toupper(creds.driveLetter[0])));
    if (dl < 'A' || dl > 'Z') dl = 'Z';
    mountPoint_[0] = static_cast<wchar_t>(dl);
    mountPoint_[1] = L':';
    mountPoint_[2] = L'\0';

    if (!NT_SUCCESS(FspLoad(0))) {
        EMFS_LOG("mount() FAIL: FspLoad failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        g_ctx = new MountContext();
        g_ctx->creds = creds;

        if (g_ctx->sftp.initialize() != Result::Success) {
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        if (g_ctx->sftp.connect(creds.host, creds.port, creds.username, creds.password) != Result::Success) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        if (g_ctx->sftp.initSftp() != Result::Success) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
            return false;
        }
        g_ctx->connected = true;
    }

    FSP_FSCTL_VOLUME_PARAMS volumeParams = {};
    volumeParams.SectorSize = 4096;
    volumeParams.SectorsPerAllocationUnit = 1;
    volumeParams.VolumeCreationTime = 0;
    volumeParams.VolumeSerialNumber = 0;
    volumeParams.FileInfoTimeout = 1000;
    volumeParams.CaseSensitiveSearch = 0;
    volumeParams.CasePreservedNames = 1;
    volumeParams.UnicodeOnDisk = 1;
    volumeParams.PersistentAcls = 1;
    volumeParams.MaxComponentLength = 256;
    volumeParams.ReadOnlyVolume = 1;
    volumeParams.Prefix[0] = L'\0';  // Disk mode: no UNC prefix
    wcscpy_s(volumeParams.FileSystemName, L"EtherMount");

    FSP_FILE_SYSTEM* fs = nullptr;
    wchar_t deviceName[] = L"" FSP_FSCTL_DISK_DEVICE_NAME;
    NTSTATUS status = FspFileSystemCreate(
        deviceName, &volumeParams, getInterface(), &fs);

    if (!NT_SUCCESS(status)) {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    fileSystem_ = fs;

    wchar_t mp[4] = {mountPoint_[0], mountPoint_[1], L'\0'};
    std::ostringstream oss;
    oss << "mount() SetMountPoint(" << static_cast<char>(mp[0]) << mp[1] << ")";
    EMFS_LOG(oss.str());
    status = FspFileSystemSetMountPoint(fs, mountPoint_);
    if (!NT_SUCCESS(status)) {
        EMFS_LOG("mount() FAIL: SetMountPoint failed");
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    EMFS_LOG("mount() StartDispatcher");
    status = FspFileSystemStartDispatcher(fs, 0);
    if (!NT_SUCCESS(status)) {
        EMFS_LOG("mount() FAIL: StartDispatcher failed");
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
        return false;
    }

    mounted_ = true;
    EMFS_LOG("mount() SUCCESS - drive ready (disk mode)");
    if (checkDokanBeforeWinFsp()) {
        EMFS_LOG("Note: Dokan appears before WinFSP in Network Provider order. "
            "If you experience drive access issues, try changing the order: "
            "https://www.interfacett.com/blogs/changing-the-network-provider-order-in-windows-10/");
    }
    return true;
#else
    (void)creds;
    return false;
#endif
}

void EtherMountFS::unmount() {
#if defined(_WIN32) && ETHERMOUNT_USE_WINFSP
    if (!mounted_) return;

    FSP_FILE_SYSTEM* fs = static_cast<FSP_FILE_SYSTEM*>(fileSystem_);
    if (fs) {
        FspFileSystemStopDispatcher(fs);
        FspFileSystemDelete(fs);
        fileSystem_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        if (g_ctx) {
            g_ctx->sftp.disconnect();
            delete g_ctx;
            g_ctx = nullptr;
        }
    }

    mounted_ = false;
#endif
}

}  // namespace EtherMount
